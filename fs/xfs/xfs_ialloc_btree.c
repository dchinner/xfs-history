/*
 * Copyright (c) 2000-2001,2005 Silicon Graphics, Inc.
 * All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write the Free Software Foundation,
 * Inc.,  51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */
#include "xfs.h"
#include "xfs_fs.h"
#include "xfs_types.h"
#include "xfs_bit.h"
#include "xfs_log.h"
#include "xfs_inum.h"
#include "xfs_trans.h"
#include "xfs_sb.h"
#include "xfs_ag.h"
#include "xfs_dir2.h"
#include "xfs_dmapi.h"
#include "xfs_mount.h"
#include "xfs_bmap_btree.h"
#include "xfs_alloc_btree.h"
#include "xfs_ialloc_btree.h"
#include "xfs_dir2_sf.h"
#include "xfs_attr_sf.h"
#include "xfs_dinode.h"
#include "xfs_inode.h"
#include "xfs_btree.h"
#include "xfs_btree_trace.h"
#include "xfs_ialloc.h"
#include "xfs_alloc.h"
#include "xfs_error.h"

STATIC void xfs_inobt_log_block(xfs_trans_t *, xfs_buf_t *, int);
STATIC void xfs_inobt_log_keys(xfs_btree_cur_t *, xfs_buf_t *, int, int);
STATIC void xfs_inobt_log_ptrs(xfs_btree_cur_t *, xfs_buf_t *, int, int);
STATIC void xfs_inobt_log_recs(xfs_btree_cur_t *, xfs_buf_t *, int, int);

/*
 * Single level of the xfs_inobt_delete record deletion routine.
 * Delete record pointed to by cur/level.
 * Remove the record from its block then rebalance the tree.
 * Return 0 for error, 1 for done, 2 to go on to the next level.
 */
STATIC int				/* error */
xfs_inobt_delrec(
	xfs_btree_cur_t		*cur,	/* btree cursor */
	int			level,	/* level removing record from */
	int			*stat)	/* fail/done/go-on */
{
	xfs_buf_t		*agbp;	/* buffer for a.g. inode header */
	xfs_mount_t		*mp;	/* mount structure */
	xfs_agi_t		*agi;	/* allocation group inode header */
	xfs_inobt_block_t	*block;	/* btree block record/key lives in */
	xfs_agblock_t		bno;	/* btree block number */
	xfs_buf_t		*bp;	/* buffer for block */
	int			error;	/* error return value */
	int			i;	/* loop index */
	xfs_inobt_key_t		key;	/* kp points here if block is level 0 */
	xfs_inobt_key_t		*kp = NULL;	/* pointer to btree keys */
	xfs_agblock_t		lbno;	/* left block's block number */
	xfs_buf_t		*lbp;	/* left block's buffer pointer */
	xfs_inobt_block_t	*left;	/* left btree block */
	xfs_inobt_key_t		*lkp;	/* left block key pointer */
	xfs_inobt_ptr_t		*lpp;	/* left block address pointer */
	int			lrecs = 0;	/* number of records in left block */
	xfs_inobt_rec_t		*lrp;	/* left block record pointer */
	xfs_inobt_ptr_t		*pp = NULL;	/* pointer to btree addresses */
	int			ptr;	/* index in btree block for this rec */
	xfs_agblock_t		rbno;	/* right block's block number */
	xfs_buf_t		*rbp;	/* right block's buffer pointer */
	xfs_inobt_block_t	*right;	/* right btree block */
	xfs_inobt_key_t		*rkp;	/* right block key pointer */
	xfs_inobt_rec_t		*rp;	/* pointer to btree records */
	xfs_inobt_ptr_t		*rpp;	/* right block address pointer */
	int			rrecs = 0;	/* number of records in right block */
	int			numrecs;
	xfs_inobt_rec_t		*rrp;	/* right block record pointer */
	xfs_btree_cur_t		*tcur;	/* temporary btree cursor */

	mp = cur->bc_mp;

	/*
	 * Get the index of the entry being deleted, check for nothing there.
	 */
	ptr = cur->bc_ptrs[level];
	if (ptr == 0) {
		*stat = 0;
		return 0;
	}

	/*
	 * Get the buffer & block containing the record or key/ptr.
	 */
	bp = cur->bc_bufs[level];
	block = XFS_BUF_TO_INOBT_BLOCK(bp);
#ifdef DEBUG
	if ((error = xfs_btree_check_sblock(cur, block, level, bp)))
		return error;
#endif
	/*
	 * Fail if we're off the end of the block.
	 */

	numrecs = be16_to_cpu(block->bb_numrecs);
	if (ptr > numrecs) {
		*stat = 0;
		return 0;
	}
	/*
	 * It's a nonleaf.  Excise the key and ptr being deleted, by
	 * sliding the entries past them down one.
	 * Log the changed areas of the block.
	 */
	if (level > 0) {
		kp = XFS_INOBT_KEY_ADDR(block, 1, cur);
		pp = XFS_INOBT_PTR_ADDR(block, 1, cur);
#ifdef DEBUG
		for (i = ptr; i < numrecs; i++) {
			if ((error = xfs_btree_check_sptr(cur, be32_to_cpu(pp[i]), level)))
				return error;
		}
#endif
		if (ptr < numrecs) {
			memmove(&kp[ptr - 1], &kp[ptr],
				(numrecs - ptr) * sizeof(*kp));
			memmove(&pp[ptr - 1], &pp[ptr],
				(numrecs - ptr) * sizeof(*kp));
			xfs_inobt_log_keys(cur, bp, ptr, numrecs - 1);
			xfs_inobt_log_ptrs(cur, bp, ptr, numrecs - 1);
		}
	}
	/*
	 * It's a leaf.  Excise the record being deleted, by sliding the
	 * entries past it down one.  Log the changed areas of the block.
	 */
	else {
		rp = XFS_INOBT_REC_ADDR(block, 1, cur);
		if (ptr < numrecs) {
			memmove(&rp[ptr - 1], &rp[ptr],
				(numrecs - ptr) * sizeof(*rp));
			xfs_inobt_log_recs(cur, bp, ptr, numrecs - 1);
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
	numrecs--;
	block->bb_numrecs = cpu_to_be16(numrecs);
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
		if (numrecs == 1 && level > 0) {
			agbp = cur->bc_private.a.agbp;
			agi = XFS_BUF_TO_AGI(agbp);
			/*
			 * pp is still set to the first pointer in the block.
			 * Make it the new root of the btree.
			 */
			bno = be32_to_cpu(agi->agi_root);
			agi->agi_root = *pp;
			be32_add_cpu(&agi->agi_level, -1);
			/*
			 * Free the block.
			 */
			if ((error = xfs_free_extent(cur->bc_tp,
				XFS_AGB_TO_FSB(mp, cur->bc_private.a.agno, bno), 1)))
				return error;
			xfs_trans_binval(cur->bc_tp, bp);
			xfs_ialloc_log_agi(cur->bc_tp, agbp,
				XFS_AGI_ROOT | XFS_AGI_LEVEL);
			/*
			 * Update the cursor so there's one fewer level.
			 */
			cur->bc_bufs[level] = NULL;
			cur->bc_nlevels--;
		} else if (level > 0 &&
			   (error = xfs_btree_decrement(cur, level, &i)))
			return error;
		*stat = 1;
		return 0;
	}
	/*
	 * If we deleted the leftmost entry in the block, update the
	 * key values above us in the tree.
	 */
	if (ptr == 1 && (error = xfs_btree_updkey(cur, (union xfs_btree_key *)kp, level + 1)))
		return error;
	/*
	 * If the number of records remaining in the block is at least
	 * the minimum, we're done.
	 */
	if (numrecs >= XFS_INOBT_BLOCK_MINRECS(level, cur)) {
		if (level > 0 &&
		    (error = xfs_btree_decrement(cur, level, &i)))
			return error;
		*stat = 1;
		return 0;
	}
	/*
	 * Otherwise, we have to move some records around to keep the
	 * tree balanced.  Look at the left and right sibling blocks to
	 * see if we can re-balance by moving only one record.
	 */
	rbno = be32_to_cpu(block->bb_rightsib);
	lbno = be32_to_cpu(block->bb_leftsib);
	bno = NULLAGBLOCK;
	ASSERT(rbno != NULLAGBLOCK || lbno != NULLAGBLOCK);
	/*
	 * Duplicate the cursor so our btree manipulations here won't
	 * disrupt the next level up.
	 */
	if ((error = xfs_btree_dup_cursor(cur, &tcur)))
		return error;
	/*
	 * If there's a right sibling, see if it's ok to shift an entry
	 * out of it.
	 */
	if (rbno != NULLAGBLOCK) {
		/*
		 * Move the temp cursor to the last entry in the next block.
		 * Actually any entry but the first would suffice.
		 */
		i = xfs_btree_lastrec(tcur, level);
		XFS_WANT_CORRUPTED_GOTO(i == 1, error0);
		if ((error = xfs_btree_increment(tcur, level, &i)))
			goto error0;
		XFS_WANT_CORRUPTED_GOTO(i == 1, error0);
		i = xfs_btree_lastrec(tcur, level);
		XFS_WANT_CORRUPTED_GOTO(i == 1, error0);
		/*
		 * Grab a pointer to the block.
		 */
		rbp = tcur->bc_bufs[level];
		right = XFS_BUF_TO_INOBT_BLOCK(rbp);
#ifdef DEBUG
		if ((error = xfs_btree_check_sblock(cur, right, level, rbp)))
			goto error0;
#endif
		/*
		 * Grab the current block number, for future use.
		 */
		bno = be32_to_cpu(right->bb_leftsib);
		/*
		 * If right block is full enough so that removing one entry
		 * won't make it too empty, and left-shifting an entry out
		 * of right to us works, we're done.
		 */
		if (be16_to_cpu(right->bb_numrecs) - 1 >=
		     XFS_INOBT_BLOCK_MINRECS(level, cur)) {
			if ((error = xfs_btree_lshift(tcur, level, &i)))
				goto error0;
			if (i) {
				ASSERT(be16_to_cpu(block->bb_numrecs) >=
				       XFS_INOBT_BLOCK_MINRECS(level, cur));
				xfs_btree_del_cursor(tcur,
						     XFS_BTREE_NOERROR);
				if (level > 0 &&
				    (error = xfs_btree_decrement(cur, level,
						&i)))
					return error;
				*stat = 1;
				return 0;
			}
		}
		/*
		 * Otherwise, grab the number of records in right for
		 * future reference, and fix up the temp cursor to point
		 * to our block again (last record).
		 */
		rrecs = be16_to_cpu(right->bb_numrecs);
		if (lbno != NULLAGBLOCK) {
			xfs_btree_firstrec(tcur, level);
			if ((error = xfs_btree_decrement(tcur, level, &i)))
				goto error0;
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
		if ((error = xfs_btree_decrement(tcur, level, &i)))
			goto error0;
		xfs_btree_firstrec(tcur, level);
		/*
		 * Grab a pointer to the block.
		 */
		lbp = tcur->bc_bufs[level];
		left = XFS_BUF_TO_INOBT_BLOCK(lbp);
#ifdef DEBUG
		if ((error = xfs_btree_check_sblock(cur, left, level, lbp)))
			goto error0;
#endif
		/*
		 * Grab the current block number, for future use.
		 */
		bno = be32_to_cpu(left->bb_rightsib);
		/*
		 * If left block is full enough so that removing one entry
		 * won't make it too empty, and right-shifting an entry out
		 * of left to us works, we're done.
		 */
		if (be16_to_cpu(left->bb_numrecs) - 1 >=
		     XFS_INOBT_BLOCK_MINRECS(level, cur)) {
			if ((error = xfs_btree_rshift(tcur, level, &i)))
				goto error0;
			if (i) {
				ASSERT(be16_to_cpu(block->bb_numrecs) >=
				       XFS_INOBT_BLOCK_MINRECS(level, cur));
				xfs_btree_del_cursor(tcur,
						     XFS_BTREE_NOERROR);
				if (level == 0)
					cur->bc_ptrs[0]++;
				*stat = 1;
				return 0;
			}
		}
		/*
		 * Otherwise, grab the number of records in right for
		 * future reference.
		 */
		lrecs = be16_to_cpu(left->bb_numrecs);
	}
	/*
	 * Delete the temp cursor, we're done with it.
	 */
	xfs_btree_del_cursor(tcur, XFS_BTREE_NOERROR);
	/*
	 * If here, we need to do a join to keep the tree balanced.
	 */
	ASSERT(bno != NULLAGBLOCK);
	/*
	 * See if we can join with the left neighbor block.
	 */
	if (lbno != NULLAGBLOCK &&
	    lrecs + numrecs <= XFS_INOBT_BLOCK_MAXRECS(level, cur)) {
		/*
		 * Set "right" to be the starting block,
		 * "left" to be the left neighbor.
		 */
		rbno = bno;
		right = block;
		rrecs = be16_to_cpu(right->bb_numrecs);
		rbp = bp;
		if ((error = xfs_btree_read_bufs(mp, cur->bc_tp,
				cur->bc_private.a.agno, lbno, 0, &lbp,
				XFS_INO_BTREE_REF)))
			return error;
		left = XFS_BUF_TO_INOBT_BLOCK(lbp);
		lrecs = be16_to_cpu(left->bb_numrecs);
		if ((error = xfs_btree_check_sblock(cur, left, level, lbp)))
			return error;
	}
	/*
	 * If that won't work, see if we can join with the right neighbor block.
	 */
	else if (rbno != NULLAGBLOCK &&
		 rrecs + numrecs <= XFS_INOBT_BLOCK_MAXRECS(level, cur)) {
		/*
		 * Set "left" to be the starting block,
		 * "right" to be the right neighbor.
		 */
		lbno = bno;
		left = block;
		lrecs = be16_to_cpu(left->bb_numrecs);
		lbp = bp;
		if ((error = xfs_btree_read_bufs(mp, cur->bc_tp,
				cur->bc_private.a.agno, rbno, 0, &rbp,
				XFS_INO_BTREE_REF)))
			return error;
		right = XFS_BUF_TO_INOBT_BLOCK(rbp);
		rrecs = be16_to_cpu(right->bb_numrecs);
		if ((error = xfs_btree_check_sblock(cur, right, level, rbp)))
			return error;
	}
	/*
	 * Otherwise, we can't fix the imbalance.
	 * Just return.  This is probably a logic error, but it's not fatal.
	 */
	else {
		if (level > 0 && (error = xfs_btree_decrement(cur, level, &i)))
			return error;
		*stat = 1;
		return 0;
	}
	/*
	 * We're now going to join "left" and "right" by moving all the stuff
	 * in "right" to "left" and deleting "right".
	 */
	if (level > 0) {
		/*
		 * It's a non-leaf.  Move keys and pointers.
		 */
		lkp = XFS_INOBT_KEY_ADDR(left, lrecs + 1, cur);
		lpp = XFS_INOBT_PTR_ADDR(left, lrecs + 1, cur);
		rkp = XFS_INOBT_KEY_ADDR(right, 1, cur);
		rpp = XFS_INOBT_PTR_ADDR(right, 1, cur);
#ifdef DEBUG
		for (i = 0; i < rrecs; i++) {
			if ((error = xfs_btree_check_sptr(cur, be32_to_cpu(rpp[i]), level)))
				return error;
		}
#endif
		memcpy(lkp, rkp, rrecs * sizeof(*lkp));
		memcpy(lpp, rpp, rrecs * sizeof(*lpp));
		xfs_inobt_log_keys(cur, lbp, lrecs + 1, lrecs + rrecs);
		xfs_inobt_log_ptrs(cur, lbp, lrecs + 1, lrecs + rrecs);
	} else {
		/*
		 * It's a leaf.  Move records.
		 */
		lrp = XFS_INOBT_REC_ADDR(left, lrecs + 1, cur);
		rrp = XFS_INOBT_REC_ADDR(right, 1, cur);
		memcpy(lrp, rrp, rrecs * sizeof(*lrp));
		xfs_inobt_log_recs(cur, lbp, lrecs + 1, lrecs + rrecs);
	}
	/*
	 * If we joined with the left neighbor, set the buffer in the
	 * cursor to the left block, and fix up the index.
	 */
	if (bp != lbp) {
		xfs_btree_setbuf(cur, level, lbp);
		cur->bc_ptrs[level] += lrecs;
	}
	/*
	 * If we joined with the right neighbor and there's a level above
	 * us, increment the cursor at that level.
	 */
	else if (level + 1 < cur->bc_nlevels &&
		 (error = xfs_btree_increment(cur, level + 1, &i)))
		return error;
	/*
	 * Fix up the number of records in the surviving block.
	 */
	lrecs += rrecs;
	left->bb_numrecs = cpu_to_be16(lrecs);
	/*
	 * Fix up the right block pointer in the surviving block, and log it.
	 */
	left->bb_rightsib = right->bb_rightsib;
	xfs_inobt_log_block(cur->bc_tp, lbp, XFS_BB_NUMRECS | XFS_BB_RIGHTSIB);
	/*
	 * If there is a right sibling now, make it point to the
	 * remaining block.
	 */
	if (be32_to_cpu(left->bb_rightsib) != NULLAGBLOCK) {
		xfs_inobt_block_t	*rrblock;
		xfs_buf_t		*rrbp;

		if ((error = xfs_btree_read_bufs(mp, cur->bc_tp,
				cur->bc_private.a.agno, be32_to_cpu(left->bb_rightsib), 0,
				&rrbp, XFS_INO_BTREE_REF)))
			return error;
		rrblock = XFS_BUF_TO_INOBT_BLOCK(rrbp);
		if ((error = xfs_btree_check_sblock(cur, rrblock, level, rrbp)))
			return error;
		rrblock->bb_leftsib = cpu_to_be32(lbno);
		xfs_inobt_log_block(cur->bc_tp, rrbp, XFS_BB_LEFTSIB);
	}
	/*
	 * Free the deleting block.
	 */
	if ((error = xfs_free_extent(cur->bc_tp, XFS_AGB_TO_FSB(mp,
				     cur->bc_private.a.agno, rbno), 1)))
		return error;
	xfs_trans_binval(cur->bc_tp, rbp);
	/*
	 * Readjust the ptr at this level if it's not a leaf, since it's
	 * still pointing at the deletion point, which makes the cursor
	 * inconsistent.  If this makes the ptr 0, the caller fixes it up.
	 * We can't use decrement because it would change the next level up.
	 */
	if (level > 0)
		cur->bc_ptrs[level]--;
	/*
	 * Return value means the next level up has something to do.
	 */
	*stat = 2;
	return 0;

error0:
	xfs_btree_del_cursor(tcur, XFS_BTREE_ERROR);
	return error;
}

/*
 * Log header fields from a btree block.
 */
STATIC void
xfs_inobt_log_block(
	xfs_trans_t		*tp,	/* transaction pointer */
	xfs_buf_t		*bp,	/* buffer containing btree block */
	int			fields)	/* mask of fields: XFS_BB_... */
{
	int			first;	/* first byte offset logged */
	int			last;	/* last byte offset logged */
	static const short	offsets[] = {	/* table of offsets */
		offsetof(xfs_inobt_block_t, bb_magic),
		offsetof(xfs_inobt_block_t, bb_level),
		offsetof(xfs_inobt_block_t, bb_numrecs),
		offsetof(xfs_inobt_block_t, bb_leftsib),
		offsetof(xfs_inobt_block_t, bb_rightsib),
		sizeof(xfs_inobt_block_t)
	};

	xfs_btree_offsets(fields, offsets, XFS_BB_NUM_BITS, &first, &last);
	xfs_trans_log_buf(tp, bp, first, last);
}

/*
 * Log keys from a btree block (nonleaf).
 */
STATIC void
xfs_inobt_log_keys(
	xfs_btree_cur_t		*cur,	/* btree cursor */
	xfs_buf_t		*bp,	/* buffer containing btree block */
	int			kfirst,	/* index of first key to log */
	int			klast)	/* index of last key to log */
{
	xfs_inobt_block_t	*block;	/* btree block to log from */
	int			first;	/* first byte offset logged */
	xfs_inobt_key_t		*kp;	/* key pointer in btree block */
	int			last;	/* last byte offset logged */

	block = XFS_BUF_TO_INOBT_BLOCK(bp);
	kp = XFS_INOBT_KEY_ADDR(block, 1, cur);
	first = (int)((xfs_caddr_t)&kp[kfirst - 1] - (xfs_caddr_t)block);
	last = (int)(((xfs_caddr_t)&kp[klast] - 1) - (xfs_caddr_t)block);
	xfs_trans_log_buf(cur->bc_tp, bp, first, last);
}

/*
 * Log block pointer fields from a btree block (nonleaf).
 */
STATIC void
xfs_inobt_log_ptrs(
	xfs_btree_cur_t		*cur,	/* btree cursor */
	xfs_buf_t		*bp,	/* buffer containing btree block */
	int			pfirst,	/* index of first pointer to log */
	int			plast)	/* index of last pointer to log */
{
	xfs_inobt_block_t	*block;	/* btree block to log from */
	int			first;	/* first byte offset logged */
	int			last;	/* last byte offset logged */
	xfs_inobt_ptr_t		*pp;	/* block-pointer pointer in btree blk */

	block = XFS_BUF_TO_INOBT_BLOCK(bp);
	pp = XFS_INOBT_PTR_ADDR(block, 1, cur);
	first = (int)((xfs_caddr_t)&pp[pfirst - 1] - (xfs_caddr_t)block);
	last = (int)(((xfs_caddr_t)&pp[plast] - 1) - (xfs_caddr_t)block);
	xfs_trans_log_buf(cur->bc_tp, bp, first, last);
}

/*
 * Log records from a btree block (leaf).
 */
STATIC void
xfs_inobt_log_recs(
	xfs_btree_cur_t		*cur,	/* btree cursor */
	xfs_buf_t		*bp,	/* buffer containing btree block */
	int			rfirst,	/* index of first record to log */
	int			rlast)	/* index of last record to log */
{
	xfs_inobt_block_t	*block;	/* btree block to log from */
	int			first;	/* first byte offset logged */
	int			last;	/* last byte offset logged */
	xfs_inobt_rec_t		*rp;	/* record pointer for btree block */

	block = XFS_BUF_TO_INOBT_BLOCK(bp);
	rp = XFS_INOBT_REC_ADDR(block, 1, cur);
	first = (int)((xfs_caddr_t)&rp[rfirst - 1] - (xfs_caddr_t)block);
	last = (int)(((xfs_caddr_t)&rp[rlast] - 1) - (xfs_caddr_t)block);
	xfs_trans_log_buf(cur->bc_tp, bp, first, last);
}


/*
 * Externally visible routines.
 */

/*
 * Delete the record pointed to by cur.
 * The cursor refers to the place where the record was (could be inserted)
 * when the operation returns.
 */
int					/* error */
xfs_inobt_delete(
	xfs_btree_cur_t	*cur,		/* btree cursor */
	int		*stat)		/* success/failure */
{
	int		error;
	int		i;		/* result code */
	int		level;		/* btree level */

	/*
	 * Go up the tree, starting at leaf level.
	 * If 2 is returned then a join was done; go to the next level.
	 * Otherwise we are done.
	 */
	for (level = 0, i = 2; i == 2; level++) {
		if ((error = xfs_inobt_delrec(cur, level, &i)))
			return error;
	}
	if (i == 0) {
		for (level = 1; level < cur->bc_nlevels; level++) {
			if (cur->bc_ptrs[level] == 0) {
				if ((error = xfs_btree_decrement(cur, level, &i)))
					return error;
				break;
			}
		}
	}
	*stat = i;
	return 0;
}


/*
 * Get the data from the pointed-to record.
 */
int					/* error */
xfs_inobt_get_rec(
	xfs_btree_cur_t		*cur,	/* btree cursor */
	xfs_agino_t		*ino,	/* output: starting inode of chunk */
	__int32_t		*fcnt,	/* output: number of free inodes */
	xfs_inofree_t		*free,	/* output: free inode mask */
	int			*stat)	/* output: success/failure */
{
	xfs_inobt_block_t	*block;	/* btree block */
	xfs_buf_t		*bp;	/* buffer containing btree block */
#ifdef DEBUG
	int			error;	/* error return value */
#endif
	int			ptr;	/* record number */
	xfs_inobt_rec_t		*rec;	/* record data */

	bp = cur->bc_bufs[0];
	ptr = cur->bc_ptrs[0];
	block = XFS_BUF_TO_INOBT_BLOCK(bp);
#ifdef DEBUG
	if ((error = xfs_btree_check_sblock(cur, block, 0, bp)))
		return error;
#endif
	/*
	 * Off the right end or left end, return failure.
	 */
	if (ptr > be16_to_cpu(block->bb_numrecs) || ptr <= 0) {
		*stat = 0;
		return 0;
	}
	/*
	 * Point to the record and extract its data.
	 */
	rec = XFS_INOBT_REC_ADDR(block, ptr, cur);
	*ino = be32_to_cpu(rec->ir_startino);
	*fcnt = be32_to_cpu(rec->ir_freecount);
	*free = be64_to_cpu(rec->ir_free);
	*stat = 1;
	return 0;
}


STATIC struct xfs_btree_cur *
xfs_inobt_dup_cursor(
	struct xfs_btree_cur	*cur)
{
	return xfs_inobt_init_cursor(cur->bc_mp, cur->bc_tp,
			cur->bc_private.a.agbp, cur->bc_private.a.agno);
}

STATIC void
xfs_inobt_set_root(
	struct xfs_btree_cur	*cur,
	union xfs_btree_ptr	*nptr,
	int			inc)	/* level change */
{
	struct xfs_buf		*agbp = cur->bc_private.a.agbp;
	struct xfs_agi		*agi = XFS_BUF_TO_AGI(agbp);

	agi->agi_root = nptr->s;
	be32_add_cpu(&agi->agi_level, inc);
	xfs_ialloc_log_agi(cur->bc_tp, agbp, XFS_AGI_ROOT | XFS_AGI_LEVEL);
}

STATIC int
xfs_inobt_alloc_block(
	struct xfs_btree_cur	*cur,
	union xfs_btree_ptr	*start,
	union xfs_btree_ptr	*new,
	int			length,
	int			*stat)
{
	xfs_alloc_arg_t		args;		/* block allocation args */
	int			error;		/* error return value */
	xfs_agblock_t		sbno = be32_to_cpu(start->s);

	XFS_BTREE_TRACE_CURSOR(cur, XBT_ENTRY);

	memset(&args, 0, sizeof(args));
	args.tp = cur->bc_tp;
	args.mp = cur->bc_mp;
	args.fsbno = XFS_AGB_TO_FSB(args.mp, cur->bc_private.a.agno, sbno);
	args.minlen = 1;
	args.maxlen = 1;
	args.prod = 1;
	args.type = XFS_ALLOCTYPE_NEAR_BNO;

	error = xfs_alloc_vextent(&args);
	if (error) {
		XFS_BTREE_TRACE_CURSOR(cur, XBT_ERROR);
		return error;
	}
	if (args.fsbno == NULLFSBLOCK) {
		XFS_BTREE_TRACE_CURSOR(cur, XBT_EXIT);
		*stat = 0;
		return 0;
	}
	ASSERT(args.len == 1);
	XFS_BTREE_TRACE_CURSOR(cur, XBT_EXIT);

	new->s = cpu_to_be32(XFS_FSB_TO_AGBNO(args.mp, args.fsbno));
	*stat = 1;
	return 0;
}

STATIC int
xfs_inobt_free_block(
	struct xfs_btree_cur	*cur,
	struct xfs_buf		*bp)
{
	xfs_fsblock_t		fsbno;
	int			error;

	fsbno = XFS_DADDR_TO_FSB(cur->bc_mp, XFS_BUF_ADDR(bp));
	error = xfs_free_extent(cur->bc_tp, fsbno, 1);
	if (error)
		return error;

	xfs_trans_binval(cur->bc_tp, bp);
	return error;
}

STATIC int
xfs_inobt_get_maxrecs(
	struct xfs_btree_cur	*cur,
	int			level)
{
	return cur->bc_mp->m_inobt_mxr[level != 0];
}

STATIC void
xfs_inobt_init_key_from_rec(
	union xfs_btree_key	*key,
	union xfs_btree_rec	*rec)
{
	key->inobt.ir_startino = rec->inobt.ir_startino;
}

STATIC void
xfs_inobt_init_rec_from_key(
	union xfs_btree_key	*key,
	union xfs_btree_rec	*rec)
{
	rec->inobt.ir_startino = key->inobt.ir_startino;
}

STATIC void
xfs_inobt_init_rec_from_cur(
	struct xfs_btree_cur	*cur,
	union xfs_btree_rec	*rec)
{
	rec->inobt.ir_startino = cpu_to_be32(cur->bc_rec.i.ir_startino);
	rec->inobt.ir_freecount = cpu_to_be32(cur->bc_rec.i.ir_freecount);
	rec->inobt.ir_free = cpu_to_be64(cur->bc_rec.i.ir_free);
}

/*
 * intial value of ptr for lookup
 */
STATIC void
xfs_inobt_init_ptr_from_cur(
	struct xfs_btree_cur	*cur,
	union xfs_btree_ptr	*ptr)
{
	struct xfs_agi		*agi = XFS_BUF_TO_AGI(cur->bc_private.a.agbp);

	ASSERT(cur->bc_private.a.agno == be32_to_cpu(agi->agi_seqno));

	ptr->s = agi->agi_root;
}

STATIC __int64_t
xfs_inobt_key_diff(
	struct xfs_btree_cur	*cur,
	union xfs_btree_key	*key)
{
	return (__int64_t)be32_to_cpu(key->inobt.ir_startino) -
			  cur->bc_rec.i.ir_startino;
}

#ifdef XFS_BTREE_TRACE
ktrace_t	*xfs_inobt_trace_buf;

STATIC void
xfs_inobt_trace_enter(
	struct xfs_btree_cur	*cur,
	const char		*func,
	char			*s,
	int			type,
	int			line,
	__psunsigned_t		a0,
	__psunsigned_t		a1,
	__psunsigned_t		a2,
	__psunsigned_t		a3,
	__psunsigned_t		a4,
	__psunsigned_t		a5,
	__psunsigned_t		a6,
	__psunsigned_t		a7,
	__psunsigned_t		a8,
	__psunsigned_t		a9,
	__psunsigned_t		a10)
{
	ktrace_enter(xfs_inobt_trace_buf, (void *)(__psint_t)type,
		(void *)func, (void *)s, NULL, (void *)cur,
		(void *)a0, (void *)a1, (void *)a2, (void *)a3,
		(void *)a4, (void *)a5, (void *)a6, (void *)a7,
		(void *)a8, (void *)a9, (void *)a10);
}

STATIC void
xfs_inobt_trace_cursor(
	struct xfs_btree_cur	*cur,
	__uint32_t		*s0,
	__uint64_t		*l0,
	__uint64_t		*l1)
{
	*s0 = cur->bc_private.a.agno;
	*l0 = cur->bc_rec.i.ir_startino;
	*l1 = cur->bc_rec.i.ir_free;
}

STATIC void
xfs_inobt_trace_key(
	struct xfs_btree_cur	*cur,
	union xfs_btree_key	*key,
	__uint64_t		*l0,
	__uint64_t		*l1)
{
	*l0 = be32_to_cpu(key->inobt.ir_startino);
	*l1 = 0;
}

STATIC void
xfs_inobt_trace_record(
	struct xfs_btree_cur	*cur,
	union xfs_btree_rec	*rec,
	__uint64_t		*l0,
	__uint64_t		*l1,
	__uint64_t		*l2)
{
	*l0 = be32_to_cpu(rec->inobt.ir_startino);
	*l1 = be32_to_cpu(rec->inobt.ir_freecount);
	*l2 = be64_to_cpu(rec->inobt.ir_free);
}
#endif /* XFS_BTREE_TRACE */

static const struct xfs_btree_ops xfs_inobt_ops = {
	.rec_len		= sizeof(xfs_inobt_rec_t),
	.key_len		= sizeof(xfs_inobt_key_t),

	.dup_cursor		= xfs_inobt_dup_cursor,
	.set_root		= xfs_inobt_set_root,
	.alloc_block		= xfs_inobt_alloc_block,
	.free_block		= xfs_inobt_free_block,
	.get_maxrecs		= xfs_inobt_get_maxrecs,
	.init_key_from_rec	= xfs_inobt_init_key_from_rec,
	.init_rec_from_key	= xfs_inobt_init_rec_from_key,
	.init_rec_from_cur	= xfs_inobt_init_rec_from_cur,
	.init_ptr_from_cur	= xfs_inobt_init_ptr_from_cur,
	.key_diff		= xfs_inobt_key_diff,

#ifdef XFS_BTREE_TRACE
	.trace_enter		= xfs_inobt_trace_enter,
	.trace_cursor		= xfs_inobt_trace_cursor,
	.trace_key		= xfs_inobt_trace_key,
	.trace_record		= xfs_inobt_trace_record,
#endif
};

/*
 * Allocate a new inode btree cursor.
 */
struct xfs_btree_cur *				/* new inode btree cursor */
xfs_inobt_init_cursor(
	struct xfs_mount	*mp,		/* file system mount point */
	struct xfs_trans	*tp,		/* transaction pointer */
	struct xfs_buf		*agbp,		/* buffer for agi structure */
	xfs_agnumber_t		agno)		/* allocation group number */
{
	struct xfs_agi		*agi = XFS_BUF_TO_AGI(agbp);
	struct xfs_btree_cur	*cur;

	cur = kmem_zone_zalloc(xfs_btree_cur_zone, KM_SLEEP);

	cur->bc_tp = tp;
	cur->bc_mp = mp;
	cur->bc_nlevels = be32_to_cpu(agi->agi_level);
	cur->bc_btnum = XFS_BTNUM_INO;
	cur->bc_blocklog = mp->m_sb.sb_blocklog;

	cur->bc_ops = &xfs_inobt_ops;

	cur->bc_private.a.agbp = agbp;
	cur->bc_private.a.agno = agno;

	return cur;
}
