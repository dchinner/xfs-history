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
#include "xfs_alloc_btree.h"
#include "xfs_ialloc.h"
#include "xfs_bmap_btree.h"
#include "xfs_btree.h"
#include "xfs_alloc.h"
#ifdef SIM
#include "sim.h"
#endif

#if !defined(SIM) || !defined(XFSDEBUG)
#define	kmem_check()		/* dummy for memory-allocation checking */
#endif

/*
 * Prototypes for internal functions.
 */

/*
 * Compute best start block and diff for "near" allocations.
 * freelen >= wantlen already checked by caller.
 */
STATIC xfs_agblock_t			/* difference value (absolute) */
xfs_alloc_compute_diff(
	xfs_agblock_t	wantbno,	/* target starting block */
	xfs_extlen_t	wantlen,	/* target length */
	xfs_agblock_t	freebno,	/* freespace's starting block */
	xfs_extlen_t	freelen,	/* freespace's length */
	xfs_agblock_t	*newbnop);	/* result: best start block from free */

/*
 * Fix up the length, based on mod and prod.
 * len should be k * prod + mod for some k.
 * If len is too small it is returned unchanged.
 * If len hits maxlen it is left alone.
 */
STATIC xfs_extlen_t			/* new length to use instead of len */
xfs_alloc_fix_len(
	xfs_extlen_t	mod,		/* mod value to fix length with */
	xfs_extlen_t	prod,		/* product value to fix length with */
	xfs_extlen_t	len,		/* input length */
	xfs_extlen_t	minlen,		/* minimum length */
	xfs_extlen_t	maxlen);	/* maximum length */

/*
 * Read in the allocation group header (free/alloc section).
 */
STATIC buf_t *				/* buffer for the ag freelist header */
xfs_alloc_read_agf(
	xfs_mount_t	*mp,		/* mount point structure */
	xfs_trans_t	*tp,		/* transaction pointer */
	xfs_agnumber_t	agno,		/* allocation group number */
	int		flags);		/* XFS_ALLOC_FLAG_... */

/*
 * Prototypes for per-ag allocation routines
 */

/*
 * Allocate a variable extent in the allocation group agno.
 * Type and bno are used to determine where in the allocation group the
 * extent will start.
 * Extent's length (returned in *len) will be between minlen and maxlen,
 * and of the form k * prod + mod unless there's nothing that large.
 * Return the starting a.g. block, or NULLAGBLOCK if we can't do it.
 */
STATIC xfs_agblock_t		/* starting block number of allocated extent */
xfs_alloc_ag_vextent(
	xfs_trans_t	*tp,	/* transaction pointer */
	buf_t		*agbuf,	/* buffer for a.g. freelist header */
	xfs_agnumber_t	agno,	/* allocation group number */
	xfs_agblock_t	bno,	/* allocation-group relative block number */
	xfs_extlen_t	minlen,	/* mininum size of extent */
	xfs_extlen_t	maxlen,	/* maximum size of extent */
	xfs_extlen_t	*len,	/* output: size of extent */
	xfs_alloctype_t	type,	/* allocation type XFS_ALLOCTYPE_... */
	int		wasdel,	/* set if allocation was previously delayed */
	xfs_extlen_t	mod,	/* mod value for extent size */
	xfs_extlen_t	prod);	/* prod value for extent size */

/*
 * Allocate a variable extent at exactly agno/bno.
 * Extent's length (returned in *len) will be between minlen and maxlen,
 * and of the form k * prod + mod unless there's nothing that large.
 * Return the starting a.g. block (bno), or NULLAGBLOCK if we can't do it.
 */
STATIC xfs_agblock_t		/* starting block number of allocated extent */
xfs_alloc_ag_vextent_exact(
	xfs_trans_t	*tp,	/* transaction pointer */
	buf_t		*agbuf,	/* buffer for a.g. freelist header */
	xfs_agnumber_t	agno,	/* allocation group number */
	xfs_agblock_t	bno,	/* allocation-group relative block number */
	xfs_extlen_t	minlen,	/* mininum size of extent */
	xfs_extlen_t	maxlen,	/* maximum size of extent */
	xfs_extlen_t	*len,	/* output: size of extent */
	xfs_extlen_t	mod,	/* mod value for extent size */
	xfs_extlen_t	prod);	/* prod value for extent size */

/*
 * Allocate a variable extent near bno in the allocation group agno.
 * Extent's length (returned in *len) will be between minlen and maxlen,
 * and of the form k * prod + mod unless there's nothing that large.
 * Return the starting a.g. block, or NULLAGBLOCK if we can't do it.
 */
STATIC xfs_agblock_t		/* starting block number of allocated extent */
xfs_alloc_ag_vextent_near(
	xfs_trans_t	*tp,	/* transaction pointer */
	buf_t		*agbuf,	/* buffer for a.g. freelist header */
	xfs_agnumber_t	agno,	/* allocation group number */
	xfs_agblock_t	bno,	/* allocation-group relative block number */
	xfs_extlen_t	minlen,	/* mininum size of extent */
	xfs_extlen_t	maxlen,	/* maximum size of extent */
	xfs_extlen_t	*len,	/* output: size of extent */
	xfs_extlen_t	mod,	/* mod value for extent size */
	xfs_extlen_t	prod);	/* prod value for extent size */

/*
 * Allocate a variable extent anywhere in the allocation group agno.
 * Extent's length (returned in *len) will be between minlen and maxlen,
 * and of the form k * prod + mod unless there's nothing that large.
 * Return the starting a.g. block, or NULLAGBLOCK if we can't do it.
 */
STATIC xfs_agblock_t		/* starting block number of allocated extent */
xfs_alloc_ag_vextent_size(
	xfs_trans_t	*tp,	/* transaction pointer */
	buf_t		*agbuf,	/* buffer for a.g. freelist header */
	xfs_agnumber_t	agno,	/* allocation group number */
	xfs_extlen_t	minlen,	/* mininum size of extent */
	xfs_extlen_t	maxlen,	/* maximum size of extent */
	xfs_extlen_t	*len,	/* output: size of extent */
	xfs_extlen_t	mod,	/* mod value for extent size */
	xfs_extlen_t	prod);	/* prod value for extent size */

/*
 * Free the extent starting at agno/bno for length.
 */
STATIC int			/* return success/failure, will be void */
xfs_free_ag_extent(
	xfs_trans_t	*tp,	/* transaction pointer */
	buf_t		*agbuf,	/* buffer for a.g. freelist header */
	xfs_agnumber_t	agno,	/* allocation group number */
	xfs_agblock_t	bno,	/* starting block number */
	xfs_extlen_t	len);	/* length of extent */

/*
 * Internal functions.
 */

/*
 * Compute best start block and diff for "near" allocations.
 * freelen >= wantlen already checked by caller.
 */
STATIC xfs_agblock_t			/* difference value (absolute) */
xfs_alloc_compute_diff(
	xfs_agblock_t	wantbno,	/* target starting block */
	xfs_extlen_t	wantlen,	/* target length */
	xfs_agblock_t	freebno,	/* freespace's starting block */
	xfs_extlen_t	freelen,	/* freespace's length */
	xfs_agblock_t	*newbnop)	/* result: best start block from free */
{
	xfs_agblock_t	freeend;	/* end of freespace extent */
	xfs_agblock_t	newbno;		/* return block number */
	xfs_agblock_t	wantend;	/* end of target extent */

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
 * Fix up the length, based on mod and prod.
 * len should be k * prod + mod for some k.
 * If len is too small it is returned unchanged.
 * If len hits maxlen it is left alone.
 */
STATIC xfs_extlen_t			/* new length to use instead of len */
xfs_alloc_fix_len(
	xfs_extlen_t	mod,		/* mod value to fix length with */
	xfs_extlen_t	prod,		/* product value to fix length with */
	xfs_extlen_t	len,		/* input length */
	xfs_extlen_t	minlen,		/* minimum length */
	xfs_extlen_t	maxlen)		/* maximum length */
{
	xfs_extlen_t	k;
	xfs_extlen_t	rlen;

	ASSERT(mod < prod);
	ASSERT(len >= minlen);
	ASSERT(len <= maxlen);
	if (prod <= 1 || len < mod || len == maxlen || (mod == 0 && len < prod))
		return len;
	k = len % prod;
	if (k == mod)
		return len;
	if (k > mod) {
		if ((rlen = len - k - mod) < minlen)
			return len;
	} else {
		if ((rlen = len - prod - (mod - k)) < minlen)
			return len;
	}
	return rlen;
}

/*
 * Read in the allocation group header (free/alloc section).
 */
STATIC buf_t *				/* buffer for the ag freelist header */
xfs_alloc_read_agf(
	xfs_mount_t	*mp,		/* mount point structure */
	xfs_trans_t	*tp,		/* transaction pointer */
	xfs_agnumber_t	agno,		/* allocation group number */
	int		flags)		/* XFS_ALLOC_FLAG_... */
{
	buf_t		*bp;		/* return value */
	daddr_t		d;		/* disk block address */
	xfs_sb_t	*sbp;		/* superblock structure */

	ASSERT(agno != NULLAGNUMBER);
	sbp = &mp->m_sb;
	d = xfs_ag_daddr(sbp, agno, XFS_AGF_DADDR);
	bp = xfs_trans_read_buf(tp, mp->m_dev, d, 1,
		(flags & XFS_ALLOC_FLAG_TRYLOCK) ? BUF_TRYLOCK : 0U);
	ASSERT(!bp || !geterror(bp));
	return bp;
}

/*
 * Allocation group level functions.
 */

/*
 * Allocate a variable extent in the allocation group agno.
 * Type and bno are used to determine where in the allocation group the
 * extent will start.
 * Extent's length (returned in *len) will be between minlen and maxlen,
 * and of the form k * prod + mod unless there's nothing that large.
 * Return the starting a.g. block, or NULLAGBLOCK if we can't do it.
 */
STATIC xfs_agblock_t		/* starting block number of allocated extent */
xfs_alloc_ag_vextent(
	xfs_trans_t	*tp,	/* transaction pointer */
	buf_t		*agbuf,	/* buffer for a.g. freelist header */
	xfs_agnumber_t	agno,	/* allocation group number */
	xfs_agblock_t	bno,	/* allocation-group relative block number */
	xfs_extlen_t	minlen,	/* mininum size of extent */
	xfs_extlen_t	maxlen,	/* maximum size of extent */
	xfs_extlen_t	*len,	/* output: size of extent */
	xfs_alloctype_t	type,	/* allocation type XFS_ALLOCTYPE_... */
	int		wasdel,	/* set if allocation was previously delayed */
	xfs_extlen_t	mod,	/* mod value for extent size */
	xfs_extlen_t	prod)	/* prod value for extent size */
{
	xfs_agf_t	*agf;	/* allocation group freelist header */
	xfs_agblock_t	r;	/* return value (starting block) */

	ASSERT(minlen > 0 && maxlen > 0 && minlen <= maxlen);
	ASSERT(mod < prod);
	/*
	 * Branch to correct routine based on the type.
	 */
	switch (type) {
	case XFS_ALLOCTYPE_THIS_AG:
		r = xfs_alloc_ag_vextent_size(tp, agbuf, agno, minlen, maxlen,
			len, mod, prod);
		break;
	case XFS_ALLOCTYPE_NEAR_BNO:
		r = xfs_alloc_ag_vextent_near(tp, agbuf, agno, bno, minlen,
			maxlen, len, mod, prod);
		break;
	case XFS_ALLOCTYPE_THIS_BNO:
		r = xfs_alloc_ag_vextent_exact(tp, agbuf, agno, bno, minlen,
			maxlen, len, mod, prod);
		break;
	default:
		ASSERT(0);
		/* NOTREACHED */
	}
	/*
	 * If the allocation worked, need to change the agf structure
	 * (and log it), and the superblock.
	 */
	if (r != NULLAGBLOCK) {
		int 	slen = (int)*len;

		ASSERT(*len >= minlen && *len <= maxlen);
		agf = xfs_buf_to_agf(agbuf);
		agf->agf_freeblks -= *len;
		xfs_alloc_log_agf(tp, agbuf, XFS_AGF_FREEBLKS);
		if (wasdel)
			xfs_trans_mod_sb(tp, XFS_TRANS_SB_RES_FDBLOCKS, -slen);
		else
			xfs_trans_mod_sb(tp, XFS_TRANS_SB_FDBLOCKS, -slen);
	}
	return r;
}

/*
 * Allocate a variable extent at exactly agno/bno.
 * Extent's length (returned in *len) will be between minlen and maxlen,
 * and of the form k * prod + mod unless there's nothing that large.
 * Return the starting a.g. block (bno), or NULLAGBLOCK if we can't do it.
 */
STATIC xfs_agblock_t		/* starting block number of allocated extent */
xfs_alloc_ag_vextent_exact(
	xfs_trans_t	*tp,	/* transaction pointer */
	buf_t		*agbuf,	/* buffer for a.g. freelist header */
	xfs_agnumber_t	agno,	/* allocation group number */
	xfs_agblock_t	bno,	/* allocation-group relative block number */
	xfs_extlen_t	minlen,	/* mininum size of extent */
	xfs_extlen_t	maxlen,	/* maximum size of extent */
	xfs_extlen_t	*len,	/* output: size of extent */
	xfs_extlen_t	mod,	/* mod value for extent size */
	xfs_extlen_t	prod)	/* prod value for extent size */
{
	xfs_btree_cur_t	*bno_cur;	/* by block-number btree cursor */
	xfs_btree_cur_t	*cnt_cur;	/* by count btree cursor */
	xfs_agblock_t	end;	/* end of allocated extent */
	xfs_agblock_t	fbno;	/* start block of found extent */
	xfs_agblock_t	fend;	/* end block of found extent */
	xfs_extlen_t	flen;	/* length of found extent */
	xfs_agblock_t	maxend;	/* end of maximal extent */
	xfs_agblock_t	minend;	/* end of minimal extent */
	xfs_mount_t	*mp;	/* mount point structure for filesystem */
	xfs_extlen_t	rlen;	/* requested extent length */

	/*
	 * Allocate/initialize a cursor for the by-number freespace btree.
	 */
	mp = tp->t_mountp;
	bno_cur = xfs_btree_init_cursor(mp, tp, agbuf, agno, XFS_BTNUM_BNO, 0);
	/*
	 * Lookup bno and minlen in the btree (minlen is irrelevant, really).
	 * Look for the closest free block <= bno, it must contain bno
	 * if any free block does.
	 */
	if (!xfs_alloc_lookup_le(bno_cur, bno, minlen)) {
		/*
		 * Didn't find it, return null.
		 */
		xfs_btree_del_cursor(bno_cur);
		return NULLAGBLOCK;
	}
	/*
	 * Grab the freespace record.
	 */
	xfs_alloc_get_rec(bno_cur, &fbno, &flen);
	ASSERT(fbno <= bno);
	minend = bno + minlen;
	maxend = bno + maxlen;
	fend = fbno + flen;
	/* 
	 * Give up if the freespace isn't long enough for the minimum request.
	 */
	if (fend < minend) {
		xfs_btree_del_cursor(bno_cur);
		return NULLAGBLOCK;
	}
	/*
	 * End of extent will be smaller of the freespace end and the
	 * maximal requested end.
	 */
	end = xfs_agbno_min(fend, maxend);
	/*
	 * Fix the length according to mod and prod if given.
	 */
	rlen = xfs_alloc_fix_len(mod, prod, end - bno, minlen, maxlen);
	end = bno + rlen;
	/*
	 * We are allocating bno for rlen [bno .. end)
	 */
	/*
	 * Allocate/initialize a cursor for the by-size btree.
	 */
	cnt_cur = xfs_btree_init_cursor(mp, tp, agbuf, agno, XFS_BTNUM_CNT, 0);
	/*
	 * Look up the previously found extent.
	 */
	{
		int	i;

		i = xfs_alloc_lookup_eq(cnt_cur, fbno, flen);
		ASSERT(i == 1);
	}
	/*
	 * Delete the extent from the by-size btree.
	 */
	xfs_alloc_delete(cnt_cur);
	/*
	 * If the found freespace starts left of the allocation, add back the
	 * leftover freespace to the by-size btree.
	 */
	if (fbno < bno) {
		xfs_alloc_lookup_eq(cnt_cur, fbno, bno - fbno);
		xfs_alloc_insert(cnt_cur);
	}
	/*
	 * If the found freespace ends right of the allocation, add back the
	 * leftover freespace to the by-size btree.
	 */
	if (fend > end) {
		xfs_alloc_lookup_eq(cnt_cur, end, fend - end);
		xfs_alloc_insert(cnt_cur);
	}
	xfs_alloc_rcheck(cnt_cur);
	xfs_alloc_kcheck(cnt_cur);
	xfs_btree_del_cursor(cnt_cur);
	/*
	 * If the found freespace matches the allocation, just delete it
	 * from the by-bno btree.
	 */
	if (fbno == bno && fend == end)
		xfs_alloc_delete(bno_cur);
	/*
	 * If the found freespace starts at the same block but is longer,
	 * just update the by-bno btree entry to be shorter.
	 */
	else if (fbno == bno)
		xfs_alloc_update(bno_cur, end, fend - end);
	else {
		/*
		 * If the found freespace starts left of the allocation,
		 * update the length of that by-bno entry.
		 */
		xfs_alloc_update(bno_cur, fbno, bno - fbno);
		/*
		 * ... and if the found freespace ends right of the
		 * allocation, add another btree entry with the leftover space.
		 */
		if (fend > end) {
			xfs_alloc_lookup_eq(bno_cur, end, fend - end);
			xfs_alloc_insert(bno_cur);
		}
	}
	*len = end - bno;
	xfs_alloc_rcheck(bno_cur);
	xfs_alloc_kcheck(bno_cur);
	xfs_btree_del_cursor(bno_cur);
	return bno;
}

/*
 * Allocate a variable extent near bno in the allocation group agno.
 * Extent's length (returned in *len) will be between minlen and maxlen,
 * and of the form k * prod + mod unless there's nothing that large.
 * Return the starting a.g. block, or NULLAGBLOCK if we can't do it.
 */
STATIC xfs_agblock_t		/* starting block number of allocated extent */
xfs_alloc_ag_vextent_near(
	xfs_trans_t	*tp,	/* transaction pointer */
	buf_t		*agbuf,	/* buffer for a.g. freelist header */
	xfs_agnumber_t	agno,	/* allocation group number */
	xfs_agblock_t	bno,	/* allocation-group relative block number */
	xfs_extlen_t	minlen,	/* mininum size of extent */
	xfs_extlen_t	maxlen,	/* maximum size of extent */
	xfs_extlen_t	*len,	/* output: size of extent */
	xfs_extlen_t	mod,	/* mod value for extent size */
	xfs_extlen_t	prod)	/* prod value for extent size */
{
	xfs_btree_cur_t	*bno_cur_gt;	/* cursor for bno btree, right side */
	xfs_btree_cur_t	*bno_cur_lt;	/* cursor for bno btree, left side */
	xfs_btree_cur_t	*cnt_cur;	/* cursor for count btree */
	xfs_agblock_t	gtbno;		/* start bno of right side entry */
	xfs_extlen_t	gtdiff;		/* difference to right side entry */
	xfs_agblock_t	gtend;		/* end bno of right side entry */
	xfs_extlen_t	gtlen;		/* length of right side entry */
	xfs_agblock_t	gtnew;		/* useful start bno of right side */
	xfs_agblock_t	gtnewend;	/* useful end bno of right side */
	int		i;		/* result code, temporary */
	xfs_agblock_t	ltbno;		/* start bno of left side entry */
	xfs_extlen_t	ltdiff;		/* difference to left side entry */
	xfs_agblock_t	ltend;		/* end bno of left side entry */
	xfs_extlen_t	ltlen;		/* length of left side entry */
	xfs_agblock_t	ltnew;		/* useful start bno of left side */
	xfs_agblock_t	ltnewend;	/* useful end bno of left side */
	xfs_mount_t	*mp;		/* mount point structure for filesys */
	xfs_extlen_t	rlen;		/* length of allocated extent */

	mp = tp->t_mountp;
	/*
	 * Get a cursor for the by-size btree.
	 */
	cnt_cur = xfs_btree_init_cursor(mp, tp, agbuf, agno, XFS_BTNUM_CNT, 0);
	ltlen = 0;
	/*
	 * See if there are any free extents as big as maxlen.
	 */
	if (!xfs_alloc_lookup_ge(cnt_cur, 0, maxlen)) {
		/*
		 * Nothing as big as maxlen.
		 * Is there anything at all in the tree?
		 * If so get the biggest extent.
		 */
		if (xfs_alloc_decrement(cnt_cur, 0))
			xfs_alloc_get_rec(cnt_cur, &ltbno, &ltlen);
		/*
		 * If nothing, or what we got is too small, give up.
		 */
		if (ltlen < minlen) {
			xfs_btree_del_cursor(cnt_cur);
			return NULLAGBLOCK;
		}
	}
	/* 
	 * First algorithm.
	 * If the requested extent is large wrt the freespaces available
	 * in this a.g., then the cursor will be pointing to a btree entry
	 * near the right edge of the tree.  If it's in the last btree leaf
	 * block, then we just examine all the entries in that block
	 * that are big enough, and pick the best one.
	 */
	if (xfs_btree_islastblock(cnt_cur, 0)) {
		xfs_extlen_t	bdiff;
		int		besti;
		xfs_agblock_t	bnew;

		/*
		 * Start from the entry that lookup found, sequence through
		 * all larger free blocks.
		 */
		i = cnt_cur->bc_ptrs[0];
		besti = 0;
		bdiff = ltdiff = (xfs_extlen_t)0;
		do {
			/*
			 * For each entry, decide if it's better than
			 * the previous best entry.
			 */
			xfs_alloc_get_rec(cnt_cur, &ltbno, &ltlen);
			rlen = xfs_extlen_min(ltlen, maxlen);
			rlen = xfs_alloc_fix_len(mod, prod, rlen, minlen,
				maxlen);
			ltdiff = xfs_alloc_compute_diff(bno, rlen, ltbno, ltlen,
				&ltnew);
			if (!besti || ltdiff < bdiff) {
				bdiff = ltdiff;
				bnew = ltnew;
				besti = i;
			}
			i++;
		} while (ltdiff > 0 && xfs_alloc_increment(cnt_cur, 0));
		/*
		 * Point at the best entry, and retrieve it again.
		 */
		cnt_cur->bc_ptrs[0] = besti;
		xfs_alloc_get_rec(cnt_cur, &ltbno, &ltlen);
		ltend = ltbno + ltlen;
		rlen = xfs_extlen_min(ltlen, maxlen);
		rlen = xfs_alloc_fix_len(mod, prod, rlen, minlen, maxlen);
		/*
		 * Delete that entry from the by-size tree.
		 */
		xfs_alloc_delete(cnt_cur);
		/*
		 * We are allocating starting at bnew for rlen blocks.
		 */
		ltnew = bnew;
		ltnewend = bnew + rlen;
		*len = rlen;
		/*
		 * Set up a cursor for the by-bno tree.
		 */
		bno_cur_lt = xfs_btree_init_cursor(mp, tp, agbuf, agno,
			XFS_BTNUM_BNO, 0);
		/*
		 * Find the entry we used.
		 */
		i = xfs_alloc_lookup_eq(bno_cur_lt, ltbno, ltlen);
		ASSERT(i == 1);
		/*
		 * Freespace properly contains allocated space.
		 */
		if (ltbno < ltnew && ltend > ltnewend) {
			/*
			 * Insert two leftover entries into the cnt tree.
			 * Update the bno entry, and add a new one for the
			 * new (leftover on the right) freespace.
			 */
			xfs_alloc_lookup_eq(cnt_cur, ltbno, ltnew - ltbno);
			xfs_alloc_insert(cnt_cur);
			xfs_alloc_update(bno_cur_lt, ltbno, ltnew - ltbno);
			xfs_alloc_lookup_eq(cnt_cur, ltnewend,
				ltend - ltnewend);
			xfs_alloc_insert(cnt_cur);
			xfs_alloc_lookup_eq(bno_cur_lt, ltnewend,
				ltend - ltnewend);
			xfs_alloc_insert(bno_cur_lt);
		}
		/*
		 * Freespace contains allocated space, matches at left side.
		 */
		else if (ltend > ltnewend) {
			/*
			 * Insert the leftover entry for the cnt tree.
			 * Update the bno entry to have just the leftover space.
			 */
			xfs_alloc_lookup_eq(cnt_cur, ltnewend,
				ltend - ltnewend);
			xfs_alloc_insert(cnt_cur);
			xfs_alloc_update(bno_cur_lt, ltnewend,
				ltend - ltnewend);
		}
		/*
		 * Freespace contains allocated space, matches at right side.
		 */
		else if (ltbno < ltnew) {
			/*
			 * Insert the leftover entry for the cnt tree.
			 * Update the bno entry to have just the leftover space.
			 */
			xfs_alloc_lookup_eq(cnt_cur, ltbno, ltnew - ltbno);
			xfs_alloc_insert(cnt_cur);
			xfs_alloc_update(bno_cur_lt, ltbno, ltnew - ltbno);
		}
		/*
		 * Freespace same size as allocated.
		 */
		else {
			/*
			 * Just delete the bno tree entry.
			 */
			xfs_alloc_delete(bno_cur_lt);
		}
		xfs_alloc_rcheck(cnt_cur);
		xfs_alloc_kcheck(cnt_cur);
		xfs_btree_del_cursor(cnt_cur);
		xfs_alloc_rcheck(bno_cur_lt);
		xfs_alloc_kcheck(bno_cur_lt);
		xfs_btree_del_cursor(bno_cur_lt);
		return ltnew;
	}
	/*
	 * Second algorithm.
	 * Search in the by-bno tree to the left and to the right
	 * simultaneously, until in each case we find a space big enough,
	 * or run into the edge of the tree.  When we run into the edge,
	 * we deallocate that cursor.
	 * If both searches succeed, we compare the two spaces and pick
	 * the better one.
	 */
	/*
	 * Allocate and initialize the cursor for the leftward search.
	 */
	bno_cur_lt = xfs_btree_init_cursor(mp, tp, agbuf, agno,
		XFS_BTNUM_BNO, 0);
	/*
	 * Lookup <= bno to find the leftward search's starting point.
	 */
	if (!xfs_alloc_lookup_le(bno_cur_lt, bno, maxlen)) {
		/*
		 * Didn't find anything; use this cursor for the rightward
		 * search.
		 */
		bno_cur_gt = bno_cur_lt;
		bno_cur_lt = 0;
	}
	/*
	 * Found something.  Duplicate the cursor for the rightward search.
	 */
	else
		bno_cur_gt = xfs_btree_dup_cursor(bno_cur_lt);
	/*
	 * Increment the cursor, so we will point at the entry just right
	 * of the leftward entry if any, or to the leftmost entry.
	 */
	if (!xfs_alloc_increment(bno_cur_gt, 0)) {
		/*
		 * It failed, there are no rightward entries.
		 */
		xfs_btree_del_cursor(bno_cur_gt);
		bno_cur_gt = 0;
	}
	/*
	 * Loop going left with the leftward cursor, right with the
	 * rightward cursor, until either both directions give up or
	 * we find an entry at least as big as minlen.
	 */
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
	/*
	 * We have to find something as big as minlen, we've already checked.
	 */
	ASSERT(bno_cur_lt || bno_cur_gt);
	/*
	 * Got both cursors still active, need to find better entry.
	 */
	if (bno_cur_lt && bno_cur_gt) {
		/*
		 * Left side is long enough, look for a right side entry.
		 */
		if (ltlen >= minlen) {
			/*
			 * Fix up the length.
			 */
			rlen = xfs_extlen_min(ltlen, maxlen);
			rlen = xfs_alloc_fix_len(mod, prod, rlen, minlen,
				maxlen);
			ltdiff = xfs_alloc_compute_diff(bno, rlen, ltbno,
				ltlen, &ltnew);
			/*
			 * Not perfect.
			 */
			if (ltdiff) {
				/*
				 * Look until we find a better one, run out of
				 * space, or run off the end.
				 */
				while (bno_cur_lt && bno_cur_gt) {
					xfs_alloc_get_rec(bno_cur_gt, &gtbno,
						&gtlen);
					/*
					 * The left one is clearly better.
					 */
					if (gtbno >= bno + ltdiff) {
						xfs_btree_del_cursor(
							bno_cur_gt);
						bno_cur_gt = 0;
						break;
					}
					/*
					 * If we reach a big enough entry,
					 * compare the two and pick the best.
					 */
					if (gtlen >= minlen) {
						rlen = xfs_extlen_min(gtlen,
							maxlen);
						rlen = xfs_alloc_fix_len(mod,
							prod, rlen, minlen,
							maxlen);
						gtdiff = xfs_alloc_compute_diff(
							bno, rlen, gtbno, gtlen,
							&gtnew);
						/*
						 * Right side is better.
						 */
						if (gtdiff < ltdiff) {
							xfs_btree_del_cursor(
								bno_cur_lt);
							bno_cur_lt = 0;
						}
						/*
						 * Left side is better.
						 */
						else {
							xfs_btree_del_cursor(
								bno_cur_gt);
							bno_cur_gt = 0;
						}
						break;
					}
					/*
					 * Fell off the right end.
					 */
					if (!xfs_alloc_increment(bno_cur_gt,
								 0)) {
						xfs_btree_del_cursor(
							bno_cur_gt);
						bno_cur_gt = 0;
						break;
					}
				}
			}
			/*
			 * The left side is perfect, trash the right side.
			 */
			else {
				xfs_btree_del_cursor(bno_cur_gt);
				bno_cur_gt = 0;
			}
		}
		/*
		 * It's the right side that was found first, look left.
		 */
		else {
			/*
			 * Fix up the length.
			 */
			rlen = xfs_extlen_min(gtlen, maxlen);
			rlen = xfs_alloc_fix_len(mod, prod, rlen, minlen,
				maxlen);
			gtdiff = xfs_alloc_compute_diff(bno, rlen, gtbno, gtlen,
				&gtnew);
			/*
			 * Right side entry isn't perfect.
			 */
			if (gtdiff) {
				/*
				 * Look until we find a better one, run out of
				 * space, or run off the end.
				 */
				while (bno_cur_lt && bno_cur_gt) {
					xfs_alloc_get_rec(bno_cur_lt, &ltbno,
						&ltlen);
					/*
					 * The right one is clearly better.
					 */
					if (ltbno <= bno - gtdiff) {
						xfs_btree_del_cursor(
							bno_cur_lt);
						bno_cur_lt = 0;
						break;
					}
					/*
					 * If we reach a big enough entry,
					 * compare the two and pick the best.
					 */
					if (ltlen >= minlen) {
						rlen = xfs_extlen_min(ltlen,
							maxlen);
						rlen = xfs_alloc_fix_len(mod,
							prod, rlen, minlen,
							maxlen);
						ltdiff = xfs_alloc_compute_diff(
							bno, rlen, ltbno, ltlen,
							&ltnew);
						/*
						 * Left side is better.
						 */
						if (ltdiff < gtdiff) {
							xfs_btree_del_cursor(
								bno_cur_gt);
							bno_cur_gt = 0;
						}
						/*
						 * Right side is better.
						 */
						else {
							xfs_btree_del_cursor(
								bno_cur_lt);
							bno_cur_lt = 0;
						}
						break;
					}
					/*
					 * Fell off the left end.
					 */
					if (!xfs_alloc_decrement(bno_cur_lt,
								 0)) {
						xfs_btree_del_cursor(
							bno_cur_lt);
						bno_cur_lt = 0;
						break;
					}
				}
			}
			/*
			 * The right side is perfect, trash the left side.
			 */
			else {
				xfs_btree_del_cursor(bno_cur_lt);
				bno_cur_lt = 0;
			}
		}
	}
	/*
	 * At this point we have selected a freespace entry, either to the
	 * left or to the right.
	 */
	/*
	 * On the left side.
	 */
	if (bno_cur_lt) {
		/*
		 * Fix up the length and compute the useful address.
		 */
		ltend = ltbno + ltlen;
		rlen = xfs_extlen_min(ltlen, maxlen);
		rlen = xfs_alloc_fix_len(mod, prod, rlen, minlen, maxlen);
		ltdiff = xfs_alloc_compute_diff(bno, rlen, ltbno, ltlen,
			&ltnew);
		ltnewend = ltnew + rlen;
		*len = rlen;
		/*
		 * Find the equivalent by-size btree record and delete it.
		 */
		i = xfs_alloc_lookup_eq(cnt_cur, ltbno, ltlen);
		ASSERT(i == 1);
		xfs_alloc_delete(cnt_cur);
		/*
		 * Freespace properly contains allocated space.
		 * Insert two leftover by-size records.
		 * Update the by-block tree for the left leftover,
		 * and insert a new by-block record for the right leftover.
		 */
		if (ltbno < ltnew && ltend > ltnewend) {
			xfs_alloc_lookup_eq(cnt_cur, ltbno, ltnew - ltbno);
			xfs_alloc_insert(cnt_cur);
			xfs_alloc_update(bno_cur_lt, ltbno, ltnew - ltbno);
			xfs_alloc_lookup_eq(cnt_cur, ltnewend, ltend - ltnewend);
			xfs_alloc_insert(cnt_cur);
			xfs_alloc_lookup_eq(bno_cur_lt, ltnewend, ltend - ltnewend);
			xfs_alloc_insert(bno_cur_lt);
		}
		/*
		 * Freespace contains allocated space, matches at left side.
		 * Insert the right-side leftover in the by-size tree.
		 * Update the by-block record with the new length.
		 */
		else if (ltend > ltnewend) {
			xfs_alloc_lookup_eq(cnt_cur, ltnewend, ltend - ltnewend);
			xfs_alloc_insert(cnt_cur);
			xfs_alloc_update(bno_cur_lt, ltnewend, ltend - ltnewend);
		}
		/*
		 * Freespace contains allocated space, matches at right side.
		 * Insert the left-side leftover in the by-size tree.
		 * Update the by-block record with the new start/length.
		 */
		else if (ltbno < ltnew) {
			xfs_alloc_lookup_eq(cnt_cur, ltbno, ltnew - ltbno);
			xfs_alloc_insert(cnt_cur);
			xfs_alloc_update(bno_cur_lt, ltbno, ltnew - ltbno);
		}
		/*
		 * Freespace same size as allocated.
		 * Just delete everything.
		 */
		else
			xfs_alloc_delete(bno_cur_lt);
		xfs_alloc_rcheck(cnt_cur);
		xfs_alloc_kcheck(cnt_cur);
		xfs_btree_del_cursor(cnt_cur);
		xfs_alloc_rcheck(bno_cur_lt);
		xfs_alloc_kcheck(bno_cur_lt);
		xfs_btree_del_cursor(bno_cur_lt);
		return ltnew;
	}
	/*
	 * On the right side.
	 */
	else {
		/*
		 * Fix up the length and compute the useful address.
		 */
		gtend = gtbno + gtlen;
		rlen = xfs_extlen_min(gtlen, maxlen);
		rlen = xfs_alloc_fix_len(mod, prod, rlen, minlen, maxlen);
		gtdiff = xfs_alloc_compute_diff(bno, rlen, gtbno, gtlen,
			&gtnew);
		gtnewend = gtnew + rlen;
		*len = rlen;
		/*
		 * Find the equivalent by-size btree record and delete it.
		 */
		i = xfs_alloc_lookup_eq(cnt_cur, gtbno, gtlen);
		ASSERT(i == 1);
		xfs_alloc_delete(cnt_cur);
		/*
		 * Other cases can't occur since gtbno > bno.
		 */
		/*
		 * Freespace contains allocated space, matches at left side.
		 * Insert the right-side leftover in the by-size tree.
		 * Update the by-block record with the new length.
		 */
		if (gtend > gtnewend) {
			xfs_alloc_lookup_eq(cnt_cur, gtnewend,
				gtend - gtnewend);
			xfs_alloc_insert(cnt_cur);
			xfs_alloc_update(bno_cur_gt, gtnewend,
				gtend - gtnewend);
		}
		/*
		 * Freespace same size as allocated.
		 * Just delete everything.
		 */
		else
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
 * Allocate a variable extent anywhere in the allocation group agno.
 * Extent's length (returned in *len) will be between minlen and maxlen,
 * and of the form k * prod + mod unless there's nothing that large.
 * Return the starting a.g. block, or NULLAGBLOCK if we can't do it.
 */
STATIC xfs_agblock_t		/* starting block number of allocated extent */
xfs_alloc_ag_vextent_size(
	xfs_trans_t	*tp,	/* transaction pointer */
	buf_t		*agbuf,	/* buffer for a.g. freelist header */
	xfs_agnumber_t	agno,	/* allocation group number */
	xfs_extlen_t	minlen,	/* mininum size of extent */
	xfs_extlen_t	maxlen,	/* maximum size of extent */
	xfs_extlen_t	*len,	/* output: size of extent */
	xfs_extlen_t	mod,	/* mod value for extent size */
	xfs_extlen_t	prod)	/* prod value for extent size */
{
	xfs_btree_cur_t	*bno_cur;	/* cursor for bno btree */
	xfs_btree_cur_t	*cnt_cur;	/* cursor for cnt btree */
	xfs_agblock_t	fbno;		/* start of found freespace */
	xfs_extlen_t	flen;		/* length of found freespace */
	xfs_mount_t	*mp;		/* mount point of filesystem */
	xfs_extlen_t	rlen;		/* length of returned extent */

	mp = tp->t_mountp;
	/*
	 * Allocate and initialize a cursor for the by-size btree.
	 */
	cnt_cur = xfs_btree_init_cursor(mp, tp, agbuf, agno, XFS_BTNUM_CNT, 0);
	/*
	 * Look for an entry >= maxlen blocks.
	 * If none, then pick up the last entry in the tree unless the
	 * tree is empty.
	 */
	if (!xfs_alloc_lookup_ge(cnt_cur, 0, maxlen)) {
		if (xfs_alloc_decrement(cnt_cur, 0))
			xfs_alloc_get_rec(cnt_cur, &fbno, &flen);
		else
			flen = 0;
		/*
		 * Nothing as big as minlen, give up.
		 */
		if (flen < minlen) {
			xfs_btree_del_cursor(cnt_cur);
			return NULLAGBLOCK;
		}
		rlen = flen;
	}
	/*
	 * There's a freespace as big as maxlen, get it.
	 */
	else {
		xfs_alloc_get_rec(cnt_cur, &fbno, &flen);
		rlen = maxlen;
	}
	/*
	 * Fix up the length.
	 */
	rlen = xfs_alloc_fix_len(mod, prod, rlen, minlen, maxlen);
	/*
	 * Delete the entry from the by-size btree.
	 */
	xfs_alloc_delete(cnt_cur);
	/*
	 * Allocate and initialize a cursor for the by-block tree.
	 * Look up the found space in that tree.
	 */
	bno_cur = xfs_btree_init_cursor(mp, tp, agbuf, agno, XFS_BTNUM_BNO, 0);
	{
		int	i;

		i = xfs_alloc_lookup_eq(bno_cur, fbno, flen);
		ASSERT(i == 1);
	}
	/*
	 * If we're not using the whole space, insert an entry for the
	 * leftover space in the by-size btree, and update the by-block entry.
	 */
	if (rlen < flen) {
		xfs_alloc_lookup_eq(cnt_cur, fbno + rlen, flen - rlen);
		xfs_alloc_insert(cnt_cur);
		xfs_alloc_update(bno_cur, fbno + rlen, flen - rlen);
	}
	/*
	 * Otherwise, just delete the entry from the by-block tree.
	 */
	else
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

/*
 * Free the extent starting at agno/bno for length.
 */
STATIC int			/* return success/failure, will be void */
xfs_free_ag_extent(
	xfs_trans_t	*tp,	/* transaction pointer */
	buf_t		*agbuf,	/* buffer for a.g. freelist header */
	xfs_agnumber_t	agno,	/* allocation group number */
	xfs_agblock_t	bno,	/* starting block number */
	xfs_extlen_t	len)	/* length of extent */
{
	xfs_btree_cur_t	*bno_cur;	/* cursor for by-block btree */
	xfs_btree_cur_t	*cnt_cur;	/* cursor for by-size btree */
	xfs_agblock_t	gtbno;		/* start of right neighbor block */
	xfs_extlen_t	gtlen;		/* length of right neighbor block */
	int		haveleft;	/* have a left neighbor block */
	int		haveright;	/* have a right neighbor block */
	int		i;		/* temp, result code */
	xfs_agblock_t	ltbno;		/* start of left neighbor block */
	xfs_extlen_t	ltlen;		/* length of left neighbor block */
	xfs_mount_t	*mp;		/* mount point struct for filesystem */
	xfs_agblock_t	nbno;		/* new starting block of freespace */
	xfs_extlen_t	nlen;		/* new length of freespace */

	mp = tp->t_mountp;
	/* 
	 * Allocate and initialize a cursor for the by-block btree.
	 */
	bno_cur = xfs_btree_init_cursor(mp, tp, agbuf, agno, XFS_BTNUM_BNO, 0);
	/* 
	 * Look for a neighboring block on the left (lower block numbers)
	 * that is contiguous with this space.
	 */
	if (haveleft = xfs_alloc_lookup_le(bno_cur, bno, len)) {
		/*
		 * There is a block to our left.
		 */
		xfs_alloc_get_rec(bno_cur, &ltbno, &ltlen);
		/*
		 * It's not contiguous, though.
		 */
		if (ltbno + ltlen < bno)
			haveleft = 0;
		/*
		 * If this happens the request to free this space was 
		 * invalid, it's (partly) already free.
		 */
		else if (ltbno + ltlen > bno) {
			xfs_btree_del_cursor(bno_cur);
			return 0;
			/* WILL BE ASSERT XXX */
		}
	}
	/* 
	 * Look for a neighboring block on the right (higher block numbers)
	 * that is contiguous with this space.
	 */
	if (haveright = xfs_alloc_increment(bno_cur, 0)) {
		/*
		 * There is a block to our right.
		 */
		xfs_alloc_get_rec(bno_cur, &gtbno, &gtlen);
		/*
		 * It's not contiguous, though.
		 */
		if (bno + len < gtbno)
			haveright = 0;
		/*
		 * If this happens the request to free this space was
		 * invalid, it's (partly) already free.
		 */
		else if (gtbno < bno + len) {
			xfs_btree_del_cursor(bno_cur);
			return 0;
			/* WILL BE ASSERT XXX */
		}
	}
	/*
	 * Now allocate and initialize a cursor for the by-size tree.
	 */
	cnt_cur = xfs_btree_init_cursor(mp, tp, agbuf, agno, XFS_BTNUM_CNT, 0);
	/*
	 * Have both left and right contiguous neighbors.
	 * Merge all three into a single free block.
	 */
	if (haveleft && haveright) {
		/*
		 * Delete the old by-size entry on the left.
		 */
		i = xfs_alloc_lookup_eq(cnt_cur, ltbno, ltlen);
		ASSERT(i == 1);
		xfs_alloc_delete(cnt_cur);
		/*
		 * Delete the old by-size entry on the right.
		 */
		i = xfs_alloc_lookup_eq(cnt_cur, gtbno, gtlen);
		ASSERT(i == 1);
		xfs_alloc_delete(cnt_cur);
		/*
		 * Delete the old by-block entry for the right block.
		 */
		xfs_alloc_delete(bno_cur);
		/*
		 * Move the by-block cursor back to the left neighbor.
		 */
		xfs_alloc_decrement(bno_cur, 0);
#ifdef DEBUG
		/*
		 * Check that this is the right record: delete didn't
		 * mangle the cursor.
		 */
		{
			xfs_agblock_t	xxbno;
			xfs_extlen_t	xxlen;

			xfs_alloc_get_rec(bno_cur, &xxbno, &xxlen);
			ASSERT(xxbno == ltbno);
			ASSERT(xxlen == ltlen);
		}
#endif
		/*
		 * Update remaining by-block entry to the new, joined block.
		 */
		nbno = ltbno;
		nlen = len + ltlen + gtlen;
		xfs_alloc_update(bno_cur, nbno, nlen);
	}
	/*
	 * Have only a left contiguous neighbor.
	 * Merge it together with the new freespace.
	 */
	else if (haveleft) {
		/*
		 * Delete the old by-size entry on the left.
		 */
		i = xfs_alloc_lookup_eq(cnt_cur, ltbno, ltlen);
		ASSERT(i == 1);
		xfs_alloc_delete(cnt_cur);
		/*
		 * Back up the by-block cursor to the left neighbor, and
		 * update its length.
		 */
		xfs_alloc_decrement(bno_cur, 0);
		nbno = ltbno;
		nlen = len + ltlen;
		xfs_alloc_update(bno_cur, nbno, nlen);
	}
	/*
	 * Have only a right contiguous neighbor.
	 * Merge it together with the new freespace.
	 */
	else if (haveright) {
		/*
		 * Delete the old by-size entry on the right.
		 */
		i = xfs_alloc_lookup_eq(cnt_cur, gtbno, gtlen);
		ASSERT(i == 1);
		xfs_alloc_delete(cnt_cur);
		/*
		 * Update the starting block and length of the right 
		 * neighbor in the by-block tree.
		 */
		nbno = bno;
		nlen = len + gtlen;
		xfs_alloc_update(bno_cur, nbno, nlen);
	}
	/*
	 * No contiguous neighbors.
	 * Insert the new freespace into the by-block tree.
	 */
	else {
		nbno = bno;
		nlen = len;
		xfs_alloc_insert(bno_cur);
	}
	xfs_alloc_rcheck(bno_cur);
	xfs_alloc_kcheck(bno_cur);
	xfs_btree_del_cursor(bno_cur);
	/*
	 * In all cases we need to insert the new freespace in the by-size tree.
	 */
	xfs_alloc_lookup_eq(cnt_cur, nbno, nlen);
	xfs_alloc_insert(cnt_cur);
	xfs_alloc_rcheck(cnt_cur);
	xfs_alloc_kcheck(cnt_cur);
	xfs_btree_del_cursor(cnt_cur);
	/*
	 * Update the freespace totals in the ag and superblock.
	 */
	{
		xfs_agf_t	*agf;

		agf = xfs_buf_to_agf(agbuf);
		agf->agf_freeblks += len;
		xfs_alloc_log_agf(tp, agbuf, XFS_AGF_FREEBLKS);
		xfs_trans_mod_sb(tp, XFS_TRANS_SB_FDBLOCKS, (int)len);
	}
	return 1;
}

/* 
 * Visible (exported) allocation/free functions.
 * Some of these are used just by xfs_alloc_btree.c and this file.
 */

/*
 * Return the number of free blocks left in the allocation group.
 * Return 0 if flags has XFS_ALLOC_FLAG_TRYLOCK and the lock fails.
 */
xfs_extlen_t			/* number of remaining free blocks */
xfs_alloc_ag_freeblks(
	xfs_mount_t	*mp,	/* file system mount structure */
	xfs_trans_t	*tp,	/* transaction pointer */
	xfs_agnumber_t	agno,	/* allocation group number */
	int		flags)	/* XFS_ALLOC_FLAG_... */
{
	buf_t		*agbuf;	/* buffer for a.g. freelist structure */
	xfs_agf_t	*agf;	/* a.g. freelist structure */
	xfs_extlen_t	freeblks;/* return value */

	agbuf = xfs_alloc_read_agf(mp, tp, agno, flags);
	if (!agbuf)
		return 0;
	agf = xfs_buf_to_agf(agbuf);
	freeblks = agf->agf_freeblks;
	xfs_trans_brelse(tp, agbuf);
	return freeblks;
}

/*
 * Allocate an extent (fixed-size).
 * Return the extent's starting block, NULLFSBLOCK on failure.
 */
xfs_fsblock_t			/* extent's starting block */
xfs_alloc_extent(
	xfs_trans_t	*tp,	/* transaction pointer */
	xfs_fsblock_t	bno,	/* requested starting block */
	xfs_extlen_t	len,	/* requested length */
	xfs_alloctype_t	type,	/* allocation type, see above defn */
	xfs_extlen_t	total,	/* total blocks needed in transaction */
	int		wasdel)	/* extent was previously del-alloced */
{
	xfs_extlen_t	rlen;	/* returned length from xfs_alloc_vextent */
	xfs_fsblock_t	rval;	/* return value */

	rval = xfs_alloc_vextent(tp, bno, len, len, &rlen, type, total, wasdel,
		0, 1);
	if (rval != NULLFSBLOCK)
		ASSERT(rlen == len);
	return rval;
}

/*
 * Decide whether to use this allocation group for this allocation.
 * If so, fix up the btree freelist's size.
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
	xfs_agblock_t	agbno;
	buf_t		*agbuf;
	xfs_agf_t	*agf;
	xfs_agblock_t	bno;
	buf_t		*buf;
	xfs_mount_t	*mp;
	xfs_extlen_t	need;

	mp = tp->t_mountp;
	/*
	 * Get the a.g. freespace buffer.
	 * Can fail if we're not blocking on locks, and it's held.
	 */
	agbuf = xfs_alloc_read_agf(mp, tp, agno, flags);
	if (!agbuf)
		return NULL;
	/*
	 * Figure out how many blocks we should have in the freelist.
	 */
	agf = xfs_buf_to_agf(agbuf);
	need = XFS_MIN_FREELIST(agf);
	/*
	 * Adjust total blocks to be allocated, if it's given.
	 */
	if (total && agf->agf_freecount < need)
		total += need - agf->agf_freecount;
	/*
	 * If there isn't enough total or single-extent, reject it.
	 */
	if (minlen > agf->agf_longest || total > agf->agf_freeblks) {
		xfs_trans_brelse(tp, agbuf);
		return NULL;
	}
	/*
	 * Make the freelist shorter if it's too long.
	 */
	while (agf->agf_freecount > need) {
		bno = xfs_alloc_get_freelist(tp, agbuf, &buf);
		xfs_free_ag_extent(tp, agbuf, agno, bno, 1);
	}
	/*
	 * Make the freelist longer if it's too short.
	 */
	while (agf->agf_freecount < need) {
		xfs_extlen_t	i;

		i = need - agf->agf_freecount;
		/*
		 * Allocate as many blocks as possible at once.
		 */
		agbno = xfs_alloc_ag_vextent(tp, agbuf, agno, 0, 1, i, &i,
			XFS_ALLOCTYPE_THIS_AG, 0, 0, 1);
		/*
		 * Stop if we run out.  Won't happen if callers are obeying
		 * the restrictions correctly.
		 */
		if (agbno == NULLAGBLOCK)
			break;
		/*
		 * Put each allocated block on the list.
		 */
		for (bno = agbno + i - 1; bno >= agbno; bno--) {
			buf = xfs_btree_get_bufs(mp, tp, agno, bno, 0);
			xfs_alloc_put_freelist(tp, agbuf, buf);
		}
	}
	return agbuf;
}

/*
 * Get a block from the freelist.
 * Returns with the buffer for the block gotten.
 */
xfs_agblock_t			/* block address retrieved from freelist */
xfs_alloc_get_freelist(
	xfs_trans_t	*tp,	/* transaction pointer */
	buf_t		*agbuf,	/* buffer containing the agf structure */
	buf_t		**bufp)	/* out: buffer pointer for the free block */
{
	xfs_agf_t		*agf;	/* a.g. freespace structure */
	xfs_agblock_t		bno;	/* block number returned */
	xfs_btree_sblock_t	*block;	/* block's data */
	buf_t			*buf;	/* buffer for the block */
	xfs_mount_t		*mp;	/* mount point for filesystem */

	mp = tp->t_mountp;
	agf = xfs_buf_to_agf(agbuf);
	bno = agf->agf_freelist;
	/*
	 * Freelist is empty, give up.
	 */
	if (bno == NULLAGBLOCK)
		return NULLAGBLOCK;
	buf = xfs_btree_read_bufs(mp, tp, agf->agf_seqno, bno, 0);
	block = xfs_buf_to_sblock(buf);
	agf->agf_freecount--;
	/*
	 * The link to the next block is stored as the first word of the block.
	 */
	agf->agf_freelist = *(xfs_agblock_t *)block;
	xfs_alloc_log_agf(tp, agbuf, XFS_AGF_FREELIST | XFS_AGF_FREECOUNT);
	*bufp = buf;
	kmem_check();
	return bno;
}

/*
 * Log the given fields from the agf structure.
 */
void
xfs_alloc_log_agf(
	xfs_trans_t	*tp,	/* transaction pointer */
	buf_t		*buf,	/* buffer for a.g. freelist header */
	int		fields)	/* mask of fields to be logged (XFS_AGF_...) */
{
	int	first;		/* first byte offset */
	int	last;		/* last byte offset */
	static const int	offsets[] = {
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
	xfs_trans_log_buf(tp, buf, (uint)first, (uint)last);
	kmem_check();
}

/*
 * Find the next freelist block number.  For printing routines.
 */
xfs_agblock_t			/* a.g.-relative block number for btree list */
xfs_alloc_next_free(
	xfs_mount_t	*mp,	/* file system mount structure */
	xfs_trans_t	*tp,	/* transaction pointer */
	buf_t		*agbuf,	/* buffer for a.g. freelist header */
	xfs_agblock_t	bno)	/* current freelist block number */
{
	xfs_agf_t	*agf;	/* a.g. freespace structure */
	buf_t		*buf;	/* buffer for next freelist block */

	agf = xfs_buf_to_agf(agbuf);
	buf = xfs_btree_read_bufs(mp, tp, agf->agf_seqno, bno, 0);
	bno = *(xfs_agblock_t *)buf->b_un.b_addr;
	xfs_trans_brelse(tp, buf);
	return bno;
}

/*
 * Put the buffer on the freelist for the allocation group.
 */
void
xfs_alloc_put_freelist(
	xfs_trans_t	*tp,	/* transaction pointer */
	buf_t		*agbuf,	/* buffer for a.g. freelist header */
	buf_t		*buf)	/* buffer for the block being freed */
{
	xfs_agf_t		*agf;	/* a.g. freespace structure */
	xfs_btree_sblock_t	*block;	/* data of "buf" */
	xfs_agblock_t		bno;	/* block number of buf */
	xfs_mount_t		*mp;	/* mount point of filesystem */
	xfs_sb_t		*sbp;	/* superblock of filesystem */

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

/*
 * Allocate an extent (variable-size).
 * Depending on the allocation type, we either look in a single allocation
 * group or loop over the allocation groups to find the result.
 */
xfs_fsblock_t			/* extent's starting block, or NULLFSBLOCK */
xfs_alloc_vextent(
	xfs_trans_t	*tp,	/* transaction pointer */
	xfs_fsblock_t	bno,	/* requested starting block */
	xfs_extlen_t	minlen,	/* minimum requested length */
	xfs_extlen_t	maxlen,	/* maximum requested length */
	xfs_extlen_t	*len,	/* output: actual allocated length */
	xfs_alloctype_t	type,	/* allocation type, see above definition */
	xfs_extlen_t	total,	/* total blocks needed in transaction */
	int		wasdel,	/* extent was previously delayed-allocated */
	xfs_extlen_t	mod,	/* length should be k * prod + mod unless */
	xfs_extlen_t	prod)	/* there's nothing as big as mod */
{
	xfs_agblock_t	agbno;	/* start of allocated extent */
	buf_t		*agbuf;	/* a.g. freespace header buffer */
	xfs_agnumber_t	agno;	/* allocation group number selected */
	xfs_agblock_t	agsize;	/* allocation group size */
	int		flags;	/* locking flags */
	xfs_mount_t	*mp;	/* mount point for filesystem */
	xfs_alloctype_t	ntype;	/* actual type used for allocating */
	xfs_sb_t	*sbp;	/* superblock structure */
	xfs_agnumber_t	tagno;	/* allocation group number being tried */

	ntype = type;
	mp = tp->t_mountp;
	sbp = &mp->m_sb;
	agsize = sbp->sb_agblocks;
	/* 
	 * These should really be asserts, left this way for now just
	 * for the benefit of xfs_test.
	 */
	if (xfs_fsb_to_agno(sbp, bno) >= sbp->sb_agcount ||
	    xfs_fsb_to_agbno(sbp, bno) >= sbp->sb_agblocks || minlen > maxlen ||
	    minlen > agsize || len == 0 || mod >= prod)
		return NULLFSBLOCK;
	/*
	 * Just fix this up, for the case where the last a.g. is shorter
	 * (or there's only one a.g.) and the caller couldn't easily figure
	 * that out (xfs_bmap_alloc).
	 */
	if (maxlen > agsize)
		maxlen = agsize;
	agbno = NULLAGBLOCK;
	ntype = XFS_ALLOCTYPE_THIS_AG;
	switch (type) {
	case XFS_ALLOCTYPE_THIS_AG:
	case XFS_ALLOCTYPE_NEAR_BNO:
	case XFS_ALLOCTYPE_THIS_BNO:
		/*
		 * These three force us into a single a.g.
		 */
		agno = xfs_fsb_to_agno(sbp, bno);
		agbuf = xfs_alloc_fix_freelist(tp, agno, minlen, total, 1);
		if (!agbuf)
			break;
		agbno = xfs_fsb_to_agbno(sbp, bno);
		agbno = xfs_alloc_ag_vextent(tp, agbuf, agno, agbno, minlen,
			maxlen, len, type, wasdel, mod, prod); 
		break;
	case XFS_ALLOCTYPE_START_BNO:
		/*
		 * Try near allocation first, then anywhere-in-ag after
		 * the first a.g. fails.
		 */
		agbno = xfs_fsb_to_agbno(sbp, bno);
		ntype = XFS_ALLOCTYPE_NEAR_BNO;
		/* FALLTHROUGH */
	case XFS_ALLOCTYPE_ANY_AG:
	case XFS_ALLOCTYPE_START_AG:
	case XFS_ALLOCTYPE_FIRST_AG:
		/*
		 * Rotate through the allocation groups looking for a winner.
		 */
		if (type == XFS_ALLOCTYPE_ANY_AG) {
			/*
			 * Start with the last place we left off.
			 */
			tagno = agno = mp->m_agrotor;
			flags = XFS_ALLOC_FLAG_TRYLOCK;
		} else if (type == XFS_ALLOCTYPE_FIRST_AG) {
			/*
			 * Start with allocation group given by bno.
			 */
			tagno = xfs_fsb_to_agno(sbp, bno);
			agno = 0;
			flags = 0;
		} else {
			/*
			 * Start with the given allocation group.
			 */
			tagno = agno = xfs_fsb_to_agno(sbp, bno);
			flags = XFS_ALLOC_FLAG_TRYLOCK;
		}
		/*
		 * Loop over allocation groups twice; first time with
		 * trylock set, second time without.
		 */
		for (;;) {
			agbuf = xfs_alloc_fix_freelist(tp, tagno, minlen,
				total, flags);
			/*
			 * If we get a buffer back then the allocation will fly.
			 */
			if (agbuf) {
				agbno = xfs_alloc_ag_vextent(tp, agbuf, tagno,
					agbno, minlen, maxlen, len, ntype,
					wasdel, mod, prod); 
				break;
			}
			/*
			 * Didn't work, figure out the next iteration.
			 */
			if (tagno == agno && type == XFS_ALLOCTYPE_START_BNO)
				ntype = XFS_ALLOCTYPE_THIS_AG;
			if (++tagno == sbp->sb_agcount)
				tagno = 0;
			/* 
			 * Reached the starting a.g., must either be done
			 * or switch to non-trylock mode.
			 */
			if (tagno == agno) {
				if (!flags)
					break;
				flags = 0;
				if (type == XFS_ALLOCTYPE_START_BNO)
					ntype = XFS_ALLOCTYPE_NEAR_BNO;
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
 * Just break up the extent address and hand off to xfs_free_ag_extent
 * after fixing up the freelist.
 */
int				/* success/failure; will become void */
xfs_free_extent(
	xfs_trans_t	*tp,	/* transaction pointer */
	xfs_fsblock_t	bno,	/* starting block number of extent */
	xfs_extlen_t	len)	/* length of extent */
{
	xfs_agblock_t	agbno;	/* bno relative to allocation group */
	buf_t		*agbuf;	/* buffer for a.g. freespace header */
	xfs_agnumber_t	agno;	/* allocation group number */
	xfs_mount_t	*mp;	/* mount point for filesystem */
	xfs_sb_t	*sbp;	/* superblock for filesystem */

	mp = tp->t_mountp;
	sbp = &mp->m_sb;
	ASSERT(len != 0);
	agno = xfs_fsb_to_agno(sbp, bno);
	ASSERT(agno < sbp->sb_agcount);
	agbno = xfs_fsb_to_agbno(sbp, bno);
	ASSERT(agbno < sbp->sb_agblocks);
	agbuf = xfs_alloc_fix_freelist(tp, agno, 0, 0, 1);
	ASSERT(agbuf != NULL);
	return xfs_free_ag_extent(tp, agbuf, agno, agbno, len);
}
