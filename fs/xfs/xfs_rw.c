/*
 * Copyright (c) 2000 Silicon Graphics, Inc.  All Rights Reserved.
 * 
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 * 
 * This program is distributed in the hope that it would be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * 
 * Further, this software is distributed without any warranty that it is
 * free of the rightful claim of any third person regarding infringement
 * or the like.  Any license provided herein, whether implied or
 * otherwise, applies only to this software file.  Patent licenses, if
 * any, provided herein do not apply to combinations of this program with
 * other software, or any other product whatsoever.
 * 
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write the Free Software Foundation, Inc., 59
 * Temple Place - Suite 330, Boston MA 02111-1307, USA.
 * 
 * Contact information: Silicon Graphics, Inc., 1600 Amphitheatre Pkwy,
 * Mountain View, CA  94043, or:
 * 
 * http://www.sgi.com 
 * 
 * For further information regarding this notice, see: 
 * 
 * http://oss.sgi.com/projects/GenInfo/SGIGPLNoticeExplan/
 */

#include <xfs.h>


/*
 * Zone allocator for xfs_gap_t structures.
 */
xfs_zone_t		*xfs_gap_zone;

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
	xfs_off_t	offset,
	size_t		count);

void
xfs_free_gap_list(
	xfs_iocore_t	*ip);

STATIC void
xfs_delalloc_cleanup(
	xfs_inode_t	*ip,
	xfs_fileoff_t	start_fsb,
	xfs_filblks_t	count_fsb);

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
	xfs_off_t	offset,
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
	xfs_off_t	offset,
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
	xfs_off_t	offset,
	xfs_off_t	len,
	xfs_off_t	first,
	xfs_off_t	last)
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
 * This is a subroutine for xfs_write() and other writers (xfs_ioctl)
 * which clears the setuid and setgid bits when a file is written.
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
	if ((error = xfs_trans_reserve(tp, 0,
				      XFS_WRITEID_LOG_RES(mp),
				      0, 0, 0))) {
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
	xfs_off_t	offset,
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
			ASSERT(count_fsb >= 0LL);
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
	int             ntries;
	int             logerror;

#if defined(XFSDEBUG) && 0
        printk("xfs_force_shutdown entered [0x%p, %d]\n",
                mp, flags);
        KDB_ENTER();
#endif

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
		while (nbufs = xfs_incore_relse(&mp->m_ddev_targ, 1, 0)) {
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
	while (xfs_incore_relse(&mp->m_ddev_targ, 1, 0)) {
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
	xfs_daddr_t		blkno)
{
	cmn_err(CE_ALERT,
            "I/O error in filesystem (\"%s\") meta-data dev 0x%x block 0x%Lx:\n"
            "    %s",
		mp->m_fsname, (int)dev, (__uint64_t)blkno, func);
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
	xfs_daddr_t 	 blkno,
	int              len,
	uint             flags,
	xfs_buf_t	 **bpp)
{
	xfs_buf_t	 *bp;
	int 		 error;

	if (flags)
		bp = xfs_buf_read_flags(target, blkno, len, flags);
	else
		bp = xfs_buf_read(target, blkno, len, flags);
	if (!bp)
		return XFS_ERROR(EIO);
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

   	if ((error = XFS_bwrite(bp))) {
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
	xfs_off_t	offset,
	xfs_off_t	len,
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
			xfs_zero_eof(vp, io, offset, isize, NULL);
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
	VOP_FLUSHINVAL_PAGES(vp, ctooff(offtoct(offset)), -1, FI_REMAPF_LOCKED);
	if (relock) {
		XFS_IUNLOCK(mp, io, XFS_IOLOCK_EXCL);
		XFS_ILOCK(mp, io, XFS_IOLOCK_SHARED);
	}
}



spinlock_t	xfs_refcache_lock = SPIN_LOCK_UNLOCKED;
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
	/* ASSERT(vp->v_count > 0); */
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

	spin_lock(&xfs_refcache_lock);

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
		spin_unlock(&xfs_refcache_lock);
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
	spin_unlock(&xfs_refcache_lock);

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
	vnode_t	*vp;
	int	error;

	/*
	 * If we're not pointing to our entry in the cache, then
	 * we must not be in the cache.
	 */
	if (ip->i_refcache == NULL) {
		return;
	}

	spin_lock(&xfs_refcache_lock);
	if (ip->i_refcache == NULL) {
		spin_unlock(&xfs_refcache_lock);
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
	spin_unlock(&xfs_refcache_lock);

	vp = XFS_ITOV(ip);
	/* ASSERT(vp->v_count > 1); */
	VOP_RELEASE(vp, error);
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
	vnode_t		*vp;
	int		error, i;
	xfs_inode_t	*ip;

	if (xfs_refcache == NULL) {
		return;
	}

	spin_lock(&xfs_refcache_lock);
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
			spin_unlock(&xfs_refcache_lock);
			vp = XFS_ITOV(ip);
			VOP_RELEASE(vp, error);
			VN_RELE(vp);
			spin_lock(&xfs_refcache_lock);
		}
	}

	xfs_refcache_busy--;
	ASSERT(xfs_refcache_busy >= 0);
	spin_unlock(&xfs_refcache_lock);
}


/*
 * This is called from the XFS sync code to ensure that the refcache
 * is emptied out over time.  We purge a small number of entries with
 * each call.
 */
void
xfs_refcache_purge_some(void)
{
	int		error, i;
	xfs_inode_t	*ip;
	int		iplist_index;
#define	XFS_REFCACHE_PURGE_COUNT	10
	xfs_inode_t	*iplist[XFS_REFCACHE_PURGE_COUNT];

	if ((xfs_refcache == NULL) || (xfs_refcache_count == 0)) {
		return;
	}

	iplist_index = 0;
	spin_lock(&xfs_refcache_lock);

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

	spin_unlock(&xfs_refcache_lock);

	/*
	 * Now drop the inodes we collected.
	 */
	for (i = 0; i < iplist_index; i++) {
		VOP_RELEASE(XFS_ITOV(iplist[i]), error);
		VN_RELE(XFS_ITOV(iplist[i]));
	}
}
