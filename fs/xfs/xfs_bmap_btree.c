#ident	"$Revision: 1.43 $"

#include <sys/param.h>
#include <sys/vnode.h>
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
#include <sys/grio.h>
#include <sys/ktrace.h>
#ifdef SIM
#undef _KERNEL
#endif
#include "xfs_log.h"
#include "xfs_trans.h"
#include "xfs_sb.h"
#include "xfs_ag.h"
#include "xfs_mount.h"
#include "xfs_alloc_btree.h"
#include "xfs_bmap_btree.h"
#include "xfs_ialloc_btree.h"
#include "xfs_btree.h"
#include "xfs_ialloc.h"
#include "xfs_dinode.h"
#include "xfs_inode_item.h"
#include "xfs_inode.h"
#include "xfs_alloc.h"
#include "xfs_bmap.h"
#ifdef SIM
#include "sim.h"
#endif

#if !defined(SIM) || !defined(XFSDEBUG)
#define	kmem_check()	/* dummy for memory-allocation checking */
#endif

ktrace_t	*xfs_bmbt_trace_buf;
zone_t		*xfs_bmbt_locals_zone;

/*
 * Prototypes for internal btree functions.
 */

#ifdef XFSDEBUG

STATIC void
xfs_bmbt_kcheck_body(
	xfs_btree_cur_t		*cur,
	xfs_bmbt_block_t	*block,
	int			level,
	xfs_bmbt_key_t		*keyp);

STATIC void
xfs_bmbt_kcheck_btree(
	xfs_btree_cur_t		*cur,
	xfs_fsblock_t		bno,
	int			level,
	xfs_bmbt_key_t		*kp);

STATIC xfs_fsblock_t
xfs_bmbt_rcheck_body(
	xfs_btree_cur_t		*cur,
	xfs_bmbt_block_t	*block,
	xfs_fsblock_t		*fbno,
	void			*rec,
	int			level);

STATIC xfs_fsblock_t
xfs_bmbt_rcheck_btree(
	xfs_btree_cur_t		*cur,
	xfs_fsblock_t		bno,
	xfs_fsblock_t		*fbno,
	void			*rec,
	int			level);

#endif	/* XFSDEBUG */

/*
 * Allocate big local variables in a structure.
 */
STATIC xfs_bmbt_locals_t *
xfs_bmbt_locals_alloc(
	xfs_mount_t		*mp);

/*
 * Free big locals structure.
 */
STATIC void
xfs_bmbt_locals_free(
	xfs_bmbt_locals_t	*l);

/*
 * Delete record pointed to by cur/level.
 */
STATIC int
xfs_bmbt_delrec(
	xfs_btree_cur_t		*cur,
	int			level);

/* 
 * Get the data from the pointed-to record.
 */
STATIC int
xfs_bmbt_get_rec(
	xfs_btree_cur_t		*cur,
	xfs_fileoff_t		*off,
	xfs_fsblock_t		*bno,
	xfs_extlen_t		*len);

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
	xfs_btree_cur_t		**curp);

STATIC int
xfs_bmbt_killroot(
	xfs_btree_cur_t		*cur);

/*
 * Log key values from the btree block.
 */
STATIC void
xfs_bmbt_log_keys(
	xfs_btree_cur_t		*cur,
	buf_t			*bp,
	int			kfirst,
	int			klast);

/*
 * Log pointer values from the btree block.
 */
STATIC void
xfs_bmbt_log_ptrs(
	xfs_btree_cur_t	*cur,
	buf_t		*bp,
	int		pfirst,
	int		plast);

/*
 * Lookup the record.  The cursor is made to point to it, based on dir.
 */
STATIC int
xfs_bmbt_lookup(
	xfs_btree_cur_t		*cur,
	xfs_lookup_t		dir);

/*
 * Move 1 record left from cur/level if possible.
 * Update cur to reflect the new path.
 */
STATIC int
xfs_bmbt_lshift(
	xfs_btree_cur_t		*cur,
	int			level);

/*
 * Move 1 record right from cur/level if possible.
 * Update cur to reflect the new path.
 */
STATIC int
xfs_bmbt_rshift(
	xfs_btree_cur_t		*cur,
	int			level);

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
	xfs_btree_cur_t		**curp);

#if defined(DEBUG) && !defined(SIM)
/*
 * Add a trace buffer entry for arguments, for a buffer & 1 integer arg.
 */
STATIC void
xfs_bmbt_trace_argbi(
	char			*name,
	xfs_btree_cur_t		*cur,
	buf_t			*b,
	int			i);

/*
 * Add a trace buffer entry for arguments, for a buffer & 2 integer args.
 */
STATIC void
xfs_bmbt_trace_argbii(
	char			*name,
	xfs_btree_cur_t		*cur,
	buf_t			*b,
	int			i0,
	int			i1);

/*
 * Add a trace buffer entry for arguments, for 2 blocks & 1 integer arg.
 */
STATIC void
xfs_bmbt_trace_argffi(
	char			*name,
	xfs_btree_cur_t		*cur,
	xfs_dfiloff_t		o,
	xfs_dfsbno_t		b,
	int			i);

/*
 * Add a trace buffer entry for arguments, for one integer arg.
 */
STATIC void
xfs_bmbt_trace_argi(
	char			*name,
	xfs_btree_cur_t		*cur,
	int			i);

/*
 * Add a trace buffer entry for arguments, for int, fsblock, key.
 */
STATIC void
xfs_bmbt_trace_argifk(
	char			*name,
	xfs_btree_cur_t		*cur,
	int			i,
	xfs_fsblock_t		f,
	xfs_bmbt_key_t		*k);

/*
 * Add a trace buffer entry for arguments, for int, fsblock, rec.
 */
STATIC void
xfs_bmbt_trace_argifr(
	char			*name,
	xfs_btree_cur_t		*cur,
	int			i,
	xfs_fsblock_t		f,
	xfs_bmbt_rec_t		*r);

/*
 * Add a trace buffer entry for arguments, for int, key.
 */
STATIC void
xfs_bmbt_trace_argik(
	char			*name,
	xfs_btree_cur_t		*cur,
	int			i,
	xfs_bmbt_key_t		*k);

/*
 * Add a trace buffer entry for the cursor/operation.
 */
STATIC void
xfs_bmbt_trace_cursor(
	char			*name,
	xfs_btree_cur_t		*cur);

/*
 * Add a trace buffer entry for the arguments given to the routine,
 * generic form.
 */
STATIC void
xfs_bmbt_trace_enter(
	char			*name,
	xfs_btree_cur_t		*cur,
	int			type,
	int			a0,
	int			a1,
	int			a2,
	int			a3,
	int			a4,
	int			a5,
	int			a6,
	int			a7,
	int			a8,
	int			a9,
	int			a10,
	int			a11);
#else
#define	xfs_bmbt_trace_argbi(n,c,b,i)
#define	xfs_bmbt_trace_argbii(n,c,b,i,j)
#define	xfs_bmbt_trace_argffi(n,c,o,b,i)
#define	xfs_bmbt_trace_argi(n,c,i)
#define	xfs_bmbt_trace_argifk(n,c,i,f,k)
#define	xfs_bmbt_trace_argifr(n,c,i,f,r)
#define	xfs_bmbt_trace_argik(n,c,i,k)
#define	xfs_bmbt_trace_cursor(n,c)
#endif	/* DEBUG && !SIM */

/*
 * Update keys for the record.
 */
STATIC void
xfs_bmbt_updkey(
	xfs_btree_cur_t		*cur,
	xfs_bmbt_key_t		*keyp,
	int			level);

/*
 * Internal functions.
 */

/*
 * Allocate big local variables in a structure.
 */
STATIC xfs_bmbt_locals_t *
xfs_bmbt_locals_alloc(
	xfs_mount_t		*mp)
{
	xfs_bmbt_locals_t	*l;

	l = kmem_zone_alloc(xfs_bmbt_locals_zone, KM_SLEEP);
	l->mp = mp;
	return l;
}

/*
 * Free big locals structure.
 */
STATIC void
xfs_bmbt_locals_free(
	xfs_bmbt_locals_t	*l)
{
	kmem_zone_free(xfs_bmbt_locals_zone, l);
}

/*
 * Delete record pointed to by cur/level.
 */
STATIC int
xfs_bmbt_delrec(
	xfs_btree_cur_t		*cur,
	int			level)
{
	int			i;
	xfs_bmbt_locals_t	*l;

	xfs_bmbt_rcheck(cur);
	xfs_bmbt_trace_cursor("xfs_bmbt_delrec entry", cur);
	xfs_bmbt_trace_argi("xfs_bmbt_delrec args", cur, level);
	l = xfs_bmbt_locals_alloc(cur->bc_mp);
	l->ptr = cur->bc_ptrs[level];
	if (l->ptr == 0) {
		xfs_bmbt_locals_free(l);
		xfs_bmbt_trace_cursor("xfs_bmbt_delrec exit0", cur);
		return 0;
	}
	l->block = xfs_bmbt_get_block(cur, level, &l->bp);
	xfs_btree_check_lblock(cur, l->block, level);
	if (l->ptr > l->block->bb_numrecs) {
		xfs_bmbt_locals_free(l);
		xfs_bmbt_trace_cursor("xfs_bmbt_delrec exit1", cur);
		return 0;
	}
	if (level > 0) {
		l->kp = XFS_BMAP_KEY_IADDR(l->block, 1, cur);
		l->pp = XFS_BMAP_PTR_IADDR(l->block, 1, cur);
		for (i = l->ptr; i < l->block->bb_numrecs; i++) {
			l->kp[i - 1] = l->kp[i];
			xfs_btree_check_lptr(cur, l->pp[i], level);
			l->pp[i - 1] = l->pp[i];
		}
		if (l->ptr < i) {
			xfs_bmbt_log_ptrs(cur, l->bp, l->ptr, i - 1);
			xfs_bmbt_log_keys(cur, l->bp, l->ptr, i - 1);
		}
	} else {
		l->rp = XFS_BMAP_REC_IADDR(l->block, 1, cur);
		for (i = l->ptr; i < l->block->bb_numrecs; i++)
			l->rp[i - 1] = l->rp[i];
		if (l->ptr < i)
			xfs_bmbt_log_recs(cur, l->bp, l->ptr, i - 1);
		if (l->ptr == 1) {
			l->key.br_startoff = xfs_bmbt_get_startoff(l->rp);
			l->kp = &l->key;
		}
	}
	l->block->bb_numrecs--;
	xfs_bmbt_log_block(cur, l->bp, XFS_BB_NUMRECS);
	/*
	 * We're at the root level.
	 * First, shrink the root block in-memory.
	 * Try to get rid of the next level down.
	 * If we can't then there's nothing left to do.
	 */
	if (level == cur->bc_nlevels - 1) {
		xfs_iroot_realloc(cur->bc_private.b.ip, -1);
		i = xfs_bmbt_killroot(cur);
		if (level > 0)
			xfs_bmbt_decrement(cur, level);
		xfs_bmbt_locals_free(l);
		xfs_bmbt_rcheck(cur);
		xfs_bmbt_trace_cursor("xfs_bmbt_delrec exit2", cur);
		return i;
	}
	if (l->ptr == 1)
		xfs_bmbt_updkey(cur, l->kp, level + 1);
	if (l->block->bb_numrecs >= XFS_BMAP_BLOCK_IMINRECS(level, cur)) {
		if (level > 0)
			xfs_bmbt_decrement(cur, level);
		xfs_bmbt_locals_free(l);
		xfs_bmbt_rcheck(cur);
		xfs_bmbt_trace_cursor("xfs_bmbt_delrec exit3", cur);
		return 1;
	}
	l->rbno = l->block->bb_rightsib;
	l->lbno = l->block->bb_leftsib;
	/*
	 * One child of root, need to get a chance to copy its contents
	 * into the root and delete it. Can't go up to next level,
	 * there's nothing to delete there.
	 */
	if (l->lbno == NULLFSBLOCK && l->rbno == NULLFSBLOCK &&
	    level == cur->bc_nlevels - 2) {
		i = xfs_bmbt_killroot(cur);
		if (level > 0)
			xfs_bmbt_decrement(cur, level);
		xfs_bmbt_locals_free(l);
		xfs_bmbt_rcheck(cur);
		xfs_bmbt_trace_cursor("xfs_bmbt_delrec exit4", cur);
		return i;
	}
	ASSERT(l->rbno != NULLFSBLOCK || l->lbno != NULLFSBLOCK);
	l->tcur = xfs_btree_dup_cursor(cur);
	l->bno = NULLFSBLOCK;
	if (l->rbno != NULLFSBLOCK) {
		xfs_btree_lastrec(l->tcur, level);
		xfs_bmbt_increment(l->tcur, level);
		xfs_btree_lastrec(l->tcur, level);
		l->rbp = l->tcur->bc_bufs[level];
		l->right = XFS_BUF_TO_BMBT_BLOCK(l->rbp);
		xfs_btree_check_lblock(cur, l->right, level);
		l->bno = l->right->bb_leftsib;
		if (l->right->bb_numrecs - 1 >= XFS_BMAP_BLOCK_IMINRECS(level, cur)) {
			if (xfs_bmbt_lshift(l->tcur, level)) {
				ASSERT(l->block->bb_numrecs >= XFS_BMAP_BLOCK_IMINRECS(level, l->tcur));
				xfs_btree_del_cursor(l->tcur);
				if (level > 0)
					xfs_bmbt_decrement(cur, level);
				xfs_bmbt_locals_free(l);
				xfs_bmbt_rcheck(cur);
				xfs_bmbt_trace_cursor("xfs_bmbt_delrec exit5",
					cur);
				return 1;
			}
		}
		l->rrecs = l->right->bb_numrecs;
		if (l->lbno != NULLFSBLOCK) {
			xfs_btree_firstrec(l->tcur, level);
			xfs_bmbt_decrement(l->tcur, level);
		}
	}
	if (l->lbno != NULLFSBLOCK) {
		xfs_btree_firstrec(l->tcur, level);
		xfs_bmbt_decrement(l->tcur, level);	/* to last */
		xfs_btree_firstrec(l->tcur, level);
		l->lbp = l->tcur->bc_bufs[level];
		l->left = XFS_BUF_TO_BMBT_BLOCK(l->lbp);
		xfs_btree_check_lblock(cur, l->left, level);
		l->bno = l->left->bb_rightsib;
		if (l->left->bb_numrecs - 1 >= XFS_BMAP_BLOCK_IMINRECS(level, cur)) {
			if (xfs_bmbt_rshift(l->tcur, level)) {
				ASSERT(l->block->bb_numrecs >= XFS_BMAP_BLOCK_IMINRECS(level, l->tcur));
				xfs_btree_del_cursor(l->tcur);
				xfs_bmbt_locals_free(l);
				xfs_bmbt_rcheck(cur);
				xfs_bmbt_trace_cursor("xfs_bmbt_delrec exit6",
					cur);
				return 1;
			}
		}
		l->lrecs = l->left->bb_numrecs;
	}
	xfs_btree_del_cursor(l->tcur);
	ASSERT(l->bno != NULLFSBLOCK);
	if (l->lbno != NULLFSBLOCK &&
	    l->lrecs + l->block->bb_numrecs <= XFS_BMAP_BLOCK_IMAXRECS(level, cur)) {
		l->rbno = l->bno;
		l->right = l->block;
		l->rbp = l->bp;
		l->lbp = xfs_btree_read_bufl(l->mp, cur->bc_tp, l->lbno, 0);
		l->left = XFS_BUF_TO_BMBT_BLOCK(l->lbp);
		xfs_btree_check_lblock(cur, l->left, level);
	} else if (l->rbno != NULLFSBLOCK &&
		   l->rrecs + l->block->bb_numrecs <= XFS_BMAP_BLOCK_IMAXRECS(level, cur)) {
		l->lbno = l->bno;
		l->left = l->block;
		l->lbp = l->bp;
		l->rbp = xfs_btree_read_bufl(l->mp, cur->bc_tp, l->rbno, 0);
		l->right = XFS_BUF_TO_BMBT_BLOCK(l->rbp);
		l->lrecs = l->left->bb_numrecs;
		xfs_btree_check_lblock(cur, l->right, level);
	} else {
		if (level > 0)
			xfs_bmbt_decrement(cur, level);
		xfs_bmbt_locals_free(l);
		xfs_bmbt_rcheck(cur);
		xfs_bmbt_trace_cursor("xfs_bmbt_delrec exit7", cur);
		return 1;
	}
	if (level > 0) {
		l->lkp = XFS_BMAP_KEY_IADDR(l->left, l->left->bb_numrecs + 1, cur);
		l->lpp = XFS_BMAP_PTR_IADDR(l->left, l->left->bb_numrecs + 1, cur);
		l->rkp = XFS_BMAP_KEY_IADDR(l->right, 1, cur);
		l->rpp = XFS_BMAP_PTR_IADDR(l->right, 1, cur);
		for (i = 0; i < l->right->bb_numrecs; i++) {
			l->lkp[i] = l->rkp[i];
			xfs_btree_check_lptr(cur, l->rpp[i], level);
			l->lpp[i] = l->rpp[i];
		}
		xfs_bmbt_log_keys(cur, l->lbp, l->left->bb_numrecs + 1, l->left->bb_numrecs + l->right->bb_numrecs);
		xfs_bmbt_log_ptrs(cur, l->lbp, l->left->bb_numrecs + 1, l->left->bb_numrecs + l->right->bb_numrecs);
	} else {
		l->lrp = XFS_BMAP_REC_IADDR(l->left, l->left->bb_numrecs + 1, cur);
		l->rrp = XFS_BMAP_REC_IADDR(l->right, 1, cur);
		for (i = 0; i < l->right->bb_numrecs; i++)
			l->lrp[i] = l->rrp[i];
		xfs_bmbt_log_recs(cur, l->lbp, l->left->bb_numrecs + 1, l->left->bb_numrecs + l->right->bb_numrecs);
	}
	l->left->bb_numrecs += l->right->bb_numrecs;
	l->left->bb_rightsib = l->right->bb_rightsib;
	xfs_bmbt_log_block(cur, l->lbp, XFS_BB_RIGHTSIB | XFS_BB_NUMRECS);
	if (l->left->bb_rightsib != NULLDFSBNO) {
		l->rrbp = xfs_btree_read_bufl(l->mp, cur->bc_tp, l->left->bb_rightsib, 0);
		l->rrblock = XFS_BUF_TO_BMBT_BLOCK(l->rrbp);
		xfs_btree_check_lblock(cur, l->rrblock, level);
		l->rrblock->bb_leftsib = l->lbno;
		xfs_bmbt_log_block(cur, l->rrbp, XFS_BB_LEFTSIB);
	}
	xfs_bmap_add_free(XFS_DADDR_TO_FSB(l->mp, l->rbp->b_blkno), 1,
		cur->bc_private.b.flist, l->mp);
	cur->bc_private.b.ip->i_d.di_nblocks--;
	xfs_trans_log_inode(cur->bc_tp, cur->bc_private.b.ip, XFS_ILOG_CORE);
	xfs_trans_binval(cur->bc_tp, l->rbp);
	if (l->bp != l->lbp) {
		cur->bc_bufs[level] = l->lbp;
		cur->bc_ptrs[level] += l->lrecs;
	} else
		xfs_bmbt_increment(cur, level + 1);
	xfs_bmbt_locals_free(l);
	if (level > 0)
		cur->bc_ptrs[level]--;
	xfs_bmbt_rcheck(cur);
	xfs_bmbt_trace_cursor("xfs_bmbt_delrec exit8", cur);
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
	xfs_alloc_arg_t		*args;
	int			i;
	xfs_bmbt_locals_t	*l;

	ASSERT(level < cur->bc_nlevels);
	xfs_bmbt_rcheck(cur);
	xfs_bmbt_trace_cursor("xfs_bmbt_insrec entry", cur);
	xfs_bmbt_trace_argifr("xfs_bmbt_insrec args", cur, level, *bnop, recp);
	l = xfs_bmbt_locals_alloc(cur->bc_mp);
	l->ncur = (xfs_btree_cur_t *)0;
	l->key.br_startoff = xfs_bmbt_get_startoff(recp);
	l->optr = l->ptr = cur->bc_ptrs[level];
	if (l->ptr == 0) {
		xfs_bmbt_locals_free(l);
		xfs_bmbt_trace_cursor("xfs_bmbt_insrec exit0", cur);
		return 0;
	}
	l->block = xfs_bmbt_get_block(cur, level, &l->bp);
	xfs_btree_check_lblock(cur, l->block, level);
#ifdef DEBUG
	if (l->ptr <= l->block->bb_numrecs) {
		if (level == 0) {
			l->rp = XFS_BMAP_REC_IADDR(l->block, l->ptr, cur);
			xfs_btree_check_rec(XFS_BTNUM_BMAP, recp, l->rp);
		} else {
			l->kp = XFS_BMAP_KEY_IADDR(l->block, l->ptr, cur);
			xfs_btree_check_key(XFS_BTNUM_BMAP, &l->key, l->kp);
		}
	}
#endif
	l->nbno = NULLFSBLOCK;
	if (l->block->bb_numrecs == XFS_BMAP_BLOCK_IMAXRECS(level, cur)) {
		if (l->block->bb_numrecs < XFS_BMAP_BLOCK_DMAXRECS(level, cur)) {
			/*
			 * A root block, that can be made bigger.
			 */
			xfs_iroot_realloc(cur->bc_private.b.ip, 1);
			l->block = xfs_bmbt_get_block(cur, level, &l->bp);
		} else if (level == cur->bc_nlevels - 1) {
			args = xfs_alloc_arg_alloc();
			/*
			 * Copy the root into a real block.
			 */
			l->pp = XFS_BMAP_PTR_IADDR(l->block, 1, cur);
			args->tp = cur->bc_tp;
			args->mp = l->mp;
			args->fsbno = cur->bc_private.b.firstblock;
			args->mod = args->minleft = args->total = args->isfl =
				args->userdata = 0;
			args->minlen = args->maxlen = args->prod = 1;
			args->wasdel =
				cur->bc_private.b.flags & XFS_BTCUR_BPRV_WASDEL;
			if (args->fsbno == NULLFSBLOCK) {
				xfs_btree_check_lptr(cur, *l->pp, level);
				args->fsbno = *l->pp;
				args->type = XFS_ALLOCTYPE_START_BNO;
			} else if (args->wasdel)
				args->type = XFS_ALLOCTYPE_FIRST_AG;
			else
				args->type = XFS_ALLOCTYPE_NEAR_BNO;
			xfs_alloc_vextent(args);
			if (args->fsbno == NULLFSBLOCK) {
				xfs_alloc_arg_free(args);
				xfs_bmbt_locals_free(l);
				xfs_bmbt_trace_cursor("xfs_bmbt_insrec exit1",
					cur);
				return 0;
			}
			ASSERT(args->len == 1);
			cur->bc_private.b.firstblock = args->fsbno;
			cur->bc_private.b.allocated++;
			cur->bc_private.b.ip->i_d.di_nblocks++;
			l->bp = xfs_btree_get_bufl(l->mp, cur->bc_tp, args->fsbno, 0);
			l->cblock = XFS_BUF_TO_BMBT_BLOCK(l->bp);
			*l->cblock = *l->block;
			l->block->bb_level++;
			l->block->bb_numrecs = 1;
			cur->bc_nlevels++;
			cur->bc_ptrs[level + 1] = 1;
			l->kp = XFS_BMAP_KEY_IADDR(l->block, 1, cur);
			l->ckp = XFS_BMAP_KEY_IADDR(l->cblock, 1, cur);
			bcopy(l->kp, l->ckp, l->cblock->bb_numrecs * (int)sizeof(*l->kp));
			l->cpp = XFS_BMAP_PTR_IADDR(l->cblock, 1, cur);
#ifdef DEBUG
			for (i = 0; i < l->cblock->bb_numrecs; i++)
				xfs_btree_check_lptr(cur, l->pp[i], level);
#endif
			bcopy(l->pp, l->cpp, l->cblock->bb_numrecs * (int)sizeof(*l->pp));
			xfs_btree_check_lptr(cur, (xfs_bmbt_ptr_t)args->fsbno,
				level);
			*l->pp = args->fsbno;
			xfs_alloc_arg_free(args);
			xfs_iroot_realloc(cur->bc_private.b.ip, 1 - l->cblock->bb_numrecs);
			xfs_btree_setbuf(cur, level, l->bp);
			/*
			 * Do all this logging at the end so that 
			 * the root is at the right level.
			 */
			xfs_bmbt_log_block(cur, l->bp, XFS_BB_ALL_BITS);
			xfs_bmbt_log_keys(cur, l->bp, 1, l->cblock->bb_numrecs);
			xfs_bmbt_log_ptrs(cur, l->bp, 1, l->cblock->bb_numrecs);
			xfs_trans_log_inode(cur->bc_tp, cur->bc_private.b.ip,
				XFS_ILOG_CORE | XFS_ILOG_BROOT);
			l->block = l->cblock;
		} else {
			if (xfs_bmbt_rshift(cur, level)) {
				/* nothing */
			} else if (xfs_bmbt_lshift(cur, level)) {
				l->optr = l->ptr = cur->bc_ptrs[level];
			} else if (xfs_bmbt_split(cur, level, &l->nbno, &l->nkey, &l->ncur)) {
				l->block = xfs_bmbt_get_block(cur, level, &l->bp);
				xfs_btree_check_lblock(cur, l->block, level);
				l->ptr = cur->bc_ptrs[level];
				xfs_bmbt_set_startoff(&l->nrec, l->nkey.br_startoff);
				xfs_bmbt_set_startblock(&l->nrec, 0);
				xfs_bmbt_set_blockcount(&l->nrec, 0);
			} else {
				xfs_bmbt_locals_free(l);
				xfs_bmbt_trace_cursor("xfs_bmbt_insrec exit2",
					cur);
				return 0;
			}
		}
	}
	if (level > 0) {
		l->kp = XFS_BMAP_KEY_IADDR(l->block, 1, cur);
		l->pp = XFS_BMAP_PTR_IADDR(l->block, 1, cur);
		for (i = l->block->bb_numrecs; i >= l->ptr; i--) {
			l->kp[i] = l->kp[i - 1];
			xfs_btree_check_lptr(cur, l->pp[i - 1], level);
			l->pp[i] = l->pp[i - 1];
		}
		xfs_btree_check_lptr(cur, (xfs_bmbt_ptr_t)*bnop, level);
		l->kp[i] = l->key;
		l->pp[i] = *bnop;
		l->block->bb_numrecs++;
		xfs_bmbt_log_keys(cur, l->bp, l->ptr, l->block->bb_numrecs);
		xfs_bmbt_log_ptrs(cur, l->bp, l->ptr, l->block->bb_numrecs);
	} else {
		l->rp = XFS_BMAP_REC_IADDR(l->block, 1, cur);
		for (i = l->block->bb_numrecs; i >= l->ptr; i--)
			l->rp[i] = l->rp[i - 1];
		l->rp[i] = *recp;
		l->block->bb_numrecs++;
		xfs_bmbt_log_recs(cur, l->bp, l->ptr, l->block->bb_numrecs);
	}
	xfs_bmbt_log_block(cur, l->bp, XFS_BB_NUMRECS);
#ifdef DEBUG
	if (l->ptr < l->block->bb_numrecs) {
		if (level == 0)
			xfs_btree_check_rec(XFS_BTNUM_BMAP, l->rp + i, l->rp + i + 1);
		else
			xfs_btree_check_key(XFS_BTNUM_BMAP, l->kp + i, l->kp + i + 1);
	}
#endif
	if (l->optr == 1)
		xfs_bmbt_updkey(cur, &l->key, level + 1);
	*bnop = l->nbno;
	xfs_bmbt_rcheck(cur);
	if (l->nbno != NULLFSBLOCK) {
		*recp = l->nrec;
		*curp = l->ncur;
	} else
		xfs_bmbt_kcheck(cur);
	xfs_bmbt_locals_free(l);
	xfs_bmbt_trace_cursor("xfs_bmbt_insrec exit3", cur);
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
	xfs_bmbt_ptr_t		*pp;

	xfs_bmbt_trace_cursor("xfs_bmbt_killroot entry", cur);
	level = cur->bc_nlevels - 1;
	ASSERT(level >= 1);
	/*
	 * Don't deal with the root block needs to be a leaf case.
	 * We're just going to turn the thing back into extents anyway.
	 */
	if (level == 1) {
		xfs_bmbt_trace_cursor("xfs_bmbt_killroot exit0", cur);
		return 1;
	}
	block = xfs_bmbt_get_block(cur, level, &cbp);
	/*
	 * Give up if the root has multiple children.
	 */
	if (block->bb_numrecs != 1) {
		xfs_bmbt_trace_cursor("xfs_bmbt_killroot exit1", cur);
		return 1;
	}
	/*
	 * Only do this if the next level will fit.
	 * Then the data must be copied up to the inode,
	 * instead of freeing the root you free the next level.
	 */
	cbp = cur->bc_bufs[level - 1];
	cblock = XFS_BUF_TO_BMBT_BLOCK(cbp);
	if (cblock->bb_numrecs > XFS_BMAP_BLOCK_DMAXRECS(level, cur)) {
		xfs_bmbt_trace_cursor("xfs_bmbt_killroot exit2", cur);
		return 1;
	}
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
	block->bb_numrecs += i;
	ASSERT(block->bb_numrecs == cblock->bb_numrecs);
	kp = XFS_BMAP_KEY_IADDR(block, 1, cur);
	ckp = XFS_BMAP_KEY_IADDR(cblock, 1, cur);
	bcopy((caddr_t)ckp, (caddr_t)kp, block->bb_numrecs * (int)sizeof(*kp));
	pp = XFS_BMAP_PTR_IADDR(block, 1, cur);
	cpp = XFS_BMAP_PTR_IADDR(cblock, 1, cur);
#ifdef DEBUG
	for (i = 0; i < cblock->bb_numrecs; i++)
		xfs_btree_check_lptr(cur, cpp[i], level - 1);
#endif
	bcopy((caddr_t)cpp, (caddr_t)pp, block->bb_numrecs * (int)sizeof(*pp));
	xfs_bmap_add_free(XFS_DADDR_TO_FSB(cur->bc_mp, cbp->b_blkno), 1,
		cur->bc_private.b.flist, cur->bc_mp);
	ip->i_d.di_nblocks--;
	xfs_trans_binval(cur->bc_tp, cbp);
	cur->bc_bufs[level - 1] = NULL;
	block->bb_level--;
	xfs_trans_log_inode(cur->bc_tp, ip, XFS_ILOG_CORE | XFS_ILOG_BROOT);
	cur->bc_nlevels--;
	xfs_bmbt_rcheck(cur);
	xfs_bmbt_trace_cursor("xfs_bmbt_killroot exit3", cur);
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

	xfs_bmbt_trace_cursor("xfs_bmbt_log_keys entry", cur);
	xfs_bmbt_trace_argbii("xfs_bmbt_log_keys args", cur, bp, kfirst, klast);
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
	xfs_bmbt_trace_cursor("xfs_bmbt_log_keys exit", cur);
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

	xfs_bmbt_trace_cursor("xfs_bmbt_log_ptrs entry", cur);
	xfs_bmbt_trace_argbii("xfs_bmbt_log_ptrs args", cur, bp, pfirst, plast);
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
	xfs_bmbt_trace_cursor("xfs_bmbt_log_ptrs exit", cur);
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
	xfs_bmbt_trace_cursor("xfs_bmbt_lookup entry", cur);
	xfs_bmbt_trace_argi("xfs_bmbt_lookup args", cur, (int)dir);
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
				ASSERT(bp);
				ASSERT(!geterror(bp));
				xfs_btree_setbuf(cur, level, bp);
			}
			ASSERT(cur->bc_bufs[level]);
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
				xfs_bmbt_trace_cursor("xfs_bmbt_lookup exit0",
					cur);
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
			xfs_bmbt_trace_cursor("xfs_bmbt_lookup exit1", cur);
			return 1;
		}
	}
	else if (dir == XFS_LOOKUP_LE && diff > 0)
		keyno--;
	cur->bc_ptrs[0] = keyno;
	if (keyno == 0 || keyno > block->bb_numrecs) {
		xfs_bmbt_trace_cursor("xfs_bmbt_lookup exit2", cur);
		return 0;
	} else {
		xfs_bmbt_trace_cursor("xfs_bmbt_lookup exit3", cur);
		return dir != XFS_LOOKUP_EQ || diff == 0;
	}
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
	int			i;
	xfs_bmbt_locals_t	*l;

	xfs_bmbt_trace_cursor("xfs_bmbt_lshift entry", cur);
	xfs_bmbt_trace_argi("xfs_bmbt_lshift args", cur, level);
	if (level == cur->bc_nlevels - 1) {
		xfs_bmbt_trace_cursor("xfs_bmbt_lshift exit0", cur);
		return 0;
	}
	xfs_bmbt_rcheck(cur);
	l = xfs_bmbt_locals_alloc(cur->bc_mp);
	l->rbp = cur->bc_bufs[level];
	l->right = XFS_BUF_TO_BMBT_BLOCK(l->rbp);
	xfs_btree_check_lblock(cur, l->right, level);
	if (l->right->bb_leftsib == NULLDFSBNO) {
		xfs_bmbt_locals_free(l);
		xfs_bmbt_trace_cursor("xfs_bmbt_lshift exit1", cur);
		return 0;
	}
	if (cur->bc_ptrs[level] <= 1) {
		xfs_bmbt_locals_free(l);
		xfs_bmbt_trace_cursor("xfs_bmbt_lshift exit2", cur);
		return 0;
	}
	l->lbp = xfs_btree_read_bufl(l->mp, cur->bc_tp, l->right->bb_leftsib, 0);
	l->left = XFS_BUF_TO_BMBT_BLOCK(l->lbp);
	xfs_btree_check_lblock(cur, l->left, level);
	if (l->left->bb_numrecs == XFS_BMAP_BLOCK_IMAXRECS(level, cur)) {
		xfs_bmbt_locals_free(l);
		xfs_bmbt_trace_cursor("xfs_bmbt_lshift exit3", cur);
		return 0;
	}
	l->lrecs = l->left->bb_numrecs + 1;
	if (level > 0) {
		l->lkp = XFS_BMAP_KEY_IADDR(l->left, l->left->bb_numrecs + 1, cur);
		l->rkp = XFS_BMAP_KEY_IADDR(l->right, 1, cur);
		*l->lkp = *l->rkp;
		xfs_bmbt_log_keys(cur, l->lbp, l->lrecs, l->lrecs);
		l->lpp = XFS_BMAP_PTR_IADDR(l->left, l->left->bb_numrecs + 1, cur);
		l->rpp = XFS_BMAP_PTR_IADDR(l->right, 1, cur);
		xfs_btree_check_lptr(cur, *l->rpp, level);
		*l->lpp = *l->rpp;
		xfs_bmbt_log_ptrs(cur, l->lbp, l->lrecs, l->lrecs);
	} else {
		l->lrp = XFS_BMAP_REC_IADDR(l->left, l->left->bb_numrecs + 1, cur);
		l->rrp = XFS_BMAP_REC_IADDR(l->right, 1, cur);
		*l->lrp = *l->rrp;
		xfs_bmbt_log_recs(cur, l->lbp, l->lrecs, l->lrecs);
	}
	l->left->bb_numrecs++;
	xfs_bmbt_log_block(cur, l->lbp, XFS_BB_NUMRECS);
#ifdef DEBUG
	if (level > 0)
		xfs_btree_check_key(XFS_BTNUM_BMAP, l->lkp - 1, l->lkp);
	else
		xfs_btree_check_rec(XFS_BTNUM_BMAP, l->lrp - 1, l->lrp);
#endif
	l->right->bb_numrecs--;
	xfs_bmbt_log_block(cur, l->rbp, XFS_BB_NUMRECS);
	if (level > 0) {
		for (i = 0; i < l->right->bb_numrecs; i++) {
			l->rkp[i] = l->rkp[i + 1];
			xfs_btree_check_lptr(cur, l->rpp[i + 1], level);
			l->rpp[i] = l->rpp[i + 1];
		}
		xfs_bmbt_log_keys(cur, l->rbp, 1, l->right->bb_numrecs);
		xfs_bmbt_log_ptrs(cur, l->rbp, 1, l->right->bb_numrecs);
	} else {
		for (i = 0; i < l->right->bb_numrecs; i++)
			l->rrp[i] = l->rrp[i + 1];
		xfs_bmbt_log_recs(cur, l->rbp, 1, l->right->bb_numrecs);
		l->key.br_startoff = xfs_bmbt_get_startoff(l->rrp);
		l->rkp = &l->key;
	}
	xfs_bmbt_updkey(cur, l->rkp, level + 1);
	cur->bc_ptrs[level]--;
	xfs_bmbt_locals_free(l);
	xfs_bmbt_rcheck(cur);
	xfs_bmbt_trace_cursor("xfs_bmbt_lshift exit4", cur);
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
	ASSERT(bp);
	ASSERT(!geterror(bp));
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
	int			i;
	xfs_bmbt_locals_t	*l;

	xfs_bmbt_trace_cursor("xfs_bmbt_rshift entry", cur);
	xfs_bmbt_trace_argi("xfs_bmbt_rshift args", cur, level);
	if (level == cur->bc_nlevels - 1) {
		xfs_bmbt_trace_cursor("xfs_bmbt_rshift exit0", cur);
		return 0;
	}
	xfs_bmbt_rcheck(cur);
	l = xfs_bmbt_locals_alloc(cur->bc_mp);
	l->lbp = cur->bc_bufs[level];
	l->left = XFS_BUF_TO_BMBT_BLOCK(l->lbp);
	xfs_btree_check_lblock(cur, l->left, level);
	if (l->left->bb_rightsib == NULLDFSBNO) {
		xfs_bmbt_locals_free(l);
		xfs_bmbt_trace_cursor("xfs_bmbt_rshift exit1", cur);
		return 0;
	}
	if (cur->bc_ptrs[level] >= l->left->bb_numrecs) {
		xfs_bmbt_locals_free(l);
		xfs_bmbt_trace_cursor("xfs_bmbt_rshift exit2", cur);
		return 0;
	}
	l->rbp = xfs_btree_read_bufl(l->mp, cur->bc_tp, l->left->bb_rightsib, 0);
	l->right = XFS_BUF_TO_BMBT_BLOCK(l->rbp);
	xfs_btree_check_lblock(cur, l->right, level);
	if (l->right->bb_numrecs == XFS_BMAP_BLOCK_IMAXRECS(level, cur)) {
		xfs_bmbt_locals_free(l);
		xfs_bmbt_trace_cursor("xfs_bmbt_rshift exit3", cur);
		return 0;
	}
	if (level > 0) {
		l->lkp = XFS_BMAP_KEY_IADDR(l->left, l->left->bb_numrecs, cur);
		l->lpp = XFS_BMAP_PTR_IADDR(l->left, l->left->bb_numrecs, cur);
		l->rkp = XFS_BMAP_KEY_IADDR(l->right, 1, cur);
		l->rpp = XFS_BMAP_PTR_IADDR(l->right, 1, cur);
		for (i = l->right->bb_numrecs - 1; i >= 0; i--) {
			l->rkp[i + 1] = l->rkp[i];
			xfs_btree_check_lptr(cur, l->rpp[i], level);
			l->rpp[i + 1] = l->rpp[i];
		}
		xfs_btree_check_lptr(cur, *l->lpp, level);
		*l->rkp = *l->lkp;
		*l->rpp = *l->lpp;
		xfs_bmbt_log_keys(cur, l->rbp, 1, l->right->bb_numrecs + 1);
		xfs_bmbt_log_ptrs(cur, l->rbp, 1, l->right->bb_numrecs + 1);
	} else {
		l->lrp = XFS_BMAP_REC_IADDR(l->left, l->left->bb_numrecs, cur);
		l->rrp = XFS_BMAP_REC_IADDR(l->right, 1, cur);
		for (i = l->right->bb_numrecs - 1; i >= 0; i--)
			l->rrp[i + 1] = l->rrp[i];
		*l->rrp = *l->lrp;
		xfs_bmbt_log_recs(cur, l->rbp, 1, l->right->bb_numrecs + 1);
		l->key.br_startoff = xfs_bmbt_get_startoff(l->rrp);
		l->rkp = &l->key;
	}
	l->left->bb_numrecs--;
	xfs_bmbt_log_block(cur, l->lbp, XFS_BB_NUMRECS);
	l->right->bb_numrecs++;
#ifdef DEBUG
	if (level > 0)
		xfs_btree_check_key(XFS_BTNUM_BMAP, l->rkp, l->rkp + 1);
	else
		xfs_btree_check_rec(XFS_BTNUM_BMAP, l->rrp, l->rrp + 1);
#endif
	xfs_bmbt_log_block(cur, l->rbp, XFS_BB_NUMRECS);
	l->tcur = xfs_btree_dup_cursor(cur);
	xfs_btree_lastrec(l->tcur, level);
	xfs_bmbt_increment(l->tcur, level);
	xfs_bmbt_updkey(l->tcur, l->rkp, level + 1);
	xfs_btree_del_cursor(l->tcur);
	xfs_bmbt_locals_free(l);
	xfs_bmbt_rcheck(cur);
	xfs_bmbt_trace_cursor("xfs_bmbt_rshift exit4", cur);
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
	xfs_alloc_arg_t		*args;
	int			i;
	xfs_bmbt_locals_t	*l;

	xfs_bmbt_rcheck(cur);
	xfs_bmbt_trace_cursor("xfs_bmbt_split entry", cur);
	xfs_bmbt_trace_argifk("xfs_bmbt_split args", cur, level, *bnop, keyp);
	args = xfs_alloc_arg_alloc();
	args->tp = cur->bc_tp;
	l = xfs_bmbt_locals_alloc(args->mp = cur->bc_mp);
	l->lbp = cur->bc_bufs[level];
	l->lbno = XFS_DADDR_TO_FSB(l->mp, l->lbp->b_blkno);
	l->left = XFS_BUF_TO_BMBT_BLOCK(l->lbp);
	args->fsbno = cur->bc_private.b.firstblock;
	if (args->fsbno == NULLFSBLOCK) {
		args->fsbno = l->lbno;
		args->type = XFS_ALLOCTYPE_START_BNO;
	} else if (cur->bc_private.b.flags & XFS_BTCUR_BPRV_LOWSPC)
		args->type = XFS_ALLOCTYPE_FIRST_AG;
	else
		args->type = XFS_ALLOCTYPE_NEAR_BNO;
	args->mod = args->minleft = args->total = args->isfl =
		args->userdata = 0;
	args->minlen = args->maxlen = args->prod = 1;
	args->wasdel = cur->bc_private.b.flags & XFS_BTCUR_BPRV_WASDEL;
	xfs_alloc_vextent(args);
	if (args->fsbno == NULLFSBLOCK) {
		xfs_bmbt_locals_free(l);
		xfs_alloc_arg_free(args);
		xfs_bmbt_trace_cursor("xfs_bmbt_split exit0", cur);
		return 0;
	}
	ASSERT(args->len == 1);
	cur->bc_private.b.firstblock = args->fsbno;
	cur->bc_private.b.allocated++;
	cur->bc_private.b.ip->i_d.di_nblocks++;
	xfs_trans_log_inode(args->tp, cur->bc_private.b.ip, XFS_ILOG_CORE);
	l->rbp = xfs_btree_get_bufl(l->mp, args->tp, args->fsbno, 0);
	l->right = XFS_BUF_TO_BMBT_BLOCK(l->rbp);
	xfs_btree_check_lblock(cur, l->left, level);
	l->right->bb_magic = XFS_BMAP_MAGIC;
	l->right->bb_level = l->left->bb_level;
	l->right->bb_numrecs = (__uint16_t)(l->left->bb_numrecs / 2);
	if ((l->left->bb_numrecs & 1) && cur->bc_ptrs[level] <= l->right->bb_numrecs + 1)
		l->right->bb_numrecs++;
	i = l->left->bb_numrecs - l->right->bb_numrecs + 1;
	if (level > 0) {
		l->lkp = XFS_BMAP_KEY_IADDR(l->left, i, cur);
		l->lpp = XFS_BMAP_PTR_IADDR(l->left, i, cur);
		l->rkp = XFS_BMAP_KEY_IADDR(l->right, 1, cur);
		l->rpp = XFS_BMAP_PTR_IADDR(l->right, 1, cur);
		for (i = 0; i < l->right->bb_numrecs; i++) {
			l->rkp[i] = l->lkp[i];
			xfs_btree_check_lptr(cur, l->lpp[i], level);
			l->rpp[i] = l->lpp[i];
		}
		xfs_bmbt_log_keys(cur, l->rbp, 1, l->right->bb_numrecs);
		xfs_bmbt_log_ptrs(cur, l->rbp, 1, l->right->bb_numrecs);
		*keyp = *l->rkp;
	} else {
		l->lrp = XFS_BMAP_REC_IADDR(l->left, i, cur);
		l->rrp = XFS_BMAP_REC_IADDR(l->right, 1, cur);
		for (i = 0; i < l->right->bb_numrecs; i++)
			l->rrp[i] = l->lrp[i];
		xfs_bmbt_log_recs(cur, l->rbp, 1, l->right->bb_numrecs);
		keyp->br_startoff = xfs_bmbt_get_startoff(l->rrp);
	}
	l->d = l->lbp->b_blkno;
	l->left->bb_numrecs -= l->right->bb_numrecs;
	l->right->bb_rightsib = l->left->bb_rightsib;
	l->left->bb_rightsib = args->fsbno;
	l->right->bb_leftsib = l->lbno;
	xfs_bmbt_log_block(cur, l->rbp, XFS_BB_ALL_BITS);
	xfs_bmbt_log_block(cur, l->lbp, XFS_BB_NUMRECS | XFS_BB_RIGHTSIB);
	if (l->right->bb_rightsib != NULLDFSBNO) {
		l->rrbp = xfs_btree_read_bufl(l->mp, args->tp, l->right->bb_rightsib, 0);
		l->rrblock = XFS_BUF_TO_BMBT_BLOCK(l->rrbp);
		xfs_btree_check_lblock(cur, l->rrblock, level);
		l->rrblock->bb_leftsib = args->fsbno;
		xfs_bmbt_log_block(cur, l->rrbp, XFS_BB_LEFTSIB);
	}
	if (cur->bc_ptrs[level] > l->left->bb_numrecs + 1) {
		xfs_btree_setbuf(cur, level, l->rbp);
		cur->bc_ptrs[level] -= l->left->bb_numrecs;
	}
	if (level + 1 < cur->bc_nlevels) {
		*curp = xfs_btree_dup_cursor(cur);
		(*curp)->bc_ptrs[level + 1]++;
	}
	*bnop = args->fsbno;
	xfs_bmbt_locals_free(l);
	xfs_alloc_arg_free(args);
	xfs_bmbt_rcheck(cur);
	xfs_bmbt_trace_cursor("xfs_bmbt_split exit1", cur);
	return 1;
}

#if defined(DEBUG) && !defined(SIM)
/*
 * Add a trace buffer entry for arguments, for a buffer & 1 integer arg.
 */
STATIC void
xfs_bmbt_trace_argbi(
	char		*name,
	xfs_btree_cur_t	*cur,
	buf_t		*b,
	int		i)
{
	xfs_bmbt_trace_enter(name, cur, XFS_BMBT_KTRACE_ARGBI,
		(int)b, i, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0);
}

/*
 * Add a trace buffer entry for arguments, for a buffer & 2 integer args.
 */
STATIC void
xfs_bmbt_trace_argbii(
	char		*name,
	xfs_btree_cur_t	*cur,
	buf_t		*b,
	int		i0,
	int		i1)
{
	xfs_bmbt_trace_enter(name, cur, XFS_BMBT_KTRACE_ARGBII,
		(int)b, i0, i1, 0,
		0, 0, 0, 0,
		0, 0, 0, 0);
}

/*
 * Add a trace buffer entry for arguments, for 2 blocks & 1 integer arg.
 */
STATIC void
xfs_bmbt_trace_argffi(
	char			*name,
	xfs_btree_cur_t		*cur,
	xfs_dfiloff_t		o,
	xfs_dfsbno_t		b,
	int			i)
{
	xfs_bmbt_trace_enter(name, cur, XFS_BMBT_KTRACE_ARGFFI,
		o >> 32, (int)o, b >> 32, (int)b,
		i, 0, 0, 0,
		0, 0, 0, 0);
}

/*
 * Add a trace buffer entry for arguments, for one integer arg.
 */
STATIC void
xfs_bmbt_trace_argi(
	char		*name,
	xfs_btree_cur_t	*cur,
	int		i)
{
	xfs_bmbt_trace_enter(name, cur, XFS_BMBT_KTRACE_ARGI,
		i, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0);
}

/*
 * Add a trace buffer entry for arguments, for int, fsblock, key.
 */
STATIC void
xfs_bmbt_trace_argifk(
	char			*name,
	xfs_btree_cur_t		*cur,
	int			i,
	xfs_fsblock_t		f,
	xfs_bmbt_key_t		*k)
{
	xfs_dfsbno_t		d;
	xfs_dfiloff_t		o;

	d = (xfs_dfsbno_t)f;
	o = k->br_startoff;
	xfs_bmbt_trace_enter(name, cur, XFS_BMBT_KTRACE_ARGIFK,
		i, d >> 32, (int)d, o >> 32,
		(int)o, 0, 0, 0,
		0, 0, 0, 0);
}

/*
 * Add a trace buffer entry for arguments, for int, fsblock, rec.
 */
STATIC void
xfs_bmbt_trace_argifr(
	char			*name,
	xfs_btree_cur_t		*cur,
	int			i,
	xfs_fsblock_t		f,
	xfs_bmbt_rec_t		*r)
{
	xfs_dfsbno_t		b;
	xfs_extlen_t		c;
	xfs_dfsbno_t		d;
	xfs_dfiloff_t		o;
	xfs_bmbt_irec_t		s;

	d = (xfs_dfsbno_t)f;
	xfs_bmbt_get_all(r, &s);
	o = (xfs_dfiloff_t)s.br_startoff;
	b = (xfs_dfsbno_t)s.br_startblock;
	c = s.br_blockcount;
	xfs_bmbt_trace_enter(name, cur, XFS_BMBT_KTRACE_ARGIFR,
		i, d >> 32, (int)d, o >> 32,
		(int)o, b >> 32, (int)b, c,
		0, 0, 0, 0);
}

/*
 * Add a trace buffer entry for arguments, for int, key.
 */
STATIC void
xfs_bmbt_trace_argik(
	char			*name,
	xfs_btree_cur_t		*cur,
	int			i,
	xfs_bmbt_key_t		*k)
{
	xfs_dfiloff_t		o;

	o = k->br_startoff;
	xfs_bmbt_trace_enter(name, cur, XFS_BMBT_KTRACE_ARGIFK,
		i, o >> 32, (int)o, 0,
		0, 0, 0, 0,
		0, 0, 0, 0);
}

/*
 * Add a trace buffer entry for the cursor/operation.
 */
STATIC void
xfs_bmbt_trace_cursor(
	char		*name,
	xfs_btree_cur_t	*cur)
{
	xfs_bmbt_rec_t	r;

	xfs_bmbt_set_all(&r, &cur->bc_rec.b);
	xfs_bmbt_trace_enter(name, cur, XFS_BMBT_KTRACE_CUR,
		(cur->bc_nlevels << 16) | cur->bc_private.b.flags,
		cur->bc_private.b.allocated,
		r.l0, r.l1, r.l2, r.l3,
		(int)cur->bc_bufs[0], (int)cur->bc_bufs[1],
		(int)cur->bc_bufs[2], (int)cur->bc_bufs[3],
		(cur->bc_ptrs[0] << 16) | cur->bc_ptrs[1],
		(cur->bc_ptrs[2] << 16) | cur->bc_ptrs[3]);
}

/*
 * Add a trace buffer entry for the arguments given to the routine,
 * generic form.
 */
STATIC void
xfs_bmbt_trace_enter(
	char		*name,
	xfs_btree_cur_t	*cur,
	int		type,
	int		a0,
	int		a1,
	int		a2,
	int		a3,
	int		a4,
	int		a5,
	int		a6,
	int		a7,
	int		a8,
	int		a9,
	int		a10,
	int		a11)
{
	xfs_inode_t	*ip;

	ip = cur->bc_private.b.ip;
	ktrace_enter(xfs_bmbt_trace_buf,
		(void *)type, (void *)name, (void *)ip, (void *)cur,
		(void *)a0, (void *)a1, (void *)a2, (void *)a3,
		(void *)a4, (void *)a5, (void *)a6, (void *)a7,
		(void *)a8, (void *)a9, (void *)a10, (void *)a11);
	ASSERT(ip->i_btrace);
	ktrace_enter(ip->i_btrace,
		(void *)type, (void *)name, (void *)ip, (void *)cur,
		(void *)a0, (void *)a1, (void *)a2, (void *)a3,
		(void *)a4, (void *)a5, (void *)a6, (void *)a7,
		(void *)a8, (void *)a9, (void *)a10, (void *)a11);
}
#endif	/* DEBUG && !SIM */

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
	xfs_bmbt_trace_cursor("xfs_bmbt_updkey entry", cur);
	xfs_bmbt_trace_argik("xfs_bmbt_updkey args", cur, level, keyp);
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
	xfs_bmbt_trace_cursor("xfs_bmbt_updkey exit", cur);
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

	xfs_bmbt_trace_cursor("xfs_bmbt_decrement entry", cur);
	xfs_bmbt_trace_argi("xfs_bmbt_decrement entry", cur, level);
	if (--cur->bc_ptrs[level] > 0) {
		xfs_bmbt_trace_cursor("xfs_bmbt_decrement exit0", cur);
		return 1;
	}
	block = xfs_bmbt_get_block(cur, level, &bp);
	xfs_btree_check_lblock(cur, block, level);
	if (block->bb_leftsib == NULLDFSBNO) {
		xfs_bmbt_trace_cursor("xfs_bmbt_decrement exit1", cur);
		return 0;
	}
	for (lev = level + 1; lev < cur->bc_nlevels; lev++) {
		if (--cur->bc_ptrs[lev] > 0)
			break;
	}
	if (lev == cur->bc_nlevels) {
		xfs_bmbt_trace_cursor("xfs_bmbt_decrement exit2", cur);
		return 0;
	}
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
	xfs_bmbt_trace_cursor("xfs_bmbt_decrement exit3", cur);
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

	xfs_bmbt_trace_cursor("xfs_bmbt_delete entry", cur);
	for (level = 0, i = 2; i == 2; level++)
		i = xfs_bmbt_delrec(cur, level);
	if (i == 0) {
		for (level = 1; level < cur->bc_nlevels; level++) {
			if (cur->bc_ptrs[level] == 0) {
				xfs_bmbt_decrement(cur, level);
				break;
			}
		}
	}
	xfs_bmbt_kcheck(cur);
	xfs_bmbt_trace_cursor("xfs_bmbt_delete exit", cur);
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

	xfs_bmbt_trace_cursor("xfs_bmbt_increment entry", cur);
	xfs_bmbt_trace_argi("xfs_bmbt_increment args", cur, level);
	block = xfs_bmbt_get_block(cur, level, &bp);
	xfs_btree_check_lblock(cur, block, level);
	if (++cur->bc_ptrs[level] <= block->bb_numrecs) {
		xfs_bmbt_trace_cursor("xfs_bmbt_increment exit0", cur);
		return 1;
	}
	if (block->bb_rightsib == NULLDFSBNO) {
		xfs_bmbt_trace_cursor("xfs_bmbt_increment exit1", cur);
		return 0;
	}
	for (lev = level + 1; lev < cur->bc_nlevels; lev++) {
		block = xfs_bmbt_get_block(cur, lev, &bp);
		xfs_btree_check_lblock(cur, block, lev);
		if (++cur->bc_ptrs[lev] <= block->bb_numrecs)
			break;
	}
	if (lev == cur->bc_nlevels) {
		xfs_bmbt_trace_cursor("xfs_bmbt_increment exit2", cur);
		return 0;
	}
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
	xfs_bmbt_trace_cursor("xfs_bmbt_increment exit3", cur);
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

	xfs_bmbt_trace_cursor("xfs_bmbt_insert entry", cur);
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
			ASSERT((cur->bc_private.b.firstblock != NULLFSBLOCK) ||
			       (cur->bc_private.b.ip->i_d.di_flags & 
				XFS_DIFLAG_REALTIME));
			cur->bc_private.b.firstblock =
				pcur->bc_private.b.firstblock;
			ASSERT(cur->bc_private.b.flist ==
			       pcur->bc_private.b.flist);
			xfs_btree_del_cursor(pcur);
		}
		if (ncur) {
			pcur = ncur;
			ncur = (xfs_btree_cur_t *)0;
		}
	} while (nbno != NULLFSBLOCK);
	xfs_bmbt_trace_cursor("xfs_bmbt_insert exit", cur);
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

	xfs_bmbt_trace_cursor("xfs_bmbt_log_block entry", cur);
	xfs_bmbt_trace_argbi("xfs_bmbt_log_block args", cur, bp, fields);
	tp = cur->bc_tp;
	if (bp) {
		xfs_btree_offsets(fields, offsets, XFS_BB_NUM_BITS, &first,
				  &last);
		xfs_trans_log_buf(tp, bp, first, last);
	} else
		xfs_trans_log_inode(tp, cur->bc_private.b.ip, XFS_ILOG_BROOT);
	xfs_bmbt_trace_cursor("xfs_bmbt_log_block exit", cur);
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

	xfs_bmbt_trace_cursor("xfs_bmbt_log_recs entry", cur);
	xfs_bmbt_trace_argbii("xfs_bmbt_log_recs args", cur, bp, rfirst, rlast);
	ASSERT(bp);
	tp = cur->bc_tp;
	block = XFS_BUF_TO_BMBT_BLOCK(bp);
	rp = XFS_BMAP_REC_DADDR(block, 1, cur);
	first = (caddr_t)&rp[rfirst - 1] - (caddr_t)block;
	last = ((caddr_t)&rp[rlast] - 1) - (caddr_t)block;
	xfs_trans_log_buf(tp, bp, first, last);
	xfs_bmbt_trace_cursor("xfs_bmbt_log_recs exit", cur);
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

	mp = cur->bc_mp;
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
			ASSERT(XFS_FSB_TO_AGNO(mp, bno) < mp->m_sb.sb_agcount);
			ASSERT(XFS_FSB_TO_AGBNO(mp, bno) < mp->m_sb.sb_agblocks);
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
	xfs_bmbt_key_t		key;
	int			ptr;
	xfs_bmbt_rec_t		*rp;
	xfs_trans_t		*tp;

	xfs_bmbt_rcheck(cur);
	xfs_bmbt_trace_cursor("xfs_bmbt_update entry", cur);
	xfs_bmbt_trace_argffi("xfs_bmbt_update args", cur,
		(xfs_dfiloff_t)off, (xfs_dfsbno_t)bno, (int)len);
	block = xfs_bmbt_get_block(cur, 0, &bp);
	xfs_btree_check_lblock(cur, block, 0);
	ptr = cur->bc_ptrs[0];
	tp = cur->bc_tp;
	rp = XFS_BMAP_REC_IADDR(block, ptr, cur);
	xfs_bmbt_set_startoff(rp, off);
	xfs_bmbt_set_startblock(rp, bno);
	xfs_bmbt_set_blockcount(rp, len);
	xfs_bmbt_log_recs(cur, bp, ptr, ptr);
	if (ptr > 1) {
		xfs_bmbt_trace_cursor("xfs_bmbt_update exit0", cur);
		return 1;
	}
	key.br_startoff = off;
	xfs_bmbt_updkey(cur, &key, 1);
	xfs_bmbt_rcheck(cur);
	xfs_bmbt_kcheck(cur);
	xfs_bmbt_trace_cursor("xfs_bmbt_update exit1", cur);
	return 1;
}
