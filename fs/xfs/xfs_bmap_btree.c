#ident	"$Revision: 1.16 $"

#include <sys/param.h>
#include <sys/vnode.h>
#include <sys/uuid.h>
#include <sys/debug.h>
#include <sys/kmem.h>
#include <stddef.h>
#ifdef SIM
#include <bstring.h>
#else
#include <sys/systm.h>
#endif
#include "xfs_types.h"
#include "xfs_inum.h"
#ifdef SIM
#define _KERNEL
#endif
#include <sys/buf.h>
#include <sys/uuid.h>
#include <sys/grio.h>
#ifdef SIM
#undef _KERNEL
#endif
#include "xfs_log.h"
#include "xfs_trans.h"
#include "xfs_sb.h"
#include "xfs_ag.h"
#include "xfs_mount.h"
#include "xfs_alloc_btree.h"
#include "xfs_ialloc.h"
#include "xfs_bmap_btree.h"
#include "xfs_btree.h"
#include "xfs_dinode.h"
#include "xfs_inode_item.h"
#include "xfs_inode.h"
#include "xfs_bmap.h"
#include "xfs_alloc.h"
#ifdef SIM
#include "sim.h"
#endif

#if !defined(SIM) || !defined(XFSDEBUG)
#define	kmem_check()	/* dummy for memory-allocation checking */
#endif

/*
 * Prototypes for internal btree functions.
 */

#ifdef XFSDEBUG

STATIC void
xfs_bmbt_kcheck_body(
	xfs_btree_cur_t *,
	xfs_bmbt_block_t *,
	int,
	xfs_bmbt_key_t *);

STATIC void
xfs_bmbt_kcheck_btree(
	xfs_btree_cur_t *,
	xfs_fsblock_t,
	int,
	xfs_bmbt_key_t *);

STATIC xfs_fsblock_t
xfs_bmbt_rcheck_body(
	xfs_btree_cur_t *,
	xfs_bmbt_block_t *,
	xfs_fsblock_t *,
	void *,
	int);

STATIC xfs_fsblock_t
xfs_bmbt_rcheck_btree(
	xfs_btree_cur_t *,
	xfs_fsblock_t,
	xfs_fsblock_t *,
	void *,
	int);

#endif	/* XFSDEBUG */

STATIC int
xfs_bmbt_delrec(
	xfs_btree_cur_t *,
	int);

STATIC int
xfs_bmbt_get_rec(
	xfs_btree_cur_t *,
	xfs_fileoff_t *,
	xfs_fsblock_t *,
	xfs_extlen_t *);

STATIC int
xfs_bmbt_insrec(
	xfs_btree_cur_t *,
	int,
	xfs_fsblock_t *,
	xfs_bmbt_rec_t *,
	xfs_btree_cur_t **);

STATIC int
xfs_bmbt_killroot(
	xfs_btree_cur_t *);

STATIC void
xfs_bmbt_log_keys(
	xfs_btree_cur_t *,
	buf_t *,
	int,
	int);

STATIC void
xfs_bmbt_log_ptrs(
	xfs_btree_cur_t *,
	buf_t *,
	int,
	int);

STATIC int
xfs_bmbt_lookup(
	xfs_btree_cur_t *,
	xfs_lookup_t);

STATIC int
xfs_bmbt_lshift(
	xfs_btree_cur_t *,
	int);

STATIC int
xfs_bmbt_rshift(
	xfs_btree_cur_t *,
	int);

STATIC int
xfs_bmbt_split(
	xfs_btree_cur_t *,
	int,
	xfs_fsblock_t *,
	xfs_bmbt_key_t *,
	xfs_btree_cur_t **);

STATIC void
xfs_bmbt_updkey(
	xfs_btree_cur_t *,
	xfs_bmbt_key_t *,
	int);

/*
 * Internal functions.
 */

/*
 * Delete record pointed to by cur/level.
 */
STATIC int
xfs_bmbt_delrec(
	xfs_btree_cur_t		*cur,
	int			level)
{
	xfs_bmbt_block_t	*block;
	xfs_fsblock_t		bno;
	buf_t			*bp;
	xfs_bmbt_block_t	*cblock;
	xfs_bmbt_key_t		*ckp;
	xfs_bmbt_ptr_t		*cpp;
	int			first;
	int			i;
	xfs_bmbt_key_t		key;
	xfs_bmbt_key_t		*kp;
	int			last;
	xfs_fsblock_t		lbno;
	buf_t			*lbp;
	xfs_bmbt_block_t	*left;
	xfs_bmbt_key_t		*lkp;
	xfs_bmbt_ptr_t		*lpp;
	int			lrecs;
	xfs_bmbt_rec_t		*lrp;
	xfs_mount_t		*mp;
	xfs_bmbt_ptr_t		*pp;
	int			ptr;
	xfs_fsblock_t		rbno;
	buf_t			*rbp;
	xfs_bmbt_block_t	*right;
	xfs_bmbt_key_t		*rkp;
	xfs_bmbt_rec_t		*rp;
	xfs_bmbt_ptr_t		*rpp;
	xfs_bmbt_block_t	*rrblock;
	buf_t			*rrbp;
	int			rrecs;
	xfs_bmbt_rec_t		*rrp;
	xfs_btree_cur_t		*tcur;
	xfs_trans_t		*tp;

	xfs_bmbt_rcheck(cur);
	tp = cur->bc_tp;
	mp = cur->bc_mp;
	ptr = cur->bc_ptrs[level];
	if (ptr == 0)
		return 0;
	block = xfs_bmbt_get_block(cur, level, &bp);
	xfs_btree_check_lblock(cur, block, level);
	if (ptr > block->bb_numrecs)
		return 0;
	if (level > 0) {
		kp = XFS_BMAP_KEY_IADDR(block, 1, cur);
		pp = XFS_BMAP_PTR_IADDR(block, 1, cur);
		for (i = ptr; i < block->bb_numrecs; i++) {
			kp[i - 1] = kp[i];
			xfs_btree_check_lptr(cur, pp[i], level);
			pp[i - 1] = pp[i];
		}
		if (ptr < i) {
			xfs_bmbt_log_ptrs(cur, bp, ptr, i - 1);
			xfs_bmbt_log_keys(cur, bp, ptr, i - 1);
		}
	} else {
		rp = XFS_BMAP_REC_IADDR(block, 1, cur);
		for (i = ptr; i < block->bb_numrecs; i++)
			rp[i - 1] = rp[i];
		if (ptr < i)
			xfs_bmbt_log_recs(cur, bp, ptr, i - 1);
		if (ptr == 1) {
			key.br_startoff = xfs_bmbt_get_startoff(rp);
			kp = &key;
		}
	}
	block->bb_numrecs--;
	xfs_bmbt_log_block(cur, bp, XFS_BB_NUMRECS);
	/*
	 * We just did a join at the previous level.
	 * Make the cursor point to the good (left) key.
	 */
	if (level > 0)
		xfs_bmbt_decrement(cur, level);
	/*
	 * We're at the root level.
	 * First, shrink the root block in-memory.
	 * Try to get rid of the next level down.
	 * If we can't then there's nothing left to do.
	 */
	if (level == cur->bc_nlevels - 1) {
		xfs_iroot_realloc(cur->bc_private.b.ip, -1);
		return xfs_bmbt_killroot(cur);
	}
	if (ptr == 1)
		xfs_bmbt_updkey(cur, kp, level + 1);
	xfs_bmbt_rcheck(cur);
	if (block->bb_numrecs >= XFS_BMAP_BLOCK_IMINRECS(level, cur))
		return 1;
	rbno = block->bb_rightsib;
	lbno = block->bb_leftsib;
	/*
	 * One child of root, need to get a chance to copy its contents
	 * into the root and delete it. Can't go up to next level,
	 * there's nothing to delete there.
	 */
	if (lbno == NULLFSBLOCK && rbno == NULLFSBLOCK &&
	    level == cur->bc_nlevels - 2)
		return xfs_bmbt_killroot(cur);
	ASSERT(rbno != NULLFSBLOCK || lbno != NULLFSBLOCK);
	tcur = xfs_btree_dup_cursor(cur);
	bno = NULLFSBLOCK;
	bp = cur->bc_bufs[level];
	if (rbno != NULLFSBLOCK) {
		xfs_btree_lastrec(tcur, level);
		xfs_bmbt_increment(tcur, level);
		xfs_btree_lastrec(tcur, level);
		rbp = tcur->bc_bufs[level];
		right = XFS_BUF_TO_BMBT_BLOCK(rbp);
		xfs_btree_check_lblock(cur, right, level);
		bno = right->bb_leftsib;
		if (right->bb_numrecs - 1 >= XFS_BMAP_BLOCK_IMINRECS(level, cur)) {
			if (xfs_bmbt_lshift(tcur, level)) {
				ASSERT(block->bb_numrecs >= XFS_BMAP_BLOCK_IMINRECS(level, tcur));
				xfs_btree_del_cursor(tcur);
				return 1;
			}
		}
		rrecs = right->bb_numrecs;
		if (lbno != NULLFSBLOCK) {
			xfs_btree_firstrec(tcur, level);
			xfs_bmbt_decrement(tcur, level);
		}
	}
	if (lbno != NULLFSBLOCK) {
		xfs_btree_firstrec(tcur, level);
		xfs_bmbt_decrement(tcur, level);	/* to last */
		xfs_btree_firstrec(tcur, level);
		lbp = tcur->bc_bufs[level];
		left = XFS_BUF_TO_BMBT_BLOCK(lbp);
		xfs_btree_check_lblock(cur, left, level);
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
	ASSERT(bno != NULLFSBLOCK);
	if (lbno != NULLFSBLOCK &&
	    lrecs + block->bb_numrecs <= XFS_BMAP_BLOCK_IMAXRECS(level, cur)) {
		rbno = bno;
		right = block;
		rbp = bp;
		lbp = xfs_btree_read_bufl(mp, tp, lbno, 0);
		left = XFS_BUF_TO_BMBT_BLOCK(lbp);
		xfs_btree_check_lblock(cur, left, level);
	} else if (rbno != NULLFSBLOCK &&
		   rrecs + block->bb_numrecs <= XFS_BMAP_BLOCK_IMAXRECS(level, cur)) {
		lbno = bno;
		left = block;
		lbp = bp;
		rbp = xfs_btree_read_bufl(mp, tp, rbno, 0);
		right = XFS_BUF_TO_BMBT_BLOCK(rbp);
		xfs_btree_check_lblock(cur, right, level);
	} else {
		xfs_bmbt_rcheck(cur);
		return 1;
	}
	if (level > 0) {
		lkp = XFS_BMAP_KEY_IADDR(left, left->bb_numrecs + 1, cur);
		lpp = XFS_BMAP_PTR_IADDR(left, left->bb_numrecs + 1, cur);
		rkp = XFS_BMAP_KEY_IADDR(right, 1, cur);
		rpp = XFS_BMAP_PTR_IADDR(right, 1, cur);
		for (i = 0; i < right->bb_numrecs; i++) {
			lkp[i] = rkp[i];
			xfs_btree_check_lptr(cur, rpp[i], level);
			lpp[i] = rpp[i];
		}
		xfs_bmbt_log_keys(cur, lbp, left->bb_numrecs + 1, left->bb_numrecs + right->bb_numrecs);
		xfs_bmbt_log_ptrs(cur, lbp, left->bb_numrecs + 1, left->bb_numrecs + right->bb_numrecs);
	} else {
		lrp = XFS_BMAP_REC_IADDR(left, left->bb_numrecs + 1, cur);
		rrp = XFS_BMAP_REC_IADDR(right, 1, cur);
		for (i = 0; i < right->bb_numrecs; i++)
			lrp[i] = rrp[i];
		xfs_bmbt_log_recs(cur, lbp, left->bb_numrecs + 1, left->bb_numrecs + right->bb_numrecs);
	}
	if (bp != lbp) {
		xfs_btree_setbuf(cur, level, lbp);
		cur->bc_ptrs[level] += left->bb_numrecs;
	} else if (level + 1 < cur->bc_nlevels)
		xfs_bmbt_increment(cur, level + 1);
	left->bb_numrecs += right->bb_numrecs;
	left->bb_rightsib = right->bb_rightsib;
	xfs_bmbt_log_block(cur, lbp, XFS_BB_RIGHTSIB | XFS_BB_NUMRECS);
	if (left->bb_rightsib != NULLDFSBNO) {
		rrbp = xfs_btree_read_bufl(mp, tp, left->bb_rightsib, 0);
		rrblock = XFS_BUF_TO_BMBT_BLOCK(rrbp);
		xfs_btree_check_lblock(cur, rrblock, level);
		rrblock->bb_leftsib = lbno;
		xfs_bmbt_log_block(cur, rrbp, XFS_BB_LEFTSIB);
	}
	xfs_bmap_add_free(XFS_DADDR_TO_FSB(mp, rbp->b_blkno), 1,
		cur->bc_private.b.flist, mp);
	xfs_bmbt_rcheck(cur);
	return 2;
}

/* 
 * Get the data from the pointed-to record.
 */
STATIC int
xfs_bmbt_get_rec(
	xfs_btree_cur_t		*cur,
	xfs_fileoff_t		*off,
	xfs_fsblock_t		*bno,
	xfs_extlen_t		*len)
{
	xfs_bmbt_block_t	*block;
	buf_t			*bp;
	int			ptr;
	xfs_bmbt_rec_t		*rp;

	block = xfs_bmbt_get_block(cur, 0, &bp);
	ptr = cur->bc_ptrs[0];
	xfs_btree_check_lblock(cur, block, 0);
	if (ptr > block->bb_numrecs || ptr <= 0)
		return 0;
	rp = XFS_BMAP_REC_IADDR(block, ptr, cur);
	*off = xfs_bmbt_get_startoff(rp);
	*bno = xfs_bmbt_get_startblock(rp);
	*len = xfs_bmbt_get_blockcount(rp);
	return 1;
}

/*
 * Insert one record/level.  Return information to the caller
 * allowing the next level up to proceed if necessary.
 */
STATIC int
xfs_bmbt_insrec(
	xfs_btree_cur_t		*cur,
	int			level,
	xfs_fsblock_t		*bnop,
	xfs_bmbt_rec_t		*recp,
	xfs_btree_cur_t		**curp)
{
	xfs_fsblock_t		askbno;
	xfs_bmbt_block_t	*block;
	buf_t			*bp;
	xfs_bmbt_block_t	*cblock;
	xfs_fsblock_t		cbno;
	xfs_bmbt_key_t		*ckp;
	xfs_bmbt_ptr_t		*cpp;
	xfs_bmbt_rec_t		*crp;
	int			i;
	xfs_inode_t		*ip;
	xfs_bmbt_key_t		key;
	xfs_bmbt_key_t		*kp;
	xfs_mount_t		*mp;
	xfs_fsblock_t		nbno;
	xfs_btree_cur_t		*ncur = (xfs_btree_cur_t *)0;
	xfs_bmbt_key_t		nkey;
	xfs_bmbt_rec_t		nrec;
	int			optr;
	xfs_bmbt_ptr_t		*pp;
	int			ptr;
	xfs_bmbt_rec_t		*rp;
	xfs_trans_t		*tp;
	xfs_alloctype_t		type;

	ASSERT(level < cur->bc_nlevels);
	xfs_bmbt_rcheck(cur);
	key.br_startoff = xfs_bmbt_get_startoff(recp);
	optr = ptr = cur->bc_ptrs[level];
	if (ptr == 0)
		return 0;
	block = xfs_bmbt_get_block(cur, level, &bp);
	xfs_btree_check_lblock(cur, block, level);
#ifdef DEBUG
	if (ptr <= block->bb_numrecs) {
		if (level == 0) {
			rp = XFS_BMAP_REC_IADDR(block, ptr, cur);
			xfs_btree_check_rec(XFS_BTNUM_BMAP, recp, rp);
		} else {
			kp = XFS_BMAP_KEY_IADDR(block, ptr, cur);
			xfs_btree_check_key(XFS_BTNUM_BMAP, &key, kp);
		}
	}
#endif
	tp = cur->bc_tp;
	mp = cur->bc_mp;
	nbno = NULLFSBLOCK;
	if (block->bb_numrecs == XFS_BMAP_BLOCK_IMAXRECS(level, cur)) {
		ip = cur->bc_private.b.ip;
		if (block->bb_numrecs < XFS_BMAP_BLOCK_DMAXRECS(level, cur)) {
			/*
			 * A root block, that can be made bigger.
			 */
			xfs_iroot_realloc(ip, 1);
			block = xfs_bmbt_get_block(cur, level, &bp);
		} else if (level == cur->bc_nlevels - 1) {
			/*
			 * Copy the root into a real block.
			 */
			pp = XFS_BMAP_PTR_IADDR(block, 1, cur);
			askbno = cur->bc_private.b.firstblock;
			if (askbno == NULLFSBLOCK) {
				xfs_btree_check_lptr(cur, *pp, level);
				askbno = *pp;
				type = XFS_ALLOCTYPE_START_BNO;
			} else
				type = XFS_ALLOCTYPE_NEAR_BNO;
			cbno = xfs_alloc_extent(tp, askbno, 1, type, 0, 0);
			if (cbno == NULLFSBLOCK)
				return 0;
			cur->bc_private.b.allocated++;
			ip->i_d.di_nblocks++;
			bp = xfs_btree_get_bufl(mp, tp, cbno, 0);
			cblock = XFS_BUF_TO_BMBT_BLOCK(bp);
			*cblock = *block;
			block->bb_level++;
			block->bb_numrecs = 1;
			cur->bc_nlevels++;
			cur->bc_ptrs[level + 1] = 1;
			kp = XFS_BMAP_KEY_IADDR(block, 1, cur);
			ckp = XFS_BMAP_KEY_IADDR(cblock, 1, cur);
			bcopy(kp, ckp, cblock->bb_numrecs * (int)sizeof(*kp));
			cpp = XFS_BMAP_PTR_IADDR(cblock, 1, cur);
#ifdef DEBUG
			for (i = 0; i < cblock->bb_numrecs; i++)
				xfs_btree_check_lptr(cur, pp[i], level);
#endif
			bcopy(pp, cpp, cblock->bb_numrecs * (int)sizeof(*pp));
			xfs_btree_check_lptr(cur, (xfs_bmbt_ptr_t)cbno, level);
			*pp = cbno;
			xfs_iroot_realloc(ip, 1 - cblock->bb_numrecs);
			xfs_btree_setbuf(cur, level, bp);
			/*
			 * Do all this logging at the end so that 
			 * the root is at the right level.
			 */
			xfs_bmbt_log_block(cur, bp, XFS_BB_ALL_BITS);
			xfs_bmbt_log_keys(cur, bp, 1, cblock->bb_numrecs);
			xfs_bmbt_log_ptrs(cur, bp, 1, cblock->bb_numrecs);
			xfs_trans_log_inode(tp, ip,
				XFS_ILOG_CORE | XFS_ILOG_BROOT);
			block = cblock;
		} else {
			if (xfs_bmbt_rshift(cur, level)) {
				/* nothing */
			} else if (xfs_bmbt_lshift(cur, level)) {
				optr = ptr = cur->bc_ptrs[level];
			} else if (xfs_bmbt_split(cur, level, &nbno, &nkey, &ncur)) {
				block = xfs_bmbt_get_block(cur, level, &bp);
				xfs_btree_check_lblock(cur, block, level);
				ptr = cur->bc_ptrs[level];
				xfs_bmbt_set_startoff(&nrec, nkey.br_startoff);
			} else
				return 0;
		}
	}
	if (level > 0) {
		kp = XFS_BMAP_KEY_IADDR(block, 1, cur);
		pp = XFS_BMAP_PTR_IADDR(block, 1, cur);
		for (i = block->bb_numrecs; i >= ptr; i--) {
			kp[i] = kp[i - 1];
			xfs_btree_check_lptr(cur, pp[i - 1], level);
			pp[i] = pp[i - 1];
		}
		xfs_btree_check_lptr(cur, (xfs_bmbt_ptr_t)*bnop, level);
		kp[i] = key;
		pp[i] = *bnop;
		block->bb_numrecs++;
		xfs_bmbt_log_keys(cur, bp, ptr, block->bb_numrecs);
		xfs_bmbt_log_ptrs(cur, bp, ptr, block->bb_numrecs);
	} else {
		rp = XFS_BMAP_REC_IADDR(block, 1, cur);
		for (i = block->bb_numrecs; i >= ptr; i--)
			rp[i] = rp[i - 1];
		rp[i] = *recp;
		block->bb_numrecs++;
		xfs_bmbt_log_recs(cur, bp, ptr, block->bb_numrecs);
	}
	xfs_bmbt_log_block(cur, bp, XFS_BB_NUMRECS);
#ifdef DEBUG
	if (ptr < block->bb_numrecs) {
		if (level == 0)
			xfs_btree_check_rec(XFS_BTNUM_BMAP, rp + i, rp + i + 1);
		else
			xfs_btree_check_key(XFS_BTNUM_BMAP, kp + i, kp + i + 1);
	}
#endif
	if (optr == 1)
		xfs_bmbt_updkey(cur, &key, level + 1);
	*bnop = nbno;
	xfs_bmbt_rcheck(cur);
	if (nbno != NULLFSBLOCK) {
		*recp = nrec;
		*curp = ncur;
	} else
		xfs_bmbt_kcheck(cur);
	return 1;
}

#ifdef XFSDEBUG
STATIC void
xfs_bmbt_kcheck_body(
	xfs_btree_cur_t		*cur,
	xfs_bmbt_block_t	*block,
	int			level,
	xfs_bmbt_key_t		*keyp)
{
	int			i;
	xfs_bmbt_key_t		key;
	xfs_bmbt_key_t		*kp;
	xfs_bmbt_ptr_t		*pp;
	xfs_bmbt_rec_t		*rp;

	xfs_btree_check_lblock(cur, block, level);
	if (level > 0) {
		kp = XFS_BMAP_KEY_IADDR(block, 1, cur);
		if (keyp)
			key = *kp;
	} else {
		rp = XFS_BMAP_REC_IADDR(block, 1, cur);
		if (keyp)
			key.br_startoff = xfs_bmbt_get_startoff(rp);
	}
	if (keyp)
		ASSERT(bcmp(keyp, &key, sizeof(key)) == 0);
	if (level > 0) {
		pp = XFS_BMAP_PTR_IADDR(block, 1, cur);
		if (*pp != NULLFSBLOCK) {
			for (i = 1; i <= block->bb_numrecs; i++, pp++, kp++)
				xfs_bmbt_kcheck_btree(cur, *pp, level - 1, kp);
		}
	}
}

STATIC void
xfs_bmbt_kcheck_btree(
	xfs_btree_cur_t		*cur,
	xfs_fsblock_t		bno,
	int			level,
	xfs_bmbt_key_t		*kp)
{
	xfs_bmbt_block_t	*block;
	buf_t			*bp;
	xfs_trans_t		*tp;

	ASSERT(bno != NULLFSBLOCK);
	tp = cur->bc_tp;
	bp = xfs_btree_read_bufl(cur->bc_mp, tp, bno, 0);
	block = XFS_BUF_TO_BMBT_BLOCK(bp);
	xfs_bmbt_kcheck_body(cur, block, level, kp);
	xfs_trans_brelse(tp, bp);
}
#endif

STATIC int
xfs_bmbt_killroot(
	xfs_btree_cur_t		*cur)
{
	xfs_bmbt_block_t	*block;
	xfs_bmbt_block_t	*cblock;
	buf_t			*cbp;
	xfs_bmbt_key_t		*ckp;
	xfs_bmbt_ptr_t		*cpp;
	int			i;
	xfs_bmbt_key_t		*kp;
	xfs_inode_t		*ip;
	int			level;
	xfs_mount_t		*mp;
	xfs_bmbt_ptr_t		*pp;
	xfs_trans_t		*tp;

	level = cur->bc_nlevels - 1;
	ASSERT(level >= 1);
	/*
	 * Don't deal with the root block needs to be a leaf case.
	 * We're just going to turn the thing back into extents anyway.
	 */
	if (level == 1)
		return 1;
	block = xfs_bmbt_get_block(cur, level, &cbp);
	/*
	 * Give up if the root has multiple children.
	 */
	if (block->bb_numrecs != 1)
		return 1;
	/*
	 * Only do this if the next level will fit.
	 * Then the data must be copied up to the inode,
	 * instead of freeing the root you free the next level.
	 */
	cbp = cur->bc_bufs[level - 1];
	cblock = XFS_BUF_TO_BMBT_BLOCK(cbp);
	if (cblock->bb_numrecs > XFS_BMAP_BLOCK_DMAXRECS(level, cur))
		return 1;
	ASSERT(cblock->bb_leftsib == NULLDFSBNO);
	ASSERT(cblock->bb_rightsib == NULLDFSBNO);
	ip = cur->bc_private.b.ip;
	ASSERT(XFS_BMAP_BLOCK_IMAXRECS(level, cur) ==
	       XFS_BMAP_BROOT_MAXRECS(ip->i_broot_bytes));
	i = (int)(cblock->bb_numrecs - XFS_BMAP_BLOCK_IMAXRECS(level, cur));
	if (i) {
		xfs_iroot_realloc(ip, i);
		block = ip->i_broot;
	}
	*block = *cblock;
	kp = XFS_BMAP_KEY_IADDR(block, 1, cur);
	ckp = XFS_BMAP_KEY_IADDR(cblock, 1, cur);
	bcopy((caddr_t)ckp, (caddr_t)kp, block->bb_numrecs * (int)sizeof(*kp));
	pp = XFS_BMAP_PTR_IADDR(block, 1, cur);
	cpp = XFS_BMAP_PTR_IADDR(cblock, 1, cur);
#ifdef DEBUG
	for (i = 0; i < block->bb_numrecs; i++)
		xfs_btree_check_lptr(cur, cpp[i], level);
#endif
	bcopy((caddr_t)cpp, (caddr_t)pp, block->bb_numrecs * (int)sizeof(*pp));
	tp = cur->bc_tp;
	mp = cur->bc_mp;
	xfs_bmap_add_free(XFS_DADDR_TO_FSB(mp, cbp->b_blkno), 1,
		cur->bc_private.b.flist, mp);
	xfs_trans_log_inode(tp, ip, XFS_ILOG_BROOT);
	xfs_btree_setbuf(cur, level - 1, 0);
	cur->bc_nlevels--;
	xfs_bmbt_rcheck(cur);
	return 1;
}

/*
 * Log key values from the btree block.
 */
STATIC void
xfs_bmbt_log_keys(
	xfs_btree_cur_t	*cur,
	buf_t		*bp,
	int		kfirst,
	int		klast)
{
	xfs_trans_t	*tp;

	tp = cur->bc_tp;
	if (bp) {
		xfs_bmbt_block_t *block;
		int first;
		int last;
		xfs_bmbt_key_t *kp;

		block = XFS_BUF_TO_BMBT_BLOCK(bp);
		kp = XFS_BMAP_KEY_DADDR(block, 1, cur);
		first = (caddr_t)&kp[kfirst - 1] - (caddr_t)block;
		last = ((caddr_t)&kp[klast] - 1) - (caddr_t)block;
		xfs_trans_log_buf(tp, bp, first, last);
	} else {
		xfs_inode_t *ip;

		ip = cur->bc_private.b.ip;
		xfs_trans_log_inode(tp, ip, XFS_ILOG_BROOT);
	}
}

/*
 * Log pointer values from the btree block.
 */
STATIC void
xfs_bmbt_log_ptrs(
	xfs_btree_cur_t	*cur,
	buf_t		*bp,
	int		pfirst,
	int		plast)
{
	xfs_trans_t	*tp;

	tp = cur->bc_tp;
	if (bp) {
		xfs_bmbt_block_t *block;
		int first;
		int last;
		xfs_bmbt_ptr_t *pp;

		block = XFS_BUF_TO_BMBT_BLOCK(bp);
		pp = XFS_BMAP_PTR_DADDR(block, 1, cur);
		first = (caddr_t)&pp[pfirst - 1] - (caddr_t)block;
		last = ((caddr_t)&pp[plast] - 1) - (caddr_t)block;
		xfs_trans_log_buf(tp, bp, first, last);
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
xfs_bmbt_lookup(
	xfs_btree_cur_t		*cur,
	xfs_lookup_t		dir)
{
	xfs_bmbt_block_t	*block;
	buf_t			*bp;
	daddr_t			d;
	int			diff;
	xfs_fsblock_t		fsbno;
	int			high;
	int			i;
	int			keyno;
	xfs_bmbt_key_t		*kkbase;
	xfs_bmbt_key_t		*kkp;
	xfs_bmbt_rec_t		*krbase;
	xfs_bmbt_rec_t		*krp;
	int			level;
	int			low;
	xfs_mount_t		*mp;
	xfs_bmbt_ptr_t		*pp;
	xfs_bmbt_irec_t		*rp;
	xfs_fileoff_t		startoff;
	xfs_trans_t		*tp;

	xfs_bmbt_rcheck(cur);
	xfs_bmbt_kcheck(cur);
	tp = cur->bc_tp;
	mp = cur->bc_mp;
	rp = &cur->bc_rec.b;
	for (level = cur->bc_nlevels - 1, diff = 1; level >= 0; level--) {
		if (level < cur->bc_nlevels - 1) {
			d = XFS_FSB_TO_DADDR(mp, fsbno);
			bp = cur->bc_bufs[level];
			if (bp && bp->b_blkno != d)
				bp = (buf_t *)0;
			if (!bp) {
				bp = xfs_trans_read_buf(tp, mp->m_dev, d,
					mp->m_bsize, 0);
				ASSERT(bp && !geterror(bp));
				xfs_btree_setbuf(cur, level, bp);
			}
		}
		block = xfs_bmbt_get_block(cur, level, &bp);
		xfs_btree_check_lblock(cur, block, level);
		if (diff == 0)
			keyno = 1;
		else {
			if (level > 0)
				kkbase = XFS_BMAP_KEY_IADDR(block, 1, cur);
			else
				krbase = XFS_BMAP_REC_IADDR(block, 1, cur);
			low = 1;
			if (!(high = block->bb_numrecs)) {
				ASSERT(level == 0);
				cur->bc_ptrs[0] = dir != XFS_LOOKUP_LE;
				return 0;
			}
			while (low <= high) {
				keyno = (low + high) >> 1;
				if (level > 0) {
					kkp = kkbase + keyno - 1;
					startoff = kkp->br_startoff;
				} else {
					krp = krbase + keyno - 1;
					startoff = xfs_bmbt_get_startoff(krp);
				}
				diff = (int)(startoff - rp->br_startoff);
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
			pp = XFS_BMAP_PTR_IADDR(block, keyno, cur);
			xfs_btree_check_lptr(cur, *pp, level);
			fsbno = *pp;
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
		    block->bb_rightsib != NULLDFSBNO) {
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

/*
 * Move 1 record left from cur/level if possible.
 * Update cur to reflect the new path.
 */
STATIC int
xfs_bmbt_lshift(
	xfs_btree_cur_t		*cur,
	int			level)
{
	int			first;
	int			i;
	xfs_bmbt_key_t		key;
	int			last;
	buf_t			*lbp;
	xfs_bmbt_block_t	*left;
	xfs_bmbt_key_t		*lkp;
	xfs_bmbt_ptr_t		*lpp;
	xfs_bmbt_rec_t		*lrp;
	xfs_mount_t		*mp;
	int			nrec;
	buf_t			*rbp;
	xfs_bmbt_block_t	*right;
	xfs_bmbt_key_t		*rkp;
	xfs_bmbt_ptr_t		*rpp;
	xfs_bmbt_rec_t		*rrp;
	xfs_trans_t		*tp;

	if (level == cur->bc_nlevels - 1)
		return 0;
	xfs_bmbt_rcheck(cur);
	rbp = cur->bc_bufs[level];
	right = XFS_BUF_TO_BMBT_BLOCK(rbp);
	xfs_btree_check_lblock(cur, right, level);
	if (right->bb_leftsib == NULLDFSBNO)
		return 0;
	if (cur->bc_ptrs[level] <= 1)
		return 0;
	tp = cur->bc_tp;
	mp = cur->bc_mp;
	lbp = xfs_btree_read_bufl(mp, tp, right->bb_leftsib, 0);
	left = XFS_BUF_TO_BMBT_BLOCK(lbp);
	xfs_btree_check_lblock(cur, left, level);
	if (left->bb_numrecs == XFS_BMAP_BLOCK_IMAXRECS(level, cur))
		return 0;
	nrec = left->bb_numrecs + 1;
	if (level > 0) {
		lkp = XFS_BMAP_KEY_IADDR(left, left->bb_numrecs + 1, cur);
		rkp = XFS_BMAP_KEY_IADDR(right, 1, cur);
		*lkp = *rkp;
		xfs_bmbt_log_keys(cur, lbp, nrec, nrec);
		lpp = XFS_BMAP_PTR_IADDR(left, left->bb_numrecs + 1, cur);
		rpp = XFS_BMAP_PTR_IADDR(right, 1, cur);
		xfs_btree_check_lptr(cur, *rpp, level);
		*lpp = *rpp;
		xfs_bmbt_log_ptrs(cur, lbp, nrec, nrec);
	} else {
		lrp = XFS_BMAP_REC_IADDR(left, left->bb_numrecs + 1, cur);
		rrp = XFS_BMAP_REC_IADDR(right, 1, cur);
		*lrp = *rrp;
		xfs_bmbt_log_recs(cur, lbp, nrec, nrec);
	}
	left->bb_numrecs++;
	xfs_bmbt_log_block(cur, lbp, XFS_BB_NUMRECS);
#ifdef DEBUG
	if (level > 0)
		xfs_btree_check_key(XFS_BTNUM_BMAP, lkp - 1, lkp);
	else
		xfs_btree_check_rec(XFS_BTNUM_BMAP, lrp - 1, lrp);
#endif
	right->bb_numrecs--;
	xfs_bmbt_log_block(cur, rbp, XFS_BB_NUMRECS);
	if (level > 0) {
		for (i = 0; i < right->bb_numrecs; i++) {
			rkp[i] = rkp[i + 1];
			xfs_btree_check_lptr(cur, rpp[i + 1], level);
			rpp[i] = rpp[i + 1];
		}
		xfs_bmbt_log_keys(cur, rbp, 1, right->bb_numrecs);
		xfs_bmbt_log_ptrs(cur, rbp, 1, right->bb_numrecs);
	} else {
		for (i = 0; i < right->bb_numrecs; i++)
			rrp[i] = rrp[i + 1];
		xfs_bmbt_log_recs(cur, rbp, 1, right->bb_numrecs);
		key.br_startoff = xfs_bmbt_get_startoff(rrp);
		rkp = &key;
	}
	xfs_bmbt_updkey(cur, rkp, level + 1);
	cur->bc_ptrs[level]--;
	xfs_bmbt_rcheck(cur);
	return 1;
}

#ifdef XFSDEBUG
STATIC xfs_fsblock_t
xfs_bmbt_rcheck_body(
	xfs_btree_cur_t		*cur,
	xfs_bmbt_block_t	*block,
	xfs_fsblock_t		*fbno,
	void			*rec,
	int			level)
{
	int			i;
	xfs_bmbt_key_t		*keyp;
	xfs_bmbt_key_t		*kp;
	xfs_bmbt_ptr_t		*pp;
	xfs_fsblock_t		rbno;
	xfs_bmbt_rec_t		*recp;
	xfs_bmbt_rec_t		*rp;

	xfs_btree_check_lblock(cur, block, level);
	if (fbno && block->bb_numrecs) {
		if (level > 0) {
			pp = XFS_BMAP_PTR_IADDR(block, 1, cur);
			xfs_btree_check_lptr(cur, *pp, level);
			*fbno = *pp;
		} else
			*fbno = NULLFSBLOCK;
	}
	rbno = block->bb_rightsib;
	if (level > 0)
		keyp = (xfs_bmbt_key_t *)rec;
	else
		recp = (xfs_bmbt_rec_t *)rec;
	for (i = 1; i <= block->bb_numrecs; i++) {
		if (level > 0) {
			kp = XFS_BMAP_KEY_IADDR(block, i, cur);
			if (i == 1 && !fbno)
				xfs_btree_check_key(XFS_BTNUM_BMAP, (void *)keyp, (void *)kp);
			else if (i > 1) {
				xfs_btree_check_key(XFS_BTNUM_BMAP, (void *)(kp - 1), (void *)kp);
				if (i == block->bb_numrecs)
					*keyp = *kp;
			}
			pp = XFS_BMAP_PTR_IADDR(block, i, cur);
			xfs_btree_check_lptr(cur, *pp, level);
		} else {
			rp = XFS_BMAP_REC_IADDR(block, i, cur);
			if (i == 1 && !fbno)
				xfs_btree_check_rec(XFS_BTNUM_BMAP, (void *)recp, (void *)rp);
			else if (i > 1) {
				xfs_btree_check_rec(XFS_BTNUM_BMAP, (void *)(rp - 1), (void *)rp);
				if (i == block->bb_numrecs)
					*recp = *rp;
			}
		}
	}
	return rbno;
}

STATIC xfs_fsblock_t
xfs_bmbt_rcheck_btree(
	xfs_btree_cur_t		*cur,
	xfs_fsblock_t		bno,
	xfs_fsblock_t		*fbno,
	void			*rec,
	int			level)
{
	xfs_bmbt_block_t	*block;
	buf_t			*bp;
	xfs_fsblock_t		rval;
	xfs_trans_t		*tp;

	tp = cur->bc_tp;
	bp = xfs_btree_read_bufl(cur->bc_mp, tp, bno, 0);
	block = XFS_BUF_TO_BMBT_BLOCK(bp);
	rval = xfs_bmbt_rcheck_body(cur, block, fbno, rec, level);
	xfs_trans_brelse(tp, bp);
	return rval;
}
#endif

/*
 * Read in the allocation group header (free/alloc section)
 */
buf_t *
xfs_bmbt_read_agf(
	xfs_mount_t	*mp,
	xfs_trans_t	*tp,
	xfs_agnumber_t	agno)
{
	buf_t		*bp;		/* return value */
	daddr_t		d;		/* disk block address */

	ASSERT(agno != NULLAGNUMBER);
	d = XFS_AG_DADDR(mp, agno, XFS_AGF_DADDR);
	bp = xfs_trans_read_buf(tp, mp->m_dev, d, 1, 0);
	ASSERT(bp && !geterror(bp));
	return bp;
}

/*
 * Move 1 record right from cur/level if possible.
 * Update cur to reflect the new path.
 */
STATIC int
xfs_bmbt_rshift(
	xfs_btree_cur_t		*cur,
	int			level)
{
	int			first;
	int			i;
	xfs_bmbt_key_t		key;
	int			last;
	buf_t			*lbp;
	xfs_bmbt_block_t	*left;
	xfs_bmbt_key_t		*lkp;
	xfs_bmbt_ptr_t		*lpp;
	xfs_bmbt_rec_t		*lrp;
	xfs_mount_t		*mp;
	buf_t			*rbp;
	xfs_bmbt_block_t	*right;
	xfs_bmbt_key_t		*rkp;
	xfs_bmbt_ptr_t		*rpp;
	xfs_bmbt_rec_t		*rrp;
	xfs_btree_cur_t		*tcur;
	xfs_trans_t		*tp;

	if (level == cur->bc_nlevels - 1)
		return 0;
	xfs_bmbt_rcheck(cur);
	lbp = cur->bc_bufs[level];
	left = XFS_BUF_TO_BMBT_BLOCK(lbp);
	xfs_btree_check_lblock(cur, left, level);
	if (left->bb_rightsib == NULLDFSBNO)
		return 0;
	if (cur->bc_ptrs[level] >= left->bb_numrecs)
		return 0;
	tp = cur->bc_tp;
	mp = cur->bc_mp;
	rbp = xfs_btree_read_bufl(mp, tp, left->bb_rightsib, 0);
	right = XFS_BUF_TO_BMBT_BLOCK(rbp);
	xfs_btree_check_lblock(cur, right, level);
	if (right->bb_numrecs == XFS_BMAP_BLOCK_IMAXRECS(level, cur))
		return 0;
	if (level > 0) {
		lkp = XFS_BMAP_KEY_IADDR(left, left->bb_numrecs, cur);
		lpp = XFS_BMAP_PTR_IADDR(left, left->bb_numrecs, cur);
		rkp = XFS_BMAP_KEY_IADDR(right, 1, cur);
		rpp = XFS_BMAP_PTR_IADDR(right, 1, cur);
		for (i = right->bb_numrecs - 1; i >= 0; i--) {
			rkp[i + 1] = rkp[i];
			xfs_btree_check_lptr(cur, rpp[i], level);
			rpp[i + 1] = rpp[i];
		}
		xfs_btree_check_lptr(cur, *lpp, level);
		*rkp = *lkp;
		*rpp = *lpp;
		xfs_bmbt_log_keys(cur, rbp, 1, right->bb_numrecs + 1);
		xfs_bmbt_log_ptrs(cur, rbp, 1, right->bb_numrecs + 1);
	} else {
		lrp = XFS_BMAP_REC_IADDR(left, left->bb_numrecs, cur);
		rrp = XFS_BMAP_REC_IADDR(right, 1, cur);
		for (i = right->bb_numrecs - 1; i >= 0; i--)
			rrp[i + 1] = rrp[i];
		*rrp = *lrp;
		xfs_bmbt_log_recs(cur, rbp, 1, right->bb_numrecs + 1);
		key.br_startoff = xfs_bmbt_get_startoff(rrp);
		rkp = &key;
	}
	left->bb_numrecs--;
	xfs_bmbt_log_block(cur, lbp, XFS_BB_NUMRECS);
	right->bb_numrecs++;
#ifdef DEBUG
	if (level > 0)
		xfs_btree_check_key(XFS_BTNUM_BMAP, rkp, rkp + 1);
	else
		xfs_btree_check_rec(XFS_BTNUM_BMAP, rrp, rrp + 1);
#endif
	xfs_bmbt_log_block(cur, rbp, XFS_BB_NUMRECS);
	tcur = xfs_btree_dup_cursor(cur);
	xfs_btree_lastrec(tcur, level);
	xfs_bmbt_increment(tcur, level);
	xfs_bmbt_updkey(tcur, rkp, level + 1);
	xfs_btree_del_cursor(tcur);
	xfs_bmbt_rcheck(cur);
	return 1;
}

/*
 * Split cur/level block in half.
 * Return new block number and its first record (to be inserted into parent).
 */
STATIC int
xfs_bmbt_split(
	xfs_btree_cur_t		*cur,
	int			level,
	xfs_fsblock_t		*bnop,
	xfs_bmbt_key_t		*keyp,
	xfs_btree_cur_t		**curp)
{
	xfs_fsblock_t		bno;
	daddr_t			d;
	int			first;
	int			i;
	xfs_inode_t		*ip;
	xfs_bmbt_key_t		key;
	int			last;
	xfs_fsblock_t		lbno;
	buf_t			*lbp;
	xfs_bmbt_block_t	*left;
	xfs_bmbt_key_t		*lkp;
	xfs_bmbt_ptr_t		*lpp;
	xfs_bmbt_rec_t		*lrp;
	xfs_mount_t		*mp;
	xfs_fsblock_t		rbno;
	buf_t			*rbp;
	xfs_bmbt_rec_t		rec;
	xfs_bmbt_block_t	*right;
	xfs_bmbt_key_t		*rkp;
	xfs_bmbt_ptr_t		*rpp;
	xfs_bmbt_block_t	*rrblock;
	buf_t			*rrbp;
	xfs_bmbt_rec_t		*rrp;
	xfs_trans_t		*tp;
	xfs_alloctype_t		type;

	xfs_bmbt_rcheck(cur);
	tp = cur->bc_tp;
	mp = cur->bc_mp;
	ip = cur->bc_private.b.ip;
	lbp = cur->bc_bufs[level];
	lbno = XFS_DADDR_TO_FSB(mp, lbp->b_blkno);
	left = XFS_BUF_TO_BMBT_BLOCK(lbp);
	bno = cur->bc_private.b.firstblock;
	if ((bno = cur->bc_private.b.firstblock) == NULLFSBLOCK) {
		bno = lbno;
		type = XFS_ALLOCTYPE_START_BNO;
	} else
		type = XFS_ALLOCTYPE_NEAR_BNO;
	rbno = xfs_alloc_extent(tp, bno, 1, type, 0, 0);
	if (rbno == NULLFSBLOCK)
		return 0;
	cur->bc_private.b.allocated++;
	ip->i_d.di_nblocks++;
	xfs_trans_log_inode(tp, ip, XFS_ILOG_CORE);
	rbp = xfs_btree_get_bufl(mp, tp, rbno, 0);
	right = XFS_BUF_TO_BMBT_BLOCK(rbp);
	xfs_btree_check_lblock(cur, left, level);
	right->bb_magic = XFS_BMAP_MAGIC;
	right->bb_level = left->bb_level;
	right->bb_numrecs = (__uint16_t)(left->bb_numrecs / 2);
	if ((left->bb_numrecs & 1) && cur->bc_ptrs[level] <= right->bb_numrecs + 1)
		right->bb_numrecs++;
	i = left->bb_numrecs - right->bb_numrecs + 1;
	if (level > 0) {
		lkp = XFS_BMAP_KEY_IADDR(left, i, cur);
		lpp = XFS_BMAP_PTR_IADDR(left, i, cur);
		rkp = XFS_BMAP_KEY_IADDR(right, 1, cur);
		rpp = XFS_BMAP_PTR_IADDR(right, 1, cur);
		for (i = 0; i < right->bb_numrecs; i++) {
			rkp[i] = lkp[i];
			xfs_btree_check_lptr(cur, lpp[i], level);
			rpp[i] = lpp[i];
		}
		xfs_bmbt_log_keys(cur, rbp, 1, right->bb_numrecs);
		xfs_bmbt_log_ptrs(cur, rbp, 1, right->bb_numrecs);
		*keyp = *rkp;
	} else {
		lrp = XFS_BMAP_REC_IADDR(left, i, cur);
		rrp = XFS_BMAP_REC_IADDR(right, 1, cur);
		for (i = 0; i < right->bb_numrecs; i++)
			rrp[i] = lrp[i];
		xfs_bmbt_log_recs(cur, rbp, 1, right->bb_numrecs);
		keyp->br_startoff = xfs_bmbt_get_startoff(rrp);
	}
	d = lbp->b_blkno;
	left->bb_numrecs -= right->bb_numrecs;
	right->bb_rightsib = left->bb_rightsib;
	left->bb_rightsib = rbno;
	right->bb_leftsib = lbno;
	xfs_bmbt_log_block(cur, rbp, XFS_BB_ALL_BITS);
	xfs_bmbt_log_block(cur, lbp, XFS_BB_NUMRECS | XFS_BB_RIGHTSIB);
	if (right->bb_rightsib != NULLDFSBNO) {
		rrbp = xfs_btree_read_bufl(mp, tp, right->bb_rightsib, 0);
		rrblock = XFS_BUF_TO_BMBT_BLOCK(rrbp);
		xfs_btree_check_lblock(cur, rrblock, level);
		rrblock->bb_leftsib = rbno;
		xfs_bmbt_log_block(cur, rrbp, XFS_BB_LEFTSIB);
	}
	if (cur->bc_ptrs[level] > left->bb_numrecs + 1) {
		xfs_btree_setbuf(cur, level, rbp);
		cur->bc_ptrs[level] -= left->bb_numrecs;
	}
	if (level + 1 < cur->bc_nlevels) {
		*curp = xfs_btree_dup_cursor(cur);
		(*curp)->bc_ptrs[level + 1]++;
	}
	*bnop = rbno;
	xfs_bmbt_rcheck(cur);
	return 1;
}

/*
 * Update keys for the record.
 */
STATIC void
xfs_bmbt_updkey(
	xfs_btree_cur_t		*cur,
	xfs_bmbt_key_t		*keyp,
	int			level)
{
	xfs_bmbt_block_t	*block;
	buf_t			*bp;
	xfs_bmbt_key_t		*kp;
	int			ptr;
	xfs_trans_t		*tp;

	ASSERT(level >= 1);
	xfs_bmbt_rcheck(cur);
	tp = cur->bc_tp;
	for (ptr = 1; ptr == 1 && level < cur->bc_nlevels; level++) {
		block = xfs_bmbt_get_block(cur, level, &bp);
		xfs_btree_check_lblock(cur, block, level);
		ptr = cur->bc_ptrs[level];
		kp = XFS_BMAP_KEY_IADDR(block, ptr, cur);
		*kp = *keyp;
		xfs_bmbt_log_keys(cur, bp, ptr, ptr);
	}
	xfs_bmbt_rcheck(cur);
}

/*
 * Convert on-disk form of btree root to in-memory form.
 */
void
xfs_bmdr_to_bmbt(
	xfs_bmdr_block_t	*dblock,
	int			dblocklen,
	xfs_bmbt_block_t	*rblock,
	int			rblocklen)
{
	int			dmxr;
	xfs_bmbt_key_t		*fkp;
	xfs_bmbt_ptr_t		*fpp;
	int			i;
	xfs_bmbt_key_t		*tkp;
	xfs_bmbt_ptr_t		*tpp;

	rblock->bb_magic = XFS_BMAP_MAGIC;
	rblock->bb_level = dblock->bb_level;
	ASSERT(rblock->bb_level > 0);
	rblock->bb_numrecs = dblock->bb_numrecs;
	rblock->bb_leftsib = rblock->bb_rightsib = NULLDFSBNO;
	dmxr = XFS_BTREE_BLOCK_MAXRECS(dblocklen, xfs_bmdr, 0);
	fkp = XFS_BTREE_KEY_ADDR(dblocklen, xfs_bmdr, dblock, 1, dmxr);
	tkp = XFS_BMAP_BROOT_KEY_ADDR(rblock, 1, rblocklen);
	fpp = XFS_BTREE_PTR_ADDR(dblocklen, xfs_bmdr, dblock, 1, dmxr);
	tpp = XFS_BMAP_BROOT_PTR_ADDR(rblock, 1, rblocklen);
	bcopy(fkp, tkp, sizeof(*fkp) * dblock->bb_numrecs);
	bcopy(fpp, tpp, sizeof(*fpp) * dblock->bb_numrecs);
}

/*
 * Decrement cursor by one record at the level.
 * For nonzero levels the leaf-ward information is untouched.
 */
int
xfs_bmbt_decrement(
	xfs_btree_cur_t		*cur,
	int			level)
{
	xfs_bmbt_block_t	*block;
	buf_t			*bp;
	xfs_fsblock_t		fsbno;
	int			lev;
	xfs_mount_t		*mp;
	xfs_bmbt_ptr_t		*pp;
	xfs_trans_t		*tp;

	if (--cur->bc_ptrs[level] > 0)
		return 1;
	block = xfs_bmbt_get_block(cur, level, &bp);
	xfs_btree_check_lblock(cur, block, level);
	if (block->bb_leftsib == NULLDFSBNO)
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
		block = xfs_bmbt_get_block(cur, lev, &bp);
		xfs_btree_check_lblock(cur, block, lev);
		pp = XFS_BMAP_PTR_IADDR(block, cur->bc_ptrs[lev], cur);
		xfs_btree_check_lptr(cur, *pp, lev);
		fsbno = *pp;
		bp = xfs_btree_read_bufl(mp, tp, fsbno, 0);
		xfs_btree_setbuf(cur, lev - 1, bp);
		block = XFS_BUF_TO_BMBT_BLOCK(bp);
		xfs_btree_check_lblock(cur, block, lev - 1);
		cur->bc_ptrs[lev - 1] = block->bb_numrecs;
	}
	return 1;
}

/*
 * Delete the record pointed to by cur.
 */
int
xfs_bmbt_delete(
	xfs_btree_cur_t	*cur)
{
	int		i;
	int		level;

	for (level = 0, i = 2; i == 2; level++)
		i = xfs_bmbt_delrec(cur, level);
	xfs_bmbt_kcheck(cur);
	return i;
}

/*
 * Convert a compressed bmap extent record to an uncompressed form.
 * This code must be in sync with the next three routines.
 */
void
xfs_bmbt_get_all(
	xfs_bmbt_rec_t	*r,
	xfs_bmbt_irec_t	*s)
{
#if XFS_BIG_FILES
	s->br_startoff = (((xfs_fileoff_t)r->l0) << 23) |
			 (((xfs_fileoff_t)r->l1) >> 9);
#else	/* !XFS_BIG_FILES */
#ifdef DEBUG
	{
		xfs_dfiloff_t	o;

		o = (((xfs_dfiloff_t)r->l0) << 23) |
		    (((xfs_dfiloff_t)r->l1) >> 9);
		ASSERT((o >> 32) == 0);
		s->br_startoff = (xfs_fileoff_t)o;
	}
#else	/* !DEBUG */
	s->br_startoff = (((xfs_fileoff_t)r->l0) << 23) |
			 (((xfs_fileoff_t)r->l1) >> 9);
#endif	/* DEBUG */
#endif	/* XFS_BIG_FILES */
#if XFS_BIG_FILESYSTEMS
	s->br_startblock = (((xfs_fsblock_t)(r->l1 & 0x000001ff)) << 43) | 
			   (((xfs_fsblock_t)r->l2) << 11) |
			   (((xfs_fsblock_t)r->l3) >> 21);
#else
#ifdef DEBUG
	{
		xfs_dfsbno_t	b;

		b = (((xfs_dfsbno_t)(r->l1 & 0x000001ff)) << 43) | 
		    (((xfs_dfsbno_t)r->l2) << 11) |
		    (((xfs_dfsbno_t)r->l3) >> 21);
		ASSERT((b >> 32) == 0 || ISNULLDSTARTBLOCK(b));
		s->br_startblock = (xfs_fsblock_t)b;
	}
#else	/* !DEBUG */
	s->br_startblock = (((xfs_fsblock_t)r->l2) << 11) |
			   (((xfs_fsblock_t)r->l3) >> 21);
#endif	/* DEBUG */
#endif	/* XFS_BIG_FILESYSTEMS */
	s->br_blockcount = (xfs_extlen_t)(r->l3 & 0x001fffff);
}

/*
 * Get the block pointer for the given level of the cursor.
 * Fill in the buffer pointer, if applicable.
 */
xfs_bmbt_block_t *
xfs_bmbt_get_block(
	xfs_btree_cur_t		*cur,
	int			level,
	buf_t			**bpp)
{
	xfs_bmbt_block_t	*rval;

	if (level < cur->bc_nlevels - 1) {
		*bpp = cur->bc_bufs[level];
		rval = XFS_BUF_TO_BMBT_BLOCK(*bpp);
	} else {
		*bpp = 0;
		rval = cur->bc_private.b.ip->i_broot;
	}
	return rval;
}

/*
 * Extract the blockcount field from a bmap extent record.
 */
xfs_extlen_t
xfs_bmbt_get_blockcount(
	xfs_bmbt_rec_t	*r)
{
	return (xfs_extlen_t)(r->l3 & 0x001fffff);
}

/*
 * Extract the startblock field from a bmap extent record.
 */
xfs_fsblock_t
xfs_bmbt_get_startblock(
	xfs_bmbt_rec_t	*r)
{
#if XFS_BIG_FILESYSTEMS
	return (((xfs_fsblock_t)(r->l1 & 0x000001ff)) << 43) | 
	       (((xfs_fsblock_t)r->l2) << 11) |
	       (((xfs_fsblock_t)r->l3) >> 21);
#else
#ifdef DEBUG
	xfs_dfsbno_t	b;

	b = (((xfs_dfsbno_t)(r->l1 & 0x000001ff)) << 43) | 
	    (((xfs_dfsbno_t)r->l2) << 11) |
	    (((xfs_dfsbno_t)r->l3) >> 21);
	ASSERT((b >> 32) == 0 || ISNULLDSTARTBLOCK(b));
	return (xfs_fsblock_t)b;
#else	/* !DEBUG */
	return (((xfs_fsblock_t)r->l2) << 11) | (((xfs_fsblock_t)r->l3) >> 21);
#endif	/* DEBUG */
#endif	/* XFS_BIG_FILESYSTEMS */
}

/*
 * Extract the startoff field from a bmap extent record.
 */
xfs_fileoff_t
xfs_bmbt_get_startoff(
	xfs_bmbt_rec_t	*r)
{
#if XFS_BIG_FILES
	return (((xfs_fileoff_t)r->l0) << 23) |
	       (((xfs_fileoff_t)r->l1) >> 9);
#else	/* !XFS_BIG_FILES */
#ifdef DEBUG
	xfs_dfiloff_t	o;

	o = (((xfs_dfiloff_t)r->l0) << 23) | (((xfs_dfiloff_t)r->l1) >> 9);
	ASSERT((o >> 32) == 0);
	return (xfs_fileoff_t)o;
#else	/* !DEBUG */
	return (((xfs_fileoff_t)r->l0) << 23) | (((xfs_fileoff_t)r->l1) >> 9);
#endif	/* DEBUG */
#endif	/* XFS_BIG_FILES */
}

/*
 * Increment cursor by one record at the level.
 * For nonzero levels the leaf-ward information is untouched.
 */
int
xfs_bmbt_increment(
	xfs_btree_cur_t		*cur,
	int			level)
{
	xfs_bmbt_block_t	*block;
	buf_t			*bp;
	xfs_fsblock_t		fsbno;
	int			lev;
	xfs_mount_t		*mp;
	xfs_bmbt_ptr_t		*pp;
	xfs_trans_t		*tp;

	block = xfs_bmbt_get_block(cur, level, &bp);
	xfs_btree_check_lblock(cur, block, level);
	if (++cur->bc_ptrs[level] <= block->bb_numrecs)
		return 1;
	if (block->bb_rightsib == NULLDFSBNO)
		return 0;
	for (lev = level + 1; lev < cur->bc_nlevels; lev++) {
		block = xfs_bmbt_get_block(cur, lev, &bp);
		xfs_btree_check_lblock(cur, block, lev);
		if (++cur->bc_ptrs[lev] <= block->bb_numrecs)
			break;
	}
	if (lev == cur->bc_nlevels)
		return 0;
	tp = cur->bc_tp;
	mp = cur->bc_mp;
	for (; lev > level; lev--) {
		block = xfs_bmbt_get_block(cur, lev, &bp);
		xfs_btree_check_lblock(cur, block, lev);
		pp = XFS_BMAP_PTR_IADDR(block, cur->bc_ptrs[lev], cur);
		xfs_btree_check_lptr(cur, *pp, lev);
		fsbno = *pp;
		bp = xfs_btree_read_bufl(mp, tp, fsbno, 0);
		xfs_btree_setbuf(cur, lev - 1, bp);
		cur->bc_ptrs[lev - 1] = 1;
	}
	return 1;
}

/*
 * Insert the current record at the point referenced by cur.
 */
int
xfs_bmbt_insert(
	xfs_btree_cur_t	*cur)
{
	int		i;
	int		level;
	xfs_fsblock_t	nbno;
	xfs_btree_cur_t	*ncur;
	xfs_bmbt_rec_t	nrec;
	xfs_btree_cur_t	*pcur;

	level = 0;
	nbno = NULLFSBLOCK;
	xfs_bmbt_set_all(&nrec, &cur->bc_rec.b);
	ncur = (xfs_btree_cur_t *)0;
	pcur = cur;
	do {
		i = xfs_bmbt_insrec(pcur, level++, &nbno, &nrec, &ncur);
		ASSERT(i == 1);
		if (pcur != cur && (ncur || nbno == NULLFSBLOCK)) {
			cur->bc_nlevels = pcur->bc_nlevels;
			cur->bc_private.b.allocated +=
				pcur->bc_private.b.allocated;
			pcur->bc_private.b.allocated = 0;
			ASSERT(cur->bc_private.b.firstblock != NULLFSBLOCK &&
			       cur->bc_private.b.firstblock ==
			       pcur->bc_private.b.firstblock);
			ASSERT(cur->bc_private.b.flist ==
			       pcur->bc_private.b.flist);
			xfs_btree_del_cursor(pcur);
		}
		if (ncur) {
			pcur = ncur;
			ncur = (xfs_btree_cur_t *)0;
		}
	} while (nbno != NULLFSBLOCK);
	return i;
}

#ifdef XFSDEBUG
/*
 * Debug routine to check key consistency.
 */
void
xfs_bmbt_kcheck(
	xfs_btree_cur_t		*cur)
{
	xfs_bmbt_block_t	*block;
	xfs_dinode_core_t	*dip;
	xfs_inode_t		*ip;
	int			level;
	int			levels;

	ip = cur->bc_private.b.ip;
	dip = &ip->i_d;
	ASSERT(dip->di_format == XFS_DINODE_FMT_BTREE);
	block = ip->i_broot;
	level = block->bb_level;
	levels = level + 1;
	ASSERT(levels == cur->bc_nlevels);
	xfs_bmbt_kcheck_body(cur, block, level, (xfs_bmbt_key_t *)0);
}
#endif	/* XFSDEBUG */

/*
 * Log fields from the btree block header.
 */
void
xfs_bmbt_log_block(
	xfs_btree_cur_t		*cur,
	buf_t			*bp,
	int			fields)
{
	int			first;
	int			last;
	xfs_trans_t		*tp;
	static const int	offsets[] = {
		offsetof(xfs_bmbt_block_t, bb_magic),
		offsetof(xfs_bmbt_block_t, bb_level),
		offsetof(xfs_bmbt_block_t, bb_numrecs),
		offsetof(xfs_bmbt_block_t, bb_leftsib),
		offsetof(xfs_bmbt_block_t, bb_rightsib),
		sizeof(xfs_bmbt_block_t)
	};

	tp = cur->bc_tp;
	if (bp) {
		xfs_btree_offsets(fields, offsets, XFS_BB_NUM_BITS, &first,
				  &last);
		xfs_trans_log_buf(tp, bp, first, last);
	} else
		xfs_trans_log_inode(tp, cur->bc_private.b.ip, XFS_ILOG_BROOT);
}

/*
 * Log record values from the btree block.
 */
void
xfs_bmbt_log_recs(
	xfs_btree_cur_t		*cur,
	buf_t			*bp,
	int			rfirst,
	int			rlast)
{
	xfs_bmbt_block_t	*block;
	int			first;
	int			last;
	xfs_bmbt_rec_t		*rp;
	xfs_trans_t		*tp;

	ASSERT(bp);
	tp = cur->bc_tp;
	block = XFS_BUF_TO_BMBT_BLOCK(bp);
	rp = XFS_BMAP_REC_DADDR(block, 1, cur);
	first = (caddr_t)&rp[rfirst - 1] - (caddr_t)block;
	last = ((caddr_t)&rp[rlast] - 1) - (caddr_t)block;
	xfs_trans_log_buf(tp, bp, first, last);
}

int
xfs_bmbt_lookup_eq(
	xfs_btree_cur_t	*cur,
	xfs_fileoff_t	off,
	xfs_fsblock_t	bno,
	xfs_extlen_t	len)
{
	cur->bc_rec.b.br_startoff = off;
	cur->bc_rec.b.br_startblock = bno;
	cur->bc_rec.b.br_blockcount = len;
	return xfs_bmbt_lookup(cur, XFS_LOOKUP_EQ);
}

int
xfs_bmbt_lookup_ge(
	xfs_btree_cur_t	*cur,
	xfs_fileoff_t	off,
	xfs_fsblock_t	bno,
	xfs_extlen_t	len)
{
	cur->bc_rec.b.br_startoff = off;
	cur->bc_rec.b.br_startblock = bno;
	cur->bc_rec.b.br_blockcount = len;
	return xfs_bmbt_lookup(cur, XFS_LOOKUP_GE);
}

int
xfs_bmbt_lookup_le(
	xfs_btree_cur_t	*cur,
	xfs_fileoff_t	off,
	xfs_fsblock_t	bno,
	xfs_extlen_t	len)
{
	cur->bc_rec.b.br_startoff = off;
	cur->bc_rec.b.br_startblock = bno;
	cur->bc_rec.b.br_blockcount = len;
	return xfs_bmbt_lookup(cur, XFS_LOOKUP_LE);
}

#ifdef XFSDEBUG
void
xfs_bmbt_rcheck(
	xfs_btree_cur_t		*cur)
{
	xfs_bmbt_block_t	*block;
	xfs_fsblock_t		bno;
	xfs_dinode_core_t	*dip;
	xfs_fsblock_t		fbno;
	xfs_inode_t		*ip;
	xfs_bmbt_key_t		key;
	int			level;
	xfs_mount_t		*mp;
	xfs_bmbt_rec_t		rec;
	void			*rp;
	xfs_sb_t		*sbp;

	mp = cur->bc_mp;
	sbp = &mp->m_sb;
	ip = cur->bc_private.b.ip;
	dip = &ip->i_d;
	ASSERT(dip->di_format == XFS_DINODE_FMT_BTREE);
	block = ip->i_broot;
	level = cur->bc_nlevels - 1;
	rp = level ? (void *)&key : (void *)&rec;
	xfs_bmbt_rcheck_body(cur, block, &bno, rp, level);
	for (level = block->bb_level - 1; level >= 0; level--, bno = fbno) {
		rp = level ? (void *)&key : (void *)&rec;
		bno = xfs_bmbt_rcheck_btree(cur, bno, &fbno, rp, level);
		while (bno != NULLFSBLOCK) {
			ASSERT(XFS_FSB_TO_AGNO(mp, bno) < sbp->sb_agcount);
			ASSERT(XFS_FSB_TO_AGBNO(mp, bno) < sbp->sb_agblocks);
			bno = xfs_bmbt_rcheck_btree(cur, bno, (xfs_bmbt_ptr_t *)0, rp, level);
		}
	}
}
#endif	/* XFSDEBUG */

/*
 * Set all the fields in a bmap extent record from the uncompressed form.
 */
void
xfs_bmbt_set_all(
	xfs_bmbt_rec_t	*r,
	xfs_bmbt_irec_t	*s)
{
#if XFS_BIG_FILES
	ASSERT((s->br_startoff & ~((1LL << 55) - 1)) == 0);
#endif
#if XFS_BIG_FILESYSTEMS
	ASSERT((s->br_startblock & ~((1LL << 52) - 1)) == 0);
#endif
	ASSERT((s->br_blockcount & 0xffe00000) == 0);
	r->l0 = (__uint32_t)(s->br_startoff >> 23);
	r->l3 = (((__uint32_t)s->br_startblock) << 21) |
		((__uint32_t)(s->br_blockcount & 0x001fffff));
#if XFS_BIG_FILESYSTEMS
	r->l1 = (((__uint32_t)s->br_startoff) << 9) |
		((__uint32_t)(s->br_startblock >> 43));
	r->l2 = (__uint32_t)(s->br_startblock >> 11);
#else
	if (ISNULLSTARTBLOCK(s->br_startblock)) {
		r->l1 = (__uint32_t)(s->br_startoff << 9) | 0x000001ff;
		r->l2 = 0xffe00000 | (__uint32_t)(s->br_startblock >> 11);
	} else {
		r->l1 = (__uint32_t)(s->br_startoff << 9);
		r->l2 = (__uint32_t)(s->br_startblock >> 11);
	}
#endif
}

/*
 * Set the blockcount field in a bmap extent record.
 */
void
xfs_bmbt_set_blockcount(
	xfs_bmbt_rec_t	*r,
	xfs_extlen_t	v)
{
	ASSERT((v & ~((1 << 21) - 1)) == 0);
	r->l3 = (r->l3 & 0xffe00000) | ((__uint32_t)v & 0x001fffff);
}

/*
 * Set the startblock field in a bmap extent record.
 */
void
xfs_bmbt_set_startblock(
	xfs_bmbt_rec_t	*r,
	xfs_fsblock_t	v)
{
#if XFS_BIG_FILESYSTEMS
	ASSERT((v & ~((1LL << 52) - 1)) == 0);
	r->l1 = (r->l1 & 0xfffffe00) | (__uint32_t)(v >> 43);
	r->l2 = (__uint32_t)(v >> 11);
#else
	if (ISNULLSTARTBLOCK(v)) {
		r->l1 |= 0x000001ff;
		r->l2 = 0xffe00000 | (__uint32_t)(v >> 11);
	} else {
		r->l1 &= ~0x000001ff;
		r->l2 = (__uint32_t)(v >> 11);
	}
#endif	/* XFS_BIG_FILESYSTEMS */
	r->l3 = (r->l3 & 0x001fffff) | (((__uint32_t)v) << 21);
}

/*
 * Set the startoff field in a bmap extent record.
 */
void
xfs_bmbt_set_startoff(
	xfs_bmbt_rec_t	*r,
	xfs_fileoff_t	v)
{
#if XFS_BIG_FILES
	ASSERT((v & ~((1LL << 55) - 1)) == 0);
#endif
	r->l0 = (__uint32_t)(v >> 23);
	r->l1 = (r->l1 & 0x000001ff) | (((__uint32_t)v) << 9);
}

/*
 * Convert in-memory form of btree root to on-disk form.
 */
void
xfs_bmbt_to_bmdr(
	xfs_bmbt_block_t	*rblock,
	int			rblocklen,
	xfs_bmdr_block_t	*dblock,
	int			dblocklen)
{
	int			dmxr;
	xfs_bmbt_key_t		*fkp;
	xfs_bmbt_ptr_t		*fpp;
	int			i;
	xfs_bmbt_key_t		*tkp;
	xfs_bmbt_ptr_t		*tpp;

	ASSERT(rblock->bb_magic == XFS_BMAP_MAGIC);
	ASSERT(rblock->bb_leftsib == NULLDFSBNO);
	ASSERT(rblock->bb_rightsib == NULLDFSBNO);
	ASSERT(rblock->bb_level > 0);
	dblock->bb_level = rblock->bb_level;
	dblock->bb_numrecs = rblock->bb_numrecs;
	dmxr = XFS_BTREE_BLOCK_MAXRECS(dblocklen, xfs_bmdr, 0);
	fkp = XFS_BMAP_BROOT_KEY_ADDR(rblock, 1, rblocklen);
	tkp = XFS_BTREE_KEY_ADDR(dblocklen, xfs_bmdr, dblock, 1, dmxr);
	fpp = XFS_BMAP_BROOT_PTR_ADDR(rblock, 1, rblocklen);
	tpp = XFS_BTREE_PTR_ADDR(dblocklen, xfs_bmdr, dblock, 1, dmxr);
	bcopy(fkp, tkp, sizeof(*fkp) * dblock->bb_numrecs);
	bcopy(fpp, tpp, sizeof(*fpp) * dblock->bb_numrecs);
}

/*
 * Update the record to the passed values.
 */
int
xfs_bmbt_update(
	xfs_btree_cur_t		*cur,
	xfs_fileoff_t		off,
	xfs_fsblock_t		bno,
	xfs_extlen_t		len)
{
	xfs_bmbt_block_t	*block;
	buf_t			*bp;
	int			first;
	xfs_bmbt_key_t		key;
	int			last;
	int			ptr;
	xfs_bmbt_rec_t		*rp;
	xfs_trans_t		*tp;

	xfs_bmbt_rcheck(cur);
	block = xfs_bmbt_get_block(cur, 0, &bp);
	xfs_btree_check_lblock(cur, block, 0);
	ptr = cur->bc_ptrs[0];
	tp = cur->bc_tp;
	rp = XFS_BMAP_REC_IADDR(block, ptr, cur);
	xfs_bmbt_set_startoff(rp, off);
	xfs_bmbt_set_startblock(rp, bno);
	xfs_bmbt_set_blockcount(rp, len);
	xfs_bmbt_log_recs(cur, bp, ptr, ptr);
	if (ptr > 1)
		return 1;
	key.br_startoff = off;
	xfs_bmbt_updkey(cur, &key, 1);
	xfs_bmbt_rcheck(cur);
	xfs_bmbt_kcheck(cur);
	return 1;
}
