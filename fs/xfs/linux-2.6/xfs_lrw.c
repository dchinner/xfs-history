
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
#include "xfs_lrw.h"
#include "xfs_quota.h"

#define min(a, b) ((a) < (b) ? (a) :  (b))
#define XFS_WRITEIO_ALIGN(io,off)       (((off) >> io->io_writeio_log) \
                                                << io->io_writeio_log)

extern int xfs_write_clear_setuid(struct xfs_inode *);

#if !defined(_USING_PAGEBUF_T)
extern void bdstrat(struct bdevsw *, buf_t *);
extern int xfs_bioerror_relse(xfs_buf_t *);
#endif

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
	} else {
		/* last zero eof */
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
	xfs_rwlockf(bdp, VRWLOCK_READ, 0);
	ret = xfs_rdwr(bdp, filp, buf, size, offsetp, 1);
	xfs_rwunlockf(bdp, VRWLOCK_READ, 0);
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
	if ((mp->m_sb.sb_blocksize < NBPP) && ((i = poff(isize)) != 0)) {
		struct page *page;
		struct page ** hash;

		hash = page_hash(&ip->i_data, isize & PAGE_CACHE_MASK);
		page = __find_lock_page(&ip->i_data, isize & PAGE_CACHE_MASK, hash);
		if (page) {
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

int					/* error */
xfs_zero_eof(
	struct inode	*ip,
	xfs_iocore_t	*io,
	off_t		offset,
	xfs_fsize_t	isize,
	struct pm       *pmp)
{
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

	printk("zero: last block %Ld end %Ld\n", last_fsb, end_zero_fsb);

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
		printk("zero: start block %Ld end %Ld\n", start_zero_fsb, end_zero_fsb);
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
			     	printk("xfs_zero_eof: look for pages to zero? HOLE\n");
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
		printk("zero: buf len is %d block\n", buf_len_fsb);
		/*
		 * Drop the inode lock while we're doing the I/O.
		 * We'll still have the iolock to protect us.
		 */
		XFS_IUNLOCK(mp, io, XFS_ILOCK_EXCL|XFS_EXTSIZE_RD);

		loff = XFS_FSB_TO_B(mp, last_fsb);
		lsize = XFS_FSB_TO_B(mp, buf_len_fsb);
		/*
		 * JIMJIM what about the real-time device
		 */
		printk("xfs_zero_eof: NEW CODE doing %d starting at %Ld\n",
			lsize, loff);

		pb = pagebuf_get(ip, loff, lsize, 0);
		if (!pb) {
			error = -ENOMEM;
			goto out_lock;
		}

		if (imap.br_startblock == DELAYSTARTBLOCK) {
			printk("xfs_zero_eof: hmmm what do we do here?\n");
			error = -ENOSYS;
			goto out_lock;
		} else {
			pb->pb_bn = XFS_FSB_TO_DB_IO(io, imap.br_startblock);
			if (imap.br_state == XFS_EXT_UNWRITTEN) {
				printk("xfs_zero_eof: unwritten? what do we do here?\n");
			}
		}
		if (io->io_flags & XFS_IOCORE_RT) {
			printk("xfs_zero_eof: real time device? use diff inode\n");
		}

		if (error = pagebuf_iozero(pb, 0, lsize)) {
			goto out_lock;
		}
		if (error = pagebuf_iostart(pb, PBF_WRITE)) {
			goto out_lock;
		}
		if (imap.br_startblock == DELAYSTARTBLOCK ||
		    imap.br_state == XFS_EXT_UNWRITTEN) { /* DELWRI */
			printk("xfs_zero_eof: need to allocate? delwri\n");
		}
		if (error) {
			goto out_lock;
		}

		prev_zero_fsb = start_zero_fsb;
		prev_zero_count = buf_len_fsb;
		start_zero_fsb = imap.br_startoff + buf_len_fsb;
		printk("moved start to %Ld\n", start_zero_fsb);
		ASSERT(start_zero_fsb <= (end_zero_fsb + 1));

		XFS_ILOCK(mp, io, XFS_ILOCK_EXCL|XFS_EXTSIZE_RD);
	}

	printk("zero: all done\n");

	return 0;

out_lock:

	XFS_ILOCK(mp, io, XFS_ILOCK_EXCL|XFS_EXTSIZE_RD);
	return error;
}

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
		ret = xfs_zero_eof(ip, io, *offsetp, isize, NULL);
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

	ip = XFS_BHVTOI(bdp);
	ASSERT((ip->i_d.di_mode & IFMT) == IFREG);
	ASSERT(((ip->i_d.di_flags & XFS_DIFLAG_REALTIME) != 0) ==
	       ((ip->i_iocore.io_flags & XFS_IOCORE_RT) != 0));
	ASSERT((flags == XFS_B_READ) || (flags == XFS_B_WRITE));

	if (XFS_FORCED_SHUTDOWN(ip->i_iocore.io_mount))
		return (EIO);

	if (flags == XFS_B_READ) {
/**
		ASSERT(ismrlocked(&ip->i_iolock, MR_ACCESS | MR_UPDATE) != 0);
**/
		unlocked = 0;
		lockmode = xfs_ilock_map_shared(ip);
		error = xfs_iomap_read(&ip->i_iocore, offset, count,
				 pbmapp, npbmaps, NULL);
		xfs_iunlock_map_shared(ip, lockmode);
	} else { /* XFS_B_WRITE */
/**
		ASSERT(ismrlocked(&ip->i_iolock, MR_UPDATE) != 0);
**/
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
		error = xfs_iomap_write(&ip->i_iocore, offset, count, 
					pbmapp, npbmaps, flags, NULL);
		/* xfs_iomap_write unlocks/locks/unlocks */
	}
/*	printk("EXIT xfs_bmap %d\n",error); */
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

		if (XFS_FSB_TO_B(mp, pbmapp->pbm_offset + pbmapp->pbm_bsize)
								>= nisize) {
			pbmapp->pbm_flags |= PBMF_EOF;
		}
		
		start_block = imap->br_startblock;
		if (start_block == HOLESTARTBLOCK) {
			pbmapp->pbm_bn = -1;
			pbmapp->pbm_flags = PBMF_HOLE;
		} else if (start_block == DELAYSTARTBLOCK) {
			pbmapp->pbm_bn = -1;
			pbmapp->pbm_flags = PBMF_DELAY;
		} else {
			pbmapp->pbm_bn = XFS_FSB_TO_DB_IO(io, start_block);
			pbmapp->pbm_flags = 0;
			if (imap->br_state == XFS_EXT_UNWRITTEN)
				pbmapp->pbm_flags |= PBMF_UNWRITTEN;
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
	int		delay, convert, aeof, direct_io;
	int		bmapi_flags;
	xfs_filblks_t	datablocks;
	int		rt; 
	int		committed;
	int		numrtextents;
	uint		resblks;
	int		rtextsize;

	/*
	 * If we aren't converting delalloc, we are
	 * looking to allocate (delay or otherwise).
	 * In this case, if the file has extents,
	 * search and see if we find any already existing for this offset.
	 * If we find 'em, return 'em up to the number
	 * of maps we were passed.
	 */

	maps = *npbmaps;
	if (!maps)
		goto out;

	convert = ioflag & PBF_FILE_ALLOCATE;
	if (!convert && ip->i_d.di_nextents) {
		error = xfs_iomap_read(io, offset, count,
					pbmapp, npbmaps, NULL);

		/*
		 * If we found mappings and they can just have data written
		 * without conversion,
		 * let the caller write these and call us again.
		 *
		 * If we have a HOLE or UNWRITTEN, proceed down lower to
		 * get the space or to convert to written.
		 */

		if (*npbmaps && !(pbmapp->pbm_flags & (PBMF_DELAY|PBMF_HOLE|PBMF_UNWRITTEN))) {
			goto out;
		}
		*npbmaps = maps; /* Restore to original */
	}

	delay = !(ioflag & PBF_DIRECT);
	direct_io = ioflag & PBF_DIRECT;

	maps = min(XFS_WRITE_IMAPS, *npbmaps);
	nimaps = maps;

	mp = io->io_mount;
	isize = XFS_SIZE(mp, io);
	/* JIMJIM what about io_new_size, used on IRIX but not on Linux? */

	if ((offset + count) > isize) {
		aeof = 1;
	} else {
		aeof = 0;
	}

	offset_fsb = XFS_B_TO_FSBT(mp, offset);
	last_fsb = XFS_B_TO_FSB(mp, ((xfs_ufsize_t)(offset + count)));
	count_fsb = last_fsb - offset_fsb;

	/*
	 * roundup the allocation request to m_dalign boundary if file size
	 * is greater that 512K and we are allocating past the allocation eof
	 */
	if (mp->m_dalign && (isize >= 524288) && aeof) {
		int eof;
		xfs_fileoff_t new_last_fsb;
		new_last_fsb = roundup(last_fsb, mp->m_dalign);
		printk("xfs_iomap_write: about to XFS_BMAP_EOF %Ld\n",
			new_last_fsb);
		error = XFS_BMAP_EOF(mp, io, new_last_fsb, XFS_DATA_FORK, &eof);
		if (error) {
			goto error_out;
		}
		if (eof)
			last_fsb = new_last_fsb;
	}

	/* XFS_BMAPI_DELAY add delay later */
	bmapi_flags = XFS_BMAPI_WRITE|XFS_BMAPI_DIRECT_IO|XFS_BMAPI_ENTIRE;
	direct_io = bmapi_flags & XFS_BMAPI_DIRECT_IO;
	bmapi_flags &= ~XFS_BMAPI_DIRECT_IO;

	/*
	 * Get delayed alloc requests out of the way first.
	 */

	if (bmapi_flags & XFS_BMAPI_DELAY) {
		XFS_BMAP_INIT(&free_list, &firstfsb);
		imapp = &imap[0];
		nimaps = maps;
		error = XFS_BMAPI(mp, NULL, io, offset_fsb,
				  count_fsb, bmapi_flags, &firstfsb,
				  1, imapp, &nimaps, &free_list);
		goto finish_maps;
	}

	/*
	 * determine if this is a realtime file
	 */
        if (rt = (ip->i_d.di_flags & XFS_DIFLAG_REALTIME) != 0) {
		ASSERT(direct_io);
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
	if (direct_io) {
		tp = xfs_trans_alloc(mp, XFS_TRANS_DIOSTRAT);
		resblks = XFS_DIOSTRAT_SPACE_RES(mp, datablocks);
	} else {	/* Converting delay to real */
		printk("xfs_iomap_write: delay path!!! not impled\n");
		tp = xfs_trans_alloc(mp, XFS_TRANS_STRAT_WRITE);
		resblks = 0;
	}

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
		goto out_no_unlock;
	}


	xfs_ilock(ip, XFS_ILOCK_EXCL);

	/* JIMJIM Do we need to check and see if the file has changed? */
	if (direct_io && XFS_IS_QUOTA_ON(mp)) {
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

out:
error_out: /* Just return error and any tracing at end of routine */
	xfs_iunlock(ip, XFS_ILOCK_EXCL);

out_no_unlock:
	XFS_INODE_CLEAR_READ_AHEAD(&ip->i_iocore);
	if (error) printk("xfs_iomap_write returning ERROR %d\n", error);
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


#endif
