
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

STATIC void
xfs_ail_ticket_wait(
	xfs_mount_t		*mp,
	xfs_ail_ticket_t	*ticketp,
	xfs_lsn_t		lsn,
	int			spl);

STATIC void
xfs_ail_ticket_wakeup(
	xfs_mount_t	*mp,
	xfs_lsn_t	lsn,
	int		wakeup_equal);

STATIC void
xfs_ail_moved_item(
	xfs_mount_t	*mp,
	xfs_log_item_t	*lip);

STATIC void
xfs_ail_insert(
	xfs_ail_entry_t	*base,
	xfs_log_item_t	*lip);

STATIC xfs_log_item_t *
xfs_ail_delete(
	xfs_ail_entry_t	*base,
	xfs_log_item_t	*lip);

STATIC xfs_log_item_t *
xfs_ail_min(
	xfs_ail_entry_t	*base);

STATIC xfs_log_item_t *
xfs_ail_next(
	xfs_ail_entry_t	*base,
	xfs_log_item_t	*lip);

#ifdef XFSDEBUG
STATIC void
xfs_ail_check(
	xfs_ail_entry_t *base);
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
xfs_trans_tail_ail(
	xfs_mount_t	*mp)
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
 * xfs_trans_push_ail
 *
 * This routine is called to move the tail of the AIL
 * forward.  It does this by trying to flush items in the AIL
 * whose lsns are below the given threshold_lsn.  The minimum_lsn
 * parameter specifies the lowest LSN allowed by the caller
 * when this routine returns.  We are allowed to sleep here
 * waiting for the tail of the AIL to move beyond the minimum_lsn.
 * The caller provides an uninitialized ticket (which must come from
 * the heap, not the stack) which we will use to sleep with if
 * necessary.
 *
 * The routine returns the lsn of the tail of the log.
 */
xfs_lsn_t
xfs_trans_push_ail(
	xfs_mount_t		*mp,
	xfs_lsn_t		threshold_lsn)
{
	xfs_lsn_t		lsn;
	xfs_log_item_t		*lip;
	int			s;
	int			gen;
	int			restarts;
	int			lock_result;
	int			flush_log;
	int			flushed_log;
	int			ail_was_unlocked;
#define	XFS_TRANS_PUSH_AIL_RESTARTS	10

	flushed_log = 0;

 startover:
	ail_was_unlocked = 0;
	s = AIL_LOCK(mp);
 startover_locked:
	lip = xfs_trans_first_ail(mp, &gen);
	if (lip == NULL) {
		/*
		 * Just return if the AIL is empty.
		 */
		AIL_UNLOCK(mp, s);
		return (xfs_lsn_t)0;
	}

	/*
	 * While the item we are looking at is below the given threshold
	 * try to flush it out.  Make sure to limit the number of times
	 * we allow xfs_trans_next_ail() to restart scanning from the
	 * beginning of the list.  We can't stop until we've at least
	 * tried to push on everything in the AIL with an LSN less than
	 * the given minimum.  That way we know that we at least tried
	 * to push on everything necessary before going to sleep below.
	 */
	flush_log = 0;
	restarts = 0;
	while (((restarts < XFS_TRANS_PUSH_AIL_RESTARTS) &&
		(XFS_LSN_CMP(lip->li_lsn, threshold_lsn) < 0))) {
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
		lock_result = IOP_TRYLOCK(lip);
		switch (lock_result) {
		case XFS_ITEM_SUCCESS:
			AIL_UNLOCK(mp, s);
			IOP_PUSH(lip);
			ail_was_unlocked = 1;
			s = AIL_LOCK(mp);
			break;
		case XFS_ITEM_PINNED:
			flush_log = 1;
			break;
		case XFS_ITEM_LOCKED:
		case XFS_ITEM_FLUSHING:
			break;
		default:
			ASSERT(0);
			break;
		}

		lip = xfs_trans_next_ail(mp, lip, &gen, &restarts);
		if (lip == NULL) {
			break;
		}
	}

	if (flush_log && !flushed_log) {
		/*
		 * If something we need to push out was pinned, then
		 * push out the log so it will become unpinned and
		 * move forward in the AIL.  We indicate that we've
		 * already flushed the log so that we don't loop
		 * flushing the log waiting for an item to be unpinned.
		 * There is a race here where things that were locked
		 * in one pass become pinned in the next and we don't
		 * flush the log, but as a last resort xfs_sync() will
		 * always flush the log and bail us out.
		 */
		AIL_UNLOCK(mp, s);
		xfs_log_force(mp, (xfs_lsn_t)0, XFS_LOG_FORCE);
		flushed_log = 1;
		goto startover;
	}

	if (ail_was_unlocked) {		/* XXXmiken: needed? */
		/*
		 * We need a pass through the AIL where we don't
		 * unlock the AIL and we decide whether or not to
		 * go to sleep below in order to prevent races with
		 * the items we wait for being released before we
		 * go to sleep.  Therefore, if we've unlocked the
		 * AIL in the last pass, then start over.  Change
		 * the threshold_lsn to be the same as the minimum_lsn
		 * so we only do the work that is critical to us
		 * in subsequent passes.
		 */
		ail_was_unlocked = 0;
		goto startover_locked;
	}

	lip = xfs_ail_min(&(mp->m_ail));
	if (lip == NULL) {
		lsn = (xfs_lsn_t)0;
	} else {
		lsn = lip->li_lsn;
	}
	
	AIL_UNLOCK(mp, s);
	return lsn;
}	/* xfs_trans_push_ail */

/*
 * Insert the ticket into the list of tickets sorted by lsn
 * and go to sleep.  The list is a doubly linked circular
 * list whose first elmt is pointed to by mp->m_ail_wait.
 * We scan from the tail of the list since things are
 * generally added at the end.
 *
 * The AIL lock must be held when calling this routine.
 */
STATIC void
xfs_ail_ticket_wait(
	xfs_mount_t		*mp,
	xfs_ail_ticket_t	*ticketp,
	xfs_lsn_t		lsn,
	int			spl)
{
	xfs_ail_ticket_t	*atp;
	xfs_ail_ticket_t	*head;

	ASSERT(valusema(&(ticketp->at_sema)) == 0);

	ticketp->at_lsn = lsn;
	if (mp->m_ail_wait == NULL) {
		mp->m_ail_wait = ticketp;
		ticketp->at_forw = ticketp;
		ticketp->at_back = ticketp;
		spunlockspl_psema(mp->m_ail_lock, spl,
				  &(ticketp->at_sema), PINOD);
		return;
	}

	/*
	 * Make a quick check to see if we're re-inserting ourselves
	 * at the front of the list.
	 */
	head = mp->m_ail_wait;
	if (XFS_LSN_CMP(lsn, head->at_lsn) <= 0) {
		/*
		 * The LSN of the item is less than or equal
		 * to that of the first elmt in the list.
		 * Just put us at the head of the list.
		 */
		ticketp->at_forw = head;
		ticketp->at_back = head->at_back;
		head->at_back->at_forw = ticketp;
		head->at_back = ticketp;
		mp->m_ail_wait = ticketp;
		spunlockspl_psema(mp->m_ail_lock, spl,
				  &(ticketp->at_sema), PINOD);
		return;
	}

	/*
	 * Walk backwards throught the list until we find our
	 * spot.
	 */
	atp = head->at_back;
	while (XFS_LSN_CMP(lsn, atp->at_lsn) < 0) {
		/*
		 * The LSN of the new item is less than that of
		 * the item we're looking at, so go on to the next
		 * one.  Assert that we don't walk back past the head.
		 */
		ASSERT(atp != head);
		atp = atp->at_back;
	}
	
	/*
	 * Now atp points at the ticket with an lsn less than ours.
	 * Insert ourselves after it.
	 */
	ticketp->at_forw = atp->at_forw;
	ticketp->at_back = atp;
	atp->at_forw->at_back = ticketp;
	atp->at_forw = ticketp;
	spunlockspl_psema(mp->m_ail_lock, spl, &(ticketp->at_sema), PINOD);
}

/*
 * Wake up any processes waiting on the AIL ticket waiting list
 * that are waiting for an LSN less than the given LSN.  Pull the
 * tickets from the list as we wake them up.  The wakeup_equal
 * parameter tells us whether we should wake up sleepers waiting
 * for an LSN equal to the given one or not.
 */
STATIC void
xfs_ail_ticket_wakeup(
	xfs_mount_t	*mp,
	xfs_lsn_t	lsn,
	int		wakeup_equal)		      
{
	xfs_ail_ticket_t	*head;
	xfs_ail_ticket_t	*forw;
	xfs_ail_ticket_t	*back;

	head = mp->m_ail_wait;
	while ((head != NULL) &&
	       ((XFS_LSN_CMP(head->at_lsn, lsn) < 0) ||
		(wakeup_equal &&
		 (XFS_LSN_CMP(head->at_lsn, lsn) == 0)))) {
		forw = head->at_forw;
		if (forw == head) {
			mp->m_ail_wait = NULL;
			forw = NULL;
		} else {
			back = head->at_back;
			forw->at_back = back;
			back->at_forw = forw;
			mp->m_ail_wait = forw;
		}

		ASSERT(valusema(&(head->at_sema)) == -1);
		vsema(&(head->at_sema));

		head = forw;
	}
}	/* xfs_ail_ticket_wakeup */

/*
 * This is to be called when an item is unlocked that may have
 * been in the AIL.  It will wake up the first member of the AIL
 * wait list if this item's unlocking might allow it to progress.
 * If the item is in the AIL, then we need to get the AIL lock
 * while doing our checking so we don't race with someone going
 * to sleep waiting for this event in xfs_trans_push_ail().
 */
void
xfs_trans_unlocked_item(
	xfs_mount_t	*mp,
	xfs_log_item_t	*lip)
{
	int		s;
	xfs_log_item_t	*min_lip;

	if (!(lip->li_flags & XFS_LI_IN_AIL)) {
		return;
	}

	s = AIL_LOCK(mp);
	if (mp->m_ail_wait == NULL) {
		/*
		 * Noone is asleep, so there is nothing to do.
		 */
		AIL_UNLOCK(mp, s);
		return;
	}

	min_lip = xfs_ail_min(&mp->m_ail);
	if (min_lip == lip)
		xfs_log_move_tail(mp, 1);
#if 0
	if (XFS_LSN_CMP(lip->li_lsn, mp->m_ail_wait->at_lsn) <= 0) {
		xfs_ail_ticket_wakeup(mp, lip->li_lsn, 1);
	}
#endif
	AIL_UNLOCK(mp, s);
}	/* xfs_trans_unlocked_item */

/*
 * This is called when an item is deleted from or moved forward
 * in the AIL.  It will wake up the first member of the AIL
 * wait list if this item's unlocking might allow it to progress.
 * The caller must be holding the AIL lock.
 */
STATIC void
xfs_ail_moved_item(
	xfs_mount_t	*mp,
	xfs_log_item_t	*lip)
{
	xfs_log_item_t *min_lip;

	if (!(lip->li_flags & XFS_LI_IN_AIL)) {
		return;
	}

	if (mp->m_ail_wait == NULL) {
		/*
		 * Noone is asleep, so there is nothing to do.
		 */
		return;
	}

	min_lip = xfs_ail_min(&mp->m_ail);
	if (min_lip == lip)
		xfs_log_move_tail(mp, 1);
#if 0
	if (XFS_LSN_CMP(lip->li_lsn, mp->m_ail_wait->at_lsn) <= 0) {
		xfs_ail_ticket_wakeup(mp, lip->li_lsn, 1);
	}
#endif
}	/* xfs_ail_moved_item */

/*
 * Update the position of the item in the AIL with the new
 * lsn.  If it is not yet in the AIL, add it.  Otherwise, move
 * it to its new position by removing it and re-adding it.
 *
 * Wakeup anyone with an lsn less than the item's lsn.  If the item
 * we move in the AIL is the minimum one, update the tail lsn in the
 * log manager.
 *
 * Increment the AIL's generation count to indicate that the tree
 * has changed.
 */
void
xfs_trans_update_ail(
	xfs_mount_t	*mp,
	xfs_log_item_t	*lip,
	xfs_lsn_t	lsn)
{
	xfs_ail_entry_t		*ailp;
	xfs_log_item_t		*dlip;
	xfs_log_item_t		*mlip;	/* ptr to minimum lip */

	ailp = &(mp->m_ail);
	mlip = xfs_ail_min(ailp);

	if (lip->li_flags & XFS_LI_IN_AIL) {
		dlip = xfs_ail_delete(ailp, lip);
		ASSERT(dlip == lip);
	} else {
		lip->li_flags |= XFS_LI_IN_AIL;
	}

	lip->li_lsn = lsn;

	xfs_ail_insert(ailp, lip);
	xfs_ail_moved_item(mp, lip);
	if (mlip == dlip) {
		mlip = xfs_ail_min(&(mp->m_ail));
		xfs_log_move_tail(mp, mlip->li_lsn);
	}

	mp->m_ail_gen++;

}	/* xfs_trans_update_ail */

/*
 * Delete the given item from the AIL.  It must already be in
 * the AIL.
 *
 * Wakeup anyone with an lsn less than item's lsn.    If the item
 * we delete in the AIL is the minimum one, update the tail lsn in the
 * log manager.
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
	xfs_log_item_t		*mlip;

	ASSERT(lip->li_flags & XFS_LI_IN_AIL);

	ailp = &(mp->m_ail);
	mlip = xfs_ail_min(ailp);
	dlip = xfs_ail_delete(ailp, lip);
	ASSERT(dlip == lip);

	xfs_ail_moved_item(mp, lip);
	if (mlip == dlip) {
		mlip = xfs_ail_min(&(mp->m_ail));
		xfs_log_move_tail(mp, (mlip ? mlip->li_lsn : 0));
	}

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
xfs_trans_first_ail(
	xfs_mount_t	*mp,
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
 * then return the minimum elmt of the AIL and bump the restarts counter
 * if one is given.
 */
xfs_log_item_t *
xfs_trans_next_ail(
	xfs_mount_t	*mp,
	xfs_log_item_t	*lip,
	int		*gen,
	int		*restarts)
{
	xfs_log_item_t	*nlip;

	if (mp->m_ail_gen == *gen) {
		nlip = xfs_ail_next(&(mp->m_ail), lip);
	} else {
		nlip = xfs_ail_min(&(mp->m_ail));
		if (gen != NULL) {
			*gen = (int)mp->m_ail_gen;
		}
		if (restarts != NULL) {
			(*restarts)++;
		}
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
xfs_trans_ail_init(
	xfs_mount_t	*mp)
{
	mp->m_ail.ail_forw = (xfs_log_item_t*)&(mp->m_ail);
	mp->m_ail.ail_back = (xfs_log_item_t*)&(mp->m_ail);
	mp->m_ail_wait = NULL;
}

/*
 * Insert the given log item into the AIL.
 * We almost always insert at the end of the list, so on inserts
 * we search from the end of the list to find where the
 * new item belongs.
 */
STATIC void
xfs_ail_insert(
	xfs_ail_entry_t	*base,
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
xfs_ail_delete(
	xfs_ail_entry_t	*base,
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
xfs_ail_min(
	xfs_ail_entry_t	*base)
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
xfs_ail_next(
	xfs_ail_entry_t	*base,
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
xfs_ail_check(
	xfs_ail_entry_t *base)
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



