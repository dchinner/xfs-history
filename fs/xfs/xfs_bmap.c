#ident	"$Revision$"

#include <sys/param.h>
#include <sys/buf.h>
#include <sys/vnode.h>
#include "xfs_types.h"
#include "xfs_inum.h"
#include "xfs.h"
#include "xfs_trans.h"
#include "xfs_sb.h"
#include "xfs_ag.h"
#include "xfs_mount.h"
#include "xfs_btree.h"
#include "xfs_alloc.h"
#include "xfs_bmap.h"
#include "xfs_dinode.h"
#include "xfs_inode.h"

#ifdef SIM
#include "sim.h"
#include <stddef.h>
#include <bstring.h>
#endif

xfs_bmap_cur_t *xfs_bmap_curfreelist;
#ifdef XFSBMDEBUG
int xfs_bmap_curfreecount;
xfs_bmap_cur_t *xfs_bmap_curalllist;
int xfs_bmap_curallcount;
#endif

/*
 * Prototypes for internal functions.
 */

#ifndef XFSBMDEBUG
#define NDEBUG
#endif
#include <assert.h>
#define	ASSERT(x)	assert(x)

#ifdef XFSBMDEBUG
void xfs_bmap_check_block(xfs_bmap_cur_t *, xfs_btree_block_t *, int);
void xfs_bmap_check_ptr(xfs_bmap_cur_t *, xfs_agblock_t, int);
void xfs_bmap_check_rec(xfs_bmap_rec_t *, xfs_bmap_rec_t *);
void xfs_bmap_kcheck(xfs_bmap_cur_t *);
void xfs_bmap_kcheck_body(xfs_bmap_cur_t *, xfs_aghdr_t *, xfs_btree_block_t *, int, xfs_bmap_rec_t *);
void xfs_bmap_kcheck_btree(xfs_bmap_cur_t *, xfs_aghdr_t *, xfs_agblock_t, int, xfs_bmap_rec_t *);
void xfs_bmap_rcheck(xfs_bmap_cur_t *);
xfs_agblock_t xfs_bmap_rcheck_body(xfs_bmap_cur_t *, xfs_btree_block_t *, xfs_agblock_t *, xfs_bmap_rec_t *, int);
xfs_agblock_t xfs_bmap_rcheck_btree(xfs_bmap_cur_t *, xfs_agblock_t, xfs_agblock_t *, xfs_bmap_rec_t *, int);
#else
#define	xfs_bmap_check_block(a,b,c)
#define	xfs_bmap_check_ptr(a,b,c)
#define	xfs_bmap_check_rec(a,b)
#define	xfs_bmap_kcheck(a)
#define	xfs_bmap_rcheck(a)
#endif

buf_t *xfs_bmap_bread(xfs_trans_t *, xfs_agnumber_t, xfs_agblock_t);
int xfs_bmap_decrement(xfs_bmap_cur_t *, int);
void xfs_bmap_del_cursor(xfs_bmap_cur_t *);
int xfs_bmap_delete(xfs_bmap_cur_t *);
int xfs_bmap_delrec(xfs_bmap_cur_t *, int);
xfs_bmap_cur_t *xfs_bmap_dup_cursor(xfs_bmap_cur_t *, int);
int xfs_bmap_firstrec(xfs_bmap_cur_t *, int);
int xfs_bmap_get_rec(xfs_bmap_cur_t *, xfs_fsblock_t *, xfs_fsblock_t *, xfs_extlen_t *);
int xfs_bmap_increment(xfs_bmap_cur_t *, int);
xfs_bmap_cur_t *xfs_bmap_init_cursor(xfs_trans_t *, buf_t *, xfs_agnumber_t, xfs_inode_t *, int);
int xfs_bmap_insert(xfs_bmap_cur_t *);
int xfs_bmap_insrec(xfs_bmap_cur_t *, int, xfs_agblock_t *, xfs_bmap_rec_t *, xfs_bmap_cur_t **);
int xfs_bmap_islastblock(xfs_bmap_cur_t *, int);
int xfs_bmap_lastrec(xfs_bmap_cur_t *, int);
int xfs_bmap_lookup(xfs_bmap_cur_t *, xfs_lookup_t);
int xfs_bmap_lookup_eq(xfs_bmap_cur_t *, xfs_fsblock_t, xfs_fsblock_t, xfs_extlen_t);
int xfs_bmap_lookup_ge(xfs_bmap_cur_t *, xfs_fsblock_t, xfs_fsblock_t, xfs_extlen_t);
int xfs_bmap_lookup_le(xfs_bmap_cur_t *, xfs_fsblock_t, xfs_fsblock_t, xfs_extlen_t);
int xfs_bmap_lshift(xfs_bmap_cur_t *, int);
int xfs_bmap_rshift(xfs_bmap_cur_t *, int);
int xfs_bmap_split(xfs_bmap_cur_t *, int, xfs_agblock_t *, xfs_bmap_rec_t *, xfs_bmap_cur_t **);
int xfs_bmap_update(xfs_bmap_cur_t *, xfs_fsblock_t, xfs_fsblock_t, xfs_extlen_t);
void xfs_bmap_updkey(xfs_bmap_cur_t *, xfs_bmap_rec_t *, int);

/*
 * Internal functions.
 */

buf_t *
xfs_bmap_bread(xfs_trans_t *tp, xfs_agnumber_t agno, xfs_agblock_t agbno)
{
	daddr_t d;
	xfs_mount_t *mp;
	xfs_sb_t *sbp;

	mp = tp->t_mountp;
	sbp = mp->m_sb;
	d = xfs_agb_to_daddr(sbp, agno, agbno);
	return xfs_trans_bread(tp, mp->m_dev, d, mp->m_bsize);
}

#ifdef XFSBMDEBUG
void
xfs_bmap_check_block(xfs_bmap_cur_t *cur, xfs_btree_block_t *block, int level)
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
	ASSERT(block->magic == XFS_BMAP_MAGIC);
	ASSERT(block->level == level);
	ASSERT(block->numrecs <= XFS_BMAP_BLOCK_MAXRECS(bl, level, cur));
	if (level == cur->nlevels - 1) {
		ASSERT(block->leftsib == NULLAGBLOCK);
		ASSERT(block->rightsib == NULLAGBLOCK);
	} else {
		ASSERT(block->leftsib == NULLAGBLOCK || 
		       block->leftsib < agp->xfsag_length);
		ASSERT(block->rightsib == NULLAGBLOCK ||
		       block->rightsib < agp->xfsag_length);
	}
}

void
xfs_bmap_check_ptr(xfs_bmap_cur_t *cur, xfs_agblock_t ptr, int level)
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
xfs_bmap_check_rec(xfs_bmap_rec_t *r1, xfs_bmap_rec_t *r2)
{
	ASSERT(r1->startoff + r1->blockcount <= r2->startoff);
}
#endif

/*
 * Decrement cursor by one record at the level.
 * For nonzero levels the leaf-ward information is untouched.
 */
int
xfs_bmap_decrement(xfs_bmap_cur_t *cur, int level)
{
	xfs_agblock_t agbno;
	int bl;
	xfs_btree_block_t *block;
	buf_t *buf;
	int lev;
	xfs_mount_t *mp;
	int ptr;
	xfs_sb_t *sbp;
	xfs_trans_t *tp;

	if (--cur->ptrs[level] > 0)
		return 1;
	block = xfs_bmap_get_block(cur, level);
	xfs_bmap_check_block(cur, block, level);
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
		block = xfs_bmap_get_block(cur, lev);
		xfs_bmap_check_block(cur, block, lev);
		agbno = *XFS_BMAP_PTR_ADDR(block, cur->ptrs[lev], bl, cur);
		buf = cur->bufs[lev - 1] = xfs_bmap_bread(tp, cur->agno, agbno);
		block = xfs_buf_to_block(buf);
		xfs_bmap_check_block(cur, block, lev - 1);
		cur->ptrs[lev - 1] = block->numrecs;
	}
	return 1;
}

/*
 * Delete the cursor, unreferencing its buffers.
 */
void
xfs_bmap_del_cursor(xfs_bmap_cur_t *cur)
{
	cur->tp = (xfs_trans_t *)xfs_bmap_curfreelist;
	xfs_bmap_curfreelist = cur;
#ifdef XFSBMDEBUG
	xfs_bmap_curfreecount++;
#endif
}

/*
 * Delete the record pointed to by cur.
 */
int
xfs_bmap_delete(xfs_bmap_cur_t *cur)
{
	int i;
	int level;

	for (level = 0, i = 2; i == 2; level++)
		i = xfs_bmap_delrec(cur, level);
	xfs_bmap_kcheck(cur);
	return i;
}

/*
 * Delete record pointed to by cur/level.
 */
int
xfs_bmap_delrec(xfs_bmap_cur_t *cur, int level)
{
	buf_t *agbuf;
	xfs_aghdr_t *agp;
	int bl;
	xfs_btree_block_t *block;
	xfs_agblock_t bno;
	buf_t *buf;
	xfs_btree_block_t *cblock;
	xfs_agblock_t *cpp;
	xfs_bmap_rec_t *crp;
	int first;
	int i;
	int last;
	xfs_agblock_t lbno;
	buf_t *lbuf;
	xfs_btree_block_t *left;
	xfs_agblock_t *lpp;
	int lrecs;
	xfs_bmap_rec_t *lrp;
	xfs_mount_t *mp;
	xfs_agblock_t *pp;
	int ptr;
	xfs_agblock_t rbno;
	buf_t *rbuf;
	xfs_btree_block_t *right;
	xfs_bmap_rec_t *rp;
	xfs_agblock_t *rpp;
	xfs_btree_block_t *rrblock;
	buf_t *rrbuf;
	int rrecs;
	xfs_bmap_rec_t *rrp;
	xfs_sb_t *sbp;
	xfs_bmap_cur_t *tcur;
	xfs_trans_t *tp;

	xfs_bmap_rcheck(cur);
	tp = cur->tp;
	mp = tp->t_mountp;
	sbp = mp->m_sb;
	bl = sbp->xfsb_blocklog;
	ptr = cur->ptrs[level];
	if (ptr == 0)
		return 0;
	block = xfs_bmap_get_block(cur, level);
	xfs_bmap_check_block(cur, block, level);
	if (ptr > block->numrecs)
		return 0;
	rp = XFS_BMAP_REC_ADDR(block, 1, bl, cur);
	if (level > 0) {
		pp = XFS_BMAP_PTR_ADDR(block, 1, bl, cur);
		for (i = ptr; i < block->numrecs; i++) {
			rp[i - 1] = rp[i];
			xfs_bmap_check_ptr(cur, pp[i], level);
			pp[i - 1] = pp[i];
		}
		if (ptr < i) {
			if (level < cur->nlevels - 1) {
				first = (caddr_t)&pp[ptr - 1] - (caddr_t)block;
				last = ((caddr_t)&pp[i - 1] - 1) - (caddr_t)block;
				xfs_trans_log_buf(tp, cur->bufs[level], first, last);
			} else {
				/* log inode fields */
				/* FIXME */
			}
		}
	} else {
		for (i = ptr; i < block->numrecs; i++)
			rp[i - 1] = rp[i];
	}
	if (ptr < i) {
		if (level < cur->nlevels - 1) {
			first = (caddr_t)&rp[ptr - 1] - (caddr_t)block;
			last = ((caddr_t)&rp[i - 1] - 1) - (caddr_t)block;
			xfs_trans_log_buf(tp, cur->bufs[level], first, last);
		} else {
			/* log inode fields */
			/* FIXME */
		}
	}
	block->numrecs--;
	if (level < cur->nlevels - 1) {
		first = offsetof(xfs_btree_block_t, numrecs);
		last = first + sizeof(block->numrecs) - 1;
		xfs_trans_log_buf(tp, cur->bufs[level], first, last);
	} else {
		/* log inode fields */
		/* FIXME */
	}
	agbuf = cur->agbuf;
	agp = xfs_buf_to_agp(agbuf);
	if (level == cur->nlevels - 1) {
		/* Only do this if the next level will fit. */
		/* Then the data must be copied up to the inode,
		 * instead of freeing the root you free the next level */
		buf = cur->bufs[level - 1];
		cblock = xfs_buf_to_block(buf);
		if (block->numrecs == 1 && level > 0 &&
		    cblock->numrecs <= XFS_BMAP_BLOCK_MAXRECS(bl, level, cur)) {
			ASSERT(cblock->leftsib == cblock->rightsib == NULLAGBLOCK);
			*block = *cblock;
			rp = XFS_BMAP_REC_ADDR(block, 1, bl, cur);
			crp = XFS_BMAP_REC_ADDR(cblock, 1, bl, cur);
			bcopy((caddr_t)crp, (caddr_t)rp, block->numrecs * sizeof(*rp));
			pp = XFS_BMAP_PTR_ADDR(block, 1, bl, cur);
			cpp = XFS_BMAP_PTR_ADDR(cblock, 1, bl, cur);
			bcopy((caddr_t)cpp, (caddr_t)pp, block->numrecs * sizeof(*pp));
			xfs_free_extent(tp, xfs_daddr_to_fsb(sbp, buf->b_blkno), 1);
			/* FIXME: block's inode logging */
			cur->bufs[level - 1] = (buf_t *)0;
			cur->nlevels--;
		}
		xfs_bmap_rcheck(cur);
		return 1;
	}
	if (ptr == 1)
		xfs_bmap_updkey(cur, rp, level + 1);
	xfs_bmap_rcheck(cur);
	if (block->numrecs >= XFS_BMAP_BLOCK_MINRECS(bl, level, cur))
		return 1;
	rbno = block->rightsib;
	lbno = block->leftsib;
	ASSERT(rbno != NULLAGBLOCK || lbno != NULLAGBLOCK);
	tcur = xfs_bmap_dup_cursor(cur, __LINE__);
	bno = NULLAGBLOCK;
	buf = cur->bufs[level];
	if (rbno != NULLAGBLOCK) {
		xfs_bmap_lastrec(tcur, level);
		xfs_bmap_increment(tcur, level);
		xfs_bmap_lastrec(tcur, level);
		rbuf = tcur->bufs[level];
		right = xfs_buf_to_block(rbuf);
		xfs_bmap_check_block(cur, right, level);
		bno = right->leftsib;
		if (right->numrecs - 1 >= XFS_BMAP_BLOCK_MINRECS(bl, level, cur)) {
			if (xfs_bmap_lshift(tcur, level)) {
				ASSERT(block->numrecs >= XFS_BMAP_BLOCK_MINRECS(bl, level, tcur));
				xfs_bmap_del_cursor(tcur);
				return 1;
			}
		}
		rrecs = right->numrecs;
		if (lbno != NULLAGBLOCK) {
			xfs_bmap_firstrec(tcur, level);
			xfs_bmap_decrement(tcur, level);
		}
	}
	if (lbno != NULLAGBLOCK) {
		xfs_bmap_firstrec(tcur, level);
		xfs_bmap_decrement(tcur, level);	/* to last */
		xfs_bmap_firstrec(tcur, level);
		lbuf = tcur->bufs[level];
		left = xfs_buf_to_block(lbuf);
		xfs_bmap_check_block(cur, left, level);
		bno = left->rightsib;
		if (left->numrecs - 1 >= XFS_BMAP_BLOCK_MINRECS(bl, level, cur)) {
			if (xfs_bmap_rshift(tcur, level)) {
				ASSERT(block->numrecs >= XFS_BMAP_BLOCK_MINRECS(bl, level, tcur));
				xfs_bmap_del_cursor(tcur);
				cur->ptrs[level]++;
				return 1;
			}
		}
		lrecs = left->numrecs;
	}
	xfs_bmap_del_cursor(tcur);
	ASSERT(bno != NULLAGBLOCK);
	if (lbno != NULLAGBLOCK &&
	    lrecs + block->numrecs <= XFS_BMAP_BLOCK_MAXRECS(bl, level, cur)) {
		rbno = bno;
		right = block;
		rbuf = buf;
		lbuf = xfs_bmap_bread(tp, cur->agno, lbno);
		left = xfs_buf_to_block(lbuf);
		xfs_bmap_check_block(cur, left, level);
	} else if (rbno != NULLAGBLOCK &&
		   rrecs + block->numrecs <= XFS_BMAP_BLOCK_MAXRECS(bl, level, cur)) {
		lbno = bno;
		left = block;
		lbuf = buf;
		rbuf = xfs_bmap_bread(tp, cur->agno, rbno);
		right = xfs_buf_to_block(rbuf);
		xfs_bmap_check_block(cur, right, level);
	} else {
		xfs_bmap_rcheck(cur);
		return 1;
	}
	lrp = XFS_BMAP_REC_ADDR(left, left->numrecs + 1, bl, cur);
	rrp = XFS_BMAP_REC_ADDR(right, 1, bl, cur);
	if (level > 0) {
		lpp = XFS_BMAP_PTR_ADDR(left, left->numrecs + 1, bl, cur);
		rpp = XFS_BMAP_PTR_ADDR(right, 1, bl, cur);
		for (i = 0; i < right->numrecs; i++) {
			lrp[i] = rrp[i];
			xfs_bmap_check_ptr(cur, rpp[i], level);
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
		xfs_bmap_increment(cur, level + 1);
	left->numrecs += right->numrecs;
	left->rightsib = right->rightsib;
	if (left->rightsib != NULLAGBLOCK) {
		rrbuf = xfs_bmap_bread(tp, cur->agno, left->rightsib);
		rrblock = xfs_buf_to_block(rrbuf);
		xfs_bmap_check_block(cur, rrblock, level);
		rrblock->leftsib = lbno;
		first = offsetof(xfs_btree_block_t, leftsib);
		last = first + sizeof(rrblock->leftsib) - 1;
		xfs_trans_log_buf(tp, rrbuf, first, last);
	}
	xfs_free_extent(tp, xfs_daddr_to_fsb(sbp, rbuf->b_blkno), 1);
	xfs_bmap_rcheck(cur);
	return 2;
}

/*
 * Duplicate the cursor.
 * Add references to its buffers.
 */
xfs_bmap_cur_t *
xfs_bmap_dup_cursor(xfs_bmap_cur_t *cur, int lineno)
{
	buf_t *agbuf;
	xfs_aghdr_t *agp;
	buf_t *buf;
	int i;
	xfs_mount_t *mp;
	xfs_bmap_cur_t *newcur;
	xfs_trans_t *tp;

	agbuf = cur->agbuf;
	agp = xfs_buf_to_agp(agbuf);
	tp = cur->tp;
	mp = tp->t_mountp;
	newcur = xfs_bmap_init_cursor(tp, agbuf, agp->xfsag_seqno, cur->ip, lineno);
	newcur->rec = cur->rec;
	for (i = 0; i < newcur->nlevels; i++) {
		newcur->ptrs[i] = cur->ptrs[i];
		if (i == newcur->nlevels - 1)
			break;
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
xfs_bmap_firstrec(xfs_bmap_cur_t *cur, int level)
{
	xfs_btree_block_t *block;

	block = xfs_bmap_get_block(cur, level);
	xfs_bmap_check_block(cur, block, level);
	if (!block->numrecs)
		return 0;
	cur->ptrs[level] = 1;
	return 1;
}

/* 
 * Get the data from the pointed-to record.
 */
int
xfs_bmap_get_rec(xfs_bmap_cur_t *cur, xfs_fsblock_t *off, xfs_fsblock_t *bno, xfs_extlen_t *len)
{
	int bl;
	xfs_btree_block_t *block;
	buf_t *buf;
	xfs_mount_t *mp;
	int ptr;
	xfs_bmap_rec_t *rp;
	xfs_sb_t *sbp;
	xfs_trans_t *tp;

	block = xfs_bmap_get_block(cur, 0);
	ptr = cur->ptrs[0];
	xfs_bmap_check_block(cur, block, 0);
	if (ptr > block->numrecs || ptr <= 0)
		return 0;
	tp = cur->tp;
	mp = tp->t_mountp;
	sbp = mp->m_sb;
	bl = sbp->xfsb_blocklog;
	rp = XFS_BMAP_REC_ADDR(block, ptr, bl, cur);
	*off = rp->startoff;
	*bno = rp->startblock;
	*len = rp->blockcount;
	return 1;
}

/*
 * Increment cursor by one record at the level.
 * For nonzero levels the leaf-ward information is untouched.
 */
int
xfs_bmap_increment(xfs_bmap_cur_t *cur, int level)
{
	xfs_agblock_t agbno;
	int bl;
	xfs_btree_block_t *block;
	buf_t *buf;
	daddr_t d;
	int lev;
	xfs_mount_t *mp;
	int ptr;
	xfs_sb_t *sbp;
	xfs_trans_t *tp;

	block = xfs_bmap_get_block(cur, level);
	xfs_bmap_check_block(cur, block, level);
	if (++cur->ptrs[level] <= block->numrecs)
		return 1;
	if (block->rightsib == NULLAGBLOCK)
		return 0;
	for (lev = level + 1; lev < cur->nlevels; lev++) {
		block = xfs_bmap_get_block(cur, lev);
		xfs_bmap_check_block(cur, block, lev);
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
		block = xfs_bmap_get_block(cur, lev);
		xfs_bmap_check_block(cur, block, lev);
		agbno = *XFS_BMAP_PTR_ADDR(block, cur->ptrs[lev], bl, cur);
		cur->bufs[lev - 1] = xfs_bmap_bread(tp, cur->agno, agbno);
		cur->ptrs[lev - 1] = 1;
	}
	return 1;
}

/*
 * Allocate and initialize a new cursor.
 */
xfs_bmap_cur_t *
xfs_bmap_init_cursor(xfs_trans_t *tp, buf_t *agbuf, xfs_agnumber_t agno, xfs_inode_t *ip, int lineno)
{
	xfs_aghdr_t *agp;
	xfs_bmap_cur_t *cur;
	xfs_mount_t *mp;
	xfs_sb_t *sbp;

	if (xfs_bmap_curfreelist) {
		cur = xfs_bmap_curfreelist;
		xfs_bmap_curfreelist = (xfs_bmap_cur_t *)cur->tp;
#ifdef XFSBMDEBUG
		xfs_bmap_curfreecount--;
#endif
		if (tp != cur->tp)
			bzero(cur->bufs, sizeof(cur->bufs));
	} else {
		cur = (xfs_bmap_cur_t *)kmem_zalloc(sizeof(*cur), 0);
#ifdef XFSBMDEBUG
		cur->next = xfs_bmap_curalllist;
		xfs_bmap_curalllist = cur;
		xfs_bmap_curallcount++;
#endif
	}
	cur->tp = tp;
	cur->agbuf = agbuf;
	cur->agno = agno;
#ifdef XFSBMDEBUG
	cur->lineno = lineno;
#endif
	cur->ip = ip;
	mp = tp->t_mountp;
	sbp = mp->m_sb;
	cur->inodesize = sbp->xfsb_inodesize;
	cur->nlevels = ip->i_d.di_u.di_bmbt.level + 1;
	cur->btnum = XFS_BTNUM_BMAP;
	return cur;
}

/*
 * Insert the current record at the point referenced by cur.
 */
int
xfs_bmap_insert(xfs_bmap_cur_t *cur)
{
	int i;
	int level;
	xfs_agblock_t nbno;
	xfs_bmap_cur_t *ncur;
	xfs_bmap_rec_t nrec;
	xfs_bmap_cur_t *pcur;

	level = 0;
	nbno = NULLAGBLOCK;
	nrec = cur->rec;
	ncur = (xfs_bmap_cur_t *)0;
	pcur = cur;
	do {
		i = xfs_bmap_insrec(pcur, level++, &nbno, &nrec, &ncur);
		if (pcur != cur && (ncur || nbno == NULLAGBLOCK)) {
			cur->nlevels = pcur->nlevels;
			xfs_bmap_del_cursor(pcur);
		}
		if (ncur) {
			pcur = ncur;
			ncur = (xfs_bmap_cur_t *)0;
		}
	} while (nbno != NULLAGBLOCK);
	return i;
}

/*
 * Insert one record/level.  Return information to the caller
 * allowing the next level up to proceed if necessary.
 */
int
xfs_bmap_insrec(xfs_bmap_cur_t *cur, int level, xfs_agblock_t *bnop, xfs_bmap_rec_t *recp, xfs_bmap_cur_t **curp)
{
	int bl;
	xfs_btree_block_t *block;
	buf_t *buf;
	xfs_btree_block_t *cblock;
	xfs_agblock_t cbno;
	xfs_fsblock_t cfsbno;
	xfs_agblock_t *cpp;
	xfs_bmap_rec_t *crp;
	int first;
	int i;
	int last;
	xfs_mount_t *mp;
	xfs_agblock_t nbno;
	xfs_bmap_cur_t *ncur = (xfs_bmap_cur_t *)0;
	xfs_bmap_rec_t nrec;
	int optr;
	xfs_agblock_t *pp;
	int ptr;
	xfs_bmap_rec_t *rbase;
	xfs_bmap_rec_t *rp;
	xfs_sb_t *sbp;
	xfs_trans_t *tp;
	xfs_bmap_rec_t xxrec;

	ASSERT(level < cur->nlevels);
	xfs_bmap_rcheck(cur);
	optr = ptr = cur->ptrs[level];
	if (ptr == 0)
		return 0;
	block = xfs_bmap_get_block(cur, level);
	xfs_bmap_check_block(cur, block, level);
	if (ptr <= block->numrecs) {
		rp = XFS_BMAP_REC_ADDR(block, ptr, bl, cur);
		xfs_bmap_check_rec(recp, rp);
	}
	tp = cur->tp;
	mp = tp->t_mountp;
	sbp = mp->m_sb;
	bl = sbp->xfsb_blocklog;
	nbno = NULLAGBLOCK;
	if (block->numrecs == XFS_BMAP_BLOCK_MAXRECS(bl, level, cur)) {
		if (level == cur->nlevels - 1) {
			/*
			 * Copy the root into a real block.
			 */
			cfsbno = xfs_agb_to_fsb(sbp, cur->agno, cur->ip->i_bno);
			cfsbno = xfs_alloc_extent(tp, cfsbno, 1, XFS_ALLOCTYPE_NEAR_BNO);
			if (cfsbno == NULLFSBLOCK)
				return 0;
			cbno = xfs_fsb_to_agbno(sbp, cfsbno);
			buf = xfs_bmap_bread(tp, cur->agno, cbno);
			cblock = xfs_buf_to_block(buf);
			*cblock = *block;
			rp = XFS_BMAP_REC_ADDR(block, 1, bl, cur);
			crp = XFS_BMAP_REC_ADDR(cblock, 1, bl, cur);
			bcopy(rp, crp, block->numrecs * sizeof(*rp));
			last = (caddr_t)&crp[block->numrecs] - (caddr_t)cblock;
			xfs_trans_log_buf(tp, buf, 0, last);
			pp = XFS_BMAP_PTR_ADDR(block, 1, bl, cur);
			cpp = XFS_BMAP_PTR_ADDR(cblock, 1, bl, cur);
			bcopy(pp, cpp, block->numrecs * sizeof(*pp));
			first = (caddr_t)pp - (caddr_t)cblock;
			last = (caddr_t)&cpp[block->numrecs] - (caddr_t)cblock;
			xfs_trans_log_buf(tp, buf, first, last);
			block->level++;
			block->numrecs = 1;
			*pp = cbno;
			/* FIXME: log these */
			cur->bufs[level] = buf;
			cur->nlevels++;
			cur->ptrs[level + 1] = 1;
			block = cblock;
		} else {
			if (xfs_bmap_rshift(cur, level)) {
				/* nothing */
			} else if (xfs_bmap_lshift(cur, level)) {
				optr = ptr = cur->ptrs[level];
			} else if (xfs_bmap_split(cur, level, &nbno, &nrec, &ncur)) {
				block = xfs_bmap_get_block(cur, level);
				xfs_bmap_check_block(cur, block, level);
				ptr = cur->ptrs[level];
			} else
				return 0;
		}
	}
	rp = XFS_BMAP_REC_ADDR(block, 1, bl, cur);
	if (level > 0) {
		pp = XFS_BMAP_PTR_ADDR(block, 1, bl, cur);
		for (i = block->numrecs; i >= ptr; i--) {
			rp[i] = rp[i - 1];
			xfs_bmap_check_ptr(cur, pp[i - 1], level);
			pp[i] = pp[i - 1];
		}
		xfs_bmap_check_ptr(cur, *bnop, level);
		pp[i] = *bnop;
		if (level < cur->nlevels - 1) {
			first = (caddr_t)&pp[i] - (caddr_t)block;
			last = (caddr_t)&pp[block->numrecs] - (caddr_t)block +
				sizeof(pp[i]) - 1;
			xfs_trans_log_buf(tp, cur->bufs[level], first, last);
		} else {
			/* log inode fields */
			/* FIXME */
		}
	} else {
		for (i = block->numrecs; i >= ptr; i--)
			rp[i] = rp[i - 1];
	}
	rp[i] = *recp;
	if (level < cur->nlevels - 1) {
		first = (caddr_t)&rp[i] - (caddr_t)block;
		last = (caddr_t)&rp[block->numrecs] - (caddr_t)block +
			sizeof(rp[i]) - 1;
		xfs_trans_log_buf(tp, cur->bufs[level], first, last);
	} else {
		/* log inode fields */
		/* FIXME */
	}
	block->numrecs++;
	if (level < cur->nlevels - 1) {
		first = offsetof(xfs_btree_block_t, numrecs);
		last = first + sizeof(block->numrecs) - 1;
		xfs_trans_log_buf(tp, cur->bufs[level], first, last);
	} else {
		/* log inode fields */
		/* FIXME */
	}
	if (ptr < block->numrecs)
		xfs_bmap_check_rec(rp + i, rp + i + 1);
	if (optr == 1)
		xfs_bmap_updkey(cur, recp, level + 1);
	*bnop = nbno;
	xfs_bmap_rcheck(cur);
	if (nbno != NULLAGBLOCK) {
		*recp = nrec;
		*curp = ncur;
	} else
		xfs_bmap_kcheck(cur);
	return 1;
}

/*
 * Check for last leaf block.
 */
int
xfs_bmap_islastblock(xfs_bmap_cur_t *cur, int level)
{
	xfs_btree_block_t *block;

	block = xfs_bmap_get_block(cur, level);
	xfs_bmap_check_block(cur, block, level);
	return block->rightsib == NULLAGBLOCK;
}

#ifdef XFSBMDEBUG
/*
 * Debug routine to check key consistency.
 */
void
xfs_bmap_kcheck(xfs_bmap_cur_t *cur)
{
	buf_t *agbuf;
	xfs_aghdr_t *agp;
	xfs_btree_block_t *block;
	xfs_dinode_t *dip;
	int i;
	xfs_inode_t *ip;
	int level;
	int levels;

	ip = cur->ip;
	dip = &ip->i_d;
	ASSERT(dip->di_format == XFS_DINODE_FMT_BTREE);
	block = &dip->di_u.di_bmbt;
	agbuf = cur->agbuf;
	agp = xfs_buf_to_agp(agbuf);
	level = block->level;
	levels = level + 1;
	ASSERT(levels == cur->nlevels);
	xfs_bmap_kcheck_body(cur, agp, block, level, (xfs_bmap_rec_t *)0);
}

void
xfs_bmap_kcheck_body(xfs_bmap_cur_t *cur, xfs_aghdr_t *agp, xfs_btree_block_t *block, int level, xfs_bmap_rec_t *kp)
{
	int bl;
	int i;
	xfs_mount_t *mp;
	xfs_agblock_t *pp;
	xfs_bmap_rec_t *rp;
	xfs_sb_t *sbp;
	xfs_trans_t *tp;

	tp = cur->tp;
	mp = tp->t_mountp;
	sbp = mp->m_sb;
	bl = sbp->xfsb_blocklog;
	xfs_bmap_check_block(cur, block, level);
	rp = XFS_BMAP_REC_ADDR(block, 1, bl, cur);
	if (kp)
		ASSERT(rp->startblock == kp->startblock &&
		       rp->blockcount == kp->blockcount);
	if (level > 0) {
		pp = XFS_BMAP_PTR_ADDR(block, 1, bl, cur);
		if (*pp != NULLAGBLOCK) {
			for (i = 1; i <= block->numrecs; i++, pp++, rp++)
				xfs_bmap_kcheck_btree(cur, agp, *pp, level - 1, rp);
		}
	}
}

void
xfs_bmap_kcheck_btree(xfs_bmap_cur_t *cur, xfs_aghdr_t *agp, xfs_agblock_t bno, int level, xfs_bmap_rec_t *kp)
{
	xfs_btree_block_t *block;
	buf_t *buf;

	ASSERT(bno != NULLAGBLOCK);
	buf = xfs_bmap_bread(cur->tp, cur->agno, bno);
	block = xfs_buf_to_block(buf);
	xfs_bmap_kcheck_body(cur, agp, block, level, kp);
}
#endif

/*
 * Point to the last record in the current block at the given level.
 */
int
xfs_bmap_lastrec(xfs_bmap_cur_t *cur, int level)
{
	xfs_btree_block_t *block;

	block = xfs_bmap_get_block(cur, level);
	xfs_bmap_check_block(cur, block, level);
	if (!block->numrecs)
		return 0;
	cur->ptrs[level] = block->numrecs;
	return 1;
}

/*
 * Lookup the record.  The cursor is made to point to it, based on dir.
 */
int
xfs_bmap_lookup(xfs_bmap_cur_t *cur, xfs_lookup_t dir)
{
	xfs_agblock_t agbno;
	buf_t *agbuf;
	xfs_agnumber_t agno;
	xfs_aghdr_t *agp;
	int bl;
	xfs_btree_block_t *block;
	buf_t *buf;
	daddr_t d;
	int diff;
	int high;
	int i;
	xfs_bmap_rec_t *kbase;
	int keyno;
	xfs_bmap_rec_t *kp;
	int level;
	int low;
	xfs_mount_t *mp;
	xfs_bmap_rec_t *rp;
	xfs_sb_t *sbp;
	xfs_trans_t *tp;

	xfs_bmap_rcheck(cur);
	xfs_bmap_kcheck(cur);
	tp = cur->tp;
	mp = tp->t_mountp;
	agbuf = cur->agbuf;
	agp = xfs_buf_to_agp(agbuf);
	agno = agp->xfsag_seqno;
	sbp = mp->m_sb;
	bl = sbp->xfsb_blocklog;
	rp = &cur->rec;
	for (level = cur->nlevels - 1, diff = 1; level >= 0; level--) {
		if (level < cur->nlevels - 1) {
			d = xfs_agb_to_daddr(sbp, agno, agbno);
			buf = cur->bufs[level];
			if (buf && buf->b_blkno != d)
				buf = (buf_t *)0;
			if (!buf)
				cur->bufs[level] = xfs_trans_bread(tp, mp->m_dev, d, mp->m_bsize);
		}
		block = xfs_bmap_get_block(cur, level);
		xfs_bmap_check_block(cur, block, level);
		if (diff == 0)
			keyno = 1;
		else {
			kbase = XFS_BMAP_REC_ADDR(block, 1, bl, cur);
			low = 1;
			if (!(high = block->numrecs)) {
				ASSERT(level == 0);
				cur->ptrs[0] = dir != XFS_LOOKUP_LE;
				return 0;
			}
			while (low <= high) {
				keyno = (low + high) >> 1;
				kp = kbase + keyno - 1;
				diff = kp->startoff - rp->startoff;
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
			agbno = *XFS_BMAP_PTR_ADDR(block, keyno, bl, cur);
			xfs_bmap_check_ptr(cur, agbno, level);
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
			i = xfs_bmap_increment(cur, 0);
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
xfs_bmap_lookup_eq(xfs_bmap_cur_t *cur, xfs_fsblock_t off, xfs_fsblock_t bno, xfs_extlen_t len)
{
	cur->rec.startoff = off;
	cur->rec.startblock = bno;
	cur->rec.blockcount = len;
	return xfs_bmap_lookup(cur, XFS_LOOKUP_EQ);
}

int
xfs_bmap_lookup_ge(xfs_bmap_cur_t *cur, xfs_fsblock_t off, xfs_fsblock_t bno, xfs_extlen_t len)
{
	cur->rec.startoff = off;
	cur->rec.startblock = bno;
	cur->rec.blockcount = len;
	return xfs_bmap_lookup(cur, XFS_LOOKUP_GE);
}

int
xfs_bmap_lookup_le(xfs_bmap_cur_t *cur, xfs_fsblock_t off, xfs_fsblock_t bno, xfs_extlen_t len)
{
	cur->rec.startoff = off;
	cur->rec.startblock = bno;
	cur->rec.blockcount = len;
	return xfs_bmap_lookup(cur, XFS_LOOKUP_LE);
}

/*
 * Move 1 record left from cur/level if possible.
 * Update cur to reflect the new path.
 */
int
xfs_bmap_lshift(xfs_bmap_cur_t *cur, int level)
{
	int bl;
	daddr_t d;
	int first;
	int i;
	int last;
	buf_t *lbuf;
	xfs_btree_block_t *left;
	int lev;
	xfs_agblock_t *lpp;
	xfs_bmap_rec_t *lrp;
	xfs_mount_t *mp;
	int ptr;
	buf_t *rbuf;
	xfs_btree_block_t *right;
	xfs_agblock_t *rpp;
	xfs_bmap_rec_t *rrp;
	xfs_sb_t *sbp;
	xfs_trans_t *tp;

	if (level == cur->nlevels - 1)
		return 0;
	xfs_bmap_rcheck(cur);
	rbuf = cur->bufs[level];
	right = xfs_buf_to_block(rbuf);
	xfs_bmap_check_block(cur, right, level);
	if (right->leftsib == NULLAGBLOCK)
		return 0;
	if (cur->ptrs[level] <= 1)
		return 0;
	tp = cur->tp;
	mp = tp->t_mountp;
	sbp = mp->m_sb;
	lbuf = xfs_bmap_bread(tp, cur->agno, right->leftsib);
	left = xfs_buf_to_block(lbuf);
	xfs_bmap_check_block(cur, left, level);
	bl = sbp->xfsb_blocklog;
	if (left->numrecs == XFS_BMAP_BLOCK_MAXRECS(bl, level, cur))
		return 0;
	lrp = XFS_BMAP_REC_ADDR(left, left->numrecs + 1, bl, cur);
	rrp = XFS_BMAP_REC_ADDR(right, 1, bl, cur);
	*lrp = *rrp;
	first = (caddr_t)lrp - (caddr_t)left;
	last = first + sizeof(*lrp) - 1;
	xfs_trans_log_buf(tp, lbuf, first, last);
	if (level > 0) {
		lpp = XFS_BMAP_PTR_ADDR(left, left->numrecs + 1, bl, cur);
		rpp = XFS_BMAP_PTR_ADDR(right, 1, bl, cur);
		xfs_bmap_check_ptr(cur, *rpp, level);
		*lpp = *rpp;
		first = (caddr_t)lpp - (caddr_t)left;
		last = first + sizeof(*lpp) - 1;
		xfs_trans_log_buf(tp, lbuf, first, last);
	}
	left->numrecs++;
	first = offsetof(xfs_btree_block_t, numrecs);
	last = first + sizeof(left->numrecs) - 1;
	xfs_trans_log_buf(tp, lbuf, first, last);
	xfs_bmap_check_rec(lrp - 1, lrp);
	right->numrecs--;
	xfs_trans_log_buf(tp, rbuf, first, last);
	if (level > 0) {
		for (i = 0; i < right->numrecs; i++) {
			rrp[i] = rrp[i + 1];
			xfs_bmap_check_ptr(cur, rpp[i + 1], level);
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
	xfs_bmap_updkey(cur, rrp, level + 1);
	cur->ptrs[level]--;
	xfs_bmap_rcheck(cur);
	return 1;
}

#ifdef XFSBMDEBUG
void
xfs_bmap_rcheck(xfs_bmap_cur_t *cur)
{
	buf_t *agbuf;
	xfs_aghdr_t *agp;
	xfs_btree_block_t *block;
	xfs_agblock_t bno;
	xfs_dinode_t *dip;
	xfs_agblock_t fbno;
	int i;
	xfs_inode_t *ip;
	int level;
	xfs_bmap_rec_t rec;
	xfs_bmap_rec_t *rp;

	agbuf = cur->agbuf;
	agp = xfs_buf_to_agp(agbuf);
	ip = cur->ip;
	dip = &ip->i_d;
	ASSERT(dip->di_format == XFS_DINODE_FMT_BTREE);
	block = &dip->di_u.di_bmbt;
	level = cur->nlevels - 1;
	xfs_bmap_rcheck_body(cur, block, &bno, &rec, level);
	for (level = block->level - 1; level >= 0; level--, bno = fbno) {
		bno = xfs_bmap_rcheck_btree(cur, bno, &fbno, &rec, level);
		while (bno != NULLAGBLOCK) {
			ASSERT(bno < agp->xfsag_length);
			bno = xfs_bmap_rcheck_btree(cur, bno, (xfs_agblock_t *)0, &rec, level);
		}
	}
}

xfs_agblock_t
xfs_bmap_rcheck_body(xfs_bmap_cur_t *cur, xfs_btree_block_t *block, xfs_agblock_t *fbno, xfs_bmap_rec_t *rec, int level)
{
	int bl;
	xfs_btree_block_t *block;
	buf_t *buf;
	int i;
	xfs_mount_t *mp;
	xfs_agblock_t *pp;
	xfs_agblock_t rbno;
	xfs_bmap_rec_t *rp;
	xfs_sb_t *sbp;
	xfs_trans_t *tp;

	tp = cur->tp;
	mp = tp->t_mountp;
	sbp = mp->m_sb;
	bl = sbp->xfsb_blocklog;
	xfs_bmap_check_block(cur, block, level);
	if (fbno && block->numrecs) {
		if (level > 0)
			*fbno = *XFS_BMAP_PTR_ADDR(block, 1, bl, cur);
		else
			*fbno = NULLAGBLOCK;
	}
	rbno = block->rightsib;
	for (i = 1; i <= block->numrecs; i++) {
		rp = XFS_BMAP_REC_ADDR(block, i, bl, cur);
		if (i == 1 && !fbno)
			xfs_bmap_check_rec(rec, rp);
		else if (i > 1) {
			xfs_bmap_check_rec(rp - 1, rp);
			if (i == block->numrecs)
				*rec = *rp;
		}
		if (level > 0) {
			pp = XFS_BMAP_PTR_ADDR(block, i, bl, cur);
			xfs_bmap_check_ptr(cur, *pp, level);
		}
	}
	return rbno;
}

xfs_agblock_t
xfs_bmap_rcheck_btree(xfs_bmap_cur_t *cur, xfs_agblock_t bno, xfs_agblock_t *fbno, xfs_bmap_rec_t *rec, int level)
{
	xfs_btree_block_t *block;
	buf_t *buf;

	buf = xfs_bmap_bread(cur->tp, cur->agno, bno);
	block = xfs_buf_to_block(buf);
	return xfs_bmap_rcheck_body(cur, block, fbno, rec, level);
}
#endif

/*
 * Move 1 record right from cur/level if possible.
 * Update cur to reflect the new path.
 */
int
xfs_bmap_rshift(xfs_bmap_cur_t *cur, int level)
{
	int bl;
	int first;
	int i;
	int last;
	buf_t *lbuf;
	xfs_btree_block_t *left;
	int lev;
	xfs_agblock_t *lpp;
	xfs_bmap_rec_t *lrp;
	xfs_mount_t *mp;
	int ptr;
	buf_t *rbuf;
	xfs_btree_block_t *right;
	xfs_bmap_rec_t *rp;
	xfs_agblock_t *rpp;
	xfs_bmap_rec_t *rrp;
	xfs_sb_t *sbp;
	xfs_bmap_cur_t *tcur;
	xfs_trans_t *tp;

	if (level == cur->nlevels - 1)
		return 0;
	xfs_bmap_rcheck(cur);
	lbuf = cur->bufs[level];
	left = xfs_buf_to_block(lbuf);
	xfs_bmap_check_block(cur, left, level);
	if (left->rightsib == NULLAGBLOCK)
		return 0;
	if (cur->ptrs[level] >= left->numrecs)
		return 0;
	tp = cur->tp;
	mp = tp->t_mountp;
	sbp = mp->m_sb;
	rbuf = xfs_bmap_bread(tp, cur->agno, left->rightsib);
	right = xfs_buf_to_block(rbuf);
	xfs_bmap_check_block(cur, right, level);
	bl = sbp->xfsb_blocklog;
	if (right->numrecs == XFS_BMAP_BLOCK_MAXRECS(bl, level, cur))
		return 0;
	lrp = XFS_BMAP_REC_ADDR(left, left->numrecs, bl, cur);
	rrp = XFS_BMAP_REC_ADDR(right, 1, bl, cur);
	if (level > 0) {
		lpp = XFS_BMAP_PTR_ADDR(left, left->numrecs, bl, cur);
		rpp = XFS_BMAP_PTR_ADDR(right, 1, bl, cur);
		for (i = right->numrecs - 1; i >= 0; i--) {
			rrp[i + 1] = rrp[i];
			xfs_bmap_check_ptr(cur, rpp[i], level);
			rpp[i + 1] = rpp[i];
		}
		xfs_bmap_check_ptr(cur, *lpp, level);
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
	first = offsetof(xfs_btree_block_t, numrecs);
	last = first + sizeof(left->numrecs) - 1;
	xfs_trans_log_buf(tp, lbuf, first, last);
	right->numrecs++;
	xfs_bmap_check_rec(rrp, rrp + 1);
	xfs_trans_log_buf(tp, rbuf, first, last);
	tcur = xfs_bmap_dup_cursor(cur, __LINE__);
	xfs_bmap_lastrec(tcur, level);
	xfs_bmap_increment(tcur, level);
	xfs_bmap_updkey(tcur, rrp, level + 1);
	xfs_bmap_del_cursor(tcur);
	xfs_bmap_rcheck(cur);
	return 1;
}

/*
 * Split cur/level block in half.
 * Return new block number and its first record (to be inserted into parent).
 */
int
xfs_bmap_split(xfs_bmap_cur_t *cur, int level, xfs_agblock_t *bnop, xfs_bmap_rec_t *recp, xfs_bmap_cur_t **curp)
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
	xfs_btree_block_t *left;
	xfs_fsblock_t lfsbno;
	xfs_agblock_t *lpp;
	xfs_bmap_rec_t *lrp;
	xfs_mount_t *mp;
	xfs_agblock_t rbno;
	buf_t *rbuf;
	xfs_bmap_rec_t rec;
	xfs_fsblock_t rfsbno;
	xfs_btree_block_t *right;
	xfs_agblock_t *rpp;
	xfs_btree_block_t *rrblock;
	buf_t *rrbuf;
	xfs_bmap_rec_t *rrp;
	xfs_sb_t *sbp;
	xfs_trans_t *tp;

	xfs_bmap_rcheck(cur);
	tp = cur->tp;
	mp = tp->t_mountp;
	sbp = mp->m_sb;
	bl = sbp->xfsb_blocklog;
	agbuf = cur->agbuf;
	agp = xfs_buf_to_agp(agbuf);
	lbuf = cur->bufs[level];
	lfsbno = xfs_daddr_to_fsb(sbp, lbuf->b_blkno);
	left = xfs_buf_to_block(lbuf);
	rfsbno = xfs_alloc_extent(tp, lfsbno, 1, XFS_ALLOCTYPE_NEAR_BNO);
	if (rfsbno == NULLFSBLOCK)
		return 0;
	rbno = xfs_fsb_to_agbno(sbp, rfsbno);
	rbuf = xfs_bmap_bread(tp, cur->agno, rbno);
	right = xfs_buf_to_block(rbuf);
	xfs_bmap_check_block(cur, left, level);
	right->magic = XFS_BMAP_MAGIC;
	right->level = left->level;
	right->numrecs = left->numrecs / 2;
	if ((left->numrecs & 1) && cur->ptrs[level] <= right->numrecs + 1)
		right->numrecs++;
	i = left->numrecs - right->numrecs + 1;
	lrp = XFS_BMAP_REC_ADDR(left, i, bl, cur);
	rrp = XFS_BMAP_REC_ADDR(right, 1, bl, cur);
	if (level > 0) {
		lpp = XFS_BMAP_PTR_ADDR(left, i, bl, cur);
		rpp = XFS_BMAP_PTR_ADDR(right, 1, bl, cur);
		for (i = 0; i < right->numrecs; i++) {
			rrp[i] = lrp[i];
			xfs_bmap_check_ptr(cur, lpp[i], level);
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
	last = offsetof(xfs_btree_block_t, rightsib) +
		sizeof(left->rightsib) - 1;
	xfs_trans_log_buf(tp, rbuf, first, last);
	first = offsetof(xfs_btree_block_t, numrecs);
	xfs_trans_log_buf(tp, lbuf, first, last);
	if (right->rightsib != NULLAGBLOCK) {
		rrbuf = xfs_bmap_bread(tp, cur->agno, right->rightsib);
		rrblock = xfs_buf_to_block(rrbuf);
		xfs_bmap_check_block(cur, rrblock, level);
		rrblock->leftsib = rbno;
		first = offsetof(xfs_btree_block_t, leftsib);
		last = first + sizeof(rrblock->leftsib) - 1;
		xfs_trans_log_buf(tp, rrbuf, first, last);
	}
	if (cur->ptrs[level] > left->numrecs + 1) {
		cur->bufs[level] = rbuf;
		cur->ptrs[level] -= left->numrecs;
	}
	if (level + 1 < cur->nlevels) {
		*curp = xfs_bmap_dup_cursor(cur, __LINE__);
		(*curp)->ptrs[level + 1]++;
	}
	*bnop = rbno;
	*recp = rec;
	xfs_bmap_rcheck(cur);
	return 1;
}

/*
 * Update the record to the passed values.
 */
int
xfs_bmap_update(xfs_bmap_cur_t *cur, xfs_fsblock_t off, xfs_fsblock_t bno, xfs_extlen_t len)
{
	buf_t *agbuf;
	xfs_aghdr_t *agp;
	int bl;
	xfs_btree_block_t *block;
	buf_t *buf;
	int first;
	int last;
	xfs_mount_t *mp;
	int ptr;
	xfs_bmap_rec_t *rp;
	xfs_sb_t *sbp;
	xfs_trans_t *tp;

	agbuf = cur->agbuf;
	agp = xfs_buf_to_agp(agbuf);
	xfs_bmap_rcheck(cur);
	block = xfs_bmap_get_block(cur, 0);
	xfs_bmap_check_block(cur, block, 0);
	ptr = cur->ptrs[0];
	tp = cur->tp;
	mp = tp->t_mountp;
	sbp = mp->m_sb;
	bl = sbp->xfsb_blocklog;
	rp = XFS_BMAP_REC_ADDR(block, ptr, bl, cur);
	rp->startoff = off;
	rp->startblock = bno;
	rp->blockcount = len;
	if (cur->nlevels > 1) {
		first = (caddr_t)rp - (caddr_t)block;
		last = first + sizeof(*rp) - 1;
		xfs_trans_log_buf(tp, cur->bufs[0], first, last);
	} else {
		/* log inode field */
		/* FIXME */
	}
	if (ptr > 1)
		return 1;
	xfs_bmap_updkey(cur, rp, 1);
	xfs_bmap_rcheck(cur);
	xfs_bmap_kcheck(cur);
	return 1;
}

/*
 * Update keys for the record.
 */
void
xfs_bmap_updkey(xfs_bmap_cur_t *cur, xfs_bmap_rec_t *kp, int level)
{
	int bl;
	xfs_btree_block_t *block;
	buf_t *buf;
	int first;
	int last;
	xfs_mount_t *mp;
	int ptr;
	xfs_bmap_rec_t *rp;
	xfs_sb_t *sbp;
	xfs_trans_t *tp;

	xfs_bmap_rcheck(cur);
	tp = cur->tp;
	mp = tp->t_mountp;
	sbp = mp->m_sb;
	bl = sbp->xfsb_blocklog;
	for (ptr = 1; ptr == 1 && level < cur->nlevels; level++) {
		block = xfs_bmap_get_block(cur, level);
		xfs_bmap_check_block(cur, block, level);
		ptr = cur->ptrs[level];
		rp = XFS_BMAP_REC_ADDR(block, ptr, bl, cur);
		*rp = *kp;
		if (level < cur->nlevels - 1) {
			first = (caddr_t)rp - (caddr_t)block;
			last = first + sizeof(*rp) - 1;
			xfs_trans_log_buf(tp, cur->bufs[level], first, last);
		} else {
			/* log inode fields */
			/* FIXME */
		}
	}
	xfs_bmap_rcheck(cur);
}
