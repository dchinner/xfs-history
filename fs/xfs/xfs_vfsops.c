/*
 * XFS filesystem operations.
 *
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
#ident  "$Revision$"

#include <xfs_os_defs.h>

#ifndef SIM
#define FSID_T
#include <linux/xfs_to_linux.h>
#include <linux/fs.h>
#include <linux/linux_to_xfs.h>
#else
#define _KERNEL	1
#endif
#include "xfs_buf.h"
#include <sys/vfs.h>
#include <sys/vnode.h>
#include <sys/uuid.h>
#include <sys/grio.h>
#include <sys/dmi_kern.h>
#include <sys/kmem.h>
#ifdef SIM
#undef _KERNEL
#endif
#include <sys/cmn_err.h>
#include <sys/debug.h>
#include <sys/fs_subr.h>
#include <sys/kabi.h>
#include <sys/sysmacros.h>
#include <ksys/vfile.h>
#ifdef SIM
#include <stdio.h>
#else
#include <sys/systm.h>
#include <sys/conf.h>
#endif
#include <sys/major.h>
#include <sys/mount.h>
#include <sys/param.h>
#include <linux/xfs_sema.h>
#include <sys/statvfs.h>
#include <sys/uio.h>
#include <sys/dirent.h>
#include <sys/ktrace.h>
#include <sys/xlate.h>
#include <sys/capability.h>

#include "xfs_macros.h"
#include "xfs_types.h"
#include "xfs_inum.h"
#include "xfs_log.h"
#include "xfs_trans.h"
#include "xfs_sb.h"
#include "xfs_dir.h"
#include "xfs_dir2.h"
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
#include "xfs_dir2_sf.h"
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
#include "xfs_quota.h"
#include "xfs_dmapi.h"
#include "xfs_dir2_trace.h"

#include "xfs_clnt.h"
#include "xfs_cxfs.h"
#include "xfs_utils.h"

#ifdef SIM
#include "sim.h"
#endif
#include <fs/fs_bhv_id.h>
#include <fs/specfs/spec_lsnode.h>

#ifndef SIM
#define	whymount_t	whymountroot_t
#define	NONROOT_MOUNT	ROOT_UNMOUNT

static char *whymount[] = { "initial mount", "remount", "unmount" };

/*
 * prototype for xlv_get_subcolumes: should go into an
 * "XLV exported functions" file (maybe the one that lists ioctls).
 */
int
xlv_get_subvolumes(
	dev_t		device,
	dev_t		*ddev,
	dev_t		*logdev,
	dev_t		*rtdev
);

/*
 * Static function prototypes.
 */
STATIC int
xfs_vfsmount(
	vfs_t		*vfsp,
	vnode_t		*mvp,
	struct mounta	*uap,
	char		*attrs,
	cred_t		*credp);

STATIC int
xfs_rootinit(
	vfs_t		*vfsp);


STATIC int
xfs_vfsmountroot(
	bhv_desc_t		*bdp,
	enum whymountroot	why);

STATIC int
xfs_unmount(
	bhv_desc_t	*bdp,
	int	flags,
	cred_t	*credp);

STATIC int
xfs_root(
	bhv_desc_t	*bdp,
	vnode_t	**vpp);

STATIC int
xfs_statvfs(
	bhv_desc_t	*bdp,
	statvfs_t	*statp,
	vnode_t		*vp);

STATIC int
xfs_sync(
	bhv_desc_t	*bdp,
	int		flags,
	cred_t		*credp);

STATIC int
xfs_cmountfs(
	vfs_t		*vfsp,
	dev_t		ddev,
	dev_t		logdev,
	dev_t		rtdev,
	whymount_t	why,
	struct xfs_args	*ap,
	struct mounta	*args,
	struct cred	*cr);

STATIC xfs_mount_t *
xfs_get_vfsmount(
	vfs_t	*vfsp,
	dev_t	ddev,
	dev_t	logdev,
	dev_t	rtdev);

extern int
spectodevs(
	struct super_block *sb,
	struct xfs_args *args,
	dev_t	*ddevp,
	dev_t   *logdevp,
        dev_t   *rtdevp);

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
xfs_init(int	fstype)
{
	extern void	xfs_start_daemons(void);
#ifndef SIM
#ifdef DATAPIPE
	extern void     fspeinit(void);
#endif
#endif
	extern mutex_t	xfs_refcache_lock;
	extern int	xfs_refcache_size;
	extern int	ncsize;
	extern int	xfs_refcache_percent;
	extern xfs_inode_t	**xfs_refcache;
	extern zone_t	*xfs_da_state_zone;
	extern zone_t	*xfs_bmap_free_item_zone;
	extern zone_t	*xfs_btree_cur_zone;
	extern zone_t	*xfs_inode_zone;
	extern zone_t	*xfs_chashlist_zone;
	extern zone_t	*xfs_trans_zone;
	extern zone_t	*xfs_buf_item_zone;
#if XFS_BIG_FILESYSTEMS
	extern zone_t	*xfs_buf_item64_zone;
#endif
	extern zone_t	*xfs_efd_zone;
	extern zone_t	*xfs_efi_zone;
	extern zone_t	*xfs_dabuf_zone;
#ifndef SIM
	extern lock_t	xfs_strat_lock;
	extern lock_t	xfsd_lock;
	extern sv_t	xfsd_wait;
	extern lock_t	xfsc_lock;
	extern sv_t	xfsc_wait;
	extern mutex_t	xfs_ancestormon;
	extern mutex_t	xfs_uuidtabmon;
	extern mutex_t	xfs_Gqm_lock;
	extern zone_t	*xfs_gap_zone;
#ifdef DEBUG
	extern ktrace_t	*xfs_alloc_trace_buf;
	extern ktrace_t	*xfs_bmap_trace_buf;
	extern ktrace_t	*xfs_bmbt_trace_buf;
	extern ktrace_t	*xfs_strat_trace_buf;
	extern ktrace_t	*xfs_dir_trace_buf;
	extern ktrace_t	*xfs_attr_trace_buf;
	extern ktrace_t	*xfs_dir2_trace_buf;
#endif	/* DEBUG */
#endif	/* !SIM */
#ifdef XFS_DABUF_DEBUG
	extern lock_t	xfs_dabuf_global_lock;
#endif

	xfs_fstype = fstype;

#ifdef XFS_DABUF_DEBUG
	spinlock_init(&xfs_dabuf_global_lock, "xfsda");
#endif
#ifndef SIM
	spinlock_init(&xfs_strat_lock, "xfsstrat");
	mutex_init(&xfs_ancestormon, MUTEX_DEFAULT, "xfs_ancestor");
	mutex_init(&xfs_uuidtabmon, MUTEX_DEFAULT, "xfs_uuidtab");
	spinlock_init(&xfsd_lock, "xfsd");
	sv_init(&xfsd_wait, SV_DEFAULT, "xfsd");
	spinlock_init(&xfsc_lock, "xfsc");
	sv_init(&xfsc_wait, SV_DEFAULT, "xfsc");
	mutex_init(&xfs_Gqm_lock, MUTEX_DEFAULT, "xfs_qmlock");

	/*
	 * Initialize the inode reference cache.
	 * Enforce (completely arbitrary) miminum size of 100.
	 */
	if (ncsize == 0) {
		xfs_refcache_size = 100;
	} else {
		xfs_refcache_size = (ncsize * xfs_refcache_percent) / 100;

		if (xfs_refcache_size < 100)
			xfs_refcache_size = 100;
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
	xfs_gap_zone = kmem_zone_init(sizeof(xfs_gap_t), "xfs_gap");
#endif
	xfs_da_state_zone =
		kmem_zone_init(sizeof(xfs_da_state_t), "xfs_da_state");
	xfs_dabuf_zone = kmem_zone_init(sizeof(xfs_dabuf_t), "xfs_dabuf");

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
	xfs_ili_zone = kmem_zone_init(sizeof(xfs_inode_log_item_t), "xfs_ili");
	xfs_chashlist_zone = kmem_zone_init(sizeof(xfs_chashlist_t),
					    "xfs_chashlist");

	/*
	 * Allocate global trace buffers.
	 */
#ifdef XFS_ALLOC_TRACE
	xfs_alloc_trace_buf = ktrace_alloc(XFS_ALLOC_TRACE_SIZE, 0);
#endif
#ifdef XFS_BMAP_TRACE
	xfs_bmap_trace_buf = ktrace_alloc(XFS_BMAP_TRACE_SIZE, 0);
#endif
#ifdef XFS_BMBT_TRACE
	xfs_bmbt_trace_buf = ktrace_alloc(XFS_BMBT_TRACE_SIZE, 0);
#endif
#ifdef XFS_STRAT_TRACE
	xfs_strat_trace_buf = ktrace_alloc(XFS_STRAT_GTRACE_SIZE, 0);
#endif
#ifdef XFS_DIR_TRACE
	xfs_dir_trace_buf = ktrace_alloc(XFS_DIR_TRACE_SIZE, 0);
#endif
#ifdef XFS_ATTR_TRACE
	xfs_attr_trace_buf = ktrace_alloc(XFS_ATTR_TRACE_SIZE, 0);
#endif
#ifdef XFS_DIR2_TRACE
	xfs_dir2_trace_buf = ktrace_alloc(XFS_DIR2_GTRACE_SIZE, 0);
#endif

	xfs_dir_startup();
	
#ifndef SIM
#ifdef DATAPIPE
	fspeinit();
#endif /* DATAPIPE */

#endif

#ifdef CELL_CAPABLE
	/*
         * Special initialization for cxfs
	 */
	cxfs_arrinit();
#endif

	/*
	 * The inode hash table is created on a per mounted
	 * file system bases.
	 */
	return 0;
}


#ifndef SIM

/*
 * xfs_fill_buftarg
 *
 * Put the "appropriate" things in a buftarg_t structure.
 */
STATIC
void
xfs_fill_buftarg(buftarg_t *btp, dev_t dev, struct super_block *sb)
{
	extern struct inode *linvfs_make_inode(kdev_t, struct super_block *);

	btp->inode = linvfs_make_inode(MKDEV(emajor(dev), eminor(dev)), sb);
	btp->dev    = dev;
}

#ifndef SIM
extern int	kmem_cache_destroy(zone_t *);

void
xfs_cleanup(void)
{
	extern zone_t	*xfs_bmap_free_item_zone;
	extern zone_t	*xfs_btree_cur_zone;
	extern zone_t	*xfs_inode_zone;
	extern zone_t	*xfs_trans_zone;
	extern zone_t	*xfs_gap_zone;
	extern zone_t	*xfs_da_state_zone;
	extern zone_t	*xfs_dabuf_zone;
	extern zone_t	*xfs_efd_zone;
	extern zone_t	*xfs_efi_zone;
	extern zone_t	*xfs_buf_item_zone;
	extern zone_t	*xfs_chashlist_zone;

	kmem_cache_destroy(xfs_bmap_free_item_zone);
	kmem_cache_destroy(xfs_btree_cur_zone);
	kmem_cache_destroy(xfs_inode_zone);
	kmem_cache_destroy(xfs_trans_zone);
	kmem_cache_destroy(xfs_gap_zone);
	kmem_cache_destroy(xfs_da_state_zone);
	kmem_cache_destroy(xfs_dabuf_zone);
	kmem_cache_destroy(xfs_buf_item_zone);
	kmem_cache_destroy(xfs_efd_zone);
	kmem_cache_destroy(xfs_efi_zone);
	kmem_cache_destroy(xfs_ifork_zone);
	kmem_cache_destroy(xfs_ili_zone);
	kmem_cache_destroy(xfs_chashlist_zone);

}
#endif

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
	struct mounta	*args,
	struct cred	*cr)
{
	xfs_mount_t	*mp;
	vnode_t 	*ddevvp = NULL, *rdevvp = NULL, *ldevvp = NULL;
	int		error = 0;
	int		vfs_flags;
	size_t		n;
	char		*tmp_fsname_buffer;
        int             client = 0;
	extern void linvfs_release_inode(struct inode *);

	/*
	 * The new use of remout to update various cxfs parameters
	 * should of already been picked of before this.  If anything
         * has gotten by, it shouldn't have.
	 */
        ASSERT((vfsp->vfs_flag & VFS_REMOUNT) == 0); 
	if (vfsp->vfs_flag & VFS_REMOUNT) 
	                return 0;

        mp = xfs_get_vfsmount(vfsp, ddev, logdev, rtdev);

	/*
 	 * Open the data and real time devices now.
	 */

	vfs_flags = (vfsp->vfs_flag & VFS_RDONLY) ? FREAD : FREAD|FWRITE;
	xfs_fill_buftarg(&mp->m_ddev_targ, ddev, vfsp->vfs_super);
	xfs_fill_buftarg(&mp->m_logdev_targ, logdev, vfsp->vfs_super);
	xfs_fill_buftarg(&mp->m_rtdev_targ, rtdev, vfsp->vfs_super);
	mp->m_ddev_targp = &mp->m_ddev_targ;

	/* Values are in BBs */
	if ((ap != NULL) && (ap->version >= 2) && 
	    (ap->flags & XFSMNT_NOALIGN) != XFSMNT_NOALIGN) {
		/*
		 * At this point the superblock has not been read
		 * in, therefore we do not know the block size.
		 * Before, the mount call ends we will convert
		 * these to FSBs.
		 */
		mp->m_dalign = ap->sunit;
		mp->m_swidth = ap->swidth;
	} else {
		mp->m_dalign = 0;
		mp->m_swidth = 0;
	}
	if (logdev != 0) {
		if (logdev == ddev) {
			ldevvp = NULL;
			mp->m_logdev_targ = mp->m_ddev_targ;
		} else {
			/* Ensure that logdev isn't already mounted */
			if (vfs_devsearch(logdev, VFS_FSTYPE_ANY) != NULL) {
				error = XFS_ERROR(EBUSY);
				goto error3;
			}

			/* Set the log device's block size */
			set_blocksize(logdev, 512);
		}
		if (ap != NULL && ap->version != 0) {
			/* Called through the mount system call */
			if ((ap->version < 1) || (ap->version > 4)) {
				error = XFS_ERROR(EINVAL);
				goto error3;
			}
			if (ap->logbufs != 0 &&
			    ap->logbufs != -1 &&
			    (ap->logbufs < 2 || ap->logbufs > 8)) {
				cmn_err(CE_WARN,
					"xfs: invalid logbufs value");
				error = XFS_ERROR(EINVAL);
				goto error3;
			}
			mp->m_logbufs = ap->logbufs;
			if (ap->logbufsize != -1 &&
			    ap->logbufsize != 16 * 1024 &&
			    ap->logbufsize != 32 * 1024) {
				cmn_err(CE_WARN, "xfs: invalid logbufsize");
				error = XFS_ERROR(EINVAL);
				goto error3;
			}
			mp->m_logbsize = ap->logbufsize;
			mp->m_fsname_len = strlen(args->spec) + 1;
			mp->m_fsname = kmem_alloc(mp->m_fsname_len, KM_SLEEP);
			strcpy(mp->m_fsname, args->spec);
		} else {
			/*
			 * Called through vfs_mountroot/xfs_mountroot.
			 * Or, called by mount with no args structure.
			 * In this case use the dev number (in hex) for 
			 * the filesystem name.
			 */
			mp->m_logbufs = -1;
			mp->m_logbsize = -1;
			mp->m_fsname_len = ap ? 11 : 2;
			mp->m_fsname = kmem_alloc(mp->m_fsname_len, KM_SLEEP);
			if (ap)
				sprintf(mp->m_fsname, "0x%x", ddev);
			else
				strcpy(mp->m_fsname, "/");
		}
	} else {
		ldevvp = NULL;
	}
	if (rtdev != 0) {
		if (rtdev == ddev || rtdev == logdev) {
			cmn_err(CE_WARN, "XFS: Cannot mount filesystem with identical rtdev and logdev.");
			error = XFS_ERROR(EINVAL);
			goto error3;
		} else {
			/* Ensure that rtdev isn't already mounted */
			if (vfs_devsearch(rtdev, VFS_FSTYPE_ANY) != NULL) {
				error = XFS_ERROR(EBUSY);
				goto error3;
			}

			/* Set the realtime device's block size */
			set_blocksize(rtdev, 512);
		}
	}
	/*
	 * Pull in the 'wsync' and 'ino64' mount options before we do the real
	 * work of mounting and recovery.  The arg pointer will
	 * be NULL when we are being called from the root mount code.
	 */
#if XFS_BIG_FILESYSTEMS
	mp->m_inoadd = 0;
#endif
	if (ap != NULL) {
		if (ap->flags & XFSMNT_WSYNC)
			mp->m_flags |= XFS_MOUNT_WSYNC;
#if XFS_BIG_FILESYSTEMS
		if (ap->flags & XFSMNT_INO64) {
			mp->m_flags |= XFS_MOUNT_INO64;
			mp->m_inoadd = XFS_INO64_OFFSET;
		}
#endif
		if (ap->flags & XFSMNT_NOATIME)
			mp->m_flags |= XFS_MOUNT_NOATIME;
		
		if (ap->flags & (XFSMNT_UQUOTA | XFSMNT_PQUOTA | 
				 XFSMNT_QUOTAMAYBE)) 
			xfs_qm_mount_quotainit(mp, ap->flags);
		
		if (ap->flags & XFSMNT_RETERR)
			mp->m_flags |= XFS_MOUNT_RETERR;

		if (ap->flags & XFSMNT_NOALIGN)
			mp->m_flags |= XFS_MOUNT_NOALIGN;

		if (ap->flags & XFSMNT_OSYNCISDSYNC)
			mp->m_flags |= XFS_MOUNT_OSYNCISDSYNC;

		if (ap->flags & XFSMNT_IOSIZE) {
			if (ap->version < 3 ||
			    ap->iosizelog > XFS_MAX_IO_LOG ||
			    ap->iosizelog < XFS_MIN_IO_LOG) {
				cmn_err(CE_WARN, "xfs: invalid log iosize");
				error = XFS_ERROR(EINVAL);
				goto error3;
			}

			mp->m_flags |= XFS_MOUNT_DFLT_IOSIZE;
			mp->m_readio_log = mp->m_writeio_log = ap->iosizelog;
		}

		/*
		 * no recovery flag requires a read-only mount
		 */
		if (ap->flags & XFSMNT_NORECOVERY) {
			if (!(vfsp->vfs_flag & VFS_RDONLY)) {
				cmn_err(CE_WARN, "xfs: tried to mount a FS read-write without recovery!");
				error = XFS_ERROR(EINVAL);
				goto error3;
			}
			mp->m_flags |= XFS_MOUNT_NORECOVERY;
		}
	}

	/*
	 * read in superblock to check read-only flags and shared
	 * mount status
	 */
	if (error = xfs_readsb(mp, ddev)) {
		goto error3;
	}

	/*
	 * prohibit r/w mounts of read-only filesystems
	 */
	if (mp->m_sb.sb_flags & XFS_SBF_READONLY &&
	    !(vfsp->vfs_flag & VFS_RDONLY)) {
		cmn_err(CE_WARN, "xfs: cannot mount a read-only filesystem as read-write");
		error = XFS_ERROR(EROFS);
		xfs_freesb(mp);
		goto error3;
	}

	/*
	 * check for shared mount.
	 */
	if (ap && ap->flags & XFSMNT_SHARED) {
		if (!XFS_SB_VERSION_HASSHARED(&mp->m_sb)) {
			error = XFS_ERROR(EINVAL);
			xfs_freesb(mp);
			goto error3;
		}

		/*
		 * For 6.5, shared mounts must have
		 * the shared version bit set, have the persistent readonly
		 * field set, must be version 0 and can only be mounted
		 * read-only.
		 */
		if (!(vfsp->vfs_flag & VFS_RDONLY) ||
		    !(mp->m_sb.sb_flags & XFS_SBF_READONLY) ||
		    mp->m_sb.sb_shared_vn != 0) {
			error = XFS_ERROR(EINVAL);
			xfs_freesb(mp);
			goto error3;
		}

		mp->m_flags |= XFS_MOUNT_SHARED;

		/*
		 * Shared XFS V0 can't deal with DMI.  Return EINVAL.
		 */
		if (mp->m_sb.sb_shared_vn == 0 && (args->flags & MS_DMI)) {
			error = XFS_ERROR(EINVAL);
			xfs_freesb(mp);
			goto error3;
		}
	}

	/* Default to local- cxfs_arrmount will change this if necessary. */
	mp->m_cxfstype = XFS_CXFS_NOT;

#ifdef CELL_CAPABLE
	error = cxfs_mount(mp, ap, ddev, &client);
#endif
	if (error) {
		goto error3;
	}

	if (client == 0) {
		if (error = xfs_mountfs(vfsp, mp, ddev)) {
#ifdef DEBUG
			cmn_err(CE_WARN, "xfs: xfs_mountfs failed: error %d.",
				error);
#endif
			goto error3;
		}
	}

	return error;

	/*
	 * Be careful not to clobber the value of 'error' here.
	 */
 error3:
	if (ldevvp) {
		xfs_binval(mp->m_logdev_targ);
		linvfs_release_inode(mp->m_logdev_targ.inode);
	}
	if (rdevvp) {
		xfs_binval(mp->m_rtdev_targ);
		linvfs_release_inode(mp->m_rtdev_targ.inode);
	}
	if (ddevvp) {
		xfs_binval(mp->m_ddev_targ);
		linvfs_release_inode(mp->m_ddev_targ.inode);
	}
	if (error) {
#ifdef CELL_CAPABLE
	        cxfs_unmount(mp);
#endif
		xfs_mount_free(mp, 1);
	}
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
	 * Allocate VFS private data (xfs mount structure).
	 */
	mp = xfs_mount_init();
	mp->m_dev    = ddev;
	mp->m_logdev = logdev;
	mp->m_rtdev  = rtdev;

	vfsp->vfs_flag |= VFS_NOTRUNC|VFS_LOCAL;
	/* vfsp->vfs_bsize filled in later from superblock */
	vfsp->vfs_fstype = xfs_fstype;
	vfs_insertbhv(vfsp, &mp->m_bhv, &xfs_vfsops, mp);
	vfsp->vfs_dev = ddev;
	/* vfsp->vfs_fsid is filled in later from superblock */

	return mp;
}	/* end of xfs_get_vfsmount() */

#if _MIPS_SIM == _ABI64
/*
 * xfs_args_ver_1
 * 
 * This is used with copyin_xlate() to copy a xfs_args version 1 structure
 * in from user space from a 32 bit application into a 64 bit kernel.
 */
/*ARGSUSED*/
int
xfs_args_to_ver_1(
	enum xlate_mode	mode,
	void		*to,
	int		count,
	xlate_info_t	*info)
{
	COPYIN_XLATE_PROLOGUE(xfs_args32_ver_1, xfs_args);

	target->version = source->version;
	target->flags = source->flags;
	target->logbufs = source->logbufs;
	target->logbufsize = source->logbufsize;
	target->fsname = (char*)(__psint_t)source->fsname;

	return 0;
}

/*
 * xfs_args_to_ver_2 
 * 
 * This is used with copyin_xlate() to copy a xfs_args version 2 structure
 * in from user space from a 32 bit application into a 64 bit kernel.
 */
/*ARGSUSED*/
int
xfs_args_to_ver_2(
	enum xlate_mode	mode,
	void		*to,
	int		count,
	xlate_info_t	*info)
{
	COPYIN_XLATE_PROLOGUE(xfs_args32_ver_2, xfs_args);

	target->version = source->version;
	target->flags = source->flags;
	target->logbufs = source->logbufs;
	target->logbufsize = source->logbufsize;
	target->fsname = (char*)(__psint_t)source->fsname;
	target->sunit = source->sunit;
	target->swidth = source->swidth;

	return 0;
}

/*
 * xfs_args_to_ver_3 
 * 
 * This is used with copyin_xlate() to copy a xfs_args version 3 structure
 * in from user space from a 32 bit application into a 64 bit kernel.
 */
/*ARGSUSED*/
int
xfs_args_to_ver_3(
	enum xlate_mode	mode,
	void		*to,
	int		count,
	xlate_info_t	*info)
{
	COPYIN_XLATE_PROLOGUE(xfs_args32_ver_3, xfs_args);

	target->version = source->version;
	target->flags = source->flags;
	target->logbufs = source->logbufs;
	target->logbufsize = source->logbufsize;
	target->fsname = (char*)(__psint_t)source->fsname;
	target->sunit = source->sunit;
	target->swidth = source->swidth;
	target->iosizelog = source->iosizelog;

	return 0;
}

/*
 * xfs_args_to_ver_4 
 * 
 * This is used with copyin_xlate() to copy a xfs_args version 4 structure
 * in from user space from a 32 bit application into a 64 bit kernel.
 */
/*ARGSUSED*/
int
xfs_args_to_ver_4(
	enum xlate_mode	mode,
	void		*to,
	int		count,
	xlate_info_t	*info)
{
	COPYIN_XLATE_PROLOGUE(xfs_args32_ver_4, xfs_args);

	target->version = source->version;
	target->flags = source->flags;
	target->logbufs = source->logbufs;
	target->logbufsize = source->logbufsize;
	target->fsname = (char*)(__psint_t)source->fsname;
	target->sunit = source->sunit;
	target->swidth = source->swidth;
	target->iosizelog = source->iosizelog;
	target->servlist = (char **)(__psint_t)source->servlist;
	target->servlistlen = (int *)(__psint_t)source->servlistlen;
	target->sunit = source->sunit;
	target->slcount = source->slcount;
	target->stimeout = source->stimeout;
	target->ctimeout = source->ctimeout;
	target->server = (char *)(__psint_t)source->server;
	target->servlen = source->servlen;
	target->servcell = source->servcell;

	return 0;
}
#endif

/*
 * xfs_mountargs -- Copy in xfs mount arguments
 *
 * Does the intialization and copying of xfs mount arguments taking account 
 * of the various xfs argument versions and abi's.
 *
 * The value returned is an error code.
 */
int
xfs_mountargs(
	struct mounta	*uap,
        struct xfs_args *ap)
{
	bzero(ap, sizeof (struct xfs_args));
	ap->slcount = -1;
        ap->stimeout = -1;
	ap->ctimeout = -1;

#define copyin(from, to, len) (memcpy(to, from, len), /*return*/0)

	if (uap->datalen && uap->dataptr) { 

		/* Copy in the xfs_args version number */
		if (copyin(uap->dataptr, ap, sizeof(ap->version)))
			return XFS_ERROR(EFAULT);

		if (ap->version == 1) {
			if (COPYIN_XLATE(uap->dataptr, ap, 
					 sizeof(struct xfs_args_ver_1),
					 xfs_args_to_ver_1, 
					 get_current_abi(), 1))
				return XFS_ERROR(EFAULT);
		} else if (ap->version == 2) {
			if (COPYIN_XLATE(uap->dataptr, ap, 
					 sizeof(struct xfs_args_ver_2),
					 xfs_args_to_ver_2, 
					 get_current_abi(), 1))
				return XFS_ERROR(EFAULT);
		} else if (ap->version == 3) {
			if (COPYIN_XLATE(uap->dataptr, ap, 
					 sizeof(struct xfs_args_ver_3),
					 xfs_args_to_ver_3, 
					 get_current_abi(), 1))
				return XFS_ERROR(EFAULT);
		} else if (ap->version == 4) {
			if (COPYIN_XLATE(uap->dataptr, ap, 
					 sizeof(struct xfs_args),
					 xfs_args_to_ver_4, 
					 get_current_abi(), 1))
				return XFS_ERROR(EFAULT);
		} else
			return XFS_ERROR(EINVAL);
	}

	ap->fsname = uap->spec;
#undef copyin

	return (0);
}

/*
 * xfs_mount
 *
 * The file system configurations are:
 *	(1) device (partition) with data and internal log
 *	(2) logical volume with data and log subvolumes.
 *	(3) logical volume with data, log, and realtime subvolumes.
 *
 */ 
STATIC int
xfs_mount(
	vfs_t		*vfsp,
	vnode_t		*mvp,
	struct mounta	*uap,
	cred_t		*credp)
{
	struct xfs_args	args;			/* xfs mount arguments */
	dev_t		ddev;
	dev_t		logdev;
	dev_t		rtdev;
	vnode_t		*rootvp;
	int		error;

	if (mvp->v_type != VDIR)
		return XFS_ERROR(ENOTDIR);
	if (((uap->flags & MS_REMOUNT) == 0) &&
	    ((mvp->v_count != 1) || (mvp->v_flag & VROOT)))
		return XFS_ERROR(EBUSY);

	/*
	 * Copy in XFS-specific arguments.
	 */
	error = xfs_mountargs(uap, &args);
	if (error)
		return (error);

	if (error = spectodevs(vfsp->vfs_super, &args, &ddev, &logdev, &rtdev))
		return error;

	/*
	 * Ensure that this device isn't already mounted,
	 * unless this is a REMOUNT request
	 */
        if (vfs_devsearch(ddev, VFS_FSTYPE_ANY) == NULL) {
		ASSERT((uap->flags & MS_REMOUNT) == 0);
	} else {
		if ((uap->flags & MS_REMOUNT) == 0)
			return XFS_ERROR(EBUSY);
	}

	error = xfs_cmountfs(vfsp, ddev, logdev, rtdev, NONROOT_MOUNT,
			     &args, uap, credp);
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
		VFS_ROOT(vfsp, &rootvp, error);
		if (error == 0) {
			bhv_desc_t	*mbdp, *rootbdp;

			mbdp = vn_bhv_lookup_unlocked(VN_BHV_HEAD(mvp), &xfs_vnodeops);
			rootbdp = vn_bhv_lookup_unlocked(VN_BHV_HEAD(rootvp), &xfs_vnodeops);
			VN_RELE(rootvp);
			error = dm_send_mount_event(vfsp, DM_RIGHT_NULL,
						mbdp, DM_RIGHT_NULL,
						rootbdp, DM_RIGHT_NULL,
						uap->dir, uap->spec);
		}
		if (error) {
			/* REFERENCED */
			int	errcode;

			vfsp->vfs_flag &= ~VFS_DMI;
			VFS_UNMOUNT(vfsp, 0, credp, errcode);
			ASSERT (errcode == 0);
		}
	}

	return error;

}	/* end of xfs_mount() */


/* VFS_MOUNT */
/*ARGSUSED*/
STATIC int
xfs_vfsmount(
	vfs_t           *vfsp,
        vnode_t         *mvp,
        struct mounta   *uap,
	char 		*attrs,
        cred_t          *credp)
{
        int		error;

	vfs_lock(vfsp);
	error = xfs_mount(vfsp, mvp, uap, credp);
	vfs_unlock(vfsp);
	return (error);
}

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
	xfs_buf_t	 *bp;
	int	 error;

	printk("DING DING DING... need to implement this once we start doing xfs root\n");
	return 0;
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
	bhv_desc_t		*bdp,
	enum whymountroot	why)
{
	int		error;
	static int	xfsrootdone;
	struct cred	*cr = get_current_cred();
	dev_t		ddev, logdev, rtdev;
	xfs_mount_t	*mp;
	xfs_buf_t		*bp;
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
		mp = XFS_BHVTOM(bdp);
		if (xfs_ibusy(mp)) {
			XFS_bflush(mp->m_ddev_targ);
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
			xfs_sync(bdp,
				 (SYNC_WAIT | SYNC_CLOSE |
				  SYNC_ATTR | SYNC_FSDATA),
				 cr);
			xfs_iflush_all(mp, XFS_FLUSH_ALL);
			if (mp->m_quotainfo) {
				xfs_qm_dqrele_all_inodes(mp, 
							 XFS_UQUOTA_ACCT |
							 XFS_PQUOTA_ACCT);
				xfs_qm_dqpurge_all(mp, 
						   XFS_QMOPT_UQUOTA|
						   XFS_QMOPT_PQUOTA|
						   XFS_QMOPT_UMOUNTING);
			}
			/*
			 * Force the log to unpin as many buffers as
			 * possible and then sync them out.
			 */
			xfs_log_force(mp, (xfs_lsn_t)0,
				      XFS_LOG_FORCE | XFS_LOG_SYNC);
			xfs_binval(mp->m_ddev_targ);
			if (mp->m_rtdev != NODEV) {
				xfs_binval(mp->m_rtdev_targ);
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
			xfs_binval(mp->m_ddev_targ);

			/*
			 * Finally, try to flush out the superblock.  If
			 * it is pinned at this point we can't, because
			 * we don't want to get stuck waiting for it to
			 * be unpinned.  It should be unpinned normally
			 * because of the log flushes above.
			 */
			bp = xfs_getsb(mp, 0);
			if (!(XFS_BUF_ISPINNED(bp))) {
				XFS_BUF_UNDONE(bp);
				XFS_BUF_UNREAD(bp);
				XFS_BUF_WRITE(bp);
				ASSERT(mp->m_dev == XFS_BUF_TARGET(bp));
				xfsbdstrat(mp, bp);
				(void) xfs_iowait(bp);
			}
			return XFS_ERROR(EBUSY);
		}
		error = xfs_unmount(bdp, 0, NULL);
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
		if (xlv_get_subvolumes(rootdev, &ddev, &logdev, &rtdev) != 0) {
			return XFS_ERROR(ENXIO);
		}
	} else {
		/*
		 * block device
		 */
		ddev = logdev = rootdev;
		rtdev = 0;
	}
	ASSERT(ddev && logdev);

	error = xfs_cmountfs(vfsp, ddev, logdev, rtdev, why, NULL, NULL, cr);

	if (error) {
		vfs_unlock(vfsp);
		goto bad;
	}
	vfs_unlock(vfsp);
	return(0);
bad:
	cmn_err(CE_WARN, "%s of root device %V failed with errno %d\n",
		whymount[why], rootdev, error);
	return error;

} /* end of xfs_mountroot() */


/* VFS_ROOTINIT */
STATIC int
xfs_rootinit(
	vfs_t *vfsp)
{
	return(xfs_mountroot(vfsp, NULL, ROOT_INIT));
}

/* VFS_MOUNTROOT */
STATIC int
xfs_vfsmountroot(
	bhv_desc_t *bdp,
	enum whymountroot       why)
{
	return(xfs_mountroot(bhvtovfs(bdp), bdp, why));
}

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
		/* Skip markers inserted by xfs_sync */
		if (ip->i_mount == NULL) {
			ip = ip->i_mnext;
			continue;
		}

		vp = XFS_ITOV(ip);
		if (vp->v_count != 0) {
		  	if (xfs_ibusy_check(ip, vp->v_count) == 0) {
				ip = ip->i_mnext;
				continue;
			}
#ifdef DEBUG
			printk("busy vp=0x%p ip=0x%p inum %Ld count=%d\n",
				vp, ip, ip->i_ino, vp->v_count);
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
 */
/*ARGSUSED*/
STATIC int
xfs_unmount(
	bhv_desc_t	*bdp,
	int	flags,
	cred_t	*credp)
{
	xfs_mount_t	*mp;
	xfs_inode_t	*rip;
	vnode_t		*rvp = 0;
	vmap_t		vmap;
	int		vfs_flags;
	struct vfs 	*vfsp = bhvtovfs(bdp);
	int		unmount_event_wanted = 0;
	int		unmount_event_flags = 0;
	int		xfs_unmountfs_needed = 0;
	int		error;

	mp = XFS_BHVTOM(bdp);
	rip = mp->m_rootip;
	rvp = XFS_ITOV(rip);

#ifndef SIM
	if (vfsp->vfs_flag & VFS_DMI) {
		bhv_desc_t	*rbdp = vn_bhv_lookup_unlocked(VN_BHV_HEAD(rvp), &xfs_vnodeops);

		error = dm_send_namesp_event(DM_EVENT_PREUNMOUNT,
				rbdp, DM_RIGHT_NULL, rbdp, DM_RIGHT_NULL,
				NULL, NULL, 0, 0,
				(mp->m_dmevmask & (1 << DM_EVENT_PREUNMOUNT)) != 0 ?
					0:DM_FLAGS_UNWANTED);
			if (error)
				return XFS_ERROR(error);
		unmount_event_wanted = 1;
		unmount_event_flags = (mp->m_dmevmask & (1 << DM_EVENT_UNMOUNT)) != 0 ?
					0 : DM_FLAGS_UNWANTED;
	}
#endif

	/*
	 * First blow any referenced inode from this file system
	 * out of the reference cache.
	 */
	xfs_refcache_purge_mp(mp);

	/*
	 * Make sure there are no active users.
	 */
	if (xfs_ibusy(mp)) {
		error = XFS_ERROR(EBUSY);
		goto out;
	}
	
	XFS_bflush(mp->m_ddev_targ);
	error = xfs_unmount_flush(mp, 0);
	if (error)
		goto out;

	VMAP(rvp, vmap);
	VN_RELE(rvp);
	vn_purge(rvp, &vmap);

	/*
	 * If we're forcing a shutdown, typically because of a media error,
	 * we want to make sure we invalidate dirty pages that belong to
	 * referenced vnodes as well.
	 */
	if (XFS_FORCED_SHUTDOWN(mp))  {
		error = xfs_sync(&mp->m_bhv,
			 (SYNC_WAIT | SYNC_CLOSE), credp);
		ASSERT(error != EFSCORRUPTED);
	}
	xfs_unmountfs_needed = 1;	

out:
	/*	Send DMAPI event, if required.
	 *	Then do xfs_unmountfs() if needed.
	 *	Then return error (or zero).
	 */
#ifndef SIM
	if (unmount_event_wanted) {
		/* Note: mp structure must still exist for 
		 * dm_send_unmount_event() call.
		 */
		dm_send_unmount_event(vfsp, error == 0 ? rvp : NULL,
			DM_RIGHT_NULL, 0, error, unmount_event_flags);
	}
#endif
	if (xfs_unmountfs_needed) {
		/*
		 * Call common unmount function to flush to disk
		 * and free the super block buffer & mount structures.
		 */
		vfs_flags = (vfsp->vfs_flag & VFS_RDONLY) ? FREAD : FREAD|FWRITE;
		xfs_unmountfs(mp, vfs_flags, credp);
	}
	return XFS_ERROR(error);

}

/*
 * xfs_unmount_flush implements a set of flush operation on special 
 * inodes, which are needed as a separate set of operations so that
 * they can be called as part of relocation process.
 */
int 
xfs_unmount_flush(
	xfs_mount_t	*mp,		/* Mount structure we are getting 
					   rid of. */
	int             relocation)	/* Called from vfs relocation. */
{
	xfs_inode_t	*rip = mp->m_rootip;
        xfs_inode_t     *rbmip;
	xfs_inode_t     *rsumip;
	vnode_t         *rvp = XFS_ITOV(rip);
	int             error;

	xfs_ilock(rip, XFS_ILOCK_EXCL);
	xfs_iflock(rip);

	/*
	 * Flush out the real time inodes.
	 */
	if ((rbmip = mp->m_rbmip) != NULL) {
		xfs_ilock(rbmip, XFS_ILOCK_EXCL);
		xfs_iflock(rbmip);
		error = xfs_iflush(rbmip, XFS_IFLUSH_SYNC);
		xfs_iunlock(rbmip, XFS_ILOCK_EXCL);
		if (error == EFSCORRUPTED)
			goto fscorrupt_out;
		ASSERT(XFS_ITOV(rbmip)->v_count == 1);

		rsumip = mp->m_rsumip;
		xfs_ilock(rsumip, XFS_ILOCK_EXCL);
		xfs_iflock(rsumip);
		error = xfs_iflush(rsumip, XFS_IFLUSH_SYNC);
		xfs_iunlock(rsumip, XFS_ILOCK_EXCL);
		if (error == EFSCORRUPTED)
			goto fscorrupt_out;
		ASSERT(XFS_ITOV(rsumip)->v_count == 1);
	}

	/*
	 * synchronously flush root inode to disk
	 */
	error = xfs_iflush(rip, XFS_IFLUSH_SYNC);
	if (error == EFSCORRUPTED)
		goto fscorrupt_out2;
	if (rvp->v_count != 1 && !relocation) {
		xfs_iunlock(rip, XFS_ILOCK_EXCL);
		error = XFS_ERROR(EBUSY);
		return (error);
	}
	/*
	 * Release dquot that rootinode, rbmino and rsumino might be holding,
	 * flush and purge the quota inodes.
	 */
	error = xfs_qm_unmount_quotas(mp);
	if (error == EFSCORRUPTED)
		goto fscorrupt_out2;

	if (rbmip) {
		VN_RELE(XFS_ITOV(rbmip));
		VN_RELE(XFS_ITOV(rsumip));
	}

	xfs_iunlock(rip, XFS_ILOCK_EXCL);
	return (0);

fscorrupt_out:
	xfs_ifunlock(rip);

fscorrupt_out2:
	xfs_iunlock(rip, XFS_ILOCK_EXCL);

	error = XFS_ERROR(EFSCORRUPTED);
	return (error);
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
	bhv_desc_t	*bdp,
	vnode_t	**vpp)
{
	vnode_t	*vp;

	vp = XFS_ITOV((XFS_BHVTOM(bdp))->m_rootip);	
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
/*ARGSUSED*/
STATIC int
xfs_statvfs(
	bhv_desc_t	*bdp,
	statvfs_t	*statp,
	vnode_t		*vp)
{
	__uint64_t	fakeinos;
	xfs_extlen_t	lsize;
	xfs_mount_t	*mp;
	xfs_sb_t	*sbp;
	int		s;
	struct vfs *vfsp = bhvtovfs(bdp);

	mp = XFS_BHVTOM(bdp);
	sbp = &(mp->m_sb);

	s = XFS_SB_LOCK(mp);
	statp->f_bsize = sbp->sb_blocksize;
	statp->f_frsize = sbp->sb_blocksize;
	lsize = sbp->sb_logstart ? sbp->sb_logblocks : 0;
	statp->f_blocks = sbp->sb_dblocks - lsize;
	statp->f_bfree = statp->f_bavail = sbp->sb_fdblocks;
	fakeinos = statp->f_bfree << sbp->sb_inopblog;
#if XFS_BIG_FILESYSTEMS
	fakeinos += mp->m_inoadd;
#endif
	statp->f_files = MIN(sbp->sb_icount + fakeinos, XFS_MAXINUMBER);
	if (mp->m_maxicount)
#if XFS_BIG_FILESYSTEMS
		if (!mp->m_inoadd)
#endif
			statp->f_files = MIN(statp->f_files, mp->m_maxicount);
	statp->f_ffree = statp->f_favail =
		statp->f_files - (sbp->sb_icount - sbp->sb_ifree);
	XFS_SB_UNLOCK(mp, s);

	statp->f_fsid = mp->m_dev;
	(void) strcpy(statp->f_basetype, "xfs");
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
	bhv_desc_t	*bdp,
	int		flags,
	cred_t		*credp)
{
	xfs_mount_t	*mp;

	mp = XFS_BHVTOM(bdp);
	return (xfs_syncsub(mp, flags, 0, NULL));
}

/*
 * xfs sync routine for internal use
 *
 * This routine supports all of the flags defined for the generic VFS_SYNC
 * interface as explained above under xys_sync.  In the interests of not
 * changing interfaces within the 6.5 family, additional internallly-
 * required functions are specified within a separate xflags parameter,
 * only available by calling this routine.
 *
 * xflags:
 * 	XFS_XSYNC_RELOC - Sync for relocation.  Don't try to get behavior
 *                        locks as this will cause you to hang.  Not all
 *                        combinations of flags are necessarily supported
 *                        when this is specified.
 */
int
xfs_syncsub(
	xfs_mount_t	*mp,
	int		flags,
	int             xflags,
	int             *bypassed)
{
	xfs_inode_t	*ip = NULL;
	xfs_inode_t	*ip_next;
	xfs_buf_t		*bp;
	vnode_t		*vp;
	vmap_t		vmap;
	int		error;
	int		last_error;
	uint64_t	fflag;
	uint		lock_flags;
	uint		base_lock_flags;
	uint		log_flags;
	boolean_t	mount_locked;
	boolean_t	vnode_refed;
	xfs_fsize_t	last_byte;
	int		preempt;
	xfs_dinode_t	*dip;
	xfs_buf_log_item_t	*bip;
	xfs_iptr_t	*ipointer;
#ifdef DEBUG
	boolean_t	ipointer_in = B_FALSE;

#define IPOINTER_SET	ipointer_in = B_TRUE
#define IPOINTER_CLR	ipointer_in = B_FALSE
#else
#define IPOINTER_SET
#define IPOINTER_CLR
#endif


/* Insert a marker record into the inode list after inode ip. The list
 * must be locked when this is called. After the call the list will no
 * longer be locked.
 */
#define IPOINTER_INSERT(ip, mp)	{ \
		ASSERT(ipointer_in == B_FALSE); \
		ipointer->ip_mnext = ip->i_mnext; \
		ipointer->ip_mprev = ip; \
		ip->i_mnext = (xfs_inode_t *)ipointer; \
		ipointer->ip_mnext->i_mprev = (xfs_inode_t *)ipointer; \
		preempt = 0; \
		XFS_MOUNT_IUNLOCK(mp); \
		mount_locked = B_FALSE; \
		IPOINTER_SET; \
	}

/* Remove the marker from the inode list. If the marker was the only item
 * in the list then there are no remaining inodes and we should zero out
 * the whole list. If we are the current head of the list then move the head
 * past us.
 */
#define IPOINTER_REMOVE(ip, mp)	{ \
		ASSERT(ipointer_in == B_TRUE); \
		if (ipointer->ip_mnext != (xfs_inode_t *)ipointer) { \
			ip = ipointer->ip_mnext; \
			ip->i_mprev = ipointer->ip_mprev; \
			ipointer->ip_mprev->i_mnext = ip; \
			if (mp->m_inodes == (xfs_inode_t *)ipointer) { \
				mp->m_inodes = ip; \
			} \
		} else { \
			ASSERT(mp->m_inodes == (xfs_inode_t *)ipointer); \
			mp->m_inodes = NULL; \
			ip = NULL; \
		} \
		IPOINTER_CLR; \
	}

#define PREEMPT_MASK	0x7f

	if (bypassed)
		*bypassed = 0;
	if (XFS_MTOVFS(mp)->vfs_flag & VFS_RDONLY)
		return 0;
	error = 0;
	last_error = 0;
	preempt = 0;

	/* Allocate a reference marker */
	ipointer = (xfs_iptr_t *)kmem_zalloc(sizeof(xfs_iptr_t), KM_SLEEP);

	fflag = XFS_B_ASYNC;		/* default is don't wait */
	if (flags & SYNC_BDFLUSH)
		fflag = XFS_B_DELWRI;
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
	if (flags & SYNC_WAIT) {
		xfs_log_force(mp, (xfs_lsn_t)0, XFS_LOG_FORCE | XFS_LOG_SYNC);
	} else {
		xfs_log_force(mp, (xfs_lsn_t)0, XFS_LOG_FORCE);
	}

	XFS_MOUNT_ILOCK(mp);
	ip = mp->m_inodes;
	mount_locked = B_TRUE;
	vnode_refed = B_FALSE;
	IPOINTER_CLR;

	do {
		ASSERT(ipointer_in == B_FALSE);
		ASSERT(vnode_refed == B_FALSE);
		lock_flags = base_lock_flags;

		/*
		 * There were no inodes in the list, just break out
		 * of the loop.
		 */
		if (ip == NULL) {
			break;
		}

		/*
		 * We found another sync thread marker - skip it
		 */
		if (ip->i_mount == NULL) {
			ip = ip->i_mnext;
			continue;
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

		if (XFS_FORCED_SHUTDOWN(mp) && !(flags & SYNC_CLOSE)) {
			XFS_MOUNT_IUNLOCK(mp);
			kmem_free(ipointer, sizeof(xfs_iptr_t));
			return 0;
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
			if (((ip->i_itemp == NULL) ||
			     !(ip->i_itemp->ili_format.ilf_fields &
			       XFS_ILOG_ALL)) &&
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
			 * to lock the inode. Insert a marker record into
			 * the inode list to remember our position, dropping
			 * the lock is now done inside the IPOINTER_INSERT
			 * macro.
			 *
			 * We also use the inode list lock to protect us
			 * in taking a snapshot of the vnode version number
			 * for use in calling vn_get().
			 */


			VMAP(vp, vmap);
			IPOINTER_INSERT(ip, mp);
			vp = vn_get(vp, &vmap, 0);
			if (vp == NULL) {
				/*
				 * The vnode was reclaimed once we let go
				 * of the inode list lock.  Skip to the
				 * next list entry. Remove the marker.
				 */

				XFS_MOUNT_ILOCK(mp);
				mount_locked = B_TRUE;
				vnode_refed = B_FALSE;
				IPOINTER_REMOVE(ip, mp);
				continue;
			}
			xfs_ilock(ip, lock_flags);
			ASSERT(vp == XFS_ITOV(ip));
			ASSERT(ip->i_mount == mp);
			vnode_refed = B_TRUE;
		}

		/* From here on in the loop we may have a marker record
		 * in the inode list.
		 */

		if (flags & SYNC_CLOSE) {
			/*
			 * This is the shutdown case.  We just need to
			 * flush and invalidate all the pages associated
			 * with the inode.  Drop the inode lock since
			 * we can't hold it across calls to the buffer
			 * cache.
			 *
			 * We don't set the VREMAPPING bit in the vnode
			 * here, because we don't hold the vnode lock
			 * exclusively.  It doesn't really matter, though,
			 * because we only come here when we're shutting
			 * down anyway.
			 */
			last_byte = xfs_file_last_byte(ip);
			xfs_iunlock(ip, XFS_ILOCK_SHARED);
			if (XFS_FORCED_SHUTDOWN(mp)) {
                                if (xflags & XFS_XSYNC_RELOC) {
					fs_tosspages(XFS_ITOBHV(ip), 0,
						     last_byte - 1, FI_REMAPF);
				}
				else {
					VOP_TOSS_PAGES(vp, 0, last_byte - 1, 
					               FI_REMAPF);
				}
			} else {
                                if (xflags & XFS_XSYNC_RELOC) {
					fs_flushinval_pages(XFS_ITOBHV(ip),
							    0, last_byte - 1,
							    FI_REMAPF);
				}
				else {
					VOP_FLUSHINVAL_PAGES(vp, 0, last_byte - 1, 
							     FI_REMAPF);
				}
			}
			xfs_ilock(ip, XFS_ILOCK_SHARED);
		} else if (flags & SYNC_DELWRI) {
			if (VN_DIRTY(vp)) {
				/* We need to have dropped the lock here,
				 * so insert a marker if we have not already
				 * done so.
				 */
				if (mount_locked) {
					IPOINTER_INSERT(ip, mp);
				}

				/*
				 * Drop the inode lock since we can't hold it
				 * across calls to the buffer cache.
				 */
				last_byte = xfs_file_last_byte(ip);
				xfs_iunlock(ip, XFS_ILOCK_SHARED);
				VOP_FLUSH_PAGES(vp, (off_t)0, last_byte - 1,
						fflag, FI_NONE, error);
				xfs_ilock(ip, XFS_ILOCK_SHARED);
			}

		}

		if (flags & SYNC_PDFLUSH) {
			if (vp->v_dpages) {
				/* Insert marker and drop lock if not already
				 * done.
				 */
				if (mount_locked) {
					IPOINTER_INSERT(ip, mp);
				}

				/*
				 * Drop the inode lock since we can't hold it
				 * across calls to the buffer cache.
				 */

				ASSERT(vp->v_type == VREG);
				if (vp->v_type == VREG) {
					xfs_iunlock(ip, XFS_ILOCK_SHARED);
					XFS_pdflush(vp, XFS_B_DELWRI);
					xfs_ilock(ip, XFS_ILOCK_SHARED);
				}
			}

		} else if (flags & SYNC_BDFLUSH) {
			if ((flags & SYNC_ATTR) &&
			    ((ip->i_update_core) ||
			     ((ip->i_itemp != NULL) &&
			      (ip->i_itemp->ili_format.ilf_fields != 0)))) {

				/* Insert marker and drop lock if not already
				 * done.
				 */
				if (mount_locked) {
					IPOINTER_INSERT(ip, mp);
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
					error = xfs_itobp(mp, NULL, ip,
							  &dip, &bp, 0);
					if (!error) {
						xfs_buf_relse(bp);
					} else {
						/* Bailing out, remove the
						 * marker and free it.
						 */
						XFS_MOUNT_ILOCK(mp);
						IPOINTER_REMOVE(ip, mp);
						XFS_MOUNT_IUNLOCK(mp);
						ASSERT(!(lock_flags &
							XFS_IOLOCK_SHARED));
						kmem_free(ipointer,
							sizeof(xfs_iptr_t));
						return (0);
					}

					/*
					 * Since we dropped the inode lock,
					 * the inode may have been reclaimed.
					 * Therefore, we reacquire the mount
					 * lock and check to see if we were the
					 * inode reclaimed. If this happened
					 * then the ipointer marker will no
					 * longer point back at us. In this
					 * case, move ip along to the inode
					 * after the marker, remove the marker
					 * and continue.
					 */
					XFS_MOUNT_ILOCK(mp);
					mount_locked = B_TRUE;
					if (ip != ipointer->ip_mprev) {
						IPOINTER_REMOVE(ip, mp);
						ASSERT(!vnode_refed);
						ASSERT(!(lock_flags &
							XFS_IOLOCK_SHARED));
						continue;
					}

					ASSERT(ip->i_mount == mp);
					if (xfs_ilock_nowait(ip,
						    XFS_ILOCK_SHARED) == 0) {
						ASSERT(ip->i_mount == mp);
						/*
						 * We failed to reacquire
						 * the inode lock without
						 * sleeping, so just skip
						 * the inode for now.  We
						 * clear the ILOCK bit from
						 * the lock_flags so that we
						 * won't try to drop a lock
						 * we don't hold below.
						 */
						lock_flags &= ~XFS_ILOCK_SHARED;
						IPOINTER_REMOVE(ip_next, mp);
					} else if ((ip->i_pincount == 0) &&
						   xfs_iflock_nowait(ip)) {
						ASSERT(ip->i_mount == mp);
						/*
						 * Since this is vfs_sync()
						 * calling we only flush the
						 * inode out if we can lock
						 * it without sleeping and
						 * it is not pinned.  Drop
						 * the mount lock here so
						 * that we don't hold it for
						 * too long. We already have
						 * a marker in the list here.
						 */
						XFS_MOUNT_IUNLOCK(mp);
						mount_locked = B_FALSE;
						error = xfs_iflush(ip,
							   XFS_IFLUSH_DELWRI);
					} else {
						ASSERT(ip->i_mount == mp);
						IPOINTER_REMOVE(ip_next, mp);
					}
				}

			}

		} else {
			if ((flags & SYNC_ATTR) &&
			    ((ip->i_update_core) ||
			     ((ip->i_itemp != NULL) &&
			      (ip->i_itemp->ili_format.ilf_fields != 0)))) {
				if (mount_locked) {
					IPOINTER_INSERT(ip, mp);
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
					else if (bypassed)
						(*bypassed)++;
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
				IPOINTER_INSERT(ip, mp);
			}
			VN_RELE(vp);
			vnode_refed = B_FALSE;
		}
		if (error) {
			last_error = error;
		}
		/*
		 * bail out if the filesystem is corrupted.
		 */
		if (error == EFSCORRUPTED)  {
			if (!mount_locked) {
				XFS_MOUNT_ILOCK(mp);
				IPOINTER_REMOVE(ip, mp);
			}
			XFS_MOUNT_IUNLOCK(mp);
			ASSERT(ipointer_in == B_FALSE);
			kmem_free(ipointer, sizeof(xfs_iptr_t));
			return XFS_ERROR(error);
		}
		/* Let other threads have a chance at the mount lock
		 * if we have looped many times without dropping the
		 * lock.
		 */
		if ((++preempt & PREEMPT_MASK) == 0) {
			if (mount_locked) {
				IPOINTER_INSERT(ip, mp);
			}
		}
		if (mount_locked == B_FALSE) {
			XFS_MOUNT_ILOCK(mp);
			mount_locked = B_TRUE;
			IPOINTER_REMOVE(ip, mp);
			continue;
		}

		ASSERT(ipointer_in == B_FALSE);
		ip = ip->i_mnext;
	} while (ip != mp->m_inodes);
	XFS_MOUNT_IUNLOCK(mp);
	ASSERT(ipointer_in == B_FALSE);

	/*
	 * Get the Quota Manager to flush the dquots in a similar manner.
	 */
	if (XFS_IS_QUOTA_ON(mp)) {
		if (error = xfs_qm_sync(mp, flags)) {
			/*
			 * If we got an IO error, we will be shutting down.
			 * So, there's nothing more for us to do here.
			 */
			ASSERT(error != EIO || XFS_FORCED_SHUTDOWN(mp));
			if (XFS_FORCED_SHUTDOWN(mp)) {
				kmem_free(ipointer, sizeof(xfs_iptr_t));
				return XFS_ERROR(error);
			}
		}
	}
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
			bp = xfs_getsb(mp, XFS_BUF_TRYLOCK);
			if (bp != NULL) {
				bip = XFS_BUF_FSPRIVATE(bp,xfs_buf_log_item_t*);
				if ((bip != NULL) &&
				    xfs_buf_item_dirty(bip)) {
					if (!(XFS_BUF_ISPINNED(bp))) {
						XFS_BUF_ASYNC(bp);
						error = xfs_bwrite(mp, bp);
					} else {
						xfs_buf_relse(bp);
					}
				} else {
					xfs_buf_relse(bp);
				}
			}
		} else {
			bp = xfs_getsb(mp, 0);
			/*
			 * If the buffer is pinned then push on the log so
			 * we won't get stuck waiting in the write for
			 * someone, maybe ourselves, to flush the log.
			 * Even though we just pushed the log above, we
			 * did not have the superblock buffer locked at
			 * that point so it can become pinned in between
			 * there and here.
			 */
			if (XFS_BUF_ISPINNED(bp)) {
				xfs_log_force(mp, (xfs_lsn_t)0,
					      XFS_LOG_FORCE);
			}
			XFS_BUF_BFLAGS(bp) |= fflag;
			error = xfs_bwrite(mp, bp);
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

	/*
	 * Now check to see if the log needs a "dummy" transaction.
	 */

	if (xfs_log_need_covered(mp)) {
		xfs_trans_t *tp;

		/*
		 * Put a dummy transaction in the log to tell
		 * recovery that all others are OK.
		 */
		tp = xfs_trans_alloc(mp, XFS_TRANS_DUMMY1);
		if (error = xfs_trans_reserve(tp, 0,
				XFS_ICHANGE_LOG_RES(mp),
				0, 0, 0))  {
			xfs_trans_cancel(tp, 0);
			kmem_free(ipointer, sizeof(xfs_iptr_t));
			return error;
		}

		ip = mp->m_rootip;
		xfs_ilock(ip, XFS_ILOCK_EXCL);

		xfs_trans_ijoin(tp, ip, XFS_ILOCK_EXCL);
		xfs_trans_ihold(tp, ip);
		xfs_trans_log_inode(tp, ip, XFS_ILOG_CORE);
		error = xfs_trans_commit(tp, 0, NULL);
		xfs_iunlock(ip, XFS_ILOCK_EXCL);
	}

	/*
	 * When shutting down, we need to insure that the AIL is pushed
	 * to disk or the filesystem can appear corrupt from the PROM.
	 */
	if ((flags & (SYNC_CLOSE|SYNC_WAIT)) == (SYNC_CLOSE|SYNC_WAIT)) {
		XFS_bflush(mp->m_ddev_targ);
		if (mp->m_rtdev != NODEV) {
			XFS_bflush(mp->m_rtdev_targ);
		}
	}

	kmem_free(ipointer, sizeof(xfs_iptr_t));
	return XFS_ERROR(last_error);
}

STATIC int
xfs_get_vnode(bhv_desc_t *bdp,
	vnode_t		**vpp,
	ino_t		ino)
{
	xfs_mount_t	*mp;
	xfs_ihash_t	*ih;
	xfs_inode_t	*ip;

	*vpp = NULL;

	mp = XFS_BHVTOM(bdp);
	ih = XFS_IHASH(mp, ino);

	mraccess(&ih->ih_lock);

	for (ip = ih->ih_next; ip != NULL; ip = ip->i_next) {
		if (ip->i_ino == ino) {
			*vpp = XFS_ITOV(ip);
			break;
		}
	}

	mrunlock(&ih->ih_lock);

	if (!*vpp)
		return XFS_ERROR(ENOENT);
	
	return(0);
}


vfsops_t xfs_vfsops = {
	xfs_vfsmount,
	xfs_rootinit,
	fs_dounmount,
	xfs_unmount,
	xfs_root,
	xfs_statvfs,
	xfs_sync,
	xfs_vfsmountroot,
	xfs_get_vnode,
};
#else	/* SIM */
vfsops_t xfs_vfsops = {
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
};

#endif	/* !SIM */
