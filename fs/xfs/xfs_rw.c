/*
 * Copyright (C) 1999 Silicon Graphics, Inc.  All Rights Reserved.
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston MA 02111-1307, USA.
 */
#ident "$Revision$"

#include <xfs_os_defs.h>

#include <sys/param.h>
#include "xfs_buf.h"
#include <sys/uio.h>
#include <sys/vfs.h>
#include <sys/vnode.h>
#include <sys/sysmacros.h>
#include <sys/uuid.h>
#include <sys/grio.h>
#include <sys/pda.h>
#include <sys/dmi_kern.h>
#include <sys/cmn_err.h>
#include <sys/debug.h>
#include <sys/fcntl.h>
#include <sys/var.h>
#include <sys/conf.h>
#include <sys/systm.h>
#include <sys/uthread.h>
#include <ksys/as.h>
#include <sys/kmem.h>
#include <linux/xfs_sema.h>
#include <ksys/vfile.h>
#include <sys/flock.h>
#include <sys/fs_subr.h>
#include <sys/dmi.h>
#include <sys/dmi_kern.h>
#include <sys/ktrace.h>
#include <sys/ksa.h>
#include <ksys/sthread.h>
#include "xfs_macros.h"
#include "xfs_types.h"
#include "xfs_inum.h"
#include "xfs_log.h"
#include "xfs_trans.h"
#include "xfs_sb.h"
#include "xfs_ag.h"
#include "xfs_dir.h"
#include "xfs_dir2.h"
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
#include "xfs_dir2_sf.h"
#include "xfs_dinode.h"
#include "xfs_inode_item.h"
#include "xfs_inode.h"
#include "xfs_error.h"
#include "xfs_bit.h"
#include "xfs_rw.h"
#include "xfs_quota.h"
#include "xfs_trans_space.h"
#include "xfs_dmapi.h"
#include "xfs_cxfs.h"

/*
 * turning on UIOSZ_DEBUG in a DEBUG kernel causes each xfs_write/xfs_read
 * to set the write/read i/o size to a random valid value and instruments
 * the distribution.
 *
#define UIOSZ_DEBUG
 */

#ifdef UIOSZ_DEBUG
int uiodbg = 0;
int uiodbg_readiolog[XFS_UIO_MAX_READIO_LOG - XFS_UIO_MIN_READIO_LOG + 1] =
		{0, 0, 0, 0};
int uiodbg_writeiolog[XFS_UIO_MAX_WRITEIO_LOG - XFS_UIO_MIN_WRITEIO_LOG + 1] =
		{0, 0, 0, 0};
int uiodbg_switch = 0;
#endif

#define XFS_NUMVNMAPS 10	    /* number of uacc maps to pass to VM */

extern int xfs_nfs_io_units;
typedef	uuid_t		stream_id_t;
void daemonize(void); /* from linux/xfs_thread.c */
void set_thread_name(char *name); /* from linux/xfs_thread.c */
extern void griostrategy(xfs_buf_t *bp);  /* prototype -- where to find it? */
int grio_io_is_guaranteed( vfile_t *fp, stream_id_t *stream_id);  /* prototype -- where to find it? */
extern int			grio_monitor_start( sysarg_t );
int grio_monitor_io_start( stream_id_t *stream_id, __int64_t iosize);
int grio_monitor_io_end( stream_id_t *stream_id, int index );

int sthread_create(char *name,
				   caddr_t stack_addr,
				   uint_t stack_size,
				   uint_t flags,
				   uint_t pri,
				   uint_t schedflags,
				   st_func_t func,
				   void *arg0,
				   void *arg1,
				   void *arg2,
				   void *arg3);  /* from linux/xfs_thread.c */
/*
 * This lock is used by xfs_strat_write().
 * The xfs_strat_lock is initialized in xfs_init().
 */
lock_t	xfs_strat_lock;

/*
 * Variables for coordination with the xfs_strat daemon.
 * The xfsc_lock and xfsc_wait variables are initialized
 * in xfs_init();
 */
static int	xfsc_count;
static xfs_buf_t	*xfsc_list;
static int	xfsc_bufcount;
lock_t		xfsc_lock;
sv_t		xfsc_wait;

/*
 * Variables for coordination with the xfsd daemons.
 * The xfsd_lock and xfsd_wait variables are initialized
 * in xfs_init();
 */
static int	xfsd_count;
static xfs_buf_t	*xfsd_list;
static int	xfsd_bufcount;
lock_t		xfsd_lock;
sv_t		xfsd_wait;

/*
 * Zone allocator for xfs_gap_t structures.
 */
zone_t		*xfs_gap_zone;

#ifdef DEBUG
/*
 * Global trace buffer for xfs_strat_write() tracing.
 */
ktrace_t	*xfs_strat_trace_buf;
#endif

STATIC int
xfs_retrieved(
	uint		available,
	off_t		offset,
	size_t		count,
	uint		*total_retrieved,
	xfs_fsize_t	isize);

#ifndef DEBUG

#define	xfs_check_gap_list(ip)

#else /* DEBUG */

void
xfs_check_gap_list(
	xfs_iocore_t	*ip);

#endif /* DEBUG */		      

int
xfs_build_gap_list(
	xfs_iocore_t	*ip,
	off_t		offset,
	size_t		count);

void
xfs_free_gap_list(
	xfs_iocore_t	*ip);

STATIC void
xfs_strat_comp(void);

STATIC int
xfsd(void);

void
xfs_strat_write_iodone(
	xfs_buf_t		*bp);

STATIC int
xfs_dio_write_zero_rtarea(
	xfs_inode_t	*ip,
	struct xfs_buf	*bp,
	xfs_fileoff_t	offset_fsb,
	xfs_filblks_t	count_fsb);
#if defined(__sgi__)
extern int
grio_io_is_guaranteed(
	vfile_t *,
	stream_id_t	*);

extern void
griostrategy(
	xfs_buf_t	*);

extern int
grio_monitor_io_start( 
	stream_id_t *, 
	__int64_t);

extern int
grio_monitor_io_end(
	stream_id_t *,
	int);
#endif	

extern void xfs_error(
	xfs_mount_t *,
	int);

STATIC void
xfs_delalloc_cleanup(
	xfs_inode_t	*ip,
	xfs_fileoff_t	start_fsb,
	xfs_filblks_t	count_fsb);

extern void xfs_buf_iodone_callbacks(struct xfs_buf *);
extern void xlog_iodone(struct xfs_buf *);

/*
 * Round the given file offset down to the nearest read/write
 * size boundary.
 */
#define	XFS_READIO_ALIGN(io,off)	(((off) >> io->io_readio_log) \
					        << io->io_readio_log)
#define	XFS_WRITEIO_ALIGN(io,off)	(((off) >> io->io_writeio_log) \
					        << io->io_writeio_log)

#if !defined(XFS_RW_TRACE)
#define	xfs_rw_enter_trace(tag, ip, uiop, ioflags)
#define	xfs_iomap_enter_trace(tag, ip, offset, count);
#define	xfs_iomap_map_trace(tag, ip, offset, count, bmapp, imapp)
#define xfs_inval_cached_trace(ip, offset, len, first, last)
#else
/*
 * Trace routine for the read/write path.  This is the routine entry trace.
 */
static void
xfs_rw_enter_trace(
	int		tag,	     
	xfs_iocore_t	*io,
	uio_t		*uiop,
	int		ioflags)
{
	xfs_inode_t	*ip = XFS_IO_INODE(io);

	if (!IO_IS_XFS(io) || (ip->i_rwtrace == NULL)) {
		return;
	}

	ktrace_enter(ip->i_rwtrace,
		     (void*)((unsigned long)tag),
		     (void*)ip,
		     (void*)((ip->i_d.di_size >> 32) & 0xffffffff),
		     (void*)(ip->i_d.di_size & 0xffffffff),
		     (void*)(((__uint64_t)uiop->uio_offset >> 32) &
			     0xffffffff),
		     (void*)(uiop->uio_offset & 0xffffffff),
		     (void*)uiop->uio_resid,
		     (void*)((unsigned long)ioflags),
		     (void*)((io->io_next_offset >> 32) & 0xffffffff),
		     (void*)(io->io_next_offset & 0xffffffff),
		     (void*)((unsigned long)((io->io_offset >> 32) &
					     0xffffffff)),
		     (void*)(io->io_offset & 0xffffffff),
		     (void*)((unsigned long)(io->io_size)),
		     (void*)((unsigned long)(io->io_last_req_sz)),
		     (void*)((unsigned long)((io->io_new_size >> 32) &
					     0xffffffff)),
		     (void*)(io->io_new_size & 0xffffffff));
}

static void
xfs_iomap_enter_trace(
	int		tag,
	xfs_iocore_t	*io,
	off_t		offset,
	size_t		count)
{
	xfs_inode_t	*ip = XFS_IO_INODE(io);

	if (!IO_IS_XFS(io) || (ip->i_rwtrace == NULL)) {
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
		     (void*)((io->io_next_offset >> 32) & 0xffffffff),
		     (void*)(io->io_next_offset & 0xffffffff),
		     (void*)((io->io_offset >> 32) & 0xffffffff),
		     (void*)(io->io_offset & 0xffffffff),
		     (void*)((unsigned long)(io->io_size)),
		     (void*)((unsigned long)(io->io_last_req_sz)),
		     (void*)((io->io_new_size >> 32) & 0xffffffff),
		     (void*)(io->io_new_size & 0xffffffff),
		     (void*)0);
}

void
xfs_iomap_map_trace(
	int		tag,	     
	xfs_iocore_t	*io,
	off_t		offset,
	size_t		count,
	struct bmapval	*bmapp,
	xfs_bmbt_irec_t	*imapp)    
{
	xfs_inode_t	*ip = XFS_IO_INODE(io);

	if (!IO_IS_XFS(io) || (ip->i_rwtrace == NULL)) {
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

static void
xfs_inval_cached_trace(
	xfs_iocore_t	*io,
	off_t		offset,
	off_t		len,
	off_t		first,
	off_t		last)
{
	xfs_inode_t	*ip = XFS_IO_INODE(io);

	if (!IO_IS_XFS(io) || (ip->i_rwtrace == NULL)) 
		return;
	ktrace_enter(ip->i_rwtrace,
		(void *)(__psint_t)XFS_INVAL_CACHED,
		(void *)ip,
		(void *)(((__uint64_t)offset >> 32) & 0xffffffff),
		(void *)(offset & 0xffffffff),
		(void *)(((__uint64_t)len >> 32) & 0xffffffff),
		(void *)(len & 0xffffffff),
		(void *)(((__uint64_t)first >> 32) & 0xffffffff),
		(void *)(first & 0xffffffff),
		(void *)(((__uint64_t)last >> 32) & 0xffffffff),
		(void *)(last & 0xffffffff),
		(void *)0,
		(void *)0,
		(void *)0,
		(void *)0,
		(void *)0,
		(void *)0);
}
#endif	/* XFS_RW_TRACE */

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
			ext_offset = req_offset - imapp->br_startoff;
			bmapp->offset = req_offset;
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
		if (imapp->br_state == XFS_EXT_UNWRITTEN)
			bmapp->eof |= BMAP_UNWRITTEN;
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
	uint		available,
	off_t		offset,
	size_t		count,
	uint		*total_retrieved,
	xfs_fsize_t	isize)
{
	uint		retrieved;
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
 * This is a subroutine for xfs_write() and other writers
 * (xfs_fcntl) which clears the setuid and setgid bits when a file is written.
 */
int
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
		xfs_trans_cancel(tp, 0);
		return error;
	}
	xfs_ilock(ip, XFS_ILOCK_EXCL);
	xfs_trans_ijoin(tp, ip, XFS_ILOCK_EXCL);
	xfs_trans_ihold(tp, ip);
	ip->i_d.di_mode &= ~ISUID;

	/*
	 * Note that we don't have to worry about mandatory
	 * file locking being disabled here because we only
	 * clear the ISGID bit if the Group execute bit is
	 * on, but if it was on then mandatory locking wouldn't
	 * have been enabled.
	 */
	if (ip->i_d.di_mode & (IEXEC >> 3)) {
		ip->i_d.di_mode &= ~ISGID;
	}
	xfs_trans_log_inode(tp, ip, XFS_ILOG_CORE);
	xfs_trans_set_sync(tp);
	error = xfs_trans_commit(tp, 0, NULL);
	xfs_iunlock(ip, XFS_ILOCK_EXCL);
	return 0;
}

/*
 * Verify that the gap list is properly sorted and that no entries
 * overlap.
 */
#ifdef DEBUG
void
xfs_check_gap_list(
	xfs_iocore_t	*io)
{
	xfs_gap_t	*last_gap;
	xfs_gap_t	*curr_gap;
	int		loops;

	last_gap = NULL;
	curr_gap = io->io_gap_list;
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
int				/* error */
xfs_build_gap_list(
	xfs_iocore_t	*io,
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

	ASSERT(ismrlocked(io->io_lock, MR_UPDATE) != 0);
	ASSERT(io->io_gap_list == NULL);

	mp = io->io_mount;
	offset_fsb = XFS_B_TO_FSBT(mp, offset);
	last_fsb = XFS_B_TO_FSB(mp, ((xfs_ufsize_t)(offset + count)));
	count_fsb = (xfs_filblks_t)(last_fsb - offset_fsb);
	ASSERT(count_fsb > 0);

	last_gap = NULL;
	while (count_fsb > 0) {
		nimaps = XFS_BGL_NIMAPS;
		firstblock = NULLFSBLOCK;
		error = XFS_BMAPI(mp, NULL, io, offset_fsb, count_fsb,
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
			 * Skip anything that is not a hole or
			 * unwritten.
			 */
			if (imapp->br_startblock != HOLESTARTBLOCK ||
			    imapp->br_state == XFS_EXT_UNWRITTEN) {
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
				io->io_gap_list = new_gap;
			} else {
				last_gap->xg_next = new_gap;
			}
			last_gap = new_gap;
		}
	}
	xfs_check_gap_list(io);
	return 0;
}

/*
 * Free up all of the entries in the inode's gap list.  This requires
 * the inode lock to be held exclusively.
 */

void
xfs_free_gap_list(
	xfs_iocore_t	*io)
{
	xfs_gap_t	*curr_gap;
	xfs_gap_t	*next_gap;

	ASSERT(ismrlocked(io->io_lock, MR_UPDATE) != 0);
	xfs_check_gap_list(io);

	curr_gap = io->io_gap_list;
	while (curr_gap != NULL) {
		next_gap = curr_gap->xg_next;
		kmem_zone_free(xfs_gap_zone, curr_gap);
		curr_gap = next_gap;
	}
	io->io_gap_list = NULL;
}

/*
 * Force a shutdown of the filesystem instantly while keeping
 * the filesystem consistent. We don't do an unmount here; just shutdown
 * the shop, make sure that absolutely nothing persistent happens to
 * this filesystem after this point. 
 */

void
xfs_force_shutdown(
	xfs_mount_t	*mp,
	int		flags)
{
	int ntries;
	int logerror;
	extern dev_t rootdev;		/* from sys/systm.h */

#define XFS_MAX_DRELSE_RETRIES	10
	logerror = flags & XFS_LOG_IO_ERROR;

	/*
	 * No need to duplicate efforts.
	 */
	if (XFS_FORCED_SHUTDOWN(mp) && !logerror)
		return;

	if (XFS_MTOVFS(mp)->vfs_dev == rootdev)
		cmn_err(CE_PANIC, "Fatal error on root filesystem");

	/*
	 * This flags XFS_MOUNT_FS_SHUTDOWN, makes sure that we don't
	 * queue up anybody new on the log reservations, and wakes up
	 * everybody who's sleeping on log reservations and tells
	 * them the bad news.
	 */
	if (xfs_log_force_umount(mp, logerror))
		return;

	if (flags & XFS_CORRUPT_INCORE)
		cmn_err(CE_ALERT,
    "Corruption of in-memory data detected.  Shutting down filesystem: %s",
			mp->m_fsname);
	else
		cmn_err(CE_ALERT,
			"I/O Error Detected.  Shutting down filesystem: %s",
			mp->m_fsname);

	cmn_err(CE_ALERT,
		"Please umount the filesystem, and rectify the problem(s)");

	/*
	 * Release all delayed write buffers for this device.
	 * It wouldn't be a fatal error if we couldn't release all
	 * delwri bufs; in general they all get unpinned eventually.
	 */
	ntries = 0;
#ifdef XFSERRORDEBUG
	{
		int nbufs;
		while (nbufs = xfs_incore_relse(mp->m_ddev_targ, 1, 0)) {
			printf("XFS: released 0x%x bufs\n", nbufs);
			if (ntries >= XFS_MAX_DRELSE_RETRIES) {
				printf("XFS: ntries 0x%x\n", ntries);
				debug("ntries");
				break;
			}
			delay(++ntries * 5);
		}
	}
#else
	while (xfs_incore_relse(mp->m_ddev_targ, 1, 0)) {
		if (ntries >= XFS_MAX_DRELSE_RETRIES)
			break;
		delay(++ntries * 5);
	}

#endif

#if CELL_CAPABLE
	if (cell_enabled && !(flags & XFS_SHUTDOWN_REMOTE_REQ)) {
		extern void cxfs_force_shutdown(xfs_mount_t *, int); /*@@@*/

		/* 
		 * We're being called for a problem discovered locally.
		 * Tell CXFS to pass along the shutdown request.
		 */
		cxfs_force_shutdown(mp, flags);
	}
#endif /* CELL_CAPABLE */
}


/*
 * Called when we want to stop a buffer from getting written or read.
 * We attach the EIO error, muck with its flags, and call biodone
 * so that the proper iodone callbacks get called.
 */
int
xfs_bioerror(
	xfs_buf_t *bp)
{

#ifdef XFSERRORDEBUG
	ASSERT(XFS_BUF_ISREAD(bp) || bp->b_iodone);
#endif

	/*
	 * No need to wait until the buffer is unpinned.
	 * We aren't flushing it.
	 */
    xfs_buftrace("XFS IOERROR", bp);
	XFS_BUF_ERROR(bp, EIO);
	/*
	 * We're calling biodone, so delete B_DONE flag. Either way
	 * we have to call the iodone callback, and calling biodone
	 * probably is the best way since it takes care of
	 * GRIO as well.
	 */
	XFS_BUF_UNREAD(bp);
	XFS_BUF_UNDELAYWRITE(bp);
	XFS_BUF_UNDONE(bp);
	XFS_BUF_STALE(bp);

	XFS_BUF_CLR_BDSTRAT_FUNC(bp);
	xfs_biodone(bp);
	
	return (EIO);
}

/*
 * Same as xfs_bioerror, except that we are releasing the buffer
 * here ourselves, and avoiding the biodone call.
 * This is meant for userdata errors; metadata bufs come with
 * iodone functions attached, so that we can track down errors.
 */
int
xfs_bioerror_relse(
	xfs_buf_t *bp)
{
	int64_t fl;

	ASSERT(XFS_BUF_IODONE_FUNC(bp) != xfs_buf_iodone_callbacks);
	ASSERT(XFS_BUF_IODONE_FUNC(bp) != xlog_iodone);

	xfs_buftrace("XFS IOERRELSE", bp);
	fl = XFS_BUF_BFLAGS(bp);
	/*
	 * No need to wait until the buffer is unpinned.
	 * We aren't flushing it.
	 *
	 * chunkhold expects B_DONE to be set, whether
	 * we actually finish the I/O or not. We don't want to
	 * change that interface.
	 */
	XFS_BUF_UNREAD(bp);
	XFS_BUF_UNDELAYWRITE(bp);
	XFS_BUF_DONE(bp);
	XFS_BUF_STALE(bp);
	XFS_BUF_CLR_IODONE_FUNC(bp);
 	XFS_BUF_CLR_BDSTRAT_FUNC(bp);
	if (!(fl & XFS_B_ASYNC)) {
		/*
		 * Mark b_error and B_ERROR _both_.
		 * Lot's of chunkcache code assumes that.
		 * There's no reason to mark error for
		 * ASYNC buffers.
		 */
		XFS_BUF_ERROR(bp, EIO);
		XFS_BUF_V_IODONESEMA(bp);
	} else {
		xfs_buf_relse(bp);
	}
	return (EIO);
}
/*
 * Prints out an ALERT message about I/O error. 
 */
void
xfs_ioerror_alert(
	char 			*func,
	struct xfs_mount	*mp,
	dev_t			dev,
	daddr_t			blkno)
{
	cmn_err(CE_ALERT,
 "I/O error in filesystem (\"%s\") meta-data dev 0x%x block 0x%x (\"%s\")",
		mp->m_fsname, 
		dev,
		blkno,
		func);
}

/*
 * This isn't an absolute requirement, but it is
 * just a good idea to call xfs_read_buf instead of
 * directly doing a read_buf call. For one, we shouldn't
 * be doing this disk read if we are in SHUTDOWN state anyway,
 * so this stops that from happening. Secondly, this does all
 * the error checking stuff and the brelse if appropriate for
 * the caller, so the code can be a little leaner.
 */

int
xfs_read_buf(
	struct xfs_mount *mp,
	buftarg_t	 *target,
        daddr_t 	 blkno,
        int              len,
        uint             flags,
	xfs_buf_t		 **bpp)
{
	xfs_buf_t		 *bp;
	int 		 error;
	
	bp = xfs_buf_read(target, blkno, len, flags);
	error = XFS_BUF_GETERROR(bp);
	if (bp && !error && !XFS_FORCED_SHUTDOWN(mp)) {
		*bpp = bp;
	} else {
		*bpp = NULL;
		if (!error)
			error = XFS_ERROR(EIO);
		if (bp) {
		    XFS_BUF_UNDONE(bp);
		    XFS_BUF_UNDELAYWRITE(bp);
		    XFS_BUF_STALE(bp);
			/* 
			 * brelse clears B_ERROR and b_error
			 */
			xfs_buf_relse(bp);
		}
	}
	return (error);
}
	
/*
 * Wrapper around bwrite() so that we can trap 
 * write errors, and act accordingly.
 */
int
xfs_bwrite(
	struct xfs_mount *mp,
	struct xfs_buf	 *bp)
{
	int	error;

	/*
	 * XXXsup how does this work for quotas.
	 */
	XFS_BUF_SET_BDSTRAT_FUNC(bp, xfs_bdstrat_cb);
	XFS_BUF_SET_FSPRIVATE3(bp, mp);
	XFS_BUF_WRITE(bp);

   	if (error = XFS_bwrite(bp)) {
		ASSERT(mp);
		/* 
		 * Cannot put a buftrace here since if the buffer is not 
		 * B_HOLD then we will brelse() the buffer before returning 
		 * from bwrite and we could be tracing a buffer that has 
		 * been reused.
		 */
		xfs_force_shutdown(mp, XFS_METADATA_IO_ERROR);
	}
	return (error);
}


#define MAX_BUF_EXAMINED 10

/*
 * This function purges the xfsd list of all bufs belonging to the
 * specified vnode.  This will allow a file about to be deleted to 
 * remove its buffers from the xfsd_list so it doesn't have to wait
 * for them to be pushed out to disk
 */
#if !defined(_USING_PAGEBUF_T) 
void 
_xfs_xfsd_list_evict(bhv_desc_t * bdp)
{
	vnode_t		*vp;
	xfs_iocore_t	*io;
	
	int	s;
	int	cur_count;	/* 
				 * Count of buffers that have been processed
				 * since aquiring the spin lock 
				 */
	int countdown;		/* 
				 * Count of buffers that have been processed
				 * since we started.  This will prevent non-
				 * termination if buffers are being added to
				 * the head of the list 
				 */
	xfs_buf_t	*bp;
	xfs_buf_t	*forw;
	xfs_buf_t	*back;
	xfs_buf_t	*next_bp;
	
	/* List and count of the saved buffers */
	xfs_buf_t	*bufs;
	unsigned int bufcount;

	/* Marker Buffers */
	xfs_buf_t	*cur_marker;
	xfs_buf_t	*end_marker;
	
	vp = BHV_TO_VNODE(bdp);
	
	/* Initialize bufcount and bufs */
	bufs = NULL;
	bufcount = 0;

	/* Allocate both markers at once... it's a little nicer. */
	cur_marker = (xfs_buf_t *)kmem_alloc(sizeof(xfs_buf_t)*2, KM_SLEEP);
	
	/* A little sketchy pointer-math, but should be ok. */
	end_marker = cur_marker + 1;

	s = mp_mutex_spinlock(&xfsd_lock);
	
	/* Make sure there are buffers to check */
	if (xfsd_list == NULL) {
		mp_mutex_spinunlock(&xfsd_lock, s);
		
		kmem_free(cur_marker, sizeof(xfs_buf_t)*2);
		return;
	}

	/* 
	 * Ok.  We know we're going to use the markers now, so let's 
	 * actually initialize them.  At Ray's suggestion, we'll make
	 * the b_vp == -1 as the signifier that this is a marker.  We 
	 * know a marker is unlinked if it's av_forw and av_back pointers
	 * point to itself.
	 */

	cur_marker->b_vp = (vnode_t *)-1L;
	end_marker->b_vp = (vnode_t *)-1L;

	/* Now link end_marker onto the end of the list */

	end_marker->av_back = xfsd_list->av_back;
	xfsd_list->av_back->av_forw = end_marker;
	end_marker->av_forw = xfsd_list;
	xfsd_list->av_back = end_marker;

	xfsd_bufcount++;

	/* 
	 * Set the countdown to it's initial value.  This will be a snapshot
	 * of the size of the list.  If we process this many buffers without
	 * finding the end_marker, then someone is putting buffers onto the 
	 * head of the list
	 */
	countdown = xfsd_bufcount;
	
	/* Zero the initial count */
	cur_count = 0;

	bp = xfsd_list;
 
	/* 
	 * Loop: Assumptions: the end_marker has been set, bp is set to the 
	 * current buf being examined, the xfsd_lock is held, cur_marker is
	 * <not> linked into the list.
	 */
	while (1) {
		/* We are processing a buffer.  Take note of that fact. */
		cur_count++;
		countdown--;
		
		if (bp == end_marker) {
			/* Unlink it from the list */
			
			/* 
			 * If it's the only thing on the list, NULL the 
			 * xfsd_list. Otherwise, unlink normally 
			 */
			if (bp->av_forw == bp) {
				xfsd_list = NULL;
			} else {
				forw = bp->av_forw;
				back = bp->av_back;
				forw->av_back = back;
				back->av_forw = forw;
			}
			
			xfsd_bufcount--;

			/* Move the head of the list forward if necessary */
			if (bp == xfsd_list)
				xfsd_list = bp->av_forw;
			
			break;
		}

		/* Check to see if this buffer should be removed */
		if (bp->b_vp == vp) {
			next_bp = bp->av_forw;
	    
			/* Remove the buffer from the xfsd_list */
			forw = bp->av_forw;
			back = bp->av_back;
			forw->av_back = back;
			back->av_forw = forw;

			/* 
			 * If we removed the head of the xfsd_list, move 
			 * it forward. 
			 */
			if (xfsd_list == bp) xfsd_list = next_bp;

			xfsd_bufcount--;
			
			/* 
			 * We can't remove all of the list, since we know 
			 * we have yet to see the end_marker.. that's the
			 * only buffer we'll see that MIGHT be the sole
			 * occupant of the list.
			 */
			   
			/* Now add the buffer to the list of buffers to free */
			if (bufcount > 0) {			
				bufs->av_back->av_forw = bp;
				bp->av_back = bufs->av_back;
				bufs->av_back = bp;
				bp->av_forw = bufs;
				
				bufcount++;
			} else {
				bufs = bp;
				bp->av_forw = bp;
				bp->av_back = bp;
				
				bufcount = 1;
			}
			
			bp = next_bp;
		} else {
			bp = bp->av_forw;
		}
		
		/* Now, bp has been advanced. */
		
		/* Now before we iterate, make sure we haven't run too long */
		if (cur_count > MAX_BUF_EXAMINED) {
			/* 
			 * Stick the cur_marker into the current pos in the 
			 * list.  The only special case is if the current bp
			 * is the head of the list, in which case we have to
			 * point the head of the list.
			 */
			
			/* First, link the cur_marker before the new bp */
			cur_marker->av_forw = bp;
			cur_marker->av_back = bp->av_back;
			bp->av_back->av_forw = cur_marker;
			bp->av_back = cur_marker;
			
			xfsd_bufcount++;
			
			if (bp == xfsd_list)
				xfsd_list = cur_marker;
			
			/* Now, it's safe to release the lock... */
			mp_mutex_spinunlock(&xfsd_lock, s);
			
			/*
			 * Kill me! I'm here! KILL ME!!! (If an interrupt
			 * needs too happen, it can. Now we won't blow
			 * realtime ;-).
			 */
			
			s = mp_mutex_spinlock(&xfsd_lock);
			
			/* Zero the current count */
			cur_count = 0;
			
			/* 
			 * Figure out if we SHOULD continue (if the end_marker
			 * has been removed, give up, unless it's the head of
			 * the otherwise empty list, since it's about to be 
			 * dequed and then we'll stop.
			 */
			if ((end_marker->av_forw == end_marker) && 
				(xfsd_list != end_marker)) {
				break;
			}
			
			/*
			 * Now determine if we should start at the marker or
			 * from the beginning of the list.  It can't be the
			 * only thing on the list, since the end_marker should
			 * be there too.
			 */
			if (cur_marker->av_forw == cur_marker) {
				bp = xfsd_list;
			} else {
				bp = cur_marker->av_forw;
				
				/* 
				 * Now dequeue the marker.  It might be the 
				 * head of the list, so we might have to move
				 * the list head... 
				 */
				
				forw = cur_marker->av_forw;
				back = cur_marker->av_back;
				forw->av_back = back;
				back->av_forw = forw;
				
				if (cur_marker == xfsd_list)
					xfsd_list = bp;
				
				xfsd_bufcount--;
				
				/* 
				 * We know we can't be the only buffer on the 
				 * list, as previously stated... so we 
				 * continue. 
				 */
			}
		}

		/*
		 * If countdown reaches zero without breaking by dequeuing the
		 * end_marker, someone is putting things on the front of the 
		 * list, so we'll quit to thwart their cunning attempt to keep
		 * us tied up in the list.  So dequeue the end_marker now and
		 * break.
		 */
		   
		/*
		 * Invarients at this point:  The end_marker is on the list.
		 * It may or may not be the head of the list.  cur_marker is
		 * NOT on the list.
		 */
		if (countdown == 0) {
			/* Unlink the end_marker from the list */
			
			if (end_marker->av_forw == end_marker) {
				xfsd_list = NULL;
			} else {
				forw = end_marker->av_forw;
				back = end_marker->av_back;
				forw->av_back = back;
				back->av_forw = forw;
			}
			
			xfsd_bufcount--;

			/* Move the head of the list forward if necessary */
			if (end_marker == xfsd_list) xfsd_list = end_marker->av_forw;
			
			break;
		}
	}
	
	mp_mutex_spinunlock(&xfsd_lock, s);
	kmem_free(cur_marker, sizeof(xfs_buf_t)*2);	
	
	/*
	 * At this point, bufs contains the list of buffers that would have
	 * been written to disk, if we hadn't swiped them (which we did
	 * because they are part of a file being deleted, so they obviously
	 * shouldn't go to the disk.  At this point, we need to make them as
	 * done.
	 */
	
	bp = bufs;
	
	/* We use s as a counter.  It's handy, so there. */
	for (s = 0; s < bufcount; s++) {
		next_bp = bp->av_forw;
		
		bp->av_forw = bp;
		bp->av_back = bp;
		
		XFS_BUF_STALE(bp);
		io = bp->b_private;
		atomicAddInt(&(io->io_queued_bufs), -1);
		bp->b_private = NULL;
		
		/* Now call biodone. */
		biodone(bp);
		
		bp = next_bp;
	}
}
#endif /* !defined(__linux__) */
/*
 * xfs_inval_cached_pages()
 * This routine is responsible for keeping direct I/O and buffered I/O
 * somewhat coherent.  From here we make sure that we're at least
 * temporarily holding the inode I/O lock exclusively and then call
 * the page cache to flush and invalidate any cached pages.  If there
 * are no cached pages this routine will be very quick.
 */
void
xfs_inval_cached_pages(
	vnode_t		*vp,
	xfs_iocore_t	*io,
	off_t		offset,
	off_t		len,
	void		*dio)		    
{
	xfs_dio_t	*diop = (xfs_dio_t *)dio;
	int		relock;
	__uint64_t	flush_end;
	xfs_mount_t	*mp;

	if (!VN_CACHED(vp)) {
		return;
	}

	mp = io->io_mount;

	/*
	 * We need to get the I/O lock exclusively in order
	 * to safely invalidate pages and mappings.
	 */
	relock = ismrlocked(io->io_iolock, MR_ACCESS);
	if (relock) {
		XFS_IUNLOCK(mp, io, XFS_IOLOCK_SHARED);
		XFS_ILOCK(mp, io, XFS_IOLOCK_EXCL);
	}

	/* Writing beyond EOF creates a hole that must be zeroed */
	if (diop && (offset > XFS_SIZE(mp, io))) {
		xfs_fsize_t	isize;

		XFS_ILOCK(mp, io, XFS_ILOCK_EXCL|XFS_EXTSIZE_RD);
		isize = XFS_SIZE(mp, io);
		if (offset > isize) {
			xfs_zero_eof(vp, io, offset, isize, diop->xd_cr,
					diop->xd_pmp);
		}
		XFS_IUNLOCK(mp, io, XFS_ILOCK_EXCL|XFS_EXTSIZE_RD);
	}

	/*
	 * Round up to the next page boundary and then back
	 * off by one byte.  We back off by one because this
	 * is a first byte/last byte interface rather than
	 * a start/len interface.  We round up to a page
	 * boundary because the page/chunk cache code is
	 * slightly broken and won't invalidate all the right
	 * buffers otherwise.
	 *
	 * We also have to watch out for overflow, so if we
	 * go over the maximum off_t value we just pull back
	 * to that max.
	 */
	flush_end = (__uint64_t)ctooff(offtoc(offset + len)) - 1;
	if (flush_end > (__uint64_t)LONGLONG_MAX) {
		flush_end = LONGLONG_MAX;
	}
	xfs_inval_cached_trace(io, offset, len, ctooff(offtoct(offset)),
		flush_end);
	VOP_FLUSHINVAL_PAGES(vp, ctooff(offtoct(offset)), (off_t)flush_end,
			FI_REMAPF_LOCKED);
	if (relock) {
		XFS_IUNLOCK(mp, io, XFS_IOLOCK_EXCL);
		XFS_ILOCK(mp, io, XFS_IOLOCK_SHARED);
	}
}

lock_t		xfs_refcache_lock;
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
			mp_mutex_spinunlock(&xfs_refcache_lock, s);
			vp = XFS_ITOV(ip);
			VN_RELE(vp);

			s = mp_mutex_spinlock(&xfs_refcache_lock);
		} else {
			/*
			 * Make sure the don't hold the lock for too long.
			 */
			if ((i & 15) == 0) {
				mp_mutex_spinunlock(&xfs_refcache_lock, s);
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
