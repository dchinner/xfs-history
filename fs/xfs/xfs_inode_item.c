
/*
 * This file contains the implementation of the xfs_inode_log_item.
 * It contains the item operations used to manipulate the inode log
 * items as well as utility routines used by the inode specific
 * transaction routines.
 */


#include <sys/param.h>
#ifdef SIM
#define _KERNEL
#endif
#include <sys/buf.h>
#include <sys/vnode.h>
#ifdef SIM
#undef _KERNEL
#endif
#include <sys/debug.h>
#include <sys/uuid.h>
#ifndef SIM
#include <sys/systm.h>
#else
#include <bstring.h>
#endif
#include "xfs_types.h"
#include "xfs_inum.h"
#include "xfs_log.h"
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
#include "sim.h"
#endif


/*
 * This returns the number of iovecs needed to log the given inode item.
 *
 * We need one iovec for the inode log format structure, one for the
 * inode core, and possibly one for the inode data/extents/b-tree root.
 */
uint
xfs_inode_item_size(xfs_inode_log_item_t *iip)
{
	uint	nvecs;

	nvecs = 2;
	if (iip->ili_format.ilf_fields &
	    (XFS_ILOG_DATA | XFS_ILOG_EXT | XFS_ILOG_BROOT)) {
		nvecs++;
	}

	return nvecs; 
}

/*
 * This is called to fill in the vector of log iovecs for the
 * given inode log item.  It fills the first item with an inode
 * log format structure, the second with the on-disk inode structure,
 * and possible a third with the inode data/extents/b-tree root.
 */
void
xfs_inode_item_format(xfs_inode_log_item_t	*iip,
		      xfs_log_iovec_t		*log_vector)
{
	uint		total_size;
	xfs_log_iovec_t	*vecp;
	xfs_inode_t	*ip;

	ip = iip->ili_inode;
	vecp = log_vector;

	vecp->i_addr = (caddr_t)&iip->ili_format;
	vecp->i_len = sizeof(xfs_inode_log_format_t);
	vecp++;
	total_size= sizeof(xfs_inode_log_format_t);

	vecp->i_addr = (caddr_t)&ip->i_d;
	vecp->i_len = sizeof(xfs_dinode_core_t);	
	vecp++;
	total_size += sizeof(xfs_dinode_core_t);

	switch (ip->i_d.di_format) {
	case XFS_DINODE_FMT_EXTENTS:
		if ((iip->ili_format.ilf_fields & XFS_ILOG_EXT) &&
		    (ip->i_bytes > 0)) {
			ASSERT(ip->i_u1.iu_extents != NULL);
			ASSERT(ip->i_d.di_nextents > 0);
			vecp->i_addr = (caddr_t)ip->i_u1.iu_extents;
			vecp->i_len = ip->i_bytes;
			vecp++;
			total_size += ip->i_bytes;
		}
		break;

	case XFS_DINODE_FMT_BTREE:
		if ((iip->ili_format.ilf_fields & XFS_ILOG_BROOT) &&
		    (ip->i_broot_bytes > 0)) {
			ASSERT(ip->i_broot != NULL);
			vecp->i_addr = (caddr_t)ip->i_broot;
			vecp->i_len = ip->i_broot_bytes;
			vecp++;
			total_size += ip->i_broot_bytes;
		}
		break;

	case XFS_DINODE_FMT_LOCAL:
		if ((iip->ili_format.ilf_fields & XFS_ILOG_DATA) &&
		    (ip->i_bytes > 0)) {
			ASSERT(ip->i_u1.iu_data != NULL);
			ASSERT(ip->i_d.di_size > 0);
			vecp->i_addr = (caddr_t)ip->i_u1.iu_data;
			vecp->i_len = ip->i_bytes;
			vecp++;
			total_size += ip->i_bytes;
		}
		break;

	case XFS_DINODE_FMT_DEV:
		if (iip->ili_format.ilf_fields & XFS_ILOG_DEV) {
			iip->ili_format.ilf_u.ilfu_rdev = ip->i_u2.iu_rdev;
		}
		break;

	default:
		ASSERT(0);
		break;
	}

	iip->ili_format.ilf_size = total_size;
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
		xfs_iunlock(ip, XFS_ILOCK_SHARED);
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
	uint	iolocked;
	uint	lock_flags;

	ASSERT(ismrlocked(&(iip->ili_inode->i_lock), MR_UPDATE));
	ASSERT((!(iip->ili_inode->i_item.ili_flags & XFS_ILI_IOLOCKED_EXCL)) ||
	       ismrlocked(&(iip->ili_inode->i_iolock), MR_UPDATE));
	ASSERT((!(iip->ili_inode->i_item.ili_flags & XFS_ILI_IOLOCKED_SHARED))||
	       ismrlocked(&(iip->ili_inode->i_iolock), MR_ACCESS));
	/*
	 * Clear the transaction pointer in the inode.
	 */
	iip->ili_inode->i_transp = NULL;

	/*
	 * Figure out if we should unlock the inode or not.
	 */
	hold = iip->ili_flags & XFS_ILI_HOLD;

	/*
	 * Before clearing out the flags, remember whether we
	 * are holding the inode's IO lock.
	 */
	iolocked = iip->ili_flags & XFS_ILI_IOLOCKED_ANY;

	/*
	 * Clear out the fields of the inode log item particular
	 * to the current transaction.
	 */
	iip->ili_ilock_recur = 0;
	iip->ili_iolock_recur = 0;
	iip->ili_flags = 0;

	/*
	 * Unlock the inode if XFS_ILI_HOLD was not set.
	 */
	if (!hold) {
		lock_flags = XFS_ILOCK_EXCL;
		if (iolocked & XFS_ILI_IOLOCKED_EXCL) {
			lock_flags |= XFS_IOLOCK_EXCL;
		} else if (iolocked & XFS_ILI_IOLOCKED_SHARED) {
			lock_flags |= XFS_IOLOCK_SHARED;
		}
		xfs_iput(iip->ili_inode, lock_flags);
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
	 * Since we were able to lock the inode's flush lock and
	 * we found it on the AIL, the inode must be dirty.  This
	 * is because the inode is removed from the AIL while still
	 * holding the flush lock in xfs_iflush_done().  Thus, if
	 * we found it in the AIL and were able to obtain the flush
	 * lock without sleeping, then there must not have been
	 * anyone in the process of flushing the inode.
	 */
	ip = iip->ili_inode;
	ASSERT(iip->ili_format.ilf_fields != 0);

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
	(void(*)(xfs_log_item_t*, xfs_log_iovec_t*))xfs_inode_item_format,
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
xfs_inode_item_init(xfs_inode_t	*ip,
		    xfs_mount_t	*mp)
{
	xfs_inode_log_item_t	*iip;

	iip = &ip->i_item;

	iip->ili_item.li_type = XFS_LI_INODE;
	iip->ili_item.li_ops = &xfs_inode_item_ops;
	iip->ili_item.li_mountp = mp;
	iip->ili_inode = ip;
	iip->ili_format.ilf_type = XFS_LI_INODE;
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
	 * We only want to pull the item from the AIL if it is
	 * actually there and its location in the log has not
	 * changed since we started the flush.  Thus, we only bother
	 * if the ili_logged flag is set and the inode's lsn has not
	 * changed.  First we check the lsn outside
	 * the lock since it's cheaper, and then we recheck while
	 * holding the lock before removing the inode from the AIL.
	 */
	if (iip->ili_logged &&
	    (iip->ili_item.li_lsn == iip->ili_flush_lsn)) {
		s = AIL_LOCK(ip->i_mount);
		if (iip->ili_item.li_lsn == iip->ili_flush_lsn) {
			xfs_trans_delete_ail(ip->i_mount,
					     (xfs_log_item_t*)iip);
		}
		AIL_UNLOCK(ip->i_mount, s);
	}

	/*
	 * Check to see if we should drop the reference taken by
	 * xfs_trans_log_inode() by looking at ili_logged.  ili_logged
	 * is guarded by the i_flock, so check and clear it before
	 * unlocking the i_flock.
	 */
	if (iip->ili_logged != 0) {
		drop_ref = 1;
		iip->ili_logged = 0;
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
		vn_rele(XFS_ITOV(ip));
	}
		
	return;
}




