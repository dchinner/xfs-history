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
#ident "$Revision: 1.17 $"

#include <xfs_os_defs.h>

#ifdef SIM
#define _KERNEL 1
#endif
#include <sys/param.h>
#include "xfs_buf.h"
#include <sys/uio.h>
#include <sys/vfs.h>
#include <sys/vnode.h>
#include <sys/sysmacros.h>
#include <sys/uuid.h>
#include <sys/dmi_kern.h>
#ifdef SIM
#undef _KERNEL
#endif
#include <sys/cmn_err.h>
#include <sys/debug.h>
#ifdef SIM
#include <stdio.h>
#else
#include <sys/systm.h>
#endif
#include <sys/kmem.h>
#include <linux/xfs_sema.h>
#include <ksys/vfile.h>
#include <sys/fs_subr.h>
#include <sys/dmi.h>
#include <sys/dmi_kern.h>
#include <sys/ktrace.h>
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

#ifdef SIM
#include "sim.h"
#endif

/* ARGSUSED */
static int
xfs_rsync_fn(
	xfs_inode_t	*ip,
	int		ioflag,
	xfs_off_t		start,
	xfs_off_t		end)
{
	xfs_mount_t	*mp = ip->i_mount;
	int		error = 0;

	if (ioflag & IO_SYNC) {
		xfs_ilock(ip, XFS_ILOCK_SHARED);
		xfs_iflock(ip);
		error = xfs_iflush(ip, XFS_IFLUSH_SYNC);
		xfs_iunlock(ip, XFS_ILOCK_SHARED);
		return error;
	} else {
		if (ioflag & IO_DSYNC) {
			xfs_log_force(mp, (xfs_lsn_t)0,
					XFS_LOG_FORCE | XFS_LOG_SYNC );
		}
	}

	return error;
}


static xfs_fsize_t
xfs_size_fn(
	xfs_inode_t	*ip)
{
	return (ip->i_d.di_size);
}

static xfs_fsize_t
xfs_setsize_fn(
	xfs_inode_t	*ip,
	xfs_fsize_t	newsize)
{
	xfs_fsize_t	isize;

	xfs_ilock(ip, XFS_ILOCK_EXCL);
	if (newsize  > ip->i_d.di_size) {
		ip->i_d.di_size = newsize;
		ip->i_update_core = 1;
		ip->i_update_size = 1;
		isize = newsize;
	} else {
		isize = ip->i_d.di_size;
	}
	xfs_iunlock(ip, XFS_ILOCK_EXCL);

	return isize;
}


xfs_ioops_t	xfs_iocore_xfs = {
#ifndef SIM
    (xfs_dio_write_t)fs_nosys,   /*(xfs_dio_write_t) xfs_dio_write, */
    (xfs_dio_read_t)fs_nosys,    /*(xfs_dio_read_t) xfs_dio_read, */
	(xfs_strat_write_t)fs_nosys, /*(xfs_strat_write_t) xfs_strat_write, */
#endif
	(xfs_bmapi_t) xfs_bmapi,
	(xfs_bmap_eof_t) xfs_bmap_eof,
	(xfs_rsync_t) xfs_rsync_fn,
	(xfs_lck_map_shared_t) xfs_ilock_map_shared,
	(xfs_lock_t) xfs_ilock,
#ifndef SIM
	(xfs_lock_demote_t) xfs_ilock_demote,
#endif
	(xfs_lock_nowait_t) xfs_ilock_nowait,
	(xfs_unlk_t) xfs_iunlock,
	(xfs_chgtime_t) xfs_ichgtime,
	(xfs_size_t) xfs_size_fn,
	(xfs_setsize_t) xfs_setsize_fn,
	(xfs_lastbyte_t) xfs_file_last_byte,
};

void
xfs_iocore_inode_reinit(
	xfs_inode_t	*ip)
{
	xfs_iocore_t	*io = &ip->i_iocore;

	io->io_flags = XFS_IOCORE_ISXFS;
	if (ip->i_d.di_flags & XFS_DIFLAG_REALTIME) {
		io->io_flags |= XFS_IOCORE_RT;
	}

	io->io_dmevmask = ip->i_d.di_dmevmask;
	io->io_dmstate = ip->i_d.di_dmstate;
}

void
xfs_iocore_inode_init(
	xfs_inode_t	*ip)
{
	xfs_iocore_t	*io = &ip->i_iocore;
	xfs_mount_t	*mp = ip->i_mount;

	io->io_mount = mp;
	io->io_lock = &ip->i_lock;
	io->io_iolock = &ip->i_iolock;
	mutex_init(&io->io_rlock, MUTEX_DEFAULT, "xfs_rlock");

	xfs_iocore_reset(io);

	io->io_obj = (void *)ip;

	xfs_iocore_inode_reinit(ip);
}

void
xfs_iocore_reset(
	xfs_iocore_t	*io)
{
	xfs_mount_t	*mp = io->io_mount;

	/*
	 * initialize read/write io sizes
	 */
	ASSERT(mp->m_readio_log <= 0xff);
	ASSERT(mp->m_writeio_log <= 0xff);

	io->io_readio_log = (uchar_t) mp->m_readio_log;
	io->io_writeio_log = (uchar_t) mp->m_writeio_log;
	io->io_max_io_log = (uchar_t) mp->m_writeio_log;
	io->io_readio_blocks = mp->m_readio_blocks;
	io->io_writeio_blocks = mp->m_writeio_blocks;
}

void
xfs_iocore_destroy(
	xfs_iocore_t	*io)
{
	mutex_destroy(&io->io_rlock);
}

