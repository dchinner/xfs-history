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
#ident  "$Revision: 1.100 $"

#include <strings.h>
#include <limits.h>
#ifdef SIM
#define _KERNEL	1
#endif
#include <sys/types.h>
#include <sys/buf.h>
#include <sys/vfs.h>
#include <sys/pfdat.h>
#include <sys/user.h>
#include <sys/vnode.h>
#include <sys/grio.h>
#include <sys/dmi.h>
#include <sys/dmi_kern.h>
#include <specfs/snode.h>
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
#include <sys/proc.h>
#include <sys/sema.h>
#include <sys/statvfs.h>
#include <sys/uio.h>
#include <sys/dirent.h>
#include <sys/ktrace.h>
#include <sys/uuid.h>
#ifndef SIM
#include <sys/xlv_base.h>
#include <sys/xlv_tab.h>
#include <sys/xlv_lock.h>
#endif
#include <sys/xlate.h>
#include <sys/capability.h>

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
#include "xfs_attr_sf.h"
#include "xfs_dir_sf.h"
#include "xfs_dinode.h"
#include "xfs_inode_item.h"
#include "xfs_inode.h"
#include "xfs_ag.h"
#include "xfs_error.h"
#include "xfs_bmap.h"
#include "xfs_da_btree.h"
#include "xfs_rw.h"
#include "xfs_buf_item.h"
#include "xfs_extfree_item.h"

#ifdef SIM
#include "sim.h"
#endif

#ifndef SIM
#define	whymount_t	whymountroot_t
#define	NONROOT_MOUNT	ROOT_UNMOUNT

static char *whymount[] = { "initial mount", "remount", "unmount" };

/*
 * Static function prototypes.
 */
STATIC int
xfs_vfsmount(
	vfs_t		*vfsp,
	vnode_t		*mvp,
	struct mounta	*uap,
	cred_t		*credp);

STATIC int
xfs_mountroot(
	vfs_t			*vfsp,
	enum whymountroot	why);

STATIC int
xfs_unmount(
	vfs_t	*vfsp,
	int	flags,
	cred_t	*credp);

STATIC int
xfs_root(
	vfs_t	*vfsp,
	vnode_t	**vpp);

STATIC int
xfs_statvfs(
	vfs_t		*vfsp,
	statvfs_t	*statp,
	vnode_t		*vp);

STATIC int
xfs_sync(
	vfs_t		*vp,
	short		flags,
	cred_t		*credp);

STATIC int
xfs_vget(
	vfs_t		*vfsp,
	vnode_t		**vpp,
	fid_t		*fidp);


STATIC int
xfs_cmountfs(
	vfs_t		*vfsp,
	dev_t		ddev,
	dev_t		logdev,
	dev_t		rtdev,
	whymount_t	why,
	struct xfs_args	*ap,
	struct cred	*cr);

STATIC xfs_mount_t *
xfs_get_vfsmount(
	vfs_t	*vfsp,
	dev_t	ddev,
	dev_t	logdev,
	dev_t	rtdev);

STATIC int
spectodev(
	char	*spec,
	dev_t	*devp);

STATIC int
xfs_isdev(
	dev_t	dev);

STATIC int
xfs_ibusy(
	xfs_mount_t	*mp);

#endif	/* !SIM */




/*
 * xfs_fstype is the number given to xfs to indicate
 * its type among vfs's.  It is initialized in xfs_init().
 */
int	xfs_fstype;

/*
 * xfs_init
 *
 * This is called through the vfs switch at system initialization
 * to initialize any global state associated with XFS.  All we
 * need to do currently is save the type given to XFS in xfs_fstype.
 *
 * vswp -- pointer to the XFS entry in the vfs switch table
 * fstype -- index of XFS in the vfs switch table used as the XFS fs type.
 */
/* ARGSUSED */
int
xfs_init(
	vfssw_t	*vswp,
	int	fstype)
{
	extern mutex_t	xfs_refcache_lock;
	extern int	xfs_refcache_size;
	extern int	ncsize;
	extern xfs_inode_t	**xfs_refcache;
	extern zone_t	*xfs_da_state_zone;
	extern zone_t	*xfs_bmap_free_item_zone;
	extern zone_t	*xfs_btree_cur_zone;
	extern zone_t	*xfs_inode_zone;
	extern zone_t	*xfs_trans_zone;
	extern zone_t	*xfs_buf_item_zone;
	extern zone_t	*xfs_efd_zone;
	extern zone_t	*xfs_efi_zone;

#ifndef SIM
	extern mutex_t	xfs_strat_lock;
	extern mutex_t	xfsd_lock;
	extern sv_t	xfsd_wait;
	extern mutex_t	xfs_ancestormon;
	extern zone_t	*xfs_bmap_zone;
	extern zone_t	*xfs_irec_zone;
	extern zone_t	*xfs_strat_write_zone;
	extern zone_t	*xfs_gap_zone;
#ifdef DEBUG
	extern ktrace_t	*xfs_alloc_trace_buf;
	extern ktrace_t	*xfs_bmap_trace_buf;
	extern ktrace_t	*xfs_bmbt_trace_buf;
	extern ktrace_t	*xfs_strat_trace_buf;
#endif	/* DEBUG */
#endif	/* !SIM */

	xfs_fstype = fstype;

#ifndef SIM
	mutex_init(&xfs_strat_lock, MUTEX_SPIN, "xfsstrat");
	mutex_init(&xfs_ancestormon, MUTEX_DEFAULT, "xfs_ancestor");
	mutex_init(&xfsd_lock, MUTEX_SPIN, "xfsd");
	sv_init(&xfsd_wait, SV_DEFAULT, "xfsd");

	/*
	 * Initialize the inode reference cache.
	 */
	if (ncsize == 0) {
		xfs_refcache_size = 100;
	} else {
		xfs_refcache_size = ncsize;
	}
#endif	/* !SIM */

	/*
	 * Initialize all of the zone allocators we use.
	 */
	xfs_bmap_free_item_zone = kmem_zone_init(sizeof(xfs_bmap_free_item_t),
						 "xfs_bmap_free_item");
	xfs_btree_cur_zone = kmem_zone_init(sizeof(xfs_btree_cur_t),
					    "xfs_btree_cur");
	xfs_inode_zone = kmem_zone_init(sizeof(xfs_inode_t), "xfs_inode");
	xfs_trans_zone = kmem_zone_init(sizeof(xfs_trans_t), "xfs_trans");
#ifndef SIM
	xfs_irec_zone = kmem_zone_init((XFS_BMAP_MAX_NMAP *
					sizeof(xfs_bmbt_irec_t)), "xfs_irec");
	xfs_bmap_zone = kmem_zone_init((XFS_ZONE_NBMAPS *
					sizeof(struct bmapval)), "xfs_bmap");
	xfs_strat_write_zone = kmem_zone_init(sizeof(xfs_strat_write_locals_t),
					      "xfs_strat_write");
	xfs_gap_zone = kmem_zone_init(sizeof(xfs_gap_t), "xfs_gap");
#endif
	xfs_da_state_zone =
		kmem_zone_init(sizeof(xfs_da_state_t), "xfs_da_state");

	/*
	 * The size of the zone allocated buf log item is the maximum
	 * size possible under XFS.  This wastes a little bit of memory,
	 * but it is much faster.
	 */
	xfs_buf_item_zone =
		kmem_zone_init((sizeof(xfs_buf_log_item_t) +
				(((XFS_MAX_BLOCKSIZE / XFS_BLI_CHUNK) /
                                  NBWORD) * sizeof(int))),
			       "xfs_buf_item");
	xfs_efd_zone = kmem_zone_init((sizeof(xfs_efd_log_item_t) +
				       (15 * sizeof(xfs_extent_t))),
				      "xfs_efd_item");
	xfs_efi_zone = kmem_zone_init((sizeof(xfs_efi_log_item_t) +
				       (15 * sizeof(xfs_extent_t))),
				      "xfs_efi_item");
	xfs_ifork_zone = kmem_zone_init(sizeof(xfs_ifork_t), "xfs_ifork");

	/*
	 * Allocate global trace buffers.
	 */
#if defined(DEBUG) && !defined(SIM)
	xfs_alloc_trace_buf = ktrace_alloc(XFS_ALLOC_TRACE_SIZE, 0);
	xfs_bmap_trace_buf = ktrace_alloc(XFS_BMAP_TRACE_SIZE, 0);
	xfs_bmbt_trace_buf = ktrace_alloc(XFS_BMBT_TRACE_SIZE, 0);
	xfs_strat_trace_buf = ktrace_alloc(XFS_STRAT_GTRACE_SIZE, 0);
#endif
	
	/*
	 * The inode hash table is created on a per mounted
	 * file system bases.
	 */
	return 0;
}


#ifndef SIM
/*
 * Resolve path name of special file to its device.
 */
STATIC int
spectodev(
	char	*spec,
	dev_t	*devp)
{
	vnode_t	*bvp;
	int	error;

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
 * This function is the common mount file system function for XFS.
 */
STATIC int
xfs_cmountfs(
	vfs_t		*vfsp,
	dev_t		ddev,
	dev_t		logdev,
	dev_t		rtdev,
	whymount_t	why,
	struct xfs_args	*ap,
	struct cred	*cr)
{
	xfs_mount_t	*mp;
	struct vnode 	*ddevvp, *rdevvp, *ldevvp;
	int		error = 0;
	int		vfs_flags;
	size_t		n;
	struct vnode 	*makespecvp(dev_t, vtype_t);

	/*
	 * Remounting a XFS file system is bad. The log manager
	 * automatically handles recovery so no action is required.
	 */
	if (vfsp->vfs_flag & VFS_REMOUNT)
		return 0;
	
	mp = xfs_get_vfsmount(vfsp, ddev, logdev, rtdev);

	/*
 	 * Open the data and real time devices now.
	 */
	vfs_flags = (vfsp->vfs_flag & VFS_RDONLY) ? FREAD : FREAD|FWRITE;
	if (ddev != 0) {
		ddevvp = makespecvp( ddev, VBLK );
		error = VOP_OPEN(&ddevvp, vfs_flags, cr);
		if (error) {
			VN_RELE(ddevvp);
			goto error0;
		}
		mp->m_ddevp = ddevvp;
	} else {
		ddevvp = NULL;
	}
	if (rtdev != 0) {
		rdevvp = makespecvp( rtdev, VBLK );
		error = VOP_OPEN(&rdevvp, vfs_flags, cr);
		if (error) {
			VN_RELE(rdevvp);
			goto error1;
		}
		mp->m_rtdevp = rdevvp;
	} else {
		rdevvp = NULL;
	}
	if (logdev != 0) {
		if (logdev == ddev) {
			ldevvp = NULL;
			mp->m_logdevp = ddevvp;
		} else {
			ldevvp = makespecvp( logdev, VBLK );
			error = VOP_OPEN(&ldevvp, vfs_flags, cr);
			if (error) {
				VN_RELE(ldevvp);
				goto error2;
			}
			mp->m_logdevp = ldevvp;
		}
		if (ap != NULL) {
			/* Called through the mount system call */
			if (ap->version != 1) {
				error = XFS_ERROR(EINVAL);
				goto error3;
			}
			mp->m_logbufs = ap->logbufs;
			mp->m_logbsize = ap->logbufsize;
			if (error = copyinstr(ap->fsname, mp->m_fsname,
					      PATH_MAX, &n)) {
				if (error == ENAMETOOLONG)
					error = EINVAL;
				goto error3;
			}
		} else {
			/*
			 * Called through vfs_mountroot/xfs_mountroot.
			 */
			mp->m_logbufs = -1;
			mp->m_logbsize = -1;
			strcpy(mp->m_fsname, "/");
		}
	} else {
		ldevvp = NULL;
	}

	/*
	 * Pull in the 'wsync' mount option before we do the real
	 * work of mounting and recovery.  The arg pointer will
	 * be NULL when we are being called from the root mount code.
	 */
	if ((ap != NULL) && (ap->flags & XFSMNT_WSYNC)) {
		mp->m_flags |= XFS_MOUNT_WSYNC;
	}

	if (error = xfs_mountfs(vfsp, ddev)) {
		goto error3;
	}

	/*
	 * For root mounts, make sure the clock is set.  This
	 * is just a traditional root file system thing to do.
	 */
        if (why == ROOT_INIT) {
                extern int rtodc( void );

                clkset( rtodc() );
        }

	return error;

 error3:
	if (ldevvp) {
		VOP_CLOSE(ldevvp, vfs_flags, L_TRUE, 0, cr);
		binval(logdev);
		VN_RELE(ldevvp);
	}
 error2:
	if (rdevvp) {
		VOP_CLOSE(rdevvp, vfs_flags, L_TRUE, 0, cr);
		binval(rtdev);
		VN_RELE(rdevvp);
	}
 error1:
	if (ddevvp) {
		VOP_CLOSE(ddevvp, vfs_flags, L_TRUE, 0, cr);
		binval(ddev);
		VN_RELE(ddevvp);
	}
 error0:
	xfs_mount_free(mp);
	return error;
}	/* end of xfs_cmountfs() */


/*
 * xfs_get_vfsmount() ensures that the given vfs struct has an
 * associated mount struct. If a mount struct doesn't exist, as
 * is the case during the initial mount, a mount structure is
 * created and initialized.
 */
STATIC xfs_mount_t *
xfs_get_vfsmount(
	vfs_t	*vfsp,
	dev_t	ddev,
	dev_t	logdev,
	dev_t	rtdev)
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
		/* vfsp->vfs_bsize filled in later from superblock */
		vfsp->vfs_fstype = xfs_fstype;
		vfsp->vfs_data = mp;
		vfsp->vfs_dev = ddev;
		vfsp->vfs_nsubmounts = 0;
		vfsp->vfs_bcount = 0;
		/* vfsp->vfs_fsid is filled in later from superblock */
	}

	return mp;
}	/* end of xfs_get_vfsmount() */

#if _K64U64
/*
 * irix5_to_xfs_args
 * 
 * This is used with copyin_xlate() to copy a xfs_args structure
 * in from user space from a 32 bit application into a 64 bit kernel.
 */
/*ARGSUSED*/
int
irix5_to_xfs_args(
	enum xlate_mode	mode,
	void		*to,
	int		count,
	xlate_info_t	*info)
{
	COPYIN_XLATE_PROLOGUE(irix5_xfs_args, xfs_args);

	target->version = source->version;
	target->flags = source->flags;
	target->logbufs = source->logbufs;
	target->logbufsize = source->logbufsize;
	target->fsname = (char*)(__psint_t)source->fsname;

	return 0;
}
#endif

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
xfs_vfsmount(
	vfs_t		*vfsp,
	vnode_t		*mvp,
	struct mounta	*uap,
	cred_t		*credp)
{
	struct xfs_args	args;			/* xfs mount arguments */
	dev_t		device;			/* device: block or logical */
	dev_t		ddev;
	dev_t		logdev;
	dev_t		rtdev;
	int		error;

	if (!_CAP_ABLE(CAP_MOUNT_MGT))
		return XFS_ERROR(EPERM);
	if (mvp->v_type != VDIR)
		return XFS_ERROR(ENOTDIR);
	if (((uap->flags & MS_REMOUNT) == 0) &&
	    ((mvp->v_count != 1) || (mvp->v_flag & VROOT)))
		return XFS_ERROR(EBUSY);

	/*
	 * Copy in XFS-specific arguments.
	 */
	bzero(&args, sizeof args);
	if (copyin_xlate(uap->dataptr, &args, sizeof(args),
			 irix5_to_xfs_args, u.u_procp->p_abi, 1))
		return XFS_ERROR(EFAULT);

	/*
	 * Resolve path name of special file being mounted.
	 */
	if (error = spectodev(uap->spec, &device))
		return error;
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
		if ((xlv_tab == NULL) || (xlv_tab->num_subvols == 0) ||
		    (minor(device) >= xlv_tab->max_subvols))
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

		if (!logdev) {
			logdev = ddev;
		}

		ASSERT(ddev && logdev);
		XLV_IO_UNLOCK(minor(device));

	} else { /* block device */
		ddev = logdev = device;
		rtdev = 0;			/* no realtime */
	}

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
	if (error) {
		return error;
	}
	/*
	 *  Don't set the VFS_DMI flag until here because we don't want
	 *  to send events while replaying the log.
	 */
	if (uap->flags & MS_DMI) {
		vfsp->vfs_flag |= VFS_DMI;
		/* Always send mount event (when mounted with dmi option) */
		error = dm_namesp_event(DM_MOUNT, mvp, mvp,
					uap->dir, uap->spec, 0, 0);
		if (error) {
			int	errcode;

			vfsp->vfs_flag &= ~VFS_DMI;
			errcode = xfs_unmount(vfsp, 0, credp);
			ASSERT (errcode == 0);
		}
	}

	return error;

}	/* end of xfs_vfsmount() */



/*
 * This function determines whether or not the given device has a
 * XFS file system. It reads a XFS superblock from the device and
 * checks the magic and version numbers.
 *
 * Return 0 if device has a XFS file system.
 */
STATIC int
xfs_isdev(
	dev_t dev)
{
	xfs_sb_t *sbp;
	buf_t	 *bp;
	int	 error;

	bp = bread(dev, XFS_SB_DADDR, BTOBB(sizeof(xfs_sb_t)));
	error = (bp->b_flags & B_ERROR);

	if (error == 0) {
		sbp = XFS_BUF_TO_SBP(bp);
		error = (sbp->sb_magicnum != XFS_SB_MAGIC) ||
			(!XFS_SB_GOOD_VERSION(sbp->sb_versionnum)) ||
			(sbp->sb_inprogress != 0);
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
 */
STATIC int
xfs_mountroot(
	vfs_t			*vfsp,
	enum whymountroot	why)
{
	int		error = ENOSYS;
	static int	xfsrootdone;
	struct cred	*cr = u.u_cred;
	dev_t		ddev, logdev, rtdev;
	xfs_mount_t	*mp;
	buf_t		*bp;
	extern dev_t	rootdev;		/* from sys/systm.h */


	/*
	 * Check that the root device holds an XFS file system.
	 *
	 * If the device is an XLV volume, cannot check for an
	 * XFS superblock because the device is not yet open.
	 */
	if ((why == ROOT_INIT) && 
	     (emajor(rootdev) != XLV_MAJOR) &&
	     (xfs_isdev(rootdev))) {
		return XFS_ERROR(ENOSYS);
	}
	
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
		mp = XFS_VFSTOM(vfsp);
		if (xfs_ibusy(mp)) {
			/*
			 * There are still busy vnodes in the file system.
			 * Flush what we can and then get out without
			 * unmounting cleanly.  This will force recovery
			 * to run when we reboot.  We return an error
			 * even though nobody looks at it since we're
			 * going down.
			 *
			 * It turns out that we always take this path,
			 * because init and the uadmin command still have
			 * files open when we get here.  We just try to
			 * flush everything so that we'll be clean
			 * anyway.
			 *
			 * First try the inodes.  xfs_sync() will try to
			 * flush even those inodes which are currently
			 * being referenced.  xfs_iflush_all() will purge
			 * and flush all the unreferenced vnodes.
			 */
			xfs_sync(vfsp,
				 (SYNC_WAIT | SYNC_CLOSE |
				  SYNC_ATTR | SYNC_FSDATA),
				 cr);
			xfs_iflush_all(mp, XFS_FLUSH_ALL);

			/*
			 * Force the log to unpin as many buffers as
			 * possible and then sync them out.
			 */
			xfs_log_force(mp, (xfs_lsn_t)0,
				      XFS_LOG_FORCE | XFS_LOG_SYNC);
			bflush(mp->m_dev);
			if (mp->m_rtdev) {
				bflush(mp->m_dev);
			}

			/*
			 * Force the log again to sync out any changes
			 * caused by any delayed allocation buffers just
			 * getting flushed out now.  Even though it seems
			 * silly, flush the file system device again so that
			 * any meta-data in the log gets flushed.
			 */
			xfs_log_force(mp, (xfs_lsn_t)0,
				      XFS_LOG_FORCE | XFS_LOG_SYNC);
			bflush(mp->m_dev);

			/*
			 * Finally, try to flush out the superblock.  If
			 * it is pinned at this point we can't, because
			 * we don't want to get stuck waiting for it to
			 * be unpinned.  It should be unpinned normally
			 * because of the log flushes above.
			 */
			bp = xfs_getsb(mp, 0);
			if (bp->b_pincount == 0) {
				bp->b_flags &= ~(B_DONE | B_READ);
				bp->b_flags |= B_WRITE;
				bdstrat(bmajor(mp->m_dev), bp);
				(void) iowait(bp);
			}
			return EBUSY;
		}
		error = xfs_unmount(vfsp, 0, NULL);
		return error;
	}
	error = vfs_lock(vfsp);
	if (error) {
		goto bad;
	}

	if (emajor(rootdev) == XLV_MAJOR) {
		/*
		 * logical volume
		 */
		xlv_tab_subvol_t *xlv_p;
		xlv_tab_subvol_t *sv_p;

		/*
		 * XXX This XLV section should be moved to a xlv function
		 * that can be stub'ed when xlv does not exist.
		 */
		if (xlv_tab == NULL) {
			cmn_err(CE_WARN, "logical volume info not present");
			ddev = rootdev;
			logdev = rtdev = 0;
		} else {
			XLV_IO_LOCK(minor(rootdev), MR_ACCESS);
			xlv_p = &xlv_tab->subvolume[minor(rootdev)];
			if (! XLV_SUBVOL_EXISTS(xlv_p)) {
				XLV_IO_UNLOCK(minor(rootdev));
				return XFS_ERROR(ENXIO);
			}
			ddev   = (sv_p = xlv_p->vol_p->data_subvol) ?
				sv_p->dev : 0;
			logdev = (sv_p = xlv_p->vol_p->log_subvol) ?
				sv_p->dev : 0;
			rtdev  = (sv_p = xlv_p->vol_p->rt_subvol) ?
				sv_p->dev : 0;

			if (!logdev) {
				logdev = ddev;
			}

			XLV_IO_UNLOCK(minor(rootdev));
		}
	} else {
		/*
		 * block device
		 */
		ddev = logdev = rootdev;
		rtdev = 0;
	}
	ASSERT(ddev && logdev);

	error = xfs_cmountfs(vfsp, ddev, logdev, rtdev, why, NULL, cr);

	if (error) {
		vfs_unlock(vfsp);
		goto bad;
	}
	vfs_add(NULL, vfsp, (vfsp->vfs_flag & VFS_RDONLY) ? MS_RDONLY : 0);
	vfs_unlock(vfsp);
	return(0);
bad:
	cmn_err(CE_WARN, "%s of root device 0x%x failed with errno %d\n",
		whymount[why], rootdev, error);
	return error;

} /* end of xfs_mountroot() */


/*
 * xfs_ibusy searches for a busy inode in the mounted file system.
 *
 * Return 0 if there are no active inodes otherwise return 1.
 */
STATIC int
xfs_ibusy(
	xfs_mount_t	*mp)
{
	xfs_inode_t	*ip;
	vnode_t		*vp;
	int		busy;

	busy = 0;

	XFS_MOUNT_ILOCK(mp);

	ip = mp->m_inodes;
	if (ip == NULL) {
		XFS_MOUNT_IUNLOCK(mp);
		return busy;
	}

	do {
		vp = XFS_ITOV(ip);
		if (vp->v_count != 0) {
			if ((vp->v_count == 1) && (ip == mp->m_rootip)) {
				ip = ip->i_mnext;
				continue;
			}
			if ((vp->v_count == 1) && (ip == mp->m_rbmip)) {
				ip = ip->i_mnext;
				continue;
			}
			if ((vp->v_count == 1) && (ip == mp->m_rsumip)) {
				ip = ip->i_mnext;
				continue;
			}
#ifdef DEBUG
			printf("busy vp=0x%x count=%d\n", vp, vp->v_count);
#endif
			busy++;
		}
		ip = ip->i_mnext;
	} while ((ip != mp->m_inodes) && !busy);
	
	XFS_MOUNT_IUNLOCK(mp);

	return busy;
}


/*
 * xfs_unmount
 *
 * XXX xfs_unmount() needs return code work and more error checking.
 */
/*ARGSUSED*/
STATIC int
xfs_unmount(
	vfs_t	*vfsp,
	int	flags,
	cred_t	*credp)
{
	xfs_mount_t	*mp;
	xfs_inode_t	*rip, *rbmip, *rsumip;
	vnode_t		*rvp = 0;
	vmap_t		vmap;
	int		vfs_flags;
	int		sendunmountevent = 0;
	int		error;

	if (!_CAP_ABLE(CAP_MOUNT_MGT))
		return XFS_ERROR(EPERM);

	mp = XFS_VFSTOM(vfsp);
	rip = mp->m_rootip;
	rvp = XFS_ITOV(rip);

	if (vfsp->vfs_flag & VFS_DMI) {
		if (mp->m_dmevmask & DM_ETOM(DM_PREUNMOUNT)) {
			error = dm_namesp_event(DM_PREUNMOUNT,
						rvp, rvp, 0, 0, 0, 0);
			if (error)
				return XFS_ERROR(error);
		}
		if (mp->m_dmevmask & DM_ETOM(DM_UNMOUNT))
			sendunmountevent = 1;
	}

	/*
	 * First blow any referenced inode from this file system
	 * out of the reference cache.
	 */
	xfs_refcache_purge_mp(mp);

	/*
	 * Make sure there are no active users.
	 */
	if (xfs_ibusy(mp))
		return XFS_ERROR(EBUSY);
	
	xfs_ilock(rip, XFS_ILOCK_EXCL);
	xfs_iflock(rip);

	/*
	 * Flush out the real time inodes.
	 */
	if ((rbmip = mp->m_rbmip) != NULL) {
		xfs_ilock(rbmip, XFS_ILOCK_EXCL);
		xfs_iflock(rbmip);
		xfs_iflush(rbmip, XFS_IFLUSH_SYNC);
		xfs_iunlock(rbmip, XFS_ILOCK_EXCL);
		ASSERT(XFS_ITOV(rbmip)->v_count == 1);

		rsumip = mp->m_rsumip;
		xfs_ilock(rsumip, XFS_ILOCK_EXCL);
		xfs_iflock(rsumip);
		xfs_iflush(rsumip, XFS_IFLUSH_SYNC);
		xfs_iunlock(rsumip, XFS_ILOCK_EXCL);
		ASSERT(XFS_ITOV(rsumip)->v_count == 1);
	}

	xfs_iflush(rip, XFS_IFLUSH_SYNC);   /* synchronously flush to disk */
	if (rvp->v_count != 1) {
		xfs_iunlock(rip, XFS_ILOCK_EXCL);
		error = EBUSY;
		goto out;
	}

	if (rbmip) {
		VN_RELE(XFS_ITOV(rbmip));
		VN_RELE(XFS_ITOV(rsumip));
	}

	xfs_iunlock(rip, XFS_ILOCK_EXCL);
	VMAP(rvp, vmap);
	VN_RELE(rvp);
	vn_purge(rvp, &vmap);

	error = 0;
	/*
	 * Call common unmount function to flush to disk
	 * and free the super block buffer & mount structures.
	 */
	vfs_flags = (vfsp->vfs_flag & VFS_RDONLY) ? FREAD : FREAD|FWRITE;
	xfs_unmountfs(mp, vfs_flags, credp);	
out:
	if (sendunmountevent) {
		(void) dm_namesp_event(DM_UNMOUNT, (vnode_t *) vfsp,
				       0, 0, 0, 0, error);
	}
	return XFS_ERROR(error);
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
xfs_root(
	vfs_t	*vfsp,
	vnode_t	**vpp)
{
	vnode_t	*vp;

	vp = XFS_ITOV((XFS_VFSTOM(vfsp))->m_rootip);	
	VN_HOLD(vp);
	*vpp = vp;
	return 0;
}

/*
 * Get a buffer containing the superblock from an XFS filesystem given its
 * device vnode pointer.
 * Used by statfs.
 */
static int
devvptoxfs(
	vnode_t		*devvp,
	buf_t		**bpp,
	xfs_sb_t	**fsp,
	cred_t		*cr)
{
	buf_t		*bp;
	dev_t		dev;
	int		error;
	xfs_sb_t	*fs;

	if (devvp->v_type != VBLK)
		return ENOTBLK;
	if (error = VOP_OPEN(&devvp, FREAD, cr))
		return error;
	dev = devvp->v_rdev;
	VOP_RWLOCK(devvp, VRWLOCK_WRITE);
	if (VTOS(devvp)->s_flag & SMOUNTED) {
		/*
		 * Device is mounted.  Get an empty buffer to hold a
		 * copy of its superblock, so we don't have to worry
		 * about racing with unmount.  Hold devvp's lock to
		 * block unmount here.
		 */
		bp = ngeteblk(BBSIZE);
		fs = (xfs_sb_t *)bp->b_un.b_addr;
		bcopy(&XFS_VFSTOM(vfs_devsearch(dev))->m_sb, fs, sizeof(*fs));
	} else {
		/*
		 * If the buffer is already in core, it might be stale.
		 * User might have been doing block reads, then mkfs.
		 * Very unlikely, but would cause a buffer to contain
		 * stale information.
		 * If buffer is marked DELWRI, then use it since it reflects
		 * what should be on the disk.
		 */
		bp = incore(dev, XFS_SB_DADDR, BLKDEV_BB, 1);
		if (bp && !(bp->b_flags & B_DELWRI)) {
			bp->b_flags |= B_STALE;
			brelse(bp);
			bp = NULL;
		}
		if (!bp)
			bp = bread(dev, XFS_SB_DADDR, BLKDEV_BB);
		if (bp->b_flags & B_ERROR) {
			error = bp->b_error;
			brelse(bp);
		} else
			fs = (xfs_sb_t *)bp->b_un.b_addr;
	}
	VOP_RWUNLOCK(devvp, VRWLOCK_WRITE);
	*bpp = bp;
	*fsp = fs;
	return error;
}

/*
 * Get file system statistics - from a device vp - used by statfs only
 */
int
xfs_statdevvp(
	statvfs_t	*sp,
	vnode_t		*devvp)
{
	buf_t		*bp;
	int		error;
	__uint64_t	fakeinos;
	xfs_extlen_t	lsize;
	xfs_sb_t	*sbp;

	if (error = devvptoxfs(devvp, &bp, &sbp, u.u_cred))
		return error;
	if (sbp->sb_magicnum == XFS_SB_MAGIC &&
	    XFS_SB_GOOD_VERSION(sbp->sb_versionnum) &&
	    sbp->sb_inprogress == 0) {
		sp->f_bsize = sbp->sb_blocksize;
		sp->f_frsize = sbp->sb_blocksize;
		lsize = sbp->sb_logstart ? sbp->sb_logblocks : 0;
		sp->f_blocks = sbp->sb_dblocks - lsize;
		sp->f_bfree = sp->f_bavail = sbp->sb_fdblocks;
		fakeinos = sp->f_bfree << sbp->sb_inopblog;
		sp->f_files = MIN(sbp->sb_icount + fakeinos, 0xffffffffULL);
		sp->f_ffree = sp->f_favail =
			sp->f_files - (sbp->sb_icount - sbp->sb_ifree);
		sp->f_fsid = devvp->v_rdev;
		(void) strcpy(sp->f_basetype, vfssw[xfs_fstype].vsw_name);
		sp->f_flag = 0;
		sp->f_namemax = MAXNAMELEN - 1;
		bzero(sp->f_fstr, sizeof(sp->f_fstr));
	} else {
		error = EINVAL;
	}
	brelse(bp);
	(void) VOP_CLOSE(devvp, FREAD, L_TRUE, 0, u.u_cred);
	return error;
}

/*
 * xfs_statvfs
 *
 * Fill in the statvfs structure for the given file system.  We use
 * the superblock lock in the mount structure to ensure a consistent
 * snapshot of the counters returned.
 */
/*ARGSUSED*/
STATIC int
xfs_statvfs(
	vfs_t		*vfsp,
	statvfs_t	*statp,
	vnode_t		*vp)
{
	__uint64_t	fakeinos;
	xfs_extlen_t	lsize;
	xfs_mount_t	*mp;
	xfs_sb_t	*sbp;
	int		s;

	mp = XFS_VFSTOM(vfsp);
	sbp = &(mp->m_sb);

	s = XFS_SB_LOCK(mp);
	statp->f_bsize = sbp->sb_blocksize;
	statp->f_frsize = sbp->sb_blocksize;
	lsize = sbp->sb_logstart ? sbp->sb_logblocks : 0;
	statp->f_blocks = sbp->sb_dblocks - lsize;
	statp->f_bfree = statp->f_bavail = sbp->sb_fdblocks;
	fakeinos = statp->f_bfree << sbp->sb_inopblog;
	statp->f_files = MIN(sbp->sb_icount + fakeinos, 0xffffffffULL);
	statp->f_ffree = statp->f_favail =
		statp->f_files - (sbp->sb_icount - sbp->sb_ifree);
	statp->f_flag = vf_to_stf(vfsp->vfs_flag);
	XFS_SB_UNLOCK(mp, s);

	statp->f_fsid = mp->m_dev;
	(void) strcpy(statp->f_basetype, vfssw[xfs_fstype].vsw_name);
	statp->f_namemax = MAXNAMELEN - 1;
	bcopy((char *)&(mp->m_sb.sb_uuid), statp->f_fstr, sizeof(uuid_t));
	bzero(&(statp->f_fstr[sizeof(uuid_t)]),
	      (sizeof(statp->f_fstr) - sizeof(uuid_t)));

	return 0;
}


/*
 * xfs_sync flushes any pending I/O to file system vfsp.
 *
 * This routine is called by vfs_sync() to make sure that things make it
 * out to disk eventually, on sync() system calls to flush out everything,
 * and when the file system is unmounted.  For the vfs_sync() case, all
 * we really need to do is sync out the log to make all of our meta-data
 * updates permanent (except for timestamps).  For calls from pflushd(),
 * dirty pages are kept moving by calling pdflush() on the inodes
 * containing them.  We also flush the inodes that we can lock without
 * sleeping and the superblock if we can lock it without sleeping from
 * vfs_sync() so that items at the tail of the log are always moving out.
 *
 * Flags:
 *      SYNC_BDFLUSH - We're being called from vfs_sync() so we don't want
 *		       to sleep if we can help it.  All we really need
 *		       to do is ensure that the log is synced at least
 *		       periodically.  We also push the inodes and
 *		       superblock if we can lock them without sleeping
 *			and they are not pinned.
 *      SYNC_ATTR    - We need to flush the inodes.  If SYNC_BDFLUSH is not
 *		       set, then we really want to lock each inode and flush
 *		       it.
 *      SYNC_WAIT    - All the flushes that take place in this call should
 *		       be synchronous.
 *      SYNC_DELWRI  - This tells us to push dirty pages associated with
 *		       inodes.  SYNC_WAIT and SYNC_BDFLUSH are used to
 *		       determine if they should be flushed sync, async, or
 *		       delwri.
 *	SYNC_PDFLUSH - Make sure that dirty pages are kept moving by
 *		       calling pdflush() on those inodes that have them.
 *      SYNC_CLOSE   - This flag is passed when the system is being
 *		       unmounted.  We should sync and invalidate everthing.
 *      SYNC_FSDATA  - This indicates that the caller would like to make
 *		       sure the superblock is safe on disk.  We can ensure
 *		       this by simply makeing sure the log gets flushed
 *		       if SYNC_BDFLUSH is set, and by actually writing it
 *		       out otherwise.
 *
 */
/*ARGSUSED*/
STATIC int
xfs_sync(
	vfs_t		*vfsp,
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
	int		restarts;
	uint		lock_flags;
	uint		base_lock_flags;
	uint		log_flags;
	boolean_t	mount_locked;
	boolean_t	vnode_refed;
	xfs_fsize_t	last_byte;
	int		preempt;
	int		i;
	xfs_dinode_t	*dip;
	xfs_buf_log_item_t	*bip;

#define	RESTART_LIMIT	10
#define PREEMPT_MASK	0x7f

	mp = XFS_VFSTOM(vfsp);
	error = 0;
	last_error = 0;
	preempt = 0;
	restarts = 0;

	fflag = B_ASYNC;		/* default is don't wait */
	if (flags & SYNC_BDFLUSH)
		fflag = B_DELWRI;
	if (flags & SYNC_WAIT)
		fflag = 0;		/* synchronous overrides all */

	base_lock_flags = XFS_ILOCK_SHARED;
	if (flags & (SYNC_DELWRI | SYNC_CLOSE | SYNC_PDFLUSH)) {
		/*
		 * We need the I/O lock if we're going to call any of
		 * the flush/inval routines.
		 */
		base_lock_flags |= XFS_IOLOCK_SHARED;
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
	ip = mp->m_inodes;
	do {
		lock_flags = base_lock_flags;

		/*
		 * There were no inodes in the list, just break out
		 * of the loop.
		 */
		if (ip == NULL) {
			break;
		}

		vp = XFS_ITOV(ip);

		/*
		 * We don't mess with swap files from here since it is
		 * too easy to deadlock on memory.
		 */
		if (vp->v_flag & VISSWAP) {
			ip = ip->i_mnext;
			continue;
		}

		/*
		 * If this is just vfs_sync() or pflushd() calling
		 * then we can skip inodes for which it looks like
		 * there is nothing to do.  Since we don't have the
		 * inode locked this is racey, but these are periodic
		 * calls so it doesn't matter.  For the others we want
		 * to know for sure, so we at least try to lock them.
		 */
		if (flags & SYNC_BDFLUSH) {
			if (!(ip->i_item.ili_format.ilf_fields &
			      XFS_ILOG_ALL) &&
			    (ip->i_update_core == 0)) {
				ip = ip->i_mnext;
				continue;
			    }
		} else if (flags & SYNC_PDFLUSH) {
			if (vp->v_dpages == NULL) {
				ip = ip->i_mnext;
				continue;
			}
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
		 * being freed while we reference it.  If we lock the inode
		 * while it's on the mount list here, then the spurious inode
		 * lock in xfs_ireclaim() after the inode is pulled from
		 * the mount list will sleep until we release it here.
		 * This keeps the vnode from being freed while we reference
		 * it.  It is also cheaper and simpler than actually doing
		 * a vn_get() for every inode we touch here.
		 */
		if (xfs_ilock_nowait(ip, lock_flags) == 0) {
			if (flags & SYNC_BDFLUSH) {
				ip = ip->i_mnext;
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
				 * If the caller didn't want to wait
				 * anyway, then only spend so much
				 * time trying to get through
				 * the entire list.  This keeps
				 * us from spending all day here
				 * on busy file systems.
				 */
				if (!(flags & SYNC_WAIT) &&
				    (restarts == RESTART_LIMIT)) {
					XFS_MOUNT_ILOCK(mp);
					break;
				}
				restarts++;
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
			last_byte = xfs_file_last_byte(ip);
			xfs_iunlock(ip, XFS_ILOCK_SHARED);
			if (VN_MAPPED(vp)) {
				remapf(vp, 0, 1);
			}
			pflushinvalvp(vp, 0, last_byte);
			xfs_ilock(ip, XFS_ILOCK_SHARED);

		} else if (flags & SYNC_DELWRI) {
			if (VN_DIRTY(vp)) {
				if (mount_locked) {
					ireclaims = mp->m_ireclaims;
					XFS_MOUNT_IUNLOCK(mp);
					mount_locked = B_FALSE;
				}

				/*
				 * Drop the inode lock since we can't hold it
				 * across calls to the buffer cache.
				 */
				last_byte = xfs_file_last_byte(ip);
				xfs_iunlock(ip, XFS_ILOCK_SHARED);
				error = pflushvp(vp, last_byte, fflag);
				xfs_ilock(ip, XFS_ILOCK_SHARED);
			}

		}

		if (flags & SYNC_PDFLUSH) {
			if (vp->v_dpages) {
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
				pdflush(vp, B_DELWRI);
				xfs_ilock(ip, XFS_ILOCK_SHARED);
			}

		} else if (flags & SYNC_BDFLUSH) {
			if ((flags & SYNC_ATTR) &&
			    ((ip->i_update_core) ||
			     (ip->i_item.ili_format.ilf_fields != 0))) {
				if (mount_locked) {
					ireclaims = mp->m_ireclaims;
					XFS_MOUNT_IUNLOCK(mp);
					mount_locked = B_FALSE;
				}

				/*
				 * We don't want the periodic flushing of the
				 * inodes by vfs_sync() to interfere with
				 * I/O to the file, especially read I/O
				 * where it is only the access time stamp
				 * that is being flushed out.  To prevent
				 * long periods where we have both inode
				 * locks held shared here while reading the
				 * inode's buffer in from disk, we drop the
				 * inode lock while reading in the inode
				 * buffer.  We have to release the buffer
				 * and reacquire the inode lock so that they
				 * are acquired in the proper order (inode
				 * locks first).  The buffer will go at the
				 * end of the lru chain, though, so we can
				 * expect it to still be there when we go
				 * for it again in xfs_iflush().
				 */
				if ((ip->i_pincount == 0) &&
				    xfs_iflock_nowait(ip)) {
					xfs_ifunlock(ip);
					xfs_iunlock(ip, XFS_ILOCK_SHARED);
					error = xfs_inotobp(mp, NULL,
							    ip->i_ino,
							    &dip, &bp);
					if (!error) {
						brelse(bp);
					}

					/*
					 * Since we dropped the inode lock,
					 * the inode may have been reclaimed.
					 * Therefore, we reacquire the mount
					 * lock and start from the beginning
					 * again if any inodes were
					 * reclaimed.
					 */
					XFS_MOUNT_ILOCK(mp);
					if (mp->m_ireclaims != ireclaims) {
						restarts++;
						XFS_MOUNT_IUNLOCK(mp);
						goto loop;
					}
					mount_locked = B_TRUE;
					if (xfs_ilock_nowait(ip,
						    XFS_ILOCK_SHARED) == 0) {
						/*
						 * We failed to reacquire
						 * the inode lock without
						 * sleeping, so just skip
						 * the inode for now.  We
						 * clear the ILOCK bit from
						 * the lock_flags so that we
						 * won't try to drop a lock
						 * we don't hold below.  Drop
						 * the mount lock so that we
						 */
						lock_flags &= ~XFS_ILOCK_SHARED;
					} else if ((ip->i_pincount == 0) &&
						   xfs_iflock_nowait(ip)) {
						/*
						 * Since this is vfs_sync()
						 * calling we only flush the
						 * inode out if we can lock
						 * it without sleeping and
						 * it is not pinned.  Drop
						 * the mount lock here so
						 * that we don't hold it for
						 * too long.
						 */
						ireclaims = mp->m_ireclaims;
						XFS_MOUNT_IUNLOCK(mp);
						mount_locked = B_FALSE;
						error = xfs_iflush(ip,
							   XFS_IFLUSH_DELWRI);
					}
				}

			}

		} else {
			if ((flags & SYNC_ATTR) &&
			    ((ip->i_update_core) ||
			     (ip->i_item.ili_format.ilf_fields != 0))) {
				if (mount_locked) {
					ireclaims = mp->m_ireclaims;
					XFS_MOUNT_IUNLOCK(mp);
					mount_locked = B_FALSE;
				}

				if (flags & SYNC_WAIT) {
					xfs_iflock(ip);
					error = xfs_iflush(ip,
							   XFS_IFLUSH_SYNC);
				} else {
					/*
					 * If we can't acquire the flush
					 * lock, then the inode is already
					 * being flushed so don't bother
					 * waiting.  If we can lock it then
					 * do a delwri flush so we can
					 * combine multiple inode flushes
					 * in each disk write.
					 */
					if (xfs_iflock_nowait(ip)) {
						error = xfs_iflush(ip,
							   XFS_IFLUSH_DELWRI);
					}
				}
			}
		}

		if (lock_flags != 0) {
			xfs_iunlock(ip, lock_flags);
		}
		if (vnode_refed) {
			/*
			 * If we had to take a reference on the vnode
			 * above, then wait until after we've unlocked
			 * the inode to release the reference.  This is
			 * because we can be already holding the inode
			 * lock when VN_RELE() calls xfs_inactive().
			 *
			 * Make sure to drop the mount lock before calling
			 * VN_RELE() so that we don't trip over ourselves if
			 * we have to go for the mount lock again in the
			 * inactive code.
			 */
			if (mount_locked) {
				ireclaims = mp->m_ireclaims;
				XFS_MOUNT_IUNLOCK(mp);
				mount_locked = B_FALSE;
			}
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
				if (!(flags & SYNC_WAIT) &&
				    (restarts == RESTART_LIMIT)) {
					/*
					 * If the caller didn't want to wait
					 * anyway, then only spend so much
					 * time trying to get through
					 * the entire list.  This keeps
					 * us from spending all day here
					 * on busy file systems.
					 */
					break;
				}
				restarts++;
				XFS_MOUNT_IUNLOCK(mp);
				goto loop;
			}
			mount_locked = B_TRUE;
		}

		ip = ip->i_mnext;
	} while (ip != mp->m_inodes);
	if ((restarts == RESTART_LIMIT) && !(flags & SYNC_WAIT) &&
	    (mp->m_inodes != NULL)) {
		/*
		 * We may have missed some of the inodes at the end
		 * of the list.  Rotate the list so that some of the
		 * inodes at the tail of the list are now at the head
		 * so that they get looked at eventually.  This is
		 * actually necessary and not just nice since we depend
		 * on vfs_sync() to push out dirty inodes eventually
		 * as part of guaranteeing that the tail of the log
		 * will move forward eventually.
		 */
		ip = mp->m_inodes;
		for (i = 0; i < 10; i++) {
			ip = ip->i_mprev;
		}
		ASSERT(ip != NULL);
		mp->m_inodes = ip;
	}
	XFS_MOUNT_IUNLOCK(mp);

	/*
	 * Flushing out dirty data above probably generated more
	 * log activity, so if this isn't vfs_sync() then flush
	 * the log again.  If SYNC_WAIT is set then do it synchronously.
	 */
	if (!(flags & SYNC_BDFLUSH)) {
		log_flags = XFS_LOG_FORCE;
		if (flags & SYNC_WAIT) {
			log_flags |= XFS_LOG_SYNC;
		}
		xfs_log_force(mp, (xfs_lsn_t)0, log_flags);
	}

	if (flags & SYNC_FSDATA) {
		/*
		 * If this is vfs_sync() then only sync the superblock
		 * if we can lock it without sleeping and it is not pinned.
		 */
		if (flags & SYNC_BDFLUSH) {
			bp = xfs_getsb(mp, BUF_TRYLOCK);
			if (bp != NULL) {
				bip = (xfs_buf_log_item_t *)bp->b_fsprivate;
				if ((bip != NULL) &&
				    xfs_buf_item_dirty(bip)) {
					if (bp->b_pincount == 0) {
						bp->b_flags |= B_ASYNC;
						error = bwrite(bp);
					} else {
						brelse(bp);
					}
				} else {
					brelse(bp);
				}
			}
		} else {
			bp = xfs_getsb(mp, 0);
			bp->b_flags |= fflag;
			error = bwrite(bp);
		}
		if (error) {
			last_error = error;
		}
	}

	/*
	 * If this is the 30 second sync, then kick some entries out of
	 * the reference cache.  This ensures that idle entries are
	 * eventually kicked out of the cache.
	 */
	if (flags & SYNC_BDFLUSH) {
		xfs_refcache_purge_some();
	}

	return last_error;
}


/*
 * xfs_vget
 */
STATIC int
xfs_vget(
	vfs_t		*vfsp,
	vnode_t		**vpp,
	fid_t		*fidp)
{
        xfs_fid_t	*xfid;
        xfs_inode_t	*ip;
	int		error;

        xfid = (struct xfs_fid *)fidp;
	error = xfs_iget(XFS_VFSTOM(vfsp), NULL, (xfs_ino_t)(xfid->fid_ino),
			 XFS_ILOCK_EXCL, &ip);
	if (error) {
		*vpp = NULL;
		return error;
	}
        if (ip == NULL) {
                *vpp = NULL;
                return XFS_ERROR(EIO);
        }
        if (ip->i_d.di_gen != xfid->fid_gen) {
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
#else	/* SIM */
struct vfsops xfs_vfsops;
#endif	/* !SIM */

















