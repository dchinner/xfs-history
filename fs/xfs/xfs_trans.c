

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
#include <sys/errno.h>
#include <sys/uuid.h>
#include <sys/kmem.h>
#include <stddef.h>
#ifndef SIM
#include <sys/sysinfo.h>
#include <sys/conf.h>
#include <sys/user.h>
#include <sys/systm.h>
#endif
#include "xfs_types.h"
#include "xfs_inum.h"
#include "xfs_log.h"
#include "xfs_trans.h"
#include "xfs_bio.h"
#include "xfs_sb.h"
#include "xfs_ag.h"
#include "xfs_mount.h"
#include "xfs_trans_priv.h"

#ifdef SIM
#include "sim.h"
#include <stdio.h>
#include <stdlib.h>
#endif

STATIC void	xfs_trans_apply_sb_deltas(xfs_trans_t *);
STATIC void	xfs_trans_do_commit(xfs_trans_t *, uint);
STATIC uint	xfs_trans_count_vecs(xfs_trans_t *);
STATIC void	xfs_trans_fill_vecs(xfs_trans_t *, xfs_log_iovec_t *);
STATIC void	xfs_trans_committed(xfs_trans_t *);
STATIC void	xfs_trans_chunk_committed(xfs_log_item_chunk_t *, xfs_lsn_t);
STATIC void	xfs_trans_free(xfs_trans_t *);

zone_t		*xfs_trans_zone;

xfs_tid_t	
xfs_trans_id_alloc(xfs_mount_t *mp)
{
	/*
	 * XXXajs
	 * Do this.
	 */
	return (mp->m_tid++);
}



int
xfs_trans_lsn_danger(xfs_mount_t	*mp,
		     xfs_lsn_t		lsn)
/* ARGSUSED */
{
	/*
	 * XXXajs
	 * Do this.
	 */
	return (0);
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
xfs_trans_alloc(xfs_mount_t	*mp,
		uint		type)
{
	xfs_trans_t	*tp;

	if (!xfs_trans_zone)
		xfs_trans_zone = kmem_zone_init(sizeof(*tp), "xfs_trans");
	tp = kmem_zone_zalloc(xfs_trans_zone, KM_SLEEP);

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
 * This is called to create a new transaction which will share the
 * permanent log reservation of the given transaction.  Other
 * reservations, locks, etc are not inherited.
 */
xfs_trans_t *
xfs_trans_dup(xfs_trans_t *tp)
{
	xfs_trans_t	*ntp;

	ntp = kmem_zone_zalloc(xfs_trans_zone, KM_SLEEP);

	/*
	 * Initialize the new transaction structure.
	 */
	ntp->t_tid = xfs_trans_id_alloc(tp->t_mountp);
	ntp->t_type = tp->t_type;
	ntp->t_mountp = tp->t_mountp;
	initnsema(&(ntp->t_sema), 0, "xfs_trans");
	ntp->t_items_free = XFS_LIC_NUM_SLOTS;
	XFS_LIC_INIT(&(ntp->t_items));

	ASSERT(tp->t_flags & XFS_TRANS_PERM_LOG_RES);
	ASSERT(!log_debug || (tp->t_ticket != NULL));
	ntp->t_flags = XFS_TRANS_PERM_LOG_RES;
	ntp->t_ticket = tp->t_ticket;
	ntp->t_log_res = tp->t_log_res;
	ntp->t_blk_res = tp->t_blk_res - tp->t_blk_res_used;
	ntp->t_rtx_res = tp->t_rtx_res - tp->t_rtx_res_used;

	return ntp;
}

/*
 * This is called to reserve free disk blocks and log space for the
 * given transaction.  This must be done before allocating any resources
 * within the transaction.
 *
 * This will return ENOSPC if there are not enough blocks available.
 * It will sleep waiting for available log space.
 * The only valid value for the flags parameter is XFS_RES_LOG_PERM, which
 * is used by long running transactions.  If any one of the reservations
 * fails then they will all be backed out.
 */
int
xfs_trans_reserve(xfs_trans_t	*tp,
		  uint		blocks,
		  uint		logspace,
		  uint		rtextents,
		  uint		flags)
{
	int	log_flags;
	int	error;

	error = 0;
	/*
	 * Attempt to reserve the needed disk blocks by decrementing
	 * the number needed from the number available.  This will
	 * fail if the count would go below zero.
	 */
	if (blocks > 0) {
		error = xfs_mod_incore_sb(tp->t_mountp, XFS_SB_FDBLOCKS,
					  -blocks);
		if (error != 0) {
			return (ENOSPC);
		}
		tp->t_blk_res = blocks;
	}

	/*
	 * Reserve the log space needed for this transaction.
	 */
	if (logspace > 0) {
		ASSERT(tp->t_ticket == NULL);
		ASSERT(!(tp->t_flags & XFS_TRANS_PERM_LOG_RES));
		if (flags & XFS_TRANS_PERM_LOG_RES) {
			log_flags = XFS_LOG_PERM_RESERV;
			tp->t_flags |= XFS_TRANS_PERM_LOG_RES;
		} else {
			log_flags = 0;
		}
		error = xfs_log_reserve(tp->t_mountp, logspace,
					&tp->t_ticket,
					XFS_TRANSACTION, log_flags);
#ifdef SIM
		if (error != 0) {
			printf("Log reservation failed\n");
			abort();
		}
#endif
		if (error) {
			goto undo_blocks;
		}
		tp->t_log_res = logspace;
	}

	/*
	 * Attempt to reserve the needed realtime extents by decrementing
	 * the number needed from the number available.  This will
	 * fail if the count would go below zero.
	 */
	if (rtextents > 0) {
		error = xfs_mod_incore_sb(tp->t_mountp, XFS_SB_FREXTENTS,
					  -rtextents);
		if (error) {
			error = ENOSPC;
			goto undo_log;
		}
		tp->t_rtx_res = rtextents;
	}

	return 0;

	/*
	 * Error cases jump to one of these labels to undo any
	 * reservations which have already been performed.
	 */
undo_log:
	if (logspace > 0) {
		if (flags & XFS_TRANS_PERM_LOG_RES) {
			log_flags = XFS_LOG_REL_PERM_RESERV;
		} else {
			log_flags = 0;
		}
		xfs_log_done(tp->t_mountp, tp->t_ticket, log_flags);
		tp->t_ticket = NULL;
		tp->t_log_res = 0;
		tp->t_flags &= ~XFS_TRANS_PERM_LOG_RES;
	}

undo_blocks:
	if (blocks > 0) {
		(void) xfs_mod_incore_sb(tp->t_mountp, XFS_SB_FDBLOCKS,
					 blocks);
		tp->t_blk_res = 0;
	}

	return (error);
}


/*
 * This is called to set the a callback to be called when the given
 * transaction is committed to disk.  The transaction pointer and the
 * argument pointer will be passed to the callback routine.
 *
 * Only one callback can be associated with any single transaction.
 */
void
xfs_trans_callback(xfs_trans_t		*tp,
		   xfs_trans_callback_t	callback,
		   void			*arg)
{
	ASSERT(tp->t_callback == NULL);
	tp->t_callback = callback;
	tp->t_callarg = arg;
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
xfs_trans_mod_sb(xfs_trans_t	*tp,
		 uint		field,
		 int		delta)
{
	xfs_sb_t		*sbp;

	switch (field) {
	case XFS_TRANS_SB_ICOUNT:
		ASSERT(delta > 0);
		tp->t_icount_delta += delta;
		break;
	case XFS_TRANS_SB_IFREE:
		tp->t_ifree_delta += delta;
		break;
	case XFS_TRANS_SB_FDBLOCKS:
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
	case XFS_TRANS_SB_RES_FDBLOCKS:
		/*
		 * The allocation has already been applied to the
		 * in-core superblock's counter.  This should only
		 * be applied to the on-disk superblock.
		 */
		ASSERT(delta < 0);
		tp->t_res_fdblocks_delta += delta;
		break;
	case XFS_TRANS_SB_FREXTENTS:
		/*
		 * Track the number of blocks allocated in the
		 * transaction.  Make sure it does not exceed the
		 * number reserved.
		 */
		if (delta < 0) {
			sbp = &tp->t_mountp->m_sb;
			tp->t_rtx_res_used += (uint)-delta;
			ASSERT(tp->t_rtx_res_used <= tp->t_rtx_res);
		}
		tp->t_frextents_delta += delta;
		break;
	case XFS_TRANS_SB_RES_FREXTENTS:
		/*
		 * The allocation has already been applied to the
		 * in-core superblocks's counter.  This should only
		 * be applied to the on-disk superblock.
		 */
		ASSERT(delta < 0);
		tp->t_res_frextents_delta += delta;
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
	if (tp->t_res_fdblocks_delta != 0) {
		sbp->sb_fdblocks += tp->t_res_fdblocks_delta;
	}
	if (tp->t_frextents_delta != 0) {
		sbp->sb_frextents += tp->t_frextents_delta;
	}

	/*
	 * Since all the modifiable fields are contiguous, we
	 * can get away with this.
	 */
	xfs_trans_log_buf(tp, bp, offsetof(xfs_sb_t, sb_icount),
			  offsetof(xfs_sb_t, sb_frextents) +
			  sizeof(sbp->sb_frextents) - 1);
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
	 * The t_res_fdblocks_delta and t_res_frextents_delta fields are
	 * explicity NOT applied to the in-core superblock.
	 * The idea is that that has already been done.
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
xfs_trans_commit(xfs_trans_t	*tp,
		 uint		flags)
{
	uint		async;

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
 * by both xfs_trans_commit() and the xfs_sync routine.
 */
void
xfs_trans_commit_async(xfs_mount_t *mp)
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

STATIC void
xfs_trans_do_commit(xfs_trans_t	*tp,
		    uint	flags)
/* ARGSUSED */
{
	char			*trans_headerp;
	char			*trans_commitp;
	xfs_log_iovec_t		*log_vector;
	int			nvec;
	xfs_log_item_desc_t	*start_desc;
	xfs_log_item_desc_t	*desc;
	xfs_lsn_t		commit_lsn;
	int			error;
	int			log_flags;
	static xfs_lsn_t	trans_lsn = 1;

	/*
	 * Determine whether this commit is releasing a permanent
	 * log reservation or not.
	 */
	if (flags & XFS_TRANS_RELEASE_LOG_RES) {
		ASSERT(tp->t_flags & XFS_TRANS_PERM_LOG_RES);
		log_flags = XFS_LOG_REL_PERM_RESERV;
	} else {
		log_flags = 0;
	}

	/*
	 * If there is nothing to be logged by the transaction,
	 * then unlock all of the items associated with the
	 * transaction and free the transaction structure.
	 * Also make sure to return any reserved blocks to
	 * the free pool.
	 */
	if (!(tp->t_flags & XFS_TRANS_DIRTY)) {
		xfs_trans_unreserve_and_mod_sb(tp);
		if (tp->t_ticket) {
			xfs_log_done(tp->t_mountp, tp->t_ticket, log_flags);
		}
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
	 * Ask each log item how many log_vector entries it will
	 * need so we can figure out how many to allocate.
	 */
	nvec = xfs_trans_count_vecs(tp);

	log_vector = (xfs_log_iovec_t *)kmem_alloc(nvec *
						   sizeof(xfs_log_iovec_t),
						   KM_SLEEP);

	/*
	 * Fill in the log_vector and pin the logged items, and
	 * then write the transaction to the log.
	 */
	xfs_trans_fill_vecs(tp, log_vector);
	error = xfs_log_write(tp->t_mountp, log_vector, nvec, tp->t_ticket,
			      &(tp->t_lsn));
	ASSERT(error == 0);
	if (log_debug) {
		commit_lsn = xfs_log_done(tp->t_mountp, tp->t_ticket,
					  log_flags);
	} else {
		tp->t_lsn = trans_lsn++;
	}

	kmem_free(log_vector, nvec * sizeof(xfs_log_iovec_t));

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
	 * Once the transaction has been committed, unused
	 * reservations need to be released and changes to
	 * the superblock need to be reflected in the in-core
	 * version.  Do that now.
	 */
	xfs_trans_unreserve_and_mod_sb(tp);

	/*
 	 * Tell the LM to call the transaction completion routine
	 * when the log write with LSN commit_lsn completes.
	 * After this call we cannot reference tp, because the call
	 * can happen at any time and tp can be freed.
	 */
	if (log_debug) {
		tp->t_logcb.cb_func = (void(*)(void*))xfs_trans_committed;
		tp->t_logcb.cb_arg = tp;
		xfs_log_notify(tp->t_mountp, commit_lsn, &(tp->t_logcb));
	} else {
		xfs_trans_committed(tp);
	}
}

#if 0

STATIC void
xfs_trans_do_commit(xfs_trans_t *tp, uint flags)
/* ARGSUSED */
{
	xfs_log_iovec_t		*log_vector;
	uint			nvec;
	int			error;
	static xfs_lsn_t	trans_lsn = 1;
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
	 * Ask each log item how many log_vector entries it will
	 * need so we can figure out how many to allocate.
	 */
	nvec = xfs_trans_count_vecs(tp);
	ASSERT(nvec > 1);

	log_vector = (xfs_log_iovec_t *)kmem_alloc(nvec *
						   sizeof(xfs_log_iovec_t),
						   KM_SLEEP);

	/*
	 * Fill in the log_vector and pin the logged items, and
	 * then write the transaction to the log.
	 */
	xfs_trans_fill_vecs(tp, log_vector);
	error = xfs_log_write(tp->t_mountp, log_vector, nvec, tp->t_ticket,
			      &(tp->t_lsn));
	ASSERT(error == 0);
	tp->t_lsn = trans_lsn++;
/*
	commit_lsn = xfs_log_done(tp->t_mountp, tp->t_ticket, 0);
*/

	kmem_free(log_vector, nvec * sizeof(xfs_log_iovec_t));

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
	xfs_trans_unreserve_and_mod_sb(tp);

	/*
 	 * Tell the LM to call the transaction completion routine
	 * when the log write with LSN commit_lsn completes.
	 * After this call we cannot reference tp, because the call
	 * can happen at any time and tp can be freed.
	xfs_log_notify((void(*)(void*))xfs_trans_committed, tp, commit_lsn);
	 */
	xfs_trans_committed(tp);
}
#endif /* 0 */

/*
 * Total up the number of log iovecs needed to commit this
 * transaction.  The transaction itself needs one for the
 * transaction header.  Ask each dirty item in turn how many
 * it needs to get the total.
 */
STATIC uint
xfs_trans_count_vecs(xfs_trans_t *tp)
{
	int			nvecs;
	xfs_log_item_desc_t	*lidp;

	nvecs = 1;
	lidp = xfs_trans_first_item(tp);
	ASSERT(lidp != NULL);
	while (lidp != NULL) {
		/*
		 * Skip items which aren't dirty in this transaction.
		 */
		if (!(lidp->lid_flags & XFS_LID_DIRTY)) {
			lidp = xfs_trans_next_item(tp, lidp);
			continue;
		}
		lidp->lid_size = IOP_SIZE(lidp->lid_item);
		nvecs += lidp->lid_size;
		lidp = xfs_trans_next_item(tp, lidp);
	}

	return nvecs;
}

/*
 * Fill in the vector with pointers to data to be logged
 * by this transaction.  The transaction header takes
 * the first vector, and then each dirty item takes the
 * number of vectors it indicated it needed in xfs_trans_count_vecs().
 *
 * As each item fills in the entries it needs, also pin the item
 * so that it cannot be flushed out until the log write completes.
 */
STATIC void
xfs_trans_fill_vecs(xfs_trans_t		*tp,
		    xfs_log_iovec_t	*log_vector)
{
	xfs_log_item_desc_t	*lidp;
	xfs_log_iovec_t		*vecp;
	uint			nitems;			

	/*
	 * Skip over the entry for the transaction header, we'll
	 * fill that in at the end.
	 */
	vecp = log_vector + 1;		/* pointer arithmetic */

	nitems = 0;
	lidp = xfs_trans_first_item(tp);
	ASSERT(lidp != NULL);
	while (lidp != NULL) {
		/*
		 * Skip items which aren't dirty in this transaction.
		 */
		if (!(lidp->lid_flags & XFS_LID_DIRTY)) {
			lidp = xfs_trans_next_item(tp, lidp);
			continue;
		}
		/*
		 * The item may be marked dirty but not log anything.
		 * This can be used to get called when a transaction
		 * is committed.
		 */
		if (lidp->lid_size) {
			nitems++;
		}
		IOP_FORMAT(lidp->lid_item, vecp);
		vecp += lidp->lid_size;		/* pointer arithmetic */
		IOP_PIN(lidp->lid_item);
		lidp = xfs_trans_next_item(tp, lidp);
	}

	/*
	 * Now that we've counted the number of items in this
	 * transaction, fill in the transaction header.
	 */
	tp->t_header.th_magic = XFS_TRANS_HEADER_MAGIC;
	tp->t_header.th_type = tp->t_type;
	tp->t_header.th_tid = tp->t_tid;
	tp->t_header.th_num_items = nitems;
	log_vector->i_addr = (caddr_t)&tp->t_header;
	log_vector->i_len = sizeof(xfs_trans_header_t);
}


/*
 * Unlock all of the transaction's items and free the transaction.
 * The transaction must not have modified any of its items, because
 * there is no way to restore them to their previous state.
 *
 * If the transaction has made a log reservation, make sure to release
 * it as well.
 */
void
xfs_trans_cancel(xfs_trans_t	*tp,
		 int		flags)
{
	int	log_flags;

	ASSERT(!(tp->t_flags & XFS_TRANS_DIRTY));

	xfs_trans_unreserve_and_mod_sb(tp);
	if (tp->t_ticket) {
		if (flags & XFS_TRANS_RELEASE_LOG_RES) {
			ASSERT(tp->t_flags & XFS_TRANS_PERM_LOG_RES);
			log_flags = XFS_LOG_REL_PERM_RESERV;
		} else {
			log_flags = 0;
		}
		xfs_log_done(tp->t_mountp, tp->t_ticket, log_flags);
	}
	xfs_trans_free_items(tp);
	xfs_trans_free(tp);
}


/*
 * Free the transaction structure.  If there is more clean up
 * to do when the structure is freed, add it here.
 */
STATIC void
xfs_trans_free(xfs_trans_t *tp)
{
	freesema(&(tp->t_sema));
	kmem_zone_free(xfs_trans_zone, tp);
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
 * The commit processing consists of unlocking items which were
 * held locked with the SYNC_UNLOCK attribute, calling the committed
 * routine of each logged item, updating the item's position in the AIL
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
xfs_trans_chunk_committed(xfs_log_item_chunk_t	*licp,
			  xfs_lsn_t		lsn)
{
	xfs_log_item_desc_t	*lidp;
	xfs_log_item_t		*lip;
	xfs_lsn_t		item_lsn;
	struct xfs_mount	*mp;
	int			i;
	int			s;

	lidp = licp->lic_descs;
	for (i = 0; i <= XFS_LIC_MAX_SLOT; i++, lidp++) {
		if (XFS_LIC_ISFREE(licp, i)) {
			continue;
		}

		lip = lidp->lid_item;

		if (lidp->lid_flags & XFS_LID_SYNC_UNLOCK) {
			IOP_UNLOCK(lip);
		}

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














