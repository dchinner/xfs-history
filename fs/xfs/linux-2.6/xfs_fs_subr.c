#ident	"$Revision: 1.6 $"

#include <xfs_linux.h>
#include <sys/types.h>
#undef sysinfo
#include <linux/kernel.h> /* for printk... remove later if needed */
#include <ksys/as.h>
#include <sys/cmn_err.h>
#include <sys/cred.h>
#include <sys/debug.h>
#include <sys/errno.h>
#include <sys/fcntl.h>
#include <ksys/vfile.h>
#include <sys/flock.h>
#include <sys/fs_subr.h>
#include <sys/kabi.h>
#include <sys/mman.h>
#include <sys/param.h>
#include <sys/poll.h>
#include <sys/statvfs.h>
#include <sys/sysmacros.h>
#include <sys/systm.h>
#include <sys/unistd.h>
#include <sys/vfs.h>
#include <sys/pvfs.h>
#include <sys/vnode.h>
#include <sys/dirent.h>
#include <sys/pfdat.h>
#include <string.h>
#include <limits.h>

#include <sys/sat.h>		/* all these for fs_mount */
#include <sys/mount.h>
#include <sys/kmem.h>
#include <sys/dnlc.h>
#include <sys/imon.h>

/*
 * Cover a vnode.  Implementation routine for VOP_COVER.
 */
/*ARGSUSED3*/
int
fs_cover(bhv_desc_t *bdp, struct mounta *uap, char *attrs, struct cred *cred)
{
	printk("XFS: fs_cover() NOT IMPLEMENTED\n");
	return 0;
}

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
	vnode_t 	*coveredvp;
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
	 * Get covered vnode after vfs_lock.
	 */
	coveredvp = vfsp->vfs_vnodecovered;

	/*
	 * Purge all dnlc entries for this vfs.
	 */
	(void) dnlc_purge_vfsp(vfsp, 0);

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
	} else {
		if (coveredvp) {
			--coveredvp->v_vfsp->vfs_nsubmounts;
		}
		ASSERT(vfsp->vfs_nsubmounts == 0);
/***
		vfs_remove(vfsp);
***/
		if (coveredvp) {
			VN_RELE(coveredvp);
		}
	}
	return error;
}

/*
 * fs_realvfsop()
 * base file system vfs_realvfsops() uses this.
 */
/* ARGSUSED */
int
fs_realvfsops(vfs_t *vfsp, struct mounta *uap, vfsops_t **vfsop)
{
	return(0);
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
 * For VOP's, like VOP_MAP, that wish to return ENODEV.
 */
int
fs_nodev()
{
	return ENODEV;
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
 * Compare given vnodes.
 */
int
fs_cmp(bhv_desc_t * bdp, vnode_t * vp2)
{
	vnode_t *vp1 = BHV_TO_VNODE(bdp);
	return vp1 == vp2;
}

/* ARGSUSED */
int
fs_frlock(
	register bhv_desc_t *bdp,
	int cmd,
	struct flock *bfp,
	int flag,
	off_t offset,
	vrwlock_t vrwlock,
	cred_t *cr)
{
	printk("XFS: fs_frlock() NOT IMPLEMENTED\n");
	return 0;
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
/***
	printk("XFS: fs_tosspages() NOT IMPLEMENTED\n");
***/
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
	printk("XFS: fs_flushinval_pages() NOT IMPLEMENTED\n");
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
	printk("XFS: fs_flush_pages() NOT IMPLEMENTED\n");
	return 0;
}


/*
 * vnode pcache layer for vnode_invalfree_pages.
 */
void
fs_invalfree_pages(
        bhv_desc_t	*bdp,
	off_t		filesize)
{
	printk("XFS: fs_invalfree_pages() NOT IMPLEMENTED\n");
}

/*
 * vnode pcache layer for vnode_pages_sethole.
 */
void
fs_pages_sethole(
        bhv_desc_t	*bdp,
	pfd_t		*pfd,
	int		cnt,
	int		doremap,
	off_t		remap_offset)
{
	printk("XFS: fs_pages_sethole() NOT IMPLEMENTED\n");
}


/*
 * Return requested answer when non-device files poll()'ed.
 */
/* ARGSUSED */
int
fs_poll(bhv_desc_t *bdp,
	short events,
	int anyyet,
	short *reventsp,
	struct pollhead **phpp,
	unsigned int *genp)
{
	printk("XFS: fs_cover() NOT IMPLEMENTED\n");
	return 0;
}

/* ARGSUSED */
int
fs_pathconf(bhv_desc_t *bdp, int cmd, long *valp, struct cred *cr)
{
	printk("XFS: fs_pathconf() NOT IMPLEMENTED\n");
	return 0;
}

int
fs_strgetmsg(bhv_desc_t *bdp,
        struct strbuf   *mctl,
        struct strbuf   *mdata,
        unsigned char   *prip,
        int             *flagsp,
        int             fmode,
        union rval      *rvp)
{
	printk("XFS: fs_strgetmsg() NOT IMPLEMENTED\n");
	return(ENOSYS);
}

int
fs_strputmsg(bhv_desc_t *bdp,
        struct strbuf   *mctl,
        struct strbuf   *mdata,
        unsigned char   pri,
        int             flag,
        int             fmode)
{
	printk("XFS: fs_strputmsg() NOT IMPLEMENTED\n");
	return(ENOSYS);
}
