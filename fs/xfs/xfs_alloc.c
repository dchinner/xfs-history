#ident	"$Revision: 1.3 $"

#include "xfs_sb.h"
#include "xfs_ag.h"
#include "xfs_alloc.h"
#include "xfs_ialloc.h"
#include "xfs_mount.h"
#include "sim.h"
#include <stddef.h>
#include <bstring.h>

xfs_alloc_cur_t *xfs_alloc_curfreelist;
#ifdef XFSADEBUG
int xfs_alloc_curfreecount;
xfs_alloc_cur_t *xfs_alloc_curalllist;
int xfs_alloc_curallcount;
#endif

u_int32_t xfs_magics[XFS_BTNUM_MAX] =
{
	XFS_ABTB_MAGIC, XFS_ABTC_MAGIC, XFS_IBT_MAGIC
};

/*
 * Prototypes for internal functions.
 */

#ifndef XFSADEBUG
#define NDEBUG
#endif
#include <assert.h>
#define	ASSERT(x)	assert(x)

#ifdef XFSADEBUG
void xfs_alloc_check_block(xfs_alloc_cur_t *, xfs_alloc_block_t *, int);
void xfs_alloc_check_ptr(xfs_alloc_cur_t *, xfs_agblock_t, int);
void xfs_alloc_check_rec(xfs_btnum_t, xfs_alloc_rec_t *, xfs_alloc_rec_t *);
void xfs_alloc_kcheck(xfs_alloc_cur_t *);
void xfs_alloc_kcheck_btree(xfs_alloc_cur_t *, xfs_aghdr_t *, xfs_agblock_t, int, xfs_alloc_rec_t *);
void xfs_alloc_rcheck(xfs_alloc_cur_t *);
void xfs_alloc_rcheck_btree(xfs_alloc_cur_t *, xfs_aghdr_t *, xfs_agblock_t, int);
xfs_agblock_t xfs_alloc_rcheck_btree_block(xfs_alloc_cur_t *, xfs_agnumber_t, xfs_agblock_t, xfs_agblock_t *, xfs_alloc_rec_t *, int);
#else
#define	xfs_alloc_check_block(a,b,c)
#define	xfs_alloc_check_ptr(a,b,c)
#define	xfs_alloc_check_rec(a,b,c)
#define	xfs_alloc_kcheck(a)
#define	xfs_alloc_rcheck(a)
#endif

buf_t *xfs_alloc_bread(xfs_trans_t *, xfs_agnumber_t, xfs_agblock_t);
xfs_agblock_t xfs_alloc_compute_diff(xfs_agblock_t, xfs_extlen_t, xfs_agblock_t, xfs_extlen_t, xfs_agblock_t *);
int xfs_alloc_decrement(xfs_alloc_cur_t *, int);
void xfs_alloc_del_cursor(xfs_alloc_cur_t *);
int xfs_alloc_delete(xfs_alloc_cur_t *);
int xfs_alloc_delrec(xfs_alloc_cur_t *, int);
xfs_alloc_cur_t *xfs_alloc_dup_cursor(xfs_alloc_cur_t *, int);
int xfs_alloc_firstrec(xfs_alloc_cur_t *, int);
xfs_agblock_t xfs_alloc_get_freelist(xfs_trans_t *, buf_t *, buf_t **);
int xfs_alloc_get_rec(xfs_alloc_cur_t *, xfs_agblock_t *, xfs_extlen_t *);
int xfs_alloc_increment(xfs_alloc_cur_t *, int);
xfs_alloc_cur_t *xfs_alloc_init_cursor(xfs_trans_t *, buf_t *, xfs_agnumber_t, xfs_btnum_t, int);
int xfs_alloc_insert(xfs_alloc_cur_t *);
int xfs_alloc_insrec(xfs_alloc_cur_t *, int, xfs_agblock_t *, xfs_alloc_rec_t *, xfs_alloc_cur_t **);
int xfs_alloc_islastblock(xfs_alloc_cur_t *, int);
int xfs_alloc_lastrec(xfs_alloc_cur_t *, int);
int xfs_alloc_lookup(xfs_alloc_cur_t *, xfs_lookup_t);
int xfs_alloc_lookup_eq(xfs_alloc_cur_t *, xfs_agblock_t, xfs_extlen_t);
int xfs_alloc_lookup_ge(xfs_alloc_cur_t *, xfs_agblock_t, xfs_extlen_t);
int xfs_alloc_lookup_le(xfs_alloc_cur_t *, xfs_agblock_t, xfs_extlen_t);
int xfs_alloc_lshift(xfs_alloc_cur_t *, int);
int xfs_alloc_newroot(xfs_alloc_cur_t *);
void xfs_alloc_put_freelist(xfs_trans_t *, buf_t *, buf_t *);
int xfs_alloc_rshift(xfs_alloc_cur_t *, int);
int xfs_alloc_split(xfs_alloc_cur_t *, int, xfs_agblock_t *, xfs_alloc_rec_t *, xfs_alloc_cur_t **);
int xfs_alloc_update(xfs_alloc_cur_t *, xfs_agblock_t, xfs_extlen_t);
void xfs_alloc_updkey(xfs_alloc_cur_t *, xfs_alloc_rec_t *, int);

/*
 * Internal functions.
 */

buf_t *
xfs_alloc_bread(xfs_trans_t *tp, xfs_agnumber_t agno, xfs_agblock_t agbno)
{
	daddr_t d;
	xfs_mount_t *mp;
	xfs_sb_t *sbp;

	mp = tp->t_mountp;
	sbp = mp->m_sb;
	d = xfs_agb_to_daddr(sbp, agno, agbno);
	return xfs_trans_bread(tp, mp->m_dev, d, mp->m_bsize);
}

#ifdef XFSADEBUG
void
xfs_alloc_check_block(xfs_alloc_cur_t *cur, xfs_alloc_block_t *block, int level)
{
	buf_t *agbuf;
	xfs_aghdr_t *agp;
	int bl;
	xfs_mount_t *mp;
	xfs_sb_t *sbp;
	xfs_trans_t *tp;

	agbuf = cur->agbuf;
	tp = cur->tp;
	mp = tp->t_mountp;
	agp = xfs_buf_to_agp(agbuf);
	sbp = mp->m_sb;
	bl = sbp->xfsb_blocklog;
	ASSERT(block->magic == xfs_magics[cur->btnum]);
	ASSERT(block->level == level);
	ASSERT(block->numrecs <= XFS_ALLOC_BLOCK_MAXRECS(bl, level));
	ASSERT(block->leftsib == NULLAGBLOCK || block->leftsib < agp->xfsag_length);
	ASSERT(block->rightsib == NULLAGBLOCK || block->rightsib < agp->xfsag_length);
}

void
xfs_alloc_check_ptr(xfs_alloc_cur_t *cur, xfs_agblock_t ptr, int level)
{
	buf_t *agbuf;
	xfs_aghdr_t *agp;

	ASSERT(level > 0);
	agbuf = cur->agbuf;
	agp = xfs_buf_to_agp(agbuf);
	ASSERT(ptr != NULLAGBLOCK && ptr < agp->xfsag_length);
}

/*
 * Debug routine: check that two records are in the right order.
 */
void
xfs_alloc_check_rec(xfs_btnum_t btnum, xfs_alloc_rec_t *r1, xfs_alloc_rec_t *r2)
{
	if (btnum == XFS_BTNUM_BNO)
		ASSERT(r1->startblock + r1->blockcount <= r2->startblock);
	else
		ASSERT(r1->blockcount < r2->blockcount ||
		       (r1->blockcount == r2->blockcount &&
			r1->startblock < r2->startblock));
}
#endif

/*
 * Compute best start block and diff for "near" allocations.
 * freelen >= wantlen already checked by caller.
 */
xfs_agblock_t
xfs_alloc_compute_diff(xfs_agblock_t wantbno, xfs_extlen_t wantlen, xfs_agblock_t freebno, xfs_extlen_t freelen, xfs_agblock_t *newbnop)
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
int
xfs_alloc_decrement(xfs_alloc_cur_t *cur, int level)
{
	xfs_agblock_t agbno;
	int bl;
	xfs_alloc_block_t *block;
	buf_t *buf;
	int lev;
	xfs_mount_t *mp;
	int ptr;
	xfs_sb_t *sbp;
	xfs_trans_t *tp;

	if (--cur->ptrs[level] > 0)
		return 1;
	buf = cur->bufs[level];
	block = xfs_buf_to_ablock(buf);
	xfs_alloc_check_block(cur, block, level);
	if (block->leftsib == NULLAGBLOCK)
		return 0;
	for (lev = level + 1; lev < cur->nlevels; lev++) {
		if (--cur->ptrs[lev] > 0)
			break;
	}
	if (lev == cur->nlevels)
		return 0;
	tp = cur->tp;
	mp = tp->t_mountp;
	sbp = mp->m_sb;
	bl = sbp->xfsb_blocklog;
	for (; lev > level; lev--) {
		buf = cur->bufs[lev];
		block = xfs_buf_to_ablock(buf);
		xfs_alloc_check_block(cur, block, lev);
		agbno = *XFS_ALLOC_PTR_ADDR(block, cur->ptrs[lev], bl);
		buf = cur->bufs[lev - 1] = xfs_alloc_bread(tp, cur->agno, agbno);
		block = xfs_buf_to_ablock(buf);
		xfs_alloc_check_block(cur, block, lev - 1);
		cur->ptrs[lev - 1] = block->numrecs;
	}
	return 1;
}

/*
 * Delete the cursor, unreferencing its buffers.
 */
void
xfs_alloc_del_cursor(xfs_alloc_cur_t *cur)
{
	cur->tp = (xfs_trans_t *)xfs_alloc_curfreelist;
	xfs_alloc_curfreelist = cur;
#ifdef XFSADEBUG
	xfs_alloc_curfreecount++;
#endif
}

/*
 * Delete the record pointed to by cur.
 */
int
xfs_alloc_delete(xfs_alloc_cur_t *cur)
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
int
xfs_alloc_delrec(xfs_alloc_cur_t *cur, int level)
{
	buf_t *agbuf;
	xfs_aghdr_t *agp;
	int bl;
	xfs_alloc_block_t *block;
	xfs_agblock_t bno = NULLAGBLOCK;
	buf_t *buf;
	int first;
	int i;
	int last;
	xfs_agblock_t lbno;
	buf_t *lbuf;
	xfs_alloc_block_t *left;
	xfs_agblock_t *lpp;
	int lrecs;
	xfs_alloc_rec_t *lrp;
	xfs_mount_t *mp;
	xfs_agblock_t *pp;
	int ptr;
	xfs_agblock_t rbno;
	buf_t *rbuf;
	xfs_alloc_block_t *right;
	xfs_alloc_rec_t *rp;
	xfs_agblock_t *rpp;
	xfs_alloc_block_t *rrblock;
	buf_t *rrbuf;
	int rrecs;
	xfs_alloc_rec_t *rrp;
	xfs_sb_t *sbp;
	xfs_alloc_cur_t *tcur;
	xfs_trans_t *tp;

	xfs_alloc_rcheck(cur);
	tp = cur->tp;
	mp = tp->t_mountp;
	sbp = mp->m_sb;
	bl = sbp->xfsb_blocklog;
	ptr = cur->ptrs[level];
	if (ptr == 0)
		return 0;
	buf = cur->bufs[level];
	block = xfs_buf_to_ablock(buf);
	xfs_alloc_check_block(cur, block, level);
	if (ptr > block->numrecs)
		return 0;
	rp = XFS_ALLOC_REC_ADDR(block, 1, bl);
	if (level > 0) {
		pp = XFS_ALLOC_PTR_ADDR(block, 1, bl);
		for (i = ptr; i < block->numrecs; i++) {
			rp[i - 1] = rp[i];
			xfs_alloc_check_ptr(cur, pp[i], level);
			pp[i - 1] = pp[i];
		}
		if (ptr < i) {
			first = (caddr_t)&pp[ptr - 1] - (caddr_t)block;
			last = ((caddr_t)&pp[i - 1] - 1) - (caddr_t)block;
			xfs_trans_log_buf(tp, buf, first, last);
		}
	} else {
		for (i = ptr; i < block->numrecs; i++)
			rp[i - 1] = rp[i];
	}
	if (ptr < i) {
		first = (caddr_t)&rp[ptr - 1] - (caddr_t)block;
		last = ((caddr_t)&rp[i - 1] - 1) - (caddr_t)block;
		xfs_trans_log_buf(tp, buf, first, last);
	}
	first = offsetof(xfs_alloc_block_t, numrecs);
	last = first + sizeof(block->numrecs) - 1;
	block->numrecs--;
	xfs_trans_log_buf(tp, buf, first, last);
	agbuf = cur->agbuf;
	agp = xfs_buf_to_agp(agbuf);
	if (level == 0 && cur->btnum == XFS_BTNUM_CNT &&
	    block->rightsib == NULLAGBLOCK && ptr > block->numrecs) {
		if (block->numrecs) {
			rrp = XFS_ALLOC_REC_ADDR(block, block->numrecs, bl);
			agp->xfsag_longest = rrp->blockcount;
		} else
			agp->xfsag_longest = 0;
		first = offsetof(xfs_aghdr_t, xfsag_longest);
		last = first + sizeof(agp->xfsag_longest) - 1;
		xfs_trans_log_buf(tp, agbuf, first, last);
	}
	if (level == cur->nlevels - 1) {
		if (block->numrecs == 1 && level > 0) {
			xfs_alloc_put_freelist(tp, agbuf, buf);
			agp->xfsag_roots[cur->btnum] = *pp;
			agp->xfsag_levels[cur->btnum]--;
			first = offsetof(xfs_aghdr_t, xfsag_roots) + 
				cur->btnum * sizeof(agp->xfsag_roots[0]);
			last = offsetof(xfs_aghdr_t, xfsag_levels) +
			    (cur->btnum + 1) * sizeof(agp->xfsag_levels[0]) - 1;
			xfs_trans_log_buf(tp, agbuf, first, last);
			cur->bufs[level] = (buf_t *)0;
			cur->nlevels--;
		}
		xfs_alloc_rcheck(cur);
		return 1;
	}
	if (ptr == 1)
		xfs_alloc_updkey(cur, rp, level + 1);
	xfs_alloc_rcheck(cur);
	if (block->numrecs >= XFS_ALLOC_BLOCK_MINRECS(bl, level))
		return 1;
	rbno = block->rightsib;
	lbno = block->leftsib;
	ASSERT(rbno != NULLAGBLOCK || lbno != NULLAGBLOCK);
	tcur = xfs_alloc_dup_cursor(cur, __LINE__);
	if (rbno != NULLAGBLOCK) {
		xfs_alloc_lastrec(tcur, level);
		xfs_alloc_increment(tcur, level);
		xfs_alloc_lastrec(tcur, level);
		rbuf = tcur->bufs[level];
		right = xfs_buf_to_ablock(rbuf);
		xfs_alloc_check_block(cur, right, level);
		bno = right->leftsib;
		if (right->numrecs - 1 >= XFS_ALLOC_BLOCK_MINRECS(bl, level)) {
			if (xfs_alloc_lshift(tcur, level)) {
				ASSERT(block->numrecs >= XFS_ALLOC_BLOCK_MINRECS(bl, level));
				xfs_alloc_del_cursor(tcur);
				return 1;
			}
		}
		rrecs = right->numrecs;
		if (lbno != NULLAGBLOCK) {
			xfs_alloc_firstrec(tcur, level);
			xfs_alloc_decrement(tcur, level);
		}
	}
	if (lbno != NULLAGBLOCK) {
		xfs_alloc_firstrec(tcur, level);
		xfs_alloc_decrement(tcur, level);	/* to last */
		xfs_alloc_firstrec(tcur, level);
		lbuf = tcur->bufs[level];
		left = xfs_buf_to_ablock(lbuf);
		xfs_alloc_check_block(cur, left, level);
		bno = left->rightsib;
		if (left->numrecs - 1 >= XFS_ALLOC_BLOCK_MINRECS(bl, level)) {
			if (xfs_alloc_rshift(tcur, level)) {
				ASSERT(block->numrecs >= XFS_ALLOC_BLOCK_MINRECS(bl, level));
				xfs_alloc_del_cursor(tcur);
				cur->ptrs[level]++;
				return 1;
			}
		}
		lrecs = left->numrecs;
	}
	xfs_alloc_del_cursor(tcur);
	ASSERT(bno != NULLAGBLOCK);
	if (lbno != NULLAGBLOCK &&
	    lrecs + block->numrecs <= XFS_ALLOC_BLOCK_MAXRECS(bl, level)) {
		rbno = bno;
		right = block;
		rbuf = buf;
		lbuf = xfs_alloc_bread(tp, cur->agno, lbno);
		left = xfs_buf_to_ablock(lbuf);
		xfs_alloc_check_block(cur, left, level);
	} else if (rbno != NULLAGBLOCK &&
		   rrecs + block->numrecs <= XFS_ALLOC_BLOCK_MAXRECS(bl, level)) {
		lbno = bno;
		left = block;
		lbuf = buf;
		rbuf = xfs_alloc_bread(tp, cur->agno, rbno);
		right = xfs_buf_to_ablock(rbuf);
		xfs_alloc_check_block(cur, right, level);
	} else {
		xfs_alloc_rcheck(cur);
		return 1;
	}
	lrp = XFS_ALLOC_REC_ADDR(left, left->numrecs + 1, bl);
	rrp = XFS_ALLOC_REC_ADDR(right, 1, bl);
	if (level > 0) {
		lpp = XFS_ALLOC_PTR_ADDR(left, left->numrecs + 1, bl);
		rpp = XFS_ALLOC_PTR_ADDR(right, 1, bl);
		for (i = 0; i < right->numrecs; i++) {
			lrp[i] = rrp[i];
			xfs_alloc_check_ptr(cur, rpp[i], level);
			lpp[i] = rpp[i];
		}
	} else {
		for (i = 0; i < right->numrecs; i++)
			lrp[i] = rrp[i];
	}
	if (buf != lbuf) {
		cur->bufs[level] = lbuf;
		cur->ptrs[level] += left->numrecs;
	} else if (level + 1 < cur->nlevels)
		xfs_alloc_increment(cur, level + 1);
	left->numrecs += right->numrecs;
	left->rightsib = right->rightsib;
	if (left->rightsib != NULLAGBLOCK) {
		rrbuf = xfs_alloc_bread(tp, cur->agno, left->rightsib);
		rrblock = xfs_buf_to_ablock(rrbuf);
		xfs_alloc_check_block(cur, rrblock, level);
		rrblock->leftsib = lbno;
		first = offsetof(xfs_alloc_block_t, leftsib);
		last = first + sizeof(rrblock->leftsib) - 1;
		xfs_trans_log_buf(tp, rrbuf, first, last);
	}
	xfs_alloc_put_freelist(tp, agbuf, rbuf);
	xfs_alloc_rcheck(cur);
	return 2;
}

/*
 * Duplicate the cursor.
 * Add references to its buffers.
 */
xfs_alloc_cur_t *
xfs_alloc_dup_cursor(xfs_alloc_cur_t *cur, int lineno)
{
	buf_t *agbuf;
	xfs_aghdr_t *agp;
	buf_t *buf;
	int i;
	xfs_mount_t *mp;
	xfs_alloc_cur_t *newcur;
	xfs_trans_t *tp;

	agbuf = cur->agbuf;
	agp = xfs_buf_to_agp(agbuf);
	tp = cur->tp;
	mp = tp->t_mountp;
	newcur = xfs_alloc_init_cursor(tp, agbuf, agp->xfsag_seqno, cur->btnum, lineno);
	newcur->rec = cur->rec;
	newcur->nlevels = cur->nlevels;
	for (i = 0; i < newcur->nlevels; i++) {
		newcur->ptrs[i] = cur->ptrs[i];
		if ((buf = cur->bufs[i]) != (buf_t *)0)
			newcur->bufs[i] = xfs_trans_bread(tp, mp->m_dev, buf->b_blkno, mp->m_bsize);
		else
			newcur->bufs[i] = (buf_t *)0;
	}
	return newcur;
}

/*
 * Point to the first record at that level.  
 * Other levels are unaffected.
 */
int
xfs_alloc_firstrec(xfs_alloc_cur_t *cur, int level)
{
	xfs_alloc_block_t *block;
	buf_t *buf;

	buf = cur->bufs[level];
	block = xfs_buf_to_ablock(buf);
	xfs_alloc_check_block(cur, block, level);
	if (!block->numrecs)
		return 0;
	cur->ptrs[level] = 1;
	return 1;
}

/*
 * Get a block from the freelist.
 * Returns with the buffer gotten.
 */
xfs_agblock_t
xfs_alloc_get_freelist(xfs_trans_t *tp, buf_t *agbuf, buf_t **bufp)
{
	xfs_aghdr_t *agp;
	xfs_agblock_t bno;
	xfs_alloc_block_t *block;
	buf_t *buf;
	int first;
	int last;
	xfs_mount_t *mp;
	xfs_sb_t *sbp;

	mp = tp->t_mountp;
	agp = xfs_buf_to_agp(agbuf);
	bno = agp->xfsag_freelist;
	if (bno == NULLAGBLOCK)
		return NULLAGBLOCK;
	sbp = mp->m_sb;
	buf = xfs_alloc_bread(tp, agp->xfsag_seqno, bno);
	block = xfs_buf_to_ablock(buf);
	agp->xfsag_flist_count--;
	agp->xfsag_freelist = *(xfs_agblock_t *)block;
	first = offsetof(xfs_aghdr_t, xfsag_freelist);
	last = offsetof(xfs_aghdr_t, xfsag_flist_count) + sizeof(agp->xfsag_flist_count) - 1;
	xfs_trans_log_buf(tp, agbuf, first, last);
	*bufp = buf;
	return bno;
}

/* 
 * Get the data from the pointed-to record.
 */
int
xfs_alloc_get_rec(xfs_alloc_cur_t *cur, xfs_agblock_t *bno, xfs_extlen_t *len)
{
	int bl;
	xfs_alloc_block_t *block;
	buf_t *buf;
	xfs_mount_t *mp;
	int ptr;
	xfs_alloc_rec_t *rec;
	xfs_sb_t *sbp;
	xfs_trans_t *tp;

	buf = cur->bufs[0];
	ptr = cur->ptrs[0];
	block = xfs_buf_to_ablock(buf);
	xfs_alloc_check_block(cur, block, 0);
	if (ptr > block->numrecs || ptr <= 0)
		return 0;
	tp = cur->tp;
	mp = tp->t_mountp;
	sbp = mp->m_sb;
	bl = sbp->xfsb_blocklog;
	rec = XFS_ALLOC_REC_ADDR(block, ptr, bl);
	*bno = rec->startblock;
	*len = rec->blockcount;
	return 1;
}

/*
 * Increment cursor by one record at the level.
 * For nonzero levels the leaf-ward information is untouched.
 */
int
xfs_alloc_increment(xfs_alloc_cur_t *cur, int level)
{
	xfs_agblock_t agbno;
	int bl;
	xfs_alloc_block_t *block;
	buf_t *buf;
	daddr_t d;
	int lev;
	xfs_mount_t *mp;
	int ptr;
	xfs_sb_t *sbp;
	xfs_trans_t *tp;

	buf = cur->bufs[level];
	block = xfs_buf_to_ablock(buf);
	xfs_alloc_check_block(cur, block, level);
	if (++cur->ptrs[level] <= block->numrecs)
		return 1;
	if (block->rightsib == NULLAGBLOCK)
		return 0;
	for (lev = level + 1; lev < cur->nlevels; lev++) {
		buf = cur->bufs[lev];
		block = xfs_buf_to_ablock(buf);
		xfs_alloc_check_block(cur, block, lev);
		if (++cur->ptrs[lev] <= block->numrecs)
			break;
	}
	if (lev == cur->nlevels)
		return 0;
	tp = cur->tp;
	mp = tp->t_mountp;
	sbp = mp->m_sb;
	bl = sbp->xfsb_blocklog;
	for (; lev > level; lev--) {
		buf = cur->bufs[lev];
		block = xfs_buf_to_ablock(buf);
		xfs_alloc_check_block(cur, block, lev);
		agbno = *XFS_ALLOC_PTR_ADDR(block, cur->ptrs[lev], bl);
		cur->bufs[lev - 1] = xfs_alloc_bread(tp, cur->agno, agbno);
		cur->ptrs[lev - 1] = 1;
	}
	return 1;
}

/*
 * Allocate and initialize a new cursor.
 */
xfs_alloc_cur_t *
xfs_alloc_init_cursor(xfs_trans_t *tp, buf_t *agbuf, xfs_agnumber_t agno, xfs_btnum_t btnum, int lineno)
{
	xfs_aghdr_t *agp;
	xfs_alloc_cur_t *cur;

	if (xfs_alloc_curfreelist) {
		cur = xfs_alloc_curfreelist;
		xfs_alloc_curfreelist = (xfs_alloc_cur_t *)cur->tp;
#ifdef XFSADEBUG
		xfs_alloc_curfreecount--;
#endif
		if (tp != cur->tp)
			bzero(cur->bufs, sizeof(cur->bufs));
	} else {
		cur = (xfs_alloc_cur_t *)kmem_zalloc(sizeof(*cur), 0);
#ifdef XFSADEBUG
		cur->next = xfs_alloc_curalllist;
		xfs_alloc_curalllist = cur;
		xfs_alloc_curallcount++;
#endif
	}
	cur->tp = tp;
	cur->agbuf = agbuf;
	cur->agno = agno;
#ifdef XFSADEBUG
	cur->lineno = lineno;
#endif
	cur->btnum = btnum;
	agp = xfs_buf_to_agp(agbuf);
	cur->nlevels = agp->xfsag_levels[btnum];
	return cur;
}

/*
 * Insert the current record at the point referenced by cur.
 */
int
xfs_alloc_insert(xfs_alloc_cur_t *cur)
{
	int i;
	int level;
	xfs_agblock_t nbno;
	xfs_alloc_cur_t *ncur;
	xfs_alloc_rec_t nrec;
	xfs_alloc_cur_t *pcur;

	level = 0;
	nbno = NULLAGBLOCK;
	nrec = cur->rec;
	ncur = (xfs_alloc_cur_t *)0;
	pcur = cur;
	do {
		i = xfs_alloc_insrec(pcur, level++, &nbno, &nrec, &ncur);
		if (pcur != cur && (ncur || nbno == NULLAGBLOCK)) {
			cur->nlevels = pcur->nlevels;
			xfs_alloc_del_cursor(pcur);
		}
		if (ncur) {
			pcur = ncur;
			ncur = (xfs_alloc_cur_t *)0;
		}
	} while (nbno != NULLAGBLOCK);
	return i;
}

/*
 * Insert one record/level.  Return information to the caller
 * allowing the next level up to proceed if necessary.
 */
int
xfs_alloc_insrec(xfs_alloc_cur_t *cur, int level, xfs_agblock_t *bnop, xfs_alloc_rec_t *recp, xfs_alloc_cur_t **curp)
{
	buf_t *agbuf;
	xfs_aghdr_t *agp;
	int bl;
	xfs_alloc_block_t *block;
	buf_t *buf;
	int first;
	int i;
	int last;
	xfs_mount_t *mp;
	xfs_agblock_t nbno;
	xfs_alloc_cur_t *ncur = (xfs_alloc_cur_t *)0;
	xfs_alloc_rec_t nrec;
#ifdef XFSADEBUG
	int op = 0;
#endif
	int optr;
	xfs_agblock_t *pp;
	int ptr;
	xfs_alloc_rec_t *rbase;
	xfs_alloc_rec_t *rp;
	xfs_sb_t *sbp;
	xfs_trans_t *tp;
	xfs_alloc_rec_t xxrec;

	if (level >= cur->nlevels) {
		i = xfs_alloc_newroot(cur);
		*bnop = NULLAGBLOCK;
		return i;
	}
	xfs_alloc_rcheck(cur);
	optr = ptr = cur->ptrs[level];
	if (ptr == 0)
		return 0;
	buf = cur->bufs[level];
	block = xfs_buf_to_ablock(buf);
	xfs_alloc_check_block(cur, block, level);
	if (ptr <= block->numrecs) {
		rp = XFS_ALLOC_REC_ADDR(block, ptr, bl);
		xfs_alloc_check_rec(cur->btnum, recp, rp);
	}
	tp = cur->tp;
	mp = tp->t_mountp;
	sbp = mp->m_sb;
	bl = sbp->xfsb_blocklog;
	nbno = NULLAGBLOCK;
	if (block->numrecs == XFS_ALLOC_BLOCK_MAXRECS(bl, level)) {
		if (xfs_alloc_rshift(cur, level)) {
#ifdef XFSADEBUG
			op = 'r';
#endif
		} else if (xfs_alloc_lshift(cur, level)) {
#ifdef XFSADEBUG
			op = 'l';
#endif
			optr = ptr = cur->ptrs[level];
		} else if (xfs_alloc_split(cur, level, &nbno, &nrec, &ncur)) {
#ifdef XFSADEBUG
			op = 's';
#endif
			buf = cur->bufs[level];
			block = xfs_buf_to_ablock(buf);
			xfs_alloc_check_block(cur, block, level);
			ptr = cur->ptrs[level];
		} else
			return 0;
	}
	rp = XFS_ALLOC_REC_ADDR(block, 1, bl);
	if (level > 0) {
		pp = XFS_ALLOC_PTR_ADDR(block, 1, bl);
		for (i = block->numrecs; i >= ptr; i--) {
			rp[i] = rp[i - 1];
			xfs_alloc_check_ptr(cur, pp[i - 1], level);
			pp[i] = pp[i - 1];
		}
		xfs_alloc_check_ptr(cur, *bnop, level);
		pp[i] = *bnop;
		first = (caddr_t)&pp[i] - (caddr_t)block;
		last = (caddr_t)&pp[block->numrecs] - (caddr_t)block +
			sizeof(pp[i]) - 1;
		xfs_trans_log_buf(tp, buf, first, last);
	} else {
		for (i = block->numrecs; i >= ptr; i--)
			rp[i] = rp[i - 1];
	}
	rp[i] = *recp;
	first = (caddr_t)&rp[i] - (caddr_t)block;
	last = (caddr_t)&rp[block->numrecs] - (caddr_t)block +
		sizeof(rp[i]) - 1;
	xfs_trans_log_buf(tp, buf, first, last);
	block->numrecs++;
	first = offsetof(xfs_alloc_block_t, numrecs);
	last = first + sizeof(block->numrecs) - 1;
	xfs_trans_log_buf(tp, buf, first, last);
	if (ptr < block->numrecs)
		xfs_alloc_check_rec(cur->btnum, rp + i, rp + i + 1);
	if (optr == 1)
		xfs_alloc_updkey(cur, recp, level + 1);
	agbuf = cur->agbuf;
	agp = xfs_buf_to_agp(agbuf);
	if (level == 0 && cur->btnum == XFS_BTNUM_CNT &&
	    block->rightsib == NULLAGBLOCK &&
	    recp->blockcount > agp->xfsag_longest) {
		agp->xfsag_longest = recp->blockcount;
		first = offsetof(xfs_aghdr_t, xfsag_longest);
		last = first + sizeof(agp->xfsag_longest) - 1;
		xfs_trans_log_buf(tp, agbuf, first, last);
	}
	*bnop = nbno;
	xfs_alloc_rcheck(cur);
	if (nbno != NULLAGBLOCK) {
		*recp = nrec;
		*curp = ncur;
	} else
		xfs_alloc_kcheck(cur);
	return 1;
}

/*
 * Check for last leaf block.
 */
int
xfs_alloc_islastblock(xfs_alloc_cur_t *cur, int level)
{
	xfs_alloc_block_t *block;
	buf_t *buf;

	buf = cur->bufs[level];
	block = xfs_buf_to_ablock(buf);
	xfs_alloc_check_block(cur, block, level);
	return block->rightsib == NULLAGBLOCK;
}

#ifdef XFSADEBUG
/*
 * Debug routine to check key consistency.
 */
void
xfs_alloc_kcheck(xfs_alloc_cur_t *cur)
{
	buf_t *agbuf;
	xfs_aghdr_t *agp;
	xfs_agblock_t bno;
	int levels;

	agbuf = cur->agbuf;
	agp = xfs_buf_to_agp(agbuf);
	bno = agp->xfsag_roots[cur->btnum];
	levels = agp->xfsag_levels[cur->btnum];
	ASSERT(levels == cur->nlevels);
	xfs_alloc_kcheck_btree(cur, agp, bno, levels - 1, (xfs_alloc_rec_t *)0);
}

void
xfs_alloc_kcheck_btree(xfs_alloc_cur_t *cur, xfs_aghdr_t *agp, xfs_agblock_t bno, int level, xfs_alloc_rec_t *kp)
{
	int bl;
	xfs_alloc_block_t *block;
	buf_t *buf;
	daddr_t d;
	int i;
	xfs_mount_t *mp;
	xfs_agblock_t *pp;
	xfs_alloc_rec_t *rp;
	xfs_sb_t *sbp;
	xfs_trans_t *tp;

	ASSERT(bno != NULLAGBLOCK);
	tp = cur->tp;
	mp = tp->t_mountp;
	sbp = mp->m_sb;
	bl = sbp->xfsb_blocklog;
	buf = xfs_alloc_bread(tp, agp->xfsag_seqno, bno);
	block = xfs_buf_to_ablock(buf);
	xfs_alloc_check_block(cur, block, level);
	rp = XFS_ALLOC_REC_ADDR(block, 1, bl);
	if (kp)
		ASSERT(rp->startblock == kp->startblock &&
		       rp->blockcount == kp->blockcount);
	if (level > 0) {
		pp = XFS_ALLOC_PTR_ADDR(block, 1, bl);
		if (*pp != NULLAGBLOCK) {
			for (i = 1; i <= block->numrecs; i++, pp++, rp++)
				xfs_alloc_kcheck_btree(cur, agp, *pp, level - 1, rp);
		}
	}
}
#endif

/*
 * Point to the last record in the current block at the given level.
 */
int
xfs_alloc_lastrec(xfs_alloc_cur_t *cur, int level)
{
	xfs_alloc_block_t *block;
	buf_t *buf;

	buf = cur->bufs[level];
	block = xfs_buf_to_ablock(buf);
	xfs_alloc_check_block(cur, block, level);
	if (!block->numrecs)
		return 0;
	cur->ptrs[level] = block->numrecs;
	return 1;
}

/*
 * Lookup the record.  The cursor is made to point to it, based on dir.
 */
int
xfs_alloc_lookup(xfs_alloc_cur_t *cur, xfs_lookup_t dir)
{
	xfs_agblock_t agbno;
	buf_t *agbuf;
	xfs_agnumber_t agno;
	xfs_aghdr_t *agp;
	int bl;
	xfs_alloc_block_t *block;
	buf_t *buf;
	daddr_t d;
	int diff;
	int high;
	int i;
	xfs_alloc_rec_t *kbase;
	int keyno;
	xfs_alloc_rec_t *kp;
	int level;
	int low;
	xfs_mount_t *mp;
	xfs_alloc_rec_t *rp;
	xfs_sb_t *sbp;
	xfs_trans_t *tp;

	xfs_alloc_rcheck(cur);
	xfs_alloc_kcheck(cur);
	tp = cur->tp;
	mp = tp->t_mountp;
	agbuf = cur->agbuf;
	agp = xfs_buf_to_agp(agbuf);
	agno = agp->xfsag_seqno;
	agbno = agp->xfsag_roots[cur->btnum];
	sbp = mp->m_sb;
	bl = sbp->xfsb_blocklog;
	rp = &cur->rec;
	for (level = cur->nlevels - 1, diff = 1; level >= 0; level--) {
		d = xfs_agb_to_daddr(sbp, agno, agbno);
		buf = cur->bufs[level];
		if (buf && buf->b_blkno != d)
			buf = (buf_t *)0;
		if (!buf)
			buf = cur->bufs[level] = xfs_trans_bread(tp, mp->m_dev, d, mp->m_bsize);
		block = xfs_buf_to_ablock(buf);
		xfs_alloc_check_block(cur, block, level);
		if (diff == 0)
			keyno = 1;
		else {
			kbase = XFS_ALLOC_REC_ADDR(block, 1, bl);
			low = 1;
			if (!(high = block->numrecs)) {
				ASSERT(level == 0);
				cur->ptrs[0] = dir != XFS_LOOKUP_LE;
				return 0;
			}
			while (low <= high) {
				keyno = (low + high) >> 1;
				kp = kbase + keyno - 1;
				if (cur->btnum == XFS_BTNUM_BNO)
					diff = kp->startblock - rp->startblock;
				else if (!(diff = kp->blockcount - rp->blockcount))
					diff = kp->startblock - rp->startblock;
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
			agbno = *XFS_ALLOC_PTR_ADDR(block, keyno, bl);
			xfs_alloc_check_ptr(cur, agbno, level);
			cur->ptrs[level] = keyno;
		}
	}
	if (dir != XFS_LOOKUP_LE && diff < 0) {
		keyno++;
		/*
		 * If ge search and we went off the end of the block, but it's
		 * not the last block, we're in the wrong block.
		 */
		if (dir == XFS_LOOKUP_GE && keyno > block->numrecs &&
		    block->rightsib != NULLAGBLOCK) {
			cur->ptrs[0] = keyno;
			i = xfs_alloc_increment(cur, 0);
			ASSERT(i == 1);
			return 1;
		}
	}
	else if (dir == XFS_LOOKUP_LE && diff > 0)
		keyno--;
	cur->ptrs[0] = keyno;
	if (keyno == 0 || keyno > block->numrecs)
		return 0;
	else
		return dir != XFS_LOOKUP_EQ || diff == 0;
}

int
xfs_alloc_lookup_eq(xfs_alloc_cur_t *cur, xfs_agblock_t bno, xfs_extlen_t len)
{
	cur->rec.startblock = bno;
	cur->rec.blockcount = len;
	return xfs_alloc_lookup(cur, XFS_LOOKUP_EQ);
}

int
xfs_alloc_lookup_ge(xfs_alloc_cur_t *cur, xfs_agblock_t bno, xfs_extlen_t len)
{
	cur->rec.startblock = bno;
	cur->rec.blockcount = len;
	return xfs_alloc_lookup(cur, XFS_LOOKUP_GE);
}

int
xfs_alloc_lookup_le(xfs_alloc_cur_t *cur, xfs_agblock_t bno, xfs_extlen_t len)
{
	cur->rec.startblock = bno;
	cur->rec.blockcount = len;
	return xfs_alloc_lookup(cur, XFS_LOOKUP_LE);
}

/*
 * Move 1 record left from cur/level if possible.
 * Update cur to reflect the new path.
 */
int
xfs_alloc_lshift(xfs_alloc_cur_t *cur, int level)
{
	int bl;
	daddr_t d;
	int first;
	int i;
	int last;
	buf_t *lbuf;
	xfs_alloc_block_t *left;
	int lev;
	xfs_agblock_t *lpp;
	xfs_alloc_rec_t *lrp;
	xfs_mount_t *mp;
	int ptr;
	buf_t *rbuf;
	xfs_alloc_block_t *right;
	xfs_agblock_t *rpp;
	xfs_alloc_rec_t *rrp;
	xfs_sb_t *sbp;
	xfs_trans_t *tp;

	xfs_alloc_rcheck(cur);
	rbuf = cur->bufs[level];
	right = xfs_buf_to_ablock(rbuf);
	xfs_alloc_check_block(cur, right, level);
	if (right->leftsib == NULLAGBLOCK)
		return 0;
	if (cur->ptrs[level] <= 1)
		return 0;
	tp = cur->tp;
	mp = tp->t_mountp;
	sbp = mp->m_sb;
	lbuf = xfs_alloc_bread(tp, cur->agno, right->leftsib);
	left = xfs_buf_to_ablock(lbuf);
	xfs_alloc_check_block(cur, left, level);
	bl = sbp->xfsb_blocklog;
	if (left->numrecs == XFS_ALLOC_BLOCK_MAXRECS(bl, level))
		return 0;
	lrp = XFS_ALLOC_REC_ADDR(left, left->numrecs + 1, bl);
	rrp = XFS_ALLOC_REC_ADDR(right, 1, bl);
	*lrp = *rrp;
	first = (caddr_t)lrp - (caddr_t)left;
	last = first + sizeof(*lrp) - 1;
	xfs_trans_log_buf(tp, lbuf, first, last);
	if (level > 0) {
		lpp = XFS_ALLOC_PTR_ADDR(left, left->numrecs + 1, bl);
		rpp = XFS_ALLOC_PTR_ADDR(right, 1, bl);
		xfs_alloc_check_ptr(cur, *rpp, level);
		*lpp = *rpp;
		first = (caddr_t)lpp - (caddr_t)left;
		last = first + sizeof(*lpp) - 1;
		xfs_trans_log_buf(tp, lbuf, first, last);
	}
	left->numrecs++;
	first = offsetof(xfs_alloc_block_t, numrecs);
	last = first + sizeof(left->numrecs) - 1;
	xfs_trans_log_buf(tp, lbuf, first, last);
	xfs_alloc_check_rec(cur->btnum, lrp - 1, lrp);
	right->numrecs--;
	xfs_trans_log_buf(tp, rbuf, first, last);
	if (level > 0) {
		for (i = 0; i < right->numrecs; i++) {
			rrp[i] = rrp[i + 1];
			xfs_alloc_check_ptr(cur, rpp[i + 1], level);
			rpp[i] = rpp[i + 1];
		}
		first = (caddr_t)rpp - (caddr_t)right;
		last = first + sizeof(rpp[0]) * right->numrecs - 1;
		xfs_trans_log_buf(tp, rbuf, first, last);
	} else {
		for (i = 0; i < right->numrecs; i++)
			rrp[i] = rrp[i + 1];
	}
	first = (caddr_t)rrp - (caddr_t)right;
	last = first + sizeof(rrp[0]) * right->numrecs - 1;
	xfs_trans_log_buf(tp, rbuf, first, last);
	xfs_alloc_updkey(cur, rrp, level + 1);
	cur->ptrs[level]--;
	xfs_alloc_rcheck(cur);
	return 1;
}

/*
 * Allocate a new root block, fill it in.
 */
int
xfs_alloc_newroot(xfs_alloc_cur_t *cur)
{
	buf_t *agbuf;
	xfs_aghdr_t *agp;
	int bl;
	xfs_alloc_block_t *block;
	buf_t *buf;
	daddr_t d;
	int first;
	int last;
	xfs_agblock_t lbno;
	buf_t *lbuf;
	xfs_alloc_block_t *left;
	xfs_mount_t *mp;
	xfs_agblock_t nbno;
	buf_t *nbuf;
	xfs_alloc_block_t *new;
	xfs_agblock_t nptr;
	xfs_agblock_t *pp;
	xfs_agblock_t rbno;
	buf_t *rbuf;
	xfs_alloc_block_t *right;
	xfs_alloc_rec_t *rp;
	xfs_sb_t *sbp;
	xfs_trans_t *tp;

	xfs_alloc_rcheck(cur);
	ASSERT(cur->nlevels < XFS_ALLOC_MAXLEVELS);
	tp = cur->tp;
	mp = tp->t_mountp;
	sbp = mp->m_sb;
	bl = sbp->xfsb_blocklog;
	agbuf = cur->agbuf;
	agp = xfs_buf_to_agp(agbuf);
	nbno = xfs_alloc_get_freelist(tp, agbuf, &nbuf);
	if (nbno == NULLAGBLOCK)
		return 0;
	new = xfs_buf_to_ablock(nbuf);
	agp->xfsag_roots[cur->btnum] = nbno;
	agp->xfsag_levels[cur->btnum]++;
	first = (caddr_t)&agp->xfsag_roots[cur->btnum] - (caddr_t)agp;
	last = (caddr_t)&agp->xfsag_levels[cur->btnum] - (caddr_t)agp +
		sizeof(agp->xfsag_levels[cur->btnum]) - 1;
	xfs_trans_log_buf(tp, agbuf, first, last);
	buf = cur->bufs[cur->nlevels - 1];
	block = xfs_buf_to_ablock(buf);
	xfs_alloc_check_block(cur, block, cur->nlevels - 1);
	if (block->rightsib != NULLAGBLOCK) {
		lbuf = buf;
		lbno = xfs_daddr_to_agbno(sbp, lbuf->b_blkno);
		left = block;
		rbno = left->rightsib;
		buf = rbuf = xfs_alloc_bread(tp, cur->agno, rbno);
		right = xfs_buf_to_ablock(rbuf);
		xfs_alloc_check_block(cur, right, cur->nlevels - 1);
		nptr = 1;
	} else {
		rbuf = buf;
		rbno = xfs_daddr_to_agbno(sbp, rbuf->b_blkno);
		right = block;
		lbno = right->leftsib;
		buf = lbuf = xfs_alloc_bread(tp, cur->agno, lbno);
		left = xfs_buf_to_ablock(lbuf);
		xfs_alloc_check_block(cur, left, cur->nlevels - 1);
		nptr = 2;
	}
	new->magic = xfs_magics[cur->btnum];
	new->level = cur->nlevels;
	new->numrecs = 2;
	new->leftsib = new->rightsib = NULLAGBLOCK;
	ASSERT(lbno != NULLAGBLOCK && rbno != NULLAGBLOCK);
	rp = XFS_ALLOC_REC_ADDR(new, 1, bl);
	rp[0] = *XFS_ALLOC_REC_ADDR(left, 1, bl);
	rp[1] = *XFS_ALLOC_REC_ADDR(right, 1, bl);
	first = offsetof(xfs_alloc_block_t, magic);
	last = ((caddr_t)&rp[2] - (caddr_t)new) - 1;
	xfs_trans_log_buf(tp, nbuf, first, last);
	pp = XFS_ALLOC_PTR_ADDR(new, 1, bl);
	pp[0] = lbno;
	pp[1] = rbno;
	first = (caddr_t)pp - (caddr_t)new;
	last = ((caddr_t)&pp[2] - (caddr_t)new) - 1;
	xfs_trans_log_buf(tp, nbuf, first, last);
	cur->bufs[cur->nlevels] = nbuf;
	cur->ptrs[cur->nlevels] = nptr;
	cur->nlevels++;
	xfs_alloc_rcheck(cur);
	xfs_alloc_kcheck(cur);
	return 1;
}

/*
 * Put the buffer on the freelist for the ag.
 */
void
xfs_alloc_put_freelist(xfs_trans_t *tp, buf_t *agbuf, buf_t *buf)
{
	xfs_aghdr_t *agp;
	xfs_alloc_block_t *block;
	xfs_agblock_t bno;
	int first;
	int last;
	xfs_mount_t *mp;
	xfs_sb_t *sbp;

	mp = tp->t_mountp;
	sbp = mp->m_sb;
	agp = xfs_buf_to_agp(agbuf);
	block = xfs_buf_to_ablock(buf);
	/*
	 * Point the new block to the old head of the list.
	 */
	*(xfs_agblock_t *)block = agp->xfsag_freelist;
	last = sizeof(xfs_agblock_t) - 1;
	xfs_trans_log_buf(tp, buf, 0, last);
	bno = xfs_daddr_to_agbno(sbp, buf->b_blkno);
	agp->xfsag_freelist = bno;
	agp->xfsag_flist_count++;
	first = offsetof(xfs_aghdr_t, xfsag_freelist);
	last = offsetof(xfs_aghdr_t, xfsag_flist_count) +
	       sizeof(agp->xfsag_flist_count) - 1;
	xfs_trans_log_buf(tp, agbuf, first, last);
}

#ifdef XFSADEBUG
void
xfs_alloc_rcheck(xfs_alloc_cur_t *cur)
{
	buf_t *agbuf;
	xfs_aghdr_t *agp;
	xfs_agblock_t bno;
	int levels;

	agbuf = cur->agbuf;
	agp = xfs_buf_to_agp(agbuf);
	bno = agp->xfsag_roots[cur->btnum];
	levels = agp->xfsag_levels[cur->btnum];
	xfs_alloc_rcheck_btree(cur, agp, bno, levels);
}

void
xfs_alloc_rcheck_btree(xfs_alloc_cur_t *cur, xfs_aghdr_t *agp, xfs_agblock_t rbno, int levels)
{
	xfs_agblock_t bno;
	xfs_agblock_t fbno;
	int level;
	xfs_alloc_rec_t rec;

	for (level = levels - 1, bno = rbno; level >= 0; level--, bno = fbno) {
		bno = xfs_alloc_rcheck_btree_block(cur, agp->xfsag_seqno, bno, &fbno, &rec, level);
		while (bno != NULLAGBLOCK) {
			ASSERT(bno < agp->xfsag_length);
			bno = xfs_alloc_rcheck_btree_block(cur, agp->xfsag_seqno, bno, (xfs_agblock_t *)0, &rec, level);
		}
	}
}

xfs_agblock_t
xfs_alloc_rcheck_btree_block(xfs_alloc_cur_t *cur, xfs_agnumber_t agno, xfs_agblock_t bno, xfs_agblock_t *fbno, xfs_alloc_rec_t *rec, int level)
{
	int bl;
	xfs_alloc_block_t *block;
	buf_t *buf;
	daddr_t d;
	int i;
	xfs_mount_t *mp;
	xfs_agblock_t *pp;
	xfs_agblock_t rbno;
	xfs_alloc_rec_t *rp;
	xfs_sb_t *sbp;
	xfs_trans_t *tp;

	tp = cur->tp;
	mp = tp->t_mountp;
	sbp = mp->m_sb;
	bl = sbp->xfsb_blocklog;
	buf = xfs_alloc_bread(tp, agno, bno);
	block = xfs_buf_to_ablock(buf);
	xfs_alloc_check_block(cur, block, level);
	if (fbno && block->numrecs) {
		if (level > 0)
			*fbno = *XFS_ALLOC_PTR_ADDR(block, 1, bl);
		else
			*fbno = NULLAGBLOCK;
	}
	rbno = block->rightsib;
	for (i = 1; i <= block->numrecs; i++) {
		rp = XFS_ALLOC_REC_ADDR(block, i, bl);
		if (i == 1 && !fbno)
			xfs_alloc_check_rec(cur->btnum, rec, rp);
		else if (i > 1) {
			xfs_alloc_check_rec(cur->btnum, rp - 1, rp);
			if (i == block->numrecs)
				*rec = *rp;
		}
		if (level > 0)
			xfs_alloc_check_ptr(cur, *XFS_ALLOC_PTR_ADDR(block, i, bl), level);
	}
	return rbno;
}
#endif

/*
 * Move 1 record right from cur/level if possible.
 * Update cur to reflect the new path.
 */
int
xfs_alloc_rshift(xfs_alloc_cur_t *cur, int level)
{
	int bl;
	int first;
	int i;
	int last;
	buf_t *lbuf;
	xfs_alloc_block_t *left;
	int lev;
	xfs_agblock_t *lpp;
	xfs_alloc_rec_t *lrp;
	xfs_mount_t *mp;
	int ptr;
	buf_t *rbuf;
	xfs_alloc_block_t *right;
	xfs_alloc_rec_t *rp;
	xfs_agblock_t *rpp;
	xfs_alloc_rec_t *rrp;
	xfs_sb_t *sbp;
	xfs_alloc_cur_t *tcur;
	xfs_trans_t *tp;

	xfs_alloc_rcheck(cur);
	lbuf = cur->bufs[level];
	left = xfs_buf_to_ablock(lbuf);
	xfs_alloc_check_block(cur, left, level);
	if (left->rightsib == NULLAGBLOCK)
		return 0;
	if (cur->ptrs[level] >= left->numrecs)
		return 0;
	tp = cur->tp;
	mp = tp->t_mountp;
	sbp = mp->m_sb;
	rbuf = xfs_alloc_bread(tp, cur->agno, left->rightsib);
	right = xfs_buf_to_ablock(rbuf);
	xfs_alloc_check_block(cur, right, level);
	bl = sbp->xfsb_blocklog;
	if (right->numrecs == XFS_ALLOC_BLOCK_MAXRECS(bl, level))
		return 0;
	lrp = XFS_ALLOC_REC_ADDR(left, left->numrecs, bl);
	rrp = XFS_ALLOC_REC_ADDR(right, 1, bl);
	if (level > 0) {
		lpp = XFS_ALLOC_PTR_ADDR(left, left->numrecs, bl);
		rpp = XFS_ALLOC_PTR_ADDR(right, 1, bl);
		for (i = right->numrecs - 1; i >= 0; i--) {
			rrp[i + 1] = rrp[i];
			xfs_alloc_check_ptr(cur, rpp[i], level);
			rpp[i + 1] = rpp[i];
		}
		xfs_alloc_check_ptr(cur, *lpp, level);
		*rpp = *lpp;
		first = (caddr_t)rpp - (caddr_t)right;
		last = (caddr_t)&rpp[right->numrecs] - (caddr_t)right +
			sizeof(*rpp) - 1;
		xfs_trans_log_buf(tp, rbuf, first, last);
	} else {
		for (i = right->numrecs - 1; i >= 0; i--)
			rrp[i + 1] = rrp[i];
	}
	*rrp = *lrp;
	first = (caddr_t)rrp - (caddr_t)right;
	last = (caddr_t)&rrp[right->numrecs] - (caddr_t)right +
		sizeof(*rrp) - 1;
	xfs_trans_log_buf(tp, lbuf, first, last);
	left->numrecs--;
	first = offsetof(xfs_alloc_block_t, numrecs);
	last = first + sizeof(left->numrecs) - 1;
	xfs_trans_log_buf(tp, lbuf, first, last);
	right->numrecs++;
	xfs_alloc_check_rec(cur->btnum, rrp, rrp + 1);
	xfs_trans_log_buf(tp, rbuf, first, last);
	tcur = xfs_alloc_dup_cursor(cur, __LINE__);
	xfs_alloc_lastrec(tcur, level);
	xfs_alloc_increment(tcur, level);
	xfs_alloc_updkey(tcur, rrp, level + 1);
	xfs_alloc_del_cursor(tcur);
	xfs_alloc_rcheck(cur);
	return 1;
}

/*
 * Split cur/level block in half.
 * Return new block number and its first record (to be inserted into parent).
 */
int
xfs_alloc_split(xfs_alloc_cur_t *cur, int level, xfs_agblock_t *bnop, xfs_alloc_rec_t *recp, xfs_alloc_cur_t **curp)
{
	buf_t *agbuf;
	xfs_aghdr_t *agp;
	int bl;
	daddr_t d;
	int first;
	int i;
	int last;
	xfs_agblock_t lbno;
	buf_t *lbuf;
	xfs_alloc_block_t *left;
	xfs_agblock_t *lpp;
	xfs_alloc_rec_t *lrp;
	xfs_mount_t *mp;
	xfs_agblock_t rbno;
	buf_t *rbuf;
	xfs_alloc_rec_t rec;
	xfs_alloc_block_t *right;
	xfs_agblock_t *rpp;
	xfs_alloc_block_t *rrblock;
	buf_t *rrbuf;
	xfs_alloc_rec_t *rrp;
	xfs_sb_t *sbp;
	xfs_trans_t *tp;

	xfs_alloc_rcheck(cur);
	tp = cur->tp;
	mp = tp->t_mountp;
	sbp = mp->m_sb;
	bl = sbp->xfsb_blocklog;
	agbuf = cur->agbuf;
	agp = xfs_buf_to_agp(agbuf);
	rbno = xfs_alloc_get_freelist(tp, agbuf, &rbuf);
	if (rbno == NULLAGBLOCK)
		return 0;
	right = xfs_buf_to_ablock(rbuf);
	lbuf = cur->bufs[level];
	left = xfs_buf_to_ablock(lbuf);
	xfs_alloc_check_block(cur, left, level);
	right->magic = xfs_magics[cur->btnum];
	right->level = left->level;
	right->numrecs = left->numrecs / 2;
	if ((left->numrecs & 1) && cur->ptrs[level] <= right->numrecs + 1)
		right->numrecs++;
	i = left->numrecs - right->numrecs + 1;
	lrp = XFS_ALLOC_REC_ADDR(left, i, bl);
	rrp = XFS_ALLOC_REC_ADDR(right, 1, bl);
	if (level > 0) {
		lpp = XFS_ALLOC_PTR_ADDR(left, i, bl);
		rpp = XFS_ALLOC_PTR_ADDR(right, 1, bl);
		for (i = 0; i < right->numrecs; i++) {
			rrp[i] = lrp[i];
			xfs_alloc_check_ptr(cur, lpp[i], level);
			rpp[i] = lpp[i];
		}
		first = (caddr_t)rpp - (caddr_t)right;
		last = ((caddr_t)&rpp[i] - (caddr_t)right) - 1;
		xfs_trans_log_buf(tp, rbuf, first, last);
	} else {
		for (i = 0; i < right->numrecs; i++)
			rrp[i] = lrp[i];
	}
	first = (caddr_t)rrp - (caddr_t)right;
	last = ((caddr_t)&rrp[i] - (caddr_t)right) - 1;
	xfs_trans_log_buf(tp, rbuf, first, last);
	rec = *rrp;
	d = lbuf->b_blkno;
	lbno = xfs_daddr_to_agbno(sbp, d);
	left->numrecs -= right->numrecs;
	right->rightsib = left->rightsib;
	left->rightsib = rbno;
	right->leftsib = lbno;
	first = 0;
	last = offsetof(xfs_alloc_block_t, rightsib) +
		sizeof(left->rightsib) - 1;
	xfs_trans_log_buf(tp, rbuf, first, last);
	first = offsetof(xfs_alloc_block_t, numrecs);
	xfs_trans_log_buf(tp, lbuf, first, last);
	if (right->rightsib != NULLAGBLOCK) {
		rrbuf = xfs_alloc_bread(tp, cur->agno, right->rightsib);
		rrblock = xfs_buf_to_ablock(rrbuf);
		xfs_alloc_check_block(cur, rrblock, level);
		rrblock->leftsib = rbno;
		first = offsetof(xfs_alloc_block_t, leftsib);
		last = first + sizeof(rrblock->leftsib) - 1;
		xfs_trans_log_buf(tp, rrbuf, first, last);
	}
	if (cur->ptrs[level] > left->numrecs + 1) {
		cur->bufs[level] = rbuf;
		cur->ptrs[level] -= left->numrecs;
	}
	if (level + 1 < cur->nlevels) {
		*curp = xfs_alloc_dup_cursor(cur, __LINE__);
		(*curp)->ptrs[level + 1]++;
	}
	*bnop = rbno;
	*recp = rec;
	xfs_alloc_rcheck(cur);
	return 1;
}

int
xfs_alloc_update(xfs_alloc_cur_t *cur, xfs_agblock_t bno, xfs_extlen_t len)
{
	buf_t *agbuf;
	xfs_aghdr_t *agp;
	int bl;
	xfs_alloc_block_t *block;
	buf_t *buf;
	int first;
	int last;
	xfs_mount_t *mp;
	int ptr;
	xfs_alloc_rec_t *rp;
	xfs_sb_t *sbp;
	xfs_trans_t *tp;

	agbuf = cur->agbuf;
	agp = xfs_buf_to_agp(agbuf);
	xfs_alloc_rcheck(cur);
	buf = cur->bufs[0];
	block = xfs_buf_to_ablock(buf);
	xfs_alloc_check_block(cur, block, 0);
	ptr = cur->ptrs[0];
	tp = cur->tp;
	mp = tp->t_mountp;
	sbp = mp->m_sb;
	bl = sbp->xfsb_blocklog;
	rp = XFS_ALLOC_REC_ADDR(block, ptr, bl);
	rp->startblock = bno;
	rp->blockcount = len;
	first = (caddr_t)rp - (caddr_t)block;
	last = first + sizeof(*rp) - 1;
	xfs_trans_log_buf(tp, buf, first, last);
	if (cur->btnum == XFS_BTNUM_CNT && block->rightsib == NULLAGBLOCK &&
	    ptr == block->numrecs) {
		agp->xfsag_longest = len;
		first = offsetof(xfs_aghdr_t, xfsag_longest);
		last = first + sizeof(agp->xfsag_longest) - 1;
		xfs_trans_log_buf(tp, agbuf, first, last);
	}
	if (ptr > 1)
		return 1;
	xfs_alloc_updkey(cur, rp, 1);
	xfs_alloc_rcheck(cur);
	xfs_alloc_kcheck(cur);
	return 1;
}

void
xfs_alloc_updkey(xfs_alloc_cur_t *cur, xfs_alloc_rec_t *kp, int level)
{
	int bl;
	xfs_alloc_block_t *block;
	buf_t *buf;
	int first;
	int last;
	xfs_mount_t *mp;
	int ptr;
	xfs_alloc_rec_t *rp;
	xfs_sb_t *sbp;
	xfs_trans_t *tp;

	xfs_alloc_rcheck(cur);
	tp = cur->tp;
	mp = tp->t_mountp;
	sbp = mp->m_sb;
	bl = sbp->xfsb_blocklog;
	for (ptr = 1; ptr == 1 && level < cur->nlevels; level++) {
		buf = cur->bufs[level];
		block = xfs_buf_to_ablock(buf);
		xfs_alloc_check_block(cur, block, level);
		ptr = cur->ptrs[level];
		rp = XFS_ALLOC_REC_ADDR(block, ptr, bl);
		*rp = *kp;
		first = (caddr_t)rp - (caddr_t)block;
		last = first + sizeof(*rp) - 1;
		xfs_trans_log_buf(tp, buf, first, last);
	}
	xfs_alloc_rcheck(cur);
}

/*
 * Allocation group level functions.
 */

xfs_agblock_t
xfs_alloc_ag_vextent_exact(xfs_trans_t *tp, buf_t *agbuf, xfs_agnumber_t agno, xfs_agblock_t bno, xfs_extlen_t minlen, xfs_extlen_t maxlen, xfs_extlen_t *len)
{
	xfs_alloc_cur_t *bno_cur;
	xfs_alloc_cur_t *cnt_cur;
	xfs_agblock_t end;
	xfs_agblock_t fbno;
	xfs_agblock_t fend;
	xfs_extlen_t flen;
	int i;
	xfs_agblock_t maxend;
	xfs_agblock_t minend;

	bno_cur = xfs_alloc_init_cursor(tp, agbuf, agno, XFS_BTNUM_BNO, __LINE__);
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
			cnt_cur = xfs_alloc_init_cursor(tp, agbuf, agno, XFS_BTNUM_CNT, __LINE__);
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
			xfs_alloc_del_cursor(cnt_cur);
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
	xfs_alloc_del_cursor(bno_cur);
	return fbno;
}

xfs_agblock_t
xfs_alloc_ag_vextent_size(xfs_trans_t *tp, buf_t *agbuf, xfs_agnumber_t agno, xfs_extlen_t minlen, xfs_extlen_t maxlen, xfs_extlen_t *len)
{
	xfs_alloc_cur_t *bno_cur;
	xfs_alloc_cur_t *cnt_cur;
	xfs_agblock_t fbno;
	xfs_extlen_t flen = 0;
	int i;
	xfs_extlen_t rlen;

	cnt_cur = xfs_alloc_init_cursor(tp, agbuf, agno, XFS_BTNUM_CNT, __LINE__);
	if (!xfs_alloc_lookup_ge(cnt_cur, 0, maxlen)) {
		if (xfs_alloc_decrement(cnt_cur, 0))
			xfs_alloc_get_rec(cnt_cur, &fbno, &flen);
		if (flen < minlen) {
			xfs_alloc_del_cursor(cnt_cur);
			return NULLAGBLOCK;
		}
		rlen = flen;
	} else {
		xfs_alloc_get_rec(cnt_cur, &fbno, &flen);
		rlen = maxlen;
	}
	xfs_alloc_delete(cnt_cur);
	bno_cur = xfs_alloc_init_cursor(tp, agbuf, agno, XFS_BTNUM_BNO, __LINE__);
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
	xfs_alloc_del_cursor(bno_cur);
	xfs_alloc_rcheck(cnt_cur);
	xfs_alloc_kcheck(cnt_cur);
	xfs_alloc_del_cursor(cnt_cur);
	return fbno;
}

xfs_agblock_t
xfs_alloc_ag_vextent_near(xfs_trans_t *tp, buf_t *agbuf, xfs_agnumber_t agno, xfs_agblock_t bno, xfs_extlen_t minlen, xfs_extlen_t maxlen, xfs_extlen_t *len)
{
	xfs_extlen_t bdiff;
	int besti;
	xfs_agblock_t bnew;
	xfs_alloc_cur_t *bno_cur_gt;
	xfs_alloc_cur_t *bno_cur_lt;
	xfs_alloc_cur_t *cnt_cur;
	xfs_extlen_t diff;
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
	xfs_extlen_t rlen;

	cnt_cur = xfs_alloc_init_cursor(tp, agbuf, agno, XFS_BTNUM_CNT, __LINE__);
	ltlen = 0;
	if (!xfs_alloc_lookup_ge(cnt_cur, 0, maxlen)) {
		if (xfs_alloc_decrement(cnt_cur, 0))
			xfs_alloc_get_rec(cnt_cur, &ltbno, &ltlen);
		if (ltlen < minlen) {
			xfs_alloc_del_cursor(cnt_cur);
			return NULLAGBLOCK;
		}
	}
	if (xfs_alloc_islastblock(cnt_cur, 0)) {
		i = cnt_cur->ptrs[0];
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
		cnt_cur->ptrs[0] = besti;
		xfs_alloc_get_rec(cnt_cur, &ltbno, &ltlen);
		ltend = ltbno + ltlen;
		rlen = ltlen < maxlen ? ltlen : maxlen;
		xfs_alloc_delete(cnt_cur);
		ltnew = bnew;
		ltnewend = bnew + rlen;
		*len = rlen;
		bno_cur_lt = xfs_alloc_init_cursor(tp, agbuf, agno, XFS_BTNUM_BNO, __LINE__);
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
		xfs_alloc_del_cursor(cnt_cur);
		xfs_alloc_rcheck(bno_cur_lt);
		xfs_alloc_kcheck(bno_cur_lt);
		xfs_alloc_del_cursor(bno_cur_lt);
		return ltnew;
	}
	bno_cur_lt = xfs_alloc_init_cursor(tp, agbuf, agno, XFS_BTNUM_BNO, __LINE__);
	if (!xfs_alloc_lookup_le(bno_cur_lt, bno, maxlen)) {
		bno_cur_gt = bno_cur_lt;
		bno_cur_lt = 0;
	} else
		bno_cur_gt = xfs_alloc_dup_cursor(bno_cur_lt, __LINE__);
	if (!xfs_alloc_increment(bno_cur_gt, 0)) {
		xfs_alloc_del_cursor(bno_cur_gt);
		bno_cur_gt = 0;
	}
	do {
		if (bno_cur_lt) {
			xfs_alloc_get_rec(bno_cur_lt, &ltbno, &ltlen);
			if (ltlen >= minlen)
				break;
			if (!xfs_alloc_decrement(bno_cur_lt, 0)) {
				xfs_alloc_del_cursor(bno_cur_lt);
				bno_cur_lt = 0;
			}
		}
		if (bno_cur_gt) {
			xfs_alloc_get_rec(bno_cur_gt, &gtbno, &gtlen);
			if (gtlen >= minlen)
				break;
			if (!xfs_alloc_increment(bno_cur_gt, 0)) {
				xfs_alloc_del_cursor(bno_cur_gt);
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
						xfs_alloc_del_cursor(bno_cur_gt);
						bno_cur_gt = 0;
					} else if (gtlen >= minlen) {
						rlen = gtlen < maxlen ? gtlen : maxlen;
						gtdiff = xfs_alloc_compute_diff(bno, rlen, gtbno, gtlen, &gtnew);
						if (gtdiff < ltdiff) {
							xfs_alloc_del_cursor(bno_cur_lt);
							bno_cur_lt = 0;
						}
					} else if (!xfs_alloc_increment(bno_cur_gt, 0)) {
						xfs_alloc_del_cursor(bno_cur_gt);
						bno_cur_gt = 0;
					}
				}
			} else {
				xfs_alloc_del_cursor(bno_cur_gt);
				bno_cur_gt = 0;
			}
		} else {
			rlen = gtlen < maxlen ? gtlen : maxlen;
			gtdiff = xfs_alloc_compute_diff(bno, rlen, gtbno, gtlen, &gtnew);
			if (gtdiff) {
				while (bno_cur_lt && bno_cur_gt) {
					xfs_alloc_get_rec(bno_cur_lt, &ltbno, &ltlen);
					if (ltbno <= bno - gtdiff) {
						xfs_alloc_del_cursor(bno_cur_lt);
						bno_cur_lt = 0;
					} else if (ltlen >= minlen) {
						rlen = ltlen < maxlen ? ltlen : maxlen;
						ltdiff = xfs_alloc_compute_diff(bno, rlen, ltbno, ltlen, &ltnew);
						if (ltdiff < gtdiff) {
							xfs_alloc_del_cursor(bno_cur_gt);
							bno_cur_gt = 0;
						}
					} else if (!xfs_alloc_decrement(bno_cur_lt, 0)) {
						xfs_alloc_del_cursor(bno_cur_lt);
						bno_cur_lt = 0;
					}
				}
			} else {
				xfs_alloc_del_cursor(bno_cur_lt);
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
		xfs_alloc_del_cursor(cnt_cur);
		xfs_alloc_rcheck(bno_cur_lt);
		xfs_alloc_kcheck(bno_cur_lt);
		xfs_alloc_del_cursor(bno_cur_lt);
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
		xfs_alloc_del_cursor(cnt_cur);
		xfs_alloc_rcheck(bno_cur_gt);
		xfs_alloc_kcheck(bno_cur_gt);
		xfs_alloc_del_cursor(bno_cur_gt);
		return gtnew;
	}
}

/*
 * Generic extent allocation in an allocation group, variable-size.
 */
xfs_agblock_t
xfs_alloc_ag_vextent(xfs_trans_t *tp, buf_t *agbuf, xfs_agnumber_t agno, xfs_agblock_t bno, xfs_extlen_t minlen, xfs_extlen_t maxlen, xfs_extlen_t *len, xfs_alloctype_t flag) 
{
	xfs_aghdr_t *agp;
	int first;
	int last;
	xfs_mount_t *mp;
	xfs_agblock_t r;
	xfs_sb_t *sbp;

	if (minlen == 0 || maxlen == 0 || minlen > maxlen)
		return NULLAGBLOCK;
	switch (flag) {
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
	}
	if (r != NULLAGBLOCK) {
		ASSERT(*len >= minlen && *len <= maxlen);
		agp = xfs_buf_to_agp(agbuf);
		agp->xfsag_freeblks -= *len;
		first = offsetof(xfs_aghdr_t, xfsag_freeblks);
		last = first + sizeof(agp->xfsag_freeblks) - 1;
		xfs_trans_log_buf(tp, agbuf, first, last);
		mp = tp->t_mountp;
		sbp = mp->m_sb;
		sbp->xfsb_fdblocks -= *len;
		first = offsetof(xfs_sb_t, xfsb_fdblocks);
		last = first + sizeof(sbp->xfsb_fdblocks) - 1;
		xfs_mod_sb(tp, first, last);
	}
	ASSERT(xfs_alloc_curallcount == xfs_alloc_curfreecount);
	return r;
}

/*
 * Fix up the btree freelist's size.
 */
buf_t *
xfs_alloc_fix_freelist(xfs_trans_t *tp, xfs_agnumber_t agno)
{
	xfs_agblock_t agbno;
	buf_t *agbuf;
	xfs_aghdr_t *agp;
	xfs_agblock_t bno, nbno;
	buf_t *buf;
	xfs_extlen_t i;
	xfs_mount_t *mp;
	xfs_extlen_t need;
	xfs_sb_t *sbp;

	mp = tp->t_mountp;
	sbp = mp->m_sb;
	agbuf = xfs_alloc_bread(tp, agno, XFS_AGH_BLOCK);
	agp = xfs_buf_to_agp(agbuf);
	need = XFS_MIN_FREELIST(agp);
	while (agp->xfsag_flist_count > need) {
		bno = xfs_alloc_get_freelist(tp, agbuf, &buf);
		xfs_free_ag_extent(tp, agbuf, agno, bno, 1);
	}
	while (agp->xfsag_flist_count < need) {
		i = need - agp->xfsag_flist_count;
		agbno = xfs_alloc_ag_vextent(tp, agbuf, agno, 0, 1, i, &i, XFS_ALLOCTYPE_THIS_AG);
		if (agbno == NULLAGBLOCK)
			break;
		for (bno = agbno + i - 1; bno >= agbno; bno--) {
			buf = xfs_alloc_bread(tp, agno, bno);
			xfs_alloc_put_freelist(tp, agbuf, buf);
		}
	}
	ASSERT(xfs_alloc_curallcount == xfs_alloc_curfreecount);
	return agbuf;
}

/*
 * Free an extent in an ag.
 */
int
xfs_free_ag_extent(xfs_trans_t *tp, buf_t *agbuf, xfs_agnumber_t agno, xfs_agblock_t bno, xfs_extlen_t len)
{
	xfs_aghdr_t *agp;
	xfs_alloc_cur_t *bno_cur;
	xfs_alloc_cur_t *cnt_cur;
	int first;
	xfs_extlen_t flen = len;
	xfs_agblock_t gtbno;
	xfs_extlen_t gtlen;
	int haveleft;
	int haveright;
	int i;
	int last;
	xfs_agblock_t ltbno;
	xfs_extlen_t ltlen;
	xfs_mount_t *mp;
	xfs_sb_t *sbp;

	bno_cur = xfs_alloc_init_cursor(tp, agbuf, agno, XFS_BTNUM_BNO, __LINE__);
	if (haveleft = xfs_alloc_lookup_le(bno_cur, bno, len)) {
		xfs_alloc_get_rec(bno_cur, &ltbno, &ltlen);
		if (ltbno + ltlen < bno)
			haveleft = 0;
		else if (ltbno + ltlen > bno) {
			xfs_alloc_del_cursor(bno_cur);
			return 0;
		}
	}
	if (haveright = xfs_alloc_increment(bno_cur, 0)) {
		xfs_alloc_get_rec(bno_cur, &gtbno, &gtlen);
		if (bno + len < gtbno)
			haveright = 0;
		else if (gtbno < bno + len) {
			xfs_alloc_del_cursor(bno_cur);
			return 0;
		}
	}
	cnt_cur = xfs_alloc_init_cursor(tp, agbuf, agno, XFS_BTNUM_CNT, __LINE__);
	if (haveleft && haveright) {
		i = xfs_alloc_lookup_eq(cnt_cur, ltbno, ltlen);
		ASSERT(i == 1);
		xfs_alloc_delete(cnt_cur);
		i = xfs_alloc_lookup_eq(cnt_cur, gtbno, gtlen);
		ASSERT(i == 1);
		xfs_alloc_delete(cnt_cur);
		xfs_alloc_delete(bno_cur);
		xfs_alloc_decrement(bno_cur, 0);
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
	xfs_alloc_del_cursor(bno_cur);
	xfs_alloc_lookup_eq(cnt_cur, bno, len);
	xfs_alloc_insert(cnt_cur);
	xfs_alloc_rcheck(cnt_cur);
	xfs_alloc_kcheck(cnt_cur);
	agp = xfs_buf_to_agp(agbuf);
	agp->xfsag_freeblks += flen;
	first = offsetof(xfs_aghdr_t, xfsag_freeblks);
	last = first + sizeof(agp->xfsag_freeblks) - 1;
	xfs_trans_log_buf(tp, agbuf, first, last);
	xfs_alloc_del_cursor(cnt_cur);
	mp = tp->t_mountp;
	sbp = mp->m_sb;
	sbp->xfsb_fdblocks += flen;
	first = offsetof(xfs_sb_t, xfsb_fdblocks);
	last = first + sizeof(sbp->xfsb_fdblocks) - 1;
	xfs_mod_sb(tp, first, last);
	ASSERT(xfs_alloc_curallcount == xfs_alloc_curfreecount);
	return 1;
}

/* 
 * File system level functions.
 */

/*
 * Allocate an extent (fixed-size).
 */
xfs_fsblock_t
xfs_alloc_extent(xfs_trans_t *tp, xfs_fsblock_t bno, xfs_extlen_t len, xfs_alloctype_t flag)
{
	xfs_extlen_t rlen;
	xfs_fsblock_t rval;

	rval = xfs_alloc_vextent(tp, bno, len, len, &rlen, flag);
	if (rval != NULLFSBLOCK)
		ASSERT(rlen == len);
	return rval;
}

/*
 * Allocate an extent (variable-size).
 */
xfs_fsblock_t
xfs_alloc_vextent(xfs_trans_t *tp, xfs_fsblock_t bno, xfs_extlen_t minlen, xfs_extlen_t maxlen, xfs_extlen_t *len, xfs_alloctype_t flag)
{
	xfs_agblock_t agbno;
	buf_t *agbuf;
	xfs_agnumber_t agno;
	xfs_aghdr_t *agp;
	xfs_agblock_t agsize;
	xfs_mount_t *mp;
	xfs_sb_t *sbp;
	xfs_agnumber_t tagno;

	mp = tp->t_mountp;
	sbp = mp->m_sb;
	agsize = sbp->xfsb_agblocks;
	if (bno >= sbp->xfsb_dblocks || minlen > maxlen ||
	    minlen > agsize || len == 0)
		return NULLFSBLOCK;
	if (maxlen > agsize)
		maxlen = agsize;
	agbno = NULLAGBLOCK;
	switch (flag) {
	case XFS_ALLOCTYPE_ANY_AG:
	case XFS_ALLOCTYPE_START_AG:
		agno = flag == XFS_ALLOCTYPE_ANY_AG ?
			mp->m_agrotor : xfs_fsb_to_agno(sbp, bno);
		tagno = agno;
		do {
			agbuf = xfs_alloc_fix_freelist(tp, tagno);
			agp = xfs_buf_to_agp(agbuf);
			if (agp->xfsag_longest >= minlen) {
				agbno = xfs_alloc_ag_vextent(tp, agbuf, tagno, 0, minlen, maxlen, len, XFS_ALLOCTYPE_THIS_AG); 
				ASSERT(agbno != NULLAGBLOCK);
				break;
			} else if (++tagno == sbp->xfsb_agcount)
				tagno = 0;
		} while (tagno != agno);
		agno = tagno;
		mp->m_agrotor = (agno + 1) % sbp->xfsb_agcount;
		break;
	case XFS_ALLOCTYPE_THIS_AG:
	case XFS_ALLOCTYPE_NEAR_BNO:
	case XFS_ALLOCTYPE_THIS_BNO:
		agno = xfs_fsb_to_agno(sbp, bno);
		agbuf = xfs_alloc_fix_freelist(tp, agno);
		agp = xfs_buf_to_agp(agbuf);
		if (agp->xfsag_longest >= minlen) {
			agbno = xfs_fsb_to_agbno(sbp, bno);
			agbno = xfs_alloc_ag_vextent(tp, agbuf, agno, agbno, minlen, maxlen, len, flag); 
		}
		break;
	default:
		ASSERT(0);
	}
	if (agbno == NULLAGBLOCK)
		return NULLFSBLOCK;
	else
		return xfs_agb_to_fsb(sbp, agno, agbno);
}

/*
 * Find the next freelist block number.
 */
xfs_agblock_t
xfs_alloc_next_free(xfs_trans_t *tp, buf_t *agbuf, xfs_agblock_t bno)
{
	xfs_aghdr_t *agp;
	buf_t *buf;
	xfs_mount_t *mp;
	xfs_sb_t *sbp;

	agp = xfs_buf_to_agp(agbuf);
	mp = tp->t_mountp;
	sbp = mp->m_sb;
	buf = xfs_alloc_bread(tp, agp->xfsag_seqno, bno);
	bno = *(xfs_agblock_t *)buf->b_un.b_addr;
	return bno;
}

/*
 * Free an extent.
 */
int
xfs_free_extent(xfs_trans_t *tp, xfs_fsblock_t bno, xfs_extlen_t len)
{
	xfs_agblock_t agbno;
	buf_t *agbuf;
	xfs_agnumber_t agno;
	int i;
	xfs_mount_t *mp;
	xfs_sb_t *sbp;

	mp = tp->t_mountp;
	sbp = mp->m_sb;
	if (bno >= sbp->xfsb_dblocks || len == 0)
		return 0;
	agno = xfs_fsb_to_agno(sbp, bno);
	agbno = xfs_fsb_to_agbno(sbp, bno);
	agbuf = xfs_alloc_fix_freelist(tp, agno);
	i = xfs_free_ag_extent(tp, agbuf, agno, agbno, len);
	return i;
}
