
#include <sys/param.h>
#include <sys/debug.h>
#include <sys/uuid.h>
#ifdef SIM
#define _KERNEL
#endif
#include <sys/buf.h>
#include <sys/vnode.h>
#ifdef SIM
#undef _KERNEL
#endif
#ifndef SIM
#include <sys/systm.h>
#endif
#include "xfs_types.h"
#include "xfs_inum.h"
#include "xfs_log.h"
#include "xfs_trans.h"
#include "xfs_sb.h"
#include "xfs_mount.h"
#include "xfs_log.h"
#include "xfs_trans_priv.h"

#ifdef SIM
#include "sim.h"
#endif

STATIC void	xfs_ail_insert(xfs_ail_entry_t *, xfs_log_item_t *);
STATIC xfs_log_item_t	*xfs_ail_delete(xfs_ail_entry_t	*, xfs_log_item_t *);
STATIC xfs_log_item_t	*xfs_ail_min(xfs_ail_entry_t *);
STATIC xfs_log_item_t	*xfs_ail_next(xfs_ail_entry_t *, xfs_log_item_t	*);

#ifdef XFSDEBUG
STATIC void	xfs_ail_check(xfs_ail_entry_t *);
#else
#define	xfs_ail_check(a)
#endif /* XFSDEBUG */


/*
 * This is called by the log manager code to determine the LSN
 * of the tail of the log.  This is exactly the LSN of the first
 * item in the AIL.  If the AIL is empty, then this function
 * returns 0.
 *
 * We need the AIL lock in order to get a coherent read of the
 * lsn of the last item in the AIL.
 */
xfs_lsn_t
xfs_trans_tail_ail(xfs_mount_t *mp)
{
	int		s;
	xfs_lsn_t	lsn;
	xfs_log_item_t	*lip;

	s = AIL_LOCK(mp);
	lip = xfs_ail_min(&(mp->m_ail));
	if (lip == NULL) {
		lsn = (xfs_lsn_t)0;
	} else {
		lsn = lip->li_lsn;
	}
	AIL_UNLOCK(mp, s);

	return lsn;
}



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
#ifdef NOTYET
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
xfs_trans_update_ail(xfs_mount_t	*mp,
		     xfs_log_item_t	*lip,
		     xfs_lsn_t		lsn)
{
	xfs_ail_entry_t		*ailp;
	xfs_log_item_t		*dlip;

	ailp = &(mp->m_ail);
	if (lip->li_flags & XFS_LI_IN_AIL) {
		dlip = xfs_ail_delete(ailp, lip);
		ASSERT(dlip == lip);
	} else {
		lip->li_flags |= XFS_LI_IN_AIL;
	}

	lip->li_lsn = lsn;

	xfs_ail_insert(ailp, lip);
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
xfs_trans_delete_ail(xfs_mount_t	*mp,
		     xfs_log_item_t	*lip)
{
	xfs_ail_entry_t		*ailp;
	xfs_log_item_t		*dlip;

	ASSERT(lip->li_flags & XFS_LI_IN_AIL);

	ailp = &(mp->m_ail);
	dlip = xfs_ail_delete(ailp, lip);
	ASSERT(dlip == lip);

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
xfs_trans_first_ail(xfs_mount_t	*mp,
		    int		*gen)
{
	xfs_log_item_t	*lip;

	lip = xfs_ail_min(&(mp->m_ail));
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
xfs_trans_next_ail(xfs_mount_t		*mp,
		   xfs_log_item_t	*lip,
		   int			*gen)
{
	xfs_log_item_t	*nlip;

	if (mp->m_ail_gen == *gen) {
		nlip = xfs_ail_next(&(mp->m_ail), lip);
	} else {
		nlip = xfs_ail_min(&(mp->m_ail));
	}

	return (nlip);
}


/*
 * The active item list (AIL) is a doubly linked list of log
 * items sorted by ascending lsn.  The base of the list is
 * a forw/back pointer pair embedded in the xfs mount structure.
 * The base is initialized with both pointers pointing to the
 * base.  This case always needs to be distinguished, because
 * the base has no lsn to look at.  We almost always insert
 * at the end of the list, so on inserts we search from the
 * end of the list to find where the new item belongs.
 */

/*
 * Initialize the doubly linked list to point only to itself.
 */
void
xfs_trans_ail_init(xfs_mount_t *mp)
{
	mp->m_ail.ail_forw = (xfs_log_item_t*)&(mp->m_ail);
	mp->m_ail.ail_back = (xfs_log_item_t*)&(mp->m_ail);
}

/*
 * Insert the given log item into the AIL.
 * We almost always insert at the end of the list, so on inserts
 * we search from the end of the list to find where the
 * new item belongs.
 */
STATIC void
xfs_ail_insert(xfs_ail_entry_t	*base,
	       xfs_log_item_t	*lip)
/* ARGSUSED */
{
	xfs_log_item_t	*next_lip;

	/*
	 * If the list is empty, just insert the item.
	 */
	if (base->ail_back == (xfs_log_item_t*)base) {
		base->ail_forw = lip;
		base->ail_back = lip;
		lip->li_ail.ail_forw = (xfs_log_item_t*)base;
		lip->li_ail.ail_back = (xfs_log_item_t*)base;
		return;
	}

	next_lip = base->ail_back;
	while ((next_lip != (xfs_log_item_t*)base) &&
	       (next_lip->li_lsn > lip->li_lsn)) {
		next_lip = next_lip->li_ail.ail_back;
	}
	ASSERT((next_lip == (xfs_log_item_t*)base) ||
	       (next_lip->li_lsn <= lip->li_lsn));
	lip->li_ail.ail_forw = next_lip->li_ail.ail_forw;
	lip->li_ail.ail_back = next_lip;
	next_lip->li_ail.ail_forw = lip;
	lip->li_ail.ail_forw->li_ail.ail_back = lip;

	xfs_ail_check(base);
	return;
}

/*
 * Delete the given item from the AIL.  Return a pointer to the item.
 */
STATIC xfs_log_item_t *
xfs_ail_delete(xfs_ail_entry_t	*base,
	       xfs_log_item_t	*lip)
/* ARGSUSED */
{
	lip->li_ail.ail_forw->li_ail.ail_back = lip->li_ail.ail_back;
	lip->li_ail.ail_back->li_ail.ail_forw = lip->li_ail.ail_forw;
	lip->li_ail.ail_forw = NULL;
	lip->li_ail.ail_back = NULL;

	xfs_ail_check(base);
	return lip;
}

/*
 * Return a pointer to the first item in the AIL.
 * If the AIL is empty, then return NULL.
 */
STATIC xfs_log_item_t *
xfs_ail_min(xfs_ail_entry_t *base)
/* ARGSUSED */
{
	if (base->ail_forw == (xfs_log_item_t*)base) {
		return NULL;
	}
	return base->ail_forw;

}

/*
 * Return a pointer to the item which follows
 * the given item in the AIL.  If the given item
 * is the last item in the list, then return NULL.
 */
STATIC xfs_log_item_t *
xfs_ail_next(xfs_ail_entry_t	*base,
	     xfs_log_item_t	*lip)
/* ARGSUSED */
{
	if (lip->li_ail.ail_forw == (xfs_log_item_t*)base) {
		return NULL;
	}
	return lip->li_ail.ail_forw;

}

#ifdef XFSDEBUG
/*
 * Check that the list is sorted as it should be.
 */
STATIC void
xfs_ail_check(xfs_ail_entry_t *base)
{
	xfs_log_item_t	*lip;
	xfs_log_item_t	*prev_lip;

	lip = base->ail_forw;
	if (lip == (xfs_log_item_t*)base) {
		/*
		 * Make sure the pointers are correct when the list
		 * is empty.
		 */
		ASSERT(base->ail_back == (xfs_log_item_t*)base);
		return;
	}

	/*
	 * Walk the list checking forward and backward pointers,
	 * lsn ordering, and that every entry has the XFS_LI_IN_AIL
	 * flag set.
	 */
	prev_lip = (xfs_log_item_t*)base;
	while (lip != (xfs_log_item_t*)base) {
		if (prev_lip != (xfs_log_item_t*)base) {
			ASSERT(prev_lip->li_ail.ail_forw == lip);
			ASSERT(prev_lip->li_lsn <= lip->li_lsn);
		}
		ASSERT(lip->li_ail.ail_back == prev_lip);
		ASSERT((lip->li_flags & XFS_LI_IN_AIL) != 0);
		prev_lip = lip;
		lip = lip->li_ail.ail_forw;
	}
	ASSERT(lip == (xfs_log_item_t*)base);
	ASSERT(base->ail_back == prev_lip);
}
#endif /* XFSDEBUG */



