#ident	"$Revision: 1.37 $"

#include <sys/param.h>
#ifdef SIM
#define _KERNEL
#endif
#include <sys/buf.h>
#include <sys/sysmacros.h>
#include <sys/vfs.h>
#include <sys/vnode.h>
#include <sys/grio.h>
#ifdef SIM
#undef _KERNEL
#endif
#include <sys/uuid.h>
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
#endif
#include <stddef.h>
#include "xfs_types.h"
#include "xfs_inum.h"
#include "xfs_log.h"
#include "xfs_trans.h"
#include "xfs_sb.h"
#include "xfs_ag.h"
#include "xfs_mount.h"
#include "xfs_alloc_btree.h"
#include "xfs_ialloc.h"
#include "xfs_bmap_btree.h"
#include "xfs_btree.h"
#include "xfs_dinode.h"
#include "xfs_inode_item.h"
#include "xfs_inode.h"
#include "xfs_dir.h"

#ifdef SIM
#include "sim.h"
#endif


STATIC int	_xfs_mod_incore_sb(xfs_mount_t *, uint, int);
STATIC void	xfs_sb_relse(buf_t *);

/*
 * Return a pointer to an initialized xfs_mount structure.
 */
xfs_mount_t *
xfs_mount_init(void)
{
	xfs_mount_t *mp;

	mp = kmem_zalloc(sizeof(*mp), 0);

	initnlock(&mp->m_ail_lock, "xfs_ail");
	initnlock(&mp->m_async_lock, "xfs_async");
	initnsema(&mp->m_ilock, 1, "xfs_ilock");
	initnlock(&mp->m_ipinlock, "xfs_ipin");
	initnlock(&mp->m_sb_lock, "xfs_sb");
	/*
	 * Initialize the AIL.
	 */
	xfs_trans_ail_init(mp);

	return mp;
}
	
/*
 * xfs_mountfs		XXXjleong Needs more error checking
 *
 * This function does the following on an initial mount of a file system:
 *	- reads the superblock from disk and init the mount struct
 *	- init mount struct realtime fields
 *	- allocate inode hash table for fs
 *	- init directory manager
 * Note:
 *	- does not handle remounts
 *	- does not start the log manager
 */
int
xfs_mountfs(vfs_t *vfsp, dev_t dev)
{
	buf_t		*bp;
	xfs_sb_t	*sbp;
	int		error;
	int		s;
	xfs_mount_t	*mp;
	xfs_inode_t	*rip;
	vnode_t		*rvp = 0;

	if (vfsp->vfs_flag & VFS_REMOUNT)
		return 0;

	mp = XFS_VFSTOM(vfsp);

	ASSERT(mp->m_sb_bp == 0);

	/*
	 * Allocate a (locked) buffer to hold the superblock.
	 * This will be kept around at all time to optimize
	 * access to the superblock.
	 */
	bp = ngetrbuf(BBTOB(BTOBB(sizeof(xfs_sb_t))));
	ASSERT(buf != NULL);
	ASSERT((bp->b_flags & B_BUSY) && valusema(&bp->b_lock) <= 0);

	/*
	 * Initialize and read in the superblock buffer.
	 */
	bp->b_edev = dev;
	bp->b_relse = xfs_sb_relse;
	bp->b_blkno = XFS_SB_DADDR;		
	bp->b_flags |= B_READ;
	bdstrat(bmajor(dev), bp);
	error = iowait(bp);
	if (error) {
		ASSERT(error == 0);
		goto bad;
	}

	/*
	 * Initialize the mount structure from the superblock.
	 */
	sbp = xfs_buf_to_sbp(bp);
	if ((sbp->sb_magicnum != XFS_SB_MAGIC) ||
	    (sbp->sb_versionnum != XFS_SB_VERSION)) {
		error = EINVAL;		/* or EIO ? */
		goto bad;
	}
	mp->m_sb_bp = bp;
	mp->m_sb = *sbp;
	mp->m_bsize = xfs_btod(sbp, 1);
	mp->m_agrotor = 0;
	vfsp->vfs_bsize = xfs_fsb_to_b(sbp, 1);

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
	 * Initialize realtime fields in the mount structure
	 */
	if (sbp->sb_rblocks) {
		mp->m_rsumlevels = sbp->sb_rextslog + 1;
		mp->m_rsumsize = sizeof(xfs_suminfo_t) * mp->m_rsumlevels * sbp->sb_rbmblocks;
		mp->m_rsumsize = roundup(mp->m_rsumsize, sbp->sb_blocksize);
		mp->m_rbmip = mp->m_rsumip = NULL;
	}

	/*
	 * Allocate and initialize the inode hash table for this
	 * file system.
	 */
	xfs_ihash_init(mp);

#ifdef SIM
	/*
	 * Mkfs calls mount before the root inode is allocated.
	 */
	if (sbp->sb_rootino != NULLFSINO)
#endif
	{
		/*
		 * Get and sanity-check the root inode.
		 * Save the pointer to it in the mount structure.
		 */
		rip = xfs_iget(mp, NULL, sbp->sb_rootino, XFS_ILOCK_EXCL);
		rvp = XFS_ITOV(rip);
		ASSERT(IFDIR == (rip->i_d.di_mode & IFMT));
		if ((rip->i_d.di_mode & IFMT) != IFDIR) {
			vmap_t vmap;

			VMAP(rvp, vmap);
			VN_RELE(rvp);
			vn_purge(rvp, &vmap);

			prdev("Root inode %d is not a directory",
			      rip->i_dev, rip->i_ino);

			error = EINVAL;
			goto bad;
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
	 * Initialize directory manager's entries.
	 */
	xfs_dir_mount(mp);

	vsema(&bp->b_lock);
	ASSERT(valusema(&bp->b_lock) > 0);

	return 0;
bad:
	brelse(bp);
	freerbuf(bp);
	return error;
}

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
	vfsp->vfs_dev = dev;


        error = xfs_mountfs(vfsp, dev);
	ASSERT(error == 0);
	if (error) {
		kmem_free(mp, sizeof(*mp));
		return 0;
	}

	/*
	 * Call the log's mount-time initialization.
	 */
	if (logdev) {
		xfs_sb_t *sbp;
		sbp = xfs_buf_to_sbp(mp->m_sb_bp);
		xfs_log_mount(mp, logdev, xfs_btod(sbp, sbp->sb_logstart),
			      xfs_btod(sbp, sbp->sb_logblocks), 0);
	}

	return mp;
}
#endif

/*
 * xfs_unmountfs
 */
int
xfs_unmountfs(xfs_mount_t *mp, int vfs_flags, struct cred *cr )
{
	buf_t	*bp;
	dev_t	dev;
	int	error;

	xfs_iflush_all(mp);
	/* XXX someone needs to free the inodes' memory */

	bflush(mp->m_dev);
	if (mp->m_rtdev)
		bflush(mp->m_rtdev);
	bp = xfs_getsb(mp);
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

	brelse(bp);
	freerbuf(bp);

	freesplock(mp->m_ail_lock);
	freesplock(mp->m_async_lock);
	freesema(&mp->m_ilock);
	freesplock(mp->m_ipinlock);
	freesplock(mp->m_sb_lock);
	kmem_free(mp, sizeof(*mp));
}


#ifdef SIM
/*
 * xfs_umount is the function used by the simulation environment
 * to stop the file system.
 */
void
xfs_umount(xfs_mount_t *mp)
{
	xfs_inode_t	*ip;
	vnode_t		*vp;
	int		s;
	int		error = 0;

	/* need to give up if vnodes are referenced */
	s = XFS_MOUNT_ILOCK(mp);
	for (ip = mp->m_inodes; ip && !error; ip = ip->i_mnext) {
		vp = XFS_ITOV(ip);
		if (vp->v_count != 0) {
			if ((vp->v_count == 1) && (ip == mp->m_rootip))
				continue;
			error++;
		}
	}
	XFS_MOUNT_IUNLOCK(mp, s);

	if (error == 0)
		error = xfs_unmountfs(mp, 0, NULL);
}
#endif


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
	buf_t		*buf;
	int		first;
	int		last;
	xfs_mount_t	*mp;
	xfs_sb_t	*sbp;
	static const int offsets[] = {
		offsetof(xfs_sb_t, sb_uuid),
		offsetof(xfs_sb_t, sb_dblocks),
		offsetof(xfs_sb_t, sb_blocksize),
		offsetof(xfs_sb_t, sb_magicnum),
		offsetof(xfs_sb_t, sb_rblocks),
		offsetof(xfs_sb_t, sb_rextents),
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
		offsetof(xfs_sb_t, sb_icount),
		offsetof(xfs_sb_t, sb_ifree),
		offsetof(xfs_sb_t, sb_fdblocks),
		offsetof(xfs_sb_t, sb_frextents),
		sizeof(xfs_sb_t)
	};
 
	mp = tp->t_mountp;
	buf = xfs_trans_getsb(tp);
	sbp = xfs_buf_to_sbp(buf);
	xfs_btree_offsets(fields, offsets, XFS_SB_NUM_BITS, &first, &last);
	bcopy((caddr_t)&mp->m_sb + first, (caddr_t)sbp + first, last - first + 1);
	xfs_trans_log_buf(tp, buf, first, last);
}


/*
 * _xfs_mod_incore_sb() is a utility routine common used to apply
 * a delta to a specified field in the in-core superblock.  Simply
 * switch on the field indicated and apply the delta to that field.
 * Fields are not allowed to dip below zero, so if the delta would
 * do this do not apply it and return EINVAL.
 *
 * The SB_LOCK must be held when this routine is called.
 */
STATIC int
_xfs_mod_incore_sb(xfs_mount_t *mp, uint field, int delta)
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
			return (EINVAL);
		}
		mp->m_sb.sb_icount = lcounter;
		return (0);
	case XFS_SB_IFREE:
		lcounter = mp->m_sb.sb_ifree;
		lcounter += delta;
		if (lcounter < 0) {
			return (EINVAL);
		}
		mp->m_sb.sb_ifree = lcounter;
		return (0);
	case XFS_SB_FDBLOCKS:
		lcounter = mp->m_sb.sb_fdblocks;
		lcounter += delta;
		if (lcounter < 0) {
			return (EINVAL);
		}
		mp->m_sb.sb_fdblocks = lcounter;
		return (0);
	case XFS_SB_FREXTENTS:
		lcounter = mp->m_sb.sb_frextents;
		lcounter += delta;
		if (lcounter < 0) {
			return (EINVAL);
		}
		mp->m_sb.sb_frextents = lcounter;
		return (0);
	default:
		ASSERT(0);
		return (EINVAL);
	}
}

/*
 * xfs_mod_incore_sb() is used to change a field in the in-core
 * superblock structure by the specified delta.  This modification
 * is protected by the SB_LOCK.  Just use the _xfs_mod_incore_sb()
 * routine to do the work.
 */
int
xfs_mod_incore_sb(xfs_mount_t *mp, uint field, int delta)
{
	int	s;
	int	status;

	s = XFS_SB_LOCK(mp);
	status = _xfs_mod_incore_sb(mp, field, delta);
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
		status = _xfs_mod_incore_sb(mp, msbp->msb_field,
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
			status = _xfs_mod_incore_sb(mp, msbp->msb_field,
						    -(msbp->msb_delta));
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
 */
buf_t *
xfs_getsb(xfs_mount_t *mp)
{
	buf_t	*bp;

	ASSERT(mp->m_sb_bp != NULL);
	bp = mp->m_sb_bp;
	psema(&bp->b_lock, PRIBIO);
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
	bp->b_flags &= ~(B_ASYNC);
	vsema(&bp->b_lock);
}



