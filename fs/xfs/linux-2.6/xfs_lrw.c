
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

ssize_t
xfs_rdwr(
	bhv_desc_t      *bdp,
	struct file	*filp,
	char 		*buf,
	size_t		size,
	loff_t		*offsetp,
	int		read)	/* set if read, otherwise this is write */
{
	ssize_t ret;
	struct xfs_inode *xip;

	printk("ENTER xfs_rdwr %x %d %d\n", (unsigned int)filp, size, read);

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
	printk("EXIT xfs_rdwr %d %d %X\n", read, ret, *offsetp);
	return(ret);
}

ssize_t
xfs_read(
	bhv_desc_t      *bdp,
	struct file	*filp,
	char 		*buf,
	size_t		size,
	loff_t		*offsetp)
{
	return(xfs_rdwr(bdp, filp, buf, size, offsetp, 1));
}

ssize_t
xfs_write(
	bhv_desc_t      *bdp,
	struct file	*filp,
	char		*buf,
	size_t		size,
	loff_t		*offsetp)
{
	return(xfs_rdwr(bdp, filp, buf, size, offsetp, 0));
}
