
#include <sys/param.h>
#define _KERNEL
#include <sys/buf.h>
#include <sys/sysmacros.h>
#undef _KERNEL
#include <sys/vnode.h>
#include <sys/debug.h>
#include "xfs_types.h"
#include "xfs_inum.h"
#include "xfs.h"
#include "xfs_trans.h"
#include "xfs_buf_item.h"
#include "xfs_trans_priv.h"
#include "xfs_bio.h"
#include "xfs_log.h"
#include "xfs_sb.h"
#include "xfs_ag.h"
#include "xfs_mount.h"
#include "xfs_btree.h"
#include "xfs_bmap.h"
#include "xfs_dinode.h"
#include "xfs_inode.h"

#ifdef SIM
#include "sim.h"
#endif

#define	XFS_TRANS_LOG_CHUNK	8192

STATIC void	xfs_trans_do_commit(xfs_trans_t *, uint);
STATIC void	xfs_trans_large_item(xfs_trans_t *, xfs_log_item_desc_t *);
STATIC xfs_log_item_desc_t *xfs_trans_log_items(xfs_trans_t *,
						xfs_log_item_desc_t *, uint);
STATIC void	xfs_trans_write_header(xfs_trans_t *, caddr_t);
STATIC void	xfs_trans_write_commit(xfs_trans_t *, caddr_t);
STATIC void	xfs_trans_committed(xfs_trans_t *);
STATIC void	xfs_trans_chunk_committed(xfs_log_item_chunk_t *, xfs_lsn_t);
STATIC void	xfs_trans_free(xfs_trans_t *);

xfs_tid_t	
xfs_trans_id_alloc(struct xfs_mount *mp)
{
#ifndef SIM
	ASSERT(0);
#else
	return (mp->m_tid++);
#endif
}



int
xfs_trans_lsn_danger(struct xfs_mount *mp, xfs_lsn_t lsn)
{
#ifndef SIM
	abort();
#else
	return (0);
#endif
}

/*
 * This routine is called to allocate a transaction structure and
 * reserve log space for the commit of the transaction. If the flags
 * value is XFS_TRANS_NOSLEEP, then the caller does not want to sleep
 * waiting for a log space reservation.
 *
 * Call the log manager to reserve the log space for the transaction.
 * Dynamically allocate the transaction structure from the transaction
 * zone, initialize it, and return it to the caller.
 */
xfs_trans_t *
xfs_trans_alloc(struct xfs_mount *mp, uint type, uint reserve, uint flags)
{
	xfs_trans_t	*tp;
	/*
	 * This call can only return NULL if the caller specified
	 * the TRANS_NOSLEEP flag.
	 */
	if (xfs_log_reserve(mp, reserve, flags & XFS_TRANS_NOSLEEP) == 0) {
		return (NULL);
	}

#ifndef SIM
	tp = (xfs_trans_t*)kmem_zone_zalloc(xfs_trans_zone, KM_SLEEP);
#else
	tp = (xfs_trans_t*)kmem_zalloc(sizeof(xfs_trans_t), 0);
#endif

	/*
	 * Initialize the transaction structure.
	 */
	tp->t_tid = xfs_trans_id_alloc(mp);
	tp->t_reserve = reserve;
	tp->t_type = type;
	tp->t_mountp = mp;
	tp->t_callback = NULL;
	tp->t_callarg = NULL;
	tp->t_forw = NULL;
	tp->t_back = NULL;
	tp->t_flags = 0;
	initnsema(&(tp->t_sema), 0, "xfs_trans");
	tp->t_items_free = XFS_LIC_NUM_SLOTS;
	XFS_LIC_INIT(&(tp->t_items));

	return (tp);
}

/*
 * This is called to set the a callback to be called when the given
 * transaction is committed to disk.  The transaction pointer and the
 * argument pointer will be passed to the callback routine.
 *
 * Only one callback can be associated with any single transaction.
 */
void
xfs_trans_callback(xfs_trans_t *tp, xfs_trans_callback_t callback, void *arg)
{
	ASSERT(tp->t_callback == NULL);
	tp->t_callback = callback;
	tp->t_callarg = arg;
}

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
 */
buf_t *
xfs_trans_getblk(xfs_trans_t *tp, dev_t dev, daddr_t blkno, int len)
{
	buf_t			*bp;
	xfs_buf_log_item_t	*bip;

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
	 */
	if (bp->b_fsprivate == NULL) {
		xfs_buf_item_init(bp, tp->t_mountp);
	}

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
 */
buf_t *
xfs_trans_bread(xfs_trans_t *tp, dev_t dev, daddr_t blkno, int len)
{
	buf_t			*bp;
	xfs_buf_log_item_t	*bip;

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
		if (!bp->b_flags & B_DONE) {
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
	 */
	if (bp->b_fsprivate == NULL) {
		xfs_buf_item_init(bp, tp->t_mountp);
	}

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
 */
#ifndef SIM
buf_t *
xfs_trans_getchunk(xfs_trans_t *tp, vnode_t *vp,
		   struct bmapval *bmap, struct cred *cred)
{
	buf_t			*bp;
	xfs_buf_log_item_t	*bip;

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
	 */
	if (bp->b_fsprivate == NULL) {
		xfs_buf_item_init(bp, tp->t_mountp);
	}

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
 */
#ifndef SIM
buf_t *
xfs_trans_chunkread(xfs_trans_t *tp, vnode_t *vp,
		    struct bmapval *bmap, struct cred *cred)
{
	buf_t			*bp;
	xfs_buf_log_item_t	*bip;

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
			SYSINFO.lread += len;
#endif

			ASSERT(!(bp->b_flags & B_ASYNC));
			bp->b_flags |= B_READ;
			VOP_STRATEGY(vp, bp);

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

	bp = xfs_chunkread(vp, bmap, 1, cred);

	/*
	 * The xfs_buf_log_item pointer is stored in b_fsprivate.  If
	 * it doesn't have one yet, then allocate one and initialize it.
	 */
	if (bp->b_fsprivate == NULL) {
		xfs_buf_item_init(bp, tp->t_mountp);
	}

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
 * xfs_trans_... buffer allocation routines. This will decrement the lock
 * recursion count of the buffer item.  If the count goes to less than 0,
 * the buffer will be unlocked and disassociated from the transaction. 
 *
 * The buffer must not have been modified within this transaction,
 * because we have no way to put it back to its previous state.
 *
 * If the buffer was not modified before this transaction began,
 * then free the buf_log_item associated with it.
 */
void
xfs_trans_brelse(xfs_trans_t *tp, buf_t *bp)
{
	xfs_buf_log_item_t	*bip;
	xfs_log_item_desc_t	*lidp;

	ASSERT(bp->b_fsprivate2 == tp);
	bip = (xfs_buf_log_item_t*)bp->b_fsprivate;	
	/*
	 * Find the item descriptor pointing to this buffer's
	 * log item.  It must be there, and it must not be
	 * dirty.
	 */
	lidp = xfs_trans_find_item(tp, (xfs_log_item_t*)bip);
	ASSERT(lidp != NULL);
	ASSERT(!(lidp->lid_flags & XFS_LID_DIRTY));

	/*
	 * If the release is just for a recursive lock,
	 * then decrement the count and return.
	 */
	if (bip->bli_recur > 0) {
		bip->bli_recur--;
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
	 */
	if (bp->b_fsprivate == NULL) {
		xfs_buf_item_init(bp, tp->t_mountp);
	}

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
 * Get and lock the inode for the caller if it is not already
 * locked within the given transaction.  If it is already locked
 * within the transaction, just increment its lock recursion count
 * and return a pointer to it.
 *
 * Use the inode cache routine xfs_inode_incore() to find the inode
 * if it is already owned by this transaction.
 *
 * If we don't already own the buffer, use xfs_iget() to get it.
 * Since the inode log item structure is embedded in the incore
 * inode structure and is initialized when the inode is brought
 * into memory, there is nothing to do with it here.
 */
#ifndef SIM
void
xfs_trans_iget(xfs_trans_t *tp, xfs_ino_t ino, xfs_inode_t **ipp)
{
	xfs_inode_log_item_t	*iip;
	xfs_inode_t		*ip;

	/*
	 * If we find the inode in core with this transaction
	 * pointer in its i_transp field, then we know we already
	 * have it locked.  In this case we just increment the lock
	 * recursion count and return the inode to the caller.
	 */
	if ((ip = xfs_inode_incore(tp->t_mountp, ino, tp)) != NULL) {
		ip->i_item.ili_recur++;
		*ipp = ip;
		return;
	}

	ip = xfs_iget(tp->t_mountp, ino);

	/*
	 * Get a log_item_desc to point at the new item.
	 */
	(void) xfs_trans_add_item(tp, (xfs_log_item_t*)&(ip->i_item));

	/*
	 * Initialize i_transp so we can find it with xfs_inode_incore()
	 * above.
	 */
	ip->i_transp = tp;

	*ipp = ip;
	return;
}
#endif /* SIM */


/*
 * Release the inode ip which was previously acquired with xfs_trans_iget()
 * or added with xfs_trans_ijoin(). This will decrement the lock
 * recursion count of the inode item.  If the count goes to less than 0,
 * the inode will be unlocked and disassociated from the transaction. 
 *
 * The inode must not have been modified within this transaction,
 * because we have no way to put it back to its previous state.
 */
#ifndef SIM
void
xfs_trans_irelse(xfs_trans_t *tp, xfs_inode_t *ip)
{
	xfs_inode_log_item_t	*iip;
	xfs_log_item_desc_t	*lidp;

	ASSERT(ip->i_transp == tp);
	iip = &ip->i_item;	

	/*
	 * Find the item descriptor pointing to this inode's
	 * log item.  It must be there, and it must not be
	 * dirty.
	 */
	lidp = xfs_trans_find_item(tp, (xfs_log_item_t*)iip);
	ASSERT(lidp != NULL);
	ASSERT(!(lidp->lid_flags & XFS_LID_DIRTY));

	/*
	 * If the release is just for a recursive lock,
	 * then decrement the count and return.
	 */
	if (iip->ili_recur > 0) {
		iip->ili_recur--;
		return;
	}

	xfs_trans_free_item(tp, lidp);

	/*
	 * Clear the hold flag in the inode log item.
	 * We wouldn't want the next user of the inode to
	 * get confused.
	 */
	if (iip->ili_flags & XFS_ILI_HOLD) {
		iip->ili_flags &= ~XFS_ILI_HOLD;
	}

	/*
	 * Unlike xfs_brelse() the inode log item cannot be
	 * freed, because it is embedded within the inode.
	 * All we have to do is release the inode.
	 */
	xfs_irelse(ip);
	return;
}
#endif /* SIM */


/*
 * Add the locked inode to the transaction.
 * The inode must be locked, and it cannot be associated with any
 * transaction.
 */
#ifndef SIM
void
xfs_trans_ijoin(xfs_trans_t *tp, xfs_inode_t *ip)
{
	xfs_inode_log_item_t	*iip;

	ASSERT(ip->i_flags & I_LOCKED);
	ASSERT(ip->i_transp == NULL);

	/*
	 * Get a log_item_desc to point at the new item.
	 */
	(void) xfs_trans_add_item(tp, (xfs_log_item_t*)&(ip->i_item));

	/*
	 * Initialize i_transp so we can find it with xfs_inode_incore()
	 * in xfs_trans_iget() above.
	 */
	ip->i_transp = tp;
}
#endif /* SIM */



/*
 * Mark the inode as not needing to be unlocked when the inode item's
 * IOP_UNLOCK() routine is called.  The inode must already be locked
 * and associated with the given transaction.
 */
#ifndef SIM
void
xfs_trans_ihold(xfs_trans_t *tp, xfs_inode_t *ip)
{
	ASSERT(ip->i_flags & I_LOCKED);
	ASSERT(ip->i_transp == tp);

	ip->i_item.ili_flags |= XFS_ILI_HOLD;
}
#endif /* SIM */

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
	ASSERT((first >= 0) && (first <= last) && (last <= bp->b_bcount));
	ASSERT((bp->b_iodone == NULL) || (bp->b_iodone == xfs_buf_iodone));

	/*
	 * Mark the buffer as needing to be written out eventually,
	 * and set its iodone function to remove the buffer's buf log
	 * item from the AIL and free it when the buffer is flushed
	 * to disk.
	 */
	bp->b_iodone = xfs_buf_iodone;
	bp->b_flags |= B_DELWRI;

	bip = (xfs_buf_log_item_t*)bp->b_fsprivate;
	lidp = xfs_trans_find_item(tp, (xfs_log_item_t*)bip);
	ASSERT(lidp != NULL);

	tp->t_flags |= XFS_TRANS_DIRTY;
	lidp->lid_flags |= XFS_LID_DIRTY;
	xfs_buf_item_log(bip, first, last);
}


/*
 * This is called to mark the fields indicated in fieldmask as needing
 * to be logged when the transaction is committed.  The inode must
 * already be associated with the given transaction.  The values for
 * fieldmask are defined in xfs_inode_item.h.
 */
#ifndef SIM
void
xfs_trans_log_inode(xfs_trans_t *tp, xfs_inode_t *ip, uint fieldmask)
{
	xfs_inode_log_item_t	*iip;
	xfs_log_item_desc_t	*lidp;

	ASSERT(ip->i_flags & I_LOCKED);
	ASSERT(ip->i_transp == tp);

	lidp = xfs_trans_find_item(tp, (xfs_log_item_t*)&(ip->i_item));
	ASSERT(lidp != NULL);

	tp->t_flags |= XFS_TRANS_DIRTY;
	lidp->lid_flags |= XFS_LID_DIRTY;
	ip->i_item.ili_field_mask |= fieldmask;
}
#endif /* SIM */


/*
 * Add the xfs_log_item structure to the list of items associated
 * with the transaction.  It should be logged when the transaction
 * is committed.
 *
 * Mark the log item descriptor for the operation as DIRTY so it
 * will be logged during commit.
 */
void
xfs_trans_log_op(xfs_trans_t *tp, xfs_log_item_t *op)
{
	xfs_log_item_desc_t	*lidp;

	tp->t_flags |= XFS_TRANS_DIRTY;
	lidp = xfs_trans_add_item(tp, op);
	lidp->lid_flags |= XFS_LID_DIRTY;
}



/*
 * Return the unique transaction id of the given transaction.
 */
xfs_trans_id_t
xfs_trans_id(xfs_trans_t *tp)
{
	return (tp->t_tid);
}


/*
 * This routine is called to commit a transaction to the incore log.
 * xfs_trans_do_commit() does the real work.  If the flags include
 * XFS_TRANS_NOSLEEP, then the commit will take place asynchronously.
 * If this flag is not set, then any async transactions which are
 * pending will be committed by our lucky caller and then the given
 * transaction will be committed.
 *
 * If the flags include XFS_TRANS_SYNC, then the log will be flushed
 * right away.  If the flags include XFS_TRANS_NOSLEEP, then the commit
 * will be asynchronous.  If the flags include XFS_TRANS_WAIT, then
 * the caller will sleep until the transaction is committed.  Obviously,
 * XFS_TRANS_NOSLEEP and XFS_TRANS_WAIT cannot be set simultaneously.
 */
void
xfs_trans_commit(xfs_trans_t *tp, uint flags)
{
	int		async;
	xfs_trans_t	*async_list;
	xfs_trans_t	*atp;

	async = flags & XFS_TRANS_NOSLEEP;

	/*
	 * If the transaction is not asynchronous and there are
	 * no asynch transactions to commit, then just do it.
	 */
	if (!(async) && !(xfs_trans_any_async(tp->t_mountp))) {
		xfs_trans_do_commit(tp, flags);
		return;
	}

	/*
	 * If the transaction is asynchronous, put it on the list.
	 */
	if (async) {
		xfs_trans_add_async(tp);
		return;
	}	

	/*
	 * If we are not asynchronous and there are async transactions
	 * that need to be committed, we get volunteered to commit
	 * them.  The async transactions may be gone by the time we
	 * check again, but in that case we just won't do anything.
	 */
	xfs_trans_commit_async(tp->t_mountp);

	/*
	 * Now that we've processed any async transactions, do our
	 * own.
	 */
	xfs_trans_do_commit(tp, flags);
} 


/*
 * This is called to commit all of the transactions which are
 * currently hung on the list in the given mount structure.
 * Each transaction should be committed in turn.  This is called
 * by both xfs_trans_commit() and the utility thread which manages
 * the tail of the log.  That thread is also responsible for making
 * sure that async transactions get committed in a timely manner.
 */
void
xfs_trans_commit_async(struct xfs_mount *mp)
{
	xfs_trans_t	*async_list;
	xfs_trans_t	*atp;

	async_list = xfs_trans_get_async(mp);
	atp = async_list;
	while (atp != NULL) {
		async_list = atp->t_forw;
		atp->t_forw = NULL;
		xfs_trans_do_commit(atp, 0);
		atp = async_list;
	}
}

/*
 * This is called to commit a transaction to the in core log.
 * All resources which are logged are pinned, and all resources
 * are unlocked.  
 *
 * If there is nothing to log, then all log items will be unlocked
 * and the transaction will freed.
 */
#ifndef SIM
STATIC void
xfs_trans_do_commit(xfs_trans_t *tp, uint flags)
{
	char			*trans_headerp;
	char			*trans_commitp;
	char			*log_ptr;
	xfs_log_item_desc_t	*start_desc;
	xfs_log_item_desc_t	*desc;
	uint			space;
	xfs_lsn_t		commit_lsn;

	/*
	 * If there is nothing to be logged by the transaction,
	 * then unlock all of the items associated with the
	 * transaction and free the transaction structure.
	 */
	if (!(tp->t_flags & XFS_TRANS_DIRTY)) {
		xfs_trans_free_items(tp);
		xfs_trans_free(tp);
		return;
	}

	/*
	 * Write a transaction header into the in core log.
	 */
	trans_headerp = xfs_log_alloc(sizeof(xfs_trans_header_t), 0,
				      &tp->t_lsn);
	xfs_trans_write_header(tp, trans_headerp);
	xfs_log_free(trans_headerp, sizeof(xfs_trans_header_t));

	/*
	 * Write the log items into the in core log in chunks
	 * of size XFS_TRANS_LOG_CHUNK or smaller.
	 */
	start_desc = xfs_trans_first_item(tp);
	ASSERT(start_desc != NULL);
	while (start_desc != NULL) {
		/*
		 * Find the first group of items that will fit in a chunk
		 * of XFS_TRANS_LOG_CHUNK bytes in the in core log.  Keep
		 * all sizes rounded to 32 byte boundaries.  When the total
		 * size would be bigger than XFS_TRANS_LOG_CHUNK, then break
		 * out of the loop.  This includes the case where a single
		 * item is too big.
		 */
		space = 0;
		desc = start_desc;
		while (desc != NULL) {
			/*
			 * Skip items which aren't dirty in this transaction.
			 */
			if (!(desc->lid_flags & XFS_LID_DIRTY)) {
				desc = xfs_trans_next_item(tp, desc);
				continue;
			}
			/*
			 * Only ask the item for its size the first time here.
			 * If the item is large or just missed fitting in to
			 * the last group we don't want to ask it again.  If
			 * it was large then we're tracking how much is left
			 * in lid_size and don't want to overwrite it.
			 */
			if (desc->lid_size == 0) {
				desc->lid_size = IOP_SIZE(desc->lid_item);
				desc->lid_size = ROUNDUP32(desc->lid_size);
			}
			if ((space + desc->lid_size) > XFS_TRANS_LOG_CHUNK) {
				break;
			}
			space += desc->lid_size;
			desc = xfs_trans_next_item(tp, desc);
		}

		/*
		 * Desc will only be NULL if we reached the last item.
		 * If we did this and found nothing new to log, we're
		 * done.
		 */
		if ((desc == NULL) && (space == 0)) {
			break;
		}

		/*
		 * If we have a single item which is too large, then size will
		 * never have been incremented above 0 in the loop above.
		 * We deal with the big item here.  We know there is only one,
		 * because it would never fit under XFS_TRANS_LOG_CHUNK with
		 * another item since it can't by itself. Call 
		 * xfs_trans_large_item() to process the large
		 * item in a loop doing multiple partial writes of the item.
		 */
		if (space == 0) {
			xfs_trans_large_item(tp, desc);
			start_desc = xfs_trans_next_item(tp, desc);
			continue;
		}

		/*
		 * Here we have a group of items to write into the log.
		 * Call xfs_trans_log_items() to log the items starting
		 * with start_desc which will fit into log space space.
		 * This will return the log item which follows the last
		 * one written, which will be used to start this loop
		 * over again for the next group of items.
		 */
		start_desc = xfs_trans_log_items(tp, start_desc, space);
	}

	/*
	 * Now write the commit record for the transaction into
	 * the log.
	 */
	trans_commitp = xfs_log_alloc(sizeof(xfs_trans_commit_t), 0,
				      &commit_lsn);
	xfs_trans_write_commit(tp, trans_commitp);
	xfs_log_free(trans_commitp, sizeof(xfs_trans_commit_t));

	/*
 	 * Tell the LM to call the transaction completion routine
	 * when the log write with LSN commit_lsn completes.
	 * After this call we cannot reference tp, because the call
	 * can happen at any time and tp can be freed.
	 */
	xfs_log_notify((void(*)(void*))xfs_trans_committed, tp, commit_lsn);

	/*
	 * Once all the items of the transaction have been copied
	 * to the in core log we can release them.  Do that here.
	 * This will free descriptors pointing to items which were
	 * not logged since there is nothing more to do with them.
	 * For items which were logged, we will keep pointers to them
	 * so they can be unpinned after the transaction commits.
	 */
	xfs_trans_unlock_items(tp);

	/*
	 * If the caller wants the log written immediately,
	 * then ask the LM to do it.
	 */
	if (flags & XFS_TRANS_SYNC) {
		xfs_log_sync(commit_lsn);
	}

	/*
	 * If the caller wants to wait for the transaction to be
	 * committed to disk, then ask the LM to put us to sleep
	 * and wake us up when the transaction is committed to disk.
	 */
	if (flags & XFS_TRANS_WAIT) {
		xfs_log_wait(commit_lsn);
	}
}	
#else
STATIC void
xfs_trans_do_commit(xfs_trans_t *tp, uint flags)
{
	char			*trans_headerp;
	char			*trans_commitp;
	char			*log_ptr;
	xfs_log_item_desc_t	*start_desc;
	xfs_log_item_desc_t	*desc;
	uint			space;
	xfs_lsn_t		commit_lsn;

	/*
	 * If there is nothing to be logged by the transaction,
	 * then unlock all of the items associated with the
	 * transaction and free the transaction structure.
	 */
	if (!(tp->t_flags & XFS_TRANS_DIRTY)) {
		xfs_trans_free_items(tp);
		xfs_trans_free(tp);
		return;
	}

	/*
	 * Instead of writing items into the log, just release
	 * them delayed write. They'll be written out eventually. 
	 */
	xfs_trans_unlock_items(tp);

	/*
	 * Free the transaction structure now that it has been committed.
	 */
	xfs_trans_free(tp);
}
#endif

/*
 * This is called by xfs_trans_commit() to write a large item into
 * the incore log.  Just do it in a loop, logging up to XFS_TRANS_LOG_CHUNK
 * bytes at a time.
 */
#ifndef SIM
STATIC void
xfs_trans_large_item(xfs_trans_t *tp, xfs_log_item_desc_t *desc)
{
	int	key;
	uint	space;
	char	*log_ptr;
 
	key = -1;
	while (desc->lid_size != 0) {
		space = MIN(XFS_TRANS_LOG_CHUNK,
			    desc->lid_size);
		log_ptr = xfs_log_alloc(space, 0, NULL);
		desc->lid_size = IOP_FORMAT(desc->lid_item, log_ptr,
					    space, &key);
		desc->lid_size = ROUNDUP32(desc->lid_size);
		xfs_log_free(log_ptr, space);
	}

	/*
	 * Once we've copied the item into the in core log, pin it
	 * so it can't be written out once we unlock it.
	 */
	IOP_PIN(desc->lid_item);
}
#endif /* SIM */

/*
 * This is called from xfs_trans_commit() to write a group of
 * log items to the in core log.
 *
 * Allocate a chunk of space for them and write each one
 * into that space.  They must fit, because they said they
 * would earlier.  Because the sizes and items are correlated,
 * at the end desc will point to the next item to be written
 * into the log (but not yet having space allocated for it).
 *
 * We return the pointer to the log item descriptor that follows
 * the last one we write into the in core log.  This is for use
 * in the loop in xfs_trans_commit().
 */
#ifndef SIM
STATIC xfs_log_item_desc_t *
xfs_trans_log_items(xfs_trans_t		*tp,
		    xfs_log_item_desc_t	*start_desc,
		    uint		space)
{
	char			*log_ptr;
	char			*save_ptr;
	xfs_log_item_desc_t	*desc;
	uint			tmp_space;
	int			key;

	log_ptr = xfs_log_alloc(space, 0, NULL);
	save_ptr = log_ptr;
	desc = start_desc;
	tmp_space = space;
	while (tmp_space != 0) {
		/*
		 * If the item is not dirty then skip it.
		 */
		if (!(desc->lid_flags & XFS_LID_DIRTY)) {
			desc = xfs_trans_next_item(tp, desc);
			continue;
		}	
		key = -1;
		(void) IOP_FORMAT(desc->lid_item, log_ptr,
				  desc->lid_size, &key);
		/*
		 * Once we've copied the item into the in core log, pin it
		 * so it can't be written out once we unlock it.
		 */
		IOP_PIN(desc->lid_item);
		log_ptr += desc->lid_size;
		tmp_space -= desc->lid_size;
		desc = xfs_trans_next_item(tp, desc);
	}
	xfs_log_free(save_ptr, space);
	return (desc);
}
#endif /* SIM */

STATIC void
xfs_trans_write_header(xfs_trans_t *tp, caddr_t buffer)
{

}

STATIC void
xfs_trans_write_commit(xfs_trans_t *tp, caddr_t buffer)
{

}

/*
 * Unlock all of the transaction's items and free the transaction.
 * The transaction must not have modified any of its items, because
 * there is no way to restore them to their previous state.
 */
void
xfs_trans_cancel(xfs_trans_t *tp)
{
	ASSERT(!(tp->t_flags & XFS_TRANS_DIRTY));

	xfs_trans_free_items(tp);
	xfs_trans_free(tp);
}


/*
 * Free the log reservation taken by this transaction and
 * free the transaction structure itself.
 */
STATIC void
xfs_trans_free(xfs_trans_t *tp)
{
#ifndef SIM
	xfs_log_unreserve(tp->t_mountp, tp->t_reserve);

	kmem_zone_free(xfs_trans_zone, tp);
#else
	kmem_free(tp, sizeof(*tp));
#endif
}


/*
 * THIS SHOULD BE REWRITTEN TO USE xfs_trans_next_item().
 *
 * This is called by the LM when a transaction has been fully
 * committed to disk.  It needs to unpin the items which have
 * been logged by the transaction and update their positions
 * in the AIL if necessary.
 *
 * Call xfs_trans_chunk_committed() to process the items in
 * each chunk.
 */
STATIC void
xfs_trans_committed(xfs_trans_t *tp)
{
	xfs_log_item_desc_t	*lidp;
	xfs_log_item_chunk_t	*licp;
	xfs_log_item_chunk_t	*next_licp;
	xfs_log_item_t		*lip;

	/*
	 * Call the transaction's completion callback if there
	 * is one.
	 */
	if (tp->t_callback != NULL) {
		tp->t_callback(tp, tp->t_callarg);
	}

	/*
	 * Special case the chunk embedded in the transaction.
	 */
	licp = &(tp->t_items);
	if (!(XFS_LIC_ARE_ALL_FREE(licp))) {
		xfs_trans_chunk_committed(licp, tp->t_lsn);
	}

	/*
	 * Process the items in each chunk in turn.
	 */
	licp = licp->lic_next;
	while (licp != NULL) {
		ASSERT(!XFS_LIC_ARE_ALL_FREE(licp));
		xfs_trans_chunk_committed(licp, tp->t_lsn);
		next_licp = licp->lic_next;
		kmem_free(licp, sizeof(xfs_log_item_chunk_t));
		licp = next_licp;
	}

	/*
	 * That's it for the transaction structure.  Free it.
	 */
	xfs_trans_free(tp);
}

/*
 * This is called to perform the commit processing for each
 * item described by the given chunk.
 *
 * The commit processing consists of calling the committed routine
 * of each logged item, updating the item's position in the AIL
 * if necessary, and unpinning each item.  If the committed routine
 * returns -1, then do nothing further with the item because it
 * may have been freed.
 *
 * Since items are unlocked when they are copied to the incore
 * log, it is possible for two transactions to be completing
 * and manipulating the same item simultaneously.  The AIL lock
 * will protect the lsn field of each item.  The value of this
 * field can never go backwards.
 *
 * We unpin the items after repositioning them in the AIL, because
 * otherwise they could be immediately flushed and we'd have to race
 * with the flusher trying to pull the item from the AIL as we add it.
 */
STATIC void
xfs_trans_chunk_committed(xfs_log_item_chunk_t *licp, xfs_lsn_t lsn)
{
	xfs_log_item_desc_t	*lidp;
	xfs_log_item_t		*lip;
	xfs_lsn_t		item_lsn;
	struct xfs_mount	*mp;
	int			i;
	int			s;

	lidp = licp->lic_descs;
	for (i = 0; i <= XFS_LIC_MAX_SLOT; i++) {
		if (XFS_LIC_ISFREE(licp, i)) {
			lidp++;
			continue;
		}

		lip = lidp->lid_item;
		item_lsn = IOP_COMMITTED(lip, lsn);

		/*
		 * If the committed routine returns -1, make
		 * no more references to the item.
		 */
		if (XFS_LSN_CMP(item_lsn, (xfs_lsn_t)-1) == 0) {
			continue;
		}

		/*
		 * If the returned lsn is greater than what it
		 * contained before, update the location of the
		 * item in the AIL.  If it is not, then do nothing.
		 * Items can never move backwards in the AIL.
		 *
		 * While the new lsn should usually be greater, it
		 * is possible that a later transaction completing
		 * simultaneously with an earlier one using the
		 * same item could complete first with a higher lsn.
		 * This would cause the earlier transaction to fail
		 * the test below.
		 */ 
		mp = lip->li_mountp;
		s = AIL_LOCK(mp);
		if (XFS_LSN_CMP(item_lsn, lip->li_lsn) > 0) {
			/*
			 * This will set the item's lsn to item_lsn
			 * and update the position of the item in
			 * the AIL.
			 */
			xfs_trans_update_ail(mp, lip, item_lsn);
		}
		AIL_UNLOCK(mp, s);

		/*
		 * Now that we've repositioned the item in the AIL,
		 * unpin it so it can be flushed.
		 */
		IOP_UNPIN(lip);
	}
}














