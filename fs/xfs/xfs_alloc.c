#ident	"$Revision: 1.66 $"

/*
 * Free space allocation for xFS.
 */

#include <sys/param.h>
#include <sys/sysinfo.h>
#ifdef SIM
#define _KERNEL 1
#endif
#include <sys/buf.h>
#include <sys/ksa.h>
#ifdef SIM
#undef _KERNEL
#endif
#include <sys/vnode.h>
#include <sys/debug.h>
#include <sys/ktrace.h>
#include <sys/kmem.h>
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
#include "xfs_bmap_btree.h"
#include "xfs_ialloc_btree.h"
#include "xfs_btree.h"
#include "xfs_ialloc.h"
#include "xfs_alloc.h"
#ifdef SIM
#include "sim.h"
#endif

#if !defined(SIM) || !defined(XFSDEBUG)
#define	kmem_check()		/* dummy for memory-allocation checking */
#endif

/*
 * Allocation tracing.
 */
ktrace_t	*xfs_alloc_trace_buf;

zone_t		*xfs_alloc_arg_zone;

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
STATIC void
xfs_alloc_fix_len(
	xfs_alloc_arg_t	*args);		/* allocation argument structure */

/*
 * Fix up length if there is too little space left in the a.g.
 * Return 1 if ok, 0 if too little, should give up.
 */
STATIC int
xfs_alloc_fix_minleft(
	xfs_alloc_arg_t	*args);		/* allocation argument structure */

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
 * Read in the allocation group free block array.
 */
STATIC buf_t *				/* buffer for the ag free block array */
xfs_alloc_read_agfl(
	xfs_mount_t	*mp,		/* mount point structure */
	xfs_trans_t	*tp,		/* transaction pointer */
	xfs_agnumber_t	agno);		/* allocation group number */

#if defined(DEBUG) && !defined(SIM)
/*
 * Put an entry in the allocation trace buffer.
 */
STATIC void
xfs_alloc_trace_addentry(
	int		tag,		/* XFS_ALLOC_KTRACE_... */
	char		*name,		/* function tag string */
	char		*str,		/* additional string */
	xfs_mount_t	*mp,		/* file system mount point */
	int		agno,		/* allocation group number */
	int		agbno,		/* a.g. relative block number */
	int		minlen,		/* minimum allocation length */
	int		maxlen,		/* maximum allocation length */
	int		mod,		/* mod value for extent size */
	int		prod,		/* prod value for extent size */
	int		minleft,	/* min left in a.g. after allocation */
	int		total,		/* total blocks needed in xaction */
	int		len,		/* length of extent */
	int		type,		/* allocation type */
	int		wasdel,		/* set if allocation was prev delayed */
	int		isfl);		/* set if is freelist allocation/free */

/*
 * Add an allocation trace entry for an alloc call.
 */
STATIC void
xfs_alloc_trace_alloc(
	char		*name,		/* function tag string */
	char		*str,		/* additional string */
	xfs_alloc_arg_t	*args);		/* allocation argument structure */

/*
 * Add an allocation trace entry for a free call.
 */
STATIC void
xfs_alloc_trace_free(
	char		*name,		/* function tag string */
	char		*str,		/* additional string */
	xfs_mount_t	*mp,		/* file system mount point */
	xfs_agnumber_t	agno,		/* allocation group number */
	xfs_agblock_t	agbno,		/* a.g. relative block number */
	xfs_extlen_t	len,		/* length of extent */
	int		isfl);		/* set if is freelist allocation/free */

/*
 * Add an allocation trace entry for modifying an agf.
 */
STATIC void
xfs_alloc_trace_modagf(
	char		*name,		/* function tag string */
	char		*str,		/* additional string */
	xfs_mount_t	*mp,		/* file system mount point */
	xfs_agf_t	*agf,		/* new agf value */
	int		flags);		/* logging flags for agf */
#else
#define	xfs_alloc_trace_alloc(n,s,a)
#define	xfs_alloc_trace_free(n,s,a,b,c,d,e)
#define	xfs_alloc_trace_modagf(n,s,a,b,c)
#endif	/* DEBUG && !SIM */

/*
 * Prototypes for per-ag allocation routines
 */

/*
 * Allocate a variable extent in the allocation group agno.
 * Type and bno are used to determine where in the allocation group the
 * extent will start.
 * Extent's length (returned in len) will be between minlen and maxlen,
 * and of the form k * prod + mod unless there's nothing that large.
 * Return the starting a.g. block, or NULLAGBLOCK if we can't do it.
 */
STATIC void
xfs_alloc_ag_vextent(
	xfs_alloc_arg_t	*args);	/* allocation argument structure */

/*
 * Allocate a variable extent at exactly agno/bno.
 * Extent's length (returned in *len) will be between minlen and maxlen,
 * and of the form k * prod + mod unless there's nothing that large.
 * Return the starting a.g. block (bno), or NULLAGBLOCK if we can't do it.
 */
STATIC int
xfs_alloc_ag_vextent_exact(
	xfs_alloc_arg_t	*args);	/* allocation argument structure */

/*
 * Allocate a variable extent near bno in the allocation group agno.
 * Extent's length (returned in *len) will be between minlen and maxlen,
 * and of the form k * prod + mod unless there's nothing that large.
 * Return the starting a.g. block, or NULLAGBLOCK if we can't do it.
 */
STATIC int
xfs_alloc_ag_vextent_near(
	xfs_alloc_arg_t	*args);	/* allocation argument structure */

/*
 * Allocate a variable extent anywhere in the allocation group agno.
 * Extent's length (returned in *len) will be between minlen and maxlen,
 * and of the form k * prod + mod unless there's nothing that large.
 * Return the starting a.g. block, or NULLAGBLOCK if we can't do it.
 */
STATIC int
xfs_alloc_ag_vextent_size(
	xfs_alloc_arg_t	*args);	/* allocation argument structure */

/*
 * Free the extent starting at agno/bno for length.
 */
STATIC int			/* return success/failure, will be void */
xfs_free_ag_extent(
	xfs_trans_t	*tp,	/* transaction pointer */
	buf_t		*agbp,	/* buffer for a.g. freelist header */
	xfs_agnumber_t	agno,	/* allocation group number */
	xfs_agblock_t	bno,	/* starting block number */
	xfs_extlen_t	len,	/* length of extent */
	int		isfl);	/* set if is freelist blocks - no sb acctg */

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
STATIC void
xfs_alloc_fix_len(
	xfs_alloc_arg_t	*args)		/* allocation argument structure */
{
	xfs_extlen_t	k;
	xfs_extlen_t	rlen;

	ASSERT(args->mod < args->prod);
	rlen = args->len;
	ASSERT(rlen >= args->minlen);
	ASSERT(rlen <= args->maxlen);
	if (args->prod <= 1 || rlen < args->mod || rlen == args->maxlen ||
	    (args->mod == 0 && rlen < args->prod))
		return;
	k = rlen % args->prod;
	if (k == args->mod)
		return;
	if (k > args->mod) {
		if ((int)(rlen = rlen - k - args->mod) < (int)args->minlen)
			return;
	} else {
		if ((int)(rlen = rlen - args->prod - (args->mod - k)) <
		    (int)args->minlen)
			return;
	}
	ASSERT(rlen >= args->minlen);
	ASSERT(rlen <= args->maxlen);
	args->len = rlen;
}

/*
 * Fix up length if there is too little space left in the a.g.
 * Return 1 if ok, 0 if too little, should give up.
 */
STATIC int
xfs_alloc_fix_minleft(
	xfs_alloc_arg_t	*args)		/* allocation argument structure */
{
	xfs_agf_t	*agf;		/* a.g. freelist header */
	int		diff;		/* free space difference */

	if (args->minleft == 0)
		return 1;
	agf = XFS_BUF_TO_AGF(args->agbp);
	diff = agf->agf_freeblks + agf->agf_flcount - args->len - args->minleft;
	if (diff >= 0)
		return 1;
	args->len += diff;		/* shrink the allocated space */
	if (args->len >= args->minlen)
		return 1;
	args->agbno = NULLAGBLOCK;
	return 0;
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
	xfs_agf_t	*agf;		/* ag freelist header */
	buf_t		*bp;		/* return value */
	daddr_t		d;		/* disk block address */
	xfs_perag_t	*pag;		/* per allocation group data */

	ASSERT(agno != NULLAGNUMBER);
	d = XFS_AG_DADDR(mp, agno, XFS_AGF_DADDR);
	bp = xfs_trans_read_buf(tp, mp->m_dev, d, 1,
		(flags & XFS_ALLOC_FLAG_TRYLOCK) ? BUF_TRYLOCK : 0U);
	ASSERT(!bp || !geterror(bp));
	if (!bp)
		return NULL;
	pag = &mp->m_perag[agno];
	agf = XFS_BUF_TO_AGF(bp);
	if (!pag->pagf_init) {
		pag->pagf_freeblks = agf->agf_freeblks;
		pag->pagf_flcount = agf->agf_flcount;
		pag->pagf_longest = agf->agf_longest;
		pag->pagf_levels[XFS_BTNUM_BNOi] =
			agf->agf_levels[XFS_BTNUM_BNOi];
		pag->pagf_levels[XFS_BTNUM_CNTi] =
			agf->agf_levels[XFS_BTNUM_CNTi];
		pag->pagf_init = 1;
	} else {
		ASSERT(pag->pagf_freeblks == agf->agf_freeblks);
		ASSERT(pag->pagf_flcount == agf->agf_flcount);
		ASSERT(pag->pagf_longest == agf->agf_longest);
		ASSERT(pag->pagf_levels[XFS_BTNUM_BNOi] ==
		       agf->agf_levels[XFS_BTNUM_BNOi]);
		ASSERT(pag->pagf_levels[XFS_BTNUM_CNTi] ==
		       agf->agf_levels[XFS_BTNUM_CNTi]);
	}
	return bp;
}

/*
 * Read in the allocation group free block array.
 */
STATIC buf_t *				/* buffer for the ag free block array */
xfs_alloc_read_agfl(
	xfs_mount_t	*mp,		/* mount point structure */
	xfs_trans_t	*tp,		/* transaction pointer */
	xfs_agnumber_t	agno)		/* allocation group number */
{
	buf_t		*bp;		/* return value */
	daddr_t		d;		/* disk block address */

	ASSERT(agno != NULLAGNUMBER);
	d = XFS_AG_DADDR(mp, agno, XFS_AGFL_DADDR);
	bp = xfs_trans_read_buf(tp, mp->m_dev, d, 1, 0);
	ASSERT(bp);
	ASSERT(!geterror(bp));
	return bp;
}

#if defined(DEBUG) && !defined(SIM)
/*
 * Put an entry in the allocation trace buffer.
 */
STATIC void
xfs_alloc_trace_addentry(
	int		tag,		/* XFS_ALLOC_KTRACE_... */
	char		*name,		/* function tag string */
	char		*str,		/* additional string */
	xfs_mount_t	*mp,		/* file system mount point */
	int		agno,		/* allocation group number */
	int		agbno,		/* a.g. relative block number */
	int		minlen,		/* minimum allocation length */
	int		maxlen,		/* maximum allocation length */
	int		mod,		/* mod value for extent size */
	int		prod,		/* prod value for extent size */
	int		minleft,	/* min left in a.g. after allocation */
	int		total,		/* total blocks needed in xaction */
	int		len,		/* length of extent */
	int		type,		/* allocation type */
	int		wasdel,		/* set if allocation was prev delayed */
	int		isfl)		/* set if is freelist allocation/free */
{
	ktrace_enter(xfs_alloc_trace_buf,
		(void *)tag, (void *)name, (void *)str, (void *)mp,
		(void *)agno, (void *)agbno, (void *)minlen, (void *)maxlen,
		(void *)mod, (void *)prod, (void *)minleft, (void *)total,
		(void *)len, (void *)type, (void *)wasdel, (void *)isfl);
}

/*
 * Add an allocation trace entry for an alloc call.
 */
STATIC void
xfs_alloc_trace_alloc(
	char		*name,		/* function tag string */
	char		*str,		/* additional string */
	xfs_alloc_arg_t	*args)		/* allocation argument structure */
{
	xfs_alloc_trace_addentry(XFS_ALLOC_KTRACE_ALLOC, name, str, args->mp,
		(int)args->agno, (int)args->agbno, (int)args->minlen,
		(int)args->maxlen, (int)args->mod, (int)args->prod,
		(int)args->minleft, (int)args->total, (int)args->len,
		(((int)args->type) << 16) | (int)args->otype,
		args->wasdel, args->isfl);
}

/*
 * Add an allocation trace entry for a free call.
 */
STATIC void
xfs_alloc_trace_free(
	char		*name,		/* function tag string */
	char		*str,		/* additional string */
	xfs_mount_t	*mp,		/* file system mount point */
	xfs_agnumber_t	agno,		/* allocation group number */
	xfs_agblock_t	agbno,		/* a.g. relative block number */
	xfs_extlen_t	len,		/* length of extent */
	int		isfl)		/* set if is freelist allocation/free */
{
	xfs_alloc_trace_addentry(XFS_ALLOC_KTRACE_FREE, name, str, mp,
		(int)agno, (int)agbno, 0, 0, 0, 0, 0, 0, (int)len, 0, 0, isfl);
}

/*
 * Add an allocation trace entry for modifying an agf.
 */
STATIC void
xfs_alloc_trace_modagf(
	char		*name,		/* function tag string */
	char		*str,		/* additional string */
	xfs_mount_t	*mp,		/* file system mount point */
	xfs_agf_t	*agf,		/* new agf value */
	int		flags)		/* logging flags for agf */
{
	xfs_alloc_trace_addentry(XFS_ALLOC_KTRACE_MODAGF, name, str, mp,
		flags, (int)agf->agf_seqno, (int)agf->agf_length,
		(int)agf->agf_roots[XFS_BTNUM_BNO],
		(int)agf->agf_roots[XFS_BTNUM_CNT],
		(int)agf->agf_levels[XFS_BTNUM_BNO],
		(int)agf->agf_levels[XFS_BTNUM_CNT],
		(int)agf->agf_flfirst, (int)agf->agf_fllast,
		(int)agf->agf_flcount, (int)agf->agf_freeblks,
		(int)agf->agf_longest);
}
#endif	/* DEBUG && !SIM */

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
STATIC void
xfs_alloc_ag_vextent(
	xfs_alloc_arg_t	*args)	/* argument structure for allocation */
{
	int		wasfromfl;	/* alloc made from freelist */

	ASSERT(args->minlen > 0);
	ASSERT(args->maxlen > 0);
	ASSERT(args->minlen <= args->maxlen);
	ASSERT(args->mod < args->prod);
	/*
	 * Branch to correct routine based on the type.
	 */
	switch (args->type) {
	case XFS_ALLOCTYPE_THIS_AG:
		wasfromfl = xfs_alloc_ag_vextent_size(args);
		break;
	case XFS_ALLOCTYPE_NEAR_BNO:
		wasfromfl = xfs_alloc_ag_vextent_near(args);
		break;
	case XFS_ALLOCTYPE_THIS_BNO:
		wasfromfl = xfs_alloc_ag_vextent_exact(args);
		break;
	default:
		ASSERT(0);
		/* NOTREACHED */
	}
	/*
	 * If the allocation worked, need to change the agf structure
	 * (and log it), and the superblock.
	 */
	if (args->agbno != NULLAGBLOCK) {
		xfs_agf_t	*agf;	/* allocation group freelist header */
		int 		slen = (int)args->len;

		ASSERT(args->len >= args->minlen && args->len <= args->maxlen);
		ASSERT(!wasfromfl || !args->isfl);
		if (!wasfromfl) {
			agf = XFS_BUF_TO_AGF(args->agbp);
			agf->agf_freeblks -= args->len;
			xfs_trans_agblocks_delta(args->tp, -(args->len));
			args->pag->pagf_freeblks -= args->len;
			ASSERT(agf->agf_freeblks <= agf->agf_length);
			xfs_alloc_trace_modagf("xfs_alloc_ag_vextent", NULL,
				args->mp, agf, XFS_AGF_FREEBLKS);
			xfs_alloc_log_agf(args->tp, args->agbp,
				XFS_AGF_FREEBLKS);
		}
		if (!args->isfl)
			xfs_trans_mod_sb(args->tp,
				args->wasdel ? XFS_TRANS_SB_RES_FDBLOCKS :
					XFS_TRANS_SB_FDBLOCKS, -slen);
		XFSSTATS.xs_allocx++;
		XFSSTATS.xs_allocb += args->len;
	}
}

/*
 * Allocate a variable extent at exactly agno/bno.
 * Extent's length (returned in *len) will be between minlen and maxlen,
 * and of the form k * prod + mod unless there's nothing that large.
 * Return the starting a.g. block (bno), or NULLAGBLOCK if we can't do it.
 */
STATIC int
xfs_alloc_ag_vextent_exact(
	xfs_alloc_arg_t	*args)	/* allocation argument structure */
{
	xfs_btree_cur_t	*bno_cur;/* by block-number btree cursor */
	xfs_btree_cur_t	*cnt_cur;/* by count btree cursor */
	xfs_agblock_t	end;	/* end of allocated extent */
	xfs_agblock_t	fbno;	/* start block of found extent */
	xfs_agblock_t	fend;	/* end block of found extent */
	xfs_extlen_t	flen;	/* length of found extent */
#if defined(DEBUG) && !defined(SIM)
	static char	fname[] = "xfs_alloc_ag_vextent_exact";
#endif
	xfs_agblock_t	maxend;	/* end of maximal extent */
	xfs_agblock_t	minend;	/* end of minimal extent */
	xfs_extlen_t	rlen;	/* length of returned extent */

	/*
	 * Allocate/initialize a cursor for the by-number freespace btree.
	 */
	bno_cur = xfs_btree_init_cursor(args->mp, args->tp, args->agbp,
		args->agno, XFS_BTNUM_BNO, 0);
	/*
	 * Lookup bno and minlen in the btree (minlen is irrelevant, really).
	 * Look for the closest free block <= bno, it must contain bno
	 * if any free block does.
	 */
	if (!xfs_alloc_lookup_le(bno_cur, args->agbno, args->minlen)) {
		/*
		 * Didn't find it, return null.
		 */
		xfs_btree_del_cursor(bno_cur);
		args->agbno = NULLAGBLOCK;
		return 0;
	}
	/*
	 * Grab the freespace record.
	 */
	xfs_alloc_get_rec(bno_cur, &fbno, &flen);
	ASSERT(fbno <= args->agbno);
	minend = args->agbno + args->minlen;
	maxend = args->agbno + args->maxlen;
	fend = fbno + flen;
	/* 
	 * Give up if the freespace isn't long enough for the minimum request.
	 */
	if (fend < minend) {
		xfs_btree_del_cursor(bno_cur);
		args->agbno = NULLAGBLOCK;
		return 0;
	}
	/*
	 * End of extent will be smaller of the freespace end and the
	 * maximal requested end.
	 */
	end = XFS_AGBLOCK_MIN(fend, maxend);
	/*
	 * Fix the length according to mod and prod if given.
	 */
	args->len = end - args->agbno;
	xfs_alloc_fix_len(args);
	if (!xfs_alloc_fix_minleft(args)) {
		xfs_btree_del_cursor(bno_cur);
		return 0;
	}
	rlen = args->len;
	ASSERT(args->agbno + rlen <= fend);
	end = args->agbno + rlen;
	/*
	 * We are allocating agbno for rlen [agbno .. end)
	 * Allocate/initialize a cursor for the by-size btree.
	 */
	cnt_cur = xfs_btree_init_cursor(args->mp, args->tp, args->agbp,
		args->agno, XFS_BTNUM_CNT, 0);
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
	if (fbno < args->agbno) {
		xfs_alloc_lookup_eq(cnt_cur, fbno, args->agbno - fbno);
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
	if (fbno == args->agbno && fend == end)
		xfs_alloc_delete(bno_cur);
	/*
	 * If the found freespace starts at the same block but is longer,
	 * just update the by-bno btree entry to be shorter.
	 */
	else if (fbno == args->agbno)
		xfs_alloc_update(bno_cur, end, fend - end);
	else {
		/*
		 * If the found freespace starts left of the allocation,
		 * update the length of that by-bno entry.
		 */
		xfs_alloc_update(bno_cur, fbno, args->agbno - fbno);
		/*
		 * ... and if the found freespace ends right of the
		 * allocation, add another btree entry with the leftover space.
		 */
		if (fend > end) {
			xfs_alloc_lookup_eq(bno_cur, end, fend - end);
			xfs_alloc_insert(bno_cur);
		}
	}
	xfs_alloc_rcheck(bno_cur);
	xfs_alloc_kcheck(bno_cur);
	xfs_btree_del_cursor(bno_cur);
	args->len = rlen;
	ASSERT(args->agbno + args->len <=
	       XFS_BUF_TO_AGF(args->agbp)->agf_length);
	xfs_alloc_trace_alloc(fname, NULL, args);
	return 0;
}

/*
 * Allocate a variable extent near bno in the allocation group agno.
 * Extent's length (returned in len) will be between minlen and maxlen,
 * and of the form k * prod + mod unless there's nothing that large.
 * Return the starting a.g. block, or NULLAGBLOCK if we can't do it.
 */
STATIC int
xfs_alloc_ag_vextent_near(
	xfs_alloc_arg_t	*args)		/* allocation argument structure */
{
	xfs_btree_cur_t	*bno_cur_gt;	/* cursor for bno btree, right side */
	xfs_btree_cur_t	*bno_cur_lt;	/* cursor for bno btree, left side */
	xfs_btree_cur_t	*cnt_cur;	/* cursor for count btree */
#if defined(DEBUG) && !defined(SIM)
	static char	fname[] = "xfs_alloc_ag_vextent_near";
#endif
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
	xfs_extlen_t	rlen;		/* length of returned extent */

	/*
	 * Get a cursor for the by-size btree.
	 */
	cnt_cur = xfs_btree_init_cursor(args->mp, args->tp, args->agbp,
		args->agno, XFS_BTNUM_CNT, 0);
	ltlen = 0;
	/*
	 * See if there are any free extents as big as maxlen.
	 */
	if (!xfs_alloc_lookup_ge(cnt_cur, 0, args->maxlen)) {
		/*
		 * Nothing as big as maxlen.
		 * Is there anything at all in the tree?
		 * If so get the biggest extent.
		 */
		if (xfs_alloc_decrement(cnt_cur, 0))
			xfs_alloc_get_rec(cnt_cur, &ltbno, &ltlen);
		/*
		 * Nothing in the tree, try the freelist.  Make sure
		 * to respect minleft even when pulling from the
		 * freelist.
		 */
		else if (args->minlen == 1 && !args->isfl &&
			 (XFS_BUF_TO_AGF(args->agbp)->agf_flcount >
			  args->minleft) &&
			 (ltbno = xfs_alloc_get_freelist(args->tp,
				 args->agbp)) != NULLAGBLOCK) {
			if (args->userdata) {
				buf_t	*bp;

				bp = xfs_btree_read_bufs(args->mp, args->tp,
					args->agno, ltbno, 0);
				xfs_trans_binval(args->tp, bp);
				/*
				 * Since blocks move to the free list without
				 * the coordination used in xfs_bmap_finish,
				 * we can't allow the user to write to the
				 * block until we know that the transaction
				 * that moved it to the free list is
				 * permanently on disk.  The only way to
				 * ensure that is to make this transaction
				 * synchronous.
				 */
				xfs_trans_set_sync(args->tp);
			}
			xfs_btree_del_cursor(cnt_cur);
			args->len = 1;
			args->agbno = ltbno;
			ASSERT(args->agbno + args->len <=
			       XFS_BUF_TO_AGF(args->agbp)->agf_length);
			xfs_alloc_trace_alloc(fname, "freelist", args);
			return 1;
		}
		/*
		 * If nothing, or what we got is too small, give up.
		 */
		if (ltlen < args->minlen) {
			xfs_btree_del_cursor(cnt_cur);
			args->agbno = NULLAGBLOCK;
			return 0;
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
		 * all larger free blocks.  If we're actually pointing at a
		 * record smaller than maxlen, go to the start of this block,
		 * and skip all those smaller than minlen.
		 */
		if (ltlen) {
			ASSERT(ltlen >= args->minlen && ltlen < args->maxlen);
			cnt_cur->bc_ptrs[0] = 1;
			do {
				xfs_alloc_get_rec(cnt_cur, &ltbno, &ltlen);
				if (ltlen >= args->minlen)
					break;
			} while (xfs_alloc_increment(cnt_cur, 0));
			ASSERT(ltlen >= args->minlen);
		}
		i = cnt_cur->bc_ptrs[0];
		besti = 0;
		bdiff = ltdiff = (xfs_extlen_t)0;
		do {
			/*
			 * For each entry, decide if it's better than
			 * the previous best entry.
			 */
			xfs_alloc_get_rec(cnt_cur, &ltbno, &ltlen);
			args->len = XFS_EXTLEN_MIN(ltlen, args->maxlen);
			xfs_alloc_fix_len(args);
			rlen = args->len;
			ltdiff = xfs_alloc_compute_diff(args->agbno, rlen,
				ltbno, ltlen, &ltnew);
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
		args->len = XFS_EXTLEN_MIN(ltlen, args->maxlen);
		xfs_alloc_fix_len(args);
		if (!xfs_alloc_fix_minleft(args)) {
			xfs_btree_del_cursor(cnt_cur);
			return 0;
		}
		rlen = args->len;
		/*
		 * Delete that entry from the by-size tree.
		 */
		xfs_alloc_delete(cnt_cur);
		/*
		 * We are allocating starting at bnew for rlen blocks.
		 */
		ltnew = bnew;
		ltnewend = bnew + rlen;
		ASSERT(ltnew >= ltbno);
		ASSERT(ltnewend <= ltend);
		/*
		 * Set up a cursor for the by-bno tree.
		 */
		bno_cur_lt = xfs_btree_init_cursor(args->mp, args->tp,
			args->agbp, args->agno, XFS_BTNUM_BNO, 0);
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
		args->agbno = ltnew;
		ASSERT(args->agbno + args->len <=
		       XFS_BUF_TO_AGF(args->agbp)->agf_length);
		xfs_alloc_trace_alloc(fname, "first", args);
		return 0;
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
	bno_cur_lt = xfs_btree_init_cursor(args->mp, args->tp, args->agbp,
		args->agno, XFS_BTNUM_BNO, 0);
	/*
	 * Lookup <= bno to find the leftward search's starting point.
	 */
	if (!xfs_alloc_lookup_le(bno_cur_lt, args->agbno, args->maxlen)) {
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
			if (ltlen >= args->minlen)
				break;
			if (!xfs_alloc_decrement(bno_cur_lt, 0)) {
				xfs_btree_del_cursor(bno_cur_lt);
				bno_cur_lt = 0;
			}
		}
		if (bno_cur_gt) {
			xfs_alloc_get_rec(bno_cur_gt, &gtbno, &gtlen);
			if (gtlen >= args->minlen)
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
		if (ltlen >= args->minlen) {
			/*
			 * Fix up the length.
			 */
			args->len = XFS_EXTLEN_MIN(ltlen, args->maxlen);
			xfs_alloc_fix_len(args);
			rlen = args->len;
			ltdiff = xfs_alloc_compute_diff(args->agbno, rlen,
				ltbno, ltlen, &ltnew);
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
					if (gtbno >= args->agbno + ltdiff) {
						xfs_btree_del_cursor(
							bno_cur_gt);
						bno_cur_gt = 0;
						break;
					}
					/*
					 * If we reach a big enough entry,
					 * compare the two and pick the best.
					 */
					if (gtlen >= args->minlen) {
						args->len =
							XFS_EXTLEN_MIN(gtlen,
								args->maxlen);
						xfs_alloc_fix_len(args);
						rlen = args->len;
						gtdiff = xfs_alloc_compute_diff(
							args->agbno, rlen,
							gtbno, gtlen, &gtnew);
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
			args->len = XFS_EXTLEN_MIN(gtlen, args->maxlen);
			xfs_alloc_fix_len(args);
			rlen = args->len;
			gtdiff = xfs_alloc_compute_diff(args->agbno, rlen,
				gtbno, gtlen, &gtnew);
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
					if (ltbno <= args->agbno - gtdiff) {
						xfs_btree_del_cursor(
							bno_cur_lt);
						bno_cur_lt = 0;
						break;
					}
					/*
					 * If we reach a big enough entry,
					 * compare the two and pick the best.
					 */
					if (ltlen >= args->minlen) {
						args->len = XFS_EXTLEN_MIN(
							ltlen, args->maxlen);
						xfs_alloc_fix_len(args);
						rlen = args->len;
						ltdiff = xfs_alloc_compute_diff(
							args->agbno, rlen,
							ltbno, ltlen, &ltnew);
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
		args->len = XFS_EXTLEN_MIN(ltlen, args->maxlen);
		xfs_alloc_fix_len(args);
		if (!xfs_alloc_fix_minleft(args)) {
			xfs_btree_del_cursor(bno_cur_lt);
			xfs_btree_del_cursor(cnt_cur);
			return 0;
		}
		rlen = args->len;
		ltdiff = xfs_alloc_compute_diff(args->agbno, rlen, ltbno, ltlen,
			&ltnew);
		ltnewend = ltnew + rlen;
		ASSERT(ltnew >= ltbno);
		ASSERT(ltnewend <= ltend);
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
			xfs_alloc_lookup_eq(cnt_cur, ltnewend,
				ltend - ltnewend);
			xfs_alloc_insert(cnt_cur);
			xfs_alloc_lookup_eq(bno_cur_lt, ltnewend,
				ltend - ltnewend);
			xfs_alloc_insert(bno_cur_lt);
		}
		/*
		 * Freespace contains allocated space, matches at left side.
		 * Insert the right-side leftover in the by-size tree.
		 * Update the by-block record with the new length.
		 */
		else if (ltend > ltnewend) {
			xfs_alloc_lookup_eq(cnt_cur, ltnewend,
				ltend - ltnewend);
			xfs_alloc_insert(cnt_cur);
			xfs_alloc_update(bno_cur_lt, ltnewend,
				ltend - ltnewend);
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
		args->agbno = ltnew;
		ASSERT(args->agbno + args->len <=
		       XFS_BUF_TO_AGF(args->agbp)->agf_length);
		xfs_alloc_trace_alloc(fname, "lt", args);
	}
	/*
	 * On the right side.
	 */
	else {
		/*
		 * Fix up the length and compute the useful address.
		 */
		gtend = gtbno + gtlen;
		args->len = XFS_EXTLEN_MIN(gtlen, args->maxlen);
		xfs_alloc_fix_len(args);
		if (!xfs_alloc_fix_minleft(args)) {
			xfs_btree_del_cursor(bno_cur_gt);
			xfs_btree_del_cursor(cnt_cur);
			return 0;
		}
		rlen = args->len;
		gtdiff = xfs_alloc_compute_diff(args->agbno, rlen, gtbno, gtlen,
			&gtnew);
		gtnewend = gtnew + rlen;
		ASSERT(gtnew >= gtbno);
		ASSERT(gtnewend <= gtend);
		/*
		 * Find the equivalent by-size btree record and delete it.
		 */
		i = xfs_alloc_lookup_eq(cnt_cur, gtbno, gtlen);
		ASSERT(i == 1);
		xfs_alloc_delete(cnt_cur);
		/*
		 * Other cases can't occur since gtbno > agbno.
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
		args->agbno = gtnew;
		ASSERT(args->agbno + args->len <=
		       XFS_BUF_TO_AGF(args->agbp)->agf_length);
		xfs_alloc_trace_alloc(fname, "gt", args);
	}
	return 0;
}

/*
 * Allocate a variable extent anywhere in the allocation group agno.
 * Extent's length (returned in len) will be between minlen and maxlen,
 * and of the form k * prod + mod unless there's nothing that large.
 * Return the starting a.g. block, or NULLAGBLOCK if we can't do it.
 */
STATIC int
xfs_alloc_ag_vextent_size(
	xfs_alloc_arg_t	*args)	/* allocation argument structure */
{
	xfs_btree_cur_t	*bno_cur;	/* cursor for bno btree */
	xfs_btree_cur_t	*cnt_cur;	/* cursor for cnt btree */
	xfs_agblock_t	fbno;		/* start of found freespace */
	xfs_extlen_t	flen;		/* length of found freespace */
#if defined(DEBUG) && !defined(SIM)
	static char	fname[] = "xfs_alloc_ag_vextent_size";
#endif
	xfs_extlen_t	rlen;		/* length of returned extent */

	/*
	 * Allocate and initialize a cursor for the by-size btree.
	 */
	cnt_cur = xfs_btree_init_cursor(args->mp, args->tp, args->agbp,
		args->agno, XFS_BTNUM_CNT, 0);
	/*
	 * Look for an entry >= maxlen blocks.
	 * If none, then pick up the last entry in the tree unless the
	 * tree is empty.
	 */
	if (!xfs_alloc_lookup_ge(cnt_cur, 0, args->maxlen)) {
		if (xfs_alloc_decrement(cnt_cur, 0))
			xfs_alloc_get_rec(cnt_cur, &fbno, &flen);
		/*
		 * Nothing in the btree, try the freelist.  Make sure
		 * to respect minleft even when pulling from the
		 * freelist.
		 */
		else if (args->minlen == 1 && !args->isfl &&
			 (XFS_BUF_TO_AGF(args->agbp)->agf_flcount >
			  args->minleft) &&
			 (fbno = xfs_alloc_get_freelist(args->tp,
				 args->agbp)) != NULLAGBLOCK) {
			if (args->userdata) {
				buf_t	*bp;

				bp = xfs_btree_read_bufs(args->mp, args->tp,
					args->agno, fbno, 0);
				xfs_trans_binval(args->tp, bp);
				/*
				 * Since blocks move to the free list without
				 * the coordination used in xfs_bmap_finish,
				 * we can't allow the user to write to the
				 * block until we know that the transaction
				 * that moved it to the free list is
				 * permanently on disk.  The only way to
				 * ensure that is to make this transaction
				 * synchronous.
				 */
				xfs_trans_set_sync(args->tp);
			}
			xfs_btree_del_cursor(cnt_cur);
			args->len = 1;
			args->agbno = fbno;
			ASSERT(args->agbno + args->len <=
			       XFS_BUF_TO_AGF(args->agbp)->agf_length);
			xfs_alloc_trace_alloc(fname, "freelist", args);
			return 1;
		} else
			flen = 0;
		/*
		 * Nothing as big as minlen, give up.
		 */
		if (flen < args->minlen) {
			xfs_btree_del_cursor(cnt_cur);
			args->agbno = NULLAGBLOCK;
			return 0;
		}
		rlen = flen;
	}
	/*
	 * There's a freespace as big as maxlen, get it.
	 */
	else {
		xfs_alloc_get_rec(cnt_cur, &fbno, &flen);
		rlen = args->maxlen;
	}
	/*
	 * Fix up the length.
	 */
	args->len = rlen;
	xfs_alloc_fix_len(args);
	if (!xfs_alloc_fix_minleft(args)) {
		xfs_btree_del_cursor(cnt_cur);
		return 0;
	}
	rlen = args->len;
	ASSERT(rlen <= flen);
	/*
	 * Delete the entry from the by-size btree.
	 */
	xfs_alloc_delete(cnt_cur);
	/*
	 * Allocate and initialize a cursor for the by-block tree.
	 * Look up the found space in that tree.
	 */
	bno_cur = xfs_btree_init_cursor(args->mp, args->tp, args->agbp,
		args->agno, XFS_BTNUM_BNO, 0);
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
	xfs_alloc_rcheck(bno_cur);
	xfs_alloc_kcheck(bno_cur);
	xfs_btree_del_cursor(bno_cur);
	xfs_alloc_rcheck(cnt_cur);
	xfs_alloc_kcheck(cnt_cur);
	xfs_btree_del_cursor(cnt_cur);
	args->len = rlen;
	args->agbno = fbno;
	ASSERT(args->agbno + args->len <=
	       XFS_BUF_TO_AGF(args->agbp)->agf_length);
	xfs_alloc_trace_alloc(fname, "normal", args);
	return 0;
}

/*
 * Free the extent starting at agno/bno for length.
 */
STATIC int			/* return success/failure, will be void */
xfs_free_ag_extent(
	xfs_trans_t	*tp,	/* transaction pointer */
	buf_t		*agbp,	/* buffer for a.g. freelist header */
	xfs_agnumber_t	agno,	/* allocation group number */
	xfs_agblock_t	bno,	/* starting block number */
	xfs_extlen_t	len,	/* length of extent */
	int		isfl)	/* set if is freelist blocks - no sb acctg */
{
	xfs_btree_cur_t	*bno_cur;	/* cursor for by-block btree */
	xfs_btree_cur_t	*cnt_cur;	/* cursor for by-size btree */
#if defined(DEBUG) && !defined(SIM)
	static char	fname[] = "xfs_free_ag_extent";
#endif
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
	bno_cur = xfs_btree_init_cursor(mp, tp, agbp, agno, XFS_BTNUM_BNO, 0);
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
	cnt_cur = xfs_btree_init_cursor(mp, tp, agbp, agno, XFS_BTNUM_CNT, 0);
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
		xfs_perag_t	*pag;		/* per allocation group data */

		agf = XFS_BUF_TO_AGF(agbp);
		pag = &mp->m_perag[agno];
		agf->agf_freeblks += len;
		xfs_trans_agblocks_delta(tp, len);
		pag->pagf_freeblks += len;
		ASSERT(agf->agf_freeblks <= agf->agf_length);
		xfs_alloc_trace_modagf(fname, NULL, mp, agf, XFS_AGF_FREEBLKS);
		xfs_alloc_log_agf(tp, agbp, XFS_AGF_FREEBLKS);
		if (!isfl)
			xfs_trans_mod_sb(tp, XFS_TRANS_SB_FDBLOCKS, (int)len);
		XFSSTATS.xs_freex++;
		XFSSTATS.xs_freeb += len;
	}
	xfs_alloc_trace_free(fname, NULL, mp, agno, bno, len, isfl);
	return 1;
}

/* 
 * Visible (exported) allocation/free functions.
 * Some of these are used just by xfs_alloc_btree.c and this file.
 */

/*
 * Allocate an alloc_arg structure.
 */
xfs_alloc_arg_t *
xfs_alloc_arg_alloc(void)
{
	return kmem_zone_alloc(xfs_alloc_arg_zone, KM_SLEEP);
}

/*
 * Free an alloc_arg structure.
 */
void
xfs_alloc_arg_free(
	xfs_alloc_arg_t	*args)
{
	kmem_zone_free(xfs_alloc_arg_zone, args);
}

/*
 * Compute and fill in value of m_ag_maxlevels.
 */
void
xfs_alloc_compute_maxlevels(
	xfs_mount_t	*mp)	/* file system mount structure */
{
	int		level;
	uint		maxblocks;
	uint		maxleafents;
	int		minleafrecs;
	int		minnoderecs;

	maxleafents = (mp->m_sb.sb_agblocks + 1) / 2;
	minleafrecs = mp->m_alloc_mnr[0];
	minnoderecs = mp->m_alloc_mnr[1];
	maxblocks = (maxleafents + minleafrecs - 1) / minleafrecs;
	for (level = 1; maxblocks > 1; level++)
		maxblocks = (maxblocks + minnoderecs - 1) / minnoderecs;
	mp->m_ag_maxlevels = level;
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
	xfs_extlen_t	minleft,/* min blocks must be left afterwards */
	int		flags,	/* XFS_ALLOC_FLAG_... */
	xfs_perag_t	*pag)	/* per allocation group data */
{
	buf_t		*agbp;
	xfs_agf_t	*agf;
	buf_t		*agflbp;
	xfs_alloc_arg_t	*args;
	xfs_agblock_t	bno;
	xfs_extlen_t	longest;
	xfs_mount_t	*mp;
	xfs_extlen_t	need;

	mp = tp->t_mountp;
	if (!pag->pagf_init) {
		agbp = xfs_alloc_read_agf(mp, tp, agno, flags);
		if (!pag->pagf_init)
			return NULL;
	} else
		agbp = NULL;
	need = XFS_MIN_FREELIST_PAG(pag, mp);
	/*
	 * If it looks like there isn't a long enough extent, or enough
	 * total blocks, reject it.
	 */
	longest = (pag->pagf_longest > need) ?
		(pag->pagf_longest - need) :
		(pag->pagf_flcount > 0 || pag->pagf_longest > 0);
	if (minlen > longest ||
	    (minleft &&
	     (int)(pag->pagf_freeblks + pag->pagf_flcount - need - total) <
	     (int)minleft)) {
		if (agbp)
			xfs_trans_brelse(tp, agbp);
		return NULL;
	}
	/*
	 * Get the a.g. freespace buffer.
	 * Can fail if we're not blocking on locks, and it's held.
	 */
	if (agbp == NULL) {
		agbp = xfs_alloc_read_agf(mp, tp, agno, flags);
		if (agbp == NULL)
			return NULL;
	}
	/*
	 * Figure out how many blocks we should have in the freelist.
	 */
	agf = XFS_BUF_TO_AGF(agbp);
	ASSERT(agf->agf_magicnum == XFS_AGF_MAGIC);
	ASSERT(agf->agf_freeblks <= agf->agf_length);
	ASSERT(agf->agf_flfirst < XFS_AGFL_SIZE);
	ASSERT(agf->agf_fllast < XFS_AGFL_SIZE);
	ASSERT(agf->agf_flcount <= XFS_AGFL_SIZE);
	need = XFS_MIN_FREELIST(agf, mp);
	/*
	 * If there isn't enough total or single-extent, reject it.
	 */
	longest = (agf->agf_longest > need) ?
		(agf->agf_longest - need) :
		(agf->agf_flcount > 0 || agf->agf_longest > 0);
	if (minlen > longest ||
	    (minleft &&
	     (int)(agf->agf_freeblks + agf->agf_flcount - need - total) <
	     (int)minleft)) {
		xfs_trans_brelse(tp, agbp);
		return NULL;
	}
	/*
	 * Make the freelist shorter if it's too long.
	 */
	while (agf->agf_flcount > need) {
		buf_t	*bp;

		bno = xfs_alloc_get_freelist(tp, agbp);
		xfs_free_ag_extent(tp, agbp, agno, bno, 1, 1);
		bp = xfs_btree_read_bufs(mp, tp, agno, bno, 0);
		xfs_trans_binval(tp, bp);
		/*
		 * Since blocks move to the free list without
		 * the coordination used in xfs_bmap_finish,
		 * we can't allow block to be available for reallocation
		 * and non-transaction writing (user data)
		 * until we know that the transaction
		 * that moved it to the free list is
		 * permanently on disk.  The only way to
		 * ensure that is to make this transaction
		 * synchronous.
		 */
		xfs_trans_set_sync(tp);
	}
	/*
	 * Allocate and initialize the args structure.
	 */
	args = xfs_alloc_arg_alloc();
	args->tp = tp;
	args->mp = mp;
	args->agbp = agbp;
	args->agno = agno;
	args->mod = args->minleft = args->wasdel = args->userdata = 0;
	args->minlen = args->prod = args->isfl = 1;
	args->type = XFS_ALLOCTYPE_THIS_AG;
	args->pag = pag;
	agflbp = xfs_alloc_read_agfl(mp, tp, agno);
	/*
	 * Make the freelist longer if it's too short.
	 */
	while (agf->agf_flcount < need) {
		args->agbno = 0;
		args->maxlen = need - agf->agf_flcount;
		/*
		 * Allocate as many blocks as possible at once.
		 */
		xfs_alloc_ag_vextent(args);
		/*
		 * Stop if we run out.  Won't happen if callers are obeying
		 * the restrictions correctly.
		 */
		if (args->agbno == NULLAGBLOCK)
			break;
		/*
		 * Put each allocated block on the list.
		 */
		for (bno = args->agbno; bno < args->agbno + args->len; bno++)
			xfs_alloc_put_freelist(tp, agbp, agflbp, bno);
	}
	xfs_alloc_arg_free(args);
	return agbp;
}

/*
 * Get a block from the freelist.
 * Returns with the buffer for the block gotten.
 */
xfs_agblock_t			/* block address retrieved from freelist */
xfs_alloc_get_freelist(
	xfs_trans_t	*tp,	/* transaction pointer */
	buf_t		*agbp)	/* buffer containing the agf structure */
{
	xfs_agf_t	*agf;	/* a.g. freespace structure */
	xfs_agfl_t	*agfl;	/* a.g. freelist structure */
	buf_t		*agflbp;/* buffer for a.g. freelist structure */
	xfs_agblock_t	bno;	/* block number returned */
#if defined(DEBUG) && !defined(SIM)
	static char	fname[] = "xfs_alloc_get_freelist";
#endif
	xfs_perag_t	*pag;	/* per allocation group data */

	agf = XFS_BUF_TO_AGF(agbp);
	/*
	 * Freelist is empty, give up.
	 */
	if (agf->agf_flcount == 0)
		return NULLAGBLOCK;
	/*
	 * Read the array of free blocks.
	 */
	agflbp = xfs_alloc_read_agfl(tp->t_mountp, tp, agf->agf_seqno);
	agfl = XFS_BUF_TO_AGFL(agflbp);
	/*
	 * Get the block number and update the data structures.
	 */
	bno = agfl->agfl_bno[agf->agf_flfirst++];
	xfs_trans_brelse(tp, agflbp);
	if (agf->agf_flfirst == XFS_AGFL_SIZE)
		agf->agf_flfirst = 0;
	pag = &tp->t_mountp->m_perag[agf->agf_seqno];
	agf->agf_flcount--;
	xfs_trans_agflist_delta(tp, -1);
	pag->pagf_flcount--;
	xfs_alloc_trace_modagf(fname, NULL, tp->t_mountp, agf,
		XFS_AGF_FLFIRST | XFS_AGF_FLCOUNT);
	xfs_alloc_log_agf(tp, agbp, XFS_AGF_FLFIRST | XFS_AGF_FLCOUNT);
	kmem_check();
	return bno;
}

/*
 * Log the given fields from the agf structure.
 */
void
xfs_alloc_log_agf(
	xfs_trans_t	*tp,	/* transaction pointer */
	buf_t		*bp,	/* buffer for a.g. freelist header */
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
		offsetof(xfs_agf_t, agf_flfirst),
		offsetof(xfs_agf_t, agf_fllast),
		offsetof(xfs_agf_t, agf_flcount),
		offsetof(xfs_agf_t, agf_freeblks),
		offsetof(xfs_agf_t, agf_longest),
		sizeof(xfs_agf_t)
	};

	xfs_btree_offsets(fields, offsets, XFS_AGF_NUM_BITS, &first, &last);
	xfs_trans_log_buf(tp, bp, (uint)first, (uint)last);
	kmem_check();
}

/*
 * Interface for inode allocation to force the pag data to be initialized.
 */
void
xfs_alloc_pagf_init(
	xfs_mount_t		*mp,	/* file system mount structure */
	xfs_trans_t		*tp,	/* transaction pointer */
	xfs_agnumber_t		agno,	/* allocation group number */
	int			flags)	/* XFS_ALLOC_FLAGS_... */
{
	buf_t			*bp;

	bp = xfs_alloc_read_agf(mp, tp, agno, flags);
	if (bp)
		xfs_trans_brelse(tp, bp);
}

/*
 * Put the block on the freelist for the allocation group.
 */
void
xfs_alloc_put_freelist(
	xfs_trans_t		*tp,	/* transaction pointer */
	buf_t			*agbp,	/* buffer for a.g. freelist header */
	buf_t			*agflbp,/* buffer for a.g. free block array */
	xfs_agblock_t		bno)	/* block being freed */
{
	xfs_agf_t		*agf;	/* a.g. freespace structure */
	xfs_agfl_t		*agfl;	/* a.g. free block array */
	xfs_agblock_t		*blockp;/* pointer to array entry */
#if defined(DEBUG) && !defined(SIM)
	static char		fname[] = "xfs_alloc_put_freelist";
#endif
	xfs_perag_t		*pag;	/* per allocation group data */

	agf = XFS_BUF_TO_AGF(agbp);
	if (!agflbp)
		agflbp = xfs_alloc_read_agfl(tp->t_mountp, tp, agf->agf_seqno);
	agfl = XFS_BUF_TO_AGFL(agflbp);
	if (++agf->agf_fllast == XFS_AGFL_SIZE)
		agf->agf_fllast = 0;
	pag = &tp->t_mountp->m_perag[agf->agf_seqno];
	agf->agf_flcount++;
	xfs_trans_agflist_delta(tp, 1);
	pag->pagf_flcount++;
	ASSERT(agf->agf_flcount <= XFS_AGFL_SIZE);
	blockp = &agfl->agfl_bno[agf->agf_fllast];
	*blockp = bno;
	xfs_alloc_trace_modagf(fname, NULL, tp->t_mountp, agf,
		XFS_AGF_FLLAST | XFS_AGF_FLCOUNT);
	xfs_alloc_log_agf(tp, agbp, XFS_AGF_FLLAST | XFS_AGF_FLCOUNT);
	xfs_trans_log_buf(tp, agflbp, (caddr_t)blockp - (caddr_t)agfl,
		(caddr_t)blockp - (caddr_t)agfl + sizeof(*blockp) - 1);
	kmem_check();
}

/*
 * Allocate an extent (variable-size).
 * Depending on the allocation type, we either look in a single allocation
 * group or loop over the allocation groups to find the result.
 */
void
xfs_alloc_vextent(
	xfs_alloc_arg_t	*args)	/* allocation argument structure */
{
	xfs_agblock_t	agsize;	/* allocation group size */
	int		flags;	/* XFS_ALLOC_FLAG_... locking flags */
#if defined(DEBUG) && !defined(SIM)
	static char	fname[] = "xfs_alloc_vextent";
#endif
	xfs_mount_t	*mp;	/* mount structure pointer */
	xfs_agnumber_t	sagno;	/* starting allocation group number */
	xfs_alloctype_t	type;	/* input allocation type */

	mp = args->mp;
	type = args->otype = args->type;
	args->agbno = NULLAGBLOCK;
	/*
	 * Just fix this up, for the case where the last a.g. is shorter
	 * (or there's only one a.g.) and the caller couldn't easily figure
	 * that out (xfs_bmap_alloc).
	 */
	agsize = mp->m_sb.sb_agblocks;
	if (args->maxlen > agsize)
		args->maxlen = agsize;
	/* 
	 * These should really be asserts, left this way for now just
	 * for the benefit of xfs_test.
	 */
	if (XFS_FSB_TO_AGNO(mp, args->fsbno) >= mp->m_sb.sb_agcount ||
	    XFS_FSB_TO_AGBNO(mp, args->fsbno) >= agsize ||
	    args->minlen > args->maxlen || args->minlen > agsize ||
	    args->mod >= args->prod) {
		args->fsbno = NULLFSBLOCK;
		xfs_alloc_trace_alloc(fname, "badargs", args);
		return;
	}
	switch (type) {
	case XFS_ALLOCTYPE_THIS_AG:
	case XFS_ALLOCTYPE_NEAR_BNO:
	case XFS_ALLOCTYPE_THIS_BNO:
		/*
		 * These three force us into a single a.g.
		 */
		args->agno = XFS_FSB_TO_AGNO(mp, args->fsbno);
		args->pag = &mp->m_perag[args->agno];
		if (!(args->agbp = xfs_alloc_fix_freelist(args->tp, args->agno,
				args->minlen, args->total, 0, 0, args->pag)))
			break;
		args->agbno = XFS_FSB_TO_AGBNO(mp, args->fsbno);
		xfs_alloc_ag_vextent(args);
		break;
	case XFS_ALLOCTYPE_START_BNO:
		/*
		 * Try near allocation first, then anywhere-in-ag after
		 * the first a.g. fails.
		 */
		args->agbno = XFS_FSB_TO_AGBNO(mp, args->fsbno);
		args->type = XFS_ALLOCTYPE_NEAR_BNO;
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
			args->agno = sagno = mp->m_agfrotor;
			args->type = XFS_ALLOCTYPE_THIS_AG;
			flags = XFS_ALLOC_FLAG_TRYLOCK;
		} else if (type == XFS_ALLOCTYPE_FIRST_AG) {
			/*
			 * Start with allocation group given by bno.
			 */
			args->agno = XFS_FSB_TO_AGNO(mp, args->fsbno);
			args->type = XFS_ALLOCTYPE_THIS_AG;
			sagno = 0;
			flags = 0;
		} else {
			if (type == XFS_ALLOCTYPE_START_AG)
				args->type = XFS_ALLOCTYPE_THIS_AG;
			/*
			 * Start with the given allocation group.
			 */
			args->agno = sagno = XFS_FSB_TO_AGNO(mp, args->fsbno);
			flags = XFS_ALLOC_FLAG_TRYLOCK;
		}
		/*
		 * Loop over allocation groups twice; first time with
		 * trylock set, second time without.
		 */
		for (;;) {
			args->pag = &mp->m_perag[args->agno];
			args->agbp = xfs_alloc_fix_freelist(args->tp,
				args->agno, args->minlen, args->total,
				args->minleft, flags, args->pag);
			/*
			 * If we get a buffer back then the allocation will fly.
			 */
			if (args->agbp) {
				xfs_alloc_ag_vextent(args);
				ASSERT(args->agbno != NULLAGBLOCK);
				break;
			}
			xfs_alloc_trace_alloc(fname, "loopfailed", args);
			/*
			 * Didn't work, figure out the next iteration.
			 */
			if (args->agno == sagno &&
			    type == XFS_ALLOCTYPE_START_BNO)
				args->type = XFS_ALLOCTYPE_THIS_AG;
			if (++(args->agno) == mp->m_sb.sb_agcount)
				args->agno = 0;
			/* 
			 * Reached the starting a.g., must either be done
			 * or switch to non-trylock mode.
			 */
			if (args->agno == sagno) {
				if (flags == 0) {
					args->agbno = NULLAGBLOCK;
					xfs_alloc_trace_alloc(fname,
						"allfailed", args);
					break;
				}
				flags = 0;
				if (type == XFS_ALLOCTYPE_START_BNO) {
					args->agbno = XFS_FSB_TO_AGBNO(mp,
						args->fsbno);
					args->type = XFS_ALLOCTYPE_NEAR_BNO;
				}
			}
		}
		mp->m_agfrotor = (args->agno + 1) % mp->m_sb.sb_agcount;
		break;
	default:
		ASSERT(0);
		/* NOTREACHED */
	}
	if (args->agbno == NULLAGBLOCK)
		args->fsbno = NULLFSBLOCK;
	else {
		args->fsbno = XFS_AGB_TO_FSB(mp, args->agno, args->agbno);
		ASSERT(args->len >= args->minlen);
		ASSERT(args->len <= args->maxlen);
		XFS_AG_CHECK_DADDR(mp, XFS_FSB_TO_DADDR(mp, args->fsbno),
			args->len);
	}
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
	buf_t		*agbp;	/* buffer for a.g. freespace header */
#ifdef DEBUG
	xfs_agf_t	*agf;	/* a.g. freespace header */
#endif
	xfs_agnumber_t	agno;	/* allocation group number */
	xfs_perag_t	*pag;	/* per allocation group data */

	ASSERT(len != 0);
	agno = XFS_FSB_TO_AGNO(tp->t_mountp, bno);
	ASSERT(agno < tp->t_mountp->m_sb.sb_agcount);
	agbno = XFS_FSB_TO_AGBNO(tp->t_mountp, bno);
	pag = &tp->t_mountp->m_perag[agno];
	agbp = xfs_alloc_fix_freelist(tp, agno, 0, 0, 0, 0, pag);
#ifdef DEBUG
	ASSERT(agbp != NULL);
	agf = XFS_BUF_TO_AGF(agbp);
	ASSERT(agbno + len <= agf->agf_length);
#endif
	return xfs_free_ag_extent(tp, agbp, agno, agbno, len, 0);
}
