

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
#include <stddef.h>
#ifndef SIM
#include <sys/sysinfo.h>
#include <sys/kmem.h>
#include <sys/conf.h>
#include <sys/user.h>
#include <sys/systm.h>
#endif
#include "xfs_types.h"
#include "xfs_inum.h"
#include "xfs.h"
#include "xfs_trans.h"
#include "xfs_bio.h"
#include "xfs_sb.h"
#include "xfs_ag.h"
#include "xfs_mount.h"
#include "xfs_log.h"
#include "xfs_trans_priv.h"

#ifdef SIM
#include "sim.h"
#endif

#define	XFS_TRANS_LOG_CHUNK	8192

#ifndef SIM
#define ROUNDUP32(x)		(((x) + 31) & ~31)
#endif

STATIC void	xfs_trans_apply_sb_deltas(xfs_trans_t *);
STATIC void	xfs_trans_do_commit(xfs_trans_t *, uint);
STATIC void	xfs_trans_large_item(xfs_trans_t *, xfs_log_item_desc_t *);
STATIC xfs_log_item_desc_t *xfs_trans_log_items(xfs_trans_t *,
						xfs_log_item_desc_t *, uint);
STATIC void	xfs_trans_write_header(xfs_trans_t *, caddr_t);
STATIC void	xfs_trans_write_commit(xfs_trans_t *, caddr_t);
STATIC void	xfs_trans_committed(xfs_trans_t *);
STATIC void	xfs_trans_chunk_committed(xfs_log_item_chunk_t *, xfs_lsn_t);
STATIC void	xfs_trans_free(xfs_trans_t *);

struct zone	*xfs_trans_zone;

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
/* ARGSUSED */
{
#ifndef SIM
	ASSERT(0);
	/* NOTREACHED */
#else
	return (0);
#endif
}

/*
 * This routine is called to allocate a transaction structure.
 * The type parameter indicates the type of the transaction.  These
 * are enumerated in xfs_trans.h.
 *
 * Dynamically allocate the transaction structure from the transaction
 * zone, initialize it, and return it to the caller.
 */
xfs_trans_t *
xfs_trans_alloc(struct xfs_mount *mp, uint type)
{
	xfs_trans_t	*tp;

#ifndef SIM
	tp = (xfs_trans_t*)kmem_zone_zalloc(xfs_trans_zone, KM_SLEEP);
#else
	tp = (xfs_trans_t*)kmem_zalloc(sizeof(xfs_trans_t), 0);
#endif

	/*
	 * Initialize the transaction structure.
	 */
	tp->t_tid = xfs_trans_id_alloc(mp);
	tp->t_type = type;
	tp->t_mountp = mp;
	initnsema(&(tp->t_sema), 0, "xfs_trans");
	tp->t_items_free = XFS_LIC_NUM_SLOTS;
	XFS_LIC_INIT(&(tp->t_items));

	return (tp);
}

/*
 * This is called to reserve free disk blocks and log space for the
 * given transaction.  This must be done before allocating any resources
 * within the transaction.
 *
 * This will return ENOSPC if there are not enough blocks available.
 * It will sleep waiting for available log space.
 * The only valid value for the flags parameter is XFS_RES_LOG_PERM, which
 * is used by long running transactions.
 */
int
xfs_trans_reserve(xfs_trans_t *tp, uint blocks, uint logspace, uint flags)
{
	int		status;

	/*
	 * Attempt to reserve the needed disk blocks by decrementing
	 * the number needed from the number available.  This will
	 * fail if the count would go below zero.
	 */
	if (blocks > 0) {
		status = xfs_mod_incore_sb(tp->t_mountp, XFS_SB_FDBLOCKS,
					   -blocks);
		if (status != 0) {
#if 0
			printf("trans_reserve: reserve %u failed\n", blocks);
#endif
			return (status);
		}
		tp->t_blk_res = blocks;
#if 0
		printf("trans_reserve: reserved %u\n", blocks);
#endif
	}

	/*
	 * Reserve the log space needed for this transaction.
	 */
	if (logspace > 0) {
		(void) xfs_log_reserve(tp->t_mountp, logspace, flags);
		tp->t_log_res = logspace;
	}

	return (0);
}

#if 0
/*
 * xfs_trans_use_reserve() is called to indicate to the transaction
 * mechanism that that some of the blocks reserved through a call
 * to xfs_trans_reserve() have been used.  This should be called
 * each time blocks are allocated.
 */
void
xfs_trans_use_reserve(xfs_trans_t *tp, uint blocks)
{
	/*
	 * Make sure that the number of blocks used is not greater
	 * than the number reserved, and then simply decrement
	 * the number used from the number reserved.
	 */
	ASSERT(tp->t_blk_res >= blocks);
	tp->t_blk_res -= blocks;
	printf("trans_use: used %u\n", blocks);
}
#endif
	

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
 * Record the indicated change to the given field for application
 * to the file system's superblock when the transaction commits.
 * For now, just store the change in the transaction structure.
 *
 * Mark the transaction structure to indicate that the superblock
 * needs to be updated before committing.
 */
void
xfs_trans_mod_sb(xfs_trans_t *tp, uint field, int delta)
{
	switch (field) {
	case XFS_SB_ICOUNT:
		ASSERT(delta > 0);
		tp->t_icount_delta += delta;
		break;
	case XFS_SB_IFREE:
		tp->t_ifree_delta += delta;
		break;
	case XFS_SB_FDBLOCKS:
#if 0
		printf("trans_mod_sb: delta %d\n", delta);
#endif
		/*
		 * Track the number of blocks allocated in the
		 * transaction.  Make sure it does not exceed the
		 * number reserved.
		 */
		if (delta < 0) {
			tp->t_blk_res_used += (uint)-delta;
			ASSERT(tp->t_blk_res_used <= tp->t_blk_res);
		}
		tp->t_fdblocks_delta += delta;
		break;
	case XFS_SB_FREXTENTS:
		tp->t_frextents_delta += delta;
		break;
	default:
		ASSERT(0);
		return;
	}

	tp->t_flags |= (XFS_TRANS_SB_DIRTY | XFS_TRANS_DIRTY);
}

/*
 * xfs_trans_apply_sb_deltas() is called from the commit code
 * to bring the superblock buffer into the current transaction
 * and modify it as requested by earlier calls to xfs_trans_mod_sb().
 *
 * For now we just look at each field allowed to change and change
 * it if necessary.
 */
STATIC void
xfs_trans_apply_sb_deltas(xfs_trans_t *tp)
{
	xfs_sb_t	*sbp;
	buf_t		*bp;

	bp = xfs_trans_getsb(tp);
	sbp = xfs_buf_to_sbp(bp);

	if (tp->t_icount_delta != 0) {
		sbp->sb_icount += tp->t_icount_delta;
	}
	if (tp->t_ifree_delta != 0) {
		sbp->sb_ifree += tp->t_ifree_delta;
	}
	if (tp->t_fdblocks_delta != 0) {
		sbp->sb_fdblocks += tp->t_fdblocks_delta;
	}
	if (tp->t_frextents_delta != 0) {
		sbp->sb_frextents += tp->t_frextents_delta;
	}

	/*
	 * Since all the modifiable fields are contiguous, we
	 * can get away with this.
	 */
	xfs_trans_log_buf(tp, bp, offsetof(xfs_sb_t, sb_icount),
			  (offsetof(xfs_sb_t, sb_frextents) + 3));
}

/*
 * xfs_trans_unreserve_and_mod_sb() is called to release unused
 * reservations and apply superblock counter changes to the in-core
 * superblock.
 *
 * This is done efficiently with a single call to xfs_mod_incore_sb_batch().
 */
void
xfs_trans_unreserve_and_mod_sb(xfs_trans_t *tp)
{
	xfs_mod_sb_t	msb[5];		/* If you add cases, add entries */
	xfs_mod_sb_t	*msbp;
	int		n;
	int		error;

	msbp = &msb[0];
	n = 0;

	/*
	 * Release any reserved blocks.  Any that were allocated
	 * will be taken back again by fdblocks_delta below.
	 */
	if (tp->t_blk_res > 0) {
		msbp->msb_field = XFS_SB_FDBLOCKS;
		msbp->msb_delta = tp->t_blk_res;
		msbp++;
		n++;
	}

	/*
	 * Apply any superblock modifications to the in-core version.
	 */
	if (tp->t_flags & XFS_TRANS_SB_DIRTY) {
		if (tp->t_icount_delta != 0) {
			msbp->msb_field = XFS_SB_ICOUNT;
			msbp->msb_delta = tp->t_icount_delta;
			msbp++;
			n++;
		}
		if (tp->t_ifree_delta != 0) {
			msbp->msb_field = XFS_SB_IFREE;
			msbp->msb_delta = tp->t_ifree_delta;
			msbp++;
			n++;
		}
		if (tp->t_fdblocks_delta != 0) {
			msbp->msb_field = XFS_SB_FDBLOCKS;
			msbp->msb_delta = tp->t_fdblocks_delta;
			msbp++;
			n++;
		}
		if (tp->t_frextents_delta != 0) {
			msbp->msb_field = XFS_SB_FREXTENTS;
			msbp->msb_delta = tp->t_frextents_delta;
			msbp++;
			n++;
		}
	}

	/*
	 * If we need to change anything, do it.
	 */
	if (n > 0) {
		error = xfs_mod_incore_sb_batch(tp->t_mountp, msb, n);
		ASSERT(error == 0);
	}
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

	async = (int)flags & XFS_TRANS_NOSLEEP;

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
/* ARGSUSED */
{
	/*
	 * If there is nothing to be logged by the transaction,
	 * then unlock all of the items associated with the
	 * transaction and free the transaction structure.
	 * Also make sure to return any reserved blocks to
	 * the free pool.
	 */
	if (!(tp->t_flags & XFS_TRANS_DIRTY)) {
		xfs_trans_unreserve_and_mod_sb(tp);
		xfs_trans_free_items(tp);
		xfs_trans_free(tp);
		return;
	}

	/*
	 * If we need to update the superblock, then do it now.
	 */
	if (tp->t_flags & XFS_TRANS_SB_DIRTY) {
		xfs_trans_apply_sb_deltas(tp);
	}

	/*
	 * Instead of writing items into the log, just release
	 * them delayed write. They'll be written out eventually. 
	 */
	xfs_trans_unlock_items(tp);

	/*
	 * Once the transaction has been committed, unused
	 * reservations need to be released and changes to
	 * the superblock need to be reflected in the in-core
	 * version.  Do that now.
	 */
#if 0
	printf("trans_commit: blkres %u delta %d fdblocks %lld\n",
	       tp->t_blk_res, tp->t_fdblocks_delta,
	       tp->t_mountp->m_sb.sb_fdblocks);
#endif
	xfs_trans_unreserve_and_mod_sb(tp);
#if 0
	printf("trans committed: fdblocks == %lld\n",
	       tp->t_mountp->m_sb.sb_fdblocks);
#endif

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
/* ARGSUSED */
{

}

STATIC void
xfs_trans_write_commit(xfs_trans_t *tp, caddr_t buffer)
/* ARGSUSED */
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

	xfs_trans_unreserve_and_mod_sb(tp);
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
	xfs_log_unreserve(tp->t_mountp, tp->t_log_res);

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
	xfs_log_item_chunk_t	*licp;
	xfs_log_item_chunk_t	*next_licp;

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














