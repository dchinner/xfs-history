#ident "$Revision: 1.31 $"

#ifdef SIM
#define _KERNEL	1
#endif
#include <sys/param.h>
#include <sys/buf.h>
#include <sys/sysmacros.h>
#include <sys/atomic_ops.h>
#ifdef SIM
#undef _KERNEL
#endif
#include <sys/vnode.h>
#include <sys/debug.h>
#include <sys/errno.h>
#ifdef SIM
#include <bstring.h>
#else
#include <sys/sysinfo.h>
#include <sys/kmem.h>
#include <sys/conf.h>
#include <sys/user.h>
#include <sys/systm.h>
#endif
#include <sys/cmn_err.h>
#include "xfs_types.h"
#include "xfs_inum.h"
#include "xfs_log.h"
#include "xfs_trans.h"
#include "xfs_buf_item.h"
#include "xfs_sb.h"
#include "xfs_ag.h"
#include "xfs_mount.h"
#include "xfs_trans_priv.h"

#ifdef SIM
#include "sim.h"
#endif

STATIC buf_t *
xfs_trans_buf_item_match(
	xfs_trans_t	*tp,
	dev_t		dev,
	daddr_t		blkno,
	int		len);


/*
 * Get and lock the buffer for the caller if it is not already
 * locked within the given transaction.  If it is already locked
 * within the transaction, just increment its lock recursion count
 * and return a pointer to it.
 *
 * Use the fast path function xfs_trans_buf_item_match() or the buffer
 * cache routine incore_match() to find the buffer
 * if it is already owned by this transaction.
 *
 * If we don't already own the buffer, use get_buf() to get it.
 * If it doesn't yet have an associated xfs_buf_log_item structure,
 * then allocate one and add the item to this transaction.
 *
 * If the transaction pointer is NULL, make this just a normal
 * get_buf() call.
 */
buf_t *
xfs_trans_get_buf(xfs_trans_t	*tp,
		  dev_t		dev,
		  daddr_t	blkno,
		  int		len,
		  uint		flags)
{
	buf_t			*bp;
	xfs_buf_log_item_t	*bip;

	/*
	 * Default to a normal get_buf() call if the tp is NULL.
	 * Always specify the BUF_BUSY flag so that get_buf() does
	 * not try to push out dirty buffers.  This keeps us from
	 * running out of stack space due to recursive calls into
	 * the buffer cache.
	 */
	if (tp == NULL) {
		return (get_buf(dev, blkno, len, flags | BUF_BUSY));
	}

	/*
	 * If we find the buffer in the cache with this transaction
	 * pointer in its b_fsprivate2 field, then we know we already
	 * have it locked.  In this case we just increment the lock
	 * recursion count and return the buffer to the caller.
	 */
	if (tp->t_items.lic_next == NULL) {
		bp = xfs_trans_buf_item_match(tp, dev, blkno, len);
	} else {
		bp = incore_match(dev, blkno, len, BUF_FSPRIV2, tp);
	}
	if (bp != NULL) {
		ASSERT(bp->b_fsprivate2 == tp);
		bip = (xfs_buf_log_item_t*)bp->b_fsprivate;
		ASSERT(bip != NULL);
		ASSERT(bip->bli_refcount > 0);
		bip->bli_recur++;
		xfs_buf_item_trace("GET RECUR", bip);
		return (bp);
	}

	/*
	 * We always specify the BUF_BUSY flag within a transaction so
	 * that get_buf does not try to push out a delayed write buffer
	 * which might cause another transaction to take place (if the
	 * buffer was delayed alloc).  Such recursive transactions can
	 * easily deadlock with our current transaction as well as cause
	 * us to run out of stack space.
	 */
	bp = get_buf(dev, blkno, len, flags | BUF_BUSY);
	if (bp == NULL) {
		return NULL;
	}

	/*
	 * The xfs_buf_log_item pointer is stored in b_fsprivate.  If
	 * it doesn't have one yet, then allocate one and initialize it.
	 * The checks to see if one is there are in xfs_buf_item_init().
	 */
	xfs_buf_item_init(bp, tp->t_mountp);

	/*
	 * Set the recursion count for the buffer within this transaction
	 * to 0.
	 */
	bip = (xfs_buf_log_item_t*)bp->b_fsprivate;
	ASSERT(!(bip->bli_flags & XFS_BLI_STALE));
	ASSERT(!(bip->bli_format.blf_flags & XFS_BLI_CANCEL));
 	ASSERT(!(bip->bli_flags & XFS_BLI_LOGGED));
	bip->bli_recur = 0;

	/*
	 * Take a reference for this transaction on the buf item.
	 */
	(void) atomicAddInt(&bip->bli_refcount, 1);

	/*
	 * Get a log_item_desc to point at the new item.
	 */
	(void) xfs_trans_add_item(tp, (xfs_log_item_t*)bip);

	/*
	 * Initialize b_fsprivate2 so we can find it with incore_match()
	 * above.
	 */
	bp->b_fsprivate2 = tp;

	xfs_buf_item_trace("GET", bip);
	return (bp);
}

/*
 * Get and lock the superblock buffer of this file system for the
 * given transaction.
 *
 * We don't need to use incore_match() here, because the superblock
 * buffer is a private buffer which we keep a pointer to in the
 * mount structure.
 */
buf_t *
xfs_trans_getsb(xfs_trans_t	*tp,
		int		flags)
{
	buf_t			*bp;
	xfs_buf_log_item_t	*bip;

	/*
	 * Default to just trying to lock the superblock buffer
	 * if tp is NULL.
	 */
	if (tp == NULL) {
		return (xfs_getsb(tp->t_mountp, flags));
	}

	/*
	 * If the superblock buffer already has this transaction
	 * pointer in its b_fsprivate2 field, then we know we already
	 * have it locked.  In this case we just increment the lock
	 * recursion count and return the buffer to the caller.
	 */
	bp = tp->t_mountp->m_sb_bp;
	if (((xfs_trans_t *)bp->b_fsprivate2) == tp) {
		bip = (xfs_buf_log_item_t*)bp->b_fsprivate;
		ASSERT(bip != NULL);
		ASSERT(bip->bli_refcount > 0);
		bip->bli_recur++;
		xfs_buf_item_trace("GETSB RECUR", bip);
		return (bp);
	}

	bp = xfs_getsb(tp->t_mountp, flags);
	if (bp == NULL) {
		return NULL;
	}

	/*
	 * The xfs_buf_log_item pointer is stored in b_fsprivate.  If
	 * it doesn't have one yet, then allocate one and initialize it.
	 * The checks to see if one is there are in xfs_buf_item_init().
	 */
	xfs_buf_item_init(bp, tp->t_mountp);

	/*
	 * Set the recursion count for the buffer within this transaction
	 * to 0.
	 */
	bip = (xfs_buf_log_item_t*)bp->b_fsprivate;
	ASSERT(!(bip->bli_flags & XFS_BLI_STALE));
	ASSERT(!(bip->bli_format.blf_flags & XFS_BLI_CANCEL));
 	ASSERT(!(bip->bli_flags & XFS_BLI_LOGGED));
	bip->bli_recur = 0;

	/*
	 * Take a reference for this transaction on the buf item.
	 */
	(void) atomicAddInt(&bip->bli_refcount, 1);

	/*
	 * Get a log_item_desc to point at the new item.
	 */
	(void) xfs_trans_add_item(tp, (xfs_log_item_t*)bip);

	/*
	 * Initialize b_fsprivate2 so we can find it with incore_match()
	 * above.
	 */
	bp->b_fsprivate2 = tp;

	xfs_buf_item_trace("GETSB", bip);
	return (bp);
}

#ifdef DEBUG
dev_t	xfs_error_dev = 0x2000027;
int	xfs_do_error;
int	xfs_req_num;
int	xfs_error_mod = 33;
#endif

/*
 * Get and lock the buffer for the caller if it is not already
 * locked within the given transaction.  If it has not yet been
 * read in, read it from disk. If it is already locked
 * within the transaction and already read in, just increment its
 * lock recursion count and return a pointer to it.
 *
 * Use the fast path function xfs_trans_buf_item_match() or the buffer
 * cache routine incore_match() to find the buffer
 * if it is already owned by this transaction.
 *
 * If we don't already own the buffer, use read_buf() to get it.
 * If it doesn't yet have an associated xfs_buf_log_item structure,
 * then allocate one and add the item to this transaction.
 *
 * If the transaction pointer is NULL, make this just a normal
 * read_buf() call.
 */
int
xfs_trans_read_buf(xfs_trans_t	*tp,
		   dev_t	dev,
		   daddr_t	blkno,
		   int		len,
		   uint		flags,
		   buf_t	**bpp)
{
	buf_t			*bp;
	xfs_buf_log_item_t	*bip;
	int			error;

	/*
	 * Default to a normal get_buf() call if the tp is NULL.
	 * Always specify the BUF_BUSY flag so that get_buf() does
	 * not try to push out dirty buffers.  This keeps us from
	 * running out of stack space due to recursive calls into
	 * the buffer cache.
	 */
	if (tp == NULL) {
		bp = read_buf(dev, blkno, len, flags | BUF_BUSY);
		if ((bp != NULL) && (geterror(bp) != 0)) {
			prdev("XFS read error in file system meta-data block %ld", bp->b_edev, bp->b_blkno);
			error = geterror(bp);
			brelse(bp);
			return error;
		}
#ifdef DEBUG
		if (xfs_do_error && (bp != NULL)) {
			if (xfs_error_dev == bp->b_edev) {
				if (((xfs_req_num++) % xfs_error_mod) == 0) {
					brelse(bp);
					printf("Returning error!\n");
					return EIO;
				}
			}
		}
#endif
		*bpp = bp;
		return 0;
	}

	/*
	 * If we find the buffer in the cache with this transaction
	 * pointer in its b_fsprivate2 field, then we know we already
	 * have it locked.  If it is already read in we just increment
	 * the lock recursion count and return the buffer to the caller.
	 * If the buffer is not yet read in, then we read it in, increment
	 * the lock recursion count, and return it to the caller.
	 */
	if (tp->t_items.lic_next == NULL) {
		bp = xfs_trans_buf_item_match(tp, dev, blkno, len);
	} else {
		bp = incore_match(dev, blkno, len, BUF_FSPRIV2, tp);
	}
	if (bp != NULL) {
		ASSERT(bp->b_fsprivate2 == tp);
		ASSERT(bp->b_fsprivate != NULL);
		if (!(bp->b_flags & B_DONE)) {
			ASSERT(0);
#ifndef SIM
			SYSINFO.lread += len;
#endif

			ASSERT(!(bp->b_flags & B_ASYNC));
			bp->b_flags |= B_READ;
			bdstrat(bmajor(dev), bp);

#ifndef SIM
			u.u_ior++;
			SYSINFO.bread += len;
#endif

			iowait(bp);
			if (geterror(bp) != 0) {
				cmn_err(CE_PANIC, "XFS dev 0x%x read error in file system meta-data", bp->b_edev);
			}
		}

		bip = (xfs_buf_log_item_t*)bp->b_fsprivate;
		bip->bli_recur++;

		ASSERT(bip->bli_refcount > 0);
		xfs_buf_item_trace("READ RECUR", bip);
		*bpp = bp;
		return 0;
	}

	/*
	 * We always specify the BUF_BUSY flag within a transaction so
	 * that get_buf does not try to push out a delayed write buffer
	 * which might cause another transaction to take place (if the
	 * buffer was delayed alloc).  Such recursive transactions can
	 * easily deadlock with our current transaction as well as cause
	 * us to run out of stack space.
	 */
	bp = read_buf(dev, blkno, len, flags | BUF_BUSY);
	if (bp == NULL) {
		*bpp = NULL;
		return 0;
	}
	if (geterror(bp) != 0) {
		cmn_err(CE_PANIC, "XFS dev 0x%x read error in file system meta-data", bp->b_edev);
	}
#ifdef DEBUG
	if (xfs_do_error && !(tp->t_flags & XFS_TRANS_DIRTY)) {
		if (xfs_error_dev == bp->b_edev) {
			if (((xfs_req_num++) % xfs_error_mod) == 0) {
				brelse(bp);
				printf("Returning error in trans!\n");
				return EIO;
			}
		}
	}
#endif

	/*
	 * The xfs_buf_log_item pointer is stored in b_fsprivate.  If
	 * it doesn't have one yet, then allocate one and initialize it.
	 * The checks to see if one is there are in xfs_buf_item_init().
	 */
	xfs_buf_item_init(bp, tp->t_mountp);

	/*
	 * Set the recursion count for the buffer within this transaction
	 * to 0.
	 */
	bip = (xfs_buf_log_item_t*)bp->b_fsprivate;
	ASSERT(!(bip->bli_flags & XFS_BLI_STALE));
	ASSERT(!(bip->bli_format.blf_flags & XFS_BLI_CANCEL));
 	ASSERT(!(bip->bli_flags & XFS_BLI_LOGGED));
	bip->bli_recur = 0;

	/*
	 * Take a reference for this transaction on the buf item.
	 */
	(void) atomicAddInt(&bip->bli_refcount, 1);

	/*
	 * Get a log_item_desc to point at the new item.
	 */
	(void) xfs_trans_add_item(tp, (xfs_log_item_t*)bip);

	/*
	 * Initialize b_fsprivate2 so we can find it with incore_match()
	 * above.
	 */
	bp->b_fsprivate2 = tp;

	xfs_buf_item_trace("READ", bip);
	*bpp = bp;
	return 0;
}


/*
 * Release the buffer bp which was previously acquired with one of the
 * xfs_trans_... buffer allocation routines if the buffer has not
 * been modified within this transaction.  If the buffer is modified
 * within this transaction, do decrement the recursion count but do
 * not release the buffer even if the count goes to 0.  If the buffer is not
 * modified within the transaction, decrement the recursion count and
 * release the buffer if the recursion count goes to 0.
 *
 * If the buffer is to be released and it was not modified before
 * this transaction began, then free the buf_log_item associated with it.
 *
 * If the transaction pointer is NULL, make this just a normal
 * brelse() call.
 */
void
xfs_trans_brelse(xfs_trans_t	*tp,
		 buf_t		*bp)
{
	xfs_buf_log_item_t	*bip;
	xfs_log_item_t		*lip;
	xfs_log_item_desc_t	*lidp;

	/*
	 * Default to a normal brelse() call if the tp is NULL.
	 */
	if (tp == NULL) {
		ASSERT(bp->b_fsprivate2 == NULL);
		/*
		 * If there's a buf log item attached to the buffer,
		 * then let the AIL know that the buffer is being
		 * unlocked.
		 */
		if (bp->b_fsprivate != NULL) {
			lip = (xfs_log_item_t *)bp->b_fsprivate;
			if (lip->li_type == XFS_LI_BUF) {
				bip = (xfs_buf_log_item_t*)bp->b_fsprivate;
				xfs_trans_unlocked_item(
						bip->bli_item.li_mountp,
						lip);
			}
		}
		brelse(bp);
		return;
	}

	ASSERT(((xfs_trans_t *)bp->b_fsprivate2) == tp);
	bip = (xfs_buf_log_item_t*)bp->b_fsprivate;	
	ASSERT(bip->bli_item.li_type == XFS_LI_BUF);
	ASSERT(!(bip->bli_flags & XFS_BLI_STALE));
	ASSERT(!(bip->bli_format.blf_flags & XFS_BLI_CANCEL));
	ASSERT(bip->bli_refcount > 0);

	/*
	 * Find the item descriptor pointing to this buffer's
	 * log item.  It must be there.
	 */
	lidp = xfs_trans_find_item(tp, (xfs_log_item_t*)bip);
	ASSERT(lidp != NULL);

	/*
	 * If the release is just for a recursive lock,
	 * then decrement the count and return.
	 */
	if (bip->bli_recur > 0) {
		bip->bli_recur--;
		xfs_buf_item_trace("RELSE RECUR", bip);
		return;
	}

	/*
	 * If the buffer is dirty within this transaction, we can't
	 * release it until we commit.
	 */
	if (lidp->lid_flags & XFS_LID_DIRTY) {
		xfs_buf_item_trace("RELSE DIRTY", bip);
		return;
	}

	/*
	 * If the buffer has been invalidated, then we can't release
	 * it until the transaction commits to disk unless it is re-dirtied
	 * as part of this transaction.  This prevents us from pulling
	 * the item from the AIL before we should.
	 */
	if (bip->bli_flags & XFS_BLI_STALE) {
		xfs_buf_item_trace("RELSE STALE", bip);
		return;
	}
	 
	ASSERT(!(bip->bli_flags & XFS_BLI_LOGGED));
	xfs_buf_item_trace("RELSE", bip);

	/*
	 * Free up the log item descriptor tracking the released item.
	 */
	xfs_trans_free_item(tp, lidp);

	/*
	 * Clear the hold flag in the buf log item if it is set.
	 * We wouldn't want the next user of the buffer to
	 * get confused.
	 */
	if (bip->bli_flags & XFS_BLI_HOLD) {
		bip->bli_flags &= ~XFS_BLI_HOLD;
	}

	/*
	 * Drop our reference to the buf log item.
	 */
	(void) atomicAddInt(&bip->bli_refcount, -1);

	/*
	 * If the buf item is not tracking data in the log, then
	 * we must free it before releasing the buffer back to the
	 * free pool.  Before releasing the buffer to the free pool,
	 * clear the transaction pointer in b_fsprivate2 to disolve
	 * its relation to this transaction.
	 */
	if (!xfs_buf_item_dirty(bip)) {
		ASSERT(bp->b_pincount == 0);
		ASSERT(bip->bli_refcount == 0);
		ASSERT(!(bip->bli_item.li_flags & XFS_LI_IN_AIL));
		ASSERT(!(bip->bli_flags & XFS_BLI_INODE_ALLOC_BUF));
		xfs_buf_item_relse(bp);
		bip = NULL;
	}
	bp->b_fsprivate2 = NULL;

	/*
	 * If we've still got a buf log item on the buffer, then
	 * tell the AIL that the buffer is being unlocked.
	 */
	if (bip != NULL) {
		xfs_trans_unlocked_item(bip->bli_item.li_mountp,
					(xfs_log_item_t*)bip);
	}
	brelse(bp);
	return;
}

/*
 * Add the locked buffer to the transaction.
 * The buffer must be locked, and it cannot be associated with any
 * transaction.
 *
 * If the buffer does not yet have a buf log item associated with it,
 * then allocate one for it.  Then add the buf item to the transaction.
 */
void
xfs_trans_bjoin(xfs_trans_t	*tp,
		buf_t		*bp)
{
	xfs_buf_log_item_t	*bip;

	ASSERT(bp->b_flags & B_BUSY);
	ASSERT(bp->b_fsprivate2 == NULL);

	/*
	 * The xfs_buf_log_item pointer is stored in b_fsprivate.  If
	 * it doesn't have one yet, then allocate one and initialize it.
	 * The checks to see if one is there are in xfs_buf_item_init().
	 */
	xfs_buf_item_init(bp, tp->t_mountp);
	bip = bp->b_fsprivate;
	ASSERT(!(bip->bli_flags & XFS_BLI_STALE));
	ASSERT(!(bip->bli_format.blf_flags & XFS_BLI_CANCEL));
	ASSERT(!(bip->bli_flags & XFS_BLI_LOGGED));

	/*
	 * Take a reference for this transaction on the buf item.
	 */
	(void) atomicAddInt(&bip->bli_refcount, 1);

	/*
	 * Get a log_item_desc to point at the new item.
	 */
	(void) xfs_trans_add_item(tp, (xfs_log_item_t *)bip);

	/*
	 * Initialize b_fsprivate2 so we can find it with incore_match()
	 * in xfs_trans_get_buf() and friends above.
	 */
	bp->b_fsprivate2 = tp;

	xfs_buf_item_trace("BJOIN", bip);
}

/*
 * Mark the buffer as not needing to be unlocked when the buf item's
 * IOP_UNLOCK() routine is called.  The buffer must already be locked
 * and associated with the given transaction.
 */
/* ARGSUSED */
void
xfs_trans_bhold(xfs_trans_t	*tp,
		buf_t		*bp)
{
	xfs_buf_log_item_t	*bip;

	ASSERT(bp->b_flags & B_BUSY);
	ASSERT((xfs_trans_t*)(bp->b_fsprivate2) == tp);
	ASSERT(bp->b_fsprivate != NULL);

	bip = (xfs_buf_log_item_t*)(bp->b_fsprivate);
	ASSERT(!(bip->bli_flags & XFS_BLI_STALE));
	ASSERT(!(bip->bli_format.blf_flags & XFS_BLI_CANCEL));
	ASSERT(bip->bli_refcount > 0);
	bip->bli_flags |= XFS_BLI_HOLD;
	xfs_buf_item_trace("BHOLD", bip);
}

/*
 * This function is used to indicate that the buffer should not be
 * unlocked until the transaction is committed to disk.  Since we
 * are going to keep the lock held, make the transaction synchronous
 * so that the lock is not held too long.
 *
 * It uses the log item descriptor flag XFS_LID_SYNC_UNLOCK to
 * delay the buf items's unlock call until the transaction is
 * committed to disk or aborted.
 */
void
xfs_trans_bhold_until_committed(xfs_trans_t	*tp,
				buf_t		*bp)
{
	xfs_log_item_desc_t	*lidp;
	xfs_buf_log_item_t	*bip;

	ASSERT(bp->b_flags & B_BUSY);
	ASSERT((xfs_trans_t*)(bp->b_fsprivate2) == tp);
	ASSERT(bp->b_fsprivate != NULL);

	bip = (xfs_buf_log_item_t *)(bp->b_fsprivate);
	ASSERT(!(bip->bli_flags & XFS_BLI_STALE));
	ASSERT(!(bip->bli_format.blf_flags & XFS_BLI_CANCEL));
	ASSERT(bip->bli_refcount > 0);
	lidp = xfs_trans_find_item(tp, (xfs_log_item_t*)bip);
	ASSERT(lidp != NULL);

	lidp->lid_flags |= XFS_LID_SYNC_UNLOCK;
	xfs_buf_item_trace("BHOLD UNTILC OMMIT", bip);

	xfs_trans_set_sync(tp);
}


/*
 * This is called to mark bytes first through last inclusive of the given
 * buffer as needing to be logged when the transaction is committed.
 * The buffer must already be associated with the given transaction.
 *
 * First and last are numbers relative to the beginning of this buffer,
 * so the first byte in the buffer is numbered 0 regardless of the
 * value of b_blkno.
 */
void
xfs_trans_log_buf(xfs_trans_t	*tp,
		  buf_t		*bp,
		  uint		first,
		  uint		last)
{
	xfs_buf_log_item_t	*bip;
	xfs_log_item_desc_t	*lidp;

	ASSERT(bp->b_flags & B_BUSY);
	ASSERT((xfs_trans_t*)bp->b_fsprivate2 == tp);
	ASSERT(bp->b_fsprivate != NULL);
	ASSERT((first <= last) && (last <= bp->b_bcount));
	ASSERT((bp->b_iodone == NULL) ||
	       (bp->b_iodone == xfs_buf_iodone_callbacks));

	/*
	 * Mark the buffer as needing to be written out eventually,
	 * and set its iodone function to remove the buffer's buf log
	 * item from the AIL and free it when the buffer is flushed
	 * to disk.  See xfs_buf_attach_iodone() for more details
	 * on li_cb and xfs_buf_iodone_callbacks().
	 */
	bp->b_flags |= B_DELWRI | B_DONE;
	bip = (xfs_buf_log_item_t*)bp->b_fsprivate;
	ASSERT(bip->bli_refcount > 0);
	if (bp->b_iodone == NULL) {
		bp->b_iodone = xfs_buf_iodone_callbacks;
	}
	bip->bli_item.li_cb = (void(*)(buf_t*,xfs_log_item_t*))xfs_buf_iodone;

	/*
	 * If we invalidated the buffer within this transaction, then
	 * cancel the invalidation now that we're dirtying the buffer
	 * again.  There are no races with the code in xfs_buf_item_unpin(),
	 * because we have a reference to the buffer this entire time.
	 */
	if (bip->bli_flags & XFS_BLI_STALE) {
		xfs_buf_item_trace("BLOG UNSTALE", bip);
		bip->bli_flags &= ~XFS_BLI_STALE;
		bip->bli_format.blf_flags &= ~XFS_BLI_CANCEL;
	}

	lidp = xfs_trans_find_item(tp, (xfs_log_item_t*)bip);
	ASSERT(lidp != NULL);

	tp->t_flags |= XFS_TRANS_DIRTY;
	lidp->lid_flags |= XFS_LID_DIRTY;
	bip->bli_flags |= XFS_BLI_LOGGED;
	xfs_buf_item_log(bip, first, last);
	xfs_buf_item_trace("BLOG", bip);
}


/*
 * This called to invalidate a buffer that is being used within
 * a transaction.  Typically this is because the blocks in the
 * buffer are being freed, so we need to prevent it from being
 * written out when we're done.  Allowing it to be written again
 * might overwrite data in the free blocks if they are reallocated
 * to a file.
 *
 * We prevent the buffer from being written out by clearing the
 * B_DELWRI flag.  We can't always
 * get rid of the buf log item at this point, though, because
 * the buffer may still be pinned by other transaction.  If that
 * is the case, then we'll wait until the buffer is committed to
 * disk for the last time (we can tell by the ref count) and
 * free it in xfs_buf_item_unpin().  Until it is cleaned up we
 * will keep the buffer locked so that the buffer and buf log item
 * are not reused.
 */
void
xfs_trans_binval(
	xfs_trans_t	*tp,
	buf_t		*bp)
{
	xfs_log_item_desc_t	*lidp;
	xfs_buf_log_item_t	*bip;

	ASSERT(bp->b_flags & B_BUSY);
	ASSERT((xfs_trans_t*)(bp->b_fsprivate2) == tp);
	ASSERT(bp->b_fsprivate != NULL);

	bip = (xfs_buf_log_item_t *)(bp->b_fsprivate);
	lidp = xfs_trans_find_item(tp, (xfs_log_item_t*)bip);
	ASSERT(lidp != NULL);
	ASSERT(bip->bli_refcount > 0);

	if (bip->bli_flags & XFS_BLI_STALE) {
		/*
		 * If the buffer is already invalidated, then
		 * just return.
		 */
		ASSERT(!(bp->b_flags & B_DELWRI));
		ASSERT(!(bip->bli_flags & (XFS_BLI_LOGGED | XFS_BLI_DIRTY)));
		ASSERT(!(bip->bli_format.blf_flags & XFS_BLI_INODE_BUF));
		ASSERT(bip->bli_format.blf_flags & XFS_BLI_CANCEL);
		ASSERT(lidp->lid_flags & XFS_LID_DIRTY);
		ASSERT(tp->t_flags & XFS_TRANS_DIRTY);
		xfs_buf_item_trace("BINVAL RECUR", bip);
		return;
	}

	/*
	 * Clear the dirty bit in the buffer and set the STALE flag
	 * in the buf log item.  The STALE flag will be used in
	 * xfs_buf_item_unpin() to determine if it should clean up
	 * when the last reference to the buf item is given up.
	 * We set the XFS_BLI_CANCEL flag in the buf log format structure
	 * and log the buf item.  This will be used at recovery time
	 * to determine that copies of the buffer in the log before
	 * this should not be replayed.
	 * We mark the item descriptor and the transaction dirty so
	 * that we'll hold the buffer until after the commit.
	 *
	 * Since we're invalidating the buffer, we also clear the state
	 * about which parts of the buffer have been logged.  We also
	 * clear the flag indicating that this is an inode buffer since
	 * the data in the buffer will no longer be valid.
	 */
	bp->b_flags &= ~B_DELWRI;
	bip->bli_flags |= XFS_BLI_STALE;
	bip->bli_flags &= ~(XFS_BLI_LOGGED | XFS_BLI_DIRTY);
	bip->bli_format.blf_flags &= ~XFS_BLI_INODE_BUF;
	bip->bli_format.blf_flags |= XFS_BLI_CANCEL;
	bzero((char *)(bip->bli_format.blf_data_map),
	      (bip->bli_format.blf_map_size * sizeof(uint)));
	lidp->lid_flags |= XFS_LID_DIRTY;
	tp->t_flags |= XFS_TRANS_DIRTY;
	xfs_buf_item_trace("BINVAL", bip);
}

/*
 * This call is used to indicate that the buffer contains on-disk
 * inodes which must be handled specially during recovery.  They
 * require special handling because only the di_next_unlinked from
 * the inodes in the buffer should be recovered.  The rest of the
 * data in the buffer is logged via the inodes themselves.
 *
 * All we do is set the XFS_BLI_INODE_BUF flag in the buffer's log
 * format structure so that we'll know what to do at recovery time.
 */
/* ARGSUSED */
void
xfs_trans_inode_buf(
	xfs_trans_t	*tp,
	buf_t		*bp)
{
	xfs_buf_log_item_t	*bip;

	ASSERT(bp->b_flags & B_BUSY);
	ASSERT((xfs_trans_t*)(bp->b_fsprivate2) == tp);
	ASSERT(bp->b_fsprivate != NULL);

	bip = (xfs_buf_log_item_t *)(bp->b_fsprivate);
	ASSERT(bip->bli_refcount > 0);

	bip->bli_format.blf_flags |= XFS_BLI_INODE_BUF;
}


/*
 * Mark the buffer as being one which contains newly allocated
 * inodes.  We need to make sure that even if this buffer is
 * relogged as an 'inode buf' we still recover all of the inode
 * images in the face of a crash.  This works in coordination with
 * xfs_buf_item_committed() to ensure that the buffer remains in the
 * AIL at its original location even after it has been relogged.
 */
/* ARGSUSED */
void
xfs_trans_inode_alloc_buf(
	xfs_trans_t	*tp,
	buf_t		*bp)
{
	xfs_buf_log_item_t	*bip;

	ASSERT(bp->b_flags & B_BUSY);
	ASSERT((xfs_trans_t*)(bp->b_fsprivate2) == tp);
	ASSERT(bp->b_fsprivate != NULL);

	bip = (xfs_buf_log_item_t *)(bp->b_fsprivate);
	ASSERT(bip->bli_refcount > 0);
	ASSERT(!(bip->bli_flags & XFS_BLI_INODE_ALLOC_BUF));

	bip->bli_flags |= XFS_BLI_INODE_ALLOC_BUF;
}


/*
 * Check to see if a buffer matching the given parameters is already
 * a part of the given transaction.  Only check the first, embedded
 * chunk, since we don't want to spend all day scanning large transactions.
 */
STATIC buf_t *
xfs_trans_buf_item_match(
	xfs_trans_t	*tp,
	dev_t		dev,
	daddr_t		blkno,
	int		len)
{
	xfs_log_item_chunk_t	*licp;
	xfs_log_item_desc_t	*lidp;
	xfs_buf_log_item_t	*blip;
	buf_t			*bp;
	int			i;

	bp = NULL;
	len = BBTOB(len);
	licp = &tp->t_items;
	if (!XFS_LIC_ARE_ALL_FREE(licp)) {
		for (i = 0; i <= XFS_LIC_MAX_SLOT; i++) {
			/*
			 * Skip unoccupied slots.
			 */
			if (XFS_LIC_ISFREE(licp, i)) {
				continue;
			}

			lidp = XFS_LIC_SLOT(licp, i);
			blip = (xfs_buf_log_item_t *)lidp->lid_item;
			if (blip->bli_item.li_type != XFS_LI_BUF) {
				continue;
			}

			bp = blip->bli_buf;
			if ((bp->b_edev == dev) &&
			    (bp->b_blkno == blkno) &&
			    (bp->b_bcount == len)) {
				/*
				 * We found it.  Break out and
				 * return the pointer to the buffer.
				 */
				break;
			} else {
				bp = NULL;
			}
		}
	}
	return bp;
}
