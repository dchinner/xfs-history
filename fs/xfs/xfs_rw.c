

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
#ifdef SIM
#undef _KERNEL
#endif
#include <sys/cmn_err.h>
#include <sys/debug.h>
#include <sys/errno.h>
#include <sys/fcntl.h>
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
#include "xfs_types.h"
#include "xfs_inum.h"
#include "xfs_log.h"
#include "xfs_trans.h"
#include "xfs_sb.h"
#include "xfs_mount.h"
#include "xfs_alloc_btree.h"
#include "xfs_ialloc.h"
#include "xfs_ag.h"
#include "xfs_bmap_btree.h"
#include "xfs_bmap.h"
#include "xfs_btree.h"
#include "xfs_dinode.h"
#include "xfs_inode_item.h"
#include "xfs_inode.h"

#ifdef SIM
#include "sim.h"
#endif

/*
 * This lock is used by xfs_strat_write().
 */
lock_t	xfs_strat_lock;

STATIC void
xfs_zero_bp(buf_t	*bp,
	    int		data_offset,
	    int		data_len);

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
xfs_next_bmap(xfs_mount_t	*mp,
	      xfs_bmbt_irec_t	*imapp,
	      struct bmapval	*bmapp,
	      int		iosize,
	      xfs_fsblock_t	ioalign,
	      xfs_fsblock_t	last_offset,
	      xfs_fsblock_t	req_offset)
{
	int		extra_blocks;
	xfs_sb_t	*sbp = &mp->m_sb;
	xfs_fsblock_t	size_diff;
	xfs_fsblock_t	ext_offset;

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
			if (req_offset < (last_offset + iosize)) {
				/*
				 * This request overlaps the buffer
				 * we used for the last request.  Just
				 * get that buffer again.
				 */
				ext_offset = last_offset -
					     imapp->br_startoff;
				bmapp->offset = last_offset;
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
	if (imapp->br_startblock != NULLSTARTBLOCK) {
		bmapp->bn = imapp->br_startblock + ext_offset;
		bmapp->eof = 0;
	} else {
		bmapp->bn = -1;
		bmapp->eof = BMAP_HOLE;
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
	bmapp->bsize = xfs_fsb_to_b(sbp, bmapp->length);
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
xfs_retrieved(int	available,
	      off_t	offset,
	      int	count,
	      uint	*total_retrieved,
	      __int64_t	isize)
{
	int		retrieved;
	__int64_t	file_bytes_left;
	

	if ((available + *total_retrieved) > count) {
		/*
		 * This buffer will return more bytes
		 * than we asked for.  Trim retrieved
		 * _bytes so we can set
		 * bmapp->pbsize correctly.
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


void
xfs_iomap_read(xfs_inode_t	*ip,
	       off_t		offset,
	       size_t		count,
	       struct bmapval	*bmapp,
	       int		*nbmaps)
{
	xfs_fsblock_t	offset_fsb;
	xfs_fsblock_t	ioalign;
	xfs_fsblock_t	last_offset;
	xfs_fsblock_t	last_required_offset;
	xfs_fsblock_t	last_fsb;
	xfs_fsblock_t	next_offset;
	off_t		last_page;
	off_t		offset_page;
	unsigned int	request_size;
	int		nimaps;
	unsigned int	iosize;
	unsigned int	retrieved_bytes;
	unsigned int	total_retrieved_bytes;
	int		filled_bmaps;
	int		read_aheads;
	int		x;
	xfs_mount_t	*mp;
	xfs_sb_t	*sbp;
	struct bmapval	*curr_bmapp;
	struct bmapval	*next_bmapp;
	struct bmapval	*last_bmapp;
	struct bmapval	*first_read_ahead_bmapp;
	struct bmapval	*next_read_ahead_bmapp;
	xfs_bmbt_irec_t	*curr_imapp;
	xfs_bmbt_irec_t	*last_imapp;
#define	XFS_READ_IMAPS	XFS_BMAP_MAX_NMAP
	xfs_bmbt_irec_t	imap[XFS_READ_IMAPS];

	mp = XFS_VFSTOM(XFS_ITOV(ip)->v_vfsp);
	sbp = &mp->m_sb;
	offset_fsb = xfs_b_to_fsbt(sbp, offset);
	request_size = count;
	nimaps = XFS_READ_IMAPS;
	(void)xfs_bmapi(NULL, ip, offset_fsb,
			xfs_b_to_fsb(sbp, ip->i_d.di_size) - offset_fsb,
			XFS_BMAPI_ENTIRE, NULLFSBLOCK, 0, imap,
			&nimaps, NULL);


	if ((offset == ip->i_next_offset) &&
	    (request_size <= ip->i_last_req_sz)) {
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
		if (xfs_b_to_fsb(sbp, request_size) < mp->m_readio_blocks) {
			/*
			 * The request is smaller than our
			 * minimum I/O size, so default to
			 * the minimum.  For these size requests
			 * we always want to align the requests
			 * to XFS_READ_SIZE boundaries as well.
			 */
			iosize = mp->m_readio_blocks;
			ioalign = XFS_READIO_ALIGN(mp, offset);
			ioalign = xfs_b_to_fsbt(sbp, ioalign);
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
			offset_page = ctob(btoct(xfs_fsb_to_b(sbp,
							      offset_fsb)));
			last_page = ctob(btoc(offset + count));
			last_fsb = xfs_b_to_fsb(sbp, last_page);
			iosize = last_fsb - xfs_b_to_fsbt(sbp, offset_page);
			ioalign = xfs_b_to_fsb(sbp, offset_page);
		}
		last_offset = -1;
	}

	/*
	 * Now we've got the I/O size and the last offset,
	 * so start figuring out how to align our
	 * buffers.
	 */
	xfs_next_bmap(mp, imap, bmapp, iosize, ioalign,
		      last_offset, offset_fsb);
	ASSERT((bmapp->length > 0) &&
	       (offset >= xfs_fsb_to_b(sbp, bmapp->offset)));
	
	if ((nimaps == 1) &&
	    (xfs_fsb_to_b(sbp, bmapp->offset + bmapp->length) >=
	     ip->i_d.di_size)) {
		bmapp->eof |= BMAP_EOF;
	}

	bmapp->pboff = offset - xfs_fsb_to_b(sbp, bmapp->offset);
	retrieved_bytes = bmapp->bsize - bmapp->pboff;
	total_retrieved_bytes = 0;
	bmapp->pbsize = xfs_retrieved(retrieved_bytes, offset, count,
				      &total_retrieved_bytes,
				      ip->i_d.di_size);

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
			if (next_offset <
			    (curr_imapp->br_startoff +
			     curr_imapp->br_blockcount)) {
				xfs_next_bmap(mp, curr_imapp,
					 next_bmapp, iosize, -1,
					 curr_bmapp->offset,
					 next_offset);
			} else {
				curr_imapp++;
				if (curr_imapp <= last_imapp) {
					xfs_next_bmap(mp,
					    curr_imapp, next_bmapp,
					    iosize, -1,
					    curr_bmapp->offset,
					    next_offset);
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
						      ip->i_d.di_size);
			}
			
			if ((curr_imapp == last_imapp) &&
			    (xfs_fsb_to_b(sbp, curr_bmapp->offset +
					  curr_bmapp->length) >=
			     ip->i_d.di_size)) {
				curr_bmapp->eof |= BMAP_EOF;
			}

 			/*
			 * Keep track of the offset of the last buffer
			 * needed to satisfy the I/O request.  This will
			 * be used for i_io_offset later.  Also record
			 * the first bmapp used to track a read ahead.
			 */
			if (xfs_fsb_to_b(sbp, curr_bmapp->offset) <
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
			read_aheads = curr_bmapp - first_read_ahead_bmapp;
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
	if (request_size > ip->i_last_req_sz) {
		/*
		 * Record the "last request size" for the file.
		 * We don't let it shrink so that big requests
		 * that are not satisfied in one call here still
		 * record the full request size (not the smaller
		 * one that comes in to finish mapping the request).
		 */
		ip->i_last_req_sz = request_size;
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
		curr_bmapp->offset = xfs_fsb_to_bb(sbp, curr_bmapp->offset);
		curr_bmapp->length = xfs_fsb_to_bb(sbp, curr_bmapp->length);
		if (curr_bmapp->bn != -1) {
			curr_bmapp->bn =
				xfs_fsb_to_daddr(sbp, curr_bmapp->bn);
		}
	}
	return;
}				
			
		
int
xfs_read_file(vnode_t	*vp,
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
		xfs_ilock(ip, XFS_ILOCK_SHARED);

		/*
		 * We've fallen off the end of the file, so
		 * just return with what we've done so far.
		 */
		if (uiop->uio_offset >= ip->i_d.di_size) {
			xfs_iunlock(ip, XFS_ILOCK_SHARED);
			break;
		}
 
		nbmaps = XFS_READ_BMAPS;
		xfs_iomap_read(ip, uiop->uio_offset, uiop->uio_resid,
			       bmaps, &nbmaps);

		xfs_iunlock(ip, XFS_ILOCK_SHARED);

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

			bp = read_chunk(vp, bmapp, read_bmaps, 0, credp);

			if (bp->b_flags & B_ERROR) {
				error = geterror(bp);
				break;
			} else if (bp->b_resid != 0) {
				buffer_bytes_ok = 0;
				break;
			} else {
				buffer_bytes_ok = 1;
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
 * This is a stub.
 */
int
xfs_read(vnode_t	*vp,
	 uio_t		*uiop,
	 int		ioflag,
	 cred_t		*credp)
{
	xfs_inode_t	*ip;
	int		type;
	off_t		offset;
	size_t		count;
	int		error;


	ip = XFS_VTOI(vp);
	ASSERT(ismrlocked(&ip->i_lock, MR_ACCESS) != 0);

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
		return EINVAL;
	if (count <= 0)
		return 0;

	switch (type) {

	case IFREG:
		if (ioflag & IO_DIRECT) {
			error = EINVAL;
			break;
		}

		/*
		 * Not ready for in-line files yet.
		 */
		ASSERT((ip->i_d.di_format == XFS_DINODE_FMT_EXTENTS) ||
		       (ip->i_d.di_format == XFS_DINODE_FMT_BTREE));


		xfs_read_file(vp, uiop, ioflag, credp);

		break;

	default:
		ASSERT(0);
		error = EINVAL;
	}
	return error;
}


/*
 * Map the given I/O size and I/O alignment over the given extent.
 * If we're at the end of the file, make sure we extend out to the
 * next m_writeio_blocks boundary.  Otherwise make sure that we
 * are confined to the given extent.
 */
STATIC void
xfs_write_bmap(xfs_mount_t	*mp,
	       xfs_bmbt_irec_t	*imapp,
	       struct bmapval	*bmapp,
	       int		iosize,
	       xfs_fsblock_t	ioalign,
	       __int64_t	isize)
{
	int		extra_blocks;
	xfs_sb_t	*sbp = &mp->m_sb;
	xfs_fsblock_t	size_diff;
	xfs_fsblock_t	ext_offset;
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
	if (imapp->br_startblock != NULLSTARTBLOCK) {
		bmapp->bn = imapp->br_startblock + ext_offset;
		bmapp->eof = 0;
	} else {
		bmapp->bn = -1;
		bmapp->eof = BMAP_HOLE;
	}
	bmapp->length = iosize;

	/*
	 * If the iosize from our offset extends beyond the end of
	 * the extent and we're not at the end of the file, then trim
	 * down length to match that of the extent.
	 */
	extra_blocks = (int)((bmapp->offset + bmapp->length) -
		       (imapp->br_startoff + imapp->br_blockcount));
	last_imap_byte = xfs_fsb_to_b(sbp, imapp->br_startoff +
				      imapp->br_blockcount);
	if ((extra_blocks > 0) && (last_imap_byte < isize)) {
		bmapp->length -= extra_blocks;
		ASSERT(bmapp->length > 0);
	}
	bmapp->bsize = xfs_fsb_to_b(sbp, bmapp->length);
}


/*
 * This routine is called to handle zeroing the buffer which overlaps
 * the end of the file if the user seeks and writes beyond the EOF.
 * This is necessary because those pages in the buffer which used
 * to be beyond EOF and therefore invalid become valid when isize
 * is extended beyond them.
 */
STATIC int
xfs_zero_eof_bp(xfs_inode_t	*ip,
		off_t		offset,
		__int64_t	isize,
		cred_t		*credp)
{
	xfs_fsblock_t	last_byte_fsb;
	off_t		ioalign;
	off_t		bmap_end;
	buf_t		*bp;
	xfs_mount_t	*mp;
	xfs_sb_t	*sbp;
	int		zero_len;
	int		zero_offset;
	int		iosize;
	int		nimaps;
	int		nbmaps;
	xfs_bmbt_irec_t	imap;
	struct bmapval	bmap;

	ASSERT(ismrlocked(&ip->i_lock, MR_UPDATE) != 0);
	ASSERT(offset > isize);

	mp = XFS_VFSTOM(XFS_ITOV(ip)->v_vfsp);
	sbp = &mp->m_sb;
	iosize = mp->m_writeio_blocks;
	ioalign = XFS_WRITEIO_ALIGN(mp, isize);

	if (ioalign == isize) {
		/*
		 * The buffer containing the last byte of the file
		 * would end at that last byte, so there is nothing
		 * to zero.
		 */
		return 0;
	}

	/*
	 * Map the space underlying the end of the file.
	 */
	last_byte_fsb = xfs_b_to_fsbt(sbp, isize - 1);
	nimaps = 1;
	(void) xfs_bmapi(NULL, ip, last_byte_fsb, 1, XFS_BMAPI_ENTIRE,
			 NULLFSBLOCK, 0, &imap, &nimaps, NULL);

	/*
	 * Map a buffer over the space at the end of the file.
	 * Any buffer overlapping the EOF should match this one
	 * exactly.
	 */
	xfs_write_bmap(mp, &imap, &bmap, iosize, ioalign, isize);
	bmap.eof |= BMAP_EOF;

	xfs_iunlock(ip, XFS_ILOCK_EXCL);

	bmap.offset = xfs_fsb_to_bb(sbp, bmap.offset);
	bmap.length = xfs_fsb_to_bb(sbp, bmap.length);
	if (bmap.bn != -1) {
		bmap.bn = xfs_fsb_to_daddr(sbp, bmap.bn);
	}

	bp = read_chunk(XFS_ITOV(ip), &bmap, 1, 0, credp);

	/*
	 * Zero the part of the buffer from isize to the end of
	 * the buffer or to where the new write will be starting.
	 */
	zero_offset = (int)(isize - BBTOB(bmap.offset));
	bmap_end = BBTOB(bmap.offset + bmap.length);
	if (bmap_end < offset) {
		zero_len = bmap_end - isize;
	} else {
		zero_len = offset - isize;
	}

#ifdef _VCE_AVOIDANCE
	if (vce_avoidance) {
		extern void biozero(buf_t *, uint, int);

		biozero(bp, zero_offset, zero_len);
	} else
#endif
	{
#ifndef SIM	       
		(void) bp_mapin(bp);
		bzero(bp->b_un.b_addr + zero_offset, zero_len);
#else
		xfs_zero_bp(bp, zero_offset, zero_len);
#endif /* SIM */
	}

	/*
	 * We know that there are underlying blocks from the
	 * beginning of the buffer to the end of the file, so
	 * record that fact here.  Later user's of this same
	 * buffer will have to reset this value as they change
	 * the underlying file.
	 */
	bp->b_valid = (int)(isize - BBTOB(bmap.offset));

	bdwrite(bp);

	return 1;
}

STATIC void
xfs_iomap_write(xfs_inode_t	*ip,
		off_t		offset,
		size_t		count,
		struct bmapval	*bmapp,
		int		*nbmaps)
{
	xfs_fsblock_t	offset_fsb;
	xfs_fsblock_t	ioalign;
	xfs_fsblock_t	next_offset_fsb;
	xfs_fsblock_t	last_fsb;
	xfs_fsblock_t	bmap_end_fsb;
	off_t		next_offset;
	__int64_t	isize;
	xfs_extlen_t	blocks;
	int		nimaps;
	unsigned int	iosize;
	unsigned int	writing_bytes;
	int		filled_bmaps;
	int		x;
	size_t		count_remaining;
	xfs_mount_t	*mp;
	xfs_sb_t	*sbp;
	struct bmapval	*curr_bmapp;
	struct bmapval	*next_bmapp;
	struct bmapval	*last_bmapp;
	xfs_bmbt_irec_t	*curr_imapp;
	xfs_bmbt_irec_t	*last_imapp;
#define	XFS_WRITE_IMAPS	XFS_BMAP_MAX_NMAP
	xfs_bmbt_irec_t	imap[XFS_WRITE_IMAPS];

	ASSERT(ismrlocked(&ip->i_lock, MR_UPDATE) != 0);

	mp = XFS_VFSTOM(XFS_ITOV(ip)->v_vfsp);
	sbp = &mp->m_sb;
	isize = ip->i_d.di_size;
	offset_fsb = xfs_b_to_fsbt(sbp, offset);
	nimaps = XFS_WRITE_IMAPS;
	last_fsb = xfs_b_to_fsb(sbp, offset + count);
	blocks = (xfs_extlen_t)(last_fsb - offset_fsb);
	(void) xfs_bmapi(NULL, ip, offset_fsb, blocks,
			 XFS_BMAPI_DELAY | XFS_BMAPI_WRITE |
			 XFS_BMAPI_ENTIRE, NULLFSBLOCK, blocks, imap,
			 &nimaps, NULL);
	
	iosize = mp->m_writeio_blocks;
	ioalign = XFS_WRITEIO_ALIGN(mp, offset);
	ioalign = xfs_b_to_fsbt(sbp, ioalign);

	/*
	 * Now map our desired I/O size and alignment over the
	 * extents returned by xfs_bmapi().
	 */
	xfs_write_bmap(mp, imap, bmapp, iosize, ioalign, isize);
	ASSERT((bmapp->length > 0) &&
	       (offset >= xfs_fsb_to_b(sbp, bmapp->offset)));

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
	    (xfs_fsb_to_b(sbp, bmap_end_fsb) >= isize)) {
		bmapp->eof |= BMAP_EOF;
	}
	bmapp->pboff = offset - xfs_fsb_to_b(sbp, bmapp->offset);
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
				 * a writio_blocks boundary.  Thus,
				 * we just start where the last one
				 * left off.
				 */
				ASSERT((xfs_fsb_to_b(sbp, next_offset_fsb) &
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
					 */
					ioalign = xfs_fsb_to_b(sbp,
					                    next_offset_fsb);
					ioalign = XFS_WRITEIO_ALIGN(mp,
								    ioalign);
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
			next_offset = xfs_fsb_to_b(sbp, next_offset_fsb);
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
			    (xfs_fsb_to_b(sbp, bmap_end_fsb) >= isize)) {
				curr_bmapp->eof |= BMAP_EOF;
			}					
		}
	}
	*nbmaps = filled_bmaps;
	for (x = 0; x < filled_bmaps; x++) {
		curr_bmapp = &bmapp[x];
		curr_bmapp->offset = xfs_fsb_to_bb(sbp, curr_bmapp->offset);
		curr_bmapp->length = xfs_fsb_to_bb(sbp, curr_bmapp->length);
		if (curr_bmapp->bn != -1) {
			curr_bmapp->bn =
				xfs_fsb_to_daddr(sbp, curr_bmapp->bn);
		}
	}
}


void
xfs_write_file(vnode_t	*vp,
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
	int		delalloc;
	int		eof_zeroed;
	uint		valid;
	uint		pbcount;
	off_t		bp_offset;
	__int64_t	isize;

	ip = XFS_VTOI(vp);
	error = 0;
	buffer_bytes_ok = 0;
	eof_zeroed = 0;
	/*
	 * Loop until uiop->uio_resid, which is the number of bytes the
	 * caller has requested to write, goes to 0 or we get an error.
	 * Each call to xfs_iomap_write() tries to map as much of the
	 * request as it can in mp->m_writeio_blks sized chunks.
	 */
	do {
		xfs_ilock(ip, XFS_ILOCK_EXCL);
		isize = ip->i_d.di_size;

		/*
		 * If we've seeked passed the EOF to do this write,
		 * then we need to make sure that any buffer overlapping
		 * the EOF is zeroed beyond the EOF.
		 * xfs_zero_eof_bp() unlocks the inode lock, so make
		 * sure to reacquire it after the call.
		 */
		if (!eof_zeroed &&
		    (uiop->uio_offset > isize) &&
		    (isize != 0)) {
			if (xfs_zero_eof_bp(ip, uiop->uio_offset,
					    isize, credp) != 0) {
				xfs_ilock(ip, XFS_ILOCK_EXCL);
			}
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
			if (bmapp->eof & BMAP_HOLE) {
				delalloc = B_DELALLOC;
			} else {
				delalloc = 0;
			}
			if ((bmapp->pbsize != bmapp->bsize) &&
			    !((bmapp->pboff == 0) &&
			      (uiop->uio_offset == isize))) {
				bp = read_chunk(vp, bmapp, 1, delalloc,
						credp);

				pbcount = bmapp->pboff + bmapp->pbsize;
				bp_offset = BBTOB(bmapp->offset);
				if ((bp_offset + bmapp->bsize) < isize) {
					/*
					 * The entire buffer is interior to
					 * the file, so the whole buffer
					 * is valid.
					 */
					valid = bmapp->bsize;
				} else if ((bp_offset + pbcount) < isize)
					/*
					 * The buffer extends past the EOF
					 * but we're not writing past EOF
					 * so up to EOF is valid.
					 */
					valid = isize - bp_offset;
				else {
					/*
					 * The buffer extends past the EOF
					 * and we're writing past EOF so
					 * up to where we finish writing
					 * is valid.
					 */
					valid = pbcount;
				}
			} else {
				bp = alloc_chunk(vp, bmapp, delalloc, credp);
				/*
				 * We're either writing the entire buffer
				 * or up to the EOF, so up to where we
				 * finish writing is valid.
				 */
				valid = bmapp->pboff + bmapp->pbsize;
			}

			/*
			 * XXXajs
			 * The error handling below needs more work.
			 */
			if (bp->b_flags & B_ERROR) {
				error = geterror(bp);
				brelse(bp);
				break;
			}

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
			 * Set the number of bytes in the buffer which
			 * are known to have been initialized.  This will
			 * be the entire buffer except for when we have
			 * a buffer which extends beyond the EOF.
			 */
			bp->b_valid = valid;

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
}


/*
 * xfs_write
 *
 * This is a stub.
 */
int
xfs_write(vnode_t	*vp,
	  uio_t		*uiop,
	  int		ioflag,
	  cred_t	*credp)
{
	return 0;
}


/*
 * Set up rbp so that it points to the memory attached to bp
 * from rbp_offset from the start of bp for rbp_len bytes.
 */
STATIC void
xfs_overlap_bp(buf_t	*bp,
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
xfs_zero_bp(buf_t	*bp,
	    int		data_offset,
	    int		data_len)
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
 */
STATIC void
xfs_strat_read(vnode_t	*vp,
	       buf_t	*bp)
{
	xfs_fsblock_t	offset_fsb;
	xfs_fsblock_t   map_start_fsb;
	xfs_fsblock_t	imap_offset;
	xfs_extlen_t	count_fsb;
	xfs_extlen_t	imap_blocks;
	int		x;
	caddr_t		datap;
	buf_t		*rbp;
	xfs_mount_t	*mp;
	xfs_sb_t	*sbp;
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
	sbp = &mp->m_sb;
	offset_fsb = xfs_bb_to_fsbt(sbp, bp->b_offset);
	count_fsb = xfs_b_to_fsb(sbp, bp->b_bcount);
	map_start_fsb = offset_fsb;
	xfs_ilock(ip, XFS_ILOCK_SHARED);
	while (count_fsb != 0) {
		nimaps = XFS_STRAT_READ_IMAPS;
		(void) xfs_bmapi(NULL, ip, map_start_fsb, count_fsb, 0,
				 NULLFSBLOCK, 0, imap, &nimaps, NULL);
		ASSERT(nimaps >= 1);
		
		for (x = 0; x < nimaps; x++) {
			imap_offset = imap[x].br_startoff;
			ASSERT(xfs_fsb_to_bb(sbp, imap_offset) ==
			       map_start_fsb);
			imap_blocks = imap[x].br_blockcount;
			ASSERT(imap_blocks <= count_fsb);
			if (imap[x].br_startblock == NULLSTARTBLOCK) {
				/*
				 * This is either a hole or a delayed
				 * alloc extent.  Either way, just fill
				 * it with zeroes.
				 */
#ifndef SIM
				datap = bp_mapin(bp);
				datap += xfs_fsb_to_b(sbp, imap_offset -
						      offset_fsb);
				data_bytes = xfs_fsb_to_b(sbp, imap_blocks);

				bzero(datap, data_bytes);
#else /* SIM */
				ASSERT(bp->b_flags & B_PAGEIO);
				data_offset = xfs_fsb_to_b(sbp, imap_offset -
							   offset_fsb);
				data_len = xfs_fsb_to_b(sbp, imap_blocks);
				xfs_zero_bp(bp, data_offset, data_len);
#endif /* SIM */
			} else {
				/*
				 * The extent really exists on disk, so
				 * read it in.
				 */
				rbp = getrbuf(KM_SLEEP);
				data_offset = xfs_fsb_to_b(sbp, imap_offset -
							  offset_fsb);
				data_len = xfs_fsb_to_b(sbp, imap_blocks);
				xfs_overlap_bp(bp, rbp, data_offset,
					       data_len);
				
				rbp->b_flags |= B_READ;
				rbp->b_flags &= ~B_ASYNC;

				bdstrat(bmajor(rbp->b_edev), rbp);
				iowait(rbp);

				if (rbp->b_flags & B_ERROR) {
					bp->b_flags |= B_ERROR;
					bp->b_error = rbp->b_error;
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



/*
 * This is the completion routine for the heap-allocated buffers
 * used to write out a buffer which becomes fragmented during
 * xfs_strat_write().  It must coordinate with xfs_strat_write()
 * to properly mark the lead buffer as done when necessary and
 * to free the subordinate buffer.
 */
STATIC void
xfs_strat_write_relse(buf_t	*rbp)
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
			leader->b_error = rbp->b_error;
		}
		leader->b_flags |= B_DONE;
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
			leader->b_error = rbp->b_error;
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
xfs_strat_write(vnode_t	*vp,
		buf_t	*bp)
{
	xfs_fsblock_t	offset_fsb;
	xfs_fsblock_t   map_start_fsb;
	xfs_fsblock_t	imap_offset;
	xfs_fsblock_t	first_block;
	xfs_extlen_t	count_fsb;
	xfs_extlen_t	imap_blocks;
	int		x;
	caddr_t		datap;
	buf_t		*rbp;
	xfs_mount_t	*mp;
	xfs_sb_t	*sbp;
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
	mp = XFS_VFSTOM(vp->v_vfsp);
	sbp = &mp->m_sb;
	ASSERT((bp->b_blkno == -1) ||
	       (xfs_b_to_fsb(sbp, bp->b_bcount) !=
		 xfs_b_to_fsb(sbp, bp->b_valid)));
	offset_fsb = xfs_bb_to_fsbt(sbp, bp->b_offset);
	/*
	 * bp->b_valid tells us how many bytes are in the buffer which
	 * need to be written out.  It will be smaller than b_bcount
	 * only when the buffer extends beyond the EOF.
	 *
	 * When I allow seeks beyond the EOF it will let us deal with
	 * the old EOF-overlapping buffers as well.
	 */
	ASSERT(bp->b_valid > 0);
	count_fsb = xfs_b_to_fsb(sbp, bp->b_valid);
	map_start_fsb = offset_fsb;
	bp->b_flags |= B_STALE;
	while (count_fsb != 0) {
		xfs_ilock(ip, XFS_ILOCK_EXCL);
		/*
		 * Set up a transaction with which to allocate the
		 * backing store for the file.
		 */
		tp = xfs_trans_alloc(mp, XFS_TRANS_FILE_WRITE);
		error = xfs_trans_reserve(tp, 0, BBTOB(128), 0, 0);
		ASSERT(error == 0);
		xfs_trans_ijoin(tp, ip, XFS_ILOCK_EXCL);
		xfs_trans_ihold(tp, ip);

		/*
		 * Allocate the backing store for the file.
		 */
		free_list.xbf_first = NULL;
		free_list.xbf_count = 0;
		nimaps = XFS_STRAT_WRITE_IMAPS;
		first_block = xfs_bmapi(tp, ip, map_start_fsb, count_fsb,
					XFS_BMAPI_WRITE, NULLFSBLOCK, 1,
					imap, &nimaps, &free_list);
		ASSERT(nimaps > 0);
		xfs_bmap_finish(&tp, &free_list, first_block);

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
			bp->b_blkno = xfs_fsb_to_daddr(sbp,
						     imap[0].br_startblock);
			bp->b_bcount = xfs_fsb_to_b(sbp, count_fsb);
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
			imap_offset = imapp->br_startoff;
			rbp_offset = xfs_fsb_to_b(sbp, imap_offset -
						 offset_fsb);
			imap_blocks = imapp->br_blockcount;
			rbp_len = xfs_fsb_to_b(sbp, imap_blocks);
			xfs_overlap_bp(bp, rbp, rbp_offset, rbp_len);
			
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
xfs_strategy(vnode_t	*vp,
	     buf_t	*bp)
{
	xfs_mount_t	*mp;
	xfs_sb_t	*sbp;

	mp = XFS_VFSTOM(vp->v_vfsp);
	sbp = &mp->m_sb;

	/*
	 * If this is just a buffer whose underlying disk space
	 * is already allocated, then just do the requested I/O.
	 * b_valid equal to 0 indicates that the buffer is being read
	 * in by read, in which case there must be proper underlying
	 * disk space.  Otherwise, b_valid is the number of bytes
	 * which we know have underlying disk space so we only want
	 * to do the whole buffer if the buffer size matches that.
	 */
	if ((bp->b_blkno > 0) &&
	    ((bp->b_valid == 0) ||
	     (xfs_b_to_fsb(sbp, bp->b_bcount) ==
	      xfs_b_to_fsb(sbp, bp->b_valid)))) {
		ASSERT((bp->b_flags & B_READ) || (bp->b_valid > 0));
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
	 * some underlying disk space.  In the real version this will
	 * need to queue the buffer for an xfsd if it is an ASYNC write.
	 * For now we'll just do the allocation regardless.
	 */
	if (bp->b_flags & B_ASYNC) {
		/*
		 * Here's where we'll queue it.
		 */
		xfs_strat_write(vp, bp);
	} else {
		xfs_strat_write(vp, bp);
	}
}
