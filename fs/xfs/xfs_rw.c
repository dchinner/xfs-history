#ident "$Revision: 1.137 $"

#ifdef SIM
#define _KERNEL 1
#endif
#include <sys/param.h>
#include <sys/buf.h>
#include <sys/uio.h>
#include <sys/vfs.h>
#include <sys/vnode.h>
#include <sys/cred.h>
#include <sys/sysmacros.h>
#include <sys/pfdat.h>
#include <sys/proc.h>
#include <sys/user.h>
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
#include <sys/file.h>
#include <sys/dmi.h>
#include <sys/dmi_kern.h>
#include <sys/region.h>
#include <sys/runq.h>
#include <sys/schedctl.h>
#include <sys/atomic_ops.h>
#include <sys/ktrace.h>
#include <sys/sysinfo.h>
#include <sys/ksa.h>
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
#include "xfs_itable.h"
#include "xfs_btree.h"
#include "xfs_alloc.h"
#include "xfs_bmap.h"
#include "xfs_ialloc.h"
#include "xfs_attr_sf.h"
#include "xfs_dir_sf.h"
#include "xfs_dinode.h"
#include "xfs_inode_item.h"
#include "xfs_inode.h"
#include "xfs_error.h"
#include "xfs_rw.h"

#ifdef SIM
#include "sim.h"
#endif


/*
 * This lock is used by xfs_strat_write().
 * The xfs_strat_lock is initialized in xfs_init().
 */
mutex_t	xfs_strat_lock;

/*
 * Variables for coordination with the xfsd daemons.
 * The xfsd_lock and xfsd_wait variables are initialized
 * in xfs_init();
 */
static int	xfsd_count;
static buf_t	*xfsd_list;
static int	xfsd_bufcount;
mutex_t		xfsd_lock;
sv_t		xfsd_wait;

/*
 * Zone allocator for arrays of xfs_bmbt_irec_t used
 * for calls to xfs_bmapi().
 */
zone_t		*xfs_irec_zone;

/*
 * Zone allocator for arrays of bmapval structures
 * used in calls to xfs_iomap_XXX routines.
 */
zone_t		*xfs_bmap_zone;

/*
 * Zone allocator for local variables in xfs_strat_write().
 * This routine is a real problem so we take extreme measures.
 */
zone_t		*xfs_strat_write_zone;

/*
 * Zone allocator for xfs_gap_t structures.
 */
zone_t		*xfs_gap_zone;

/*
 * Global trace buffer for xfs_strat_write() tracing.
 */
ktrace_t	*xfs_strat_trace_buf;
#ifndef DEBUG 
#define	xfs_strat_write_bp_trace(tag, ip, bp)
#define	xfs_strat_write_subbp_trace(tag, ip, bp, rbp, loff, lcnt, lblk)
#endif	/* !DEBUG */

STATIC int
xfs_zero_last_block(
	xfs_inode_t	*ip,
	off_t		offset,
	xfs_fsize_t	isize,
	cred_t		*credp);

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
#define	xfs_check_rbp(ip,bp,rbp,locked)
#define	xfs_check_bp(ip,bp)
#define	xfs_check_gap_list(ip)

#else /* DEBUG */

STATIC void
xfs_strat_write_check(
	xfs_inode_t	*ip,
	xfs_fileoff_t	offset_fsb,
	xfs_filblks_t	buf_fsb,
	xfs_bmbt_irec_t	*imap,
	int		imap_count);

STATIC void
xfs_check_rbp(
	xfs_inode_t	*ip,
	buf_t		*bp,
	buf_t		*rbp,
	int		locked);

STATIC void
xfs_check_bp(
	xfs_inode_t	*ip,
	buf_t		*bp);

STATIC void
xfs_check_gap_list(
	xfs_inode_t	*ip);

#endif /* DEBUG */		      

STATIC int
xfs_build_gap_list(
	xfs_inode_t	*ip,
	off_t		offset,
	size_t		count);

STATIC void
xfs_free_gap_list(
	xfs_inode_t	*ip);

STATIC void
xfs_cmp_gap_list_and_zero(
	xfs_inode_t	*ip,
	buf_t		*bp);

STATIC void
xfs_delete_gap_list(
	xfs_inode_t	*ip,
	xfs_fileoff_t	offset_fsb,
	xfs_extlen_t	count_fsb);

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
xfs_io_is_guaranteed(
	xfs_inode_t	*,
	stream_id_t	*);

extern int
grio_monitor_io_start( 
	stream_id_t *, 
	__int64_t);

extern int
grio_monitor_io_end(
	stream_id_t *,
	int);
	

extern void xfs_error(
	xfs_mount_t *,
	int);

STATIC void
xfs_delalloc_cleanup(
	xfs_inode_t	*ip,
	xfs_fileoff_t	start_fsb,
	xfs_filblks_t	count_fsb);

/*
 * Round the given file offset down to the nearest read/write
 * size boundary.
 */
#define	XFS_READIO_ALIGN(mp,off)	(((off) >> mp->m_readio_log) \
					        << mp->m_readio_log)
#define	XFS_WRITEIO_ALIGN(mp,off)	(((off) >> mp->m_writeio_log) \
					        << mp->m_writeio_log)

#ifndef DEBUG
#define	xfs_rw_enter_trace(tag, ip, uiop, ioflags)
#define	xfs_iomap_enter_trace(tag, ip, offset, count);
#define	xfs_iomap_map_trace(tag, ip, offset, count, bmapp, imapp)
#else
/*
 * Trace routine for the read/write path.  This is the routine entry trace.
 */
void
xfs_rw_enter_trace(
	int		tag,	     
	xfs_inode_t	*ip,
	uio_t		*uiop,
	int		ioflags)
{
	if (ip->i_rwtrace == NULL) {
		return;
	}

	ktrace_enter(ip->i_rwtrace,
		     (void*)((unsigned long)tag),
		     (void*)ip, 
		     (void*)((ip->i_d.di_size >> 32) & 0xffffffff),
		     (void*)(ip->i_d.di_size & 0xffffffff),
		     (void*)(((__uint64_t)uiop->uio_offset >> 32) & 0xffffffff),
		     (void*)(uiop->uio_offset & 0xffffffff),
		     (void*)uiop->uio_resid,
		     (void*)((unsigned long)ioflags),
		     (void*)((ip->i_next_offset >> 32) & 0xffffffff),
		     (void*)(ip->i_next_offset & 0xffffffff),
		     (void*)((unsigned long)((ip->i_io_offset >> 32) & 0xffffffff)),
		     (void*)(ip->i_io_offset & 0xffffffff),
		     (void*)((unsigned long)(ip->i_io_size)),
		     (void*)((unsigned long)(ip->i_last_req_sz)),
		     (void*)((unsigned long)((ip->i_new_size >> 32) & 0xffffffff)),
		     (void*)(ip->i_new_size & 0xffffffff));
}

void
xfs_iomap_enter_trace(
	int		tag,
	xfs_inode_t	*ip,
	off_t		offset,
	int		count)
{
	if (ip->i_rwtrace == NULL) {
		return;
	}

	ktrace_enter(ip->i_rwtrace,
		     (void*)((unsigned long)tag),
		     (void*)ip, 
		     (void*)((ip->i_d.di_size >> 32) & 0xffffffff),
		     (void*)(ip->i_d.di_size & 0xffffffff),
		     (void*)(((__uint64_t)offset >> 32) & 0xffffffff),
		     (void*)(offset & 0xffffffff),
		     (void*)((unsigned long)count),
		     (void*)((ip->i_next_offset >> 32) & 0xffffffff),
		     (void*)(ip->i_next_offset & 0xffffffff),
		     (void*)((ip->i_io_offset >> 32) & 0xffffffff),
		     (void*)(ip->i_io_offset & 0xffffffff),
		     (void*)((unsigned long)(ip->i_io_size)),
		     (void*)((unsigned long)(ip->i_last_req_sz)),
		     (void*)((ip->i_new_size >> 32) & 0xffffffff),
		     (void*)(ip->i_new_size & 0xffffffff),
		     (void*)0);
}

void
xfs_iomap_map_trace(
	int		tag,	     
	xfs_inode_t	*ip,
	off_t		offset,
	int		count,
	struct bmapval	*bmapp,
	xfs_bmbt_irec_t	*imapp)    
{
	if (ip->i_rwtrace == NULL) {
		return;
	}

	ktrace_enter(ip->i_rwtrace,
		     (void*)((unsigned long)tag),
		     (void*)ip, 
		     (void*)((ip->i_d.di_size >> 32) & 0xffffffff),
		     (void*)(ip->i_d.di_size & 0xffffffff),
		     (void*)(((__uint64_t)offset >> 32) & 0xffffffff),
		     (void*)(offset & 0xffffffff),
		     (void*)((unsigned long)count),
		     (void*)((bmapp->offset >> 32) & 0xffffffff),
		     (void*)(bmapp->offset & 0xffffffff),
		     (void*)((unsigned long)(bmapp->length)),
		     (void*)((unsigned long)(bmapp->pboff)),
		     (void*)((unsigned long)(bmapp->pbsize)),
		     (void*)(bmapp->bn),
		     (void*)(__psint_t)(imapp->br_startoff),
		     (void*)((unsigned long)(imapp->br_blockcount)),
		     (void*)(__psint_t)(imapp->br_startblock));
}
#endif	/* DEBUG */
	     
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
	xfs_fileoff_t	req_offset,
	xfs_fsize_t	isize)
{
	__int64_t	extra_blocks;
	xfs_fileoff_t	size_diff;
	xfs_fileoff_t	ext_offset;
	xfs_fileoff_t	last_file_fsb;
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
	 extra_blocks = (off_t)(bmapp->offset + bmapp->length) -
	                (__uint64_t)(imapp->br_startoff +
				     imapp->br_blockcount);   
	 if (extra_blocks > 0) {
	    	bmapp->length -= extra_blocks;
		ASSERT(bmapp->length > 0);
	}

	/*
	 * If the iosize from our offset extends beyond the end
	 * of the file and the current extent is simply a hole,
	 * then trim down the length to match the
	 * size of the file.  This keeps us from going out too
	 * far into hole at the EOF that extends to infinity.
	 */
	if (start_block == HOLESTARTBLOCK) {
		last_file_fsb = XFS_B_TO_FSB(mp, isize);
		extra_blocks = (off_t)(bmapp->offset + bmapp->length) -
			(__uint64_t)last_file_fsb;
		if (extra_blocks > 0) {
			bmapp->length -= extra_blocks;
			ASSERT(bmapp->length > 0);
		}
		ASSERT((bmapp->offset + bmapp->length) <= last_file_fsb);
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
 * This is called when the VM/chunk cache is trying to create a buffer
 * for a page which is beyond the end of the file.  If we're at the
 * start of the page we give it as much of a mapping as we can, but
 * if it comes back for the rest of the page we say there is nothing there.
 * This behavior is tied to the code in the VM/chunk cache (do_pdflush())
 * that will call here.
 */
STATIC int					/* error */
xfs_iomap_extra(
	xfs_inode_t	*ip,
	off_t		offset,
	size_t		count,
	struct bmapval	*bmapp,
	int		*nbmaps)
{
	xfs_fileoff_t	offset_fsb;
	xfs_fileoff_t	end_fsb;
	xfs_fsize_t	nisize;
	xfs_mount_t	*mp;
	int		nimaps;
	xfs_fsblock_t	firstblock;
	int		error;
	xfs_bmbt_irec_t	imap;

	nisize = ip->i_new_size;
	if (nisize < ip->i_d.di_size) {
		nisize = ip->i_d.di_size;
	}
	mp = ip->i_mount;
	offset_fsb = XFS_B_TO_FSBT(mp, offset);

	if (poff(offset) != 0) {
		/*
		 * This is the 'remainder' of a page being mapped out.
		 * Since it is already beyond the EOF, there is no reason
		 * to bother.
		 */
		ASSERT(count < NBPP);
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
	} else {
		/*
		 * A page is being mapped out so that it can be flushed.
		 * The page is beyond the EOF, but we need to return
		 * something to keep the chunk cache happy.
		 */
		ASSERT(count <= NBPP);
		end_fsb = XFS_B_TO_FSB(mp, ((xfs_ufsize_t)(offset + count)));
		nimaps = 1;
		firstblock = NULLFSBLOCK;
		error = xfs_bmapi(NULL, ip, offset_fsb,
				  (xfs_filblks_t)(end_fsb - offset_fsb),
				  0, &firstblock, 0, &imap,
				  &nimaps, NULL);
		if (error) {
			return error;
		}
		ASSERT(nimaps == 1);
		*nbmaps = 1;
		bmapp->eof = BMAP_EOF;
		if (imap.br_startblock == HOLESTARTBLOCK) {
			bmapp->eof |= BMAP_HOLE;
			bmapp->bn = -1;
		} else if (imap.br_startblock == DELAYSTARTBLOCK) {
			bmapp->eof |= BMAP_DELAY;
			bmapp->bn = -1;
		} else {
			bmapp->bn = XFS_FSB_TO_DB(mp, ip, imap.br_startblock);
		}
		bmapp->offset = XFS_FSB_TO_BB(mp, offset_fsb);
		bmapp->length =	XFS_FSB_TO_BB(mp, imap.br_blockcount);
		ASSERT(bmapp->length > 0);
		bmapp->bsize = BBTOB(bmapp->length);
		bmapp->pboff = offset - BBTOOFF(bmapp->offset);
		ASSERT(bmapp->pboff >= 0);
		bmapp->pbsize = bmapp->bsize - bmapp->pboff;
		ASSERT(bmapp->pbsize > 0);
		if (bmapp->pbsize > count) {
			bmapp->pbsize = count;
		}
		if (ip->i_d.di_flags & XFS_DIFLAG_REALTIME) {
			bmapp->pbdev = mp->m_rtdev;
		} else {
			bmapp->pbdev = mp->m_dev;
		}
	}
	return 0;
}

/*
 * xfs_iomap_read()
 *
 * This is the main I/O policy routine for reads.  It fills in
 * the given bmapval structures to indicate what I/O requests
 * should be used to read in the portion of the file for the given
 * offset and count.
 *
 * The inode's I/O lock may be held SHARED here, but the inode lock
 * must be held EXCL because it protects the read ahead state variables
 * in the inode.
 */
int					/* error */
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
	xfs_fileoff_t	next_offset;
	xfs_fileoff_t	last_fsb;
	xfs_fileoff_t	max_fsb;
	xfs_fsize_t	nisize;
	off_t		offset_page;
	off_t		aligned_offset;
	xfs_fsblock_t	firstblock;
	int		nimaps;
	int		error;
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

	ASSERT(ismrlocked(&ip->i_lock, MR_UPDATE) != 0);
	ASSERT(ismrlocked(&ip->i_iolock, MR_UPDATE | MR_ACCESS) != 0);
	xfs_iomap_enter_trace(XFS_IOMAP_READ_ENTER, ip, offset, count);

	mp = ip->i_mount;
	nisize = ip->i_new_size;
	if (nisize < ip->i_d.di_size) {
		nisize = ip->i_d.di_size;
	}
	offset_fsb = XFS_B_TO_FSBT(mp, offset);
	nimaps = XFS_READ_IMAPS;
	last_fsb = XFS_B_TO_FSB(mp, ((xfs_ufsize_t)nisize));
	if (offset >= nisize) {
		/*
		 * The VM/chunk code is trying to map a page or part
		 * of a page to be pushed out that is beyond the end
		 * of the file.  We handle these cases separately so
		 * that they do not interfere with the normal path
		 * code.
		 */
		error = xfs_iomap_extra(ip, offset, count, bmapp, nbmaps);
		return error;
	}
	/*
	 * Map out to the maximum possible file size.  This will return
	 * an extra hole we don't really care about at the end, but we
	 * won't do any read-ahead beyond the EOF anyway.  We do this
	 * so that the buffers we create here line up well with those
	 * created in xfs_iomap_write() which extend beyond the end of
	 * the file.
	 */
	max_fsb = XFS_B_TO_FSB(mp, (xfs_ufsize_t)XFS_MAX_FILE_OFFSET);
	firstblock = NULLFSBLOCK;
	error = xfs_bmapi(NULL, ip, offset_fsb,
			  (xfs_filblks_t)(max_fsb - offset_fsb),
			  XFS_BMAPI_ENTIRE, &firstblock, 0, imap,
			  &nimaps, NULL);
	if (error) {
		return error;
	}

	if ((offset == ip->i_next_offset) &&
	    (count <= ip->i_last_req_sz)) {
		/*
		 * Sequential I/O of same size as last time.
	 	 */
		ASSERT(ip->i_io_size > 0);
		iosize = ip->i_io_size;
		ASSERT(iosize <= XFS_BB_TO_FSBT(mp, XFS_MAX_BMAP_LEN_BB));
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
			ASSERT(iosize <=
			       XFS_BB_TO_FSBT(mp, XFS_MAX_BMAP_LEN_BB));
			aligned_offset = XFS_READIO_ALIGN(mp, offset);
			ioalign = XFS_B_TO_FSBT(mp, aligned_offset);
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
			offset_page = ctooff(offtoct(XFS_FSB_TO_B(mp,
							 offset_fsb)));
			last_fsb = XFS_B_TO_FSB(mp,
					ctooff(offtoc(offset + count)));
			iosize = last_fsb - XFS_B_TO_FSBT(mp, offset_page);
			if (iosize >
			    XFS_BB_TO_FSBT(mp, XFS_MAX_BMAP_LEN_BB)) {
				iosize = XFS_BB_TO_FSBT(mp,
							XFS_MAX_BMAP_LEN_BB);
			}
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
		      last_offset, offset_fsb, nisize);
	ASSERT((bmapp->length > 0) &&
	       (offset >= XFS_FSB_TO_B(mp, bmapp->offset)));
	
	if (XFS_FSB_TO_B(mp, bmapp->offset + bmapp->length) >= nisize) {
		bmapp->eof |= BMAP_EOF;
	}

	bmapp->pboff = offset - XFS_FSB_TO_B(mp, bmapp->offset);
	retrieved_bytes = bmapp->bsize - bmapp->pboff;
	total_retrieved_bytes = 0;
	bmapp->pbsize = xfs_retrieved(retrieved_bytes, offset, count,
				      &total_retrieved_bytes, nisize);
	xfs_iomap_map_trace(XFS_IOMAP_READ_MAP,
			    ip, offset, count, bmapp, imap);

	/*
	 * Only map additional buffers if they've been asked for
	 * and the I/O being done is sequential and has reached the
	 * point where we need to initiate more read ahead or we didn't get
	 * the whole request in the first bmap.
	 */
	last_fsb = XFS_B_TO_FSB(mp, nisize);
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
			if (next_offset >= last_fsb) {
				/*
				 * We've mapped all the way to the EOF.
				 * Everything beyond there is inaccessible,
				 * so get out now.
				 */
				break;
			}

			last_iosize = curr_bmapp->length;
			if (next_offset <
			    (curr_imapp->br_startoff +
			     curr_imapp->br_blockcount)) {
				xfs_next_bmap(mp, curr_imapp,
					 next_bmapp, iosize, last_iosize, -1,
					 curr_bmapp->offset, next_offset,
					 nisize);
			} else {
				curr_imapp++;
				if (curr_imapp <= last_imapp) {
					xfs_next_bmap(mp,
					    curr_imapp, next_bmapp,
					    iosize, last_iosize, -1,
					    curr_bmapp->offset, next_offset,
					    nisize);	      
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
			ASSERT(curr_bmapp->length > 0);
		       
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
			
			if (XFS_FSB_TO_B(mp, curr_bmapp->offset +
					 curr_bmapp->length) >= nisize) {
				curr_bmapp->eof |= BMAP_EOF;
			}
			xfs_iomap_map_trace(XFS_IOMAP_READ_MAP, ip, offset,
					    count, curr_bmapp, curr_imapp);

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

	ASSERT(iosize <= XFS_BB_TO_FSBT(mp, XFS_MAX_BMAP_LEN_BB));
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
		ASSERT(curr_bmapp->offset <= XFS_B_TO_FSB(mp, nisize));
		curr_bmapp->offset = XFS_FSB_TO_BB(mp, curr_bmapp->offset);
		curr_bmapp->length = XFS_FSB_TO_BB(mp, curr_bmapp->length);
		ASSERT(curr_bmapp->length > 0);
		ASSERT((x == 0) ||
		       ((bmapp[x - 1].offset + bmapp[x - 1].length) ==
			curr_bmapp->offset));
		if (curr_bmapp->bn != -1) {
			curr_bmapp->bn = XFS_FSB_TO_DB(mp, ip,
						       curr_bmapp->bn);
		}
	}
	return 0;
}				
			
/* ARGSUSED */		
int
xfs_read_file(
	vnode_t	*vp,
	uio_t	*uiop,
	int	ioflag,
	cred_t	*credp)
{
#define	XFS_READ_BMAPS	XFS_ZONE_NBMAPS
	struct bmapval	bmaps[XFS_ZONE_NBMAPS];
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
	XFSSTATS.xs_read_calls++;
	XFSSTATS.xs_read_bytes += uiop->uio_resid;

	/*
	 * Loop until uio->uio_resid, which is the number of bytes the
	 * caller has requested, goes to 0 or we get an error.  Each
	 * call to xfs_iomap_read tries to map as much of the request
	 * plus read-ahead as it can.  We must hold the inode lock
	 * exclusively when calling xfs_iomap_read.
	 */
	do {
		xfs_ilock(ip, XFS_ILOCK_EXCL);
		xfs_rw_enter_trace(XFS_READ_ENTER, ip, uiop, ioflag);

		/*
		 * We've fallen off the end of the file, so
		 * just return with what we've done so far.
		 */
		if (uiop->uio_offset >= ip->i_d.di_size) {
			xfs_iunlock(ip, XFS_ILOCK_EXCL);
			break;
		}
 
		nbmaps = XFS_READ_BMAPS;
		error = xfs_iomap_read(ip, uiop->uio_offset, uiop->uio_resid,
				       bmaps, &nbmaps);

		xfs_iunlock(ip, XFS_ILOCK_EXCL);

		if (error || (bmaps[0].pbsize == 0)) {
			break;
		}

		bmapp = &bmaps[0];
		read_bmaps = nbmaps;
		ASSERT(BBTOOFF(bmapp->offset) <= uiop->uio_offset);
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
				brelse(bp);
				break;
			} else if (bp->b_resid != 0) {
				buffer_bytes_ok = 0;
				brelse(bp);
				break;
			} else {
				buffer_bytes_ok = 1;
				ASSERT((BBTOOFF(bmapp->offset) + bmapp->pboff)
				       == uiop->uio_offset);
				error = biomove(bp, bmapp->pboff,
						bmapp->pbsize, UIO_READ,
						uiop);
				if (error) {
					brelse(bp);
					break;
				}
			}

			brelse(bp);

			XFSSTATS.xs_read_bufs++;
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
 * This is the XFS VOP_READ entry point.  It does some minimal
 * error checking and then switches out based on the file type.
 */
int
xfs_read(
	vnode_t		*vp,
	uio_t		*uiop,
	int		ioflag,
	cred_t		*credp)
{
	xfs_inode_t	*ip;
	int		type;
	off_t		offset;
	off_t		n;
	size_t		count;
	int		error;
	xfs_mount_t	*mp;
	size_t		resid;

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
	if (ioflag & IO_RSYNC) {
		/* First we sync the data */
	    if ((ioflag & IO_SYNC) || (ioflag & IO_DSYNC)) {
		chunkpush(vp, offset, offset + count , 0);
	    }
		/* Now we sync the timestamps */
	    if (ioflag & IO_SYNC) {
		xfs_inode_t	*ip;

		ip = XFS_VTOI(vp);
		xfs_ilock(ip, XFS_ILOCK_SHARED);
		xfs_iflock(ip);
		xfs_iflush(ip, XFS_IFLUSH_SYNC);
		xfs_iunlock(ip, XFS_IOLOCK_EXCL | XFS_ILOCK_SHARED);
	    } else {
		if (ioflag & IO_DSYNC) {
		    mp = ip->i_mount;
		    xfs_log_force(mp, (xfs_lsn_t)0,
				  XFS_LOG_FORCE | XFS_LOG_SYNC );
		}
	    }
	}
	switch (type) {
	case IFREG:
		/*
		 * Don't allow reads to pass down counts which could
		 * overflow.  Be careful to record the part that we
		 * refuse so that we can add it back into uio_resid
		 * so that the caller will see a short read.
		 */
		n = XFS_MAX_FILE_OFFSET - offset;
		if (n <= 0) {
			return 0;
		}
		if (n < uiop->uio_resid) {
			resid = uiop->uio_resid - n;
			uiop->uio_resid = n;
		} else {
			resid = 0;
		}
			    
		/*
		 * Not ready for in-line files yet.
		 */
		ASSERT((ip->i_d.di_format == XFS_DINODE_FMT_EXTENTS) ||
		       (ip->i_d.di_format == XFS_DINODE_FMT_BTREE));

		if (DM_EVENT_ENABLED(vp->v_vfsp, ip, DM_READ) &&
		    !(ioflag & IO_INVIS)) {
			if (error = dm_data_event(vp, DM_READ, offset, count))
				return error;
		}
		if (ioflag & IO_DIRECT)
			error = xfs_diordwr( vp, uiop, ioflag, credp, B_READ);
		else
			error = xfs_read_file( vp, uiop, ioflag, credp);

		ASSERT(ismrlocked(&ip->i_iolock, MR_ACCESS | MR_UPDATE) != 0);
		/* don't update timestamps if doing invisible I/O */
		if (!(ioflag & IO_INVIS)) {
			xfs_ichgtime(ip, XFS_ICHGTIME_ACC);
		}

		/*
		 * Add back whatever we refused to do because of file
		 * size limitations.
		 */
		uiop->uio_resid += resid;

		break;

	case IFDIR:
		error = XFS_ERROR(EISDIR);
		break;

	case IFLNK:
		error = XFS_ERROR(EINVAL);
		break;
	      
	case IFSOCK:
		error = XFS_ERROR(ENODEV);
		break;

	default:
		ASSERT(0);
		error = XFS_ERROR(EINVAL);
		break;
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
/*ARGSUSED*/
STATIC void
xfs_write_bmap(
	xfs_mount_t	*mp,
	xfs_bmbt_irec_t	*imapp,
	struct bmapval	*bmapp,
	int		iosize,
	xfs_fileoff_t	ioalign,
	xfs_fsize_t	isize)
{
	__int64_t	extra_blocks;
	xfs_fileoff_t	size_diff;
	xfs_fileoff_t	ext_offset;
	xfs_fsblock_t	start_block;
	
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
	extra_blocks = (off_t)(bmapp->offset + bmapp->length) -
		       (__uint64_t)(imapp->br_startoff +
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
/* ARGSUSED */
STATIC int				/* error */
xfs_zero_last_block(
	xfs_inode_t	*ip,
	off_t		offset,
	xfs_fsize_t	isize,
	cred_t		*credp)
{
	xfs_fileoff_t	last_fsb;
	xfs_fsblock_t	firstblock;
	xfs_mount_t	*mp;
	buf_t		*bp;
	int		nimaps;
	int		zero_offset;
	int		zero_len;
	int		isize_fsb_offset;
	int		i;
	int		error;
	pfd_t		*pfdp;
	xfs_bmbt_irec_t	imap;
	struct bmapval	bmap;

	ASSERT(ismrlocked(&ip->i_lock, MR_UPDATE) != 0);
	ASSERT(offset > isize);

	mp = ip->i_mount;

	/*
	 * If the file system block size is less than the page size,
	 * then there could be bytes in the last page after the last
	 * fsblock containing isize which have not been initialized.
	 * Since if such a page is in memory it will be marked P_DONE
	 * and my now be fully accessible, we need to zero any part of
	 * it which is beyond the old file size.  We don't need to send
	 * this out to disk, we're just iniitializing it to zeroes like
	 * we would have done in xfs_strat_read() had the size been bigger.
	 */
	if ((mp->m_sb.sb_blocksize < NBPP) && ((i = poff(isize)) != 0)) {
		pfdp = pfind(XFS_ITOV(ip), offtoct(isize), VM_ATTACH);
		if (pfdp != NULL) {
			page_zero(pfdp, NOCOLOR, i, (NBPP - i));
			pagefree(pfdp);
		}
	}

	isize_fsb_offset = XFS_B_FSB_OFFSET(mp, isize);
	if (isize_fsb_offset == 0) {
		/*
		 * There are not extra bytes in the last block to
		 * zero, so return.
		 */
		return 0;
	}

	last_fsb = XFS_B_TO_FSBT(mp, isize);
	nimaps = 1;
	firstblock = NULLFSBLOCK;
	error = xfs_bmapi(NULL, ip, last_fsb, 1, 0, &firstblock, 0, &imap,
			  &nimaps, NULL);
	if (error) {
		return error;
	}
	ASSERT(nimaps > 0);
	/*
	 * If the block underlying isize is just a hole, then there
	 * is nothing to zero.
	 */
	if (imap.br_startblock == HOLESTARTBLOCK) {
		return 0;
	}
	/*
	 * Get a buffer for the last block, zero the part beyond the
	 * EOF, and write it out sync.  We need to drop the ilock
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
		bmap.bn = XFS_FSB_TO_DB(mp, ip, imap.br_startblock);
	} else {
		bmap.bn = -1;
		bmap.eof |= BMAP_DELAY;
	}
	bp = chunkread(XFS_ITOV(ip), &bmap, 1, credp);
	if (bp->b_flags & B_ERROR) {
		error = geterror(bp);
		brelse(bp);
		return error;
	}
	zero_offset = isize_fsb_offset;
	zero_len = mp->m_sb.sb_blocksize - isize_fsb_offset;
	xfs_zero_bp(bp, zero_offset, zero_len);
	/*
	 * We don't want to start a transaction here, so don't
	 * push out a buffer over a delayed allocation extent.
	 * Also, we can get away with it since the space isn't
	 * allocated so it's faster anyway.
	 */
	if (imap.br_startblock == DELAYSTARTBLOCK) {
		bdwrite(bp);
	} else {
		error = bwrite(bp);
	}

	xfs_ilock(ip, XFS_ILOCK_EXCL);
	return error;
}

/*
 * Zero any on disk space between the current EOF and the new,
 * larger EOF.  This handles the normal case of zeroing the remainder
 * of the last block in the file and the unusual case of zeroing blocks
 * out beyond the size of the file.  This second case only happens
 * with fixed size extents and when the system crashes before the inode
 * size was updated but after blocks were allocated.
 */
int					/* error */
xfs_zero_eof(
	xfs_inode_t	*ip,
	off_t		offset,
	xfs_fsize_t	isize,
	cred_t		*credp)
{
	xfs_fileoff_t	start_zero_fsb;
	xfs_fileoff_t	end_zero_fsb;
	xfs_fileoff_t	zero_count_fsb;
	xfs_fileoff_t	last_fsb;
	xfs_fsblock_t	firstblock;
	xfs_extlen_t	buf_len_fsb;
	xfs_mount_t	*mp;
	buf_t		*bp;
	int		nimaps;
	int		error;
	xfs_bmbt_irec_t	imap;
	struct bmapval	bmap;

	ASSERT(ismrlocked(&(ip->i_lock), MR_UPDATE));
	ASSERT(ismrlocked(&(ip->i_iolock), MR_UPDATE));

	mp = ip->i_mount;

	/*
	 * First handle zeroing the block on which isize resides.
	 * We only zero a part of that block so it is handled specially.
	 */
	error = xfs_zero_last_block(ip, offset, isize, credp);
	if (error) {
		return error;
	}

	/*
	 * Calculate the range between the new size and the old
	 * where blocks needing to be zeroed may exist.  To get the
	 * block where the last byte in the file currently resides,
	 * we need to subtrace one from the size and truncate back
	 * to a block boundary.  We subtract 1 in case the size is
	 * exactly on a block boundary.
	 */
	last_fsb = XFS_B_TO_FSBT(mp, isize - 1);
	start_zero_fsb = XFS_B_TO_FSB(mp, ((xfs_ufsize_t)isize));
	end_zero_fsb = XFS_B_TO_FSBT(mp, offset - 1);
	ASSERT(last_fsb < start_zero_fsb);
	if (last_fsb == end_zero_fsb) {
		/*
		 * The size was only incremented on its last block.
		 * We took care of that above, so just return.
		 */
		return 0;
	}

	ASSERT(start_zero_fsb <= end_zero_fsb);
	while (start_zero_fsb <= end_zero_fsb) {
		nimaps = 1;
		zero_count_fsb = end_zero_fsb - start_zero_fsb + 1;
		firstblock = NULLFSBLOCK;
		error = xfs_bmapi(NULL, ip, start_zero_fsb, zero_count_fsb,
				  0, &firstblock, 0, &imap, &nimaps, NULL);
		if (error) {
			return error;
		}
		ASSERT(nimaps > 0);

		if (imap.br_startblock == HOLESTARTBLOCK) {
			start_zero_fsb = imap.br_startoff +
				         imap.br_blockcount;
			ASSERT(start_zero_fsb <= (end_zero_fsb + 1));
			continue;
		}

		/*
		 * Drop the inode lock while we're doing the I/O.
		 * We'll still have the iolock to protect us.
		 */
		xfs_iunlock(ip, XFS_ILOCK_EXCL);

		/*
		 * There are blocks in the range requested.
		 * Zero them a single write at a time.  We actually
		 * don't zero the entire range returned if it is
		 * too big and simply loop around to get the rest.
		 * That is not the most efficient thing to do, but it
		 * is simple and this path should not be exercised often.
		 */
		buf_len_fsb = XFS_FILBLKS_MIN(imap.br_blockcount,
					      mp->m_writeio_blocks);
		bmap.offset = XFS_FSB_TO_BB(mp, imap.br_startoff);
		bmap.length = XFS_FSB_TO_BB(mp, buf_len_fsb);
		bmap.bsize = BBTOB(bmap.length);
		bmap.eof = BMAP_EOF;
		if (imap.br_startblock == DELAYSTARTBLOCK) {
			bmap.eof |= BMAP_DELAY;
			bmap.bn = -1;
		} else {
			bmap.bn = XFS_FSB_TO_DB(mp, ip, imap.br_startblock);
		}
		if (ip->i_d.di_flags & XFS_DIFLAG_REALTIME) {
			bmap.pbdev = mp->m_rtdev;
		} else {
			bmap.pbdev = mp->m_dev;
		}
		bp = getchunk(XFS_ITOV(ip), &bmap, credp);

		bp_mapin(bp);
		bzero(bp->b_un.b_addr, bp->b_bcount);

		if (imap.br_startblock == DELAYSTARTBLOCK) {
			bdwrite(bp);
		} else {
			error = bwrite(bp);
			if (error) {
				xfs_ilock(ip, XFS_ILOCK_EXCL);
				return error;
			}
		}

		start_zero_fsb = imap.br_startoff + buf_len_fsb;
		ASSERT(start_zero_fsb <= (end_zero_fsb + 1));

		xfs_ilock(ip, XFS_ILOCK_EXCL);
	}

	return 0;
}

STATIC int
xfs_iomap_write(
	xfs_inode_t	*ip,
	off_t		offset,
	size_t		count,
	struct bmapval	*bmapp,
	int		*nbmaps,
	int		ioflag)		
{
	xfs_fileoff_t	offset_fsb;
	xfs_fileoff_t	ioalign;
	xfs_fileoff_t	next_offset_fsb;
	xfs_fileoff_t	last_fsb;
	xfs_fileoff_t	bmap_end_fsb;
	xfs_fileoff_t	last_file_fsb;
	xfs_fileoff_t	start_fsb;
	xfs_filblks_t	count_fsb;
	off_t		aligned_offset;
	xfs_fsize_t	isize;
	xfs_fsblock_t	firstblock;
	__uint64_t	last_page_offset;
	int		nimaps;
	int		error;
	int		n;
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
	xfs_iomap_enter_trace(XFS_IOMAP_WRITE_ENTER, ip, offset, count);

	mp = ip->i_mount;
	if (ip->i_new_size > ip->i_d.di_size) {
		isize = ip->i_new_size;
	} else {
		isize = ip->i_d.di_size;
	}

	offset_fsb = XFS_B_TO_FSBT(mp, offset);
	last_fsb = XFS_B_TO_FSB(mp, ((xfs_ufsize_t)(offset + count)));
	/*
	 * If the caller is doing a write at the end of the file,
	 * then extend the allocation (and the buffer used for the write)
	 * out to the file system's write iosize.  We clean up any extra
	 * space left over when the file is closed in xfs_inactive().
	 * We can only do this if we are sure that we will create buffers
	 * over all of the space we allocate beyond the end of the file.
	 * Not doing so would allow us to create delalloc blocks with
	 * no pages in memory covering them.  So, we need to check that
	 * there are not any real blocks in the area beyond the end of
	 * the file which we are optimistically going to preallocate. If
	 * there are then our buffers will stop when they encounter them
	 * and we may accidentally create delalloc blocks beyond them
	 * that we never cover with a buffer.  All of this is because
	 * we are not actually going to write the extra blocks preallocated
	 * at this point.
	 *
	 * We don't bother with this for sync writes, because we need
	 * to minimize the amount we write for good performance.
	 */
	if (!(ioflag & IO_SYNC) && ((offset + count) > ip->i_d.di_size)) {
		start_fsb = XFS_B_TO_FSBT(mp,
				  ((xfs_ufsize_t)(offset + count - 1)));
		count_fsb = mp->m_writeio_blocks;
		while (count_fsb > 0) {
			nimaps = XFS_WRITE_IMAPS;
			firstblock = NULLFSBLOCK;
			error = xfs_bmapi(NULL, ip, start_fsb, count_fsb,
					  0, &firstblock, 0, imap, &nimaps,
					  NULL);
			if (error) {
				return error;
			}
			for (n = 0; n < nimaps; n++) {
				if ((imap[n].br_startblock != HOLESTARTBLOCK) &&
				    (imap[n].br_startblock != DELAYSTARTBLOCK)) {
					goto write_map;
				}
				start_fsb += imap[n].br_blockcount;
				count_fsb -= imap[n].br_blockcount;
				ASSERT(count_fsb < 0xffff000);
			}
		}
		iosize = mp->m_writeio_blocks;
		aligned_offset = XFS_WRITEIO_ALIGN(mp, (offset + count - 1));
		ioalign = XFS_B_TO_FSBT(mp, aligned_offset);
		last_fsb = ioalign + iosize;
	}
 write_map:
	nimaps = XFS_WRITE_IMAPS;
	firstblock = NULLFSBLOCK;
	error = xfs_bmapi(NULL, ip, offset_fsb,
			  (xfs_filblks_t)(last_fsb - offset_fsb),
			  XFS_BMAPI_DELAY | XFS_BMAPI_WRITE |
			  XFS_BMAPI_ENTIRE, &firstblock, 1, imap,
			  &nimaps, NULL);
	if (error) {
		return error;
	}
	/*
	 * If bmapi returned us nothing, then we must have run out of space.
	 */
	if (nimaps == 0) {
		xfs_iomap_enter_trace(XFS_IOMAP_WRITE_NOSPACE,
				      ip, offset, count);
		return ENOSPC;
	}

	if (!(ioflag & IO_SYNC) ||
	    ((last_fsb - offset_fsb) >= mp->m_writeio_blocks)) {
		/*
		 * For normal or large sync writes, align everything
		 * into m_writeio_blocks sized chunks.
		 */
		iosize = mp->m_writeio_blocks;
		aligned_offset = XFS_WRITEIO_ALIGN(mp, offset);
		ioalign = XFS_B_TO_FSBT(mp, aligned_offset);
	} else {
		/*
		 * For small sync writes try to minimize the amount
		 * of I/O we do.  Round down and up to the larger of
		 * page or block boundaries.
		 */
		if (NBPP > mp->m_sb.sb_blocksize) {
			aligned_offset = ctooff(offtoct(offset));
			ioalign = XFS_B_TO_FSBT(mp, aligned_offset);
			last_page_offset = ctob64(btoc64(offset + count));
			iosize = XFS_B_TO_FSBT(mp, last_page_offset -
					       aligned_offset);
		} else {
			ioalign = offset_fsb;
			iosize = last_fsb - offset_fsb;
		}
	}

	/*
	 * Now map our desired I/O size and alignment over the
	 * extents returned by xfs_bmapi().
	 */
	xfs_write_bmap(mp, imap, bmapp, iosize, ioalign, isize);
	ASSERT((bmapp->length > 0) &&
	       (offset >= XFS_FSB_TO_B(mp, bmapp->offset)));

	/*
	 * A bmap is the EOF bmap when it reaches to or beyond the new
	 * inode size.
	 */
	bmap_end_fsb = bmapp->offset + bmapp->length;
	if (XFS_FSB_TO_B(mp, bmap_end_fsb) >= isize) {
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
	xfs_iomap_map_trace(XFS_IOMAP_WRITE_MAP,
			    ip, offset, count, bmapp, imap);

	/*
	 * Map more buffers if the first does not map the entire
	 * request.  We do this until we run out of bmaps, imaps,
	 * or bytes to write.
	 */
	last_file_fsb = XFS_B_TO_FSB(mp, ((xfs_ufsize_t)isize));
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
			if (next_offset_fsb >= last_file_fsb) {
				/*
				 * We've gone beyond the region asked for
				 * by the caller, so we're done.
				 */
				break;
			}
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
					aligned_offset = XFS_FSB_TO_B(mp,
					                    next_offset_fsb);
					aligned_offset =
						XFS_WRITEIO_ALIGN(mp,
							aligned_offset);
					ioalign = XFS_B_TO_FSBT(mp,
							aligned_offset);
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
			writing_bytes = next_bmapp->bsize;
			if (writing_bytes > count_remaining) {
				writing_bytes = count_remaining;
			}
			next_bmapp->pbsize = writing_bytes;
			count_remaining -= writing_bytes;
			ASSERT(((long)count_remaining) >= 0);

			filled_bmaps++;
			curr_bmapp++;
			next_bmapp++;
			/*
			 * A bmap is the EOF bmap when it reaches to
			 * or beyond the new inode size.
			 */
			bmap_end_fsb = curr_bmapp->offset +
				       curr_bmapp->length;
			if (((xfs_ufsize_t)XFS_FSB_TO_B(mp, bmap_end_fsb)) >=
			    (xfs_ufsize_t)isize) {
				curr_bmapp->eof |= BMAP_EOF;
			}
			xfs_iomap_map_trace(XFS_IOMAP_WRITE_MAP, ip, offset,
					    count, curr_bmapp, curr_imapp);
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
			curr_bmapp->bn = XFS_FSB_TO_DB(mp, ip,
						       curr_bmapp->bn);
		}
	}

	/*
	 * Clear out any read-ahead info since the write may
	 * have made it invalid.
	 */
	XFS_INODE_CLEAR_READ_AHEAD(ip);
	return 0;
}


int
xfs_write_file(
	vnode_t	*vp,
	uio_t	*uiop,
	int	ioflag,
	cred_t	*credp)
{
#define	XFS_WRITE_BMAPS	XFS_ZONE_NBMAPS
	struct bmapval	bmaps[XFS_WRITE_BMAPS];
	struct bmapval	*bmapp;
	int		nbmaps;
	buf_t		*bp;
	xfs_inode_t	*ip;
	int		error;
	int		eof_zeroed;
	int		gaps_mapped;
	xfs_fsize_t	isize;
	xfs_fsize_t	new_size;
	xfs_mount_t	*mp;
	extern void	chunkrelse(buf_t*);

	ip = XFS_VTOI(vp);
	mp = ip->i_mount;

	/*
	 * If the file has fixed size extents or is a real time file, 
	 * buffered I/O cannot be performed.
	 * This check will be removed in the future.
	 */
	if ((ip->i_d.di_extsize) || 
	    (ip->i_d.di_flags & XFS_DIFLAG_REALTIME))  {
		return( EINVAL );
	}

	error = 0;
	eof_zeroed = 0;
	gaps_mapped = 0;
	XFSSTATS.xs_write_calls++;
	XFSSTATS.xs_write_bytes += uiop->uio_resid;

	/*
	 * i_new_size is used by xfs_iomap_read() when the chunk
	 * cache code calls back into the file system through
	 * xfs_bmap().  This way we can tell where the end of
	 * file is going to be even though we haven't yet updated
	 * ip->i_d.di_size.  This is guarded by the iolock and the
	 * inode lock.  Either is sufficient for reading the value.
	 */
	new_size = uiop->uio_offset + uiop->uio_resid;

	/*
	 * i_write_offset is used by xfs_strat_read() when the chunk
	 * cache code calls back into the file system through
	 * xfs_strategy() to initialize a buffer.  We use it there
	 * to know how much of the buffer needs to be zeroed and how
	 * much will be initialize here by the write or not need to
	 * be initialized because it will be beyond the inode size.
	 * This is protected by the io lock.
	 */
	ip->i_write_offset = uiop->uio_offset;

	/*
	 * Loop until uiop->uio_resid, which is the number of bytes the
	 * caller has requested to write, goes to 0 or we get an error.
	 * Each call to xfs_iomap_write() tries to map as much of the
	 * request as it can in mp->m_writeio_blks sized chunks.
	 */
	do {
		xfs_ilock(ip, XFS_ILOCK_EXCL);
		isize = ip->i_d.di_size;
		if (new_size > isize) {
			ip->i_new_size = new_size;
		}

		xfs_rw_enter_trace(XFS_WRITE_ENTER, ip, uiop, ioflag);

		/*
		 * If this is the first pass through the loop, then map
		 * out all of the holes we might fill in with this write
		 * and list them in the inode's gap list.  This is for
		 * use by xfs_strat_read() in determining if the real
		 * blocks underlying a delalloc buffer have been initialized
		 * or not.  Since writes are single threaded, if the blocks
		 * were holes when we started and xfs_strat_read() is asked
		 * to read one in while we're still here in xfs_write_file(),
		 * then the block is not initialized.  Only we can
		 * initialize it and once we write out a buffer we remove
		 * any entries in the gap list which overlap that buffer.
		 */
		if (!gaps_mapped) {
			error = xfs_build_gap_list(ip, uiop->uio_offset,
						   uiop->uio_resid);
			if (error) {
				goto error0;
			}
			gaps_mapped = 1;
		}

		/*
		 * If we've seeked passed the EOF to do this write,
		 * then we need to make sure that any buffer overlapping
		 * the EOF is zeroed beyond the EOF.
		 */
		if (!eof_zeroed &&
		    (uiop->uio_offset > isize) &&
		    (isize != 0)) {
			error = xfs_zero_eof(ip, uiop->uio_offset, isize,
					     credp);
			if (error) {
				goto error0;
			}
			eof_zeroed = 1;
		}

		nbmaps = XFS_WRITE_BMAPS;
		error = xfs_iomap_write(ip, uiop->uio_offset,
					uiop->uio_resid, bmaps, &nbmaps,
					ioflag);
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
		 *
		 * Error handling is a bit tricky because of delayed
		 * allocation.  We need to make sure that we create
		 * dirty buffers over all the delayed allocation
		 * extents created in xfs_iomap_write().  Thus, when
		 * we get an error we continue to process each of
		 * the bmaps returned by xfs_iomap_write().  Each is
		 * read in so that it is fully initialized and then
		 * written out without actually copying in the user's
		 * data.
		 */
		while (((uiop->uio_resid != 0) || (error != 0)) &&
		       (nbmaps > 0)) {
			/*
			 * If the write doesn't completely overwrite
			 * the buffer and we're not writing from
			 * the beginning of the buffer to the end
			 * of the file then we need to read the
			 * buffer.  We also always want to read the
			 * buffer if we've encountered an error and
			 * we're just cleaning up.
			 *
			 * Reading the buffer will send it to xfs_strategy
			 * which will take care of zeroing the holey
			 * parts of the buffer and coordinating with
			 * other, simultaneous writers.
			 */
			if ((error != 0) ||
			    ((bmapp->pbsize != bmapp->bsize) &&
			    !((bmapp->pboff == 0) &&
			      (uiop->uio_offset >= isize)))) {
				bp = chunkread(vp, bmapp, 1, credp);
			} else {
				bp = getchunk(vp, bmapp, credp);
			}

			/*
			 * There is not much we can do with buffer errors.
			 * The assumption here is that the space underlying
			 * the buffer must now be allocated (even if it
			 * wasn't when we mapped the buffer) and we got an
			 * error reading from it.  In this case the blocks
			 * will remain unreadable, so we just toss the buffer
			 * and its associated pages.
			 */
			if (bp->b_flags & B_ERROR) {
				error = geterror(bp);
				ASSERT(error != EINVAL);
				brelse(bp);
				bmapp++;
				nbmaps--;
				continue;
			}

			/*
			 * If we've already encountered an error, then
			 * write the buffers out without copying the user's
			 * data into them.  This way we get dirty buffers
			 * over our delayed allocation extents which
			 * have been initialized by xfs_strategy() since
			 * we forced the chunkread() above.
			 * We write the data out synchronously here so that
			 * we don't have to worry about having buffers
			 * possibly out beyond the EOF when we later flush
			 * or truncate the file.  We set the B_STALE bit so
			 * that the buffer will be decommissioned after it
			 * is synced out.
			 */
			if (error != 0) {
				bp->b_flags |= B_STALE;
				bwrite(bp);
				bmapp++;
				nbmaps--;
				continue;
			}

			ASSERT((BBTOOFF(bmapp->offset) + bmapp->pboff) ==
			       uiop->uio_offset);
			if (error = biomove(bp, bmapp->pboff, bmapp->pbsize,
					    UIO_WRITE, uiop)) {
				/*
				 * If the buffer is already done then just
				 * mark it dirty without copying any more
				 * data into it.  It is already fully
				 * initialized.
				 * Otherwise, we must have getchunk()'d
				 * the buffer above.  Use chunkreread()
				 * to get it initialized by xfs_strategy()
				 * and then write it out.
				 * We write the data out synchronously here
				 * so that we don't have to worry about
				 * having buffers possibly out beyond the
				 * EOF when we later flush or truncate
				 * the file.  We set the B_STALE bit so
				 * that the buffer will be decommissioned
				 * after it is synced out.
				 */
				if (!(bp->b_flags & B_DONE)) {
					chunkreread(bp);
				}
				bp->b_flags |= B_STALE;
				bwrite(bp);
				bmapp++;
				nbmaps--;
				continue;
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

			/*
			 * Make sure that any gap list entries overlapping
			 * the buffer being written are removed now that
			 * we know that the blocks underlying the buffer
			 * will be initialized.  We don't need the inode
			 * lock to manipulate the gap list here, because
			 * we have the io lock held exclusively so noone
			 * else can get to xfs_strat_read() where we look
			 * at the list.
			 */
			xfs_delete_gap_list(ip,
					    XFS_BB_TO_FSBT(mp, bp->b_offset),
					    XFS_B_TO_FSBT(mp, bp->b_bcount));

			if ((ioflag & IO_SYNC) || (ioflag & IO_DSYNC)) {
				if ((bmapp->pboff + bmapp->pbsize) ==
				    bmapp->bsize) {
					bp->b_relse = chunkrelse;
				}
				bwrite(bp);
			} else {
				bdwrite(bp);
			}

			XFSSTATS.xs_write_bufs++;
			bmapp++;
			nbmaps--;

		}
	} while ((uiop->uio_resid > 0) && !error);

	/*
	 * Free up any remaining entries in the gap list, because the 
	 * list only applies to this write call.  Also clear the new_size
	 * field of the inode while we've go it locked.
	 */
	xfs_ilock(ip, XFS_ILOCK_EXCL);
error0:
	xfs_free_gap_list(ip);
	ip->i_new_size = 0;
	xfs_iunlock(ip, XFS_ILOCK_EXCL);
	ip->i_write_offset = 0;

	return error;
}

/*
 * This is a subroutine for xfs_write() which clears the setuid and
 * setgid bits when a file is written.
 */
STATIC int
xfs_write_clear_setuid(
	xfs_inode_t	*ip)
{
	xfs_mount_t	*mp;
	xfs_trans_t	*tp;
	int		error;

	mp = ip->i_mount;
	tp = xfs_trans_alloc(mp, XFS_TRANS_WRITEID);
	if (error = xfs_trans_reserve(tp, 0,
				      XFS_WRITEID_LOG_RES(mp),
				      0, 0, 0)) {
		ASSERT(0);
		xfs_trans_cancel(tp, 0);
		return error;
	}
	xfs_ilock(ip, XFS_ILOCK_EXCL);
	xfs_trans_ijoin(tp, ip, XFS_ILOCK_EXCL);
	xfs_trans_ihold(tp, ip);
	ip->i_d.di_mode &= ~ISUID;
	if (ip->i_d.di_mode & (IEXEC >> 3)) {
		ip->i_d.di_mode &= ~ISGID;
	}
	xfs_trans_log_inode(tp, ip, XFS_ILOG_CORE);
	xfs_trans_set_sync(tp);
	error = xfs_trans_commit(tp, 0);
	xfs_iunlock(ip, XFS_ILOCK_EXCL);
	return 0;
}

/*
 * xfs_write
 *
 * This is the XFS VOP_WRITE entry point.  It does some minimal error
 * checking and then switches out based on the file type.
 */
int
xfs_write(
	vnode_t	*vp,
	uio_t	*uiop,
	int	ioflag,
	cred_t	*credp)
{
	xfs_inode_t	*ip;
	xfs_mount_t	*mp;
	xfs_trans_t	*tp;
	int		type;
	off_t		offset;
	size_t		count;
	int		error;
	off_t		n;
	int		resid;
	off_t		savedsize;
	xfs_fsize_t	limit;
	int		eventsent = 0;


	ASSERT(!(vp->v_vfsp->vfs_flag & VFS_RDONLY));

	ip = XFS_VTOI(vp);
	ASSERT(ismrlocked(&ip->i_iolock, MR_UPDATE) ||
	       (ismrlocked(&ip->i_iolock, MR_ACCESS) &&
		(ioflag & IO_DIRECT)));

	type = ip->i_d.di_mode & IFMT;
	ASSERT(type == IFREG || type == IFDIR ||
	       type == IFLNK || type == IFSOCK);

start:
	if (ioflag & IO_APPEND) {
		/*
		 * In append mode, start at the end of the file.
		 * Since I've got the iolock exclusive I can look
		 * at di_size.
		 */
		uiop->uio_offset = savedsize = ip->i_d.di_size;
	}

	offset = uiop->uio_offset;
	count = uiop->uio_resid;

#ifndef SIM
	if (MANDLOCK(vp, ip->i_d.di_mode) &&
	    (error = chklock(vp, FWRITE, offset, count, uiop->uio_fmode))) {
		return error;
	}
#endif

	if (offset < 0) {
		return XFS_ERROR(EINVAL);
	}
	if (count <= 0) {
		return 0;
	}

	switch (type) {
	case IFREG:
		limit = ((uiop->uio_limit < XFS_MAX_FILE_OFFSET) ?
			 uiop->uio_limit : XFS_MAX_FILE_OFFSET);
		n = limit - uiop->uio_offset;
		if (n <= 0) {
			return XFS_ERROR(EFBIG);
		}
		if (n < uiop->uio_resid) {
			resid = uiop->uio_resid - n;
			uiop->uio_resid = n;
		} else {
			resid = 0;
		}

		if (DM_EVENT_ENABLED(vp->v_vfsp, ip, DM_WRITE) &&
				!(ioflag & IO_INVIS) &&
				!eventsent) {
			if (error = dm_data_event(vp, DM_WRITE, offset, count))
				return error;
			eventsent = 1;
		}
		/*
		 *  The iolock is dropped and reaquired in dm_data_event(),
		 *  so we have to recheck the size when appending.  Will
		 *  only "goto start;" once, since having sent the event 
		 *  prevents another call to dm_data_event, which is what
		 *  allows the size to change in the first place.
		 */
		if ((ioflag & IO_APPEND) && savedsize != ip->i_d.di_size)
			goto start;

		/*
		 * If we're writing the file then make sure to clear the
		 * setuid and setgid bits if the process is not being run
		 * by root.  This keeps people from modifying setuid and
		 * setgid binaries.  Don't allow this to happen if this
		 * file is a swap file (I know, weird).
		 */
		if (((ip->i_d.di_mode & (ISUID | ISGID)) &&
		     (credp->cr_uid != 0)) &&
		    !(vp->v_flag & VISSWAP)) {
			error = xfs_write_clear_setuid(ip);
			if (error) {
				return error;
			}
		}
retry:
		if (ioflag & IO_DIRECT)
			error = xfs_diordwr( vp, uiop, ioflag, credp, B_WRITE);
		else
			error = xfs_write_file( vp, uiop, ioflag, credp );

		if (error == ENOSPC &&
		    DM_EVENT_ENABLED(vp->v_vfsp, ip, DM_NOSPACE) &&
		    !(ioflag & IO_INVIS)) {
			error = dm_data_event(vp, DM_NOSPACE, 0, 0);
			if (error) {
				return error;
			}
			offset = uiop->uio_offset;
			goto retry;
		} else if (error == ENOSPC) {
			mp = ip->i_mount;
			if (ip->i_d.di_flags & XFS_DIFLAG_REALTIME)  {
				xfs_error(mp, 2);
			} else {
				xfs_error(mp, 1);
			}
		}


		/*
		 * Add back whatever we refused to do because of
		 * uio_limit.
		 */
		uiop->uio_resid += resid;

		/*
		 * We've done at least a partial write, so don't
		 * return an error on this call.  Also update the
		 * timestamps since we changed the file.
		 */
		if (count != uiop->uio_resid) {
			error = 0;
			/* don't update timestamps if doing invisible I/O */
			if (!(ioflag & IO_INVIS))
				xfs_ichgtime(ip,
					XFS_ICHGTIME_MOD | XFS_ICHGTIME_CHG);
		}

		/*
		 * If the write was synchronous then we need to make
		 * sure that the inode modification time is permanent.
		 * We'll have update the timestamp above, so here
		 * we use a synchronous transaction to log the inode.
		 * It's not fast, but it's necessary.
		 *
		 * If the vnode is a swap vnode, then don't do anything
		 * which could require allocating memory.
		 */
		if ((ioflag & IO_SYNC) && !(vp->v_flag & VISSWAP)) {
			mp = ip->i_mount;
			tp = xfs_trans_alloc(mp, XFS_TRANS_WRITE_SYNC);
			if (error = xfs_trans_reserve(tp, 0,
						      XFS_SWRITE_LOG_RES(mp),
						      0, 0, 0)) {
				ASSERT(0);
				xfs_trans_cancel(tp, 0);
				break;
			}
			xfs_ilock(ip, XFS_ILOCK_EXCL);
			xfs_trans_ijoin(tp, ip, XFS_ILOCK_EXCL);
			xfs_trans_ihold(tp, ip);
			xfs_trans_log_inode(tp, ip, XFS_ILOG_CORE);
			xfs_trans_set_sync(tp);
			error = xfs_trans_commit(tp, 0);
			xfs_iunlock(ip, XFS_ILOCK_EXCL);
		}
		if ((ioflag & IO_DSYNC) && !(vp->v_flag & VISSWAP)) {
		    mp = ip->i_mount;
		    xfs_log_force(mp, (xfs_lsn_t)0,
				  XFS_LOG_FORCE | XFS_LOG_SYNC );
		}
		if (ioflag & IO_NFS) {
			xfs_refcache_insert(ip);
		}
		break;

	case IFDIR:
		return XFS_ERROR(EISDIR);

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
 * This is the XFS entry point for VOP_BMAP().
 * It simply switches based on the given flags
 * to either xfs_iomap_read() or xfs_iomap_write().
 * This cannot be used to grow a file or to read
 * beyond the end of the file.
 *
 * The caller is required to be holding the inode's
 * iolock in at least shared mode for a read mapping
 * and exclusively for a write mapping.
 */
/*ARGSUSED*/
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
	int		error;

	ip = XFS_VTOI(vp);
	ASSERT((ip->i_d.di_mode & IFMT) == IFREG);
	ASSERT((flags == B_READ) || (flags == B_WRITE));

	xfs_ilock(ip, XFS_ILOCK_EXCL);

	if (flags == B_READ) {
		ASSERT(ismrlocked(&ip->i_iolock, MR_ACCESS | MR_UPDATE) != 0);
		error = xfs_iomap_read(ip, offset, count, bmapp, nbmaps);
	} else {
		ASSERT(ismrlocked(&ip->i_iolock, MR_UPDATE) != 0);
		ASSERT(ip->i_d.di_size >= (offset + count));
		error = xfs_iomap_write(ip, offset, count, bmapp, nbmaps, 0);
	}

	xfs_iunlock(ip, XFS_ILOCK_EXCL);

	return error;
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
		bytes_off = rbp_offset + BBTOOFF(dpoff(bp->b_offset));
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

	if (BP_ISMAPPED(bp)) {
		bzero(bp->b_un.b_addr + data_offset, data_len);
		return;
	}

	data_offset += BBTOOFF(dpoff(bp->b_offset));
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
 * Verify that the gap list is properly sorted and that no entries
 * overlap.
 */
#ifdef DEBUG
STATIC void
xfs_check_gap_list(
	xfs_inode_t	*ip)
{
	xfs_gap_t	*last_gap;
	xfs_gap_t	*curr_gap;
	int		loops;

	last_gap = NULL;
	curr_gap = ip->i_gap_list;
	loops = 0;
	while (curr_gap != NULL) {
		ASSERT(curr_gap->xg_count_fsb > 0);
		if (last_gap != NULL) {
			ASSERT((last_gap->xg_offset_fsb +
				last_gap->xg_count_fsb) <
			       curr_gap->xg_offset_fsb);
		}
		last_gap = curr_gap;
		curr_gap = curr_gap->xg_next;
		ASSERT(loops++ < 1000);
	}
}
#endif

/*
 * For the given inode, offset, and count of bytes, build a list
 * of xfs_gap_t structures in the inode's gap list describing the
 * holes in the file in the range described by the offset and count.
 *
 * The list must be empty when we start, and the inode lock must
 * be held exclusively.
 */
STATIC int				/* error */
xfs_build_gap_list(
	xfs_inode_t	*ip,
	off_t		offset,
	size_t		count)
{
	xfs_fileoff_t	offset_fsb;
	xfs_fileoff_t	last_fsb;
	xfs_filblks_t	count_fsb;
	xfs_fsblock_t	firstblock;
	xfs_gap_t	*new_gap;
	xfs_gap_t	*last_gap;
	xfs_mount_t	*mp;
	int		i;
	int		error;
	int		nimaps;
#define	XFS_BGL_NIMAPS	8
	xfs_bmbt_irec_t	imaps[XFS_BGL_NIMAPS];
	xfs_bmbt_irec_t	*imapp;

	ASSERT(ismrlocked(&(ip->i_lock), MR_UPDATE) != 0);
	ASSERT(ip->i_gap_list == NULL);

	mp = ip->i_mount;
	offset_fsb = XFS_B_TO_FSBT(mp, offset);
	last_fsb = XFS_B_TO_FSB(mp, ((xfs_ufsize_t)(offset + count)));
	count_fsb = (xfs_filblks_t)(last_fsb - offset_fsb);
	ASSERT(count_fsb > 0);

	last_gap = NULL;
	while (count_fsb > 0) {
		nimaps = XFS_BGL_NIMAPS;
		firstblock = NULLFSBLOCK;
		error = xfs_bmapi(NULL, ip, offset_fsb, count_fsb,
				  0, &firstblock, 0, imaps, &nimaps, NULL);
		if (error) {
			return error;
		}
		ASSERT(nimaps != 0);

		/*
		 * Look for the holes in the mappings returned by bmapi.
		 * Decrement count_fsb and increment offset_fsb as we go.
		 */
		for (i = 0; i < nimaps; i++) {
			imapp = &imaps[i];
			count_fsb -= imapp->br_blockcount;
			ASSERT(((long)count_fsb) >= 0);
			ASSERT(offset_fsb == imapp->br_startoff);
			offset_fsb += imapp->br_blockcount;
			ASSERT(offset_fsb <= last_fsb);
			ASSERT((offset_fsb < last_fsb) || (count_fsb == 0));

			/*
			 * Skip anything that is not a hole.
			 */
			if (imapp->br_startblock != HOLESTARTBLOCK) {
				continue;
			}

			/*
			 * We found a hole.  Now add an entry to the inode's
			 * gap list corresponding to it.  The list is
			 * a singly linked, NULL terminated list.  We add
			 * each entry to the end of the list so that it is
			 * sorted by file offset.
			 */
			new_gap = kmem_zone_alloc(xfs_gap_zone, KM_SLEEP);
			new_gap->xg_offset_fsb = imapp->br_startoff;
			new_gap->xg_count_fsb = imapp->br_blockcount;
			new_gap->xg_next = NULL;

			if (last_gap == NULL) {
				ip->i_gap_list = new_gap;
			} else {
				last_gap->xg_next = new_gap;
			}
			last_gap = new_gap;
		}
	}
	xfs_check_gap_list(ip);
	return 0;
}

/*
 * Remove or trim any entries in the inode's gap list which overlap
 * the given range.  I'm going to assume for now that we never give
 * a range which is actually in the middle of an entry (i.e. we'd need
 * to split it in two).  This is a valid assumption for now given the
 * use of this in xfs_write_file() where we start at the front and
 * move sequentially forward.
 */
STATIC void
xfs_delete_gap_list(
	xfs_inode_t	*ip,
	xfs_fileoff_t	offset_fsb,
	xfs_extlen_t	count_fsb)
{
	xfs_gap_t	*curr_gap;
	xfs_gap_t	*last_gap;
	xfs_gap_t	*next_gap;
	xfs_fileoff_t	gap_offset_fsb;
	xfs_extlen_t	gap_count_fsb;
	xfs_fileoff_t	gap_end_fsb;
	xfs_fileoff_t	end_fsb;

	last_gap = NULL;
	curr_gap = ip->i_gap_list;
	while (curr_gap != NULL) {
		gap_offset_fsb = curr_gap->xg_offset_fsb;
		gap_count_fsb = curr_gap->xg_count_fsb;

		/*
		 * The entries are sorted by offset, so if we see
		 * one beyond our range we're done.
		 */
		end_fsb = offset_fsb + count_fsb;
		if (gap_offset_fsb >= end_fsb) {
			return;
		}

		gap_end_fsb = gap_offset_fsb + gap_count_fsb;
		if (gap_end_fsb <= offset_fsb) {
			/*
			 * This shouldn't be able to happen for now.
			 */
			ASSERT(0);
			last_gap = curr_gap;
			curr_gap = curr_gap->xg_next;
			continue;
		}

		/*
		 * We've go an overlap.  If the gap is entirely contained
		 * in the region then remove it.  If not, then shrink it
		 * by the amount overlapped.
		 */
		if (gap_end_fsb > end_fsb) {
			/*
			 * The region does not extend to the end of the gap.
			 * Shorten the gap by the amount in the region,
			 * and then we're done since we've reached the
			 * end of the region.
			 */
			ASSERT(gap_offset_fsb >= offset_fsb);
			curr_gap->xg_offset_fsb = end_fsb;
			curr_gap->xg_count_fsb = gap_end_fsb - end_fsb;
			return;
		}

		next_gap = curr_gap->xg_next;
		if (last_gap == NULL) {
			ip->i_gap_list = next_gap;
		} else {
			ASSERT(0);
			ASSERT(last_gap->xg_next == curr_gap);
			last_gap->xg_next = next_gap;
		}
		kmem_zone_free(xfs_gap_zone, curr_gap);
		curr_gap = next_gap;
	}
}		    
/*
 * Free up all of the entries in the inode's gap list.  This requires
 * the inode lock to be held exclusively.
 */
STATIC void
xfs_free_gap_list(
	xfs_inode_t	*ip)
{
	xfs_gap_t	*curr_gap;
	xfs_gap_t	*next_gap;

	ASSERT(ismrlocked(&(ip->i_lock), MR_UPDATE) != 0);
	xfs_check_gap_list(ip);

	curr_gap = ip->i_gap_list;
	while (curr_gap != NULL) {
		next_gap = curr_gap->xg_next;
		kmem_zone_free(xfs_gap_zone, curr_gap);
		curr_gap = next_gap;
	}
	ip->i_gap_list = NULL;
}

/*
 * Zero the parts of the buffer which overlap gaps in the inode's gap list.
 * Deal with everything in BBs since the buffer is not guaranteed to be block
 * aligned.
 */
STATIC void
xfs_cmp_gap_list_and_zero(
	xfs_inode_t	*ip,
	buf_t		*bp)
{
	off_t		bp_offset_bb;
	int		bp_len_bb;
	off_t		gap_offset_bb;
	int		gap_len_bb;
	int		zero_offset_bb;
	int		zero_len_bb;
	xfs_gap_t	*curr_gap;
	xfs_mount_t	*mp;

	ASSERT(ismrlocked(&(ip->i_lock), MR_UPDATE | MR_ACCESS) != 0);
	xfs_check_gap_list(ip);

	bp_offset_bb = bp->b_offset;
	bp_len_bb = BTOBB(bp->b_bcount);
	mp = ip->i_mount;
	curr_gap = ip->i_gap_list;
	while (curr_gap != NULL) {
		gap_offset_bb = XFS_FSB_TO_BB(mp, curr_gap->xg_offset_fsb);
		gap_len_bb = XFS_FSB_TO_BB(mp, curr_gap->xg_count_fsb);

		/*
		 * Check to see if this gap is before the buffer starts.
		 */
		if ((gap_offset_bb + gap_len_bb) <= bp_offset_bb) {
			curr_gap = curr_gap->xg_next;
			continue;
		}

		/*
		 * Check to see if this gap is after th buffer ends.
		 * If it is then we're done since the list is sorted
		 * by gap offset.
		 */
		if (gap_offset_bb >= (bp_offset_bb + bp_len_bb)) {
			break;
		}

		/*
		 * We found a gap which overlaps the buffer.  Zero
		 * the portion of the buffer overlapping the gap.
		 */
		if (gap_offset_bb < bp_offset_bb) {
			/*
			 * The gap starts before the buffer, so we start
			 * zeroing from the start of the buffer.
			 */
			zero_offset_bb = 0;
			/*
			 * To calculate the amount of overlap.  First
			 * subtract the portion of the gap which is before
			 * the buffer.  If the length is still longer than
			 * the buffer, then just zero the entire buffer.
			 */
			zero_len_bb = gap_len_bb -
				      (bp_offset_bb - gap_offset_bb);
			if (zero_len_bb > bp_len_bb) {
				zero_len_bb = bp_len_bb;
			}
			ASSERT(zero_len_bb > 0);
		} else {
			/*
			 * The gap starts at the beginning or in the middle
			 * of the buffer.  The offset into the buffer is
			 * the difference between the two offsets.
			 */
			zero_offset_bb = gap_offset_bb - bp_offset_bb;
			/*
			 * Figure out the length of the overlap.  If the
			 * gap extends beyond the end of the buffer, then
			 * just zero to the end of the buffer.  Otherwise
			 * just zero the length of the gap.
			 */
			if ((gap_offset_bb + gap_len_bb) >
			    (bp_offset_bb + bp_len_bb)) {
				zero_len_bb = bp_len_bb - zero_offset_bb;
			} else {
				zero_len_bb = gap_len_bb;
			}
		}

		/*
		 * Now that we've calculated the range of the buffer to
		 * zero, do it.
		 */
		xfs_zero_bp(bp, BBTOB(zero_offset_bb), BBTOB(zero_len_bb));

		curr_gap = curr_gap->xg_next;
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
STATIC int
xfs_strat_read(
	vnode_t	*vp,
	buf_t	*bp)
{
	xfs_fileoff_t	offset_fsb;
	xfs_fileoff_t   map_start_fsb;
	xfs_fileoff_t	imap_offset;
	xfs_fsblock_t	last_bp_bb;
	xfs_fsblock_t	last_map_bb;
	xfs_fsblock_t	firstblock;
	xfs_filblks_t	count_fsb;
	xfs_extlen_t	imap_blocks;
	xfs_fsize_t	isize;
	off_t		offset;
	off_t		end_offset;
	off_t		init_limit;
	int		x;
	caddr_t		datap;
	buf_t		*rbp;
	xfs_mount_t	*mp;
	xfs_inode_t	*ip;
	int		count;
	int		block_off;
	int		data_bytes;
	int		data_offset;
	int		nimaps;
	int		error;
#define	XFS_STRAT_READ_IMAPS	XFS_BMAP_MAX_NMAP
	xfs_bmbt_irec_t	imap[XFS_STRAT_READ_IMAPS];
	
	ASSERT(bp->b_blkno == -1);
	ip = XFS_VTOI(vp);
	mp = XFS_VFSTOM(vp->v_vfsp);
	offset_fsb = XFS_BB_TO_FSBT(mp, bp->b_offset);
	/*
	 * Only read up to the EOF or the current write offset.
	 * The idea here is to avoid initializing pages which are
	 * going to be immediately overwritten in xfs_write_file().
	 * The most important case is the sequential write case, where
	 * the new pages at the end of the file are sent here by
	 * chunk_patch().  We don't want to zero them since they
	 * are about to be overwritten.
	 *
	 * The ip->i_write_off tells us the offset of any write in
	 * progress.  If it is 0 then we assume that no write is
	 * in progress.  If the write offset is within the file size,
	 * the the file size is the upper limit.  If the write offset
	 * is beyond the file size, then we only want to initialize the
	 * buffer up to the write offset.  Beyond that will either be
	 * overwritten or be beyond the new EOF.
	 */
	isize = ip->i_d.di_size;
	offset = BBTOOFF(bp->b_offset);
	end_offset = offset + bp->b_bcount;

	if (ip->i_write_offset == 0) {
		init_limit = isize;
	} else if (ip->i_write_offset <= isize) {
		init_limit = isize;
	} else {
		init_limit = ip->i_write_offset;
	}

	if (end_offset <= init_limit) {
		count = bp->b_bcount;
	} else {
		count = init_limit - offset;
	}

	if (count <= 0) {
		iodone(bp);
		return 0;
	}

	/*
	 * Since the buffer may not be file system block aligned, we
	 * can't do a simple shift to find the number of blocks underlying
	 * it.  Instead we subtract the last block it sits on from the first.
	 */
	count_fsb = XFS_B_TO_FSB(mp, ((xfs_ufsize_t)(offset + count))) -
		    XFS_B_TO_FSBT(mp, offset);
	map_start_fsb = offset_fsb;
	xfs_ilock(ip, XFS_ILOCK_SHARED);
	while (count_fsb != 0) {
		nimaps = XFS_STRAT_READ_IMAPS;
		firstblock = NULLFSBLOCK;
		error = xfs_bmapi(NULL, ip, map_start_fsb, count_fsb, 0,
				  &firstblock, 0, imap, &nimaps, NULL);
		if (error) {
			xfs_iunlock(ip, XFS_ILOCK_SHARED);
			bp->b_flags |= B_ERROR;
			bp->b_error = error;
			return error;
		}
		ASSERT(nimaps >= 1);
		
		for (x = 0; x < nimaps; x++) {
			imap_offset = imap[x].br_startoff;
			ASSERT(imap_offset == map_start_fsb);
			imap_blocks = imap[x].br_blockcount;
			ASSERT(imap_blocks <= count_fsb);
			/*
			 * Calculate the offset of this mapping in the
			 * buffer and the number of bytes of this mapping
			 * that are in the buffer.  If the block size is
			 * greater than the page size, then the buffer may
			 * not line up on file system block boundaries
			 * (e.g. pages being read in from chunk_patch()).
			 * In that case we need to account for the space
			 * in the file system blocks underlying the buffer
			 * that is not actually a part of the buffer.  This
			 * space is the space in the first block before the
			 * start of the buffer and the space in the last
			 * block after the end of the buffer.
			 */
			data_offset = XFS_FSB_TO_B(mp,
						   imap_offset - offset_fsb);
			data_bytes = XFS_FSB_TO_B(mp, imap_blocks);
			block_off = 0;

			if (mp->m_sb.sb_blocksize > NBPP) {
				/*
				 * If the buffer is actually fsb
				 * aligned then this will simply
				 * subtract 0 and do no harm.  If the
				 * current mapping is for the start of
				 * the buffer, then data offset will be
				 * zero so we don't need to subtract out
				 * any space at the beginning.
				 */
				if (data_offset > 0) {
					data_offset -= BBTOB(
							XFS_BB_FSB_OFFSET(mp,
							      bp->b_offset));
				}

				if (map_start_fsb == offset_fsb) {
					ASSERT(data_offset == 0);
					/*
					 * This is on the first block
					 * mapped, so it must be the start
					 * of the buffer.  Subtract out from
					 * the number of bytes the bytes
					 * between the start of the block
					 * and the start of the buffer.
					 */
					data_bytes -=
						BBTOB(XFS_BB_FSB_OFFSET(
							mp, bp->b_offset));

					/*
					 * Set block_off to the number of
					 * BBs that the buffer is offset
					 * from the start of this mapping.
					 */
					block_off = XFS_BB_FSB_OFFSET(mp,
							    bp->b_offset);
					ASSERT(block_off >= 0);
				}

				if (imap_blocks == count_fsb) {
					/*
					 * This mapping includes the last
					 * block to be mapped.  Subtract out
					 * from the number of bytes the bytes
					 * between the end of the buffer and
					 * the end of the block.  It may
					 * be the case that the buffer
					 * extends beyond the mapping (if
					 * it is beyond the end of the file),
					 * in which case no adjustment
					 * is necessary.
					 */
					last_bp_bb = bp->b_offset +
						BTOBB(bp->b_bcount);
					last_map_bb =
						XFS_FSB_TO_BB(mp,
							      (imap_offset +
							       imap_blocks));

					if (last_map_bb > last_bp_bb) {
						data_bytes -=
							BBTOB(last_map_bb -
							      last_bp_bb);
					}

				}
			}
			ASSERT(data_bytes > 0);
			ASSERT(data_offset >= 0);
			if ((imap[x].br_startblock == DELAYSTARTBLOCK) ||
			    (imap[x].br_startblock == HOLESTARTBLOCK)) {
				/*
				 * This is either a hole or a delayed
				 * alloc extent.  Either way, just fill
				 * it with zeroes.
				 */
				datap = bp_mapin(bp);
				datap += data_offset;
				bzero(datap, data_bytes);
				if (!dpoff(bp->b_offset)) {
					bp_mapout(bp);
				}

			} else {
				/*
				 * The extent really exists on disk, so
				 * read it in.
				 */
				rbp = getrbuf(KM_SLEEP);
				xfs_overlap_bp(bp, rbp, data_offset,
					       data_bytes);
				rbp->b_blkno = XFS_FSB_TO_DB(mp, ip,
						   imap[x].br_startblock) +
					       block_off;
				rbp->b_offset = XFS_FSB_TO_BB(mp,
							      imap_offset) +
						block_off;
				rbp->b_flags |= B_READ;
				rbp->b_flags &= ~B_ASYNC;

				xfs_check_rbp(ip, bp, rbp, 1);
				bdstrat(bmajor(rbp->b_edev), rbp);
				iowait(rbp);

				if (rbp->b_flags & B_ERROR) {
					bp->b_flags |= B_ERROR;
					bp->b_error = XFS_ERROR(rbp->b_error);
					ASSERT(bp->b_error != EINVAL);
					error = bp->b_error;
				}

				/*
				 * Check to see if the block extent (or parts
				 * of it) have not yet been initialized and
				 * should therefore be zeroed.
				 */
				xfs_cmp_gap_list_and_zero(ip, rbp);

				if (BP_ISMAPPED(rbp)) {
					bp_mapout(rbp);
				}

				freerbuf(rbp);
			}
			count_fsb -= imap_blocks;
			map_start_fsb += imap_blocks;
		}
	}
	xfs_iunlock(ip, XFS_ILOCK_SHARED);
	iodone(bp);
	return error;
}


#ifdef DEBUG

void
xfs_strat_write_bp_trace(
	int		tag,
	xfs_inode_t	*ip,
	buf_t		*bp)
{
	if (ip->i_strat_trace == NULL) {
		return;
	}

	ktrace_enter(ip->i_strat_trace,
		     (void*)((unsigned long)tag),
		     (void*)ip,
		     (void*)((unsigned long)((ip->i_d.di_size > 32) & 0xffffffff)),
		     (void*)(ip->i_d.di_size & 0xffffffff),
		     (void*)bp,
		     (void*)((unsigned long)((bp->b_offset > 32) & 0xffffffff)),
		     (void*)(bp->b_offset & 0xffffffff),
		     (void*)((unsigned long)(bp->b_bcount)),
		     (void*)((unsigned long)(bp->b_bufsize)),
		     (void*)(bp->b_blkno),
		     (void*)((unsigned long)(bp->b_flags)),
		     (void*)(bp->b_pages),
		     (void*)(bp->b_pages->pf_pageno),
		     (void*)0,
		     (void*)0,
		     (void*)0);

	ktrace_enter(xfs_strat_trace_buf,
		     (void*)((unsigned long)tag),
		     (void*)ip,
		     (void*)((unsigned long)((ip->i_d.di_size > 32) & 0xffffffff)),
		     (void*)(ip->i_d.di_size & 0xffffffff),
		     (void*)bp,
		     (void*)((unsigned long)((bp->b_offset > 32) & 0xffffffff)),
		     (void*)(bp->b_offset & 0xffffffff),
		     (void*)((unsigned long)(bp->b_bcount)),
		     (void*)((unsigned long)(bp->b_bufsize)),
		     (void*)(bp->b_blkno),
		     (void*)((unsigned long)(bp->b_flags)),
		     (void*)(bp->b_pages),
		     (void*)(bp->b_pages->pf_pageno),
		     (void*)0,
		     (void*)0,
		     (void*)0);
}


void
xfs_strat_write_subbp_trace(
	int		tag,
	xfs_inode_t	*ip,
	buf_t		*bp,
	buf_t		*rbp,
	off_t		last_off,
	int		last_bcount,
	daddr_t		last_blkno)			    
{
	if (ip->i_strat_trace == NULL) {
		return;
	}

	ktrace_enter(ip->i_strat_trace,
		     (void*)((unsigned long)tag),
		     (void*)ip,
		     (void*)((unsigned long)((ip->i_d.di_size > 32) & 0xffffffff)),
		     (void*)(ip->i_d.di_size & 0xffffffff),
		     (void*)bp,
		     (void*)rbp,
		     (void*)((unsigned long)((rbp->b_offset > 32) & 0xffffffff)),
		     (void*)(rbp->b_offset & 0xffffffff),
		     (void*)((unsigned long)(rbp->b_bcount)),
		     (void*)(rbp->b_blkno),
		     (void*)((unsigned long)(rbp->b_flags)),
		     (void*)(rbp->b_un.b_addr),
		     (void*)(bp->b_pages),
		     (void*)(last_off),
		     (void*)((unsigned long)(last_bcount)),
		     (void*)(last_blkno));

	ktrace_enter(xfs_strat_trace_buf,
		     (void*)((unsigned long)tag),
		     (void*)ip,
		     (void*)((unsigned long)((ip->i_d.di_size > 32) & 0xffffffff)),
		     (void*)(ip->i_d.di_size & 0xffffffff),
		     (void*)bp,
		     (void*)rbp,
		     (void*)((unsigned long)((rbp->b_offset > 32) & 0xffffffff)),
		     (void*)(rbp->b_offset & 0xffffffff),
		     (void*)((unsigned long)(rbp->b_bcount)),
		     (void*)(rbp->b_blkno),
		     (void*)((unsigned long)(rbp->b_flags)),
		     (void*)(rbp->b_un.b_addr),
		     (void*)(bp->b_pages),
		     (void*)(last_off),
		     (void*)((unsigned long)(last_bcount)),
		     (void*)(last_blkno));
}

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
	xfs_filblks_t	buf_fsb,
	xfs_bmbt_irec_t	*imap,
	int		imap_count)
{
	xfs_filblks_t	count_fsb;
	xfs_fsblock_t	firstblock;
	int		nimaps;
	int		n;
	int		error;

	xfs_ilock(ip, XFS_ILOCK_SHARED);
	count_fsb = 0;
	while (count_fsb < buf_fsb) {
		nimaps = imap_count;
		firstblock = NULLFSBLOCK;
		error = xfs_bmapi(NULL, ip, (offset_fsb + count_fsb),
				  (buf_fsb - count_fsb), 0, &firstblock, 0,
				  imap, &nimaps, NULL);
		if (error) {
			xfs_iunlock(ip, XFS_ILOCK_SHARED);
			return;
		}
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
	

	s = mutex_spinlock(&xfs_strat_lock);
	ASSERT(rbp->b_flags & B_DONE);

	forw = (buf_t*)rbp->b_fsprivate2;
	back = (buf_t*)rbp->b_fsprivate;
	ASSERT(back != NULL);
	ASSERT(((buf_t *)back->b_fsprivate2) == rbp);
	ASSERT((forw == NULL) || (((buf_t *)forw->b_fsprivate) == rbp));

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
		mutex_spinunlock(&xfs_strat_lock, s);

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
		mutex_spinunlock(&xfs_strat_lock, s);
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

#ifdef DEBUG
/*ARGSUSED*/
void
xfs_check_rbp(
	xfs_inode_t	*ip,
	buf_t		*bp,
	buf_t		*rbp,
	int		locked)
{
	xfs_mount_t	*mp;
	int		nimaps;
	xfs_bmbt_irec_t	imap;
	xfs_fileoff_t	rbp_offset_fsb;
	xfs_filblks_t	rbp_len_fsb;
	pfd_t		*pfdp;
	xfs_fsblock_t	firstblock;
	int		error;

	mp = ip->i_mount;
	rbp_offset_fsb = XFS_BB_TO_FSBT(mp, rbp->b_offset);
	rbp_len_fsb = XFS_BB_TO_FSB(mp, rbp->b_offset+BTOBB(rbp->b_bcount)) -
		      XFS_BB_TO_FSBT(mp, rbp->b_offset);
	nimaps = 1;
	if (!locked) {
		xfs_ilock(ip, XFS_ILOCK_SHARED);
	}
	firstblock = NULLFSBLOCK;
	error = xfs_bmapi(NULL, ip, rbp_offset_fsb, rbp_len_fsb, 0,
			  &firstblock, 0, &imap, &nimaps, NULL);
	if (!locked) {
		xfs_iunlock(ip, XFS_ILOCK_SHARED);
	}
	if (error) {
		return;
	}

	ASSERT(imap.br_startoff == rbp_offset_fsb);
	ASSERT(imap.br_blockcount == rbp_len_fsb);
	ASSERT((XFS_FSB_TO_DB(mp, ip, imap.br_startblock) +
		XFS_BB_FSB_OFFSET(mp, rbp->b_offset)) ==
	       rbp->b_blkno);

	if (rbp->b_flags & B_PAGEIO) {
		pfdp = NULL;
		pfdp = getnextpg(rbp, pfdp);
		ASSERT(pfdp != NULL);
		ASSERT(dtopt(rbp->b_offset) == pfdp->pf_pageno);
		if (dpoff(rbp->b_offset)) {
			ASSERT(rbp->b_flags & B_MAPPED);
		}
	}

	if (rbp->b_flags & B_MAPPED) {
		ASSERT(BTOBB(poff(rbp->b_un.b_addr)) ==
		       dpoff(rbp->b_offset));
	}
}

/*
 * Verify that the given buffer is going to the right place in its
 * file.  Also check that it is properly mapped and points to the
 * right page.  We can only do a trylock from here in order to prevent
 * deadlocks, since this is called from the strategy routine.
 */
void
xfs_check_bp(
	xfs_inode_t	*ip,
	buf_t		*bp)
{
	xfs_mount_t	*mp;
	int		nimaps;
	xfs_bmbt_irec_t	imap;
	xfs_fileoff_t	bp_offset_fsb;
	xfs_filblks_t	bp_len_fsb;
	pfd_t		*pfdp;
	int		locked;
	xfs_fsblock_t	firstblock;
	int		error;

	mp = ip->i_mount;

	if (bp->b_flags & B_PAGEIO) {
		pfdp = NULL;
		pfdp = getnextpg(bp, pfdp);
		ASSERT(pfdp != NULL);
		ASSERT(dtopt(bp->b_offset) == pfdp->pf_pageno);
		if (dpoff(bp->b_offset)) {
			ASSERT(bp->b_flags & B_MAPPED);
		}
	}

	if (bp->b_flags & B_MAPPED) {
		ASSERT(BTOBB(poff(bp->b_un.b_addr)) ==
		       dpoff(bp->b_offset));
	}

	bp_offset_fsb = XFS_BB_TO_FSBT(mp, bp->b_offset);
	bp_len_fsb = XFS_BB_TO_FSB(mp, bp->b_offset + BTOBB(bp->b_bcount)) -
		     XFS_BB_TO_FSBT(mp, bp->b_offset);
	ASSERT(bp_len_fsb > 0);
	nimaps = 1;

	locked = xfs_ilock_nowait(ip, XFS_ILOCK_SHARED);
	if (!locked) {
		return;
	}
	firstblock = NULLFSBLOCK;
	error = xfs_bmapi(NULL, ip, bp_offset_fsb, bp_len_fsb, 0,
			  &firstblock, 0, &imap, &nimaps, NULL);
	xfs_iunlock(ip, XFS_ILOCK_SHARED);

	if (error) {
		return;
	}

	ASSERT(nimaps == 1);
	ASSERT(imap.br_startoff == bp_offset_fsb);
	ASSERT(imap.br_blockcount == bp_len_fsb);
	ASSERT((XFS_FSB_TO_DB(mp, ip, imap.br_startblock) +
		XFS_BB_FSB_OFFSET(mp, bp->b_offset)) ==
	       bp->b_blkno);
}
#endif /* DEBUG */


/*
 * This is called to convert all delayed allocation blocks in the given
 * range back to 'holes' in the file.  It is used when a buffer will not
 * be able to be written out due to disk errors in the allocation calls.
 */
STATIC void
xfs_delalloc_cleanup(
	xfs_inode_t	*ip,
	xfs_fileoff_t	start_fsb,
	xfs_filblks_t	count_fsb)
{
	xfs_fsblock_t	first_block;
	int		nimaps;
	int		done;
	int		error;
	int		n;
#define	XFS_CLEANUP_MAPS	4
	xfs_bmbt_irec_t	imap[XFS_CLEANUP_MAPS];

	ASSERT(count_fsb < 0xffff000);
	xfs_ilock(ip, XFS_ILOCK_EXCL);
	while (count_fsb != 0) {
		first_block = NULLFSBLOCK;
		nimaps = XFS_CLEANUP_MAPS;
		error = xfs_bmapi(NULL, ip, start_fsb, count_fsb, 0,
				  &first_block, 1, imap, &nimaps, NULL);
		if (error) {
			/*
			 * We should never get errors in this case, but if
			 * we do then just bail out.
			 */
			ASSERT(0);
			xfs_iunlock(ip, XFS_ILOCK_EXCL);
			return;
		}

		ASSERT(nimaps > 0);
		n = 0;
		while (n < nimaps) {
			if (imap[n].br_startblock == DELAYSTARTBLOCK) {
				error = xfs_bunmapi(NULL, ip,
						    imap[n].br_startoff,
						    imap[n].br_blockcount,
						    0, 1, &first_block, NULL,
						    &done);
				if (error) {
					/*
					 * We should never get errors in
					 * this case, but if
					 * we do then just bail out.
					 */
					ASSERT(0);
					xfs_iunlock(ip, XFS_ILOCK_EXCL);
					return;
				}
				ASSERT(done);
			}
			start_fsb += imap[n].br_blockcount;
			count_fsb -= imap[n].br_blockcount;
			ASSERT(count_fsb < 0xffff000);
			n++;
		}
	}
	xfs_iunlock(ip, XFS_ILOCK_EXCL);
}		     

STATIC int
xfs_strat_write(
	vnode_t	*vp,
	buf_t	*bp)
{
	xfs_fileoff_t	offset_fsb;
	xfs_fileoff_t   map_start_fsb;
	xfs_fileoff_t	imap_offset;
	xfs_fsblock_t	first_block;
	xfs_filblks_t	count_fsb;
	xfs_extlen_t	imap_blocks;
#ifdef DEBUG
	off_t		last_rbp_offset;
	xfs_extlen_t	last_rbp_bcount;
	daddr_t		last_rbp_blkno;
#endif
	int		rbp_count;
	buf_t		*rbp;
	xfs_mount_t	*mp;
	xfs_inode_t	*ip;
	xfs_trans_t	*tp;
	int		error;
	xfs_bmap_free_t	free_list;
	xfs_bmbt_irec_t	*imapp;
	int		rbp_offset;
	int		rbp_len;
	int		set_lead;
	int		s;
	int		loops;
	int		imap_index;
	int		nimaps;
	int		committed;
	xfs_bmbt_irec_t	imap[XFS_BMAP_MAX_NMAP];
#define	XFS_STRAT_WRITE_IMAPS	2
	/*
	 * If XFS_STRAT_WRITE_IMAPS is changed then the definition
	 * of XFS_STRATW_LOG_RES in xfs_trans.h must be changed to
	 * reflect the new number of extents that can actually be
	 * allocated in a single transaction.
	 */
	 
	XFSSTATS.xs_xstrat_bytes += bp->b_bcount;

	ip = XFS_VTOI(vp);
	mp = ip->i_mount;
	set_lead = 0;
	rbp_count = 0;
	error = 0;
	bp->b_flags |= B_STALE;

	ASSERT(bp->b_blkno == -1);
	offset_fsb = XFS_BB_TO_FSBT(mp, bp->b_offset);
	count_fsb = XFS_B_TO_FSB(mp, bp->b_bcount);
	xfs_strat_write_check(ip, offset_fsb,
			      count_fsb, imap,
			      XFS_STRAT_WRITE_IMAPS);
	map_start_fsb = offset_fsb;
	while (count_fsb != 0) {
		/*
		 * Set up a transaction with which to allocate the
		 * backing store for the file.  Do allocations in a
		 * loop until we get some space in the range we are
		 * interested in.  The other space that might be allocated
		 * is in the delayed allocation extent on which we sit
		 * but before our buffer starts.
		 */
		nimaps = 0;
		loops = 0;
		while (nimaps == 0) {
			tp = xfs_trans_alloc(mp,
					     XFS_TRANS_STRAT_WRITE);
			error = xfs_trans_reserve(tp, 0,
					XFS_WRITE_LOG_RES(mp),
					0, XFS_TRANS_PERM_LOG_RES,
					XFS_WRITE_LOG_COUNT);
			if (error) {
				xfs_trans_cancel(tp,
						 XFS_TRANS_RELEASE_LOG_RES);
				bp->b_flags |= B_ERROR;
				bp->b_error = error;
				goto error0;
			}
			ASSERT(error == 0);
			xfs_ilock(ip, XFS_ILOCK_EXCL);
			xfs_trans_ijoin(tp, ip,
					XFS_ILOCK_EXCL);
			xfs_trans_ihold(tp, ip);
			xfs_strat_write_bp_trace(XFS_STRAT_ENTER,
						 ip, bp);

			/*
			 * Allocate the backing store for the file.
			 */
			XFS_BMAP_INIT(&(free_list),
				      &(first_block));
			nimaps = XFS_STRAT_WRITE_IMAPS;
			error = xfs_bmapi(tp, ip, map_start_fsb, count_fsb,
					  XFS_BMAPI_WRITE, &first_block, 1,
					  imap, &nimaps, &free_list);
			if (error) {
				xfs_bmap_cancel(&free_list);
				xfs_trans_cancel(tp,
						 (XFS_TRANS_RELEASE_LOG_RES |
						  XFS_TRANS_ABORT));
				xfs_iunlock(ip, XFS_ILOCK_EXCL);
				bp->b_flags |= B_ERROR;
				bp->b_error = error;
				goto error0;
			}
			ASSERT(loops++ <=
			       (offset_fsb +
				XFS_B_TO_FSB(mp, bp->b_bcount)));
			error = xfs_bmap_finish(&(tp), &(free_list),
						first_block, &committed);
			if (error) {
				xfs_bmap_cancel(&free_list);
				xfs_trans_cancel(tp,
						 (XFS_TRANS_RELEASE_LOG_RES |
						  XFS_TRANS_ABORT));
				xfs_iunlock(ip, XFS_ILOCK_EXCL);
				bp->b_flags |= B_ERROR;
				bp->b_error = error;
				goto error0;
			}
			error = xfs_trans_commit(tp,
						 XFS_TRANS_RELEASE_LOG_RES);
			if (error) {
				xfs_iunlock(ip, XFS_ILOCK_EXCL);
				bp->b_flags |= B_ERROR;
				bp->b_error = error;
				goto error0;
			}

			/*
			 * Before dropping the lock, clear any read-ahead
			 * state since in allocating space here we may have
			 * made it invalid.
			 */
			XFS_INODE_CLEAR_READ_AHEAD(ip);
			xfs_iunlock(ip, XFS_ILOCK_EXCL);
		}

		/*
		 * This is a quick check to see if the first time through
		 * was able to allocate a single extent over which to
		 * write.
		 */
		if ((map_start_fsb == offset_fsb) &&
		    (imap[0].br_blockcount == count_fsb)) {
			ASSERT(nimaps == 1);
			bp->b_blkno = XFS_FSB_TO_DB(mp, ip,
					    imap[0].br_startblock);
			bp->b_bcount = XFS_FSB_TO_B(mp,
						    count_fsb);
			xfs_strat_write_bp_trace(XFS_STRAT_FAST,
						 ip, bp);
			xfs_check_bp(ip, bp);
			bdstrat(bmajor(bp->b_edev), bp);
			/*
			 * Drop the count of queued buffers.
			 */
			atomicAddInt(&(ip->i_queued_bufs), -1);
			ASSERT(ip->i_queued_bufs >= 0);
			XFSSTATS.xs_xstrat_quick++;
			return 0;
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
		XFSSTATS.xs_xstrat_split++;
		imap_index = 0;
		if (!set_lead) {
			bp->b_flags |= B_LEADER | B_PARTIAL;
			set_lead = 1;
		}
		while (imap_index < nimaps) {
			rbp = getrbuf(KM_SLEEP);

			imapp = &(imap[imap_index]);
			ASSERT((imapp->br_startblock !=
				DELAYSTARTBLOCK) &&
			       (imapp->br_startblock !=
				HOLESTARTBLOCK));
			imap_offset = imapp->br_startoff;
			rbp_offset = XFS_FSB_TO_B(mp,
						  imap_offset -
						  offset_fsb);
			imap_blocks = imapp->br_blockcount;
			ASSERT((imap_offset + imap_blocks) <=
			       (offset_fsb +
				XFS_B_TO_FSB(mp, bp->b_bcount)));
			rbp_len = XFS_FSB_TO_B(mp,
					       imap_blocks);
			xfs_overlap_bp(bp, rbp, rbp_offset,
				       rbp_len);
			rbp->b_blkno =
				XFS_FSB_TO_DB(mp, ip,
					      imapp->br_startblock);
			rbp->b_offset = XFS_FSB_TO_BB(mp,
						      imap_offset);
			xfs_strat_write_subbp_trace(XFS_STRAT_SUB,
						    ip, bp,
						    rbp,
						    last_rbp_offset,
						    last_rbp_bcount,
						    last_rbp_blkno);
#ifdef DEBUG
			xfs_check_rbp(ip, bp, rbp, 0);
			if (rbp_count > 0) {
				ASSERT((last_rbp_offset +
					BTOBB(last_rbp_bcount)) ==
				       rbp->b_offset);
				ASSERT((rbp->b_blkno <
					last_rbp_blkno) ||
				       (rbp->b_blkno >=
					(last_rbp_blkno +
					 BTOBB(last_rbp_bcount))));
				if (rbp->b_blkno <
				    last_rbp_blkno) {
					ASSERT((rbp->b_blkno +
					      BTOBB(rbp->b_bcount)) <
					       last_rbp_blkno);
				}
			}
			last_rbp_offset = rbp->b_offset;
			last_rbp_bcount = rbp->b_bcount;
			last_rbp_blkno = rbp->b_blkno;
#endif
					       
			
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
			s = mutex_spinlock(&xfs_strat_lock);
			rbp->b_fsprivate = bp;
			rbp->b_fsprivate2 = bp->b_fsprivate2;
			if (bp->b_fsprivate2 != NULL) {
				((buf_t*)(bp->b_fsprivate2))->b_fsprivate =
								rbp;
			}
			bp->b_fsprivate2 = rbp;
			mutex_spinunlock(&xfs_strat_lock, s);

			rbp->b_relse = xfs_strat_write_relse;
			rbp->b_flags |= B_ASYNC;

			bdstrat(bmajor(rbp->b_edev), rbp);

			map_start_fsb +=
				imapp->br_blockcount;
			count_fsb -= imapp->br_blockcount;
			ASSERT(count_fsb < 0xffff000);

			imap_index++;
		}
	}

	/*
	 * Now that we've issued all the partial I/Os, check to see
	 * if they've all completed.  If they have then mark the buffer
	 * as done, otherwise clear the B_PARTIAL flag in the buffer to
	 * indicate that the last subordinate buffer to complete should
	 * mark the buffer done.  Also, drop the count of queued buffers
	 * now that we know that all the space underlying the buffer has
	 * been allocated and it has really been sent out to disk.
	 *
	 * Use set_lead to tell whether we kicked off any partial I/Os
	 * or whether we jumped here after an error before issuing any.
	 */
 error0:
	atomicAddInt(&(ip->i_queued_bufs), -1);
	ASSERT(ip->i_queued_bufs >= 0);
	if (error) {
		ASSERT(count_fsb != 0);
		/*
		 * Since we're never going to convert the remaining
		 * delalloc blocks beneath this buffer into real block,
		 * get rid of them now.
		 */
		xfs_delalloc_cleanup(ip, map_start_fsb, count_fsb);
	}
	if (set_lead) {
		s = mutex_spinlock(&xfs_strat_lock);
		ASSERT((bp->b_flags & (B_DONE | B_PARTIAL)) == B_PARTIAL);
		ASSERT(bp->b_flags & B_LEADER);
		
		if (bp->b_fsprivate2 == NULL) {
			/*
			 * All of the subordinate buffers have completed.
			 * Call iodone() to note that the I/O has completed.
			 */
			bp->b_flags &= ~(B_PARTIAL | B_LEADER);
			mutex_spinunlock(&xfs_strat_lock, s);

			iodone(bp);
			return error;
		}

		bp->b_flags &= ~B_PARTIAL;
		mutex_spinunlock(&xfs_strat_lock, s);
	} else {
		biodone(bp);
	}
	return error;
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
	int		s;

	/*
	 * If this is just a buffer whose underlying disk space
	 * is already allocated, then just do the requested I/O.
	 */
	if (bp->b_blkno >= 0) {
		xfs_check_bp(XFS_VTOI(vp), bp);
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
		s = mp_mutex_spinlock(&xfsd_lock);
		/*
		 * Queue the buffer at the end of the list.
		 * Bump the inode count of the number of queued buffers.
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
		ASSERT(XFS_VTOI(vp)->i_queued_bufs >= 0);
		atomicAddInt(&(XFS_VTOI(vp)->i_queued_bufs), 1);
		(void)sv_signal(&xfsd_wait);
		mp_mutex_spinunlock(&xfsd_lock, s);
	} else {
		/*
		 * We're not going to queue it for the xfsds, but bump the
		 * inode's count anyway so that we can tell that this
		 * buffer is still on its way out.
		 */
		ASSERT(XFS_VTOI(vp)->i_queued_bufs >= 0);
		atomicAddInt(&(XFS_VTOI(vp)->i_queued_bufs), 1);
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

	s = mp_mutex_spinlock(&xfsd_lock);
	xfsd_count++;

	while (1) {
		while (xfsd_list == NULL) {
			mp_sv_wait(&xfsd_wait, PRIBIO, &xfsd_lock, s);
			s = mp_mutex_spinlock(&xfsd_lock);
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
		xfsd_bufcount--;;
		ASSERT(xfsd_bufcount >= 0);

		mp_mutex_spinunlock(&xfsd_lock, s);
		bp->av_forw = bp;
		bp->av_back = bp;

		ASSERT((bp->b_flags & (B_BUSY | B_ASYNC | B_READ)) ==
		       (B_BUSY | B_ASYNC));
		XFSSTATS.xs_xfsd_bufs++;
		xfs_strat_write(bp->b_vp, bp);

		s = mp_mutex_spinlock(&xfsd_lock);
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
	xfs_bmbt_irec_t	imaps[XFS_BMAP_MAX_NMAP], *imapp;
	buf_t		*bps[XFS_BMAP_MAX_NMAP], *nbp;
	xfs_fileoff_t	offset_fsb;
	xfs_fsblock_t	firstfsb;
	xfs_filblks_t	count_fsb, datablocks;
	xfs_bmap_free_t free_list;
	caddr_t		base;
	ssize_t		resid, count, totxfer;
	off_t		offset, offset_this_req, bytes_this_req, trail = 0;
	int		i, j, error, writeflag, reccount;
	int		end_of_file, bufsissued, totresid, exist;
	int		blk_algn, rt, numrtextents, sbrtextsize, iprtextsize;
	int		committed;
	uint		lock_mode;
	xfs_fsize_t	new_size;

	dp        = (struct dio_s *)bp->b_private;
	vp        = dp->vp;
	ip        = XFS_VTOI(vp); 
	mp 	  = XFS_VFSTOM(XFS_ITOV(ip)->v_vfsp);
	base	  = bp->b_un.b_addr;
	error     = resid = totxfer = end_of_file = 0;
	offset    = BBTOOFF((off_t)bp->b_blkno);
	blk_algn  = 0;
	totresid  = count  = bp->b_bcount;

	/*
 	 * Determine if this file is using the realtime volume.
	 */
	if ( ip->i_d.di_flags & XFS_DIFLAG_REALTIME )  {
		rt = 1;
		sbrtextsize = mp->m_sb.sb_rextsize;
		iprtextsize = sbrtextsize;
		if ( ip->i_d.di_extsize ) {
			iprtextsize = ip->i_d.di_extsize;
		}
	} else {
		numrtextents = 0;
		iprtextsize = sbrtextsize = 0;
		rt = 0;
	}

	ASSERT(!(bp->b_flags & B_DONE));
        ASSERT(ismrlocked(&ip->i_iolock, MR_ACCESS| MR_UPDATE) != 0);

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
	if ( (offset & mp->m_blockmask) != 0 ) {
		/*
 		 * The request is NOT on a file system block boundary.
		 */
		blk_algn = OFFTOBB(offset & mp->m_blockmask);
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
		count_fsb  = XFS_B_TO_FSB( mp, count);

		tp = NULL;
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
			error = xfs_bmapi( NULL, ip, offset_fsb, 
				count_fsb, 0, &firstfsb, 0, imaps, 
				&reccount, 0);
			if (error) {
				xfs_iunlock( ip, XFS_ILOCK_EXCL );
				break;
			}
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
					numrtextents = 
						(count_fsb + iprtextsize - 1)/
							sbrtextsize;

					datablocks = 0;
				} else {
					/*
					 * If this is a write to the data
					 * partition, reserve the space.
					 */
					datablocks = count_fsb;
				}

				/*
 				 * Setup transactions.
 				 */
				tp = xfs_trans_alloc( mp, XFS_TRANS_DIOSTRAT);

				xfs_iunlock( ip, XFS_ILOCK_EXCL );
				error = xfs_trans_reserve( tp, 
					   XFS_BM_MAXLEVELS(mp, XFS_DATA_FORK) + datablocks, 
					   XFS_WRITE_LOG_RES(mp),
					   numrtextents,
					   XFS_TRANS_PERM_LOG_RES,
					   XFS_WRITE_LOG_COUNT );
				xfs_ilock( ip, XFS_ILOCK_EXCL );

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
		error = xfs_bmapi( tp, ip, offset_fsb, count_fsb, 
			writeflag, &firstfsb, 0, imapp, &reccount, &free_list);

		if ( writeflag ) {
			if (error) {
				xfs_bmap_cancel(&free_list);
				xfs_trans_cancel(tp,
						 (XFS_TRANS_RELEASE_LOG_RES |
						  XFS_TRANS_ABORT));
				xfs_iunlock( ip, XFS_ILOCK_EXCL );
				break;
			}
			/*
 			 * Complete the bmapi() transactions.
			 */
			if (!exist) {
			    error = xfs_bmap_finish( &tp, &free_list,
						     firstfsb, &committed );
			    if (error) {
				    xfs_bmap_cancel(&free_list);
				    xfs_trans_cancel(tp,
						 (XFS_TRANS_RELEASE_LOG_RES |
						  XFS_TRANS_ABORT));
				    xfs_iunlock( ip, XFS_ILOCK_EXCL );
				    break;
			    }
			    xfs_trans_commit(tp, XFS_TRANS_RELEASE_LOG_RES );
			}
			xfs_iunlock( ip, XFS_ILOCK_EXCL);
		} else {
			xfs_iunlock_map_shared( ip, lock_mode);
			if (error) {
				break;
			}
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

			ASSERT(bytes_this_req);

			offset_this_req = XFS_FSB_TO_B(mp,
				imapp->br_startoff) + BBTOB(blk_algn); 

			/*
			 * Reduce request size, if it
			 * is longer than user buffer.
			 */
			if ( bytes_this_req > count ) {
				 bytes_this_req = count;
			}

			/*
			 * Check if this is the end of the file.
			 */
			new_size = offset_this_req + bytes_this_req;
			if (new_size >ip->i_d.di_size){
				if ( writeflag ) {
					/*
					 * File is being extended on a
					 * write, update the file size if
					 * someone else didn't make it even
					 * bigger.
					 */
			         	ASSERT((vp->v_flag & VISSWAP) == 0);
					xfs_ilock(ip, XFS_ILOCK_EXCL);
					if (new_size > ip->i_d.di_size) {
				 		ip->i_d.di_size =
							offset_this_req + 
							bytes_this_req;
						ip->i_update_core = 1;
					}
					xfs_iunlock(ip, XFS_ILOCK_EXCL);
				} else {
					/*
 				 	 * If trying to read past end of
 				 	 * file, shorten the request size.
					 */
					if (ip->i_d.di_size > offset_this_req) {
						trail = ip->i_d.di_size - 
							offset_this_req;
						bytes_this_req = trail;
						bytes_this_req &= ~BBMASK;
						bytes_this_req += BBSIZE;
					} else {
						bytes_this_req =  0;
					}




					end_of_file = 1;

					if (!bytes_this_req) {
						break;
					}
				}
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

				nbp->b_grio_private = bp->b_grio_private;

	     			nbp->b_error     = 0;
	     			nbp->b_proc      = bp->b_proc;
	     			nbp->b_edev      = bp->b_edev;
				if (rt) {
	     				nbp->b_blkno = XFS_FSB_TO_BB(mp,
						imapp->br_startblock);
				} else {
	     				nbp->b_blkno = XFS_FSB_TO_DADDR(mp,
						imapp->br_startblock) + 
						blk_algn;
				}
				ASSERT(bytes_this_req);
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
					nbp->b_grio_private = 0;
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
				if (trail) 
					totxfer += trail;
				else
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
			nbp->b_flags2 &= ~B_GR_BUF;

	     		if (!error)
				error = geterror( nbp );

	     		if (!error && !resid) {
				resid = nbp->b_resid;

				/*
				 * prevent adding up partial xfers
				 */
				if (trail && (j == (bufsissued -1 )) ) {
					if (resid <= (nbp->b_bcount - trail) )
						totxfer += trail;
				} else {
					totxfer += (nbp->b_bcount - resid);
				}
			} 
	    	 	nbp->b_flags		= 0;
	     		nbp->b_bcount		= 0;
	     		nbp->b_un.b_addr	= 0;
	     		nbp->b_grio_private	= 0;
	    	 	putphysbuf( nbp );
	     	}
	} /* end of while loop */

	/*
 	 * Fill in resid count for original buffer.
	 * if any of the io's fail, the whole thing fails
	 */
	if ( error ) {
		totxfer = 0;
	}

	bp->b_resid = totresid - totxfer;

	/*
 	 *  Update the inode timestamp if file was written.
 	 */
	if ( writeflag ) {
		xfs_ilock(ip, XFS_ILOCK_EXCL);
		if ((ip->i_d.di_mode & (ISUID|ISGID)) && dp->cr->cr_uid != 0){
			ip->i_d.di_mode &= ~ISUID;
			if (ip->i_d.di_mode & (IEXEC >> 3))
				ip->i_d.di_mode &= ~ISGID;
		}
		xfs_ichgtime(ip, XFS_ICHGTIME_MOD | XFS_ICHGTIME_CHG);
		xfs_iunlock(ip, XFS_ILOCK_EXCL);
	}

	/*
	 * Issue completion on the original buffer.
	 */
	bioerror( bp, error);
	iodone( bp );

        ASSERT(ismrlocked(&ip->i_iolock, MR_ACCESS| MR_UPDATE) != 0);

	return(0);
}

/*
 * xfs_diordwr()
 *	This routine sets up a buf structure to be used to perform 
 * 	direct I/O operations to user space. The user specified 
 *	parameters are checked for alignment and size limitations. A buf
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
	extern 		zone_t	*grio_buf_data_zone;

	xfs_inode_t	*ip;
	struct dio_s	dp;
	xfs_mount_t	*mp;
	uuid_t		stream_id;
	buf_t		*bp;
	int		error, index;
	__int64_t	iosize;

	ip = XFS_VTOI(vp);
	mp = XFS_VFSTOM(vp->v_vfsp);

	/*
 	 * Check that the user buffer address is on a BBISZE offset, 
	 * while file offset, and
 	 * request size are all multiples of file system block size. 
	 * This prevents the need for read/modify/write operations.
	 *
	 * This enforces the alignment restrictions indicated by 
 	 * the F_DIOINFO fcntl call.
	 *
	 * We make an exception for swap I/O.  This will always be
	 * page aligned and all the blocks will already be allocated,
	 * so we don't need to worry about read/modify/write stuff.
 	 */
	if (!(vp->v_flag & VISSWAP) &&
	    ((((long)(uiop->uio_iov->iov_base)) & BBMASK) ||
	     (uiop->uio_offset & mp->m_blockmask) ||
	     (uiop->uio_resid & mp->m_blockmask))) {

		/*
		 * if the user tries to start reading at the
		 * end of the file, just return 0.
		 */
		if ( (rw & B_READ) &&
	  	     (uiop->uio_offset == ip->i_d.di_size) ) {
			return( 0 );
		}
#ifndef SIM
		return XFS_ERROR(EINVAL);
#endif
	}

	/*
 	 * Do maxio check.
 	 */
	if (uiop->uio_resid > ctooff(v.v_maxdmasz)) {
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

		/*
 	 	 * Check if this is a guaranteed rate I/O
	 	 */
		if ( xfs_io_is_guaranteed( ip, &stream_id ) ) {
			bp->b_flags2 |= B_GR_BUF;
			ASSERT( bp->b_grio_private == NULL );
			bp->b_grio_private = 
				kmem_zone_alloc( grio_buf_data_zone, KM_SLEEP );
			ASSERT( BUF_GRIO_PRIVATE( bp ) );
			COPY_STREAM_ID(stream_id,BUF_GRIO_PRIVATE(bp)->grio_id);
			iosize =  uiop->uio_iov[0].iov_len;
			index = grio_monitor_io_start( &stream_id, iosize );
		} else {
			bp->b_grio_private = NULL;
			bp->b_flags2 &= ~B_GR_BUF;
		}
	} else {
		bp->b_edev = mp->m_dev;
		bp->b_grio_private = NULL;
		bp->b_flags2 &= ~B_GR_BUF;
	}


	/*
 	 * Perform I/O operation.
	 */
	error = biophysio(xfs_diostrat, bp, bp->b_edev, rw, 
		(daddr_t)OFFTOBB(uiop->uio_offset), uiop);

	/*
 	 * Free local buf structure.
 	 */
	bp->b_flags = 0;

	if ( bp->b_flags2 & B_GR_BUF ) {
		grio_monitor_io_end( &stream_id, index );

		ASSERT( BUF_GRIO_PRIVATE(bp) );
		kmem_zone_free( grio_buf_data_zone, BUF_GRIO_PRIVATE(bp));
		bp->b_grio_private = NULL;
		bp->b_flags2 &= ~B_GR_BUF;
	}

#ifdef SIM
	bp->b_un.b_addr = 0;
#endif
	putphysbuf(bp);

	return( error );
}




mutex_t		xfs_refcache_lock;
xfs_inode_t	**xfs_refcache;
int		xfs_refcache_size;
int		xfs_refcache_index;
int		xfs_refcache_busy;
int		xfs_refcache_count;

/*
 * Insert the given inode into the reference cache.
 */
void
xfs_refcache_insert(
	xfs_inode_t	*ip)
{
	int		s;
	vnode_t		*vp;
	xfs_inode_t	*release_ip;
	xfs_inode_t	**refcache;

	ASSERT(ismrlocked(&(ip->i_iolock), MR_UPDATE));

	/*
	 * If an unmount is busy blowing entries out of the cache,
	 * then don't bother.
	 */
	if (xfs_refcache_busy) {
		return;
	}

	/*
	 * The inode is already in the refcache, so don't bother
	 * with it.
	 */
	if (ip->i_refcache != NULL) {
		return;
	}

	vp = XFS_ITOV(ip);
	ASSERT(vp->v_count > 0);
	VN_HOLD(vp);

	/*
	 * We allocate the reference cache on use so that we don't
	 * waste the memory on systems not being used as NFS servers.
	 */
	if (xfs_refcache == NULL) {
		refcache = (xfs_inode_t **)kmem_zalloc(xfs_refcache_size *
						       sizeof(xfs_inode_t *),
						       KM_SLEEP);
	} else {
		refcache = NULL;
	}

	s = mp_mutex_spinlock(&xfs_refcache_lock);

	/*
	 * If we allocated memory for the refcache above and it still
	 * needs it, then use the memory we allocated.  Otherwise we'll
	 * free the memory below.
	 */
	if (refcache != NULL) {
		if (xfs_refcache == NULL) {
			xfs_refcache = refcache;
			refcache = NULL;
		}
	}

	/*
	 * If an unmount is busy clearing out the cache, don't add new
	 * entries to it.
	 */
	if ((xfs_refcache_busy) || (vp->v_vfsp->vfs_flag & VFS_OFFLINE)) {
		mp_mutex_spinunlock(&xfs_refcache_lock, s);
		VN_RELE(vp);
		/*
		 * If we allocated memory for the refcache above but someone
		 * else beat us to using it, then free the memory now.
		 */
		if (refcache != NULL) {
			kmem_free(refcache,
				  xfs_refcache_size * sizeof(xfs_inode_t *));
		}
		return;
	}
	release_ip = xfs_refcache[xfs_refcache_index];
	if (release_ip != NULL) {
		release_ip->i_refcache = NULL;
		xfs_refcache_count--;
		ASSERT(xfs_refcache_count >= 0);
	}
	xfs_refcache[xfs_refcache_index] = ip;
	ASSERT(ip->i_refcache == NULL);
	ip->i_refcache = &(xfs_refcache[xfs_refcache_index]);
	xfs_refcache_count++;
	ASSERT(xfs_refcache_count <= xfs_refcache_size);
	xfs_refcache_index++;
	if (xfs_refcache_index == xfs_refcache_size) {
		xfs_refcache_index = 0;
	}
	mp_mutex_spinunlock(&xfs_refcache_lock, s);

	/*
	 * Save the pointer to the inode to be released so that we can
	 * VN_RELE it once we've dropped our inode locks in xfs_rwunlock().
	 * The pointer may be NULL, but that's OK.
	 */
	ip->i_release = release_ip;

	/*
	 * If we allocated memory for the refcache above but someone
	 * else beat us to using it, then free the memory now.
	 */
	if (refcache != NULL) {
		kmem_free(refcache,
			  xfs_refcache_size * sizeof(xfs_inode_t *));
	}
	return;
}


/*
 * If the given inode is in the reference cache, purge its entry and
 * release the reference on the vnode.
 */
void
xfs_refcache_purge_ip(
	xfs_inode_t	*ip)
{
	int	s;
	vnode_t	*vp;

	/*
	 * If we're not pointing to our entry in the cache, then
	 * we must not be in the cache.
	 */
	if (ip->i_refcache == NULL) {
		return;
	}

	s = mp_mutex_spinlock(&xfs_refcache_lock);
	if (ip->i_refcache == NULL) {
		mp_mutex_spinunlock(&xfs_refcache_lock, s);
		return;
	}

	/*
	 * Clear both our pointer to the cache entry and its pointer
	 * back to us.
	 */
	ASSERT(*(ip->i_refcache) == ip);
	*(ip->i_refcache) = NULL;
	ip->i_refcache = NULL;
	xfs_refcache_count--;
	ASSERT(xfs_refcache_count >= 0);
	mp_mutex_spinunlock(&xfs_refcache_lock, s);

	vp = XFS_ITOV(ip);
	ASSERT(vp->v_count > 1);
	VN_RELE(vp);

	return;
}


/*
 * This is called from the XFS unmount code to purge all entries for the
 * given mount from the cache.  It uses the refcache busy counter to
 * make sure that new entries are not added to the cache as we purge them.
 */
void
xfs_refcache_purge_mp(
	xfs_mount_t	*mp)
{
	int		s;
	vnode_t		*vp;
	int		i;
	xfs_inode_t	*ip;

	if (xfs_refcache == NULL) {
		return;
	}

	s = mp_mutex_spinlock(&xfs_refcache_lock);
	/*
	 * Bumping the busy counter keeps new entries from being added
	 * to the cache.  We use a counter since multiple unmounts could
	 * be in here simultaneously.
	 */
	xfs_refcache_busy++;

	for (i = 0; i < xfs_refcache_size; i++) {
		ip = xfs_refcache[i];
		if ((ip != NULL) && (ip->i_mount == mp)) {
			xfs_refcache[i] = NULL;
			ip->i_refcache = NULL;
			xfs_refcache_count--;
			ASSERT(xfs_refcache_count >= 0);
			mp_mutex_spinunlock(&xfs_refcache_lock, s)
;
			vp = XFS_ITOV(ip);
			VN_RELE(vp);

			s = mp_mutex_spinlock(&xfs_refcache_lock);
		} else {
			/*
			 * Make sure the don't hold the lock for too long.
			 */
			if (i % 16 == 0) {
				mp_mutex_spinunlock(&xfs_refcache_lock, s);
				preemptchk();
				s = mp_mutex_spinlock(&xfs_refcache_lock);
			}
		}
	}

	xfs_refcache_busy--;
	ASSERT(xfs_refcache_busy >= 0);
	mp_mutex_spinunlock(&xfs_refcache_lock, s);
}


/*
 * This is called from the XFS sync code to ensure that the refcache
 * is emptied out over time.  We purge a small number of entries with
 * each call.
 */
void
xfs_refcache_purge_some(void)
{
	int		s;
	int		i;
	xfs_inode_t	*ip;
	int		iplist_index;
#define	XFS_REFCACHE_PURGE_COUNT	10
	xfs_inode_t	*iplist[XFS_REFCACHE_PURGE_COUNT];

	if ((xfs_refcache == NULL) || (xfs_refcache_count == 0)) {
		return;
	}

	iplist_index = 0;
	s = mp_mutex_spinlock(&xfs_refcache_lock);

	/*
	 * Store any inodes we find in the next several entries
	 * into the iplist array to be released after dropping
	 * the spinlock.  We always start looking from the currently
	 * oldest place in the cache.  We move the refcache index
	 * forward as we go so that we are sure to eventually clear
	 * out the entire cache when the system goes idle.
	 */
	for (i = 0; i < XFS_REFCACHE_PURGE_COUNT; i++) {
		ip = xfs_refcache[xfs_refcache_index];
		if (ip != NULL) {
			xfs_refcache[xfs_refcache_index] = NULL;
			ip->i_refcache = NULL;
			xfs_refcache_count--;
			ASSERT(xfs_refcache_count >= 0);
			iplist[iplist_index] = ip;
			iplist_index++;
		}
		xfs_refcache_index++;
		if (xfs_refcache_index == xfs_refcache_size) {
			xfs_refcache_index = 0;
		}
	}

	mp_mutex_spinunlock(&xfs_refcache_lock, s);

	/*
	 * Now drop the inodes we collected.
	 */
	for (i = 0; i < iplist_index; i++) {
		VN_RELE(XFS_ITOV(iplist[i]));
	}
}
