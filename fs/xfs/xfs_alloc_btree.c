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
	xfs_alloc_ptr_t	*bnop,		/* i/o: block number inserted */
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
	xfs_alloc_ptr_t	bno,		/* block number to check */
	int		level,		/* level of the block */
	xfs_alloc_key_t	*keyp);		/* value of expected first key */
#endif	/* XFSDEBUG */

/*
 * Log header fields from a btree block.
 */
STATIC void
xfs_alloc_log_block(
	xfs_trans_t	*tp,		/* transaction pointer */
	buf_t		*buf,		/* buffer containing btree block */
	int		fields);	/* mask of fields: XFS_BB_... */

/*
 * Log keys from a btree block (nonleaf).
 */
STATIC void
xfs_alloc_log_keys(
	xfs_btree_cur_t	*cur,		/* btree cursor */
	buf_t		*buf,		/* buffer containing btree block */
	int		kfirst,		/* index of first key to log */
	int		klast);		/* index of last key to log */

/*
 * Log block pointer fields from a btree block (nonleaf).
 */
STATIC void
xfs_alloc_log_ptrs(
	xfs_btree_cur_t	*cur,		/* btree cursor */
	buf_t		*buf,		/* buffer containing btree block */
	int		pfirst,		/* index of first pointer to log */
	int		plast);		/* index of last pointer to log */

/*
 * Log records from a btree block (leaf).
 */
STATIC void
xfs_alloc_log_recs(
	xfs_btree_cur_t	*cur,		/* btree cursor */
	buf_t		*buf,		/* buffer containing btree block */
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
	xfs_alloc_ptr_t	rbno,		/* root block number */
	int		levels);	/* number of levels in the btree */
#endif	/* XFSDEBUG */

#ifdef XFSDEBUG
/*
 * Check one btree block for record consistency.
 */
STATIC xfs_alloc_ptr_t			/* next block to process */
xfs_alloc_rcheck_btree_block(
	xfs_btree_cur_t	*cur,		/* btree cursor */
	xfs_agnumber_t	agno,		/* allocation group number */
	xfs_alloc_ptr_t	bno,		/* block number to check */
	xfs_alloc_ptr_t	*fbno,		/* output: first block at next level */
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
	xfs_alloc_ptr_t	*bnop,		/* output: block number allocated */
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
STATIC int					/* fail/done/go-on */
xfs_alloc_delrec(
	xfs_btree_cur_t		*cur,		/* btree cursor */
	int			level)		/* level removing record from */
{
	buf_t			*agbuf;
	xfs_agf_t		*agf;
	xfs_alloc_block_t	*block;
	xfs_alloc_ptr_t		bno;
	buf_t			*buf;
	int			i;
	xfs_alloc_key_t		key;
	xfs_alloc_key_t		*kp;
	xfs_alloc_ptr_t		lbno;
	buf_t			*lbuf;
	xfs_alloc_block_t	*left;
	xfs_alloc_key_t		*lkp;
	xfs_alloc_ptr_t		*lpp;
	int			lrecs;
	xfs_alloc_rec_t		*lrp;
	xfs_mount_t		*mp;
	xfs_alloc_ptr_t		*pp;
	int			ptr;
	xfs_alloc_ptr_t		rbno;
	buf_t			*rbuf;
	xfs_alloc_block_t	*right;
	xfs_alloc_key_t		*rkp;
	xfs_alloc_rec_t		*rp;
	xfs_alloc_ptr_t		*rpp;
	xfs_alloc_block_t	*rrblock;
	buf_t			*rrbuf;
	int			rrecs;
	xfs_alloc_rec_t		*rrp;
	xfs_btree_cur_t		*tcur;
	xfs_trans_t		*tp;

	xfs_alloc_rcheck(cur);
	tp = cur->bc_tp;
	mp = cur->bc_mp;
	ptr = cur->bc_ptrs[level];
	if (ptr == 0)
		return 0;
	buf = cur->bc_bufs[level];
	block = XFS_BUF_TO_ALLOC_BLOCK(buf);
	xfs_btree_check_sblock(cur, block, level);
	if (ptr > block->bb_numrecs)
		return 0;
	if (level > 0) {
		kp = XFS_ALLOC_KEY_ADDR(block, 1, cur);
		pp = XFS_ALLOC_PTR_ADDR(block, 1, cur);
		for (i = ptr; i < block->bb_numrecs; i++) {
			kp[i - 1] = kp[i];
			xfs_btree_check_sptr(cur, pp[i], level);
			pp[i - 1] = pp[i];
		}
		if (ptr < i) {
			xfs_alloc_log_ptrs(cur, buf, ptr, i - 1);
			xfs_alloc_log_keys(cur, buf, ptr, i - 1);
		}
	} else {
		rp = XFS_ALLOC_REC_ADDR(block, 1, cur);
		for (i = ptr; i < block->bb_numrecs; i++)
			rp[i - 1] = rp[i];
		if (ptr < i)
			xfs_alloc_log_recs(cur, buf, ptr, i - 1);
		if (ptr == 1) {
			key.ar_startblock = rp->ar_startblock;
			key.ar_blockcount = rp->ar_blockcount;
			kp = &key;
		}
	}
	block->bb_numrecs--;
	xfs_alloc_log_block(tp, buf, XFS_BB_NUMRECS);
	agbuf = cur->bc_private.a.agbuf;
	agf = XFS_BUF_TO_AGF(agbuf);
	if (level == 0 &&
	    cur->bc_btnum == XFS_BTNUM_CNT &&
	    block->bb_rightsib == NULLAGBLOCK &&
	    ptr > block->bb_numrecs) {
		if (block->bb_numrecs) {
			rrp = XFS_ALLOC_REC_ADDR(block, block->bb_numrecs, cur);
			agf->agf_longest = rrp->ar_blockcount;
		} else
			agf->agf_longest = 0;
		xfs_alloc_log_agf(tp, agbuf, XFS_AGF_LONGEST);
	}
	/*
	 * We just did a join at the previous level.
	 * Make the cursor point to the good (left) key.
	 */
	if (level > 0)
		xfs_alloc_decrement(cur, level);
	if (level == cur->bc_nlevels - 1) {
		if (block->bb_numrecs == 1 && level > 0) {
			agf->agf_roots[cur->bc_btnum] = *pp;
			agf->agf_levels[cur->bc_btnum]--;
			xfs_alloc_put_freelist(tp, agbuf, buf);
			xfs_alloc_log_agf(tp, agbuf,
				XFS_AGF_ROOTS | XFS_AGF_LEVELS);
			xfs_btree_setbuf(cur, level, 0);
			cur->bc_nlevels--;
		}
		xfs_alloc_rcheck(cur);
		kmem_check();
		return 1;
	}
	if (ptr == 1)
		xfs_alloc_updkey(cur, kp, level + 1);
	xfs_alloc_rcheck(cur);
	if (block->bb_numrecs >= XFS_ALLOC_BLOCK_MINRECS(level, cur)) {
		kmem_check();
		return 1;
	}
	rbno = block->bb_rightsib;
	lbno = block->bb_leftsib;
	bno = NULLAGBLOCK;
	ASSERT(rbno != NULLAGBLOCK || lbno != NULLAGBLOCK);
	tcur = xfs_btree_dup_cursor(cur);
	if (rbno != NULLAGBLOCK) {
		xfs_btree_lastrec(tcur, level);
		xfs_alloc_increment(tcur, level);
		xfs_btree_lastrec(tcur, level);
		rbuf = tcur->bc_bufs[level];
		right = XFS_BUF_TO_ALLOC_BLOCK(rbuf);
		xfs_btree_check_sblock(cur, right, level);
		bno = right->bb_leftsib;
		if (right->bb_numrecs - 1 >=
		    XFS_ALLOC_BLOCK_MINRECS(level, cur)) {
			if (xfs_alloc_lshift(tcur, level)) {
				ASSERT(block->bb_numrecs >=
				       XFS_ALLOC_BLOCK_MINRECS(level, cur));
				xfs_btree_del_cursor(tcur);
				kmem_check();
				return 1;
			}
		}
		rrecs = right->bb_numrecs;
		if (lbno != NULLAGBLOCK) {
			xfs_btree_firstrec(tcur, level);
			xfs_alloc_decrement(tcur, level);
		}
	}
	if (lbno != NULLAGBLOCK) {
		xfs_btree_firstrec(tcur, level);
		xfs_alloc_decrement(tcur, level);	/* to last */
		xfs_btree_firstrec(tcur, level);
		lbuf = tcur->bc_bufs[level];
		left = XFS_BUF_TO_ALLOC_BLOCK(lbuf);
		xfs_btree_check_sblock(cur, left, level);
		bno = left->bb_rightsib;
		if (left->bb_numrecs - 1 >=
		    XFS_ALLOC_BLOCK_MINRECS(level, cur)) {
			if (xfs_alloc_rshift(tcur, level)) {
				ASSERT(block->bb_numrecs >=
				       XFS_ALLOC_BLOCK_MINRECS(level, cur));
				xfs_btree_del_cursor(tcur);
				cur->bc_ptrs[level]++;
				kmem_check();
				return 1;
			}
		}
		lrecs = left->bb_numrecs;
	}
	xfs_btree_del_cursor(tcur);
	ASSERT(bno != NULLAGBLOCK);
	if (lbno != NULLAGBLOCK &&
	    lrecs + block->bb_numrecs <= XFS_ALLOC_BLOCK_MAXRECS(level, cur)) {
		rbno = bno;
		right = block;
		rbuf = buf;
		lbuf = xfs_btree_read_bufs(mp, tp, cur->bc_private.a.agno,
			lbno, 0);
		left = XFS_BUF_TO_ALLOC_BLOCK(lbuf);
		xfs_btree_check_sblock(cur, left, level);
	} else if (rbno != NULLAGBLOCK &&
		   rrecs + block->bb_numrecs <=
		    XFS_ALLOC_BLOCK_MAXRECS(level, cur)) {
		lbno = bno;
		left = block;
		lbuf = buf;
		rbuf = xfs_btree_read_bufs(mp, tp, cur->bc_private.a.agno,
			rbno, 0);
		right = XFS_BUF_TO_ALLOC_BLOCK(rbuf);
		xfs_btree_check_sblock(cur, right, level);
	} else {
		xfs_alloc_rcheck(cur);
		kmem_check();
		return 1;
	}
	if (level > 0) {
		lkp = XFS_ALLOC_KEY_ADDR(left, left->bb_numrecs + 1, cur);
		lpp = XFS_ALLOC_PTR_ADDR(left, left->bb_numrecs + 1, cur);
		rkp = XFS_ALLOC_KEY_ADDR(right, 1, cur);
		rpp = XFS_ALLOC_PTR_ADDR(right, 1, cur);
		for (i = 0; i < right->bb_numrecs; i++) {
			lkp[i] = rkp[i];
			xfs_btree_check_sptr(cur, rpp[i], level);
			lpp[i] = rpp[i];
		}
		xfs_alloc_log_keys(cur, lbuf, left->bb_numrecs + 1,
				   left->bb_numrecs + right->bb_numrecs);
		xfs_alloc_log_ptrs(cur, lbuf, left->bb_numrecs + 1,
				   left->bb_numrecs + right->bb_numrecs);
	} else {
		lrp = XFS_ALLOC_REC_ADDR(left, left->bb_numrecs + 1, cur);
		rrp = XFS_ALLOC_REC_ADDR(right, 1, cur);
		for (i = 0; i < right->bb_numrecs; i++)
			lrp[i] = rrp[i];
		xfs_alloc_log_recs(cur, lbuf, left->bb_numrecs + 1,
				   left->bb_numrecs + right->bb_numrecs);
	}
	if (buf != lbuf) {
		xfs_btree_setbuf(cur, level, lbuf);
		cur->bc_ptrs[level] += left->bb_numrecs;
	} else if (level + 1 < cur->bc_nlevels)
		xfs_alloc_increment(cur, level + 1);
	left->bb_numrecs += right->bb_numrecs;
	left->bb_rightsib = right->bb_rightsib;
	xfs_alloc_log_block(tp, lbuf, XFS_BB_NUMRECS | XFS_BB_RIGHTSIB);
	if (left->bb_rightsib != NULLAGBLOCK) {
		rrbuf = xfs_btree_read_bufs(mp, tp, cur->bc_private.a.agno,
					    left->bb_rightsib, 0);
		rrblock = XFS_BUF_TO_ALLOC_BLOCK(rrbuf);
		xfs_btree_check_sblock(cur, rrblock, level);
		rrblock->bb_leftsib = lbno;
		xfs_alloc_log_block(tp, rrbuf, XFS_BB_LEFTSIB);
	}
	xfs_alloc_put_freelist(tp, agbuf, rbuf);
	xfs_alloc_rcheck(cur);
	kmem_check();
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
	xfs_alloc_ptr_t		*bnop,	/* i/o: block number inserted */
	xfs_alloc_rec_t		*recp,	/* i/o: record data inserted */
	xfs_btree_cur_t		**curp)	/* output: new cursor replacing cur */
{
	buf_t			*agbuf;
	xfs_agf_t		*agf;
	xfs_alloc_block_t	*block;
	buf_t			*buf;
	int			i;
	xfs_alloc_key_t		key;
	xfs_alloc_key_t		*kp;
	xfs_alloc_ptr_t		nbno;
	xfs_btree_cur_t		*ncur;
	xfs_alloc_key_t		nkey;
	xfs_alloc_rec_t		nrec;
	int			optr;
	xfs_alloc_ptr_t		*pp;
	int			ptr;
	xfs_alloc_rec_t		*rp;
	xfs_trans_t		*tp;

	if (level >= cur->bc_nlevels) {
		i = xfs_alloc_newroot(cur);
		*bnop = NULLAGBLOCK;
		kmem_check();
		return i;
	}
	xfs_alloc_rcheck(cur);
	key.ar_startblock = recp->ar_startblock;
	key.ar_blockcount = recp->ar_blockcount;
	optr = ptr = cur->bc_ptrs[level];
	if (ptr == 0) {
		kmem_check();
		return 0;
	}
	buf = cur->bc_bufs[level];
	block = XFS_BUF_TO_ALLOC_BLOCK(buf);
	xfs_btree_check_sblock(cur, block, level);
#ifdef XFSDEBUG
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
	tp = cur->bc_tp;
	nbno = NULLAGBLOCK;
	ncur = (xfs_btree_cur_t *)0;
	if (block->bb_numrecs == XFS_ALLOC_BLOCK_MAXRECS(level, cur)) {
		if (xfs_alloc_rshift(cur, level)) {
			/* nothing */
		} else if (xfs_alloc_lshift(cur, level)) {
			optr = ptr = cur->bc_ptrs[level];
		} else if (xfs_alloc_split(cur, level, &nbno, &nkey, &ncur)) {
			buf = cur->bc_bufs[level];
			block = XFS_BUF_TO_ALLOC_BLOCK(buf);
			xfs_btree_check_sblock(cur, block, level);
			ptr = cur->bc_ptrs[level];
			nrec.ar_startblock = nkey.ar_startblock;
			nrec.ar_blockcount = nkey.ar_blockcount;
		} else {
			kmem_check();
			return 0;
		}
	}
	if (level > 0) {
		kp = XFS_ALLOC_KEY_ADDR(block, 1, cur);
		pp = XFS_ALLOC_PTR_ADDR(block, 1, cur);
		for (i = block->bb_numrecs; i >= ptr; i--) {
			kp[i] = kp[i - 1];
			xfs_btree_check_sptr(cur, pp[i - 1], level);
			pp[i] = pp[i - 1];
		}
		xfs_btree_check_sptr(cur, *bnop, level);
		kp[i] = key;
		pp[i] = *bnop;
		block->bb_numrecs++;
		xfs_alloc_log_keys(cur, buf, ptr, block->bb_numrecs);
		xfs_alloc_log_ptrs(cur, buf, ptr, block->bb_numrecs);
	} else {
		rp = XFS_ALLOC_REC_ADDR(block, 1, cur);
		for (i = block->bb_numrecs; i >= ptr; i--)
			rp[i] = rp[i - 1];
		rp[i] = *recp;
		block->bb_numrecs++;
		xfs_alloc_log_recs(cur, buf, ptr, block->bb_numrecs);
	}
	xfs_alloc_log_block(tp, buf, XFS_BB_NUMRECS);
#ifdef XFSDEBUG
	if (ptr < block->bb_numrecs) {
		if (level == 0)
			xfs_btree_check_rec(cur->bc_btnum, rp + i, rp + i + 1);
		else
			xfs_btree_check_key(cur->bc_btnum, kp + i, kp + i + 1);
	}
#endif
	if (optr == 1)
		xfs_alloc_updkey(cur, &key, level + 1);
	agbuf = cur->bc_private.a.agbuf;
	agf = XFS_BUF_TO_AGF(agbuf);
	if (level == 0 &&
	    cur->bc_btnum == XFS_BTNUM_CNT &&
	    block->bb_rightsib == NULLAGBLOCK &&
	    recp->ar_blockcount > agf->agf_longest) {
		agf->agf_longest = recp->ar_blockcount;
		xfs_alloc_log_agf(tp, agbuf, XFS_AGF_LONGEST);
	}
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
	xfs_alloc_ptr_t		bno,	/* block number to check */
	int			level,	/* level of the block */
	xfs_alloc_key_t		*keyp)	/* value of expected first key */
{
	xfs_alloc_block_t	*block;
	buf_t			*buf;
	int			i;
	xfs_alloc_key_t		key;
	xfs_alloc_key_t		*kp;
	xfs_mount_t		*mp;
	xfs_alloc_ptr_t		*pp;
	xfs_alloc_rec_t		*rp;
	xfs_trans_t		*tp;

	ASSERT(bno != NULLAGBLOCK);
	tp = cur->bc_tp;
	mp = cur->bc_mp;
	buf = xfs_btree_read_bufs(mp, tp, agf->agf_seqno, bno, 0);
	block = XFS_BUF_TO_ALLOC_BLOCK(buf);
	xfs_btree_check_sblock(cur, block, level);
	if (level > 0) {
		kp = XFS_ALLOC_KEY_ADDR(block, 1, cur);
		if (keyp)
			key = *kp;
	} else {
		rp = XFS_ALLOC_REC_ADDR(block, 1, cur);
		if (keyp) {
			key.ar_startblock = rp->ar_startblock;
			key.ar_blockcount = rp->ar_blockcount;
		}
	}
	if (keyp)
		ASSERT(bcmp(keyp, &key, sizeof(key)) == 0);
	if (level > 0) {
		pp = XFS_ALLOC_PTR_ADDR(block, 1, cur);
		if (*pp != NULLAGBLOCK) {
			for (i = 1; i <= block->bb_numrecs; i++, pp++, kp++)
				xfs_alloc_kcheck_btree(cur, agf, *pp,
					level - 1, kp);
		}
	}
	xfs_trans_brelse(tp, buf);
}
#endif

/*
 * Log header fields from a btree block.
 */
STATIC void
xfs_alloc_log_block(
	xfs_trans_t		*tp,	/* transaction pointer */
	buf_t			*buf,	/* buffer containing btree block */
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
	xfs_trans_log_buf(tp, buf, first, last);
	kmem_check();
}

/*
 * Log keys from a btree block (nonleaf).
 */
STATIC void
xfs_alloc_log_keys(
	xfs_btree_cur_t		*cur,	/* btree cursor */
	buf_t			*buf,	/* buffer containing btree block */
	int			kfirst,	/* index of first key to log */
	int			klast)	/* index of last key to log */
{
	xfs_alloc_block_t	*block;
	int			first;
	xfs_alloc_key_t		*kp;
	int			last;

	block = XFS_BUF_TO_ALLOC_BLOCK(buf);
	kp = XFS_ALLOC_KEY_ADDR(block, 1, cur);
	first = (caddr_t)&kp[kfirst - 1] - (caddr_t)block;
	last = ((caddr_t)&kp[klast] - 1) - (caddr_t)block;
	xfs_trans_log_buf(cur->bc_tp, buf, first, last);
	kmem_check();
}

/*
 * Log block pointer fields from a btree block (nonleaf).
 */
STATIC void
xfs_alloc_log_ptrs(
	xfs_btree_cur_t		*cur,	/* btree cursor */
	buf_t			*buf,	/* buffer containing btree block */
	int			pfirst,	/* index of first pointer to log */
	int			plast)	/* index of last pointer to log */
{
	xfs_alloc_block_t	*block;
	int			first;
	int			last;
	xfs_alloc_ptr_t		*pp;

	block = XFS_BUF_TO_ALLOC_BLOCK(buf);
	pp = XFS_ALLOC_PTR_ADDR(block, 1, cur);
	first = (caddr_t)&pp[pfirst - 1] - (caddr_t)block;
	last = ((caddr_t)&pp[plast] - 1) - (caddr_t)block;
	xfs_trans_log_buf(cur->bc_tp, buf, first, last);
	kmem_check();
}

/*
 * Log records from a btree block (leaf).
 */
STATIC void
xfs_alloc_log_recs(
	xfs_btree_cur_t		*cur,	/* btree cursor */
	buf_t			*buf,	/* buffer containing btree block */
	int			rfirst,	/* index of first record to log */
	int			rlast)	/* index of last record to log */
{
	xfs_alloc_block_t	*block;
	int			first;
	int			last;
	xfs_alloc_rec_t		*rp;

	block = XFS_BUF_TO_ALLOC_BLOCK(buf);
	rp = XFS_ALLOC_REC_ADDR(block, 1, cur);
	first = (caddr_t)&rp[rfirst - 1] - (caddr_t)block;
	last = ((caddr_t)&rp[rlast] - 1) - (caddr_t)block;
	xfs_trans_log_buf(cur->bc_tp, buf, first, last);
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
	xfs_alloc_ptr_t		agbno;
	buf_t			*agbuf;
	xfs_agnumber_t		agno;
	xfs_agf_t		*agf;
	xfs_alloc_block_t	*block;
	xfs_extlen_t		blockcount;
	buf_t			*buf;
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
	xfs_trans_t		*tp;

	xfs_alloc_rcheck(cur);
	xfs_alloc_kcheck(cur);
	tp = cur->bc_tp;
	mp = cur->bc_mp;
	agbuf = cur->bc_private.a.agbuf;
	agf = XFS_BUF_TO_AGF(agbuf);
	agno = agf->agf_seqno;
	agbno = agf->agf_roots[cur->bc_btnum];
	rp = &cur->bc_rec.a;
	for (level = cur->bc_nlevels - 1, diff = 1; level >= 0; level--) {
		d = XFS_AGB_TO_DADDR(mp, agno, agbno);
		buf = cur->bc_bufs[level];
		if (buf && buf->b_blkno != d)
			buf = (buf_t *)0;
		if (!buf) {
			buf = xfs_trans_read_buf(tp, mp->m_dev, d,
						 mp->m_bsize, 0);
			ASSERT(buf && !geterror(buf));
			xfs_btree_setbuf(cur, level, buf);
		}
		block = XFS_BUF_TO_ALLOC_BLOCK(buf);
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
	buf_t			*lbuf;
	xfs_alloc_block_t	*left;
	xfs_alloc_key_t		*lkp;
	xfs_alloc_ptr_t		*lpp;
	xfs_alloc_rec_t		*lrp;
	xfs_mount_t		*mp;
	int			nrec;
	buf_t			*rbuf;
	xfs_alloc_block_t	*right;
	xfs_alloc_key_t		*rkp;
	xfs_alloc_ptr_t		*rpp;
	xfs_alloc_rec_t		*rrp;
	xfs_trans_t		*tp;

	xfs_alloc_rcheck(cur);
	rbuf = cur->bc_bufs[level];
	right = XFS_BUF_TO_ALLOC_BLOCK(rbuf);
	xfs_btree_check_sblock(cur, right, level);
	if (right->bb_leftsib == NULLAGBLOCK)
		return 0;
	if (cur->bc_ptrs[level] <= 1)
		return 0;
	tp = cur->bc_tp;
	mp = cur->bc_mp;
	lbuf = xfs_btree_read_bufs(mp, tp, cur->bc_private.a.agno,
		right->bb_leftsib, 0);
	left = XFS_BUF_TO_ALLOC_BLOCK(lbuf);
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
		xfs_alloc_log_keys(cur, lbuf, nrec, nrec);
		lpp = XFS_ALLOC_PTR_ADDR(left, nrec, cur);
		rpp = XFS_ALLOC_PTR_ADDR(right, 1, cur);
		xfs_btree_check_sptr(cur, *rpp, level);
		*lpp = *rpp;
		xfs_alloc_log_ptrs(cur, lbuf, nrec, nrec);
	} else {
		lrp = XFS_ALLOC_REC_ADDR(left, nrec, cur);
		rrp = XFS_ALLOC_REC_ADDR(right, 1, cur);
		*lrp = *rrp;
		xfs_alloc_log_recs(cur, lbuf, nrec, nrec);
	}
	left->bb_numrecs++;
	xfs_alloc_log_block(tp, lbuf, XFS_BB_NUMRECS);
#ifdef XFSDEBUG
	if (level > 0)
		xfs_btree_check_key(cur->bc_btnum, lkp - 1, lkp);
	else
		xfs_btree_check_rec(cur->bc_btnum, lrp - 1, lrp);
#endif
	right->bb_numrecs--;
	xfs_alloc_log_block(tp, rbuf, XFS_BB_NUMRECS);
	if (level > 0) {
		for (i = 0; i < right->bb_numrecs; i++) {
			rkp[i] = rkp[i + 1];
			xfs_btree_check_sptr(cur, rpp[i + 1], level);
			rpp[i] = rpp[i + 1];
		}
		xfs_alloc_log_keys(cur, rbuf, 1, right->bb_numrecs);
		xfs_alloc_log_ptrs(cur, rbuf, 1, right->bb_numrecs);
	} else {
		for (i = 0; i < right->bb_numrecs; i++)
			rrp[i] = rrp[i + 1];
		xfs_alloc_log_recs(cur, rbuf, 1, right->bb_numrecs);
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
	buf_t			*agbuf;
	xfs_agf_t		*agf;
	xfs_alloc_block_t	*block;
	buf_t			*buf;
	xfs_alloc_key_t		*kp;
	xfs_alloc_ptr_t		lbno;
	buf_t			*lbuf;
	xfs_alloc_block_t	*left;
	xfs_mount_t		*mp;
	xfs_alloc_ptr_t		nbno;
	buf_t			*nbuf;
	xfs_alloc_block_t	*new;
	int			nptr;
	xfs_alloc_ptr_t		*pp;
	xfs_alloc_ptr_t		rbno;
	buf_t			*rbuf;
	xfs_alloc_block_t	*right;
	xfs_alloc_rec_t		*rp;
	xfs_trans_t		*tp;

	xfs_alloc_rcheck(cur);
	tp = cur->bc_tp;
	mp = cur->bc_mp;
	ASSERT(cur->bc_nlevels < XFS_AG_MAXLEVELS(mp));
	agbuf = cur->bc_private.a.agbuf;
	agf = XFS_BUF_TO_AGF(agbuf);
	nbno = xfs_alloc_get_freelist(tp, agbuf, &nbuf);
	if (nbno == NULLAGBLOCK)
		return 0;
	new = XFS_BUF_TO_ALLOC_BLOCK(nbuf);
	agf->agf_roots[cur->bc_btnum] = nbno;
	agf->agf_levels[cur->bc_btnum]++;
	xfs_alloc_log_agf(tp, agbuf, XFS_AGF_ROOTS | XFS_AGF_LEVELS);
	buf = cur->bc_bufs[cur->bc_nlevels - 1];
	block = XFS_BUF_TO_ALLOC_BLOCK(buf);
	xfs_btree_check_sblock(cur, block, cur->bc_nlevels - 1);
	if (block->bb_rightsib != NULLAGBLOCK) {
		lbuf = buf;
		lbno = XFS_DADDR_TO_AGBNO(mp, lbuf->b_blkno);
		left = block;
		rbno = left->bb_rightsib;
		rbuf = xfs_btree_read_bufs(mp, tp, cur->bc_private.a.agno,
			rbno, 0);
		buf = rbuf;
		right = XFS_BUF_TO_ALLOC_BLOCK(rbuf);
		xfs_btree_check_sblock(cur, right, cur->bc_nlevels - 1);
		nptr = 1;
	} else {
		rbuf = buf;
		rbno = XFS_DADDR_TO_AGBNO(mp, rbuf->b_blkno);
		right = block;
		lbno = right->bb_leftsib;
		lbuf = xfs_btree_read_bufs(mp, tp, cur->bc_private.a.agno,
			lbno, 0);
		buf = lbuf;
		left = XFS_BUF_TO_ALLOC_BLOCK(lbuf);
		xfs_btree_check_sblock(cur, left, cur->bc_nlevels - 1);
		nptr = 2;
	}
	new->bb_magic = xfs_magics[cur->bc_btnum];
	new->bb_level = (__uint16_t)cur->bc_nlevels;
	new->bb_numrecs = 2;
	new->bb_leftsib = new->bb_rightsib = NULLAGBLOCK;
	xfs_alloc_log_block(tp, nbuf, XFS_BB_ALL_BITS);
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
	xfs_alloc_log_keys(cur, nbuf, 1, 2);
	pp = XFS_ALLOC_PTR_ADDR(new, 1, cur);
	pp[0] = lbno;
	pp[1] = rbno;
	xfs_alloc_log_ptrs(cur, nbuf, 1, 2);
	xfs_btree_setbuf(cur, cur->bc_nlevels, nbuf);
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
	xfs_alloc_ptr_t	rbno,		/* root block number */
	int		levels)		/* number of levels in the btree */
{
	xfs_alloc_ptr_t	bno;
	xfs_alloc_ptr_t	fbno;
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
				bno, (xfs_alloc_ptr_t *)0, rp, level);
		}
	}
}

/*
 * Check one btree block for record consistency.
 */
STATIC xfs_alloc_ptr_t			/* next block to process */
xfs_alloc_rcheck_btree_block(
	xfs_btree_cur_t		*cur,	/* btree cursor */
	xfs_agnumber_t		agno,	/* allocation group number */
	xfs_alloc_ptr_t		bno,	/* block number to check */
	xfs_alloc_ptr_t		*fbno,	/* output: first block at next level */
	void			*rec,	/* previous record/key value */
	int			level)	/* level of this block */
{
	xfs_alloc_block_t	*block;
	buf_t			*buf;
	int			i;
	xfs_alloc_key_t		*keyp;
	xfs_alloc_key_t		*kp;
	xfs_mount_t		*mp;
	xfs_alloc_ptr_t		*pp;
	xfs_alloc_ptr_t		rbno;
	xfs_alloc_rec_t		*recp;
	xfs_alloc_rec_t		*rp;
	xfs_trans_t		*tp;

	tp = cur->bc_tp;
	mp = cur->bc_mp;
	buf = xfs_btree_read_bufs(mp, tp, agno, bno, 0);
	block = XFS_BUF_TO_ALLOC_BLOCK(buf);
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
	xfs_trans_brelse(tp, buf);
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
	buf_t			*lbuf;
	xfs_alloc_block_t	*left;
	xfs_alloc_key_t		*lkp;
	xfs_alloc_ptr_t		*lpp;
	xfs_alloc_rec_t		*lrp;
	xfs_mount_t		*mp;
	buf_t			*rbuf;
	xfs_alloc_block_t	*right;
	xfs_alloc_key_t		*rkp;
	xfs_alloc_ptr_t		*rpp;
	xfs_alloc_rec_t		*rrp;
	xfs_btree_cur_t		*tcur;
	xfs_trans_t		*tp;

	xfs_alloc_rcheck(cur);
	lbuf = cur->bc_bufs[level];
	left = XFS_BUF_TO_ALLOC_BLOCK(lbuf);
	xfs_btree_check_sblock(cur, left, level);
	if (left->bb_rightsib == NULLAGBLOCK)
		return 0;
	if (cur->bc_ptrs[level] >= left->bb_numrecs)
		return 0;
	tp = cur->bc_tp;
	mp = cur->bc_mp;
	rbuf = xfs_btree_read_bufs(mp, tp, cur->bc_private.a.agno,
		left->bb_rightsib, 0);
	right = XFS_BUF_TO_ALLOC_BLOCK(rbuf);
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
		xfs_alloc_log_keys(cur, rbuf, 1, right->bb_numrecs + 1);
		xfs_alloc_log_ptrs(cur, rbuf, 1, right->bb_numrecs + 1);
	} else {
		lrp = XFS_ALLOC_REC_ADDR(left, left->bb_numrecs, cur);
		rrp = XFS_ALLOC_REC_ADDR(right, 1, cur);
		for (i = right->bb_numrecs - 1; i >= 0; i--)
			rrp[i + 1] = rrp[i];
		*rrp = *lrp;
		xfs_alloc_log_recs(cur, rbuf, 1, right->bb_numrecs + 1);
		key.ar_startblock = rrp->ar_startblock;
		key.ar_blockcount = rrp->ar_blockcount;
		rkp = &key;
	}
	left->bb_numrecs--;
	xfs_alloc_log_block(tp, lbuf, XFS_BB_NUMRECS);
	right->bb_numrecs++;
#ifdef XFSDEBUG
	if (level > 0)
		xfs_btree_check_key(cur->bc_btnum, rkp, rkp + 1);
	else
		xfs_btree_check_rec(cur->bc_btnum, rrp, rrp + 1);
#endif
	xfs_alloc_log_block(tp, rbuf, XFS_BB_NUMRECS);
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
	xfs_alloc_ptr_t		*bnop,	/* output: block number allocated */
	xfs_alloc_key_t		*keyp,	/* output: first key of new block */
	xfs_btree_cur_t		**curp)	/* output: new cursor */
{
	buf_t			*agbuf;
	xfs_agf_t		*agf;
	daddr_t			d;
	int			i;
	xfs_alloc_key_t		key;
	xfs_alloc_ptr_t		lbno;
	buf_t			*lbuf;
	xfs_alloc_block_t	*left;
	xfs_alloc_key_t		*lkp;
	xfs_alloc_ptr_t		*lpp;
	xfs_alloc_rec_t		*lrp;
	xfs_mount_t		*mp;
	xfs_alloc_ptr_t		rbno;
	buf_t			*rbuf;
	xfs_alloc_rec_t		rec;
	xfs_alloc_block_t	*right;
	xfs_alloc_key_t		*rkp;
	xfs_alloc_ptr_t		*rpp;
	xfs_alloc_block_t	*rrblock;
	buf_t			*rrbuf;
	xfs_alloc_rec_t		*rrp;
	xfs_trans_t		*tp;

	xfs_alloc_rcheck(cur);
	tp = cur->bc_tp;
	mp = cur->bc_mp;
	agbuf = cur->bc_private.a.agbuf;
	agf = XFS_BUF_TO_AGF(agbuf);
	rbno = xfs_alloc_get_freelist(tp, agbuf, &rbuf);
	if (rbno == NULLAGBLOCK) {
		kmem_check();
		return 0;
	}
	right = XFS_BUF_TO_ALLOC_BLOCK(rbuf);
	lbuf = cur->bc_bufs[level];
	left = XFS_BUF_TO_ALLOC_BLOCK(lbuf);
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
		xfs_alloc_log_keys(cur, rbuf, 1, right->bb_numrecs);
		xfs_alloc_log_ptrs(cur, rbuf, 1, right->bb_numrecs);
		*keyp = *rkp;
	} else {
		lrp = XFS_ALLOC_REC_ADDR(left, i, cur);
		rrp = XFS_ALLOC_REC_ADDR(right, 1, cur);
		for (i = 0; i < right->bb_numrecs; i++)
			rrp[i] = lrp[i];
		xfs_alloc_log_recs(cur, rbuf, 1, right->bb_numrecs);
		keyp->ar_startblock = rrp->ar_startblock;
		keyp->ar_blockcount = rrp->ar_blockcount;
	}
	d = lbuf->b_blkno;
	lbno = XFS_DADDR_TO_AGBNO(mp, d);
	left->bb_numrecs -= right->bb_numrecs;
	right->bb_rightsib = left->bb_rightsib;
	left->bb_rightsib = rbno;
	right->bb_leftsib = lbno;
	xfs_alloc_log_block(tp, rbuf, XFS_BB_ALL_BITS);
	xfs_alloc_log_block(tp, lbuf, XFS_BB_NUMRECS | XFS_BB_RIGHTSIB);
	if (right->bb_rightsib != NULLAGBLOCK) {
		rrbuf = xfs_btree_read_bufs(mp, tp, cur->bc_private.a.agno,
			right->bb_rightsib, 0);
		rrblock = XFS_BUF_TO_ALLOC_BLOCK(rrbuf);
		xfs_btree_check_sblock(cur, rrblock, level);
		rrblock->bb_leftsib = rbno;
		xfs_alloc_log_block(tp, rrbuf, XFS_BB_LEFTSIB);
	}
	if (cur->bc_ptrs[level] > left->bb_numrecs + 1) {
		xfs_btree_setbuf(cur, level, rbuf);
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
	xfs_alloc_block_t	*block;
	buf_t			*buf;
	xfs_alloc_key_t		*kp;
	xfs_mount_t		*mp;
	int			ptr;
	xfs_trans_t		*tp;

	xfs_alloc_rcheck(cur);
	tp = cur->bc_tp;
	mp = cur->bc_mp;
	for (ptr = 1; ptr == 1 && level < cur->bc_nlevels; level++) {
		buf = cur->bc_bufs[level];
		block = XFS_BUF_TO_ALLOC_BLOCK(buf);
		xfs_btree_check_sblock(cur, block, level);
		ptr = cur->bc_ptrs[level];
		kp = XFS_ALLOC_KEY_ADDR(block, ptr, cur);
		*kp = *keyp;
		xfs_alloc_log_keys(cur, buf, ptr, ptr);
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
	buf_t			*buf;	/* buffer containing btree block */
	int			lev;	/* btree level */
	xfs_mount_t		*mp;	/* file system mount point */
	xfs_trans_t		*tp;	/* transaction pointer */

	/*
	 * Decrement the ptr at this level.  If we're still in the block
	 * then we're done.
	 */
	if (--cur->bc_ptrs[level] > 0)
		return 1;
	/*
	 * Get a pointer to the btree block.
	 */
	buf = cur->bc_bufs[level];
	block = XFS_BUF_TO_ALLOC_BLOCK(buf);
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
	tp = cur->bc_tp;
	mp = cur->bc_mp;
	for (; lev > level; lev--) {
		xfs_alloc_ptr_t	agbno;	/* block number of btree block */

		buf = cur->bc_bufs[lev];
		block = XFS_BUF_TO_ALLOC_BLOCK(buf);
		xfs_btree_check_sblock(cur, block, lev);
		agbno = *XFS_ALLOC_PTR_ADDR(block, cur->bc_ptrs[lev], cur);
		buf = xfs_btree_read_bufs(mp, tp, cur->bc_private.a.agno,
			agbno, 0);
		xfs_btree_setbuf(cur, lev - 1, buf);
		block = XFS_BUF_TO_ALLOC_BLOCK(buf);
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
	buf_t			*buf;	/* buffer containing btree block */
	int			ptr;	/* record number */
	xfs_alloc_rec_t		*rec;	/* record data */

	buf = cur->bc_bufs[0];
	ptr = cur->bc_ptrs[0];
	block = XFS_BUF_TO_ALLOC_BLOCK(buf);
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
	buf_t			*buf;	/* buffer containing btree block */
	int			lev;	/* btree level */
	xfs_mount_t		*mp;	/* file system mount point */
	xfs_trans_t		*tp;	/* transaction pointer */

	/*
	 * Get a pointer to the btree block.
	 */
	buf = cur->bc_bufs[level];
	block = XFS_BUF_TO_ALLOC_BLOCK(buf);
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
		buf = cur->bc_bufs[lev];
		block = XFS_BUF_TO_ALLOC_BLOCK(buf);
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
	tp = cur->bc_tp;
	mp = cur->bc_mp;
	for (; lev > level; lev--) {
		xfs_alloc_ptr_t	agbno;	/* block number of btree block */

		buf = cur->bc_bufs[lev];
		block = XFS_BUF_TO_ALLOC_BLOCK(buf);
		xfs_btree_check_sblock(cur, block, lev);
		agbno = *XFS_ALLOC_PTR_ADDR(block, cur->bc_ptrs[lev], cur);
		buf = xfs_btree_read_bufs(mp, tp, cur->bc_private.a.agno,
			agbno, 0);
		xfs_btree_setbuf(cur, lev - 1, buf);
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
	xfs_alloc_ptr_t	nbno;		/* new block number (split result) */
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
	buf_t		*agbuf;
	xfs_agf_t	*agf;
	xfs_agblock_t	bno;
	int		levels;

	agbuf = cur->bc_private.a.agbuf;
	agf = XFS_BUF_TO_AGF(agbuf);
	bno = agf->agf_roots[cur->bc_btnum];
	levels = agf->agf_levels[cur->bc_btnum];
	ASSERT(levels == cur->bc_nlevels);
	xfs_alloc_kcheck_btree(cur, agf, (xfs_alloc_ptr_t)bno, levels - 1,
		(xfs_alloc_key_t *)0);
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
	buf_t		*agbuf;
	xfs_agf_t	*agf;
	xfs_agblock_t	bno;
	int		levels;

	agbuf = cur->bc_private.a.agbuf;
	agf = XFS_BUF_TO_AGF(agbuf);
	bno = agf->agf_roots[cur->bc_btnum];
	levels = agf->agf_levels[cur->bc_btnum];
	xfs_alloc_rcheck_btree(cur, agf, (xfs_alloc_ptr_t)bno, levels);
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
	buf_t			*agbuf;	/* a.g. freespace header buffer */
	xfs_agf_t		*agf;	/* a.g. freespace header */
	xfs_alloc_block_t	*block;	/* btree block to update */
	buf_t			*buf;	/* buffer containing btree block */
	xfs_alloc_key_t		key;	/* key containing [bno, len] */
	xfs_mount_t		*mp;	/* mount point for filesystem */
	int			ptr;	/* current record number (updating) */
	xfs_alloc_rec_t		*rp;	/* pointer to updated record */
	xfs_trans_t		*tp;	/* transaction pointer */

	/*
	 * Pick up the a.g. freelist struct and the current block.
	 */
	agbuf = cur->bc_private.a.agbuf;
	agf = XFS_BUF_TO_AGF(agbuf);
	xfs_alloc_rcheck(cur);
	buf = cur->bc_bufs[0];
	block = XFS_BUF_TO_ALLOC_BLOCK(buf);
	xfs_btree_check_sblock(cur, block, 0);
	/*
	 * Get the address of the rec to be updated.
	 */
	ptr = cur->bc_ptrs[0];
	tp = cur->bc_tp;
	mp = cur->bc_mp;
	rp = XFS_ALLOC_REC_ADDR(block, ptr, cur);
	/*
	 * Fill in the new contents and log them.
	 */
	rp->ar_startblock = bno;
	rp->ar_blockcount = len;
	xfs_alloc_log_recs(cur, buf, ptr, ptr);
	/*
	 * If it's the by-size btree and it's the last leaf block and
	 * it's the last record... then update the size of the longest
	 * extent in the a.g., which we cache in the a.g. freelist header.
	 */
	if (cur->bc_btnum == XFS_BTNUM_CNT &&
	    block->bb_rightsib == NULLAGBLOCK &&
	    ptr == block->bb_numrecs) {
		agf->agf_longest = len;
		xfs_alloc_log_agf(tp, agbuf, XFS_AGF_LONGEST);
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
