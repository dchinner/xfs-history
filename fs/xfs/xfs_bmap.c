#ident	"$Revision$"

#include <sys/param.h>
#include <sys/buf.h>
#include <sys/vnode.h>
#include <sys/uuid.h>
#include <sys/debug.h>
#ifdef SIM
#include <bstring.h>
#else
#include <sys/systm.h>
#endif
#include "xfs_types.h"
#include "xfs_inum.h"
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
#endif

/*
 * Prototypes for internal functions.
 */

#ifdef XFSDEBUG
STATIC void xfs_bmbt_kcheck(xfs_btree_cur_t *);
STATIC void xfs_bmbt_kcheck_body(xfs_btree_cur_t *, xfs_aghdr_t *, xfs_btree_block_t *, int, xfs_bmbt_rec_t *);
STATIC void xfs_bmbt_kcheck_btree(xfs_btree_cur_t *, xfs_aghdr_t *, xfs_agblock_t, int, xfs_bmbt_rec_t *);
STATIC void xfs_bmbt_rcheck(xfs_btree_cur_t *);
STATIC xfs_agblock_t xfs_bmbt_rcheck_body(xfs_btree_cur_t *, xfs_btree_block_t *, xfs_agblock_t *, xfs_bmbt_rec_t *, int);
STATIC xfs_agblock_t xfs_bmbt_rcheck_btree(xfs_btree_cur_t *, xfs_agblock_t, xfs_agblock_t *, xfs_bmbt_rec_t *, int);
#else
#define	xfs_bmbt_kcheck(a)
#define	xfs_bmbt_rcheck(a)
#endif

STATIC int xfs_bmbt_decrement(xfs_btree_cur_t *, int);
STATIC int xfs_bmbt_delete(xfs_btree_cur_t *);
STATIC int xfs_bmbt_delrec(xfs_btree_cur_t *, int);
STATIC int xfs_bmbt_get_rec(xfs_btree_cur_t *, xfs_fsblock_t *, xfs_fsblock_t *, xfs_extlen_t *);
STATIC int xfs_bmbt_increment(xfs_btree_cur_t *, int);
STATIC int xfs_bmbt_insert(xfs_btree_cur_t *);
STATIC int xfs_bmbt_insrec(xfs_btree_cur_t *, int, xfs_agblock_t *, xfs_bmbt_rec_t *, xfs_btree_cur_t **);
STATIC void xfs_bmbt_log_block(xfs_btree_cur_t *, buf_t *, int);
STATIC void xfs_bmbt_log_ptrs(xfs_btree_cur_t *, buf_t *, int, int);
STATIC void xfs_bmbt_log_recs(xfs_btree_cur_t *, buf_t *, int, int);
STATIC int xfs_bmbt_lookup(xfs_btree_cur_t *, xfs_lookup_t);
STATIC int xfs_bmbt_lookup_eq(xfs_btree_cur_t *, xfs_fsblock_t, xfs_fsblock_t, xfs_extlen_t);
STATIC int xfs_bmbt_lookup_ge(xfs_btree_cur_t *, xfs_fsblock_t, xfs_fsblock_t, xfs_extlen_t);
STATIC int xfs_bmbt_lookup_le(xfs_btree_cur_t *, xfs_fsblock_t, xfs_fsblock_t, xfs_extlen_t);
STATIC int xfs_bmbt_lshift(xfs_btree_cur_t *, int);
STATIC int xfs_bmbt_rshift(xfs_btree_cur_t *, int);
STATIC int xfs_bmbt_split(xfs_btree_cur_t *, int, xfs_agblock_t *, xfs_bmbt_rec_t *, xfs_btree_cur_t **);
STATIC int xfs_bmbt_update(xfs_btree_cur_t *, xfs_fsblock_t, xfs_fsblock_t, xfs_extlen_t);
STATIC void xfs_bmbt_updkey(xfs_btree_cur_t *, xfs_bmbt_rec_t *, int);

/*
 * Internal functions.
 */

/*
 * Decrement cursor by one record at the level.
 * For nonzero levels the leaf-ward information is untouched.
 */
STATIC int
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
	block = xfs_bmbt_get_block(cur, level, &buf);
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
		block = xfs_bmbt_get_block(cur, lev, &buf);
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
STATIC int
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
STATIC int
xfs_bmbt_delrec(xfs_btree_cur_t *cur, int level)
{
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
	block = xfs_bmbt_get_block(cur, level, &buf);
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
		if (ptr < i)
			xfs_bmbt_log_ptrs(cur, buf, ptr, i - 1);
	} else {
		for (i = ptr; i < block->bb_numrecs; i++)
			rp[i - 1] = rp[i];
	}
	if (ptr < i)
		xfs_bmbt_log_recs(cur, buf, ptr, i - 1);
	block->bb_numrecs--;
	xfs_bmbt_log_block(cur, buf, XFS_BB_NUMRECS);
	/*
	 * We just did a join at the previous level.
	 * Make the cursor point to the good (left) key.
	 */
	if (level > 0)
		xfs_bmbt_decrement(cur, level);
	if (level == cur->bc_nlevels - 1) {
		/*
		 * Only do this if the next level will fit.
		 * Then the data must be copied up to the inode,
		 * instead of freeing the root you free the next level
		 */
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
			xfs_trans_log_inode(tp, ip, XFS_ILOG_BROOT);
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
		xfs_bmbt_log_ptrs(cur, lbuf, left->bb_numrecs + 1, left->bb_numrecs + right->bb_numrecs);
	} else {
		for (i = 0; i < right->bb_numrecs; i++)
			lrp[i] = rrp[i];
	}
	xfs_bmbt_log_recs(cur, lbuf, left->bb_numrecs + 1, left->bb_numrecs + right->bb_numrecs);
	if (buf != lbuf) {
		xfs_btree_setbuf(cur, level, lbuf);
		cur->bc_ptrs[level] += left->bb_numrecs;
	} else if (level + 1 < cur->bc_nlevels)
		xfs_bmbt_increment(cur, level + 1);
	left->bb_numrecs += right->bb_numrecs;
	left->bb_rightsib = right->bb_rightsib;
	xfs_btree_log_block(tp, lbuf, XFS_BB_RIGHTSIB | XFS_BB_NUMRECS);
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
STATIC int
xfs_bmbt_get_rec(xfs_btree_cur_t *cur, xfs_fsblock_t *off, xfs_fsblock_t *bno, xfs_extlen_t *len)
{
	xfs_btree_block_t *block;
	buf_t *buf;
	int ptr;
	xfs_bmbt_rec_t *rp;

	block = xfs_bmbt_get_block(cur, 0, &buf);
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
STATIC int
xfs_bmbt_increment(xfs_btree_cur_t *cur, int level)
{
	xfs_agblock_t agbno;
	xfs_btree_block_t *block;
	buf_t *buf;
	int lev;
	xfs_mount_t *mp;
	xfs_trans_t *tp;

	block = xfs_bmbt_get_block(cur, level, &buf);
	xfs_btree_check_block(cur, block, level);
	if (++cur->bc_ptrs[level] <= block->bb_numrecs)
		return 1;
	if (block->bb_rightsib == NULLAGBLOCK)
		return 0;
	for (lev = level + 1; lev < cur->bc_nlevels; lev++) {
		block = xfs_bmbt_get_block(cur, lev, &buf);
		xfs_btree_check_block(cur, block, lev);
		if (++cur->bc_ptrs[lev] <= block->bb_numrecs)
			break;
	}
	if (lev == cur->bc_nlevels)
		return 0;
	tp = cur->bc_tp;
	mp = cur->bc_mp;
	for (; lev > level; lev--) {
		block = xfs_bmbt_get_block(cur, lev, &buf);
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
STATIC int
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
STATIC int
xfs_bmbt_insrec(xfs_btree_cur_t *cur, int level, xfs_agblock_t *bnop, xfs_bmbt_rec_t *recp, xfs_btree_cur_t **curp)
{
	xfs_btree_block_t *block;
	buf_t *buf;
	xfs_btree_block_t *cblock;
	xfs_agblock_t cbno;
	buf_t *cbuf;
	xfs_fsblock_t cfsbno;
	xfs_agblock_t *cpp;
	xfs_bmbt_rec_t *crp;
	int i;
	xfs_inode_t *ip;
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
	block = xfs_bmbt_get_block(cur, level, &buf);
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
			cbuf = xfs_btree_bread(mp, tp, cur->bc_agno, cbno);
			cblock = xfs_buf_to_block(cbuf);
			*cblock = *block;
			rp = XFS_BMAP_REC_IADDR(block, 1, cur);
			crp = XFS_BMAP_REC_IADDR(cblock, 1, cur);
			bcopy(rp, crp, block->bb_numrecs * (int)sizeof(*rp));
			pp = XFS_BMAP_PTR_IADDR(block, 1, cur);
			cpp = XFS_BMAP_PTR_IADDR(cblock, 1, cur);
			bcopy(pp, cpp, block->bb_numrecs * (int)sizeof(*pp));
			xfs_iroot_realloc(ip, 1 - block->bb_numrecs);
			block = xfs_bmbt_get_block(cur, level, &buf);
			block->bb_level++;
			block->bb_numrecs = 1;
			*pp = cbno;
			xfs_btree_setbuf(cur, level, cbuf);
			cur->bc_nlevels++;
			cur->bc_ptrs[level + 1] = 1;
			/*
			 * Do all this logging at the end so that 
			 * the root is at the right level.
			 */
			xfs_bmbt_log_block(cur, cbuf, XFS_BB_ALL_BITS);
			xfs_bmbt_log_recs(cur, cbuf, 1, block->bb_numrecs);
			xfs_bmbt_log_ptrs(cur, cbuf, 1, block->bb_numrecs);
			xfs_trans_log_inode(tp, ip, XFS_ILOG_BROOT);
			block = cblock;
		} else {
			if (xfs_bmbt_rshift(cur, level)) {
				/* nothing */
			} else if (xfs_bmbt_lshift(cur, level)) {
				optr = ptr = cur->bc_ptrs[level];
			} else if (xfs_bmbt_split(cur, level, &nbno, &nrec, &ncur)) {
				block = xfs_bmbt_get_block(cur, level, &buf);
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
		xfs_bmbt_log_ptrs(cur, buf, ptr, block->bb_numrecs + 1);
	} else {
		for (i = block->bb_numrecs; i >= ptr; i--)
			rp[i] = rp[i - 1];
	}
	rp[i] = *recp;
	xfs_bmbt_log_recs(cur, buf, ptr, block->bb_numrecs + 1);
	block->bb_numrecs++;
	xfs_bmbt_log_block(cur, buf, XFS_BB_NUMRECS);
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
STATIC void
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

STATIC void
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

STATIC void
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

/*
 * Log fields from the btree block header.
 */
STATIC void
xfs_bmbt_log_block(xfs_btree_cur_t *cur, buf_t *buf, int fields)
{
	xfs_trans_t *tp;

	tp = cur->bc_tp;
	if (buf)
		xfs_btree_log_block(tp, buf, fields);
	else
		xfs_trans_log_inode(tp, cur->bc_private.b.ip, XFS_ILOG_BROOT);
}

/*
 * Log pointer values from the btree block.
 */
STATIC void
xfs_bmbt_log_ptrs(xfs_btree_cur_t *cur, buf_t *buf, int pfirst, int plast)
{
	xfs_trans_t *tp;

	tp = cur->bc_tp;
	if (buf) {
		xfs_btree_block_t *block;
		int first;
		int last;
		xfs_agblock_t *pp;

		block = xfs_buf_to_block(buf);
		pp = XFS_BMAP_PTR_DADDR(block, 1, cur);
		first = (caddr_t)&pp[pfirst - 1] - (caddr_t)block;
		last = ((caddr_t)&pp[plast] - 1) - (caddr_t)block;
		xfs_trans_log_buf(tp, buf, first, last);
	} else {
		xfs_inode_t *ip;

		ip = cur->bc_private.b.ip;
		xfs_trans_log_inode(tp, ip, XFS_ILOG_BROOT);
	}
}

/*
 * Log record values from the btree block.
 */
STATIC void
xfs_bmbt_log_recs(xfs_btree_cur_t *cur, buf_t *buf, int rfirst, int rlast)
{
	xfs_trans_t *tp;

	tp = cur->bc_tp;
	if (buf) {
		xfs_btree_block_t *block;
		int first;
		int last;
		xfs_bmbt_rec_t *rp;

		block = xfs_buf_to_block(buf);
		rp = XFS_BMAP_REC_DADDR(block, 1, cur);
		first = (caddr_t)&rp[rfirst - 1] - (caddr_t)block;
		last = ((caddr_t)&rp[rlast] - 1) - (caddr_t)block;
		xfs_trans_log_buf(tp, buf, first, last);
	} else {
		xfs_inode_t *ip;

		ip = cur->bc_private.b.ip;
		xfs_trans_log_inode(tp, ip, XFS_ILOG_BROOT);
	}
}

/*
 * Lookup the record.  The cursor is made to point to it, based on dir.
 */
STATIC int
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
		block = xfs_bmbt_get_block(cur, level, &buf);
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

STATIC int
xfs_bmbt_lookup_eq(xfs_btree_cur_t *cur, xfs_fsblock_t off, xfs_fsblock_t bno, xfs_extlen_t len)
{
	cur->bc_rec.b.br_startoff = off;
	cur->bc_rec.b.br_startblock = bno;
	cur->bc_rec.b.br_blockcount = len;
	return xfs_bmbt_lookup(cur, XFS_LOOKUP_EQ);
}

STATIC int
xfs_bmbt_lookup_ge(xfs_btree_cur_t *cur, xfs_fsblock_t off, xfs_fsblock_t bno, xfs_extlen_t len)
{
	cur->bc_rec.b.br_startoff = off;
	cur->bc_rec.b.br_startblock = bno;
	cur->bc_rec.b.br_blockcount = len;
	return xfs_bmbt_lookup(cur, XFS_LOOKUP_GE);
}

STATIC int
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
STATIC int
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
	int nrec;
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
	nrec = left->bb_numrecs + 1;
	lrp = XFS_BMAP_REC_IADDR(left, left->bb_numrecs + 1, cur);
	rrp = XFS_BMAP_REC_IADDR(right, 1, cur);
	*lrp = *rrp;
	xfs_bmbt_log_recs(cur, lbuf, nrec, nrec);
	if (level > 0) {
		lpp = XFS_BMAP_PTR_IADDR(left, left->bb_numrecs + 1, cur);
		rpp = XFS_BMAP_PTR_IADDR(right, 1, cur);
		xfs_btree_check_ptr(cur, *rpp, level);
		*lpp = *rpp;
		xfs_bmbt_log_ptrs(cur, lbuf, nrec, nrec);
	}
	left->bb_numrecs++;
	xfs_bmbt_log_block(cur, lbuf, XFS_BB_NUMRECS);
	xfs_btree_check_rec(XFS_BTNUM_BMAP, lrp - 1, lrp);
	right->bb_numrecs--;
	xfs_bmbt_log_block(cur, rbuf, XFS_BB_NUMRECS);
	if (level > 0) {
		for (i = 0; i < right->bb_numrecs; i++) {
			rrp[i] = rrp[i + 1];
			xfs_btree_check_ptr(cur, rpp[i + 1], level);
			rpp[i] = rpp[i + 1];
		}
		xfs_bmbt_log_ptrs(cur, rbuf, 1, right->bb_numrecs);
	} else {
		for (i = 0; i < right->bb_numrecs; i++)
			rrp[i] = rrp[i + 1];
	}
	xfs_bmbt_log_recs(cur, rbuf, 1, right->bb_numrecs);
	xfs_bmbt_updkey(cur, rrp, level + 1);
	cur->bc_ptrs[level]--;
	xfs_bmbt_rcheck(cur);
	return 1;
}

#ifdef XFSDEBUG
STATIC void
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

STATIC xfs_agblock_t
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

STATIC xfs_agblock_t
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
STATIC int
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
		xfs_bmbt_log_ptrs(cur, rbuf, 1, right->bb_numrecs + 1);
	} else {
		for (i = right->bb_numrecs - 1; i >= 0; i--)
			rrp[i + 1] = rrp[i];
	}
	*rrp = *lrp;
	xfs_bmbt_log_recs(cur, rbuf, 1, right->bb_numrecs + 1);
	left->bb_numrecs--;
	xfs_bmbt_log_block(cur, lbuf, XFS_BB_NUMRECS);
	right->bb_numrecs++;
	xfs_btree_check_rec(XFS_BTNUM_BMAP, rrp, rrp + 1);
	xfs_bmbt_log_block(cur, rbuf, XFS_BB_NUMRECS);
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
STATIC int
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
		xfs_bmbt_log_ptrs(cur, rbuf, 1, right->bb_numrecs);
	} else {
		for (i = 0; i < right->bb_numrecs; i++)
			rrp[i] = lrp[i];
	}
	xfs_bmbt_log_recs(cur, rbuf, 1, right->bb_numrecs);
	rec = *rrp;
	d = lbuf->b_blkno;
	lbno = xfs_daddr_to_agbno(sbp, d);
	left->bb_numrecs -= right->bb_numrecs;
	right->bb_rightsib = left->bb_rightsib;
	left->bb_rightsib = rbno;
	right->bb_leftsib = lbno;
	xfs_bmbt_log_block(cur, rbuf, XFS_BB_ALL_BITS);
	xfs_bmbt_log_block(cur, lbuf, XFS_BB_NUMRECS | XFS_BB_RIGHTSIB);
	if (right->bb_rightsib != NULLAGBLOCK) {
		rrbuf = xfs_btree_bread(mp, tp, cur->bc_agno, right->bb_rightsib);
		rrblock = xfs_buf_to_block(rrbuf);
		xfs_btree_check_block(cur, rrblock, level);
		rrblock->bb_leftsib = rbno;
		xfs_bmbt_log_block(cur, rrbuf, XFS_BB_LEFTSIB);
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
STATIC int
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
	block = xfs_bmbt_get_block(cur, 0, &buf);
	xfs_btree_check_block(cur, block, 0);
	ptr = cur->bc_ptrs[0];
	tp = cur->bc_tp;
	rp = XFS_BMAP_REC_IADDR(block, ptr, cur);
	xfs_bmbt_set_startoff(rp, off);
	xfs_bmbt_set_startblock(rp, bno);
	xfs_bmbt_set_blockcount(rp, len);
	xfs_bmbt_log_recs(cur, buf, ptr, ptr);
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
STATIC void
xfs_bmbt_updkey(xfs_btree_cur_t *cur, xfs_bmbt_rec_t *kp, int level)
{
	xfs_btree_block_t *block;
	buf_t *buf;
	int ptr;
	xfs_bmbt_rec_t *rp;
	xfs_trans_t *tp;

	xfs_bmbt_rcheck(cur);
	tp = cur->bc_tp;
	for (ptr = 1; ptr == 1 && level < cur->bc_nlevels; level++) {
		block = xfs_bmbt_get_block(cur, level, &buf);
		xfs_btree_check_block(cur, block, level);
		ptr = cur->bc_ptrs[level];
		rp = XFS_BMAP_REC_IADDR(block, ptr, cur);
		*rp = *kp;
		xfs_bmbt_log_recs(cur, buf, ptr, ptr);
	}
	xfs_bmbt_rcheck(cur);
}

/*
 * Map file blocks to filesystem blocks.
 * Add blocks to file if a write into a hole/past eof.
 */
void
xfs_bmapi(xfs_mount_t *mp, xfs_trans_t *tp, xfs_inode_t *ip, xfs_fsblock_t bno,
	  xfs_extlen_t len, int wr, xfs_bmbt_irec_t *mval, int *nmap)
{
	xfs_btree_block_t *ablock;
	xfs_fsblock_t abno;
	buf_t *abuf;
	xfs_agblock_t agbno;
	buf_t *agbuf = NULL;
	xfs_agnumber_t agno;
	xfs_extlen_t alen;
	xfs_bmbt_rec_t *arp;
	xfs_fsblock_t askbno;
	xfs_extlen_t asklen;
	xfs_btree_block_t *block;
	xfs_btree_cur_t *cur = NULL;
	xfs_fsblock_t end;
	int eof;
	xfs_bmbt_rec_t *ep;
	xfs_bmbt_irec_t got;
	int grown = 0;
	xfs_extnum_t i;
	int inhole;
	xfs_extnum_t lastx;
	int logcore = 0;
	int logext = 0;
	int n;
	xfs_bmbt_irec_t prev;
	xfs_sb_t *sbp;

	ASSERT(*nmap >= 1 && *nmap <= XFS_BMAP_MAX_NMAP);
	ASSERT(!(ip->i_flags & XFS_IINLINE));
	ASSERT(ip->i_d.di_format == XFS_DINODE_FMT_BTREE ||
	       ip->i_d.di_format == XFS_DINODE_FMT_EXTENTS);
	if (!(ip->i_flags & XFS_IEXTENTS)) {
		xfs_iread_extents(mp, tp, ip);
		lastx = ip->i_lastex = NULLEXTNUM;
		ep = NULL;
	} else {
		lastx = ip->i_lastex;
		if (lastx != NULLEXTNUM && lastx < ip->i_d.di_nextents)
			ep = &ip->i_u1.iu_extents[lastx];
		else
			ep = NULL;
	}
	end = bno + len;
	sbp = &mp->m_sb;
	agno = xfs_ino_to_agno(sbp, ip->i_ino);
	eof = 0;
	if (ep && bno >= (got.br_startoff = xfs_bmbt_get_startoff(ep)) &&
	    bno < got.br_startoff +
	          (got.br_blockcount = xfs_bmbt_get_blockcount(ep))) {
		got.br_startblock = xfs_bmbt_get_startblock(ep);
	} else if (ep && lastx < ip->i_d.di_nextents - 1 &&
		   bno >= (got.br_startoff = xfs_bmbt_get_startoff(ep + 1)) &&
		   bno < got.br_startoff +
			(got.br_blockcount = xfs_bmbt_get_blockcount(ep + 1))) {
		lastx++;
		ep++;
		got.br_startblock = xfs_bmbt_get_startblock(ep);
	} else {
		/* bsearch the extents array */
		/* for now do a linear search */
		ep = ip->i_u1.iu_extents;
		lastx = 0;
		while (lastx < ip->i_d.di_nextents) {
			got.br_startoff = xfs_bmbt_get_startoff(ep);
			got.br_blockcount = xfs_bmbt_get_blockcount(ep);
			if (got.br_startoff + got.br_blockcount > bno)
				break;
			lastx++;
			ep++;
		}
		if (lastx == ip->i_d.di_nextents) {
			eof = 1;
			if (lastx)
				xfs_bmbt_get_all(&ip->i_u1.iu_extents[lastx - 1], prev);
			else
				prev.br_startblock = NULLFSBLOCK;
		} else
			got.br_startblock = xfs_bmbt_get_startblock(ep);
	}
	n = 0;
	while (bno < end && n < *nmap) {
		if (eof && !wr)
			break;
		inhole = eof || got.br_startoff > bno;
		/*
		 * First, deal with the hole before the allocated space 
		 * that we found, if any.
		 */
		if (inhole && wr) {
			if (eof) {
				asklen = len;
				if (prev.br_startblock == NULLFSBLOCK)
					askbno = xfs_agb_to_fsb(&mp->m_sb, agno, ip->i_bno);
				else
					askbno = prev.br_startblock + prev.br_blockcount;
			} else {
				asklen = (xfs_extlen_t)(got.br_startoff - bno);
				askbno = got.br_startblock - asklen;
			}
			abno = xfs_alloc_vextent(tp, askbno, 1, asklen, &alen, XFS_ALLOCTYPE_NEAR_BNO);
			if (abno == NULLFSBLOCK) {
				abno = xfs_alloc_vextent(tp, askbno, 1, asklen, &alen, XFS_ALLOCTYPE_START_AG);
				if (abno == NULLFSBLOCK)
					break;
			}
			if ((ip->i_flags & XFS_IBROOT) && !cur) {
				agbuf = xfs_btree_bread(mp, tp, agno, XFS_AGH_BLOCK);
				cur = xfs_btree_init_cursor(mp, tp, agbuf, agno, XFS_BTNUM_BMAP, ip);
			}
			logext = 1;
			if (!eof && abno + alen == got.br_startblock) {
				if (cur)
					xfs_bmbt_lookup_eq(cur, got.br_startoff, got.br_startblock, got.br_blockcount);
				got.br_startoff = bno;
				got.br_startblock = abno;
				got.br_blockcount += alen;
				xfs_bmbt_set_all(ep, got);
				if (cur)
					xfs_bmbt_update(cur, got.br_startoff, got.br_startblock, got.br_blockcount);
				continue;
			}
			mval->br_startoff = bno;
			mval->br_startblock = abno;
			mval->br_blockcount = alen;
			xfs_iext_realloc(ip, 1);
			ep = &ip->i_u1.iu_extents[lastx];
			for (i = ip->i_d.di_nextents - 1; i >= lastx; i--)
				ip->i_u1.iu_extents[i + 1] = ip->i_u1.iu_extents[i];
			xfs_bmbt_set_all(ep, *mval);
			if (cur) {
				cur->bc_rec.b = *mval;
				xfs_bmbt_insert(cur);
			}
			ip->i_d.di_nextents++;
			logcore = 1;
			grown = 1;
		} else if (inhole) {
			mval->br_startoff = bno;
			mval->br_startblock = NULLFSBLOCK;
			mval->br_blockcount = xfs_extlen_min(len, got.br_startoff - bno);
		}
		if (inhole) {
			bno += mval->br_blockcount;
			len -= mval->br_blockcount;
			mval++;
			n++;
			continue;
		}
		/*
		 * Then deal with the allocated space we found.
		 */
		ASSERT(ep != NULL);
		mval->br_startoff = bno;
		mval->br_startblock = got.br_startblock + (bno - got.br_startoff);
		mval->br_blockcount = xfs_extlen_min(len, got.br_blockcount - (bno - got.br_startoff));
		bno += mval->br_blockcount;
		len -= mval->br_blockcount;
		mval++;
		n++;
		/*
		 * If we're done, stop now.
		 */
		if (bno >= end || n >= *nmap)
			break;
		/*
		 * Else go on to the next record.
		 */
		ep++;
		lastx++;
		if (cur)
			xfs_bmbt_increment(cur, 0);
		if (lastx >= ip->i_lastex) {
			eof = 1;
			prev = got;
		} else
			xfs_bmbt_get_all(ep, got);
	}
	ip->i_lastex = lastx;
	*nmap = n;
	/*
	 * Convert to a btree if necessary.
	 */
	if (grown && ip->i_d.di_format == XFS_DINODE_FMT_EXTENTS &&
	    ip->i_d.di_nextents > XFS_BMAP_EXT_MAXRECS(sbp->sb_inodesize)) {
		/*
		 * Make space in the inode incore.
		 */
		xfs_iroot_realloc(ip, 1);
		/*
		 * Fill in the root.
		 */
		block = ip->i_broot;
		block->bb_magic = XFS_BMAP_MAGIC;
		block->bb_level = 1;
		block->bb_numrecs = 1;
		block->bb_leftsib = block->bb_rightsib = NULLAGBLOCK;
		/*
		 * Need a cursor.  Can't allocate until bb_level is filled in.
		 */
		agbuf = xfs_btree_bread(mp, tp, agno, XFS_AGH_BLOCK);
		cur = xfs_btree_init_cursor(mp, tp, agbuf, agno, XFS_BTNUM_BMAP,
					    ip);
		/*
		 * Convert to a btree with two levels, one record in root.
		 */
		ip->i_d.di_format = XFS_DINODE_FMT_BTREE;
		logcore = 1;
		abno = xfs_agb_to_fsb(sbp, agno, ip->i_bno);
		abno = xfs_alloc_extent(tp, abno, 1, XFS_ALLOCTYPE_NEAR_BNO);
		/*
		 * Allocation can't fail, the space was reserved.
		 */
		ASSERT(abno != NULLFSBLOCK);
		agbno = xfs_fsb_to_agbno(sbp, abno);
		abuf = xfs_btree_bread(mp, tp, agno, agbno);
		/*
		 * Fill in the child block.
		 */
		ablock = xfs_buf_to_block(abuf);
		ablock->bb_magic = XFS_BMAP_MAGIC;
		ablock->bb_level = 0;
		ablock->bb_numrecs = ip->i_d.di_nextents;
		ablock->bb_leftsib = ablock->bb_rightsib = NULLAGBLOCK;
		arp = XFS_BMAP_REC_IADDR(ablock, 1, cur);
		bcopy(ip->i_u1.iu_extents, arp,
		      ablock->bb_numrecs * (int)sizeof(*arp));
		/*
		 * Fill in the root record and pointer.
		 */
		*XFS_BMAP_REC_IADDR(block, 1, cur) = *arp;
		*XFS_BMAP_PTR_IADDR(block, 1, cur) = abno;
		/*
		 * Do all this logging at the end so that 
		 * the root is at the right level.
		 */
		xfs_bmbt_log_block(cur, abuf, XFS_BB_ALL_BITS);
		xfs_bmbt_log_recs(cur, abuf, 1, ablock->bb_numrecs);
		xfs_bmbt_log_ptrs(cur, abuf, 1, ablock->bb_numrecs);
		xfs_trans_log_inode(tp, ip, XFS_ILOG_BROOT);
	}
	/*
	 * Log everything.  Do this after conversion, there's no point in
	 * logging the extent list if we've converted to btree format.
	 */
	if (logcore)
		xfs_trans_log_inode(tp, ip, XFS_ILOG_CORE);
	if (logext && ip->i_d.di_format == XFS_DINODE_FMT_EXTENTS)
		xfs_trans_log_inode(tp, ip, XFS_ILOG_EXT);
	/* logging of the BROOT happens elsewhere */
	if (cur) {
		xfs_btree_del_cursor(cur);
		xfs_trans_brelse(tp, agbuf);
	}
}

/*
 * Read in the extents to iu_extents.
 * All inode fields are set up by caller, we just traverse the btree
 * and copy the records in.
 */
void
xfs_bmap_read_extents(xfs_mount_t *mp, xfs_trans_t *tp, xfs_inode_t *ip)
{
	xfs_agnumber_t agno;
	xfs_btree_block_t *block;
	xfs_agblock_t bno = NULLAGBLOCK;
	buf_t *buf;
	xfs_bmbt_rec_t *frp;
	xfs_extnum_t i;
	xfs_extnum_t room;
	xfs_bmbt_rec_t *trp;
	xfs_sb_t *sbp;

	sbp = &mp->m_sb;
	agno = xfs_ino_to_agno(sbp, ip->i_ino);
	block = ip->i_broot;
	if (block->bb_level)
		bno = *XFS_BMAP_BROOT_PTR_ADDR(block, 1, sbp->sb_inodesize);
	while (block->bb_level) {
		buf = xfs_btree_bread(mp, tp, agno, bno);
		block = xfs_buf_to_block(buf);
		if (block->bb_level == 0)
			break;
		bno = *XFS_BTREE_PTR_ADDR(sbp->sb_blocksize, xfs_bmbt_rec_t, block, 1);
		xfs_trans_brelse(tp, buf);
	}
	trp = ip->i_u1.iu_extents;
	room = ip->i_bytes / sizeof(*trp);
	i = 0;
	for (;;) {
		ASSERT(i + block->bb_numrecs <= room);
		frp = XFS_BTREE_REC_ADDR(sbp->sb_blocksize, xfs_bmbt_rec_t, block, 1);
		bcopy(frp, trp, block->bb_numrecs * sizeof(*frp));
		trp += block->bb_numrecs;
		i += block->bb_numrecs;
		if (bno != NULLAGBLOCK)
			xfs_trans_brelse(tp, buf);
		bno = block->bb_rightsib;
		if (bno == NULLAGBLOCK)
			break;
		buf = xfs_btree_bread(mp, tp, agno, bno);
		block = xfs_buf_to_block(buf);
	}
}
