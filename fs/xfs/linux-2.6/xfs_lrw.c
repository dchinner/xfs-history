
/*
 * Copyright (C) 1999 Silicon Graphics, Inc.  All Rights Reserved.
 * 
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as published
 * by the Free Software Fondation.
 * 
 * This program is distributed in the hope that it would be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  Further, any license provided herein,
 * whether implied or otherwise, is limited to this program in accordance with
 * the express provisions of the GNU General Public License.  Patent licenses,
 * if any, provided herein do not apply to combinations of this program with
 * other product or programs, or any other product whatsoever.  This program is
 * distributed without any warranty that the program is delivered free of the
 * rightful claim of any third person by way of infringement or the like.  See
 * the GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write the Free Software Foundation, Inc., 59 Temple
 * Place - Suite 330, Boston MA 02111-1307, USA.
 */
/*
 *  fs/xfs/linux/xfs_lrw.c (Linux Read Write stuff)
 *
 */
#define FSID_T
#include <sys/types.h>
#include <linux/errno.h>
#include "xfs_coda_oops.h"

#include <linux/xfs_to_linux.h>

#undef  NODEV
#include <linux/version.h>
#include <linux/fs.h>
#include <linux/page_buf.h>
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

/* 	printk("ENTER xfs_rdwr %x %d %d\n", (unsigned int)filp, size, read); */

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
/* 		ret = generic_file_read(filp, buf, size, offsetp); */
	} else {
		/* last zero eof */
		ret = pagebuf_generic_file_write(filp, buf, size, offsetp);
	}
out:
/* 	printk("EXIT xfs_rdwr %d %d %X\n", read, ret, *offsetp); */
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
	return(xfs_rdwr(bdp, filp, buf, size, offsetp, 1));
}

ssize_t
xfs_write(
	bhv_desc_t	*bdp,
	struct file	*filp,
	char		*buf,
	size_t		size,
	loff_t		*offsetp)
{
	return(xfs_rdwr(bdp, filp, buf, size, offsetp, 0));
}

/*
 * xfs_bmap() is the same as the irix xfs_bmap from xfs_rw.c 
 * execpt for slight changes to the params
 */
int
xfs_bmap(bhv_desc_t	*bdp,
		off_t		offset,
		ssize_t		count,
		int		flags,
		pb_bmap_t	*pbmapp,
		int		*npbmaps)
{
	xfs_inode_t	*ip;
	int		error;
	int		unlocked;
	int		lockmode;

/*	printk("ENTER xfs_bmap\n"); */
	ip = XFS_BHVTOI(bdp);
	ASSERT((ip->i_d.di_mode & FMT) == IFREG);
	ASSERT((flags == XFS_B_READ) || (flags == XFS_B_WRITE));

	if (flags == XFS_B_READ) {
		ASSERT(ismrlocked(&ip->i_iolock, MR_ACCESS | MR_UPDATE) != 0);
		unlocked = 0;
		lockmode = xfs_ilock_map_shared(ip);
		error = xfs_iomap_read(&ip->i_iocore, offset, count,
				 pbmapp, npbmaps, NULL, &unlocked, lockmode);
		if (!unlocked)
			xfs_iunlock_map_shared(ip, lockmode);
	} else { /* XFS_B_WRITE */
		ASSERT(ismrlocked(&ip->i_iolock, MR_UPDATE) != 0);
		xfs_ilock(ip, XFS_ILOCK_EXCL);
		ASSERT(ip->i_d.di_size >= (offset + count));
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
							pbmapp, npbmaps, 0, NULL);
		xfs_iunlock(ip, XFS_ILOCK_EXCL);
		if (!error)
			XFS_INODE_CLEAR_READ_AHEAD(&ip->i_iocore);
	}
/*	printk("EXIT xfs_bmap %d\n",error); */
	return error;
}	


int
xfs_iomap_read(
	xfs_iocore_t	*io,
	off_t		offset,
	size_t		count,
	pb_bmap_t	*pbmapp,
	int		*npbmaps,
	struct pm	*pmp,
	int		*unlocked,
	unsigned int	lockmode)
{	
	xfs_fileoff_t	offset_fsb;
	xfs_fileoff_t	last_fsb;
	xfs_fileoff_t	max_fsb;
	xfs_fsblock_t	firstblock;

	xfs_fsize_t	nisize;
	int		nimaps;
	int		error;
	xfs_mount_t	*mp;
	xfs_bmbt_irec_t	imap[XFS_MAX_RW_NBMAPS];

	ASSERT(ismrlocked(io->io_lock, MR_UPDATE | MR_ACCESS) != 0);
	ASSERT(ismrlocked(io->io_iolock, MR_UPDATE | MR_ACCESS) != 0);
/*	xfs_iomap_enter_trace(XFS_IOMAP_READ_ENTER, io, offset, count); */

	mp = io->io_mount;
	nisize = io->io_new_size;
/*	printk("nisize %d XFS_SIZE(mp, io) %d\n", nisize, XFS_SIZE(mp, io)); */
	if (nisize < XFS_SIZE(mp, io)) {
		nisize = XFS_SIZE(mp, io);
	}
	offset_fsb = XFS_B_TO_FSBT(mp, offset);
	nimaps = sizeof(imap) / sizeof(imap[0]);
	last_fsb = XFS_B_TO_FSB(mp, ((xfs_ufsize_t)nisize));
/*	printk("offset %d nisize %d\n", offset, nisize); */
	if (offset >= nisize) {
		/*
		 * The VM/chunk code is trying to map a page or part
		 * of a page to be pushed out that is beyond the end
		 * of the file.  We handle these cases separately so
		 * that they do not interfere with the normal path
		 * code.
		 */
		error = xfs_iomap_extra(io, offset, count, pbmapp, npbmaps, pmp);
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
/*	printk("xfs_bmap nimaps %d\n",nimaps); */
	error = XFS_BMAPI(mp, NULL, io, offset_fsb,
				(xfs_filblks_t)(max_fsb - offset_fsb),
				XFS_BMAPI_ENTIRE, &firstblock, 0, imap,
				&nimaps, NULL);
/*	printk("xfs_bmap nimaps %d imaps %d\n",nimaps,imap); */
	if (error) {
		return error;
	}


	{ 
	int i;
	xfs_fsblock_t	start_block=0;
	xfs_fsblock_t	ext_offset=0;
/*	printk("xfs_bmap nimaps %d npbmaps %d\n",nimaps,*npbmaps); */
	for (i=0; i < (*npbmaps < nimaps?*npbmaps:nimaps); i++) {
/*		printk("xfs_bmap %lld %lld %lld %d\n",
			(imap[i]).br_startoff, 
 			(imap[i]).br_startblock,
			(imap[i]).br_blockcount,
			(imap[i]).br_state); */
		if ((imap[i]).br_startblock != -1) {
			(imap[i]).br_startblock =
				XFS_FSB_TO_DB_IO(io, (imap[i]).br_startblock);
		}
		if (XFS_FSB_TO_B(mp, pbmapp->pbm_offset + pbmapp->pbm_bsize) >= nisize) {
			pbmapp->pbm_flags |= PBMF_EOF;
		}
		(pbmapp[i]).pbm_offset = offset - XFS_FSB_TO_B(mp, (imap[i]).br_startoff);
		
		/* this may be wrong... need to convert from BB to byte ... so << 9 */
		/* as long as the m_blkbb_log is set correctly in xfs_mount_int this seems to be correct */
		(pbmapp[i]).pbm_bsize = XFS_FSB_TO_BB(mp,(imap[i]).br_blockcount) << 9;
/*		printk("SIZE IS %d should be %d\n", (pbmapp[i]).pbm_bsize); */

		start_block = (imap[i]).br_startblock;
		if (start_block == HOLESTARTBLOCK) {
			(pbmapp[i]).pbm_bn = -1;
			(pbmapp[i]).pbm_flags = PBMF_HOLE;
		} else if (start_block == DELAYSTARTBLOCK) {
			(pbmapp[i]).pbm_bn = -1;
			(pbmapp[i]).pbm_flags	= PBMF_DELAY;
		} else {
			(pbmapp[i]).pbm_bn = start_block + ext_offset;
			(pbmapp[i]).pbm_flags = 0;
		if ((imap[i]).br_state == XFS_EXT_UNWRITTEN)
			(pbmapp[i]).pbm_flags |= PBMF_UNWRITTEN;
		}
#if 0
	switch((imap[i]).br_state){
	case XFS_EXT_NORM:
/* 		printk("XFS_EXT_NORM:\n"); */
		break;
	case XFS_EXT_UNWRITTEN:
/* 		printk("XFS_EXT_UNWRITTEN:\n"); */
		break;
	case XFS_EXT_DMAPI_OFFLINE:
/* 		printk("XFS_EXT_NORM:\n"); */
		break;
	}
#endif
	}
	*npbmaps = nimaps;
	} /* local variable */
	return error;
}

STATIC int
xfs_iomap_write(
	xfs_iocore_t	*io,
	off_t		offset,
	size_t		count,
	pb_bmap_t	*pbmapp,
	int		*npbmaps,
	int		ioflag,
	struct pm	*pmp)
{
#ifdef NOTYET
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
	short		small_write;
	size_t		count_remaining;
	xfs_mount_t	*mp;
	struct bmapval	*curr_bmapp;
	struct bmapval	*next_bmapp;
	struct bmapval	*last_bmapp;
	xfs_bmbt_irec_t	*curr_imapp;
	xfs_bmbt_irec_t	*last_imapp;
#define	XFS_WRITE_IMAPS	XFS_BMAP_MAX_NMAP
	xfs_bmbt_irec_t	imap[XFS_WRITE_IMAPS];
	int		aeof;

	ASSERT(ismrlocked(io->io_lock, MR_UPDATE) != 0);

	xfs_iomap_enter_trace(XFS_IOMAP_WRITE_ENTER, io, offset, count);

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
		error = XFS_BMAP_EOF(mp, io, new_last_fsb, XFS_DATA_FORK, &eof);
		if (error) {
			return error;
		}
		if (eof)
			last_fsb = new_last_fsb;
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
		xfs_iomap_enter_trace(XFS_IOMAP_WRITE_NOSPACE,
				      io, offset, count);
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
			aligned_offset = ctooff(offtoct(offset));
			ioalign = XFS_B_TO_FSBT(mp, aligned_offset);
			last_page_offset = ctob64(btoc64(offset + count));
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
			    io, offset, count, bmapp, imap);

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
			if (small_write) {
				iosize -= curr_bmapp->length;
				ASSERT((iosize > 0) ||
				       (curr_imapp == last_imapp));
				/*
				 * We have nothing more to write, so
				 * we're done.
				 */
				if (iosize == 0) {
					break;
				}
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
					((1 << (int) io->io_writeio_log) - 1))
					==0);
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
					 * For small, sync writes we don't
					 * bother with the alignment stuff.
					 *
					 * XXXajs
					 * Adding a macro to writeio align
					 * fsblocks would be good to reduce
					 * the bit shifting here.
					 */
					if (small_write) {
						ioalign = next_offset_fsb;
					} else {
						aligned_offset =
							XFS_FSB_TO_B(mp,
							    next_offset_fsb);
						aligned_offset =
							XFS_WRITEIO_ALIGN(io,
							    aligned_offset);
						ioalign = XFS_B_TO_FSBT(mp,
							    aligned_offset);
					}
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
			xfs_iomap_map_trace(XFS_IOMAP_WRITE_MAP, io, offset,
					    count, curr_bmapp, curr_imapp);
		}
	}
	*nbmaps = filled_bmaps;
	for (x = 0; x < filled_bmaps; x++) {
		curr_bmapp = &bmapp[x];
		if (io->io_flags & XFS_IOCORE_RT) {
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
			curr_bmapp->bn = XFS_FSB_TO_DB_IO(io, curr_bmapp->bn);
		}
		curr_bmapp->pmp = pmp;
	}
#endif
	printk("xfs_iomap_write NOT!... getting to it\n");
	return 0;
}



STATIC int					/* error */
_xfs_iomap_extra( xfs_iocore_t	*io,
				 off_t		offset,
				 size_t		count,
				 pb_bmap_t	*pbmapp,
				 int		*npbmaps,
				 struct pm	*pmp)
{
	printk("xfs_iomap_extra NOT!... getting to it\n");
	return 0;
}

STATIC int					/* error */
xfs_iomap_extra(
	xfs_iocore_t	*io,
	off_t		offset,
	size_t		count,
	pb_bmap_t	*pbmapp,
	int		*npbmaps,
	struct pm	*pmp)
{
	xfs_fileoff_t	offset_fsb;
	xfs_fileoff_t	end_fsb;
	xfs_fsize_t	nisize;
	xfs_mount_t	*mp;
	int		nimaps;
	xfs_fsblock_t	firstblock;
	int		error;
	xfs_bmbt_irec_t	imap;

	mp = io->io_mount;
	nisize = io->io_new_size;
	if (nisize < XFS_SIZE(mp, io)) {
		nisize = XFS_SIZE(mp, io);
	}
	offset_fsb = XFS_B_TO_FSBT(mp, offset);

	if (poff(offset) != 0) {
		/*
		 * This is the 'remainder' of a page being mapped out.
		 * Since it is already beyond the EOF, there is no reason
		 * to bother.
		 */
		ASSERT(count < NBPP);
		*npbmaps = 1;
		pbmapp->pbm_flags = PBMF_EOF;
		pbmapp->pbm_bn = -1;
		pbmapp->pbm_offset = XFS_FSB_TO_BB(mp, offset_fsb);
		/*		pbmapp->length = 0; */
		pbmapp->pbm_bsize = 0;
#if 0
		if (io->io_flags & XFS_IOCORE_RT) {
			pbmapp->pbdev = mp->m_rtdev;
		} else {
			pbmapp->pbdev = mp->m_dev;
		}
#endif
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
		error = XFS_BMAPI(mp, NULL, io, offset_fsb,
					(xfs_filblks_t)(end_fsb - offset_fsb),
					0, &firstblock, 0, &imap,
					&nimaps, NULL);
		if (error) {
			return error;
		}
		ASSERT(nimaps == 1);
		*npbmaps = 1;
		pbmapp->pbm_flags = PBMF_EOF;
		if (imap.br_startblock == HOLESTARTBLOCK) {
			pbmapp->pbm_flags |= PBMF_HOLE;
			pbmapp->pbm_bn = -1;
		} else if (imap.br_startblock == DELAYSTARTBLOCK) {
			pbmapp->pbm_flags |= PBMF_DELAY;
			pbmapp->pbm_bn = -1;
		} else {
			pbmapp->pbm_bn = XFS_FSB_TO_DB_IO(io, imap.br_startblock);
			if (imap.br_state == XFS_EXT_UNWRITTEN)
				pbmapp->pbm_flags |= PBMF_UNWRITTEN;
		}
		pbmapp->pbm_offset = XFS_FSB_TO_BB(mp, offset_fsb);
/* 		pbmapp->pbm_length = XFS_FSB_TO_BB(mp, imap.br_blockcount); */
/* 		ASSERT(pbmapp->pbm_length > 0); */
/*		pbmapp->pbm_bsize = BBTOB(bmapp->length); */
		pbmapp->pbm_bsize = BBTOB(XFS_FSB_TO_BB(mp, imap.br_blockcount));
		pbmapp->pbm_offset = offset - BBTOOFF(pbmapp->pbm_offset);
		ASSERT(pbmapp->pbm_offset >= 0);
#if 0
 		pbmapp->pbsize = pbmapp->pbm_bsize - pbmapp->pbm_offset; 
 		ASSERT(bmapp->pbsize > 0); 
		bmapp->pmp = pmp;
		if (bmapp->pbsize > count) {
			bmapp->pbsize = count;
		}
		if (io->io_flags & XFS_IOCORE_RT) {
			bmapp->pbdev = mp->m_rtdev;
		} else {
			bmapp->pbdev = mp->m_dev;
		}
#endif
	}
	return 0;
}
