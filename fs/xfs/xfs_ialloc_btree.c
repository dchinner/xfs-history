#ident	"$Revision: 1.19 $"

/*
 * Inode allocation management for xFS.
 */

#ifdef SIM
#define _KERNEL 1
#endif
#include <sys/param.h>
#include <sys/buf.h>
#ifdef SIM
#undef _KERNEL
#endif
#include <sys/vnode.h>
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
#include "xfs_bmap_btree.h"
#include "xfs_ialloc_btree.h"
#include "xfs_btree.h"
#include "xfs_ialloc.h"
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

#ifdef _NOTYET_
/*
 * Single level of the xfs_inobt_delete record deletion routine.
 * Delete record pointed to by cur/level.
 * Remove the record from its block then rebalance the tree.
 * Return 0 for error, 1 for done, 2 to go on to the next level.
 */
STATIC int				/* fail/done/go-on */
xfs_inobt_delrec(
	xfs_btree_cur_t	*cur,		/* btree cursor */
	int		level);		/* level removing record from */
#endif	/* _NOTYET_ */

/*
 * Insert one record/level.  Return information to the caller
 * allowing the next level up to proceed if necessary.
 */
STATIC int				/* success/failure */
xfs_inobt_insrec(
	xfs_btree_cur_t	*cur,		/* btree cursor */
	int		level,		/* level to insert record at */
	xfs_agblock_t	*bnop,		/* i/o: block number inserted */
	xfs_inobt_rec_t	*recp,		/* i/o: record data inserted */
	xfs_btree_cur_t	**curp);	/* output: new cursor replacing cur */

#ifdef XFSDEBUG
/*
 * Check key consistency in one btree level, then recurse down to the next.
 */
STATIC void
xfs_inobt_kcheck_btree(
	xfs_btree_cur_t	*cur,		/* btree cursor */
	xfs_agi_t	*agi,		/* a.g. inode header */
	xfs_agblock_t	bno,		/* block number to check */
	int		level,		/* level of the block */
	xfs_inobt_key_t	*keyp);		/* value of expected first key */
#endif	/* XFSDEBUG */

/*
 * Log header fields from a btree block.
 */
STATIC void
xfs_inobt_log_block(
	xfs_trans_t	*tp,		/* transaction pointer */
	buf_t		*bp,		/* buffer containing btree block */
	int		fields);	/* mask of fields: XFS_BB_... */

/*
 * Log keys from a btree block (nonleaf).
 */
STATIC void
xfs_inobt_log_keys(
	xfs_btree_cur_t	*cur,		/* btree cursor */
	buf_t		*bp,		/* buffer containing btree block */
	int		kfirst,		/* index of first key to log */
	int		klast);		/* index of last key to log */

/*
 * Log block pointer fields from a btree block (nonleaf).
 */
STATIC void
xfs_inobt_log_ptrs(
	xfs_btree_cur_t	*cur,		/* btree cursor */
	buf_t		*bp,		/* buffer containing btree block */
	int		pfirst,		/* index of first pointer to log */
	int		plast);		/* index of last pointer to log */

/*
 * Log records from a btree block (leaf).
 */
STATIC void
xfs_inobt_log_recs(
	xfs_btree_cur_t	*cur,		/* btree cursor */
	buf_t		*bp,		/* buffer containing btree block */
	int		rfirst,		/* index of first record to log */
	int		rlast);		/* index of last record to log */

/*
 * Lookup the record.  The cursor is made to point to it, based on dir.
 * Return 0 if can't find any such record, 1 for success.
 */
STATIC int				/* success/failure */
xfs_inobt_lookup(
	xfs_btree_cur_t	*cur,		/* btree cursor */
	xfs_lookup_t	dir);		/* <=, ==, or >= */

/*
 * Move 1 record left from cur/level if possible.
 * Update cur to reflect the new path.
 */
STATIC int				/* success/failure */
xfs_inobt_lshift(
	xfs_btree_cur_t	*cur,		/* btree cursor */
	int		level);		/* level to shift record on */

/*
 * Allocate a new root block, fill it in.
 */
STATIC int				/* success/failure */
xfs_inobt_newroot(
	xfs_btree_cur_t	*cur);		/* btree cursor */

#ifdef XFSDEBUG
/*
 * Guts of xfs_inobt_rcheck.  For each level in the btree, check
 * all its blocks for consistency.
 */
STATIC void
xfs_inobt_rcheck_btree(
	xfs_btree_cur_t	*cur,		/* btree cursor */
	xfs_agi_t	*agi,		/* a.g. inode header */
	xfs_agblock_t	rbno,		/* root block number */
	int		levels);	/* number of levels in the btree */
#endif	/* XFSDEBUG */

#ifdef XFSDEBUG
/*
 * Check one btree block for record consistency.
 */
STATIC xfs_agblock_t			/* next block to process */
xfs_inobt_rcheck_btree_block(
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
xfs_inobt_rshift(
	xfs_btree_cur_t	*cur,		/* btree cursor */
	int		level);		/* level to shift record on */

/*
 * Split cur/level block in half.
 * Return new block number and its first record (to be inserted into parent).
 * Also returns a new cursor in this case, for next level of insert to use.
 */
STATIC int				/* success/failure */
xfs_inobt_split(
	xfs_btree_cur_t	*cur,		/* btree cursor */
	int		level,		/* level to split */
	xfs_agblock_t	*bnop,		/* output: block number allocated */
	xfs_inobt_key_t	*keyp,		/* output: first key of new block */
	xfs_btree_cur_t	**curp);	/* output: new cursor */

/*
 * Update keys at all levels from here to the root along the cursor's path.
 */
STATIC void
xfs_inobt_updkey(
	xfs_btree_cur_t	*cur,		/* btree cursor */
	xfs_inobt_key_t	*keyp,		/* new key value to update to */
	int		level);		/* starting level for update */

/*
 * Internal functions.
 */

#ifdef _NOTYET_
/*
 * Single level of the xfs_inobt_delete record deletion routine.
 * Delete record pointed to by cur/level.
 * Remove the record from its block then rebalance the tree.
 * Return 0 for error, 1 for done, 2 to go on to the next level.
 */
STATIC int				/* fail/done/go-on */
xfs_inobt_delrec(
	xfs_btree_cur_t		*cur,	/* btree cursor */
	int			level)	/* level removing record from */
{
	buf_t			*agbp;	/* buffer for a.g. inode header */
	xfs_agnumber_t		agfbno;	/* agf block of freed btree block */
	buf_t			*agfbp;	/* bp of agf block of freed block */
	xfs_agi_t		*agi;	/* allocation group inode header */
	xfs_inobt_block_t	*block;	/* btree block record/key lives in */
	xfs_agblock_t		bno;	/* btree block number */
	buf_t			*bp;	/* buffer for block */
	int			i;	/* loop index */
	xfs_inobt_key_t		key;	/* kp points here if block is level 0 */
	xfs_inobt_key_t		*kp;	/* pointer to btree keys */
	xfs_agblock_t		lbno;	/* left block's block number */
	buf_t			*lbp;	/* left block's buffer pointer */
	xfs_inobt_block_t	*left;	/* left btree block */
	xfs_inobt_key_t		*lkp;	/* left block key pointer */
	xfs_inobt_ptr_t		*lpp;	/* left block address pointer */
	int			lrecs;	/* number of records in left block */
	xfs_inobt_rec_t		*lrp;	/* left block record pointer */
	xfs_inobt_ptr_t		*pp;	/* pointer to btree addresses */
	int			ptr;	/* index in btree block for this rec */
	xfs_agblock_t		rbno;	/* right block's block number */
	buf_t			*rbp;	/* right block's buffer pointer */
	xfs_inobt_block_t	*right;	/* right btree block */
	xfs_inobt_key_t		*rkp;	/* right block key pointer */
	xfs_inobt_rec_t		*rp;	/* pointer to btree records */
	xfs_inobt_ptr_t		*rpp;	/* right block address pointer */
	int			rrecs;	/* number of records in right block */
	xfs_inobt_rec_t		*rrp;	/* right block record pointer */
	xfs_btree_cur_t		*tcur;	/* temporary btree cursor */

	xfs_inobt_rcheck(cur);
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
	block = XFS_BUF_TO_INOBT_BLOCK(bp);
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
		kp = XFS_INOBT_KEY_ADDR(block, 1, cur);
		pp = XFS_INOBT_PTR_ADDR(block, 1, cur);
#ifdef DEBUG
		for (i = ptr; i < block->bb_numrecs; i++)
			xfs_btree_check_sptr(cur, pp[i], level);
#endif
		if (ptr < block->bb_numrecs) {
			ovbcopy(&kp[ptr], &kp[ptr - 1],
				(block->bb_numrecs - ptr) * sizeof(*kp));
			ovbcopy(&pp[ptr], &pp[ptr - 1],
				(block->bb_numrecs - ptr) * sizeof(*pp));
			xfs_inobt_log_keys(cur, bp, ptr, block->bb_numrecs - 1);
			xfs_inobt_log_ptrs(cur, bp, ptr, block->bb_numrecs - 1);
		}
	}
	/*
	 * It's a leaf.  Excise the record being deleted, by sliding the
	 * entries past it down one.  Log the changed areas of the block.
	 */
	else {
		rp = XFS_INOBT_REC_ADDR(block, 1, cur);
		if (ptr < block->bb_numrecs) {
			ovbcopy(&rp[ptr], &rp[ptr - 1],
				(block->bb_numrecs - ptr) * sizeof(*rp));
			xfs_inobt_log_recs(cur, bp, ptr, block->bb_numrecs - 1);
		}
		/*
		 * If it's the first record in the block, we'll need a key
		 * structure to pass up to the next level (updkey).
		 */
		if (ptr == 1) {
			key.ir_startino = rp->ir_startino;
			kp = &key;
		}
	}
	/*
	 * Decrement and log the number of entries in the block.
	 */
	block->bb_numrecs--;
	xfs_inobt_log_block(cur->bc_tp, bp, XFS_BB_NUMRECS);
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
			agbp = cur->bc_private.i.agbp;
			agi = XFS_BUF_TO_AGI(agbp);
			/*
			 * pp is still set to the first pointer in the block.
			 * Make it the new root of the btree.
			 */
			bno = agi->agi_root;
			agi->agi_root = *pp;
			agi->agi_level--;
			/*
			 * Free the block.
			 */
			xfs_free_extent(cur->bc_tp, bno, 1);
			xfs_trans_binval(cur->bc_tp, bp);
			xfs_ialloc_log_agi(cur->bc_tp, agbp,
				XFS_AGI_ROOT | XFS_AGI_LEVEL);
			/*
			 * Update the cursor so there's one fewer level.
			 */
			cur->bc_bufs[level] = NULL;
			cur->bc_nlevels--;
			/*
			 * To ensure that the freed block is not used for
			 * user data until this transactin is permanent,
			 * we lock the agf buffer for this ag until the
			 * transaction record makes it to the on-disk log.
			 */
			agfbno = XFS_AG_DADDR(cur->bc_mp,
					      cur->bc_private.i.agno,
					      XFS_AGF_DADDR);
			agfbp = xfs_trans_read_buf(cur->bc_tp,
						   cur->bc_mp->m_dev,
						   agfbno, 1, 0);
			ASSERT(!geterror(agfbp));
			xfs_trans_bhold_until_committed(cur->bc_tp, agfbp);
		}
		if (level > 0)
			xfs_inobt_decrement(cur, level);
		xfs_inobt_rcheck(cur);
		kmem_check();
		return 1;
	}
	/*
	 * If we deleted the leftmost entry in the block, update the
	 * key values above us in the tree.
	 */
	if (ptr == 1)
		xfs_inobt_updkey(cur, kp, level + 1);
	/*
	 * If the number of records remaining in the block is at least
	 * the minimum, we're done.
	 */
	if (block->bb_numrecs >= XFS_INOBT_BLOCK_MINRECS(level, cur)) {
		if (level > 0)
			xfs_inobt_decrement(cur, level);
		kmem_check();
		xfs_inobt_rcheck(cur);
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
		xfs_inobt_increment(tcur, level);
		xfs_btree_lastrec(tcur, level);
		/*
		 * Grab a pointer to the block.
		 */
		rbp = tcur->bc_bufs[level];
		right = XFS_BUF_TO_INOBT_BLOCK(rbp);
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
		     XFS_INOBT_BLOCK_MINRECS(level, cur) &&
		    xfs_inobt_lshift(tcur, level)) {
			ASSERT(block->bb_numrecs >=
			       XFS_INOBT_BLOCK_MINRECS(level, cur));
			xfs_btree_del_cursor(tcur);
			if (level > 0)
				xfs_inobt_decrement(cur, level);
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
			xfs_inobt_decrement(tcur, level);
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
		xfs_inobt_decrement(tcur, level);
		xfs_btree_firstrec(tcur, level);
		/*
		 * Grab a pointer to the block.
		 */
		lbp = tcur->bc_bufs[level];
		left = XFS_BUF_TO_INOBT_BLOCK(lbp);
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
		     XFS_INOBT_BLOCK_MINRECS(level, cur) &&
		    xfs_inobt_rshift(tcur, level)) {
			ASSERT(block->bb_numrecs >=
			       XFS_INOBT_BLOCK_MINRECS(level, cur));
			xfs_btree_del_cursor(tcur);
			if (level == 0)
				cur->bc_ptrs[0]++;
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
	    lrecs + block->bb_numrecs <= XFS_INOBT_BLOCK_MAXRECS(level, cur)) {
		/*
		 * Set "right" to be the starting block,
		 * "left" to be the left neighbor.
		 */
		rbno = bno;
		right = block;
		rbp = bp;
		lbp = xfs_btree_read_bufs(cur->bc_mp, cur->bc_tp,
			cur->bc_private.i.agno, lbno, 0);
		left = XFS_BUF_TO_INOBT_BLOCK(lbp);
		xfs_btree_check_sblock(cur, left, level);
	}
	/*
	 * If that won't work, see if we can join with the right neighbor block.
	 */
	else if (rbno != NULLAGBLOCK &&
		 rrecs + block->bb_numrecs <=
		  XFS_INOBT_BLOCK_MAXRECS(level, cur)) {
		/*
		 * Set "left" to be the starting block,
		 * "right" to be the right neighbor.
		 */
		lbno = bno;
		left = block;
		lbp = bp;
		rbp = xfs_btree_read_bufs(cur->bc_mp, cur->bc_tp,
			cur->bc_private.i.agno, rbno, 0);
		right = XFS_BUF_TO_INOBT_BLOCK(rbp);
		xfs_btree_check_sblock(cur, right, level);
	}
	/*
	 * Otherwise, we can't fix the imbalance.
	 * Just return.  This is probably a logic error, but it's not fatal.
	 */
	else {
		if (level > 0)
			xfs_inobt_decrement(cur, level);
		xfs_inobt_rcheck(cur);
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
		lkp = XFS_INOBT_KEY_ADDR(left, left->bb_numrecs + 1, cur);
		lpp = XFS_INOBT_PTR_ADDR(left, left->bb_numrecs + 1, cur);
		rkp = XFS_INOBT_KEY_ADDR(right, 1, cur);
		rpp = XFS_INOBT_PTR_ADDR(right, 1, cur);
#ifdef DEBUG
		for (i = 0; i < right->bb_numrecs; i++)
			xfs_btree_check_sptr(cur, rpp[i], level);
#endif
		bcopy(rkp, lkp, right->bb_numrecs * sizeof(*lkp));
		bcopy(rpp, lpp, right->bb_numrecs * sizeof(*lpp));
		xfs_inobt_log_keys(cur, lbp, left->bb_numrecs + 1,
				   left->bb_numrecs + right->bb_numrecs);
		xfs_inobt_log_ptrs(cur, lbp, left->bb_numrecs + 1,
				   left->bb_numrecs + right->bb_numrecs);
	} else {
		/*
		 * It's a leaf.  Move records.
		 */
		lrp = XFS_INOBT_REC_ADDR(left, left->bb_numrecs + 1, cur);
		rrp = XFS_INOBT_REC_ADDR(right, 1, cur);
		bcopy(rrp, lrp, right->bb_numrecs * sizeof(*lrp));
		xfs_inobt_log_recs(cur, lbp, left->bb_numrecs + 1,
				   left->bb_numrecs + right->bb_numrecs);
	}
	/*
	 * Fix up the number of records in the surviving block.
	 */
	left->bb_numrecs += right->bb_numrecs;
	/*
	 * Fix up the right block pointer in the surviving block, and log it.
	 */
	left->bb_rightsib = right->bb_rightsib;
	xfs_inobt_log_block(cur->bc_tp, lbp, XFS_BB_NUMRECS | XFS_BB_RIGHTSIB);
	/*
	 * If there is a right sibling now, make it point to the 
	 * remaining block.
	 */
	if (left->bb_rightsib != NULLAGBLOCK) {
		xfs_inobt_block_t	*rrblock;
		buf_t			*rrbp;

		rrbp = xfs_btree_read_bufs(cur->bc_mp, cur->bc_tp,
			cur->bc_private.i.agno, left->bb_rightsib, 0);
		rrblock = XFS_BUF_TO_INOBT_BLOCK(rrbp);
		xfs_btree_check_sblock(cur, rrblock, level);
		rrblock->bb_leftsib = lbno;
		xfs_inobt_log_block(cur->bc_tp, rrbp, XFS_BB_LEFTSIB);
	}
	/*
	 * Free the deleting block.
	 */
	xfs_free_extent(cur->bc_tp, rbno, 1);
	xfs_trans_binval(cur->bc_tp, rbp);
	/*
	 * To ensure that the freed block is not used for
	 * user data until this transaction is permanent,
	 * we lock the agf buffer for this ag until the
	 * transaction record makes it to the on-disk log.
	 */
	agfbno = XFS_AG_DADDR(cur->bc_mp, cur->bc_private.i.agno,
			      XFS_AGF_DADDR);
	agfbp = xfs_trans_read_buf(cur->bc_tp, cur->bc_mp->m_dev, agfbno,
				   1, 0);
	ASSERT(!geterror(agfbp));
	xfs_trans_bhold_until_committed(cur->bc_tp, agfbp);
	/*
	 * If we joined with the left neighbor, set the buffer in the
	 * cursor to the left block, and fix up the index.
	 */
	if (bp != lbp) {
		cur->bc_bufs[level] = NULL;
		cur->bc_ptrs[level] += left->bb_numrecs;
	}
	/*
	 * If we joined with the right neighbor and there's a level above
	 * us, increment the cursor at that level.
	 */
	else if (level + 1 < cur->bc_nlevels)
		xfs_inobt_increment(cur, level + 1);
	/*
	 * Readjust the ptr at this level if it's not a leaf, since it's
	 * still pointing at the deletion point, which makes the cursor
	 * inconsistent.  If this makes the ptr 0, the caller fixes it up.
	 * We can't use decrement because it would change the next level up.
	 */
	if (level > 0)
		cur->bc_ptrs[level]--;
	xfs_inobt_rcheck(cur);
	kmem_check();
	/* 
	 * Return value means the next level up has something to do.
	 */
	return 2;
}
#endif	/* _NOTYET_ */

/*
 * Insert one record/level.  Return information to the caller
 * allowing the next level up to proceed if necessary.
 */
STATIC int				/* success/failure */
xfs_inobt_insrec(
	xfs_btree_cur_t		*cur,	/* btree cursor */
	int			level,	/* level to insert record at */
	xfs_agblock_t		*bnop,	/* i/o: block number inserted */
	xfs_inobt_rec_t		*recp,	/* i/o: record data inserted */
	xfs_btree_cur_t		**curp)	/* output: new cursor replacing cur */
{
	xfs_inobt_block_t	*block;	/* btree block record/key lives in */
	buf_t			*bp;	/* buffer for block */
	int			i;	/* loop index */
	xfs_inobt_key_t		key;	/* key value being inserted */
	xfs_inobt_key_t		*kp;	/* pointer to btree keys */
	xfs_agblock_t		nbno;	/* block number of allocated block */
	xfs_btree_cur_t		*ncur;	/* new cursor to be used at next lvl */
	xfs_inobt_key_t		nkey;	/* new key value, from split */
	xfs_inobt_rec_t		nrec;	/* new record value, for caller */
	int			optr;	/* old ptr value */
	xfs_inobt_ptr_t		*pp;	/* pointer to btree addresses */
	int			ptr;	/* index in btree block for this rec */
	xfs_inobt_rec_t		*rp;	/* pointer to btree records */

	/*
	 * If we made it to the root level, allocate a new root block
	 * and we're done.
	 */
	if (level >= cur->bc_nlevels) {
		i = xfs_inobt_newroot(cur);
		*bnop = NULLAGBLOCK;
		kmem_check();
		return i;
	}
	xfs_inobt_rcheck(cur);
	/*
	 * Make a key out of the record data to be inserted, and save it.
	 */
	key.ir_startino = recp->ir_startino;
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
	block = XFS_BUF_TO_INOBT_BLOCK(bp);
	xfs_btree_check_sblock(cur, block, level);
#ifdef DEBUG
	/* 
	 * Check that the new entry is being inserted in the right place.
	 */
	if (ptr <= block->bb_numrecs) {
		if (level == 0) {
			rp = XFS_INOBT_REC_ADDR(block, ptr, cur);
			xfs_btree_check_rec(cur->bc_btnum, recp, rp);
		} else {
			kp = XFS_INOBT_KEY_ADDR(block, ptr, cur);
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
	if (block->bb_numrecs == XFS_INOBT_BLOCK_MAXRECS(level, cur)) {
		/*
		 * First, try shifting an entry to the right neighbor.
		 */
		if (xfs_inobt_rshift(cur, level)) {
			/* nothing */
		}
		/*
		 * Next, try shifting an entry to the left neighbor.
		 */
		else if (xfs_inobt_lshift(cur, level)) {
			optr = ptr = cur->bc_ptrs[level];
		}
		/*
		 * Next, try splitting the current block in half.
		 * If this works we have to re-set our variables because
		 * we could be in a different block now.
		 */
		else if (xfs_inobt_split(cur, level, &nbno, &nkey, &ncur)) {
			bp = cur->bc_bufs[level];
			block = XFS_BUF_TO_INOBT_BLOCK(bp);
			xfs_btree_check_sblock(cur, block, level);
			ptr = cur->bc_ptrs[level];
			nrec.ir_startino = nkey.ir_startino;
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
		kp = XFS_INOBT_KEY_ADDR(block, 1, cur);
		pp = XFS_INOBT_PTR_ADDR(block, 1, cur);
#ifdef DEBUG
		for (i = block->bb_numrecs; i >= ptr; i--)
			xfs_btree_check_sptr(cur, pp[i - 1], level);
#endif
		ovbcopy(&kp[ptr - 1], &kp[ptr],
			(block->bb_numrecs - ptr + 1) * sizeof(*kp));
		ovbcopy(&pp[ptr - 1], &pp[ptr],
			(block->bb_numrecs - ptr + 1) * sizeof(*pp));
		/*
		 * Now stuff the new data in, bump numrecs and log the new data.
		 */
		xfs_btree_check_sptr(cur, *bnop, level);
		kp[ptr - 1] = key;
		pp[ptr - 1] = *bnop;
		block->bb_numrecs++;
		xfs_inobt_log_keys(cur, bp, ptr, block->bb_numrecs);
		xfs_inobt_log_ptrs(cur, bp, ptr, block->bb_numrecs);
	} else {
		/*
		 * It's a leaf entry.  Make a hole for the new record.
		 */
		rp = XFS_INOBT_REC_ADDR(block, 1, cur);
		ovbcopy(&rp[ptr - 1], &rp[ptr],
			(block->bb_numrecs - ptr + 1) * sizeof(*rp));
		/*
		 * Now stuff the new record in, bump numrecs
		 * and log the new data.
		 */
		rp[ptr - 1] = *recp;
		block->bb_numrecs++;
		xfs_inobt_log_recs(cur, bp, ptr, block->bb_numrecs);
	}
	/*
	 * Log the new number of records in the btree header.
	 */
	xfs_inobt_log_block(cur->bc_tp, bp, XFS_BB_NUMRECS);
#ifdef DEBUG
	/*
	 * Check that the key/record is in the right place, now.
	 */
	if (ptr < block->bb_numrecs) {
		if (level == 0)
			xfs_btree_check_rec(cur->bc_btnum, rp + ptr - 1,
				rp + ptr);
		else
			xfs_btree_check_key(cur->bc_btnum, kp + ptr - 1,
				kp + ptr);
	}
#endif
	/*
	 * If we inserted at the start of a block, update the parents' keys.
	 */
	if (optr == 1)
		xfs_inobt_updkey(cur, &key, level + 1);
	/*
	 * Return the new block number, if any.
	 * If there is one, give back a record value and a cursor too.
	 */
	*bnop = nbno;
	xfs_inobt_rcheck(cur);
	if (nbno != NULLAGBLOCK) {
		*recp = nrec;
		*curp = ncur;
	} else
		xfs_inobt_kcheck(cur);
	kmem_check();
	return 1;
}

#ifdef XFSDEBUG
/*
 * Check key consistency in one btree level, then recurse down to the next.
 */
STATIC void
xfs_inobt_kcheck_btree(
	xfs_btree_cur_t		*cur,	/* btree cursor */
	xfs_agi_t		*agi,	/* a.g. inode header */
	xfs_agblock_t		bno,	/* block number to check */
	int			level,	/* level of the block */
	xfs_inobt_key_t		*keyp)	/* value of expected first key */
{
	xfs_inobt_block_t	*block;	/* btree block pointer */
	buf_t			*bp;	/* buffer for block */
	int			i;	/* loop index */
	xfs_inobt_key_t		key;	/* key value for comparisons */
	xfs_inobt_key_t		*kp;	/* pointer to btree keys */
	xfs_inobt_ptr_t		*pp;	/* pointer to btree index values */
	xfs_inobt_rec_t		*rp;	/* pointer to btree records */

	/*
	 * Get the buffer and block for the one we're checking.
	 */
	ASSERT(bno != NULLAGBLOCK);
	bp = xfs_btree_read_bufs(cur->bc_mp, cur->bc_tp, agi->agi_seqno,
		bno, 0);
	block = XFS_BUF_TO_INOBT_BLOCK(bp);
	xfs_btree_check_sblock(cur, block, level);
	/*
	 * If a non-leaf, set up the key pointer & save the first
	 * key value, if requested.
	 */
	if (level > 0) {
		kp = XFS_INOBT_KEY_ADDR(block, 1, cur);
		if (keyp)
			key = *kp;
	}
	/*
	 * If a leaf, set up the record pointer and save the key value
	 * if requested.
	 */
	else {
		rp = XFS_INOBT_REC_ADDR(block, 1, cur);
		if (keyp)
			key.ir_startino = rp->ir_startino;
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
		pp = XFS_INOBT_PTR_ADDR(block, 1, cur);
		if (*pp != NULLAGBLOCK) {
			for (i = 1; i <= block->bb_numrecs; i++, pp++, kp++)
				xfs_inobt_kcheck_btree(cur, agi, *pp,
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
xfs_inobt_log_block(
	xfs_trans_t		*tp,	/* transaction pointer */
	buf_t			*bp,	/* buffer containing btree block */
	int			fields)	/* mask of fields: XFS_BB_... */
{
	int			first;	/* first byte offset logged */
	int			last;	/* last byte offset logged */
	static const int	offsets[] = {	/* table of offsets */
		offsetof(xfs_inobt_block_t, bb_magic),
		offsetof(xfs_inobt_block_t, bb_level),
		offsetof(xfs_inobt_block_t, bb_numrecs),
		offsetof(xfs_inobt_block_t, bb_leftsib),
		offsetof(xfs_inobt_block_t, bb_rightsib),
		sizeof(xfs_inobt_block_t)
	};

	xfs_btree_offsets(fields, offsets, XFS_BB_NUM_BITS, &first, &last);
	xfs_trans_log_buf(tp, bp, first, last);
	kmem_check();
}

/*
 * Log keys from a btree block (nonleaf).
 */
STATIC void
xfs_inobt_log_keys(
	xfs_btree_cur_t		*cur,	/* btree cursor */
	buf_t			*bp,	/* buffer containing btree block */
	int			kfirst,	/* index of first key to log */
	int			klast)	/* index of last key to log */
{
	xfs_inobt_block_t	*block;	/* btree block to log from */
	int			first;	/* first byte offset logged */
	xfs_inobt_key_t		*kp;	/* key pointer in btree block */
	int			last;	/* last byte offset logged */

	block = XFS_BUF_TO_INOBT_BLOCK(bp);
	kp = XFS_INOBT_KEY_ADDR(block, 1, cur);
	first = (caddr_t)&kp[kfirst - 1] - (caddr_t)block;
	last = ((caddr_t)&kp[klast] - 1) - (caddr_t)block;
	xfs_trans_log_buf(cur->bc_tp, bp, first, last);
	kmem_check();
}

/*
 * Log block pointer fields from a btree block (nonleaf).
 */
STATIC void
xfs_inobt_log_ptrs(
	xfs_btree_cur_t		*cur,	/* btree cursor */
	buf_t			*bp,	/* buffer containing btree block */
	int			pfirst,	/* index of first pointer to log */
	int			plast)	/* index of last pointer to log */
{
	xfs_inobt_block_t	*block;	/* btree block to log from */
	int			first;	/* first byte offset logged */
	int			last;	/* last byte offset logged */
	xfs_inobt_ptr_t		*pp;	/* block-pointer pointer in btree blk */

	block = XFS_BUF_TO_INOBT_BLOCK(bp);
	pp = XFS_INOBT_PTR_ADDR(block, 1, cur);
	first = (caddr_t)&pp[pfirst - 1] - (caddr_t)block;
	last = ((caddr_t)&pp[plast] - 1) - (caddr_t)block;
	xfs_trans_log_buf(cur->bc_tp, bp, first, last);
	kmem_check();
}

/*
 * Log records from a btree block (leaf).
 */
STATIC void
xfs_inobt_log_recs(
	xfs_btree_cur_t		*cur,	/* btree cursor */
	buf_t			*bp,	/* buffer containing btree block */
	int			rfirst,	/* index of first record to log */
	int			rlast)	/* index of last record to log */
{
	xfs_inobt_block_t	*block;	/* btree block to log from */
	int			first;	/* first byte offset logged */
	int			last;	/* last byte offset logged */
	xfs_inobt_rec_t		*rp;	/* record pointer for btree block */

	block = XFS_BUF_TO_INOBT_BLOCK(bp);
	rp = XFS_INOBT_REC_ADDR(block, 1, cur);
	first = (caddr_t)&rp[rfirst - 1] - (caddr_t)block;
	last = ((caddr_t)&rp[rlast] - 1) - (caddr_t)block;
	xfs_trans_log_buf(cur->bc_tp, bp, first, last);
	kmem_check();
}

/*
 * Lookup the record.  The cursor is made to point to it, based on dir.
 * Return 0 if can't find any such record, 1 for success.
 */
STATIC int				/* success/failure */
xfs_inobt_lookup(
	xfs_btree_cur_t		*cur,	/* btree cursor */
	xfs_lookup_t		dir)	/* <=, ==, or >= */
{
	xfs_agblock_t		agbno;	/* a.g. relative btree block number */
	xfs_agnumber_t		agno;	/* allocation group number */
	xfs_inobt_block_t	*block;	/* current btree block */
	int			diff;	/* difference for the current key */
	int			keyno;	/* current key number */
	int			level;	/* level in the btree */
	xfs_mount_t		*mp;	/* file system mount point */
	xfs_inobt_rec_t		*rp;	/* pointer to the lookup record */

	xfs_inobt_rcheck(cur);
	xfs_inobt_kcheck(cur);
	/*
	 * Get the allocation group header, and the root block number.
	 */
	mp = cur->bc_mp;
	rp = &cur->bc_rec.i;
	{
		xfs_agi_t	*agi;	/* a.g. inode header */

		agi = XFS_BUF_TO_AGI(cur->bc_private.i.agbp);
		agno = agi->agi_seqno;
		agbno = agi->agi_root;
	}
	/*
	 * Iterate over each level in the btree, starting at the root.
	 * For each level above the leaves, find the key we need, based
	 * on the lookup record, then follow the corresponding block
	 * pointer down to the next level.
	 */
	for (level = cur->bc_nlevels - 1, diff = 1; level >= 0; level--) {
		buf_t	*bp;		/* buffer pointer for btree block */
		daddr_t	d;		/* disk address of btree block */

		/*
		 * Get the disk address we're looking for.
		 */
		d = XFS_AGB_TO_DADDR(mp, agno, agbno);
		/*
		 * If the old buffer at this level is for a different block,
		 * throw it away, otherwise just use it.
		 */
		bp = cur->bc_bufs[level];
		if (bp && bp->b_blkno != d)
			bp = (buf_t *)0;
		if (!bp) {
			/*
			 * Need to get a new buffer.  Read it, then 
			 * set it in the cursor, releasing the old one.
			 */
			bp = xfs_trans_read_buf(cur->bc_tp, mp->m_dev, d,
				mp->m_bsize, 0);
			ASSERT(bp && !geterror(bp));
			xfs_btree_setbuf(cur, level, bp);
			bp->b_ref = XFS_INO_BTREE_REF;
		}
		/*
		 * Point to the btree block, now that we have the buffer.
		 */
		block = XFS_BUF_TO_INOBT_BLOCK(bp);
		xfs_btree_check_sblock(cur, block, level);
		/*
		 * If we already had a key match at a higher level, we know
		 * we need to use the first entry in this block.
		 */
		if (diff == 0)
			keyno = 1;
		/*
		 * Otherwise we need to search this block.  Do a binary search.
		 */
		else {
			int		high;	/* high entry number */
			xfs_inobt_key_t	*kkbase;/* base of keys in block */
			xfs_inobt_rec_t	*krbase;/* base of records in block */
			int		low;	/* low entry number */

			/*
			 * Get a pointer to keys or records.
			 */
			if (level > 0)
				kkbase = XFS_INOBT_KEY_ADDR(block, 1, cur);
			else
				krbase = XFS_INOBT_REC_ADDR(block, 1, cur);
			/*
			 * Set low and high entry numbers, 1-based.
			 */
			low = 1;
			if (!(high = block->bb_numrecs)) {
				/*
				 * If the block is empty, the tree must
				 * be an empty leaf.
				 */
				ASSERT(level == 0 && cur->bc_nlevels == 1);
				cur->bc_ptrs[0] = dir != XFS_LOOKUP_LE;
				kmem_check();
				return 0;
			}
			/*
			 * Binary search the block.
			 */
			while (low <= high) {
				xfs_agino_t	startino;	/* key value */

				/*
				 * keyno is average of low and high.
				 */
				keyno = (low + high) >> 1;
				/*
				 * Get startino.
				 */
				if (level > 0) {
					xfs_inobt_key_t	*kkp;

					kkp = kkbase + keyno - 1;
					startino = kkp->ir_startino;
				} else {
					xfs_inobt_rec_t	*krp;

					krp = krbase + keyno - 1;
					startino = krp->ir_startino;
				}
				/*
				 * Compute difference to get next direction.
				 */
				diff = (int)startino - (int)rp->ir_startino;
				/*
				 * Less than, move right.
				 */
				if (diff < 0)
					low = keyno + 1;
				/*
				 * Greater than, move left.
				 */
				else if (diff > 0)
					high = keyno - 1;
				/*
				 * Equal, we're done.
				 */
				else
					break;
			}
		}
		/*
		 * If there are more levels, set up for the next level
		 * by getting the block number and filling in the cursor.
		 */
		if (level > 0) {
			/*
			 * If we moved left, need the previous key number,
			 * unless there isn't one.
			 */
			if (diff > 0 && --keyno < 1)
				keyno = 1;
			agbno = *XFS_INOBT_PTR_ADDR(block, keyno, cur);
			xfs_btree_check_sptr(cur, agbno, level);
			cur->bc_ptrs[level] = keyno;
		}
	}
	/*
	 * Done with the search.
	 * See if we need to adjust the results.
	 */
	if (dir != XFS_LOOKUP_LE && diff < 0) {
		keyno++;
		/*
		 * If ge search and we went off the end of the block, but it's
		 * not the last block, we're in the wrong block.
		 */
		if (dir == XFS_LOOKUP_GE &&
		    keyno > block->bb_numrecs &&
		    block->bb_rightsib != NULLAGBLOCK) {
			int	i;

			cur->bc_ptrs[0] = keyno;
			i = xfs_inobt_increment(cur, 0);
			ASSERT(i == 1);
			kmem_check();
			return 1;
		}
	}
	else if (dir == XFS_LOOKUP_LE && diff > 0)
		keyno--;
	cur->bc_ptrs[0] = keyno;
	kmem_check();
	/*
	 * Return if we succeeded or not.
	 */
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
xfs_inobt_lshift(
	xfs_btree_cur_t		*cur,	/* btree cursor */
	int			level)	/* level to shift record on */
{
	int			i;	/* loop index */
	xfs_inobt_key_t		key;	/* key value for leaf level upward */
	buf_t			*lbp;	/* buffer for left neighbor block */
	xfs_inobt_block_t	*left;	/* left neighbor btree block */
	xfs_inobt_key_t		*lkp;	/* key pointer for left block */
	xfs_inobt_ptr_t		*lpp;	/* address pointer for left block */
	xfs_inobt_rec_t		*lrp;	/* record pointer for left block */
	int			nrec;	/* new number of left block entries */
	buf_t			*rbp;	/* buffer for right (current) block */
	xfs_inobt_block_t	*right;	/* right (current) btree block */
	xfs_inobt_key_t		*rkp;	/* key pointer for right block */
	xfs_inobt_ptr_t		*rpp;	/* address pointer for right block */
	xfs_inobt_rec_t		*rrp;	/* record pointer for right block */

	xfs_inobt_rcheck(cur);
	/*
	 * Set up variables for this block as "right".
	 */
	rbp = cur->bc_bufs[level];
	right = XFS_BUF_TO_INOBT_BLOCK(rbp);
	xfs_btree_check_sblock(cur, right, level);
	/*
	 * If we've got no left sibling then we can't shift an entry left.
	 */
	if (right->bb_leftsib == NULLAGBLOCK)
		return 0;
	/*
	 * If the cursor entry is the one that would be moved, don't 
	 * do it... it's too complicated.
	 */
	if (cur->bc_ptrs[level] <= 1)
		return 0;
	/*
	 * Set up the left neighbor as "left".
	 */
	lbp = xfs_btree_read_bufs(cur->bc_mp, cur->bc_tp,
		cur->bc_private.i.agno, right->bb_leftsib, 0);
	left = XFS_BUF_TO_INOBT_BLOCK(lbp);
	xfs_btree_check_sblock(cur, left, level);
	/*
	 * If it's full, it can't take another entry.
	 */
	if (left->bb_numrecs == XFS_INOBT_BLOCK_MAXRECS(level, cur)) {
		kmem_check();
		return 0;
	}
	nrec = left->bb_numrecs + 1;
	/*
	 * If non-leaf, copy a key and a ptr to the left block.
	 */
	if (level > 0) {
		lkp = XFS_INOBT_KEY_ADDR(left, nrec, cur);
		rkp = XFS_INOBT_KEY_ADDR(right, 1, cur);
		*lkp = *rkp;
		xfs_inobt_log_keys(cur, lbp, nrec, nrec);
		lpp = XFS_INOBT_PTR_ADDR(left, nrec, cur);
		rpp = XFS_INOBT_PTR_ADDR(right, 1, cur);
		xfs_btree_check_sptr(cur, *rpp, level);
		*lpp = *rpp;
		xfs_inobt_log_ptrs(cur, lbp, nrec, nrec);
	}
	/*
	 * If leaf, copy a record to the left block.
	 */
	else {
		lrp = XFS_INOBT_REC_ADDR(left, nrec, cur);
		rrp = XFS_INOBT_REC_ADDR(right, 1, cur);
		*lrp = *rrp;
		xfs_inobt_log_recs(cur, lbp, nrec, nrec);
	}
	/*
	 * Bump and log left's numrecs, decrement and log right's numrecs.
	 */
	left->bb_numrecs++;
	xfs_inobt_log_block(cur->bc_tp, lbp, XFS_BB_NUMRECS);
#ifdef DEBUG
	if (level > 0)
		xfs_btree_check_key(cur->bc_btnum, lkp - 1, lkp);
	else
		xfs_btree_check_rec(cur->bc_btnum, lrp - 1, lrp);
#endif
	right->bb_numrecs--;
	xfs_inobt_log_block(cur->bc_tp, rbp, XFS_BB_NUMRECS);
	/*
	 * Slide the contents of right down one entry.
	 */
	if (level > 0) {
#ifdef DEBUG
		for (i = 0; i < right->bb_numrecs; i++)
			xfs_btree_check_sptr(cur, rpp[i + 1], level);
#endif
		ovbcopy(rkp + 1, rkp, right->bb_numrecs * sizeof(*rkp));
		ovbcopy(rpp + 1, rpp, right->bb_numrecs * sizeof(*rpp));
		xfs_inobt_log_keys(cur, rbp, 1, right->bb_numrecs);
		xfs_inobt_log_ptrs(cur, rbp, 1, right->bb_numrecs);
	} else {
		ovbcopy(rrp + 1, rrp, right->bb_numrecs * sizeof(*rrp));
		xfs_inobt_log_recs(cur, rbp, 1, right->bb_numrecs);
		key.ir_startino = rrp->ir_startino;
		rkp = &key;
	}
	/*
	 * Update the parent key values of right.
	 */
	xfs_inobt_updkey(cur, rkp, level + 1);
	/*
	 * Slide the cursor value left one.
	 */
	cur->bc_ptrs[level]--;
	xfs_inobt_rcheck(cur);
	kmem_check();
	return 1;
}

/*
 * Allocate a new root block, fill it in.
 */
STATIC int				/* success/failure */
xfs_inobt_newroot(
	xfs_btree_cur_t		*cur)	/* btree cursor */
{
	xfs_agi_t		*agi;	/* a.g. inode header */
	xfs_alloc_arg_t		args;	/* allocation argument structure */
	xfs_inobt_block_t	*block;	/* one half of the old root block */
	buf_t			*bp;	/* buffer containing block */
	xfs_inobt_key_t		*kp;	/* btree key pointer */
	xfs_agblock_t		lbno;	/* left block number */
	buf_t			*lbp;	/* left buffer pointer */
	xfs_inobt_block_t	*left;	/* left btree block */
	buf_t			*nbp;	/* new (root) buffer */
	xfs_inobt_block_t	*new;	/* new (root) btree block */
	int			nptr;	/* new value for key index, 1 or 2 */
	xfs_inobt_ptr_t		*pp;	/* btree address pointer */
	xfs_agblock_t		rbno;	/* right block number */
	buf_t			*rbp;	/* right buffer pointer */
	xfs_inobt_block_t	*right;	/* right btree block */
	xfs_inobt_rec_t		*rp;	/* btree record pointer */

	xfs_inobt_rcheck(cur);
	ASSERT(cur->bc_nlevels < XFS_IN_MAXLEVELS(cur->bc_mp));
	/*
	 * Get a block & a buffer.
	 */
	agi = XFS_BUF_TO_AGI(cur->bc_private.i.agbp);
	args.tp = cur->bc_tp;
	args.mp = cur->bc_mp;
	args.fsbno = XFS_AGB_TO_FSB(args.mp, cur->bc_private.i.agno,
		agi->agi_root);
	args.mod = args.minleft = args.total = args.wasdel = args.isfl =
		args.userdata = 0;
	args.minlen = args.maxlen = args.prod = 1;
	args.type = XFS_ALLOCTYPE_NEAR_BNO;
	xfs_alloc_vextent(&args);
	/*
	 * None available, we fail.
	 */
	if (args.fsbno == NULLFSBLOCK)
		return 0;
	ASSERT(args.len == 1);
	nbp = xfs_btree_get_bufs(args.mp, args.tp, args.agno, args.agbno, 0);
	new = XFS_BUF_TO_INOBT_BLOCK(nbp);
	/*
	 * Set the root data in the a.g. inode structure.
	 */
	agi->agi_root = args.agbno;
	agi->agi_level++;
	xfs_ialloc_log_agi(args.tp, cur->bc_private.i.agbp,
		XFS_AGI_ROOT | XFS_AGI_LEVEL);
	/*
	 * At the previous root level there are now two blocks: the old
	 * root, and the new block generated when it was split.
	 * We don't know which one the cursor is pointing at, so we
	 * set up variables "left" and "right" for each case.
	 */
	bp = cur->bc_bufs[cur->bc_nlevels - 1];
	block = XFS_BUF_TO_INOBT_BLOCK(bp);
	xfs_btree_check_sblock(cur, block, cur->bc_nlevels - 1);
	if (block->bb_rightsib != NULLAGBLOCK) {
		/*
		 * Our block is left, pick up the right block.
		 */
		lbp = bp;
		lbno = XFS_DADDR_TO_AGBNO(args.mp, lbp->b_blkno);
		left = block;
		rbno = left->bb_rightsib;
		rbp = xfs_btree_read_bufs(args.mp, args.tp, args.agno, rbno, 0);
		bp = rbp;
		right = XFS_BUF_TO_INOBT_BLOCK(rbp);
		xfs_btree_check_sblock(cur, right, cur->bc_nlevels - 1);
		nptr = 1;
	} else {
		/*
		 * Our block is right, pick up the left block.
		 */
		rbp = bp;
		rbno = XFS_DADDR_TO_AGBNO(args.mp, rbp->b_blkno);
		right = block;
		lbno = right->bb_leftsib;
		lbp = xfs_btree_read_bufs(args.mp, args.tp, args.agno, lbno, 0);
		bp = lbp;
		left = XFS_BUF_TO_INOBT_BLOCK(lbp);
		xfs_btree_check_sblock(cur, left, cur->bc_nlevels - 1);
		nptr = 2;
	}
	/*
	 * Fill in the new block's btree header and log it.
	 */
	new->bb_magic = xfs_magics[cur->bc_btnum];
	new->bb_level = (__uint16_t)cur->bc_nlevels;
	new->bb_numrecs = 2;
	new->bb_leftsib = new->bb_rightsib = NULLAGBLOCK;
	xfs_inobt_log_block(args.tp, nbp, XFS_BB_ALL_BITS);
	ASSERT(lbno != NULLAGBLOCK && rbno != NULLAGBLOCK);
	/*
	 * Fill in the key data in the new root.
	 */
	kp = XFS_INOBT_KEY_ADDR(new, 1, cur);
	if (left->bb_level > 0) {
		kp[0] = *XFS_INOBT_KEY_ADDR(left, 1, cur);
		kp[1] = *XFS_INOBT_KEY_ADDR(right, 1, cur);
	} else {
		rp = XFS_INOBT_REC_ADDR(left, 1, cur);
		kp[0].ir_startino = rp->ir_startino;
		rp = XFS_INOBT_REC_ADDR(right, 1, cur);
		kp[1].ir_startino = rp->ir_startino;
	}
	xfs_inobt_log_keys(cur, nbp, 1, 2);
	/*
	 * Fill in the pointer data in the new root.
	 */
	pp = XFS_INOBT_PTR_ADDR(new, 1, cur);
	pp[0] = lbno;
	pp[1] = rbno;
	xfs_inobt_log_ptrs(cur, nbp, 1, 2);
	/*
	 * Fix up the cursor.
	 */
	xfs_btree_setbuf(cur, cur->bc_nlevels, nbp);
	cur->bc_ptrs[cur->bc_nlevels] = nptr;
	cur->bc_nlevels++;
	xfs_inobt_rcheck(cur);
	xfs_inobt_kcheck(cur);
	kmem_check();
	return 1;
}

#ifdef XFSDEBUG
/*
 * Guts of xfs_inobt_rcheck.  For each level in the btree, check
 * all its blocks for consistency.
 */
STATIC void
xfs_inobt_rcheck_btree(
	xfs_btree_cur_t	*cur,		/* btree cursor */
	xfs_agi_t	*agi,		/* a.g. inode header */
	xfs_agblock_t	rbno,		/* root block number */
	int		levels)		/* number of levels in the btree */
{
	xfs_agblock_t	bno;		/* current block number being checked */
	xfs_agblock_t	fbno;		/* first block at this level */
	xfs_inobt_key_t	key;		/* space for key value */
	int		level;		/* btree level */
	xfs_inobt_rec_t	rec;		/* space for record value */
	void		*rp;		/* pointer to record or key */

	/*
	 * Starting at the root, check each block at each level of the tree.
	 */
	rp = levels - 1 ? (void *)&key : (void *)&rec;
	for (level = levels - 1, bno = rbno; level >= 0; level--, bno = fbno) {
		bno = xfs_inobt_rcheck_btree_block(cur, agi->agi_seqno, bno,
			&fbno, rp, level);
		while (bno != NULLAGBLOCK) {
			ASSERT(bno < agi->agi_length);
			bno = xfs_inobt_rcheck_btree_block(cur, agi->agi_seqno,
				bno, (xfs_agblock_t *)0, rp, level);
		}
	}
}

/*
 * Check one btree block for record consistency.
 * Returns address of the next block to check.
 */
STATIC xfs_agblock_t			/* next block to process */
xfs_inobt_rcheck_btree_block(
	xfs_btree_cur_t		*cur,	/* btree cursor */
	xfs_agnumber_t		agno,	/* allocation group number */
	xfs_agblock_t		bno,	/* block number to check */
	xfs_agblock_t		*fbno,	/* output: first block at next level */
	void			*rec,	/* previous record/key value */
	int			level)	/* level of this block */
{
	xfs_inobt_block_t	*block;	/* allocation btree block */
	buf_t			*bp;	/* buffer containing block */
	int			i;	/* loop index */
	xfs_inobt_key_t		*keyp;	/* btree key pointer (input) */
	xfs_inobt_key_t		*kp;	/* btree key pointer in block */
	xfs_inobt_ptr_t		*pp;	/* btree address pointer in block */
	xfs_agblock_t		rbno;	/* return value */
	xfs_inobt_rec_t		*recp;	/* btree record pointer (input) */
	xfs_inobt_rec_t		*rp;	/* btree record pointer in block */

	/*
	 * Grab the buffer and btree block.
	 */
	bp = xfs_btree_read_bufs(cur->bc_mp, cur->bc_tp, agno, bno, 0);
	block = XFS_BUF_TO_INOBT_BLOCK(bp);
	xfs_btree_check_sblock(cur, block, level);
	/*
	 * If we're supposed to return the first block number for the
	 * next level, get it.
	 */
	if (fbno && block->bb_numrecs) {
		if (level > 0)
			*fbno = *XFS_INOBT_PTR_ADDR(block, 1, cur);
		else
			*fbno = NULLAGBLOCK;
	}
	rbno = block->bb_rightsib;
	if (level > 0)
		keyp = (xfs_inobt_key_t *)rec;
	else
		recp = (xfs_inobt_rec_t *)rec;
	/*
	 * Loop over the entries in the btree block.
	 */
	for (i = 1; i <= block->bb_numrecs; i++) {
		/*
		 * Non-leaf block.  Check the key sequence.
		 */
		if (level > 0) {
			kp = XFS_INOBT_KEY_ADDR(block, i, cur);
			if (i == 1 && !fbno)
				xfs_btree_check_key(cur->bc_btnum, keyp, kp);
			else if (i > 1) {
				xfs_btree_check_key(cur->bc_btnum,
					(void *)(kp - 1), (void *)kp);
				if (i == block->bb_numrecs)
					*keyp = *kp;
			}
			pp = XFS_INOBT_PTR_ADDR(block, i, cur);
			xfs_btree_check_sptr(cur, *pp, level);
		}
		/*
		 * Leaf block.  Check the record sequence.
		 */
		else {
			rp = XFS_INOBT_REC_ADDR(block, i, cur);
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
xfs_inobt_rshift(
	xfs_btree_cur_t		*cur,	/* btree cursor */
	int			level)	/* level to shift record on */
{
	int			i;	/* loop index */
	xfs_inobt_key_t		key;	/* key value for leaf level upward */
	buf_t			*lbp;	/* buffer for left (current) block */
	xfs_inobt_block_t	*left;	/* left (current) btree block */
	xfs_inobt_key_t		*lkp;	/* key pointer for left block */
	xfs_inobt_ptr_t		*lpp;	/* address pointer for left block */
	xfs_inobt_rec_t		*lrp;	/* record pointer for left block */
	buf_t			*rbp;	/* buffer for right neighbor block */
	xfs_inobt_block_t	*right;	/* right neighbor btree block */
	xfs_inobt_key_t		*rkp;	/* key pointer for right block */
	xfs_inobt_ptr_t		*rpp;	/* address pointer for right block */
	xfs_inobt_rec_t		*rrp;	/* record pointer for right block */
	xfs_btree_cur_t		*tcur;	/* temporary cursor */

	xfs_inobt_rcheck(cur);
	/*
	 * Set up variables for this block as "left".
	 */
	lbp = cur->bc_bufs[level];
	left = XFS_BUF_TO_INOBT_BLOCK(lbp);
	xfs_btree_check_sblock(cur, left, level);
	/*
	 * If we've got no right sibling then we can't shift an entry right.
	 */
	if (left->bb_rightsib == NULLAGBLOCK)
		return 0;
	/*
	 * If the cursor entry is the one that would be moved, don't
	 * do it... it's too complicated.
	 */
	if (cur->bc_ptrs[level] >= left->bb_numrecs)
		return 0;
	/*
	 * Set up the right neighbor as "right".
	 */
	rbp = xfs_btree_read_bufs(cur->bc_mp, cur->bc_tp,
		cur->bc_private.i.agno, left->bb_rightsib, 0);
	right = XFS_BUF_TO_INOBT_BLOCK(rbp);
	xfs_btree_check_sblock(cur, right, level);
	/*
	 * If it's full, it can't take another entry.
	 */
	if (right->bb_numrecs == XFS_INOBT_BLOCK_MAXRECS(level, cur)) {
		kmem_check();
		return 0;
	}
	/*
	 * Make a hole at the start of the right neighbor block, then
	 * copy the last left block entry to the hole.
	 */
	if (level > 0) {
		lkp = XFS_INOBT_KEY_ADDR(left, left->bb_numrecs, cur);
		lpp = XFS_INOBT_PTR_ADDR(left, left->bb_numrecs, cur);
		rkp = XFS_INOBT_KEY_ADDR(right, 1, cur);
		rpp = XFS_INOBT_PTR_ADDR(right, 1, cur);
#ifdef DEBUG
		for (i = right->bb_numrecs - 1; i >= 0; i--)
			xfs_btree_check_sptr(cur, rpp[i], level);
#endif
		ovbcopy(rkp, rkp + 1, right->bb_numrecs * sizeof(*rkp));
		ovbcopy(rpp, rpp + 1, right->bb_numrecs * sizeof(*rpp));
		xfs_btree_check_sptr(cur, *lpp, level);
		*rkp = *lkp;
		*rpp = *lpp;
		xfs_inobt_log_keys(cur, rbp, 1, right->bb_numrecs + 1);
		xfs_inobt_log_ptrs(cur, rbp, 1, right->bb_numrecs + 1);
	} else {
		lrp = XFS_INOBT_REC_ADDR(left, left->bb_numrecs, cur);
		rrp = XFS_INOBT_REC_ADDR(right, 1, cur);
		ovbcopy(rrp, rrp + 1, right->bb_numrecs * sizeof(*rrp));
		*rrp = *lrp;
		xfs_inobt_log_recs(cur, rbp, 1, right->bb_numrecs + 1);
		key.ir_startino = rrp->ir_startino;
		rkp = &key;
	}
	/*
	 * Decrement and log left's numrecs, bump and log right's numrecs.
	 */
	left->bb_numrecs--;
	xfs_inobt_log_block(cur->bc_tp, lbp, XFS_BB_NUMRECS);
	right->bb_numrecs++;
#ifdef DEBUG
	if (level > 0)
		xfs_btree_check_key(cur->bc_btnum, rkp, rkp + 1);
	else
		xfs_btree_check_rec(cur->bc_btnum, rrp, rrp + 1);
#endif
	xfs_inobt_log_block(cur->bc_tp, rbp, XFS_BB_NUMRECS);
	/*
	 * Using a temporary cursor, update the parent key values of the
	 * block on the right.
	 */
	tcur = xfs_btree_dup_cursor(cur);
	xfs_btree_lastrec(tcur, level);
	xfs_inobt_increment(tcur, level);
	xfs_inobt_updkey(tcur, rkp, level + 1);
	xfs_btree_del_cursor(tcur);
	xfs_inobt_rcheck(cur);
	kmem_check();
	return 1;
}

/*
 * Split cur/level block in half.
 * Return new block number and its first record (to be inserted into parent).
 */
STATIC int				/* success/failure */
xfs_inobt_split(
	xfs_btree_cur_t		*cur,	/* btree cursor */
	int			level,	/* level to split */
	xfs_agblock_t		*bnop,	/* output: block number allocated */
	xfs_inobt_key_t		*keyp,	/* output: first key of new block */
	xfs_btree_cur_t		**curp)	/* output: new cursor */
{
	xfs_alloc_arg_t		args;	/* allocation argument structure */
	int			i;	/* loop index/record number */
	xfs_agblock_t		lbno;	/* left (current) block number */
	buf_t			*lbp;	/* buffer for left block */
	xfs_inobt_block_t	*left;	/* left (current) btree block */
	xfs_inobt_key_t		*lkp;	/* left btree key pointer */
	xfs_inobt_ptr_t		*lpp;	/* left btree address pointer */
	xfs_inobt_rec_t		*lrp;	/* left btree record pointer */
	buf_t			*rbp;	/* buffer for right block */
	xfs_inobt_block_t	*right;	/* right (new) btree block */
	xfs_inobt_key_t		*rkp;	/* right btree key pointer */
	xfs_inobt_ptr_t		*rpp;	/* right btree address pointer */
	xfs_inobt_rec_t		*rrp;	/* right btree record pointer */

	xfs_inobt_rcheck(cur);
	/*
	 * Set up left block (current one).
	 */
	lbp = cur->bc_bufs[level];
	args.tp = cur->bc_tp;
	args.mp = cur->bc_mp;
	lbno = XFS_DADDR_TO_AGBNO(args.mp, lbp->b_blkno);
	/*
	 * Allocate the new block.
	 * If we can't do it, we're toast.  Give up.
	 */
	args.fsbno = XFS_AGB_TO_FSB(args.mp, cur->bc_private.i.agno, lbno);
	args.mod = args.minleft = args.total = args.wasdel = args.isfl =
		args.userdata = 0;
	args.minlen = args.maxlen = args.prod = 1;
	args.type = XFS_ALLOCTYPE_NEAR_BNO;
	xfs_alloc_vextent(&args);
	if (args.fsbno == NULLFSBLOCK) {
		kmem_check();
		return 0;
	}
	ASSERT(args.len == 1);
	rbp = xfs_btree_get_bufs(args.mp, args.tp, args.agno, args.agbno, 0);
	/*
	 * Set up the new block as "right".
	 */
	right = XFS_BUF_TO_INOBT_BLOCK(rbp);
	/*
	 * "Left" is the current (according to the cursor) block.
	 */
	left = XFS_BUF_TO_INOBT_BLOCK(lbp);
	xfs_btree_check_sblock(cur, left, level);
	/*
	 * Fill in the btree header for the new block.
	 */
	right->bb_magic = xfs_magics[cur->bc_btnum];
	right->bb_level = left->bb_level;
	right->bb_numrecs = (__uint16_t)(left->bb_numrecs / 2);
	/*
	 * Make sure that if there's an odd number of entries now, that
	 * each new block will have the same number of entries.
	 */
	if ((left->bb_numrecs & 1) &&
	    cur->bc_ptrs[level] <= right->bb_numrecs + 1)
		right->bb_numrecs++;
	i = left->bb_numrecs - right->bb_numrecs + 1;
	/*
	 * For non-leaf blocks, copy keys and addresses over to the new block.
	 */
	if (level > 0) {
		lkp = XFS_INOBT_KEY_ADDR(left, i, cur);
		lpp = XFS_INOBT_PTR_ADDR(left, i, cur);
		rkp = XFS_INOBT_KEY_ADDR(right, 1, cur);
		rpp = XFS_INOBT_PTR_ADDR(right, 1, cur);
#ifdef DEBUG
		for (i = 0; i < right->bb_numrecs; i++)
			xfs_btree_check_sptr(cur, lpp[i], level);
#endif
		bcopy(lkp, rkp, right->bb_numrecs * sizeof(*rkp));
		bcopy(lpp, rpp, right->bb_numrecs * sizeof(*rpp));
		xfs_inobt_log_keys(cur, rbp, 1, right->bb_numrecs);
		xfs_inobt_log_ptrs(cur, rbp, 1, right->bb_numrecs);
		*keyp = *rkp;
	}
	/*
	 * For leaf blocks, copy records over to the new block.
	 */
	else {
		lrp = XFS_INOBT_REC_ADDR(left, i, cur);
		rrp = XFS_INOBT_REC_ADDR(right, 1, cur);
		bcopy(lrp, rrp, right->bb_numrecs * sizeof(*rrp));
		xfs_inobt_log_recs(cur, rbp, 1, right->bb_numrecs);
		keyp->ir_startino = rrp->ir_startino;
	}
	/*
	 * Find the left block number by looking in the buffer.
	 * Adjust numrecs, sibling pointers.
	 */
	left->bb_numrecs -= right->bb_numrecs;
	right->bb_rightsib = left->bb_rightsib;
	left->bb_rightsib = args.agbno;
	right->bb_leftsib = lbno;
	xfs_inobt_log_block(args.tp, rbp, XFS_BB_ALL_BITS);
	xfs_inobt_log_block(args.tp, lbp, XFS_BB_NUMRECS | XFS_BB_RIGHTSIB);
	/*
	 * If there's a block to the new block's right, make that block
	 * point back to right instead of to left.
	 */
	if (right->bb_rightsib != NULLAGBLOCK) {
		xfs_inobt_block_t	*rrblock;	/* rr btree block */
		buf_t			*rrbp;		/* buffer for rrblock */

		rrbp = xfs_btree_read_bufs(args.mp, args.tp, args.agno,
			right->bb_rightsib, 0);
		rrblock = XFS_BUF_TO_INOBT_BLOCK(rrbp);
		xfs_btree_check_sblock(cur, rrblock, level);
		rrblock->bb_leftsib = args.agbno;
		xfs_inobt_log_block(args.tp, rrbp, XFS_BB_LEFTSIB);
	}
	/*
	 * If the cursor is really in the right block, move it there.
	 * If it's just pointing past the last entry in left, then we'll
	 * insert there, so don't change anything in that case.
	 */
	if (cur->bc_ptrs[level] > left->bb_numrecs + 1) {
		xfs_btree_setbuf(cur, level, rbp);
		cur->bc_ptrs[level] -= left->bb_numrecs;
	}
	/*
	 * If there are more levels, we'll need another cursor which refers
	 * the right block, no matter where this cursor was.
	 */
	if (level + 1 < cur->bc_nlevels) {
		*curp = xfs_btree_dup_cursor(cur);
		(*curp)->bc_ptrs[level + 1]++;
	}
	*bnop = args.agbno;
	xfs_inobt_rcheck(cur);
	kmem_check();
	return 1;
}

/*
 * Update keys at all levels from here to the root along the cursor's path.
 */
STATIC void
xfs_inobt_updkey(
	xfs_btree_cur_t		*cur,	/* btree cursor */
	xfs_inobt_key_t		*keyp,	/* new key value to update to */
	int			level)	/* starting level for update */
{
	xfs_inobt_block_t	*block;	/* btree block */
	buf_t			*bp;	/* buffer for block */
	xfs_inobt_key_t		*kp;	/* pointer to btree block keys */
	int			ptr;	/* index of key in block */

	xfs_inobt_rcheck(cur);
	/*
	 * Go up the tree from this level toward the root.
	 * At each level, update the key value to the value input.
	 * Stop when we reach a level where the cursor isn't pointing
	 * at the first entry in the block.
	 */
	for (ptr = 1; ptr == 1 && level < cur->bc_nlevels; level++) {
		bp = cur->bc_bufs[level];
		block = XFS_BUF_TO_INOBT_BLOCK(bp);
		xfs_btree_check_sblock(cur, block, level);
		ptr = cur->bc_ptrs[level];
		kp = XFS_INOBT_KEY_ADDR(block, ptr, cur);
		*kp = *keyp;
		xfs_inobt_log_keys(cur, bp, ptr, ptr);
	}
	xfs_inobt_rcheck(cur);
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
xfs_inobt_decrement(
	xfs_btree_cur_t		*cur,	/* btree cursor */
	int			level)	/* level in btree, 0 is leaf */
{
	xfs_inobt_block_t	*block;	/* btree block */
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
	block = XFS_BUF_TO_INOBT_BLOCK(bp);
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
		block = XFS_BUF_TO_INOBT_BLOCK(bp);
		xfs_btree_check_sblock(cur, block, lev);
		agbno = *XFS_INOBT_PTR_ADDR(block, cur->bc_ptrs[lev], cur);
		bp = xfs_btree_read_bufs(cur->bc_mp, cur->bc_tp,
			cur->bc_private.i.agno, agbno, 0);
		xfs_btree_setbuf(cur, lev - 1, bp);
		block = XFS_BUF_TO_INOBT_BLOCK(bp);
		xfs_btree_check_sblock(cur, block, lev - 1);
		cur->bc_ptrs[lev - 1] = block->bb_numrecs;
	}
	kmem_check();
	return 1;
}

#ifdef _NOTYET_
/*
 * Delete the record pointed to by cur.
 * The cursor refers to the place where the record was (could be inserted)
 * when the operation returns.
 */
int					/* success/failure */
xfs_inobt_delete(
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
		i = xfs_inobt_delrec(cur, level);
	if (i == 0) {
		for (level = 1; level < cur->bc_nlevels; level++) {
			if (cur->bc_ptrs[level] == 0) {
				xfs_inobt_decrement(cur, level);
				break;
			}
		}
	}
	xfs_inobt_kcheck(cur);
	return i;
}
#endif	/* _NOTYET_ */

/* 
 * Get the data from the pointed-to record.
 */
int					/* success/failure */
xfs_inobt_get_rec(
	xfs_btree_cur_t		*cur,	/* btree cursor */
	xfs_agino_t		*ino,	/* output: starting inode of chunk */
	__int32_t		*fcnt,	/* output: number of free inodes */
	xfs_inofree_t		*free)	/* output: free inode mask */
{
	xfs_inobt_block_t	*block;	/* btree block */
	buf_t			*bp;	/* buffer containing btree block */
	int			ptr;	/* record number */
	xfs_inobt_rec_t		*rec;	/* record data */

	bp = cur->bc_bufs[0];
	ptr = cur->bc_ptrs[0];
	block = XFS_BUF_TO_INOBT_BLOCK(bp);
	xfs_btree_check_sblock(cur, block, 0);
	/*
	 * Off the right end or left end, return failure.
	 */
	if (ptr > block->bb_numrecs || ptr <= 0)
		return 0;
	/*
	 * Point to the record and extract its data.
	 */
	rec = XFS_INOBT_REC_ADDR(block, ptr, cur);
	*ino = rec->ir_startino;
	*fcnt = rec->ir_freecount;
	*free = rec->ir_free;
	kmem_check();
	return 1;
}

/*
 * Increment cursor by one record at the level.
 * For nonzero levels the leaf-ward information is untouched.
 */
int					/* success/failure */
xfs_inobt_increment(
	xfs_btree_cur_t		*cur,	/* btree cursor */
	int			level)	/* level in btree, 0 is leaf */
{
	xfs_inobt_block_t	*block;	/* btree block */
	buf_t			*bp;	/* buffer containing btree block */
	int			lev;	/* btree level */

	/*
	 * Get a pointer to the btree block.
	 */
	bp = cur->bc_bufs[level];
	block = XFS_BUF_TO_INOBT_BLOCK(bp);
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
		block = XFS_BUF_TO_INOBT_BLOCK(bp);
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
		block = XFS_BUF_TO_INOBT_BLOCK(bp);
		xfs_btree_check_sblock(cur, block, lev);
		agbno = *XFS_INOBT_PTR_ADDR(block, cur->bc_ptrs[lev], cur);
		bp = xfs_btree_read_bufs(cur->bc_mp, cur->bc_tp,
			cur->bc_private.i.agno, agbno, 0);
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
xfs_inobt_insert(
	xfs_btree_cur_t	*cur)		/* btree cursor */
{
	int		i;		/* result value, 0 for failure */
	int		level;		/* current level number in btree */
	xfs_agblock_t	nbno;		/* new block number (split result) */
	xfs_btree_cur_t	*ncur;		/* new cursor (split result) */
	xfs_inobt_rec_t	nrec;		/* record being inserted this level */
	xfs_btree_cur_t	*pcur;		/* previous level's cursor */

	level = 0;
	nbno = NULLAGBLOCK;
	nrec = cur->bc_rec.i;
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
		i = xfs_inobt_insrec(pcur, level++, &nbno, &nrec, &ncur);
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
xfs_inobt_kcheck(
	xfs_btree_cur_t	*cur)		/* btree cursor */
{
	xfs_agi_t	*agi;		/* allocation group inode header */
	xfs_agblock_t	bno;		/* block # of btree root */
	int		levels;		/* # of levels in btree */

	agi = XFS_BUF_TO_AGI(cur->bc_private.i.agbp);
	bno = agi->agi_root;
	levels = agi->agi_level;
	ASSERT(levels == cur->bc_nlevels);
	xfs_inobt_kcheck_btree(cur, agi, bno, levels - 1, (xfs_inobt_key_t *)0);
}
#endif	/* XFSDEBUG */

/*
 * Lookup the record equal to ino in the btree given by cur.
 */
int					/* success/failure */
xfs_inobt_lookup_eq(
	xfs_btree_cur_t	*cur,		/* btree cursor */
	xfs_agino_t	ino,		/* starting inode of chunk */
	__int32_t	fcnt,		/* free inode count */
	xfs_inofree_t	free)		/* free inode mask */
{
	cur->bc_rec.i.ir_startino = ino;
	cur->bc_rec.i.ir_freecount = fcnt;
	cur->bc_rec.i.ir_free = free;
	return xfs_inobt_lookup(cur, XFS_LOOKUP_EQ);
}

/*
 * Lookup the first record greater than or equal to ino
 * in the btree given by cur.
 */
int					/* success/failure */
xfs_inobt_lookup_ge(
	xfs_btree_cur_t	*cur,		/* btree cursor */
	xfs_agino_t	ino,		/* starting inode of chunk */
	__int32_t	fcnt,		/* free inode count */
	xfs_inofree_t	free)		/* free inode mask */
{
	cur->bc_rec.i.ir_startino = ino;
	cur->bc_rec.i.ir_freecount = fcnt;
	cur->bc_rec.i.ir_free = free;
	return xfs_inobt_lookup(cur, XFS_LOOKUP_GE);
}

/*
 * Lookup the first record less than or equal to ino
 * in the btree given by cur.
 */
int					/* success/failure */
xfs_inobt_lookup_le(
	xfs_btree_cur_t	*cur,		/* btree cursor */
	xfs_agino_t	ino,		/* starting inode of chunk */
	__int32_t	fcnt,		/* free inode count */
	xfs_inofree_t	free)		/* free inode mask */
{
	cur->bc_rec.i.ir_startino = ino;
	cur->bc_rec.i.ir_freecount = fcnt;
	cur->bc_rec.i.ir_free = free;
	return xfs_inobt_lookup(cur, XFS_LOOKUP_LE);
}

#ifdef XFSDEBUG
/*
 * Check consistency in the given btree.
 * Checks header consistency and that keys/records are in the right order.
 */
void
xfs_inobt_rcheck(
	xfs_btree_cur_t	*cur)		/* btree cursor */
{
	xfs_agi_t	*agi;		/* a.g. inode header */
	xfs_agblock_t	bno;		/* block number of btree root */
	int		levels;		/* number of levels in btree */

	agi = XFS_BUF_TO_AGI(cur->bc_private.i.agbp);
	bno = agi->agi_root;
	levels = agi->agi_level;
	xfs_inobt_rcheck_btree(cur, agi, bno, levels);
}
#endif	/* XFSDEBUG */

/*
 * Update the record referred to by cur, to the value given
 * by [ino, fcnt, free].
 */
int					/* success/failure */
xfs_inobt_update(
	xfs_btree_cur_t		*cur,	/* btree cursor */
	xfs_agino_t		ino,	/* starting inode of chunk */
	__int32_t		fcnt,	/* free inode count */
	xfs_inofree_t		free)	/* free inode mask */
{
	xfs_inobt_block_t	*block;	/* btree block to update */
	buf_t			*bp;	/* buffer containing btree block */
	int			ptr;	/* current record number (updating) */
	xfs_inobt_rec_t		*rp;	/* pointer to updated record */

	/*
	 * Pick up the current block.
	 */
	xfs_inobt_rcheck(cur);
	bp = cur->bc_bufs[0];
	block = XFS_BUF_TO_INOBT_BLOCK(bp);
	xfs_btree_check_sblock(cur, block, 0);
	/*
	 * Get the address of the rec to be updated.
	 */
	ptr = cur->bc_ptrs[0];
	rp = XFS_INOBT_REC_ADDR(block, ptr, cur);
	/*
	 * Fill in the new contents and log them.
	 */
	rp->ir_startino = ino;
	rp->ir_freecount = fcnt;
	rp->ir_free = free;
	xfs_inobt_log_recs(cur, bp, ptr, ptr);
	/*
	 * Updating first record in leaf. Pass new key value up to our parent.
	 */
	if (ptr == 1) {
		xfs_inobt_key_t	key;	/* key containing [ino] */

		key.ir_startino = ino;
		xfs_inobt_updkey(cur, &key, 1);
		xfs_inobt_rcheck(cur);
		xfs_inobt_kcheck(cur);
	}
	kmem_check();
	return 1;
}
