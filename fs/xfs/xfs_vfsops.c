/*
 * XFS filesystem operations.
 *
 * Copyright 1994, Silicon Graphics, Inc.
 * All Rights Reserved.
 *
 * This is UNPUBLISHED PROPRIETARY SOURCE CODE of Silicon Graphics, Inc.;
 * the contents of this file may not be disclosed to third parties, copied or
 * duplicated in any form, in whole or in part, without the prior written
 * permission of Silicon Graphics, Inc.
 *
 * RESTRICTED RIGHTS LEGEND:
 * Use, duplication or disclosure by the Government is subject to restrictions
 * as set forth in subdivision (c)(1)(ii) of the Rights in Technical Data
 * and Computer Software clause at DFARS 252.227-7013, and/or in similar or
 * successor clauses in the FAR, DOD or NASA FAR Supplement. Unpublished -
 * rights reserved under the Copyright Laws of the United States.
 */
#ident  "$Revision: 1.31 $"

#include <strings.h>
#include <sys/types.h>
#ifdef SIM
#define _KERNEL
#endif
#include <sys/buf.h>
#include <sys/vfs.h>
#ifdef SIM
#undef _KERNEL
#endif
#include <sys/cmn_err.h>
#include <sys/debug.h>
#include <sys/errno.h>
#include <sys/fs_subr.h>
#include <sys/kmem.h>
#include <sys/sysmacros.h>
#include <sys/file.h>
#ifdef SIM
#include <bstring.h>
#include <stdio.h>
#else
#include <sys/systm.h>
#include <sys/conf.h>
#endif
#include <sys/major.h>
#include <sys/mount.h>
#include <sys/param.h>
#include <sys/pathname.h>
#ifdef SIM
#define _KERNEL
#endif
#include <sys/pfdat.h>
#ifdef SIM
#undef _KERNEL
#endif
#include <sys/proc.h>
#include <sys/sema.h>
#include <sys/statvfs.h>
#include <sys/uio.h>
#ifdef SIM
#define _KERNEL
#endif
#include <sys/user.h>
#include <sys/vnode.h>
#include <sys/uuid.h>
#include <sys/grio.h>
#ifdef SIM
#undef _KERNEL
#endif
#include <sys/uuid.h>
#include <sys/dirent.h>
#include <sys/ktrace.h>
#ifndef SIM
#include <sys/xlv_base.h>
#include <sys/xlv_tab.h>
#endif

#include "xfs_clnt.h"
#include "xfs_types.h"
#include "xfs_inum.h"
#include "xfs_log.h"
#include "xfs_trans.h"
#include "xfs_sb.h"
#include "xfs_mount.h"
#include "xfs_bmap_btree.h"
#include "xfs_ialloc_btree.h"
#include "xfs_alloc_btree.h"
#include "xfs_btree.h"
#include "xfs_alloc.h"
#include "xfs_ialloc.h"
#include "xfs_alloc.h"
#include "xfs_dinode.h"
#include "xfs_inode_item.h"
#include "xfs_inode.h"
#include "xfs_ag.h"
#include "xfs_error.h"
#include "xfs_bmap.h"
#include "xfs_dir.h"
#include "xfs_dir_btree.h"
#include "xfs_rw.h"

#ifdef SIM
#include "sim.h"
#endif

#define	whymount_t	whymountroot_t
#define	NONROOT_MOUNT	ROOT_UNMOUNT

static char *whymount[] = { "initial mount", "remount", "unmount" };

/*
 * Static function prototypes.
 */
STATIC int	xfs_vfsmount(vfs_t		*vfsp,
			     vnode_t		*mvp,
			     struct mounta	*uap,
			     cred_t		*credp);

STATIC int	xfs_mountroot(vfs_t		*vfsp,
			      enum whymountroot	why);

STATIC int	xfs_unmount(vfs_t	*vfsp,
			    int		flags,
			    cred_t	*credp);

STATIC int	xfs_root(vfs_t		*vfsp,
			 vnode_t	**vpp);

STATIC int	xfs_statvfs(vfs_t	*vfsp,
			    statvfs_t	*statp,
			    vnode_t	*vp);

STATIC int	xfs_sync(vfs_t		*vp,
			 short		flags,
			 cred_t		*credp);

STATIC int	xfs_vget(vfs_t		*vfsp,
			 vnode_t	**vpp,
			 fid_t		*fidp);


STATIC int	xfs_cmountfs(struct vfs	*vfsp,
			     dev_t		ddev,
			     dev_t		logdev,
			     dev_t		rtdev,
			     whymount_t		why,
			     struct xfs_args	*ap,
			     struct cred	*cr);

STATIC xfs_mount_t *_xfs_get_vfsmount(struct vfs	*vfsp,
				      dev_t		ddev,
				      dev_t		logdev,
				      dev_t		rtdev);

STATIC int	_spectodev(char *spec, dev_t *devp);
STATIC int	_xfs_isdev(dev_t dev);
STATIC int	_xfs_ibusy(xfs_mount_t *mp);




/*
 * xfs_type is the number given to xfs to indicate
 * its type among vfs's.  It is initialized in xfs_init().
 */
int	xfs_type;

/*
 * xfs_init
 *
 * This is called through the vfs switch at system initialization
 * to initialize any global state associated with XFS.  All we
 * need to do currently is save the type given to XFS in xfs_type.
 *
 * vswp -- pointer to the XFS entry in the vfs switch table
 * fstype -- index of XFS in the vfs switch table used as the XFS fs type.
 */
/* ARGSUSED */
int
xfs_init(vfssw_t	*vswp,
	 int		fstype)
{
	extern lock_t	xfs_strat_lock;
	extern sema_t	xfs_ancestormon;
	extern lock_t	xfsd_lock;
	extern sema_t	xfsd_wait;
	extern zone_t	*xfs_dir_state_zone;
	extern zone_t	*xfs_bmap_free_item_zone;
	extern zone_t	*xfs_btree_cur_zone;
	extern zone_t	*xfs_inode_zone;
	extern zone_t	*xfs_trans_zone;
	extern zone_t	*xfs_irec_zone;
	extern zone_t	*xfs_bmap_zone;
	extern zone_t	*xfs_strat_write_zone;
	extern ktrace_t	*xfs_alloc_trace_buf;
	extern ktrace_t	*xfs_bmap_trace_buf;
	extern ktrace_t	*xfs_bmbt_trace_buf;

	xfs_type = fstype;

	initnlock(&xfs_strat_lock, "xfsstrat");
	initnsema(&xfs_ancestormon, 1, "xfs_ancestor");
	initnlock(&xfsd_lock, "xfsd");
	initnsema(&xfsd_wait, 0, "xfsd");
	
	/*
	 * Initialize all of the zone allocators we use.
	 */
	xfs_bmap_free_item_zone = kmem_zone_init(sizeof(xfs_bmap_free_item_t),
						 "xfs_bmap_free_item");
	xfs_btree_cur_zone = kmem_zone_init(sizeof(xfs_btree_cur_t),
					    "xfs_btree_cur");
	xfs_inode_zone = kmem_zone_init(sizeof(xfs_inode_t), "xfs_inode");
	xfs_trans_zone = kmem_zone_init(sizeof(xfs_trans_t), "xfs_trans");
	xfs_irec_zone = kmem_zone_init((XFS_BMAP_MAX_NMAP *
					sizeof(xfs_bmbt_irec_t)), "xfs_irec");
	xfs_bmap_zone = kmem_zone_init((XFS_ZONE_NBMAPS *
					sizeof(struct bmapval)), "xfs_bmap");
	xfs_strat_write_zone = kmem_zone_init(sizeof(xfs_strat_write_locals_t),
					      "xfs_strat_write");
	xfs_alloc_arg_zone =
		kmem_zone_init(sizeof(xfs_alloc_arg_t), "xfs_alloc_arg");
	xfs_dir_state_zone =
		kmem_zone_init(sizeof(struct xfs_dir_state), "xfs_dir_state");

	/*
	 * Allocate global trace buffers.
	 */
#if defined(DEBUG) && !defined(SIM)
	xfs_alloc_trace_buf = ktrace_alloc(XFS_ALLOC_TRACE_SIZE);
	xfs_bmap_trace_buf = ktrace_alloc(XFS_BMAP_TRACE_SIZE);
	xfs_bmbt_trace_buf = ktrace_alloc(XFS_BMBT_TRACE_SIZE);
#endif
	
	/*
	 * The inode hash table is created on a per mounted
	 * file system bases.
	 */
	return 0;
}


/*
 * Resolve path name of special file to its device.
 */
STATIC int
_spectodev(char	 *spec,
	   dev_t *devp)
{
	struct vnode *bvp;
	int error;

	if (error = lookupname(spec, UIO_USERSPACE, FOLLOW, NULLVPP, &bvp))
		return error;
	if (bvp->v_type != VBLK) {
		VN_RELE(bvp);
		return XFS_ERROR(ENOTBLK);
	}
	*devp = bvp->v_rdev;
	VN_RELE(bvp);
	return 0;
}


/*
 * xfs_cmountfs
 *
 * This function is the common mount file system function.
 */
STATIC int
xfs_cmountfs(struct vfs 	*vfsp,
	     dev_t		ddev,
	     dev_t		logdev,
	     dev_t		rtdev,
	     whymount_t		why,
	     struct xfs_args	*ap,
	     struct cred	*cr)
{
	xfs_mount_t	*mp;
	struct vnode 	*ddevvp, *rdevvp, *ldevvp;
	int		error = 0;
	int		s, vfs_flags;
	xfs_sb_t	*sbp;
#ifndef SIM
	struct vnode 	*makespecvp(dev_t, vtype_t);
#endif

	/*
	 * Remounting a XFS file system is bad. The log manager
	 * automatically handles recovery so no action is required.
	 */
	if (vfsp->vfs_flag & VFS_REMOUNT)
		return 0;
	
	mp = _xfs_get_vfsmount(vfsp, ddev, logdev, rtdev);

#ifndef SIM
	/*
 	 * Open the data and real time devices now.
	 */
	vfs_flags = (vfsp->vfs_flag & VFS_RDONLY) ? FREAD : FREAD|FWRITE;
	if (ddev != 0) {
		ddevvp = makespecvp( ddev, VBLK );
		error = VOP_OPEN(&ddevvp, vfs_flags, cr);
		if (error) {
			VN_RELE(ddevvp);
			return(error);
		}
		mp->m_ddevp = ddevvp;
	} else
		ddevvp = NULL;
	if (rtdev != 0) {
		rdevvp = makespecvp( rtdev, VBLK );
		error = VOP_OPEN(&rdevvp, vfs_flags, cr);
		if (error) {
			VN_RELE(rdevvp);
			if (ddevvp) {
				VOP_CLOSE(ddevvp, vfs_flags, 1, 0, cr);
				binval(ddev);
				VN_RELE(ddevvp);
			}
			return(error);
		}
		mp->m_rtdevp = rdevvp;
	} else
		rdevvp = NULL;
	if (logdev != 0) {
		if (logdev == ddev) {
			ldevvp = NULL;
			mp->m_logdevp = ddevvp;
		} else {
			ldevvp = makespecvp( logdev, VBLK );
			error = VOP_OPEN(&ldevvp, vfs_flags, cr);
			if (error) {
				VN_RELE(ldevvp);
				if (rdevvp) {
					VOP_CLOSE(rdevvp, vfs_flags, 1, 0, cr);
					binval(rtdev);
					VN_RELE(rdevvp);
				}
				if (ddevvp) {
					VOP_CLOSE(ddevvp, vfs_flags, 1, 0, cr);
					binval(ddev);
					VN_RELE(ddevvp);
				}
				return(error);
			}
			mp->m_logdevp = ldevvp;
		}
	} else
		ldevvp = NULL;
#else
	mp->m_rtdevp = NULL;
	mp->m_logdevp = NULL;
	mp->m_ddevp  = NULL;
#endif

	if (error = xfs_mountfs(vfsp, ddev)) {
		/*
		 * Clean up. Shouldn't need to worry about
		 * locking stuff.
		 */
		kmem_free(mp, sizeof(*mp));
		if (ldevvp) {
			VOP_CLOSE(ldevvp, vfs_flags, 1, 0, cr);
			binval(logdev);
			VN_RELE(ldevvp);
		}
		if (rdevvp) {
			VOP_CLOSE(rdevvp, vfs_flags, 1, 0, cr);
			binval(rtdev);
			VN_RELE(rdevvp);
		}
		if (ddevvp) {
			VOP_CLOSE(ddevvp, vfs_flags, 1, 0, cr);
			binval(ddev);
			VN_RELE(ddevvp);
		}
		return error;		/* error should be in errno.h */
	}

	/*
	 * At this point, the super block has been read into
	 * the mount structure - get the pointer to it.
	 */
	sbp = XFS_BUF_TO_SBP(mp->m_sb_bp);

	/*
	 * XXX Anything special for root mounts?
	 * if (why == ROOT_INIT) {}
	 */

	/*
	 * XXX Anything special for read-only mounts?
	 * if ((vfsp->vfs_flag & VFS_RDONLY) == 0) {}
	 */

	return error;
} /* end of xfs_cmountfs() */


/*
 * _xfs_get_vfsmount() ensures that the given vfs struct has an
 * associated mount struct. If a mount struct doesn't exist, as
 * is the case during the initial mount, a mount structure is
 * created and initialized.
 */
STATIC xfs_mount_t *
_xfs_get_vfsmount(struct vfs	*vfsp,
		  dev_t		ddev,
		  dev_t		logdev,
		  dev_t		rtdev)
{
	xfs_mount_t *mp;

	/*
	 * Check whether dev is already mounted.
	 */
	if (vfsp->vfs_flag & VFS_REMOUNT) {
		mp = XFS_VFSTOM(vfsp);
		(void) xfs_iflush_all(mp, XFS_FLUSH_ALL);
	} else {
		/*
		 * Allocate VFS private data (xfs mount structure).
		 * XXX Should the xfs_mount_t have a m_devvp for dio?
		 */
		mp = xfs_mount_init();
		mp->m_vfsp   = vfsp;
		mp->m_dev    = ddev;
		mp->m_logdev = logdev;
		mp->m_rtdev  = rtdev;
		mp->m_ddevp  = NULL;
		mp->m_logdevp = NULL;
		mp->m_rtdevp = NULL;

		vfsp->vfs_flag |= VFS_NOTRUNC|VFS_LOCAL;
		/* vfsp->vfs_bsize filled in later from the superblock. */
		vfsp->vfs_fstype = xfs_type;
		vfsp->vfs_fsid.val[0] = ddev;
		vfsp->vfs_fsid.val[1] = xfs_type;
		vfsp->vfs_data = mp;
		vfsp->vfs_dev = ddev;
		vfsp->vfs_nsubmounts = 0;
		vfsp->vfs_syncnt = 0;
		vfsp->vfs_bcount = 0;
	}

	return mp;
}


/*
 * xfs_vfsmount
 *
 * The file system configurations are:
 *	(1) device (partition) with data and internal log
 *	(2) logical volume with data and log subvolumes.
 *	(3) logical volude with data, log, and realtime subvolumes.
 *
 */ 
STATIC int
xfs_vfsmount(vfs_t		*vfsp,
	     vnode_t		*mvp,
	     struct mounta	*uap,
	     cred_t		*credp)
{
	struct xfs_args	args;			/* xfs mount arguments */
	xfs_mount_t	*mp;
	xfs_sb_t	*sbp;
	dev_t		device;			/* device: block or logical */
	dev_t		ddev;
	dev_t		logdev;
	dev_t		rtdev;
	int		error;

	if (!suser())
		return XFS_ERROR(EPERM);
	if (mvp->v_type != VDIR)
		return XFS_ERROR(ENOTDIR);
	if ((uap->flags & MS_REMOUNT) == 0
	    && (mvp->v_count != 1 || (mvp->v_flag & VROOT)))
		return XFS_ERROR(EBUSY);

	/*
	 * Copy in XFS-specific arguments.
	 */
	bzero(&args, sizeof args);
	if (copyin(uap->dataptr, &args, MIN(uap->datalen, sizeof args)))
		return XFS_ERROR(EFAULT);

	/*
	 * Resolve path name of special file being mounted.
	 */
	if (error = _spectodev(uap->spec, &device))
		return error;
#ifdef SIM
	/*
	 * Simulation does not use logical volume.
	 */
	ddev = logdev = device;
	rtdev = 0;
#else
	if (emajor(device) == XLV_MAJOR) {
		/*
		 * logical volume
		 */
		xlv_tab_subvol_t *xlv_p;
		xlv_tab_subvol_t *sv_p;

		/*
		 * XXX This XLV section should be moved to a xlv function
		 * that can be stub'ed when xlv does not exist.
		 */
		if (xlv_tab == NULL)
			return XFS_ERROR(ENXIO);

		XLV_IO_LOCK(minor(device), MR_ACCESS);
		xlv_p = &xlv_tab->subvolume[minor(device)];
		if (! XLV_SUBVOL_EXISTS(xlv_p)) {
			XLV_IO_UNLOCK(minor(device));
			return XFS_ERROR(ENXIO);
		}
		ddev   = (sv_p = xlv_p->vol_p->data_subvol) ? sv_p->dev : 0;
		logdev = (sv_p = xlv_p->vol_p->log_subvol) ? sv_p->dev : 0;
		rtdev  = (sv_p = xlv_p->vol_p->rt_subvol) ? sv_p->dev : 0;
		ASSERT(ddev && logdev);
		XLV_IO_UNLOCK(minor(device));

	} else { /* block device */
		ddev = logdev = device;
		rtdev = 0;			/* no realtime */
	}
#endif	/* SIM */

	/*
	 * Ensure that this device isn't already mounted,
	 * unless this is a REMOUNT request
	 */
        if (vfs_devsearch(ddev) == NULL) {
		ASSERT((uap->flags & MS_REMOUNT) == 0);
	} else {
		if ((uap->flags & MS_REMOUNT) == 0)
			return XFS_ERROR(EBUSY);
	}

	error = xfs_cmountfs(vfsp, ddev, logdev, rtdev, NONROOT_MOUNT,
			     &args, credp);

	return error;

} /* xfs_vfsmount() */


/*
 * Clean the given file system.
 * This function should be called when unmounting.
 */
STATIC int
xfs_cleanfs(xfs_mount_t *mp)
{
	/* XXX xfs_cleanfs() is a stub */
	return 0;
}


/*
 * This function determines whether or not the given device has a
 * XFS file system. It reads a XFS superblock from the device and
 * checks the magic and version numbers.
 *
 * Return 0 if device has a XFS file system.
 */
STATIC int
_xfs_isdev(dev_t dev)
{
	xfs_sb_t *sbp;
	buf_t	 *bp;
	int	 error;

	bp = bread(dev, XFS_SB_DADDR, BTOBB(sizeof(xfs_sb_t)));
	error = (bp->b_flags & B_ERROR);

	if (error == 0) {
		sbp = XFS_BUF_TO_SBP(bp);
		error = ((sbp->sb_magicnum != XFS_SB_MAGIC) ||
			 (sbp->sb_versionnum != XFS_SB_VERSION));
	}

	bp->b_flags |= B_AGE;
	brelse(bp);
	return error;
}


/*
 * xfs_mountroot() mounts the root file system.
 *
 * This function is called by vfs_mountroot(), which is called my main().
 * Must check that the root device has a XFS superblock and as such a
 * XFS file system. If the device does not, reject the mountroot request
 * and return error so that vfs_mountroot() knows to continue its search
 * for someone able to mount root.
 *
 * "why" is:
 *	ROOT_INIT	initial call.
 *	ROOT_REMOUNT	remount the root file system.
 *	ROOT_UNMOUNT	unmounting the root (e.g., as part a system shutdown).
 *
 * NOTE:
 *	Currently XFS doesn't support mounting a logical volume as root.
 * Mounting a logical volume as root requires xlv_assemble to run before
 * the mount.
 */
STATIC int
xfs_mountroot(vfs_t		*vfsp,
	      enum whymountroot	why)
#ifdef SIM
{ return ENOSYS; }
#else	/* !SIM */
{
	int		error = ENOSYS;
	static int	xfsrootdone;
	struct cred	*cr = u.u_cred;
	extern dev_t	rootdev;		/* from sys/systm.h */

	/*
	 * Check that the root device holds a XFS file system.
	 */
	if ((why == ROOT_INIT) && _xfs_isdev(rootdev))
		return XFS_ERROR(ENOSYS);
	
	switch (why) {
	  case ROOT_INIT:
		if (xfsrootdone++)
			return XFS_ERROR(EBUSY);
		if (rootdev == NODEV)
			return XFS_ERROR(ENODEV);
		vfsp->vfs_dev = rootdev;
		break;
	  case ROOT_REMOUNT:
		vfs_setflag(vfsp, VFS_REMOUNT);
		break;
	  case ROOT_UNMOUNT:
		/*
		 * XXX copied from efs_mountroot() - Is this right?
		 */
		error = xfs_cleanfs(XFS_VFSTOM(vfsp));
		binval(rootdev);
		pflushinvalvfsp(vfsp);
		return error;
	}
	error = vfs_lock(vfsp);
	if (error)
		goto bad;
	error = xfs_cmountfs(vfsp, rootdev, rootdev, 0, why, NULL, cr);
	if (error) {
		vfs_unlock(vfsp);
		goto bad;
	}
	vfs_add(NULL, vfsp, (vfsp->vfs_flag & VFS_RDONLY) ? MS_RDONLY : 0);
	vfs_unlock(vfsp);
	return 0;
bad:
	cmn_err(CE_WARN, "%s of root device 0x%x failed with errno %d\n",
		whymount[why], rootdev, error);
	return error;

} /* end of xfs_mountroot() */
#endif


/*
 * _xfs_ibusy searches for a busy inode in the mounted file system.
 *
 * Return 0 if there are no active inodes otherwise return 1.
 */
STATIC int
_xfs_ibusy(xfs_mount_t *mp)
{
	xfs_inode_t	*ip;
	vnode_t		*vp;
	int		busy;

	busy = 0;

	XFS_MOUNT_ILOCK(mp);

	for (ip = mp->m_inodes; ip && !busy; ip = ip->i_mnext) {
		vp = XFS_ITOV(ip);
		if (vp->v_count != 0) {
			if ((vp->v_count == 1) && (ip == mp->m_rootip))
				continue;
			if ((vp->v_count == 1) && (ip == mp->m_rbmip))
				continue;
			if ((vp->v_count == 1) && (ip == mp->m_rsumip))
				continue;
			busy++;
		}
	}
	
	XFS_MOUNT_IUNLOCK(mp);

	return busy;
}


/*
 * xfs_unmount
 *
 * XXX xfs_unmount() needs return code work and more error checking.
 */
STATIC int
xfs_unmount(vfs_t	*vfsp,
	    int		flags,
	    cred_t	*credp)
{
	xfs_mount_t	*mp;
	xfs_inode_t	*rip, *rbmip, *rsumip;
	vnode_t		*rvp = 0;
	vmap_t		vmap;
	int		vfs_flags;

	if (!suser())
		return XFS_ERROR(EPERM);

	mp = XFS_VFSTOM(vfsp);

	/*
	 * Make sure there are no active users.
	 */
	if (_xfs_ibusy(mp))
		return XFS_ERROR(EBUSY);
	
	/*
	 * Get rid of root inode (vnode) first, if we can.
	 *
	 * Does this prevent another process from traversing 
	 * into this file system?
	 */
	rip = mp->m_rootip;
	xfs_ilock(rip, XFS_ILOCK_EXCL);
	xfs_iflock(rip);

	/*
	 * Flush out the real time inodes.
	 */
	if ((rbmip = mp->m_rbmip) != NULL) {
		xfs_ilock(rbmip, XFS_ILOCK_EXCL);
		xfs_iflock(rbmip);
		xfs_iflush(rbmip, 0);
		xfs_iunlock(rbmip, XFS_ILOCK_EXCL);
		ASSERT(XFS_ITOV(rbmip)->v_count == 1);

		rsumip = mp->m_rsumip;
		xfs_ilock(rsumip, XFS_ILOCK_EXCL);
		xfs_iflock(rsumip);
		xfs_iflush(rsumip, 0);
		xfs_iunlock(rsumip, XFS_ILOCK_EXCL);
		ASSERT(XFS_ITOV(rsumip)->v_count == 1);
	}

	xfs_iflush(rip, 0);		/* synchronously flush to disk */
	rvp = XFS_ITOV(rip);
	if (rvp->v_count != 1) {
		xfs_iunlock(rip, XFS_ILOCK_EXCL);
		return XFS_ERROR(EBUSY);
	}

	if (rbmip) {
		VN_RELE(XFS_ITOV(rbmip));
		VN_RELE(XFS_ITOV(rsumip));
	}

	xfs_iunlock(rip, XFS_ILOCK_EXCL);
	VMAP(rvp, vmap);
	VN_RELE(rvp);
	vn_purge(rvp, &vmap);

	/*
	 * XXX Okay to call xfs_unmountfs() after deleting the root inode?
	 *
	 * Call common unmount function to flush to disk
	 * and free the super block buffer & mount structures.
	 */
	vfs_flags = (vfsp->vfs_flag & VFS_RDONLY) ? FREAD : FREAD|FWRITE;
	xfs_unmountfs(mp, vfs_flags, credp);	

	/*
	 * XXX Later when xfs_unmount()'s inode freeing is implemented, do:
	 * ASSERT(mp->m_inodes == NULL);
	 */

	return 0;	/* XXX Must determine unmount return value. */
}


/*
 * xfs_root extracts the root vnode from a vfs.
 * This function is called by traverse() and vfs_mountroot()
 * when crossing a mount point.
 *
 * vfsp -- the vfs struct for the desired file system
 * vpp  -- address of the caller's vnode pointer which should be
 *         set to the desired fs root vnode
 */
STATIC int
xfs_root(vfs_t		*vfsp,
	 vnode_t	**vpp)
{
	struct vnode	*vp;

	vp = XFS_ITOV((XFS_VFSTOM(vfsp))->m_rootip);	
	VN_HOLD(vp);
	*vpp = vp;
	return 0;
}


/*
 * xfs_statvfs
 *
 * Fill in the statvfs structure for the given file system.  We use
 * the superblock lock in the mount structure to ensure a consistent
 * snapshot of the counters returned.
 */
STATIC int
xfs_statvfs(vfs_t	*vfsp,
	    statvfs_t	*statp,
	    vnode_t	*vp)
{
	xfs_mount_t	*mp;
	xfs_sb_t	*sbp;
	int		s;

	mp = XFS_VFSTOM(vfsp);
	sbp = &(mp->m_sb);

	s = XFS_SB_LOCK(mp);
	statp->f_bsize = sbp->sb_blocksize;
	statp->f_frsize = sbp->sb_blocksize;
	statp->f_blocks = sbp->sb_dblocks + sbp->sb_rextents * sbp->sb_rextsize;
	statp->f_bfree = sbp->sb_fdblocks + sbp->sb_frextents * sbp->sb_rextsize;
	statp->f_files = sbp->sb_icount;
	statp->f_ffree = sbp->sb_ifree;
	statp->f_flag = vf_to_stf(vfsp->vfs_flag);
	XFS_SB_UNLOCK(mp, s);

	statp->f_fsid = mp->m_dev;
	(void) strcpy(statp->f_basetype, vfssw[xfs_type].vsw_name);
	statp->f_namemax = MAXNAMELEN;
	bzero(statp->f_fstr, sizeof(statp->f_fstr));

	return 0;
}


/*
 * xfs_sync flushes any pending I/O to file system vfsp.
 *
 * This routine is called by bdflush() to make sure that things make it
 * out to disk eventually, on sync() system calls to flush out everything,
 * and when the file system is unmounted.  For the bdflush() case, all
 * we really need to do is sync out the log to make all of our meta-data
 * updates permanent (except for timestamps) and dirty pages are kept
 * moving by calling pdflush() on the inodes containing them.
 *
 * Flags:
 *      SYNC_BDFLUSH - We're being called from bdflush() so we don't want
 *		       to sleep if we can help it.  All we really need to
 *		       do is ensure that dirty pages are not building up
 *		       on the inodes (so we call pdflush()) and that the
 *		       log is synced at least periodically.  The log itself
 *		       will take care of flushing the inodes and superblock.
 *      SYNC_ATTR    - We need to flush the inodes.  If SYNC_BDFLUSH is not
 *		       set, then we really want to lock each inode and flush
 *		       it.
 *      SYNC_WAIT    - All the flushes that take place in this call should
 *		       be synchronous.
 *      SYNC_DELWRI  - This tells us to push dirty pages associated with
 *		       inodes.  SYNC_WAIT and SYNC_BDFLUSH are used to
 *		       determine if they should be flushed sync, async, or
 *		       delwri.
 *      SYNC_CLOSE   - This flag is passed when the file system is being
 *		       unmounted.  We should sync and invalidate everthing.
 *      SYNC_FSDATA  - This indicates that the caller would like to make
 *		       sure the superblock is safe on disk.  We can ensure
 *		       this by simply makeing sure the log gets flushed
 *		       if SYNC_BDFLUSH is set, and by actually writing it
 *		       out otherwise.
 *
 */
STATIC int
xfs_sync(vfs_t		*vfsp,
	 short		flags,
	 cred_t		*credp)
{
	xfs_mount_t	*mp;
	xfs_inode_t	*ip;
	buf_t		*bp;
	vnode_t		*vp;
	vmap_t		vmap;
	int		error;
	int		last_error;
	int		fflag;
	int		ireclaims;
	uint		lock_flags;
	uint		log_flags;
	boolean_t	mount_locked;
	boolean_t	vnode_refed;
	int		preempt;
#define PREEMPT_MASK	0x7f

	mp = XFS_VFSTOM(vfsp);
	error = 0;
	last_error = 0;
	preempt = 0;

	fflag = B_ASYNC;		/* default is don't wait */
	if (flags & SYNC_BDFLUSH)
		fflag = B_DELWRI;
	if (flags & SYNC_WAIT)
		fflag = 0;		/* synchronous overrides all */

	lock_flags = XFS_ILOCK_SHARED;
	if (flags & (SYNC_DELWRI | SYNC_CLOSE)) {
		/*
		 * We need the I/O lock if we're going to call any of
		 * the flush/inval routines.
		 */
		lock_flags |= XFS_IOLOCK_SHARED;
	}

	/*
	 * Sync out the log.  This ensures that the log is periodically
	 * flushed even if there is not enough activity to fill it up.
	 */
	xfs_log_force(mp, (xfs_lsn_t)0, XFS_LOG_FORCE);

 loop:
	XFS_MOUNT_ILOCK(mp);
	mount_locked = B_TRUE;
	vnode_refed = B_FALSE;
	for (ip = mp->m_inodes; ip; ip = ip->i_mnext) {
		vp = XFS_ITOV(ip);
		
		/*
		 * Calls from bdflush() only want to keep files with
		 * dirty pages moving along, so if this inode doesn't
		 * have any then skip it.
		 */
		if ((flags & SYNC_BDFLUSH) && !(VN_DIRTY(vp))) {
			continue;
		}

		/*
		 * We don't mess with swap files from here since it is
		 * too easy to deadlock on memory here.
		 */
		if (vp->v_flag & VISSWAP) {
			continue;
		}

		/*
		 * Try to lock without sleeping.  We're out of order with
		 * the inode list lock here, so if we fail we need to drop
		 * the mount lock and try again.  If we're called from
		 * bdflush() here, then don't bother.
		 *
		 * The inode lock here actually coordinates with the
		 * almost spurious inode lock in xfs_ireclaim() to prevent
		 * the vnode we handle here without a reference from
		 * being free while we reference it.  If we lock the inode
		 * while it's on the mount list here, then the spurious inode
		 * lock in xfs_ireclaim() after the inode is pulled from
		 * the mount list will sleep until we release it here.
		 * This keeps the vnode from being freed while we reference
		 * it.  It is also cheaper and simpler than actually doing
		 * a vn_get() for every inode we touch here.
		 */
		if (xfs_ilock_nowait(ip, lock_flags) == 0) {
			if (flags & SYNC_BDFLUSH) {
				continue;
			}

			/*
			 * We need to unlock the inode list lock in order
			 * to lock the inode.  Use the m_ireclaims counter
			 * as a list version number to tell us whether we
			 * need to start again at the beginning of the list.
			 *
			 * We also use the inode list lock to protect us
			 * in taking a snapshot of the vnode version number
			 * for use in calling vn_get().
			 */
			ireclaims = mp->m_ireclaims;
			VMAP(vp, vmap);
			XFS_MOUNT_IUNLOCK(mp);
			mount_locked = B_FALSE;
			vp = vn_get(vp, &vmap);
			if (vp == NULL) {
				/*
				 * The vnode was reclaimed once we let go
				 * of the inode list lock.  Start again
				 * at the beginning of the list.
				 */
				goto loop;
			}
			xfs_ilock(ip, lock_flags);
			vnode_refed = B_TRUE;
		}

		if (flags & SYNC_CLOSE) {
			/*
			 * This is the shutdown case.  We just need to
			 * flush and invalidate all the pages associated
			 * with the inode.  Drop the inode lock since
			 * we can't hold it across calls to the buffer
			 * cache.
			 */
			xfs_iunlock(ip, XFS_ILOCK_SHARED);
			pflushinvalvp(vp, 0, XFS_ISIZE_MAX(ip));
			xfs_ilock(ip, XFS_ILOCK_SHARED);
		}

		if ((flags & SYNC_DELWRI) && VN_DIRTY(vp)) {
			if (mount_locked) {
				ireclaims = mp->m_ireclaims;
				XFS_MOUNT_IUNLOCK(mp);
				mount_locked = B_FALSE;
			}

			/*
			 * Drop the inode lock since we can't hold it
			 * across calls to the buffer cache.
			 */
			xfs_iunlock(ip, XFS_ILOCK_SHARED);
			if (flags & SYNC_BDFLUSH) {
				pdflush(vp, B_DELWRI);
			} else {
				error = pflushvp(vp, ip->i_d.di_size, fflag);
			}
			xfs_ilock(ip, XFS_ILOCK_SHARED);
		}

		/*
		 * If we're not being called from bdflush() and the inode
		 * is dirty, then sync it out.
		 */
		if ((!(flags & SYNC_BDFLUSH)) &&
		    (flags & SYNC_ATTR) &&
		    ((ip->i_update_core) ||
		     (ip->i_item.ili_format.ilf_fields != 0))) {
			if (mount_locked) {
				ireclaims = mp->m_ireclaims;
				XFS_MOUNT_IUNLOCK(mp);
				mount_locked = B_FALSE;
			}

			xfs_iflock(ip);
			xfs_iflush(ip, fflag);
		}

		xfs_iunlock(ip, lock_flags);
		if (vnode_refed) {
			/*
			 * If we had to take a reference on the vnode
			 * above, then wait until after we've unlocked
			 * the inode to release the reference.  This is
			 * because we can be already holding the inode
			 * lock when VN_RELE() calls xfs_inactive().
			 */
			VN_RELE(vp);
			vnode_refed = B_FALSE;
		}
		if (error) {
			last_error = error;
		}
		if ((++preempt & PREEMPT_MASK) == 0) {
			if (mount_locked) {
				ireclaims = mp->m_ireclaims;
				XFS_MOUNT_IUNLOCK(mp);
				mount_locked = B_FALSE;
			}
			PREEMPT();
		}
		if (!mount_locked) {
			XFS_MOUNT_ILOCK(mp);
			if (mp->m_ireclaims != ireclaims) {
				XFS_MOUNT_IUNLOCK(mp);
				goto loop;
			}
			mount_locked = B_TRUE;
		}
			
	}
	XFS_MOUNT_IUNLOCK(mp);

	/*
	 * Flushing out dirty data above probably generated more
	 * log activity, so if this isn't bdflush() then flush
	 * the log again.  If SYNC_WAIT is set then do it synchronously.
	 */
	if (!(flags & SYNC_BDFLUSH)) {
		log_flags = XFS_LOG_FORCE;
		if (flags & SYNC_WAIT) {
			log_flags |= XFS_LOG_SYNC;
		}
		xfs_log_force(mp, (xfs_lsn_t)0, log_flags);
	}

	if ((flags & (SYNC_FSDATA | SYNC_BDFLUSH)) == SYNC_FSDATA) {
		/*
		 * Only flush the superblock if we're not being called
		 * from bdflush().
		 */
		bp = xfs_getsb(mp);
		bp->b_flags |= fflag;
		error = bwrite(bp);
		if (error) {
			last_error = error;
		}
	}


	return last_error;
}


/*
 * xfs_vget
 */
STATIC int
xfs_vget(vfs_t		*vfsp,
	 vnode_t	**vpp,
	 fid_t		*fidp)
{
        xfs_fid_t	*xfid;
        xfs_inode_t	*ip;

        xfid = (struct xfs_fid *)fidp;
	ip = xfs_iget(XFS_VFSTOM(vfsp), NULL, xfid->fid_ino, XFS_ILOCK_EXCL);
        if (NULL == ip) {
                *vpp = NULL;
                return XFS_ERROR(EIO);
        }
        if (ip->i_gen != xfid->fid_gen) {
                xfs_iput(ip, XFS_ILOCK_EXCL);
                *vpp = NULL;
                return 0;
        }
        xfs_iunlock(ip, XFS_ILOCK_EXCL);
        *vpp = XFS_ITOV(ip);
        return 0;


}



struct vfsops xfs_vfsops = {
	xfs_vfsmount,
	xfs_unmount,
	xfs_root,
	xfs_statvfs,
	xfs_sync,
	xfs_vget,
	xfs_mountroot,
	fs_nosys,	/* swapvp */
};
