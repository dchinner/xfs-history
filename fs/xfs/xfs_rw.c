
#include <sys/types.h>
#ifdef SIM
#define _KERNEL 1
#endif
#include <sys/buf.h>
#include <sys/uio.h>
#include <sys/vfs.h>
#include <sys/vnode.h>
#include <sys/cred.h>
#include <sys/sysmacros.h>
#include <sys/pfdat.h>
#include <sys/proc.h>
#include <sys/user.h>
#include <sys/uuid.h>
#include <sys/grio.h>
#include <sys/pda.h>
#ifdef SIM
#undef _KERNEL
#endif
#include <sys/cmn_err.h>
#include <sys/debug.h>
#include <sys/errno.h>
#include <sys/fcntl.h>
#include <sys/var.h>
#ifdef SIM
#include <bstring.h>
#include <stdio.h>
#else
#include <sys/conf.h>
#include <sys/systm.h>
#endif
#include <sys/kmem.h>
#include <sys/sema.h>
#include <sys/uuid.h>
#include <sys/param.h>
#include <sys/file.h>
#include <sys/region.h>
#include <sys/runq.h>
#include <sys/schedctl.h>
#include "xfs_types.h"
#include "xfs_inum.h"
#include "xfs_log.h"
#include "xfs_trans.h"
#include "xfs_sb.h"
#include "xfs_ag.h"
#include "xfs_mount.h"
#include "xfs_alloc_btree.h"
#include "xfs_bmap_btree.h"
#include "xfs_ialloc_btree.h"
#include "xfs_btree.h"
#include "xfs_bmap.h"
#include "xfs_ialloc.h"
#include "xfs_dinode.h"
#include "xfs_inode_item.h"
#include "xfs_inode.h"
#include "xfs_error.h"

#ifdef SIM
#include "sim.h"
#endif

/*
 * This lock is used by xfs_strat_write().
 * The xfs_strat_lock is initialized in xfs_init().
 */
lock_t	xfs_strat_lock;

/*
 * Variables for coordination with the xfsd daemons.
 * The xfsd_lock and xfsd_wait variables are initialized
 * in xfs_init();
 */
static int	xfsd_count;
static buf_t	*xfsd_list;
static int	xfsd_bufcount;
lock_t		xfsd_lock;
sema_t		xfsd_wait;

STATIC void
xfs_zero_bp(
	buf_t	*bp,
	int	data_offset,
	int	data_len);

STATIC int
xfs_retrieved(
	int		available,
	off_t		offset,
	int		count,
	uint		*total_retrieved,
	xfs_fsize_t	isize);

#ifndef DEBUG

#define	xfs_strat_write_check(ip,off,count,imap,nimap)

#else /* DEBUG */

STATIC void
xfs_strat_write_check(
	xfs_inode_t	*ip,
	xfs_fileoff_t	offset_fsb,
	xfs_extlen_t	buf_fsb,
	xfs_bmbt_irec_t	*imap,
	int		imap_count);

#endif /* DEBUG */		      

STATIC int
xfsd(void);

int
xfs_diordwr(
	vnode_t	*vp,
	uio_t		*uiop,
	int		ioflag,
	cred_t		*credp,
	int		rw);

extern int
xfs_grio_req(
	xfs_inode_t *,
	struct reservation_id *, 
	uio_t *,
	int,
	cred_t *,
	int);

/*
 * Round the given file offset down to the nearest read/write
 * size boundary.
 */
#define	XFS_READIO_ALIGN(mp,off)	(((off) >> mp->m_readio_log) \
					        << mp->m_readio_log)
#define	XFS_WRITEIO_ALIGN(mp,off)	(((off) >> mp->m_writeio_log) \
					        << mp->m_writeio_log)

/*
 * Fill in the bmap structure to indicate how the next bp
 * should fit over the given extent.
 *
 * Everything here is in terms of file system blocks, not BBs.
 */
void
xfs_next_bmap(
	xfs_mount_t	*mp,
	xfs_bmbt_irec_t	*imapp,
	struct bmapval	*bmapp,
	int		iosize,
	int		last_iosize,
	xfs_fileoff_t	ioalign,
	xfs_fileoff_t	last_offset,
	xfs_fileoff_t	req_offset)
{
	int		extra_blocks;
	xfs_fileoff_t	size_diff;
	xfs_fileoff_t	ext_offset;
	xfs_fsblock_t	start_block;

	/*
	 * Make sure that the request offset lies in the extent given.
	 */
	ASSERT(req_offset >= imapp->br_startoff);
	ASSERT(req_offset < (imapp->br_startoff + imapp->br_blockcount));

	if (last_offset == -1) {
		ASSERT(ioalign != -1);
		if (ioalign < imapp->br_startoff) {
			/*
			 * The alignment we guessed at can't
			 * happen on this extent, so align
			 * to the beginning of this extent.
			 * Subtract whatever we drop from the
			 * iosize so that we stay aligned on
			 * our iosize boundaries.
			 */
			size_diff = imapp->br_startoff - ioalign;
			iosize -= size_diff;
			ASSERT(iosize > 0);
			ext_offset = 0;
			bmapp->offset = imapp->br_startoff;
			ASSERT(bmapp->offset <= req_offset);
		} else {
			/*
			 * The alignment requested fits on this
			 * extent, so use it.
			 */
			ext_offset = ioalign - imapp->br_startoff;
			bmapp->offset = ioalign;
			ASSERT(bmapp->offset <= req_offset);
		}
	} else {
		/*
		 * This is one of a series of sequential access to the
		 * file.  Make sure to line up the buffer we specify
		 * so that it doesn't overlap the last one.  It should
		 * either be the same as the last one (if we need data
		 * from it) or follow immediately after the last one.
		 */
		ASSERT(ioalign == -1);
		if (last_offset >= imapp->br_startoff) {
			/*
			 * The last I/O was from the same extent
			 * that this one will at least start on.
			 * This assumes that we're going sequentially.
			 */
			if (req_offset < (last_offset + last_iosize)) {
				/*
				 * This request overlaps the buffer
				 * we used for the last request.  Just
				 * get that buffer again.
				 */
				ext_offset = last_offset -
					     imapp->br_startoff;
				bmapp->offset = last_offset;
				iosize = last_iosize;
			} else {
				/*
				 * This request does not overlap the buffer
				 * used for the last one.  Get it its own.
				 */
				ext_offset = req_offset - imapp->br_startoff;
				bmapp->offset = req_offset;
			}
		} else {
			/*
			 * The last I/O was on a different extent than
			 * this one.  We start at the beginning of this one.
			 * This assumes that we're going sequentially.
			 */
			ext_offset = 0;
			bmapp->offset = req_offset;
			ASSERT(bmapp->offset == imapp->br_startoff);
		}

	}
	start_block = imapp->br_startblock;
	if (start_block == HOLESTARTBLOCK) {
		bmapp->bn = -1;
		bmapp->eof = BMAP_HOLE;
	} else if (start_block == DELAYSTARTBLOCK) {
		bmapp->bn = -1;
		bmapp->eof = BMAP_DELAY;
	} else {
		bmapp->bn = start_block + ext_offset;
		bmapp->eof = 0;
	}
	bmapp->length = iosize;
	
	/*
	 * If the iosize from our offset extends beyond the
	 * end of the extent, then trim down the length
	 * to match that of the extent.
	 */
	 extra_blocks = (bmapp->offset + bmapp->length) -
	                (imapp->br_startoff + imapp->br_blockcount);   
	 if (extra_blocks > 0) {
	    	bmapp->length -= extra_blocks;
		ASSERT(bmapp->length > 0);
	}
	bmapp->bsize = XFS_FSB_TO_B(mp, bmapp->length);
}

/*
 * xfs_retrieved() is a utility function used to calculate the
 * value of bmap.pbsize.
 *
 * Available is the number of bytes mapped by the current bmap.
 * Offset is the file offset of the current request by the user.
 * Count is the size of the current request by the user.
 * Total_retrieved is a running total of the number of bytes
 *  which have been setup for the user in this call so far.
 * Isize is the current size of the file being read.
 */
STATIC int
xfs_retrieved(
	int		available,
	off_t		offset,
	int		count,
	uint		*total_retrieved,
	xfs_fsize_t	isize)
{
	int		retrieved;
	xfs_fsize_t	file_bytes_left;
	

	if ((available + *total_retrieved) > count) {
		/*
		 * This buffer will return more bytes
		 * than we asked for.  Trim retrieved
		 * so we can set bmapp->pbsize correctly.
		 */
		retrieved = count - *total_retrieved;
	} else {
		retrieved = available;
	}

	file_bytes_left = isize - (offset + *total_retrieved);
	if (file_bytes_left < retrieved) {
		/*
		 * The user has requested more bytes
		 * than there are in the file.  Trim
		 * down the number to those left in
		 * the file.
		 */
		retrieved = file_bytes_left;
	}

	*total_retrieved += retrieved;
	return retrieved;
}

/*
 * xfs_iomap_extra()
 *
 * This is called to fill in the bmapval for a page which overlaps
 * the end of the file and the fs block size is less than the page
 * size.  We fill in the bmapval with zero sizes and offsets to
 * indicate that there is nothing here to read since we're beyond
 * the end of the file.
 */
STATIC void
xfs_iomap_extra(
	xfs_inode_t	*ip,
	off_t		offset,
	size_t		count,
	struct bmapval	*bmapp,
	int		*nbmaps)
{
	xfs_fileoff_t	offset_fsb;
	xfs_fsize_t	nisize;
	xfs_mount_t	*mp;

	nisize = ip->i_new_size;
	if (nisize < ip->i_d.di_size) {
		nisize = ip->i_d.di_size;
	}
	ASSERT((offset == BBTOB(BTOBB(nisize))) && (count < NBPP));
	ASSERT(ip->i_mount->m_sb.sb_blocksize < NBPP);

	mp = ip->i_mount;
	offset_fsb = XFS_B_TO_FSBT(mp, offset);

	*nbmaps = 1;
	bmapp->eof = BMAP_EOF;
	bmapp->bn = -1;
	bmapp->offset = XFS_FSB_TO_BB(mp, offset_fsb);
	bmapp->length = 0;
	bmapp->bsize = 0;
	bmapp->pboff = 0;
	bmapp->pbsize = 0;
	if (ip->i_d.di_flags & XFS_DIFLAG_REALTIME) {
		bmapp->pbdev = mp->m_rtdev;
	} else {
		bmapp->pbdev = mp->m_dev;
	}
}

void
xfs_iomap_read(
	xfs_inode_t	*ip,
	off_t		offset,
	size_t		count,
	struct bmapval	*bmapp,
	int		*nbmaps)
{
	xfs_fileoff_t	offset_fsb;
	xfs_fileoff_t	ioalign;
	xfs_fileoff_t	last_offset;
	xfs_fileoff_t	last_required_offset;
	xfs_fileoff_t	last_fsb;
	xfs_fileoff_t	next_offset;
	xfs_fsize_t	nisize;
	off_t		offset_page;
	int		nimaps;
	unsigned int	iosize;
	unsigned int	last_iosize;
	unsigned int	retrieved_bytes;
	unsigned int	total_retrieved_bytes;
	short		filled_bmaps;
	short		read_aheads;
	int		x;
	xfs_mount_t	*mp;
	struct bmapval	*curr_bmapp;
	struct bmapval	*next_bmapp;
	struct bmapval	*last_bmapp;
	struct bmapval	*first_read_ahead_bmapp;
	struct bmapval	*next_read_ahead_bmapp;
	xfs_bmbt_irec_t	*curr_imapp;
	xfs_bmbt_irec_t	*last_imapp;
#define	XFS_READ_IMAPS	XFS_BMAP_MAX_NMAP
	xfs_bmbt_irec_t	imap[XFS_READ_IMAPS];

	mp = ip->i_mount;
	nisize = ip->i_new_size;
	if (nisize < ip->i_d.di_size) {
		nisize = ip->i_d.di_size;
	}
	offset_fsb = XFS_B_TO_FSBT(mp, offset);
	nimaps = XFS_READ_IMAPS;
	last_fsb = XFS_B_TO_FSB(mp, nisize);
	if (last_fsb <= offset_fsb) {
		/*
		 * The VM/chunk code is trying to map a page to be
		 * pushed out which contains the last file block
		 * and byte.  Since the file ended, we did not map
		 * the entire page, and now it is calling back to
		 * map the rest of it.  Handle it here so that it
		 * does not interfere with the normal path code.
		 * This can only happen if the fs block size is
		 * less than the page size.
		 */
		xfs_iomap_extra(ip, offset, count, bmapp, nbmaps);
		return;
	}
	(void)xfs_bmapi(NULL, ip, offset_fsb,
			(xfs_extlen_t)(last_fsb - offset_fsb),
			XFS_BMAPI_ENTIRE, NULLFSBLOCK, 0, imap,
			&nimaps, NULL);


	if ((offset == ip->i_next_offset) &&
	    (count <= ip->i_last_req_sz)) {
		/*
		 * Sequential I/O of same size as last time.
	 	 */
		ASSERT(ip->i_io_size > 0);
		iosize = ip->i_io_size;
		last_offset = ip->i_io_offset;
		ioalign = -1;
	} else {
		/*
		 * The I/O size for the file has not yet been
		 * determined, so figure it out.
		 */
		if (XFS_B_TO_FSB(mp, count) <= mp->m_readio_blocks) {
			/*
			 * The request is smaller than our
			 * minimum I/O size, so default to
			 * the minimum.  For these size requests
			 * we always want to align the requests
			 * to XFS_READ_SIZE boundaries as well.
			 */
			iosize = mp->m_readio_blocks;
			ioalign = XFS_READIO_ALIGN(mp, offset);
			ioalign = XFS_B_TO_FSBT(mp, ioalign);
		} else {
			/*
			 * The request is bigger than our
			 * minimum I/O size and it's the
			 * first one in this sequence, so
			 * set the I/O size for the file
			 * now.
			 *
			 * In calculating the offset rounded down
			 * to a page, make sure to round down the
			 * fs block offset rather than the byte
			 * offset for the case where our block size
			 * is greater than the page size.  This way
			 * offset_page will always align to a fs block
			 * as well as a page.
			 *
			 * For the end of the I/O we need to round
			 * offset + count up to a page boundary and
			 * then round that up to a file system block
			 * boundary.
			 */
			offset_page = ctob(btoct(XFS_FSB_TO_B(mp, offset_fsb)));
			last_fsb = XFS_B_TO_FSB(mp,
						ctob(btoc(offset + count)));
			iosize = last_fsb - XFS_B_TO_FSBT(mp, offset_page);
			ioalign = XFS_B_TO_FSB(mp, offset_page);
		}
		last_offset = -1;
	}

	/*
	 * Now we've got the I/O size and the last offset,
	 * so start figuring out how to align our
	 * buffers.
	 */
	xfs_next_bmap(mp, imap, bmapp, iosize, iosize, ioalign,
		      last_offset, offset_fsb);
	ASSERT((bmapp->length > 0) &&
	       (offset >= XFS_FSB_TO_B(mp, bmapp->offset)));
	
	if ((nimaps == 1) &&
	    (XFS_FSB_TO_B(mp, bmapp->offset + bmapp->length) >= nisize)) {
		bmapp->eof |= BMAP_EOF;
	}

	bmapp->pboff = offset - XFS_FSB_TO_B(mp, bmapp->offset);
	retrieved_bytes = bmapp->bsize - bmapp->pboff;
	total_retrieved_bytes = 0;
	bmapp->pbsize = xfs_retrieved(retrieved_bytes, offset, count,
				      &total_retrieved_bytes, nisize);

	/*
	 * Only map additional buffers if they've been asked for
	 * and the I/O being done is sequential and has reached the
	 * point where we need to initiate more read ahead or we didn't get
	 * the whole request in the first bmap.
	 */
	filled_bmaps = 1;
	last_required_offset = bmapp[0].offset;
	first_read_ahead_bmapp = NULL;
	if ((*nbmaps > 1) &&
	    (((offset == ip->i_next_offset) &&
	      (offset_fsb >= ip->i_reada_blkno)) ||
	     (retrieved_bytes < count))) {
		curr_bmapp = &bmapp[0];
		next_bmapp = &bmapp[1];
		last_bmapp = &bmapp[*nbmaps - 1];
		curr_imapp = &imap[0];
		last_imapp = &imap[nimaps - 1];

		/*
		 * curr_bmap is always the last one we filled
		 * in, and next_bmapp is always the next one
		 * to be filled in.
		 */
		while (next_bmapp <= last_bmapp) {
			next_offset = curr_bmapp->offset +
				      curr_bmapp->length;
			last_iosize = curr_bmapp->length;
			if (next_offset <
			    (curr_imapp->br_startoff +
			     curr_imapp->br_blockcount)) {
				xfs_next_bmap(mp, curr_imapp,
					 next_bmapp, iosize, last_iosize, -1,
					 curr_bmapp->offset, next_offset);
			} else {
				curr_imapp++;
				if (curr_imapp <= last_imapp) {
					xfs_next_bmap(mp,
					    curr_imapp, next_bmapp,
					    iosize, last_iosize, -1,
					    curr_bmapp->offset, next_offset);
				} else {
					/*
					 * We're out of imaps.  We
					 * either hit the end of
					 * file or just didn't give
					 * enough of them to bmapi.
					 * The caller will just come
					 * back if we haven't done
					 * enough yet.
					 */
					break;
				}
			}
			
			filled_bmaps++;
			curr_bmapp = next_bmapp;
			next_bmapp++;
		       
			/*
			 * Make sure to fill in the pboff and pbsize
			 * fields as long as the bmaps are required for
			 * the request (as opposed to strictly read-ahead).
			 */
			if (total_retrieved_bytes < count) {
				curr_bmapp->pboff = 0;
				curr_bmapp->pbsize =
					xfs_retrieved(curr_bmapp->bsize,
						      offset, count,
						      &total_retrieved_bytes,
						      nisize);
			}
			
			if ((curr_imapp == last_imapp) &&
			    (XFS_FSB_TO_B(mp, curr_bmapp->offset +
					  curr_bmapp->length) >= nisize)) {
				curr_bmapp->eof |= BMAP_EOF;
			}

 			/*
			 * Keep track of the offset of the last buffer
			 * needed to satisfy the I/O request.  This will
			 * be used for i_io_offset later.  Also record
			 * the first bmapp used to track a read ahead.
			 */
			if (XFS_FSB_TO_B(mp, curr_bmapp->offset) <
			    (offset + count)) {
				last_required_offset = curr_bmapp->offset;
			} else if (first_read_ahead_bmapp == NULL) {
				first_read_ahead_bmapp = curr_bmapp;
			}

		}

		/*
		 * If we're describing any read-ahead here, then move
		 * the read-ahead blkno up to the point where we've
		 * gone through half the read-ahead described here.
		 * This way we don't issue more read-ahead until we
		 * are half-way through the last read-ahead.
		 * 
		 * If we're not describing any read-ahead because the
		 * request is just fragmented, then move up the
		 * read-ahead blkno to just past what we're returning
		 * so that we can maybe start it on the next request.
		 */
		if (first_read_ahead_bmapp != NULL) {
			read_aheads = curr_bmapp - first_read_ahead_bmapp +1;
			next_read_ahead_bmapp = first_read_ahead_bmapp +
						(read_aheads / 2);
			ip->i_reada_blkno = next_read_ahead_bmapp->offset;
		} else {
			ip->i_reada_blkno = curr_bmapp->offset +
					    curr_bmapp->length;
		}
	} else if ((*nbmaps > 1) && (offset != ip->i_io_offset)) {
		/*
		 * In this case the caller is not yet accessing the
		 * file sequentially, but set the read-ahead blkno
		 * so that we can tell if they start doing so.
		 */
		ip->i_reada_blkno = bmapp[0].offset + bmapp[0].length;
	}

	ip->i_io_size = iosize;
	ip->i_io_offset = last_required_offset;
	if (count > ip->i_last_req_sz) {
		/*
		 * Record the "last request size" for the file.
		 * We don't let it shrink so that big requests
		 * that are not satisfied in one call here still
		 * record the full request size (not the smaller
		 * one that comes in to finish mapping the request).
		 */
		ip->i_last_req_sz = count;
	}
	if (total_retrieved_bytes >= count) {
		/*
		 * We've mapped all of the caller's request, so
		 * the next request in a sequential read will
		 * come in the block this one ended on or the
		 * next if we consumed up to the very end of
		 * a block.
		 */
		ip->i_next_offset = offset + count;
	} else {
		/*
		 * We didn't satisfy the entire request, so we
		 * can expect xfs_read_file() to come back with
		 * what is left of the request.
		 */
		ip->i_next_offset = offset + total_retrieved_bytes;
	}
	*nbmaps = filled_bmaps;
	for (x = 0; x < filled_bmaps; x++) {
		curr_bmapp = &bmapp[x];
		if (ip->i_d.di_flags & XFS_DIFLAG_REALTIME) {
			curr_bmapp->pbdev = mp->m_rtdev;
		} else {
			curr_bmapp->pbdev = mp->m_dev;
		}
		curr_bmapp->offset = XFS_FSB_TO_BB(mp, curr_bmapp->offset);
		curr_bmapp->length = XFS_FSB_TO_BB(mp, curr_bmapp->length);
		ASSERT((x == 0) ||
		       ((bmapp[x - 1].offset + bmapp[x - 1].length) ==
			curr_bmapp->offset));
		if (curr_bmapp->bn != -1) {
			curr_bmapp->bn =
				XFS_FSB_TO_DADDR(mp, curr_bmapp->bn);
		}
	}
	return;
}				
			
		
int
xfs_read_file(
	vnode_t	*vp,
	uio_t	*uiop,
	int	ioflag,
	cred_t	*credp)
{
#define	XFS_READ_BMAPS	4
	struct bmapval	bmaps[XFS_READ_BMAPS];
	struct bmapval	*bmapp;
	int		nbmaps;
	buf_t		*bp;
	int		read_bmaps;
	int		buffer_bytes_ok;
	xfs_inode_t	*ip;
	int		error;
	uint		lock_mode;

	ip = XFS_VTOI(vp);
	error = 0;
	buffer_bytes_ok = 0;
	/*
	 * Loop until uio->uio_resid, which is the number of bytes the
	 * caller has requested, goes to 0 or we get an error.  Each
	 * call to xfs_iomap_read tries to map as much of the request
	 * plus read-ahead as it can.
	 */
	do {
		lock_mode = xfs_ilock_map_shared(ip);

		/*
		 * We've fallen off the end of the file, so
		 * just return with what we've done so far.
		 */
		if (uiop->uio_offset >= ip->i_d.di_size) {
			xfs_iunlock_map_shared(ip, lock_mode);
			break;
		}
 
		nbmaps = XFS_READ_BMAPS;
		xfs_iomap_read(ip, uiop->uio_offset, uiop->uio_resid,
			       bmaps, &nbmaps);

		xfs_iunlock_map_shared(ip, lock_mode);

		if (error || (bmaps[0].pbsize == 0)) {
			break;
		}

		bmapp = &bmaps[0];
		read_bmaps = nbmaps;
		/*
		 * The first time through this loop we kick off I/O on
		 * all the bmaps described by the iomap_read call.
		 * Subsequent passes just wait for each buffer needed
		 * to satisfy this request to complete.  Buffers which
		 * are started in the first pass but are actually just
		 * read ahead buffers are never waited for, since uio_resid
		 * will go to 0 before we get to them.
		 *
		 * This works OK because iomap_read always tries to
		 * describe all the buffers we need to satisfy this
		 * read call plus the necessary read-ahead in the
		 * first call to it.
		 */
		while ((uiop->uio_resid != 0) && (nbmaps > 0)) {

			bp = chunkread(vp, bmapp, read_bmaps, credp);

			if (bp->b_flags & B_ERROR) {
				error = geterror(bp);
				ASSERT(error != EINVAL);
				break;
			} else if (bp->b_resid != 0) {
				buffer_bytes_ok = 0;
				break;
			} else {
				buffer_bytes_ok = 1;
				ASSERT((BBTOB(bmapp->offset) + bmapp->pboff)
				       == uiop->uio_offset);
				error = biomove(bp, bmapp->pboff,
						bmapp->pbsize, UIO_READ,
						uiop);
				if (error) {
					break;
				}
			}

			brelse(bp);

			read_bmaps = 1;
			nbmaps--;
			bmapp++;
		}

	} while (!error && (uiop->uio_resid != 0) && buffer_bytes_ok);

	return error;
}





/*
 * xfs_read
 *
 * This is the xFS VOP_READ entry point.  It does some minimal
 * error checking and then switches out based on the file type.
 */
int
xfs_read(
	vnode_t	*vp,
	uio_t		*uiop,
	int		ioflag,
	cred_t		*credp)
{
	struct reservation_id id;
	xfs_inode_t	*ip;
	int		type;
	off_t		offset;
	size_t		count;
	int		error;


	ip = XFS_VTOI(vp);
	ASSERT(ismrlocked(&ip->i_iolock, MR_ACCESS | MR_UPDATE) != 0);

	type = ip->i_d.di_mode & IFMT;
	ASSERT(type == IFREG || type == IFDIR ||
	       type == IFLNK || type == IFSOCK);

	offset = uiop->uio_offset;
	count = uiop->uio_resid;

#ifndef SIM
	if (MANDLOCK(vp, ip->i_d.di_mode) &&
	    (error = chklock(vp, FREAD, offset, count, uiop->uio_fmode))) {
		return error;
	}
#endif

	if (offset < 0)
		return XFS_ERROR(EINVAL);
	if (count <= 0)
		return 0;

	switch (type) {
	case IFREG:
		/*
		 * Not ready for in-line files yet.
		 */
		ASSERT((ip->i_d.di_format == XFS_DINODE_FMT_EXTENTS) ||
		       (ip->i_d.di_format == XFS_DINODE_FMT_BTREE));

#ifdef SIM
		/*
		 * Need pid of client not of simulator process.
		 * This may not always be correct.
		 */
		id.pid = MAKE_REQ_PID(u.u_procp->p_pid - 1, 0);
#else
		id.pid = MAKE_REQ_PID(u.u_procp->p_pid, 0);
#endif
		id.ino = ip->i_ino;
		error  = xfs_grio_req(ip, &id, uiop, ioflag, credp, UIO_READ);

		break;

	case IFDIR:
	case IFLNK:
		error = XFS_ERROR(EINVAL);
		break;
	      
	case IFSOCK:
		error = XFS_ERROR(ENODEV);
		break;

	default:
		ASSERT(0);
		error = XFS_ERROR(EINVAL);
	}
	return error;
}


/*
 * Map the given I/O size and I/O alignment over the given extent.
 * If we're at the end of the file and the underlying extent is
 * delayed alloc, make sure we extend out to the
 * next m_writeio_blocks boundary.  Otherwise make sure that we
 * are confined to the given extent.
 */
STATIC void
xfs_write_bmap(
	xfs_mount_t	*mp,
	xfs_bmbt_irec_t	*imapp,
	struct bmapval	*bmapp,
	int		iosize,
	xfs_fileoff_t	ioalign,
	xfs_fsize_t	isize)
{
	int		extra_blocks;
	xfs_fileoff_t	size_diff;
	xfs_fileoff_t	ext_offset;
	xfs_fsblock_t	start_block;
	off_t		last_imap_byte;
	
	if (ioalign < imapp->br_startoff) {
		/*
		 * The desired alignment doesn't end up on this
		 * extent.  Move up to the beginning of the extent.
		 * Subtract whatever we drop from the iosize so that
		 * we stay aligned on iosize boundaries.
		 */
		size_diff = imapp->br_startoff - ioalign;
		iosize -= (int)size_diff;
		ASSERT(iosize > 0);
		ext_offset = 0;
		bmapp->offset = imapp->br_startoff;
	} else {
		/*
		 * The alignment requested fits on this extent,
		 * so use it.
		 */
		ext_offset = ioalign - imapp->br_startoff;
		bmapp->offset = ioalign;
	}
	start_block = imapp->br_startblock;
	ASSERT(start_block != HOLESTARTBLOCK);
	if (start_block != DELAYSTARTBLOCK) {
		bmapp->bn = start_block + ext_offset;
		bmapp->eof = 0;
	} else {
		bmapp->bn = -1;
		bmapp->eof = BMAP_DELAY;
	}
	bmapp->length = iosize;

	/*
	 * If the iosize from our offset extends beyond the end of
	 * the extent, then trim down length to match that of the extent.
	 */
	extra_blocks = (int)((bmapp->offset + bmapp->length) -
		       (imapp->br_startoff + imapp->br_blockcount));
	last_imap_byte = XFS_FSB_TO_B(mp, imapp->br_startoff +
				      imapp->br_blockcount);
	if (extra_blocks > 0) {
		bmapp->length -= extra_blocks;
		ASSERT(bmapp->length > 0);
	}
	bmapp->bsize = XFS_FSB_TO_B(mp, bmapp->length);
}


/*
 * This routine is called to handle zeroing any space in the last
 * block of the file that is beyond the EOF.  We do this since the
 * size is being increased without writing anything to that block
 * and we don't want anyone to read the garbage on the disk.
 */
void
xfs_zero_eof(
	xfs_inode_t	*ip,
	off_t		offset,
	xfs_fsize_t	isize,
	cred_t		*credp)
{
	xfs_fileoff_t	last_fsb;
	xfs_mount_t	*mp;
	buf_t		*bp;
	int		iosize;
	vnode_t		*vp;
	int		nimaps;
	int		zero_offset;
	int		zero_len;
	int		isize_fsb_offset;
	xfs_bmbt_irec_t	imap;
	struct bmapval	bmap;

	ASSERT(ismrlocked(&ip->i_lock, MR_UPDATE) != 0);
	ASSERT(offset > isize);

	mp = ip->i_mount;
	vp = XFS_ITOV(ip);
	isize_fsb_offset = XFS_B_FSB_OFFSET(mp, isize);

	if (isize_fsb_offset == 0) {
		/*
		 * There are not extra bytes in the last block to
		 * zero, so return.
		 */
		return;
	}

	last_fsb = XFS_B_TO_FSBT(mp, isize);
	nimaps = 1;
	(void) xfs_bmapi(NULL, ip, last_fsb, 1, 0, NULLFSBLOCK, 0, &imap,
			 &nimaps, NULL);
	ASSERT(nimaps > 0);
	/*
	 * If the block underlying isize is just a hole, then there
	 * is nothing to zero.
	 */
	if (imap.br_startblock == HOLESTARTBLOCK) {
		return;
	}
	/*
	 * Get a buffer for the last block, zero the part beyond the
	 * EOF, and write it out async.  We need to drop the ilock
	 * while we do this so we don't deadlock when the buffer cache
	 * calls back to us.
	 */
	xfs_iunlock(ip, XFS_ILOCK_EXCL);
	bmap.offset = XFS_FSB_TO_BB(mp, last_fsb);
	bmap.length = XFS_FSB_TO_BB(mp, 1);
	bmap.bsize = BBTOB(bmap.length);
	bmap.pboff = 0;
	bmap.pbsize = bmap.bsize;
	if (ip->i_d.di_flags & XFS_DIFLAG_REALTIME) {
		bmap.pbdev = mp->m_rtdev;
	} else {
		bmap.pbdev = mp->m_dev;
	}
	bmap.eof = BMAP_EOF;
	if (imap.br_startblock != DELAYSTARTBLOCK) {
		bmap.bn = XFS_FSB_TO_DADDR(mp, imap.br_startblock);
	} else {
		bmap.bn = -1;
		bmap.eof |= BMAP_DELAY;
	}
	bp = chunkread(XFS_ITOV(ip), &bmap, 1, credp);
	zero_offset = isize_fsb_offset;
	zero_len = mp->m_sb.sb_blocksize - isize_fsb_offset;
	xfs_zero_bp(bp, zero_offset, zero_len);
	bawrite(bp);
	xfs_ilock(ip, XFS_ILOCK_EXCL);
	return;
}

STATIC void
xfs_iomap_write(
	xfs_inode_t	*ip,
	off_t		offset,
	size_t		count,
	struct bmapval	*bmapp,
	int		*nbmaps)
{
	xfs_fileoff_t	offset_fsb;
	xfs_fileoff_t	ioalign;
	xfs_fileoff_t	next_offset_fsb;
	xfs_fileoff_t	last_fsb;
	xfs_fileoff_t	bmap_end_fsb;
	off_t		next_offset;
	xfs_fsize_t	isize;
	int		nimaps;
	unsigned int	iosize;
	unsigned int	writing_bytes;
	short		filled_bmaps;
	short		x;
	size_t		count_remaining;
	xfs_mount_t	*mp;
	struct bmapval	*curr_bmapp;
	struct bmapval	*next_bmapp;
	struct bmapval	*last_bmapp;
	xfs_bmbt_irec_t	*curr_imapp;
	xfs_bmbt_irec_t	*last_imapp;
#define	XFS_WRITE_IMAPS	XFS_BMAP_MAX_NMAP
	xfs_bmbt_irec_t	imap[XFS_WRITE_IMAPS];

	ASSERT(ismrlocked(&ip->i_lock, MR_UPDATE) != 0);

	mp = ip->i_mount;
	isize = ip->i_d.di_size;
	offset_fsb = XFS_B_TO_FSBT(mp, offset);
	nimaps = XFS_WRITE_IMAPS;
	last_fsb = XFS_B_TO_FSB(mp, offset + count);
	(void) xfs_bmapi(NULL, ip, offset_fsb,
			 (xfs_extlen_t)(last_fsb - offset_fsb),
			 XFS_BMAPI_DELAY | XFS_BMAPI_WRITE |
			 XFS_BMAPI_ENTIRE, NULLFSBLOCK, 1, imap,
			 &nimaps, NULL);
	
	iosize = mp->m_writeio_blocks;
	ioalign = XFS_WRITEIO_ALIGN(mp, offset);
	ioalign = XFS_B_TO_FSBT(mp, ioalign);

	/*
	 * Now map our desired I/O size and alignment over the
	 * extents returned by xfs_bmapi().
	 */
	xfs_write_bmap(mp, imap, bmapp, iosize, ioalign, isize);
	ASSERT((bmapp->length > 0) &&
	       (offset >= XFS_FSB_TO_B(mp, bmapp->offset)));

	/*
	 * A bmap is the EOF bmap when it reaches to or beyond the current
	 * inode size AND it encompasses the last block allocated to the
	 * file.  Beyond the inode size is not good enough, because we
	 * could be writing more in this very request beyond what we
	 * are willing to describe in a single bmap.
	 */
	bmap_end_fsb = bmapp->offset + bmapp->length;
	if ((nimaps == 1) &&
	    (bmap_end_fsb >= imap[0].br_startoff + imap[0].br_blockcount) &&
	    (XFS_FSB_TO_B(mp, bmap_end_fsb) >= isize)) {
		bmapp->eof |= BMAP_EOF;
	}
	bmapp->pboff = offset - XFS_FSB_TO_B(mp, bmapp->offset);
	writing_bytes = bmapp->bsize - bmapp->pboff;
	if (writing_bytes > count) {
		/*
		 * The mapping is for more bytes than we're actually
		 * going to write, so trim writing_bytes so we can
		 * get bmapp->pbsize right.
		 */
		writing_bytes = count;
	}
	bmapp->pbsize = writing_bytes;

	/*
	 * Map more buffers if the first does not map the entire
	 * request.  We do this until we run out of bmaps, imaps,
	 * or bytes to write.
	 */
	filled_bmaps = 1;
	if ((*nbmaps > 1) &&
	    ((nimaps > 1) || (bmapp->offset + bmapp->length <
	     imap[0].br_startoff + imap[0].br_blockcount)) &&
	    (writing_bytes < count)) {
		curr_bmapp = &bmapp[0];
		next_bmapp = &bmapp[1];
		last_bmapp = &bmapp[*nbmaps - 1];
		curr_imapp = &imap[0];
		last_imapp = &imap[nimaps - 1];
		count_remaining = count - writing_bytes;

		/*
		 * curr_bmapp is always the last one we filled
		 * in, and next_bmapp is always the next one to
		 * be filled in.
		 */
		while (next_bmapp <= last_bmapp) {
			next_offset_fsb = curr_bmapp->offset +
					  curr_bmapp->length;
			if (next_offset_fsb <
			    (curr_imapp->br_startoff +
			     curr_imapp->br_blockcount)) {
				/*
				 * I'm still on the same extent, so
				 * the last bmap must have ended on
				 * a writeio_blocks boundary.  Thus,
				 * we just start where the last one
				 * left off.
				 */
				ASSERT((XFS_FSB_TO_B(mp, next_offset_fsb) &
					((1 << mp->m_writeio_log) - 1))==0);
				xfs_write_bmap(mp, curr_imapp, next_bmapp,
					       iosize, next_offset_fsb,
					       isize);
			} else {
				curr_imapp++;
				if (curr_imapp <= last_imapp) {
					/*
					 * We're moving on to the next
					 * extent.  Since we try to end
					 * all buffers on writeio_blocks
					 * boundaries, round next_offset
					 * down to a writeio_blocks boundary
					 * before calling xfs_write_bmap().
					 *
					 * XXXajs
					 * Adding a macro to writeio align
					 * fsblocks would be good to reduce
					 * the bit shifting here.
					 */
					ioalign = XFS_FSB_TO_B(mp,
					                    next_offset_fsb);
					ioalign = XFS_WRITEIO_ALIGN(mp,
								    ioalign);
					ioalign = XFS_B_TO_FSBT(mp, ioalign);
					xfs_write_bmap(mp, curr_imapp,
						       next_bmapp, iosize,
						       ioalign, isize);
				} else {
					/*
					 * We're out of imaps.  The caller
					 * will have to call again to map
					 * the rest of the write request.
					 */
					break;
				}
			}
			/*
			 * The write must start at offset 0 in this bmap
			 * since we're just continuing from the last
			 * buffer.  Thus the request offset in the buffer
			 * indicated by pboff must be 0.
			 */
			next_bmapp->pboff = 0;

			/*
			 * The request size within this buffer is the
			 * entire buffer unless the count of bytes to
			 * write runs out.
			 */
			next_offset = XFS_FSB_TO_B(mp, next_offset_fsb);
			writing_bytes = next_bmapp->bsize;
			if (writing_bytes > count_remaining) {
				writing_bytes = count_remaining;
			}
			next_bmapp->pbsize = writing_bytes;
			count_remaining -= writing_bytes;
			ASSERT(count_remaining >= 0);

			filled_bmaps++;
			curr_bmapp++;
			next_bmapp++;
			/*
			 * A bmap is the EOF bmap when it reaches to
			 * or beyond the current inode size AND it
			 * encompasses the last block allocated to the
			 * file.  Beyond the inode size is not good
			 * enough, because we could be writing more
			 * in this very request beyond what we
			 * are willing to describe in a single bmap.
			 */
			bmap_end_fsb = curr_bmapp->offset +
				       curr_bmapp->length;
			if ((curr_imapp == last_imapp) &&
			    (bmap_end_fsb >= curr_imapp->br_startoff +
			     curr_imapp->br_blockcount) &&
			    (XFS_FSB_TO_B(mp, bmap_end_fsb) >= isize)) {
				curr_bmapp->eof |= BMAP_EOF;
			}					
		}
	}
	*nbmaps = filled_bmaps;
	for (x = 0; x < filled_bmaps; x++) {
		curr_bmapp = &bmapp[x];
		if (ip->i_d.di_flags & XFS_DIFLAG_REALTIME) {
			curr_bmapp->pbdev = mp->m_rtdev;
		} else {
			curr_bmapp->pbdev = mp->m_dev;
		}
		curr_bmapp->offset = XFS_FSB_TO_BB(mp, curr_bmapp->offset);
		curr_bmapp->length = XFS_FSB_TO_BB(mp, curr_bmapp->length);
		ASSERT((x == 0) ||
		       ((bmapp[x - 1].offset + bmapp[x - 1].length) ==
			curr_bmapp->offset));
		if (curr_bmapp->bn != -1) {
			curr_bmapp->bn =
				XFS_FSB_TO_DADDR(mp, curr_bmapp->bn);
		}
	}
}


int
xfs_write_file(
	vnode_t	*vp,
	uio_t	*uiop,
	int	ioflag,
	cred_t	*credp)
{
#define	XFS_WRITE_BMAPS	4
	struct bmapval	bmaps[XFS_WRITE_BMAPS];
	struct bmapval	*bmapp;
	int		nbmaps;
	buf_t		*bp;
	int		buffer_bytes_ok;
	xfs_inode_t	*ip;
	int		error;
	int		eof_zeroed;
	xfs_fsize_t	isize;

	ip = XFS_VTOI(vp);
	error = 0;
	buffer_bytes_ok = 0;
	eof_zeroed = 0;
	/*
	 * i_new_size is used by xfs_iomap_read() when the chunk
	 * cache code calls back into the file system through
	 * xfs_bmap().  This way we can tell where the end of
	 * file is going to be even though we haven't yet updated
	 * ip->i_d.di_size.  This is guarded by the iolock which
	 * is held exclusively through here.
	 */
	ip->i_new_size = uiop->uio_offset + uiop->uio_resid;

	/*
	 * Loop until uiop->uio_resid, which is the number of bytes the
	 * caller has requested to write, goes to 0 or we get an error.
	 * Each call to xfs_iomap_write() tries to map as much of the
	 * request as it can in mp->m_writeio_blks sized chunks.
	 */
	do {
		xfs_ilock(ip, XFS_ILOCK_EXCL);
		isize = ip->i_d.di_size;
		if (ip->i_new_size < isize) {
			ip->i_new_size = isize;
		}

		/*
		 * If we've seeked passed the EOF to do this write,
		 * then we need to make sure that any buffer overlapping
		 * the EOF is zeroed beyond the EOF.
		 */
		if (!eof_zeroed &&
		    (uiop->uio_offset > isize) &&
		    (isize != 0)) {
			xfs_zero_eof(ip, uiop->uio_offset, isize, credp);
			eof_zeroed = 1;
		}

		nbmaps = XFS_WRITE_BMAPS;
		xfs_iomap_write(ip, uiop->uio_offset, uiop->uio_resid,
				bmaps, &nbmaps);

		xfs_iunlock(ip, XFS_ILOCK_EXCL);

		if (error || (bmaps[0].pbsize == 0)) {
			break;
		}

		bmapp = &bmaps[0];
		/*
		 * Each pass through the loop writes another buffer
		 * to the file.  For big requests, iomap_write will
		 * have given up multiple bmaps to use so we make fewer
		 * calls to it on big requests than if it only gave
		 * us one at a time.
		 */
		while ((uiop->uio_resid != 0) && (nbmaps > 0)) {
			/*
			 * If the write doesn't completely overwrite
			 * the buffer and we're not writing from
			 * the beginning of the buffer to the end
			 * of the file then we need to read the
			 * buffer.
			 *
			 * Reading the buffer will send it to xfs_strategy
			 * which will take care of zeroing the holey
			 * parts of the buffer and coordinating with
			 * other, simultaneous writers.
			 */
			if ((bmapp->pbsize != bmapp->bsize) &&
			    !((bmapp->pboff == 0) &&
			      (uiop->uio_offset >= isize))) {
				bp = chunkread(vp, bmapp, 1, credp);
			} else {
				bp = getchunk(vp, bmapp, credp);
			}

			/*
			 * XXXajs
			 * The error handling below needs more work.
			 */
			if (bp->b_flags & B_ERROR) {
				error = geterror(bp);
				ASSERT(error != EINVAL);
				brelse(bp);
				break;
			}

			ASSERT((BBTOB(bmapp->offset) + bmapp->pboff) ==
			       uiop->uio_offset);
			if (error = biomove(bp, bmapp->pboff, bmapp->pbsize,
					    UIO_WRITE, uiop)) {
				if (!(bp->b_flags & B_DONE)) {
					bp->b_flags |= B_STALE | B_DONE |
						       B_ERROR;
				}
				brelse(bp);
				break;
			}

			/*
			 * If we've grown the file, get back the
			 * inode lock and move di_size up to the
			 * new size.  It may be that someone else
			 * made it even bigger, so be careful not
			 * to shrink it.
			 *
			 * Noone could have shrunken the file, because
			 * we are holding the iolock shared.
			 */
			if (uiop->uio_offset > isize) {
				xfs_ilock(ip, XFS_ILOCK_EXCL);
				if (uiop->uio_offset > ip->i_d.di_size) {
					ip->i_d.di_size = uiop->uio_offset;
					ip->i_update_core = 1;
					isize = uiop->uio_offset;
				}
				xfs_iunlock(ip, XFS_ILOCK_EXCL);
			}

			if (uiop->uio_fmode & FSYNC) {
				bwrite(bp);
			} else {
				bdwrite(bp);
			}

			bmapp++;
			nbmaps--;

		}
	} while ((uiop->uio_resid > 0) && !error);

	ip->i_new_size = 0;
	return error;
}


/*
 * xfs_write
 *
 * This is the xFS VOP_WRITE entry point.  It does some minimal error
 * checking and then switches out based on the file type.
 */
int
xfs_write(
	vnode_t	*vp,
	uio_t	*uiop,
	int	ioflag,
	cred_t	*credp)
{
	struct reservation_id id;
	xfs_inode_t	*ip;
	int		type;
	off_t		offset;
	size_t		count;
	int		error;
	int		n;
	int		resid;
	timestruc_t	tv;


	ip = XFS_VTOI(vp);
	ASSERT(ismrlocked(&ip->i_iolock, MR_UPDATE) != 0);

	type = ip->i_d.di_mode & IFMT;
	ASSERT(type == IFREG || type == IFDIR ||
	       type == IFLNK || type == IFSOCK);

	if (ioflag & IO_APPEND) {
		/*
		 * In append mode, start at the end of the file.
		 * Since I've got the iolock exclusive I can look
		 * at di_size.
		 */
		uiop->uio_offset = ip->i_d.di_size;
	}

	offset = uiop->uio_offset;
	count = uiop->uio_resid;

#ifndef SIM
	if (MANDLOCK(vp, ip->i_d.di_mode) &&
	    (error = chklock(vp, FWRITE, offset, count, uiop->uio_fmode))) {
		return error;
	}
#endif

	if ((offset < 0) || ((offset + count) > XFS_MAX_FILE_OFFSET)) {
		return XFS_ERROR(EINVAL);
	}
	if (count <= 0) {
		return 0;
	}

	switch (type) {
	case IFREG:
		n = uiop->uio_limit - uiop->uio_offset;
		if (n <= 0) {
			return XFS_ERROR(EFBIG);
		}
		if (n < uiop->uio_resid) {
			resid = uiop->uio_resid - n;
			uiop->uio_resid = n;
		} else {
			resid = 0;
		}

#ifdef SIM
		/*
		 * Need pid of client not of simulator process.
		 * This may not always be correct.
		 */
		id.pid = MAKE_REQ_PID(u.u_procp->p_pid - 1, 0);
#else
		id.pid = MAKE_REQ_PID(u.u_procp->p_pid, 0);
#endif
		id.ino = ip->i_ino;
		error = xfs_grio_req( ip, &id, uiop, ioflag, credp, UIO_WRITE);

		/*
		 * Add back whatever we refused to do because of
		 * uio_limit.
		 */
		uiop->uio_resid += resid;


		/*
		 * We've done at least a partial write, so don't
		 * return an error on this call.  Also update the
		 * timestamps since we changed the file.
		 *
		 * XXXajs
		 * Are these the right timestamps?
		 */
		if (count != uiop->uio_resid) {
			error = 0;
			nanotime(&tv);
			ip->i_d.di_mtime.t_sec = tv.tv_sec;
			ip->i_d.di_ctime.t_sec = tv.tv_sec;
			ip->i_update_core = 1;
		}
		break;

	case IFDIR:
	case IFLNK:
		return XFS_ERROR(EINVAL);

	case IFSOCK:
		return XFS_ERROR(ENODEV);

	default:
		ASSERT(0);
		return XFS_ERROR(EINVAL);
	}
	
	return error;
}


/*
 * This is the xFS entry point for VOP_BMAP().
 * It simply switches based on the given flags
 * to either xfs_iomap_read() or xfs_iomap_write().
 * This cannot be used to grow a file or to read
 * beyond the end of the file.
 *
 * The caller is required to be holding the inode's
 * iolock in at least shared mode.
 */
int
xfs_bmap(
	vnode_t		*vp,
	off_t		offset,
	ssize_t		count,
	int		flags,
	cred_t		*credp,
	struct bmapval	*bmapp,
	int		*nbmaps)
{
	xfs_inode_t	*ip;
	uint		lock_mode;

	ip = XFS_VTOI(vp);
	ASSERT((ip->i_d.di_mode & IFMT) == IFREG);
	ASSERT(ismrlocked(&ip->i_iolock, MR_ACCESS | MR_UPDATE) != 0);
	ASSERT((flags == B_READ) || (flags == B_WRITE));

	if (flags == B_READ) {
		lock_mode = xfs_ilock_map_shared(ip);
		xfs_iomap_read(ip, offset, count, bmapp, nbmaps);
		xfs_iunlock_map_shared(ip, lock_mode);
	} else {
		xfs_ilock(ip, XFS_ILOCK_EXCL);
		ASSERT(ip->i_d.di_size >= (offset + count));
		xfs_iomap_write(ip, offset, count, bmapp, nbmaps);
		xfs_iunlock(ip, XFS_ILOCK_EXCL);
	}

	return 0;
}

/*
 * Set up rbp so that it points to the memory attached to bp
 * from rbp_offset from the start of bp for rbp_len bytes.
 */
STATIC void
xfs_overlap_bp(
	buf_t	*bp,
	buf_t	*rbp,
	uint	rbp_offset,
	uint	rbp_len)
{
	int	pgbboff;
	int	bytes_off;
	pfd_t	*pfdp;

	
	if (BP_ISMAPPED(bp)) {
		/*
		 * The real buffer is already mapped, so just use
		 * its virtual memory for ourselves.
		 */
		rbp->b_un.b_addr = bp->b_un.b_addr + rbp_offset;
		rbp->b_bcount = rbp_len;
		rbp->b_bufsize = rbp_len;
	} else {
		/*
		 * The real buffer is not yet mapped to virtual memory.
		 * Just get the subordinate buffer's page pointers
		 * set up and make it a PAGEIO buffer like the real one.
		 *
		 * First find the first page of rbp.  We do this by
		 * walking the list of pages in bp until we find the
		 * one containing the start of rbp.  Note that neither
		 * bp nor rbp are required to start on page boundaries.
		 */
		bytes_off = rbp_offset + BBTOB(dpoff(bp->b_offset));
		pfdp = NULL;
		pfdp = getnextpg(bp, pfdp);
		ASSERT(pfdp != NULL);
		while (bytes_off >= NBPP) {
			pfdp = getnextpg(bp, pfdp);
			ASSERT(pfdp != NULL);
			bytes_off -= NBPP;
		}
		rbp->b_pages = pfdp;

		rbp->b_bcount = rbp_len;
		rbp->b_offset = bp->b_offset + BTOBB(rbp_offset);
		pgbboff = dpoff(rbp->b_offset);
		rbp->b_bufsize = ctob(dtop(pgbboff + BTOBB(rbp_len)));

		rbp->b_flags |= B_PAGEIO;
#ifndef SIM
		if (pgbboff != 0) {
			bp_mapin(rbp);
		}
#endif
	}
	rbp->b_blkno = bp->b_blkno + BTOBB(rbp_offset);
	rbp->b_remain = 0;
	rbp->b_vp = bp->b_vp;
	rbp->b_edev = bp->b_edev;
	rbp->b_flags |= (bp->b_flags & (B_UNCACHED | B_ASYNC));
}


/*
 * Zero the given bp from data_offset from the start of it for data_len
 * bytes.
 */
STATIC void
xfs_zero_bp(
	buf_t	*bp,
	int	data_offset,
	int	data_len)
{
	pfd_t	*pfdp;
	caddr_t	page_addr;
	int	len;

	data_offset += BBTOB(dpoff(bp->b_offset));
	pfdp = NULL;
	pfdp = getnextpg(bp, pfdp);
	ASSERT(pfdp != NULL);
	while (data_offset >= NBPP) {
		pfdp = getnextpg(bp, pfdp);
		ASSERT(pfdp != NULL);
		data_offset -= NBPP;
	}
	ASSERT(data_offset >= 0);
	while (data_len > 0) {
		page_addr = page_mapin(pfdp, (bp->b_flags & B_UNCACHED ?
					      VM_UNCACHED : 0), 0);
		len = MIN(data_len, NBPP - data_offset);
		bzero(page_addr + data_offset, len);
		data_len -= len;
		data_offset = 0;
		page_mapout(page_addr);
		pfdp = getnextpg(bp, pfdp);
	}
}

/*
 * "Read" in a buffer whose b_blkno is -1.  This means that
 * at the time the buffer was created there was no underlying
 * backing store for the range of the file covered by the bp.
 * To figure out the current state of things, we lock the inode
 * and call xfs_bmapi() to look at the current extents format.
 * If we're over a hole or delayed allocation space we simply
 * zero the corresponding portions of the buffer.  For parts
 * over real disk space we need to read in the stuff from disk.
 *
 * We know that we can just use xfs_ilock(SHARED) rather than
 * xfs_ilock_map_shared() here, because the extents had to be
 * read in in order to create the buffer we're trying to write out.
 */
STATIC void
xfs_strat_read(
	vnode_t	*vp,
	buf_t	*bp)
{
	xfs_fileoff_t	offset_fsb;
	xfs_fileoff_t   map_start_fsb;
	xfs_fileoff_t	imap_offset;
	xfs_extlen_t	count_fsb;
	xfs_extlen_t	imap_blocks;
	xfs_fsize_t	isize;
	off_t		offset;
	off_t		end_offset;
	int		x;
	caddr_t		datap;
	buf_t		*rbp;
	xfs_mount_t	*mp;
	xfs_inode_t	*ip;
	int		data_bytes;
	int		data_offset;
	int		data_len;
	int		nimaps;
#define	XFS_STRAT_READ_IMAPS	4
        xfs_bmbt_irec_t	imap[XFS_STRAT_READ_IMAPS];
	
	ASSERT(bp->b_blkno == -1);
	ip = XFS_VTOI(vp);
	mp = XFS_VFSTOM(vp->v_vfsp);
	offset_fsb = XFS_BB_TO_FSBT(mp, bp->b_offset);
	/*
	 * Only read up to the EOF.
	 */
	isize = ip->i_d.di_size;
	offset = BBTOB(bp->b_offset);
	end_offset = offset + bp->b_bcount;
	if ((offset < isize) && (end_offset > isize)) {
		count_fsb = XFS_B_TO_FSB(mp, isize - offset);
	} else {
		count_fsb = XFS_B_TO_FSB(mp, bp->b_bcount);
	}
	map_start_fsb = offset_fsb;
	xfs_ilock(ip, XFS_ILOCK_SHARED);
	while (count_fsb != 0) {
		nimaps = XFS_STRAT_READ_IMAPS;
		(void) xfs_bmapi(NULL, ip, map_start_fsb, count_fsb, 0,
				 NULLFSBLOCK, 0, imap, &nimaps, NULL);
		ASSERT(nimaps >= 1);
		
		for (x = 0; x < nimaps; x++) {
			imap_offset = imap[x].br_startoff;
			ASSERT(imap_offset == map_start_fsb);
			imap_blocks = imap[x].br_blockcount;
			ASSERT(imap_blocks <= count_fsb);
			if ((imap[x].br_startblock == DELAYSTARTBLOCK) ||
			    (imap[x].br_startblock == HOLESTARTBLOCK)) {
				/*
				 * This is either a hole or a delayed
				 * alloc extent.  Either way, just fill
				 * it with zeroes.
				 */
#ifndef SIM
				datap = bp_mapin(bp);
				datap += XFS_FSB_TO_B(mp, imap_offset -
						      offset_fsb);
				data_bytes = XFS_FSB_TO_B(mp, imap_blocks);

				bzero(datap, data_bytes);
#else /* SIM */
				ASSERT(bp->b_flags & B_PAGEIO);
				data_offset = XFS_FSB_TO_B(mp, imap_offset -
							   offset_fsb);
				data_len = XFS_FSB_TO_B(mp, imap_blocks);
				xfs_zero_bp(bp, data_offset, data_len);
#endif /* SIM */
			} else {
				/*
				 * The extent really exists on disk, so
				 * read it in.
				 */
				rbp = getrbuf(KM_SLEEP);
				data_offset = XFS_FSB_TO_B(mp, imap_offset -
							  offset_fsb);
				data_len = XFS_FSB_TO_B(mp, imap_blocks);
				xfs_overlap_bp(bp, rbp, data_offset,
					       data_len);
				rbp->b_blkno = XFS_FSB_TO_DADDR(mp,
						imap[x].br_startblock);
				rbp->b_flags |= B_READ;
				rbp->b_flags &= ~B_ASYNC;

				bdstrat(bmajor(rbp->b_edev), rbp);
				iowait(rbp);

				if (rbp->b_flags & B_ERROR) {
					bp->b_flags |= B_ERROR;
					bp->b_error = XFS_ERROR(rbp->b_error);
					ASSERT(bp->b_error != EINVAL);
				}
#ifndef SIM
				if (BP_ISMAPPED(rbp)) {
					bp_mapout(rbp);
				}
#endif
				freerbuf(rbp);
			}
			count_fsb -= imap_blocks;
			map_start_fsb += imap_blocks;
		}
	}
	xfs_iunlock(ip, XFS_ILOCK_SHARED);
	iodone(bp);
}


#ifdef DEBUG
/*
 * xfs_strat_write_check
 *
 * Make sure that there are blocks or delayed allocation blocks
 * underlying the entire area given.  The imap parameter is simply
 * given as a scratch area in order to reduce stack space.  No
 * values are returned within it.
 */
STATIC void
xfs_strat_write_check(
	xfs_inode_t	*ip,
	xfs_fileoff_t	offset_fsb,
	xfs_extlen_t	buf_fsb,
	xfs_bmbt_irec_t	*imap,
	int		imap_count)
{
	xfs_extlen_t	count_fsb;
	boolean_t	done;
	int		nimaps;
	int		n;

	xfs_ilock(ip, XFS_ILOCK_SHARED);
	count_fsb = 0;
	done = B_FALSE;
	while (count_fsb < buf_fsb) {
		nimaps = imap_count;
		(void) xfs_bmapi(NULL, ip, (offset_fsb + count_fsb),
				 (buf_fsb - count_fsb), 0, NULLFSBLOCK, 0,
				 imap, &nimaps, NULL);
		ASSERT(nimaps > 0);
		n = 0;
		while (n < nimaps) {
			ASSERT(imap[n].br_startblock != HOLESTARTBLOCK);
			count_fsb += imap[n].br_blockcount;
			ASSERT(count_fsb <= buf_fsb);
			n++;
		}
	}
	xfs_iunlock(ip, XFS_ILOCK_SHARED);
		
	return;
}
#endif /* DEBUG */

/*
 * This is the completion routine for the heap-allocated buffers
 * used to write out a buffer which becomes fragmented during
 * xfs_strat_write().  It must coordinate with xfs_strat_write()
 * to properly mark the lead buffer as done when necessary and
 * to free the subordinate buffer.
 */
STATIC void
xfs_strat_write_relse(
	buf_t	*rbp)
{
	int	s;
	buf_t	*leader;
	buf_t	*forw;
	buf_t	*back;
	

	s = splockspl(xfs_strat_lock, splhi);
	ASSERT(rbp->b_flags & B_DONE);

	forw = (buf_t*)rbp->b_fsprivate2;
	back = (buf_t*)rbp->b_fsprivate;
	ASSERT(back != NULL);
	ASSERT(back->b_fsprivate2 == rbp);
	ASSERT((forw == NULL) || (forw->b_fsprivate == rbp));

	/*
	 * Pull ourselves from the list.
	 */
	back->b_fsprivate2 = forw;
	if (forw != NULL) {
		forw->b_fsprivate = back;
	}

	if ((forw == NULL) &&
	    (back->b_flags & B_LEADER) &&
	    !(back->b_flags & B_PARTIAL)) {
		/*
		 * We are the only buffer in the list and the lead buffer
		 * has cleared the B_PARTIAL bit to indicate that all
		 * subordinate buffers have been issued.  That means it
		 * is time to finish off the lead buffer.
		 */
		leader = back;
		if (rbp->b_flags & B_ERROR) {
			leader->b_flags |= B_ERROR;
			leader->b_error = XFS_ERROR(rbp->b_error);
			ASSERT(leader->b_error != EINVAL);
		}
		leader->b_flags &= ~B_LEADER;
		spunlockspl(xfs_strat_lock, s);

		iodone(leader);
	} else {
		/*
		 * Either there are still other buffers in the list or
		 * not all of the subordinate buffers have yet been issued.
		 * In this case just pass any errors on to the lead buffer.
		 */
		while (!(back->b_flags & B_LEADER)) {
			back = (buf_t*)back->b_fsprivate;
		}
		ASSERT(back != NULL);
		ASSERT(back->b_flags & B_LEADER);
		leader = back;
		if (rbp->b_flags & B_ERROR) {
			leader->b_flags |= B_ERROR;
			leader->b_error = XFS_ERROR(rbp->b_error);
			ASSERT(leader->b_error != EINVAL);
		}
		spunlockspl(xfs_strat_lock, s);
	}

	rbp->b_fsprivate = NULL;
	rbp->b_fsprivate2 = NULL;
	rbp->b_relse = NULL;
#ifndef SIM
	if (BP_ISMAPPED(rbp)) {
		bp_mapout(rbp);
	}
#endif
	freerbuf(rbp);
}

STATIC void
xfs_strat_write(
	vnode_t	*vp,
	buf_t	*bp)
{
	xfs_fileoff_t	offset_fsb;
	xfs_fileoff_t   map_start_fsb;
	xfs_fileoff_t	imap_offset;
	xfs_fsblock_t	first_block;
	xfs_extlen_t	count_fsb;
	xfs_extlen_t	imap_blocks;
	int		x;
	caddr_t		datap;
	buf_t		*rbp;
	xfs_mount_t	*mp;
	xfs_inode_t	*ip;
	xfs_trans_t	*tp;
	int		error;
	xfs_bmap_free_t	free_list;
	xfs_bmbt_irec_t	*imapp;
	int		rbp_offset;
	int		rbp_len;
	int		set_lead = 0;
	int		s;
	int		imap_index;
	int		nimaps;
#define	XFS_STRAT_WRITE_IMAPS	4
	xfs_bmbt_irec_t	imap[XFS_STRAT_WRITE_IMAPS];
	
	ip = XFS_VTOI(vp);
	mp = ip->i_mount;
	bp->b_flags |= B_STALE;

	ASSERT(bp->b_blkno == -1);
	offset_fsb = XFS_BB_TO_FSBT(mp, bp->b_offset);
	count_fsb = XFS_B_TO_FSB(mp, bp->b_bcount);
	xfs_strat_write_check(ip, offset_fsb, count_fsb, imap,
			      XFS_STRAT_WRITE_IMAPS);
	map_start_fsb = offset_fsb;
	while (count_fsb != 0) {
		/*
		 * Set up a transaction with which to allocate the
		 * backing store for the file.
		 */
		tp = xfs_trans_alloc(mp, XFS_TRANS_FILE_WRITE);
		error = xfs_trans_reserve(tp, 0, XFS_DEFAULT_LOG_RES(mp),
					  0, 0);
		ASSERT(error == 0);
		xfs_ilock(ip, XFS_ILOCK_EXCL);
		xfs_trans_ijoin(tp, ip, XFS_ILOCK_EXCL);
		xfs_trans_ihold(tp, ip);

		/*
		 * Allocate the backing store for the file.
		 */
		XFS_BMAP_INIT(&free_list, &first_block);
		nimaps = XFS_STRAT_WRITE_IMAPS;
		first_block = xfs_bmapi(tp, ip, map_start_fsb, count_fsb,
					XFS_BMAPI_WRITE, first_block, 1,
					imap, &nimaps, &free_list);
		ASSERT(nimaps > 0);
		(void) xfs_bmap_finish(&tp, &free_list, first_block, 0);

		xfs_trans_commit(tp, 0);
		xfs_iunlock(ip, XFS_ILOCK_EXCL);

		/*
		 * This is a quick check to see if the first time through
		 * was able to allocate a single extent over which to
		 * write
		 */
		if ((map_start_fsb == offset_fsb) &&
		    (imap[0].br_blockcount == count_fsb)) {
			ASSERT(nimaps == 1);
			bp->b_blkno = XFS_FSB_TO_DADDR(mp,
						     imap[0].br_startblock);
			bp->b_bcount = XFS_FSB_TO_B(mp, count_fsb);
			bdstrat(bmajor(bp->b_edev), bp);
			return;
		}

		/*
		 * Bmap couldn't manage to lay the buffer out as
		 * one extent, so we need to do multiple writes
		 * to push the data to the multiple extents.
		 * Write out the subordinate bps asynchronously
		 * and have their completion functions coordinate
		 * with the code at the end of this function to
		 * deal with marking our bp as done when they have
		 * ALL completed.
		 */
		imap_index = 0;
		if (!set_lead) {
			bp->b_flags |= B_LEADER | B_PARTIAL;
			set_lead = 1;
		}
		while (imap_index < nimaps) {
			rbp = getrbuf(KM_SLEEP);

			imapp = &imap[imap_index];
			ASSERT((imapp->br_startblock != DELAYSTARTBLOCK) &&
			       (imapp->br_startblock != HOLESTARTBLOCK));
			imap_offset = imapp->br_startoff;
			rbp_offset = XFS_FSB_TO_B(mp, imap_offset - offset_fsb);
			imap_blocks = imapp->br_blockcount;
			rbp_len = XFS_FSB_TO_B(mp, imap_blocks);
			xfs_overlap_bp(bp, rbp, rbp_offset, rbp_len);
			rbp->b_blkno = XFS_FSB_TO_DADDR(mp,
							imapp->br_startblock);
			
			/*
			 * Link the buffer into the list of subordinate
			 * buffers started at bp->b_fsprivate2.  The
			 * subordinate buffers use b_fsprivate and
			 * b_fsprivate2 for back and forw pointers, but
			 * the lead buffer cannot use b_fsprivate.
			 * A subordinate buffer can always find the lead
			 * buffer by searching back through the fsprivate
			 * fields until it finds the buffer marked with
			 * B_LEADER.
			 */
			s = splockspl(xfs_strat_lock, splhi);
			rbp->b_fsprivate = bp;
			rbp->b_fsprivate2 = bp->b_fsprivate2;
			if (bp->b_fsprivate2 != NULL) {
				((buf_t*)(bp->b_fsprivate2))->b_fsprivate =
									rbp;
			}
			bp->b_fsprivate2 = rbp;
			spunlockspl(xfs_strat_lock, s);

			rbp->b_relse = xfs_strat_write_relse;
			rbp->b_flags |= B_ASYNC;

			bdstrat(bmajor(rbp->b_edev), rbp);

			map_start_fsb += imapp->br_blockcount;
			count_fsb -= imapp->br_blockcount;
			ASSERT(count_fsb >= 0);

			imap_index++;
		}
	}

	/*
	 * Now that we've issued all the partial I/Os, check to see
	 * if they've all completed.  If they have then mark the buffer
	 * as done, otherwise clear the B_PARTIAL flag in the buffer to
	 * indicate that the last subordinate buffer to complete should
	 * mark the buffer done.
	 */
	s = splockspl(xfs_strat_lock, splhi);
	ASSERT((bp->b_flags & (B_DONE | B_PARTIAL)) == B_PARTIAL);
	ASSERT(bp->b_flags & B_LEADER);

	if (bp->b_fsprivate2 == NULL) {
		/*
		 * All of the subordinate buffers have completed.
		 * Call iodone() to note that the I/O has completed.
		 */
		bp->b_flags &= ~(B_PARTIAL | B_LEADER);
		spunlockspl(xfs_strat_lock, s);

		iodone(bp);
		return;
	}

	bp->b_flags &= ~B_PARTIAL;
	spunlockspl(xfs_strat_lock, s);
	return;
}

/*
 * xfs_strategy
 *
 * This is where all the I/O and all the REAL allocations take
 * place.  For buffers with -1 for their b_blkno field, we need
 * to do a bmap to figure out what to do with them.  If it's a
 * write we may need to do an allocation, while if it's a read
 * we may either need to read from disk or do some block zeroing.
 * If b_blkno specifies a real block, then all we need to do
 * is pass the buffer on to the underlying driver.
 */
void
xfs_strategy(
	vnode_t	*vp,
	buf_t	*bp)
{
	xfs_mount_t	*mp;
	int		s;

	mp = XFS_VFSTOM(vp->v_vfsp);

	/*
	 * If this is just a buffer whose underlying disk space
	 * is already allocated, then just do the requested I/O.
	 */
	if (bp->b_blkno >= 0) {
		bdstrat(bmajor(bp->b_edev), bp);
		return;
	}

	/*
	 * If we're reading, then we need to find out how the
	 * portion of the file required for this buffer is layed
	 * out and zero/read in the appropriate data.
	 */
	if (bp->b_flags & B_READ) {
		xfs_strat_read(vp, bp);
		return;
	}

	/*
	 * Here we're writing the file and probably need to allocate
	 * some underlying disk space. If the buffer is being written
	 * asynchronously by bdflush() then we queue if for the xfsds
	 * so that we won't put bdflush() to sleep.
	 */
	if ((bp->b_flags & (B_ASYNC | B_BDFLUSH)) == (B_ASYNC | B_BDFLUSH) &&
	    (xfsd_count > 0)) {
		s = splock(xfsd_lock);
		/*
		 * Queue the buffer at the end of the list.
		 */
		if (xfsd_list == NULL) {
			bp->av_forw = bp;
			bp->av_back = bp;
			xfsd_list = bp;
		} else {
			bp->av_back = xfsd_list->av_back;
			xfsd_list->av_back->av_forw = bp;
			xfsd_list->av_back = bp;
			bp->av_forw = xfsd_list;
		}
		xfsd_bufcount++;
		cvsema(&xfsd_wait);
		spunlock(xfsd_lock, s);
	} else {
		xfs_strat_write(vp, bp);
	}
}

#ifndef SIM

/*
 * This is called from main() to start the xfs daemons.
 * We'll start with a minimum of 4 of them, and add 1
 * for each 128 MB of memory up to 1 GB.  That should
 * be enough.
 */
void
xfs_start_daemons(void)
{
	int	num_daemons;
	int	i;
	int	num_pages;

	num_daemons = 4;
	num_pages = (int)physmem - 32768;
	while ((num_pages > 0) && (num_daemons < 13)) {
		num_pages -= 32768;
		num_daemons++;
	}
	ASSERT(num_daemons <= 13);

	for (i = 0; i < num_daemons; i++) {
		if (newproc(NP_SYSPROC, 0)) {
#if !STAT_TIME
			ASSERT(private.p_activetimer ==
			       &u.u_ptimer[PTIMER_INDEX(AS_SYS_RUN)]);
			/* u_utime, u_stime are evaluated on exit */
			/* u_cutime, u_cstime are accumulated during wait() */
			timerclear(&u.u_cutime);
			timerclear(&u.u_cstime);
#else
			u.u_cstime = u.u_stime = u.u_cutime = u.u_utime = 0;
#endif
			bcopy("xfsd", u.u_psargs, 5);
			bcopy("xfsd", u.u_comm, 4);
			xfsd();
		}
	}
	return;
}

/*
 * This is the main loop for the xfs daemons.
 * From here they wait in a loop for buffers which will
 * require transactions to write out and process them as they come.
 * This way we never force bdflush() to wait on one of our transactions,
 * thereby keeping the system happier and preventing buffer deadlocks.
 */
STATIC int
xfsd(void)
{
	int	s;
	buf_t	*bp;
	buf_t	*forw;
	buf_t	*back;

	/*
	 * Make us a high non-degrading priority process like bdflush(),
	 * since that is who we're relieving of work.
	 */
	setinfoRunq(u.u_procp, RQRTPRI, NDPHIMIN);

	s = splock(xfsd_lock);
	xfsd_count++;

	while (1) {
		while (xfsd_list == NULL) {
			(void) spunlock_psema(xfsd_lock, s, &xfsd_wait,
					      PRIBIO);
			s = splock(xfsd_lock);
		}

		/*
		 * Pull a buffer off of the list.
		 */
		bp = xfsd_list;
		forw = bp->av_forw;
		back = bp->av_back;
		forw->av_back = back;
		back->av_forw = forw;
		if (forw == bp) {
			xfsd_list = NULL;
		} else {
			xfsd_list = forw;
		}
		bp->av_forw = bp;
		bp->av_back = bp;
		xfsd_bufcount--;;
		ASSERT(xfsd_bufcount >= 0);

		spunlock(xfsd_lock, s);

		ASSERT((bp->b_flags & (B_BUSY | B_ASYNC | B_READ)) ==
		       (B_BUSY | B_ASYNC));
		xfs_strat_write(bp->b_vp, bp);

		s = splock(xfsd_lock);
	}
}
#endif	/* !SIM */


struct dio_s {
	struct vnode *vp;
	struct cred *cr;
	int 	ioflag;
};

/*
 * xfs_diostrat()
 *	This routine issues the calls to the disk device strategy routine
 *	for file system reads and writes made using direct I/O from user
 *	space. In the case of a write request the I/Os are issued one 
 *	extent at a time. In the case of a read request I/Os for each extent
 *	involved are issued at once.
 *
 * RETURNS:
 *	none
 */
int
xfs_diostrat( buf_t *bp)
{
	struct dio_s	*dp;
	xfs_inode_t 	*ip;
	xfs_trans_t	*tp;
	vnode_t		*vp;
	xfs_mount_t	*mp;
	xfs_bmbt_irec_t	imaps[XFS_BMAP_MAX_NMAP], *imapp, *timapp;
	buf_t		*bps[XFS_BMAP_MAX_NMAP], *nbp;
	xfs_fileoff_t	offset_fsb, last_fsb;
	xfs_fsblock_t	firstfsb;
	xfs_extlen_t	blocks, count_fsb, total;
	xfs_bmap_free_t free_list;
	caddr_t		base;
	ssize_t		bytes_this_req, resid, count, totxfer;
	off_t		offset, offset_this_req;
	int		i, j, error, writeflag, reccount;
	int		end_of_file, bufsissued, totresid, exist;
	int		ioflag, blk_algn, rt, numrtextents, rtextsize;
	uint		lock_mode;

	dp        = (struct dio_s *)bp->b_private;
	vp        = dp->vp;
	ip        = XFS_VTOI(vp); 
	mp 	  = XFS_VFSTOM(XFS_ITOV(ip)->v_vfsp);
	base	  = bp->b_un.b_addr;
	error     = resid = totxfer = end_of_file = 0;
	ioflag	  = dp->ioflag;
	offset    = BBTOB(bp->b_blkno);
	blk_algn  = 0;
	totresid  = count  = bp->b_bcount;

	/*
 	 * Determine if this file is using the realtime volume.
	 */
	if ( ip->i_d.di_flags & XFS_DIFLAG_REALTIME )  {
		rt = 1;
		rtextsize = mp->m_sb.sb_rextsize;
	} else {
		numrtextents = 0;
		rtextsize = 0;
		rt = 0;
	}

	ASSERT(!(bp->b_flags & B_DONE));
        ASSERT(ismrlocked(&ip->i_iolock, MR_UPDATE) != 0);

	/*
	 * Alignment checks are done in xfs_diordwr().
	 * Determine if the operation is a read or a write.
	 */
	if (bp->b_flags & B_READ) {
		writeflag = 0;
	} else {
		writeflag = XFS_BMAPI_WRITE;
	}

	/*
 	 * Check if the request is on a file system block boundary.
	 */
	if ( (offset % mp->m_sb.sb_blocksize) != 0 ) {
		/*
 		 * The request is NOT on a file system block boundary.
		 */
		blk_algn =  BTOBB(offset % mp->m_sb.sb_blocksize);
	}

	/*
	 * Process the request until:
	 * 1) an I/O error occurs
	 * 2) end of file is reached.
	 * 3) end of device (driver error) occurs
	 * 4) request is completed.
	 */
	while ( !error && !end_of_file && !resid && count ) {
		offset_fsb = XFS_B_TO_FSBT( mp, offset );
		last_fsb   = XFS_B_TO_FSB( mp, offset + count);
		count_fsb  = XFS_B_TO_FSB( mp, count);
		blocks     = (xfs_extlen_t)(last_fsb - offset_fsb);

		exist = 1;
		XFS_BMAP_INIT(&free_list, &firstfsb);

		if ( writeflag ) {
			/*
 			 * In the write case, need to call bmapi() with
			 * the read flag set first to determine the existing 
			 * extents. This is done so that the correct amount
			 * of space can be reserved in the transaction 
			 * structure. 
			 */
			reccount = XFS_BMAP_MAX_NMAP;
			xfs_ilock( ip, XFS_ILOCK_EXCL );
			firstfsb = xfs_bmapi( tp, ip, offset_fsb, 
				count_fsb, 0, firstfsb, 0, imaps, 
				&reccount, 0);
			/*
 			 * Get a pointer to the current extent map.
			 * Writes will always be issued one at a time.
			 */
			reccount = 1;
			imapp = &imaps[0];
			count_fsb = imapp->br_blockcount;

			if ((imapp->br_startblock == DELAYSTARTBLOCK) ||
			    (imapp->br_startblock == HOLESTARTBLOCK)) {
				exist = 0;
			}

                        /*
                         * If blocks are not yet allocated for this part of
                         * the file, allocate space for the transactions.
                         */
			if (!exist) {
				if (rt) {
					/*
					 * Round up to even number of extents.
					 */
					numrtextents = (count_fsb+rtextsize-1)/
						rtextsize;
				}

				/*
 				 * Setup transactions.
 				 */
				tp = xfs_trans_alloc( mp, XFS_TRANS_FILE_WRITE);
				error = xfs_trans_reserve( tp, 
					   XFS_BM_MAXLEVELS(mp) + count_fsb, 
					   XFS_DEFAULT_LOG_RES(mp),
					   numrtextents, 0 );

				if (error) {
					/*
					 * Ran out of file system space.
					 * Free the transaction structure.
					 */
					ASSERT( error == ENOSPC );
					xfs_trans_cancel(tp, 0);
					xfs_iunlock( ip, XFS_ILOCK_EXCL);
					break;
				} else {
					xfs_trans_ijoin(tp,ip,XFS_ILOCK_EXCL);
					xfs_trans_ihold( tp, ip);
				}
			} else {
				tp = NULL;
			}
		} else {
			/*
			 * Read case.
			 * Read requests will be issued 
			 * up to XFS_BMAP_MAX_MAP at a time.
			 */
			reccount = XFS_BMAP_MAX_NMAP;
			imapp = &imaps[0];
			lock_mode = xfs_ilock_map_shared( ip);
		}

		/*
 		 * Issue the bmapi() call to get the extent info.
 		 * In the case of write requests this call does the
		 * actual file space allocation.
		 */
		firstfsb = xfs_bmapi( tp, ip, offset_fsb, count_fsb, 
			writeflag, firstfsb, 0, imapp, &reccount, &free_list);

		if ( writeflag ) {
			/*
 			 * Complete the bmapi() transactions.
			 */
			if (!exist) {
				xfs_bmap_finish( &tp, &free_list, firstfsb, 0 );
				xfs_trans_commit(tp , 0 );
			}
			xfs_iunlock( ip, XFS_ILOCK_EXCL);
		} else {
			xfs_iunlock_map_shared( ip, lock_mode);
		}

                /*
                 * xfs_bmapi() was unable to allocate space
                 */
                if (reccount == 0) {
                        error = XFS_ERROR(ENOSPC);
                        break;
                }

		/*
   		 * Run through each extent.
		 */
		bufsissued = 0;
		for (i = 0; (i < reccount) && (!end_of_file) && (count); i++){
			imapp = &imaps[i];

			bytes_this_req  = XFS_FSB_TO_B(mp,
				imapp->br_blockcount) - BBTOB(blk_algn);


			offset_this_req = XFS_FSB_TO_B(mp,
				imapp->br_startoff) + BBTOB(blk_algn); 

			/*
			 * Check if this is the end of the file.
			 */
			if (offset_this_req +bytes_this_req >ip->i_d.di_size){
				if ( writeflag ) {
					/*
					 * File is being extended on a
					 * write, update the file size.
					 */
			         	ASSERT((vp->v_flag & VISSWAP) == 0);
					xfs_ilock(ip, XFS_ILOCK_EXCL);
				 	ip->i_d.di_size = offset_this_req + 
							bytes_this_req;
					ip->i_update_core = 1;
					xfs_iunlock(ip, XFS_ILOCK_EXCL);
				} else {
					/*
 				 	 * If trying to read past end of
 				 	 * file, shorten the request size.
					 */
					bytes_this_req = ip->i_d.di_size - 
						offset_this_req;
					end_of_file = 1;
				}
			}

	     		/*
 	      		 * Reduce request size, if it 
			 * is longer than user buffer.
	      		 */
	     		if ( bytes_this_req > count ) {
	 			 bytes_this_req = count;
			}

			/*
 			 * Do not do I/O if there is a hole in the file.
			 */
			if (imapp->br_startblock != HOLESTARTBLOCK) {
				/*
 				 * Setup I/O request for this extent.
				 */
	     			bps[bufsissued++]= nbp = getphysbuf();
	     			nbp->b_flags     = bp->b_flags;
	     			nbp->b_flags2    = bp->b_flags2;

				if (ioflag & IO_GRIO) {
					nbp->b_flags2 |= B_GR_BUF;
				} else {
					nbp->b_flags2 &= ~B_GR_BUF;
				}

	     			nbp->b_error     = 0;
	     			nbp->b_proc      = bp->b_proc;
	     			nbp->b_edev      = bp->b_edev;
				if (rt) {
	     				nbp->b_blkno = XFS_BTOD(mp,
						imapp->br_startblock);
				} else {
	     				nbp->b_blkno = XFS_FSB_TO_DADDR(mp,
						imapp->br_startblock) + 
						blk_algn;
				}
	     			nbp->b_bcount    = bytes_this_req;
	     			nbp->b_un.b_addr = base;
				/*
 				 * Issue I/O request.
				 */
				bdstrat( bmajor(nbp->b_edev), nbp );
	
		    		if (error = geterror(nbp)) {
					iowait( nbp );
					nbp->b_flags = 0;
		     			nbp->b_un.b_addr = 0;
					putphysbuf( nbp );
					bps[bufsissued--] = 0;
					break;
		     		}
			} else {
				/*
 				 * Hole in file can only happen on read case.
				 */
				ASSERT(!writeflag);
				
				/*
 				 * Physio() has already mapped user address.
				 */
				bzero(base, bytes_this_req);

				/*
				 * Bump the transfer count.
				 */
				totxfer += bytes_this_req;
			}

			/*
			 * update pointers for next round.
			 */

	     		base   += bytes_this_req;
	     		offset += bytes_this_req;
	     		count  -= bytes_this_req;
			blk_algn= 0;

		} /* end of for loop */

		/*
		 * Wait for I/O completion and recover buffers.
		 */
		for ( j = 0; j < bufsissued ; j++) {
	  		nbp = bps[j];
	    		iowait( nbp );

	     		if (!error)
				error = geterror( nbp );

	     		if (!error && !resid) {
				resid = nbp->b_resid;
				totxfer += (nbp->b_bcount - resid);
			} 
	    	 	nbp->b_flags = 0;
	     		nbp->b_un.b_addr = 0;
	     		nbp->b_bcount = 0;
	    	 	putphysbuf( nbp );
	     	}
	} /* end of while loop */

	/*
 	 * Fill in resid count for original buffer.
	 */
	bp->b_resid = totresid - totxfer;

	/*
 	 *  Update the inode timestamp if file was written.
 	 */
	if ( writeflag ) {
		timestruc_t tv;

		xfs_ilock(ip, XFS_ILOCK_EXCL);
		if ((ip->i_d.di_mode & (ISUID|ISGID)) && dp->cr->cr_uid != 0){
			ip->i_d.di_mode &= ~ISUID;
			if (ip->i_d.di_mode & (IEXEC >> 3))
				ip->i_d.di_mode &= ~ISGID;
		}
		nanotime( &tv );
		ip->i_d.di_mtime.t_sec = ip->i_d.di_ctime.t_sec = tv.tv_sec;
		ip->i_update_core = 1;
		xfs_iunlock(ip, XFS_ILOCK_EXCL);
	}

	/*
	 * Issue completion on the original buffer.
	 */
	bioerror( bp, error);
	iodone( bp );
	return(0);
}

/*
 * xfs_diordwr()
 *	This routine sets up a buf structure to be used to perform 
 * 	direct I/O operations to user space. The user specified 
 *	paramters are checked for alignment and size limitations. A buf
 *	structure is allocated an biophysio() is called.
 *
 * RETURNS:
 * 	 0 on success
 * 	errno on error
 */
int
xfs_diordwr(vnode_t	*vp,
	 uio_t		*uiop,
	 int		ioflag,
	 cred_t		*credp,
	 int		rw)
{
	xfs_inode_t	*ip;
	struct dio_s	dp;
	xfs_mount_t	*mp;
	buf_t		*bp;
	int		error;

	ip = XFS_VTOI(vp);

	/*
 	 * Check that the user buffer address, file offset, and
 	 * request size are all multiples of BBSIZE. This prevents
	 * the need for read/modify/write operations.
 	 */
	if ((((int)(uiop->uio_iov->iov_base)) & BBMASK) ||
	     (uiop->uio_offset & BBMASK) ||
	     (uiop->uio_resid & BBMASK)) {
#ifndef SIM
		return XFS_ERROR(EINVAL);
#endif
	}

	/*
 	 * Do maxio check.
 	 */
	if (uiop->uio_resid > ctob(v.v_maxdmasz)) {
#ifndef SIM
		return XFS_ERROR(EINVAL);
#endif
	}

	/*
 	 * Use dio_s structure to pass file/credential
	 * information to file system strategy routine.
	 */
	dp.vp = vp;
	dp.cr = credp;
	dp.ioflag = ioflag;

	/*
 	 * Allocate local buf structure.
	 */
	bp = getphysbuf();
	bp->b_private = &dp;
	mp = XFS_VFSTOM(vp->v_vfsp);
	if (ip->i_d.di_flags & XFS_DIFLAG_REALTIME) {
		bp->b_edev = mp->m_rtdev;
	} else {
		bp->b_edev = mp->m_dev;
	}

	/*
 	 * Perform I/O operation.
	 */
	error = biophysio(xfs_diostrat, bp, bp->b_edev, rw, 
		(daddr_t)BTOBB(uiop->uio_offset), uiop);

	/*
 	 * Free local buf structure.
 	 */
	bp->b_flags = 0;
#ifdef SIM
	bp->b_un.b_addr = 0;
#endif
	putphysbuf(bp);

	return( error );
}
