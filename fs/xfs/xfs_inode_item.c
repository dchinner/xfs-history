
/*
 * This file contains the implementation of the xfs_inode_log_item.
 * It contains the item operations used to manipulate the inode log
 * items as well as utility routines used by the inode specific
 * transaction routines.
 */


#include <sys/param.h>
#define _KERNEL
#include <sys/buf.h>
#undef _KERNEL
#include <sys/vnode.h>
#include <sys/debug.h>
#include <sys/uuid.h>
#include "xfs_types.h"
#include "xfs_inum.h"
#include "xfs.h"
#include "xfs_trans.h"
#include "xfs_buf_item.h"
#include "xfs_bio.h"
#include "xfs_sb.h"
#include "xfs_mount.h"
#include "xfs_trans_priv.h"
#include "xfs_ag.h"
#include "xfs_alloc.h"
#include "xfs_ialloc.h"
#include "xfs_bmap.h"
#include "xfs_btree.h"
#include "xfs_dinode.h"
#include "xfs_inode_item.h"
#include "xfs_inode.h"
#ifdef SIM
#include <bstring.h>
#include "sim.h"
#else
#include <sys/systm.h>
#endif


/*
 * This returns the amount of space needed to log the given inode
 * log item.
 */
uint
xfs_inode_item_size(xfs_inode_log_item_t *iip)
{
	uint	base_size;
	uint	core_size;
	uint	other_size;

	/*
	 * Always log an inode log format structure.
	 */
	base_size = sizeof(xfs_inode_log_format_t);

	/*
	 * If we're logging the xfs_dinode_core structure
	 * count it here.
	 */
	core_size = 0;
	if (iip->ili_fields & XFS_ILOG_CORE) {
		core_size = sizeof(xfs_dinode_core_t);
	}

	/*
	 * If we need to log inline data/extents/b-tree root,
	 * figure out the size here.  We always log all
	 * of it.
	 */
	other_size = 0;
	if (iip->ili_fields & (XFS_ILOG_DATA | XFS_ILOG_EXT)) {
		other_size = iip->ili_inode->i_bytes;
	} else if (iip->ili_fields & XFS_ILOG_BROOT) {
		other_size = iip->ili_inode->i_broot_bytes;
	}
		
	return (base_size + core_size + other_size);
}

/*
 * This will remain unimplemented until the logging interfaces
 * are reworked.
 */
uint
xfs_inode_item_format(xfs_inode_log_item_t *bip, caddr_t buffer,
		    uint buffer_size, int *keyp)
{
	*keyp = 0;
	return (0);
}
	

/*
 * This is called to pin the inode associated with the inode log
 * item in memory so it cannot be written out.  Do this by calling
 * xfs_ipin() to bump the pin count in the inode while holding the
 * inode pin lock.
 */
void
xfs_inode_item_pin(xfs_inode_log_item_t *iip)
{
	ASSERT(ismrlocked(&(iip->ili_inode->i_lock), MR_UPDATE));
	xfs_ipin(iip->ili_inode);
}


/*
 * This is called to unpin the inode associated with the inode log
 * item which was previously pinned with a call to xfs_inode_item_pin().
 * Just call xfs_iunpin() on the inode to do this.
 */
void
xfs_inode_item_unpin(xfs_inode_log_item_t *iip)
{
	xfs_iunpin(iip->ili_inode);
}

/*
 * This is called to attempt to lock the inode associated with this
 * inode log item.  Don't sleep on the inode lock.  If we can't get
 * the lock right away, return 0.  We know that we have a reference
 * for the vnode associated with our inode since we took one when
 * this inode was first logged.
 *
 * We only have 1 reference, though, so we need to make sure that only
 * one process tries to flush the inode.  This is because the reference
 * will be released when the flush completes and the inode can then be
 * freed.  We use the i_flock for this synchronization.
 */
uint
xfs_inode_item_trylock(xfs_inode_log_item_t *iip)
{
	register xfs_inode_t	*ip;

	ip = iip->ili_inode;

	if (!xfs_ilock_nowait(ip, XFS_ILOCK_SHARED)) {
		return (0);
	}

	if (!xfs_iflock_nowait(ip)) {
		xfs_iunlock(ip);
		return (0);
	}

	return (1);
}

/*
 * Unlock the inode associated with the inode log item.
 * Clear the fields of the inode and inode log item that
 * are specific to the current transaction.  If the
 * hold flags is set, do not unlock the inode.  
 */
void
xfs_inode_item_unlock(xfs_inode_log_item_t *iip)
{
	uint	hold;

	/*
	 * Clear the transaction pointer in the inode.
	 */
	iip->ili_inode->i_transp = NULL;

	/*
	 * Figure out if we should unlock the inode or not.
	 */
	hold = iip->ili_flags & XFS_ILI_HOLD;

	/*
	 * Clear out the fields of the inode log item particular
	 * to the current transaction.
	 */
	iip->ili_recur = 0;
	iip->ili_flags = 0;

	/*
	 * Unlock the inode if XFS_ILI_HOLD was not set.
	 */
	if (!hold) {
		xfs_iunlock(iip->ili_inode);
	}
}

/*
 * This is called to find out where the oldest active copy of the
 * inode log item in the on disk log resides now that the last log
 * write of it completed at the given lsn.  Since we always re-log
 * all dirty data in an inode, the latest copy in the on disk log
 * is the only one that matters.  Therefore, simply return the
 * given lsn.
 */
xfs_lsn_t
xfs_inode_item_committed(xfs_inode_log_item_t *iip, xfs_lsn_t lsn)
{
	return (lsn);
}


/*
 * This is called to asynchronously write the inode associated with this
 * inode log item out to disk. The inode will already have been locked by
 * a successful call to xfs_inode_item_trylock().  If the inode still has
 * some of its fields marked as logged, then write it out.  If not, then
 * just unlock the inode.
 */
void
xfs_inode_item_push(xfs_inode_log_item_t *iip)
{
	buf_t		*bp;
	xfs_dinode_t	*dip;
	xfs_inode_t	*ip;

	ASSERT(ismrlocked(&(iip->ili_inode->i_lock), XFS_ILOCK_SHARED));
	ASSERT(valusema(&(iip->ili_inode->i_flock)) <= 0);

	/*
	 * If there is nothing to flush, don't bother.
	 * Someone else must have flushed the inode (not through
	 * xfs_trans_push_ail()).  Just make sure it's off the
	 * AIL and then release our reference to the vnode.
	 */
	ip = iip->ili_inode;
	if (iip->ili_fields == 0) {
		xfs_trans_delete_ail(ip->i_mount, (xfs_log_item_t *)iip);
		xfs_ifunlock(ip);
		xfs_iput(ip);
		return;
	}

	/*
	 * Write out the inode.  The completion routine will
	 * pull it from the AIL, mark it clean, and xfs_iput()
	 * the inode.
	 */
	xfs_iflush(ip, B_ASYNC);
}

/*
 * This is the ops vector shared by all buf log items.
 */
struct xfs_item_ops xfs_inode_item_ops = {
	(uint(*)(xfs_log_item_t*))xfs_inode_item_size,
	(uint(*)(xfs_log_item_t*, caddr_t, uint, int*))xfs_inode_item_format,
	(void(*)(xfs_log_item_t*))xfs_inode_item_pin,
	(void(*)(xfs_log_item_t*))xfs_inode_item_unpin,
	(uint(*)(xfs_log_item_t*))xfs_inode_item_trylock,
	(void(*)(xfs_log_item_t*))xfs_inode_item_unlock,
	(xfs_lsn_t(*)(xfs_log_item_t*, xfs_lsn_t))xfs_inode_item_committed,
	(void(*)(xfs_log_item_t*))xfs_inode_item_push
};


/*
 * Initialize the inode log item for a newly allocated (in-core) inode.
 */
void
xfs_inode_item_init(xfs_inode_t *ip, xfs_mount_t *mp)
{
	xfs_inode_log_item_t	*iip;

	iip = &ip->i_item;

	iip->ili_item.li_type = XFS_LI_INODE;
	iip->ili_item.li_ops = &xfs_inode_item_ops;
	iip->ili_item.li_mountp = mp;
	iip->ili_inode = ip;
}


/*
 * This is the inode flushing I/O completion routine.  It is called
 * from interrupt level when the buffer containing the inode is
 * flushed to disk.  It is responsible for removing the inode item
 * from the AIL if it has not been re-logged, unlocking the inode's
 * flush lock, and releasing the vnode reference taken by the
 * inode transaction code if there is one.
 */
void
xfs_iflush_done(buf_t *bp, xfs_inode_log_item_t *iip)
{
	xfs_lsn_t	lsn;
	xfs_inode_t	*ip;
	int		s;
	int		drop_ref;

	ip = iip->ili_inode;

	/*
	 * We only want to pull the item from the AIL if its
	 * location in the log has not changed since we started
	 * the flush.  Thus, we only bother if the inode's lsn
	 * has not changed.  First we check outside the lock since
	 * it's cheaper, and then we recheck while holding the
	 * lock before removing the inode from the AIL.
	 */
	if (iip->ili_item.li_lsn == iip->ili_flush_lsn) {
		s = AIL_LOCK(ip->i_mount);
		if (iip->ili_item.li_lsn == iip->ili_flush_lsn) {
			xfs_trans_delete_ail(ip->i_mount, (xfs_log_item_t*)iip);
		}
		AIL_UNLOCK(ip->i_mount, s);
	}

	/*
	 * Check to see if we should drop the reference taken by
	 * xfs_trans_log_inode() by looking at ili_ref.  ili_ref
	 * is guarded by the i_flock, so check and clear it before
	 * unlocking the i_flock.
	 */
	if (iip->ili_ref != 0) {
		drop_ref = 1;
		iip->ili_ref = 0;
	} else {
		drop_ref = 0;
	}
	
	/*
	 * Release the inode's flush lock since we're done with it.
	 */
	xfs_ifunlock(ip);

	/*
	 * Here we release the inode reference made by the transaction
	 * code if we have one.
	 */
	if (drop_ref) {
#ifdef NOTYET
		vn_rele(XFS_ITOV(ip));
#endif
	}
		
	return;
}




