#ident	"$Revision: 1.83 $"

#include <limits.h>
#include <sys/param.h>
#ifdef SIM
#define _KERNEL
#endif /* SIM */
#include <sys/buf.h>
#include <sys/sysmacros.h>
#include <sys/vfs.h>
#include <sys/vnode.h>
#include <sys/grio.h>
#include <sys/uuid.h>
#ifdef SIM
#undef _KERNEL
#endif /* SIM */
#include <sys/debug.h>
#include <sys/errno.h>
#include <sys/kmem.h>
#include <sys/open.h>
#include <sys/cred.h>
#ifdef SIM
#include <bstring.h>
#else
#include <sys/systm.h>
#include <sys/conf.h>
#endif /* SIM */
#include <stddef.h>
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
#include "xfs_dinode.h"
#include "xfs_inode_item.h"
#include "xfs_inode.h"
#include "xfs_dir.h"
#include "xfs_alloc.h"
#include "xfs_rtalloc.h"
#include "xfs_bmap.h"
#include "xfs_error.h"
#include "xfs_bit.h"

#ifdef SIM
#include "sim.h"
#endif /* SIM */


STATIC int	xfs_mod_incore_sb_unlocked(xfs_mount_t *, uint, int);
STATIC void	xfs_sb_relse(buf_t *);

/*
 * Return a pointer to an initialized xfs_mount structure.
 */
xfs_mount_t *
xfs_mount_init(void)
{
	xfs_mount_t *mp;

	mp = kmem_zalloc(sizeof(*mp), 0);
	mp->m_fsname = kmem_alloc(PATH_MAX, 0);

	initnlock(&mp->m_ail_lock, "xfs_ail");
	initnlock(&mp->m_async_lock, "xfs_async");
	initnsema(&mp->m_ilock, 1, "xfs_ilock");
	initnsema(&mp->m_growlock, 1, "xfs_grow");
	initnlock(&mp->m_ipinlock, "xfs_ipin");
	initnlock(&mp->m_sb_lock, "xfs_sb");
	/*
	 * Initialize the AIL.
	 */
	xfs_trans_ail_init(mp);

	return mp;
}	/* xfs_mount_init */
	
/*
 * Free up the resources associated with a mount structure.  Assume that
 * the structure was initially zeroed, so we can tell which fields got
 * initialized.
 */
void
xfs_mount_free(xfs_mount_t *mp)
{
	if (mp->m_ihashmask)
		xfs_ihash_free(mp);

	if (mp->m_perag) {
		mrfree(&mp->m_peraglock);
		kmem_free(mp->m_perag,
			  sizeof(xfs_perag_t) * mp->m_sb.sb_agcount);
	}
	
	freesplock(mp->m_ail_lock);
	freesplock(mp->m_async_lock);
	freesema(&mp->m_ilock);
	freesema(&mp->m_growlock);
	freesplock(mp->m_ipinlock);
	freesplock(mp->m_sb_lock);

	kmem_free(mp->m_fsname, PATH_MAX);
	kmem_free(mp, sizeof(xfs_mount_t));
}	/* xfs_mount_free */


/*
 * xfs_mountfs		XXXjleong Needs more error checking
 *
 * This function does the following on an initial mount of a file system:
 *	- reads the superblock from disk and init the mount struct
 *	- init mount struct realtime fields
 *	- allocate inode hash table for fs
 *	- init directory manager
 *	- perform recovery and init the log manager
 */
int
xfs_mountfs(vfs_t *vfsp, dev_t dev)
{
	buf_t		*bp;
	xfs_sb_t	*sbp;
	int		error = 0;
	int		s, i, brsize;
	xfs_mount_t	*mp;
	xfs_inode_t	*rip;
	vnode_t		*rvp = 0;

	if (vfsp->vfs_flag & VFS_REMOUNT)   /* Can't remount xFS filesystems */
		return 0;

	mp = XFS_VFSTOM(vfsp);

	ASSERT(mp->m_sb_bp == 0);

	/*
	 * Allocate a (locked) buffer to hold the superblock.
	 * This will be kept around at all time to optimize
	 * access to the superblock.
	 */
	bp = ngetrbuf(BBTOB(BTOBB(sizeof(xfs_sb_t))));
	ASSERT(bp != NULL);
	ASSERT((bp->b_flags & B_BUSY) && valusema(&bp->b_lock) <= 0);

	/*
	 * Initialize and read in the superblock buffer.
	 */
	bp->b_edev = dev;
	bp->b_relse = xfs_sb_relse;
	bp->b_blkno = XFS_SB_DADDR;		
	bp->b_flags |= B_READ;
	bdstrat(bmajor(dev), bp);
	if (error = iowait(bp)) {
		ASSERT(error == 0);
		goto bad;
	}

	/*
	 * Initialize the mount structure from the superblock.  If the log
	 * device and data device have the same device number, the log is
	 * internal.  Consequently, the sb_logstart should be non-zero.  If
	 * we have a zero sb_logstart in this case, we may be trying to mount
	 * a volume filesystem in a non-volume manner.
	 */
	sbp = XFS_BUF_TO_SBP(bp);
	if ((sbp->sb_magicnum != XFS_SB_MAGIC)		||
	    (sbp->sb_versionnum != XFS_SB_VERSION)	||
	    (sbp->sb_logstart == 0 && mp->m_logdev == mp->m_dev)) {
		error = XFS_ERROR(EINVAL);
		goto bad;
	}
#ifndef SIM
	/*
	 * Except for from mkfs, don't let partly-mkfs'ed filesystems mount.
	 */
	if (sbp->sb_inprogress) {
		error = XFS_ERROR(EINVAL);
		goto bad;
	}
#endif
	mp->m_sb_bp = bp;
	mp->m_sb = *sbp;				/* bcopy structure */
	brelse(bp);
	ASSERT(valusema(&bp->b_lock) > 0);

	sbp = &(mp->m_sb);
	mp->m_agfrotor = mp->m_agirotor = mp->m_rbmrotor = 0;
	mp->m_blkbit_log = sbp->sb_blocklog + XFS_NBBYLOG;
	mp->m_blkbb_log = sbp->sb_blocklog - BBSHIFT;
	mp->m_agno_log = xfs_highbit32(sbp->sb_agcount - 1) + 1;
	mp->m_blockmask = sbp->sb_blocksize - 1;
	mp->m_blockwsize = sbp->sb_blocksize >> XFS_WORDLOG;
	mp->m_blockwmask = mp->m_blockwsize - 1;
	for (i = 0; i < 2; i++) {
		mp->m_alloc_mxr[i] = XFS_BTREE_BLOCK_MAXRECS(sbp->sb_blocksize,
			xfs_alloc, i == 0);
		mp->m_alloc_mnr[i] = XFS_BTREE_BLOCK_MINRECS(sbp->sb_blocksize,
			xfs_alloc, i == 0);
	}
	for (i = 0; i < 2; i++) {
		mp->m_bmap_dmxr[i] = XFS_BTREE_BLOCK_MAXRECS(sbp->sb_blocksize,
			xfs_bmbt, i == 0);
		mp->m_bmap_dmnr[i] = XFS_BTREE_BLOCK_MINRECS(sbp->sb_blocksize,
			xfs_bmbt, i == 0);
	}
	brsize = XFS_BMAP_BROOT_SIZE(sbp->sb_inodesize);
	for (i = 0; i < 2; i++) {
		mp->m_bmap_dmxr[i + 2] = XFS_BTREE_BLOCK_MAXRECS(brsize,
			xfs_bmdr, i == 0);
		ASSERT(mp->m_bmap_dmxr[i + 2] >= 2);
		mp->m_bmap_dmnr[i + 2] = XFS_BTREE_BLOCK_MINRECS(brsize,
			xfs_bmdr, i == 0);
	}
	mp->m_bmap_ext_mxr = brsize / sizeof(xfs_bmbt_rec_t);
	for (i = 0; i < 2; i++) {
		mp->m_inobt_mxr[i] = XFS_BTREE_BLOCK_MAXRECS(sbp->sb_blocksize,
			xfs_inobt, i == 0);
		mp->m_inobt_mnr[i] = XFS_BTREE_BLOCK_MINRECS(sbp->sb_blocksize,
			xfs_inobt, i == 0);
	}
	xfs_alloc_compute_maxlevels(mp);
	xfs_bmap_compute_maxlevels(mp);
	xfs_ialloc_compute_maxlevels(mp);
	mp->m_bsize = XFS_FSB_TO_BB(mp, 1);
	vfsp->vfs_bsize = XFS_FSB_TO_B(mp, 1);

	/*
	 * Set the default minimum read and write sizes.
	 */
	if (sbp->sb_blocklog > XFS_READIO_LOG) {
		mp->m_readio_log = sbp->sb_blocklog;
	} else {
		mp->m_readio_log = XFS_READIO_LOG;
	}
	mp->m_readio_blocks = 1 << (mp->m_readio_log - sbp->sb_blocklog);
	if (sbp->sb_blocklog > XFS_WRITEIO_LOG) {
		mp->m_writeio_log = sbp->sb_blocklog;
	} else {
		mp->m_writeio_log = XFS_WRITEIO_LOG;
	}
	mp->m_writeio_blocks = 1 << (mp->m_writeio_log - sbp->sb_blocklog);

	/*
	 * Check that the data (and log if separate) are an ok size.
	 */
	bp = read_buf(mp->m_dev, XFS_FSB_TO_BB(mp, mp->m_sb.sb_dblocks) - 1,
		1, 0);
	ASSERT(bp);
	error = geterror(bp);
	brelse(bp);
	if (error == ENOSPC)
		return XFS_ERROR(E2BIG);
	else if (error)
		return XFS_ERROR(error);
	if (mp->m_logdev && mp->m_logdev != mp->m_dev) {
		bp = read_buf(mp->m_logdev,
			XFS_FSB_TO_BB(mp, mp->m_sb.sb_logblocks) - 1, 1, 0);
		ASSERT(bp);
		error = geterror(bp);
		brelse(bp);
		if (error == ENOSPC)
			return XFS_ERROR(E2BIG);
		else if (error)
			return XFS_ERROR(error);
	}
	/*
	 * Initialize realtime fields in the mount structure
	 */
	if (sbp->sb_rblocks) {
		mp->m_rsumlevels = sbp->sb_rextslog + 1;
		mp->m_rsumsize = sizeof(xfs_suminfo_t) * mp->m_rsumlevels * sbp->sb_rbmblocks;
		mp->m_rsumsize = roundup(mp->m_rsumsize, sbp->sb_blocksize);
		mp->m_rbmip = mp->m_rsumip = NULL;
		/*
		 * Check that the realtime section is an ok size.
		 */
		bp = read_buf(mp->m_rtdev,
			XFS_FSB_TO_BB(mp, mp->m_sb.sb_rblocks) - 1, 1, 0);
		ASSERT(bp);
		error = geterror(bp);
		brelse(bp);
		if (error == ENOSPC)
			return XFS_ERROR(E2BIG);
		else if (error)
			return XFS_ERROR(error);
	}
	/*
	 *  Copies the low order bits of the timestamp and the randomly
	 *  set "sequence" number out of a UUID.
	 */
	uuid_getnodeuniq (&sbp->sb_uuid, mp->m_fixedfsid);
	/*
	 *  The vfs structure needs to have a file system independent
	 *  way of checking for the invariant file system ID.  Since it
	 *  can't look at mount structures it has a pointer to the data
	 *  in the mount structure.
	 *
	 *  File systems that don't support user level file handles (i.e.
	 *  all of them except for XFS) will leave vfs_altfsid as NULL.
	 */
	vfsp->vfs_altfsid = (fsid_t *) mp->m_fixedfsid;
	mp->m_dmevmask = 0;	/* not persistent; set after each mount */

	/*
	 * Allocate and initialize the inode hash table for this
	 * file system.
	 */
	xfs_ihash_init(mp);

	/*
	 * Allocate and initialize the per-ag data.
	 */
	mrinit(&mp->m_peraglock, "xperag");
	mp->m_perag =
		kmem_zalloc(sbp->sb_agcount * sizeof(xfs_perag_t), KM_SLEEP);

	/*
	 * log's mount-time initialization. Perform 1st part recovery if needed
	 */
	if (sbp->sb_logblocks > 0) {		/* check for volume case */
		error = xfs_log_mount(mp, mp->m_logdev,
				      XFS_FSB_TO_DADDR(mp, sbp->sb_logstart),
				      XFS_FSB_TO_BB(mp, sbp->sb_logblocks));
		if (error > 0) {
			return (XFS_ERROR(error));
		}
	} else {	/* No log has been defined */
		return (XFS_ERROR(EINVAL));
	}

#ifdef SIM
	/*
	 * Mkfs calls mount before the root inode is allocated.
	 */
	if (sbp->sb_rootino != NULLFSINO)
#endif /* SIM */
	{
		/*
		 * Get and sanity-check the root inode.
		 * Save the pointer to it in the mount structure.
		 */
		rip = xfs_iget(mp, NULL, sbp->sb_rootino, XFS_ILOCK_EXCL);
		rvp = XFS_ITOV(rip);
		if ((rip->i_d.di_mode & IFMT) != IFDIR) {
			vmap_t vmap;

			VMAP(rvp, vmap);
			prdev("Root inode %d is not a directory",
			      rip->i_dev, rip->i_ino);
			xfs_iunlock(rip, XFS_ILOCK_EXCL);
			VN_RELE(rvp);
			vn_purge(rvp, &vmap);
			error = XFS_ERROR(EINVAL);
			return (error);
		}
		s = VN_LOCK(rvp);
		rvp->v_flag |= VROOT;
		VN_UNLOCK(rvp, s);
		mp->m_rootip = rip;				/* save it */
		xfs_iunlock(rip, XFS_ILOCK_EXCL);
	}

	/*
	 * Initialize realtime inode pointers in the mount structure
	 */
	if (sbp->sb_rbmino != NULLFSINO) {
		mp->m_rbmip = xfs_iget(mp, NULL, sbp->sb_rbmino, NULL);
		ASSERT(sbp->sb_rsumino != NULLFSINO);
		mp->m_rsumip = xfs_iget(mp, NULL, sbp->sb_rsumino, NULL);
	}

	/*
	 * Finish recovering the file system.  This part needed to be
	 * delayed until after the root and real-time bitmap inodes
	 * were consistently read in.
	 */
#ifndef SIM
	xfs_log_mount_finish(mp);
#endif

	/*
	 * Initialize directory manager's entries.
	 */
	xfs_dir_mount(mp);

	return 0;
bad:
	brelse(bp);
	freerbuf(bp);
	return error;
}	/* xfs_mountfs */

#ifdef SIM
/*
 * xfs_mount is the function used by the simulation environment
 * to start the file system.
 */
xfs_mount_t *
xfs_mount(dev_t dev, dev_t logdev, dev_t rtdev)
{
	int		error;
	xfs_mount_t	*mp;
	vfs_t		*vfsp;
	extern vfsops_t	xfs_vfsops;

	mp = xfs_mount_init();
	vfsp = kmem_zalloc(sizeof(vfs_t), KM_SLEEP);
	VFS_INIT(vfsp, &xfs_vfsops, NULL);
	mp->m_vfsp = vfsp;
	vfsp->vfs_data = mp;
	mp->m_dev = dev;
	mp->m_rtdev = rtdev;
	mp->m_logdev = logdev;
	vfsp->vfs_dev = dev;


        error = xfs_mountfs(vfsp, dev);
	if (error) {
		kmem_free(mp, sizeof(*mp));
		return 0;
	}

	/*
	 * Call the log's mount-time initialization.
	 */
	if (logdev) {
		xfs_sb_t *sbp;
		xfs_fsblock_t logstart;

		sbp = XFS_BUF_TO_SBP(mp->m_sb_bp);
		logstart = sbp->sb_logstart;
		xfs_log_mount(mp, logdev, XFS_FSB_TO_DADDR(mp, logstart),
			      XFS_FSB_TO_BB(mp, sbp->sb_logblocks));
	}

	return mp;
}
#endif /* SIM */

/*
 * xfs_unmountfs
 */
int
xfs_unmountfs(xfs_mount_t *mp, int vfs_flags, struct cred *cr)
{
	buf_t	*bp;
	dev_t	dev;
	int	error;

	xfs_iflush_all(mp, 0);
	bflush(mp->m_dev);
	if (mp->m_rtdev)
		bflush(mp->m_rtdev);
	bp = xfs_getsb(mp, 0);
	bp->b_flags &= ~(B_DONE | B_READ);
	bp->b_flags |= B_WRITE;
	bwait_unpin(bp);
	dev = mp->m_dev;
	bdstrat(bmajor(mp->m_dev), bp);
	error = iowait(bp);
	ASSERT(error == 0);
	
	xfs_log_unmount(mp);			/* Done! No more fs ops. */

	if (mp->m_ddevp) {
		VOP_CLOSE(mp->m_ddevp, vfs_flags, 1, 0, cr);
		VN_RELE(mp->m_ddevp);
	}
	if (mp->m_rtdevp) {
		VOP_CLOSE(mp->m_rtdevp, vfs_flags, 1, 0, cr);
		VN_RELE(mp->m_rtdevp);
	}
	if (mp->m_logdevp && mp->m_logdevp != mp->m_ddevp) {
		VOP_CLOSE(mp->m_logdevp, vfs_flags, 1, 0, cr);
		VN_RELE(mp->m_logdevp);
	}

	nfreerbuf(bp);

	/*
	 * All inodes from this mount point should be freed.
	 */
	ASSERT( mp->m_inodes == NULL );

	xfs_mount_free(mp);
}	/* xfs_unmountfs */


#ifdef SIM
/*
 * xfs_umount is the function used by the simulation environment
 * to stop the file system.
 */
void
xfs_umount(xfs_mount_t *mp)
{
	xfs_unmountfs(mp, 0, NULL);
}
#endif /* SIM */


/*
 * xfs_mod_sb() can be used to copy arbitrary changes to the
 * in-core superblock into the superblock buffer to be logged.
 * It does not provide the higher level of locking that is
 * needed to protect the in-core superblock from concurrent
 * access.
 */
void
xfs_mod_sb(xfs_trans_t *tp, int fields)
{
	buf_t		*bp;
	int		first;
	int		last;
	xfs_mount_t	*mp;
	xfs_sb_t	*sbp;
	static const int offsets[] = {
		offsetof(xfs_sb_t, sb_magicnum),
		offsetof(xfs_sb_t, sb_blocksize),
		offsetof(xfs_sb_t, sb_dblocks),
		offsetof(xfs_sb_t, sb_rblocks),
		offsetof(xfs_sb_t, sb_rextents),
		offsetof(xfs_sb_t, sb_uuid),
		offsetof(xfs_sb_t, sb_logstart),
		offsetof(xfs_sb_t, sb_rootino),
		offsetof(xfs_sb_t, sb_rbmino),
		offsetof(xfs_sb_t, sb_rsumino),
		offsetof(xfs_sb_t, sb_rextsize),
		offsetof(xfs_sb_t, sb_agblocks),
		offsetof(xfs_sb_t, sb_agcount),
		offsetof(xfs_sb_t, sb_rbmblocks),
		offsetof(xfs_sb_t, sb_logblocks),
		offsetof(xfs_sb_t, sb_versionnum),
		offsetof(xfs_sb_t, sb_sectsize),
		offsetof(xfs_sb_t, sb_inodesize),
		offsetof(xfs_sb_t, sb_inopblock),
		offsetof(xfs_sb_t, sb_fname[0]),
		offsetof(xfs_sb_t, sb_fpack[0]),
		offsetof(xfs_sb_t, sb_blocklog),
		offsetof(xfs_sb_t, sb_sectlog),
		offsetof(xfs_sb_t, sb_inodelog),
		offsetof(xfs_sb_t, sb_inopblog),
		offsetof(xfs_sb_t, sb_agblklog),
		offsetof(xfs_sb_t, sb_rextslog),
		offsetof(xfs_sb_t, sb_inprogress),
		offsetof(xfs_sb_t, sb_icount),
		offsetof(xfs_sb_t, sb_ifree),
		offsetof(xfs_sb_t, sb_fdblocks),
		offsetof(xfs_sb_t, sb_frextents),
		sizeof(xfs_sb_t)
	};
 
	mp = tp->t_mountp;
	bp = xfs_trans_getsb(tp, 0);
	sbp = XFS_BUF_TO_SBP(bp);
	xfs_btree_offsets(fields, offsets, XFS_SB_NUM_BITS, &first, &last);
	bcopy((caddr_t)&mp->m_sb + first, (caddr_t)sbp + first, last - first + 1);
	xfs_trans_log_buf(tp, bp, first, last);
}


/*
 * xfs_mod_incore_sb_unlocked() is a utility routine common used to apply
 * a delta to a specified field in the in-core superblock.  Simply
 * switch on the field indicated and apply the delta to that field.
 * Fields are not allowed to dip below zero, so if the delta would
 * do this do not apply it and return EINVAL.
 *
 * The SB_LOCK must be held when this routine is called.
 */
STATIC int
xfs_mod_incore_sb_unlocked(xfs_mount_t *mp, uint field, int delta)
{
	register int		scounter; /* short counter for 32 bit fields */
	register long long	lcounter; /* long counter for 64 bit fields */

	/*
	 * Obtain the in-core superblock spin lock and switch
	 * on the indicated field.  Apply the delta to the
	 * proper field.  If the fields value would dip below
	 * 0, then do not apply the delta and return EINVAL.
	 */
	switch (field) {
	case XFS_SB_ICOUNT:
		lcounter = mp->m_sb.sb_icount;
		lcounter += delta;
		if (lcounter < 0) {
			ASSERT(0);
			return (XFS_ERROR(EINVAL));
		}
		mp->m_sb.sb_icount = lcounter;
		return (0);
	case XFS_SB_IFREE:
		lcounter = mp->m_sb.sb_ifree;
		lcounter += delta;
		if (lcounter < 0) {
			ASSERT(0);
			return (XFS_ERROR(EINVAL));
		}
		mp->m_sb.sb_ifree = lcounter;
		return (0);
	case XFS_SB_FDBLOCKS:
		lcounter = mp->m_sb.sb_fdblocks;
		lcounter += delta;
		if (lcounter < 0) {
			return (XFS_ERROR(ENOSPC));
		}
		mp->m_sb.sb_fdblocks = lcounter;
		return (0);
	case XFS_SB_FREXTENTS:
		lcounter = mp->m_sb.sb_frextents;
		lcounter += delta;
		if (lcounter < 0) {
			return (XFS_ERROR(ENOSPC));
		}
		mp->m_sb.sb_frextents = lcounter;
		return (0);
	case XFS_SB_DBLOCKS:
		lcounter = mp->m_sb.sb_dblocks;
		lcounter += delta;
		if (lcounter < 0) {
			ASSERT(0);
			return (XFS_ERROR(EINVAL));
		}
		mp->m_sb.sb_dblocks = lcounter;
		return (0);
	case XFS_SB_AGCOUNT:
		scounter = mp->m_sb.sb_agcount;
		scounter += delta;
		if (scounter < 0) {
			ASSERT(0);
			return (XFS_ERROR(EINVAL));
		}
		mp->m_sb.sb_agcount = scounter;
		return (0);
	default:
		ASSERT(0);
		return (XFS_ERROR(EINVAL));
	}
}

/*
 * xfs_mod_incore_sb() is used to change a field in the in-core
 * superblock structure by the specified delta.  This modification
 * is protected by the SB_LOCK.  Just use the xfs_mod_incore_sb_unlocked()
 * routine to do the work.
 */
int
xfs_mod_incore_sb(xfs_mount_t *mp, uint field, int delta)
{
	int	s;
	int	status;

	s = XFS_SB_LOCK(mp);
	status = xfs_mod_incore_sb_unlocked(mp, field, delta);
	XFS_SB_UNLOCK(mp, s);
	return (status);
}



/*
 * xfs_mod_incore_sb_batch() is used to change more than one field
 * in the in-core superblock structure at a time.  This modification
 * is protected by a lock internal to this module.  The fields and
 * changes to those fields are specified in the array of xfs_mod_sb
 * structures passed in.
 *
 * Either all of the specified deltas will be applied or none of
 * them will.  If any modified field dips below 0, then all modifications
 * will be backed out and EINVAL will be returned.
 */
int
xfs_mod_incore_sb_batch(xfs_mount_t *mp, xfs_mod_sb_t *msb, uint nmsb)
{
	int		s;
	int		n;
	int		status;
	xfs_mod_sb_t	*msbp;

	/*
	 * Loop through the array of mod structures and apply each
	 * individually.  If any fail, then back out all those
	 * which have already been applied.  Do all of this within
	 * the scope of the SB_LOCK so that all of the changes will
	 * be atomic.
	 */
	s = XFS_SB_LOCK(mp);
	msbp = &msb[0];
	for (msbp = &msbp[0]; msbp < (msb + nmsb); msbp++) {
		/*
		 * Apply the delta at index n.  If it fails, break
		 * from the loop so we'll fall into the undo loop
		 * below.
		 */
		status = xfs_mod_incore_sb_unlocked(mp, msbp->msb_field,
						    msbp->msb_delta);
		if (status != 0) {
			break;
		}
	}

	/*
	 * If we didn't complete the loop above, then back out
	 * any changes made to the superblock.  If you add code
	 * between the loop above and here, make sure that you
	 * preserve the value of status. Loop back until
	 * we step below the beginning of the array.  Make sure
	 * we don't touch anything back there.
	 */
	if (status != 0) {
		msbp--;
		while (msbp >= msb) {
			status = xfs_mod_incore_sb_unlocked(mp,
				    msbp->msb_field, -(msbp->msb_delta));
			ASSERT(status == 0);
			msbp--;
		}
	}
	XFS_SB_UNLOCK(mp, s);
	return (status);
}
				
		
/*
 * xfs_getsb() is called to obtain the buffer for the superblock.
 * The buffer is returned locked and read in from disk.
 * The buffer should be released with a call to xfs_brelse().
 *
 * If the flags parameter is BUF_TRYLOCK, then we'll only return
 * the superblock buffer if it can be locked without sleeping.
 * If it can't then we'll return NULL.
 */
buf_t *
xfs_getsb(xfs_mount_t	*mp,
	  int		flags)
{
	buf_t	*bp;

	ASSERT(mp->m_sb_bp != NULL);
	bp = mp->m_sb_bp;
	if (flags & BUF_TRYLOCK) {
		if (!cpsema(&bp->b_lock)) {
			return NULL;
		}
	} else {
		psema(&bp->b_lock, PRIBIO);
	}
	ASSERT(bp->b_flags & B_DONE);
	return (bp);
}


/*
 * This is the brelse function for the private superblock buffer.
 * All it needs to do is unlock the buffer and clear any spurious
 * flags.
 */
STATIC void
xfs_sb_relse(buf_t *bp)
{
	ASSERT(bp->b_flags & B_BUSY);
	ASSERT(valusema(&bp->b_lock) <= 0);
	bp->b_flags &= ~(B_ASYNC | B_READ);
	bp->b_flags2 = 0;
	bp->av_forw = NULL;
	bp->av_back = NULL;
	vsema(&bp->b_lock);
}



