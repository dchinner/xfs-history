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
#ident	"$Revision: 1.31 $"

#include <xfs_os_defs.h>

#include <xfs_linux.h>

#ifdef SIM
#include <stdio.h>
#define _KERNEL 1
#endif
#include <linux/config.h>
#include <sys/types.h>
#include <sys/debug.h>
#include <sys/file.h>
#include <sys/fs_subr.h>
#include <sys/param.h>
#include <sys/kmem.h>
#include <sys/pathname.h>
#include <linux/xfs_sema.h>
#include <linux/xfs_cred.h>
#include <sys/systm.h>
#include <sys/uio.h>
#ifdef SIM
#undef _KERNEL
#endif
#include <sys/vfs.h>
#include <sys/vnode_private.h>
#include <sys/mode.h>
#include <sys/sysmacros.h>
#include <sys/cmn_err.h>

#include <xfs_types.h>		/* for XFS_BHVTOI */
#include <xfs_bmap_btree.h>	/* for XFS_BHVTOI */
#include <xfs_inum.h>		/* for XFS_BHVTOI */
#include <xfs_dir_sf.h>		/* for XFS_BHVTOI */
#include <xfs_dir.h>		/* for XFS_BHVTOI */
#include <xfs_dir2.h>		/* for XFS_BHVTOI */
#include <xfs_dir2_sf.h>	/* for XFS_BHVTOI */
#include <xfs_attr_sf.h>	/* for XFS_BHVTOI */
#include <xfs_dinode.h>		/* for XFS_BHVTOI */
#include <xfs_inode.h>		/* for XFS_BHVTOI */

#include <xfs_log.h>
#include <xfs_trans.h>
#include <xfs_sb.h>
#include <xfs_mount.h>
#include <xfs_iops.h>

#ifdef	CONFIG_XFS_VNODE_TRACING
#include <sys/ktrace.h>
#endif	/* CONFIG_XFS_VNODE_TRACING */

#ifdef SIM
#include "sim.h"
#endif

#define	VFREELIST(count)	&vfreelist[count].vf_freelist

#define LOCK_VFREELIST(list)	mutex_spinlock(&vfreelist[list].vf_lock)
#define UNLOCK_VFREELIST(l,s)	mutex_spinunlock(&vfreelist[l].vf_lock, s)

#define LOCK_VFP(listp)	       	mutex_spinlock(&(listp)->vf_lock)
#define UNLOCK_VFP(listp,s)	mutex_spinunlock(&(listp)->vf_lock, s)
#define	NESTED_LOCK_VFP(listp)	nested_spinlock(&(listp)->vf_lock)
#define	NESTED_UNLOCK_VFP(listp) nested_spinunlock(&(listp)->vf_lock)

static struct xfs_zone *vn_zone;	/* vnode heap zone */
uint64_t vn_generation;		/* vnode generation number */
atomic_t vn_vnumber;		/* # of vnodes ever allocated */
int	vn_epoch;		/* # of vnodes freed */
				/* vn_vnumber - vn_epoch == # current vnodes */
int vn_minvn;			/* minimum # vnodes before reclaiming */
static int vn_shaken;		/* damper for vn_alloc */
static unsigned int vn_coin;	/* coin for vn_alloc */
int vnode_free_ratio = 1;	/* tuneable parameter for vn_alloc */

spinlock_t	vnumber_lock = SPIN_LOCK_UNLOCKED;

/*
 * The following counters are used to manage the pool of allocated and
 * free vnodes.  vnode_free_ratio is the target ratio of free vnodes
 * to in-use vnodes.
 *
 * Whenever a vnode is needed and the number of free vnodes is above
 * (vn_vnumber - vn_epoch) * vnode_free_ratio, an attempt is made t
 * reclaim a vnode from a vnod freelist.  Otherwise, or if a short search
 * of a freelist doesn't produce a reclaimable vnode, a vnode is
 * constructed from the heap.
 *
 * It is up to vn_shake to and deconstruct free vnodes.
 */

vfreelist_t	*vfreelist;	/* pointer to array of freelist structs */
static int	vfreelistmask;	/* number of free-lists - 1 */

typedef struct vhash {
	struct vnode	*vh_vnode;
	lock_t		 vh_lock;
} vhash_t;

vhash_t	*vhash;			/* hash buckets for active vnodes */

#define VHASHMASK 127
#define VHASH(vnumber)		(&vhash[(vnumber) & VHASHMASK])

/*
 * Dedicated vnode inactive/reclaim sync semaphores.
 * Prime number of hash buckets since address is used as the key.
 */
#define NVSYNC                  37
#define vptosync(v)             (&vsync[((unsigned long)v) % NVSYNC])
sv_t vsync[NVSYNC];

/*
 * Translate stat(2) file types to vnode types and vice versa.
 * Aware of numeric order of S_IFMT and vnode type values.
 */
enum vtype iftovt_tab[] = {
	VNON, VFIFO, VCHR, VNON, VDIR, VNON, VBLK, VNON,
	VREG, VNON, VLNK, VNON, VSOCK, VNON, VNON, VNON
};

u_short vttoif_tab[] = {
	0, S_IFREG, S_IFDIR, S_IFBLK, S_IFCHR, S_IFLNK, S_IFIFO, 0, S_IFSOCK
};

void
vn_init(void)
{
        register sv_t *svp;
        register int i;

	for (svp = vsync, i = 0; i < NVSYNC; i++, svp++)
		init_sv(svp, SV_DEFAULT, "vsy", i);
}


/*
 * Clean a vnode of filesystem-specific data and prepare it for reuse.
 */
int
vn_reclaim(struct vnode *vp, int flag)
{
	int error, s;

	VOPINFO.vn_reclaim++;

	vn_trace_entry(vp, "vn_reclaim", (inst_t *)__return_address);

	/*
	 * Only make the VOP_RECLAIM call if there are behaviors
	 * to call.
	 */
	if (vp->v_fbhv != NULL) {
		VOP_RECLAIM(vp, flag, error);
		if (error)
			return error;
	}
	ASSERT(vp->v_fbhv == NULL);

	/*
	 * File system erred somewhere along the line, and there
	 * are still pages associated with the object.
	 * Remove the debris and print a warning.
	 * XXX LONG_MAX won't work for 64-bit offsets!
	 */
	if (VN_CACHED(vp) || vp->v_dpages) {
		int i;

		if (vp->v_vfsp)
			i = vp->v_vfsp->vfs_fstype;
		else
			i = 0;

		cmn_err(CE_WARN,
			"vn_reclaim: vnode 0x%x fstype %d (xfs) has unreclaimed data (pgcnt %d, dbuf %d dpages 0x%x), flag:%x",
			vp, i, 
			VN_CACHED(vp), vp->v_dbuf, vp->v_dpages, vp->v_flag);
	}

	ASSERT(vp->v_dpages == NULL && vp->v_dbuf == 0 && VN_CACHED(vp) == 0);

	s = VN_LOCK(vp);

	vp->v_flag &= (VRECLM|VWAIT|VLOCK);
	VN_UNLOCK(vp, s);

	vp->v_type = VNON;
	vp->v_fbhv = NULL;

#ifdef  CONFIG_XFS_VNODE_TRACING
	ktrace_free(vp->v_trace);

	vp->v_trace = NULL;
#endif  /* CONFIG_XFS_VNODE_TRACING */

	return 0;
}

STATIC void
vn_wakeup(struct vnode *vp)
{
	int s = VN_LOCK(vp);
	if (vp->v_flag & VWAIT) {
		sv_broadcast(vptosync(vp));
	}
	vp->v_flag &= ~(VRECLM|VWAIT);
	VN_UNLOCK(vp, s);
}


struct vnode *
vn_address(struct inode *inode)
{
	vnode_t		*vp;


	vp = (vnode_t *)(&((inode)->u.xfs_i.vnode));

	if (vp->v_inode == NULL)
		return NULL;
	/*
	 * Catch half-constructed linux-inode/vnode/xfs-inode setups.
	 */
	if (vp->v_fbhv == NULL)
		return NULL;

	return vp;
}


struct vnode *
vn_initialize(vfs_t *vfsp, struct inode *inode, int from_readinode)
{
	struct vnode	*vp;
	xfs_inode_t	*ip;
	xfs_mount_t	*mp;

	
	VOPINFO.vn_active++;

	vp = LINVFS_GET_VN_ADDRESS(inode);

	vp->v_inode = inode;

	vp->v_flag = 0;

	atomic_inc(&vn_vnumber);

	/* We never free the vnodes in the simulator, so these don't
	   get destroyed either */
	spinlock_init(&vp->v_lock, "v_lock");

	spin_lock(&vnumber_lock);
	vn_generation += 1;
	vp->v_number = vn_generation;
	spin_unlock(&vnumber_lock);

	ASSERT(vp->v_number);

	ASSERT(vp->v_dpages == NULL && vp->v_dbuf == 0 && VN_CACHED(vp) == 0);

	/* Initialize the first behavior and the behavior chain head. */
	vn_bhv_head_init(VN_BHV_HEAD(vp), "vnode");

#ifdef	CONFIG_XFS_VNODE_TRACING
	vp->v_trace = ktrace_alloc(VNODE_TRACE_SIZE, KM_SLEEP);
#endif	/* CONFIG_XFS_VNODE_TRACING */

	/*
	 * Check to see if we've been called from
	 * read_inode, and we need to "stitch" it all
	 * together right now.
	 */
	if (from_readinode) {
		mp = XFS_BHVTOM(vfsp->vfs_fbhv);	ASSERT(mp);

		if (xfs_vn_iget(vp, mp, NULL, (xfs_ino_t)inode->i_ino,
								0, &ip, 0)) {
			panic("vn_initialize: vp/0x%p inode/0x%p bad xfs_iget!",
								vp, inode);
		}

		vp->v_vfsp  = vfsp;
		vp->v_inode = inode;
		vp->v_type  = IFTOVT(ip->i_d.di_mode);
		vp->v_rdev  = MKDEV(emajor(ip->i_df.if_u2.if_rdev),
				    eminor(ip->i_df.if_u2.if_rdev));

		linvfs_set_inode_ops(inode);
	}

	vn_trace_exit(vp, "vn_initialize", (inst_t *)__return_address);

	return vp;
}

struct vnode *
vn_alloc(struct vfs *vfsp, __uint64_t ino, enum vtype type, dev_t dev)
{
	struct inode	*inode;
	struct vnode	*vp;
	xfs_ino_t	inum = (xfs_ino_t) ino;

	VOPINFO.vn_alloc++;

#ifdef	CONFIG_XFS_DEBUG
	inode = iget4_noallocate(vfsp->vfs_super, inum, NULL, NULL);
	if (inode) {
		panic("vn_alloc: Found inode/0x%p when it shouldn't be!",
								inode);
	}
#endif	/* CONFIG_XFS_DEBUG */

	inode = get_empty_inode();

	if (inode == NULL) {
		panic("vn_alloc: ENOMEM inode!");
	}

	inode->i_sb    = vfsp->vfs_super;
	inode->i_dev   = vfsp->vfs_super->s_dev;
	inode->i_ino   = inum;
	inode->i_flags = 0;
	inode->i_count = 1;
	inode->i_state = 0;

	vn_initialize(vfsp, inode, 0);

	vp = LINVFS_GET_VN_ADDRESS(inode);

	ASSERT((vp->v_flag & VPURGE) == 0);

	vp->v_vfsp  = vfsp;
	vp->v_type  = type;
	vp->v_rdev  = dev;
	vp->v_inode = inode;

	vn_trace_exit(vp, "vn_alloc", (inst_t *)__return_address);

	return vp;
}


/*
 * Free an isolated vnode, putting it at the front of a vfreelist.
 * The vnode must not have any other references.
 */
void
vn_free(struct vnode *vp)
{
	VOPINFO.vn_free++;

	vn_trace_entry(vp, "vn_free", (inst_t *)__return_address);

	ASSERT(vn_count(vp) == 1);

	ASSERT((vp->v_flag & VPURGE) == 0);

	vp->v_fbhv = NULL;
}


/*
 * Get a reference on a vnode.
 */
vnode_t *
vn_get(struct vnode *vp, vmap_t *vmap, uint flags)
{
	struct inode	*inode;
	xfs_ino_t	inum;

	VOPINFO.vn_get++;

	inode = iget(vmap->v_vfsp->vfs_super, vmap->v_ino);

	if (inode == NULL)		/* I_FREEING conflict */
		return NULL;

	inum = inode->i_ino;

	/*
	 * Verify that the linux inode we just 'grabbed'
	 * is still the one we want.
	 */
	if (vmap->v_number != vp->v_number) {
printk("vn_get: vp/0x%p inode/0x%p v_number %Ld/%Ld\n",
vp, inode, vmap->v_number, vp->v_number);

		goto fail;
	}

	if (vmap->v_epoch != vn_epoch) {
printk("vn_get: vp/0x%p inode/0x%p v_epoch %d/%d\n",
vp, inode, vmap->v_epoch, vn_epoch);

		goto fail;
	}

	if (vmap->v_ino != inum) {
printk("vn_get: vp/0x%p inode/0x%p v_ino %Ld/%Ld\n",
vp, inode, vmap->v_ino, inum);

		goto fail;
	}

	vn_trace_exit(vp, "vn_get", (inst_t *)__return_address);

	ASSERT((vp->v_flag & VPURGE) == 0);

	return vp;

fail:
	iput(inode);

	return NULL;
}


/*
 * "put" the linux inode.
 */
void
vn_put(struct vnode *vp)
{
	struct inode *inode;

	vn_trace_entry(vp, "vn_put", (inst_t *)__return_address);

	inode = LINVFS_GET_IP(vp);

	ASSERT(inode);

	iput(inode);
}


/*
 * "Temporary" routine to return the linux inode
 * hold count, after everybody else can directly
 * reference the inode (header magic!), this
 * routine is dead meat..
 */
int
vn_count(struct vnode *vp)
{
	struct inode *inode;

	inode = LINVFS_GET_IP(vp);

	ASSERT(inode);

	return inode->i_count;
}


/*
 * "Temporary" routine to return the linux inode
 * 'pages mapped' state, after everybody else can
 * directly reference the inode (header magic!),
 * this routine is dead meat..
 */
int
vn_mapped(struct vnode *vp)
{
	struct inode *inode;

	inode = LINVFS_GET_IP(vp);

	ASSERT(inode);

	return inode->i_data.i_mmap != NULL;
}


/*
 * "Temporary" routine to return the linux inode
 * # pages cached count, after everybody else can
 * directly reference the inode (header magic!),
 * this routine is dead meat..
 */
int
vn_cached(struct vnode *vp)
{
	struct inode *inode;

	inode = LINVFS_GET_IP(vp);

	ASSERT(inode);

	return inode->i_data.nrpages;
}


/*
 * "hash" the linux inode.
 */
void
vn_insert_in_linux_hash(struct vnode *vp)
{
	struct inode *inode;

	vn_trace_entry(vp, "vn_insert_in_linux_hash",
				(inst_t *)__return_address);

	inode = LINVFS_GET_IP(vp);

	ASSERT(inode);

	ASSERT(list_empty(&inode->i_hash));

	insert_inode_hash(inode);	/* Add to i_hash	*/
	mark_inode_dirty(inode);	/* Add to i_list	*/
}


/*
 * "revalidate" the linux inode.
 */
int
vn_revalidate(struct vnode *vp, int flags)
{
	int		error;
	struct inode	*inode;
	vattr_t		va;

	vn_trace_entry(vp, "vn_revalidate", (inst_t *)__return_address);

	va.va_mask = AT_STAT|AT_GENCOUNT;

	ASSERT(vp->v_bh.bh_first != NULL);

	VOP_GETATTR(vp, &va, flags, NULL, error);

	if (! error) {
		inode = LINVFS_GET_IP(vp);

		ASSERT(inode);

		inode->i_mode       = VTTOIF(va.va_type) | va.va_mode;
		inode->i_nlink      = va.va_nlink;
		inode->i_uid        = va.va_uid;
		inode->i_gid        = va.va_gid;
		inode->i_rdev       = va.va_rdev;
		inode->i_size       = va.va_size;
		inode->i_blocks     = va.va_nblocks;
		inode->i_blksize    = va.va_blksize;
		inode->i_atime      = va.va_atime.tv_sec;
		inode->i_mtime      = va.va_mtime.tv_sec;
		inode->i_ctime      = va.va_ctime.tv_sec;
		inode->i_generation = va.va_gencount;
	} else {
		vn_trace_exit(vp, "vn_revalidate.error",
					(inst_t *)__return_address);
	}

	return -error;
}


/*
 * purge a vnode from the cache
 * At this point the vnode is guaranteed to have no references (vn_count == 0)
 * The caller has to make sure that there are no ways someone could
 * get a handle (via vn_get) on the vnode (usually done via a mount/vfs lock).
 */
void
vn_purge(struct vnode *vp, vmap_t *vmap)
{
	unsigned	s;

	vn_trace_entry(vp, "vn_purge", (inst_t *)__return_address);

	ASSERT(vp->v_flag & VPURGE);

again:
	/*
	 * Check whether vp has already been reclaimed since our caller
	 * sampled its version while holding a filesystem cache lock that
	 * its VOP_RECLAIM function acquires.
	 */
	NESTED_VN_LOCK(vp);
	if (vp->v_number != vmap->v_number) {
		NESTED_VN_UNLOCK(vp);
		return;
	}

	/*
	 * If vp is being reclaimed or inactivated, wait until it is inert,
	 * then proceed.  Can't assume that vnode is actually reclaimed
	 * just because the reclaimed flag is asserted -- a vn_alloc
	 * reclaim can fail.
	 */
	if (vp->v_flag & (VINACT | VRECLM)) {
		ASSERT(vn_count(vp) == 0);
		vp->v_flag |= VWAIT;
		sv_wait(vptosync(vp), PINOD, &vp->v_lock, 0);
		goto again;
	}

	/*
	 * Another process could have raced in and gotten this vnode...
	 */
	if (vn_count(vp) > 0) {
		VN_UNLOCK(vp, s);
		return;
	}

	VOPINFO.vn_active--;
	vp->v_flag |= VRECLM;
	NESTED_VN_UNLOCK(vp);

	/*
	 * Call VOP_RECLAIM and clean vp. The FSYNC_INVAL flag tells
	 * vp's filesystem to flush and invalidate all cached resources.
	 * When vn_reclaim returns, vp should have no private data,
	 * either in a system cache or attached to v_data.
	 */
	if (vn_reclaim(vp, FSYNC_INVAL) != 0)
		panic("vn_purge: cannot reclaim");

	/*
	 * Wakeup anyone waiting for vp to be reclaimed.
	 */
	vn_wakeup(vp);
}

/*
 * Add a reference to a referenced vnode.
 */
struct vnode *
vn_hold(struct vnode *vp)
{
	register int s = VN_LOCK(vp);
	struct inode *inode;

	VOPINFO.vn_hold++;

	inode = LINVFS_GET_IP(vp);

	inode = igrab(inode);

	ASSERT(inode);

	VN_UNLOCK(vp, s);

	return vp;
}


/*
 * Release a vnode.  Decrements reference count and calls
 * VOP_INACTIVE on last reference.
 */
void
vn_rele(struct vnode *vp)
{
	int	s;
	int	vcnt;
	/* REFERENCED */
	int cache;

	VOPINFO.vn_rele++;

	s = VN_LOCK(vp);

	vcnt = vn_count(vp);

	ASSERT(vcnt > 0);

	/*
	 * Note that we are allowing for the fact that the
	 * i_count won't be decremented until we do the
	 * 'iput' below.
	 */
	if (vcnt == 1) {
		/*
		 * It is absolutely, positively the case that
		 * the lock manager will not be releasing vnodes
		 * without first having released all of its locks.
		 */
		ASSERT(!(vp->v_flag & VLOCKHOLD));

		/*
		 * As soon as we turn this on, noone can find us in vn_get
		 * until we turn off VINACT or VRECLM
		 */
		vp->v_flag |= VINACT;
		VN_UNLOCK(vp, s);

		/*
		 * Do not make the VOP_INACTIVE call if there
		 * are no behaviors attached to the vnode to call.
		 */
		if (vp->v_fbhv != NULL) {
			VOP_INACTIVE(vp, get_current_cred(), cache);
		}

		s = VN_LOCK(vp);

		vp->v_flag &= ~(VINACT|VWAIT|VRECLM|VGONE);

		VN_UNLOCK(vp, s);

		vn_trace_exit(vp, "vn_rele", (inst_t *)__return_address);

		/*
		 * Don't reference the vnode after the 'put',
		 * it'll be gone.
		 */
		vn_put(vp);

		return;
	}

	VN_UNLOCK(vp, s);

	vn_trace_exit(vp, "vn_rele", (inst_t *)__return_address);

	/*
	 * The vnode will still be around after the 'put',
	 * but keep the same trace sequence for consistency.
	 */
	vn_put(vp);
}


/*
 * Finish the removal of a vnode.
 * A 'special' case of vn_rele.
 */
void
vn_remove(struct vnode *vp)
{
	int	s;
	/* REFERENCED */
	int cache;
	vmap_t  vmap;

	vn_trace_entry(vp, "vn_remove", (inst_t *)__return_address);

	VOPINFO.vn_remove++;

	s = VN_LOCK(vp);

	ASSERT(vn_count(vp) == 0);

	/*
	 * It is absolutely, positively the case that
	 * the lock manager will not be releasing vnodes
	 * without first having released all of its locks.
	 */
	ASSERT(!(vp->v_flag & VLOCKHOLD));

	/*
	 * As soon as we turn this on, noone can find us in vn_get
	 * until we turn off VINACT or VRECLM
	 */
	vp->v_flag |= VINACT;
	VN_UNLOCK(vp, s);

	/*
	 * Do not make the VOP_INACTIVE call if there
	 * are no behaviors attached to the vnode to call.
	 */
	if (vp->v_fbhv != NULL) {
		VOP_INACTIVE(vp, get_current_cred(), cache);
	}

	s = VN_LOCK(vp);

	vp->v_flag &= ~(VINACT|VWAIT|VRECLM|VGONE);

	VN_UNLOCK(vp, s);

	vn_trace_exit(vp, "vn_remove", (inst_t *)__return_address);

	/*
	 * After the following purge the vnode
	 * will no longer exist.
	 */
	VMAP(vp, XFS_BHVTOI(vp->v_fbhv), vmap);

	vn_purge(vp, &vmap);

	vp->v_inode = NULL;		/* No more references to inode */
}


#ifdef	CONFIG_XFS_VNODE_TRACING

#define	cpuid smp_processor_id
#define	current_pid() (current->pid)

/*
 * Vnode tracing code.
 */
void
vn_trace_entry(vnode_t *vp, char *func, inst_t *ra)
{
	ktrace_enter(	vp->v_trace,
/*  0 */		(void *)(__psint_t)VNODE_KTRACE_ENTRY,
/*  1 */		(void *)func,
/*  2 */		0,
/*  3 */		(void *)(vp->v_inode ? vp->v_inode->i_count : -9),
/*  4 */		(void *)ra,
/*  5 */		(void *)(__psunsigned_t)vp->v_flag,
/*  6 */		(void *)(__psint_t)cpuid(),
/*  7 */		(void *)(__psint_t)current_pid(),
/*  8 */		(void *)__return_address,
/*  9 */		0, 0, 0, 0, 0, 0, 0);
}

void
vn_trace_exit(vnode_t *vp, char *func, inst_t *ra)
{
	ktrace_enter(	vp->v_trace,
/*  0 */		(void *)(__psint_t)VNODE_KTRACE_EXIT,
/*  1 */		(void *)func,
/*  2 */		0,
/*  3 */		(void *)(vp->v_inode ? vp->v_inode->i_count : -9),
/*  4 */		(void *)ra,
/*  5 */		(void *)(__psunsigned_t)vp->v_flag,
/*  6 */		(void *)(__psint_t)cpuid(),
/*  7 */		(void *)(__psint_t)current_pid(),
/*  8 */		(void *)__return_address,
/*  9 */		0, 0, 0, 0, 0, 0, 0);
}

void
vn_trace_hold(vnode_t *vp, char *file, int line, inst_t *ra)
{
	ktrace_enter(	vp->v_trace,
/*  0 */		(void *)(__psint_t)VNODE_KTRACE_HOLD,
/*  1 */		(void *)file,
/*  2 */		(void *)(__psint_t)line,
/*  3 */		(void *)(vp->v_inode ? vp->v_inode->i_count : -9),
/*  4 */		(void *)ra,
/*  5 */		(void *)(__psunsigned_t)vp->v_flag,
/*  6 */		(void *)(__psint_t)cpuid(),
/*  7 */		(void *)(__psint_t)current_pid(),
/*  8 */		(void *)__return_address,
/*  9 */		0, 0, 0, 0, 0, 0, 0);
}

void
vn_trace_ref(vnode_t *vp, char *file, int line, inst_t *ra)
{
	ktrace_enter(	vp->v_trace,
/*  0 */		(void *)(__psint_t)VNODE_KTRACE_REF,
/*  1 */		(void *)file,
/*  2 */		(void *)(__psint_t)line,
/*  3 */		(void *)(vp->v_inode ? vp->v_inode->i_count : -9),
/*  4 */		(void *)ra,
/*  5 */		(void *)(__psunsigned_t)vp->v_flag,
/*  6 */		(void *)(__psint_t)cpuid(),
/*  7 */		(void *)(__psint_t)current_pid(),
/*  8 */		(void *)__return_address,
/*  9 */		0, 0, 0, 0, 0, 0, 0);
}

void
vn_trace_rele(vnode_t *vp, char *file, int line, inst_t *ra)
{
	ktrace_enter(	vp->v_trace,
/*  0 */		(void *)(__psint_t)VNODE_KTRACE_RELE,
/*  1 */		(void *)file,
/*  2 */		(void *)(__psint_t)line,
/*  3 */		(void *)(vp->v_inode ? vp->v_inode->i_count : -9),
/*  4 */		(void *)ra,
/*  5 */		(void *)(__psunsigned_t)vp->v_flag,
/*  6 */		(void *)(__psint_t)cpuid(),
/*  7 */		(void *)(__psint_t)current_pid(),
/*  8 */		(void *)__return_address,
/*  9 */		0, 0, 0, 0, 0, 0, 0);
}
#endif	/* CONFIG_XFS_VNODE_TRACING */
