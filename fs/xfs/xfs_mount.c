#ident	"$Revision: 1.10 $"

#include <sys/param.h>
#ifdef SIM
#define _KERNEL
#endif
#include <sys/buf.h>
#include <sys/sysmacros.h>
#ifdef SIM
#undef _KERNEL
#endif
#include <sys/vnode.h>
#include <sys/uuid.h>
#include <sys/debug.h>
#include <sys/errno.h>
#ifdef SIM
#include <bstring.h>
#else
#include <sys/systm.h>
#include <sys/kmem.h>
#include <sys/conf.h>
#endif
#include <stddef.h>
#include "xfs_types.h"
#include "xfs_inum.h"
#include "xfs.h"
#include "xfs_trans.h"
#include "xfs_sb.h"
#include "xfs_ag.h"
#include "xfs_mount.h"
#include "xfs_alloc.h"
#include "xfs_ialloc.h"
#include "xfs_bmap.h"
#include "xfs_btree.h"
#include "xfs_dinode.h"
#include "xfs_inode_item.h"
#include "xfs_inode.h"

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
	initnlock(&mp->m_ilock, "xfs_ilock");
	initnlock(&mp->m_ipinlock, "xfs_ipin");
	initnlock(&mp->m_sb_lock, "xfs_sb");

	return (mp);
}
	
void
xfs_mount(xfs_mount_t *mp, dev_t dev)
{
	buf_t		*bp;
	xfs_sb_t	*sbp;
	int		error;

	/*
	 * Allocate a buffer to hold the superblock.
	 * This will be kept around at all time to optimize
	 * access to the superblock.
	 */
	bp = ngetrbuf(BBTOB(BTOBB(sizeof(xfs_sb_t))));
	ASSERT(buf != NULL);
	ASSERT(valusema(&bp->b_lock) == 1);
	psema(&bp->b_lock, PRIBIO);

	/*
	 * Initialize and read in the superblock buffer.
	 */
	bp->b_edev = dev;
	bp->b_relse = xfs_sb_relse;
	bp->b_blkno = XFS_SB_DADDR;		
	bp->b_flags |= B_READ;
	bdstrat(bmajor(dev), bp);
	error = iowait(bp);
	ASSERT(error == 0);

	/*
	 * Initialize the mount structure from the superblock.
	 */
	sbp = xfs_buf_to_sbp(bp);
	mp->m_dev = dev;
	mp->m_sb_bp = bp;
	mp->m_sb = *sbp;
	mp->m_bsize = xfs_btod(sbp, 1);
	mp->m_agrotor = 0;

	vsema(&bp->b_lock);
	ASSERT(valusema(&bp->b_lock) > 0);

	/*
	 * Allocate and initialize the inode hash table for this
	 * file system.
	 */
	xfs_ihash_init(mp);
}

void
xfs_umount(xfs_mount_t *mp)
/* ARGSUSED */
{
	int	error;
	buf_t	*bp;

	bp = xfs_getsb(mp);
	bp->b_flags &= ~(B_DONE | B_READ);
	bp->b_flags |= B_WRITE;
	bwait_unpin(bp);
	bdstrat(bmajor(mp->m_dev), bp);
	error = iowait(bp);
	ASSERT(error == 0);	
	freerbuf(bp);
}

/*
 * xfs_mod_sb() can be used to copy arbitray changes to the
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
		offsetof(xfs_sb_t, sb_rbitmap),
		offsetof(xfs_sb_t, sb_rsummary),
		offsetof(xfs_sb_t, sb_rootino),
		offsetof(xfs_sb_t, sb_rextsize),
		offsetof(xfs_sb_t, sb_agblocks),
		offsetof(xfs_sb_t, sb_agcount),
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
		offsetof(xfs_sb_t, sb_smallfiles),
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
		scounter = mp->m_sb.sb_frextents;
		scounter += delta;
		if (scounter < 0) {
			return (EINVAL);
		}
		mp->m_sb.sb_frextents = scounter;
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
	ASSERT(valusema(&bp->b_lock) == 0);
	bp->b_flags &= ~(B_ASYNC);
	vsema(&bp->b_lock);
}



