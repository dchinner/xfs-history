
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
#ident	"$Revision$"

#include <linux/xfs_linux.h>
#include <linux/xfs_cred.h>

#include <sys/types.h>
#undef sysinfo
#include <linux/kernel.h> /* for printk... remove later if needed */
#include <linux/page_buf.h>
#include <ksys/as.h>
#include <sys/cmn_err.h>
#include <sys/debug.h>
#include <sys/fcntl.h>
#include <ksys/vfile.h>
#include <sys/flock.h>
#include <sys/fs_subr.h>
#include <sys/kabi.h>
#include <sys/param.h>
#include <sys/poll.h>
#include <sys/statvfs.h>
#include <sys/sysmacros.h>
#include <sys/systm.h>
#include <sys/vfs.h>
#include <sys/pvfs.h>
#include <sys/vnode.h>

/*
 * Implementation for VFS_DOUNMOUNT.
 */
int
fs_dounmount(
	bhv_desc_t 	*bdp, 
        int 		flags, 
        vnode_t 	*rootvp, 
        cred_t 		*cr)
{
	struct vfs 	*vfsp = bhvtovfs(bdp);
        bhv_desc_t      *fbdp = vfsp->vfs_fbhv;
	int 		error;
	extern void 	nfs_purge_vfsp(struct vfs*);

	/*
         * Wait for sync to finish and lock vfsp.  This also sets the
         * VFS_OFFLINE flag.  Once we do this we can give up reference
         * the root vnode which we hold to avoid the another unmount
         * ripping the vfs out from under us before we get to lock it.
         * The VFS_DOUNMOUNT calling convention is that the reference
         * on the rot vnode is released whether the call succeeds or 
         * fails.
	 */
	error = vfs_lock_offline(vfsp);	
	if (rootvp)
		VN_RELE(rootvp);
	if (error)
		return error;

	/*
	 * Now invoke SYNC and UNMOUNT ops, using the PVFS versions is
	 * OK since we already have a behavior lock as a result of
	 * being in VFS_DOUNMOUNT.  It is necessary to do things this
	 * way since using the VFS versions would cause us to get the
	 * behavior lock twice which can cause deadlock as well as
	 * making the coding of vfs relocation unnecessarilty difficult
	 * by making relocations invoked by unmount occur in a different
	 * environment than those invoked by mount-update.
	 */
	PVFS_SYNC(fbdp, SYNC_ATTR|SYNC_DELWRI|SYNC_NOWAIT, cr, error);
	if (error == 0)
		PVFS_UNMOUNT(fbdp, flags, cr, error);

	if (error) {
		vfs_unlock(vfsp);	/* clears VFS_OFFLINE flag, too */
	}
	return error;
}

/*
 * Stub for no-op vnode operations that return error status.
 */
int
fs_noerr()
{
	return 0;
}

/*
 * Operation unsupported under this file system.
 */
int
fs_nosys()
{
	return ENOSYS;
}

/*
 * Stub for inactive, strategy, and read/write lock/unlock.  Does nothing.
 */
/* ARGSUSED */
void
fs_noval()
{
}

/*
 * fs_frlock2 is identical to fs_frlock, except it has an extra
 * argument for use by CXFS.  Also, it calls reclock2 instead of reclock.
 */
/* ARGSUSED */
int
fs_frlock2(
	register bhv_desc_t *bdp,
	int cmd,
	struct flock *bfp,
	int flag,
	off_t offset,
	vrwlock_t vrwlock,
	cred_t *cr,
	int ioflag,
	int want_vn_chg,
	int *need_vn_chg)
{
	printk("XFS: fs_frlock2() NOT IMPLEMENTED\n");
	return 0;
}

/*
 * Change state of vnode itself.
 * 
 * This routine may or may not require that the caller(s) prohibit 
 * simultaneous changes to a given piece of state.  This depends
 * on the particular 'cmd' - and individual commands should assert
 * appropriately if they so desire.
 */
void
fs_vnode_change(
        bhv_desc_t	*bdp, 
        vchange_t 	cmd, 
        __psint_t 	val)
{
	printk("XFS: fs_vnode_change() NOT IMPLEMENTED\n");
}


/*
 * vnode pcache layer for vnode_tosspages.
 */
void
fs_tosspages(
        bhv_desc_t	*bdp,
	off_t		first,
	off_t		last,
	int		fiopt)
{
	pagebuf_inval(BHV_TO_VNODE(bdp)->v_inode, first, last - first, 0);
}


/*
 * vnode pcache layer for vnode_flushinval_pages.
 */
void
fs_flushinval_pages(
        bhv_desc_t	*bdp,
	off_t		first,
	off_t		last,
	int		fiopt)
{
	pagebuf_flushinval(BHV_TO_VNODE(bdp)->v_inode, first, last - first, 0);
}



/*
 * vnode pcache layer for vnode_flush_pages.
 */
int
fs_flush_pages(
        bhv_desc_t	*bdp,
	off_t		first,
	off_t		last,
	uint64_t	flags,
	int		fiopt)
{
	pagebuf_flush(BHV_TO_VNODE(bdp)->v_inode, first, last - first, 0);
	return 0;
}


/*
 * vnode pcache layer for vnode_pages_sethole.
 */
void
fs_pages_sethole(
        bhv_desc_t	*bdp,
	void		*pfd,
	int		cnt,
	int		doremap,
	off_t		remap_offset)
{
	printk("XFS: fs_pages_sethole() NOT IMPLEMENTED\n");
}
