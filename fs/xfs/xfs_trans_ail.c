
#include <sys/param.h>
#include <sys/buf.h>
#include <sys/vnode.h>
#include <sys/debug.h>
#include "xfs_types.h"
#include "xfs_inum.h"
#include "xfs.h"
#include "xfs_trans.h"
#include "xfs_sb.h"
#include "xfs_mount.h"
#include "xfs_trans_priv.h"

#ifdef SIM
#include "sim.h"
#endif

STATIC void		sbb_insert(xfs_log_item_t **, xfs_log_item_t*);
STATIC xfs_log_item_t	*sbb_delete(xfs_log_item_t **, xfs_log_item_t*);
STATIC xfs_log_item_t	*sbb_min(xfs_log_item_t **);
STATIC xfs_log_item_t	*sbb_next(xfs_log_item_t **, xfs_log_item_t*);

/*
 * This will be called from xfs_trans_do_commit() after a transaction
 * has been committed to the incore log to see if any items at the
 * tail of the log need to be pushed out and to push on them if
 * necessary.  For efficiency, it makes an approximate comparison
 * of the locations of the head and tail of the log to see if it looks
 * like there is anything to do before finding out for sure.  This
 * should allow the routine to return quickly in the common case without
 * doing anything.
 */
#ifndef SIM
void
xfs_trans_push_ail(struct xfs_mount *mp)
{
	xfs_lsn_t		tail_lsn;
	xfs_lsn_t		head_lsn;
	xfs_log_item_t		*lip;
	long			diff;
	int			s;
	int			gen;

	/*
	 * m_ail_lsn will only be written holding the AIL lock.
	 * By reading it without the lock, we may read an inconsistent
	 * value.  It will be inconsistent in that one of the two 32 bit
	 * values in an lsn will have its old value with respect to
	 * the concurrent write.  This can make us check for work
	 * unnecessarily or not check at all, but neither case will
	 * kill us.
	 */
	tail_lsn = mp->m_ail_lsn;
	head_lsn = xfs_log_lsn(mp);

	/*
	 * If the head is far enough from the tail of the log,
	 * then return without doing anything.
	 */
	diff = (long)(XFS_LSN_DIFF(head_lsn, tail_lsn));
	if (diff > mp->m_log_thresh) {
		return;
	}

	/*
	 * We've determined that there might be some things to
	 * flush, so check now while holding the AIL lock.
	 * If we can't get the AIL lock, then someone else should
	 * be taking care of it.  In that case just get out.
	 */
	if (!(s = AIL_TRYLOCK(mp))) {
		return;
	}
	lip = xfs_trans_first_ail(mp, &gen);
	if (lip == NULL) {
		AIL_UNLOCK(mp, s);
		return;
	}

	/*
	 * While the item we are looking at is too close to the head
	 * of the log, try to flush it out.
	 */
	diff = (long)(XFS_LSN_DIFF(head_lsn, lip->li_lsn));
	while (diff < mp->m_log_thresh) {
		/*
		 * If we can lock the item without sleeping, unlock
		 * the AIL lock and flush the item.  Then re-grab the
		 * AIL lock so we can look for the next item on the
		 * AIL.  Since we unlock the AIL while we flush the
		 * item, the next routine may start over again at the
		 * the beginning of the list if anything has changed.
		 * That is what the generation count is for.
		 *
		 * If we can't lock the item, either its holder will flush
		 * it or it is already being flushed or it is being relogged.
		 * In any of these case it is being taken care of and we
		 * can just skip to the next item in the list.
		 */
		if (IOP_TRYLOCK(lip)) {
			AIL_UNLOCK(mp, s);
			IOP_PUSH(lip);
			s = AIL_LOCK(mp);
		}
		lip = xfs_trans_next_ail(mp, lip, &gen);
		if (lip == NULL) {
			break;
		}
		diff = (long)(XFS_LSN_DIFF(head_lsn, lip->li_lsn));
	}
	AIL_UNLOCK(mp, s);
	return;
}
#endif


/*
 * Update the position of the item in the AIL with the new
 * lsn.  If it is not yet in the AIL, add it.  Otherwise, move
 * it to its new position by removing it and re-adding it.
 *
 * Increment the AIL's generation count to indicate that the tree
 * has changed.
 */
void
xfs_trans_update_ail(struct xfs_mount *mp, xfs_log_item_t *lip, xfs_lsn_t lsn)
{
	xfs_log_item_t		**ailp;
	xfs_log_item_t		*dlip;

	ailp = &(mp->m_ail);
	if (lip->li_flags & XFS_LI_IN_AIL) {
		dlip = sbb_delete(ailp, lip);
		/* ASSERT(dlip == lip); */
	} else {
		lip->li_flags |= XFS_LI_IN_AIL;
	}

	lip->li_lsn = lsn;

	sbb_insert(ailp, lip);
	mp->m_ail_gen++;
}

/*
 * Delete the given item from the AIL.  It must already be in
 * the AIL.
 *
 * Clear the IN_AIL flag from the item, reset its lsn to 0, and
 * bump the AIL's generation count to indicate that the tree
 * has changed.
 */
void
xfs_trans_delete_ail(struct xfs_mount *mp, xfs_log_item_t *lip)
{
	xfs_log_item_t		**ailp;
	xfs_log_item_t		*dlip;

	/* ASSERT(lip->li_flags & XFS_LI_IN_AIL); */

	ailp = &(mp->m_ail);
	dlip = sbb_delete(ailp, lip);
	/* ASSERT(dlip == lip); */

	lip->li_flags &= ~XFS_LI_IN_AIL;
	lip->li_lsn = 0;

	mp->m_ail_gen++;
}



/*
 * Return the item in the AIL with the smallest lsn.
 * Return the current tree generation number for use
 * in calls to xfs_trans_next_ail().
 */
xfs_log_item_t *
xfs_trans_first_ail(struct xfs_mount *mp, int *gen)
{
	xfs_log_item_t	*lip;

	lip = sbb_min(&(mp->m_ail));
	*gen = (int)mp->m_ail_gen;

	return (lip);
}

/*
 * If the generation count of the tree has not changed since the
 * caller last took something from the AIL, then return the elmt
 * in the tree which follows the one given.  If the count has changed,
 * then return the minimum elmt of the AIL.
 */
xfs_log_item_t *
xfs_trans_next_ail(struct xfs_mount *mp, xfs_log_item_t *lip, int *gen)
{
	xfs_log_item_t	*nlip;

	if (mp->m_ail_gen == *gen) {
		nlip = sbb_next(&(mp->m_ail), lip);
	} else {
		nlip = sbb_min(&(mp->m_ail));
	}

	return (nlip);
}






STATIC void
sbb_insert(xfs_log_item_t **root, xfs_log_item_t *lip)
/* ARGSUSED */
{
	return;
}


STATIC xfs_log_item_t *
sbb_delete(xfs_log_item_t **root, xfs_log_item_t *lip)
/* ARGSUSED */
{
	return (NULL);
}


STATIC xfs_log_item_t *
sbb_min(xfs_log_item_t **root)
/* ARGSUSED */
{
	return (NULL);

}


STATIC xfs_log_item_t *
sbb_next(xfs_log_item_t **root, xfs_log_item_t *lip)
/* ARGSUSED */
{
	return (NULL);

}




