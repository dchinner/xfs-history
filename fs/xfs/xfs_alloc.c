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

STATIC xfs_agblock_t
xfs_alloc_compute_diff(
	xfs_agblock_t	wantbno,
	xfs_extlen_t	wantlen,
	xfs_agblock_t	freebno,
	xfs_extlen_t	freelen,
	xfs_agblock_t	*newbnop);

STATIC xfs_extlen_t
xfs_alloc_fix_len(
	xfs_extlen_t	mod,
	xfs_extlen_t	prod,
	xfs_extlen_t	len);

STATIC buf_t *
xfs_alloc_read_agf(
	xfs_mount_t	*mp,
	xfs_trans_t	*tp,
	xfs_agnumber_t	agno,
	int		flags);

/*
 * Prototypes for per-ag allocation routines
 */

STATIC xfs_agblock_t
xfs_alloc_ag_vextent(
	xfs_trans_t	*tp,
	buf_t		*agbuf,
	xfs_agnumber_t	agno,
	xfs_agblock_t	bno,
	xfs_extlen_t	minlen,
	xfs_extlen_t	maxlen,
	xfs_extlen_t	*len,
	xfs_alloctype_t	type,
	int		wasdel,
	xfs_extlen_t	mod,
	xfs_extlen_t	prod);

STATIC xfs_agblock_t
xfs_alloc_ag_vextent_exact(
	xfs_trans_t	*tp,
	buf_t 		*agbuf,
	xfs_agnumber_t	agno,
	xfs_agblock_t	bno,
	xfs_extlen_t	minlen,
	xfs_extlen_t	maxlen,
	xfs_extlen_t	*len,
	xfs_extlen_t	mod,
	xfs_extlen_t	prod);

STATIC xfs_agblock_t
xfs_alloc_ag_vextent_near(
	xfs_trans_t	*tp,
	buf_t		*agbuf,
	xfs_agnumber_t	agno,
	xfs_agblock_t	bno,
	xfs_extlen_t	minlen,
	xfs_extlen_t	maxlen,
	xfs_extlen_t	*len,
	xfs_extlen_t	mod,
	xfs_extlen_t	prod);

STATIC xfs_agblock_t
xfs_alloc_ag_vextent_size(
	xfs_trans_t	*tp,
	buf_t		*agbuf,
	xfs_agnumber_t	agno,
	xfs_extlen_t	minlen,
	xfs_extlen_t	maxlen,
	xfs_extlen_t	*len,
	xfs_extlen_t	mod,
	xfs_extlen_t	prod);

STATIC int
xfs_free_ag_extent(
	xfs_trans_t	*tp,
	buf_t		*agbuf,
	xfs_agnumber_t	agno,
	xfs_agblock_t	bno,
	xfs_extlen_t	len);

/*
 * Internal functions.
 */

/*
 * Compute best start block and diff for "near" allocations.
 * freelen >= wantlen already checked by caller.
 */
STATIC xfs_agblock_t
xfs_alloc_compute_diff(
	xfs_agblock_t	wantbno,
	xfs_extlen_t	wantlen,
	xfs_agblock_t	freebno,
	xfs_extlen_t	freelen,
	xfs_agblock_t	*newbnop)
{
	xfs_agblock_t	freeend;
	xfs_agblock_t	newbno;
	xfs_agblock_t	wantend;

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
 */
STATIC xfs_extlen_t
xfs_alloc_fix_len(
	xfs_extlen_t	mod,
	xfs_extlen_t	prod,
	xfs_extlen_t	len)
{
	xfs_extlen_t	k;

	if (prod <= 1 || len < mod || (mod == 0 && len < prod))
		return len;
	k = len % prod;
	if (k == mod)
		return len;
	if (k > mod)
		len -= k - mod;
	else
		len -= prod - (mod - k);
	return len;
}

/*
 * Read in the allocation group header (free/alloc section)
 */
STATIC buf_t *
xfs_alloc_read_agf(
	xfs_mount_t	*mp,
	xfs_trans_t	*tp,
	xfs_agnumber_t	agno,
	int		flags)
{
	daddr_t		d;		/* disk block address */
	xfs_sb_t	*sbp;		/* superblock structure */

	ASSERT(agno != NULLAGNUMBER);
	sbp = &mp->m_sb;
	d = xfs_ag_daddr(sbp, agno, XFS_AGF_DADDR);
	return xfs_trans_read_buf(tp, mp->m_dev, d, 1,
		(flags & XFS_ALLOC_FLAG_TRYLOCK) ? BUF_TRYLOCK : 0U);
}

/*
 * Allocation group level functions.
 */

STATIC xfs_agblock_t
xfs_alloc_ag_vextent_exact(
	xfs_trans_t	*tp,
	buf_t		*agbuf,
	xfs_agnumber_t	agno,
	xfs_agblock_t	bno,
	xfs_extlen_t	minlen,
	xfs_extlen_t	maxlen,
	xfs_extlen_t	*len,
	xfs_extlen_t	mod,
	xfs_extlen_t	prod)
{
	xfs_btree_cur_t	*bno_cur;
	xfs_btree_cur_t	*cnt_cur;
	xfs_agblock_t	end;
	xfs_agblock_t	fbno;
	xfs_agblock_t	fend;
	xfs_extlen_t	flen;
	int		i;
	xfs_extlen_t	k;
	xfs_agblock_t	maxend;
	xfs_agblock_t	minend;
	xfs_mount_t	*mp;
	xfs_extlen_t	rlen;

	mp = tp->t_mountp;
	bno_cur = xfs_btree_init_cursor(mp, tp, agbuf, agno, XFS_BTNUM_BNO, 0);
	if (!xfs_alloc_lookup_le(bno_cur, bno, minlen)) {
		xfs_btree_del_cursor(bno_cur);
		return NULLAGBLOCK;
	}
	xfs_alloc_get_rec(bno_cur, &fbno, &flen);
	ASSERT(fbno <= bno);
	minend = bno + minlen;
	maxend = bno + maxlen;
	fend = fbno + flen;
	if (fend < minend) {
		xfs_btree_del_cursor(bno_cur);
		return NULLAGBLOCK;
	}
	end = fend < maxend ? fend : maxend;
	rlen = xfs_alloc_fix_len(mod, prod, end - bno);
	end = bno + rlen;
	if (end < minend) {
		xfs_btree_del_cursor(bno_cur);
		return NULLAGBLOCK;
	}
	cnt_cur = xfs_btree_init_cursor(mp, tp, agbuf, agno, XFS_BTNUM_CNT, 0);
	i = xfs_alloc_lookup_eq(cnt_cur, fbno, flen);
	ASSERT(i == 1);
	xfs_alloc_delete(cnt_cur);
	if (fbno < bno) {
		xfs_alloc_lookup_eq(cnt_cur, fbno, bno - fbno);
		xfs_alloc_insert(cnt_cur);
	}
	if (fend > end) {
		xfs_alloc_lookup_eq(cnt_cur, end, fend - end);
		xfs_alloc_insert(cnt_cur);
	}
	xfs_alloc_rcheck(cnt_cur);
	xfs_alloc_kcheck(cnt_cur);
	xfs_btree_del_cursor(cnt_cur);
	if (fbno == bno) {
		if (fend == end)
			xfs_alloc_delete(bno_cur);
		else
			xfs_alloc_update(bno_cur, end, fend - end);
	} else {
		xfs_alloc_update(bno_cur, fbno, bno - fbno);
		if (fend > end) {
			xfs_alloc_lookup_eq(bno_cur, end, fend - end);
			xfs_alloc_insert(bno_cur);
		}
		fbno = bno;
	}
	*len = end - bno;
	xfs_alloc_rcheck(bno_cur);
	xfs_alloc_kcheck(bno_cur);
	xfs_btree_del_cursor(bno_cur);
	return fbno;
}

STATIC xfs_agblock_t
xfs_alloc_ag_vextent_size(
	xfs_trans_t	*tp,
	buf_t		*agbuf,
	xfs_agnumber_t	agno,
	xfs_extlen_t	minlen,
	xfs_extlen_t	maxlen,
	xfs_extlen_t	*len,
	xfs_extlen_t	mod,
	xfs_extlen_t	prod)
{
	xfs_btree_cur_t	*bno_cur;
	xfs_btree_cur_t	*cnt_cur;
	xfs_agblock_t	fbno;
	xfs_extlen_t	flen = 0;
	int		i;
	xfs_extlen_t	k;
	xfs_mount_t	*mp;
	xfs_extlen_t	rlen;

	mp = tp->t_mountp;
	cnt_cur = xfs_btree_init_cursor(mp, tp, agbuf, agno, XFS_BTNUM_CNT, 0);
	if (!xfs_alloc_lookup_ge(cnt_cur, 0, maxlen)) {
		if (xfs_alloc_decrement(cnt_cur, 0))
			xfs_alloc_get_rec(cnt_cur, &fbno, &flen);
		if (flen < minlen) {
			xfs_btree_del_cursor(cnt_cur);
			return NULLAGBLOCK;
		}
		rlen = flen;
	} else {
		xfs_alloc_get_rec(cnt_cur, &fbno, &flen);
		rlen = maxlen;
	}
	rlen = xfs_alloc_fix_len(mod, prod, rlen);
	if (rlen < minlen) {
		xfs_btree_del_cursor(cnt_cur);
		return NULLAGBLOCK;
	}
	xfs_alloc_delete(cnt_cur);
	bno_cur = xfs_btree_init_cursor(mp, tp, agbuf, agno, XFS_BTNUM_BNO, 0);
	i = xfs_alloc_lookup_eq(bno_cur, fbno, flen);
	ASSERT(i == 1);
	if (rlen < flen) {
		xfs_alloc_lookup_eq(cnt_cur, fbno + rlen, flen - rlen);
		xfs_alloc_insert(cnt_cur);
		xfs_alloc_update(bno_cur, fbno + rlen, flen - rlen);
	} else
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

STATIC xfs_agblock_t
xfs_alloc_ag_vextent_near(
	xfs_trans_t	*tp,
	buf_t		*agbuf,
	xfs_agnumber_t	agno,
	xfs_agblock_t	bno,
	xfs_extlen_t	minlen,
	xfs_extlen_t	maxlen,
	xfs_extlen_t	*len,
	xfs_extlen_t	mod,
	xfs_extlen_t	prod)
{
	xfs_extlen_t	bdiff;
	int		besti;
	xfs_agblock_t	bnew;
	xfs_btree_cur_t	*bno_cur_gt;
	xfs_btree_cur_t	*bno_cur_lt;
	xfs_btree_cur_t	*cnt_cur;
	xfs_agblock_t	gtbno;
	xfs_extlen_t	gtdiff;
	xfs_agblock_t	gtend;
	xfs_extlen_t	gtlen;
	xfs_agblock_t	gtnew;
	xfs_agblock_t	gtnewend;
	int		i;
	xfs_extlen_t	k;
	xfs_agblock_t	ltbno;
	xfs_extlen_t	ltdiff;
	xfs_agblock_t	ltend;
	xfs_extlen_t	ltlen;
	xfs_agblock_t	ltnew;
	xfs_agblock_t	ltnewend;
	xfs_mount_t	*mp;
	xfs_extlen_t	rlen;

	mp = tp->t_mountp;
	cnt_cur = xfs_btree_init_cursor(mp, tp, agbuf, agno, XFS_BTNUM_CNT, 0);
	ltlen = 0;
	if (!xfs_alloc_lookup_ge(cnt_cur, 0, maxlen)) {
		if (xfs_alloc_decrement(cnt_cur, 0))
			xfs_alloc_get_rec(cnt_cur, &ltbno, &ltlen);
		if (ltlen < minlen) {
			xfs_btree_del_cursor(cnt_cur);
			return NULLAGBLOCK;
		}
	}
	if (xfs_btree_islastblock(cnt_cur, 0)) {
		i = cnt_cur->bc_ptrs[0];
		besti = 0;
		bdiff = ltdiff = (xfs_extlen_t)0;
		do {
			xfs_alloc_get_rec(cnt_cur, &ltbno, &ltlen);
			rlen = ltlen < maxlen ? ltlen : maxlen;
			rlen = xfs_alloc_fix_len(mod, prod, rlen);
			if (rlen < minlen)
				ltdiff = 1;
			else {
				ltdiff = xfs_alloc_compute_diff(bno, rlen,
					ltbno, ltlen, &ltnew);
				if (!besti || ltdiff < bdiff) {
					bdiff = ltdiff;
					bnew = ltnew;
					besti = i;
				}
			}
			i++;
		} while (ltdiff > 0 && xfs_alloc_increment(cnt_cur, 0));
		cnt_cur->bc_ptrs[0] = besti;
		xfs_alloc_get_rec(cnt_cur, &ltbno, &ltlen);
		ltend = ltbno + ltlen;
		rlen = ltlen < maxlen ? ltlen : maxlen;
		rlen = xfs_alloc_fix_len(mod, prod, rlen);
		xfs_alloc_delete(cnt_cur);
		ltnew = bnew;
		ltnewend = bnew + rlen;
		*len = rlen;
		bno_cur_lt = xfs_btree_init_cursor(mp, tp, agbuf, agno,
			XFS_BTNUM_BNO, 0);
		i = xfs_alloc_lookup_eq(bno_cur_lt, ltbno, ltlen);
		ASSERT(i == 1);
		/*
		 * Freespace properly contains allocated space.
		 */
		if (ltbno < ltnew && ltend > ltnewend) {
			xfs_alloc_lookup_eq(cnt_cur, ltbno, ltnew - ltbno);
			xfs_alloc_insert(cnt_cur);
			xfs_alloc_update(bno_cur_lt, ltbno, ltnew - ltbno);
			xfs_alloc_lookup_eq(cnt_cur, ltnewend, ltend - ltnewend);
			xfs_alloc_insert(cnt_cur);
			xfs_alloc_lookup_eq(bno_cur_lt, ltnewend, ltend - ltnewend);
			xfs_alloc_insert(bno_cur_lt);
		/*
		 * Freespace contains allocated space, matches at left side.
		 */
		} else if (ltend > ltnewend) {
			xfs_alloc_lookup_eq(cnt_cur, ltnewend, ltend - ltnewend);
			xfs_alloc_insert(cnt_cur);
			xfs_alloc_update(bno_cur_lt, ltnewend, ltend - ltnewend);
		/*
		 * Freespace contains allocated space, matches at right side.
		 */
		} else if (ltbno < ltnew) {
			xfs_alloc_lookup_eq(cnt_cur, ltbno, ltnew - ltbno);
			xfs_alloc_insert(cnt_cur);
			xfs_alloc_update(bno_cur_lt, ltbno, ltnew - ltbno);
		/*
		 * Freespace same size as allocated.
		 */
		} else
			xfs_alloc_delete(bno_cur_lt);
		xfs_alloc_rcheck(cnt_cur);
		xfs_alloc_kcheck(cnt_cur);
		xfs_btree_del_cursor(cnt_cur);
		xfs_alloc_rcheck(bno_cur_lt);
		xfs_alloc_kcheck(bno_cur_lt);
		xfs_btree_del_cursor(bno_cur_lt);
		return ltnew;
	}
	bno_cur_lt = xfs_btree_init_cursor(mp, tp, agbuf, agno,
		XFS_BTNUM_BNO, 0);
	if (!xfs_alloc_lookup_le(bno_cur_lt, bno, maxlen)) {
		bno_cur_gt = bno_cur_lt;
		bno_cur_lt = 0;
	} else
		bno_cur_gt = xfs_btree_dup_cursor(bno_cur_lt);
	if (!xfs_alloc_increment(bno_cur_gt, 0)) {
		xfs_btree_del_cursor(bno_cur_gt);
		bno_cur_gt = 0;
	}
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
	if (!bno_cur_lt && !bno_cur_gt)
		return NULLAGBLOCK;
	/*
	 * Got both cursors still active, need to find better entry.
	 */
	if (bno_cur_lt && bno_cur_gt) {
		/*
		 * Left side is long enough, look for a right side entry.
		 */
		if (ltlen >= minlen) {
			rlen = ltlen < maxlen ? ltlen : maxlen;
			rlen = xfs_alloc_fix_len(mod, prod, rlen);
			ltdiff = xfs_alloc_compute_diff(bno, rlen, ltbno, ltlen, &ltnew);
			/*
			 * Not perfect.
			 */
			if (ltdiff) {
				/*
				 * Look until we find a better one, run out of
				 * space, or run off the end.
				 */
				while (bno_cur_lt && bno_cur_gt) {
					xfs_alloc_get_rec(bno_cur_gt, &gtbno, &gtlen);
					if (gtbno >= bno + ltdiff) {
						xfs_btree_del_cursor(bno_cur_gt);
						bno_cur_gt = 0;
					} else if (gtlen >= minlen) {
						rlen = gtlen < maxlen ? gtlen : maxlen;
						rlen = xfs_alloc_fix_len(mod, prod, rlen);
						gtdiff = xfs_alloc_compute_diff(bno, rlen, gtbno, gtlen, &gtnew);
						if (gtdiff < ltdiff) {
							xfs_btree_del_cursor(bno_cur_lt);
							bno_cur_lt = 0;
						}
					} else if (!xfs_alloc_increment(bno_cur_gt, 0)) {
						xfs_btree_del_cursor(bno_cur_gt);
						bno_cur_gt = 0;
					}
				}
			} else {
				xfs_btree_del_cursor(bno_cur_gt);
				bno_cur_gt = 0;
			}
		} else {
			rlen = gtlen < maxlen ? gtlen : maxlen;
			rlen = xfs_alloc_fix_len(mod, prod, rlen);
			gtdiff = xfs_alloc_compute_diff(bno, rlen, gtbno, gtlen, &gtnew);
			if (gtdiff) {
				while (bno_cur_lt && bno_cur_gt) {
					xfs_alloc_get_rec(bno_cur_lt, &ltbno, &ltlen);
					if (ltbno <= bno - gtdiff) {
						xfs_btree_del_cursor(bno_cur_lt);
						bno_cur_lt = 0;
					} else if (ltlen >= minlen) {
						rlen = ltlen < maxlen ? ltlen : maxlen;
						rlen = xfs_alloc_fix_len(mod, prod, rlen);
						ltdiff = xfs_alloc_compute_diff(bno, rlen, ltbno, ltlen, &ltnew);
						if (ltdiff < gtdiff) {
							xfs_btree_del_cursor(bno_cur_gt);
							bno_cur_gt = 0;
						}
					} else if (!xfs_alloc_decrement(bno_cur_lt, 0)) {
						xfs_btree_del_cursor(bno_cur_lt);
						bno_cur_lt = 0;
					}
				}
			} else {
				xfs_btree_del_cursor(bno_cur_lt);
				bno_cur_lt = 0;
			}
		}
	}
	if (bno_cur_lt) {
		ltend = ltbno + ltlen;
		rlen = ltlen < maxlen ? ltlen : maxlen;
		rlen = xfs_alloc_fix_len(mod, prod, rlen);
		ltdiff = xfs_alloc_compute_diff(bno, rlen, ltbno, ltlen, &ltnew);
		ltnewend = ltnew + rlen;
		*len = rlen;
		i = xfs_alloc_lookup_eq(cnt_cur, ltbno, ltlen);
		ASSERT(i == 1);
		xfs_alloc_delete(cnt_cur);
		/*
		 * Freespace properly contains allocated space.
		 */
		if (ltbno < ltnew && ltend > ltnewend) {
			xfs_alloc_lookup_eq(cnt_cur, ltbno, ltnew - ltbno);
			xfs_alloc_insert(cnt_cur);
			xfs_alloc_update(bno_cur_lt, ltbno, ltnew - ltbno);
			xfs_alloc_lookup_eq(cnt_cur, ltnewend, ltend - ltnewend);
			xfs_alloc_insert(cnt_cur);
			xfs_alloc_lookup_eq(bno_cur_lt, ltnewend, ltend - ltnewend);
			xfs_alloc_insert(bno_cur_lt);
		/*
		 * Freespace contains allocated space, matches at left side.
		 */
		} else if (ltend > ltnewend) {
			xfs_alloc_lookup_eq(cnt_cur, ltnewend, ltend - ltnewend);
			xfs_alloc_insert(cnt_cur);
			xfs_alloc_update(bno_cur_lt, ltnewend, ltend - ltnewend);
		/*
		 * Freespace contains allocated space, matches at right side.
		 */
		} else if (ltbno < ltnew) {
			xfs_alloc_lookup_eq(cnt_cur, ltbno, ltnew - ltbno);
			xfs_alloc_insert(cnt_cur);
			xfs_alloc_update(bno_cur_lt, ltbno, ltnew - ltbno);
		/*
		 * Freespace same size as allocated.
		 */
		} else
			xfs_alloc_delete(bno_cur_lt);
		xfs_alloc_rcheck(cnt_cur);
		xfs_alloc_kcheck(cnt_cur);
		xfs_btree_del_cursor(cnt_cur);
		xfs_alloc_rcheck(bno_cur_lt);
		xfs_alloc_kcheck(bno_cur_lt);
		xfs_btree_del_cursor(bno_cur_lt);
		return ltnew;
	} else {
		gtend = gtbno + gtlen;
		rlen = gtlen < maxlen ? gtlen : maxlen;
		rlen = xfs_alloc_fix_len(mod, prod, rlen);
		gtdiff = xfs_alloc_compute_diff(bno, rlen, gtbno, gtlen, &gtnew);
		gtnewend = gtnew + rlen;
		*len = rlen;
		i = xfs_alloc_lookup_eq(cnt_cur, gtbno, gtlen);
		ASSERT(i == 1);
		xfs_alloc_delete(cnt_cur);
		/*
		 * Other cases can't occur since gtbno > bno.
		 */
		/*
		 * Freespace contains allocated space, matches at left side.
		 */
		if (gtend > gtnewend) {
			xfs_alloc_lookup_eq(cnt_cur, gtnewend, gtend - gtnewend);
			xfs_alloc_insert(cnt_cur);
			xfs_alloc_update(bno_cur_gt, gtnewend, gtend - gtnewend);
		/*
		 * Freespace same size as allocated.
		 */
		} else
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
 * Generic extent allocation in an allocation group, variable-size.
 */
STATIC xfs_agblock_t
xfs_alloc_ag_vextent(
	xfs_trans_t	*tp,
	buf_t		*agbuf,
	xfs_agnumber_t	agno,
	xfs_agblock_t	bno,
	xfs_extlen_t	minlen,
	xfs_extlen_t	maxlen,
	xfs_extlen_t	*len,
	xfs_alloctype_t	type,
	int		wasdel,
	xfs_extlen_t	mod,
	xfs_extlen_t	prod)
{
	xfs_agf_t	*agf;
	xfs_agblock_t	r;

	ASSERT(minlen > 0 && maxlen > 0 && minlen <= maxlen);
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
 * Free an extent in an ag.
 */
STATIC int
xfs_free_ag_extent(
	xfs_trans_t	*tp,
	buf_t		*agbuf,
	xfs_agnumber_t	agno,
	xfs_agblock_t	bno,
	xfs_extlen_t	len)
{
	xfs_agf_t	*agf;
	xfs_btree_cur_t	*bno_cur;
	xfs_btree_cur_t	*cnt_cur;
	xfs_extlen_t	flen = len;
	xfs_agblock_t	gtbno;
	xfs_extlen_t	gtlen;
	int		haveleft;
	int		haveright;
	int		i;
	xfs_agblock_t	ltbno;
	xfs_extlen_t	ltlen;
	xfs_mount_t	*mp;
	xfs_agblock_t	nbno;
	xfs_extlen_t	nlen;

	mp = tp->t_mountp;
	bno_cur = xfs_btree_init_cursor(mp, tp, agbuf, agno, XFS_BTNUM_BNO, 0);
	if (haveleft = xfs_alloc_lookup_le(bno_cur, bno, len)) {
		xfs_alloc_get_rec(bno_cur, &ltbno, &ltlen);
		if (ltbno + ltlen < bno)
			haveleft = 0;
		else if (ltbno + ltlen > bno) {
			xfs_btree_del_cursor(bno_cur);
			return 0;
		}
	}
	if (haveright = xfs_alloc_increment(bno_cur, 0)) {
		xfs_alloc_get_rec(bno_cur, &gtbno, &gtlen);
		if (bno + len < gtbno)
			haveright = 0;
		else if (gtbno < bno + len) {
			xfs_btree_del_cursor(bno_cur);
			return 0;
		}
	}
	cnt_cur = xfs_btree_init_cursor(mp, tp, agbuf, agno, XFS_BTNUM_CNT, 0);
	if (haveleft && haveright) {
		i = xfs_alloc_lookup_eq(cnt_cur, ltbno, ltlen);
		ASSERT(i == 1);
		xfs_alloc_delete(cnt_cur);
		i = xfs_alloc_lookup_eq(cnt_cur, gtbno, gtlen);
		ASSERT(i == 1);
		xfs_alloc_delete(cnt_cur);
		xfs_alloc_delete(bno_cur);
		xfs_alloc_decrement(bno_cur, 0);
#ifdef XFSDEBUG
		{ xfs_agblock_t xxbno; xfs_extlen_t xxlen;
		  xfs_alloc_get_rec(bno_cur, &xxbno, &xxlen);
		  ASSERT(xxbno == ltbno);
		  ASSERT(xxlen == ltlen);
		}
#endif
		nbno = ltbno;
		nlen = len + ltlen + gtlen;
		xfs_alloc_update(bno_cur, nbno, nlen);
	} else if (haveleft) {
		i = xfs_alloc_lookup_eq(cnt_cur, ltbno, ltlen);
		ASSERT(i == 1);
		xfs_alloc_delete(cnt_cur);
		xfs_alloc_decrement(bno_cur, 0);
		nbno = ltbno;
		nlen = len + ltlen;
		xfs_alloc_update(bno_cur, nbno, nlen);
	} else if (haveright) {
		i = xfs_alloc_lookup_eq(cnt_cur, gtbno, gtlen);
		ASSERT(i == 1);
		xfs_alloc_delete(cnt_cur);
		nbno = bno;
		nlen = len + gtlen;
		xfs_alloc_update(bno_cur, nbno, nlen);
	} else {
		nbno = bno;
		nlen = len;
		xfs_alloc_insert(bno_cur);
	}
	xfs_alloc_rcheck(bno_cur);
	xfs_alloc_kcheck(bno_cur);
	xfs_btree_del_cursor(bno_cur);
	xfs_alloc_lookup_eq(cnt_cur, nbno, nlen);
	xfs_alloc_insert(cnt_cur);
	xfs_alloc_rcheck(cnt_cur);
	xfs_alloc_kcheck(cnt_cur);
	agf = xfs_buf_to_agf(agbuf);
	agf->agf_freeblks += flen;
	xfs_alloc_log_agf(tp, agbuf, XFS_AGF_FREEBLKS);
	xfs_btree_del_cursor(cnt_cur);
	xfs_trans_mod_sb(tp, XFS_TRANS_SB_FDBLOCKS, (int)flen);
	return 1;
}

/* 
 * Visible (exported) allocation/free functions.
 * Some of these are used just by xfs_alloc_btree.c and this file.
 */

/*
 * Return the number of free blocks left in the allocation group.
 */
xfs_extlen_t			/* number of remaining free blocks */
xfs_alloc_ag_freeblks(
	xfs_mount_t	*mp,	/* file system mount structure */
	xfs_trans_t	*tp,	/* transaction pointer */
	xfs_agnumber_t	agno,	/* allocation group number */
	int		flags)	/* XFS_ALLOC_FLAG_... */
{
	buf_t		*agbuf;
	xfs_agf_t	*agf;
	xfs_extlen_t	freeblks;

	agbuf = xfs_alloc_read_agf(mp, tp, agno, flags);
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
	xfs_extlen_t	rlen;
	xfs_fsblock_t	rval;

	rval = xfs_alloc_vextent(tp, bno, len, len, &rlen, type, total, wasdel,
		0, 1);
	if (rval != NULLFSBLOCK)
		ASSERT(rlen == len);
	return rval;
}

/*
 * Fix up the btree freelist's size.
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
	xfs_agblock_t agbno;
	buf_t *agbuf;
	xfs_agf_t *agf;
	xfs_agblock_t bno;
	buf_t *buf;
	xfs_extlen_t i;
	xfs_mount_t *mp;
	xfs_extlen_t need;

	mp = tp->t_mountp;
	agbuf = xfs_alloc_read_agf(mp, tp, agno, flags);
	if (!agbuf)
		return agbuf;
	agf = xfs_buf_to_agf(agbuf);
	need = XFS_MIN_FREELIST(agf);
	if (total && agf->agf_freecount < need)
		total += need - agf->agf_freecount;
	if (minlen > agf->agf_longest || total > agf->agf_freeblks) {
		xfs_trans_brelse(tp, agbuf);
		return NULL;
	}
	while (agf->agf_freecount > need) {
		bno = xfs_alloc_get_freelist(tp, agbuf, &buf);
		xfs_free_ag_extent(tp, agbuf, agno, bno, 1);
	}
	while (agf->agf_freecount < need) {
		i = need - agf->agf_freecount;
		agbno = xfs_alloc_ag_vextent(tp, agbuf, agno, 0, 1, i, &i,
			XFS_ALLOCTYPE_THIS_AG, 0, 0, 1);
		if (agbno == NULLAGBLOCK)
			break;
		for (bno = agbno + i - 1; bno >= agbno; bno--) {
			buf = xfs_btree_get_bufs(mp, tp, agno, bno, 0);
			xfs_alloc_put_freelist(tp, agbuf, buf);
		}
	}
	return agbuf;
}

/*
 * Get a block from the freelist.
 * Returns with the buffer gotten.
 */
xfs_agblock_t
xfs_alloc_get_freelist(xfs_trans_t *tp, buf_t *agbuf, buf_t **bufp)
{
	xfs_agf_t *agf;
	xfs_agblock_t bno;
	xfs_btree_sblock_t *block;
	buf_t *buf;
	xfs_mount_t *mp;

	mp = tp->t_mountp;
	agf = xfs_buf_to_agf(agbuf);
	bno = agf->agf_freelist;
	if (bno == NULLAGBLOCK)
		return NULLAGBLOCK;
	buf = xfs_btree_read_bufs(mp, tp, agf->agf_seqno, bno, 0);
	block = xfs_buf_to_sblock(buf);
	agf->agf_freecount--;
	agf->agf_freelist = *(xfs_agblock_t *)block;
	xfs_alloc_log_agf(tp, agbuf, XFS_AGF_FREELIST | XFS_AGF_FREECOUNT);
	*bufp = buf;
	kmem_check();
	return bno;
}

void
xfs_alloc_log_agf(xfs_trans_t *tp, buf_t *buf, int fields)
{
	int first;
	int last;
	static const int offsets[] = {
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
 * Find the next freelist block number.
 */
xfs_agblock_t			/* a.g.-relative block number for btree list */
xfs_alloc_next_free(
	xfs_mount_t	*mp,	/* file system mount structure */
	xfs_trans_t	*tp,	/* transaction pointer */
	buf_t		*agbuf,	/* buffer for a.g. freelist header */
	xfs_agblock_t	bno)	/* current freelist block number */
{
	xfs_agf_t *agf;
	buf_t *buf;

	agf = xfs_buf_to_agf(agbuf);
	buf = xfs_btree_read_bufs(mp, tp, agf->agf_seqno, bno, 0);
	bno = *(xfs_agblock_t *)buf->b_un.b_addr;
	xfs_trans_brelse(tp, buf);
	return bno;
}

/*
 * Put the buffer on the freelist for the ag.
 */
void
xfs_alloc_put_freelist(xfs_trans_t *tp, buf_t *agbuf, buf_t *buf)
{
	xfs_agf_t *agf;
	xfs_btree_sblock_t *block;
	xfs_agblock_t bno;
	xfs_mount_t *mp;
	xfs_sb_t *sbp;

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
	xfs_agblock_t	agbno;
	buf_t		*agbuf;
	xfs_agnumber_t	agno;
	xfs_agblock_t	agsize;
	int		flags;
	xfs_mount_t	*mp;
	xfs_alloctype_t	ntype;
	xfs_sb_t	*sbp;
	xfs_agnumber_t	tagno;

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
	if (maxlen > agsize)
		maxlen = agsize;
	agbno = NULLAGBLOCK;
	ntype = XFS_ALLOCTYPE_THIS_AG;
	switch (type) {
	case XFS_ALLOCTYPE_THIS_AG:
	case XFS_ALLOCTYPE_NEAR_BNO:
	case XFS_ALLOCTYPE_THIS_BNO:
		agno = xfs_fsb_to_agno(sbp, bno);
		agbuf = xfs_alloc_fix_freelist(tp, agno, minlen, total, 1);
		if (!agbuf)
			break;
		agbno = xfs_fsb_to_agbno(sbp, bno);
		agbno = xfs_alloc_ag_vextent(tp, agbuf, agno, agbno, minlen,
			maxlen, len, type, wasdel, mod, prod); 
		break;
	case XFS_ALLOCTYPE_START_BNO:
		agbno = xfs_fsb_to_agbno(sbp, bno);
		ntype = XFS_ALLOCTYPE_NEAR_BNO;
		/* FALLTHROUGH */
	case XFS_ALLOCTYPE_ANY_AG:
	case XFS_ALLOCTYPE_START_AG:
		flags = XFS_ALLOC_FLAG_TRYLOCK;
		if (type == XFS_ALLOCTYPE_ANY_AG)
			tagno = agno = mp->m_agrotor;
		else
			tagno = agno = xfs_fsb_to_agno(sbp, bno);
		for (;;) {
			agbuf = xfs_alloc_fix_freelist(tp, tagno, minlen,
				total, flags);
			if (agbuf) {
				agbno = xfs_alloc_ag_vextent(tp, agbuf, tagno,
					agbno, minlen, maxlen, len, ntype,
					wasdel, mod, prod); 
				break;
			}
			if (tagno == agno && type == XFS_ALLOCTYPE_START_BNO)
				ntype = XFS_ALLOCTYPE_THIS_AG;
			if (++tagno == sbp->sb_agcount)
				tagno = 0;
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
 */
int				/* success/failure; will become void */
xfs_free_extent(
	xfs_trans_t	*tp,	/* transaction pointer */
	xfs_fsblock_t	bno,	/* starting block number of extent */
	xfs_extlen_t	len)	/* length of extent */
{
	xfs_agblock_t agbno;
	buf_t *agbuf;
	xfs_agnumber_t agno;
	int i;
	xfs_mount_t *mp;
	xfs_sb_t *sbp;

	mp = tp->t_mountp;
	sbp = &mp->m_sb;
	ASSERT(len != 0);
	agno = xfs_fsb_to_agno(sbp, bno);
	ASSERT(agno < sbp->sb_agcount);
	agbno = xfs_fsb_to_agbno(sbp, bno);
	ASSERT(agbno < sbp->sb_agblocks);
	agbuf = xfs_alloc_fix_freelist(tp, agno, 0, 0, 1);
	ASSERT(agbuf != NULL);
	i = xfs_free_ag_extent(tp, agbuf, agno, agbno, len);
	return i;
}
