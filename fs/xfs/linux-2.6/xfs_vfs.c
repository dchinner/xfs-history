/*	Copyright (c) 1984, 1986, 1987, 1988, 1989, 1990 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF     	*/
/*	UNIX System Laboratories, Inc.                     	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

/*
 * +++++++++++++++++++++++++++++++++++++++++++++++++++++++++
 * 		PROPRIETARY NOTICE (Combined)
 * 
 * This source code is unpublished proprietary information
 * constituting, or derived under license from AT&T's UNIX(r) System V.
 * In addition, portions of such source code were derived from Berkeley
 * 4.3 BSD under license from the Regents of the University of
 * California.
 * 
 * 
 * 
 * 		Copyright Notice 
 * 
 * Notice of copyright on this source code product does not indicate 
 * publication.
 * 
 * 	(c) 1986,1987,1988,1989  Sun Microsystems, Inc
 * 	(c) 1983,1984,1985,1986,1987,1988,1989  AT&T.
 * 	          All rights reserved.
 *  
 */

/*#ident	"@(#)uts-comm:fs/vfs.c	1.18"*/
#ident	"$Revision: 1.137 $"

#if defined(__linux__)
#include <xfs_linux.h>
#endif

#include <sys/types.h>
#include <sys/param.h>
#include <sys/cmn_err.h>
#include <sys/conf.h>
#include <sys/cred.h>
#include <sys/debug.h>
#include <sys/errno.h>
#include <ksys/vfile.h>
#include <ksys/fdt.h>
#include <sys/kabi.h>
#include <sys/kmem.h>
#include <sys/mount.h>
#include <sys/proc.h>
#include <sys/sema.h>
#include <sys/statfs.h>
#include <sys/statvfs.h>
#include <sys/systm.h>
#include <sys/uio.h>
#include <sys/vfs.h>
#include <sys/vnode.h>
#include <sys/vnode_private.h>
#include <sys/buf.h>
#include <sys/quota.h>
#include <sys/xlate.h>
#include <string.h>
#include <sys/imon.h>
#include <sys/sat.h>
#include <sys/dnlc.h>


/*
 * VFS global data.
 */
vfs_t 		rootvfs_data;	/* root vfs */
vfs_t 		*rootvfs = &rootvfs_data; 	/* pointer to root vfs; */
                                                /*   head of VFS list. */
lock_t 		vfslock;	/* spinlock protecting rootvfs and vfs_flag */
sema_t 		synclock;	/* sync in progress; initialized in sinit() */
                                
extern int xfs_statdevvp(struct statvfs *, struct vnode *);
extern void	vn_init(void);

/*
 * vfs_add is called by mount to add the new vfs into the vfs list and to
 * cover the mounted-on vnode.  The mounted-on vnode's v_vfsmountedhere link
 * to vfsp must have already been set, protected by VN_LOCK and VN_UNLOCK.
 * The vfs must already have been locked by the caller.
 *
 * coveredvp is zero if this is the root.
 */
void
vfs_add(vnode_t *coveredvp, struct vfs *vfsp, int mflag)
{
	struct vfs *vfsq;
	int s;

	s = vfs_spinlock();
	if (coveredvp != NULL) {
		if (vfsq = rootvfs->vfs_next)
			vfsq->vfs_prevp = &vfsp->vfs_next;
		rootvfs->vfs_next = vfsp;
		vfsp->vfs_next = vfsq;
		vfsp->vfs_prevp = &rootvfs->vfs_next;
	} else {
		/*
		 * This is the root of the whole world.
		 * Some filesystems might already be 'on the list'
		 * since they aren't really mounted and add themselves
		 * at init time.
		 * This essentially replaces the static 'root'
		 * that rootvfs is initialized to
		 */
		if (vfsp != rootvfs) {
			vfsp->vfs_next = rootvfs->vfs_next;
			vfsp->vfs_prevp = NULL;
			if (vfsq = rootvfs->vfs_next)
				vfsq->vfs_prevp = &vfsp->vfs_next;
			
			rootvfs = vfsp;
		}
	}
	vfsp->vfs_vnodecovered = coveredvp;

	if (mflag != VFS_FLAGS_PRESET) {
		vfsp->vfs_flag &= ~(VFS_RDONLY|VFS_NOSUID|VFS_NODEV|VFS_GRPID);

		if (mflag & MS_RDONLY)
			vfsp->vfs_flag |= VFS_RDONLY;

		if (mflag & MS_NOSUID)
			vfsp->vfs_flag |= VFS_NOSUID;

		if (mflag & MS_NODEV)
			vfsp->vfs_flag |= VFS_NODEV;

		if (mflag & MS_GRPID)
			vfsp->vfs_flag |= VFS_GRPID;

		if (mflag & MS_DEFXATTR)
			vfsp->vfs_flag |= VFS_DEFXATTR;

		if (mflag & MS_DOXATTR)
			vfsp->vfs_flag |= VFS_DOXATTR;
	}

	vfs_spinunlock(s);
}

/*
 * Remove a vfs from the vfs list, and destroy pointers to it.
 * Called by umount after it determines that an unmount is legal but
 * before it destroys the vfs.
 */
void
vfs_remove(struct vfs *vfsp)
{
	register vnode_t *vp;
	register struct vfs *vfsq;
	int s;

	/*
	 * Can't unmount root.  Should never happen because fs will
	 * be busy.
	 */
	ASSERT(vfsp != rootvfs);

	/*
	 * Clear covered vnode pointer.  No thread can traverse vfsp's
	 * mount point while vfsp is locked.
	 */
	vp = vfsp->vfs_vnodecovered;
	s = VN_LOCK(vp);
	vp->v_vfsmountedhere = NULL;
	VN_UNLOCK(vp, s);

	/*
	 * Unlink vfsp from the rootvfs list.
	 */
	s = vfs_spinlock();
	if (vfsq = vfsp->vfs_next)
		vfsq->vfs_prevp = vfsp->vfs_prevp;
	*vfsp->vfs_prevp = vfsq;
	vfs_spinunlock(s);

	/*
	 * Release lock and wakeup anybody waiting.
	 * Turns off VFS_OFFLINE bit.
	 */
	vfs_unlock(vfsp);
}

/*
 * Allocate and initialize a new vfs
 */
vfs_t *
vfs_allocate(void)
{
	vfs_t	*vfsp;

        vfsp = kmem_zalloc(sizeof(vfs_t), KM_SLEEP);
	ASSERT(vfsp);
	VFS_INIT(vfsp);
	return (vfsp);
}

void
vfs_deallocate(vfs_t *vfsp)
{
        VFS_FREE(vfsp);
        kmem_free(vfsp, sizeof(vfs_t));
}

/*
 * Implement a simplified multi-access, single-update protocol for vfs.
 *
 * Only one update-lock (mount/unmount) can be held at a time; if another
 * process holds the vfs stucture with update capabilities, or is waiting
 * to acquire update rights, other update calls fail.
 *
 * Multiple accessors are allowed: vfs_busycnt tracks the number of
 * concurrent accesses.  Update permission sleeps until the last access
 * has finished, but leaves the VFS_MWANT flag to hold (if called via
 * vfs_busy) or reject (called via vfs_busydev or vfs_lock) subsequent
 * accesses/updates.
 * Note that traverse understands the vfs locking model and waits for
 * any update to complete, and retries the mount-point traversal.
 *
 * Accessors include: vfs_syncall, traverse, VFS_STATVFS, and quota checking.
 */
static int
vfs_lock_flags(struct vfs *vfsp, int flags)
{
	register int error;
	int s;

	s = vfs_spinlock();
	if (vfsp->vfs_flag & (VFS_MLOCK|VFS_MWANT)) {
		vfs_spinunlock(s);
		return EBUSY;
	}

	while (vfsp->vfs_busycnt) {
		ASSERT(vfsp->vfs_busycnt > 0);
		ASSERT(!(vfsp->vfs_flag & (VFS_MLOCK|VFS_OFFLINE)));
		vfsp->vfs_flag |= VFS_MWAIT|VFS_MWANT;
		vfsp_waitsig(vfsp, PVFS, s); /* REMOVED setting error. */
		error = 0; /* JIMJIM always no error */
		s = vfs_spinlock();
		if (error) {
			ASSERT(vfsp->vfs_flag & VFS_MWANT);
			vfsp->vfs_flag &= ~VFS_MWANT;
			if (vfsp->vfs_flag & VFS_MWAIT) {
				vfsp->vfs_flag &= ~VFS_MWAIT;
				sv_broadcast(&vfsp->vfs_wait);
			}
			vfs_spinunlock(s);
			return EINTR;
		}
		ASSERT(vfsp->vfs_flag & VFS_MWANT);
		vfsp->vfs_flag &= ~VFS_MWANT;
	}

	ASSERT((vfsp->vfs_flag & (VFS_MLOCK|VFS_OFFLINE)) != VFS_OFFLINE);
	if (vfsp->vfs_flag & VFS_MLOCK) {
		error = EBUSY;
	} else {
		vfsp->vfs_flag |= VFS_MLOCK|flags;
		error = 0;
	}
	vfs_spinunlock(s);
	return error;
}

/*
 * Lock a filesystem to prevent access to it while mounting.
 * Returns error if already locked.
 */
int
vfs_lock(struct vfs *vfsp)
{
	return (vfs_lock_flags(vfsp, 0));
}

/*
 * Lock a filesystem and mark it offline,
 * to prevent access to it while unmounting.
 * Returns error if already locked.
 */
int
vfs_lock_offline(struct vfs *vfsp)
{
	return (vfs_lock_flags(vfsp, VFS_OFFLINE));
}

/*
 * Unlock a locked filesystem.
 */
void
vfs_unlock(register struct vfs *vfsp)
{
	int s = vfs_spinlock();
	ASSERT((vfsp->vfs_flag & (VFS_MWANT|VFS_MLOCK)) == VFS_MLOCK);
	vfsp->vfs_flag &= ~(VFS_MLOCK|VFS_OFFLINE);

	/*
	 * Wake accessors (traverse() or vfs_syncall())
	 * waiting for the lock to clear.
	 */
	if (vfsp->vfs_flag & VFS_MWAIT) {
		vfsp->vfs_flag &= ~VFS_MWAIT;
		sv_broadcast(&vfsp->vfs_wait);
	}
	vfs_spinunlock(s);
}

/*
 * Get access permission for vfsp.
 */
int
vfs_busy(struct vfs *vfsp)
{
	int s = vfs_spinlock();
	ASSERT((vfsp->vfs_flag & (VFS_MLOCK|VFS_OFFLINE)) != VFS_OFFLINE);
	while (vfsp->vfs_flag & (VFS_MLOCK|VFS_MWANT)) {
		ASSERT(vfsp->vfs_flag & VFS_MWANT || vfsp->vfs_busycnt == 0);
		if (vfsp->vfs_flag & VFS_OFFLINE) {
			vfs_spinunlock(s);
			return EBUSY;
		}
		vfsp->vfs_flag |= VFS_MWAIT;
		vfsp_waitsig(vfsp, PVFS, s);	/* JIMJIM this has no sig "nificance". */
		s = vfs_spinlock();
	}

	ASSERT(vfsp->vfs_busycnt >= 0);
	vfsp->vfs_busycnt++;
	vfs_spinunlock(s);

	return 0;
}

/*
 * Given a <dev, filesystem-type> pair, return the vfs-entry for it.
 * If the type sent in is VFS_FSTYPE_ANY, then this'll only try to
 * match the device number.
 *
 * This extra parameter was necessary since duplicate vfs entries with
 * the same vfs_dev are possible because of lofs.
 */
struct vfs *
vfs_busydev(dev_t dev, int type)
{
	int s;
	struct vfs *vfsp;
again:
	s = vfs_spinlock();
	for (vfsp = rootvfs; vfsp != NULL; vfsp = vfsp->vfs_next) {
		if (vfsp->vfs_dev == dev &&
		    (type == VFS_FSTYPE_ANY || type == vfsp->vfs_fstype)) {

			if (vfsp->vfs_flag & VFS_OFFLINE) {
				vfsp = NULL;
				break;
			}
			if (vfsp->vfs_flag & (VFS_MLOCK|VFS_MWANT)) {
				ASSERT(vfsp->vfs_flag & VFS_MWANT ||
				       vfsp->vfs_busycnt == 0);
				vfsp->vfs_flag |= VFS_MWAIT;
				vfsp_wait(vfsp, 0, s);	 /* JIMJIM removed PZERO */
				goto again;
			}

			ASSERT(vfsp->vfs_busycnt >= 0);
			vfsp->vfs_busycnt++;
			break;
		}
	}
	vfs_spinunlock(s);
	return vfsp;
}

void
vfs_unbusy(struct vfs *vfsp)
{
	int s = vfs_spinlock();
	ASSERT(!(vfsp->vfs_flag & (VFS_MLOCK|VFS_OFFLINE)));
	ASSERT(vfsp->vfs_busycnt > 0);
	if (--vfsp->vfs_busycnt == 0)
		vfs_unbusy_wakeup(vfsp);
	vfs_spinunlock(s);
}

void
vfs_unbusy_wakeup(register struct vfs *vfsp)
{
        /*
         * If there's an updater (mount/unmount) waiting for the vfs lock,
         * wake up only it.  Updater should be the first on the sema queue.
         *
         * Otherwise, wake all accessors (traverse() or vfs_syncall())
         * waiting for the lock to clear.
         */
        if (vfsp->vfs_flag & VFS_MWANT) {
                sv_signal(&vfsp->vfs_wait);
        } else
        if (vfsp->vfs_flag & VFS_MWAIT) {
                vfsp->vfs_flag &= ~VFS_MWAIT;
                sv_broadcast(&vfsp->vfs_wait);
        }
}


/*
 * Search the vfs list for a specified device.  Returns a pointer to it
 * or NULL if no suitable entry is found.
 *
 * Any calls to this routine (as opposed to vfs_busydev) should
 * considered extremely suspicious.  Once the vfs_spinunlock is done,
 * there is likely to be nothing guaranteeing that the vfs pointer
 * returned continues to point to a vfs.  There are numerous bugs
 * which would quickly become intolerable if the frequency of unmount
 * was to rise above its typically low level.
 */
struct vfs *
vfs_devsearch(dev_t dev, int fstype)
{
	register struct vfs *vfsp;

	int s = vfs_spinlock();
	vfsp = vfs_devsearch_nolock(dev, fstype);
	vfs_spinunlock(s);
	return vfsp;
}

/*
 * Same as vfs_devsearch without locking the list.
 * Useful for debugging code, but put it here anyway.
 */
struct vfs *
vfs_devsearch_nolock(dev_t dev, int fstype)
{
        register struct vfs *vfsp;

        for (vfsp = rootvfs; vfsp != NULL; vfsp = vfsp->vfs_next)
                if ((vfsp->vfs_dev == dev) &&
                    (fstype == VFS_FSTYPE_ANY || fstype == vfsp->vfs_fstype))
                        break;
        return vfsp;
}

/*
 * Map VFS flags to statvfs flags.  These shouldn't really be separate
 * flags at all.
 */
u_long
vf_to_stf(u_long vf)
{
	u_long stf = 0;

	if (vf & VFS_RDONLY)
		stf |= ST_RDONLY;
	if (vf & VFS_NOSUID)
		stf |= ST_NOSUID;
	if (vf & VFS_NOTRUNC)
		stf |= ST_NOTRUNC;
	if (vf & VFS_DMI)
		stf |= ST_DMI;
	if (vf & VFS_NODEV)
		stf |= ST_NODEV;
	if (vf & VFS_GRPID)
		stf |= ST_GRPID;
	if (vf & VFS_LOCAL)
		stf |= ST_LOCAL;

	return stf;
}
void
vfsinit()
{
	register int i;
	/*
	 * Initialize vfs stuff.
	 */
	spinlock_init(&vfslock, "vfslock");

	/*
	 * Initialize vnode stuff.
	 */
	vn_init();

	/*
	 * Initialize the name cache.
	 */
	dnlc_init();

	/*
	 * Call all the init routines.
	 */
}

/*
 * Called by fs dependent VFS_MOUNT code to link the VFS base file system 
 * dependent behavior with the VFS virtual object.
 */
void
vfs_insertbhv(
	vfs_t *vfsp, 
	bhv_desc_t *bdp, 
	vfsops_t *vfsops, 	
	void *mount)
{
	/* 
	 * Initialize behavior desc with ops and data and then
	 * attach it to the vfs.
	 */
	bhv_desc_init(bdp, mount, vfsp, vfsops);
	bhv_insert_initial(&vfsp->vfs_bh, bdp);
}
