

#include <sys/param.h>
#ifdef SIM
#define _KERNEL
#endif
#include <sys/buf.h>
#include <sys/sysmacros.h>
#include <sys/vnode.h>
#include <sys/uuid.h>
#include <sys/grio.h>
#ifdef SIM
#undef _KERNEL
#endif
#include <sys/debug.h>
#include <sys/uuid.h>
#ifndef SIM
#include <sys/sysinfo.h>
#include <sys/kmem.h>
#include <sys/conf.h>
#include <sys/systm.h>
#endif
#include "xfs_types.h"
#include "xfs_inum.h"
#include "xfs_log.h"
#include "xfs_trans.h"
#include "xfs_sb.h"
#include "xfs_ag.h"
#include "xfs_mount.h"
#include "xfs_trans_priv.h"
#include "xfs_alloc_btree.h"
#include "xfs_bmap_btree.h"
#include "xfs_ialloc_btree.h"
#include "xfs_btree.h"
#include "xfs_ialloc.h"
#include "xfs_dinode.h"
#include "xfs_inode_item.h"
#include "xfs_inode.h"

#ifdef SIM
#include "sim.h"
#endif

/*
 * Get and lock the inode for the caller if it is not already
 * locked within the given transaction.  If it is already locked
 * within the transaction, just increment its lock recursion count
 * and return a pointer to it.
 *
 * For an inode to be locked in a transaction, the inode lock, as
 * opposed to the io lock, must be taken exclusively.  This ensures
 * that the inode can be involved in only 1 transaction at a time.
 * Lock recursion is handled on the io lock, but only for lock modes
 * of equal or lesser strength.  That is, you can recur on the io lock
 * held EXCL with a SHARED request but not vice versa.  Also, if
 * the inode is already a part of the transaction then you cannot
 * go from not holding the io lock to having it EXCL or SHARED.
 *
 * Use the inode cache routine xfs_inode_incore() to find the inode
 * if it is already owned by this transaction.
 *
 * If we don't already own the inode, use xfs_iget() to get it.
 * Since the inode log item structure is embedded in the incore
 * inode structure and is initialized when the inode is brought
 * into memory, there is nothing to do with it here.
 *
 * If the given transaction pointer is NULL, just call xfs_iget().
 * This simplifies code which must handle both cases.
 */
xfs_inode_t *
xfs_trans_iget(xfs_mount_t	*mp,
	       xfs_trans_t	*tp,
	       xfs_ino_t	ino,
	       uint		lock_flags)
{
	xfs_inode_log_item_t	*iip;
	xfs_inode_t		*ip;

	/*
	 * If the transaction pointer is NULL, just call the normal
	 * xfs_iget().
	 */
	if (tp == NULL) {
		return (xfs_iget(mp, NULL, ino, lock_flags));
	}

	/*
	 * If we find the inode in core with this transaction
	 * pointer in its i_transp field, then we know we already
	 * have it locked.  In this case we just increment the lock
	 * recursion count and return the inode to the caller.
	 * Assert that the inode is already locked in the mode requested
	 * by the caller.  We cannot do lock promotions yet, so
	 * die if someone gets this wrong.
	 */
	if ((ip = xfs_inode_incore(tp->t_mountp, ino, tp)) != NULL) {
		/*
		 * Make sure that the inode lock is held EXCL and
		 * that the io lock is never upgraded when the inode
		 * is already a part of the transaction.
		 */
		ASSERT(lock_flags & XFS_ILOCK_EXCL);
		ASSERT(ismrlocked(&ip->i_lock, MR_UPDATE));
		ASSERT((!(lock_flags & XFS_IOLOCK_EXCL)) ||
		       ismrlocked(&ip->i_iolock, MR_UPDATE));
		ASSERT((!(lock_flags & XFS_IOLOCK_EXCL)) ||
		       (ip->i_item.ili_flags & XFS_ILI_IOLOCKED_EXCL));
		ASSERT((!(lock_flags & XFS_IOLOCK_SHARED)) ||
		       ismrlocked(&ip->i_iolock, (MR_UPDATE | MR_ACCESS)));
		ASSERT((!(lock_flags & XFS_IOLOCK_SHARED)) ||
		       (ip->i_item.ili_flags & XFS_ILI_IOLOCKED_ANY));

		if (lock_flags & (XFS_IOLOCK_SHARED | XFS_IOLOCK_EXCL)) {
			ip->i_item.ili_iolock_recur++;
		}
		if (lock_flags & XFS_ILOCK_EXCL) {
			ip->i_item.ili_ilock_recur++;
		}
		return (ip);
	}

	ASSERT(lock_flags & XFS_ILOCK_EXCL);
	ip = xfs_iget(tp->t_mountp, tp, ino, lock_flags);

	/*
	 * Get a log_item_desc to point at the new item.
	 */
	(void) xfs_trans_add_item(tp, (xfs_log_item_t*)&(ip->i_item));

	/*
	 * If the IO lock has been acquired, mark that in
	 * the inode log item so we'll know to unlock it
	 * when the transaction commits.
	 */
	ASSERT(ip->i_item.ili_flags == 0);
	if (lock_flags & XFS_IOLOCK_EXCL) {
		ip->i_item.ili_flags |= XFS_ILI_IOLOCKED_EXCL;
	} else if (lock_flags & XFS_IOLOCK_SHARED) {
		ip->i_item.ili_flags |= XFS_ILI_IOLOCKED_SHARED;
	}

	/*
	 * Initialize i_transp so we can find it with xfs_inode_incore()
	 * above.
	 */
	ip->i_transp = tp;

	return (ip);
}


/*
 * Release the inode ip which was previously acquired with xfs_trans_iget()
 * or added with xfs_trans_ijoin(). This will decrement the lock
 * recursion count of the inode item.  If the count goes to less than 0,
 * the inode will be unlocked and disassociated from the transaction. 
 *
 * If the inode has been modified within the transaction, it will not be
 * unlocked until the transaction commits.
 */
void
xfs_trans_iput(xfs_trans_t	*tp,
	       xfs_inode_t	*ip,
	       uint		lock_flags)
{
	xfs_inode_log_item_t	*iip;
	xfs_log_item_desc_t	*lidp;

	/*
	 * If the transaction pointer is NULL, just call xfs_iput().
	 */
	if (tp == NULL) {
		xfs_iput(ip, lock_flags);
	}

	ASSERT(ip->i_transp == tp);
	iip = &ip->i_item;	

	/*
	 * Find the item descriptor pointing to this inode's
	 * log item.  It must be there.
	 */
	lidp = xfs_trans_find_item(tp, (xfs_log_item_t*)iip);
	ASSERT(lidp != NULL);
	ASSERT(lidp->lid_item == (xfs_log_item_t*)iip);

	/*
	 * Be consistent about the bookkeeping for the inode's
	 * io lock, but it doesn't mean much really.
	 */
	ASSERT((iip->ili_flags & XFS_ILI_IOLOCKED_ANY) != XFS_ILI_IOLOCKED_ANY);
	if (lock_flags & (XFS_IOLOCK_EXCL | XFS_IOLOCK_SHARED)) {
		ASSERT(iip->ili_flags & XFS_ILI_IOLOCKED_ANY);
		ASSERT((!(lock_flags & XFS_IOLOCK_EXCL)) ||
		       (iip->ili_flags & XFS_ILI_IOLOCKED_EXCL));
		ASSERT((!(lock_flags & XFS_IOLOCK_SHARED)) ||
		       (iip->ili_flags &
			(XFS_ILI_IOLOCKED_EXCL | XFS_ILI_IOLOCKED_SHARED)));
		if (iip->ili_iolock_recur > 0) {
			iip->ili_iolock_recur--;
		}
	}
 
	/*
	 * If the release is just for a recursive lock on the inode lock,
	 * then decrement the count and return.  We can assert that
	 * the caller is dropping an EXCL lock on the inode, because
	 * inode must be locked EXCL within transactions.
	 */
	ASSERT(lock_flags & XFS_ILOCK_EXCL);
	if (iip->ili_ilock_recur > 0) {
		iip->ili_ilock_recur--;
		return;
	}
	ASSERT(iip->ili_iolock_recur == 0);

	/*
	 * If the inode was dirtied within this transaction, it cannot
	 * be released until the transaction commits.
	 */
	if (lidp->lid_flags & XFS_LID_DIRTY) {
		return;
	}

	xfs_trans_free_item(tp, lidp);

	/*
	 * Clear the hold and iolocked flags in the inode log item.
	 * We wouldn't want the next user of the inode to
	 * get confused.  Assert that if the iolocked flag is set
	 * in the item then we are unlocking it in the call to xfs_iput()
	 * below.
	 */
	ASSERT((!(iip->ili_flags & XFS_ILI_IOLOCKED_ANY)) ||
	       (lock_flags & (XFS_IOLOCK_EXCL | XFS_IOLOCK_SHARED)));
	if (iip->ili_flags & (XFS_ILI_HOLD | XFS_ILI_IOLOCKED_ANY)) {
		iip->ili_flags &= ~(XFS_ILI_HOLD | XFS_ILI_IOLOCKED_ANY);
	}

	/*
	 * Unlike xfs_brelse() the inode log item cannot be
	 * freed, because it is embedded within the inode.
	 * All we have to do is release the inode.
	 */
	xfs_iput(ip, lock_flags);
	return;
}


/*
 * Add the locked inode to the transaction.
 * The inode must be locked, and it cannot be associated with any
 * transaction.  The caller must specify the locks already held
 * on the inode.
 */
void
xfs_trans_ijoin(xfs_trans_t	*tp,
		xfs_inode_t	*ip,
		uint		lock_flags)
{
	xfs_inode_log_item_t	*iip;

	ASSERT(ip->i_transp == NULL);
	ASSERT(ismrlocked(&ip->i_lock, MR_UPDATE));
	ASSERT(lock_flags & XFS_ILOCK_EXCL);
	ASSERT(ip->i_item.ili_flags == 0);
	ASSERT(ip->i_item.ili_ilock_recur == 0);
	ASSERT(ip->i_item.ili_iolock_recur == 0);

	/*
	 * Get a log_item_desc to point at the new item.
	 */
	(void) xfs_trans_add_item(tp, (xfs_log_item_t*)&(ip->i_item));

	/*
	 * If the IO lock is already held, mark that in the inode log item.
	 */
	if (lock_flags & XFS_IOLOCK_EXCL) {
		ip->i_item.ili_flags |= XFS_ILI_IOLOCKED_EXCL;
	} else if (lock_flags & XFS_IOLOCK_SHARED) {
		ip->i_item.ili_flags |= XFS_ILI_IOLOCKED_SHARED;
	}

	/*
	 * Initialize i_transp so we can find it with xfs_inode_incore()
	 * in xfs_trans_iget() above.
	 */
	ip->i_transp = tp;
}



/*
 * Mark the inode as not needing to be unlocked when the inode item's
 * IOP_UNLOCK() routine is called.  The inode must already be locked
 * and associated with the given transaction.
 */
void
xfs_trans_ihold(xfs_trans_t	*tp,
		xfs_inode_t	*ip)
{
	ASSERT(ip->i_transp == tp);
	ASSERT(ismrlocked(&ip->i_lock, MR_UPDATE));

	ip->i_item.ili_flags |= XFS_ILI_HOLD;
}


/*
 * This is called to mark the fields indicated in fieldmask as needing
 * to be logged when the transaction is committed.  The inode must
 * already be associated with the given transaction.
 *
 * The values for fieldmask are defined in xfs_inode_item.h.  We always
 * log all of the core inode if any of it has changed, and we always log
 * all of the inline data/extents/b-tree root if any of them has changed.
 */
void
xfs_trans_log_inode(xfs_trans_t	*tp,
		    xfs_inode_t	*ip,
		    uint	flags)
{
	xfs_inode_log_item_t	*iip;
	xfs_log_item_desc_t	*lidp;

	ASSERT(ip->i_transp == tp);
	ASSERT(ismrlocked(&ip->i_lock, MR_UPDATE));

	lidp = xfs_trans_find_item(tp, (xfs_log_item_t*)&(ip->i_item));
	ASSERT(lidp != NULL);

	tp->t_flags |= XFS_TRANS_DIRTY;
	lidp->lid_flags |= XFS_LID_DIRTY;

	ip->i_item.ili_format.ilf_fields |= flags;
}

