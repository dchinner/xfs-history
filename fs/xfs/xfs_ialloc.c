#ident	"$Revision: 1.8 $"

#include <sys/param.h>
#include <sys/stat.h>		/* should really? */
#include <sys/buf.h>
#include <sys/vnode.h>
#include "xfs_types.h"
#include "xfs_inum.h"
#include "xfs.h"
#include "xfs_trans.h"
#include "xfs_sb.h"
#include "xfs_ag.h"
#include "xfs_mount.h"
#include "xfs_alloc.h"
#include "xfs_ialloc.h"
#include "xfs_bmap.h"
#include "xfs_btree.h"
#include "xfs_dinode.h"
#include "xfs_inode_item.h"
#include "xfs_inode.h"
#ifdef SIM
#include "sim.h"
#include <stddef.h>
#include <bstring.h>
#endif

/*
 * Prototypes for internal functions.
 */

#ifndef XFSDEBUG
#define NDEBUG
#endif
#include <assert.h>
#define	ASSERT(x)	assert(x)

#ifdef XFSDEBUG
void xfs_ialloc_kcheck(xfs_btree_cur_t *);
void xfs_ialloc_kcheck_btree(xfs_btree_cur_t *, xfs_aghdr_t *, xfs_agblock_t, int, xfs_ialloc_rec_t *);
void xfs_ialloc_rcheck(xfs_btree_cur_t *);
void xfs_ialloc_rcheck_btree(xfs_btree_cur_t *, xfs_aghdr_t *, xfs_agblock_t, int);
xfs_agblock_t xfs_ialloc_rcheck_btree_block(xfs_btree_cur_t *, xfs_agnumber_t, xfs_agblock_t, xfs_agblock_t *, xfs_ialloc_rec_t *, int);
#else
#define	xfs_ialloc_kcheck(a)
#define	xfs_ialloc_rcheck(a)
#endif

int xfs_ialloc_decrement(xfs_btree_cur_t *, int);
int xfs_ialloc_get_rec(xfs_btree_cur_t *, xfs_agino_t *, xfs_agblock_t *, xfs_agino_t *);
int xfs_ialloc_increment(xfs_btree_cur_t *, int);
int xfs_ialloc_insert(xfs_btree_cur_t *);
int xfs_ialloc_insrec(xfs_btree_cur_t *, int, xfs_agblock_t *, xfs_ialloc_rec_t *, xfs_btree_cur_t **);
int xfs_ialloc_lookup(xfs_btree_cur_t *, xfs_lookup_t);
int xfs_ialloc_lookup_eq(xfs_btree_cur_t *, xfs_agino_t, xfs_agblock_t, xfs_agino_t);
int xfs_ialloc_lookup_ge(xfs_btree_cur_t *, xfs_agino_t, xfs_agblock_t, xfs_agino_t);
int xfs_ialloc_lookup_le(xfs_btree_cur_t *, xfs_agino_t, xfs_agblock_t, xfs_agino_t);
int xfs_ialloc_lshift(xfs_btree_cur_t *, int);
int xfs_ialloc_newroot(xfs_btree_cur_t *);
int xfs_ialloc_rshift(xfs_btree_cur_t *, int);
int xfs_ialloc_split(xfs_btree_cur_t *, int, xfs_agblock_t *, xfs_ialloc_rec_t *, xfs_btree_cur_t **);
int xfs_ialloc_update(xfs_btree_cur_t *, xfs_agino_t, xfs_agblock_t, xfs_agino_t);
void xfs_ialloc_updkey(xfs_btree_cur_t *, xfs_ialloc_rec_t *, int);

/*
 * Prototypes for per-ag routines.
 */
int xfs_ialloc_ag_alloc(xfs_btree_cur_t *);
int xfs_ialloc_ag_locate(xfs_btree_cur_t *, xfs_agino_t, xfs_agblock_t *, int *);
buf_t *xfs_ialloc_ag_select(xfs_trans_t *, xfs_ino_t, int, mode_t);

/*
 * Internal functions.
 */

/*
 * Decrement cursor by one record at the level.
 * For nonzero levels the leaf-ward information is untouched.
 */
int
xfs_ialloc_decrement(xfs_btree_cur_t *cur, int level)
{
	xfs_agblock_t agbno;
	xfs_btree_block_t *block;
	buf_t *buf;
	int lev;
	xfs_mount_t *mp;
	xfs_trans_t *tp;

	if (--cur->bc_ptrs[level] > 0)
		return 1;
	buf = cur->bc_bufs[level];
	block = xfs_buf_to_block(buf);
	xfs_btree_check_block(cur, block, level);
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
		block = xfs_buf_to_block(buf);
		xfs_btree_check_block(cur, block, lev);
		agbno = *XFS_IALLOC_PTR_ADDR(block, cur->bc_ptrs[lev], cur);
		buf = xfs_btree_bread(mp, tp, cur->bc_agno, agbno);
		xfs_btree_setbuf(cur, lev - 1, buf);
		block = xfs_buf_to_block(buf);
		xfs_btree_check_block(cur, block, lev - 1);
		cur->bc_ptrs[lev - 1] = block->bb_numrecs;
	}
	return 1;
}

/* 
 * Get the data from the pointed-to record.
 */
int
xfs_ialloc_get_rec(xfs_btree_cur_t *cur, xfs_agino_t *ino, xfs_agblock_t *bno, xfs_agino_t *len)
{
	xfs_btree_block_t *block;
	buf_t *buf;
	int ptr;
	xfs_ialloc_rec_t *rec;

	buf = cur->bc_bufs[0];
	ptr = cur->bc_ptrs[0];
	block = xfs_buf_to_block(buf);
	xfs_btree_check_block(cur, block, 0);
	if (ptr > block->bb_numrecs || ptr <= 0)
		return 0;
	rec = XFS_IALLOC_REC_ADDR(block, ptr, cur);
	*ino = rec->ir_startinode;
	*bno = rec->ir_startblock;
	*len = rec->ir_inodecount;
	return 1;
}

/*
 * Increment cursor by one record at the level.
 * For nonzero levels the leaf-ward information is untouched.
 */
int
xfs_ialloc_increment(xfs_btree_cur_t *cur, int level)
{
	xfs_agblock_t agbno;
	xfs_btree_block_t *block;
	buf_t *buf;
	int lev;
	xfs_mount_t *mp;
	xfs_trans_t *tp;

	buf = cur->bc_bufs[level];
	block = xfs_buf_to_block(buf);
	xfs_btree_check_block(cur, block, level);
	if (++cur->bc_ptrs[level] <= block->bb_numrecs)
		return 1;
	if (block->bb_rightsib == NULLAGBLOCK)
		return 0;
	for (lev = level + 1; lev < cur->bc_nlevels; lev++) {
		buf = cur->bc_bufs[lev];
		block = xfs_buf_to_block(buf);
		xfs_btree_check_block(cur, block, lev);
		if (++cur->bc_ptrs[lev] <= block->bb_numrecs)
			break;
	}
	if (lev == cur->bc_nlevels)
		return 0;
	tp = cur->bc_tp;
	mp = cur->bc_mp;
	for (; lev > level; lev--) {
		buf = cur->bc_bufs[lev];
		block = xfs_buf_to_block(buf);
		xfs_btree_check_block(cur, block, lev);
		agbno = *XFS_IALLOC_PTR_ADDR(block, cur->bc_ptrs[lev], cur);
		buf = xfs_btree_bread(mp, tp, cur->bc_agno, agbno);
		xfs_btree_setbuf(cur, lev - 1, buf);
		cur->bc_ptrs[lev - 1] = 1;
	}
	return 1;
}

/*
 * Insert the current record at the point referenced by cur.
 */
int
xfs_ialloc_insert(xfs_btree_cur_t *cur)
{
	int i;
	int level;
	xfs_agblock_t nbno;
	xfs_btree_cur_t *ncur;
	xfs_ialloc_rec_t nrec;
	xfs_btree_cur_t *pcur;

	level = 0;
	nbno = NULLAGBLOCK;
	nrec = cur->bc_rec.i;
	ncur = (xfs_btree_cur_t *)0;
	pcur = cur;
	do {
		i = xfs_ialloc_insrec(pcur, level++, &nbno, &nrec, &ncur);
		if (pcur != cur && (ncur || nbno == NULLAGBLOCK)) {
			cur->bc_nlevels = pcur->bc_nlevels;
			xfs_btree_del_cursor(pcur);
		}
		if (ncur) {
			pcur = ncur;
			ncur = (xfs_btree_cur_t *)0;
		}
	} while (nbno != NULLAGBLOCK);
	return i;
}

/*
 * Insert one record/level.  Return information to the caller
 * allowing the next level up to proceed if necessary.
 */
int
xfs_ialloc_insrec(xfs_btree_cur_t *cur, int level, xfs_agblock_t *bnop, xfs_ialloc_rec_t *recp, xfs_btree_cur_t **curp)
{
	xfs_btree_block_t *block;
	buf_t *buf;
	int first;
	int i;
	int last;
	xfs_agblock_t nbno;
	xfs_btree_cur_t *ncur = (xfs_btree_cur_t *)0;
	xfs_ialloc_rec_t nrec;
	int optr;
	xfs_agblock_t *pp;
	int ptr;
	xfs_ialloc_rec_t *rp;
	xfs_trans_t *tp;

	if (level >= cur->bc_nlevels) {
		i = xfs_ialloc_newroot(cur);
		*bnop = NULLAGBLOCK;
		return i;
	}
	xfs_ialloc_rcheck(cur);
	optr = ptr = cur->bc_ptrs[level];
	if (ptr == 0)
		return 0;
	buf = cur->bc_bufs[level];
	block = xfs_buf_to_block(buf);
	xfs_btree_check_block(cur, block, level);
	tp = cur->bc_tp;
	if (ptr <= block->bb_numrecs) {
		rp = XFS_IALLOC_REC_ADDR(block, ptr, cur);
		xfs_btree_check_rec(XFS_BTNUM_IBT, recp, rp);
	}
	nbno = NULLAGBLOCK;
	if (block->bb_numrecs == XFS_IALLOC_BLOCK_MAXRECS(level, cur)) {
		if (xfs_ialloc_rshift(cur, level)) {
			/* nothing */
		} else if (xfs_ialloc_lshift(cur, level)) {
			optr = ptr = cur->bc_ptrs[level];
		} else if (xfs_ialloc_split(cur, level, &nbno, &nrec, &ncur)) {
			buf = cur->bc_bufs[level];
			block = xfs_buf_to_block(buf);
			xfs_btree_check_block(cur, block, level);
			ptr = cur->bc_ptrs[level];
		} else
			return 0;
	}
	rp = XFS_IALLOC_REC_ADDR(block, 1, cur);
	if (level > 0) {
		pp = XFS_IALLOC_PTR_ADDR(block, 1, cur);
		for (i = block->bb_numrecs; i >= ptr; i--) {
			rp[i] = rp[i - 1];
			xfs_btree_check_ptr(cur, pp[i - 1], level);
			pp[i] = pp[i - 1];
		}
		xfs_btree_check_ptr(cur, *bnop, level);
		pp[i] = *bnop;
		first = (caddr_t)&pp[i] - (caddr_t)block;
		last = (caddr_t)&pp[block->bb_numrecs] - (caddr_t)block +
			(int)sizeof(pp[i]) - 1;
		xfs_trans_log_buf(tp, buf, first, last);
	} else {
		for (i = block->bb_numrecs; i >= ptr; i--)
			rp[i] = rp[i - 1];
	}
	rp[i] = *recp;
	first = (caddr_t)&rp[i] - (caddr_t)block;
	last = (caddr_t)&rp[block->bb_numrecs] - (caddr_t)block +
		(int)sizeof(rp[i]) - 1;
	xfs_trans_log_buf(tp, buf, first, last);
	block->bb_numrecs++;
	first = (int)offsetof(xfs_btree_block_t, bb_numrecs);
	last = first + (int)sizeof(block->bb_numrecs) - 1;
	xfs_trans_log_buf(tp, buf, first, last);
	if (ptr < block->bb_numrecs)
		xfs_btree_check_rec(XFS_BTNUM_IBT, rp + i, rp + i + 1);
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

#ifdef XFSDEBUG
/*
 * Debug routine to check key consistency.
 */
void
xfs_ialloc_kcheck(xfs_btree_cur_t *cur)
{
	buf_t *agbuf;
	xfs_aghdr_t *agp;
	xfs_agblock_t bno;
	int levels;

	agbuf = cur->bc_agbuf;
	agp = xfs_buf_to_agp(agbuf);
	bno = agp->ag_iroot;
	levels = agp->ag_ilevels;
	ASSERT(levels == cur->bc_nlevels);
	xfs_ialloc_kcheck_btree(cur, agp, bno, levels - 1, (xfs_ialloc_rec_t *)0);
}

void
xfs_ialloc_kcheck_btree(xfs_btree_cur_t *cur, xfs_aghdr_t *agp, xfs_agblock_t bno, int level, xfs_ialloc_rec_t *kp)
{
	xfs_btree_block_t *block;
	buf_t *buf;
	int i;
	xfs_mount_t *mp;
	xfs_agblock_t *pp;
	xfs_ialloc_rec_t *rp;
	xfs_trans_t *tp;

	ASSERT(bno != NULLAGBLOCK);
	tp = cur->bc_tp;
	mp = cur->bc_mp;
	buf = xfs_btree_bread(mp, tp, agp->ag_seqno, bno);
	block = xfs_buf_to_block(buf);
	xfs_btree_check_block(cur, block, level);
	rp = XFS_IALLOC_REC_ADDR(block, 1, cur);
	if (kp)
		ASSERT(rp->ir_startinode == kp->ir_startinode &&
		       rp->ir_startblock == kp->ir_startblock &&
		       rp->ir_inodecount == kp->ir_inodecount);
	if (level > 0) {
		pp = XFS_IALLOC_PTR_ADDR(block, 1, cur);
		if (*pp != NULLAGBLOCK) {
			for (i = 1; i <= block->bb_numrecs; i++, pp++, rp++)
				xfs_ialloc_kcheck_btree(cur, agp, *pp, level - 1, rp);
		}
	}
	xfs_trans_brelse(tp, buf);
}
#endif

/*
 * Lookup the record.  The cursor is made to point to it, based on dir.
 */
int
xfs_ialloc_lookup(xfs_btree_cur_t *cur, xfs_lookup_t dir)
{
	xfs_agblock_t agbno;
	buf_t *agbuf;
	xfs_agnumber_t agno;
	xfs_aghdr_t *agp;
	xfs_btree_block_t *block;
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
	tp = cur->bc_tp;
	mp = cur->bc_mp;
	agbuf = cur->bc_agbuf;
	agp = xfs_buf_to_agp(agbuf);
	agno = agp->ag_seqno;
	agbno = agp->ag_iroot;
	sbp = &mp->m_sb;
	rp = &cur->bc_rec.i;
	for (level = cur->bc_nlevels - 1, diff = 1; level >= 0; level--) {
		d = xfs_agb_to_daddr(sbp, agno, agbno);
		buf = cur->bc_bufs[level];
		if (buf && buf->b_blkno != d)
			buf = (buf_t *)0;
		if (!buf) {
			buf = xfs_trans_bread(tp, mp->m_dev, d, mp->m_bsize);
			xfs_btree_setbuf(cur, level, buf);
		}
		block = xfs_buf_to_block(buf);
		xfs_btree_check_block(cur, block, level);
		if (diff == 0)
			keyno = 1;
		else {
			kbase = XFS_IALLOC_REC_ADDR(block, 1, cur);
			low = 1;
			if (!(high = block->bb_numrecs)) {
				ASSERT(level == 0);
				cur->bc_ptrs[0] = dir != XFS_LOOKUP_LE;
				return 0;
			}
			while (low <= high) {
				keyno = (low + high) >> 1;
				kp = kbase + keyno - 1;
				diff = (int)kp->ir_startinode - (int)rp->ir_startinode;
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
			agbno = *XFS_IALLOC_PTR_ADDR(block, keyno, cur);
			xfs_btree_check_ptr(cur, agbno, level);
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
			i = xfs_ialloc_increment(cur, 0);
			ASSERT(i == 1);
			return 1;
		}
	}
	else if (dir == XFS_LOOKUP_LE && diff > 0)
		keyno--;
	cur->bc_ptrs[0] = keyno;
	if (keyno == 0 || keyno > block->bb_numrecs)
		return 0;
	else
		return dir != XFS_LOOKUP_EQ || diff == 0;
}

int
xfs_ialloc_lookup_eq(xfs_btree_cur_t *cur, xfs_agino_t ino, xfs_agblock_t bno, xfs_agino_t len)
{
	cur->bc_rec.i.ir_startinode = ino;
	cur->bc_rec.i.ir_startblock = bno;
	cur->bc_rec.i.ir_inodecount = len;
	return xfs_ialloc_lookup(cur, XFS_LOOKUP_EQ);
}

int
xfs_ialloc_lookup_ge(xfs_btree_cur_t *cur, xfs_agino_t ino, xfs_agblock_t bno, xfs_agino_t len)
{
	cur->bc_rec.i.ir_startinode = ino;
	cur->bc_rec.i.ir_startblock = bno;
	cur->bc_rec.i.ir_inodecount = len;
	return xfs_ialloc_lookup(cur, XFS_LOOKUP_GE);
}

int
xfs_ialloc_lookup_le(xfs_btree_cur_t *cur, xfs_agino_t ino, xfs_agblock_t bno, xfs_agino_t len)
{
	cur->bc_rec.i.ir_startinode = ino;
	cur->bc_rec.i.ir_startblock = bno;
	cur->bc_rec.i.ir_inodecount = len;
	return xfs_ialloc_lookup(cur, XFS_LOOKUP_LE);
}

/*
 * Move 1 record left from cur/level if possible.
 * Update cur to reflect the new path.
 */
int
xfs_ialloc_lshift(xfs_btree_cur_t *cur, int level)
{
	int first;
	int i;
	int last;
	buf_t *lbuf;
	xfs_btree_block_t *left;
	xfs_agblock_t *lpp;
	xfs_ialloc_rec_t *lrp;
	xfs_mount_t *mp;
	buf_t *rbuf;
	xfs_btree_block_t *right;
	xfs_agblock_t *rpp;
	xfs_ialloc_rec_t *rrp;
	xfs_trans_t *tp;

	xfs_ialloc_rcheck(cur);
	rbuf = cur->bc_bufs[level];
	right = xfs_buf_to_block(rbuf);
	xfs_btree_check_block(cur, right, level);
	if (right->bb_leftsib == NULLAGBLOCK)
		return 0;
	if (cur->bc_ptrs[level] <= 1)
		return 0;
	tp = cur->bc_tp;
	mp = cur->bc_mp;
	lbuf = xfs_btree_bread(mp, tp, cur->bc_agno, right->bb_leftsib);
	left = xfs_buf_to_block(lbuf);
	xfs_btree_check_block(cur, left, level);
	if (left->bb_numrecs == XFS_IALLOC_BLOCK_MAXRECS(level, cur))
		return 0;
	lrp = XFS_IALLOC_REC_ADDR(left, left->bb_numrecs + 1, cur);
	rrp = XFS_IALLOC_REC_ADDR(right, 1, cur);
	*lrp = *rrp;
	first = (caddr_t)lrp - (caddr_t)left;
	last = first + (int)sizeof(*lrp) - 1;
	xfs_trans_log_buf(tp, lbuf, first, last);
	if (level > 0) {
		lpp = XFS_IALLOC_PTR_ADDR(left, left->bb_numrecs + 1, cur);
		rpp = XFS_IALLOC_PTR_ADDR(right, 1, cur);
		xfs_btree_check_ptr(cur, *rpp, level);
		*lpp = *rpp;
		first = (caddr_t)lpp - (caddr_t)left;
		last = first + (int)sizeof(*lpp) - 1;
		xfs_trans_log_buf(tp, lbuf, first, last);
	}
	left->bb_numrecs++;
	first = (int)offsetof(xfs_btree_block_t, bb_numrecs);
	last = first + (int)sizeof(left->bb_numrecs) - 1;
	xfs_trans_log_buf(tp, lbuf, first, last);
	xfs_btree_check_rec(XFS_BTNUM_IBT, lrp - 1, lrp);
	right->bb_numrecs--;
	xfs_trans_log_buf(tp, rbuf, first, last);
	if (level > 0) {
		for (i = 0; i < right->bb_numrecs; i++) {
			rrp[i] = rrp[i + 1];
			xfs_btree_check_ptr(cur, rpp[i + 1], level);
			rpp[i] = rpp[i + 1];
		}
		first = (caddr_t)rpp - (caddr_t)right;
		last = first + (int)sizeof(rpp[0]) * right->bb_numrecs - 1;
		xfs_trans_log_buf(tp, rbuf, first, last);
	} else {
		for (i = 0; i < right->bb_numrecs; i++)
			rrp[i] = rrp[i + 1];
	}
	first = (caddr_t)rrp - (caddr_t)right;
	last = first + (int)sizeof(rrp[0]) * right->bb_numrecs - 1;
	xfs_trans_log_buf(tp, rbuf, first, last);
	xfs_ialloc_updkey(cur, rrp, level + 1);
	cur->bc_ptrs[level]--;
	xfs_ialloc_rcheck(cur);
	return 1;
}

/*
 * Allocate a new root block, fill it in.
 */
int
xfs_ialloc_newroot(xfs_btree_cur_t *cur)
{
	buf_t *agbuf;
	xfs_aghdr_t *agp;
	xfs_btree_block_t *block;
	buf_t *buf;
	int first;
	int last;
	xfs_agblock_t lbno;
	buf_t *lbuf;
	xfs_btree_block_t *left;
	xfs_mount_t *mp;
	xfs_agblock_t nbno;
	buf_t *nbuf;
	xfs_btree_block_t *new;
	xfs_fsblock_t nfsbno;
	int nptr;
	xfs_agblock_t *pp;
	xfs_agblock_t rbno;
	buf_t *rbuf;
	xfs_btree_block_t *right;
	xfs_ialloc_rec_t *rp;
	xfs_sb_t *sbp;
	xfs_trans_t *tp;

	xfs_ialloc_rcheck(cur);
	ASSERT(cur->bc_nlevels < XFS_IALLOC_MAXLEVELS);
	tp = cur->bc_tp;
	mp = cur->bc_mp;
	sbp = &mp->m_sb;
	agbuf = cur->bc_agbuf;
	agp = xfs_buf_to_agp(agbuf);
	nfsbno = xfs_agb_to_fsb(sbp, cur->bc_agno, agp->ag_iroot);
	nfsbno = xfs_alloc_extent(tp, nfsbno, 1, XFS_ALLOCTYPE_NEAR_BNO);
	if (nfsbno == NULLFSBLOCK)
		return 0;
	nbno = xfs_fsb_to_agbno(sbp, nfsbno);
	nbuf = xfs_btree_bread(mp, tp, cur->bc_agno, nbno);
	new = xfs_buf_to_block(nbuf);
	agp->ag_iroot = nbno;
	agp->ag_ilevels++;
	first = (int)offsetof(xfs_aghdr_t, ag_iroot);
	last = (int)offsetof(xfs_aghdr_t, ag_ilevels) +
		(int)sizeof(agp->ag_ilevels) - 1;
	xfs_trans_log_buf(tp, agbuf, first, last);
	buf = cur->bc_bufs[cur->bc_nlevels - 1];
	block = xfs_buf_to_block(buf);
	xfs_btree_check_block(cur, block, cur->bc_nlevels - 1);
	if (block->bb_rightsib != NULLAGBLOCK) {
		lbuf = buf;
		lbno = xfs_daddr_to_agbno(sbp, lbuf->b_blkno);
		left = block;
		rbno = left->bb_rightsib;
		buf = rbuf = xfs_btree_bread(mp, tp, cur->bc_agno, rbno);
		right = xfs_buf_to_block(rbuf);
		xfs_btree_check_block(cur, right, cur->bc_nlevels - 1);
		nptr = 1;
	} else {
		rbuf = buf;
		rbno = xfs_daddr_to_agbno(sbp, rbuf->b_blkno);
		right = block;
		lbno = right->bb_leftsib;
		buf = lbuf = xfs_btree_bread(mp, tp, cur->bc_agno, lbno);
		left = xfs_buf_to_block(lbuf);
		xfs_btree_check_block(cur, left, cur->bc_nlevels - 1);
		nptr = 2;
	}
	new->bb_magic = XFS_IBT_MAGIC;
	new->bb_level = (__uint16_t)cur->bc_nlevels;
	new->bb_numrecs = 2;
	new->bb_leftsib = new->bb_rightsib = NULLAGBLOCK;
	ASSERT(lbno != NULLAGBLOCK && rbno != NULLAGBLOCK);
	rp = XFS_IALLOC_REC_ADDR(new, 1, cur);
	rp[0] = *XFS_IALLOC_REC_ADDR(left, 1, cur);
	rp[1] = *XFS_IALLOC_REC_ADDR(right, 1, cur);
	first = (int)offsetof(xfs_btree_block_t, bb_magic);
	last = ((caddr_t)&rp[2] - (caddr_t)new) - 1;
	xfs_trans_log_buf(tp, nbuf, first, last);
	pp = XFS_IALLOC_PTR_ADDR(new, 1, cur);
	pp[0] = lbno;
	pp[1] = rbno;
	first = (caddr_t)pp - (caddr_t)new;
	last = ((caddr_t)&pp[2] - (caddr_t)new) - 1;
	xfs_trans_log_buf(tp, nbuf, first, last);
	xfs_btree_setbuf(cur, cur->bc_nlevels, nbuf);
	cur->bc_ptrs[cur->bc_nlevels] = nptr;
	cur->bc_nlevels++;
	xfs_ialloc_rcheck(cur);
	xfs_ialloc_kcheck(cur);
	return 1;
}

#ifdef XFSDEBUG
void
xfs_ialloc_rcheck(xfs_btree_cur_t *cur)
{
	buf_t *agbuf;
	xfs_aghdr_t *agp;
	xfs_agblock_t bno;
	int levels;

	agbuf = cur->bc_agbuf;
	agp = xfs_buf_to_agp(agbuf);
	bno = agp->ag_iroot;
	levels = agp->ag_ilevels;
	xfs_ialloc_rcheck_btree(cur, agp, bno, levels);
}

void
xfs_ialloc_rcheck_btree(xfs_btree_cur_t *cur, xfs_aghdr_t *agp, xfs_agblock_t rbno, int levels)
{
	xfs_agnumber_t agno;
	xfs_agblock_t bno;
	xfs_agblock_t fbno;
	int level;
	xfs_ialloc_rec_t rec;

	agno = agp->ag_seqno;
	for (level = levels - 1, bno = rbno; level >= 0; level--, bno = fbno) {
		bno = xfs_ialloc_rcheck_btree_block(cur, agno, bno, &fbno, &rec, level);
		while (bno != NULLAGBLOCK) {
			ASSERT(bno < agp->ag_length);
			bno = xfs_ialloc_rcheck_btree_block(cur, agno, bno, (xfs_agblock_t *)0, &rec, level);
		}
	}
}

xfs_agblock_t
xfs_ialloc_rcheck_btree_block(xfs_btree_cur_t *cur, xfs_agnumber_t agno, xfs_agblock_t bno, xfs_agblock_t *fbno, xfs_ialloc_rec_t *rec, int level)
{
	xfs_btree_block_t *block;
	buf_t *buf;
	int i;
	xfs_mount_t *mp;
	xfs_agblock_t rbno;
	xfs_ialloc_rec_t *rp;
	xfs_trans_t *tp;

	tp = cur->bc_tp;
	mp = cur->bc_mp;
	buf = xfs_btree_bread(mp, tp, agno, bno);
	block = xfs_buf_to_block(buf);
	xfs_btree_check_block(cur, block, level);
	if (fbno && block->bb_numrecs) {
		if (level > 0)
			*fbno = *XFS_IALLOC_PTR_ADDR(block, 1, cur);
		else
			*fbno = NULLAGBLOCK;
	}
	rbno = block->bb_rightsib;
	for (i = 1; i <= block->bb_numrecs; i++) {
		rp = XFS_IALLOC_REC_ADDR(block, i, cur);
		if (i == 1 && !fbno)
			xfs_btree_check_rec(XFS_BTNUM_IBT, rec, rp);
		else if (i > 1) {
			xfs_btree_check_rec(XFS_BTNUM_IBT, rp - 1, rp);
			if (i == block->bb_numrecs)
				*rec = *rp;
		}
		if (level > 0)
			xfs_btree_check_ptr(cur, *XFS_IALLOC_PTR_ADDR(block, i, cur), level);
	}
	xfs_trans_brelse(tp, buf);
	return rbno;
}
#endif

/*
 * Move 1 record right from cur/level if possible.
 * Update cur to reflect the new path.
 */
int
xfs_ialloc_rshift(xfs_btree_cur_t *cur, int level)
{
	int first;
	int i;
	int last;
	buf_t *lbuf;
	xfs_btree_block_t *left;
	xfs_agblock_t *lpp;
	xfs_ialloc_rec_t *lrp;
	xfs_mount_t *mp;
	buf_t *rbuf;
	xfs_btree_block_t *right;
	xfs_agblock_t *rpp;
	xfs_ialloc_rec_t *rrp;
	xfs_btree_cur_t *tcur;
	xfs_trans_t *tp;

	xfs_ialloc_rcheck(cur);
	lbuf = cur->bc_bufs[level];
	left = xfs_buf_to_block(lbuf);
	xfs_btree_check_block(cur, left, level);
	if (left->bb_rightsib == NULLAGBLOCK)
		return 0;
	if (cur->bc_ptrs[level] >= left->bb_numrecs)
		return 0;
	tp = cur->bc_tp;
	mp = cur->bc_mp;
	rbuf = xfs_btree_bread(mp, tp, cur->bc_agno, left->bb_rightsib);
	right = xfs_buf_to_block(rbuf);
	xfs_btree_check_block(cur, right, level);
	if (right->bb_numrecs == XFS_IALLOC_BLOCK_MAXRECS(level, cur))
		return 0;
	lrp = XFS_IALLOC_REC_ADDR(left, left->bb_numrecs, cur);
	rrp = XFS_IALLOC_REC_ADDR(right, 1, cur);
	if (level > 0) {
		lpp = XFS_IALLOC_PTR_ADDR(left, left->bb_numrecs, cur);
		rpp = XFS_IALLOC_PTR_ADDR(right, 1, cur);
		for (i = right->bb_numrecs - 1; i >= 0; i--) {
			rrp[i + 1] = rrp[i];
			xfs_btree_check_ptr(cur, rpp[i], level);
			rpp[i + 1] = rpp[i];
		}
		xfs_btree_check_ptr(cur, *lpp, level);
		*rpp = *lpp;
		first = (caddr_t)rpp - (caddr_t)right;
		last = (caddr_t)&rpp[right->bb_numrecs] - (caddr_t)right +
			(int)sizeof(*rpp) - 1;
		xfs_trans_log_buf(tp, rbuf, first, last);
	} else {
		for (i = right->bb_numrecs - 1; i >= 0; i--)
			rrp[i + 1] = rrp[i];
	}
	*rrp = *lrp;
	first = (caddr_t)rrp - (caddr_t)right;
	last = (caddr_t)&rrp[right->bb_numrecs] - (caddr_t)right +
		(int)sizeof(*rrp) - 1;
	xfs_trans_log_buf(tp, rbuf, first, last);
	left->bb_numrecs--;
	first = (int)offsetof(xfs_btree_block_t, bb_numrecs);
	last = first + (int)sizeof(left->bb_numrecs) - 1;
	xfs_trans_log_buf(tp, lbuf, first, last);
	right->bb_numrecs++;
	xfs_btree_check_rec(XFS_BTNUM_IBT, rrp, rrp + 1);
	xfs_trans_log_buf(tp, rbuf, first, last);
	tcur = xfs_btree_dup_cursor(cur);
	xfs_btree_lastrec(tcur, level);
	xfs_ialloc_increment(tcur, level);
	xfs_ialloc_updkey(tcur, rrp, level + 1);
	xfs_btree_del_cursor(tcur);
	xfs_ialloc_rcheck(cur);
	return 1;
}

/*
 * Split cur/level block in half.
 * Return new block number and its first record (to be inserted into parent).
 */
int
xfs_ialloc_split(xfs_btree_cur_t *cur, int level, xfs_agblock_t *bnop, xfs_ialloc_rec_t *recp, xfs_btree_cur_t **curp)
{
	buf_t *agbuf;
	xfs_aghdr_t *agp;
	int first;
	int i;
	int last;
	xfs_agblock_t lbno;
	buf_t *lbuf;
	xfs_btree_block_t *left;
	xfs_fsblock_t lfsbno;
	xfs_agblock_t *lpp;
	xfs_ialloc_rec_t *lrp;
	xfs_mount_t *mp;
	xfs_agblock_t rbno;
	buf_t *rbuf;
	xfs_ialloc_rec_t rec;
	xfs_fsblock_t rfsbno;
	xfs_btree_block_t *right;
	xfs_agblock_t *rpp;
	xfs_btree_block_t *rrblock;
	buf_t *rrbuf;
	xfs_ialloc_rec_t *rrp;
	xfs_sb_t *sbp;
	xfs_trans_t *tp;

	xfs_ialloc_rcheck(cur);
	tp = cur->bc_tp;
	mp = cur->bc_mp;
	sbp = &mp->m_sb;
	agbuf = cur->bc_agbuf;
	agp = xfs_buf_to_agp(agbuf);
	lbuf = cur->bc_bufs[level];
	lfsbno = xfs_daddr_to_fsb(sbp, lbuf->b_blkno);
	lbno = xfs_fsb_to_agbno(sbp, lfsbno);
	left = xfs_buf_to_block(lbuf);
	rfsbno = xfs_alloc_extent(tp, lfsbno, 1, XFS_ALLOCTYPE_NEAR_BNO);
	if (rfsbno == NULLFSBLOCK)
		return 0;
	rbno = xfs_fsb_to_agbno(sbp, rfsbno);
	rbuf = xfs_btree_bread(mp, tp, cur->bc_agno, rbno);
	right = xfs_buf_to_block(rbuf);
	xfs_btree_check_block(cur, left, level);
	right->bb_magic = XFS_IBT_MAGIC;
	right->bb_level = left->bb_level;
	right->bb_numrecs = (__uint16_t)(left->bb_numrecs / 2);
	if ((left->bb_numrecs & 1) && cur->bc_ptrs[level] <= right->bb_numrecs + 1)
		right->bb_numrecs++;
	i = left->bb_numrecs - right->bb_numrecs + 1;
	lrp = XFS_IALLOC_REC_ADDR(left, i, cur);
	rrp = XFS_IALLOC_REC_ADDR(right, 1, cur);
	if (level > 0) {
		lpp = XFS_IALLOC_PTR_ADDR(left, i, cur);
		rpp = XFS_IALLOC_PTR_ADDR(right, 1, cur);
		for (i = 0; i < right->bb_numrecs; i++) {
			rrp[i] = lrp[i];
			xfs_btree_check_ptr(cur, lpp[i], level);
			rpp[i] = lpp[i];
		}
		first = (caddr_t)rpp - (caddr_t)right;
		last = ((caddr_t)&rpp[i] - (caddr_t)right) - 1;
		xfs_trans_log_buf(tp, rbuf, first, last);
	} else {
		for (i = 0; i < right->bb_numrecs; i++)
			rrp[i] = lrp[i];
	}
	first = (caddr_t)rrp - (caddr_t)right;
	last = ((caddr_t)&rrp[i] - (caddr_t)right) - 1;
	xfs_trans_log_buf(tp, rbuf, first, last);
	rec = *rrp;
	left->bb_numrecs -= right->bb_numrecs;
	right->bb_rightsib = left->bb_rightsib;
	left->bb_rightsib = rbno;
	right->bb_leftsib = lbno;
	first = 0;
	last = (int)offsetof(xfs_btree_block_t, bb_rightsib) +
		(int)sizeof(left->bb_rightsib) - 1;
	xfs_trans_log_buf(tp, rbuf, first, last);
	first = (int)offsetof(xfs_btree_block_t, bb_numrecs);
	xfs_trans_log_buf(tp, lbuf, first, last);
	if (right->bb_rightsib != NULLAGBLOCK) {
		rrbuf = xfs_btree_bread(mp, tp, cur->bc_agno, right->bb_rightsib);
		rrblock = xfs_buf_to_block(rrbuf);
		xfs_btree_check_block(cur, rrblock, level);
		rrblock->bb_leftsib = rbno;
		first = (int)offsetof(xfs_btree_block_t, bb_leftsib);
		last = first + (int)sizeof(rrblock->bb_leftsib) - 1;
		xfs_trans_log_buf(tp, rrbuf, first, last);
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
	*recp = rec;
	xfs_ialloc_rcheck(cur);
	return 1;
}

int
xfs_ialloc_update(xfs_btree_cur_t *cur, xfs_agino_t ino, xfs_agblock_t bno, xfs_agino_t len)
{
	xfs_btree_block_t *block;
	buf_t *buf;
	int first;
	int last;
	int ptr;
	xfs_ialloc_rec_t *rp;
	xfs_trans_t *tp;

	xfs_ialloc_rcheck(cur);
	buf = cur->bc_bufs[0];
	block = xfs_buf_to_block(buf);
	xfs_btree_check_block(cur, block, 0);
	ptr = cur->bc_ptrs[0];
	tp = cur->bc_tp;
	rp = XFS_IALLOC_REC_ADDR(block, ptr, cur);
	rp->ir_startinode = ino;
	rp->ir_startblock = bno;
	rp->ir_inodecount = len;
	first = (caddr_t)rp - (caddr_t)block;
	last = first + (int)sizeof(*rp) - 1;
	xfs_trans_log_buf(tp, buf, first, last);
	if (ptr > 1)
		return 1;
	xfs_ialloc_updkey(cur, rp, 1);
	xfs_ialloc_rcheck(cur);
	xfs_ialloc_kcheck(cur);
	return 1;
}

void
xfs_ialloc_updkey(xfs_btree_cur_t *cur, xfs_ialloc_rec_t *kp, int level)
{
	xfs_btree_block_t *block;
	buf_t *buf;
	int first;
	int last;
	int ptr;
	xfs_ialloc_rec_t *rp;
	xfs_trans_t *tp;

	xfs_ialloc_rcheck(cur);
	tp = cur->bc_tp;
	for (ptr = 1; ptr == 1 && level < cur->bc_nlevels; level++) {
		buf = cur->bc_bufs[level];
		block = xfs_buf_to_block(buf);
		xfs_btree_check_block(cur, block, level);
		ptr = cur->bc_ptrs[level];
		rp = XFS_IALLOC_REC_ADDR(block, ptr, cur);
		*rp = *kp;
		first = (caddr_t)rp - (caddr_t)block;
		last = first + (int)sizeof(*rp) - 1;
		xfs_trans_log_buf(tp, buf, first, last);
	}
	xfs_ialloc_rcheck(cur);
}

/*
 * Allocation group level functions.
 */

int 
xfs_ialloc_ag_alloc(xfs_btree_cur_t *cur)
{
	buf_t *agbuf;
	xfs_aghdr_t *agp;
	buf_t *fbuf;
	int first;
	int foffset;
	xfs_dinode_t *free;
	xfs_agblock_t gotbno;
	xfs_agino_t gotino;
	xfs_agino_t gotlen;
	int i;
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

	tp = cur->bc_tp;
	mp = cur->bc_mp;
	agbuf = cur->bc_agbuf;
	agp = xfs_buf_to_agp(agbuf);
	newino = agp->ag_icount;
	sbp = &mp->m_sb;
	minnewblocks = XFS_IALLOC_MIN_ALLOC(sbp, agp) >> sbp->sb_inopblog;
	maxnewblocks = XFS_IALLOC_MAX_ALLOC(sbp, agp) >> sbp->sb_inopblog;
	newlen = minnewblocks << sbp->sb_inopblog;
	xfs_ialloc_lookup_eq(cur, newino, NULLAGBLOCK, newlen);
	if (xfs_ialloc_decrement(cur, 0)) {
		xfs_ialloc_get_rec(cur, &gotino, &gotbno, &gotlen);
		ASSERT(gotino + gotlen == newino);
	} else {
		gotino = 0;
		gotbno = 0;
		gotlen = 0;
	}
	wantbno = newbno = gotbno + (gotlen >> sbp->sb_inopblog);
	newfsbno = xfs_agb_to_fsb(sbp, agp->ag_seqno, newbno);
	newfsbno = xfs_alloc_vextent(tp, newfsbno, minnewblocks, maxnewblocks, &newblocks, XFS_ALLOCTYPE_NEAR_BNO);
	if (newfsbno == NULLFSBLOCK)
		return 0;
	newlen = newblocks << sbp->sb_inopblog;
	newbno = xfs_fsb_to_agbno(sbp, newfsbno);
	if (wantbno == newbno)
		xfs_ialloc_update(cur, gotino, gotbno, gotlen + newlen);
	else {
		xfs_ialloc_increment(cur, 0);
		cur->bc_rec.i.ir_startblock = newbno;
		cur->bc_rec.i.ir_inodecount = newlen;
		xfs_ialloc_insert(cur);
	}
	for (j = (int)newblocks - 1; j >= 0; j--) {
		fbuf = xfs_btree_bread(mp, tp, agp->ag_seqno, newbno + j);
		for (i = sbp->sb_inopblock - 1; i >= 0; i--) {
			free = xfs_make_iptr(sbp, fbuf, i);
			foffset = (caddr_t)free - (caddr_t)xfs_buf_to_dinode(fbuf);
			free->di_core.di_magic = XFS_DINODE_MAGIC;
			free->di_core.di_mode = 0;
			free->di_core.di_version = XFS_DINODE_VERSION;
			free->di_core.di_format = XFS_DINODE_FMT_AGINO;
			first = foffset + (int)offsetof(xfs_dinode_t, di_core) +
				(int)offsetof(xfs_dinode_core_t, di_magic);
			last = foffset + (int)offsetof(xfs_dinode_t, di_core) +
				(int)offsetof(xfs_dinode_core_t, di_format) +
				(int)sizeof(free->di_core.di_format) - 1;
			xfs_trans_log_buf(tp, fbuf, first, last);
			free->di_u.di_next = agp->ag_iflist;
			first = foffset + (int)offsetof(xfs_dinode_t, di_u);
			last = first + (int)sizeof(free->di_u.di_next) - 1;
			xfs_trans_log_buf(tp, fbuf, first, last);
			agp->ag_iflist = newino + (j << sbp->sb_inopblog) + i;
		}
	}
	agp->ag_icount += newlen;
	agp->ag_ifcount += newlen;
	first = (int)offsetof(xfs_aghdr_t, ag_icount);
	/* includes ag_iflist */
	last = (int)offsetof(xfs_aghdr_t, ag_ifcount) + (int)sizeof(agp->ag_ifcount) - 1;
	xfs_trans_log_buf(tp, agbuf, first, last);
	sbp->sb_icount += newlen;
	sbp->sb_ifree += newlen;
	first = (int)offsetof(xfs_sb_t, sb_icount);
	last = (int)offsetof(xfs_sb_t, sb_ifree) + (int)sizeof(sbp->sb_ifree) - 1;
	xfs_mod_sb(tp, first, last);
	return 1;
}

/*
 * Locate the inode in the ag.
 * Returns agblock#, index of inode in block.
 */
int
xfs_ialloc_ag_locate(xfs_btree_cur_t *cur, xfs_agino_t agino, xfs_agblock_t *agbno, int *off)
{
	xfs_agblock_t bdiff;
	int i;
	xfs_agblock_t gotbno;
	xfs_agino_t gotino;
	xfs_agino_t gotlen;
	xfs_agino_t idiff;
	xfs_mount_t *mp;
	xfs_sb_t *sbp;

	mp = cur->bc_mp;
	sbp = &mp->m_sb;
	if (xfs_ialloc_lookup_le(cur, agino, 0, 0)) {
		xfs_ialloc_get_rec(cur, &gotino, &gotbno, &gotlen);
		if (agino < gotino + gotlen) {
			idiff = agino - gotino;
			bdiff = idiff >> sbp->sb_inopblog;
			*agbno = gotbno + bdiff;
			*off = (int)(agino - (gotino + (bdiff << sbp->sb_inopblog)));
			i = 1;
		} else
			i = 0;
	} else
		i = 0;
	return i;
}

buf_t *
xfs_ialloc_ag_select(xfs_trans_t *tp, xfs_ino_t parent, int sameag, mode_t mode)
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
	sbp = &mp->m_sb;
	agcount = sbp->sb_agcount;
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
		agbuf = xfs_btree_bread(mp, tp, agno, XFS_AGH_BLOCK);
		agp = xfs_buf_to_agp(agbuf);
		if (agp->ag_freeblks >= needspace + (agp->ag_ifcount == 0))
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
xfs_dialloc(xfs_trans_t *tp, xfs_ino_t parent, int sameag, mode_t mode)
{
	xfs_agblock_t agbno;
	xfs_agnumber_t agcount;
	buf_t *agbuf;
	xfs_agino_t agino;
	xfs_agnumber_t agno;
	xfs_aghdr_t *agp;
	xfs_btree_cur_t *cur;
	buf_t *fbuf;
	int first;
	xfs_dinode_t *free;
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
	sbp = &mp->m_sb;
	agcount = sbp->sb_agcount;
	agp = xfs_buf_to_agp(agbuf);
	agno = agp->ag_seqno;
	tagno = agno;
	cur = xfs_btree_init_cursor(mp, tp, agbuf, tagno, XFS_BTNUM_IBT, 0);
	while (agp->ag_ifcount == 0) {
		if (xfs_ialloc_ag_alloc(cur))
			break;
		xfs_btree_del_cursor(cur);
		if (sameag)
			return NULLFSINO;
		if (++tagno == agcount)
			tagno = 0;
		if (tagno == agno)
			return NULLFSINO;
		agbuf = xfs_btree_bread(mp, tp, tagno, XFS_AGH_BLOCK);
		agp = xfs_buf_to_agp(agbuf);
		cur = xfs_btree_init_cursor(mp, tp, agbuf, tagno, XFS_BTNUM_IBT, 0);
	}
	agno = tagno;
	agino = agp->ag_iflist;
	xfs_ialloc_ag_locate(cur, agino, &agbno, &off);
	xfs_btree_del_cursor(cur);
	fbuf = xfs_btree_bread(mp, tp, agno, agbno);
	free = xfs_make_iptr(sbp, fbuf, off);
	ASSERT(free->di_core.di_magic == XFS_DINODE_MAGIC);
	ASSERT(free->di_core.di_mode == 0);
	agp->ag_iflist = free->di_u.di_next;
	free->di_core.di_version = XFS_DINODE_VERSION;
	first = (caddr_t)&free->di_core.di_version -
		(caddr_t)xfs_buf_to_dinode(fbuf);
	last = first + (int)sizeof(free->di_core.di_version) - 1;
	xfs_trans_log_buf(tp, fbuf, first, last);
	agp->ag_ifcount--;
	first = (int)offsetof(xfs_aghdr_t, ag_ifcount);
	last = first + (int)sizeof(agp->ag_ifcount) - 1;
	xfs_trans_log_buf(tp, agbuf, first, last);
	sbp->sb_ifree--;
	first = (int)offsetof(xfs_sb_t, sb_ifree);
	last = first + (int)sizeof(sbp->sb_ifree) - 1;
	xfs_mod_sb(tp, first, last);
	ino = xfs_agino_to_ino(agno, agino);
	return ino;
}

xfs_agino_t
xfs_dialloc_next_free(xfs_mount_t *mp, xfs_trans_t *tp, buf_t *agbuf, xfs_agino_t agino)
{
	xfs_agblock_t agbno;
	xfs_agnumber_t agno;
	xfs_aghdr_t *agp;
	xfs_btree_cur_t *cur;
	buf_t *fbuf;
	xfs_dinode_t *free;
	int off;
	xfs_sb_t *sbp;

	agp = xfs_buf_to_agp(agbuf);
	agno = agp->ag_seqno;
	cur = xfs_btree_init_cursor(mp, tp, agbuf, agno, XFS_BTNUM_IBT, 0);
	xfs_ialloc_ag_locate(cur, agino, &agbno, &off);
	xfs_btree_del_cursor(cur);
	sbp = &mp->m_sb;
	fbuf = xfs_btree_bread(mp, tp, agno, agbno);
	free = xfs_make_iptr(sbp, fbuf, off);
	agino = free->di_u.di_next;
	xfs_trans_brelse(tp, fbuf);
	return agino;
}

int
xfs_difree(xfs_trans_t *tp, xfs_ino_t inode)
{
	xfs_agblock_t agbno;
	buf_t *agbuf;
	xfs_agino_t agino;
	xfs_agnumber_t agno;
	xfs_aghdr_t *agp;
	xfs_btree_cur_t *cur;
	buf_t *fbuf;
	int first;
	xfs_dinode_t *free;
	int last;
	xfs_mount_t *mp;
	int off;
	xfs_sb_t *sbp;

	agno = xfs_ino_to_agno(inode);
	mp = tp->t_mountp;
	sbp = &mp->m_sb;
	if (agno >= sbp->sb_agcount)
		return 0;
	agino = xfs_ino_to_agino(inode);
	agbuf = xfs_btree_bread(mp, tp, agno, XFS_AGH_BLOCK);
	agp = xfs_buf_to_agp(agbuf);
	if (agino >= agp->ag_icount)
		return 0;
	cur = xfs_btree_init_cursor(mp, tp, agbuf, agno, XFS_BTNUM_IBT, 0);
	xfs_ialloc_ag_locate(cur, agino, &agbno, &off);
	xfs_btree_del_cursor(cur);
	fbuf = xfs_btree_bread(mp, tp, agno, agbno);
	free = xfs_make_iptr(sbp, fbuf, off);
	ASSERT(free->di_core.di_magic == XFS_DINODE_MAGIC);
	ASSERT(free->di_core.di_mode);
	free->di_core.di_mode = 0;
	free->di_core.di_format = XFS_DINODE_FMT_AGINO;
	first = (caddr_t)&free->di_core.di_mode -
		(caddr_t)xfs_buf_to_dinode(fbuf);
	last = (caddr_t)&free->di_core.di_format -
		(caddr_t)xfs_buf_to_dinode(fbuf) +
		(int)sizeof(free->di_core.di_format) - 1;
	/* FIXME: inode fields ??? */
	xfs_trans_log_buf(tp, fbuf, first, last);
	free->di_u.di_next = agp->ag_iflist;
	first = (caddr_t)&free->di_u - (caddr_t)xfs_buf_to_dinode(fbuf);
	last = first + (int)sizeof(free->di_u.di_next) - 1;
	xfs_trans_log_buf(tp, fbuf, first, last);
	agp->ag_iflist = agino;
	agp->ag_ifcount++;
	first = (int)offsetof(xfs_aghdr_t, ag_iflist);
	last = (int)offsetof(xfs_aghdr_t, ag_ifcount) + (int)sizeof(agp->ag_ifcount) - 1;
	xfs_trans_log_buf(tp, agbuf, first, last);
	sbp->sb_ifree++;
	first = (int)offsetof(xfs_sb_t, sb_ifree);
	last = first + (int)sizeof(sbp->sb_ifree) - 1;
	xfs_mod_sb(tp, first, last);
	return 1;
}

int
xfs_dilocate(xfs_mount_t *mp, xfs_trans_t *tp, xfs_ino_t ino, xfs_fsblock_t *bno, int *off)
{
	xfs_agblock_t agbno;
	buf_t *agbuf;
	xfs_agino_t agino;
	xfs_agnumber_t agno;
	xfs_btree_cur_t *cur;
	int i;

	agno = xfs_ino_to_agno(ino);
	agino = xfs_ino_to_agino(ino);
	agbuf = xfs_btree_bread(mp, tp, agno, XFS_AGH_BLOCK);
	cur = xfs_btree_init_cursor(mp, tp, agbuf, agno, XFS_BTNUM_IBT, 0);
	i = xfs_ialloc_ag_locate(cur, agino, &agbno, off);
	xfs_btree_del_cursor(cur);
	xfs_trans_brelse(tp, agbuf);
	if (!i)
		return 0;
	*bno = xfs_agb_to_fsb(&mp->m_sb, agno, agbno);
	return 1;
}
