#ident "$Revision: 1.47 $"

#ifdef SIM
#define _KERNEL 1
#endif
#include <sys/param.h>
#include <sys/mode.h>
#include <sys/stat.h>
#include <sys/buf.h>
#include <sys/sysmacros.h>
#include <sys/vnode.h>
#include <sys/grio.h>
#include <sys/sysinfo.h>
#include <sys/ksa.h>
#ifdef SIM
#undef _KERNEL
#endif
#include <sys/debug.h>
#include <sys/uuid.h>
#include <sys/kmem.h>
#ifndef SIM
#include <sys/systm.h>
#endif
#include "xfs_types.h"
#include "xfs_inum.h"
#include "xfs_log.h"
#include "xfs_trans.h"
#include "xfs_sb.h"
#include "xfs_ag.h"
#include "xfs_mount.h"
#include "xfs_alloc_btree.h"
#include "xfs_bmap_btree.h"
#include "xfs_ialloc_btree.h"
#include "xfs_btree.h"
#include "xfs_ialloc.h"
#include "xfs_dir.h"
#include "xfs_dinode.h"
#include "xfs_inode_item.h"
#include "xfs_inode.h"

#ifdef SIM
#include "sim.h"
#endif /* SIM */

extern struct vnodeops xfs_vnodeops;

/*
 * Inode hashing and hash bucket locking.
 */
#define XFS_IHASH(mp,ino)	((mp)->m_ihash + \
				 (ino & (__uint64_t)((mp)->m_ihashmask)))
#define	XFS_IHLOCK(ih)		appsema(&(ih)->ih_lock, PINOD)
#define	XFS_IHUNLOCK(ih)	apvsema(&(ih)->ih_lock)


/*
 * Initialize the inode hash table for the newly mounted file system.
 *
 * mp -- this is the mount point structure for the file system being
 *       initialized
 */
void
xfs_ihash_init(xfs_mount_t *mp)
{
	int	i;
	ulong	hsize;	
	char	name[8];
	extern int	ncsize;

	/*
	 * For now just use a fixed size hash table per file system.
	 * This MUST be changed eventually so we don't waste so much
	 * memory.
	 */
	if (ncsize < 5000) {
		hsize = 512;
	} else {
		hsize = 1024;
	}
	mp->m_ihashmask = hsize - 1;
	mp->m_ihash = (xfs_ihash_t *)kmem_zalloc(hsize * sizeof(xfs_ihash_t),
						 KM_SLEEP);
	ASSERT(mp->m_ihash != NULL);
	for (i = 0; i < hsize; i++) {
		initnsema(&(mp->m_ihash[i].ih_lock), 1,
			  makesname(name, "xih", i));
	}
}

/*
 * Free up structures allocated by xfs_ihash_init, at unmount time.
 */
void
xfs_ihash_free(xfs_mount_t *mp)
{
	int	hsize, i;

	hsize = mp->m_ihashmask + 1;
	for (i = 0; i < hsize; i++)
		freesema(&mp->m_ihash[i].ih_lock);
	kmem_free(mp->m_ihash, hsize * sizeof(xfs_ihash_t));
}

/*
 * Look up an inode by number in the given file system.
 * The inode is looked up in the hash table for the file system
 * represented by the mount point parameter mp.  Each bucket of
 * the hash table is guarded by an individual semaphore.
 *
 * If the inode is found in the hash table, its corresponding vnode
 * is obtained with a call to vn_get().  This call takes care of
 * coordination with the reclamation of the inode and vnode.  Note
 * that the vmap structure is filled in while holding the hash lock.
 * This gives us the state of the inode/vnode when we found it and
 * is used for coordination in vn_get().
 *
 * If it is not in core, read it in from the file system's device and
 * add the inode into the hash table.
 *
 * The inode is locked according to the value of the lock_flags parameter.
 * This flag parameter indicates how and if the inode's IO lock and inode lock
 * should be taken.
 *
 * mp -- the mount point structure for the current file system.  It points
 *       to the inode hash table.
 * tp -- a pointer to the current transaction if there is one.  This is
 *       simply passed through to the xfs_iread() call.
 * ino -- the number of the inode desired.  This is the unique identifier
 *       within the file system for the inode being requested.
 * lock_flags -- flags indicating how to lock the inode.  See the comment
 *	 for xfs_ilock() for a list of valid values.
 */
xfs_inode_t *
xfs_iget(xfs_mount_t	*mp,
	 xfs_trans_t	*tp,
	 xfs_ino_t	ino,
	 uint		lock_flags)
{
	xfs_ihash_t	*ih;
	xfs_inode_t	*ip;
	xfs_inode_t	*iq;
	vnode_t		*vp;
	ulong		version;
	vmap_t		vmap;
	char		name[8];

	SYSINFO.iget++;
	XFSSTATS.xs_ig_attempts++;

	ih = XFS_IHASH(mp, ino);
again:
	XFS_IHLOCK(ih);
	for (ip = ih->ih_next; ip != NULL; ip = ip->i_next) {
		if (ip->i_ino == ino) {
			XFSSTATS.xs_ig_found++;
			vp = XFS_ITOV(ip);
			VMAP(vp, vmap);
			XFS_IHUNLOCK(ih);
			/*
			 * Get a reference to the vnode/inode.
			 * vn_get() takes care of coordination with
			 * the file system inode release and reclaim
			 * functions.  If it returns NULL, the inode
			 * has been reclaimed so just start the search
			 * over again.  We probably won't find it,
			 * but we could be racing with another cpu
			 * looking for the same inode so we have to at
			 * least look.
			 */
			if (!(vp = vn_get(vp, &vmap))) {
				XFSSTATS.xs_ig_frecycle++;
				goto again;
			}

			/*
			 * Inode cache hit: if ip is not at the front of
			 * its hash chain, move it there now.
			 */
			XFS_IHLOCK(ih);
			if (ip->i_prevp != &ih->ih_next) {
				if (iq = ip->i_next) {
					iq->i_prevp = ip->i_prevp;
				}
				*ip->i_prevp = iq;
				iq = ih->ih_next;
				iq->i_prevp = &ip->i_next;
				ip->i_next = iq;
				ip->i_prevp = &ih->ih_next;
				ih->ih_next = ip;
			}
			XFS_IHUNLOCK(ih);
			if (lock_flags != 0) {
				xfs_ilock(ip, lock_flags);
			}

			return ip;
		}
	}

	/*
	 * Inode cache miss: save the hash chain version stamp and unlock
	 * the chain, so we don't deadlock in vn_alloc.
	 */
	XFSSTATS.xs_ig_missed++;
	version = ih->ih_version;
	XFS_IHUNLOCK(ih);

	/*
	 * Read the disk inode attributes into a new inode structure and get
	 * a new vnode for it.  Initialize the inode lock so we can idestroy
	 * it soon if it's a dup.  This should also initialize i_dev, i_ino,
	 * i_bno, and i_index;
	 */
	ip = xfs_iread(mp, tp, ino);
	if (ip == NULL)
		return NULL;
	vp = vn_alloc(&xfs_vnodeops, mp->m_vfsp, IFTOVT(ip->i_d.di_mode),
		      ip->i_u2.iu_rdev, ip);

	mrinit(&ip->i_lock, makesname(name, "xino", (int)vp->v_number));
	mrinit(&ip->i_iolock, makesname(name, "xio", (int)vp->v_number));
	initnlock(&ip->i_ticketlock, "xtck");
#ifdef NOTYET
	initnlock(&ip->i_range_lock.r_splock, "xrange");
#endif /* NOTYET */
	initnsema(&ip->i_flock, 1, makesname(name, "fino", vp->v_number));
	sv_init(&ip->i_pinsema, SV_DEFAULT,
		makesname(name, "pino", vp->v_number));
	xfs_inode_item_init(ip, mp);
	if (lock_flags != 0)
		xfs_ilock(ip, lock_flags);

	/*
	 * Put ip on its hash chain, unless someone else hashed a duplicate
	 * after we released the hash lock.
	 */
	XFS_IHLOCK(ih);
	if (ih->ih_version != version) {
		for (iq = ih->ih_next; iq != NULL; iq = iq->i_next) {
			if (iq->i_ino == ino) {
				XFS_IHUNLOCK(ih);
				vn_free(vp);
				xfs_idestroy(ip);
				XFSSTATS.xs_ig_dup++;
				goto again;
			}
		}
	}

	/*
	 * These values _must_ be set before releasing ihlock!
	 */
	ip->i_vnode = vp;
	ip->i_mount = mp;
	ip->i_hash = ih;
	if (iq = ih->ih_next) {
		iq->i_prevp = &ip->i_next;
	}
	ip->i_next = iq;
	ip->i_prevp = &ih->ih_next;
	ih->ih_next = ip;
	ih->ih_version++;
	ip->i_dmevents = ip->i_d.di_dmevmask;	/* FIX: OR in vfs mask */
	XFS_IHUNLOCK(ih);

	/*
	 * Link ip to its mount and thread it on the mount's inode list.
	 */
	XFS_MOUNT_ILOCK(mp);
	if (iq = mp->m_inodes) {
		ASSERT(iq->i_mprev->i_mnext == iq);
		ip->i_mprev = iq->i_mprev;
		iq->i_mprev->i_mnext = ip;
		iq->i_mprev = ip;
		ip->i_mnext = iq;
	} else {
		ip->i_mnext = ip;
		ip->i_mprev = ip;
	}
	mp->m_inodes = ip;
	XFS_MOUNT_IUNLOCK(mp);

	return ip;
}

/*
 * Look for the inode corresponding to the given ino in the hash table.
 * If it is there and its i_transp pointer matches tp, return it.
 * Otherwise, return NULL.
 */
xfs_inode_t *
xfs_inode_incore(xfs_mount_t	*mp,
		 xfs_ino_t	ino,
		 xfs_trans_t	*tp)
{
	xfs_ihash_t	*ih;
	xfs_inode_t	*ip;
	xfs_inode_t	*iq;

	ih = XFS_IHASH(mp, ino);
	XFS_IHLOCK(ih);
	for (ip = ih->ih_next; ip != NULL; ip = ip->i_next) {
		if (ip->i_ino == ino) {
			/*
			 * If we find it and tp matches, return it.
			 * Also move it to the front of the hash list
			 * if we find it and it is not already there.
			 * Otherwise break from the loop and return
			 * NULL.
			 */
			if (ip->i_transp == tp) {
				if (ip->i_prevp != &ih->ih_next) {
					if (iq = ip->i_next) {
						iq->i_prevp = ip->i_prevp;
					}
					*ip->i_prevp = iq;
					iq = ih->ih_next;
					iq->i_prevp = &ip->i_next;
					ip->i_next = iq;
					ip->i_prevp = &ih->ih_next;
					ih->ih_next = ip;
				}
				XFS_IHUNLOCK(ih);
				return (ip);
			}
			break;
		}
	}	
	XFS_IHUNLOCK(ih);
	return (NULL);
}

/*
 * Decrement reference count of an inode structure and unlock it.
 *
 * ip -- the inode being released
 * lock_flags -- this parameter indicates the inode's locks to be
 *       to be released.  See the comment on xfs_iunlock() for a list
 *	 of valid values.
 */
void
xfs_iput(xfs_inode_t	*ip,
	 uint		lock_flags)
{
	xfs_iunlock(ip, lock_flags);
	VN_RELE(XFS_ITOV(ip));
}

/*
 * This routine embodies the part of the reclaim code that pulls
 * the inode from the inode hash table and the mount structure's
 * inode list.
 * This should only be called from xfs_reclaim().
 */
void
xfs_ireclaim(xfs_inode_t *ip)
{
	xfs_ihash_t	*ih;
	xfs_inode_t	*iq;
	xfs_mount_t	*mp;

	/*
	 * Remove from old hash list.
	 */
	XFSSTATS.xs_ig_reclaims++;
	ih = ip->i_hash;
	XFS_IHLOCK(ih);
	if (iq = ip->i_next) {
		iq->i_prevp = ip->i_prevp;
	}
	*ip->i_prevp = iq;
	XFS_IHUNLOCK(ih);

	/*
	 * Remove from mount's inode list.
	 */
	mp = ip->i_mount;
	XFS_MOUNT_ILOCK(mp);
	ASSERT((ip->i_mnext != NULL) && (ip->i_mprev != NULL));
	iq = ip->i_mnext;
	iq->i_mprev = ip->i_mprev;
	ip->i_mprev->i_mnext = iq;

	/*
	 * Fix up the head pointer if it points to the inode being deleted.
	 */
	if (mp->m_inodes == ip) {
		if (ip == iq) {
			mp->m_inodes = NULL;
		} else {
			mp->m_inodes = iq;
		}
	}
	mp->m_ireclaims++;
	XFS_MOUNT_IUNLOCK(mp);

	/*
	 * Here we do a spurious inode lock in order to coordinate with
	 * xfs_sync().  This is because xfs_sync() references the inodes
	 * in the mount list without taking references on the corresponding
	 * vnodes.  We make that OK here by ensuring that we wait until
	 * the inode is unlocked in xfs_sync() before we go ahead and
	 * free it.  We get both the regular lock and the io lock because
	 * the xfs_sync() code may need to drop the regular one but will
	 * still hold the io lock.
	 */
	xfs_ilock(ip, XFS_ILOCK_EXCL | XFS_IOLOCK_EXCL);

	/*
	 * Free all memory associated with the inode.
	 */
	xfs_idestroy(ip);
}


/*
 * This is a wrapper routine around the xfs_ilock() routine
 * used to centralize some grungy code.  It is used in places
 * that wish to lock the inode solely for reading the extents.
 * The reason these places can't just call xfs_ilock(SHARED)
 * is that the inode lock also guards to bringing in of the
 * extents from disk for a file in b-tree format.  If the inode
 * is in b-tree format, then we need to lock the inode exclusively
 * until the extents are read in.  Locking it exclusively all
 * the time would limit our parallelism unnecessarily, though.
 * What we do instead is check to see if the extents have been
 * read in yet, and only lock the inode exclusively if they
 * have not.
 *
 * The function returns a value which should be given to the
 * corresponding xfs_iunlock_map_shared().  This value is
 * the mode in which the lock was actually taken.
 */
uint
xfs_ilock_map_shared(
	xfs_inode_t	*ip)
{
	uint	lock_mode;

	if ((ip->i_d.di_format == XFS_DINODE_FMT_BTREE) &&
	    ((ip->i_flags & XFS_IEXTENTS) == 0)) {
		lock_mode = XFS_ILOCK_EXCL;
	} else {
		lock_mode = XFS_ILOCK_SHARED;
	}

	xfs_ilock(ip, lock_mode);

	return lock_mode;
}

/*
 * This is simply the unlock routine to go with xfs_ilock_map_shared().
 * All it does is call xfs_iunlock() with the given lock_mode.
 */
void
xfs_iunlock_map_shared(
	xfs_inode_t	*ip,
	unsigned int	lock_mode)
{
	xfs_iunlock(ip, lock_mode);
}


/*
 * The xfs inode contains 2 locks: a multi-reader lock called the
 * i_iolock and a multi-reader lock called the i_lock.  This routine
 * allows either or both of the locks to be obtained.
 *
 * The 2 locks should always be ordered so that the IO lock is
 * obtained first in order to prevent deadlock.
 *
 * ip -- the inode being locked
 * lock_flags -- this parameter indicates the inode's locks to be
 *       to be locked.  It can be:
 *		XFS_IOLOCK_SHARED,
 *		XFS_IOLOCK_EXCL,
 *	 	XFS_ILOCK_SHARED,
 *		XFS_ILOCK_EXCL,
 *		XFS_IOLOCK_SHARED | XFS_ILOCK_SHARED,
 *		XFS_IOLOCK_SHARED | XFS_ILOCK_EXCL,
 *		XFS_IOLOCK_EXCL | XFS_ILOCK_SHARED,
 *		XFS_IOLOCK_EXCL | XFS_ILOCK_EXCL
 *
 */
void
xfs_ilock(xfs_inode_t	*ip,
	  uint		lock_flags)
{
	/*
	 * You can't set both SHARED and EXCL for the same lock,
	 * and only XFS_IOLOCK_SHARED, XFS_IOLOCK_EXCL, XFS_ILOCK_SHARED,
	 * and XFS_ILOCK_EXCL are valid values to set in lock_flags.
	 */
	ASSERT((lock_flags & (XFS_IOLOCK_SHARED | XFS_IOLOCK_EXCL)) !=
	       (XFS_IOLOCK_SHARED | XFS_IOLOCK_EXCL));
	ASSERT((lock_flags & (XFS_ILOCK_SHARED | XFS_ILOCK_EXCL)) !=
	       (XFS_ILOCK_SHARED | XFS_ILOCK_EXCL));
	ASSERT((lock_flags & ~(XFS_IOLOCK_SHARED | XFS_IOLOCK_EXCL |
		XFS_ILOCK_SHARED | XFS_ILOCK_EXCL)) == 0);
	ASSERT(lock_flags != 0);

	if (lock_flags & XFS_IOLOCK_EXCL) {
		mrlock(&ip->i_iolock, MR_UPDATE, PINOD);
	} else if (lock_flags & XFS_IOLOCK_SHARED) {
		mrlock(&ip->i_iolock, MR_ACCESS, PINOD);
	}

	if (lock_flags & XFS_ILOCK_EXCL) {
		mrlock(&ip->i_lock, MR_UPDATE, PINOD);
	} else if (lock_flags & XFS_ILOCK_SHARED) {
		mrlock(&ip->i_lock, MR_ACCESS, PINOD);
	}

}

/*
 * This is just like xfs_ilock(), except that the caller
 * is guaranteed not to sleep.  It returns 1 if it gets
 * the requested locks and 0 otherwise.  If the IO lock is
 * obtained but the inode lock cannot be, then the IO lock
 * is dropped before returning.
 *
 * ip -- the inode being locked
 * lock_flags -- this parameter indicates the inode's locks to be
 *       to be locked.  See the comment for xfs_ilock() for a list
 *	 of valid values.
 *
 */
int
xfs_ilock_nowait(xfs_inode_t	*ip,
		 uint		lock_flags)
{
	int	iolocked;
	int	ilocked;

	/*
	 * You can't set both SHARED and EXCL for the same lock,
	 * and only XFS_IOLOCK_SHARED, XFS_IOLOCK_EXCL, XFS_ILOCK_SHARED,
	 * and XFS_ILOCK_EXCL are valid values to set in lock_flags.
	 */
	ASSERT((lock_flags & (XFS_IOLOCK_SHARED | XFS_IOLOCK_EXCL)) !=
	       (XFS_IOLOCK_SHARED | XFS_IOLOCK_EXCL));
	ASSERT((lock_flags & (XFS_ILOCK_SHARED | XFS_ILOCK_EXCL)) !=
	       (XFS_ILOCK_SHARED | XFS_ILOCK_EXCL));
	ASSERT((lock_flags & ~(XFS_IOLOCK_SHARED | XFS_IOLOCK_EXCL |
		XFS_ILOCK_SHARED | XFS_ILOCK_EXCL)) == 0);
	ASSERT(lock_flags != 0);

	iolocked = 0;
	if (lock_flags & XFS_IOLOCK_EXCL) {
		iolocked = cmrlock(&ip->i_iolock, MR_UPDATE);
		if (!iolocked) {
			return 0;
		}
	} else if (lock_flags & XFS_IOLOCK_SHARED) {
		iolocked = cmrlock(&ip->i_iolock, MR_ACCESS);
		if (!iolocked) {
			return 0;
		}
	}

	if (lock_flags & XFS_ILOCK_EXCL) {
		ilocked = cmrlock(&ip->i_lock, MR_UPDATE);
		if (!ilocked) {
			if (iolocked) {
				mrunlock(&ip->i_iolock);
			}
			return 0;
		}
	} else if (lock_flags & XFS_ILOCK_SHARED) {
		ilocked = cmrlock(&ip->i_lock, MR_ACCESS);
		if (!ilocked) {
			if (iolocked) {
				mrunlock(&ip->i_iolock);
			}
			return 0;
		}
	}
	return 1;
}

/*
 * xfs_iunlock() is used to drop the inode locks acquired with
 * xfs_ilock() and xfs_ilock_nowait().  The caller must pass
 * in the flags given to xfs_ilock() or xfs_ilock_nowait() so
 * that we know which locks to drop.
 *
 * ip -- the inode being unlocked
 * lock_flags -- this parameter indicates the inode's locks to be
 *       to be unlocked.  See the comment for xfs_ilock() for a list
 *	 of valid values for this parameter.
 *
 */
void
xfs_iunlock(xfs_inode_t	*ip,
	    uint	lock_flags)
{
	/*
	 * You can't set both SHARED and EXCL for the same lock,
	 * and only XFS_IOLOCK_SHARED, XFS_IOLOCK_EXCL, XFS_ILOCK_SHARED,
	 * and XFS_ILOCK_EXCL are valid values to set in lock_flags.
	 */
	ASSERT((lock_flags & (XFS_IOLOCK_SHARED | XFS_IOLOCK_EXCL)) !=
	       (XFS_IOLOCK_SHARED | XFS_IOLOCK_EXCL));
	ASSERT((lock_flags & (XFS_ILOCK_SHARED | XFS_ILOCK_EXCL)) !=
	       (XFS_ILOCK_SHARED | XFS_ILOCK_EXCL));
	ASSERT((lock_flags &
		~(XFS_IOLOCK_SHARED | XFS_IOLOCK_EXCL |
		  XFS_ILOCK_SHARED | XFS_ILOCK_EXCL |
		  XFS_IUNLOCK_NONOTIFY)) == 0);
	ASSERT(lock_flags != 0);

	if (lock_flags & (XFS_IOLOCK_SHARED | XFS_IOLOCK_EXCL)) {
		ASSERT(!(lock_flags & XFS_IOLOCK_SHARED) ||
		       (ismrlocked(&ip->i_iolock, MR_ACCESS)));
		ASSERT(!(lock_flags & XFS_IOLOCK_EXCL) ||
		       (ismrlocked(&ip->i_iolock, MR_UPDATE)));
		mrunlock(&ip->i_iolock);
	}

	if (lock_flags & (XFS_ILOCK_SHARED | XFS_ILOCK_EXCL)) {
		ASSERT(!(lock_flags & XFS_ILOCK_SHARED) ||
		       (ismrlocked(&ip->i_lock, MR_ACCESS)));
		ASSERT(!(lock_flags & XFS_ILOCK_EXCL) ||
		       (ismrlocked(&ip->i_lock, MR_UPDATE)));
		mrunlock(&ip->i_lock);
	}

	/*
	 * Let the AIL know that this item has been unlocked in case
	 * it is in the AIL and anyone is waiting on it.  Don't do
	 * this if the caller has asked us not to.
	 */
	if (!(lock_flags & XFS_IUNLOCK_NONOTIFY)) {
		xfs_trans_unlocked_item(ip->i_mount,
					(xfs_log_item_t*)&(ip->i_item));
	}
}

/*
 * The following three routines simply manage the i_flock
 * semaphore embedded in the inode.  This semaphore synchronizes
 * processes attempting to flush the in-core inode back to disk.
 */
void
xfs_iflock(xfs_inode_t *ip)
{
	psema(&(ip->i_flock), PINOD);
}

int
xfs_iflock_nowait(xfs_inode_t *ip)
{
	return (cpsema(&(ip->i_flock)));
}

void
xfs_ifunlock(xfs_inode_t *ip)
{
	ASSERT(valusema(&(ip->i_flock)) <= 0);
	vsema(&(ip->i_flock));
}
