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
#ident  "$Revision: 1.10 $"

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
#include <sys/grio.h>
#ifdef SIM
#undef _KERNEL
#endif
#include <sys/uuid.h>
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
#include "xfs_alloc_btree.h"
#include "xfs_ialloc.h"
#include "xfs_bmap_btree.h"
#include "xfs_btree.h"
#include "xfs_dinode.h"
#include "xfs_inode_item.h"
#include "xfs_inode.h"

#ifdef SIM
#include "sim.h"
#endif

#define	xfs_vfstosb(vfsp)	(&((xfs_mount_t *)XFS_VFSTOM(vfsp))->m_sb)

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
			     dev_t		rtdev,
			     whymount_t		why,
			     struct xfs_args	*ap,
			     struct cred	*cr,
			     int		lflags);

STATIC xfs_mount_t *_xfs_get_vfsmount(struct vfs	*vfsp,
				      dev_t		ddev,
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

	xfs_type = fstype;

	initnlock(&xfs_strat_lock, "xfsstrat");
	initnsema(&xfs_ancestormon, 1, "xfs_ancestor");
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
		return ENOTBLK;
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
	     dev_t		rtdev,
	     whymount_t		why,
	     struct xfs_args	*ap,
	     struct cred	*cr,
	     int		lflags)
{
	xfs_mount_t	*mp;
	int		error = 0;
	int		s;
	xfs_sb_t	*sbp;

	/*
	 * Remounting a XFS file system is bad. The log manager
	 * automatically handles recovery so no action is required.
	 */
	if (vfsp->vfs_flag & VFS_REMOUNT)
		return 0;
	
	mp = _xfs_get_vfsmount(vfsp, ddev, rtdev);
	if (error = xfs_mountfs(vfsp, ddev)) {
		/*
		 * Clean up. Shouldn't need to worry about
		 * locking stuff.
		 */
		kmem_free(mp, sizeof(*mp));
		return error;		/* error should be in errno.h */
	}

	/*
	 * At this point, the super block has been read into
	 * the mount structure - get the pointer to it.
	 */
	sbp = xfs_buf_to_sbp(mp->m_sb_bp);

	/*
	 * XXX Anything special for root mounts?
	 * if (why == ROOT_INIT) {}
	 */

	/*
	 * XXX Anything special for read-only mounts?
	 * if ((vfsp->vfs_flag & VFS_RDONLY) == 0) {}
	 */

	/*
	 * Call the log's mount-time initialization.
	 * xfs_log_mount() always does XFS_LOG_RECOVER.
	 */
	ASSERT(sbp->sb_logblocks > 0);		/* check for volume case */
	error = xfs_log_mount(mp, ddev, xfs_btod(sbp, sbp->sb_logstart),
			      xfs_btod(sbp, sbp->sb_logblocks), lflags);
	if (error > 0) {
		/*
		 * XXX	log recovery failure - What action should be taken?
		 * 	Translate log error to something user understandable
		 */
		error = EBUSY;		/* XXX log recovery fail - errno? */
	}

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
		  dev_t		rtdev)
{
	xfs_mount_t *mp;

	/*
	 * Check whether dev is already mounted.
	 */
	if (vfsp->vfs_flag & VFS_REMOUNT) {
		mp = XFS_VFSTOM(vfsp);
		(void) xfs_iflush_all(mp);
	} else {
		/*
		 * Allocate VFS private data (xfs mount structure).
		 * XXX Should the xfs_mount_t have a m_devvp for dio?
		 */
		mp = xfs_mount_init();
		mp->m_vfsp = vfsp;
		mp->m_dev = ddev;
		mp->m_rtdev = rtdev;

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
	int		lflags;

	if (!suser())
		return EPERM;
	if (mvp->v_type != VDIR)
		return ENOTDIR;
	if ((uap->flags & MS_REMOUNT) == 0
	    && (mvp->v_count != 1 || (mvp->v_flag & VROOT)))
		return EBUSY;

	/*
	 * Copy in XFS-specific arguments.
	 */
	bzero(&args, sizeof args);
	if (copyin(uap->dataptr, &args, MIN(uap->datalen, sizeof args)))
		return EFAULT;

	lflags = 0;
	if (args.flags & XFSMNT_CHKLOG)
		lflags |= XFS_LOG_RECOVER;

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

		XLV_TAB_LOCK(minor(device), MR_ACCESS);
		xlv_p = &xlv_tab->subvolume[minor(device)];
		if (! XLV_SUBVOL_EXISTS(xlv_p)) {
			XLV_TAB_UNLOCK(minor(device));
			return ENXIO;
		}
		ddev   = (sv_p = xlv_p->vol_p->data_subvol) ? sv_p->dev : 0;
		logdev = (sv_p = xlv_p->vol_p->log_subvol) ? sv_p->dev : 0;
		rtdev  = (sv_p = xlv_p->vol_p->rt_subvol) ? sv_p->dev : 0;
		ASSERT(ddev && logdev);
		XLV_TAB_UNLOCK(minor(device));

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
			return EBUSY;
	}

	error = xfs_cmountfs(vfsp, ddev, rtdev, NONROOT_MOUNT,
			     &args, credp, lflags);

	return error;

} /* xfs_vfsmount() */


/*
 * Clean the given file system.
 * This function should be called when unmounting.
 */
STATIC int
xfs_cleanfs(xfs_sb_t *sbp)
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
		sbp = xfs_buf_to_sbp(bp);
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
		return ENOSYS;
	
	switch (why) {
	  case ROOT_INIT:
		if (xfsrootdone++)
			return EBUSY;
		if (rootdev == NODEV)
			return ENODEV;
		vfsp->vfs_dev = rootdev;
		break;
	  case ROOT_REMOUNT:
		vfs_setflag(vfsp, VFS_REMOUNT);
		break;
	  case ROOT_UNMOUNT:
		/*
		 * XXX copied from efs_mountroot() - Is this right?
		 */
		error = xfs_cleanfs(xfs_vfstosb(vfsp));
		binval(rootdev);
		pflushinvalvfsp(vfsp);
		return error;
	}
	error = vfs_lock(vfsp);
	if (error)
		goto bad;
	error = xfs_cmountfs(vfsp, rootdev, NULL, why, NULL, cr, NULL);
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
	int		s;

	busy = 0;

	s = XFS_MOUNT_ILOCK(mp);

	for (ip = mp->m_inodes; ip && !busy; ip = ip->i_mnext) {
		vp = XFS_ITOV(ip);
		if (vp->v_count != 0) {
			if ((vp->v_count == 1) && (ip == mp->m_rootip))
				continue;
			busy++;
		}
	}
	
	XFS_MOUNT_IUNLOCK(mp, s);

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
	xfs_inode_t	*rip;
	vnode_t		*rvp = 0;
	vmap_t		vmap;

	if (!suser())
		return EPERM;

	mp = XFS_VFSTOM(vfsp);

	/*
	 * Make sure there are no active users.
	 */
	if (_xfs_ibusy(mp))
		return EBUSY;
	
	/*
	 * Get rid of root inode (vnode) first, if we can.
	 *
	 * Does this prevent another process from traversing 
	 * into this file system?
	 */
	rip = mp->m_rootip;
	xfs_ilock(rip, XFS_ILOCK_EXCL);
	xfs_iflock(rip);
	xfs_iflush(rip, 0);		/* synchronously flush to disk */
	rvp = XFS_ITOV(rip);
	if (rvp->v_count != 1) {
		xfs_iunlock(rip, XFS_ILOCK_EXCL);
		return EBUSY;
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
	xfs_unmountfs(mp);	

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
	statp->f_blocks = sbp->sb_dblocks;
	statp->f_bfree = sbp->sb_fdblocks;
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
 * Flags: from efs_sync()'s
 *      SYNC_BDFLUSH - do NOT sleep waiting for an inode - also, when
 *                      when pushing DELWRI - only push old ones.
 *      SYNC_ATTR    - sync attributes - note that ordering considerations
 *                      dictate that we also flush dirty pages
 *      SYNC_WAIT    - do synchronouse writes - inode & delwri
 *      SYNC_DELWRI  - look at inodes w/ delwri pages. Other flags
 *                      decide how to deal with them.
 *      SYNC_CLOSE   - flush delwri and invalidate others.
 *      SYNC_FSDATA  - push fs data (e.g. superblocks)
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
	int		error;
	int		fflag;

	mp = XFS_VFSTOM(vfsp);

	error = 0;

	/*
	 * if not interested in inodes, skip all this
	 */
	if ((flags & (SYNC_DELWRI|SYNC_ATTR)) == 0)
		goto end;

	/*
	 * XXX if (flags & SYNC_CLOSE) { pflushinvalvp(vp, 0, ???); }
	 */

	/*
	 * Should not need to flush the log cuz flushing the
	 * buffers should do the trick.
	 *
	 * Synchronize inodes
	 *
	 * xfs_iflush() only flushes dirty inode so don't need
	 * to check if the inode is dirty.
	 */
	fflag = B_ASYNC;		/* default is don't wait */
	if (flags & SYNC_BDFLUSH)
		fflag = B_DELWRI;
	if (flags & SYNC_WAIT)
		fflag = 0;		/* synchronous overrides all */
	for (ip = mp->m_inodes; ip; ip = ip->i_mnext) {
		xfs_ilock(ip, XFS_ILOCK_EXCL);
		xfs_iflock(ip);
		xfs_iflush(ip, B_DELWRI);
		xfs_iunlock(ip, XFS_ILOCK_EXCL);
	}

	/*
	 * Done with inodes and their data.  Push the superblock.
	 */
end:
#ifndef SIM
	if (flags & SYNC_FSDATA) {
		/*
		 * get and lock the buffer for the superblock
		 */
		ASSERT(mp->m_sb_bp != NULL);
		bp = mp->m_sb_bp;
		psema(&bp->b_lock, PRIBIO);
		ASSERT(bp->b_flags & B_DONE);

		/*
		 * write to disk
		 */
		bp->b_flags &= ~(B_DONE | B_READ);
		bp->b_flags |= B_WRITE;
		bdstrat(bmajor(mp->m_dev), bp);
		error = iowait(bp);
		ASSERT(error == 0);

		vsema(&bp->b_lock);
	}
#endif

	return error;
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
                return EIO;
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
