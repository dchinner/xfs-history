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
#include "xfs_alloc.h"
#include "xfs_ialloc.h"
#include "xfs_bmap.h"
#include "xfs_btree.h"
#ifdef SIM
#include "sim.h"
#endif

#if !defined(SIM) || !defined(XFSDEBUG)
#define	kmem_check()		/* dummy for memory-allocation checking */
#endif

#ifdef XFSDEBUG
/*
 * Prototypes for the debugging routines
 */

/*
 * Check key consistency in the btree given by cur.
 */
STATIC void
xfs_alloc_kcheck(
	xfs_btree_cur_t	*cur);		/* btree cursor */

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

/*
 * Check consistency in the given btree.
 * Checks header consistency and that keys/records are in the right order.
 */
STATIC void
xfs_alloc_rcheck(
	xfs_btree_cur_t	*cur);		/* btree cursor */

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

#else
/*
 * Dummy defines for the visible debugging routines.
 */
#define	xfs_alloc_kcheck(a)
#define	xfs_alloc_rcheck(a)
#endif

/*
 * Prototypes for internal functions.
 */

STATIC xfs_agblock_t
xfs_alloc_compute_diff(
	xfs_agblock_t	wantbno,
	xfs_extlen_t	wantlen,
	xfs_agblock_t	freebno,
	xfs_extlen_t	freelen,
	xfs_agblock_t	*newbnop);

STATIC int
xfs_alloc_decrement(
	xfs_btree_cur_t	*cur,
	int		level);

STATIC int
xfs_alloc_delete(
	xfs_btree_cur_t	*cur);

STATIC int
xfs_alloc_delrec(
	xfs_btree_cur_t	*cur,
	int		level);

STATIC xfs_agblock_t
xfs_alloc_get_freelist(
	xfs_trans_t	*tp,
	buf_t		*agbuf,
	buf_t		**bufp);

STATIC int
xfs_alloc_get_rec(
	xfs_btree_cur_t	*cur,
	xfs_agblock_t	*bno,
	xfs_extlen_t	*len);

STATIC int
xfs_alloc_increment(
	xfs_btree_cur_t	*cur,
	int		level);

STATIC int
xfs_alloc_insert(
	xfs_btree_cur_t	*cur);

STATIC int
xfs_alloc_insrec(
	xfs_btree_cur_t	*cur,
	int		level,
	xfs_alloc_ptr_t	*bnop,
	xfs_alloc_rec_t	*recp,
	xfs_btree_cur_t	**curp);

STATIC void
xfs_alloc_log_agf(
	xfs_trans_t	*tp,
	buf_t		*buf,
	int		fields);

STATIC void
xfs_alloc_log_block(
	xfs_trans_t	*tp,
	buf_t		*buf,
	int		fields);

STATIC void
xfs_alloc_log_keys(
	xfs_btree_cur_t	*cur,
	buf_t		*buf,
	int		kfirst,
	int		klast);

STATIC void
xfs_alloc_log_ptrs(
	xfs_btree_cur_t	*cur,
	buf_t		*buf,
	int		pfirst,
	int		plast);

STATIC void
xfs_alloc_log_recs(
	xfs_btree_cur_t	*cur,
	buf_t		*buf,
	int		rfirst,
	int		rlast);

STATIC int
xfs_alloc_lookup(
	xfs_btree_cur_t	*cur,
	xfs_lookup_t	dir);

STATIC int
xfs_alloc_lookup_eq(
	xfs_btree_cur_t	*cur,
	xfs_agblock_t	bno,
	xfs_extlen_t	len);

STATIC int
xfs_alloc_lookup_ge(
	xfs_btree_cur_t	*cur,
	xfs_agblock_t	bno,
	xfs_extlen_t	len);

STATIC int
xfs_alloc_lookup_le(
	xfs_btree_cur_t	*cur,
	xfs_agblock_t	bno,
	xfs_extlen_t	len);

STATIC int
xfs_alloc_lshift(
	xfs_btree_cur_t	*cur,
	int		level);

STATIC int
xfs_alloc_newroot(
	xfs_btree_cur_t	*cur);

STATIC void
xfs_alloc_put_freelist(
	xfs_trans_t	*tp,
	buf_t		*agbuf,
	buf_t		*buf);

STATIC buf_t *
xfs_alloc_read_agf(
	xfs_mount_t	*mp,
	xfs_trans_t	*tp,
	xfs_agnumber_t	agno,
	int		flags);

STATIC int
xfs_alloc_rshift(
	xfs_btree_cur_t	*cur,
	int		level);

STATIC int
xfs_alloc_split(
	xfs_btree_cur_t	*cur,
	int		level,
	xfs_alloc_ptr_t	*bnop,
	xfs_alloc_key_t	*keyp,
	xfs_btree_cur_t	**curp);

STATIC int
xfs_alloc_update(
	xfs_btree_cur_t	*cur,
	xfs_agblock_t	bno,
	xfs_extlen_t	len);

STATIC void
xfs_alloc_updkey(
	xfs_btree_cur_t	*cur,
	xfs_alloc_key_t	*keyp,
	int		level);

/*
 * Prototypes for per-ag allocation routines
 */

STATIC xfs_agblock_t
xfs_alloc_ag_vextent(
	xfs_trans_t	*tp,
	buf_t		*agbuf,
	xfs_agnumber_t	agno,
	xfs_agblock_t	bno,
	xfs_extlen_t	minlen,
	xfs_extlen_t	maxlen,
	xfs_extlen_t	*len,
	xfs_alloctype_t	type);

STATIC xfs_agblock_t
xfs_alloc_ag_vextent_exact(
	xfs_trans_t	*tp,
	buf_t		*agbuf,
	xfs_agnumber_t	agno,
	xfs_agblock_t	bno,
	xfs_extlen_t	minlen,
	xfs_extlen_t	maxlen,
	xfs_extlen_t	*len);

STATIC xfs_agblock_t
xfs_alloc_ag_vextent_near(
	xfs_trans_t	*tp,
	buf_t		*agbuf,
	xfs_agnumber_t	agno,
	xfs_agblock_t	bno,
	xfs_extlen_t	minlen,
	xfs_extlen_t	maxlen,
	xfs_extlen_t	*len);

STATIC xfs_agblock_t
xfs_alloc_ag_vextent_size(
	xfs_trans_t	*tp,
	buf_t		*agbuf,
	xfs_agnumber_t	agno,
	xfs_extlen_t	minlen,
	xfs_extlen_t	maxlen,
	xfs_extlen_t	*len);

STATIC int
xfs_free_ag_extent(
	xfs_trans_t	*tp,
	buf_t		*agbuf,
	xfs_agnumber_t	agno,
	xfs_agblock_t	bno,
	xfs_extlen_t	len);

/*
 * Internal functions.
 */

/*
 * Compute best start block and diff for "near" allocations.
 * freelen >= wantlen already checked by caller.
 */
STATIC xfs_agblock_t
xfs_alloc_compute_diff(xfs_agblock_t wantbno, xfs_extlen_t wantlen,
		       xfs_agblock_t freebno, xfs_extlen_t freelen,
		       xfs_agblock_t *newbnop)
{
	xfs_agblock_t freeend;
	xfs_agblock_t newbno;
	xfs_agblock_t wantend;

	freeend = freebno + freelen;
	wantend = wantbno + wantlen;
	if (freebno >= wantbno)
		newbno = freebno;
	else if (freeend >= wantend)
		newbno = wantbno;
	else
		newbno = freeend - wantlen;
	*newbnop = newbno;
	return newbno <= wantbno ? wantbno - newbno : newbno - wantbno;
}

/*
 * Decrement cursor by one record at the level.
 * For nonzero levels the leaf-ward information is untouched.
 */
STATIC int
xfs_alloc_decrement(xfs_btree_cur_t *cur, int level)
{
	xfs_alloc_ptr_t agbno;
	xfs_btree_sblock_t *block;
	buf_t *buf;
	int lev;
	xfs_mount_t *mp;
	xfs_trans_t *tp;

	if (--cur->bc_ptrs[level] > 0)
		return 1;
	buf = cur->bc_bufs[level];
	block = xfs_buf_to_sblock(buf);
	xfs_btree_check_sblock(cur, block, level);
	if (block->bb_leftsib == NULLAGBLOCK)
		return 0;
	for (lev = level + 1; lev < cur->bc_nlevels; lev++) {
		if (--cur->bc_ptrs[lev] > 0)
			break;
	}
	if (lev == cur->bc_nlevels)
		return 0;
	tp = cur->bc_tp;
	mp = cur->bc_mp;
	for (; lev > level; lev--) {
		buf = cur->bc_bufs[lev];
		block = xfs_buf_to_sblock(buf);
		xfs_btree_check_sblock(cur, block, lev);
		agbno = *XFS_ALLOC_PTR_ADDR(block, cur->bc_ptrs[lev], cur);
		buf = xfs_btree_read_bufs(mp, tp, cur->bc_private.a.agno, agbno, 0);
		xfs_btree_setbuf(cur, lev - 1, buf);
		block = xfs_buf_to_sblock(buf);
		xfs_btree_check_sblock(cur, block, lev - 1);
		cur->bc_ptrs[lev - 1] = block->bb_numrecs;
	}
	kmem_check();
	return 1;
}

/*
 * Delete the record pointed to by cur.
 */
STATIC int
xfs_alloc_delete(xfs_btree_cur_t *cur)
{
	int i;
	int level;

	for (level = 0, i = 2; i == 2; level++)
		i = xfs_alloc_delrec(cur, level);
	xfs_alloc_kcheck(cur);
	return i;
}

/*
 * Delete record pointed to by cur/level.
 */
STATIC int
xfs_alloc_delrec(xfs_btree_cur_t *cur, int level)
{
	buf_t *agbuf;
	xfs_agf_t *agf;
	xfs_btree_sblock_t *block;
	xfs_alloc_ptr_t bno = NULLAGBLOCK;
	buf_t *buf;
	int i;
	xfs_alloc_key_t key;
	xfs_alloc_key_t *kp;
	xfs_alloc_ptr_t lbno;
	buf_t *lbuf;
	xfs_btree_sblock_t *left;
	xfs_alloc_key_t *lkp;
	xfs_alloc_ptr_t *lpp;
	int lrecs;
	xfs_alloc_rec_t *lrp;
	xfs_mount_t *mp;
	xfs_alloc_ptr_t *pp;
	int ptr;
	xfs_alloc_ptr_t rbno;
	buf_t *rbuf;
	xfs_btree_sblock_t *right;
	xfs_alloc_rec_t *rp;
	xfs_alloc_key_t *rkp;
	xfs_alloc_ptr_t *rpp;
	xfs_btree_sblock_t *rrblock;
	buf_t *rrbuf;
	int rrecs;
	xfs_alloc_rec_t *rrp;
	xfs_btree_cur_t *tcur;
	xfs_trans_t *tp;

	xfs_alloc_rcheck(cur);
	tp = cur->bc_tp;
	mp = cur->bc_mp;
	ptr = cur->bc_ptrs[level];
	if (ptr == 0)
		return 0;
	buf = cur->bc_bufs[level];
	block = xfs_buf_to_sblock(buf);
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
	agf = xfs_buf_to_agf(agbuf);
	if (level == 0 && cur->bc_btnum == XFS_BTNUM_CNT &&
	    block->bb_rightsib == NULLAGBLOCK && ptr > block->bb_numrecs) {
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
	kmem_check();
	if (block->bb_numrecs >= XFS_ALLOC_BLOCK_MINRECS(level, cur))
		return 1;
	rbno = block->bb_rightsib;
	lbno = block->bb_leftsib;
	ASSERT(rbno != NULLAGBLOCK || lbno != NULLAGBLOCK);
	tcur = xfs_btree_dup_cursor(cur);
	if (rbno != NULLAGBLOCK) {
		xfs_btree_lastrec(tcur, level);
		xfs_alloc_increment(tcur, level);
		xfs_btree_lastrec(tcur, level);
		rbuf = tcur->bc_bufs[level];
		right = xfs_buf_to_sblock(rbuf);
		xfs_btree_check_sblock(cur, right, level);
		bno = right->bb_leftsib;
		if (right->bb_numrecs - 1 >= XFS_ALLOC_BLOCK_MINRECS(level, cur)) {
			if (xfs_alloc_lshift(tcur, level)) {
				ASSERT(block->bb_numrecs >= XFS_ALLOC_BLOCK_MINRECS(level, cur));
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
		left = xfs_buf_to_sblock(lbuf);
		xfs_btree_check_sblock(cur, left, level);
		bno = left->bb_rightsib;
		if (left->bb_numrecs - 1 >= XFS_ALLOC_BLOCK_MINRECS(level, cur)) {
			if (xfs_alloc_rshift(tcur, level)) {
				ASSERT(block->bb_numrecs >= XFS_ALLOC_BLOCK_MINRECS(level, cur));
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
		lbuf = xfs_btree_read_bufs(mp, tp, cur->bc_private.a.agno, lbno, 0);
		left = xfs_buf_to_sblock(lbuf);
		xfs_btree_check_sblock(cur, left, level);
	} else if (rbno != NULLAGBLOCK &&
		   rrecs + block->bb_numrecs <= XFS_ALLOC_BLOCK_MAXRECS(level, cur)) {
		lbno = bno;
		left = block;
		lbuf = buf;
		rbuf = xfs_btree_read_bufs(mp, tp, cur->bc_private.a.agno, rbno, 0);
		right = xfs_buf_to_sblock(rbuf);
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
		rrblock = xfs_buf_to_sblock(rrbuf);
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
 * Get a block from the freelist.
 * Returns with the buffer gotten.
 */
STATIC xfs_agblock_t
xfs_alloc_get_freelist(xfs_trans_t *tp, buf_t *agbuf, buf_t **bufp)
{
	xfs_agf_t *agf;
	xfs_agblock_t bno;
	xfs_btree_sblock_t *block;
	buf_t *buf;
	xfs_mount_t *mp;

	mp = tp->t_mountp;
	agf = xfs_buf_to_agf(agbuf);
	bno = agf->agf_freelist;
	if (bno == NULLAGBLOCK)
		return NULLAGBLOCK;
	buf = xfs_btree_read_bufs(mp, tp, agf->agf_seqno, bno, 0);
	block = xfs_buf_to_sblock(buf);
	agf->agf_freecount--;
	agf->agf_freelist = *(xfs_agblock_t *)block;
	xfs_alloc_log_agf(tp, agbuf, XFS_AGF_FREELIST | XFS_AGF_FREECOUNT);
	*bufp = buf;
	kmem_check();
	return bno;
}

/* 
 * Get the data from the pointed-to record.
 */
STATIC int
xfs_alloc_get_rec(xfs_btree_cur_t *cur, xfs_agblock_t *bno, xfs_extlen_t *len)
{
	xfs_btree_sblock_t *block;
	buf_t *buf;
	int ptr;
	xfs_alloc_rec_t *rec;

	buf = cur->bc_bufs[0];
	ptr = cur->bc_ptrs[0];
	block = xfs_buf_to_sblock(buf);
	xfs_btree_check_sblock(cur, block, 0);
	if (ptr > block->bb_numrecs || ptr <= 0)
		return 0;
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
STATIC int
xfs_alloc_increment(xfs_btree_cur_t *cur, int level)
{
	xfs_alloc_ptr_t agbno;
	xfs_btree_sblock_t *block;
	buf_t *buf;
	int lev;
	xfs_mount_t *mp;
	xfs_trans_t *tp;

	buf = cur->bc_bufs[level];
	block = xfs_buf_to_sblock(buf);
	xfs_btree_check_sblock(cur, block, level);
	if (++cur->bc_ptrs[level] <= block->bb_numrecs)
		return 1;
	if (block->bb_rightsib == NULLAGBLOCK)
		return 0;
	for (lev = level + 1; lev < cur->bc_nlevels; lev++) {
		buf = cur->bc_bufs[lev];
		block = xfs_buf_to_sblock(buf);
		xfs_btree_check_sblock(cur, block, lev);
		if (++cur->bc_ptrs[lev] <= block->bb_numrecs)
			break;
	}
	kmem_check();
	if (lev == cur->bc_nlevels)
		return 0;
	tp = cur->bc_tp;
	mp = cur->bc_mp;
	for (; lev > level; lev--) {
		buf = cur->bc_bufs[lev];
		block = xfs_buf_to_sblock(buf);
		xfs_btree_check_sblock(cur, block, lev);
		agbno = *XFS_ALLOC_PTR_ADDR(block, cur->bc_ptrs[lev], cur);
		buf = xfs_btree_read_bufs(mp, tp, cur->bc_private.a.agno, agbno, 0);
		xfs_btree_setbuf(cur, lev - 1, buf);
		cur->bc_ptrs[lev - 1] = 1;
	}
	kmem_check();
	return 1;
}

/*
 * Insert the current record at the point referenced by cur.
 */
STATIC int
xfs_alloc_insert(xfs_btree_cur_t *cur)
{
	int i;
	int level;
	xfs_alloc_ptr_t nbno;
	xfs_btree_cur_t *ncur;
	xfs_alloc_rec_t nrec;
	xfs_btree_cur_t *pcur;

	level = 0;
	nbno = NULLAGBLOCK;
	nrec = cur->bc_rec.a;
	ncur = (xfs_btree_cur_t *)0;
	pcur = cur;
	do {
		i = xfs_alloc_insrec(pcur, level++, &nbno, &nrec, &ncur);
		if (pcur != cur && (ncur || nbno == NULLAGBLOCK)) {
			cur->bc_nlevels = pcur->bc_nlevels;
			xfs_btree_del_cursor(pcur);
		}
		if (ncur) {
			pcur = ncur;
			ncur = (xfs_btree_cur_t *)0;
		}
		kmem_check();
	} while (nbno != NULLAGBLOCK);
	kmem_check();
	return i;
}

/*
 * Insert one record/level.  Return information to the caller
 * allowing the next level up to proceed if necessary.
 */
STATIC int
xfs_alloc_insrec(xfs_btree_cur_t *cur, int level, xfs_alloc_ptr_t *bnop, xfs_alloc_rec_t *recp, xfs_btree_cur_t **curp)
{
	buf_t *agbuf;
	xfs_agf_t *agf;
	xfs_btree_sblock_t *block;
	buf_t *buf;
	int i;
	xfs_alloc_key_t key;
	xfs_alloc_key_t *kp;
	xfs_alloc_ptr_t nbno;
	xfs_btree_cur_t *ncur = (xfs_btree_cur_t *)0;
	xfs_alloc_key_t nkey;
	xfs_alloc_rec_t nrec;
	int optr;
	xfs_alloc_ptr_t *pp;
	int ptr;
	xfs_alloc_rec_t *rp;
	xfs_trans_t *tp;

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
	kmem_check();
	if (ptr == 0)
		return 0;
	buf = cur->bc_bufs[level];
	block = xfs_buf_to_sblock(buf);
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
	if (block->bb_numrecs == XFS_ALLOC_BLOCK_MAXRECS(level, cur)) {
		if (xfs_alloc_rshift(cur, level)) {
			/* nothing */
		} else if (xfs_alloc_lshift(cur, level)) {
			optr = ptr = cur->bc_ptrs[level];
		} else if (xfs_alloc_split(cur, level, &nbno, &nkey, &ncur)) {
			buf = cur->bc_bufs[level];
			block = xfs_buf_to_sblock(buf);
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
	agf = xfs_buf_to_agf(agbuf);
	if (level == 0 && cur->bc_btnum == XFS_BTNUM_CNT &&
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
 * Check key consistency in the btree given by cur.
 */
STATIC void
xfs_alloc_kcheck(
	xfs_btree_cur_t	*cur)		/* btree cursor */
{
	buf_t *agbuf;
	xfs_agf_t *agf;
	xfs_agblock_t bno;
	int levels;

	agbuf = cur->bc_private.a.agbuf;
	agf = xfs_buf_to_agf(agbuf);
	bno = agf->agf_roots[cur->bc_btnum];
	levels = agf->agf_levels[cur->bc_btnum];
	ASSERT(levels == cur->bc_nlevels);
	xfs_alloc_kcheck_btree(cur, agf, (xfs_alloc_ptr_t)bno, levels - 1, (xfs_alloc_key_t *)0);
}

/*
 * Check key consistency in one btree level, then recurse down to the next.
 */
STATIC void
xfs_alloc_kcheck_btree(
	xfs_btree_cur_t	*cur,		/* btree cursor */
	xfs_agf_t	*agf,		/* a.g. freespace header */
	xfs_alloc_ptr_t	bno,		/* block number to check */
	int		level,		/* level of the block */
	xfs_alloc_key_t	*keyp)		/* value of expected first key */
{
	xfs_btree_sblock_t *block;
	buf_t *buf;
	int i;
	xfs_alloc_key_t key;
	xfs_alloc_key_t *kp;
	xfs_mount_t *mp;
	xfs_alloc_ptr_t *pp;
	xfs_alloc_rec_t *rp;
	xfs_trans_t *tp;

	ASSERT(bno != NULLAGBLOCK);
	tp = cur->bc_tp;
	mp = cur->bc_mp;
	buf = xfs_btree_read_bufs(mp, tp, agf->agf_seqno, bno, 0);
	block = xfs_buf_to_sblock(buf);
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
				xfs_alloc_kcheck_btree(cur, agf, *pp, level - 1, kp);
		}
	}
	xfs_trans_brelse(tp, buf);
}
#endif

STATIC void
xfs_alloc_log_agf(xfs_trans_t *tp, buf_t *buf, int fields)
{
	int first;
	int last;
	static const int offsets[] = {
		offsetof(xfs_agf_t, agf_magicnum),
		offsetof(xfs_agf_t, agf_versionnum),
		offsetof(xfs_agf_t, agf_seqno),
		offsetof(xfs_agf_t, agf_length),
		offsetof(xfs_agf_t, agf_roots[0]),
		offsetof(xfs_agf_t, agf_levels[0]),
		offsetof(xfs_agf_t, agf_freelist),
		offsetof(xfs_agf_t, agf_freecount),
		offsetof(xfs_agf_t, agf_freeblks),
		offsetof(xfs_agf_t, agf_longest),
		sizeof(xfs_agf_t)
	};

	xfs_btree_offsets(fields, offsets, XFS_AGF_NUM_BITS, &first, &last);
	xfs_trans_log_buf(tp, buf, first, last);
	kmem_check();
}

/*
 * Log btree blocks (headers)
 */
STATIC void
xfs_alloc_log_block(xfs_trans_t *tp, buf_t *buf, int fields)
{
	int first;
	int last;
	static const int offsets[] = {
		offsetof(xfs_btree_sblock_t, bb_magic),
		offsetof(xfs_btree_sblock_t, bb_level),
		offsetof(xfs_btree_sblock_t, bb_numrecs),
		offsetof(xfs_btree_sblock_t, bb_leftsib),
		offsetof(xfs_btree_sblock_t, bb_rightsib),
		sizeof(xfs_btree_sblock_t)
	};

	xfs_btree_offsets(fields, offsets, XFS_BB_NUM_BITS, &first, &last);
	xfs_trans_log_buf(tp, buf, first, last);
	kmem_check();
}

STATIC void
xfs_alloc_log_keys(xfs_btree_cur_t *cur, buf_t *buf, int kfirst, int klast)
{
	xfs_btree_sblock_t *block;
	int first;
	xfs_alloc_key_t *kp;
	int last;

	block = xfs_buf_to_sblock(buf);
	kp = XFS_ALLOC_KEY_ADDR(block, 1, cur);
	first = (caddr_t)&kp[kfirst - 1] - (caddr_t)block;
	last = ((caddr_t)&kp[klast] - 1) - (caddr_t)block;
	xfs_trans_log_buf(cur->bc_tp, buf, first, last);
	kmem_check();
}

STATIC void
xfs_alloc_log_ptrs(xfs_btree_cur_t *cur, buf_t *buf, int pfirst, int plast)
{
	xfs_btree_sblock_t *block;
	int first;
	int last;
	xfs_alloc_ptr_t *pp;

	block = xfs_buf_to_sblock(buf);
	pp = XFS_ALLOC_PTR_ADDR(block, 1, cur);
	first = (caddr_t)&pp[pfirst - 1] - (caddr_t)block;
	last = ((caddr_t)&pp[plast] - 1) - (caddr_t)block;
	xfs_trans_log_buf(cur->bc_tp, buf, first, last);
	kmem_check();
}

STATIC void
xfs_alloc_log_recs(xfs_btree_cur_t *cur, buf_t *buf, int rfirst, int rlast)
{
	xfs_btree_sblock_t *block;
	int first;
	int last;
	xfs_alloc_rec_t *rp;

	block = xfs_buf_to_sblock(buf);
	rp = XFS_ALLOC_REC_ADDR(block, 1, cur);
	first = (caddr_t)&rp[rfirst - 1] - (caddr_t)block;
	last = ((caddr_t)&rp[rlast] - 1) - (caddr_t)block;
	xfs_trans_log_buf(cur->bc_tp, buf, first, last);
	kmem_check();
}

/*
 * Lookup the record.  The cursor is made to point to it, based on dir.
 */
STATIC int
xfs_alloc_lookup(xfs_btree_cur_t *cur, xfs_lookup_t dir)
{
	xfs_alloc_ptr_t agbno;
	buf_t *agbuf;
	xfs_agnumber_t agno;
	xfs_agf_t *agf;
	xfs_btree_sblock_t *block;
	xfs_extlen_t blockcount;
	buf_t *buf;
	daddr_t d;
	int diff;
	int high;
	int i;
	int keyno;
	xfs_alloc_key_t *kkbase;
	xfs_alloc_key_t *kkp;
	xfs_alloc_rec_t *krbase;
	xfs_alloc_rec_t *krp;
	int level;
	int low;
	xfs_mount_t *mp;
	xfs_alloc_rec_t *rp;
	xfs_sb_t *sbp;
	xfs_agblock_t startblock;
	xfs_trans_t *tp;

	xfs_alloc_rcheck(cur);
	xfs_alloc_kcheck(cur);
	tp = cur->bc_tp;
	mp = cur->bc_mp;
	agbuf = cur->bc_private.a.agbuf;
	agf = xfs_buf_to_agf(agbuf);
	agno = agf->agf_seqno;
	agbno = agf->agf_roots[cur->bc_btnum];
	sbp = &mp->m_sb;
	rp = &cur->bc_rec.a;
	for (level = cur->bc_nlevels - 1, diff = 1; level >= 0; level--) {
		d = xfs_agb_to_daddr(sbp, agno, agbno);
		buf = cur->bc_bufs[level];
		if (buf && buf->b_blkno != d)
			buf = (buf_t *)0;
		if (!buf) {
			buf = xfs_trans_read_buf(tp, mp->m_dev, d,
						 mp->m_bsize, 0);
			xfs_btree_setbuf(cur, level, buf);
		}
		block = xfs_buf_to_sblock(buf);
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
					diff = (int)startblock - (int)rp->ar_startblock;
				else if (!(diff = (int)blockcount - (int)rp->ar_blockcount))
					diff = (int)startblock - (int)rp->ar_startblock;
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
		if (dir == XFS_LOOKUP_GE && keyno > block->bb_numrecs &&
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

STATIC int
xfs_alloc_lookup_eq(xfs_btree_cur_t *cur, xfs_agblock_t bno, xfs_extlen_t len)
{
	cur->bc_rec.a.ar_startblock = bno;
	cur->bc_rec.a.ar_blockcount = len;
	return xfs_alloc_lookup(cur, XFS_LOOKUP_EQ);
}

STATIC int
xfs_alloc_lookup_ge(xfs_btree_cur_t *cur, xfs_agblock_t bno, xfs_extlen_t len)
{
	cur->bc_rec.a.ar_startblock = bno;
	cur->bc_rec.a.ar_blockcount = len;
	return xfs_alloc_lookup(cur, XFS_LOOKUP_GE);
}

STATIC int
xfs_alloc_lookup_le(xfs_btree_cur_t *cur, xfs_agblock_t bno, xfs_extlen_t len)
{
	cur->bc_rec.a.ar_startblock = bno;
	cur->bc_rec.a.ar_blockcount = len;
	return xfs_alloc_lookup(cur, XFS_LOOKUP_LE);
}

/*
 * Move 1 record left from cur/level if possible.
 * Update cur to reflect the new path.
 */
STATIC int
xfs_alloc_lshift(xfs_btree_cur_t *cur, int level)
{
	int i;
	xfs_alloc_key_t key;
	buf_t *lbuf;
	xfs_btree_sblock_t *left;
	xfs_alloc_key_t *lkp;
	xfs_alloc_ptr_t *lpp;
	xfs_alloc_rec_t *lrp;
	xfs_mount_t *mp;
	int nrec;
	buf_t *rbuf;
	xfs_btree_sblock_t *right;
	xfs_alloc_key_t *rkp;
	xfs_alloc_ptr_t *rpp;
	xfs_alloc_rec_t *rrp;
	xfs_trans_t *tp;

	xfs_alloc_rcheck(cur);
	rbuf = cur->bc_bufs[level];
	right = xfs_buf_to_sblock(rbuf);
	xfs_btree_check_sblock(cur, right, level);
	if (right->bb_leftsib == NULLAGBLOCK)
		return 0;
	if (cur->bc_ptrs[level] <= 1)
		return 0;
	tp = cur->bc_tp;
	mp = cur->bc_mp;
	lbuf = xfs_btree_read_bufs(mp, tp, cur->bc_private.a.agno, right->bb_leftsib, 0);
	left = xfs_buf_to_sblock(lbuf);
	xfs_btree_check_sblock(cur, left, level);
	kmem_check();
	if (left->bb_numrecs == XFS_ALLOC_BLOCK_MAXRECS(level, cur))
		return 0;
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
STATIC int
xfs_alloc_newroot(xfs_btree_cur_t *cur)
{
	buf_t *agbuf;
	xfs_agf_t *agf;
	xfs_btree_sblock_t *block;
	buf_t *buf;
	xfs_alloc_key_t *kp;
	xfs_alloc_ptr_t lbno;
	buf_t *lbuf;
	xfs_btree_sblock_t *left;
	xfs_mount_t *mp;
	xfs_alloc_ptr_t nbno;
	buf_t *nbuf;
	xfs_btree_sblock_t *new;
	int nptr;
	xfs_alloc_ptr_t *pp;
	xfs_alloc_ptr_t rbno;
	buf_t *rbuf;
	xfs_btree_sblock_t *right;
	xfs_alloc_rec_t *rp;
	xfs_sb_t *sbp;
	xfs_trans_t *tp;

	xfs_alloc_rcheck(cur);
	ASSERT(cur->bc_nlevels < XFS_BTREE_MAXLEVELS);
	tp = cur->bc_tp;
	mp = cur->bc_mp;
	sbp = &mp->m_sb;
	agbuf = cur->bc_private.a.agbuf;
	agf = xfs_buf_to_agf(agbuf);
	nbno = xfs_alloc_get_freelist(tp, agbuf, &nbuf);
	if (nbno == NULLAGBLOCK)
		return 0;
	new = xfs_buf_to_sblock(nbuf);
	agf->agf_roots[cur->bc_btnum] = nbno;
	agf->agf_levels[cur->bc_btnum]++;
	xfs_alloc_log_agf(tp, agbuf, XFS_AGF_ROOTS | XFS_AGF_LEVELS);
	buf = cur->bc_bufs[cur->bc_nlevels - 1];
	block = xfs_buf_to_sblock(buf);
	xfs_btree_check_sblock(cur, block, cur->bc_nlevels - 1);
	if (block->bb_rightsib != NULLAGBLOCK) {
		lbuf = buf;
		lbno = xfs_daddr_to_agbno(sbp, lbuf->b_blkno);
		left = block;
		rbno = left->bb_rightsib;
		buf = rbuf = xfs_btree_read_bufs(mp, tp, cur->bc_private.a.agno, rbno, 0);
		right = xfs_buf_to_sblock(rbuf);
		xfs_btree_check_sblock(cur, right, cur->bc_nlevels - 1);
		nptr = 1;
	} else {
		rbuf = buf;
		rbno = xfs_daddr_to_agbno(sbp, rbuf->b_blkno);
		right = block;
		lbno = right->bb_leftsib;
		buf = lbuf = xfs_btree_read_bufs(mp, tp, cur->bc_private.a.agno, lbno, 0);
		left = xfs_buf_to_sblock(lbuf);
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

/*
 * Put the buffer on the freelist for the ag.
 */
STATIC void
xfs_alloc_put_freelist(xfs_trans_t *tp, buf_t *agbuf, buf_t *buf)
{
	xfs_agf_t *agf;
	xfs_btree_sblock_t *block;
	xfs_agblock_t bno;
	xfs_mount_t *mp;
	xfs_sb_t *sbp;

	mp = tp->t_mountp;
	sbp = &mp->m_sb;
	agf = xfs_buf_to_agf(agbuf);
	block = xfs_buf_to_sblock(buf);
	/*
	 * Point the new block to the old head of the list.
	 */
	*(xfs_agblock_t *)block = agf->agf_freelist;
	xfs_trans_log_buf(tp, buf, 0, (int)sizeof(xfs_agblock_t) - 1);
	bno = xfs_daddr_to_agbno(sbp, buf->b_blkno);
	agf->agf_freelist = bno;
	agf->agf_freecount++;
	xfs_alloc_log_agf(tp, agbuf, XFS_AGF_FREELIST | XFS_AGF_FREECOUNT);
	kmem_check();
}

#ifdef XFSDEBUG
/*
 * Check consistency in the given btree.
 * Checks header consistency and that keys/records are in the right order.
 */
STATIC void
xfs_alloc_rcheck(
	xfs_btree_cur_t	*cur)		/* btree cursor */
{
	buf_t *agbuf;
	xfs_agf_t *agf;
	xfs_agblock_t bno;
	int levels;

	agbuf = cur->bc_private.a.agbuf;
	agf = xfs_buf_to_agf(agbuf);
	bno = agf->agf_roots[cur->bc_btnum];
	levels = agf->agf_levels[cur->bc_btnum];
	xfs_alloc_rcheck_btree(cur, agf, (xfs_alloc_ptr_t)bno, levels);
}

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
	xfs_alloc_ptr_t bno;
	xfs_alloc_ptr_t fbno;
	xfs_alloc_key_t key;
	int level;
	xfs_alloc_rec_t rec;
	void *rp;

	rp = level ? (void *)&key : (void *)&rec;
	for (level = levels - 1, bno = rbno; level >= 0; level--, bno = fbno) {
		bno = xfs_alloc_rcheck_btree_block(cur, agf->agf_seqno, bno, &fbno, rp, level);
		while (bno != NULLAGBLOCK) {
			ASSERT(bno < agf->agf_length);
			bno = xfs_alloc_rcheck_btree_block(cur, agf->agf_seqno, bno, (xfs_alloc_ptr_t *)0, rp, level);
		}
	}
}

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
	int		level)		/* level of this block */
{
	xfs_btree_sblock_t *block;
	buf_t *buf;
	int i;
	xfs_alloc_key_t *keyp;
	xfs_alloc_key_t *kp;
	xfs_mount_t *mp;
	xfs_alloc_ptr_t *pp;
	xfs_alloc_ptr_t rbno;
	xfs_alloc_rec_t *recp;
	xfs_alloc_rec_t *rp;
	xfs_trans_t *tp;

	tp = cur->bc_tp;
	mp = cur->bc_mp;
	buf = xfs_btree_read_bufs(mp, tp, agno, bno, 0);
	block = xfs_buf_to_sblock(buf);
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
				xfs_btree_check_key(cur->bc_btnum, (void *)(kp - 1), (void *)kp);
				if (i == block->bb_numrecs)
					*keyp = *kp;
			}
			pp = XFS_ALLOC_PTR_ADDR(block, i, cur);
			xfs_btree_check_sptr(cur, *pp, level);
		} else {
			rp = XFS_ALLOC_REC_ADDR(block, i, cur);
			if (i == 1 && !fbno)
				xfs_btree_check_rec(cur->bc_btnum, (void *)recp, (void *)rp);
			else if (i > 1) {
				xfs_btree_check_rec(cur->bc_btnum, (void *)(rp - 1), (void *)rp);
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
 * Read in the allocation group header (free/alloc section)
 */
STATIC buf_t *
xfs_alloc_read_agf(xfs_mount_t *mp, xfs_trans_t *tp, xfs_agnumber_t agno, int flags)
{
	daddr_t		d;		/* disk block address */
	xfs_sb_t	*sbp;		/* superblock structure */

	ASSERT(agno != NULLAGNUMBER);
	sbp = &mp->m_sb;
	d = xfs_ag_daddr(sbp, agno, XFS_AGF_DADDR);
	return xfs_trans_read_buf(tp, mp->m_dev, d, 1,
			  (flags & XFS_ALLOC_FLAG_TRYLOCK) ? BUF_TRYLOCK : 0);
}

/*
 * Move 1 record right from cur/level if possible.
 * Update cur to reflect the new path.
 */
STATIC int
xfs_alloc_rshift(xfs_btree_cur_t *cur, int level)
{
	int i;
	xfs_alloc_key_t key;
	buf_t *lbuf;
	xfs_btree_sblock_t *left;
	xfs_alloc_key_t *lkp;
	xfs_alloc_ptr_t *lpp;
	xfs_alloc_rec_t *lrp;
	xfs_mount_t *mp;
	buf_t *rbuf;
	xfs_btree_sblock_t *right;
	xfs_alloc_key_t *rkp;
	xfs_alloc_ptr_t *rpp;
	xfs_alloc_rec_t *rrp;
	xfs_btree_cur_t *tcur;
	xfs_trans_t *tp;

	xfs_alloc_rcheck(cur);
	lbuf = cur->bc_bufs[level];
	left = xfs_buf_to_sblock(lbuf);
	xfs_btree_check_sblock(cur, left, level);
	if (left->bb_rightsib == NULLAGBLOCK)
		return 0;
	if (cur->bc_ptrs[level] >= left->bb_numrecs)
		return 0;
	tp = cur->bc_tp;
	mp = cur->bc_mp;
	rbuf = xfs_btree_read_bufs(mp, tp, cur->bc_private.a.agno, left->bb_rightsib, 0);
	right = xfs_buf_to_sblock(rbuf);
	xfs_btree_check_sblock(cur, right, level);
	kmem_check();
	if (right->bb_numrecs == XFS_ALLOC_BLOCK_MAXRECS(level, cur))
		return 0;
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
STATIC int
xfs_alloc_split(xfs_btree_cur_t *cur, int level, xfs_alloc_ptr_t *bnop, xfs_alloc_key_t *keyp, xfs_btree_cur_t **curp)
{
	buf_t *agbuf;
	xfs_agf_t *agf;
	daddr_t d;
	int i;
	xfs_alloc_key_t key;
	xfs_alloc_ptr_t lbno;
	buf_t *lbuf;
	xfs_btree_sblock_t *left;
	xfs_alloc_key_t *lkp;
	xfs_alloc_ptr_t *lpp;
	xfs_alloc_rec_t *lrp;
	xfs_mount_t *mp;
	xfs_alloc_ptr_t rbno;
	buf_t *rbuf;
	xfs_alloc_rec_t rec;
	xfs_btree_sblock_t *right;
	xfs_alloc_key_t *rkp;
	xfs_alloc_ptr_t *rpp;
	xfs_btree_sblock_t *rrblock;
	buf_t *rrbuf;
	xfs_alloc_rec_t *rrp;
	xfs_sb_t *sbp;
	xfs_trans_t *tp;

	xfs_alloc_rcheck(cur);
	tp = cur->bc_tp;
	mp = cur->bc_mp;
	sbp = &mp->m_sb;
	agbuf = cur->bc_private.a.agbuf;
	agf = xfs_buf_to_agf(agbuf);
	rbno = xfs_alloc_get_freelist(tp, agbuf, &rbuf);
	kmem_check();
	if (rbno == NULLAGBLOCK)
		return 0;
	right = xfs_buf_to_sblock(rbuf);
	lbuf = cur->bc_bufs[level];
	left = xfs_buf_to_sblock(lbuf);
	xfs_btree_check_sblock(cur, left, level);
	right->bb_magic = xfs_magics[cur->bc_btnum];
	right->bb_level = left->bb_level;
	right->bb_numrecs = (__uint16_t)(left->bb_numrecs / 2);
	if ((left->bb_numrecs & 1) && cur->bc_ptrs[level] <= right->bb_numrecs + 1)
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
	lbno = xfs_daddr_to_agbno(sbp, d);
	left->bb_numrecs -= right->bb_numrecs;
	right->bb_rightsib = left->bb_rightsib;
	left->bb_rightsib = rbno;
	right->bb_leftsib = lbno;
	xfs_alloc_log_block(tp, rbuf, XFS_BB_ALL_BITS);
	xfs_alloc_log_block(tp, lbuf, XFS_BB_NUMRECS | XFS_BB_RIGHTSIB);
	if (right->bb_rightsib != NULLAGBLOCK) {
		rrbuf = xfs_btree_read_bufs(mp, tp, cur->bc_private.a.agno, right->bb_rightsib, 0);
		rrblock = xfs_buf_to_sblock(rrbuf);
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

STATIC int
xfs_alloc_update(xfs_btree_cur_t *cur, xfs_agblock_t bno, xfs_extlen_t len)
{
	buf_t *agbuf;
	xfs_agf_t *agf;
	xfs_btree_sblock_t *block;
	buf_t *buf;
	xfs_alloc_key_t key;
	xfs_mount_t *mp;
	int ptr;
	xfs_alloc_rec_t *rp;
	xfs_trans_t *tp;

	agbuf = cur->bc_private.a.agbuf;
	agf = xfs_buf_to_agf(agbuf);
	xfs_alloc_rcheck(cur);
	buf = cur->bc_bufs[0];
	block = xfs_buf_to_sblock(buf);
	xfs_btree_check_sblock(cur, block, 0);
	ptr = cur->bc_ptrs[0];
	tp = cur->bc_tp;
	mp = cur->bc_mp;
	rp = XFS_ALLOC_REC_ADDR(block, ptr, cur);
	rp->ar_startblock = bno;
	rp->ar_blockcount = len;
	xfs_alloc_log_recs(cur, buf, ptr, ptr);
	if (cur->bc_btnum == XFS_BTNUM_CNT && block->bb_rightsib == NULLAGBLOCK &&
	    ptr == block->bb_numrecs) {
		agf->agf_longest = len;
		xfs_alloc_log_agf(tp, agbuf, XFS_AGF_LONGEST);
	}
	kmem_check();
	if (ptr > 1)
		return 1;
	key.ar_startblock = bno;
	key.ar_blockcount = len;
	xfs_alloc_updkey(cur, &key, 1);
	xfs_alloc_rcheck(cur);
	xfs_alloc_kcheck(cur);
	kmem_check();
	return 1;
}

STATIC void
xfs_alloc_updkey(xfs_btree_cur_t *cur, xfs_alloc_key_t *keyp, int level)
{
	xfs_btree_sblock_t *block;
	buf_t *buf;
	xfs_alloc_key_t *kp;
	xfs_mount_t *mp;
	int ptr;
	xfs_trans_t *tp;

	xfs_alloc_rcheck(cur);
	tp = cur->bc_tp;
	mp = cur->bc_mp;
	for (ptr = 1; ptr == 1 && level < cur->bc_nlevels; level++) {
		buf = cur->bc_bufs[level];
		block = xfs_buf_to_sblock(buf);
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
 * Allocation group level functions.
 */

STATIC xfs_agblock_t
xfs_alloc_ag_vextent_exact(xfs_trans_t *tp, buf_t *agbuf, xfs_agnumber_t agno, xfs_agblock_t bno, xfs_extlen_t minlen, xfs_extlen_t maxlen, xfs_extlen_t *len)
{
	xfs_btree_cur_t *bno_cur;
	xfs_btree_cur_t *cnt_cur;
	xfs_agblock_t end;
	xfs_agblock_t fbno;
	xfs_agblock_t fend;
	xfs_extlen_t flen;
	int i;
	xfs_agblock_t maxend;
	xfs_agblock_t minend;
	xfs_mount_t *mp;

	mp = tp->t_mountp;
	bno_cur = xfs_btree_init_cursor(mp, tp, agbuf, agno, XFS_BTNUM_BNO, 0);
	if (xfs_alloc_lookup_le(bno_cur, bno, minlen)) {
		xfs_alloc_get_rec(bno_cur, &fbno, &flen);
		minend = bno + minlen;
		maxend = bno + maxlen;
		fend = fbno + flen;
		if (fbno <= bno && fend >= minend) {
			if (fend > minend)
				end = fend < maxend ? fend : maxend;
			else
				end = minend;
			cnt_cur = xfs_btree_init_cursor(mp, tp, agbuf, agno, XFS_BTNUM_CNT, 0);
			i = xfs_alloc_lookup_eq(cnt_cur, fbno, flen);
			ASSERT(i == 1);
			xfs_alloc_delete(cnt_cur);
			if (fbno < bno) {
				xfs_alloc_lookup_eq(cnt_cur, fbno, bno - fbno);
				xfs_alloc_insert(cnt_cur);
			}
			if (fend > end) {
				xfs_alloc_lookup_eq(cnt_cur, end, fend - end);
				xfs_alloc_insert(cnt_cur);
			}
			xfs_alloc_rcheck(cnt_cur);
			xfs_alloc_kcheck(cnt_cur);
			xfs_btree_del_cursor(cnt_cur);
			if (fbno == bno) {
				if (fend == end)
					xfs_alloc_delete(bno_cur);
				else
					xfs_alloc_update(bno_cur, end, fend - end);
			} else {
				xfs_alloc_update(bno_cur, fbno, bno - fbno);
				if (fend > end) {
					xfs_alloc_lookup_eq(bno_cur, end, fend - end);
					xfs_alloc_insert(bno_cur);
				}
				fbno = bno;
			}
			*len = end - bno;
		} else
			fbno = NULLAGBLOCK;
	} else
		fbno = NULLAGBLOCK;
	xfs_alloc_rcheck(bno_cur);
	xfs_alloc_kcheck(bno_cur);
	xfs_btree_del_cursor(bno_cur);
	return fbno;
}

STATIC xfs_agblock_t
xfs_alloc_ag_vextent_size(xfs_trans_t *tp, buf_t *agbuf, xfs_agnumber_t agno, xfs_extlen_t minlen, xfs_extlen_t maxlen, xfs_extlen_t *len)
{
	xfs_btree_cur_t *bno_cur;
	xfs_btree_cur_t *cnt_cur;
	xfs_agblock_t fbno;
	xfs_extlen_t flen = 0;
	int i;
	xfs_extlen_t rlen;
	xfs_mount_t *mp;

	mp = tp->t_mountp;
	cnt_cur = xfs_btree_init_cursor(mp, tp, agbuf, agno, XFS_BTNUM_CNT, 0);
	if (!xfs_alloc_lookup_ge(cnt_cur, 0, maxlen)) {
		if (xfs_alloc_decrement(cnt_cur, 0))
			xfs_alloc_get_rec(cnt_cur, &fbno, &flen);
		if (flen < minlen) {
			xfs_btree_del_cursor(cnt_cur);
			return NULLAGBLOCK;
		}
		rlen = flen;
	} else {
		xfs_alloc_get_rec(cnt_cur, &fbno, &flen);
		rlen = maxlen;
	}
	xfs_alloc_delete(cnt_cur);
	bno_cur = xfs_btree_init_cursor(mp, tp, agbuf, agno, XFS_BTNUM_BNO, 0);
	i = xfs_alloc_lookup_eq(bno_cur, fbno, flen);
	ASSERT(i == 1);
	if (rlen < flen) {
		xfs_alloc_lookup_eq(cnt_cur, fbno + rlen, flen - rlen);
		xfs_alloc_insert(cnt_cur);
		xfs_alloc_update(bno_cur, fbno + rlen, flen - rlen);
	} else
		xfs_alloc_delete(bno_cur);
	*len = rlen;
	xfs_alloc_rcheck(bno_cur);
	xfs_alloc_kcheck(bno_cur);
	xfs_btree_del_cursor(bno_cur);
	xfs_alloc_rcheck(cnt_cur);
	xfs_alloc_kcheck(cnt_cur);
	xfs_btree_del_cursor(cnt_cur);
	return fbno;
}

STATIC xfs_agblock_t
xfs_alloc_ag_vextent_near(xfs_trans_t *tp, buf_t *agbuf, xfs_agnumber_t agno, xfs_agblock_t bno, xfs_extlen_t minlen, xfs_extlen_t maxlen, xfs_extlen_t *len)
{
	xfs_extlen_t bdiff;
	int besti;
	xfs_agblock_t bnew;
	xfs_btree_cur_t *bno_cur_gt;
	xfs_btree_cur_t *bno_cur_lt;
	xfs_btree_cur_t *cnt_cur;
	xfs_agblock_t gtbno;
	xfs_extlen_t gtdiff;
	xfs_agblock_t gtend;
	xfs_extlen_t gtlen;
	xfs_agblock_t gtnew;
	xfs_agblock_t gtnewend;
	int i;
	xfs_agblock_t ltbno;
	xfs_extlen_t ltdiff;
	xfs_agblock_t ltend;
	xfs_extlen_t ltlen;
	xfs_agblock_t ltnew;
	xfs_agblock_t ltnewend;
	xfs_mount_t *mp;
	xfs_extlen_t rlen;

	mp = tp->t_mountp;
	cnt_cur = xfs_btree_init_cursor(mp, tp, agbuf, agno, XFS_BTNUM_CNT, 0);
	ltlen = 0;
	if (!xfs_alloc_lookup_ge(cnt_cur, 0, maxlen)) {
		if (xfs_alloc_decrement(cnt_cur, 0))
			xfs_alloc_get_rec(cnt_cur, &ltbno, &ltlen);
		if (ltlen < minlen) {
			xfs_btree_del_cursor(cnt_cur);
			return NULLAGBLOCK;
		}
	}
	if (xfs_btree_islastblock(cnt_cur, 0)) {
		i = cnt_cur->bc_ptrs[0];
		besti = 0;
		do {
			xfs_alloc_get_rec(cnt_cur, &ltbno, &ltlen);
			rlen = ltlen < maxlen ? ltlen : maxlen;
			ltdiff = xfs_alloc_compute_diff(bno, rlen, ltbno, ltlen, &ltnew);
			if (!besti || ltdiff < bdiff) {
				bdiff = ltdiff;
				bnew = ltnew;
				besti = i;
			}
			i++;
		} while (ltdiff > 0 && xfs_alloc_increment(cnt_cur, 0));
		cnt_cur->bc_ptrs[0] = besti;
		xfs_alloc_get_rec(cnt_cur, &ltbno, &ltlen);
		ltend = ltbno + ltlen;
		rlen = ltlen < maxlen ? ltlen : maxlen;
		xfs_alloc_delete(cnt_cur);
		ltnew = bnew;
		ltnewend = bnew + rlen;
		*len = rlen;
		bno_cur_lt = xfs_btree_init_cursor(mp, tp, agbuf, agno, XFS_BTNUM_BNO, 0);
		i = xfs_alloc_lookup_eq(bno_cur_lt, ltbno, ltlen);
		ASSERT(i == 1);
		/*
		 * Freespace properly contains allocated space.
		 */
		if (ltbno < ltnew && ltend > ltnewend) {
			xfs_alloc_lookup_eq(cnt_cur, ltbno, ltnew - ltbno);
			xfs_alloc_insert(cnt_cur);
			xfs_alloc_update(bno_cur_lt, ltbno, ltnew - ltbno);
			xfs_alloc_lookup_eq(cnt_cur, ltnewend, ltend - ltnewend);
			xfs_alloc_insert(cnt_cur);
			xfs_alloc_lookup_eq(bno_cur_lt, ltnewend, ltend - ltnewend);
			xfs_alloc_insert(bno_cur_lt);
		/*
		 * Freespace contains allocated space, matches at left side.
		 */
		} else if (ltend > ltnewend) {
			xfs_alloc_lookup_eq(cnt_cur, ltnewend, ltend - ltnewend);
			xfs_alloc_insert(cnt_cur);
			xfs_alloc_update(bno_cur_lt, ltnewend, ltend - ltnewend);
		/*
		 * Freespace contains allocated space, matches at right side.
		 */
		} else if (ltbno < ltnew) {
			xfs_alloc_lookup_eq(cnt_cur, ltbno, ltnew - ltbno);
			xfs_alloc_insert(cnt_cur);
			xfs_alloc_update(bno_cur_lt, ltbno, ltnew - ltbno);
		/*
		 * Freespace same size as allocated.
		 */
		} else
			xfs_alloc_delete(bno_cur_lt);
		xfs_alloc_rcheck(cnt_cur);
		xfs_alloc_kcheck(cnt_cur);
		xfs_btree_del_cursor(cnt_cur);
		xfs_alloc_rcheck(bno_cur_lt);
		xfs_alloc_kcheck(bno_cur_lt);
		xfs_btree_del_cursor(bno_cur_lt);
		return ltnew;
	}
	bno_cur_lt = xfs_btree_init_cursor(mp, tp, agbuf, agno, XFS_BTNUM_BNO, 0);
	if (!xfs_alloc_lookup_le(bno_cur_lt, bno, maxlen)) {
		bno_cur_gt = bno_cur_lt;
		bno_cur_lt = 0;
	} else
		bno_cur_gt = xfs_btree_dup_cursor(bno_cur_lt);
	if (!xfs_alloc_increment(bno_cur_gt, 0)) {
		xfs_btree_del_cursor(bno_cur_gt);
		bno_cur_gt = 0;
	}
	do {
		if (bno_cur_lt) {
			xfs_alloc_get_rec(bno_cur_lt, &ltbno, &ltlen);
			if (ltlen >= minlen)
				break;
			if (!xfs_alloc_decrement(bno_cur_lt, 0)) {
				xfs_btree_del_cursor(bno_cur_lt);
				bno_cur_lt = 0;
			}
		}
		if (bno_cur_gt) {
			xfs_alloc_get_rec(bno_cur_gt, &gtbno, &gtlen);
			if (gtlen >= minlen)
				break;
			if (!xfs_alloc_increment(bno_cur_gt, 0)) {
				xfs_btree_del_cursor(bno_cur_gt);
				bno_cur_gt = 0;
			}
		}
	} while (bno_cur_lt || bno_cur_gt);
	if (!bno_cur_lt && !bno_cur_gt)
		return NULLAGBLOCK;
	/*
	 * Got both cursors still active, need to find better entry.
	 */
	if (bno_cur_lt && bno_cur_gt) {
		/*
		 * Left side is long enough, look for a right side entry.
		 */
		if (ltlen >= minlen) {
			rlen = ltlen < maxlen ? ltlen : maxlen;
			ltdiff = xfs_alloc_compute_diff(bno, rlen, ltbno, ltlen, &ltnew);
			/*
			 * Not perfect.
			 */
			if (ltdiff) {
				/*
				 * Look until we find a better one, run out of
				 * space, or run off the end.
				 */
				while (bno_cur_lt && bno_cur_gt) {
					xfs_alloc_get_rec(bno_cur_gt, &gtbno, &gtlen);
					if (gtbno >= bno + ltdiff) {
						xfs_btree_del_cursor(bno_cur_gt);
						bno_cur_gt = 0;
					} else if (gtlen >= minlen) {
						rlen = gtlen < maxlen ? gtlen : maxlen;
						gtdiff = xfs_alloc_compute_diff(bno, rlen, gtbno, gtlen, &gtnew);
						if (gtdiff < ltdiff) {
							xfs_btree_del_cursor(bno_cur_lt);
							bno_cur_lt = 0;
						}
					} else if (!xfs_alloc_increment(bno_cur_gt, 0)) {
						xfs_btree_del_cursor(bno_cur_gt);
						bno_cur_gt = 0;
					}
				}
			} else {
				xfs_btree_del_cursor(bno_cur_gt);
				bno_cur_gt = 0;
			}
		} else {
			rlen = gtlen < maxlen ? gtlen : maxlen;
			gtdiff = xfs_alloc_compute_diff(bno, rlen, gtbno, gtlen, &gtnew);
			if (gtdiff) {
				while (bno_cur_lt && bno_cur_gt) {
					xfs_alloc_get_rec(bno_cur_lt, &ltbno, &ltlen);
					if (ltbno <= bno - gtdiff) {
						xfs_btree_del_cursor(bno_cur_lt);
						bno_cur_lt = 0;
					} else if (ltlen >= minlen) {
						rlen = ltlen < maxlen ? ltlen : maxlen;
						ltdiff = xfs_alloc_compute_diff(bno, rlen, ltbno, ltlen, &ltnew);
						if (ltdiff < gtdiff) {
							xfs_btree_del_cursor(bno_cur_gt);
							bno_cur_gt = 0;
						}
					} else if (!xfs_alloc_decrement(bno_cur_lt, 0)) {
						xfs_btree_del_cursor(bno_cur_lt);
						bno_cur_lt = 0;
					}
				}
			} else {
				xfs_btree_del_cursor(bno_cur_lt);
				bno_cur_lt = 0;
			}
		}
	}
	if (bno_cur_lt) {
		ltend = ltbno + ltlen;
		rlen = ltlen < maxlen ? ltlen : maxlen;
		ltdiff = xfs_alloc_compute_diff(bno, rlen, ltbno, ltlen, &ltnew);
		ltnewend = ltnew + rlen;
		*len = rlen;
		i = xfs_alloc_lookup_eq(cnt_cur, ltbno, ltlen);
		ASSERT(i == 1);
		xfs_alloc_delete(cnt_cur);
		/*
		 * Freespace properly contains allocated space.
		 */
		if (ltbno < ltnew && ltend > ltnewend) {
			xfs_alloc_lookup_eq(cnt_cur, ltbno, ltnew - ltbno);
			xfs_alloc_insert(cnt_cur);
			xfs_alloc_update(bno_cur_lt, ltbno, ltnew - ltbno);
			xfs_alloc_lookup_eq(cnt_cur, ltnewend, ltend - ltnewend);
			xfs_alloc_insert(cnt_cur);
			xfs_alloc_lookup_eq(bno_cur_lt, ltnewend, ltend - ltnewend);
			xfs_alloc_insert(bno_cur_lt);
		/*
		 * Freespace contains allocated space, matches at left side.
		 */
		} else if (ltend > ltnewend) {
			xfs_alloc_lookup_eq(cnt_cur, ltnewend, ltend - ltnewend);
			xfs_alloc_insert(cnt_cur);
			xfs_alloc_update(bno_cur_lt, ltnewend, ltend - ltnewend);
		/*
		 * Freespace contains allocated space, matches at right side.
		 */
		} else if (ltbno < ltnew) {
			xfs_alloc_lookup_eq(cnt_cur, ltbno, ltnew - ltbno);
			xfs_alloc_insert(cnt_cur);
			xfs_alloc_update(bno_cur_lt, ltbno, ltnew - ltbno);
		/*
		 * Freespace same size as allocated.
		 */
		} else
			xfs_alloc_delete(bno_cur_lt);
		xfs_alloc_rcheck(cnt_cur);
		xfs_alloc_kcheck(cnt_cur);
		xfs_btree_del_cursor(cnt_cur);
		xfs_alloc_rcheck(bno_cur_lt);
		xfs_alloc_kcheck(bno_cur_lt);
		xfs_btree_del_cursor(bno_cur_lt);
		return ltnew;
	} else {
		gtend = gtbno + gtlen;
		rlen = gtlen < maxlen ? gtlen : maxlen;
		gtdiff = xfs_alloc_compute_diff(bno, rlen, gtbno, gtlen, &gtnew);
		gtnewend = gtnew + rlen;
		*len = rlen;
		i = xfs_alloc_lookup_eq(cnt_cur, gtbno, gtlen);
		ASSERT(i == 1);
		xfs_alloc_delete(cnt_cur);
		/*
		 * Other cases can't occur since gtbno > bno.
		 */
		/*
		 * Freespace contains allocated space, matches at left side.
		 */
		if (gtend > gtnewend) {
			xfs_alloc_lookup_eq(cnt_cur, gtnewend, gtend - gtnewend);
			xfs_alloc_insert(cnt_cur);
			xfs_alloc_update(bno_cur_gt, gtnewend, gtend - gtnewend);
		/*
		 * Freespace same size as allocated.
		 */
		} else
			xfs_alloc_delete(bno_cur_gt);
		xfs_alloc_rcheck(cnt_cur);
		xfs_alloc_kcheck(cnt_cur);
		xfs_btree_del_cursor(cnt_cur);
		xfs_alloc_rcheck(bno_cur_gt);
		xfs_alloc_kcheck(bno_cur_gt);
		xfs_btree_del_cursor(bno_cur_gt);
		return gtnew;
	}
}

/*
 * Generic extent allocation in an allocation group, variable-size.
 */
STATIC xfs_agblock_t
xfs_alloc_ag_vextent(xfs_trans_t *tp, buf_t *agbuf, xfs_agnumber_t agno, xfs_agblock_t bno, xfs_extlen_t minlen, xfs_extlen_t maxlen, xfs_extlen_t *len, xfs_alloctype_t type) 
{
	xfs_agf_t *agf;
	xfs_mount_t *mp;
	xfs_agblock_t r;
	xfs_sb_t *sbp;

	if (minlen == 0 || maxlen == 0 || minlen > maxlen)
		return NULLAGBLOCK;
	switch (type) {
	case XFS_ALLOCTYPE_THIS_AG:
		r = xfs_alloc_ag_vextent_size(tp, agbuf, agno, minlen, maxlen, len);
		break;
	case XFS_ALLOCTYPE_NEAR_BNO:
		r = xfs_alloc_ag_vextent_near(tp, agbuf, agno, bno, minlen, maxlen, len);
		break;
	case XFS_ALLOCTYPE_THIS_BNO:
		r = xfs_alloc_ag_vextent_exact(tp, agbuf, agno, bno, minlen, maxlen, len);
		break;
	default:
		ASSERT(0);
		/* NOTREACHED */
	}
	if (r != NULLAGBLOCK) {
		ASSERT(*len >= minlen && *len <= maxlen);
		agf = xfs_buf_to_agf(agbuf);
		agf->agf_freeblks -= *len;
		xfs_alloc_log_agf(tp, agbuf, XFS_AGF_FREEBLKS);
		xfs_trans_mod_sb(tp, XFS_SB_FDBLOCKS, -(*len));
	}
	return r;
}

/*
 * Free an extent in an ag.
 */
STATIC int
xfs_free_ag_extent(xfs_trans_t *tp, buf_t *agbuf, xfs_agnumber_t agno, xfs_agblock_t bno, xfs_extlen_t len)
{
	xfs_agf_t *agf;
	xfs_btree_cur_t *bno_cur;
	xfs_btree_cur_t *cnt_cur;
	xfs_extlen_t flen = len;
	xfs_agblock_t gtbno;
	xfs_extlen_t gtlen;
	int haveleft;
	int haveright;
	int i;
	xfs_agblock_t ltbno;
	xfs_extlen_t ltlen;
	xfs_mount_t *mp;
	xfs_sb_t *sbp;

	mp = tp->t_mountp;
	bno_cur = xfs_btree_init_cursor(mp, tp, agbuf, agno, XFS_BTNUM_BNO, 0);
	if (haveleft = xfs_alloc_lookup_le(bno_cur, bno, len)) {
		xfs_alloc_get_rec(bno_cur, &ltbno, &ltlen);
		if (ltbno + ltlen < bno)
			haveleft = 0;
		else if (ltbno + ltlen > bno) {
			xfs_btree_del_cursor(bno_cur);
			return 0;
		}
	}
	if (haveright = xfs_alloc_increment(bno_cur, 0)) {
		xfs_alloc_get_rec(bno_cur, &gtbno, &gtlen);
		if (bno + len < gtbno)
			haveright = 0;
		else if (gtbno < bno + len) {
			xfs_btree_del_cursor(bno_cur);
			return 0;
		}
	}
	cnt_cur = xfs_btree_init_cursor(mp, tp, agbuf, agno, XFS_BTNUM_CNT, 0);
	if (haveleft && haveright) {
		i = xfs_alloc_lookup_eq(cnt_cur, ltbno, ltlen);
		ASSERT(i == 1);
		xfs_alloc_delete(cnt_cur);
		i = xfs_alloc_lookup_eq(cnt_cur, gtbno, gtlen);
		ASSERT(i == 1);
		xfs_alloc_delete(cnt_cur);
		xfs_alloc_delete(bno_cur);
		xfs_alloc_decrement(bno_cur, 0);
		{ xfs_agblock_t xxbno; xfs_extlen_t xxlen;
		  xfs_alloc_get_rec(bno_cur, &xxbno, &xxlen);
		  ASSERT(xxbno == ltbno);
		  ASSERT(xxlen == ltlen);
		}
		bno = ltbno;
		len += ltlen + gtlen;
		xfs_alloc_update(bno_cur, bno, len);
	} else if (haveleft) {
		i = xfs_alloc_lookup_eq(cnt_cur, ltbno, ltlen);
		ASSERT(i == 1);
		xfs_alloc_delete(cnt_cur);
		xfs_alloc_decrement(bno_cur, 0);
		bno = ltbno;
		len += ltlen;
		xfs_alloc_update(bno_cur, bno, len);
	} else if (haveright) {
		i = xfs_alloc_lookup_eq(cnt_cur, gtbno, gtlen);
		ASSERT(i == 1);
		xfs_alloc_delete(cnt_cur);
		len += gtlen;
		xfs_alloc_update(bno_cur, bno, len);
	} else
		xfs_alloc_insert(bno_cur);
	xfs_alloc_rcheck(bno_cur);
	xfs_alloc_kcheck(bno_cur);
	xfs_btree_del_cursor(bno_cur);
	xfs_alloc_lookup_eq(cnt_cur, bno, len);
	xfs_alloc_insert(cnt_cur);
	xfs_alloc_rcheck(cnt_cur);
	xfs_alloc_kcheck(cnt_cur);
	agf = xfs_buf_to_agf(agbuf);
	agf->agf_freeblks += flen;
	xfs_alloc_log_agf(tp, agbuf, XFS_AGF_FREEBLKS);
	xfs_btree_del_cursor(cnt_cur);
	xfs_trans_mod_sb(tp, XFS_SB_FDBLOCKS, flen);
	return 1;
}

/* 
 * Visible (exported) allocation/free functions.
 */

/*
 * Return the number of free blocks left in the allocation group.
 */
xfs_extlen_t			/* number of remaining free blocks */
xfs_alloc_ag_freeblks(
	xfs_mount_t	*mp,	/* file system mount structure */
	xfs_trans_t	*tp,	/* transaction pointer */
	xfs_agnumber_t	agno,	/* allocation group number */
	int		flags)	/* XFS_ALLOC_FLAG_... */
{
	buf_t *agbuf;
	xfs_agf_t *agf;
	xfs_extlen_t freeblks;

	agbuf = xfs_alloc_read_agf(mp, tp, agno, flags);
	agf = xfs_buf_to_agf(agbuf);
	freeblks = agf->agf_freeblks;
	xfs_trans_brelse(tp, agbuf);
	return freeblks;
}

/*
 * Allocate an extent (fixed-size).
 */
xfs_fsblock_t			/* extent's starting block, or NULLFSBLOCK */
xfs_alloc_extent(
	xfs_trans_t	*tp,	/* transaction pointer */
	xfs_fsblock_t	bno,	/* requested starting block */
	xfs_extlen_t	len,	/* requested length */
	xfs_alloctype_t	type,	/* allocation type, see above definition */
	xfs_extlen_t	total)	/* total blocks needed in transaction */
{
	xfs_extlen_t rlen;
	xfs_fsblock_t rval;

	rval = xfs_alloc_vextent(tp, bno, len, len, &rlen, type, total);
	if (rval != NULLFSBLOCK)
		ASSERT(rlen == len);
	return rval;
}

/*
 * Fix up the btree freelist's size.
 * This is external so mkfs can call it, too.
 */
buf_t *				/* buffer for the a.g. freelist header */
xfs_alloc_fix_freelist(
	xfs_trans_t	*tp,	/* transaction pointer */
	xfs_agnumber_t	agno,	/* allocation group number */
	xfs_extlen_t	minlen,	/* minimum extent length, else reject */
	xfs_extlen_t	total,	/* total free blocks, else reject */
	int		flags)	/* XFS_ALLOC_FLAG_... */
{
	xfs_agblock_t agbno;
	buf_t *agbuf;
	xfs_agf_t *agf;
	xfs_agblock_t bno;
	buf_t *buf;
	xfs_extlen_t i;
	xfs_mount_t *mp;
	xfs_extlen_t need;

	mp = tp->t_mountp;
	agbuf = xfs_alloc_read_agf(mp, tp, agno, flags);
	if (!agbuf)
		return agbuf;
	agf = xfs_buf_to_agf(agbuf);
	need = XFS_MIN_FREELIST(agf);
	if (total && agf->agf_freecount < need)
		total += need - agf->agf_freecount;
	if (minlen > agf->agf_longest || total > agf->agf_freeblks) {
		xfs_trans_brelse(tp, agbuf);
		return NULL;
	}
	while (agf->agf_freecount > need) {
		bno = xfs_alloc_get_freelist(tp, agbuf, &buf);
		xfs_free_ag_extent(tp, agbuf, agno, bno, 1);
	}
	while (agf->agf_freecount < need) {
		i = need - agf->agf_freecount;
		agbno = xfs_alloc_ag_vextent(tp, agbuf, agno, 0, 1, i, &i, XFS_ALLOCTYPE_THIS_AG);
		if (agbno == NULLAGBLOCK)
			break;
		for (bno = agbno + i - 1; bno >= agbno; bno--) {
			buf = xfs_btree_get_bufs(mp, tp, agno, bno, 0);
			xfs_alloc_put_freelist(tp, agbuf, buf);
		}
	}
	return agbuf;
}

/*
 * Find the next freelist block number.
 */
xfs_agblock_t			/* a.g.-relative block number for btree list */
xfs_alloc_next_free(
	xfs_mount_t	*mp,	/* file system mount structure */
	xfs_trans_t	*tp,	/* transaction pointer */
	buf_t		*agbuf,	/* buffer for a.g. freelist header */
	xfs_agblock_t	bno)	/* current freelist block number */
{
	xfs_agf_t *agf;
	buf_t *buf;

	agf = xfs_buf_to_agf(agbuf);
	buf = xfs_btree_read_bufs(mp, tp, agf->agf_seqno, bno, 0);
	bno = *(xfs_agblock_t *)buf->b_un.b_addr;
	xfs_trans_brelse(tp, buf);
	return bno;
}

/*
 * Allocate an extent (variable-size).
 */
xfs_fsblock_t			/* extent's starting block, or NULLFSBLOCK */
xfs_alloc_vextent(
	xfs_trans_t	*tp,	/* transaction pointer */
	xfs_fsblock_t	bno,	/* requested starting block */
	xfs_extlen_t	minlen,	/* minimum requested length */
	xfs_extlen_t	maxlen,	/* maximum requested length */
	xfs_extlen_t	*len,	/* output: actual allocated length */
	xfs_alloctype_t	type,	/* allocation type, see above definition */
	xfs_extlen_t	total)	/* total blocks needed in transaction */
{
	xfs_agblock_t	agbno;
	buf_t		*agbuf;
	xfs_agf_t	*agf;
	xfs_agnumber_t	agno;
	xfs_agblock_t	agsize;
	int		flags;
	xfs_mount_t	*mp;
	xfs_alloctype_t	ntype;
	xfs_sb_t	*sbp;
	xfs_agnumber_t	tagno;

	ntype = type;
	mp = tp->t_mountp;
	sbp = &mp->m_sb;
	agsize = sbp->sb_agblocks;
	/* 
	 * These should really be asserts, left this way for now just
	 * for the benefit of xfs_test.
	 */
	if (xfs_fsb_to_agno(sbp, bno) >= sbp->sb_agcount ||
	    xfs_fsb_to_agbno(sbp, bno) >= sbp->sb_agblocks ||
	    minlen > maxlen || minlen > agsize || len == 0)
		return NULLFSBLOCK;
	if (maxlen > agsize)
		maxlen = agsize;
	agbno = NULLAGBLOCK;
	ntype = XFS_ALLOCTYPE_THIS_AG;
	switch (type) {
	case XFS_ALLOCTYPE_THIS_AG:
	case XFS_ALLOCTYPE_NEAR_BNO:
	case XFS_ALLOCTYPE_THIS_BNO:
		agno = xfs_fsb_to_agno(sbp, bno);
		agbuf = xfs_alloc_fix_freelist(tp, agno, minlen, total, 1);
		if (agbuf) {
			agf = xfs_buf_to_agf(agbuf);
			agbno = xfs_fsb_to_agbno(sbp, bno);
			agbno = xfs_alloc_ag_vextent(tp, agbuf, agno, agbno, minlen, maxlen, len, ntype); 
			ASSERT(agbno != NULLAGBLOCK);
		}
		break;
	case XFS_ALLOCTYPE_START_BNO:
		agbno = xfs_fsb_to_agbno(sbp, bno);
		ntype = XFS_ALLOCTYPE_NEAR_BNO;
		/* fall through */
	case XFS_ALLOCTYPE_ANY_AG:
	case XFS_ALLOCTYPE_START_AG:
		flags = XFS_ALLOC_FLAG_TRYLOCK;
		if (type == XFS_ALLOCTYPE_ANY_AG)
			tagno = agno = mp->m_agrotor;
		else
			tagno = agno = xfs_fsb_to_agno(sbp, bno);
		for (;;) {
			agbuf = xfs_alloc_fix_freelist(tp, tagno, minlen, total, flags);
			if (agbuf) {
				agf = xfs_buf_to_agf(agbuf);
				agbno = xfs_alloc_ag_vextent(tp, agbuf, tagno, agbno, minlen, maxlen, len, ntype); 
				ASSERT(agbno != NULLAGBLOCK);
				break;
			}
			if (tagno == agno && type == XFS_ALLOCTYPE_START_BNO)
				ntype = XFS_ALLOCTYPE_THIS_AG;
			if (++tagno == sbp->sb_agcount)
				tagno = 0;
			if (tagno == agno) {
				if (!flags)
					break;
				else {
					flags = 0;
					if (type == XFS_ALLOCTYPE_START_BNO)
						ntype = XFS_ALLOCTYPE_NEAR_BNO;
				}
			}
		}
		agno = tagno;
		mp->m_agrotor = (agno + 1) % sbp->sb_agcount;
		break;
	default:
		ASSERT(0);
		/* NOTREACHED */
	}
	if (agbno == NULLAGBLOCK)
		return NULLFSBLOCK;
	else
		return xfs_agb_to_fsb(sbp, agno, agbno);
}

/*
 * Free an extent.
 */
int				/* success/failure; will become void */
xfs_free_extent(
	xfs_trans_t	*tp,	/* transaction pointer */
	xfs_fsblock_t	bno,	/* starting block number of extent */
	xfs_extlen_t	len)	/* length of extent */
{
	xfs_agblock_t agbno;
	buf_t *agbuf;
	xfs_agnumber_t agno;
	int i;
	xfs_mount_t *mp;
	xfs_sb_t *sbp;

	mp = tp->t_mountp;
	sbp = &mp->m_sb;
	ASSERT(len != 0);
	agno = xfs_fsb_to_agno(sbp, bno);
	ASSERT(agno < sbp->sb_agcount);
	agbno = xfs_fsb_to_agbno(sbp, bno);
	ASSERT(agbno < sbp->sb_agblocks);
	agbuf = xfs_alloc_fix_freelist(tp, agno, 0, 0, 1);
	ASSERT(agbuf != NULL);
	i = xfs_free_ag_extent(tp, agbuf, agno, agbno, len);
	return i;
}
