
#include <sys/types.h>
#include <sys/param.h>
#ifndef SIM
#include <sys/kmem.h>
#else
#define _KERNEL
#endif
#include <sys/mode.h>
#include <sys/stat.h>
#include <sys/buf.h>
#include <sys/sysmacros.h>
#include <sys/vnode.h>
#ifdef SIM
#undef _KERNEL
#endif
#include <sys/sysinfo.h>
#include <sys/ksa.h>
#include <sys/debug.h>
#include <sys/uuid.h>
#ifndef SIM
#include <sys/systm.h>
#endif
#include "xfs_types.h"
#include "xfs_inum.h"
#include "xfs_trans.h"
#include "xfs_sb.h"
#include "xfs_mount.h"
#include "xfs_alloc.h"
#include "xfs_ialloc.h"
#include "xfs_ag.h"
#include "xfs_bmap.h"
#include "xfs_btree.h"
#include "xfs_dinode.h"
#include "xfs_inode_item.h"
#include "xfs_inode.h"

#ifdef SIM
#include "sim.h"
#endif /* SIM */

struct igetstats	XFS_IGETINFO;

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
 */
void
xfs_ihash_init(xfs_mount_t *mp)
{
	int	i;
	ulong	hsize;	
	char	name[8];

	/*
	 * For now just use a fixed size hash table per file system.
	 * This MUST be changed eventually so we don't waste so much
	 * memory.
	 */
	hsize = 512;
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
 * Look up an inode by inumber in the given file system.
 * If it is in core, honor the locking protocol.
 * If it is not in core, read it in from the file system's device.
 * In all cases, a pointer to a locked inode is returned.
 * The mode of the lock depends on the flags parameter.  Flags can
 * be either XFS_ILOCK_EXCL or XFS_ILOCK_SHARED. 
 */
xfs_inode_t *
xfs_iget(xfs_mount_t *mp, xfs_trans_t *tp, xfs_ino_t ino, uint flags)
{
	xfs_ihash_t	*ih;
	xfs_inode_t	*ip;
	xfs_inode_t	*iq;
	vnode_t		*vp;
	ulong		version;
#ifdef NOTYET
	vmap_t		vmap;
#endif
	int		s;
	char		name[8];

	SYSINFO.iget++;
	XFS_IGETINFO.ig_attempts++;

	ASSERT((flags == XFS_ILOCK_EXCL) || (flags == XFS_ILOCK_SHARED));
	ih = XFS_IHASH(mp, ino);
again:
	XFS_IHLOCK(ih);
	for (ip = ih->ih_next; ip != NULL; ip = ip->i_next) {
		if (ip->i_ino == ino) {
			XFS_IGETINFO.ig_found++;
#ifdef NOTYET
			vp = XFS_ITOV(ip);
			VMAP(vp, vmap);
#endif
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
#ifdef NOTYET
			if (!(vp = vn_get(vp, &vmap))) {
				XFS_IGETINFO.ig_frecycle++;
				goto again;
			}
#endif

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
			xfs_ilock(ip, (int)flags);
			goto out;
		}
	}

	/*
	 * Inode cache miss: save the hash chain version stamp and unlock
	 * the chain, so we don't deadlock in vn_alloc.
	 */
	XFS_IGETINFO.ig_missed++;
	version = ih->ih_version;
	XFS_IHUNLOCK(ih);

	/*
	 * Read the disk inode attributes into a new inode structure and get
	 * a new vnode for it.  Initialize the inode lock so we can idestroy
	 * it soon if it's a dup.  This should also initialize i_dev, i_ino,
	 * i_bno, and i_index;
	 */
	ip = xfs_iread(mp, tp, ino);
#ifdef NOTYET
	vp = vn_alloc(&xfs_vnodeops, mp->m_vfsp, IFTOVT(ip->i_d.di_mode),
		      ip->i_u2.iu_rdev, ip);
#endif

#ifdef NOTYET
	mrinit(&ip->i_lock, makesname(name, "xino", (int)vp->v_number));
	initnsema(&ip->i_flock, 1, makesname(name, "fino", vp->v_number));
	initnsema(&ip->i_pinsema, 0, makesname(name, "pino", vp->v_number));
#else
	mrinit(&ip->i_lock, makesname(name, "xino", (int)0));
	initnsema(&ip->i_flock, 1, makesname(name, "fino", 0));
	initnsema(&ip->i_pinsema, 0, makesname(name, "pino", 0));
#endif
	xfs_inode_item_init(ip, mp);
	xfs_ilock(ip, (int)flags);

	/*
	 * Put ip on its hash chain, unless someone else hashed a duplicate
	 * after we released the hash lock.
	 */
	XFS_IHLOCK(ih);
	if (ih->ih_version != version) {
		for (iq = ih->ih_next; iq != NULL; iq = iq->i_next) {
			if (iq->i_ino == ino) {
				XFS_IHUNLOCK(ih);
#ifdef NOTYET
				vn_free(vp);
#endif
				xfs_idestroy(ip);
				XFS_IGETINFO.ig_dup++;
				goto again;
			}
		}
	}

	/*
	 * These values _must_ be set before releasing ihlock!
	 */
	ip->i_vnode = vp;
	ip->i_hash = ih;
	if (iq = ih->ih_next) {
		iq->i_prevp = &ip->i_next;
	}
	ip->i_next = iq;
	ip->i_prevp = &ih->ih_next;
	ih->ih_next = ip;
	ih->ih_version++;
	XFS_IHUNLOCK(ih);

	/*
	 * Link ip to its mount and thread it on the mount's inode list.
	 */
	ip->i_mount = mp;
	s = XFS_MOUNT_ILOCK(mp);
	if (iq = mp->m_inodes) {
		iq->i_mprevp = &ip->i_mnext;
	}
	ip->i_mnext = iq;
	ip->i_mprevp = &mp->m_inodes;
	mp->m_inodes = ip;
	XFS_MOUNT_IUNLOCK(mp, s);

out:
	return ip;
}

/*
 * Look for the inode corresponding to the given ino in the hash table.
 * If it is there and its i_transp pointer matches tp, return it.
 * Otherwise, return NULL.
 */
xfs_inode_t *
xfs_inode_incore(xfs_mount_t *mp, xfs_ino_t ino, xfs_trans_t *tp)
{
	xfs_ihash_t	*ih;
	xfs_inode_t	*ip;
	xfs_inode_t	*iq;
	vnode_t		*vp;
	ulong		version;
	vmap_t		vmap;
	int		s;

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
 */
void
xfs_iput(xfs_inode_t *ip)
{
	xfs_iunlock(ip);
#ifdef NOTYET
	vn_rele(XFS_ITOV(ip));
#endif
}

/*
 * It's not clear what we need to do here.
 * If the link count has gone to zero, we need to truncate
 * the file.  This needs to be tied in to what we do when
 * we unlink an open file.  In this case we also need to
 * mark the inode free at this point, since we couldn't when
 * it was unlinked.
 * Perhaps this shouldn't exist and we should only have
 * xfs_inactive().  It should probably clear any read-ahead
 * hints, it needs to call remapf() if we truncate the file
 * and it was mapped, we may want to sync the inode.
 */
void
xfs_iinactive(xfs_inode_t *ip)
/* ARGSUSED */
{
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
	int		s;

	/*
	 * Remove from old hash list.
	 */
	XFS_IGETINFO.ig_reclaims++;
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
	s = XFS_MOUNT_ILOCK(mp);
	if (iq = ip->i_mnext) {
		iq->i_mprevp = ip->i_mprevp;
	}
	*ip->i_mprevp = iq;
	XFS_MOUNT_IUNLOCK(mp, s);
}



/*
 * The xfs inode lock is a multi-reader lock.
 * For now we will not allow lock trips, because there
 * is no single pid we can store as the holder of a multi-reader
 * lock.
 * The flags to be passed to this routine are XFS_LOCK_SHARED
 * or XFS_LOCK_EXCL.  These corespond directly to MR_ACCESS
 * and MR_UPDATE.
 */
void
xfs_ilock(xfs_inode_t *ip, int flags)
{
	mrlock(&ip->i_lock, flags, PINOD);
}

/*
 * This is just like xfs_ilock(), except that the caller
 * is guaranteed not to sleep.  It returns 1 if it gets
 * the lock and 0 otherwise.
 */
int
xfs_ilock_nowait(xfs_inode_t *ip, int flags)
{
	if (cmrlock(&ip->i_lock, flags)) {
		return 1;
	}
	return 0;
}

/*
 * This is used to drop an inode lock acquired by xfs_ilock()
 * or xfs_ilock_nowait().  The mode in which the lock is held
 * does not matter.
 */
void
xfs_iunlock(xfs_inode_t *ip)
{
	mrunlock(&ip->i_lock);
}

/*
 * The following three routines simply manage the i_flock
 * semaphore embedded in the inode.  This semaphore synchronizes
 * processes attempting to flush the in-core inode back to disk.
 */
void
xfs_iflock(xfs_inode_t *ip)
{
	psema(&ip->i_flock, PINOD);
}

int
xfs_iflock_nowait(xfs_inode_t *ip)
{
	return (cpsema(&ip->i_flock));
}

void
xfs_ifunlock(xfs_inode_t *ip)
{
	vsema(&ip->i_flock);
}
