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
/*
 *  fs/xfs/linux/xfs_lrw.c (Linux Read Write stuff)
 *
 */
#define FSID_T
#include <sys/types.h>
#include <sys/sysmacros.h>
#include <linux/errno.h>

#include <linux/xfs_to_linux.h>

#undef  NODEV
#include <linux/version.h>
#include <linux/fs.h>
#include <asm/uaccess.h>
#include <linux/page_buf.h>
#include <linux/pagemap.h>
#include <linux/capability.h>

#include <linux/linux_to_xfs.h>

#include <sys/cmn_err.h>
#include "xfs_buf.h"
#include <ksys/behavior.h>
#include <sys/vnode.h>
#include <sys/uuid.h>
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
#include "xfs_trans_space.h"
#include "xfs_log_priv.h"
#include "xfs_lrw.h"
#include "xfs_quota.h"

#define min(a, b) ((a) < (b) ? (a) :  (b))
#define XFS_WRITEIO_ALIGN(io,off)       (((off) >> io->io_writeio_log) \
                                                << io->io_writeio_log)

extern int xfs_write_clear_setuid(struct xfs_inode *);
int xfs_iomap_write_delay(xfs_iocore_t *, loff_t, size_t, pb_bmap_t *,
			int *, int, int);
int xfs_iomap_write_convert(xfs_iocore_t *, loff_t, size_t, pb_bmap_t *,
			int *, int, int);
int xfs_iomap_write_direct(xfs_iocore_t *, loff_t, size_t, pb_bmap_t *,
			int *, int, int);
extern int xfs_bioerror_relse(xfs_buf_t *);

#if !defined(_USING_PAGEBUF_T)
extern void bdstrat(struct bdevsw *, buf_t *);
#endif

#ifndef DEBUG
#define	xfs_strat_write_check(io,off,count,imap,nimap)
#else /* DEBUG */
STATIC void
xfs_strat_write_check(
	xfs_iocore_t	*io,
	xfs_fileoff_t	offset_fsb,
	xfs_filblks_t	buf_fsb,
	xfs_bmbt_irec_t	*imap,
	int		imap_count);

#endif /* DEBUG */

STATIC void
xfs_delalloc_cleanup(
	xfs_inode_t	*ip,
	xfs_fileoff_t	start_fsb,
	xfs_filblks_t	count_fsb);

ssize_t
xfs_rdwr(
	bhv_desc_t	*bdp,
	struct file	*filp,
	char 		*buf,
	size_t		size,
	loff_t		*offsetp,
	int		read)	/* set if read, otherwise this is write */
{
	ssize_t ret;
	struct xfs_inode *xip;

	xip = XFS_BHVTOI(bdp);
	if (XFS_FORCED_SHUTDOWN(xip->i_mount)) {
		ret = -EIO;
		goto out;
	}

	ret = 0;
	if (size == 0) {
		goto out;
	}

	if (read) {
		ret = pagebuf_generic_file_read(filp, buf, size, offsetp); 
		/* if (!(ioflag & IO_INVIS)) add this somehow with DMAPI */
			xfs_ichgtime(xip, XFS_ICHGTIME_ACC);
	} else {
		ret = pagebuf_generic_file_write(filp, buf, size, offsetp);
	}
out:
	return(ret);
}

ssize_t
xfs_read(
	bhv_desc_t	*bdp,
	struct file	*filp,
	char 		*buf,
	size_t		size,
	loff_t		*offsetp)
{
	ssize_t ret;

	/* xfs_rwlockf(bdp, VRWLOCK_READ, 0); obtained in readpage or linvfs_file_read */
	ret = xfs_rdwr(bdp, filp, buf, size, offsetp, 1);
	return(ret);
}

/*
 * This routine is called to handle zeroing any space in the last
 * block of the file that is beyond the EOF.  We do this since the
 * size is being increased without writing anything to that block
 * and we don't want anyone to read the garbage on the disk.
 */

/* We don' want the IRIX poff */
#define poff(x) ((x) & (PAGE_SIZE-1))

int xfs_zlb_debug = 0;

/* ARGSUSED */
STATIC int				/* error */
xfs_zero_last_block(
	struct inode	*ip,
	xfs_iocore_t	*io,
	off_t		offset,
	xfs_fsize_t	isize,
	struct pm	*pmp)
{
	xfs_fileoff_t	last_fsb;
	xfs_fileoff_t	next_fsb;
	xfs_fileoff_t	end_fsb;
	xfs_fsblock_t	firstblock;
	xfs_mount_t	*mp;
	page_buf_t	*pb;
	int		nimaps;
	int		zero_offset;
	int		zero_len;
	int		isize_fsb_offset;
	int		i;
	int		error;
	int		hole;
	xfs_bmbt_irec_t	imap;
	loff_t		loff;
	size_t		lsize;


	dprintk(xfs_zlb_debug,
	       ("zlb: ip 0x%p off 0x%Lx isize 0x%Lx\n",
		ip, offset, isize));
	ASSERT(ismrlocked(io->io_lock, MR_UPDATE) != 0);
	ASSERT(offset > isize);

	mp = io->io_mount;

	/*
	 * If the file system block size is less than the page size,
	 * then there could be bytes in the last page after the last
	 * fsblock containing isize which have not been initialized.
	 * Since if such a page is in memory it will be
	 * fully accessible, we need to zero any part of
	 * it which is beyond the old file size.  We don't need to send
	 * this out to disk, we're just initializing it to zeroes like
	 * we would have done in xfs_strat_read() had the size been bigger.
	 */
	dprintk(xfs_zlb_debug,
	     ("zlb: sb_blocksize 0x%x poff(isize) 0x%Lx\n",
		    mp->m_sb.sb_blocksize, poff(isize)));
	if ((mp->m_sb.sb_blocksize < NBPP) && ((i = poff(isize)) != 0)) {
		struct page *page;
		struct page ** hash;

		hash = page_hash(&ip->i_data, isize >> PAGE_CACHE_SHIFT);
		page = __find_lock_page(&ip->i_data, isize >> PAGE_CACHE_SHIFT, hash);
		if (page) {

			dprintk(xfs_zlb_debug,
			        ("zlb: memset page 0x%p paddr 0x%lx from 0x%lx sz 0x%lx\n",
				page, page_address(page),
				page_address(page) + i, PAGE_SIZE -i));

			memset((void *)page_address(page)+i, 0, PAGE_SIZE-i);

			/*
			 * Now we check to see if there are any holes in the
			 * page over the end of the file that are beyond the
			 * end of the file.  If so, we want to set the P_HOLE
			 * flag in the page and blow away any active mappings
			 * to it so that future faults on the page will cause
			 * the space where the holes are to be allocated.
			 * This keeps us from losing updates that are beyond
			 * the current end of file when the page is already
			 * in memory.
			 */
			next_fsb = XFS_B_TO_FSBT(mp, isize);
			end_fsb = XFS_B_TO_FSB(mp, ctooff(offtoc(isize)));
			hole = 0;
			while (next_fsb < end_fsb) {
				nimaps = 1;
				firstblock = NULLFSBLOCK;
				error = XFS_BMAPI(mp, NULL, io, next_fsb, 1, 0,
						  &firstblock, 0, &imap,
						  &nimaps, NULL);
				if (error) {
					clear_bit(PG_locked, &page->flags);
					page_cache_release(page);
					return error;
				}
				ASSERT(nimaps > 0);
				if (imap.br_startblock == HOLESTARTBLOCK) {
					hole = 1;
					break;
				}
				next_fsb++;
			}
			if (hole) {
				printk("xfs_zero_last_block: hole found? need more implementation\n");
#ifndef linux
				/*
				 * In order to make processes notice the
				 * newly set P_HOLE flag, blow away any
				 * mappings to the file.  We have to drop
				 * the inode lock while doing this to avoid
				 * deadlocks with the chunk cache.
				 */
				if (VN_MAPPED(vp)) {
					XFS_IUNLOCK(mp, io, XFS_ILOCK_EXCL |
							    XFS_EXTSIZE_RD);
					VOP_PAGES_SETHOLE(vp, pfdp, 1, 1,
						ctooff(offtoct(isize)));
					XFS_ILOCK(mp, io, XFS_ILOCK_EXCL |
							  XFS_EXTSIZE_RD);
				}
#endif
			}
			clear_bit(PG_locked, &page->flags);
			page_cache_release(page);
		} 
	}

	isize_fsb_offset = XFS_B_FSB_OFFSET(mp, isize);
	if (isize_fsb_offset == 0) {
		/*
		 * There are no extra bytes in the last block on disk to
		 * zero, so return.
		 */
		return 0;
	}

	last_fsb = XFS_B_TO_FSBT(mp, isize);
	nimaps = 1;
	firstblock = NULLFSBLOCK;
	error = XFS_BMAPI(mp, NULL, io, last_fsb, 1, 0, &firstblock, 0, &imap,
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
	 * Get a pagebuf for the last block, zero the part beyond the
	 * EOF, and write it out sync.  We need to drop the ilock
	 * while we do this so we don't deadlock when the buffer cache
	 * calls back to us. JIMJIM is this true with pagebufs?
	 */
	XFS_IUNLOCK(mp, io, XFS_ILOCK_EXCL| XFS_EXTSIZE_RD);
	loff = XFS_FSB_TO_B(mp, last_fsb);
	lsize = BBTOB(XFS_FSB_TO_BB(mp, 1));

	dprintk(xfs_zlb_debug,
	      ("zlb: pbget ip 0x%p loff 0x%Lx lsize 0x%x last_fsb 0x%Lx\n",
		ip, loff, lsize, last_fsb));

	/*
	 * JIMJIM what about the real-time device
	 */
	pb = pagebuf_get(ip, loff, lsize, 0);
	if (!pb) {
		error = -ENOMEM;
		XFS_ILOCK(mp, io, XFS_ILOCK_EXCL|XFS_EXTSIZE_RD);
		return error;
	}
	if (imap.br_startblock > 0) {
		pb->pb_bn = XFS_FSB_TO_DB_IO(io, imap.br_startblock);
		if (imap.br_state == XFS_EXT_UNWRITTEN) {
			printk("xfs_zero_last_block: unwritten?\n");
		}
	} else {
		printk("xfs_zero_last_block: delay alloc???\n");
		error = -ENOSYS;
		goto out_lock;
	}

	if (PBF_NOT_DONE(pb)) {
		if (error = pagebuf_iostart(pb, PBF_READ)) {
			pagebuf_rele(pb);
			goto out_lock;
		}
	}

	zero_offset = isize_fsb_offset;
	zero_len = mp->m_sb.sb_blocksize - isize_fsb_offset;

	dprintk(xfs_zlb_debug,
	      ("zlb: pb_iozero pb 0x%p zf 0x%x zl 0x%x\n",
		pb, zero_offset, zero_len));

	if (error = pagebuf_iozero(pb, zero_offset, zero_len)) {
		pagebuf_rele(pb);
		goto out_lock;
	}
	if (error = pagebuf_iostart(pb, PBF_WRITE)) {
		pagebuf_rele(pb);
		goto out_lock;
	}

	/*
	 * We don't want to start a transaction here, so don't
	 * push out a buffer over a delayed allocation extent.
	 * Also, we can get away with it since the space isn't
	 * allocated so it's faster anyway.
	 *
	 * We don't bother to call xfs_b*write here since this is
	 * just userdata, and we don't want to bring the filesystem
	 * down if they hit an error. Since these will go through
	 * xfsstrategy anyway, we have control over whether to let the
	 * buffer go thru or not, in case of a forced shutdown.
	 */

	if (imap.br_startblock == DELAYSTARTBLOCK ||
	    imap.br_state == XFS_EXT_UNWRITTEN) {
		printk("xfs_zero_last_block: We want DELWRI? not waiting?\n");
		/* XFS_bdwrite(bp);*/
	}

out_lock:
	XFS_ILOCK(mp, io, XFS_ILOCK_EXCL|XFS_EXTSIZE_RD);
	return error;
}

/*
 * Zero any on disk space between the current EOF and the new,
 * larger EOF.  This handles the normal case of zeroing the remainder
 * of the last block in the file and the unusual case of zeroing blocks
 * out beyond the size of the file.  This second case only happens
 * with fixed size extents and when the system crashes before the inode
 * size was updated but after blocks were allocated.  If fill is set,
 * then any holes in the range are filled and zeroed.  If not, the holes
 * are left alone as holes.
 */

int xfs_zeof_debug = 0;

int					/* error */
xfs_zero_eof(
	vnode_t		*vp,
	xfs_iocore_t	*io,
	off_t		offset,
	xfs_fsize_t	isize,
	struct pm       *pmp)
{
	struct inode	*ip = vp->v_inode;
	xfs_fileoff_t	start_zero_fsb;
	xfs_fileoff_t	end_zero_fsb;
	xfs_fileoff_t	prev_zero_fsb;
	xfs_fileoff_t	zero_count_fsb;
	xfs_fileoff_t	last_fsb;
	xfs_fsblock_t	firstblock;
	xfs_extlen_t	buf_len_fsb;
	xfs_extlen_t	prev_zero_count;
	xfs_mount_t	*mp;
	page_buf_t	*pb;
	int		nimaps;
	int		error = 0;
	xfs_bmbt_irec_t	imap;
	int		i;
	int		length;
	loff_t		loff;
	size_t		lsize;

	ASSERT(ismrlocked(io->io_lock, MR_UPDATE));
	ASSERT(ismrlocked(io->io_iolock, MR_UPDATE));

	mp = io->io_mount;

	dprintk(xfs_zeof_debug,
		("zeof ip 0x%p offset 0x%Lx size 0x%Lx\n",
		ip, offset, isize));
	/*
	 * First handle zeroing the block on which isize resides.
	 * We only zero a part of that block so it is handled specially.
	 */
	error = xfs_zero_last_block(ip, io, offset, isize, pmp);
	if (error) {
		ASSERT(ismrlocked(io->io_lock, MR_UPDATE));
		ASSERT(ismrlocked(io->io_iolock, MR_UPDATE));
		return error;
	}

	/*
	 * Calculate the range between the new size and the old
	 * where blocks needing to be zeroed may exist.  To get the
	 * block where the last byte in the file currently resides,
	 * we need to subtract one from the size and truncate back
	 * to a block boundary.  We subtract 1 in case the size is
	 * exactly on a block boundary.
	 */
	last_fsb = isize ? XFS_B_TO_FSBT(mp, isize - 1) : (xfs_fileoff_t)-1;
	start_zero_fsb = XFS_B_TO_FSB(mp, (xfs_ufsize_t)isize);
	end_zero_fsb = XFS_B_TO_FSBT(mp, offset - 1);

	dprintk(xfs_zeof_debug,
		("zero: last block %Ld end %Ld\n",
		last_fsb, end_zero_fsb));

	ASSERT((xfs_sfiloff_t)last_fsb < (xfs_sfiloff_t)start_zero_fsb);
	if (last_fsb == end_zero_fsb) {
		/*
		 * The size was only incremented on its last block.
		 * We took care of that above, so just return.
		 */
		return 0;
	}

	ASSERT(start_zero_fsb <= end_zero_fsb);
	prev_zero_fsb = NULLFILEOFF;
	prev_zero_count = 0;
	/*
	 * JIMJIM maybe change this loop to do the bmapi call and
	 * loop while we split the mappings into pagebufs?
	 */
	while (start_zero_fsb <= end_zero_fsb) {
		dprintk(xfs_zeof_debug,
			("zero: start block %Ld end %Ld\n",
			start_zero_fsb, end_zero_fsb));
		nimaps = 1;
		zero_count_fsb = end_zero_fsb - start_zero_fsb + 1;
		firstblock = NULLFSBLOCK;
		error = XFS_BMAPI(mp, NULL, io, start_zero_fsb, zero_count_fsb,
				  0, &firstblock, 0, &imap, &nimaps, NULL);
		if (error) {
			ASSERT(ismrlocked(io->io_lock, MR_UPDATE));
			ASSERT(ismrlocked(io->io_iolock, MR_UPDATE));
			return error;
		}
		ASSERT(nimaps > 0);

		if (imap.br_startblock == HOLESTARTBLOCK) {
			/* 
			 * This loop handles initializing pages that were
			 * partially initialized by the code below this 
			 * loop. It basically zeroes the part of the page
			 * that sits on a hole and sets the page as P_HOLE
			 * and calls remapf if it is a mapped file.
			 */	
			if ((prev_zero_fsb != NULLFILEOFF) && 
			    (dtopt(XFS_FSB_TO_BB(mp, prev_zero_fsb)) ==
			     dtopt(XFS_FSB_TO_BB(mp, imap.br_startoff)) ||
			     dtopt(XFS_FSB_TO_BB(mp, prev_zero_fsb + 
						     prev_zero_count)) ==
			     dtopt(XFS_FSB_TO_BB(mp, imap.br_startoff)))) {
				dprintk(xfs_zeof_debug,
			     		("xfs_zero_eof: look for pages to zero? HOLE\n"));
			}
		   	prev_zero_fsb = NULLFILEOFF;
			prev_zero_count = 0;
		   	start_zero_fsb = imap.br_startoff +
					 imap.br_blockcount;
			ASSERT(start_zero_fsb <= (end_zero_fsb + 1));
			continue;
		}

		/*
		 * There are blocks in the range requested.
		 * Zero them a single write at a time.  We actually
		 * don't zero the entire range returned if it is
		 * too big and simply loop around to get the rest.
		 * That is not the most efficient thing to do, but it
		 * is simple and this path should not be exercised often.
		 */
		buf_len_fsb = XFS_FILBLKS_MIN(imap.br_blockcount,
					      io->io_writeio_blocks);
		dprintk(xfs_zeof_debug,
			("zero: buf len is %d block\n", buf_len_fsb));
		/*
		 * Drop the inode lock while we're doing the I/O.
		 * We'll still have the iolock to protect us.
		 */
		XFS_IUNLOCK(mp, io, XFS_ILOCK_EXCL|XFS_EXTSIZE_RD);

		loff = XFS_FSB_TO_B(mp, start_zero_fsb);
		lsize = XFS_FSB_TO_B(mp, buf_len_fsb);
		/*
		 * JIMJIM what about the real-time device
		 */
		dprintk(xfs_zeof_debug,
			("xfs_zero_eof: NEW CODE doing %d starting at %Ld\n",
			lsize, loff));

		pb = pagebuf_get(ip, loff, lsize, 0);
		if (!pb) {
			error = -ENOMEM;
			goto out_lock;
		}

		if (imap.br_startblock == DELAYSTARTBLOCK) {
			dprintk(xfs_zeof_debug,
				("xfs_zero_eof: hmmm what do we do here?\n"));
			error = -ENOSYS;
			goto out_lock;
		} else {
			pb->pb_bn = XFS_FSB_TO_DB_IO(io, imap.br_startblock);
			if (imap.br_state == XFS_EXT_UNWRITTEN) {
				dprintk(xfs_zeof_debug,
					("xfs_zero_eof: unwritten? what do we do here?\n"));
			}
		}
		if (io->io_flags & XFS_IOCORE_RT) {
			dprintk(xfs_zeof_debug,
				("xfs_zero_eof: real time device? use diff inode\n"));
		}

		if (error = pagebuf_iozero(pb, 0, lsize)) {
			goto out_lock;
		}
		if (error = pagebuf_iostart(pb, PBF_WRITE)) {
			goto out_lock;
		}
		if (imap.br_startblock == DELAYSTARTBLOCK ||
		    imap.br_state == XFS_EXT_UNWRITTEN) { /* DELWRI */
			dprintk(xfs_zeof_debug,
				("xfs_zero_eof: need to allocate? delwri\n"));
		}
		if (error) {
			goto out_lock;
		}

		prev_zero_fsb = start_zero_fsb;
		prev_zero_count = buf_len_fsb;
		start_zero_fsb = imap.br_startoff + buf_len_fsb;
		dprintk(xfs_zeof_debug,
			("moved start to %Ld\n", start_zero_fsb));
		ASSERT(start_zero_fsb <= (end_zero_fsb + 1));

		XFS_ILOCK(mp, io, XFS_ILOCK_EXCL|XFS_EXTSIZE_RD);
	}

	dprintk(xfs_zeof_debug, ("zero: all done\n"));

	return 0;

out_lock:

	XFS_ILOCK(mp, io, XFS_ILOCK_EXCL|XFS_EXTSIZE_RD);
	return error;
}

int xfsw_debug = 0;

ssize_t
xfs_write(
	bhv_desc_t	*bdp,
	struct file	*filp,
	char		*buf,
	size_t		size,
	loff_t		*offsetp)
{
	xfs_inode_t *xip;
	struct dentry *dentry = filp->f_dentry;
	struct inode *ip = dentry->d_inode;
	struct xfs_mount *mp;
	ssize_t ret;
	xfs_fsize_t     isize;
	xfs_iocore_t    *io;

	xfs_rwlockf(bdp, VRWLOCK_WRITE, 0);
	xip = XFS_BHVTOI(bdp);
	io = &(xip->i_iocore);
	mp = io->io_mount;
	isize = XFS_SIZE(mp, io); /* JIMJIM do we need to lock for this? */

	dprintk(xfsw_debug,
	     ("xfsw: ip 0x%p(is 0x%Lx) offset 0x%Lx size 0x%x\n",
		ip, ip->i_size, *offsetp, size));

	/*
	 * On Linux, generic_file_write updates the times even if
	 * no data is copied in so long as the write had a size.
	 *
	 * We must update xfs' times since revalidate will overcopy xfs.
	 */
	if (size) {
		/* if (!(ioflag & IO_INVIS)) add this somehow with DMAPI */
			xfs_ichgtime(xip, XFS_ICHGTIME_MOD | XFS_ICHGTIME_CHG);
	}
	/*
	 * If the offset is beyond the size of the file, we have a couple
	 * of things to do. First, if there is already space allocated
	 * we need to either create holes or zero the disk or ...
	 *
	 * If there is a page where the previous size lands, we need
	 * to zero it out up to the new size.
	 */
	
	if (*offsetp > isize && isize) {
		XFS_ILOCK(mp, io, XFS_ILOCK_EXCL | XFS_EXTSIZE_RD);
		io->io_writeio_blocks = mp->m_writeio_blocks;
		ret = xfs_zero_eof(BHV_TO_VNODE(bdp), io, *offsetp,
			isize, NULL);
		XFS_IUNLOCK(mp, io, XFS_ILOCK_EXCL | XFS_EXTSIZE_RD);
		if (ret) {
			xfs_rwunlock(bdp, VRWLOCK_WRITE);
			return(ret); /* JIMJIM should this be negative? */
		}
	}

	ret = xfs_rdwr(bdp, filp, buf, size, offsetp, 0);

	/* JIMJIM Lock? around the stuff below if Linux doesn't lock above */
	if (ret > 0) {
		unsigned int mode;
		/* set S_IGID if S_IXGRP is set, and always set S_ISUID */
		mode = (ip->i_mode & S_IXGRP)*(S_ISGID/S_IXGRP) | S_ISUID;

		/* was any of the uid bits set? */
		mode &= ip->i_mode;
		if (mode && !capable(CAP_FSETID)) {
			ip->i_mode &= ~mode;
			xfs_write_clear_setuid(xip);
		}
		if (*offsetp > xip->i_d.di_size) {
			XFS_SETSIZE(mp, io, *offsetp);
		}
	}
	xfs_rwunlock(bdp, VRWLOCK_WRITE);
	return(ret);
}

/*
 * xfs_bmap() is the same as the irix xfs_bmap from xfs_rw.c 
 * execpt for slight changes to the params
 */
int
xfs_bmap(bhv_desc_t	*bdp,
	loff_t		offset,
	ssize_t		count,
	int		flags,
	pb_bmap_t	*pbmapp,
	int		*npbmaps)
{
	xfs_inode_t	*ip;
	int		error;
	int		unlocked;
	int		lockmode;
	int		fsynced;
	int		ioflag = 0;	/* Needs to be passed in */
	vnode_t		*vp;

	ip = XFS_BHVTOI(bdp);
	ASSERT((ip->i_d.di_mode & IFMT) == IFREG);
	ASSERT(((ip->i_d.di_flags & XFS_DIFLAG_REALTIME) != 0) ==
	       ((ip->i_iocore.io_flags & XFS_IOCORE_RT) != 0));
	ASSERT((flags & PBF_READ) || (flags & PBF_WRITE));

	if (XFS_FORCED_SHUTDOWN(ip->i_iocore.io_mount))
		return (EIO);

	if (flags & PBF_READ) {
		ASSERT(ismrlocked(&ip->i_iolock, MR_ACCESS | MR_UPDATE) != 0);
		unlocked = 0;
		lockmode = xfs_ilock_map_shared(ip);
		error = xfs_iomap_read(&ip->i_iocore, offset, count,
				 pbmapp, npbmaps, NULL);
		xfs_iunlock_map_shared(ip, lockmode);
	} else { /* PBF_WRITE */
		ASSERT(ismrlocked(&ip->i_iolock, MR_UPDATE) != 0);
		ASSERT(flags & PBF_WRITE);
		vp = BHV_TO_VNODE(bdp);
		xfs_ilock(ip, XFS_ILOCK_EXCL);

		/* 
		 * Make sure that the dquots are there. This doesn't hold 
		 * the ilock across a disk read.
		 */

		if (XFS_IS_QUOTA_ON(ip->i_mount)) {
			if (XFS_NOT_DQATTACHED(ip->i_mount, ip)) {
				if (error = xfs_qm_dqattach(ip, XFS_QMOPT_ILOCKED)) {
					xfs_iunlock(ip, XFS_ILOCK_EXCL);
					return error;
				}
			}
		}
retry:
		error = xfs_iomap_write(&ip->i_iocore, offset, count, 
					pbmapp, npbmaps, flags, NULL);
		/* xfs_iomap_write unlocks/locks/unlocks */

		if (error == ENOSPC) {
			xfs_fsize_t	last_byte;

			switch (fsynced) {
			case 0:
				VOP_FLUSH_PAGES(vp, 0, 0, FI_NONE, error);
				error = 0;
				fsynced = 1;
				xfs_ilock(ip, XFS_ILOCK_EXCL);
				goto retry;
			case 1:
				fsynced = 2;
				if (!(ioflag & O_SYNC)) {
					ioflag |= O_SYNC;
					error = 0;
					xfs_ilock(ip, XFS_ILOCK_EXCL);
					goto retry;
				}
			case 2:
			case 3:
				VFS_SYNC(vp->v_vfsp,
					SYNC_NOWAIT|SYNC_BDFLUSH|SYNC_FSDATA,
					NULL, error);
				error = 0;
/**
				delay(HZ);
**/
				fsynced++;
				xfs_ilock(ip, XFS_ILOCK_EXCL);
				goto retry;
			}
		}
	}
	return error;
}	


int
_xfs_imap_to_bmap(
	xfs_iocore_t    *io,
	off_t		offset,
	xfs_bmbt_irec_t *imap,
	pb_bmap_t	*pbmapp,
	int		imaps,			/* Number of imap entries */
	int		pbmaps)			/* Number of pbmap entries */
{
	xfs_mount_t     *mp;
	xfs_fsize_t	nisize;
	int		im, pbm;
	xfs_fsblock_t	start_block;

	mp = io->io_mount;
	nisize = XFS_SIZE(mp, io);
	if (io->io_new_size > nisize)
		nisize = io->io_new_size;

	for (im=0, pbm=0; im < imaps && pbm < pbmaps; im++,pbmapp++,imap++,pbm++) {
 		/* printk("_xfs_imap_to_bmap %Ld %Ld %Ld %d\n",
			imap->br_startoff, imap->br_startblock,
			imap->br_blockcount, imap->br_state); */

		pbmapp->pbm_offset = offset - XFS_FSB_TO_B(mp, imap->br_startoff);
		pbmapp->pbm_bsize = XFS_FSB_TO_B(mp, imap->br_blockcount);
		pbmapp->pbm_flags = 0;

		start_block = imap->br_startblock;
		if (start_block == HOLESTARTBLOCK) {
			pbmapp->pbm_bn = -1;
			pbmapp->pbm_flags = PBMF_HOLE;
		} else if (start_block == DELAYSTARTBLOCK) {
			pbmapp->pbm_bn = -1;
			pbmapp->pbm_flags = PBMF_DELAY;
		} else {
			pbmapp->pbm_bn = XFS_FSB_TO_DB_IO(io, start_block);
			if (imap->br_state == XFS_EXT_UNWRITTEN)
				pbmapp->pbm_flags |= PBMF_UNWRITTEN;
		}

		if (XFS_FSB_TO_B(mp, pbmapp->pbm_offset + pbmapp->pbm_bsize)
								>= nisize) {
			pbmapp->pbm_flags |= PBMF_EOF;
		}
		
		offset += pbmapp->pbm_bsize;
	}
	return(pbm);	/* Return the number filled */
}

int
xfs_iomap_read(
	xfs_iocore_t	*io,
	loff_t		offset,
	size_t		count,
	pb_bmap_t	*pbmapp,
	int		*npbmaps,
	struct pm	*pmp)
{	
	xfs_fileoff_t	offset_fsb;
	xfs_fileoff_t	end_fsb;
	xfs_fsblock_t	firstblock;
	int		nimaps;
	int		error;
	xfs_mount_t	*mp;
	xfs_bmbt_irec_t	imap[XFS_MAX_RW_NBMAPS];

	ASSERT(ismrlocked(io->io_lock, MR_UPDATE | MR_ACCESS) != 0);
/**	ASSERT(ismrlocked(io->io_iolock, MR_UPDATE | MR_ACCESS) != 0); **/
/*	xfs_iomap_enter_trace(XFS_IOMAP_READ_ENTER, io, offset, count); */

	mp = io->io_mount;
	offset_fsb = XFS_B_TO_FSBT(mp, offset);
	nimaps = sizeof(imap) / sizeof(imap[0]);
	nimaps = min(nimaps, *npbmaps); /* Don't ask for more than caller has */
	end_fsb = XFS_B_TO_FSB(mp, ((xfs_ufsize_t)(offset + count)));
	firstblock = NULLFSBLOCK;
	error = XFS_BMAPI(mp, NULL, io, offset_fsb,
				(xfs_filblks_t)(end_fsb - offset_fsb),
				/* XFS_BMAPI_ENTIRE */ 0, &firstblock, 0, imap,
				&nimaps, NULL);
	if (error) {
		return error;
	}

	if(nimaps) {
		*npbmaps = _xfs_imap_to_bmap(io, offset, imap, pbmapp, nimaps,
			*npbmaps);
	} else
		*npbmaps = 0;
	return error;
}

/*
 * xfs_iomap_write: return pagebuf_bmap_t's telling higher layers
 *	where to write.
 * There are 2 main cases:
 *	1 the extents already exist
 *	2 must allocate.
 * 	There are 3 cases when we allocate:
 *		delay allocation (doesn't really allocate or use transactions)
 *		direct allocation (no previous delay allocation
 *		convert delay to real allocations
 */

STATIC int
xfs_iomap_write(
	xfs_iocore_t	*io,
	loff_t		offset,
	size_t		count,
	pb_bmap_t	*pbmapp,
	int		*npbmaps,
	int		ioflag,
	struct pm	*pmp)
{
	xfs_inode_t	*ip = XFS_IO_INODE(io);
	xfs_mount_t	*mp;
	int		maps;
	int		error;

#define	XFS_WRITE_IMAPS	XFS_BMAP_MAX_NMAP
	xfs_bmbt_irec_t	imap[XFS_WRITE_IMAPS], *imapp;
	xfs_bmap_free_t free_list;
	int		allocate; 
	int		found; 
	int		iunlock = 1; /* Cleared if lower routine did unlock */

	maps = *npbmaps;
	if (!maps)
		goto out;

	/*
	 * If we have extents that are allocated for this range,
	 * return them.
	 */

	found = 0;
	error = xfs_iomap_read(io, offset, count, pbmapp, npbmaps, NULL);
	if (error)
		goto out;

	/*
	 * If we found mappings and they can just have data written
	 * without conversion,
	 * let the caller write these and call us again.
	 *
	 * If we have a HOLE or UNWRITTEN, proceed down lower to
	 * get the space or to convert to written.
	 *
	 * If we are allocating, we can't have DELAY, either.
	 */

	allocate = ioflag & PBF_FILE_ALLOCATE;
	if (*npbmaps) {
		int not_ok_flags;

		if (allocate) {
			not_ok_flags = (PBMF_HOLE|PBMF_UNWRITTEN|PBMF_DELAY);
		} else {
			not_ok_flags = (PBMF_HOLE|PBMF_UNWRITTEN);
		}

		if (!(pbmapp->pbm_flags & not_ok_flags)) {
			*npbmaps = 1; /* Only checked the first one. */
					/* We could check more, ... */
			goto out;
		}
	}
	found = *npbmaps;
	*npbmaps = maps; /* Restore to original requested */

	if (allocate) {
		error = xfs_iomap_write_convert(io, offset, count, pbmapp,
					npbmaps, ioflag, found);
		iunlock = 0;	/* xfs_iomap_write_convert unlocks inode */
	} else {
		if (ioflag & PBF_DIRECT)
			error = xfs_iomap_write_direct(io, offset, count, pbmapp,
						npbmaps, ioflag, found);
		else
			error = xfs_iomap_write_delay(io, offset, count, pbmapp,
					npbmaps, ioflag, found); 
	}

out:
	if (iunlock)
		xfs_iunlock(ip, XFS_ILOCK_EXCL);

out_no_unlock:
	XFS_INODE_CLEAR_READ_AHEAD(&ip->i_iocore);
	return error;
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
	xfs_iocore_t	*io,
	xfs_fileoff_t	offset_fsb,
	xfs_filblks_t	buf_fsb,
	xfs_bmbt_irec_t	*imap,
	int		imap_count)
{
	xfs_filblks_t	count_fsb;
	xfs_fsblock_t	firstblock;
	xfs_mount_t	*mp;
	int		nimaps;
	int		n;
	int		error;

	if (!IO_IS_XFS(io)) return;

	mp = io->io_mount;
	count_fsb = 0;
	while (count_fsb < buf_fsb) {
		nimaps = imap_count;
		firstblock = NULLFSBLOCK;
		error = XFS_BMAPI(mp, NULL, io, (offset_fsb + count_fsb),
				  (buf_fsb - count_fsb), 0, &firstblock, 0,
				  imap, &nimaps, NULL);
		if (error) {
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
	return;
}
#endif /* DEBUG */

/*
 * Map the given I/O size and I/O alignment over the given extent.
 * If we're at the end of the file and the underlying extent is
 * delayed alloc, make sure we extend out to the
 * next i_writeio_blocks boundary.  Otherwise make sure that we
 * are confined to the given extent.
 */
/*ARGSUSED*/
STATIC void
xfs_write_bmap(
	xfs_mount_t	*mp,
	xfs_iocore_t	*io,
	xfs_bmbt_irec_t	*imapp,
	pb_bmap_t	*pbmapp,
	int		iosize,
	xfs_fileoff_t	ioalign,
	xfs_fsize_t	isize)
{
	__int64_t	extra_blocks;
	xfs_fileoff_t	size_diff;
	xfs_fileoff_t	ext_offset;
	xfs_fsblock_t	start_block;
	int		length;		/* length of this mapping in blocks */
	off_t		offset;		/* logical block offset of this mapping */
	
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
		offset = imapp->br_startoff;
		pbmapp->pbm_offset = 0; /* At the start of the map */
	} else {
		/*
		 * The alignment requested fits on this extent,
		 * so use it.
		 */
		ext_offset = ioalign - imapp->br_startoff;
		offset = ioalign;
		pbmapp->pbm_offset = XFS_FSB_TO_B(mp, ext_offset);
	}
	start_block = imapp->br_startblock;
	ASSERT(start_block != HOLESTARTBLOCK);
	if (start_block != DELAYSTARTBLOCK) {
		pbmapp->pbm_bn = XFS_FSB_TO_DB_IO(io, start_block + ext_offset);
		if (imapp->br_state == XFS_EXT_UNWRITTEN) {
			pbmapp->pbm_flags = PBMF_UNWRITTEN;
		}
	} else {
		pbmapp->pbm_bn = -1;
		pbmapp->pbm_flags = PBMF_DELAY;
	}
	length = iosize;

	/*
	 * If the iosize from our offset extends beyond the end of
	 * the extent, then trim down length to match that of the extent.
	 */
	extra_blocks = (off_t)(offset + length) -
		       (__uint64_t)(imapp->br_startoff +
				    imapp->br_blockcount);
	if (extra_blocks > 0) {
		length -= extra_blocks;
		ASSERT(length > 0);
	}

	pbmapp->pbm_bsize = XFS_FSB_TO_B(mp, length);
}


int
xfs_iomap_write_delay(
	xfs_iocore_t	*io,
	loff_t		offset,
	size_t		count,
	pb_bmap_t	*pbmapp,
	int		*npbmaps,
	int		ioflag,
	int		found)
{
	xfs_fileoff_t	offset_fsb;
	xfs_fileoff_t	ioalign;
	xfs_fileoff_t	last_fsb;
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
	short		small_write;
	size_t		count_remaining;
	xfs_mount_t	*mp;
	pb_bmap_t	*curr_bmapp;
	pb_bmap_t	*next_bmapp;
	pb_bmap_t	*last_bmapp;
	xfs_bmbt_irec_t	*curr_imapp;
	xfs_bmbt_irec_t	*last_imapp;
#define	XFS_WRITE_IMAPS	XFS_BMAP_MAX_NMAP
	xfs_bmbt_irec_t	imap[XFS_WRITE_IMAPS];
	int		aeof;

	ASSERT(ismrlocked(io->io_lock, MR_UPDATE) != 0);

/* 	xfs_iomap_enter_trace(XFS_IOMAP_WRITE_ENTER, io, offset, count); */

	mp = io->io_mount;
/***
	ASSERT(! XFS_NOT_DQATTACHED(mp, ip));
***/

	isize = XFS_SIZE(mp, io);
	if (io->io_new_size > isize) {
		isize = io->io_new_size;
	}

	aeof = 0;
	offset_fsb = XFS_B_TO_FSBT(mp, offset);
	last_fsb = XFS_B_TO_FSB(mp, ((xfs_ufsize_t)(offset + count)));
	printk("xfs_iomap_write_delay: allocating from offset %Ld to %Ld\n",
			offset_fsb, last_fsb);
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
	if (!(ioflag & IO_SYNC) && ((offset + count) > XFS_SIZE(mp, io))) {
		start_fsb = XFS_B_TO_FSBT(mp,
				  ((xfs_ufsize_t)(offset + count - 1)));
		count_fsb = io->io_writeio_blocks;
		while (count_fsb > 0) {
			nimaps = XFS_WRITE_IMAPS;
			firstblock = NULLFSBLOCK;
			error = XFS_BMAPI(mp, NULL, io, start_fsb, count_fsb,
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
		iosize = io->io_writeio_blocks;
		aligned_offset = XFS_WRITEIO_ALIGN(io, (offset + count - 1));
		ioalign = XFS_B_TO_FSBT(mp, aligned_offset);
		printk("xfs_iomap_write_delay change last_fsb %Ld to %Ld\n",
			last_fsb, ioalign + iosize);
		last_fsb = ioalign + iosize;
		aeof = 1;
	}
 write_map:
	nimaps = XFS_WRITE_IMAPS;
	firstblock = NULLFSBLOCK;

	/*
	 * roundup the allocation request to m_dalign boundary if file size
	 * is greater that 512K and we are allocating past the allocation eof
	 */
	if (mp->m_dalign && (XFS_SIZE(mp, io) >= 524288) && aeof) {
		int eof;
		xfs_fileoff_t new_last_fsb;
		new_last_fsb = roundup(last_fsb, mp->m_dalign);
		printk("xfs_iomap_write_delay XFS_BMAP_EOF m_dalign %d to %Ld\n",
			mp->m_dalign, new_last_fsb);
		error = XFS_BMAP_EOF(mp, io, new_last_fsb, XFS_DATA_FORK, &eof);
		if (error) {
			return error;
		}
		if (eof) {
			printk("xfs_iomap_write_delay XFS_BMAP_EOF changing last from %Ld to %Ld\n",
				last_fsb, new_last_fsb);
			last_fsb = new_last_fsb;
		}
	}

	error = XFS_BMAPI(mp, NULL, io, offset_fsb,
			  (xfs_filblks_t)(last_fsb - offset_fsb),
			  XFS_BMAPI_DELAY | XFS_BMAPI_WRITE |
			  XFS_BMAPI_ENTIRE, &firstblock, 1, imap,
			  &nimaps, NULL);
	/* 
	 * This can be EDQUOT, if nimaps == 0
	 */
	if (error) {
		return error;
	}
	/*
	 * If bmapi returned us nothing, and if we didn't get back EDQUOT,
	 * then we must have run out of space.
	 */
	if (nimaps == 0) {
/*		xfs_iomap_enter_trace(XFS_IOMAP_WRITE_NOSPACE,
				      io, offset, count); */
		return XFS_ERROR(ENOSPC);
	}

	if (!(ioflag & IO_SYNC) ||
	    ((last_fsb - offset_fsb) >= io->io_writeio_blocks)) {
		/*
		 * For normal or large sync writes, align everything
		 * into i_writeio_blocks sized chunks.
		 */
		iosize = io->io_writeio_blocks;
		aligned_offset = XFS_WRITEIO_ALIGN(io, offset);
		ioalign = XFS_B_TO_FSBT(mp, aligned_offset);
		small_write = 0;
	} else {
		/*
		 * For small sync writes try to minimize the amount
		 * of I/O we do.  Round down and up to the larger of
		 * page or block boundaries.  Set the small_write
		 * variable to 1 to indicate to the code below that
		 * we are not using the normal buffer alignment scheme.
		 */
		if (NBPP > mp->m_sb.sb_blocksize) {
			ASSERT(!(offset & PAGE_MASK));
			aligned_offset = offset;
			ioalign = XFS_B_TO_FSBT(mp, aligned_offset);
			ASSERT(!((offset + count) & PAGE_MASK));
			last_page_offset = offset + count;
			iosize = XFS_B_TO_FSBT(mp, last_page_offset -
					       aligned_offset);
		} else {
			ioalign = offset_fsb;
			iosize = last_fsb - offset_fsb;
		}
		small_write = 1;
	}

	/*
	 * Now map our desired I/O size and alignment over the
	 * extents returned by xfs_bmapi().
	 */
	xfs_write_bmap(mp, io, imap, pbmapp, iosize, ioalign, isize);
	ASSERT((pbmapp->pbm_bsize > 0) &&
	       (pbmapp->pbm_bsize - pbmapp->pbm_offset > 0));

	/*
	 * A bmap is the EOF bmap when it reaches to or beyond the new
	 * inode size.
	 */
	if ((offset + pbmapp->pbm_offset + pbmapp->pbm_bsize ) >= isize) {
		pbmapp->pbm_flags |= PBMF_EOF;
	}
	writing_bytes = pbmapp->pbm_bsize - pbmapp->pbm_offset;
	if (writing_bytes > count) {
		/*
		 * The mapping is for more bytes than we're actually
		 * going to write, so trim writing_bytes so we can
		 * get bmapp->pbsize right.
		 */
		writing_bytes = count;
	}
	pbmapp->pbm_bsize = writing_bytes;
	pbmapp->pbm_flags |= PBMF_NEW;

/* 	xfs_iomap_map_trace(XFS_IOMAP_WRITE_MAP,
			    io, offset, count, bmapp, imap);         */

	/* On IRIX, we walk more imaps filling in more bmaps. On Linux
		just handle one for now. To find the code on IRIX,
		look in xfs_iomap_write() in xfs_rw.c. */

	*npbmaps = 1;
	return 0;
}

/*
 * This is called to convert all delayed allocation blocks in the given
 * range back to 'holes' in the file.  It is used when a user's write will not
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
			xfs_iunlock(ip, XFS_ILOCK_EXCL);
			return;
		}

		ASSERT(nimaps > 0);
		n = 0;
		while (n < nimaps) {
			if (imap[n].br_startblock == DELAYSTARTBLOCK) {
				if (!XFS_FORCED_SHUTDOWN(ip->i_mount))
					xfs_force_shutdown(ip->i_mount,
						XFS_METADATA_IO_ERROR);
				error = xfs_bunmapi(NULL, ip,
						    imap[n].br_startoff,
						    imap[n].br_blockcount,
						    0, 1, &first_block, NULL,
						    &done);
				if (error) {
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

/*
 * xfs_iomap_write_convert
 * convert a hole/delalloc extent into real disk space
 * and return the new pbmap(s).
 *
 * found should contain the number of pbmapp entries to convert.
 * npbmaps on the way in is the number of entries in pbmapp that
 * can be set. On the way out, it gets set to how many we filled in.
 *
 * offset is the offset used to get the found pbmapp(s) and pbm_offset
 * is delta from that. The count is the size of the higher layers are trying
 * to write. This can be used to trim the conversion.
 */
int
xfs_iomap_write_convert(
	xfs_iocore_t	*io,
	loff_t		offset,
	size_t		count,
	pb_bmap_t	*pbmapp,
	int		*npbmaps,
	int		ioflag,
	int		found)
{ 
	xfs_fileoff_t	offset_fsb;
	off_t		offset_fsb_bb;
	xfs_fileoff_t   map_start_fsb;
	xfs_fileoff_t	imap_offset;
	xfs_fsblock_t	first_block;
	xfs_filblks_t	count_fsb;
	xfs_extlen_t	imap_blocks;
	/* REFERENCED */
	xfs_mount_t	*mp;
	xfs_inode_t	*ip;
	xfs_trans_t	*tp;
	int		error;
	xfs_bmap_free_t	free_list;
	xfs_bmbt_irec_t	*imapp;
	int		i;
	int		is_xfs = 1; /* This needs work for CXFS */
	/* REFERENCED */
	int		loops;
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

	 
	XFSSTATS.xs_xstrat_bytes += count;

	if (is_xfs) {
		ip = XFS_IO_INODE(io);
	}

	mp = io->io_mount;
	error = 0;

	if (is_xfs && XFS_IS_QUOTA_ON(mp)) {
		if (XFS_NOT_DQATTACHED(mp, ip)) {
			if (error = xfs_qm_dqattach(ip, 0)) {
				return error;
			}
		}
	}

	/*
	 * It is possible that the buffer does not start on a block
	 * boundary in the case where the system page size is less
	 * than the file system block size.  In this case, the buffer
	 * is guaranteed to be only a single page long, so we know
	 * that we will allocate the block for it in a single extent.
	 * Thus, the looping code below does not have to worry about
	 * this case.  It is only handled in the fast path code.
	 */

	ASSERT(found && (pbmapp->pbm_flags & PBMF_DELAY));

	/*
	 * Try to convert the entire delalloc extent.
	 * The start offset of this del alloc extent is
	 * 	the user's request - the delta into this mapping.
	 */
	ASSERT(offset >= pbmapp->pbm_offset);

	offset_fsb = XFS_B_TO_FSBT(mp, offset - pbmapp->pbm_offset);
	count_fsb = XFS_B_TO_FSB(mp, pbmapp->pbm_offset + pbmapp->pbm_bsize);
	offset_fsb_bb = XFS_FSB_TO_BB(mp, offset_fsb);
	xfs_strat_write_check(io, offset_fsb, count_fsb, imap, XFS_STRAT_WRITE_IMAPS);
	map_start_fsb = offset_fsb;

	while (count_fsb != 0) {
		XFS_IUNLOCK(mp, io, XFS_ILOCK_EXCL | XFS_EXTSIZE_WR);
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
			if (is_xfs) {
				tp = xfs_trans_alloc(mp,
						     XFS_TRANS_STRAT_WRITE);
				error = xfs_trans_reserve(tp, 0,
						XFS_WRITE_LOG_RES(mp),
						0, XFS_TRANS_PERM_LOG_RES,
						XFS_WRITE_LOG_COUNT);
				xfs_ilock(ip, XFS_ILOCK_EXCL);
				if (error) {
					xfs_trans_cancel(tp, 0);
					goto error0;
				}

				ASSERT(error == 0);
				xfs_trans_ijoin(tp, ip,
						XFS_ILOCK_EXCL);
				xfs_trans_ihold(tp, ip);
			} else {
				tp = NULL;
				XFS_ILOCK(mp, io, XFS_ILOCK_EXCL |
						  XFS_EXTSIZE_WR);
			}

			/*
			 * Allocate the backing store for the file.
			 */
			XFS_BMAP_INIT(&(free_list),
				      &(first_block));
			nimaps = XFS_STRAT_WRITE_IMAPS;
			error = XFS_BMAPI(mp, tp, io, map_start_fsb, count_fsb,
					  XFS_BMAPI_WRITE, &first_block, 1,
					  imap, &nimaps, &free_list);
			if (error) {
				if (is_xfs) {
					xfs_bmap_cancel(&free_list);
					xfs_trans_cancel(tp,
						 (XFS_TRANS_RELEASE_LOG_RES |
						  XFS_TRANS_ABORT));
				}
				XFS_IUNLOCK(mp, io, XFS_ILOCK_EXCL |
						    XFS_EXTSIZE_WR);
				goto error0;
			}
			ASSERT(loops++ <= (offset_fsb + count_fsb - map_start_fsb));
			if (is_xfs) {
				error = xfs_bmap_finish(&(tp), &(free_list),
						first_block, &committed);
				if (error) {
					xfs_bmap_cancel(&free_list);
					xfs_trans_cancel(tp,
						 (XFS_TRANS_RELEASE_LOG_RES |
						  XFS_TRANS_ABORT));
					xfs_iunlock(ip, XFS_ILOCK_EXCL);
					goto error0;
				}

				error = xfs_trans_commit(tp,
						 XFS_TRANS_RELEASE_LOG_RES,
						 NULL);
				if (error) {
					xfs_iunlock(ip, XFS_ILOCK_EXCL);
					goto error0;
				}
			}

			if (nimaps == 0) {
				XFS_IUNLOCK(mp, io, XFS_ILOCK_EXCL | XFS_EXTSIZE_WR);
			} /* else hold 'till we maybe loop again below */
		}

		/*
		 * See if we were able to allocate an extent that
		 * covers at least part of the user's requested size.
		 */

		offset_fsb = XFS_B_TO_FSBT(mp, offset);
		for(i = 0; i < nimaps; i++) {
			int maps;
			if (offset_fsb >= imap[i].br_startoff && 
				(offset_fsb < (imap[i].br_startoff + imap[i].br_blockcount))) {
				XFS_IUNLOCK(mp, io, XFS_ILOCK_EXCL | XFS_EXTSIZE_WR);
				maps = min(nimaps - i, *npbmaps);
				*npbmaps = _xfs_imap_to_bmap(io, offset, &imap[i],
					pbmapp, maps, *npbmaps);
				XFSSTATS.xs_xstrat_quick++;
				return 0;
			}
			count_fsb -= imap[i].br_blockcount; /* for the nxt another bmapi,
								if needed. */
		}

		/*
		 * We didn't get an extent the caller can write into so
		 * loop around and try starting after the last imap we got back.
		 */

		nimaps--; /* Index of last entry  */
		ASSERT(nimaps >= 0);
		ASSERT(offset_fsb >= imap[nimaps].br_startoff + imap[i].br_blockcount);
		ASSERT(count_fsb);
		offset_fsb = imap[nimaps].br_startoff + imap[i].br_blockcount;
		offset_fsb_bb = XFS_FSB_TO_BB(mp, offset_fsb);
		map_start_fsb = offset_fsb;
		XFSSTATS.xs_xstrat_split++;
	}

 	ASSERT(0); 	/* Should never get here */

 error0:
	if (error) {
		ASSERT(count_fsb != 0);
		ASSERT(is_xfs || XFS_FORCED_SHUTDOWN(mp));
		if (is_xfs) {
			xfs_delalloc_cleanup(ip, map_start_fsb, count_fsb);
		}
	}
	return error;
}

int xfs_direct_offset, xfs_map_last, xfs_last_map;

STATIC int
xfs_iomap_write_direct(
	xfs_iocore_t	*io,
	loff_t		offset,
	size_t		count,
	pb_bmap_t	*pbmapp,
	int		*npbmaps,
	int		ioflag,
	int		found)
{
	xfs_inode_t	*ip = XFS_IO_INODE(io);
	xfs_mount_t	*mp;
	xfs_fileoff_t	offset_fsb;
	xfs_fileoff_t	last_fsb;
	xfs_filblks_t	count_fsb;
	xfs_fsize_t	isize;
	xfs_fsblock_t	firstfsb;
	__uint64_t	last_page_offset;
	int		nimaps, maps;
	int		error;
	xfs_trans_t	*tp;

#define	XFS_WRITE_IMAPS	XFS_BMAP_MAX_NMAP
	xfs_bmbt_irec_t	imap[XFS_WRITE_IMAPS], *imapp;
	xfs_bmap_free_t free_list;
	int		aeof;
	int		bmapi_flags;
	xfs_filblks_t	datablocks;
	int		rt; 
	int		committed;
	int		numrtextents;
	uint		resblks;
	int		rtextsize;

	maps = min(XFS_WRITE_IMAPS, *npbmaps);
	nimaps = maps;

	mp = io->io_mount;
	isize = XFS_SIZE(mp, io);
	if (io->io_new_size > isize)
		isize = io->io_new_size;

	if ((offset + count) > isize) {
		aeof = 1;
	} else {
		aeof = 0;
	}

	offset_fsb = XFS_B_TO_FSBT(mp, offset);
	last_fsb = XFS_B_TO_FSB(mp, ((xfs_ufsize_t)(offset + count)));
	count_fsb = last_fsb - offset_fsb;
	if (found && (pbmapp->pbm_flags & PBMF_HOLE)) {
		xfs_fileoff_t	map_last_fsb;
		map_last_fsb = XFS_B_TO_FSB(mp,
			((xfs_ufsize_t)(offset +
				(pbmapp->pbm_bsize - pbmapp->pbm_offset))));
		
		if (pbmapp->pbm_offset) {
			xfs_direct_offset++;
		}
		if (map_last_fsb < last_fsb) {
			xfs_map_last++;
			last_fsb = map_last_fsb;
			count_fsb = last_fsb - offset_fsb;
		} else if (last_fsb < map_last_fsb) {
			xfs_last_map++;
		}
		ASSERT(count_fsb > 0);
	}

	/*
	 * roundup the allocation request to m_dalign boundary if file size
	 * is greater that 512K and we are allocating past the allocation eof
	 */
	if (!found && mp->m_dalign && (isize >= 524288) && aeof) {
		int eof;
		xfs_fileoff_t new_last_fsb;
		new_last_fsb = roundup(last_fsb, mp->m_dalign);
		printk("xfs_iomap_write_direct: about to XFS_BMAP_EOF %Ld\n",
			new_last_fsb);
		error = XFS_BMAP_EOF(mp, io, new_last_fsb, XFS_DATA_FORK, &eof);
		if (error) {
			goto error_out;
		}
		if (eof)
			last_fsb = new_last_fsb;
	}

	bmapi_flags = XFS_BMAPI_WRITE|XFS_BMAPI_DIRECT_IO|XFS_BMAPI_ENTIRE;
	bmapi_flags &= ~XFS_BMAPI_DIRECT_IO;

	/*
	 * determine if this is a realtime file
	 */
        if (rt = (ip->i_d.di_flags & XFS_DIFLAG_REALTIME) != 0) {
                rtextsize = mp->m_sb.sb_rextsize;
        } else
                rtextsize = 0;

	error = 0;

	/*
	 * allocate file space for the bmapp entries passed in.
 	 */

	/*
	 * determine if reserving space on
	 * the data or realtime partition.
	 */
	if (rt) {
		numrtextents = (count_fsb + rtextsize - 1) /
				rtextsize;
		datablocks = 0;
	} else {
		datablocks = count_fsb;
		numrtextents = 0;
	}

	/*
	 * allocate and setup the transaction
	 */
	tp = xfs_trans_alloc(mp, XFS_TRANS_DIOSTRAT);
	resblks = XFS_DIOSTRAT_SPACE_RES(mp, datablocks);

	xfs_iunlock(ip, XFS_ILOCK_EXCL);
	
	error = xfs_trans_reserve(tp,
				  resblks,
				  XFS_WRITE_LOG_RES(mp),
				  numrtextents,
				  XFS_TRANS_PERM_LOG_RES,
				  XFS_WRITE_LOG_COUNT);

	/*
	 * check for running out of space
	 */
	if (error) {
		/*
		 * Free the transaction structure.
		 */
		xfs_trans_cancel(tp, 0);
	}

	xfs_ilock(ip, XFS_ILOCK_EXCL);

	if (error)  {
		goto error_out; /* Don't return in above if .. trans ..,
					need lock to return */
	}

	if (XFS_IS_QUOTA_ON(mp)) {
		if (xfs_trans_reserve_quota(tp, 
					    ip->i_udquot, 
					    ip->i_pdquot,
					    resblks, 0, 0)) {
			error = (EDQUOT);
			goto error1;
		}
		nimaps = 1;
	} else {
		nimaps = 2;
	}	

	xfs_trans_ijoin(tp, ip, XFS_ILOCK_EXCL);
	xfs_trans_ihold(tp, ip);

	/*
	 * issue the bmapi() call to allocate the blocks
	 */
	XFS_BMAP_INIT(&free_list, &firstfsb);
	imapp = &imap[0];
	error = XFS_BMAPI(mp, tp, io, offset_fsb, count_fsb,
		bmapi_flags, &firstfsb, 1, imapp, &nimaps, &free_list);
	if (error) {
		goto error0;
	}

	/*
	 * complete the transaction
	 */

	error = xfs_bmap_finish(&tp, &free_list, firstfsb, &committed);
	if (error) {
		goto error0;
	}

	error = xfs_trans_commit(tp, XFS_TRANS_RELEASE_LOG_RES, NULL);
	if (error) {
		goto error_out;
	}

finish_maps:	/* copy any maps to caller's array and return any error. */
	if (nimaps == 0) {
		error = (ENOSPC);
		goto error_out;
	}

	maps = min(nimaps, maps);
	*npbmaps = _xfs_imap_to_bmap(io, offset, &imap[0], pbmapp, maps, *npbmaps);
	if(*npbmaps) {
		/*
		 * this is new since xfs_iomap_read
		 * didn't find it.
		 */
		pbmapp->pbm_flags |= PBMF_NEW;
		if (*npbmaps != 1) {
			printk("NEED MORE WORK FOR MULTIPLE BMAPS (which are new)\n");
		}
	}
	goto out;

 error0:	/* Cancel bmap, unlock inode, and cancel trans */
	xfs_bmap_cancel(&free_list);

 error1:	/* Just cancel transaction */
	xfs_trans_cancel(tp, XFS_TRANS_RELEASE_LOG_RES | XFS_TRANS_ABORT);
 	*npbmaps = 0;	/* nothing set-up here */

error_out:
out:	/* Just return error and any tracing at end of routine */
	return error;
}
int
_xfs_incore_relse(buftarg_t *targ,
				  int	delwri_only,
				  int	wait)
{
	truncate_inode_pages(&targ->inode->i_data, 0LL);
	return 0;
} 

xfs_buf_t *
_xfs_incore_match(buftarg_t     *targ,
			 daddr_t		blkno,
			 int 			len,
			 int			field,
			 void			*value)
{
  printk("_xfs_incore_match not implemented\n");
  return NULL;
}

/*
 * All xfs metadata buffers except log state machine buffers
 * get this attached as their b_bdstrat callback function. 
 * This is so that we can catch a buffer
 * after prematurely unpinning it to forcibly shutdown the filesystem.
 */
int
xfs_bdstrat_cb(struct xfs_buf *bp)
{


  /* for now we just call the io routine... once the shutdown stuff is working 
   * the rest of this function will need to be implemented 01/10/2000 RMC */
#if !defined(_USING_PAGEBUF_T)
	bdstrat(NULL, bp);
#if 0
	xfs_mount_t	*mp;

	mp = bp->b_fsprivate3;

	ASSERT(bp->b_target);
	if (!XFS_FORCED_SHUTDOWN(mp)) {
		struct bdevsw *my_bdevsw;
		my_bdevsw =  bp->b_target->bdevsw;
		ASSERT(my_bdevsw != NULL);
		bp->b_bdstrat = NULL;
		bdstrat(my_bdevsw, bp);
		return 0;
	} else { 
		xfs_buftrace("XFS__BDSTRAT IOERROR", bp);
		/*
		 * Metadata write that didn't get logged but 
		 * written delayed anyway. These aren't associated
		 * with a transaction, and can be ignored.
		 */
		if (XFS_BUF_IODONE_FUNC(bp) == NULL &&
		    (XFS_BUF_ISREAD(bp)) == 0)
			return (xfs_bioerror_relse(bp));
		else
			return (xfs_bioerror(bp));
	}
#endif
#else
	pagebuf_iorequest(bp);
#endif
	return 0;

}
/*
 * Wrapper around bdstrat so that we can stop data
 * from going to disk in case we are shutting down the filesystem.
 * Typically user data goes thru this path; one of the exceptions
 * is the superblock.
 */
int
xfsbdstrat(
	struct xfs_mount 	*mp,
	struct xfs_buf		*bp)
{
#if !defined(_USING_PAGEBUF_T)
  	int		dev_major = MAJOR(bp->b_edev);

	ASSERT(bp->b_target);
#endif

	ASSERT(mp);
	if (!XFS_FORCED_SHUTDOWN(mp)) {
		/*
		 * We want priority I/Os to non-XLV disks to go thru'
		 * griostrategy(). The rest of the I/Os follow the normal
		 * path, and are uncontrolled. If we want to rectify
		 * that, use griostrategy2.
		 */
#if !defined(_USING_PAGEBUF_T)
		if (XFS_BUF_IS_GRIO(bp)) {
			extern void griostrategy(xfs_buf_t *);
			griostrategy(bp);
		} else
			{
			struct bdevsw	*my_bdevsw;

			my_bdevsw = bp->b_target->bdevsw;
			bdstrat(my_bdevsw, bp);
		}
#else
		if (XFS_BUF_IS_GRIO(bp)) {
			printk("xfsbdstrat needs griostrategy\n");
		} else {
			pagebuf_iorequest(bp);
		}
#endif

		return 0;
	}

	xfs_buftrace("XFSBDSTRAT IOERROR", bp);
	return (xfs_bioerror_relse(bp));
}


#ifdef _USING_PAGEBUF_T
page_buf_t *
xfs_pb_getr(int sleep, xfs_mount_t *mp){
	return pagebuf_get_empty(sleep,mp->m_ddev_targ.inode);
}

page_buf_t *
xfs_pb_ngetr(int len, xfs_mount_t *mp){
	page_buf_t *bp;
	bp = pagebuf_get_no_daddr(len,mp->m_ddev_targ.inode);
	return bp;
}

void
xfs_pb_freer(page_buf_t *bp) {
	pagebuf_free(bp);
}

void
xfs_pb_nfreer(page_buf_t *bp){
	pagebuf_free(bp);
}

void
XFS_bflush(buftarg_t target)
{
	pagebuf_delwri_flush(target.inode);
	run_task_queue(&tq_disk);
}

dev_t
XFS_pb_target(page_buf_t *bp) {
	dev_t	dev;

	return bp->pb_target->i_dev;
}

void
xfs_trigger_io(void)
{
	run_task_queue(&tq_disk);
}

#endif /* _USING_PAGEBUF_T */

int
xfs_is_read_only(xlog_t *log){
  
  xfs_mount_t *mp;
  
  cmn_err(CE_NOTE, "XFS: WARNING: recovery required on readonly filesystem.\n");
  mp = log->l_mp;
  if (is_read_only(mp->m_dev) || is_read_only(mp->m_logdev)) {
	cmn_err(CE_NOTE, "XFS: write access unavailable, cannot proceed.\n");
	return EROFS;
  }
  cmn_err(CE_NOTE, "XFS: write access will be enabled during recovery.\n");
  XFS_MTOVFS(mp)->vfs_flag &= ~VFS_RDONLY;
  return 0;
}

