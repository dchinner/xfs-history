#ident	"$Revision: 1.20 $"

/*
 * Free space allocation for xFS.
 */

#include <sys/param.h>
#ifdef SIM
#define _KERNEL
#endif
#include <sys/buf.h>
#ifdef SIM
#undef _KERNEL
#endif
#include <sys/vnode.h>
#include <sys/uuid.h>
#include <sys/debug.h>
#include <stddef.h>
#ifdef SIM
#include <stdlib.h>
#include <bstring.h>
#else
#include <sys/systm.h>
#endif
#include "xfs_types.h"
#include "xfs_inum.h"
#include "xfs_log.h"
#include "xfs_trans.h"
#include "xfs_sb.h"
#include "xfs_ag.h"
#include "xfs_mount.h"
#include "xfs_alloc_btree.h"
#include "xfs_ialloc.h"
#include "xfs_bmap_btree.h"
#include "xfs_btree.h"
#include "xfs_alloc.h"
#ifdef SIM
#include "sim.h"
#endif

#if !defined(SIM) || !defined(XFSDEBUG)
#define	kmem_check()		/* dummy for memory-allocation checking */
#endif

/*
 * Prototypes for internal functions.
 */

/*
 * Single level of the xfs_alloc_delete record deletion routine.
 * Delete record pointed to by cur/level.
 * Remove the record from its block then rebalance the tree.
 * Return 0 for error, 1 for done, 2 to go on to the next level.
 */
STATIC int				/* fail/done/go-on */
xfs_alloc_delrec(
	xfs_btree_cur_t	*cur,		/* btree cursor */
	int		level);		/* level removing record from */

/*
 * Insert one record/level.  Return information to the caller
 * allowing the next level up to proceed if necessary.
 */
STATIC int				/* success/failure */
xfs_alloc_insrec(
	xfs_btree_cur_t	*cur,		/* btree cursor */
	int		level,		/* level to insert record at */
	xfs_agblock_t	*bnop,		/* i/o: block number inserted */
	xfs_alloc_rec_t	*recp,		/* i/o: record data inserted */
	xfs_btree_cur_t	**curp);	/* output: new cursor replacing cur */

#ifdef XFSDEBUG
/*
 * Check key consistency in one btree level, then recurse down to the next.
 */
STATIC void
xfs_alloc_kcheck_btree(
	xfs_btree_cur_t	*cur,		/* btree cursor */
	xfs_agf_t	*agf,		/* a.g. freespace header */
	xfs_agblock_t	bno,		/* block number to check */
	int		level,		/* level of the block */
	xfs_alloc_key_t	*keyp);		/* value of expected first key */
#endif	/* XFSDEBUG */

/*
 * Log header fields from a btree block.
 */
STATIC void
xfs_alloc_log_block(
	xfs_trans_t	*tp,		/* transaction pointer */
	buf_t		*bp,		/* buffer containing btree block */
	int		fields);	/* mask of fields: XFS_BB_... */

/*
 * Log keys from a btree block (nonleaf).
 */
STATIC void
xfs_alloc_log_keys(
	xfs_btree_cur_t	*cur,		/* btree cursor */
	buf_t		*bp,		/* buffer containing btree block */
	int		kfirst,		/* index of first key to log */
	int		klast);		/* index of last key to log */

/*
 * Log block pointer fields from a btree block (nonleaf).
 */
STATIC void
xfs_alloc_log_ptrs(
	xfs_btree_cur_t	*cur,		/* btree cursor */
	buf_t		*bp,		/* buffer containing btree block */
	int		pfirst,		/* index of first pointer to log */
	int		plast);		/* index of last pointer to log */

/*
 * Log records from a btree block (leaf).
 */
STATIC void
xfs_alloc_log_recs(
	xfs_btree_cur_t	*cur,		/* btree cursor */
	buf_t		*bp,		/* buffer containing btree block */
	int		rfirst,		/* index of first record to log */
	int		rlast);		/* index of last record to log */

/*
 * Lookup the record.  The cursor is made to point to it, based on dir.
 */
STATIC int				/* success/failure */
xfs_alloc_lookup(
	xfs_btree_cur_t	*cur,		/* btree cursor */
	xfs_lookup_t	dir);		/* <=, ==, or >= */

/*
 * Move 1 record left from cur/level if possible.
 * Update cur to reflect the new path.
 */
STATIC int				/* success/failure */
xfs_alloc_lshift(
	xfs_btree_cur_t	*cur,		/* btree cursor */
	int		level);		/* level to shift record on */

/*
 * Allocate a new root block, fill it in.
 */
STATIC int				/* success/failure */
xfs_alloc_newroot(
	xfs_btree_cur_t	*cur);		/* btree cursor */

#ifdef XFSDEBUG
/*
 * Guts of xfs_alloc_rcheck.  For each level in the btree, check
 * all its blocks for consistency.
 */
STATIC void
xfs_alloc_rcheck_btree(
	xfs_btree_cur_t	*cur,		/* btree cursor */
	xfs_agf_t	*agf,		/* a.g. freespace header */
	xfs_agblock_t	rbno,		/* root block number */
	int		levels);	/* number of levels in the btree */
#endif	/* XFSDEBUG */

#ifdef XFSDEBUG
/*
 * Check one btree block for record consistency.
 */
STATIC xfs_agblock_t			/* next block to process */
xfs_alloc_rcheck_btree_block(
	xfs_btree_cur_t	*cur,		/* btree cursor */
	xfs_agnumber_t	agno,		/* allocation group number */
	xfs_agblock_t	bno,		/* block number to check */
	xfs_agblock_t	*fbno,		/* output: first block at next level */
	void		*rec,		/* previous record/key value */
	int		level);		/* level of this block */
#endif	/* XFSDEBUG */

/*
 * Move 1 record right from cur/level if possible.
 * Update cur to reflect the new path.
 */
STATIC int				/* success/failure */
xfs_alloc_rshift(
	xfs_btree_cur_t	*cur,		/* btree cursor */
	int		level);		/* level to shift record on */

/*
 * Split cur/level block in half.
 * Return new block number and its first record (to be inserted into parent).
 * Also returns a new cursor in this case, for next level of insert to use.
 */
STATIC int				/* success/failure */
xfs_alloc_split(
	xfs_btree_cur_t	*cur,		/* btree cursor */
	int		level,		/* level to split */
	xfs_agblock_t	*bnop,		/* output: block number allocated */
	xfs_alloc_key_t	*keyp,		/* output: first key of new block */
	xfs_btree_cur_t	**curp);	/* output: new cursor */

/*
 * Update keys at all levels from here to the root along the cursor's path.
 */
STATIC void
xfs_alloc_updkey(
	xfs_btree_cur_t	*cur,		/* btree cursor */
	xfs_alloc_key_t	*keyp,		/* new key value to update to */
	int		level);		/* starting level for update */

/*
 * Internal functions.
 */

/*
 * Single level of the xfs_alloc_delete record deletion routine.
 * Delete record pointed to by cur/level.
 * Remove the record from its block then rebalance the tree.
 * Return 0 for error, 1 for done, 2 to go on to the next level.
 */
STATIC int				/* fail/done/go-on */
xfs_alloc_delrec(
	xfs_btree_cur_t		*cur,	/* btree cursor */
	int			level)	/* level removing record from */
{
	buf_t			*agbp;	/* buffer for a.g. freelist header */
	xfs_agf_t		*agf;	/* allocation group freelist header */
	xfs_alloc_block_t	*block;	/* btree block record/key lives in */
	xfs_agblock_t		bno;	/* btree block number */
	buf_t			*bp;	/* buffer for block */
	int			i;	/* loop index */
	xfs_alloc_key_t		key;	/* kp points here if block is level 0 */
	xfs_alloc_key_t		*kp;	/* pointer to btree keys */
	xfs_agblock_t		lbno;	/* left block's block number */
	buf_t			*lbp;	/* left block's buffer pointer */
	xfs_alloc_block_t	*left;	/* left btree block */
	xfs_alloc_key_t		*lkp;	/* left block key pointer */
	xfs_alloc_ptr_t		*lpp;	/* left block address pointer */
	int			lrecs;	/* number of records in left block */
	xfs_alloc_rec_t		*lrp;	/* left block record pointer */
	xfs_alloc_ptr_t		*pp;	/* pointer to btree addresses */
	int			ptr;	/* index in btree block for this rec */
	xfs_agblock_t		rbno;	/* right block's block number */
	buf_t			*rbp;	/* right block's buffer pointer */
	xfs_alloc_block_t	*right;	/* right btree block */
	xfs_alloc_key_t		*rkp;	/* right block key pointer */
	xfs_alloc_rec_t		*rp;	/* pointer to btree records */
	xfs_alloc_ptr_t		*rpp;	/* right block address pointer */
	int			rrecs;	/* number of records in right block */
	xfs_alloc_rec_t		*rrp;	/* right block record pointer */
	xfs_btree_cur_t		*tcur;	/* temporary btree cursor */

	xfs_alloc_rcheck(cur);
	/*
	 * Get the index of the entry being deleted, check for nothing there.
	 */
	ptr = cur->bc_ptrs[level];
	if (ptr == 0)
		return 0;
	/*
	 * Get the buffer & block containing the record or key/ptr.
	 */
	bp = cur->bc_bufs[level];
	block = XFS_BUF_TO_ALLOC_BLOCK(bp);
	xfs_btree_check_sblock(cur, block, level);
	/*
	 * Fail if we're off the end of the block.
	 */
	if (ptr > block->bb_numrecs)
		return 0;
	/*
	 * It's a nonleaf.  Excise the key and ptr being deleted, by
	 * sliding the entries past them down one.
	 * Log the changed areas of the block.
	 */
	if (level > 0) {
		kp = XFS_ALLOC_KEY_ADDR(block, 1, cur);
		pp = XFS_ALLOC_PTR_ADDR(block, 1, cur);
		for (i = ptr; i < block->bb_numrecs; i++) {
			kp[i - 1] = kp[i];
			xfs_btree_check_sptr(cur, pp[i], level);
			pp[i - 1] = pp[i];
		}
		if (ptr < i) {
			xfs_alloc_log_ptrs(cur, bp, ptr, i - 1);
			xfs_alloc_log_keys(cur, bp, ptr, i - 1);
		}
	}
	/*
	 * It's a leaf.  Excise the record being deleted, by sliding the
	 * entries past it down one.  Log the changed areas of the block.
	 */
	else {
		rp = XFS_ALLOC_REC_ADDR(block, 1, cur);
		for (i = ptr; i < block->bb_numrecs; i++)
			rp[i - 1] = rp[i];
		if (ptr < i)
			xfs_alloc_log_recs(cur, bp, ptr, i - 1);
		/*
		 * If it's the first record in the block, we'll need a key
		 * structure to pass up to the next level (updkey).
		 */
		if (ptr == 1) {
			key.ar_startblock = rp->ar_startblock;
			key.ar_blockcount = rp->ar_blockcount;
			kp = &key;
		}
	}
	/*
	 * Decrement and log the number of entries in the block.
	 */
	block->bb_numrecs--;
	xfs_alloc_log_block(cur->bc_tp, bp, XFS_BB_NUMRECS);
	/*
	 * See if the longest free extent in the allocation group was
	 * changed by this operation.  True if it's the by-size btree, and
	 * this is the leaf level, and there is no right sibling block,
	 * and this was the last record.
	 */
	agbp = cur->bc_private.a.agbp;
	agf = XFS_BUF_TO_AGF(agbp);
	if (level == 0 &&
	    cur->bc_btnum == XFS_BTNUM_CNT &&
	    block->bb_rightsib == NULLAGBLOCK &&
	    ptr > block->bb_numrecs) {
		ASSERT(ptr == block->bb_numrecs + 1);
		/*
		 * There are still records in the block.  Grab the size
		 * from the last one.
		 */
		if (block->bb_numrecs) {
			rrp = XFS_ALLOC_REC_ADDR(block, block->bb_numrecs, cur);
			agf->agf_longest = rrp->ar_blockcount;
		}
		/*
		 * No free extents left.
		 */
		else
			agf->agf_longest = 0;
		xfs_alloc_log_agf(cur->bc_tp, agbp, XFS_AGF_LONGEST);
	}
	/*
	 * If we get here for a non-leaf block,
	 * we just did a join at the previous level.
	 * Make the cursor point to the good (left) key.
	 */
	if (level > 0)
		xfs_alloc_decrement(cur, level);
	/*
	 * Is this the root level?  If so, we're almost done.
	 */
	if (level == cur->bc_nlevels - 1) {
		/*
		 * If this is the root level,
		 * and there's only one entry left,
		 * and it's NOT the leaf level,
		 * then we can get rid of this level.
		 */
		if (block->bb_numrecs == 1 && level > 0) {
			/*
			 * pp is still set to the first pointer in the block.
			 * Make it the new root of the btree.
			 */
			agf->agf_roots[cur->bc_btnum] = *pp;
			agf->agf_levels[cur->bc_btnum]--;
			/*
			 * Put this buffer/block on the ag's freelist.
			 */
			xfs_alloc_put_freelist(cur->bc_tp, agbp, bp);
			xfs_alloc_log_agf(cur->bc_tp, agbp,
				XFS_AGF_ROOTS | XFS_AGF_LEVELS);
			/*
			 * Update the cursor so there's one fewer level.
			 */
			xfs_btree_setbuf(cur, level, 0);
			cur->bc_nlevels--;
		}
		xfs_alloc_rcheck(cur);
		kmem_check();
		return 1;
	}
	/*
	 * If we deleted the leftmost entry in the block, update the
	 * key values above us in the tree.
	 */
	if (ptr == 1)
		xfs_alloc_updkey(cur, kp, level + 1);
	/*
	 * If the number of records remaining in the block is at least
	 * the minimum, we're done.
	 */
	if (block->bb_numrecs >= XFS_ALLOC_BLOCK_MINRECS(level, cur)) {
		kmem_check();
		xfs_alloc_rcheck(cur);
		return 1;
	}
	/*
	 * Otherwise, we have to move some records around to keep the
	 * tree balanced.  Look at the left and right sibling blocks to
	 * see if we can re-balance by moving only one record.
	 */
	rbno = block->bb_rightsib;
	lbno = block->bb_leftsib;
	bno = NULLAGBLOCK;
	ASSERT(rbno != NULLAGBLOCK || lbno != NULLAGBLOCK);
	/*
	 * Duplicate the cursor so our btree manipulations here won't
	 * disrupt the next level up.
	 */
	tcur = xfs_btree_dup_cursor(cur);
	/*
	 * If there's a right sibling, see if it's ok to shift an entry
	 * out of it.
	 */
	if (rbno != NULLAGBLOCK) {
		/*
		 * Move the temp cursor to the last entry in the next block.
		 * Actually any entry but the first would suffice.
		 */
		xfs_btree_lastrec(tcur, level);
		xfs_alloc_increment(tcur, level);
		xfs_btree_lastrec(tcur, level);
		/*
		 * Grab a pointer to the block.
		 */
		rbp = tcur->bc_bufs[level];
		right = XFS_BUF_TO_ALLOC_BLOCK(rbp);
		xfs_btree_check_sblock(cur, right, level);
		/*
		 * Grab the current block number, for future use.
		 */
		bno = right->bb_leftsib;
		/*
		 * If right block is full enough so that removing one entry
		 * won't make it too empty, and left-shifting an entry out
		 * of right to us works, we're done.
		 */
		if (right->bb_numrecs - 1 >=
		     XFS_ALLOC_BLOCK_MINRECS(level, cur) &&
		    xfs_alloc_lshift(tcur, level)) {
			ASSERT(block->bb_numrecs >=
			       XFS_ALLOC_BLOCK_MINRECS(level, cur));
			xfs_btree_del_cursor(tcur);
			kmem_check();
			return 1;
		}
		/*
		 * Otherwise, grab the number of records in right for
		 * future reference, and fix up the temp cursor to point 
		 * to our block again (last record).
		 */
		rrecs = right->bb_numrecs;
		if (lbno != NULLAGBLOCK) {
			xfs_btree_firstrec(tcur, level);
			xfs_alloc_decrement(tcur, level);
		}
	}
	/*
	 * If there's a left sibling, see if it's ok to shift an entry
	 * out of it.
	 */
	if (lbno != NULLAGBLOCK) {
		/*
		 * Move the temp cursor to the first entry in the
		 * previous block.
		 */
		xfs_btree_firstrec(tcur, level);
		xfs_alloc_decrement(tcur, level);
		xfs_btree_firstrec(tcur, level);
		/*
		 * Grab a pointer to the block.
		 */
		lbp = tcur->bc_bufs[level];
		left = XFS_BUF_TO_ALLOC_BLOCK(lbp);
		xfs_btree_check_sblock(cur, left, level);
		/*
		 * Grab the current block number, for future use.
		 */
		bno = left->bb_rightsib;
		/*
		 * If left block is full enough so that removing one entry
		 * won't make it too empty, and right-shifting an entry out
		 * of left to us works, we're done.
		 */
		if (left->bb_numrecs - 1 >=
		     XFS_ALLOC_BLOCK_MINRECS(level, cur) &&
		    xfs_alloc_rshift(tcur, level)) {
			ASSERT(block->bb_numrecs >=
			       XFS_ALLOC_BLOCK_MINRECS(level, cur));
			xfs_btree_del_cursor(tcur);
			cur->bc_ptrs[level]++;
			kmem_check();
			return 1;
		}
		/*
		 * Otherwise, grab the number of records in right for
		 * future reference.
		 */
		lrecs = left->bb_numrecs;
	}
	/*
	 * Delete the temp cursor, we're done with it.
	 */
	xfs_btree_del_cursor(tcur);
	/*
	 * If here, we need to do a join to keep the tree balanced.
	 */
	ASSERT(bno != NULLAGBLOCK);
	/*
	 * See if we can join with the left neighbor block.
	 */
	if (lbno != NULLAGBLOCK &&
	    lrecs + block->bb_numrecs <= XFS_ALLOC_BLOCK_MAXRECS(level, cur)) {
		/*
		 * Set "right" to be the starting block,
		 * "left" to be the left neighbor.
		 */
		rbno = bno;
		right = block;
		rbp = bp;
		lbp = xfs_btree_read_bufs(cur->bc_mp, cur->bc_tp,
			cur->bc_private.a.agno, lbno, 0);
		left = XFS_BUF_TO_ALLOC_BLOCK(lbp);
		xfs_btree_check_sblock(cur, left, level);
	}
	/*
	 * If that won't work, see if we can join with the right neighbor block.
	 */
	else if (rbno != NULLAGBLOCK &&
		 rrecs + block->bb_numrecs <=
		  XFS_ALLOC_BLOCK_MAXRECS(level, cur)) {
		/*
		 * Set "left" to be the starting block,
		 * "right" to be the right neighbor.
		 */
		lbno = bno;
		left = block;
		lbp = bp;
		rbp = xfs_btree_read_bufs(cur->bc_mp, cur->bc_tp,
			cur->bc_private.a.agno, rbno, 0);
		right = XFS_BUF_TO_ALLOC_BLOCK(rbp);
		xfs_btree_check_sblock(cur, right, level);
	}
	/*
	 * Otherwise, we can't fix the imbalance.
	 * Just return.  This is probably a logic error, but it's not fatal.
	 */
	else {
		xfs_alloc_rcheck(cur);
		kmem_check();
		return 1;
	}
	/*
	 * We're now going to join "left" and "right" by moving all the stuff
	 * in "right" to "left" and deleting "right".
	 */
	if (level > 0) {
		/*
		 * It's a non-leaf.  Move keys and pointers.
		 */
		lkp = XFS_ALLOC_KEY_ADDR(left, left->bb_numrecs + 1, cur);
		lpp = XFS_ALLOC_PTR_ADDR(left, left->bb_numrecs + 1, cur);
		rkp = XFS_ALLOC_KEY_ADDR(right, 1, cur);
		rpp = XFS_ALLOC_PTR_ADDR(right, 1, cur);
		for (i = 0; i < right->bb_numrecs; i++) {
			lkp[i] = rkp[i];
			xfs_btree_check_sptr(cur, rpp[i], level);
			lpp[i] = rpp[i];
		}
		xfs_alloc_log_keys(cur, lbp, left->bb_numrecs + 1,
				   left->bb_numrecs + right->bb_numrecs);
		xfs_alloc_log_ptrs(cur, lbp, left->bb_numrecs + 1,
				   left->bb_numrecs + right->bb_numrecs);
	} else {
		/*
		 * It's a leaf.  Move records.
		 */
		lrp = XFS_ALLOC_REC_ADDR(left, left->bb_numrecs + 1, cur);
		rrp = XFS_ALLOC_REC_ADDR(right, 1, cur);
		for (i = 0; i < right->bb_numrecs; i++)
			lrp[i] = rrp[i];
		xfs_alloc_log_recs(cur, lbp, left->bb_numrecs + 1,
				   left->bb_numrecs + right->bb_numrecs);
	}
	/*
	 * If we joined with the left neighbor, set the buffer in the
	 * cursor to the left block, and fix up the index.
	 */
	if (bp != lbp) {
		xfs_btree_setbuf(cur, level, lbp);
		cur->bc_ptrs[level] += left->bb_numrecs;
	}
	/*
	 * If we joined with the right neighbor and there's a level above
	 * us, increment the cursor at that level.
	 */
	else if (level + 1 < cur->bc_nlevels)
		xfs_alloc_increment(cur, level + 1);
	/*
	 * Fix up the number of records in the surviving block.
	 */
	left->bb_numrecs += right->bb_numrecs;
	/*
	 * Fix up the right block pointer in the surviving block, and log it.
	 */
	left->bb_rightsib = right->bb_rightsib;
	xfs_alloc_log_block(cur->bc_tp, lbp, XFS_BB_NUMRECS | XFS_BB_RIGHTSIB);
	/*
	 * If there is a right sibling now, make it point to the 
	 * remaining block.
	 */
	if (left->bb_rightsib != NULLAGBLOCK) {
		xfs_alloc_block_t	*rrblock;
		buf_t			*rrbp;

		rrbp = xfs_btree_read_bufs(cur->bc_mp, cur->bc_tp,
			cur->bc_private.a.agno, left->bb_rightsib, 0);
		rrblock = XFS_BUF_TO_ALLOC_BLOCK(rrbp);
		xfs_btree_check_sblock(cur, rrblock, level);
		rrblock->bb_leftsib = lbno;
		xfs_alloc_log_block(cur->bc_tp, rrbp, XFS_BB_LEFTSIB);
	}
	/*
	 * Free the deleting block by putting it on the freelist.
	 */
	xfs_alloc_put_freelist(cur->bc_tp, agbp, rbp);
	xfs_alloc_rcheck(cur);
	kmem_check();
	/* 
	 * Return value means the next level up has something to do.
	 */
	return 2;
}

/*
 * Insert one record/level.  Return information to the caller
 * allowing the next level up to proceed if necessary.
 */
STATIC int				/* success/failure */
xfs_alloc_insrec(
	xfs_btree_cur_t		*cur,	/* btree cursor */
	int			level,	/* level to insert record at */
	xfs_agblock_t		*bnop,	/* i/o: block number inserted */
	xfs_alloc_rec_t		*recp,	/* i/o: record data inserted */
	xfs_btree_cur_t		**curp)	/* output: new cursor replacing cur */
{
	buf_t			*agbp;	/* buffer for a.g. freelist header */
	xfs_agf_t		*agf;	/* allocation group freelist header */
	xfs_alloc_block_t	*block;	/* btree block record/key lives in */
	buf_t			*bp;	/* buffer for block */
	int			i;	/* loop index */
	xfs_alloc_key_t		key;	/* key value being inserted */
	xfs_alloc_key_t		*kp;	/* pointer to btree keys */
	xfs_agblock_t		nbno;	/* block number of allocated block */
	xfs_btree_cur_t		*ncur;	/* new cursor to be used at next lvl */
	xfs_alloc_key_t		nkey;	/* new key value, from split */
	xfs_alloc_rec_t		nrec;	/* new record value, for caller */
	int			optr;	/* old ptr value */
	xfs_alloc_ptr_t		*pp;	/* pointer to btree addresses */
	int			ptr;	/* index in btree block for this rec */
	xfs_alloc_rec_t		*rp;	/* pointer to btree records */

	/*
	 * If we made it to the root level, allocate a new root block
	 * and we're done.
	 */
	if (level >= cur->bc_nlevels) {
		i = xfs_alloc_newroot(cur);
		*bnop = NULLAGBLOCK;
		kmem_check();
		return i;
	}
	xfs_alloc_rcheck(cur);
	/*
	 * Make a key out of the record data to be inserted, and save it.
	 */
	key.ar_startblock = recp->ar_startblock;
	key.ar_blockcount = recp->ar_blockcount;
	optr = ptr = cur->bc_ptrs[level];
	/*
	 * If we're off the left edge, return failure.
	 */
	if (ptr == 0) {
		kmem_check();
		return 0;
	}
	/*
	 * Get pointers to the btree buffer and block.
	 */
	bp = cur->bc_bufs[level];
	block = XFS_BUF_TO_ALLOC_BLOCK(bp);
	xfs_btree_check_sblock(cur, block, level);
#ifdef DEBUG
	/* 
	 * Check that the new entry is being inserted in the right place.
	 */
	if (ptr <= block->bb_numrecs) {
		if (level == 0) {
			rp = XFS_ALLOC_REC_ADDR(block, ptr, cur);
			xfs_btree_check_rec(cur->bc_btnum, recp, rp);
		} else {
			kp = XFS_ALLOC_KEY_ADDR(block, ptr, cur);
			xfs_btree_check_key(cur->bc_btnum, &key, kp);
		}
	}
#endif
	nbno = NULLAGBLOCK;
	ncur = (xfs_btree_cur_t *)0;
	/*
	 * If the block is full, we can't insert the new entry until we
	 * make the block un-full.
	 */
	if (block->bb_numrecs == XFS_ALLOC_BLOCK_MAXRECS(level, cur)) {
		/*
		 * First, try shifting an entry to the right neighbor.
		 */
		if (xfs_alloc_rshift(cur, level)) {
			/* nothing */
		}
		/*
		 * Next, try shifting an entry to the left neighbor.
		 */
		else if (xfs_alloc_lshift(cur, level)) {
			optr = ptr = cur->bc_ptrs[level];
		}
		/*
		 * Next, try splitting the current block in half.
		 * If this works we have to re-set our variables because
		 * we could be in a different block now.
		 */
		else if (xfs_alloc_split(cur, level, &nbno, &nkey, &ncur)) {
			bp = cur->bc_bufs[level];
			block = XFS_BUF_TO_ALLOC_BLOCK(bp);
			xfs_btree_check_sblock(cur, block, level);
			ptr = cur->bc_ptrs[level];
			nrec.ar_startblock = nkey.ar_startblock;
			nrec.ar_blockcount = nkey.ar_blockcount;
		}
		/*
		 * Otherwise the insert fails.
		 */
		else {
			kmem_check();
			return 0;
		}
	}
	/*
	 * At this point we know there's room for our new entry in the block
	 * we're pointing at.
	 */
	if (level > 0) {
		/*
		 * It's a non-leaf entry.  Make a hole for the new data
		 * in the key and ptr regions of the block.
		 */
		kp = XFS_ALLOC_KEY_ADDR(block, 1, cur);
		pp = XFS_ALLOC_PTR_ADDR(block, 1, cur);
		for (i = block->bb_numrecs; i >= ptr; i--) {
			kp[i] = kp[i - 1];
			xfs_btree_check_sptr(cur, pp[i - 1], level);
			pp[i] = pp[i - 1];
		}
		xfs_btree_check_sptr(cur, *bnop, level);
		/*
		 * Now stuff the new data in, bump numrecs and log the new data.
		 */
		kp[i] = key;
		pp[i] = *bnop;
		block->bb_numrecs++;
		xfs_alloc_log_keys(cur, bp, ptr, block->bb_numrecs);
		xfs_alloc_log_ptrs(cur, bp, ptr, block->bb_numrecs);
	} else {
		/*
		 * It's a leaf entry.  Make a hole for the new record.
		 */
		rp = XFS_ALLOC_REC_ADDR(block, 1, cur);
		for (i = block->bb_numrecs; i >= ptr; i--)
			rp[i] = rp[i - 1];
		/*
		 * Now stuff the new record in, bump numrecs
		 * and log the new data.
		 */
		rp[i] = *recp;
		block->bb_numrecs++;
		xfs_alloc_log_recs(cur, bp, ptr, block->bb_numrecs);
	}
	/*
	 * Log the new number of records in the btree header.
	 */
	xfs_alloc_log_block(cur->bc_tp, bp, XFS_BB_NUMRECS);
#ifdef DEBUG
	/*
	 * Check that the key/record is in the right place, now.
	 */
	if (ptr < block->bb_numrecs) {
		if (level == 0)
			xfs_btree_check_rec(cur->bc_btnum, rp + i, rp + i + 1);
		else
			xfs_btree_check_key(cur->bc_btnum, kp + i, kp + i + 1);
	}
#endif
	/*
	 * If we inserted at the start of a block, update the parents' keys.
	 */
	if (optr == 1)
		xfs_alloc_updkey(cur, &key, level + 1);
	/*
	 * Look to see if the longest extent in the allocation group
	 * needs to be updated.
	 */
	agbp = cur->bc_private.a.agbp;
	agf = XFS_BUF_TO_AGF(agbp);
	if (level == 0 &&
	    cur->bc_btnum == XFS_BTNUM_CNT &&
	    block->bb_rightsib == NULLAGBLOCK &&
	    recp->ar_blockcount > agf->agf_longest) {
		/*
		 * If this is a leaf in the by-size btree and there
		 * is no right sibling block and this block is bigger
		 * than the previous longest block, update it.
		 */
		agf->agf_longest = recp->ar_blockcount;
		xfs_alloc_log_agf(cur->bc_tp, agbp, XFS_AGF_LONGEST);
	}
	/*
	 * Return the new block number, if any.
	 * If there is one, give back a record value and a cursor too.
	 */
	*bnop = nbno;
	xfs_alloc_rcheck(cur);
	if (nbno != NULLAGBLOCK) {
		*recp = nrec;
		*curp = ncur;
	} else
		xfs_alloc_kcheck(cur);
	kmem_check();
	return 1;
}

#ifdef XFSDEBUG
/*
 * Check key consistency in one btree level, then recurse down to the next.
 */
STATIC void
xfs_alloc_kcheck_btree(
	xfs_btree_cur_t		*cur,	/* btree cursor */
	xfs_agf_t		*agf,	/* a.g. freespace header */
	xfs_agblock_t		bno,	/* block number to check */
	int			level,	/* level of the block */
	xfs_alloc_key_t		*keyp)	/* value of expected first key */
{
	xfs_alloc_block_t	*block;	/* btree block pointer */
	buf_t			*bp;	/* buffer for block */
	int			i;	/* loop index */
	xfs_alloc_key_t		key;	/* key value for comparisons */
	xfs_alloc_key_t		*kp;	/* pointer to btree keys */
	xfs_alloc_ptr_t		*pp;	/* pointer to btree index values */
	xfs_alloc_rec_t		*rp;	/* pointer to btree records */

	/*
	 * Get the buffer and block for the one we're checking.
	 */
	ASSERT(bno != NULLAGBLOCK);
	bp = xfs_btree_read_bufs(cur->bc_mp, cur->bc_tp, agf->agf_seqno,
		bno, 0);
	block = XFS_BUF_TO_ALLOC_BLOCK(bp);
	xfs_btree_check_sblock(cur, block, level);
	/*
	 * If a non-leaf, set up the key pointer & save the first
	 * key value, if requested.
	 */
	if (level > 0) {
		kp = XFS_ALLOC_KEY_ADDR(block, 1, cur);
		if (keyp)
			key = *kp;
	}
	/*
	 * If a leaf, set up the record pointer and save the key value
	 * if requested.
	 */
	else {
		rp = XFS_ALLOC_REC_ADDR(block, 1, cur);
		if (keyp) {
			key.ar_startblock = rp->ar_startblock;
			key.ar_blockcount = rp->ar_blockcount;
		}
	}
	/*
	 * If the caller passed in a matching key, check it.
	 */
	if (keyp)
		ASSERT(bcmp(keyp, &key, sizeof(key)) == 0);
	/* 
	 * If this is not yet a leaf, check the next level down recursively.
	 */
	if (level > 0) {
		pp = XFS_ALLOC_PTR_ADDR(block, 1, cur);
		if (*pp != NULLAGBLOCK) {
			for (i = 1; i <= block->bb_numrecs; i++, pp++, kp++)
				xfs_alloc_kcheck_btree(cur, agf, *pp,
					level - 1, kp);
		}
	}
	xfs_trans_brelse(cur->bc_tp, bp);
}
#endif

/*
 * Log header fields from a btree block.
 */
STATIC void
xfs_alloc_log_block(
	xfs_trans_t		*tp,	/* transaction pointer */
	buf_t			*bp,	/* buffer containing btree block */
	int			fields)	/* mask of fields: XFS_BB_... */
{
	int			first;
	int			last;
	static const int	offsets[] = {
		offsetof(xfs_alloc_block_t, bb_magic),
		offsetof(xfs_alloc_block_t, bb_level),
		offsetof(xfs_alloc_block_t, bb_numrecs),
		offsetof(xfs_alloc_block_t, bb_leftsib),
		offsetof(xfs_alloc_block_t, bb_rightsib),
		sizeof(xfs_alloc_block_t)
	};

	xfs_btree_offsets(fields, offsets, XFS_BB_NUM_BITS, &first, &last);
	xfs_trans_log_buf(tp, bp, first, last);
	kmem_check();
}

/*
 * Log keys from a btree block (nonleaf).
 */
STATIC void
xfs_alloc_log_keys(
	xfs_btree_cur_t		*cur,	/* btree cursor */
	buf_t			*bp,	/* buffer containing btree block */
	int			kfirst,	/* index of first key to log */
	int			klast)	/* index of last key to log */
{
	xfs_alloc_block_t	*block;
	int			first;
	xfs_alloc_key_t		*kp;
	int			last;

	block = XFS_BUF_TO_ALLOC_BLOCK(bp);
	kp = XFS_ALLOC_KEY_ADDR(block, 1, cur);
	first = (caddr_t)&kp[kfirst - 1] - (caddr_t)block;
	last = ((caddr_t)&kp[klast] - 1) - (caddr_t)block;
	xfs_trans_log_buf(cur->bc_tp, bp, first, last);
	kmem_check();
}

/*
 * Log block pointer fields from a btree block (nonleaf).
 */
STATIC void
xfs_alloc_log_ptrs(
	xfs_btree_cur_t		*cur,	/* btree cursor */
	buf_t			*bp,	/* buffer containing btree block */
	int			pfirst,	/* index of first pointer to log */
	int			plast)	/* index of last pointer to log */
{
	xfs_alloc_block_t	*block;
	int			first;
	int			last;
	xfs_alloc_ptr_t		*pp;

	block = XFS_BUF_TO_ALLOC_BLOCK(bp);
	pp = XFS_ALLOC_PTR_ADDR(block, 1, cur);
	first = (caddr_t)&pp[pfirst - 1] - (caddr_t)block;
	last = ((caddr_t)&pp[plast] - 1) - (caddr_t)block;
	xfs_trans_log_buf(cur->bc_tp, bp, first, last);
	kmem_check();
}

/*
 * Log records from a btree block (leaf).
 */
STATIC void
xfs_alloc_log_recs(
	xfs_btree_cur_t		*cur,	/* btree cursor */
	buf_t			*bp,	/* buffer containing btree block */
	int			rfirst,	/* index of first record to log */
	int			rlast)	/* index of last record to log */
{
	xfs_alloc_block_t	*block;
	int			first;
	int			last;
	xfs_alloc_rec_t		*rp;

	block = XFS_BUF_TO_ALLOC_BLOCK(bp);
	rp = XFS_ALLOC_REC_ADDR(block, 1, cur);
	first = (caddr_t)&rp[rfirst - 1] - (caddr_t)block;
	last = ((caddr_t)&rp[rlast] - 1) - (caddr_t)block;
	xfs_trans_log_buf(cur->bc_tp, bp, first, last);
	kmem_check();
}

/*
 * Lookup the record.  The cursor is made to point to it, based on dir.
 */
STATIC int				/* success/failure */
xfs_alloc_lookup(
	xfs_btree_cur_t		*cur,	/* btree cursor */
	xfs_lookup_t		dir)	/* <=, ==, or >= */
{
	xfs_agblock_t		agbno;
	buf_t			*agbp;
	xfs_agnumber_t		agno;
	xfs_agf_t		*agf;
	xfs_alloc_block_t	*block;
	xfs_extlen_t		blockcount;
	buf_t			*bp;
	daddr_t			d;
	int			diff;
	int			high;
	int			i;
	int			keyno;
	xfs_alloc_key_t		*kkbase;
	xfs_alloc_key_t		*kkp;
	xfs_alloc_rec_t		*krbase;
	xfs_alloc_rec_t		*krp;
	int			level;
	int			low;
	xfs_mount_t		*mp;
	xfs_alloc_rec_t		*rp;
	xfs_agblock_t		startblock;

	xfs_alloc_rcheck(cur);
	xfs_alloc_kcheck(cur);
	mp = cur->bc_mp;
	agbp = cur->bc_private.a.agbp;
	agf = XFS_BUF_TO_AGF(agbp);
	agno = agf->agf_seqno;
	agbno = agf->agf_roots[cur->bc_btnum];
	rp = &cur->bc_rec.a;
	for (level = cur->bc_nlevels - 1, diff = 1; level >= 0; level--) {
		d = XFS_AGB_TO_DADDR(mp, agno, agbno);
		bp = cur->bc_bufs[level];
		if (bp && bp->b_blkno != d)
			bp = (buf_t *)0;
		if (!bp) {
			bp = xfs_trans_read_buf(cur->bc_tp, mp->m_dev, d,
				mp->m_bsize, 0);
			ASSERT(bp && !geterror(bp));
			xfs_btree_setbuf(cur, level, bp);
		}
		block = XFS_BUF_TO_ALLOC_BLOCK(bp);
		xfs_btree_check_sblock(cur, block, level);
		if (diff == 0)
			keyno = 1;
		else {
			if (level > 0)
				kkbase = XFS_ALLOC_KEY_ADDR(block, 1, cur);
			else
				krbase = XFS_ALLOC_REC_ADDR(block, 1, cur);
			low = 1;
			if (!(high = block->bb_numrecs)) {
				ASSERT(level == 0);
				cur->bc_ptrs[0] = dir != XFS_LOOKUP_LE;
				kmem_check();
				return 0;
			}
			while (low <= high) {
				keyno = (low + high) >> 1;
				if (level > 0) {
					kkp = kkbase + keyno - 1;
					startblock = kkp->ar_startblock;
					blockcount = kkp->ar_blockcount;
				} else {
					krp = krbase + keyno - 1;
					startblock = krp->ar_startblock;
					blockcount = krp->ar_blockcount;
				}
				if (cur->bc_btnum == XFS_BTNUM_BNO)
					diff = (int)startblock -
					       (int)rp->ar_startblock;
				else if (!(diff = (int)blockcount -
						  (int)rp->ar_blockcount))
					diff = (int)startblock -
						(int)rp->ar_startblock;
				if (diff < 0)
					low = keyno + 1;
				else if (diff > 0)
					high = keyno - 1;
				else
					break;
			}
		}
		if (level > 0) {
			if (diff > 0 && --keyno < 1)
				keyno = 1;
			agbno = *XFS_ALLOC_PTR_ADDR(block, keyno, cur);
			xfs_btree_check_sptr(cur, agbno, level);
			cur->bc_ptrs[level] = keyno;
		}
	}
	if (dir != XFS_LOOKUP_LE && diff < 0) {
		keyno++;
		/*
		 * If ge search and we went off the end of the block, but it's
		 * not the last block, we're in the wrong block.
		 */
		if (dir == XFS_LOOKUP_GE &&
		    keyno > block->bb_numrecs &&
		    block->bb_rightsib != NULLAGBLOCK) {
			cur->bc_ptrs[0] = keyno;
			i = xfs_alloc_increment(cur, 0);
			ASSERT(i == 1);
			kmem_check();
			return 1;
		}
	}
	else if (dir == XFS_LOOKUP_LE && diff > 0)
		keyno--;
	cur->bc_ptrs[0] = keyno;
	kmem_check();
	if (keyno == 0 || keyno > block->bb_numrecs)
		return 0;
	else
		return dir != XFS_LOOKUP_EQ || diff == 0;
}

/*
 * Move 1 record left from cur/level if possible.
 * Update cur to reflect the new path.
 */
STATIC int				/* success/failure */
xfs_alloc_lshift(
	xfs_btree_cur_t		*cur,	/* btree cursor */
	int			level)	/* level to shift record on */
{
	int			i;
	xfs_alloc_key_t		key;
	buf_t			*lbp;
	xfs_alloc_block_t	*left;
	xfs_alloc_key_t		*lkp;
	xfs_alloc_ptr_t		*lpp;
	xfs_alloc_rec_t		*lrp;
	int			nrec;
	buf_t			*rbp;
	xfs_alloc_block_t	*right;
	xfs_alloc_key_t		*rkp;
	xfs_alloc_ptr_t		*rpp;
	xfs_alloc_rec_t		*rrp;

	xfs_alloc_rcheck(cur);
	rbp = cur->bc_bufs[level];
	right = XFS_BUF_TO_ALLOC_BLOCK(rbp);
	xfs_btree_check_sblock(cur, right, level);
	if (right->bb_leftsib == NULLAGBLOCK)
		return 0;
	if (cur->bc_ptrs[level] <= 1)
		return 0;
	lbp = xfs_btree_read_bufs(cur->bc_mp, cur->bc_tp,
		cur->bc_private.a.agno, right->bb_leftsib, 0);
	left = XFS_BUF_TO_ALLOC_BLOCK(lbp);
	xfs_btree_check_sblock(cur, left, level);
	if (left->bb_numrecs == XFS_ALLOC_BLOCK_MAXRECS(level, cur)) {
		kmem_check();
		return 0;
	}
	nrec = left->bb_numrecs + 1;
	if (level > 0) {
		lkp = XFS_ALLOC_KEY_ADDR(left, nrec, cur);
		rkp = XFS_ALLOC_KEY_ADDR(right, 1, cur);
		*lkp = *rkp;
		xfs_alloc_log_keys(cur, lbp, nrec, nrec);
		lpp = XFS_ALLOC_PTR_ADDR(left, nrec, cur);
		rpp = XFS_ALLOC_PTR_ADDR(right, 1, cur);
		xfs_btree_check_sptr(cur, *rpp, level);
		*lpp = *rpp;
		xfs_alloc_log_ptrs(cur, lbp, nrec, nrec);
	} else {
		lrp = XFS_ALLOC_REC_ADDR(left, nrec, cur);
		rrp = XFS_ALLOC_REC_ADDR(right, 1, cur);
		*lrp = *rrp;
		xfs_alloc_log_recs(cur, lbp, nrec, nrec);
	}
	left->bb_numrecs++;
	xfs_alloc_log_block(cur->bc_tp, lbp, XFS_BB_NUMRECS);
#ifdef XFSDEBUG
	if (level > 0)
		xfs_btree_check_key(cur->bc_btnum, lkp - 1, lkp);
	else
		xfs_btree_check_rec(cur->bc_btnum, lrp - 1, lrp);
#endif
	right->bb_numrecs--;
	xfs_alloc_log_block(cur->bc_tp, rbp, XFS_BB_NUMRECS);
	if (level > 0) {
		for (i = 0; i < right->bb_numrecs; i++) {
			rkp[i] = rkp[i + 1];
			xfs_btree_check_sptr(cur, rpp[i + 1], level);
			rpp[i] = rpp[i + 1];
		}
		xfs_alloc_log_keys(cur, rbp, 1, right->bb_numrecs);
		xfs_alloc_log_ptrs(cur, rbp, 1, right->bb_numrecs);
	} else {
		for (i = 0; i < right->bb_numrecs; i++)
			rrp[i] = rrp[i + 1];
		xfs_alloc_log_recs(cur, rbp, 1, right->bb_numrecs);
		key.ar_startblock = rrp->ar_startblock;
		key.ar_blockcount = rrp->ar_blockcount;
		rkp = &key;
	}
	xfs_alloc_updkey(cur, rkp, level + 1);
	cur->bc_ptrs[level]--;
	xfs_alloc_rcheck(cur);
	kmem_check();
	return 1;
}

/*
 * Allocate a new root block, fill it in.
 */
STATIC int				/* success/failure */
xfs_alloc_newroot(
	xfs_btree_cur_t		*cur)	/* btree cursor */
{
	buf_t			*agbp;
	xfs_agf_t		*agf;
	xfs_alloc_block_t	*block;
	buf_t			*bp;
	xfs_alloc_key_t		*kp;
	xfs_agblock_t		lbno;
	buf_t			*lbp;
	xfs_alloc_block_t	*left;
	xfs_agblock_t		nbno;
	buf_t			*nbp;
	xfs_alloc_block_t	*new;
	int			nptr;
	xfs_alloc_ptr_t		*pp;
	xfs_agblock_t		rbno;
	buf_t			*rbp;
	xfs_alloc_block_t	*right;
	xfs_alloc_rec_t		*rp;

	xfs_alloc_rcheck(cur);
	ASSERT(cur->bc_nlevels < XFS_AG_MAXLEVELS(cur->bc_mp));
	agbp = cur->bc_private.a.agbp;
	agf = XFS_BUF_TO_AGF(agbp);
	nbno = xfs_alloc_get_freelist(cur->bc_tp, agbp, &nbp);
	if (nbno == NULLAGBLOCK)
		return 0;
	new = XFS_BUF_TO_ALLOC_BLOCK(nbp);
	agf->agf_roots[cur->bc_btnum] = nbno;
	agf->agf_levels[cur->bc_btnum]++;
	xfs_alloc_log_agf(cur->bc_tp, agbp, XFS_AGF_ROOTS | XFS_AGF_LEVELS);
	bp = cur->bc_bufs[cur->bc_nlevels - 1];
	block = XFS_BUF_TO_ALLOC_BLOCK(bp);
	xfs_btree_check_sblock(cur, block, cur->bc_nlevels - 1);
	if (block->bb_rightsib != NULLAGBLOCK) {
		lbp = bp;
		lbno = XFS_DADDR_TO_AGBNO(cur->bc_mp, lbp->b_blkno);
		left = block;
		rbno = left->bb_rightsib;
		rbp = xfs_btree_read_bufs(cur->bc_mp, cur->bc_tp,
			cur->bc_private.a.agno, rbno, 0);
		bp = rbp;
		right = XFS_BUF_TO_ALLOC_BLOCK(rbp);
		xfs_btree_check_sblock(cur, right, cur->bc_nlevels - 1);
		nptr = 1;
	} else {
		rbp = bp;
		rbno = XFS_DADDR_TO_AGBNO(cur->bc_mp, rbp->b_blkno);
		right = block;
		lbno = right->bb_leftsib;
		lbp = xfs_btree_read_bufs(cur->bc_mp, cur->bc_tp,
			cur->bc_private.a.agno, lbno, 0);
		bp = lbp;
		left = XFS_BUF_TO_ALLOC_BLOCK(lbp);
		xfs_btree_check_sblock(cur, left, cur->bc_nlevels - 1);
		nptr = 2;
	}
	new->bb_magic = xfs_magics[cur->bc_btnum];
	new->bb_level = (__uint16_t)cur->bc_nlevels;
	new->bb_numrecs = 2;
	new->bb_leftsib = new->bb_rightsib = NULLAGBLOCK;
	xfs_alloc_log_block(cur->bc_tp, nbp, XFS_BB_ALL_BITS);
	ASSERT(lbno != NULLAGBLOCK && rbno != NULLAGBLOCK);
	kp = XFS_ALLOC_KEY_ADDR(new, 1, cur);
	if (left->bb_level > 0) {
		kp[0] = *XFS_ALLOC_KEY_ADDR(left, 1, cur);
		kp[1] = *XFS_ALLOC_KEY_ADDR(right, 1, cur);
	} else {
		rp = XFS_ALLOC_REC_ADDR(left, 1, cur);
		kp[0].ar_startblock = rp->ar_startblock;
		kp[0].ar_blockcount = rp->ar_blockcount;
		rp = XFS_ALLOC_REC_ADDR(right, 1, cur);
		kp[1].ar_startblock = rp->ar_startblock;
		kp[1].ar_blockcount = rp->ar_blockcount;
	}
	xfs_alloc_log_keys(cur, nbp, 1, 2);
	pp = XFS_ALLOC_PTR_ADDR(new, 1, cur);
	pp[0] = lbno;
	pp[1] = rbno;
	xfs_alloc_log_ptrs(cur, nbp, 1, 2);
	xfs_btree_setbuf(cur, cur->bc_nlevels, nbp);
	cur->bc_ptrs[cur->bc_nlevels] = nptr;
	cur->bc_nlevels++;
	xfs_alloc_rcheck(cur);
	xfs_alloc_kcheck(cur);
	kmem_check();
	return 1;
}

#ifdef XFSDEBUG
/*
 * Guts of xfs_alloc_rcheck.  For each level in the btree, check
 * all its blocks for consistency.
 */
STATIC void
xfs_alloc_rcheck_btree(
	xfs_btree_cur_t	*cur,		/* btree cursor */
	xfs_agf_t	*agf,		/* a.g. freespace header */
	xfs_agblock_t	rbno,		/* root block number */
	int		levels)		/* number of levels in the btree */
{
	xfs_agblock_t	bno;
	xfs_agblock_t	fbno;
	xfs_alloc_key_t	key;
	int		level;
	xfs_alloc_rec_t	rec;
	void		*rp;

	rp = level ? (void *)&key : (void *)&rec;
	for (level = levels - 1, bno = rbno; level >= 0; level--, bno = fbno) {
		bno = xfs_alloc_rcheck_btree_block(cur, agf->agf_seqno, bno,
			&fbno, rp, level);
		while (bno != NULLAGBLOCK) {
			ASSERT(bno < agf->agf_length);
			bno = xfs_alloc_rcheck_btree_block(cur, agf->agf_seqno,
				bno, (xfs_agblock_t *)0, rp, level);
		}
	}
}

/*
 * Check one btree block for record consistency.
 */
STATIC xfs_agblock_t			/* next block to process */
xfs_alloc_rcheck_btree_block(
	xfs_btree_cur_t		*cur,	/* btree cursor */
	xfs_agnumber_t		agno,	/* allocation group number */
	xfs_agblock_t		bno,	/* block number to check */
	xfs_agblock_t		*fbno,	/* output: first block at next level */
	void			*rec,	/* previous record/key value */
	int			level)	/* level of this block */
{
	xfs_alloc_block_t	*block;
	buf_t			*bp;
	int			i;
	xfs_alloc_key_t		*keyp;
	xfs_alloc_key_t		*kp;
	xfs_alloc_ptr_t		*pp;
	xfs_agblock_t		rbno;
	xfs_alloc_rec_t		*recp;
	xfs_alloc_rec_t		*rp;

	bp = xfs_btree_read_bufs(cur->bc_mp, cur->bc_tp, agno, bno, 0);
	block = XFS_BUF_TO_ALLOC_BLOCK(bp);
	xfs_btree_check_sblock(cur, block, level);
	if (fbno && block->bb_numrecs) {
		if (level > 0)
			*fbno = *XFS_ALLOC_PTR_ADDR(block, 1, cur);
		else
			*fbno = NULLAGBLOCK;
	}
	rbno = block->bb_rightsib;
	if (level > 0)
		keyp = (xfs_alloc_key_t *)rec;
	else
		recp = (xfs_alloc_rec_t *)rec;
	for (i = 1; i <= block->bb_numrecs; i++) {
		if (level > 0) {
			kp = XFS_ALLOC_KEY_ADDR(block, i, cur);
			if (i == 1 && !fbno)
				xfs_btree_check_key(cur->bc_btnum, keyp, kp);
			else if (i > 1) {
				xfs_btree_check_key(cur->bc_btnum,
					(void *)(kp - 1), (void *)kp);
				if (i == block->bb_numrecs)
					*keyp = *kp;
			}
			pp = XFS_ALLOC_PTR_ADDR(block, i, cur);
			xfs_btree_check_sptr(cur, *pp, level);
		} else {
			rp = XFS_ALLOC_REC_ADDR(block, i, cur);
			if (i == 1 && !fbno)
				xfs_btree_check_rec(cur->bc_btnum, (void *)recp,
					(void *)rp);
			else if (i > 1) {
				xfs_btree_check_rec(cur->bc_btnum,
					(void *)(rp - 1), (void *)rp);
				if (i == block->bb_numrecs)
					*recp = *rp;
			}
		}
	}
	xfs_trans_brelse(cur->bc_tp, bp);
	return rbno;
}
#endif

/*
 * Move 1 record right from cur/level if possible.
 * Update cur to reflect the new path.
 */
STATIC int				/* success/failure */
xfs_alloc_rshift(
	xfs_btree_cur_t		*cur,	/* btree cursor */
	int			level)	/* level to shift record on */
{
	int			i;
	xfs_alloc_key_t		key;
	buf_t			*lbp;
	xfs_alloc_block_t	*left;
	xfs_alloc_key_t		*lkp;
	xfs_alloc_ptr_t		*lpp;
	xfs_alloc_rec_t		*lrp;
	buf_t			*rbp;
	xfs_alloc_block_t	*right;
	xfs_alloc_key_t		*rkp;
	xfs_alloc_ptr_t		*rpp;
	xfs_alloc_rec_t		*rrp;
	xfs_btree_cur_t		*tcur;

	xfs_alloc_rcheck(cur);
	lbp = cur->bc_bufs[level];
	left = XFS_BUF_TO_ALLOC_BLOCK(lbp);
	xfs_btree_check_sblock(cur, left, level);
	if (left->bb_rightsib == NULLAGBLOCK)
		return 0;
	if (cur->bc_ptrs[level] >= left->bb_numrecs)
		return 0;
	rbp = xfs_btree_read_bufs(cur->bc_mp, cur->bc_tp,
		cur->bc_private.a.agno, left->bb_rightsib, 0);
	right = XFS_BUF_TO_ALLOC_BLOCK(rbp);
	xfs_btree_check_sblock(cur, right, level);
	if (right->bb_numrecs == XFS_ALLOC_BLOCK_MAXRECS(level, cur)) {
		kmem_check();
		return 0;
	}
	if (level > 0) {
		lkp = XFS_ALLOC_KEY_ADDR(left, left->bb_numrecs, cur);
		lpp = XFS_ALLOC_PTR_ADDR(left, left->bb_numrecs, cur);
		rkp = XFS_ALLOC_KEY_ADDR(right, 1, cur);
		rpp = XFS_ALLOC_PTR_ADDR(right, 1, cur);
		for (i = right->bb_numrecs - 1; i >= 0; i--) {
			rkp[i + 1] = rkp[i];
			xfs_btree_check_sptr(cur, rpp[i], level);
			rpp[i + 1] = rpp[i];
		}
		xfs_btree_check_sptr(cur, *lpp, level);
		*rkp = *lkp;
		*rpp = *lpp;
		xfs_alloc_log_keys(cur, rbp, 1, right->bb_numrecs + 1);
		xfs_alloc_log_ptrs(cur, rbp, 1, right->bb_numrecs + 1);
	} else {
		lrp = XFS_ALLOC_REC_ADDR(left, left->bb_numrecs, cur);
		rrp = XFS_ALLOC_REC_ADDR(right, 1, cur);
		for (i = right->bb_numrecs - 1; i >= 0; i--)
			rrp[i + 1] = rrp[i];
		*rrp = *lrp;
		xfs_alloc_log_recs(cur, rbp, 1, right->bb_numrecs + 1);
		key.ar_startblock = rrp->ar_startblock;
		key.ar_blockcount = rrp->ar_blockcount;
		rkp = &key;
	}
	left->bb_numrecs--;
	xfs_alloc_log_block(cur->bc_tp, lbp, XFS_BB_NUMRECS);
	right->bb_numrecs++;
#ifdef XFSDEBUG
	if (level > 0)
		xfs_btree_check_key(cur->bc_btnum, rkp, rkp + 1);
	else
		xfs_btree_check_rec(cur->bc_btnum, rrp, rrp + 1);
#endif
	xfs_alloc_log_block(cur->bc_tp, rbp, XFS_BB_NUMRECS);
	tcur = xfs_btree_dup_cursor(cur);
	xfs_btree_lastrec(tcur, level);
	xfs_alloc_increment(tcur, level);
	xfs_alloc_updkey(tcur, rkp, level + 1);
	xfs_btree_del_cursor(tcur);
	xfs_alloc_rcheck(cur);
	kmem_check();
	return 1;
}

/*
 * Split cur/level block in half.
 * Return new block number and its first record (to be inserted into parent).
 */
STATIC int				/* success/failure */
xfs_alloc_split(
	xfs_btree_cur_t		*cur,	/* btree cursor */
	int			level,	/* level to split */
	xfs_agblock_t		*bnop,	/* output: block number allocated */
	xfs_alloc_key_t		*keyp,	/* output: first key of new block */
	xfs_btree_cur_t		**curp)	/* output: new cursor */
{
	buf_t			*agbp;
	xfs_agf_t		*agf;
	daddr_t			d;
	int			i;
	xfs_alloc_key_t		key;
	xfs_agblock_t		lbno;
	buf_t			*lbp;
	xfs_alloc_block_t	*left;
	xfs_alloc_key_t		*lkp;
	xfs_alloc_ptr_t		*lpp;
	xfs_alloc_rec_t		*lrp;
	xfs_agblock_t		rbno;
	buf_t			*rbp;
	xfs_alloc_rec_t		rec;
	xfs_alloc_block_t	*right;
	xfs_alloc_key_t		*rkp;
	xfs_alloc_ptr_t		*rpp;
	xfs_alloc_block_t	*rrblock;
	buf_t			*rrbp;
	xfs_alloc_rec_t		*rrp;

	xfs_alloc_rcheck(cur);
	agbp = cur->bc_private.a.agbp;
	agf = XFS_BUF_TO_AGF(agbp);
	rbno = xfs_alloc_get_freelist(cur->bc_tp, agbp, &rbp);
	if (rbno == NULLAGBLOCK) {
		kmem_check();
		return 0;
	}
	right = XFS_BUF_TO_ALLOC_BLOCK(rbp);
	lbp = cur->bc_bufs[level];
	left = XFS_BUF_TO_ALLOC_BLOCK(lbp);
	xfs_btree_check_sblock(cur, left, level);
	right->bb_magic = xfs_magics[cur->bc_btnum];
	right->bb_level = left->bb_level;
	right->bb_numrecs = (__uint16_t)(left->bb_numrecs / 2);
	if ((left->bb_numrecs & 1) &&
	    cur->bc_ptrs[level] <= right->bb_numrecs + 1)
		right->bb_numrecs++;
	i = left->bb_numrecs - right->bb_numrecs + 1;
	if (level > 0) {
		lkp = XFS_ALLOC_KEY_ADDR(left, i, cur);
		lpp = XFS_ALLOC_PTR_ADDR(left, i, cur);
		rkp = XFS_ALLOC_KEY_ADDR(right, 1, cur);
		rpp = XFS_ALLOC_PTR_ADDR(right, 1, cur);
		for (i = 0; i < right->bb_numrecs; i++) {
			rkp[i] = lkp[i];
			xfs_btree_check_sptr(cur, lpp[i], level);
			rpp[i] = lpp[i];
		}
		xfs_alloc_log_keys(cur, rbp, 1, right->bb_numrecs);
		xfs_alloc_log_ptrs(cur, rbp, 1, right->bb_numrecs);
		*keyp = *rkp;
	} else {
		lrp = XFS_ALLOC_REC_ADDR(left, i, cur);
		rrp = XFS_ALLOC_REC_ADDR(right, 1, cur);
		for (i = 0; i < right->bb_numrecs; i++)
			rrp[i] = lrp[i];
		xfs_alloc_log_recs(cur, rbp, 1, right->bb_numrecs);
		keyp->ar_startblock = rrp->ar_startblock;
		keyp->ar_blockcount = rrp->ar_blockcount;
	}
	d = lbp->b_blkno;
	lbno = XFS_DADDR_TO_AGBNO(cur->bc_mp, d);
	left->bb_numrecs -= right->bb_numrecs;
	right->bb_rightsib = left->bb_rightsib;
	left->bb_rightsib = rbno;
	right->bb_leftsib = lbno;
	xfs_alloc_log_block(cur->bc_tp, rbp, XFS_BB_ALL_BITS);
	xfs_alloc_log_block(cur->bc_tp, lbp, XFS_BB_NUMRECS | XFS_BB_RIGHTSIB);
	if (right->bb_rightsib != NULLAGBLOCK) {
		rrbp = xfs_btree_read_bufs(cur->bc_mp, cur->bc_tp,
			cur->bc_private.a.agno, right->bb_rightsib, 0);
		rrblock = XFS_BUF_TO_ALLOC_BLOCK(rrbp);
		xfs_btree_check_sblock(cur, rrblock, level);
		rrblock->bb_leftsib = rbno;
		xfs_alloc_log_block(cur->bc_tp, rrbp, XFS_BB_LEFTSIB);
	}
	if (cur->bc_ptrs[level] > left->bb_numrecs + 1) {
		xfs_btree_setbuf(cur, level, rbp);
		cur->bc_ptrs[level] -= left->bb_numrecs;
	}
	if (level + 1 < cur->bc_nlevels) {
		*curp = xfs_btree_dup_cursor(cur);
		(*curp)->bc_ptrs[level + 1]++;
	}
	*bnop = rbno;
	xfs_alloc_rcheck(cur);
	kmem_check();
	return 1;
}

/*
 * Update keys at all levels from here to the root along the cursor's path.
 */
STATIC void
xfs_alloc_updkey(
	xfs_btree_cur_t		*cur,	/* btree cursor */
	xfs_alloc_key_t		*keyp,	/* new key value to update to */
	int			level)	/* starting level for update */
{
	xfs_alloc_block_t	*block;	/* btree block */
	buf_t			*bp;	/* buffer for block */
	xfs_alloc_key_t		*kp;	/* pointer to btree block keys */
	int			ptr;	/* index of key in block */

	xfs_alloc_rcheck(cur);
	for (ptr = 1; ptr == 1 && level < cur->bc_nlevels; level++) {
		bp = cur->bc_bufs[level];
		block = XFS_BUF_TO_ALLOC_BLOCK(bp);
		xfs_btree_check_sblock(cur, block, level);
		ptr = cur->bc_ptrs[level];
		kp = XFS_ALLOC_KEY_ADDR(block, ptr, cur);
		*kp = *keyp;
		xfs_alloc_log_keys(cur, bp, ptr, ptr);
	}
	xfs_alloc_rcheck(cur);
	kmem_check();
}

/*
 * Externally visible routines.
 */

/*
 * Decrement cursor by one record at the level.
 * For nonzero levels the leaf-ward information is untouched.
 */
int					/* success/failure */
xfs_alloc_decrement(
	xfs_btree_cur_t		*cur,	/* btree cursor */
	int			level)	/* level in btree, 0 is leaf */
{
	xfs_alloc_block_t	*block;	/* btree block */
	buf_t			*bp;	/* buffer containing btree block */
	int			lev;	/* btree level */

	/*
	 * Decrement the ptr at this level.  If we're still in the block
	 * then we're done.
	 */
	if (--cur->bc_ptrs[level] > 0)
		return 1;
	/*
	 * Get a pointer to the btree block.
	 */
	bp = cur->bc_bufs[level];
	block = XFS_BUF_TO_ALLOC_BLOCK(bp);
	xfs_btree_check_sblock(cur, block, level);
	/*
	 * If we just went off the left edge of the tree, return failure.
	 */
	if (block->bb_leftsib == NULLAGBLOCK)
		return 0;
	/*
	 * March up the tree decrementing pointers.
	 * Stop when we don't go off the left edge of a block.
	 */
	for (lev = level + 1; lev < cur->bc_nlevels; lev++) {
		if (--cur->bc_ptrs[lev] > 0)
			break;
	}
	/*
	 * If we went off the root then we are seriously confused.
	 */
	ASSERT(lev < cur->bc_nlevels);
	/*
	 * Now walk back down the tree, fixing up the cursor's buffer
	 * pointers and key numbers.
	 */
	for (; lev > level; lev--) {
		xfs_agblock_t	agbno;	/* block number of btree block */

		bp = cur->bc_bufs[lev];
		block = XFS_BUF_TO_ALLOC_BLOCK(bp);
		xfs_btree_check_sblock(cur, block, lev);
		agbno = *XFS_ALLOC_PTR_ADDR(block, cur->bc_ptrs[lev], cur);
		bp = xfs_btree_read_bufs(cur->bc_mp, cur->bc_tp,
			cur->bc_private.a.agno, agbno, 0);
		xfs_btree_setbuf(cur, lev - 1, bp);
		block = XFS_BUF_TO_ALLOC_BLOCK(bp);
		xfs_btree_check_sblock(cur, block, lev - 1);
		cur->bc_ptrs[lev - 1] = block->bb_numrecs;
	}
	kmem_check();
	return 1;
}

/*
 * Delete the record pointed to by cur.
 * The cursor refers to the place where the record was (could be inserted)
 * when the operation returns.
 */
int					/* success/failure */
xfs_alloc_delete(
	xfs_btree_cur_t	*cur)		/* btree cursor */
{
	int		i;		/* result code */
	int		level;		/* btree level */

	/*
	 * Go up the tree, starting at leaf level.
	 * If 2 is returned then a join was done; go to the next level.
	 * Otherwise we are done.
	 */
	for (level = 0, i = 2; i == 2; level++)
		i = xfs_alloc_delrec(cur, level);
	xfs_alloc_kcheck(cur);
	return i;
}

/* 
 * Get the data from the pointed-to record.
 */
int					/* success/failure */
xfs_alloc_get_rec(
	xfs_btree_cur_t		*cur,	/* btree cursor */
	xfs_agblock_t		*bno,	/* output: starting block of extent */
	xfs_extlen_t		*len)	/* output: length of extent */
{
	xfs_alloc_block_t	*block;	/* btree block */
	buf_t			*bp;	/* buffer containing btree block */
	int			ptr;	/* record number */
	xfs_alloc_rec_t		*rec;	/* record data */

	bp = cur->bc_bufs[0];
	ptr = cur->bc_ptrs[0];
	block = XFS_BUF_TO_ALLOC_BLOCK(bp);
	xfs_btree_check_sblock(cur, block, 0);
	/*
	 * Off the right end or left end, return failure.
	 */
	if (ptr > block->bb_numrecs || ptr <= 0)
		return 0;
	/*
	 * Point to the record and extract its data.
	 */
	rec = XFS_ALLOC_REC_ADDR(block, ptr, cur);
	*bno = rec->ar_startblock;
	*len = rec->ar_blockcount;
	kmem_check();
	return 1;
}

/*
 * Increment cursor by one record at the level.
 * For nonzero levels the leaf-ward information is untouched.
 */
int					/* success/failure */
xfs_alloc_increment(
	xfs_btree_cur_t		*cur,	/* btree cursor */
	int			level)	/* level in btree, 0 is leaf */
{
	xfs_alloc_block_t	*block;	/* btree block */
	buf_t			*bp;	/* buffer containing btree block */
	int			lev;	/* btree level */

	/*
	 * Get a pointer to the btree block.
	 */
	bp = cur->bc_bufs[level];
	block = XFS_BUF_TO_ALLOC_BLOCK(bp);
	xfs_btree_check_sblock(cur, block, level);
	/*
	 * Increment the ptr at this level.  If we're still in the block
	 * then we're done.
	 */
	if (++cur->bc_ptrs[level] <= block->bb_numrecs)
		return 1;
	/*
	 * If we just went off the right edge of the tree, return failure.
	 */
	if (block->bb_rightsib == NULLAGBLOCK)
		return 0;
	/*
	 * March up the tree incrementing pointers.
	 * Stop when we don't go off the right edge of a block.
	 */
	for (lev = level + 1; lev < cur->bc_nlevels; lev++) {
		bp = cur->bc_bufs[lev];
		block = XFS_BUF_TO_ALLOC_BLOCK(bp);
		xfs_btree_check_sblock(cur, block, lev);
		if (++cur->bc_ptrs[lev] <= block->bb_numrecs)
			break;
	}
	/*
	 * If we went off the root then we are seriously confused.
	 */
	ASSERT(lev < cur->bc_nlevels);
	/*
	 * Now walk back down the tree, fixing up the cursor's buffer
	 * pointers and key numbers.
	 */
	for (; lev > level; lev--) {
		xfs_agblock_t	agbno;	/* block number of btree block */

		bp = cur->bc_bufs[lev];
		block = XFS_BUF_TO_ALLOC_BLOCK(bp);
		xfs_btree_check_sblock(cur, block, lev);
		agbno = *XFS_ALLOC_PTR_ADDR(block, cur->bc_ptrs[lev], cur);
		bp = xfs_btree_read_bufs(cur->bc_mp, cur->bc_tp,
			cur->bc_private.a.agno, agbno, 0);
		xfs_btree_setbuf(cur, lev - 1, bp);
		cur->bc_ptrs[lev - 1] = 1;
	}
	kmem_check();
	return 1;
}

/*
 * Insert the current record at the point referenced by cur.
 * The cursor may be inconsistent on return if splits have been done.
 */
int					/* success/failure */
xfs_alloc_insert(
	xfs_btree_cur_t	*cur)		/* btree cursor */
{
	int		i;		/* result value, 0 for failure */
	int		level;		/* current level number in btree */
	xfs_agblock_t	nbno;		/* new block number (split result) */
	xfs_btree_cur_t	*ncur;		/* new cursor (split result) */
	xfs_alloc_rec_t	nrec;		/* record being inserted this level */
	xfs_btree_cur_t	*pcur;		/* previous level's cursor */

	level = 0;
	nbno = NULLAGBLOCK;
	nrec = cur->bc_rec.a;
	ncur = (xfs_btree_cur_t *)0;
	pcur = cur;
	/*
	 * Loop going up the tree, starting at the leaf level.
	 * Stop when we don't get a split block, that must mean that
	 * the insert is finished with this level.
	 */
	do {
		/*
		 * Insert nrec/nbno into this level of the tree.
		 * Note if we fail, nbno will be null.
		 */
		i = xfs_alloc_insrec(pcur, level++, &nbno, &nrec, &ncur);
		/*
		 * See if the cursor we just used is trash.
		 * Can't trash the caller's cursor, but otherwise we should
		 * if ncur is a new cursor or we're about to be done.
		 */
		if (pcur != cur && (ncur || nbno == NULLAGBLOCK)) {
			cur->bc_nlevels = pcur->bc_nlevels;
			xfs_btree_del_cursor(pcur);
		}
		/*
		 * If we got a new cursor, switch to it.
		 */
		if (ncur) {
			pcur = ncur;
			ncur = (xfs_btree_cur_t *)0;
		}
		kmem_check();
	} while (nbno != NULLAGBLOCK);
	return i;
}

#ifdef XFSDEBUG
/*
 * Check key consistency in the btree given by cur.
 */
void
xfs_alloc_kcheck(
	xfs_btree_cur_t	*cur)		/* btree cursor */
{
	buf_t		*agbp;
	xfs_agf_t	*agf;
	xfs_agblock_t	bno;
	int		levels;

	agbp = cur->bc_private.a.agbp;
	agf = XFS_BUF_TO_AGF(agbp);
	bno = agf->agf_roots[cur->bc_btnum];
	levels = agf->agf_levels[cur->bc_btnum];
	ASSERT(levels == cur->bc_nlevels);
	xfs_alloc_kcheck_btree(cur, agf, bno, levels - 1, (xfs_alloc_key_t *)0);
}
#endif	/* XFSDEBUG */

/*
 * Lookup the record equal to [bno, len] in the btree given by cur.
 */
int					/* success/failure */
xfs_alloc_lookup_eq(
	xfs_btree_cur_t	*cur,		/* btree cursor */
	xfs_agblock_t	bno,		/* starting block of extent */
	xfs_extlen_t	len)		/* length of extent */
{
	cur->bc_rec.a.ar_startblock = bno;
	cur->bc_rec.a.ar_blockcount = len;
	return xfs_alloc_lookup(cur, XFS_LOOKUP_EQ);
}

/*
 * Lookup the first record greater than or equal to [bno, len]
 * in the btree given by cur.
 */
int					/* success/failure */
xfs_alloc_lookup_ge(
	xfs_btree_cur_t	*cur,		/* btree cursor */
	xfs_agblock_t	bno,		/* starting block of extent */
	xfs_extlen_t	len)		/* length of extent */
{
	cur->bc_rec.a.ar_startblock = bno;
	cur->bc_rec.a.ar_blockcount = len;
	return xfs_alloc_lookup(cur, XFS_LOOKUP_GE);
}

/*
 * Lookup the first record less than or equal to [bno, len]
 * in the btree given by cur.
 */
int					/* success/failure */
xfs_alloc_lookup_le(
	xfs_btree_cur_t	*cur,		/* btree cursor */
	xfs_agblock_t	bno,		/* starting block of extent */
	xfs_extlen_t	len)		/* length of extent */
{
	cur->bc_rec.a.ar_startblock = bno;
	cur->bc_rec.a.ar_blockcount = len;
	return xfs_alloc_lookup(cur, XFS_LOOKUP_LE);
}

#ifdef XFSDEBUG
/*
 * Check consistency in the given btree.
 * Checks header consistency and that keys/records are in the right order.
 */
void
xfs_alloc_rcheck(
	xfs_btree_cur_t	*cur)		/* btree cursor */
{
	buf_t		*agbp;
	xfs_agf_t	*agf;
	xfs_agblock_t	bno;
	int		levels;

	agbp = cur->bc_private.a.agbp;
	agf = XFS_BUF_TO_AGF(agbp);
	bno = agf->agf_roots[cur->bc_btnum];
	levels = agf->agf_levels[cur->bc_btnum];
	xfs_alloc_rcheck_btree(cur, agf, bno, levels);
}
#endif	/* XFSDEBUG */

/*
 * Update the record referred to by cur, to the value given by [bno, len].
 */
int					/* success/failure */
xfs_alloc_update(
	xfs_btree_cur_t		*cur,	/* btree cursor */
	xfs_agblock_t		bno,	/* starting block of extent */
	xfs_extlen_t		len)	/* length of extent */
{
	buf_t			*agbp;	/* a.g. freespace header buffer */
	xfs_agf_t		*agf;	/* a.g. freespace header */
	xfs_alloc_block_t	*block;	/* btree block to update */
	buf_t			*bp;	/* buffer containing btree block */
	xfs_alloc_key_t		key;	/* key containing [bno, len] */
	int			ptr;	/* current record number (updating) */
	xfs_alloc_rec_t		*rp;	/* pointer to updated record */

	/*
	 * Pick up the a.g. freelist struct and the current block.
	 */
	agbp = cur->bc_private.a.agbp;
	agf = XFS_BUF_TO_AGF(agbp);
	xfs_alloc_rcheck(cur);
	bp = cur->bc_bufs[0];
	block = XFS_BUF_TO_ALLOC_BLOCK(bp);
	xfs_btree_check_sblock(cur, block, 0);
	/*
	 * Get the address of the rec to be updated.
	 */
	ptr = cur->bc_ptrs[0];
	rp = XFS_ALLOC_REC_ADDR(block, ptr, cur);
	/*
	 * Fill in the new contents and log them.
	 */
	rp->ar_startblock = bno;
	rp->ar_blockcount = len;
	xfs_alloc_log_recs(cur, bp, ptr, ptr);
	/*
	 * If it's the by-size btree and it's the last leaf block and
	 * it's the last record... then update the size of the longest
	 * extent in the a.g., which we cache in the a.g. freelist header.
	 */
	if (cur->bc_btnum == XFS_BTNUM_CNT &&
	    block->bb_rightsib == NULLAGBLOCK &&
	    ptr == block->bb_numrecs) {
		agf->agf_longest = len;
		xfs_alloc_log_agf(cur->bc_tp, agbp, XFS_AGF_LONGEST);
	}
	/*
	 * If updating record that isn't the first one in its leaf block, done.
	 */
	if (ptr > 1) {
		kmem_check();
		return 1;
	}
	/*
	 * Updating first record in leaf.  Pass new key value up to our parent.
	 */
	key.ar_startblock = bno;
	key.ar_blockcount = len;
	xfs_alloc_updkey(cur, &key, 1);
	xfs_alloc_rcheck(cur);
	xfs_alloc_kcheck(cur);
	kmem_check();
	return 1;
}
