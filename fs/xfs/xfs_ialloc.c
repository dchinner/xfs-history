#ident	"$Revision: 1.4 $"

#include "xfs_sb.h"
#include "xfs_ag.h"
#include "xfs_alloc.h"
#include "xfs_ialloc.h"
#include "xfs_mount.h"
#include "sim.h"
#include <sys/stat.h>
#include <stddef.h>
#include <bstring.h>

xfs_ialloc_cur_t *xfs_ialloc_curfreelist;
#ifdef XFSIDEBUG
int xfs_ialloc_curfreecount;
xfs_ialloc_cur_t *xfs_ialloc_curalllist;
int xfs_ialloc_curallcount;
#endif

/*
 * Prototypes for internal functions.
 */

#ifndef XFSIDEBUG
#define NDEBUG
#endif
#include <assert.h>
#define	ASSERT(x)	assert(x)

#ifdef XFSIDEBUG
void xfs_ialloc_check_block(xfs_ialloc_cur_t *, xfs_ialloc_block_t *, int);
void xfs_ialloc_check_ptr(xfs_ialloc_cur_t *, xfs_agblock_t, int);
void xfs_ialloc_check_rec(xfs_ialloc_rec_t *, xfs_ialloc_rec_t *);
void xfs_ialloc_kcheck(xfs_ialloc_cur_t *);
void xfs_ialloc_kcheck_btree(xfs_ialloc_cur_t *, xfs_aghdr_t *, xfs_agblock_t, int, xfs_ialloc_rec_t *);
void xfs_ialloc_rcheck(xfs_ialloc_cur_t *);
void xfs_ialloc_rcheck_btree(xfs_ialloc_cur_t *, xfs_aghdr_t *, xfs_agblock_t, int);
xfs_agblock_t xfs_ialloc_rcheck_btree_block(xfs_ialloc_cur_t *, xfs_agnumber_t, xfs_agblock_t, xfs_agblock_t *, xfs_ialloc_rec_t *, int);
#else
#define	xfs_ialloc_check_block(a,b,c)
#define	xfs_ialloc_check_ptr(a,b,c)
#define	xfs_ialloc_check_rec(b,c)
#define	xfs_ialloc_kcheck(a)
#define	xfs_ialloc_rcheck(a)
#endif

buf_t *xfs_ialloc_bread(xfs_trans_t *, xfs_agnumber_t, xfs_agblock_t);
int xfs_ialloc_decrement(xfs_ialloc_cur_t *, int);
void xfs_ialloc_del_cursor(xfs_ialloc_cur_t *);
xfs_ialloc_cur_t *xfs_ialloc_dup_cursor(xfs_ialloc_cur_t *, int);
int xfs_ialloc_get_rec(xfs_ialloc_cur_t *, xfs_agino_t *, xfs_agblock_t *, xfs_agino_t *);
int xfs_ialloc_increment(xfs_ialloc_cur_t *, int);
xfs_ialloc_cur_t *xfs_ialloc_init_cursor(xfs_trans_t *, buf_t *, xfs_agnumber_t, int);
int xfs_ialloc_insert(xfs_ialloc_cur_t *);
int xfs_ialloc_insrec(xfs_ialloc_cur_t *, int, xfs_agblock_t *, xfs_ialloc_rec_t *, xfs_ialloc_cur_t **);
int xfs_ialloc_lastrec(xfs_ialloc_cur_t *, int);
int xfs_ialloc_lookup(xfs_ialloc_cur_t *, xfs_lookup_t);
int xfs_ialloc_lookup_eq(xfs_ialloc_cur_t *, xfs_agino_t, xfs_agblock_t, xfs_agino_t);
int xfs_ialloc_lookup_ge(xfs_ialloc_cur_t *, xfs_agino_t, xfs_agblock_t, xfs_agino_t);
int xfs_ialloc_lookup_le(xfs_ialloc_cur_t *, xfs_agino_t, xfs_agblock_t, xfs_agino_t);
int xfs_ialloc_lshift(xfs_ialloc_cur_t *, int);
int xfs_ialloc_newroot(xfs_ialloc_cur_t *);
int xfs_ialloc_rshift(xfs_ialloc_cur_t *, int);
int xfs_ialloc_split(xfs_ialloc_cur_t *, int, xfs_agblock_t *, xfs_ialloc_rec_t *, xfs_ialloc_cur_t **);
int xfs_ialloc_update(xfs_ialloc_cur_t *, xfs_agino_t, xfs_agblock_t, xfs_agino_t);
void xfs_ialloc_updkey(xfs_ialloc_cur_t *, xfs_ialloc_rec_t *, int);

/*
 * Internal functions.
 */

buf_t *
xfs_ialloc_bread(xfs_trans_t *tp, xfs_agnumber_t agno, xfs_agblock_t agbno)
{
	daddr_t d;
	xfs_mount_t *mp;
	xfs_sb_t *sbp;

	mp = tp->t_mountp;
	sbp = mp->m_sb;
	d = xfs_agb_to_daddr(sbp, agno, agbno);
	return xfs_trans_bread(tp, mp->m_dev, d, mp->m_bsize);
}

#ifdef XFSIDEBUG
void
xfs_ialloc_check_block(xfs_ialloc_cur_t *cur, xfs_ialloc_block_t *block, int level)
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
	ASSERT(block->magic == XFS_IBT_MAGIC);
	ASSERT(block->level == level);
	ASSERT(block->numrecs <= XFS_IALLOC_BLOCK_MAXRECS(bl, level));
	ASSERT(block->leftsib == NULLAGBLOCK || block->leftsib < agp->xfsag_length);
	ASSERT(block->rightsib == NULLAGBLOCK || block->rightsib < agp->xfsag_length);
}

void
xfs_ialloc_check_ptr(xfs_ialloc_cur_t *cur, xfs_agblock_t ptr, int level)
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
xfs_ialloc_check_rec(xfs_ialloc_rec_t *r1, xfs_ialloc_rec_t *r2)
{
	ASSERT(r1->startinode + r1->inodecount <= r2->startinode);
}
#endif

/*
 * Decrement cursor by one record at the level.
 * For nonzero levels the leaf-ward information is untouched.
 */
int
xfs_ialloc_decrement(xfs_ialloc_cur_t *cur, int level)
{
	xfs_agblock_t agbno;
	int bl;
	xfs_ialloc_block_t *block;
	buf_t *buf;
	int lev;
	xfs_mount_t *mp;
	int ptr;
	xfs_sb_t *sbp;
	xfs_trans_t *tp;

	if (--cur->ptrs[level] > 0)
		return 1;
	buf = cur->bufs[level];
	block = xfs_buf_to_iblock(buf);
	xfs_ialloc_check_block(cur, block, level);
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
		block = xfs_buf_to_iblock(buf);
		xfs_ialloc_check_block(cur, block, lev);
		agbno = *XFS_IALLOC_PTR_ADDR(block, cur->ptrs[lev], bl);
		buf = cur->bufs[lev - 1] = xfs_ialloc_bread(tp, cur->agno, agbno);
		block = xfs_buf_to_iblock(buf);
		xfs_ialloc_check_block(cur, block, lev - 1);
		cur->ptrs[lev - 1] = block->numrecs;
	}
	return 1;
}

/*
 * Delete the cursor, unreferencing its buffers.
 */
void
xfs_ialloc_del_cursor(xfs_ialloc_cur_t *cur)
{
	cur->tp = (xfs_trans_t *)xfs_ialloc_curfreelist;
	xfs_ialloc_curfreelist = cur;
#ifdef XFSIDEBUG
	xfs_ialloc_curfreecount++;
#endif
}

/*
 * Duplicate the cursor.
 * Add references to its buffers.
 */
xfs_ialloc_cur_t *
xfs_ialloc_dup_cursor(xfs_ialloc_cur_t *cur, int lineno)
{
	buf_t *agbuf;
	xfs_aghdr_t *agp;
	buf_t *buf;
	int i;
	xfs_mount_t *mp;
	xfs_ialloc_cur_t *newcur;
	xfs_trans_t *tp;

	agbuf = cur->agbuf;
	agp = xfs_buf_to_agp(agbuf);
	tp = cur->tp;
	mp = tp->t_mountp;
	newcur = xfs_ialloc_init_cursor(tp, agbuf, agp->xfsag_seqno, lineno);
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
 * Get the data from the pointed-to record.
 */
int
xfs_ialloc_get_rec(xfs_ialloc_cur_t *cur, xfs_agino_t *ino, xfs_agblock_t *bno, xfs_agino_t *len)
{
	int bl;
	xfs_ialloc_block_t *block;
	buf_t *buf;
	xfs_mount_t *mp;
	int ptr;
	xfs_ialloc_rec_t *rec;
	xfs_sb_t *sbp;
	xfs_trans_t *tp;

	buf = cur->bufs[0];
	ptr = cur->ptrs[0];
	block = xfs_buf_to_iblock(buf);
	xfs_ialloc_check_block(cur, block, 0);
	if (ptr > block->numrecs || ptr <= 0)
		return 0;
	tp = cur->tp;
	mp = tp->t_mountp;
	sbp = mp->m_sb;
	bl = sbp->xfsb_blocklog;
	rec = XFS_IALLOC_REC_ADDR(block, ptr, bl);
	*ino = rec->startinode;
	*bno = rec->startblock;
	*len = rec->inodecount;
	return 1;
}

/*
 * Increment cursor by one record at the level.
 * For nonzero levels the leaf-ward information is untouched.
 */
int
xfs_ialloc_increment(xfs_ialloc_cur_t *cur, int level)
{
	xfs_agblock_t agbno;
	int bl;
	xfs_ialloc_block_t *block;
	buf_t *buf;
	int lev;
	xfs_mount_t *mp;
	int ptr;
	xfs_sb_t *sbp;
	xfs_trans_t *tp;

	buf = cur->bufs[level];
	block = xfs_buf_to_iblock(buf);
	xfs_ialloc_check_block(cur, block, level);
	if (++cur->ptrs[level] <= block->numrecs)
		return 1;
	if (block->rightsib == NULLAGBLOCK)
		return 0;
	for (lev = level + 1; lev < cur->nlevels; lev++) {
		buf = cur->bufs[lev];
		block = xfs_buf_to_iblock(buf);
		xfs_ialloc_check_block(cur, block, lev);
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
		block = xfs_buf_to_iblock(buf);
		xfs_ialloc_check_block(cur, block, lev);
		agbno = *XFS_IALLOC_PTR_ADDR(block, cur->ptrs[lev], bl);
		cur->bufs[lev - 1] = xfs_ialloc_bread(tp, cur->agno, agbno);
		cur->ptrs[lev - 1] = 1;
	}
	return 1;
}

/*
 * Allocate and initialize a new cursor.
 */
xfs_ialloc_cur_t *
xfs_ialloc_init_cursor(xfs_trans_t *tp, buf_t *agbuf, xfs_agnumber_t agno, int lineno)
{
	xfs_aghdr_t *agp;
	xfs_ialloc_cur_t *cur;

	if (xfs_ialloc_curfreelist) {
		cur = xfs_ialloc_curfreelist;
		xfs_ialloc_curfreelist = (xfs_ialloc_cur_t *)cur->tp;
#ifdef XFSIDEBUG
		xfs_ialloc_curfreecount--;
#endif
		if (tp != cur->tp)
			bzero(cur->bufs, sizeof(cur->bufs));
	} else {
		cur = (xfs_ialloc_cur_t *)kmem_zalloc(sizeof(*cur), 0);
#ifdef XFSIDEBUG
		cur->next = xfs_ialloc_curalllist;
		xfs_ialloc_curalllist = cur;
		xfs_ialloc_curallcount++;
#endif
	}
	cur->tp = tp;
	cur->agbuf = agbuf;
	cur->agno = agno;
#ifdef XFSIDEBUG
	cur->lineno = lineno;
#endif
	agp = xfs_buf_to_agp(agbuf);
	cur->nlevels = agp->xfsag_ilevels;
	return cur;
}

/*
 * Insert the current record at the point referenced by cur.
 */
int
xfs_ialloc_insert(xfs_ialloc_cur_t *cur)
{
	int i;
	int level;
	xfs_agblock_t nbno;
	xfs_ialloc_cur_t *ncur;
	xfs_ialloc_rec_t nrec;
	xfs_ialloc_cur_t *pcur;

	level = 0;
	nbno = NULLAGBLOCK;
	nrec = cur->rec;
	ncur = (xfs_ialloc_cur_t *)0;
	pcur = cur;
	do {
		i = xfs_ialloc_insrec(pcur, level++, &nbno, &nrec, &ncur);
		if (pcur != cur && (ncur || nbno == NULLAGBLOCK)) {
			cur->nlevels = pcur->nlevels;
			xfs_ialloc_del_cursor(pcur);
		}
		if (ncur) {
			pcur = ncur;
			ncur = (xfs_ialloc_cur_t *)0;
		}
	} while (nbno != NULLAGBLOCK);
	return i;
}

/*
 * Insert one record/level.  Return information to the caller
 * allowing the next level up to proceed if necessary.
 */
int
xfs_ialloc_insrec(xfs_ialloc_cur_t *cur, int level, xfs_agblock_t *bnop, xfs_ialloc_rec_t *recp, xfs_ialloc_cur_t **curp)
{
	xfs_aghdr_t *agp;
	int bl;
	xfs_ialloc_block_t *block;
	buf_t *buf;
	int first;
	int i;
	int last;
	xfs_mount_t *mp;
	xfs_agblock_t nbno;
	xfs_ialloc_cur_t *ncur = (xfs_ialloc_cur_t *)0;
	xfs_ialloc_rec_t nrec;
#ifdef XFSIDEBUG
	int op = 0;
#endif
	int optr;
	xfs_agblock_t *pp;
	int ptr;
	xfs_ialloc_rec_t *rbase;
	xfs_ialloc_rec_t *rp;
	xfs_sb_t *sbp;
	xfs_trans_t *tp;
	xfs_ialloc_rec_t xxrec;

	if (level >= cur->nlevels) {
		i = xfs_ialloc_newroot(cur);
		*bnop = NULLAGBLOCK;
		return i;
	}
	xfs_ialloc_rcheck(cur);
	optr = ptr = cur->ptrs[level];
	if (ptr == 0)
		return 0;
	buf = cur->bufs[level];
	block = xfs_buf_to_iblock(buf);
	xfs_ialloc_check_block(cur, block, level);
	tp = cur->tp;
	mp = tp->t_mountp;
	sbp = mp->m_sb;
	bl = sbp->xfsb_blocklog;
	if (ptr <= block->numrecs) {
		rp = XFS_IALLOC_REC_ADDR(block, ptr, bl);
		xfs_ialloc_check_rec(recp, rp);
	}
	nbno = NULLAGBLOCK;
	if (block->numrecs == XFS_IALLOC_BLOCK_MAXRECS(bl, level)) {
		if (xfs_ialloc_rshift(cur, level)) {
#ifdef XFSIDEBUG
			op = 'r';
#endif
		} else if (xfs_ialloc_lshift(cur, level)) {
#ifdef XFSIDEBUG
			op = 'l';
#endif
			optr = ptr = cur->ptrs[level];
		} else if (xfs_ialloc_split(cur, level, &nbno, &nrec, &ncur)) {
#ifdef XFSIDEBUG
			op = 's';
#endif
			buf = cur->bufs[level];
			block = xfs_buf_to_iblock(buf);
			xfs_ialloc_check_block(cur, block, level);
			ptr = cur->ptrs[level];
		} else
			return 0;
	}
	rp = XFS_IALLOC_REC_ADDR(block, 1, bl);
	if (level > 0) {
		pp = XFS_IALLOC_PTR_ADDR(block, 1, bl);
		for (i = block->numrecs; i >= ptr; i--) {
			rp[i] = rp[i - 1];
			xfs_ialloc_check_ptr(cur, pp[i - 1], level);
			pp[i] = pp[i - 1];
		}
		xfs_ialloc_check_ptr(cur, *bnop, level);
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
	first = offsetof(xfs_ialloc_block_t, numrecs);
	last = first + sizeof(block->numrecs) - 1;
	xfs_trans_log_buf(tp, buf, first, last);
	if (ptr < block->numrecs)
		xfs_ialloc_check_rec(rp + i, rp + i + 1);
	if (optr == 1)
		xfs_ialloc_updkey(cur, recp, level + 1);
	*bnop = nbno;
	xfs_ialloc_rcheck(cur);
	if (nbno != NULLAGBLOCK) {
		*recp = nrec;
		*curp = ncur;
	} else
		xfs_ialloc_kcheck(cur);
	return 1;
}

#ifdef XFSIDEBUG
/*
 * Debug routine to check key consistency.
 */
void
xfs_ialloc_kcheck(xfs_ialloc_cur_t *cur)
{
	buf_t *agbuf;
	xfs_aghdr_t *agp;
	xfs_agblock_t bno;
	int levels;

	agbuf = cur->agbuf;
	agp = xfs_buf_to_agp(agbuf);
	bno = agp->xfsag_iroot;
	levels = agp->xfsag_ilevels;
	ASSERT(levels == cur->nlevels);
	xfs_ialloc_kcheck_btree(cur, agp, bno, levels - 1, (xfs_ialloc_rec_t *)0);
}

void
xfs_ialloc_kcheck_btree(xfs_ialloc_cur_t *cur, xfs_aghdr_t *agp, xfs_agblock_t bno, int level, xfs_ialloc_rec_t *kp)
{
	int bl;
	xfs_ialloc_block_t *block;
	buf_t *buf;
	int i;
	xfs_mount_t *mp;
	xfs_agblock_t *pp;
	xfs_ialloc_rec_t *rp;
	xfs_sb_t *sbp;
	xfs_trans_t *tp;

	ASSERT(bno != NULLAGBLOCK);
	tp = cur->tp;
	mp = tp->t_mountp;
	sbp = mp->m_sb;
	bl = sbp->xfsb_blocklog;
	buf = xfs_ialloc_bread(tp, agp->xfsag_seqno, bno);
	block = xfs_buf_to_iblock(buf);
	xfs_ialloc_check_block(cur, block, level);
	rp = XFS_IALLOC_REC_ADDR(block, 1, bl);
	if (kp)
		ASSERT(rp->startinode == kp->startinode &&
		       rp->startblock == kp->startblock &&
		       rp->inodecount == kp->inodecount);
	if (level > 0) {
		pp = XFS_IALLOC_PTR_ADDR(block, 1, bl);
		if (*pp != NULLAGBLOCK) {
			for (i = 1; i <= block->numrecs; i++, pp++, rp++)
				xfs_ialloc_kcheck_btree(cur, agp, *pp, level - 1, rp);
		}
	}
}
#endif

/*
 * Point to the last record in the current block at the given level.
 */
int
xfs_ialloc_lastrec(xfs_ialloc_cur_t *cur, int level)
{
	xfs_ialloc_block_t *block;
	buf_t *buf;

	buf = cur->bufs[level];
	block = xfs_buf_to_iblock(buf);
	xfs_ialloc_check_block(cur, block, level);
	if (!block->numrecs)
		return 0;
	cur->ptrs[level] = block->numrecs;
	return 1;
}

/*
 * Lookup the record.  The cursor is made to point to it, based on dir.
 */
int
xfs_ialloc_lookup(xfs_ialloc_cur_t *cur, xfs_lookup_t dir)
{
	xfs_agblock_t agbno;
	buf_t *agbuf;
	xfs_agnumber_t agno;
	xfs_aghdr_t *agp;
	int bl;
	xfs_ialloc_block_t *block;
	buf_t *buf;
	daddr_t d;
	int diff;
	int high;
	int i;
	xfs_ialloc_rec_t *kbase;
	int keyno;
	xfs_ialloc_rec_t *kp;
	int level;
	int low;
	xfs_mount_t *mp;
	xfs_ialloc_rec_t *rp;
	xfs_sb_t *sbp;
	xfs_trans_t *tp;

	xfs_ialloc_rcheck(cur);
	xfs_ialloc_kcheck(cur);
	tp = cur->tp;
	mp = tp->t_mountp;
	agbuf = cur->agbuf;
	agp = xfs_buf_to_agp(agbuf);
	agno = agp->xfsag_seqno;
	agbno = agp->xfsag_iroot;
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
		block = xfs_buf_to_iblock(buf);
		xfs_ialloc_check_block(cur, block, level);
		if (diff == 0)
			keyno = 1;
		else {
			kbase = XFS_IALLOC_REC_ADDR(block, 1, bl);
			low = 1;
			if (!(high = block->numrecs)) {
				ASSERT(level == 0);
				cur->ptrs[0] = dir != XFS_LOOKUP_LE;
				return 0;
			}
			while (low <= high) {
				keyno = (low + high) >> 1;
				kp = kbase + keyno - 1;
				diff = kp->startinode - rp->startinode;
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
			agbno = *XFS_IALLOC_PTR_ADDR(block, keyno, bl);
			xfs_ialloc_check_ptr(cur, agbno, level);
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
			i = xfs_ialloc_increment(cur, 0);
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
xfs_ialloc_lookup_eq(xfs_ialloc_cur_t *cur, xfs_agino_t ino, xfs_agblock_t bno, xfs_agino_t len)
{
	cur->rec.startinode = ino;
	cur->rec.startblock = bno;
	cur->rec.inodecount = len;
	return xfs_ialloc_lookup(cur, XFS_LOOKUP_EQ);
}

int
xfs_ialloc_lookup_ge(xfs_ialloc_cur_t *cur, xfs_agino_t ino, xfs_agblock_t bno, xfs_agino_t len)
{
	cur->rec.startinode = ino;
	cur->rec.startblock = bno;
	cur->rec.inodecount = len;
	return xfs_ialloc_lookup(cur, XFS_LOOKUP_GE);
}

int
xfs_ialloc_lookup_le(xfs_ialloc_cur_t *cur, xfs_agino_t ino, xfs_agblock_t bno, xfs_agino_t len)
{
	cur->rec.startinode = ino;
	cur->rec.startblock = bno;
	cur->rec.inodecount = len;
	return xfs_ialloc_lookup(cur, XFS_LOOKUP_LE);
}

/*
 * Move 1 record left from cur/level if possible.
 * Update cur to reflect the new path.
 */
int
xfs_ialloc_lshift(xfs_ialloc_cur_t *cur, int level)
{
	int bl;
	int first;
	int i;
	int last;
	buf_t *lbuf;
	xfs_ialloc_block_t *left;
	int lev;
	xfs_agblock_t *lpp;
	xfs_ialloc_rec_t *lrp;
	xfs_mount_t *mp;
	int ptr;
	buf_t *rbuf;
	xfs_ialloc_block_t *right;
	xfs_agblock_t *rpp;
	xfs_ialloc_rec_t *rrp;
	xfs_sb_t *sbp;
	xfs_trans_t *tp;

	xfs_ialloc_rcheck(cur);
	rbuf = cur->bufs[level];
	right = xfs_buf_to_iblock(rbuf);
	xfs_ialloc_check_block(cur, right, level);
	if (right->leftsib == NULLAGBLOCK)
		return 0;
	if (cur->ptrs[level] <= 1)
		return 0;
	tp = cur->tp;
	mp = tp->t_mountp;
	sbp = mp->m_sb;
	lbuf = xfs_ialloc_bread(tp, cur->agno, right->leftsib);
	left = xfs_buf_to_iblock(lbuf);
	xfs_ialloc_check_block(cur, left, level);
	bl = sbp->xfsb_blocklog;
	if (left->numrecs == XFS_IALLOC_BLOCK_MAXRECS(bl, level))
		return 0;
	lrp = XFS_IALLOC_REC_ADDR(left, left->numrecs + 1, bl);
	rrp = XFS_IALLOC_REC_ADDR(right, 1, bl);
	*lrp = *rrp;
	first = (caddr_t)lrp - (caddr_t)left;
	last = first + sizeof(*lrp) - 1;
	xfs_trans_log_buf(tp, lbuf, first, last);
	if (level > 0) {
		lpp = XFS_IALLOC_PTR_ADDR(left, left->numrecs + 1, bl);
		rpp = XFS_IALLOC_PTR_ADDR(right, 1, bl);
		xfs_ialloc_check_ptr(cur, *rpp, level);
		*lpp = *rpp;
		first = (caddr_t)lpp - (caddr_t)left;
		last = first + sizeof(*lpp) - 1;
		xfs_trans_log_buf(tp, lbuf, first, last);
	}
	left->numrecs++;
	first = offsetof(xfs_ialloc_block_t, numrecs);
	last = first + sizeof(left->numrecs) - 1;
	xfs_trans_log_buf(tp, lbuf, first, last);
	xfs_ialloc_check_rec(lrp - 1, lrp);
	right->numrecs--;
	xfs_trans_log_buf(tp, rbuf, first, last);
	if (level > 0) {
		for (i = 0; i < right->numrecs; i++) {
			rrp[i] = rrp[i + 1];
			xfs_ialloc_check_ptr(cur, rpp[i + 1], level);
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
	xfs_ialloc_updkey(cur, rrp, level + 1);
	cur->ptrs[level]--;
	xfs_ialloc_rcheck(cur);
	return 1;
}

/*
 * Allocate a new root block, fill it in.
 */
int
xfs_ialloc_newroot(xfs_ialloc_cur_t *cur)
{
	buf_t *agbuf;
	xfs_aghdr_t *agp;
	int bl;
	xfs_ialloc_block_t *block;
	buf_t *buf;
	int first;
	int last;
	xfs_agblock_t lbno;
	buf_t *lbuf;
	xfs_ialloc_block_t *left;
	xfs_mount_t *mp;
	xfs_agblock_t nbno;
	buf_t *nbuf;
	xfs_ialloc_block_t *new;
	xfs_fsblock_t nfsbno;
	xfs_agblock_t nptr;
	xfs_agblock_t *pp;
	xfs_agblock_t rbno;
	buf_t *rbuf;
	xfs_ialloc_block_t *right;
	xfs_ialloc_rec_t *rp;
	xfs_sb_t *sbp;
	xfs_trans_t *tp;

	xfs_ialloc_rcheck(cur);
	ASSERT(cur->nlevels < XFS_IALLOC_MAXLEVELS);
	tp = cur->tp;
	mp = tp->t_mountp;
	sbp = mp->m_sb;
	bl = sbp->xfsb_blocklog;
	agbuf = cur->agbuf;
	agp = xfs_buf_to_agp(agbuf);
	nfsbno = xfs_agb_to_fsb(sbp, cur->agno, agp->xfsag_iroot);
	nfsbno = xfs_alloc_extent(tp, nfsbno, 1, XFS_ALLOCTYPE_NEAR_BNO);
	if (nfsbno == NULLFSBLOCK)
		return 0;
	nbno = xfs_fsb_to_agbno(sbp, nfsbno);
	nbuf = xfs_ialloc_bread(tp, cur->agno, nbno);
	new = xfs_buf_to_iblock(nbuf);
	agp->xfsag_iroot = nbno;
	agp->xfsag_ilevels++;
	first = offsetof(xfs_aghdr_t, xfsag_iroot);
	last = offsetof(xfs_aghdr_t, xfsag_ilevels) +
		sizeof(agp->xfsag_ilevels) - 1;
	xfs_trans_log_buf(tp, agbuf, first, last);
	buf = cur->bufs[cur->nlevels - 1];
	block = xfs_buf_to_iblock(buf);
	xfs_ialloc_check_block(cur, block, cur->nlevels - 1);
	if (block->rightsib != NULLAGBLOCK) {
		lbuf = buf;
		lbno = xfs_daddr_to_agbno(sbp, lbuf->b_blkno);
		left = block;
		rbno = left->rightsib;
		buf = rbuf = xfs_ialloc_bread(tp, cur->agno, rbno);
		right = xfs_buf_to_iblock(rbuf);
		xfs_ialloc_check_block(cur, right, cur->nlevels - 1);
		nptr = 1;
	} else {
		rbuf = buf;
		rbno = xfs_daddr_to_agbno(sbp, rbuf->b_blkno);
		right = block;
		lbno = right->leftsib;
		buf = lbuf = xfs_ialloc_bread(tp, cur->agno, lbno);
		left = xfs_buf_to_iblock(lbuf);
		xfs_ialloc_check_block(cur, left, cur->nlevels - 1);
		nptr = 2;
	}
	new->magic = XFS_IBT_MAGIC;
	new->level = cur->nlevels;
	new->numrecs = 2;
	new->leftsib = new->rightsib = NULLAGBLOCK;
	ASSERT(lbno != NULLAGBLOCK && rbno != NULLAGBLOCK);
	rp = XFS_IALLOC_REC_ADDR(new, 1, bl);
	rp[0] = *XFS_IALLOC_REC_ADDR(left, 1, bl);
	rp[1] = *XFS_IALLOC_REC_ADDR(right, 1, bl);
	first = offsetof(xfs_ialloc_block_t, magic);
	last = ((caddr_t)&rp[2] - (caddr_t)new) - 1;
	xfs_trans_log_buf(tp, nbuf, first, last);
	pp = XFS_IALLOC_PTR_ADDR(new, 1, bl);
	pp[0] = lbno;
	pp[1] = rbno;
	first = (caddr_t)pp - (caddr_t)new;
	last = ((caddr_t)&pp[2] - (caddr_t)new) - 1;
	xfs_trans_log_buf(tp, nbuf, first, last);
	cur->bufs[cur->nlevels] = nbuf;
	cur->ptrs[cur->nlevels] = nptr;
	cur->nlevels++;
	xfs_ialloc_rcheck(cur);
	xfs_ialloc_kcheck(cur);
	return 1;
}

#ifdef XFSIDEBUG
void
xfs_ialloc_rcheck(xfs_ialloc_cur_t *cur)
{
	buf_t *agbuf;
	xfs_aghdr_t *agp;
	xfs_agblock_t bno;
	int levels;

	agbuf = cur->agbuf;
	agp = xfs_buf_to_agp(agbuf);
	bno = agp->xfsag_iroot;
	levels = agp->xfsag_ilevels;
	xfs_ialloc_rcheck_btree(cur, agp, bno, levels);
}

void
xfs_ialloc_rcheck_btree(xfs_ialloc_cur_t *cur, xfs_aghdr_t *agp, xfs_agblock_t rbno, int levels)
{
	xfs_agnumber_t agno;
	xfs_agblock_t bno;
	xfs_agblock_t fbno;
	int level;
	xfs_ialloc_rec_t rec;

	agno = agp->xfsag_seqno;
	for (level = levels - 1, bno = rbno; level >= 0; level--, bno = fbno) {
		bno = xfs_ialloc_rcheck_btree_block(cur, agno, bno, &fbno, &rec, level);
		while (bno != NULLAGBLOCK) {
			ASSERT(bno < agp->xfsag_length);
			bno = xfs_ialloc_rcheck_btree_block(cur, agno, bno, (xfs_agblock_t *)0, &rec, level);
		}
	}
}

xfs_agblock_t
xfs_ialloc_rcheck_btree_block(xfs_ialloc_cur_t *cur, xfs_agnumber_t agno, xfs_agblock_t bno, xfs_agblock_t *fbno, xfs_ialloc_rec_t *rec, int level)
{
	int bl;
	xfs_ialloc_block_t *block;
	buf_t *buf;
	int i;
	xfs_mount_t *mp;
	xfs_agblock_t *pp;
	xfs_agblock_t rbno;
	xfs_ialloc_rec_t *rp;
	xfs_sb_t *sbp;
	xfs_trans_t *tp;

	tp = cur->tp;
	mp = tp->t_mountp;
	sbp = mp->m_sb;
	bl = sbp->xfsb_blocklog;
	buf = xfs_ialloc_bread(tp, agno, bno);
	block = xfs_buf_to_iblock(buf);
	xfs_ialloc_check_block(cur, block, level);
	if (fbno && block->numrecs) {
		if (level > 0)
			*fbno = *XFS_IALLOC_PTR_ADDR(block, 1, bl);
		else
			*fbno = NULLAGBLOCK;
	}
	rbno = block->rightsib;
	for (i = 1; i <= block->numrecs; i++) {
		rp = XFS_IALLOC_REC_ADDR(block, i, bl);
		if (i == 1 && !fbno)
			xfs_ialloc_check_rec(rec, rp);
		else if (i > 1) {
			xfs_ialloc_check_rec(rp - 1, rp);
			if (i == block->numrecs)
				*rec = *rp;
		}
		if (level > 0)
			xfs_ialloc_check_ptr(cur, *XFS_IALLOC_PTR_ADDR(block, i, bl), level);
	}
	return rbno;
}
#endif

/*
 * Move 1 record right from cur/level if possible.
 * Update cur to reflect the new path.
 */
int
xfs_ialloc_rshift(xfs_ialloc_cur_t *cur, int level)
{
	int bl;
	int first;
	int i;
	int last;
	buf_t *lbuf;
	xfs_ialloc_block_t *left;
	int lev;
	xfs_agblock_t *lpp;
	xfs_ialloc_rec_t *lrp;
	xfs_mount_t *mp;
	int ptr;
	buf_t *rbuf;
	xfs_ialloc_block_t *right;
	xfs_ialloc_rec_t *rp;
	xfs_agblock_t *rpp;
	xfs_ialloc_rec_t *rrp;
	xfs_sb_t *sbp;
	xfs_ialloc_cur_t *tcur;
	xfs_trans_t *tp;

	xfs_ialloc_rcheck(cur);
	lbuf = cur->bufs[level];
	left = xfs_buf_to_iblock(lbuf);
	xfs_ialloc_check_block(cur, left, level);
	if (left->rightsib == NULLAGBLOCK)
		return 0;
	if (cur->ptrs[level] >= left->numrecs)
		return 0;
	tp = cur->tp;
	mp = tp->t_mountp;
	sbp = mp->m_sb;
	rbuf = xfs_ialloc_bread(tp, cur->agno, left->rightsib);
	right = xfs_buf_to_iblock(rbuf);
	xfs_ialloc_check_block(cur, right, level);
	bl = sbp->xfsb_blocklog;
	if (right->numrecs == XFS_IALLOC_BLOCK_MAXRECS(bl, level))
		return 0;
	lrp = XFS_IALLOC_REC_ADDR(left, left->numrecs, bl);
	rrp = XFS_IALLOC_REC_ADDR(right, 1, bl);
	if (level > 0) {
		lpp = XFS_IALLOC_PTR_ADDR(left, left->numrecs, bl);
		rpp = XFS_IALLOC_PTR_ADDR(right, 1, bl);
		for (i = right->numrecs - 1; i >= 0; i--) {
			rrp[i + 1] = rrp[i];
			xfs_ialloc_check_ptr(cur, rpp[i], level);
			rpp[i + 1] = rpp[i];
		}
		xfs_ialloc_check_ptr(cur, *lpp, level);
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
	xfs_trans_log_buf(tp, rbuf, first, last);
	left->numrecs--;
	first = offsetof(xfs_ialloc_block_t, numrecs);
	last = first + sizeof(left->numrecs) - 1;
	xfs_trans_log_buf(tp, lbuf, first, last);
	right->numrecs++;
	xfs_ialloc_check_rec(rrp, rrp + 1);
	xfs_trans_log_buf(tp, rbuf, first, last);
	tcur = xfs_ialloc_dup_cursor(cur, __LINE__);
	xfs_ialloc_lastrec(tcur, level);
	xfs_ialloc_increment(tcur, level);
	xfs_ialloc_updkey(tcur, rrp, level + 1);
	xfs_ialloc_del_cursor(tcur);
	xfs_ialloc_rcheck(cur);
	return 1;
}

/*
 * Split cur/level block in half.
 * Return new block number and its first record (to be inserted into parent).
 */
int
xfs_ialloc_split(xfs_ialloc_cur_t *cur, int level, xfs_agblock_t *bnop, xfs_ialloc_rec_t *recp, xfs_ialloc_cur_t **curp)
{
	buf_t *agbuf;
	xfs_aghdr_t *agp;
	int bl;
	int first;
	int i;
	int last;
	xfs_agblock_t lbno;
	buf_t *lbuf;
	xfs_ialloc_block_t *left;
	xfs_fsblock_t lfsbno;
	xfs_agblock_t *lpp;
	xfs_ialloc_rec_t *lrp;
	xfs_mount_t *mp;
	xfs_agblock_t rbno;
	buf_t *rbuf;
	xfs_ialloc_rec_t rec;
	xfs_fsblock_t rfsbno;
	xfs_ialloc_block_t *right;
	xfs_agblock_t *rpp;
	xfs_ialloc_block_t *rrblock;
	buf_t *rrbuf;
	xfs_ialloc_rec_t *rrp;
	xfs_sb_t *sbp;
	xfs_trans_t *tp;

	xfs_ialloc_rcheck(cur);
	tp = cur->tp;
	mp = tp->t_mountp;
	sbp = mp->m_sb;
	bl = sbp->xfsb_blocklog;
	agbuf = cur->agbuf;
	agp = xfs_buf_to_agp(agbuf);
	lbuf = cur->bufs[level];
	lfsbno = xfs_daddr_to_fsb(sbp, lbuf->b_blkno);
	lbno = xfs_fsb_to_agbno(sbp, lfsbno);
	left = xfs_buf_to_iblock(lbuf);
	rfsbno = xfs_alloc_extent(tp, lfsbno, 1, XFS_ALLOCTYPE_NEAR_BNO);
	if (rfsbno == NULLFSBLOCK)
		return 0;
	rbno = xfs_fsb_to_agbno(sbp, rfsbno);
	rbuf = xfs_ialloc_bread(tp, cur->agno, rbno);
	right = xfs_buf_to_iblock(rbuf);
	xfs_ialloc_check_block(cur, left, level);
	right->magic = XFS_IBT_MAGIC;
	right->level = left->level;
	right->numrecs = left->numrecs / 2;
	if ((left->numrecs & 1) && cur->ptrs[level] <= right->numrecs + 1)
		right->numrecs++;
	i = left->numrecs - right->numrecs + 1;
	lrp = XFS_IALLOC_REC_ADDR(left, i, bl);
	rrp = XFS_IALLOC_REC_ADDR(right, 1, bl);
	if (level > 0) {
		lpp = XFS_IALLOC_PTR_ADDR(left, i, bl);
		rpp = XFS_IALLOC_PTR_ADDR(right, 1, bl);
		for (i = 0; i < right->numrecs; i++) {
			rrp[i] = lrp[i];
			xfs_ialloc_check_ptr(cur, lpp[i], level);
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
	left->numrecs -= right->numrecs;
	right->rightsib = left->rightsib;
	left->rightsib = rbno;
	right->leftsib = lbno;
	first = 0;
	last = offsetof(xfs_ialloc_block_t, rightsib) +
		sizeof(left->rightsib) - 1;
	xfs_trans_log_buf(tp, rbuf, first, last);
	first = offsetof(xfs_ialloc_block_t, numrecs);
	xfs_trans_log_buf(tp, lbuf, first, last);
	if (right->rightsib != NULLAGBLOCK) {
		rrbuf = xfs_ialloc_bread(tp, cur->agno, right->rightsib);
		rrblock = xfs_buf_to_iblock(rrbuf);
		xfs_ialloc_check_block(cur, rrblock, level);
		rrblock->leftsib = rbno;
		first = offsetof(xfs_ialloc_block_t, leftsib);
		last = first + sizeof(rrblock->leftsib) - 1;
		xfs_trans_log_buf(tp, rrbuf, first, last);
	}
	if (cur->ptrs[level] > left->numrecs + 1) {
		cur->bufs[level] = rbuf;
		cur->ptrs[level] -= left->numrecs;
	}
	if (level + 1 < cur->nlevels) {
		*curp = xfs_ialloc_dup_cursor(cur, __LINE__);
		(*curp)->ptrs[level + 1]++;
	}
	*bnop = rbno;
	*recp = rec;
	xfs_ialloc_rcheck(cur);
	return 1;
}

int
xfs_ialloc_update(xfs_ialloc_cur_t *cur, xfs_agino_t ino, xfs_agblock_t bno, xfs_agino_t len)
{
	int bl;
	xfs_ialloc_block_t *block;
	buf_t *buf;
	int first;
	int last;
	xfs_mount_t *mp;
	int ptr;
	xfs_ialloc_rec_t *rp;
	xfs_sb_t *sbp;
	xfs_trans_t *tp;

	xfs_ialloc_rcheck(cur);
	buf = cur->bufs[0];
	block = xfs_buf_to_iblock(buf);
	xfs_ialloc_check_block(cur, block, 0);
	ptr = cur->ptrs[0];
	tp = cur->tp;
	mp = tp->t_mountp;
	sbp = mp->m_sb;
	bl = sbp->xfsb_blocklog;
	rp = XFS_IALLOC_REC_ADDR(block, ptr, bl);
	rp->startinode = ino;
	rp->startblock = bno;
	rp->inodecount = len;
	first = (caddr_t)rp - (caddr_t)block;
	last = first + sizeof(*rp) - 1;
	xfs_trans_log_buf(tp, buf, first, last);
	if (ptr > 1)
		return 1;
	xfs_ialloc_updkey(cur, rp, 1);
	xfs_ialloc_rcheck(cur);
	xfs_ialloc_kcheck(cur);
	return 1;
}

void
xfs_ialloc_updkey(xfs_ialloc_cur_t *cur, xfs_ialloc_rec_t *kp, int level)
{
	int bl;
	xfs_ialloc_block_t *block;
	buf_t *buf;
	int first;
	int last;
	xfs_mount_t *mp;
	int ptr;
	xfs_ialloc_rec_t *rp;
	xfs_sb_t *sbp;
	xfs_trans_t *tp;

	xfs_ialloc_rcheck(cur);
	tp = cur->tp;
	mp = tp->t_mountp;
	sbp = mp->m_sb;
	bl = sbp->xfsb_blocklog;
	for (ptr = 1; ptr == 1 && level < cur->nlevels; level++) {
		buf = cur->bufs[level];
		block = xfs_buf_to_iblock(buf);
		xfs_ialloc_check_block(cur, block, level);
		ptr = cur->ptrs[level];
		rp = XFS_IALLOC_REC_ADDR(block, ptr, bl);
		*rp = *kp;
		first = (caddr_t)rp - (caddr_t)block;
		last = first + sizeof(*rp) - 1;
		xfs_trans_log_buf(tp, buf, first, last);
	}
	xfs_ialloc_rcheck(cur);
}

/*
 * Allocation group level functions.
 */

int 
xfs_ialloc_ag_alloc(xfs_ialloc_cur_t *cur)
{
	buf_t *agbuf;
	xfs_aghdr_t *agp;
	xfs_extlen_t bno;
	buf_t *fbuf;
	int first;
	xfs_inode_free_t *free;
	xfs_agblock_t gotbno;
	xfs_agino_t gotino;
	xfs_agino_t gotlen;
	int i;
	int inopb;
	int j;
	int last;
	xfs_extlen_t maxnewblocks;
	xfs_extlen_t minnewblocks;
	xfs_mount_t *mp;
	xfs_extlen_t newblocks;
	xfs_agblock_t newbno;
	xfs_fsblock_t newfsbno;
	xfs_agino_t newino;
	xfs_agino_t newlen;
	xfs_sb_t *sbp;
	xfs_trans_t *tp;
	xfs_agblock_t wantbno;

	tp = cur->tp;
	mp = tp->t_mountp;
	agbuf = cur->agbuf;
	agp = xfs_buf_to_agp(agbuf);
	newino = agp->xfsag_icount;
	sbp = mp->m_sb;
	inopb = sbp->xfsb_inopblock;
	minnewblocks = XFS_IALLOC_MIN_ALLOC(sbp, agp) / inopb;
	maxnewblocks = XFS_IALLOC_MAX_ALLOC(sbp, agp) / inopb;
	newlen = minnewblocks * inopb;
	xfs_ialloc_lookup_eq(cur, newino, NULLAGBLOCK, newlen);
	if (xfs_ialloc_decrement(cur, 0)) {
		xfs_ialloc_get_rec(cur, &gotino, &gotbno, &gotlen);
		ASSERT(gotino + gotlen == newino);
	} else {
		gotino = 0;
		gotbno = 0;
		gotlen = 0;
	}
	wantbno = newbno = gotbno + gotlen / inopb;
	newfsbno = xfs_agb_to_fsb(sbp, agp->xfsag_seqno, newbno);
	newfsbno = xfs_alloc_vextent(tp, newfsbno, minnewblocks, maxnewblocks, &newblocks, XFS_ALLOCTYPE_NEAR_BNO);
	if (newfsbno == NULLFSBLOCK)
		return 0;
	newlen = newblocks * inopb;
	newbno = xfs_fsb_to_agbno(sbp, newfsbno);
	if (wantbno == newbno)
		xfs_ialloc_update(cur, gotino, gotbno, gotlen + newlen);
	else {
		xfs_ialloc_increment(cur, 0);
		cur->rec.startblock = newbno;
		cur->rec.inodecount = newlen;
		xfs_ialloc_insert(cur);
	}
	for (j = newblocks - 1; j >= 0; j--) {
		fbuf = xfs_ialloc_bread(tp, agp->xfsag_seqno, newbno + j);
		for (i = inopb - 1; i >= 0; i--) {
			free = xfs_make_iptr(sbp, fbuf, i);
			free->i_mode = 0;
			free->i_next = agp->xfsag_iflist;
			first = (caddr_t)free - (caddr_t)xfs_buf_to_iblock(fbuf);
			last = first + sizeof(xfs_inode_free_t) - 1;
			xfs_trans_log_buf(tp, fbuf, first, last);
			agp->xfsag_iflist = newino + j * inopb + i;
		}
	}
	agp->xfsag_icount += newlen;
	agp->xfsag_ifcount += newlen;
	first = offsetof(xfs_aghdr_t, xfsag_icount);
	/* includes xfsag_iflist */
	last = offsetof(xfs_aghdr_t, xfsag_ifcount) +
		sizeof(agp->xfsag_ifcount) - 1;
	xfs_trans_log_buf(tp, agbuf, first, last);
	sbp->xfsb_icount += newlen;
	sbp->xfsb_ifree += newlen;
	first = offsetof(xfs_sb_t, xfsb_icount);
	last = offsetof(xfs_sb_t, xfsb_ifree) + sizeof(sbp->xfsb_ifree) - 1;
	xfs_mod_sb(tp, first, last);
	return 1;
}

int
xfs_ialloc_ag_locate(xfs_ialloc_cur_t *cur, xfs_agino_t agino, xfs_agblock_t *agbno, int *off)
{
	xfs_agblock_t bdiff;
	int i;
	xfs_agblock_t gotbno;
	xfs_agino_t gotino;
	xfs_agino_t gotlen;
	xfs_agino_t idiff;
	xfs_mount_t *mp;
	xfs_sb_t *sbp;
	xfs_trans_t *tp;

	tp = cur->tp;
	mp = tp->t_mountp;
	sbp = mp->m_sb;
	if (xfs_ialloc_lookup_le(cur, agino, 0, 0)) {
		xfs_ialloc_get_rec(cur, &gotino, &gotbno, &gotlen);
		if (agino < gotino + gotlen) {
			idiff = agino - gotino;
			bdiff = idiff / sbp->xfsb_inopblock;
			*agbno = gotbno + bdiff;
			*off = agino - (gotino + bdiff * sbp->xfsb_inopblock);
			i = 1;
		} else
			i = 0;
	} else
		i = 0;
	return i;
}

buf_t *
xfs_ialloc_ag_select(xfs_trans_t *tp, xfs_ino_t parent, int sameag, int mode)
{
	buf_t *agbuf;
	xfs_agnumber_t agcount;
	xfs_agnumber_t agno;
	int agoff;
	xfs_aghdr_t *agp;
	int doneleft;
	int doneright;
	xfs_mount_t *mp;
	int needspace;
	xfs_agnumber_t pagno;
	xfs_sb_t *sbp;

	needspace = S_ISDIR(mode) || S_ISREG(mode) || S_ISLNK(mode);
	pagno = xfs_ino_to_agno(parent);
	mp = tp->t_mountp;
	sbp = mp->m_sb;
	agcount = sbp->xfsb_agcount;
	if (pagno >= agcount)
		return (buf_t *)0;
	for (agoff = S_ISDIR(mode) != 0 && !sameag, doneleft = doneright = 0;
	     !doneleft || !doneright;
	     agoff = -agoff + (agoff >= 0)) {
		if ((agoff > 0 && doneright) || (agoff < 0 && doneleft))
			continue;
		if (agoff >= 0 && pagno + agoff >= agcount) {
			doneright = 1;
			continue;
		} else if (agoff < 0 && pagno < -agoff) {
			doneleft = 1;
			continue;
		}
		agno = pagno + agoff;
		agbuf = xfs_ialloc_bread(tp, agno, XFS_AGH_BLOCK);
		agp = xfs_buf_to_agp(agbuf);
		if (agp->xfsag_freeblks >= needspace + (agp->xfsag_ifcount == 0))
			return agbuf;
		if (sameag)
			break;
	}
	return (buf_t *)0;
}

/* 
 * File system level functions.
 */

/*
 * Really need incore inodes: take parent ptr in, produce result ptr
 * in a result parameter.
 */
xfs_ino_t
xfs_ialloc(xfs_trans_t *tp, xfs_ino_t parent, int sameag, int mode)
{
	xfs_agblock_t agbno;
	xfs_agnumber_t agcount;
	buf_t *agbuf;
	xfs_agino_t agino;
	xfs_agnumber_t agno;
	xfs_aghdr_t *agp;
	xfs_ialloc_cur_t *cur;
	buf_t *fbuf;
	int first;
	xfs_inode_free_t *free;
	xfs_ino_t ino;
	int last;
	xfs_mount_t *mp;
	int off;
	xfs_sb_t *sbp;
	xfs_agnumber_t tagno;

	agbuf = xfs_ialloc_ag_select(tp, parent, sameag, mode);
	if (!agbuf)
		return NULLFSINO;
	mp = tp->t_mountp;
	sbp = mp->m_sb;
	agcount = sbp->xfsb_agcount;
	agp = xfs_buf_to_agp(agbuf);
	agno = agp->xfsag_seqno;
	tagno = agno;
	cur = xfs_ialloc_init_cursor(tp, agbuf, tagno, __LINE__);
	while (agp->xfsag_ifcount == 0) {
		if (xfs_ialloc_ag_alloc(cur))
			break;
		xfs_ialloc_del_cursor(cur);
		if (sameag)
			return NULLFSINO;
		if (++tagno == agcount)
			tagno = 0;
		if (tagno == agno)
			return NULLFSINO;
		agbuf = xfs_ialloc_bread(tp, tagno, XFS_AGH_BLOCK);
		agp = xfs_buf_to_agp(agbuf);
		cur = xfs_ialloc_init_cursor(tp, agbuf, tagno, __LINE__);
	}
	agno = tagno;
	agino = agp->xfsag_iflist;
	xfs_ialloc_ag_locate(cur, agino, &agbno, &off);
	xfs_ialloc_del_cursor(cur);
	fbuf = xfs_ialloc_bread(tp, agno, agbno);
	free = xfs_make_iptr(sbp, fbuf, off);
	ASSERT(free->i_mode == 0);
	agp->xfsag_iflist = free->i_next;
	free->i_mode = mode;
	first = (caddr_t)&free->i_mode - (caddr_t)xfs_buf_to_iblock(fbuf);
	last = first + sizeof(free->i_mode) - 1;
	xfs_trans_log_buf(tp, fbuf, first, last);
	agp->xfsag_ifcount--;
	first = offsetof(xfs_aghdr_t, xfsag_ifcount);
	last = first + sizeof(agp->xfsag_ifcount) - 1;
	xfs_trans_log_buf(tp, agbuf, first, last);
	sbp->xfsb_ifree--;
	first = offsetof(xfs_sb_t, xfsb_ifree);
	last = first + sizeof(sbp->xfsb_ifree) - 1;
	xfs_mod_sb(tp, first, last);
	ino = xfs_agino_to_ino(agno, agino);
	return ino;
}

xfs_agino_t
xfs_ialloc_next_free(xfs_trans_t *tp, buf_t *agbuf, xfs_agino_t agino)
{
	xfs_agblock_t agbno;
	xfs_agnumber_t agno;
	xfs_aghdr_t *agp;
	xfs_ialloc_cur_t *cur;
	buf_t *fbuf;
	xfs_inode_free_t *free;
	xfs_mount_t *mp;
	int off;
	xfs_sb_t *sbp;

	agp = xfs_buf_to_agp(agbuf);
	agno = agp->xfsag_seqno;
	cur = xfs_ialloc_init_cursor(tp, agbuf, agno, __LINE__);
	xfs_ialloc_ag_locate(cur, agino, &agbno, &off);
	xfs_ialloc_del_cursor(cur);
	mp = tp->t_mountp;
	sbp = mp->m_sb;
	fbuf = xfs_ialloc_bread(tp, agno, agbno);
	free = xfs_make_iptr(sbp, fbuf, off);
	agino = free->i_next;
	return agino;
}

int
xfs_ifree(xfs_trans_t *tp, xfs_ino_t inode)
{
	xfs_agblock_t agbno;
	buf_t *agbuf;
	xfs_agino_t agino;
	xfs_agnumber_t agno;
	xfs_aghdr_t *agp;
	xfs_ialloc_cur_t *cur;
	buf_t *fbuf;
	int first;
	xfs_inode_free_t *free;
	int last;
	xfs_mount_t *mp;
	int off;
	xfs_sb_t *sbp;

	agno = xfs_ino_to_agno(inode);
	mp = tp->t_mountp;
	sbp = mp->m_sb;
	if (agno >= sbp->xfsb_agcount)
		return 0;
	agino = xfs_ino_to_agino(inode);
	agbuf = xfs_ialloc_bread(tp, agno, XFS_AGH_BLOCK);
	agp = xfs_buf_to_agp(agbuf);
	if (agino >= agp->xfsag_icount)
		return 0;
	cur = xfs_ialloc_init_cursor(tp, agbuf, agno, __LINE__);
	xfs_ialloc_ag_locate(cur, agino, &agbno, &off);
	xfs_ialloc_del_cursor(cur);
	fbuf = xfs_ialloc_bread(tp, agno, agbno);
	free = xfs_make_iptr(sbp, fbuf, off);
	ASSERT(free->i_mode);
	free->i_mode = 0;
	free->i_next = agp->xfsag_iflist;
	first = (caddr_t)free - (caddr_t)xfs_buf_to_iblock(fbuf);
	last = first + sizeof(*free) - 1;
	xfs_trans_log_buf(tp, fbuf, first, last);
	agp->xfsag_iflist = agino;
	agp->xfsag_ifcount++;
	first = offsetof(xfs_aghdr_t, xfsag_iflist);
	last = offsetof(xfs_aghdr_t, xfsag_ifcount) + 
		sizeof(agp->xfsag_ifcount) - 1;
	xfs_trans_log_buf(tp, agbuf, first, last);
	sbp->xfsb_ifree++;
	first = offsetof(xfs_sb_t, xfsb_ifree);
	last = first + sizeof(sbp->xfsb_ifree) - 1;
	xfs_mod_sb(tp, first, last);
	return 1;
}
