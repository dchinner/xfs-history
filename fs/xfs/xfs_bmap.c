#ident	"$Revision: 1.6 $"

#include <sys/param.h>
#include <sys/buf.h>
#include <sys/vnode.h>
#include <sys/uuid.h>
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
void xfs_bmbt_kcheck(xfs_btree_cur_t *);
void xfs_bmbt_kcheck_body(xfs_btree_cur_t *, xfs_aghdr_t *, xfs_btree_block_t *, int, xfs_bmbt_rec_t *);
void xfs_bmbt_kcheck_btree(xfs_btree_cur_t *, xfs_aghdr_t *, xfs_agblock_t, int, xfs_bmbt_rec_t *);
void xfs_bmbt_rcheck(xfs_btree_cur_t *);
xfs_agblock_t xfs_bmbt_rcheck_body(xfs_btree_cur_t *, xfs_btree_block_t *, xfs_agblock_t *, xfs_bmbt_rec_t *, int);
xfs_agblock_t xfs_bmbt_rcheck_btree(xfs_btree_cur_t *, xfs_agblock_t, xfs_agblock_t *, xfs_bmbt_rec_t *, int);
#else
#define	xfs_bmbt_kcheck(a)
#define	xfs_bmbt_rcheck(a)
#endif

int xfs_bmbt_decrement(xfs_btree_cur_t *, int);
int xfs_bmbt_delete(xfs_btree_cur_t *);
int xfs_bmbt_delrec(xfs_btree_cur_t *, int);
int xfs_bmbt_get_rec(xfs_btree_cur_t *, xfs_fsblock_t *, xfs_fsblock_t *, xfs_extlen_t *);
int xfs_bmbt_increment(xfs_btree_cur_t *, int);
int xfs_bmbt_insert(xfs_btree_cur_t *);
int xfs_bmbt_insrec(xfs_btree_cur_t *, int, xfs_agblock_t *, xfs_bmbt_rec_t *, xfs_btree_cur_t **);
void xfs_bmbt_log_ptrs(xfs_btree_cur_t *, buf_t *, int, int);
void xfs_bmbt_log_recs(xfs_btree_cur_t *, buf_t *, int, int);
int xfs_bmbt_lookup(xfs_btree_cur_t *, xfs_lookup_t);
int xfs_bmbt_lookup_eq(xfs_btree_cur_t *, xfs_fsblock_t, xfs_fsblock_t, xfs_extlen_t);
int xfs_bmbt_lookup_ge(xfs_btree_cur_t *, xfs_fsblock_t, xfs_fsblock_t, xfs_extlen_t);
int xfs_bmbt_lookup_le(xfs_btree_cur_t *, xfs_fsblock_t, xfs_fsblock_t, xfs_extlen_t);
int xfs_bmbt_lshift(xfs_btree_cur_t *, int);
int xfs_bmbt_rshift(xfs_btree_cur_t *, int);
int xfs_bmbt_split(xfs_btree_cur_t *, int, xfs_agblock_t *, xfs_bmbt_rec_t *, xfs_btree_cur_t **);
int xfs_bmbt_update(xfs_btree_cur_t *, xfs_fsblock_t, xfs_fsblock_t, xfs_extlen_t);
void xfs_bmbt_updkey(xfs_btree_cur_t *, xfs_bmbt_rec_t *, int);

/*
 * Internal functions.
 */

/*
 * Decrement cursor by one record at the level.
 * For nonzero levels the leaf-ward information is untouched.
 */
int
xfs_bmbt_decrement(xfs_btree_cur_t *cur, int level)
{
	xfs_agblock_t agbno;
	xfs_btree_block_t *block;
	buf_t *buf;
	int lev;
	xfs_mount_t *mp;
	xfs_trans_t *tp;

	if (--cur->bc_ptrs[level] > 0)
		return 1;
	block = xfs_bmbt_get_block(cur, level);
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
		block = xfs_bmbt_get_block(cur, lev);
		xfs_btree_check_block(cur, block, lev);
		agbno = *XFS_BMAP_PTR_IADDR(block, cur->bc_ptrs[lev], cur);
		buf = xfs_btree_bread(mp, tp, cur->bc_agno, agbno);
		xfs_btree_setbuf(cur, lev - 1, buf);
		block = xfs_buf_to_block(buf);
		xfs_btree_check_block(cur, block, lev - 1);
		cur->bc_ptrs[lev - 1] = block->bb_numrecs;
	}
	return 1;
}

/*
 * Delete the record pointed to by cur.
 */
int
xfs_bmbt_delete(xfs_btree_cur_t *cur)
{
	int i;
	int level;

	for (level = 0, i = 2; i == 2; level++)
		i = xfs_bmbt_delrec(cur, level);
	xfs_bmbt_kcheck(cur);
	return i;
}

/*
 * Delete record pointed to by cur/level.
 */
int
xfs_bmbt_delrec(xfs_btree_cur_t *cur, int level)
{
	buf_t *agbuf;
	xfs_aghdr_t *agp;
	xfs_btree_block_t *block;
	xfs_agblock_t bno;
	buf_t *buf;
	xfs_btree_block_t *cblock;
	xfs_agblock_t *cpp;
	xfs_bmbt_rec_t *crp;
	int first;
	int i;
	xfs_inode_t *ip;
	int last;
	xfs_agblock_t lbno;
	buf_t *lbuf;
	xfs_btree_block_t *left;
	xfs_agblock_t *lpp;
	int lrecs;
	xfs_bmbt_rec_t *lrp;
	xfs_mount_t *mp;
	xfs_agblock_t *pp;
	int ptr;
	xfs_agblock_t rbno;
	buf_t *rbuf;
	xfs_btree_block_t *right;
	xfs_bmbt_rec_t *rp;
	xfs_agblock_t *rpp;
	xfs_btree_block_t *rrblock;
	buf_t *rrbuf;
	int rrecs;
	xfs_bmbt_rec_t *rrp;
	xfs_sb_t *sbp;
	xfs_btree_cur_t *tcur;
	xfs_trans_t *tp;

	xfs_bmbt_rcheck(cur);
	tp = cur->bc_tp;
	mp = cur->bc_mp;
	sbp = &mp->m_sb;
	ptr = cur->bc_ptrs[level];
	if (ptr == 0)
		return 0;
	block = xfs_bmbt_get_block(cur, level);
	xfs_btree_check_block(cur, block, level);
	if (ptr > block->bb_numrecs)
		return 0;
	rp = XFS_BMAP_REC_IADDR(block, 1, cur);
	if (level > 0) {
		pp = XFS_BMAP_PTR_IADDR(block, 1, cur);
		for (i = ptr; i < block->bb_numrecs; i++) {
			rp[i - 1] = rp[i];
			xfs_btree_check_ptr(cur, pp[i], level);
			pp[i - 1] = pp[i];
		}
		if (ptr < i) {
			if (level < cur->bc_nlevels - 1)
				xfs_bmbt_log_ptrs(cur, cur->bc_bufs[level], ptr, i - 1);
			else {
				/* log inode fields */
				/* FIXME */
			}
		}
	} else {
		for (i = ptr; i < block->bb_numrecs; i++)
			rp[i - 1] = rp[i];
	}
	if (ptr < i) {
		if (level < cur->bc_nlevels - 1)
			xfs_bmbt_log_recs(cur, cur->bc_bufs[level], ptr, i - 1);
		else {
			/* log inode fields */
			/* FIXME */
		}
	}
	block->bb_numrecs--;
	if (level < cur->bc_nlevels - 1)
		xfs_btree_log_block(tp, cur->bc_bufs[level], XFS_BB_NUMRECS);
	else {
		/* log inode fields */
		/* FIXME */
	}
	agbuf = cur->bc_agbuf;
	agp = xfs_buf_to_agp(agbuf);
	if (level == cur->bc_nlevels - 1) {
		/* Only do this if the next level will fit. */
		/* Then the data must be copied up to the inode,
		 * instead of freeing the root you free the next level */
		buf = cur->bc_bufs[level - 1];
		cblock = xfs_buf_to_block(buf);
		ip = cur->bc_private.b.ip;
		if (block->bb_numrecs == 1 && level > 0 &&
		    cblock->bb_numrecs <= XFS_BMAP_BLOCK_DMAXRECS(level, cur)) {
			ASSERT(cblock->bb_leftsib == cblock->bb_rightsib == NULLAGBLOCK);
			if (i = (int)(cblock->bb_numrecs - XFS_BMAP_BLOCK_IMAXRECS(level, cur))) {
				xfs_iroot_realloc(ip, i);
				block = ip->i_broot;
			}
			*block = *cblock;
			rp = XFS_BMAP_REC_IADDR(block, 1, cur);
			crp = XFS_BMAP_REC_IADDR(cblock, 1, cur);
			bcopy((caddr_t)crp, (caddr_t)rp, block->bb_numrecs * (int)sizeof(*rp));
			pp = XFS_BMAP_PTR_IADDR(block, 1, cur);
			cpp = XFS_BMAP_PTR_IADDR(cblock, 1, cur);
			bcopy((caddr_t)cpp, (caddr_t)pp, block->bb_numrecs * (int)sizeof(*pp));
			xfs_free_extent(tp, xfs_daddr_to_fsb(sbp, buf->b_blkno), 1);
			/* FIXME: block's inode logging */
			xfs_btree_setbuf(cur, level - 1, 0);
			cur->bc_nlevels--;
		} else
			xfs_iroot_realloc(ip, -1);
		xfs_bmbt_rcheck(cur);
		return 1;
	}
	if (ptr == 1)
		xfs_bmbt_updkey(cur, rp, level + 1);
	xfs_bmbt_rcheck(cur);
	if (block->bb_numrecs >= XFS_BMAP_BLOCK_IMINRECS(level, cur))
		return 1;
	rbno = block->bb_rightsib;
	lbno = block->bb_leftsib;
	ASSERT(rbno != NULLAGBLOCK || lbno != NULLAGBLOCK);
	tcur = xfs_btree_dup_cursor(cur);
	bno = NULLAGBLOCK;
	buf = cur->bc_bufs[level];
	if (rbno != NULLAGBLOCK) {
		xfs_btree_lastrec(tcur, level);
		xfs_bmbt_increment(tcur, level);
		xfs_btree_lastrec(tcur, level);
		rbuf = tcur->bc_bufs[level];
		right = xfs_buf_to_block(rbuf);
		xfs_btree_check_block(cur, right, level);
		bno = right->bb_leftsib;
		if (right->bb_numrecs - 1 >= XFS_BMAP_BLOCK_IMINRECS(level, cur)) {
			if (xfs_bmbt_lshift(tcur, level)) {
				ASSERT(block->bb_numrecs >= XFS_BMAP_BLOCK_IMINRECS(level, tcur));
				xfs_btree_del_cursor(tcur);
				return 1;
			}
		}
		rrecs = right->bb_numrecs;
		if (lbno != NULLAGBLOCK) {
			xfs_btree_firstrec(tcur, level);
			xfs_bmbt_decrement(tcur, level);
		}
	}
	if (lbno != NULLAGBLOCK) {
		xfs_btree_firstrec(tcur, level);
		xfs_bmbt_decrement(tcur, level);	/* to last */
		xfs_btree_firstrec(tcur, level);
		lbuf = tcur->bc_bufs[level];
		left = xfs_buf_to_block(lbuf);
		xfs_btree_check_block(cur, left, level);
		bno = left->bb_rightsib;
		if (left->bb_numrecs - 1 >= XFS_BMAP_BLOCK_IMINRECS(level, cur)) {
			if (xfs_bmbt_rshift(tcur, level)) {
				ASSERT(block->bb_numrecs >= XFS_BMAP_BLOCK_IMINRECS(level, tcur));
				xfs_btree_del_cursor(tcur);
				cur->bc_ptrs[level]++;
				return 1;
			}
		}
		lrecs = left->bb_numrecs;
	}
	xfs_btree_del_cursor(tcur);
	ASSERT(bno != NULLAGBLOCK);
	if (lbno != NULLAGBLOCK &&
	    lrecs + block->bb_numrecs <= XFS_BMAP_BLOCK_IMAXRECS(level, cur)) {
		rbno = bno;
		right = block;
		rbuf = buf;
		lbuf = xfs_btree_bread(mp, tp, cur->bc_agno, lbno);
		left = xfs_buf_to_block(lbuf);
		xfs_btree_check_block(cur, left, level);
	} else if (rbno != NULLAGBLOCK &&
		   rrecs + block->bb_numrecs <= XFS_BMAP_BLOCK_IMAXRECS(level, cur)) {
		lbno = bno;
		left = block;
		lbuf = buf;
		rbuf = xfs_btree_bread(mp, tp, cur->bc_agno, rbno);
		right = xfs_buf_to_block(rbuf);
		xfs_btree_check_block(cur, right, level);
	} else {
		xfs_bmbt_rcheck(cur);
		return 1;
	}
	lrp = XFS_BMAP_REC_IADDR(left, left->bb_numrecs + 1, cur);
	rrp = XFS_BMAP_REC_IADDR(right, 1, cur);
	if (level > 0) {
		lpp = XFS_BMAP_PTR_IADDR(left, left->bb_numrecs + 1, cur);
		rpp = XFS_BMAP_PTR_IADDR(right, 1, cur);
		for (i = 0; i < right->bb_numrecs; i++) {
			lrp[i] = rrp[i];
			xfs_btree_check_ptr(cur, rpp[i], level);
			lpp[i] = rpp[i];
		}
	} else {
		for (i = 0; i < right->bb_numrecs; i++)
			lrp[i] = rrp[i];
	}
	if (buf != lbuf) {
		xfs_btree_setbuf(cur, level, lbuf);
		cur->bc_ptrs[level] += left->bb_numrecs;
	} else if (level + 1 < cur->bc_nlevels)
		xfs_bmbt_increment(cur, level + 1);
	left->bb_numrecs += right->bb_numrecs;
	left->bb_rightsib = right->bb_rightsib;
	if (left->bb_rightsib != NULLAGBLOCK) {
		rrbuf = xfs_btree_bread(mp, tp, cur->bc_agno, left->bb_rightsib);
		rrblock = xfs_buf_to_block(rrbuf);
		xfs_btree_check_block(cur, rrblock, level);
		rrblock->bb_leftsib = lbno;
		xfs_btree_log_block(tp, rrbuf, XFS_BB_LEFTSIB);
	}
	xfs_free_extent(tp, xfs_daddr_to_fsb(sbp, rbuf->b_blkno), 1);
	xfs_bmbt_rcheck(cur);
	return 2;
}

/* 
 * Get the data from the pointed-to record.
 */
int
xfs_bmbt_get_rec(xfs_btree_cur_t *cur, xfs_fsblock_t *off, xfs_fsblock_t *bno, xfs_extlen_t *len)
{
	xfs_btree_block_t *block;
	int ptr;
	xfs_bmbt_rec_t *rp;

	block = xfs_bmbt_get_block(cur, 0);
	ptr = cur->bc_ptrs[0];
	xfs_btree_check_block(cur, block, 0);
	if (ptr > block->bb_numrecs || ptr <= 0)
		return 0;
	rp = XFS_BMAP_REC_IADDR(block, ptr, cur);
	*off = xfs_bmbt_get_startoff(rp);
	*bno = xfs_bmbt_get_startblock(rp);
	*len = xfs_bmbt_get_blockcount(rp);
	return 1;
}

/*
 * Increment cursor by one record at the level.
 * For nonzero levels the leaf-ward information is untouched.
 */
int
xfs_bmbt_increment(xfs_btree_cur_t *cur, int level)
{
	xfs_agblock_t agbno;
	xfs_btree_block_t *block;
	buf_t *buf;
	int lev;
	xfs_mount_t *mp;
	xfs_trans_t *tp;

	block = xfs_bmbt_get_block(cur, level);
	xfs_btree_check_block(cur, block, level);
	if (++cur->bc_ptrs[level] <= block->bb_numrecs)
		return 1;
	if (block->bb_rightsib == NULLAGBLOCK)
		return 0;
	for (lev = level + 1; lev < cur->bc_nlevels; lev++) {
		block = xfs_bmbt_get_block(cur, lev);
		xfs_btree_check_block(cur, block, lev);
		if (++cur->bc_ptrs[lev] <= block->bb_numrecs)
			break;
	}
	if (lev == cur->bc_nlevels)
		return 0;
	tp = cur->bc_tp;
	mp = cur->bc_mp;
	for (; lev > level; lev--) {
		block = xfs_bmbt_get_block(cur, lev);
		xfs_btree_check_block(cur, block, lev);
		agbno = *XFS_BMAP_PTR_IADDR(block, cur->bc_ptrs[lev], cur);
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
xfs_bmbt_insert(xfs_btree_cur_t *cur)
{
	int i;
	int level;
	xfs_agblock_t nbno;
	xfs_btree_cur_t *ncur;
	xfs_bmbt_rec_t nrec;
	xfs_btree_cur_t *pcur;

	level = 0;
	nbno = NULLAGBLOCK;
	xfs_bmbt_set_startoff(&nrec, cur->bc_rec.b.br_startoff);
	xfs_bmbt_set_startblock(&nrec, cur->bc_rec.b.br_startblock);
	xfs_bmbt_set_blockcount(&nrec, cur->bc_rec.b.br_blockcount);
	ncur = (xfs_btree_cur_t *)0;
	pcur = cur;
	do {
		i = xfs_bmbt_insrec(pcur, level++, &nbno, &nrec, &ncur);
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
xfs_bmbt_insrec(xfs_btree_cur_t *cur, int level, xfs_agblock_t *bnop, xfs_bmbt_rec_t *recp, xfs_btree_cur_t **curp)
{
	xfs_btree_block_t *block;
	buf_t *buf;
	xfs_btree_block_t *cblock;
	xfs_agblock_t cbno;
	xfs_fsblock_t cfsbno;
	xfs_agblock_t *cpp;
	xfs_bmbt_rec_t *crp;
	int first;
	int i;
	xfs_inode_t *ip;
	int last;
	xfs_mount_t *mp;
	xfs_agblock_t nbno;
	xfs_btree_cur_t *ncur = (xfs_btree_cur_t *)0;
	xfs_bmbt_rec_t nrec;
	int optr;
	xfs_agblock_t *pp;
	int ptr;
	xfs_bmbt_rec_t *rp;
	xfs_sb_t *sbp;
	xfs_trans_t *tp;

	ASSERT(level < cur->bc_nlevels);
	xfs_bmbt_rcheck(cur);
	optr = ptr = cur->bc_ptrs[level];
	if (ptr == 0)
		return 0;
	block = xfs_bmbt_get_block(cur, level);
	xfs_btree_check_block(cur, block, level);
	if (ptr <= block->bb_numrecs) {
		rp = XFS_BMAP_REC_IADDR(block, ptr, cur);
		xfs_btree_check_rec(XFS_BTNUM_BMAP, recp, rp);
	}
	tp = cur->bc_tp;
	mp = cur->bc_mp;
	sbp = &mp->m_sb;
	nbno = NULLAGBLOCK;
	if (block->bb_numrecs == XFS_BMAP_BLOCK_IMAXRECS(level, cur)) {
		ip = cur->bc_private.b.ip;
		if (block->bb_numrecs < XFS_BMAP_BLOCK_DMAXRECS(level, cur)) {
			/*
			 * A root block, that can be made bigger.
			 */
			xfs_iroot_realloc(ip, 1);
		} else if (level == cur->bc_nlevels - 1) {
			/*
			 * Copy the root into a real block.
			 */
			cfsbno = xfs_agb_to_fsb(sbp, cur->bc_agno, cur->bc_private.b.ip->i_bno);
			cfsbno = xfs_alloc_extent(tp, cfsbno, 1, XFS_ALLOCTYPE_NEAR_BNO);
			if (cfsbno == NULLFSBLOCK)
				return 0;
			cbno = xfs_fsb_to_agbno(sbp, cfsbno);
			buf = xfs_btree_bread(mp, tp, cur->bc_agno, cbno);
			cblock = xfs_buf_to_block(buf);
			*cblock = *block;
			rp = XFS_BMAP_REC_IADDR(block, 1, cur);
			crp = XFS_BMAP_REC_IADDR(cblock, 1, cur);
			bcopy(rp, crp, block->bb_numrecs * (int)sizeof(*rp));
			last = (caddr_t)&crp[block->bb_numrecs] - (caddr_t)cblock;
			xfs_trans_log_buf(tp, buf, 0, last);
			pp = XFS_BMAP_PTR_IADDR(block, 1, cur);
			cpp = XFS_BMAP_PTR_IADDR(cblock, 1, cur);
			bcopy(pp, cpp, block->bb_numrecs * (int)sizeof(*pp));
			first = (caddr_t)pp - (caddr_t)cblock;
			last = (caddr_t)&cpp[block->bb_numrecs] - (caddr_t)cblock;
			xfs_trans_log_buf(tp, buf, first, last);
			xfs_iroot_realloc(ip, 1 - block->bb_numrecs);
			block = xfs_bmbt_get_block(cur, level);
			block->bb_level++;
			block->bb_numrecs = 1;
			*pp = cbno;
			/* FIXME: log these */
			xfs_btree_setbuf(cur, level, buf);
			cur->bc_nlevels++;
			cur->bc_ptrs[level + 1] = 1;
			block = cblock;
		} else {
			if (xfs_bmbt_rshift(cur, level)) {
				/* nothing */
			} else if (xfs_bmbt_lshift(cur, level)) {
				optr = ptr = cur->bc_ptrs[level];
			} else if (xfs_bmbt_split(cur, level, &nbno, &nrec, &ncur)) {
				block = xfs_bmbt_get_block(cur, level);
				xfs_btree_check_block(cur, block, level);
				ptr = cur->bc_ptrs[level];
			} else
				return 0;
		}
	}
	rp = XFS_BMAP_REC_IADDR(block, 1, cur);
	if (level > 0) {
		pp = XFS_BMAP_PTR_IADDR(block, 1, cur);
		for (i = block->bb_numrecs; i >= ptr; i--) {
			rp[i] = rp[i - 1];
			xfs_btree_check_ptr(cur, pp[i - 1], level);
			pp[i] = pp[i - 1];
		}
		xfs_btree_check_ptr(cur, *bnop, level);
		pp[i] = *bnop;
		if (level < cur->bc_nlevels - 1) {
			first = (caddr_t)&pp[i] - (caddr_t)block;
			last = (caddr_t)&pp[block->bb_numrecs] - (caddr_t)block +
				(int)sizeof(pp[i]) - 1;
			xfs_trans_log_buf(tp, cur->bc_bufs[level], first, last);
		} else {
			/* log inode fields */
			/* FIXME */
		}
	} else {
		for (i = block->bb_numrecs; i >= ptr; i--)
			rp[i] = rp[i - 1];
	}
	rp[i] = *recp;
	if (level < cur->bc_nlevels - 1) {
		first = (caddr_t)&rp[i] - (caddr_t)block;
		last = (caddr_t)&rp[block->bb_numrecs] - (caddr_t)block +
			(int)sizeof(rp[i]) - 1;
		xfs_trans_log_buf(tp, cur->bc_bufs[level], first, last);
	} else {
		/* log inode fields */
		/* FIXME */
	}
	block->bb_numrecs++;
	if (level < cur->bc_nlevels - 1) {
		first = offsetof(xfs_btree_block_t, bb_numrecs);
		last = first + (int)sizeof(block->bb_numrecs) - 1;
		xfs_trans_log_buf(tp, cur->bc_bufs[level], first, last);
	} else {
		/* log inode fields */
		/* FIXME */
	}
	if (ptr < block->bb_numrecs)
		xfs_btree_check_rec(XFS_BTNUM_BMAP, rp + i, rp + i + 1);
	if (optr == 1)
		xfs_bmbt_updkey(cur, recp, level + 1);
	*bnop = nbno;
	xfs_bmbt_rcheck(cur);
	if (nbno != NULLAGBLOCK) {
		*recp = nrec;
		*curp = ncur;
	} else
		xfs_bmbt_kcheck(cur);
	return 1;
}

#ifdef XFSDEBUG
/*
 * Debug routine to check key consistency.
 */
void
xfs_bmbt_kcheck(xfs_btree_cur_t *cur)
{
	buf_t *agbuf;
	xfs_aghdr_t *agp;
	xfs_btree_block_t *block;
	xfs_dinode_core_t *dip;
	xfs_inode_t *ip;
	int level;
	int levels;

	ip = cur->bc_private.b.ip;
	dip = &ip->i_d;
	ASSERT(dip->di_format == XFS_DINODE_FMT_BTREE);
	block = ip->i_broot;
	agbuf = cur->bc_agbuf;
	agp = xfs_buf_to_agp(agbuf);
	level = block->bb_level;
	levels = level + 1;
	ASSERT(levels == cur->bc_nlevels);
	xfs_bmbt_kcheck_body(cur, agp, block, level, (xfs_bmbt_rec_t *)0);
}

void
xfs_bmbt_kcheck_body(xfs_btree_cur_t *cur, xfs_aghdr_t *agp, xfs_btree_block_t *block, int level, xfs_bmbt_rec_t *kp)
{
	int i;
	xfs_agblock_t *pp;
	xfs_bmbt_rec_t *rp;

	xfs_btree_check_block(cur, block, level);
	rp = XFS_BMAP_REC_IADDR(block, 1, cur);
	if (kp)
		ASSERT(bcmp(rp, kp, sizeof(*kp)) == 1);
	if (level > 0) {
		pp = XFS_BMAP_PTR_IADDR(block, 1, cur);
		if (*pp != NULLAGBLOCK) {
			for (i = 1; i <= block->bb_numrecs; i++, pp++, rp++)
				xfs_bmbt_kcheck_btree(cur, agp, *pp, level - 1, rp);
		}
	}
}

void
xfs_bmbt_kcheck_btree(xfs_btree_cur_t *cur, xfs_aghdr_t *agp, xfs_agblock_t bno, int level, xfs_bmbt_rec_t *kp)
{
	xfs_btree_block_t *block;
	buf_t *buf;
	xfs_trans_t *tp;

	ASSERT(bno != NULLAGBLOCK);
	tp = cur->bc_tp;
	buf = xfs_btree_bread(cur->bc_mp, tp, cur->bc_agno, bno);
	block = xfs_buf_to_block(buf);
	xfs_bmbt_kcheck_body(cur, agp, block, level, kp);
	xfs_trans_brelse(tp, buf);
}
#endif

void
xfs_bmbt_log_ptrs(xfs_btree_cur_t *cur, buf_t *buf, int pfirst, int plast)
{
	xfs_btree_block_t *block;
	int first;
	int last;
	xfs_agblock_t *pp;

	block = xfs_buf_to_block(buf);
	pp = XFS_BMAP_PTR_DADDR(block, 1, cur);
	first = (caddr_t)&pp[pfirst - 1] - (caddr_t)block;
	last = ((caddr_t)&pp[plast] - 1) - (caddr_t)block;
	xfs_trans_log_buf(cur->bc_tp, buf, first, last);
}

void
xfs_bmbt_log_recs(xfs_btree_cur_t *cur, buf_t *buf, int rfirst, int rlast)
{
	xfs_btree_block_t *block;
	int first;
	int last;
	xfs_bmbt_rec_t *rp;

	block = xfs_buf_to_block(buf);
	rp = XFS_BMAP_REC_DADDR(block, 1, cur);
	first = (caddr_t)&rp[rfirst - 1] - (caddr_t)block;
	last = ((caddr_t)&rp[rlast] - 1) - (caddr_t)block;
	xfs_trans_log_buf(cur->bc_tp, buf, first, last);
}

/*
 * Lookup the record.  The cursor is made to point to it, based on dir.
 */
int
xfs_bmbt_lookup(xfs_btree_cur_t *cur, xfs_lookup_t dir)
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
	xfs_bmbt_rec_t *kbase;
	int keyno;
	xfs_bmbt_rec_t *kp;
	int level;
	int low;
	xfs_mount_t *mp;
	xfs_bmbt_irec_t *rp;
	xfs_sb_t *sbp;
	xfs_trans_t *tp;

	xfs_bmbt_rcheck(cur);
	xfs_bmbt_kcheck(cur);
	tp = cur->bc_tp;
	mp = cur->bc_mp;
	agbuf = cur->bc_agbuf;
	agp = xfs_buf_to_agp(agbuf);
	agno = agp->ag_seqno;
	sbp = &mp->m_sb;
	rp = &cur->bc_rec.b;
	for (level = cur->bc_nlevels - 1, diff = 1; level >= 0; level--) {
		if (level < cur->bc_nlevels - 1) {
			d = xfs_agb_to_daddr(sbp, agno, agbno);
			buf = cur->bc_bufs[level];
			if (buf && buf->b_blkno != d)
				buf = (buf_t *)0;
			if (!buf) {
				buf = xfs_trans_bread(tp, mp->m_dev, d, mp->m_bsize);
				xfs_btree_setbuf(cur, level, buf);
			}
		}
		block = xfs_bmbt_get_block(cur, level);
		xfs_btree_check_block(cur, block, level);
		if (diff == 0)
			keyno = 1;
		else {
			kbase = XFS_BMAP_REC_IADDR(block, 1, cur);
			low = 1;
			if (!(high = block->bb_numrecs)) {
				ASSERT(level == 0);
				cur->bc_ptrs[0] = dir != XFS_LOOKUP_LE;
				return 0;
			}
			while (low <= high) {
				keyno = (low + high) >> 1;
				kp = kbase + keyno - 1;
				diff = (int)(xfs_bmbt_get_startoff(kp) - rp->br_startoff);
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
			agbno = *XFS_BMAP_PTR_IADDR(block, keyno, cur);
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
			i = xfs_bmbt_increment(cur, 0);
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
xfs_bmbt_lookup_eq(xfs_btree_cur_t *cur, xfs_fsblock_t off, xfs_fsblock_t bno, xfs_extlen_t len)
{
	cur->bc_rec.b.br_startoff = off;
	cur->bc_rec.b.br_startblock = bno;
	cur->bc_rec.b.br_blockcount = len;
	return xfs_bmbt_lookup(cur, XFS_LOOKUP_EQ);
}

int
xfs_bmbt_lookup_ge(xfs_btree_cur_t *cur, xfs_fsblock_t off, xfs_fsblock_t bno, xfs_extlen_t len)
{
	cur->bc_rec.b.br_startoff = off;
	cur->bc_rec.b.br_startblock = bno;
	cur->bc_rec.b.br_blockcount = len;
	return xfs_bmbt_lookup(cur, XFS_LOOKUP_GE);
}

int
xfs_bmbt_lookup_le(xfs_btree_cur_t *cur, xfs_fsblock_t off, xfs_fsblock_t bno, xfs_extlen_t len)
{
	cur->bc_rec.b.br_startoff = off;
	cur->bc_rec.b.br_startblock = bno;
	cur->bc_rec.b.br_blockcount = len;
	return xfs_bmbt_lookup(cur, XFS_LOOKUP_LE);
}

/*
 * Move 1 record left from cur/level if possible.
 * Update cur to reflect the new path.
 */
int
xfs_bmbt_lshift(xfs_btree_cur_t *cur, int level)
{
	int first;
	int i;
	int last;
	buf_t *lbuf;
	xfs_btree_block_t *left;
	xfs_agblock_t *lpp;
	xfs_bmbt_rec_t *lrp;
	xfs_mount_t *mp;
	buf_t *rbuf;
	xfs_btree_block_t *right;
	xfs_agblock_t *rpp;
	xfs_bmbt_rec_t *rrp;
	xfs_trans_t *tp;

	if (level == cur->bc_nlevels - 1)
		return 0;
	xfs_bmbt_rcheck(cur);
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
	if (left->bb_numrecs == XFS_BMAP_BLOCK_IMAXRECS(level, cur))
		return 0;
	lrp = XFS_BMAP_REC_IADDR(left, left->bb_numrecs + 1, cur);
	rrp = XFS_BMAP_REC_IADDR(right, 1, cur);
	*lrp = *rrp;
	first = (caddr_t)lrp - (caddr_t)left;
	last = first + (int)sizeof(*lrp) - 1;
	xfs_trans_log_buf(tp, lbuf, first, last);
	if (level > 0) {
		lpp = XFS_BMAP_PTR_IADDR(left, left->bb_numrecs + 1, cur);
		rpp = XFS_BMAP_PTR_IADDR(right, 1, cur);
		xfs_btree_check_ptr(cur, *rpp, level);
		*lpp = *rpp;
		first = (caddr_t)lpp - (caddr_t)left;
		last = first + (int)sizeof(*lpp) - 1;
		xfs_trans_log_buf(tp, lbuf, first, last);
	}
	left->bb_numrecs++;
	first = offsetof(xfs_btree_block_t, bb_numrecs);
	last = first + (int)sizeof(left->bb_numrecs) - 1;
	xfs_trans_log_buf(tp, lbuf, first, last);
	xfs_btree_check_rec(XFS_BTNUM_BMAP, lrp - 1, lrp);
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
	xfs_bmbt_updkey(cur, rrp, level + 1);
	cur->bc_ptrs[level]--;
	xfs_bmbt_rcheck(cur);
	return 1;
}

#ifdef XFSDEBUG
void
xfs_bmbt_rcheck(xfs_btree_cur_t *cur)
{
	buf_t *agbuf;
	xfs_aghdr_t *agp;
	xfs_btree_block_t *block;
	xfs_agblock_t bno;
	xfs_dinode_core_t *dip;
	xfs_agblock_t fbno;
	xfs_inode_t *ip;
	int level;
	xfs_bmbt_rec_t rec;

	agbuf = cur->bc_agbuf;
	agp = xfs_buf_to_agp(agbuf);
	ip = cur->bc_private.b.ip;
	dip = &ip->i_d;
	ASSERT(dip->di_format == XFS_DINODE_FMT_BTREE);
	block = ip->i_broot;
	level = cur->bc_nlevels - 1;
	xfs_bmbt_rcheck_body(cur, block, &bno, &rec, level);
	for (level = block->bb_level - 1; level >= 0; level--, bno = fbno) {
		bno = xfs_bmbt_rcheck_btree(cur, bno, &fbno, &rec, level);
		while (bno != NULLAGBLOCK) {
			ASSERT(bno < agp->ag_length);
			bno = xfs_bmbt_rcheck_btree(cur, bno, (xfs_agblock_t *)0, &rec, level);
		}
	}
}

xfs_agblock_t
xfs_bmbt_rcheck_body(xfs_btree_cur_t *cur, xfs_btree_block_t *block, xfs_agblock_t *fbno, xfs_bmbt_rec_t *rec, int level)
{
	int i;
	xfs_agblock_t *pp;
	xfs_agblock_t rbno;
	xfs_bmbt_rec_t *rp;

	xfs_btree_check_block(cur, block, level);
	if (fbno && block->bb_numrecs) {
		if (level > 0)
			*fbno = *XFS_BMAP_PTR_IADDR(block, 1, cur);
		else
			*fbno = NULLAGBLOCK;
	}
	rbno = block->bb_rightsib;
	for (i = 1; i <= block->bb_numrecs; i++) {
		rp = XFS_BMAP_REC_IADDR(block, i, cur);
		if (i == 1 && !fbno)
			xfs_btree_check_rec(XFS_BTNUM_BMAP, rec, rp);
		else if (i > 1) {
			xfs_btree_check_rec(XFS_BTNUM_BMAP, rp - 1, rp);
			if (i == block->bb_numrecs)
				*rec = *rp;
		}
		if (level > 0) {
			pp = XFS_BMAP_PTR_IADDR(block, i, cur);
			xfs_btree_check_ptr(cur, *pp, level);
		}
	}
	return rbno;
}

xfs_agblock_t
xfs_bmbt_rcheck_btree(xfs_btree_cur_t *cur, xfs_agblock_t bno, xfs_agblock_t *fbno, xfs_bmbt_rec_t *rec, int level)
{
	xfs_btree_block_t *block;
	buf_t *buf;
	xfs_agblock_t rval;
	xfs_trans_t *tp;

	tp = cur->bc_tp;
	buf = xfs_btree_bread(cur->bc_mp, tp, cur->bc_agno, bno);
	block = xfs_buf_to_block(buf);
	rval = xfs_bmbt_rcheck_body(cur, block, fbno, rec, level);
	xfs_trans_brelse(tp, buf);
	return rval;
}
#endif

/*
 * Move 1 record right from cur/level if possible.
 * Update cur to reflect the new path.
 */
int
xfs_bmbt_rshift(xfs_btree_cur_t *cur, int level)
{
	int first;
	int i;
	int last;
	buf_t *lbuf;
	xfs_btree_block_t *left;
	xfs_agblock_t *lpp;
	xfs_bmbt_rec_t *lrp;
	xfs_mount_t *mp;
	buf_t *rbuf;
	xfs_btree_block_t *right;
	xfs_agblock_t *rpp;
	xfs_bmbt_rec_t *rrp;
	xfs_btree_cur_t *tcur;
	xfs_trans_t *tp;

	if (level == cur->bc_nlevels - 1)
		return 0;
	xfs_bmbt_rcheck(cur);
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
	if (right->bb_numrecs == XFS_BMAP_BLOCK_IMAXRECS(level, cur))
		return 0;
	lrp = XFS_BMAP_REC_IADDR(left, left->bb_numrecs, cur);
	rrp = XFS_BMAP_REC_IADDR(right, 1, cur);
	if (level > 0) {
		lpp = XFS_BMAP_PTR_IADDR(left, left->bb_numrecs, cur);
		rpp = XFS_BMAP_PTR_IADDR(right, 1, cur);
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
	xfs_trans_log_buf(tp, lbuf, first, last);
	left->bb_numrecs--;
	first = offsetof(xfs_btree_block_t, bb_numrecs);
	last = first + (int)sizeof(left->bb_numrecs) - 1;
	xfs_trans_log_buf(tp, lbuf, first, last);
	right->bb_numrecs++;
	xfs_btree_check_rec(XFS_BTNUM_BMAP, rrp, rrp + 1);
	xfs_trans_log_buf(tp, rbuf, first, last);
	tcur = xfs_btree_dup_cursor(cur);
	xfs_btree_lastrec(tcur, level);
	xfs_bmbt_increment(tcur, level);
	xfs_bmbt_updkey(tcur, rrp, level + 1);
	xfs_btree_del_cursor(tcur);
	xfs_bmbt_rcheck(cur);
	return 1;
}

/*
 * Split cur/level block in half.
 * Return new block number and its first record (to be inserted into parent).
 */
int
xfs_bmbt_split(xfs_btree_cur_t *cur, int level, xfs_agblock_t *bnop, xfs_bmbt_rec_t *recp, xfs_btree_cur_t **curp)
{
	buf_t *agbuf;
	xfs_aghdr_t *agp;
	daddr_t d;
	int first;
	int i;
	int last;
	xfs_agblock_t lbno;
	buf_t *lbuf;
	xfs_btree_block_t *left;
	xfs_fsblock_t lfsbno;
	xfs_agblock_t *lpp;
	xfs_bmbt_rec_t *lrp;
	xfs_mount_t *mp;
	xfs_agblock_t rbno;
	buf_t *rbuf;
	xfs_bmbt_rec_t rec;
	xfs_fsblock_t rfsbno;
	xfs_btree_block_t *right;
	xfs_agblock_t *rpp;
	xfs_btree_block_t *rrblock;
	buf_t *rrbuf;
	xfs_bmbt_rec_t *rrp;
	xfs_sb_t *sbp;
	xfs_trans_t *tp;

	xfs_bmbt_rcheck(cur);
	tp = cur->bc_tp;
	mp = cur->bc_mp;
	sbp = &mp->m_sb;
	agbuf = cur->bc_agbuf;
	agp = xfs_buf_to_agp(agbuf);
	lbuf = cur->bc_bufs[level];
	lfsbno = xfs_daddr_to_fsb(sbp, lbuf->b_blkno);
	left = xfs_buf_to_block(lbuf);
	rfsbno = xfs_alloc_extent(tp, lfsbno, 1, XFS_ALLOCTYPE_NEAR_BNO);
	if (rfsbno == NULLFSBLOCK)
		return 0;
	rbno = xfs_fsb_to_agbno(sbp, rfsbno);
	rbuf = xfs_btree_bread(mp, tp, cur->bc_agno, rbno);
	right = xfs_buf_to_block(rbuf);
	xfs_btree_check_block(cur, left, level);
	right->bb_magic = XFS_BMAP_MAGIC;
	right->bb_level = left->bb_level;
	right->bb_numrecs = (__uint16_t)(left->bb_numrecs / 2);
	if ((left->bb_numrecs & 1) && cur->bc_ptrs[level] <= right->bb_numrecs + 1)
		right->bb_numrecs++;
	i = left->bb_numrecs - right->bb_numrecs + 1;
	lrp = XFS_BMAP_REC_IADDR(left, i, cur);
	rrp = XFS_BMAP_REC_IADDR(right, 1, cur);
	if (level > 0) {
		lpp = XFS_BMAP_PTR_IADDR(left, i, cur);
		rpp = XFS_BMAP_PTR_IADDR(right, 1, cur);
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
	d = lbuf->b_blkno;
	lbno = xfs_daddr_to_agbno(sbp, d);
	left->bb_numrecs -= right->bb_numrecs;
	right->bb_rightsib = left->bb_rightsib;
	left->bb_rightsib = rbno;
	right->bb_leftsib = lbno;
	first = 0;
	last = offsetof(xfs_btree_block_t, bb_rightsib) +
		sizeof(left->bb_rightsib) - 1;
	xfs_trans_log_buf(tp, rbuf, first, last);
	first = offsetof(xfs_btree_block_t, bb_numrecs);
	xfs_trans_log_buf(tp, lbuf, first, last);
	if (right->bb_rightsib != NULLAGBLOCK) {
		rrbuf = xfs_btree_bread(mp, tp, cur->bc_agno, right->bb_rightsib);
		rrblock = xfs_buf_to_block(rrbuf);
		xfs_btree_check_block(cur, rrblock, level);
		rrblock->bb_leftsib = rbno;
		first = offsetof(xfs_btree_block_t, bb_leftsib);
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
	xfs_bmbt_rcheck(cur);
	return 1;
}

/*
 * Update the record to the passed values.
 */
int
xfs_bmbt_update(xfs_btree_cur_t *cur, xfs_fsblock_t off, xfs_fsblock_t bno, xfs_extlen_t len)
{
	buf_t *agbuf;
	xfs_aghdr_t *agp;
	xfs_btree_block_t *block;
	int first;
	int last;
	int ptr;
	xfs_bmbt_rec_t *rp;
	xfs_trans_t *tp;

	agbuf = cur->bc_agbuf;
	agp = xfs_buf_to_agp(agbuf);
	xfs_bmbt_rcheck(cur);
	block = xfs_bmbt_get_block(cur, 0);
	xfs_btree_check_block(cur, block, 0);
	ptr = cur->bc_ptrs[0];
	tp = cur->bc_tp;
	rp = XFS_BMAP_REC_IADDR(block, ptr, cur);
	xfs_bmbt_set_startoff(rp, off);
	xfs_bmbt_set_startblock(rp, bno);
	xfs_bmbt_set_blockcount(rp, len);
	if (cur->bc_nlevels > 1) {
		first = (caddr_t)rp - (caddr_t)block;
		last = first + (int)sizeof(*rp) - 1;
		xfs_trans_log_buf(tp, cur->bc_bufs[0], first, last);
	} else {
		/* log inode field */
		/* FIXME */
	}
	if (ptr > 1)
		return 1;
	xfs_bmbt_updkey(cur, rp, 1);
	xfs_bmbt_rcheck(cur);
	xfs_bmbt_kcheck(cur);
	return 1;
}

/*
 * Update keys for the record.
 */
void
xfs_bmbt_updkey(xfs_btree_cur_t *cur, xfs_bmbt_rec_t *kp, int level)
{
	xfs_btree_block_t *block;
	int first;
	int last;
	int ptr;
	xfs_bmbt_rec_t *rp;
	xfs_trans_t *tp;

	xfs_bmbt_rcheck(cur);
	tp = cur->bc_tp;
	for (ptr = 1; ptr == 1 && level < cur->bc_nlevels; level++) {
		block = xfs_bmbt_get_block(cur, level);
		xfs_btree_check_block(cur, block, level);
		ptr = cur->bc_ptrs[level];
		rp = XFS_BMAP_REC_IADDR(block, ptr, cur);
		*rp = *kp;
		if (level < cur->bc_nlevels - 1) {
			first = (caddr_t)rp - (caddr_t)block;
			last = first + (int)sizeof(*rp) - 1;
			xfs_trans_log_buf(tp, cur->bc_bufs[level], first, last);
		} else {
			/* log inode fields */
			/* FIXME */
		}
	}
	xfs_bmbt_rcheck(cur);
}

/*
 * Map blocks if format is btree.
 */
void
xfs_bmbt_map(xfs_trans_t *tp, xfs_inode_t *ip, xfs_fsblock_t bno, xfs_extlen_t len, int wr, xfs_bmbt_irec_t *mval, int *nmap)
{
	xfs_fsblock_t abno;
	buf_t *agbuf;
	xfs_agnumber_t agno;
	xfs_extlen_t alen;
	xfs_fsblock_t askbno;
	xfs_extlen_t asklen;
	xfs_btree_cur_t *cur;
	xfs_fsblock_t end;
	int eof;
	xfs_fsblock_t gotbno;
	xfs_extlen_t gotlen;
	xfs_fsblock_t gotoff;
	xfs_mount_t *mp;
	int n;
	xfs_fsblock_t prevbno = NULLFSBLOCK;
	xfs_extlen_t prevlen;
	xfs_fsblock_t prevoff = NULLFSBLOCK;
	xfs_sb_t *sbp;

	ASSERT(*nmap >= 1);
	ASSERT(ip->i_d.di_format == XFS_DINODE_FMT_BTREE);
	mp = ip->i_mount;
	sbp = &mp->m_sb;
	end = bno + len;
	agno = xfs_ino_to_agno(sbp, ip->i_ino);
	agbuf = xfs_btree_bread(mp, tp, agno, XFS_AGH_BLOCK);
	cur = xfs_btree_init_cursor(mp, tp, agbuf, agno, XFS_BTNUM_BMAP, ip);
	eof = 0;
	if (!xfs_bmbt_lookup_le(cur, bno, 0, 0))
		xfs_bmbt_increment(cur, 0);
	eof = !xfs_bmbt_get_rec(cur, &gotoff, &gotbno, &gotlen);
	if (gotoff + gotlen < bno) {
		prevoff = gotoff;
		prevbno = gotbno;
		prevlen = gotlen;
		xfs_bmbt_increment(cur, 0);
		eof = !xfs_bmbt_get_rec(cur, &gotoff, &gotbno, &gotlen);
	}
	n = 0;
	while (bno < end && n < *nmap) {
		if (eof || gotoff > bno) {
			if (wr) {
				if (eof) {
					asklen = len;
					if (prevbno == NULLFSBLOCK)
						askbno = xfs_agb_to_fsb(&mp->m_sb, agno, ip->i_bno);
					else
						askbno = prevbno + prevlen;
				} else {
					asklen = (xfs_extlen_t)(gotoff - bno);
					askbno = gotbno - asklen;
				}
				abno = xfs_alloc_vextent(tp, askbno, 1, asklen, &alen, XFS_ALLOCTYPE_NEAR_BNO);
				if (abno == NULLFSBLOCK) {
					abno = xfs_alloc_vextent(tp, askbno, 1, asklen, &alen, XFS_ALLOCTYPE_START_AG);
					if (abno == NULLFSBLOCK)
						break;
				}
				if (!eof && abno + alen == gotbno) {
					xfs_bmbt_update(cur, bno, abno, alen + gotlen);
					gotoff = bno;
					gotbno = abno;
					gotlen += alen;
					continue;
				}
				ip->i_d.di_nextents++;
				/* FIXME: log this */
				mval->br_startoff = bno;
				mval->br_startblock = abno;
				mval->br_blockcount = alen;
				cur->bc_rec.b = *mval;
				xfs_bmbt_insert(cur);
			} else if (eof)
				break;
			else {
				mval->br_startoff = bno;
				mval->br_startblock = NULLFSBLOCK;
				mval->br_blockcount = xfs_extlen_min(len, gotoff - bno);
			}
			bno += mval->br_blockcount;
			len -= mval->br_blockcount;
			mval++;
			n++;
			continue;
		}
		mval->br_startoff = bno;
		mval->br_startblock = gotbno + (bno - gotoff);
		mval->br_blockcount = xfs_extlen_min(len, gotlen - (bno - gotoff));
		bno += mval->br_blockcount;
		len -= mval->br_blockcount;
		mval++;
		n++;
		if (bno >= end || n >= *nmap)
			break;
		xfs_bmbt_increment(cur, 0);
		prevoff = gotoff;
		prevbno = gotbno;
		prevlen = gotlen;
		eof = !xfs_bmbt_get_rec(cur, &gotoff, &gotbno, &gotlen);
	}
	*nmap = n;
	xfs_btree_del_cursor(cur);
	xfs_trans_brelse(tp, agbuf);
}

void
xfs_bmex_map(xfs_trans_t *tp, xfs_inode_t *ip, xfs_fsblock_t bno, xfs_extlen_t len, int wr, xfs_bmbt_irec_t *mval, int *nmap)
/* ARGSUSED */
{
	/* FIX ME */
}

void
xfs_bmapi(xfs_trans_t *tp, xfs_inode_t *ip, xfs_fsblock_t bno, xfs_extlen_t len, int wr, xfs_bmbt_irec_t *mval, int *nmap)
{
	ASSERT(ip->i_d.di_format == XFS_DINODE_FMT_BTREE ||
	       ip->i_d.di_format == XFS_DINODE_FMT_EXTENTS);
	switch (ip->i_d.di_format) {
	case XFS_DINODE_FMT_BTREE:
		xfs_bmbt_map(tp, ip, bno, len, wr, mval, nmap);
		break;
	case XFS_DINODE_FMT_EXTENTS:
		xfs_bmex_map(tp, ip, bno, len, wr, mval, nmap);
		break;
	}
}
