

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
#include <sys/debug.h>
#include <sys/uuid.h>
#ifndef SIM
#include <sys/sysinfo.h>
#include <sys/kmem.h>
#include <sys/conf.h>
#include <sys/user.h>
#include <sys/systm.h>
#endif
#include "xfs_types.h"
#include "xfs_inum.h"
#include "xfs_log.h"
#include "xfs_trans.h"
#include "xfs_buf_item.h"
#include "xfs_bio.h"
#include "xfs_sb.h"
#include "xfs_ag.h"
#include "xfs_mount.h"
#include "xfs_trans_priv.h"

#ifdef SIM
#include "sim.h"
#endif

/*
 * Get and lock the buffer for the caller if it is not already
 * locked within the given transaction.  If it is already locked
 * within the transaction, just increment its lock recursion count
 * and return a pointer to it.
 *
 * Use the buffer cache routine incore_match() to find the buffer
 * if it is already owned by this transaction.
 *
 * If we don't already own the buffer, use xfs_getblk() to get it.
 * If it doesn't yet have an associated xfs_buf_log_item structure,
 * then allocate one and add the item to this transaction.
 *
 * If the transaction pointer is NULL, make this just a normal
 * getblk() call.
 */
buf_t *
xfs_trans_getblk(xfs_trans_t *tp, dev_t dev, daddr_t blkno, int len)
{
	buf_t			*bp;
	xfs_buf_log_item_t	*bip;

	/*
	 * Default to a normal getblk() call if the tp is NULL.
	 */
	if (tp == NULL) {
		return (getblk(dev, blkno, len));
	}

	/*
	 * If we find the buffer in the cache with this transaction
	 * pointer in its b_fsprivate2 field, then we know we already
	 * have it locked.  In this case we just increment the lock
	 * recursion count and return the buffer to the caller.
	 */
	if ((bp = incore_match(dev, blkno, len, BUF_FSPRIV2, tp)) != NULL) {
		bip = (xfs_buf_log_item_t*)bp->b_fsprivate;
		ASSERT(bip != NULL);
		bip->bli_recur++;
		return (bp);
	}

	bp = xfs_getblk(dev, blkno, len);

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
	bip->bli_recur = 0;

	/*
	 * Get a log_item_desc to point at the new item.
	 */
	(void) xfs_trans_add_item(tp, (xfs_log_item_t*)bip);

	/*
	 * Initialize b_fsprivate2 so we can find it with incore_match()
	 * above.
	 */
	bp->b_fsprivate2 = tp;

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
xfs_trans_getsb(xfs_trans_t *tp)
{
	buf_t			*bp;
	xfs_buf_log_item_t	*bip;

	/*
	 * Default to just trying to lock the superblock buffer
	 * if tp is NULL.
	 */
	if (tp == NULL) {
		return (xfs_getsb(tp->t_mountp));
	}

	/*
	 * If the superblock buffer already has this transaction
	 * pointer in its b_fsprivate2 field, then we know we already
	 * have it locked.  In this case we just increment the lock
	 * recursion count and return the buffer to the caller.
	 */
	bp = tp->t_mountp->m_sb_bp;
	if (bp->b_fsprivate2 == tp) {
		bip = (xfs_buf_log_item_t*)bp->b_fsprivate;
		ASSERT(bip != NULL);
		bip->bli_recur++;
		return (bp);
	}

	bp = xfs_getsb(tp->t_mountp);

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
	bip->bli_recur = 0;

	/*
	 * Get a log_item_desc to point at the new item.
	 */
	(void) xfs_trans_add_item(tp, (xfs_log_item_t*)bip);

	/*
	 * Initialize b_fsprivate2 so we can find it with incore_match()
	 * above.
	 */
	bp->b_fsprivate2 = tp;

	return (bp);
}


/*
 * Get and lock the buffer for the caller if it is not already
 * locked within the given transaction.  If it has not yet been
 * read in, read it from disk. If it is already locked
 * within the transaction and already read in, just increment its
 * lock recursion count and return a pointer to it.
 *
 * Use the buffer cache routine incore_match() to find the buffer
 * if it is already owned by this transaction.
 *
 * If we don't already own the buffer, use xfs_bread() to get it.
 * If it doesn't yet have an associated xfs_buf_log_item structure,
 * then allocate one and add the item to this transaction.
 *
 * If the transaction pointer is NULL, make this just a normal
 * bread() call.
 */
buf_t *
xfs_trans_bread(xfs_trans_t *tp, dev_t dev, daddr_t blkno, int len)
{
	buf_t			*bp;
	xfs_buf_log_item_t	*bip;

	/*
	 * Default to a normal bread() call if the tp is NULL.
	 */
	if (tp == NULL) {
		return (bread(dev, blkno, len));
	}

	/*
	 * If we find the buffer in the cache with this transaction
	 * pointer in its b_fsprivate2 field, then we know we already
	 * have it locked.  If it is already read in we just increment
	 * the lock recursion count and return the buffer to the caller.
	 * If the buffer is not yet read in, then we read it in, increment
	 * the lock recursion count, and return it to the caller.
	 */
	if ((bp = incore_match(dev, blkno, len, BUF_FSPRIV2, tp)) != NULL) {
		ASSERT(bp->b_fsprivate != NULL);
		if (!(bp->b_flags & B_DONE)) {
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
		}

		bip = (xfs_buf_log_item_t*)bp->b_fsprivate;
		bip->bli_recur++;

		return (bp);
	}

	bp = xfs_bread(dev, blkno, len);

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
	bip->bli_recur = 0;

	/*
	 * Get a log_item_desc to point at the new item.
	 */
	(void) xfs_trans_add_item(tp, (xfs_log_item_t*)bip);

	/*
	 * Initialize b_fsprivate2 so we can find it with incore_match()
	 * above.
	 */
	bp->b_fsprivate2 = tp;

	return (bp);
}


/*
 * Get and lock the buffer for the caller if it is not already
 * locked within the given transaction.  If it is already locked
 * within the transaction, just increment its lock recursion count
 * and return a pointer to it.
 *
 * Use the buffer cache routine findchunk_match() to find the buffer
 * if it is already owned by this transaction.
 *
 * If we don't already own the buffer, use xfs_getchunk() to get it.
 * If it doesn't yet have an associated xfs_buf_log_item structure,
 * then allocate one and add the item to this transaction.
 *
 * If the transaction pointer is NULL, make this just a normal
 * getchunk() call.
 */
#ifndef SIM
buf_t *
xfs_trans_getchunk(xfs_trans_t *tp, vnode_t *vp,
		   struct bmapval *bmap, struct cred *cred)
{
	buf_t			*bp;
	xfs_buf_log_item_t	*bip;

	/*
	 * Default to a normal getchunk() call if the tp is NULL.
	 */
	if (tp == NULL) {
		return (getchunk(vp, bmap, cred));
	}

	/*
	 * If we find the buffer in the cache with this transaction
	 * pointer in its b_fsprivate2 field, then we know we already
	 * have it locked.  In this case we just increment the lock
	 * recursion count and return the buffer to the caller.
	 */
	if ((bp = findchunk_match(vp, bmap, BUF_FSPRIV2, tp)) != NULL) {
		bip = (xfs_buf_log_item_t*)bp->b_fsprivate;
		ASSERT(bip != NULL);
		bip->bli_recur++;
		return (bp);
	}

	bp = xfs_getchunk(vp, bmap, cred);

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
	bip->bli_recur = 0;

	/*
	 * Get a log_item_desc to point at the new item.
	 */
	(void) xfs_trans_add_item(tp, (xfs_log_item_t*)bip);

	/*
	 * Initialize b_fsprivate2 so we can find it with findchunk_match()
	 * above.
	 */
	bp->b_fsprivate2 = tp;

	return (bp);
}
#endif /* SIM */


/*
 * Get and lock the buffer for the caller if it is not already
 * locked within the given transaction.  If it has not yet been
 * read in, read it from disk. If it is already locked
 * within the transaction and already read in, just increment its
 * lock recursion count and return a pointer to it.
 *
 * Use the buffer cache routine findchunk_match() to find the buffer
 * if it is already owned by this transaction.
 *
 * If we don't already own the buffer, use xfs_chunkread() to get it.
 * If it doesn't yet have an associated xfs_buf_log_item structure,
 * then allocate one and add the item to this transaction.
 *
 * If the transaction pointer is NULL, make this just a normal
 * chunkread() call.
 */
#ifndef SIM
buf_t *
xfs_trans_chunkread(xfs_trans_t *tp, vnode_t *vp,
		    struct bmapval *bmap, struct cred *cred)
{
	buf_t			*bp;
	xfs_buf_log_item_t	*bip;

	/*
	 * Default to a normal chunkread() call if the tp is NULL.
	 */
	if (tp == NULL) {
		return (chunkread(vp, bmap, 1, cred));
	}
	/*
	 * If we find the buffer in the cache with this transaction
	 * pointer in its b_fsprivate2 field, then we know we already
	 * have it locked.  If it is already read in we just increment
	 * the lock recursion count and return the buffer to the caller.
	 * If the buffer is not yet read in, then we read it in, increment
	 * the lock recursion count, and return it to the caller.
	 */
	if ((bp = findchunk_match(vp, bmap, BUF_FSPRIV2, tp)) != NULL) {
		if (!bp->b_flags & B_DONE) {
#ifndef SIM
			SYSINFO.lread += BTOBB(bmap->pbsize);
#endif

			ASSERT(!(bp->b_flags & B_ASYNC));
			bp->b_flags |= B_READ;
			VOP_STRATEGY(vp, bp);

#ifndef SIM
			u.u_ior++;
			SYSINFO.bread += BTOBBT(bp->b_bcount);
#endif

			iowait(bp);
		}

		bip = (xfs_buf_log_item_t*)bp->b_fsprivate;
		bip->bli_recur++;

		return (bp);
	}

	bp = xfs_chunkread(vp, bmap, 1, cred);

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
	bip->bli_recur = 0;

	/*
	 * Get a log_item_desc to point at the new item.
	 */
	(void) xfs_trans_add_item(tp, (xfs_log_item_t*)bip);

	/*
	 * Initialize b_fsprivate2 so we can find it with incore_match()
	 * above.
	 */
	bp->b_fsprivate2 = tp;

	return (bp);
}
#endif /* SIM */


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
xfs_trans_brelse(xfs_trans_t *tp, buf_t *bp)
{
	xfs_buf_log_item_t	*bip;
	xfs_log_item_desc_t	*lidp;

	/*
	 * Default to a normal brelse() call if the tp is NULL.
	 */
	if (tp == NULL) {
		brelse(bp);
		return;
	}

	ASSERT(bp->b_fsprivate2 == tp);
	bip = (xfs_buf_log_item_t*)bp->b_fsprivate;	
	ASSERT(bip->bli_item.li_type == XFS_LI_BUF);
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
		return;
	}

	/*
	 * If the buffer is dirty within this transaction, we can't
	 * release it until we commit.
	 */
	if (lidp->lid_flags & XFS_LID_DIRTY) {
		return;
	}

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
	 * If the buf item is not tracking data in the log, then
	 * we must free it before releasing the buffer back to the
	 * free pool.  Before releasing the buffer to the free pool,
	 * clear the transaction pointer in b_fsprivate2 to disolve
	 * its relation to this transaction.
	 */
	if (!xfs_buf_item_dirty(bip)) {
		xfs_buf_item_relse(bp);
	}
	bp->b_fsprivate2 = NULL;
	xfs_brelse(bp);
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
xfs_trans_bjoin(xfs_trans_t *tp, buf_t *bp)
{
	ASSERT(bp->b_flags & B_BUSY);
	ASSERT(bp->b_fsprivate2 == NULL);

	/*
	 * The xfs_buf_log_item pointer is stored in b_fsprivate.  If
	 * it doesn't have one yet, then allocate one and initialize it.
	 * The checks to see if one is there are in xfs_buf_item_init().
	 */
	xfs_buf_item_init(bp, tp->t_mountp);

	/*
	 * Get a log_item_desc to point at the new item.
	 */
	(void) xfs_trans_add_item(tp, bp->b_fsprivate);

	/*
	 * Initialize b_fsprivate2 so we can find it with incore_match()
	 * in xfs_trans_getblk() and friends above.
	 */
	bp->b_fsprivate2 = tp;
}

/*
 * Mark the buffer as not needing to be unlocked when the buf item's
 * IOP_UNLOCK() routine is called.  The buffer must already be locked
 * and associated with the given transaction.
 */
void
xfs_trans_bhold(xfs_trans_t *tp, buf_t *bp)
{
	xfs_buf_log_item_t	*bip;

	ASSERT(bp->b_flags & B_BUSY);
	ASSERT((xfs_trans_t*)(bp->b_fsprivate2) == tp);
	ASSERT(bp->b_fsprivate != NULL);

	bip = (xfs_buf_log_item_t*)(bp->b_fsprivate);
	bip->bli_flags |= XFS_BLI_HOLD;
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
xfs_trans_log_buf(xfs_trans_t *tp, buf_t *bp, uint first, uint last)
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
	if (bp->b_iodone == NULL) {
		bp->b_iodone = xfs_buf_iodone_callbacks;
	}
	bip->bli_item.li_cb = (void(*)(buf_t*,xfs_log_item_t*))xfs_buf_iodone;

	lidp = xfs_trans_find_item(tp, (xfs_log_item_t*)bip);
	ASSERT(lidp != NULL);

	tp->t_flags |= XFS_TRANS_DIRTY;
	lidp->lid_flags |= XFS_LID_DIRTY;
	xfs_buf_item_log(bip, first, last);
}

