#ident	"$Revision: 1.85 $"

#ifdef SIM
#define _KERNEL 1
#endif
#include <sys/param.h>
#include <sys/debug.h>
#ifdef SIM
#undef _KERNEL
#endif
#include <sys/vnode.h>
#include <sys/kmem.h>
#include <sys/errno.h>
#include <stddef.h>
#ifdef SIM
#include <bstring.h>
#else
#include <sys/systm.h>
#endif
#include "xfs_macros.h"
#include "xfs_types.h"
#include "xfs_inum.h"
#ifdef SIM
#define _KERNEL 1
#endif
#include <sys/buf.h>
#include <sys/uuid.h>
#include <sys/grio.h>
#include <sys/ktrace.h>
#include <sys/sysinfo.h>
#include <sys/ksa.h>
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
#include "xfs_itable.h"
#include "xfs_attr_sf.h"
#include "xfs_dir_sf.h"
#include "xfs_dinode.h"
#include "xfs_inode_item.h"
#include "xfs_inode.h"
#include "xfs_alloc.h"
#include "xfs_bit.h"
#include "xfs_bmap.h"
#include "xfs_error.h"
#include "xfs_quota.h"

#ifdef SIM
#include "sim.h"
#endif

#ifdef DEBUG
ktrace_t	*xfs_bmbt_trace_buf;
#endif

/*
 * Prototypes for internal btree functions.
 */

/*
 * Delete record pointed to by cur/level.
 */
STATIC int
xfs_bmbt_delrec(
	xfs_btree_cur_t		*cur,
	int			level,
	int			*stat);

#if 0
/* 
 * Get the data from the pointed-to record.
 */
STATIC int
xfs_bmbt_get_rec(
	xfs_btree_cur_t		*cur,
	xfs_fileoff_t		*off,
	xfs_fsblock_t		*bno,
	xfs_filblks_t		*len);
#endif

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
	xfs_btree_cur_t		**curp,
	int			*stat);

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
	xfs_lookup_t		dir,
	int			*stat);

/*
 * Move 1 record left from cur/level if possible.
 * Update cur to reflect the new path.
 */
STATIC int
xfs_bmbt_lshift(
	xfs_btree_cur_t		*cur,
	int			level,
	int			*stat);

/*
 * Move 1 record right from cur/level if possible.
 * Update cur to reflect the new path.
 */
STATIC int
xfs_bmbt_rshift(
	xfs_btree_cur_t		*cur,
	int			level,
	int			*stat);

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
	xfs_btree_cur_t		**curp,
	int			*stat);

#if defined(XFS_BMBT_TRACE)
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
 * Add a trace buffer entry for arguments, for 3 block-length args.
 */
STATIC void
xfs_bmbt_trace_argfff(
	char			*name,
	xfs_btree_cur_t		*cur,
	xfs_dfiloff_t		o,
	xfs_dfsbno_t		b,
	xfs_dfilblks_t		i);

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
	__psunsigned_t		a0,
	__psunsigned_t		a1,
	__psunsigned_t		a2,
	__psunsigned_t		a3,
	__psunsigned_t		a4,
	__psunsigned_t		a5,
	__psunsigned_t		a6,
	__psunsigned_t		a7,
	__psunsigned_t		a8,
	__psunsigned_t		a9,
	__psunsigned_t		a10,
	__psunsigned_t		a11);
#else
#define	xfs_bmbt_trace_argbi(n,c,b,i)
#define	xfs_bmbt_trace_argbii(n,c,b,i,j)
#define	xfs_bmbt_trace_argfff(n,c,o,b,i)
#define	xfs_bmbt_trace_argi(n,c,i)
#define	xfs_bmbt_trace_argifk(n,c,i,f,k)
#define	xfs_bmbt_trace_argifr(n,c,i,f,r)
#define	xfs_bmbt_trace_argik(n,c,i,k)
#define	xfs_bmbt_trace_cursor(n,c)
#endif	/* XFS_BMBT_TRACE */

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
 * Delete record pointed to by cur/level.
 */
STATIC int					/* error */
xfs_bmbt_delrec(
	xfs_btree_cur_t		*cur,
	int			level,
	int			*stat)		/* success/failure */
{
	xfs_bmbt_block_t	*block;		/* bmap btree block */
	xfs_fsblock_t		bno;		/* fs-relative block number */
	buf_t			*bp;		/* buffer for block */
	int			error;		/* error return value */
	int			i;		/* loop counter */
	int			j;		/* temp state */
	xfs_bmbt_key_t		key;		/* bmap btree key */
	xfs_bmbt_key_t		*kp;		/* pointer to bmap btree key */
	xfs_fsblock_t		lbno;		/* left sibling block number */
	buf_t			*lbp;		/* left buffer pointer */
	xfs_bmbt_block_t	*left;		/* left btree block */
	xfs_bmbt_key_t		*lkp;		/* left btree key */
	xfs_bmbt_ptr_t		*lpp;		/* left address pointer */
	int			lrecs;		/* left record count */
	xfs_bmbt_rec_t		*lrp;		/* left record pointer */
	xfs_mount_t		*mp;		/* file system mount point */
	xfs_bmbt_ptr_t		*pp;		/* pointer to bmap block addr */
	int			ptr;		/* key/record index */
	xfs_fsblock_t		rbno;		/* right sibling block number */
	buf_t			*rbp;		/* right buffer pointer */
	xfs_bmbt_block_t	*right;		/* right btree block */
	xfs_bmbt_key_t		*rkp;		/* right btree key */
	xfs_bmbt_rec_t		*rp;		/* pointer to bmap btree rec */
	xfs_bmbt_ptr_t		*rpp;		/* right address pointer */
	xfs_bmbt_block_t	*rrblock;	/* right-right btree block */
	buf_t			*rrbp;		/* right-right buffer pointer */
	int			rrecs;		/* right record count */
	xfs_bmbt_rec_t		*rrp;		/* right record pointer */
	xfs_btree_cur_t		*tcur;		/* temporary btree cursor */

	xfs_bmbt_trace_cursor("xfs_bmbt_delrec entry", cur);
	xfs_bmbt_trace_argi("xfs_bmbt_delrec args", cur, level);
	ptr = cur->bc_ptrs[level];
	if (ptr == 0) {
		xfs_bmbt_trace_cursor("xfs_bmbt_delrec exit0", cur);
		*stat = 0;
		return 0;
	}
	block = xfs_bmbt_get_block(cur, level, &bp);
	xfs_btree_check_lblock(cur, block, level);
	if (ptr > block->bb_numrecs) {
		xfs_bmbt_trace_cursor("xfs_bmbt_delrec exit1", cur);
		*stat = 0;
		return 0;
	}
	XFSSTATS.xs_bmbt_delrec++;
	if (level > 0) {
		kp = XFS_BMAP_KEY_IADDR(block, 1, cur);
		pp = XFS_BMAP_PTR_IADDR(block, 1, cur);
#ifdef DEBUG
		for (i = ptr; i < block->bb_numrecs; i++)
			xfs_btree_check_lptr(cur, pp[i], level);
#endif
		if (ptr < block->bb_numrecs) {
			ovbcopy(&kp[ptr], &kp[ptr - 1],
				(block->bb_numrecs - ptr) * sizeof(*kp));
			ovbcopy(&pp[ptr], &pp[ptr - 1],
				(block->bb_numrecs - ptr) * sizeof(*pp));
			xfs_bmbt_log_ptrs(cur, bp, ptr, block->bb_numrecs - 1);
			xfs_bmbt_log_keys(cur, bp, ptr, block->bb_numrecs - 1);
		}
	} else {
		rp = XFS_BMAP_REC_IADDR(block, 1, cur);
		if (ptr < block->bb_numrecs) {
			ovbcopy(&rp[ptr], &rp[ptr - 1],
				(block->bb_numrecs - ptr) * sizeof(*rp));
			xfs_bmbt_log_recs(cur, bp, ptr, block->bb_numrecs - 1);
		}
		if (ptr == 1) {
			key.br_startoff = xfs_bmbt_get_startoff(rp);
			kp = &key;
		}
	}
	block->bb_numrecs--;
	xfs_bmbt_log_block(cur, bp, XFS_BB_NUMRECS);
	/*
	 * We're at the root level.
	 * First, shrink the root block in-memory.
	 * Try to get rid of the next level down.
	 * If we can't then there's nothing left to do.
	 */
	if (level == cur->bc_nlevels - 1) {
		xfs_iroot_realloc(cur->bc_private.b.ip, -1,
			cur->bc_private.b.whichfork);
		i = xfs_bmbt_killroot(cur);
		if (level > 0) {
			if (error = xfs_bmbt_decrement(cur, level, &j)) {
				xfs_bmbt_trace_cursor("xfs_bmbt_delrec error0",
					cur);
				return error;
			}
		}
		xfs_bmbt_trace_cursor("xfs_bmbt_delrec exit2", cur);
		*stat = i;
		return 0;
	}
	if (ptr == 1)
		xfs_bmbt_updkey(cur, kp, level + 1);
	if (block->bb_numrecs >= XFS_BMAP_BLOCK_IMINRECS(level, cur)) {
		if (level > 0) {
			if (error = xfs_bmbt_decrement(cur, level, &j)) {
				xfs_bmbt_trace_cursor("xfs_bmbt_delrec error1",
					cur);
				return error;
			}
		}
		xfs_bmbt_trace_cursor("xfs_bmbt_delrec exit3", cur);
		*stat = 1;
		return 0;
	}
	rbno = block->bb_rightsib;
	lbno = block->bb_leftsib;
	/*
	 * One child of root, need to get a chance to copy its contents
	 * into the root and delete it. Can't go up to next level,
	 * there's nothing to delete there.
	 */
	if (lbno == NULLFSBLOCK && rbno == NULLFSBLOCK &&
	    level == cur->bc_nlevels - 2) {
		i = xfs_bmbt_killroot(cur);
		if (level > 0) {
			if (error = xfs_bmbt_decrement(cur, level, &j)) {
				xfs_bmbt_trace_cursor("xfs_bmbt_delrec error2",
					cur);
				return error;
			}
		}
		xfs_bmbt_trace_cursor("xfs_bmbt_delrec exit4", cur);
		*stat = i;
		return 0;
	}
	ASSERT(rbno != NULLFSBLOCK || lbno != NULLFSBLOCK);
	if (error = xfs_btree_dup_cursor(cur, &tcur)) {
		xfs_bmbt_trace_cursor("xfs_bmbt_delrec error3", cur);
		return error;
	}
	bno = NULLFSBLOCK;
	if (rbno != NULLFSBLOCK) {
		xfs_btree_lastrec(tcur, level);
		if (error = xfs_bmbt_increment(tcur, level, &i)) {
			xfs_bmbt_trace_cursor("xfs_bmbt_delrec error4", cur);
			goto error0;
		}
		xfs_btree_lastrec(tcur, level);
		rbp = tcur->bc_bufs[level];
		right = XFS_BUF_TO_BMBT_BLOCK(rbp);
		xfs_btree_check_lblock(cur, right, level);
		bno = right->bb_leftsib;
		if (right->bb_numrecs - 1 >=
		    XFS_BMAP_BLOCK_IMINRECS(level, cur)) {
			if (error = xfs_bmbt_lshift(tcur, level, &i)) {
				xfs_bmbt_trace_cursor("xfs_bmbt_delrec error5",
					cur);
				goto error0;
			}
			if (i) {
				ASSERT(block->bb_numrecs >=
					XFS_BMAP_BLOCK_IMINRECS(level, tcur));
				xfs_btree_del_cursor(tcur,
						     XFS_BTREE_NOERROR);
				if (level > 0) {
					if (error = xfs_bmbt_decrement(cur,
							level, &i)) {
						xfs_bmbt_trace_cursor(
						       "xfs_bmbt_delrec error6",
							cur);
						return error;
					}
				}
				xfs_bmbt_trace_cursor("xfs_bmbt_delrec exit5",
					cur);
				*stat = 1;
				return 0;
			}
		}
		rrecs = right->bb_numrecs;
		if (lbno != NULLFSBLOCK) {
			xfs_btree_firstrec(tcur, level);
			if (error = xfs_bmbt_decrement(tcur, level, &i)) {
				xfs_bmbt_trace_cursor("xfs_bmbt_delrec error7",
					cur);
				goto error0;
			}
		}
	}
	if (lbno != NULLFSBLOCK) {
		xfs_btree_firstrec(tcur, level);
		/* decrement to last in block */
		if (error = xfs_bmbt_decrement(tcur, level, &i)) {
			xfs_bmbt_trace_cursor("xfs_bmbt_delrec error8", cur);
			goto error0;
		}
		xfs_btree_firstrec(tcur, level);
		lbp = tcur->bc_bufs[level];
		left = XFS_BUF_TO_BMBT_BLOCK(lbp);
		xfs_btree_check_lblock(cur, left, level);
		bno = left->bb_rightsib;
		if (left->bb_numrecs - 1 >=
		    XFS_BMAP_BLOCK_IMINRECS(level, cur)) {
			if (error = xfs_bmbt_rshift(tcur, level, &i)) {
				xfs_bmbt_trace_cursor("xfs_bmbt_delrec error9",
					cur);
				goto error0;
			}
			if (i) {
				ASSERT(block->bb_numrecs >=
					XFS_BMAP_BLOCK_IMINRECS(level, tcur));
				xfs_btree_del_cursor(tcur,
						     XFS_BTREE_NOERROR);
				if (level == 0)
					cur->bc_ptrs[0]++;
				xfs_bmbt_trace_cursor("xfs_bmbt_delrec exit6",
					cur);
				*stat = 1;
				return 0;
			}
		}
		lrecs = left->bb_numrecs;
	}
	xfs_btree_del_cursor(tcur, XFS_BTREE_NOERROR);
	mp = cur->bc_mp;
	ASSERT(bno != NULLFSBLOCK);
	if (lbno != NULLFSBLOCK &&
	    lrecs + block->bb_numrecs <= XFS_BMAP_BLOCK_IMAXRECS(level, cur)) {
		rbno = bno;
		right = block;
		rbp = bp;
		if (error =
		    xfs_btree_read_bufl(mp, cur->bc_tp, lbno, 0, &lbp)) {
			xfs_bmbt_trace_cursor("xfs_bmbt_delrec error10", cur);
			return error;
		}
		left = XFS_BUF_TO_BMBT_BLOCK(lbp);
		xfs_btree_check_lblock(cur, left, level);
	} else if (rbno != NULLFSBLOCK &&
		   rrecs + block->bb_numrecs <=
		   XFS_BMAP_BLOCK_IMAXRECS(level, cur)) {
		lbno = bno;
		left = block;
		lbp = bp;
		if (error = xfs_btree_read_bufl(mp, cur->bc_tp, rbno, 0,
				&rbp)) {
			xfs_bmbt_trace_cursor("xfs_bmbt_delrec error11", cur);
			return error;
		}
		right = XFS_BUF_TO_BMBT_BLOCK(rbp);
		lrecs = left->bb_numrecs;
		xfs_btree_check_lblock(cur, right, level);
	} else {
		if (level > 0) {
			if (error = xfs_bmbt_decrement(cur, level, &i)) {
				xfs_bmbt_trace_cursor("xfs_bmbt_delrec error12",
					cur);
				return error;
			}
		}
		xfs_bmbt_trace_cursor("xfs_bmbt_delrec exit7", cur);
		*stat = 1;
		return 0;
	}
	if (level > 0) {
		lkp = XFS_BMAP_KEY_IADDR(left, left->bb_numrecs + 1, cur);
		lpp = XFS_BMAP_PTR_IADDR(left, left->bb_numrecs + 1, cur);
		rkp = XFS_BMAP_KEY_IADDR(right, 1, cur);
		rpp = XFS_BMAP_PTR_IADDR(right, 1, cur);
#ifdef DEBUG
		for (i = 0; i < right->bb_numrecs; i++)
			xfs_btree_check_lptr(cur, rpp[i], level);
#endif
		bcopy(rkp, lkp, right->bb_numrecs * sizeof(*lkp));
		bcopy(rpp, lpp, right->bb_numrecs * sizeof(*lpp));
		xfs_bmbt_log_keys(cur, lbp, left->bb_numrecs + 1,
			left->bb_numrecs + right->bb_numrecs);
		xfs_bmbt_log_ptrs(cur, lbp, left->bb_numrecs + 1,
			left->bb_numrecs + right->bb_numrecs);
	} else {
		lrp = XFS_BMAP_REC_IADDR(left, left->bb_numrecs + 1, cur);
		rrp = XFS_BMAP_REC_IADDR(right, 1, cur);
		bcopy(rrp, lrp, right->bb_numrecs * sizeof(*lrp));
		xfs_bmbt_log_recs(cur, lbp, left->bb_numrecs + 1,
			left->bb_numrecs + right->bb_numrecs);
	}
	left->bb_numrecs += right->bb_numrecs;
	left->bb_rightsib = right->bb_rightsib;
	xfs_bmbt_log_block(cur, lbp, XFS_BB_RIGHTSIB | XFS_BB_NUMRECS);
	if (left->bb_rightsib != NULLDFSBNO) {
		if (error = xfs_btree_read_bufl(mp, cur->bc_tp,
				left->bb_rightsib, 0, &rrbp)) {
			xfs_bmbt_trace_cursor("xfs_bmbt_delrec error13", cur);
			return error;
		}
		rrblock = XFS_BUF_TO_BMBT_BLOCK(rrbp);
		xfs_btree_check_lblock(cur, rrblock, level);
		rrblock->bb_leftsib = lbno;
		xfs_bmbt_log_block(cur, rrbp, XFS_BB_LEFTSIB);
	}
	xfs_bmap_add_free(XFS_DADDR_TO_FSB(mp, rbp->b_blkno), 1,
		cur->bc_private.b.flist, mp);
	xfs_trans_set_sync(cur->bc_tp);
	cur->bc_private.b.ip->i_d.di_nblocks--;
	xfs_trans_log_inode(cur->bc_tp, cur->bc_private.b.ip, XFS_ILOG_CORE);
	if (XFS_IS_QUOTA_ON(mp) &&
	    cur->bc_private.b.ip->i_ino != mp->m_sb.sb_uquotino &&
	    cur->bc_private.b.ip->i_ino != mp->m_sb.sb_pquotino) {
		xfs_trans_mod_dquot_byino(cur->bc_tp, cur->bc_private.b.ip, 
					  XFS_TRANS_DQ_BCOUNT, -1L);
	}
	xfs_trans_binval(cur->bc_tp, rbp);
	if (bp != lbp) {
		cur->bc_bufs[level] = lbp;
		cur->bc_ptrs[level] += lrecs;
		cur->bc_ra[level] = 0;
	} else if (error = xfs_bmbt_increment(cur, level + 1, &i)) {
		xfs_bmbt_trace_cursor("xfs_bmbt_delrec error14", cur);
		return error;
	}
	if (level > 0)
		cur->bc_ptrs[level]--;
	xfs_bmbt_trace_cursor("xfs_bmbt_delrec exit8", cur);
	*stat = 2;
	return 0;

 error0:
	xfs_btree_del_cursor(tcur, XFS_BTREE_ERROR);
	return error;
}

#if 0
/* 
 * Get the data from the pointed-to record.
 */
STATIC int
xfs_bmbt_get_rec(
	xfs_btree_cur_t		*cur,
	xfs_fileoff_t		*off,
	xfs_fsblock_t		*bno,
	xfs_filblks_t		*len)
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
#endif

/*
 * Insert one record/level.  Return information to the caller
 * allowing the next level up to proceed if necessary.
 */
STATIC int					/* error */
xfs_bmbt_insrec(
	xfs_btree_cur_t		*cur,
	int			level,
	xfs_fsblock_t		*bnop,
	xfs_bmbt_rec_t		*recp,
	xfs_btree_cur_t		**curp,
	int			*stat)		/* no-go/done/continue */
{
	xfs_bmbt_block_t	*block;		/* bmap btree block */
	buf_t			*bp;		/* buffer for block */
	int			error;		/* error return value */
	int			i;		/* loop index */
	xfs_bmbt_key_t		key;		/* bmap btree key */
	xfs_bmbt_key_t		*kp;		/* pointer to bmap btree key */
	int			logflags;	/* inode logging flags */
	xfs_fsblock_t		nbno;		/* new block number */
	struct xfs_btree_cur	*ncur;		/* new btree cursor */
	xfs_bmbt_key_t		nkey;		/* new btree key value */
	xfs_bmbt_rec_t		nrec;		/* new record count */
	int			optr;		/* old key/record index */
	xfs_bmbt_ptr_t		*pp;		/* pointer to bmap block addr */
	int			ptr;		/* key/record index */
	xfs_bmbt_rec_t		*rp;		/* pointer to bmap btree rec */

	ASSERT(level < cur->bc_nlevels);
	xfs_bmbt_trace_cursor("xfs_bmbt_insrec entry", cur);
	xfs_bmbt_trace_argifr("xfs_bmbt_insrec args", cur, level, *bnop, recp);
	ncur = (xfs_btree_cur_t *)0;
	key.br_startoff = xfs_bmbt_get_startoff(recp);
	optr = ptr = cur->bc_ptrs[level];
	if (ptr == 0) {
		xfs_bmbt_trace_cursor("xfs_bmbt_insrec exit0", cur);
		*stat = 0;
		return 0;
	}
	XFSSTATS.xs_bmbt_insrec++;
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
	nbno = NULLFSBLOCK;
	if (block->bb_numrecs == XFS_BMAP_BLOCK_IMAXRECS(level, cur)) {
		if (block->bb_numrecs < XFS_BMAP_BLOCK_DMAXRECS(level, cur)) {
			/*
			 * A root block, that can be made bigger.
			 */
			xfs_iroot_realloc(cur->bc_private.b.ip, 1,
				cur->bc_private.b.whichfork);
			block = xfs_bmbt_get_block(cur, level, &bp);
		} else if (level == cur->bc_nlevels - 1) {
			if ((error = xfs_bmbt_newroot(cur, &logflags, stat)) ||
			    *stat == 0) {
				xfs_bmbt_trace_cursor("xfs_bmbt_insrec error0",
					cur);
				return error;
			}
			xfs_trans_log_inode(cur->bc_tp, cur->bc_private.b.ip,
				logflags);
			block = xfs_bmbt_get_block(cur, level, &bp);
		} else {
			if (error = xfs_bmbt_rshift(cur, level, &i)) {
				xfs_bmbt_trace_cursor("xfs_bmbt_insrec error1",
					cur);
				return error;
			}
			if (i) {
				/* nothing */
			} else {
				if (error = xfs_bmbt_lshift(cur, level, &i)) {
					xfs_bmbt_trace_cursor(
						"xfs_bmbt_insrec error2", cur);
					return error;
				}
				if (i) {
					optr = ptr = cur->bc_ptrs[level];
				} else {
					if (error = xfs_bmbt_split(cur, level,
							&nbno, &nkey, &ncur,
							&i)) {
						xfs_bmbt_trace_cursor(
						       "xfs_bmbt_insrec error2",
							cur);
						return error;
					}
					if (i) {
						block = xfs_bmbt_get_block(
							    cur, level, &bp);
						xfs_btree_check_lblock(cur,
							    block, level);
						ptr = cur->bc_ptrs[level];
						xfs_bmbt_set_allf(&nrec,
							nkey.br_startoff, 0, 0);
					} else {
						xfs_bmbt_trace_cursor(
							"xfs_bmbt_insrec exit2",
							cur);
						*stat = 0;
						return 0;
					}
				}
			}
		}
	}
	if (level > 0) {
		kp = XFS_BMAP_KEY_IADDR(block, 1, cur);
		pp = XFS_BMAP_PTR_IADDR(block, 1, cur);
#ifdef DEBUG
		for (i = block->bb_numrecs; i >= ptr; i--)
			xfs_btree_check_lptr(cur, pp[i - 1], level);
#endif
		ovbcopy(&kp[ptr - 1], &kp[ptr],
			(block->bb_numrecs - ptr + 1) * sizeof(*kp));
		ovbcopy(&pp[ptr - 1], &pp[ptr],
			(block->bb_numrecs - ptr + 1) * sizeof(*pp));
		xfs_btree_check_lptr(cur, (xfs_bmbt_ptr_t)*bnop, level);
		kp[ptr - 1] = key;
		pp[ptr - 1] = *bnop;
		block->bb_numrecs++;
		xfs_bmbt_log_keys(cur, bp, ptr, block->bb_numrecs);
		xfs_bmbt_log_ptrs(cur, bp, ptr, block->bb_numrecs);
	} else {
		rp = XFS_BMAP_REC_IADDR(block, 1, cur);
		ovbcopy(&rp[ptr - 1], &rp[ptr],
			(block->bb_numrecs - ptr + 1) * sizeof(*rp));
		rp[ptr - 1] = *recp;
		block->bb_numrecs++;
		xfs_bmbt_log_recs(cur, bp, ptr, block->bb_numrecs);
	}
	xfs_bmbt_log_block(cur, bp, XFS_BB_NUMRECS);
#ifdef DEBUG
	if (ptr < block->bb_numrecs) {
		if (level == 0)
			xfs_btree_check_rec(XFS_BTNUM_BMAP, rp + ptr - 1,
				rp + ptr);
		else
			xfs_btree_check_key(XFS_BTNUM_BMAP, kp + ptr - 1,
				kp + ptr);
	}
#endif
	if (optr == 1)
		xfs_bmbt_updkey(cur, &key, level + 1);
	*bnop = nbno;
	if (nbno != NULLFSBLOCK) {
		*recp = nrec;
		*curp = ncur;
	}
	xfs_bmbt_trace_cursor("xfs_bmbt_insrec exit3", cur);
	*stat = 1;
	return 0;
}

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
	xfs_ifork_t		*ifp;
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
	ifp = XFS_IFORK_PTR(ip, cur->bc_private.b.whichfork);
	ASSERT(XFS_BMAP_BLOCK_IMAXRECS(level, cur) ==
	       XFS_BMAP_BROOT_MAXRECS(ifp->if_broot_bytes));
	i = (int)(cblock->bb_numrecs - XFS_BMAP_BLOCK_IMAXRECS(level, cur));
	if (i) {
		xfs_iroot_realloc(ip, i, cur->bc_private.b.whichfork);
		block = ifp->if_broot;
	}
	block->bb_numrecs += i;
	ASSERT(block->bb_numrecs == cblock->bb_numrecs);
	kp = XFS_BMAP_KEY_IADDR(block, 1, cur);
	ckp = XFS_BMAP_KEY_IADDR(cblock, 1, cur);
	bcopy(ckp, kp, block->bb_numrecs * sizeof(*kp));
	pp = XFS_BMAP_PTR_IADDR(block, 1, cur);
	cpp = XFS_BMAP_PTR_IADDR(cblock, 1, cur);
#ifdef DEBUG
	for (i = 0; i < cblock->bb_numrecs; i++)
		xfs_btree_check_lptr(cur, cpp[i], level - 1);
#endif
	bcopy(cpp, pp, block->bb_numrecs * sizeof(*pp));
	xfs_bmap_add_free(XFS_DADDR_TO_FSB(cur->bc_mp, cbp->b_blkno), 1,
		cur->bc_private.b.flist, cur->bc_mp);
	xfs_trans_set_sync(cur->bc_tp);
	ip->i_d.di_nblocks--;
	if (XFS_IS_QUOTA_ON(cur->bc_mp) &&
	    ip->i_ino != cur->bc_mp->m_sb.sb_uquotino &&
	    ip->i_ino != cur->bc_mp->m_sb.sb_pquotino) {
		xfs_trans_mod_dquot_byino(cur->bc_tp, ip, XFS_TRANS_DQ_BCOUNT,
			-1L);
	}
	xfs_trans_binval(cur->bc_tp, cbp);
	cur->bc_bufs[level - 1] = NULL;
	block->bb_level--;
	xfs_trans_log_inode(cur->bc_tp, ip,
		XFS_ILOG_CORE | XFS_ILOG_FBROOT(cur->bc_private.b.whichfork));
	cur->bc_nlevels--;
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
		xfs_trans_log_inode(tp, ip,
			XFS_ILOG_FBROOT(cur->bc_private.b.whichfork));
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
		xfs_trans_log_inode(tp, ip,
			XFS_ILOG_FBROOT(cur->bc_private.b.whichfork));
	}
	xfs_bmbt_trace_cursor("xfs_bmbt_log_ptrs exit", cur);
}

/*
 * Lookup the record.  The cursor is made to point to it, based on dir.
 */
STATIC int				/* error */
xfs_bmbt_lookup(
	xfs_btree_cur_t		*cur,
	xfs_lookup_t		dir,
	int			*stat)		/* success/failure */
{
	xfs_bmbt_block_t	*block;
	buf_t			*bp;
	daddr_t			d;
	xfs_sfiloff_t		diff;
	int			error;		/* error return value */
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

	XFSSTATS.xs_bmbt_lookup++;
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
				if (error = xfs_trans_read_buf(mp, tp,
						mp->m_dev, d, mp->m_bsize, 0,
						&bp)) {
					xfs_bmbt_trace_cursor(
						"xfs_bmbt_lookup error0", cur);
					return error;
				}
				ASSERT(bp);
				ASSERT(!geterror(bp));
				/*
				 * Validate the magic number in the block.
				 */
				block = XFS_BUF_TO_BMBT_BLOCK(bp);
				if (block->bb_magic !=
				    xfs_magics[cur->bc_btnum]) {
					bp->b_flags |= B_ERROR;
					xfs_trans_brelse(tp, bp);
					return XFS_ERROR(EIO);
				}
				xfs_btree_setbuf(cur, level, bp);
				bp->b_ref = XFS_BMAP_BTREE_REF;
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
				*stat = 0;
				return 0;
			}
			while (low <= high) {
				XFSSTATS.xs_bmbt_compare++;
				keyno = (low + high) >> 1;
				if (level > 0) {
					kkp = kkbase + keyno - 1;
					startoff = kkp->br_startoff;
				} else {
					krp = krbase + keyno - 1;
					startoff = xfs_bmbt_get_startoff(krp);
				}
				diff = (xfs_sfiloff_t)
						(startoff - rp->br_startoff);
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
			if (error = xfs_bmbt_increment(cur, 0, &i)) {
				xfs_bmbt_trace_cursor("xfs_bmbt_lookup error1",
					cur);
				return error;
			}
			ASSERT(i == 1);
			xfs_bmbt_trace_cursor("xfs_bmbt_lookup exit1", cur);
			*stat = 1;
			return 0;
		}
	}
	else if (dir == XFS_LOOKUP_LE && diff > 0)
		keyno--;
	cur->bc_ptrs[0] = keyno;
	if (keyno == 0 || keyno > block->bb_numrecs) {
		xfs_bmbt_trace_cursor("xfs_bmbt_lookup exit2", cur);
		*stat = 0;
	} else {
		xfs_bmbt_trace_cursor("xfs_bmbt_lookup exit3", cur);
		*stat = ((dir != XFS_LOOKUP_EQ) || (diff == 0));
	}
	return 0;
}

/*
 * Move 1 record left from cur/level if possible.
 * Update cur to reflect the new path.
 */
STATIC int					/* error */
xfs_bmbt_lshift(
	xfs_btree_cur_t		*cur,
	int			level,
	int			*stat)		/* success/failure */
{
	int			error;		/* error return value */
#ifdef DEBUG
	int			i;		/* loop counter */
#endif
	xfs_bmbt_key_t		key;		/* bmap btree key */
	buf_t			*lbp;		/* left buffer pointer */
	xfs_bmbt_block_t	*left;		/* left btree block */
	xfs_bmbt_key_t		*lkp;		/* left btree key */
	xfs_bmbt_ptr_t		*lpp;		/* left address pointer */
	int			lrecs;		/* left record count */
	xfs_bmbt_rec_t		*lrp;		/* left record pointer */
	xfs_mount_t		*mp;		/* file system mount point */
	buf_t			*rbp;		/* right buffer pointer */
	xfs_bmbt_block_t	*right;		/* right btree block */
	xfs_bmbt_key_t		*rkp;		/* right btree key */
	xfs_bmbt_ptr_t		*rpp;		/* right address pointer */
	xfs_bmbt_rec_t		*rrp;		/* right record pointer */

	xfs_bmbt_trace_cursor("xfs_bmbt_lshift entry", cur);
	xfs_bmbt_trace_argi("xfs_bmbt_lshift args", cur, level);
	if (level == cur->bc_nlevels - 1) {
		xfs_bmbt_trace_cursor("xfs_bmbt_lshift exit0", cur);
		*stat = 0;
		return 0;
	}
	rbp = cur->bc_bufs[level];
	right = XFS_BUF_TO_BMBT_BLOCK(rbp);
	xfs_btree_check_lblock(cur, right, level);
	if (right->bb_leftsib == NULLDFSBNO) {
		xfs_bmbt_trace_cursor("xfs_bmbt_lshift exit1", cur);
		*stat = 0;
		return 0;
	}
	if (cur->bc_ptrs[level] <= 1) {
		xfs_bmbt_trace_cursor("xfs_bmbt_lshift exit2", cur);
		*stat = 0;
		return 0;
	}
	mp = cur->bc_mp;
	if (error = xfs_btree_read_bufl(mp, cur->bc_tp, right->bb_leftsib, 0,
			&lbp)) {
		xfs_bmbt_trace_cursor("xfs_bmbt_lshift error0", cur);
		return error;
	}
	left = XFS_BUF_TO_BMBT_BLOCK(lbp);
	xfs_btree_check_lblock(cur, left, level);
	if (left->bb_numrecs == XFS_BMAP_BLOCK_IMAXRECS(level, cur)) {
		xfs_bmbt_trace_cursor("xfs_bmbt_lshift exit3", cur);
		*stat = 0;
		return 0;
	}
	lrecs = left->bb_numrecs + 1;
	if (level > 0) {
		lkp = XFS_BMAP_KEY_IADDR(left, left->bb_numrecs + 1, cur);
		rkp = XFS_BMAP_KEY_IADDR(right, 1, cur);
		*lkp = *rkp;
		xfs_bmbt_log_keys(cur, lbp, lrecs, lrecs);
		lpp = XFS_BMAP_PTR_IADDR(left, left->bb_numrecs + 1, cur);
		rpp = XFS_BMAP_PTR_IADDR(right, 1, cur);
		xfs_btree_check_lptr(cur, *rpp, level);
		*lpp = *rpp;
		xfs_bmbt_log_ptrs(cur, lbp, lrecs, lrecs);
	} else {
		lrp = XFS_BMAP_REC_IADDR(left, left->bb_numrecs + 1, cur);
		rrp = XFS_BMAP_REC_IADDR(right, 1, cur);
		*lrp = *rrp;
		xfs_bmbt_log_recs(cur, lbp, lrecs, lrecs);
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
#ifdef DEBUG
		for (i = 0; i < right->bb_numrecs; i++)
			xfs_btree_check_lptr(cur, rpp[i + 1], level);
#endif
		ovbcopy(rkp + 1, rkp, right->bb_numrecs * sizeof(*rkp));
		ovbcopy(rpp + 1, rpp, right->bb_numrecs * sizeof(*rpp));
		xfs_bmbt_log_keys(cur, rbp, 1, right->bb_numrecs);
		xfs_bmbt_log_ptrs(cur, rbp, 1, right->bb_numrecs);
	} else {
		ovbcopy(rrp + 1, rrp, right->bb_numrecs * sizeof(*rrp));
		xfs_bmbt_log_recs(cur, rbp, 1, right->bb_numrecs);
		key.br_startoff = xfs_bmbt_get_startoff(rrp);
		rkp = &key;
	}
	xfs_bmbt_updkey(cur, rkp, level + 1);
	cur->bc_ptrs[level]--;
	xfs_bmbt_trace_cursor("xfs_bmbt_lshift exit4", cur);
	*stat = 1;
	return 0;
}

/*
 * Read in the allocation group header (free/alloc section)
 */
int					/* error */
xfs_bmbt_read_agf(
	xfs_mount_t	*mp,
	xfs_trans_t	*tp,
	xfs_agnumber_t	agno,
	buf_t		**bpp)
{
	buf_t		*bp;		/* return value */
	daddr_t		d;		/* disk block address */
	int		error;		/* error return value */
	xfs_agf_t	*agf;

	ASSERT(agno != NULLAGNUMBER);
	d = XFS_AG_DADDR(mp, agno, XFS_AGF_DADDR);
	if (error = xfs_trans_read_buf(mp, tp, mp->m_dev, d, 1, 0, &bp))
		return error;
	ASSERT(bp);
	ASSERT(!geterror(bp));
	/*
	 * Validate the magic number of the agf block.
	 */
	agf = XFS_BUF_TO_AGF(bp);
	if ((agf->agf_magicnum != XFS_AGF_MAGIC) ||
	    !XFS_AGF_GOOD_VERSION(agf->agf_versionnum)) {
		bp->b_flags |= B_ERROR;
		xfs_trans_brelse(tp, bp);
		return XFS_ERROR(EIO);
	}
	bp->b_ref = XFS_AGF_REF;
	*bpp = bp;
	return 0;
}

/*
 * Move 1 record right from cur/level if possible.
 * Update cur to reflect the new path.
 */
STATIC int					/* error */
xfs_bmbt_rshift(
	xfs_btree_cur_t		*cur,
	int			level,
	int			*stat)		/* success/failure */
{
	int			error;		/* error return value */
	int			i;		/* loop counter */
	xfs_bmbt_key_t		key;		/* bmap btree key */
	buf_t			*lbp;		/* left buffer pointer */
	xfs_bmbt_block_t	*left;		/* left btree block */
	xfs_bmbt_key_t		*lkp;		/* left btree key */
	xfs_bmbt_ptr_t		*lpp;		/* left address pointer */
	xfs_bmbt_rec_t		*lrp;		/* left record pointer */
	xfs_mount_t		*mp;		/* file system mount point */
	buf_t			*rbp;		/* right buffer pointer */
	xfs_bmbt_block_t	*right;		/* right btree block */
	xfs_bmbt_key_t		*rkp;		/* right btree key */
	xfs_bmbt_ptr_t		*rpp;		/* right address pointer */
	xfs_bmbt_rec_t		*rrp;		/* right record pointer */
	struct xfs_btree_cur	*tcur;		/* temporary btree cursor */

	xfs_bmbt_trace_cursor("xfs_bmbt_rshift entry", cur);
	xfs_bmbt_trace_argi("xfs_bmbt_rshift args", cur, level);
	if (level == cur->bc_nlevels - 1) {
		xfs_bmbt_trace_cursor("xfs_bmbt_rshift exit0", cur);
		*stat = 0;
		return 0;
	}
	lbp = cur->bc_bufs[level];
	left = XFS_BUF_TO_BMBT_BLOCK(lbp);
	xfs_btree_check_lblock(cur, left, level);
	if (left->bb_rightsib == NULLDFSBNO) {
		xfs_bmbt_trace_cursor("xfs_bmbt_rshift exit1", cur);
		*stat = 0;
		return 0;
	}
	if (cur->bc_ptrs[level] >= left->bb_numrecs) {
		xfs_bmbt_trace_cursor("xfs_bmbt_rshift exit2", cur);
		*stat = 0;
		return 0;
	}
	mp = cur->bc_mp;
	if (error = xfs_btree_read_bufl(mp, cur->bc_tp, left->bb_rightsib, 0,
			&rbp)) {
		xfs_bmbt_trace_cursor("xfs_bmbt_rshift error0", cur);
		return error;
	}
	right = XFS_BUF_TO_BMBT_BLOCK(rbp);
	xfs_btree_check_lblock(cur, right, level);
	if (right->bb_numrecs == XFS_BMAP_BLOCK_IMAXRECS(level, cur)) {
		xfs_bmbt_trace_cursor("xfs_bmbt_rshift exit3", cur);
		*stat = 0;
		return 0;
	}
	if (level > 0) {
		lkp = XFS_BMAP_KEY_IADDR(left, left->bb_numrecs, cur);
		lpp = XFS_BMAP_PTR_IADDR(left, left->bb_numrecs, cur);
		rkp = XFS_BMAP_KEY_IADDR(right, 1, cur);
		rpp = XFS_BMAP_PTR_IADDR(right, 1, cur);
#ifdef DEBUG
		for (i = right->bb_numrecs - 1; i >= 0; i--)
			xfs_btree_check_lptr(cur, rpp[i], level);
#endif
		ovbcopy(rkp, rkp + 1, right->bb_numrecs * sizeof(*rkp));
		ovbcopy(rpp, rpp + 1, right->bb_numrecs * sizeof(*rpp));
		xfs_btree_check_lptr(cur, *lpp, level);
		*rkp = *lkp;
		*rpp = *lpp;
		xfs_bmbt_log_keys(cur, rbp, 1, right->bb_numrecs + 1);
		xfs_bmbt_log_ptrs(cur, rbp, 1, right->bb_numrecs + 1);
	} else {
		lrp = XFS_BMAP_REC_IADDR(left, left->bb_numrecs, cur);
		rrp = XFS_BMAP_REC_IADDR(right, 1, cur);
		ovbcopy(rrp, rrp + 1, right->bb_numrecs * sizeof(*rrp));
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
	if (error = xfs_btree_dup_cursor(cur, &tcur)) {
		xfs_bmbt_trace_cursor("xfs_bmbt_rshift error1", cur);
		return error;
	}
	xfs_btree_lastrec(tcur, level);
	if (error = xfs_bmbt_increment(tcur, level, &i)) {
		xfs_bmbt_trace_cursor("xfs_bmbt_rshift error2", tcur);
		xfs_btree_del_cursor(tcur, XFS_BTREE_ERROR);
		return error;
	}
	xfs_bmbt_updkey(tcur, rkp, level + 1);
	xfs_btree_del_cursor(tcur, XFS_BTREE_NOERROR);
	xfs_bmbt_trace_cursor("xfs_bmbt_rshift exit4", cur);
	*stat = 1;
	return 0;
}

/*
 * Split cur/level block in half.
 * Return new block number and its first record (to be inserted into parent).
 */
STATIC int					/* error */
xfs_bmbt_split(
	xfs_btree_cur_t		*cur,
	int			level,
	xfs_fsblock_t		*bnop,
	xfs_bmbt_key_t		*keyp,
	xfs_btree_cur_t		**curp,
	int			*stat)		/* success/failure */
{
	xfs_alloc_arg_t		args;		/* block allocation args */
	int			error;		/* error return value */
	int			i;		/* loop counter */
	xfs_fsblock_t		lbno;		/* left sibling block number */
	buf_t			*lbp;		/* left buffer pointer */
	xfs_bmbt_block_t	*left;		/* left btree block */
	xfs_bmbt_key_t		*lkp;		/* left btree key */
	xfs_bmbt_ptr_t		*lpp;		/* left address pointer */
	xfs_bmbt_rec_t		*lrp;		/* left record pointer */
	buf_t			*rbp;		/* right buffer pointer */
	xfs_bmbt_block_t	*right;		/* right btree block */
	xfs_bmbt_key_t		*rkp;		/* right btree key */
	xfs_bmbt_ptr_t		*rpp;		/* right address pointer */
	xfs_bmbt_block_t	*rrblock;	/* right-right btree block */
	buf_t			*rrbp;		/* right-right buffer pointer */
	xfs_bmbt_rec_t		*rrp;		/* right record pointer */

	xfs_bmbt_trace_cursor("xfs_bmbt_split entry", cur);
	xfs_bmbt_trace_argifk("xfs_bmbt_split args", cur, level, *bnop, keyp);
	args.tp = cur->bc_tp;
	args.mp = cur->bc_mp;
	lbp = cur->bc_bufs[level];
	lbno = XFS_DADDR_TO_FSB(args.mp, lbp->b_blkno);
	left = XFS_BUF_TO_BMBT_BLOCK(lbp);
	args.fsbno = cur->bc_private.b.firstblock;
	if (args.fsbno == NULLFSBLOCK) {
		args.fsbno = lbno;
		args.type = XFS_ALLOCTYPE_START_BNO;
	} else if (cur->bc_private.b.flist->xbf_low)
		args.type = XFS_ALLOCTYPE_FIRST_AG;
	else
		args.type = XFS_ALLOCTYPE_NEAR_BNO;
	args.mod = args.minleft = args.alignment = args.total = args.isfl =
		args.userdata = 0;
	args.minlen = args.maxlen = args.prod = 1;
	args.wasdel = cur->bc_private.b.flags & XFS_BTCUR_BPRV_WASDEL;
	if (error = xfs_alloc_vextent(&args)) {
		xfs_bmbt_trace_cursor("xfs_bmbt_split error0", cur);
		return error;
	}
	if (args.fsbno == NULLFSBLOCK) {
		xfs_bmbt_trace_cursor("xfs_bmbt_split exit0", cur);
		*stat = 0;
		return 0;
	}
	ASSERT(args.len == 1);
	cur->bc_private.b.firstblock = args.fsbno;
	cur->bc_private.b.allocated++;
	cur->bc_private.b.ip->i_d.di_nblocks++;
	xfs_trans_log_inode(args.tp, cur->bc_private.b.ip, XFS_ILOG_CORE);
	if (XFS_IS_QUOTA_ON(args.mp) &&
	    cur->bc_private.b.ip->i_ino != args.mp->m_sb.sb_uquotino &&
	    cur->bc_private.b.ip->i_ino != args.mp->m_sb.sb_pquotino) {
		xfs_trans_mod_dquot_byino(args.tp, cur->bc_private.b.ip,
					  XFS_TRANS_DQ_BCOUNT, 1L);
	}
	rbp = xfs_btree_get_bufl(args.mp, args.tp, args.fsbno, 0);
	right = XFS_BUF_TO_BMBT_BLOCK(rbp);
	xfs_btree_check_lblock(cur, left, level);
	right->bb_magic = XFS_BMAP_MAGIC;
	right->bb_level = left->bb_level;
	right->bb_numrecs = (__uint16_t)(left->bb_numrecs / 2);
	if ((left->bb_numrecs & 1) &&
	    cur->bc_ptrs[level] <= right->bb_numrecs + 1)
		right->bb_numrecs++;
	i = left->bb_numrecs - right->bb_numrecs + 1;
	if (level > 0) {
		lkp = XFS_BMAP_KEY_IADDR(left, i, cur);
		lpp = XFS_BMAP_PTR_IADDR(left, i, cur);
		rkp = XFS_BMAP_KEY_IADDR(right, 1, cur);
		rpp = XFS_BMAP_PTR_IADDR(right, 1, cur);
#ifdef DEBUG
		for (i = 0; i < right->bb_numrecs; i++)
			xfs_btree_check_lptr(cur, lpp[i], level);
#endif
		bcopy(lkp, rkp, right->bb_numrecs * sizeof(*rkp));
		bcopy(lpp, rpp, right->bb_numrecs * sizeof(*rpp));
		xfs_bmbt_log_keys(cur, rbp, 1, right->bb_numrecs);
		xfs_bmbt_log_ptrs(cur, rbp, 1, right->bb_numrecs);
		*keyp = *rkp;
	} else {
		lrp = XFS_BMAP_REC_IADDR(left, i, cur);
		rrp = XFS_BMAP_REC_IADDR(right, 1, cur);
		bcopy(lrp, rrp, right->bb_numrecs * sizeof(*rrp));
		xfs_bmbt_log_recs(cur, rbp, 1, right->bb_numrecs);
		keyp->br_startoff = xfs_bmbt_get_startoff(rrp);
	}
	left->bb_numrecs -= right->bb_numrecs;
	right->bb_rightsib = left->bb_rightsib;
	left->bb_rightsib = args.fsbno;
	right->bb_leftsib = lbno;
	xfs_bmbt_log_block(cur, rbp, XFS_BB_ALL_BITS);
	xfs_bmbt_log_block(cur, lbp, XFS_BB_NUMRECS | XFS_BB_RIGHTSIB);
	if (right->bb_rightsib != NULLDFSBNO) {
		if (error = xfs_btree_read_bufl(args.mp, args.tp,
				right->bb_rightsib, 0, &rrbp)) {
			xfs_bmbt_trace_cursor("xfs_bmbt_split error1", cur);
			return error;
		}
		rrblock = XFS_BUF_TO_BMBT_BLOCK(rrbp);
		xfs_btree_check_lblock(cur, rrblock, level);
		rrblock->bb_leftsib = args.fsbno;
		xfs_bmbt_log_block(cur, rrbp, XFS_BB_LEFTSIB);
	}
	if (cur->bc_ptrs[level] > left->bb_numrecs + 1) {
		xfs_btree_setbuf(cur, level, rbp);
		cur->bc_ptrs[level] -= left->bb_numrecs;
	}
	if (level + 1 < cur->bc_nlevels) {
		if (error = xfs_btree_dup_cursor(cur, curp)) {
			xfs_bmbt_trace_cursor("xfs_bmbt_split error2", cur);
			return error;
		}
		(*curp)->bc_ptrs[level + 1]++;
	}
	*bnop = args.fsbno;
	xfs_bmbt_trace_cursor("xfs_bmbt_split exit1", cur);
	*stat = 1;
	return 0;
}

#if defined(XFS_BMBT_TRACE)
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
		(__psunsigned_t)b, i, 0, 0,
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
		(__psunsigned_t)b, i0, i1, 0,
		0, 0, 0, 0,
		0, 0, 0, 0);
}

/*
 * Add a trace buffer entry for arguments, for 3 block-length args.
 */
STATIC void
xfs_bmbt_trace_argfff(
	char			*name,
	xfs_btree_cur_t		*cur,
	xfs_dfiloff_t		o,
	xfs_dfsbno_t		b,
	xfs_dfilblks_t		i)
{
	xfs_bmbt_trace_enter(name, cur, XFS_BMBT_KTRACE_ARGFFF,
		o >> 32, (int)o, b >> 32, (int)b,
		i >> 32, (int)i, 0, 0,
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
	xfs_dfilblks_t		c;
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
		(int)o, b >> 32, (int)b, c >> 32,
		(int)c, 0, 0, 0);
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
#if BMBT_USE_64
		r.l0 >> 32, (int)r.l0, r.l1 >> 32, (int)r.l1,
#else
		r.l0, r.l1, r.l2, r.l3,
#endif
		(unsigned long)cur->bc_bufs[0], (unsigned long)cur->bc_bufs[1],
		(unsigned long)cur->bc_bufs[2], (unsigned long)cur->bc_bufs[3],
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
	__psunsigned_t	a0,
	__psunsigned_t	a1,
	__psunsigned_t	a2,
	__psunsigned_t	a3,
	__psunsigned_t	a4,
	__psunsigned_t	a5,
	__psunsigned_t	a6,
	__psunsigned_t	a7,
	__psunsigned_t	a8,
	__psunsigned_t	a9,
	__psunsigned_t	a10,
	__psunsigned_t	a11)
{
	xfs_inode_t	*ip;
	int		whichfork;

	ip = cur->bc_private.b.ip;
	whichfork = cur->bc_private.b.whichfork;
	ktrace_enter(xfs_bmbt_trace_buf,
		(void *)((__psunsigned_t)type | (whichfork << 16)), 
		(void *)name, (void *)ip, (void *)cur,
		(void *)a0, (void *)a1, (void *)a2, (void *)a3,
		(void *)a4, (void *)a5, (void *)a6, (void *)a7,
		(void *)a8, (void *)a9, (void *)a10, (void *)a11);
	ASSERT(ip->i_btrace);
	ktrace_enter(ip->i_btrace,
		(void *)((__psunsigned_t)type | (whichfork << 16)), 
		(void *)name, (void *)ip, (void *)cur,
		(void *)a0, (void *)a1, (void *)a2, (void *)a3,
		(void *)a4, (void *)a5, (void *)a6, (void *)a7,
		(void *)a8, (void *)a9, (void *)a10, (void *)a11);
}
#endif	/* XFS_BMBT_TRACE */

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

	ASSERT(level >= 1);
	xfs_bmbt_trace_cursor("xfs_bmbt_updkey entry", cur);
	xfs_bmbt_trace_argik("xfs_bmbt_updkey args", cur, level, keyp);
	for (ptr = 1; ptr == 1 && level < cur->bc_nlevels; level++) {
		block = xfs_bmbt_get_block(cur, level, &bp);
		xfs_btree_check_lblock(cur, block, level);
		ptr = cur->bc_ptrs[level];
		kp = XFS_BMAP_KEY_IADDR(block, ptr, cur);
		*kp = *keyp;
		xfs_bmbt_log_keys(cur, bp, ptr, ptr);
	}
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
int						/* error */
xfs_bmbt_decrement(
	xfs_btree_cur_t		*cur,
	int			level,
	int			*stat)		/* success/failure */
{
	xfs_bmbt_block_t	*block;
	buf_t			*bp;
	int			error;		/* error return value */
	xfs_fsblock_t		fsbno;
	int			lev;
	xfs_mount_t		*mp;
	xfs_bmbt_ptr_t		*pp;
	xfs_trans_t		*tp;

	xfs_bmbt_trace_cursor("xfs_bmbt_decrement entry", cur);
	xfs_bmbt_trace_argi("xfs_bmbt_decrement entry", cur, level);
	ASSERT(level < cur->bc_nlevels);
	if (level < cur->bc_nlevels - 1)
		xfs_btree_readahead(cur, level, XFS_BTCUR_LEFTRA);
	if (--cur->bc_ptrs[level] > 0) {
		xfs_bmbt_trace_cursor("xfs_bmbt_decrement exit0", cur);
		*stat = 1;
		return 0;
	}
	block = xfs_bmbt_get_block(cur, level, &bp);
	xfs_btree_check_lblock(cur, block, level);
	if (block->bb_leftsib == NULLDFSBNO) {
		xfs_bmbt_trace_cursor("xfs_bmbt_decrement exit1", cur);
		*stat = 0;
		return 0;
	}
	for (lev = level + 1; lev < cur->bc_nlevels; lev++) {
		if (--cur->bc_ptrs[lev] > 0)
			break;
		if (lev < cur->bc_nlevels - 1)
			xfs_btree_readahead(cur, lev, XFS_BTCUR_LEFTRA);
	}
	if (lev == cur->bc_nlevels) {
		xfs_bmbt_trace_cursor("xfs_bmbt_decrement exit2", cur);
		*stat = 0;
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
		if (error = xfs_btree_read_bufl(mp, tp, fsbno, 0, &bp)) {
			xfs_bmbt_trace_cursor("xfs_bmbt_decrement error0", cur);
			return error;
		}
		xfs_btree_setbuf(cur, lev - 1, bp);
		block = XFS_BUF_TO_BMBT_BLOCK(bp);
		xfs_btree_check_lblock(cur, block, lev - 1);
		cur->bc_ptrs[lev - 1] = block->bb_numrecs;
	}
	xfs_bmbt_trace_cursor("xfs_bmbt_decrement exit3", cur);
	*stat = 1;
	return 0;
}

/*
 * Delete the record pointed to by cur.
 */
int					/* error */
xfs_bmbt_delete(
	xfs_btree_cur_t	*cur,
	int		*stat)		/* success/failure */
{
	int		error;		/* error return value */
	int		i;
	int		level;

	xfs_bmbt_trace_cursor("xfs_bmbt_delete entry", cur);
	for (level = 0, i = 2; i == 2; level++) {
		if (error = xfs_bmbt_delrec(cur, level, &i)) {
			xfs_bmbt_trace_cursor("xfs_bmbt_delete error0", cur);
			return error;
		}
	}
	if (i == 0) {
		for (level = 1; level < cur->bc_nlevels; level++) {
			if (cur->bc_ptrs[level] == 0) {
				if (error = xfs_bmbt_decrement(cur, level,
						&i)) {
					xfs_bmbt_trace_cursor(
						"xfs_bmbt_delete error1", cur);
					return error;
				}
				break;
			}
		}
	}
	xfs_bmbt_trace_cursor("xfs_bmbt_delete exit", cur);
	*stat = i;
	return 0;
}

/*
 * Convert a compressed bmap extent record to an uncompressed form.
 * This code must be in sync with the three routines xfs_bmbt_get_startoff,
 * xfs_bmbt_get_startblock, xfs_bmbt_get_blockcount.
 */
void
xfs_bmbt_get_all(
	xfs_bmbt_rec_t	*r,
	xfs_bmbt_irec_t	*s)
{
#if BMBT_USE_64
#if XFS_BIG_FILES
	s->br_startoff = ((xfs_fileoff_t)r->l0) >> 9;
#else	/* !XFS_BIG_FILES */
	{
		xfs_dfiloff_t	o;

		o = ((xfs_dfiloff_t)r->l0) >> 9;
		ASSERT((o >> 32) == 0);
		s->br_startoff = (xfs_fileoff_t)o;
	}
#endif	/* XFS_BIG_FILES */
#if XFS_BIG_FILESYSTEMS
	s->br_startblock = (((xfs_fsblock_t)r->l0 & XFS_MASK64LO(9)) << 43) | 
			   (((xfs_fsblock_t)r->l1) >> 21);
#else
#ifdef DEBUG
	{
		xfs_dfsbno_t	b;

		b = (((xfs_dfsbno_t)r->l0 & XFS_MASK64LO(9)) << 43) | 
		    (((xfs_dfsbno_t)r->l1) >> 21);
		ASSERT((b >> 32) == 0 || ISNULLDSTARTBLOCK(b));
		s->br_startblock = (xfs_fsblock_t)b;
	}
#else	/* !DEBUG */
	s->br_startblock = (xfs_fsblock_t)(((xfs_dfsbno_t)r->l1) >> 21);
#endif	/* DEBUG */
#endif	/* XFS_BIG_FILESYSTEMS */
	s->br_blockcount = (xfs_filblks_t)(r->l1 & XFS_MASK64LO(21));
#else	/* !BMBT_USE_64 */
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
	s->br_startblock =
		(((xfs_fsblock_t)(r->l1 & XFS_MASK32LO(9))) << 43) | 
		(((xfs_fsblock_t)r->l2) << 11) |
		(((xfs_fsblock_t)r->l3) >> 21);
#else
#ifdef DEBUG
	{
		xfs_dfsbno_t	b;

		b = (((xfs_dfsbno_t)(r->l1 & XFS_MASK32LO(9))) << 43) | 
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
	s->br_blockcount = (xfs_filblks_t)(r->l3 & XFS_MASK32LO(21));
#endif	/* BMBT_USE_64 */
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
	xfs_ifork_t		*ifp;
	xfs_bmbt_block_t	*rval;

	if (level < cur->bc_nlevels - 1) {
		*bpp = cur->bc_bufs[level];
		rval = XFS_BUF_TO_BMBT_BLOCK(*bpp);
	} else {
		*bpp = 0;
		ifp = XFS_IFORK_PTR(cur->bc_private.b.ip,
			cur->bc_private.b.whichfork);
		rval = ifp->if_broot;
	}
	return rval;
}

/*
 * Extract the blockcount field from a bmap extent record.
 */
xfs_filblks_t
xfs_bmbt_get_blockcount(
	xfs_bmbt_rec_t	*r)
{
#if BMBT_USE_64
	return (xfs_filblks_t)(r->l1 & XFS_MASK64LO(21));
#else	/* !BMBT_USE_64 */
	return (xfs_filblks_t)(r->l3 & XFS_MASK32LO(21));
#endif	/* BMBT_USE_64 */
}

/*
 * Extract the startblock field from a bmap extent record.
 */
xfs_fsblock_t
xfs_bmbt_get_startblock(
	xfs_bmbt_rec_t	*r)
{
#if BMBT_USE_64
#if XFS_BIG_FILESYSTEMS
	return (((xfs_fsblock_t)r->l0 & XFS_MASK64LO(9)) << 43) |
	       (((xfs_fsblock_t)r->l1) >> 21);
#else
#ifdef DEBUG
	xfs_dfsbno_t	b;

	b = (((xfs_dfsbno_t)r->l0 & XFS_MASK64LO(9)) << 43) |
	    (((xfs_dfsbno_t)r->l1) >> 21);
	ASSERT((b >> 32) == 0 || ISNULLDSTARTBLOCK(b));
	return (xfs_fsblock_t)b;
#else	/* !DEBUG */
	return (xfs_fsblock_t)(((xfs_dfsbno_t)r->l1) >> 21);
#endif	/* DEBUG */
#endif	/* XFS_BIG_FILESYSTEMS */
#else	/* !BMBT_USE_64 */
#if XFS_BIG_FILESYSTEMS
	return (((xfs_fsblock_t)(r->l1 & XFS_MASK32LO(9))) << 43) | 
	       (((xfs_fsblock_t)r->l2) << 11) |
	       (((xfs_fsblock_t)r->l3) >> 21);
#else
#ifdef DEBUG
	xfs_dfsbno_t	b;

	b = (((xfs_dfsbno_t)(r->l1 & XFS_MASK32LO(9))) << 43) | 
	    (((xfs_dfsbno_t)r->l2) << 11) |
	    (((xfs_dfsbno_t)r->l3) >> 21);
	ASSERT((b >> 32) == 0 || ISNULLDSTARTBLOCK(b));
	return (xfs_fsblock_t)b;
#else	/* !DEBUG */
	return (((xfs_fsblock_t)r->l2) << 11) |
	       (((xfs_fsblock_t)r->l3) >> 21);
#endif	/* DEBUG */
#endif	/* XFS_BIG_FILESYSTEMS */
#endif	/* BMBT_USE_64 */
}

/*
 * Extract the startoff field from a bmap extent record.
 */
xfs_fileoff_t
xfs_bmbt_get_startoff(
	xfs_bmbt_rec_t	*r)
{
#if BMBT_USE_64
#if XFS_BIG_FILES
	return ((xfs_fileoff_t)r->l0) >> 9;
#else	/* !XFS_BIG_FILES */
	xfs_dfiloff_t	o;

	o = ((xfs_dfiloff_t)r->l0) >> 9;
	ASSERT((o >> 32) == 0);
	return (xfs_fileoff_t)o;
#endif	/* XFS_BIG_FILES */
#else	/* !BMBT_USE_64 */
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
	return (((xfs_fileoff_t)r->l0) << 23) |
	       (((xfs_fileoff_t)r->l1) >> 9);
#endif	/* DEBUG */
#endif	/* XFS_BIG_FILES */
#endif	/* BMBT_USE_64 */
}

/*
 * Increment cursor by one record at the level.
 * For nonzero levels the leaf-ward information is untouched.
 */
int						/* error */
xfs_bmbt_increment(
	xfs_btree_cur_t		*cur,
	int			level,
	int			*stat)		/* success/failure */
{
	xfs_bmbt_block_t	*block;
	buf_t			*bp;
	int			error;		/* error return value */
	xfs_fsblock_t		fsbno;
	int			lev;
	xfs_mount_t		*mp;
	xfs_bmbt_ptr_t		*pp;
	xfs_trans_t		*tp;

	xfs_bmbt_trace_cursor("xfs_bmbt_increment entry", cur);
	xfs_bmbt_trace_argi("xfs_bmbt_increment args", cur, level);
	ASSERT(level < cur->bc_nlevels);
	if (level < cur->bc_nlevels - 1)
		xfs_btree_readahead(cur, level, XFS_BTCUR_RIGHTRA);
	block = xfs_bmbt_get_block(cur, level, &bp);
	xfs_btree_check_lblock(cur, block, level);
	if (++cur->bc_ptrs[level] <= block->bb_numrecs) {
		xfs_bmbt_trace_cursor("xfs_bmbt_increment exit0", cur);
		*stat = 1;
		return 0;
	}
	if (block->bb_rightsib == NULLDFSBNO) {
		xfs_bmbt_trace_cursor("xfs_bmbt_increment exit1", cur);
		*stat = 0;
		return 0;
	}
	for (lev = level + 1; lev < cur->bc_nlevels; lev++) {
		block = xfs_bmbt_get_block(cur, lev, &bp);
		xfs_btree_check_lblock(cur, block, lev);
		if (++cur->bc_ptrs[lev] <= block->bb_numrecs)
			break;
		if (lev < cur->bc_nlevels - 1)
			xfs_btree_readahead(cur, lev, XFS_BTCUR_RIGHTRA);
	}
	if (lev == cur->bc_nlevels) {
		xfs_bmbt_trace_cursor("xfs_bmbt_increment exit2", cur);
		*stat = 0;
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
		if (error = xfs_btree_read_bufl(mp, tp, fsbno, 0, &bp)) {
			xfs_bmbt_trace_cursor("xfs_bmbt_increment error0", cur);
			return error;
		}
		xfs_btree_setbuf(cur, lev - 1, bp);
		cur->bc_ptrs[lev - 1] = 1;
	}
	xfs_bmbt_trace_cursor("xfs_bmbt_increment exit3", cur);
	*stat = 1;
	return 0;
}

/*
 * Insert the current record at the point referenced by cur.
 */
int					/* error */
xfs_bmbt_insert(
	xfs_btree_cur_t	*cur,
	int		*stat)		/* success/failure */
{
	int		error;		/* error return value */
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
		if (error = xfs_bmbt_insrec(pcur, level++, &nbno, &nrec, &ncur,
				&i)) {
			if (pcur != cur)
				xfs_btree_del_cursor(pcur, XFS_BTREE_ERROR);
			xfs_bmbt_trace_cursor("xfs_bmbt_insert error", cur);
			return error;
		}
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
			xfs_btree_del_cursor(pcur, XFS_BTREE_NOERROR);
		}
		if (ncur) {
			pcur = ncur;
			ncur = (xfs_btree_cur_t *)0;
		}
	} while (nbno != NULLFSBLOCK);
	xfs_bmbt_trace_cursor("xfs_bmbt_insert exit", cur);
	*stat = i;
	return 0;
}

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
	static const short	offsets[] = {
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
		xfs_trans_log_inode(tp, cur->bc_private.b.ip,
			XFS_ILOG_FBROOT(cur->bc_private.b.whichfork));
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

int					/* error */
xfs_bmbt_lookup_eq(
	xfs_btree_cur_t	*cur,
	xfs_fileoff_t	off,
	xfs_fsblock_t	bno,
	xfs_filblks_t	len,
	int		*stat)		/* success/failure */
{
	cur->bc_rec.b.br_startoff = off;
	cur->bc_rec.b.br_startblock = bno;
	cur->bc_rec.b.br_blockcount = len;
	return xfs_bmbt_lookup(cur, XFS_LOOKUP_EQ, stat);
}

int					/* error */
xfs_bmbt_lookup_ge(
	xfs_btree_cur_t	*cur,
	xfs_fileoff_t	off,
	xfs_fsblock_t	bno,
	xfs_filblks_t	len,
	int		*stat)		/* success/failure */
{
	cur->bc_rec.b.br_startoff = off;
	cur->bc_rec.b.br_startblock = bno;
	cur->bc_rec.b.br_blockcount = len;
	return xfs_bmbt_lookup(cur, XFS_LOOKUP_GE, stat);
}

int					/* error */
xfs_bmbt_lookup_le(
	xfs_btree_cur_t	*cur,
	xfs_fileoff_t	off,
	xfs_fsblock_t	bno,
	xfs_filblks_t	len,
	int		*stat)		/* success/failure */
{
	cur->bc_rec.b.br_startoff = off;
	cur->bc_rec.b.br_startblock = bno;
	cur->bc_rec.b.br_blockcount = len;
	return xfs_bmbt_lookup(cur, XFS_LOOKUP_LE, stat);
}

/*
 * Give the bmap btree a new root block.  Copy the old broot contents
 * down into a real block and make the broot point to it.
 */
int						/* error */
xfs_bmbt_newroot(
	xfs_btree_cur_t		*cur,		/* btree cursor */
	int			*logflags,	/* logging flags for inode */
	int			*stat)		/* return status - 0 fail */
{
	xfs_alloc_arg_t		args;		/* allocation arguments */
	xfs_bmbt_block_t	*block;		/* bmap btree block */
	buf_t			*bp;		/* buffer for block */
	xfs_bmbt_block_t	*cblock;	/* child btree block */
	xfs_bmbt_key_t		*ckp;		/* child key pointer */
	xfs_bmbt_ptr_t		*cpp;		/* child ptr pointer */
	int			error;		/* error return code */
#ifdef DEBUG
	int			i;		/* loop counter */
#endif
	xfs_bmbt_key_t		*kp;		/* pointer to bmap btree key */
	int			level;		/* btree level */
	xfs_bmbt_ptr_t		*pp;		/* pointer to bmap block addr */

	level = cur->bc_nlevels - 1;
	block = xfs_bmbt_get_block(cur, level, &bp);
	/*
	 * Copy the root into a real block.
	 */
	args.mp = cur->bc_mp;
	pp = XFS_BMAP_PTR_IADDR(block, 1, cur);
	args.tp = cur->bc_tp;
	args.fsbno = cur->bc_private.b.firstblock;
	args.mod = args.minleft = args.alignment = args.total = args.isfl =
		args.userdata = 0;
	args.minlen = args.maxlen = args.prod = 1;
	args.wasdel = cur->bc_private.b.flags & XFS_BTCUR_BPRV_WASDEL;
	if (args.fsbno == NULLFSBLOCK) {
		xfs_btree_check_lptr(cur, *pp, level);
		args.fsbno = *pp;
		args.type = XFS_ALLOCTYPE_START_BNO;
	} else if (args.wasdel)
		args.type = XFS_ALLOCTYPE_FIRST_AG;
	else
		args.type = XFS_ALLOCTYPE_NEAR_BNO;
	if (error = xfs_alloc_vextent(&args)) {
		xfs_bmbt_trace_cursor("xfs_bmbt_newroot error0", cur);
		return error;
	}
	if (args.fsbno == NULLFSBLOCK) {
		xfs_bmbt_trace_cursor("xfs_bmbt_newroot exit1", cur);
		*stat = 0;
		return 0;
	}
	ASSERT(args.len == 1);
	cur->bc_private.b.firstblock = args.fsbno;
	cur->bc_private.b.allocated++;
	cur->bc_private.b.ip->i_d.di_nblocks++;
	if (XFS_IS_QUOTA_ON(args.mp) &&
	    cur->bc_private.b.ip->i_ino != args.mp->m_sb.sb_uquotino &&
	    cur->bc_private.b.ip->i_ino != args.mp->m_sb.sb_pquotino) {
		xfs_trans_mod_dquot_byino(args.tp, cur->bc_private.b.ip,
					  XFS_TRANS_DQ_BCOUNT, 1L);
	}
	bp = xfs_btree_get_bufl(args.mp, cur->bc_tp, args.fsbno, 0);
	cblock = XFS_BUF_TO_BMBT_BLOCK(bp);
	*cblock = *block;
	block->bb_level++;
	block->bb_numrecs = 1;
	cur->bc_nlevels++;
	cur->bc_ptrs[level + 1] = 1;
	kp = XFS_BMAP_KEY_IADDR(block, 1, cur);
	ckp = XFS_BMAP_KEY_IADDR(cblock, 1, cur);
	bcopy(kp, ckp, cblock->bb_numrecs * sizeof(*kp));
	cpp = XFS_BMAP_PTR_IADDR(cblock, 1, cur);
#ifdef DEBUG
	for (i = 0; i < cblock->bb_numrecs; i++)
		xfs_btree_check_lptr(cur, pp[i], level);
#endif
	bcopy(pp, cpp, cblock->bb_numrecs * sizeof(*pp));
	xfs_btree_check_lptr(cur, (xfs_bmbt_ptr_t)args.fsbno, level);
	*pp = args.fsbno;
	xfs_iroot_realloc(cur->bc_private.b.ip, 1 - cblock->bb_numrecs,
		cur->bc_private.b.whichfork);
	xfs_btree_setbuf(cur, level, bp);
	/*
	 * Do all this logging at the end so that 
	 * the root is at the right level.
	 */
	xfs_bmbt_log_block(cur, bp, XFS_BB_ALL_BITS);
	xfs_bmbt_log_keys(cur, bp, 1, cblock->bb_numrecs);
	xfs_bmbt_log_ptrs(cur, bp, 1, cblock->bb_numrecs);
	*logflags |=
		XFS_ILOG_CORE | XFS_ILOG_FBROOT(cur->bc_private.b.whichfork);
	*stat = 1;
	return 0;
}

/*
 * Set all the fields in a bmap extent record from the uncompressed form.
 */
void
xfs_bmbt_set_all(
	xfs_bmbt_rec_t	*r,
	xfs_bmbt_irec_t	*s)
{
#if XFS_BIG_FILES
	ASSERT((s->br_startoff & XFS_MASK64HI(9)) == 0);
	ASSERT((s->br_blockcount & XFS_MASK64HI(43)) == 0);
#else	/* !XFS_BIG_FILES */
	ASSERT((s->br_blockcount & XFS_MASK32HI(11)) == 0);
#endif	/* XFS_BIG_FILES */
#if XFS_BIG_FILESYSTEMS
	ASSERT((s->br_startblock & XFS_MASK64HI(12)) == 0);
#endif	/* XFS_BIG_FILESYSTEMS */
#if BMBT_USE_64
#if XFS_BIG_FILESYSTEMS
	r->l0 = ((xfs_bmbt_rec_base_t)s->br_startoff << 9) |
		  ((xfs_bmbt_rec_base_t)s->br_startblock >> 43);
	r->l1 = ((xfs_bmbt_rec_base_t)s->br_startblock << 21) | 
		  ((xfs_bmbt_rec_base_t)s->br_blockcount &
		   (xfs_bmbt_rec_base_t)XFS_MASK64LO(21));
#else	/* !XFS_BIG_FILESYSTEMS */
	if (ISNULLSTARTBLOCK(s->br_startblock)) {
		r->l0 = ((xfs_bmbt_rec_base_t)s->br_startoff << 9) |
			  (xfs_bmbt_rec_base_t)XFS_MASK64LO(9);
		r->l1 = XFS_MASK64HI(11) |
			  ((xfs_bmbt_rec_base_t)s->br_startblock << 21) |
			  ((xfs_bmbt_rec_base_t)s->br_blockcount &
			   (xfs_bmbt_rec_base_t)XFS_MASK64LO(21));
	} else {
		r->l0 = (xfs_bmbt_rec_base_t)s->br_startoff << 9;
		r->l1 = ((xfs_bmbt_rec_base_t)s->br_startblock << 21) | 
			  ((xfs_bmbt_rec_base_t)s->br_blockcount &
			   (xfs_bmbt_rec_base_t)XFS_MASK64LO(21));
	}
#endif	/* XFS_BIG_FILESYSTEMS */
#else	/* !BMBT_USE_64 */
	r->l0 = (xfs_bmbt_rec_base_t)(s->br_startoff >> 23);
	r->l3 = (((xfs_bmbt_rec_base_t)s->br_startblock) << 21) |
		  ((xfs_bmbt_rec_base_t)(s->br_blockcount & XFS_MASK32LO(21)));
#if XFS_BIG_FILESYSTEMS
	r->l1 = (((xfs_bmbt_rec_base_t)s->br_startoff) << 9) |
		  ((xfs_bmbt_rec_base_t)(s->br_startblock >> 43));
	r->l2 = (xfs_bmbt_rec_base_t)(s->br_startblock >> 11);
#else	/* !XFS_BIG_FILESYSTEMS */
	if (ISNULLSTARTBLOCK(s->br_startblock)) {
		r->l1 = (xfs_bmbt_rec_base_t)(s->br_startoff << 9) |
			  (xfs_bmbt_rec_base_t)XFS_MASK32LO(9);
		r->l2 = (xfs_bmbt_rec_base_t)XFS_MASK32HI(11) |
			  (xfs_bmbt_rec_base_t)(s->br_startblock >> 11);
	} else {
		r->l1 = (xfs_bmbt_rec_base_t)(s->br_startoff << 9);
		r->l2 = (xfs_bmbt_rec_base_t)(s->br_startblock >> 11);
	}
#endif	/* XFS_BIG_FILESYSTEMS */
#endif	/* BMBT_USE_64 */
}

/*
 * Set all the fields in a bmap extent record from the arguments.
 */
void
xfs_bmbt_set_allf(
	xfs_bmbt_rec_t	*r,
	xfs_fileoff_t	o,
	xfs_fsblock_t	b,
	xfs_filblks_t	c)
{
#if XFS_BIG_FILES
	ASSERT((o & XFS_MASK64HI(9)) == 0);
	ASSERT((c & XFS_MASK64HI(43)) == 0);
#else	/* !XFS_BIG_FILES */
	ASSERT((c & XFS_MASK32HI(11)) == 0);
#endif	/* XFS_BIG_FILES */
#if XFS_BIG_FILESYSTEMS
	ASSERT((b & XFS_MASK64HI(12)) == 0);
#endif	/* XFS_BIG_FILESYSTEMS */
#if BMBT_USE_64
#if XFS_BIG_FILESYSTEMS
	r->l0 = ((xfs_bmbt_rec_base_t)o << 9) | ((xfs_bmbt_rec_base_t)b >> 43);
	r->l1 = ((xfs_bmbt_rec_base_t)b << 21) | 
		  ((xfs_bmbt_rec_base_t)c &
		   (xfs_bmbt_rec_base_t)XFS_MASK64LO(21));
#else	/* !XFS_BIG_FILESYSTEMS */
	if (ISNULLSTARTBLOCK(b)) {
		r->l0 = ((xfs_bmbt_rec_base_t)o << 9) |
			  (xfs_bmbt_rec_base_t)XFS_MASK64LO(9);
		r->l1 = XFS_MASK64HI(11) |
			  ((xfs_bmbt_rec_base_t)b << 21) |
			  ((xfs_bmbt_rec_base_t)c &
			   (xfs_bmbt_rec_base_t)XFS_MASK64LO(21));
	} else {
		r->l0 = (xfs_bmbt_rec_base_t)o << 9;
		r->l1 = ((xfs_bmbt_rec_base_t)b << 21) | 
			  ((xfs_bmbt_rec_base_t)c &
			   (xfs_bmbt_rec_base_t)XFS_MASK64LO(21));
	}
#endif	/* XFS_BIG_FILESYSTEMS */
#else	/* !BMBT_USE_64 */
	r->l0 = (xfs_bmbt_rec_base_t)(o >> 23);
	r->l3 = (((xfs_bmbt_rec_base_t)b) << 21) |
		  ((xfs_bmbt_rec_base_t)(c & XFS_MASK32LO(21)));
#if XFS_BIG_FILESYSTEMS
	r->l1 = (((xfs_bmbt_rec_base_t)o) << 9) |
		  ((xfs_bmbt_rec_base_t)(b >> 43));
	r->l2 = (xfs_bmbt_rec_base_t)(b >> 11);
#else	/* !XFS_BIG_FILESYSTEMS */
	if (ISNULLSTARTBLOCK(b)) {
		r->l1 = (xfs_bmbt_rec_base_t)(o << 9) |
			  (xfs_bmbt_rec_base_t)XFS_MASK32LO(9);
		r->l2 = (xfs_bmbt_rec_base_t)XFS_MASK32HI(11) |
			  (xfs_bmbt_rec_base_t)(b >> 11);
	} else {
		r->l1 = (xfs_bmbt_rec_base_t)(o << 9);
		r->l2 = (xfs_bmbt_rec_base_t)(b >> 11);
	}
#endif	/* XFS_BIG_FILESYSTEMS */
#endif	/* BMBT_USE_64 */
}

/*
 * Set the blockcount field in a bmap extent record.
 */
void
xfs_bmbt_set_blockcount(
	xfs_bmbt_rec_t	*r,
	xfs_filblks_t	v)
{
#if XFS_BIG_FILES
	ASSERT((v & XFS_MASK64HI(43)) == 0);
#else	/* !XFS_BIG_FILES */
	ASSERT((v & XFS_MASK32HI(11)) == 0);
#endif
#if BMBT_USE_64
	r->l1 = (r->l1 & (xfs_bmbt_rec_base_t)XFS_MASK64HI(43)) |
		  (xfs_bmbt_rec_base_t)(v & XFS_MASK64LO(21));
#else	/* !BMBT_USE_64 */
	r->l3 = (r->l3 & (xfs_bmbt_rec_base_t)XFS_MASK32HI(11)) |
		  ((xfs_bmbt_rec_base_t)v & XFS_MASK32LO(21));
#endif	/* BMBT_USE_64 */
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
	ASSERT((v & XFS_MASK64HI(12)) == 0);
#endif	/* XFS_BIG_FILESYSTEMS */
#if BMBT_USE_64
#if XFS_BIG_FILESYSTEMS
	r->l0 = (r->l0 & (xfs_bmbt_rec_base_t)XFS_MASK64HI(55)) |
		  (xfs_bmbt_rec_base_t)(v >> 43);
	r->l1 = (r->l1 & (xfs_bmbt_rec_base_t)XFS_MASK64LO(21)) |
		  (xfs_bmbt_rec_base_t)(v << 21);
#else	/* !XFS_BIG_FILESYSTEMS */
	if (ISNULLSTARTBLOCK(v)) {
		r->l0 |= (xfs_bmbt_rec_base_t)XFS_MASK64LO(9);
		r->l1 = (xfs_bmbt_rec_base_t)XFS_MASK64HI(11) |
			  ((xfs_bmbt_rec_base_t)v << 21) |
			  (r->l1 & (xfs_bmbt_rec_base_t)XFS_MASK64LO(21));
	} else {
		r->l0 &= ~(xfs_bmbt_rec_base_t)XFS_MASK64LO(9);
		r->l1 = ((xfs_bmbt_rec_base_t)v << 21) |
			  (r->l1 & (xfs_bmbt_rec_base_t)XFS_MASK64LO(21));
	}
#endif	/* XFS_BIG_FILESYSTEMS */
#else	/* !BMBT_USE_64 */
#if XFS_BIG_FILESYSTEMS
	r->l1 = (r->l1 & XFS_MASK32HI(23)) | (xfs_bmbt_rec_base_t)(v >> 43);
	r->l2 = (xfs_bmbt_rec_base_t)(v >> 11);
#else	/* !XFS_BIG_FILESYSTEMS */
	if (ISNULLSTARTBLOCK(v)) {
		r->l1 |= XFS_MASK32LO(9);
		r->l2 = XFS_MASK32HI(11) | (xfs_bmbt_rec_base_t)(v >> 11);
	} else {
		r->l1 &= ~XFS_MASK32LO(9);
		r->l2 = (xfs_bmbt_rec_base_t)(v >> 11);
	}
#endif	/* XFS_BIG_FILESYSTEMS */
	r->l3 = (r->l3 & XFS_MASK32LO(21)) |
		  (((xfs_bmbt_rec_base_t)v) << 21);
#endif	/* BMBT_USE_64 */
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
	ASSERT((v & XFS_MASK64HI(9)) == 0);
#endif	/* XFS_BIG_FILES */
#if BMBT_USE_64
	r->l0 = ((xfs_bmbt_rec_base_t)v << 9) |
		  (r->l0 & (xfs_bmbt_rec_base_t)XFS_MASK64LO(9));
#else	/* !BMBT_USE_64 */
	r->l0 = (xfs_bmbt_rec_base_t)(v >> 23);
	r->l1 = ((xfs_bmbt_rec_base_t)v << 9) |
		  (r->l1 & (xfs_bmbt_rec_base_t)XFS_MASK32LO(9));
#endif	/* BMBT_USE_64 */
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
	xfs_filblks_t		len)
{
	xfs_bmbt_block_t	*block;
	buf_t			*bp;
	xfs_bmbt_key_t		key;
	int			ptr;
	xfs_bmbt_rec_t		*rp;

	xfs_bmbt_trace_cursor("xfs_bmbt_update entry", cur);
	xfs_bmbt_trace_argfff("xfs_bmbt_update args", cur,
		(xfs_dfiloff_t)off, (xfs_dfsbno_t)bno, (xfs_dfilblks_t)len);
	block = xfs_bmbt_get_block(cur, 0, &bp);
	xfs_btree_check_lblock(cur, block, 0);
	ptr = cur->bc_ptrs[0];
	rp = XFS_BMAP_REC_IADDR(block, ptr, cur);
	xfs_bmbt_set_allf(rp, off, bno, len);
	xfs_bmbt_log_recs(cur, bp, ptr, ptr);
	if (ptr > 1) {
		xfs_bmbt_trace_cursor("xfs_bmbt_update exit0", cur);
		return 1;
	}
	key.br_startoff = off;
	xfs_bmbt_updkey(cur, &key, 1);
	xfs_bmbt_trace_cursor("xfs_bmbt_update exit1", cur);
	return 1;
}
