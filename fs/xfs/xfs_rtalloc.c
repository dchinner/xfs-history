#ident	"$Revision: 1.20 $"

/*
 * Free realtime space allocation for xFS.
 */

#ifdef SIM
#define _KERNEL	1
#endif
#include <sys/param.h>
#include <sys/buf.h>
#include <sys/time.h>
#include <sys/grio.h>
#ifdef SIM
#undef _KERNEL
#endif
#include <sys/vnode.h>
#include <sys/debug.h>
#include <stddef.h>
#ifdef SIM
#include <stdlib.h>
#include <bstring.h>
#include <stdio.h>
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
#include "xfs_dinode.h"
#include "xfs_inode_item.h"
#include "xfs_inode.h"
#include "xfs_alloc.h"
#include "xfs_bmap.h"
#include "xfs_bit.h"
#include "xfs_rtalloc.h"
#ifdef SIM
#include "sim.h"
#endif

/*
 * Prototypes for internal functions.
 */

#ifndef SIM
STATIC xfs_rtblock_t
xfs_rtallocate_extent_block(
	xfs_trans_t	*tp,
	xfs_rtblock_t	bbno,
	xfs_extlen_t	minlen,
	xfs_extlen_t	maxlen,
	xfs_extlen_t	*len,
	xfs_rtblock_t	*nextp,
	buf_t		**rbpp,
	xfs_fsblock_t	*rsb,
	xfs_extlen_t	prod);

STATIC xfs_rtblock_t
xfs_rtallocate_extent_exact(
	xfs_trans_t	*tp,
	xfs_rtblock_t	bno,
	xfs_extlen_t	minlen,
	xfs_extlen_t	maxlen,
	xfs_extlen_t	*len,
	buf_t		**rbpp,
	xfs_fsblock_t	*rsb,
	xfs_extlen_t	prod);

STATIC xfs_rtblock_t
xfs_rtallocate_extent_near(
	xfs_trans_t	*tp,
	xfs_rtblock_t	bno,
	xfs_extlen_t	minlen,
	xfs_extlen_t	maxlen,
	xfs_extlen_t	*len,
	buf_t		**rbpp,
	xfs_fsblock_t	*rsb,
	xfs_extlen_t	prod);

STATIC xfs_rtblock_t
xfs_rtallocate_extent_size(
	xfs_trans_t	*tp,
	xfs_extlen_t	minlen,
	xfs_extlen_t	maxlen,
	xfs_extlen_t	*len,
	buf_t		**rbpp,
	xfs_fsblock_t	*rsb,
	xfs_extlen_t	prod);

STATIC void
xfs_rtallocate_range(
	xfs_trans_t	*tp,
	xfs_rtblock_t	start,
	xfs_extlen_t	len,
	buf_t		**rbpp,
	xfs_fsblock_t	*rsb);

STATIC int
xfs_rtany_summary(
	xfs_mount_t	*mp,
	xfs_trans_t	*tp,
	int		low,
	int		high,
	xfs_rtblock_t	bbno,
	buf_t		**rbpp,
	xfs_fsblock_t	*rsb);
#endif	/* !SIM */

STATIC buf_t *
xfs_rtbuf_get(
	xfs_mount_t	*mp,
	xfs_trans_t	*tp,
	xfs_rtblock_t	block,
	int		issum);

STATIC int
xfs_rtcheck_alloc_range(
	xfs_mount_t	*mp,
	xfs_trans_t	*tp,
	xfs_rtblock_t	bno,
	xfs_extlen_t	len);

#ifdef XFSDEBUG
STATIC int
xfs_rtcheck_bit(
	xfs_mount_t	*mp,
	xfs_trans_t	*tp,
	xfs_rtblock_t	start,
	int		val);
#endif	/* XFSDEBUG */

STATIC int
xfs_rtcheck_free_range(
	xfs_mount_t	*mp,
	xfs_trans_t	*tp,
	xfs_rtblock_t	bno,
	xfs_extlen_t	len);

STATIC int
xfs_rtcheck_range(
	xfs_mount_t	*mp,
	xfs_trans_t	*tp,
	xfs_rtblock_t	start,
	xfs_extlen_t	len,
	int		val,
	xfs_rtblock_t	*new);

STATIC xfs_rtblock_t
xfs_rtfind_back(
	xfs_mount_t	*mp,
	xfs_trans_t	*tp,
	xfs_rtblock_t	start,
	xfs_rtblock_t	limit);

STATIC xfs_rtblock_t
xfs_rtfind_forw(
	xfs_mount_t	*mp,
	xfs_trans_t	*tp,
	xfs_rtblock_t	start,
	xfs_rtblock_t	limit);

STATIC void
xfs_rtfree_range(
	xfs_trans_t	*tp,
	xfs_rtblock_t	start,
	xfs_extlen_t	len,
	buf_t		**rbpp,
	xfs_fsblock_t	*rsb);

#if defined(XFSDEBUG) || !defined(SIM)
STATIC xfs_suminfo_t
xfs_rtget_summary(
	xfs_mount_t	*mp,
	xfs_trans_t	*tp,
	int		log,
	xfs_rtblock_t	bbno,
	buf_t		**rbpp,
	xfs_fsblock_t	*rsb);
#endif	/* XFSDEBUG || !SIM */

STATIC void
xfs_rtmodify_range(
	xfs_trans_t	*tp,
	xfs_rtblock_t	start,
	xfs_extlen_t	len,
	int		val);

STATIC void
xfs_rtmodify_summary(
	xfs_trans_t	*tp,
	int		log,
	xfs_rtblock_t	bbno,
	int		delta,
	buf_t		**rbpp,
	xfs_fsblock_t	*rsb);

/*
 * Internal functions.
 */

#ifndef SIM
STATIC xfs_rtblock_t
xfs_rtallocate_extent_block(
	xfs_trans_t	*tp,
	xfs_rtblock_t	bbno,
	xfs_extlen_t	minlen,
	xfs_extlen_t	maxlen,
	xfs_extlen_t	*len,
	xfs_rtblock_t	*nextp,
	buf_t		**rbpp,
	xfs_fsblock_t	*rsb,
	xfs_extlen_t	prod)
{
	xfs_rtblock_t	base;
	xfs_rtblock_t	besti = -1;
	xfs_rtblock_t	bestlen;
	xfs_rtblock_t	end;
	xfs_rtblock_t	i;
	xfs_mount_t	*mp;
	xfs_rtblock_t	next;

	ASSERT(minlen % prod == 0 && maxlen % prod == 0);
	mp = tp->t_mountp;
	base = XFS_BLOCKTOBIT(mp, bbno);
	end = XFS_BLOCKTOBIT(mp, bbno + 1) - 1;
	for (i = base; i <= end; i++) {
		if (xfs_rtcheck_range(mp, tp, i, maxlen, 1, &next)) {
			xfs_rtallocate_range(tp, i, maxlen, rbpp, rsb);
			*len = maxlen;
			return i;
		}
		if (minlen < maxlen) {
			xfs_rtblock_t	thislen;

			thislen = next - i;
			if (thislen >= minlen &&
			    (besti == -1 || thislen > bestlen)) {
				besti = i;
				bestlen = thislen;
			}
		}
		if (next < end)
			i = xfs_rtfind_forw(mp, tp, next, end);
		else
			break;
	}
	if (minlen < maxlen && besti != -1) {
		xfs_extlen_t	p;

		if (prod > 1 && (p = bestlen % prod))
			bestlen -= p;
		xfs_rtallocate_range(tp, besti, bestlen, rbpp, rsb);
		*len = bestlen;
		return besti;
	}
	*nextp = next;
	return NULLRTBLOCK;
}

STATIC xfs_rtblock_t
xfs_rtallocate_extent_exact(
	xfs_trans_t	*tp,
	xfs_rtblock_t	bno,
	xfs_extlen_t	minlen,
	xfs_extlen_t	maxlen,
	xfs_extlen_t	*len,
	buf_t		**rbpp,
	xfs_fsblock_t	*rsb,
	xfs_extlen_t	prod)
{
	xfs_rtblock_t	base;
	xfs_extlen_t	i;
	xfs_mount_t	*mp;
	xfs_rtblock_t	next;

	ASSERT(minlen % prod == 0 && maxlen % prod == 0);
	mp = tp->t_mountp;
	base = bno;
	if (xfs_rtcheck_range(mp, tp, base, maxlen, 1, &next)) {
		xfs_rtallocate_range(tp, base, maxlen, rbpp, rsb);
		*len = maxlen;
		return bno;
	}
	maxlen = next - base;
	if (maxlen < minlen)
		return NULLRTBLOCK;
	if (prod > 1 && (i = maxlen % prod)) {
		maxlen -= i;
		if (maxlen < minlen)
			return NULLRTBLOCK;
	}
	xfs_rtallocate_range(tp, base, maxlen, rbpp, rsb);
	*len = maxlen;
	return bno;
}

STATIC xfs_rtblock_t
xfs_rtallocate_extent_near(
	xfs_trans_t	*tp,
	xfs_rtblock_t	bno,
	xfs_extlen_t	minlen,
	xfs_extlen_t	maxlen,
	xfs_extlen_t	*len,
	buf_t		**rbpp,
	xfs_fsblock_t	*rsb,
	xfs_extlen_t	prod)
{
	xfs_rtblock_t	bbno;
	int		i;
	int		j;
	int		l;
	int		log2len;
	xfs_mount_t	*mp;
	xfs_rtblock_t	n;
	xfs_rtblock_t	r;

	ASSERT(minlen % prod == 0 && maxlen % prod == 0);
	mp = tp->t_mountp;
	if (bno >= mp->m_sb.sb_rextents)
		bno = mp->m_sb.sb_rextents - 1;
	r = xfs_rtallocate_extent_exact(tp, bno, minlen, maxlen, len, rbpp,
		rsb, prod);
	if (r != NULLRTBLOCK)
		return r;
	bbno = XFS_BITTOBLOCK(mp, bno);
	i = 0;
	log2len = xfs_highbit32(minlen);
	while (1) {
		if (xfs_rtany_summary(mp, tp, log2len, mp->m_rsumlevels - 1,
			bbno + i, rbpp, rsb)) {
			if (i >= 0 &&
			    (r = xfs_rtallocate_extent_block(tp, bbno + i,
					minlen, maxlen, len, &n, rbpp, rsb,
					prod)) !=
					NULLRTBLOCK)
				return r;
			else if (i < 0) {
				for (j = -1; j > i; j--) {
					if (!xfs_rtany_summary(mp, tp, log2len, mp->m_rsumlevels - 1, bbno + j, rbpp, rsb)) {
						if ((r = xfs_rtallocate_extent_block(tp, bbno + j, minlen, maxlen, len, &n, rbpp, rsb, prod)) != NULLRTBLOCK)
							return r;
					}
					if ((r = xfs_rtallocate_extent_block(tp, bbno + i, minlen, maxlen, len, &n, rbpp, rsb, prod)) != NULLRTBLOCK)
						return r;
				}
			}
		}
		if (i > 0 && (int)bbno - i >= 0)
			i = -i;
		else if (i > 0 && (int)bbno + i < mp->m_sb.sb_rbmblocks - 1)
			i++;
		else if (i <= 0 && (int)bbno - i < mp->m_sb.sb_rbmblocks - 1)
			i = 1 - i;
		else if (i <= 0 && (int)bbno + i > 0)
			i--;
		else
			break;
	}
	return NULLRTBLOCK;
}

STATIC xfs_rtblock_t
xfs_rtallocate_extent_size(
	xfs_trans_t	*tp,
	xfs_extlen_t	minlen,
	xfs_extlen_t	maxlen,
	xfs_extlen_t	*len,
	buf_t		**rbpp,
	xfs_fsblock_t	*rsb,
	xfs_extlen_t	prod)
{
	int		i;
	int		l;
	xfs_mount_t	*mp;
	xfs_rtblock_t	n;
	xfs_rtblock_t	r;

	ASSERT(minlen % prod == 0 && maxlen % prod == 0);
	mp = tp->t_mountp;
	for (l = xfs_highbit32(maxlen); l < mp->m_rsumlevels; l++) {
		for (i = 0; i < mp->m_sb.sb_rbmblocks; i++) {
			if (!xfs_rtget_summary(mp, tp, l, i, rbpp, rsb))
				continue;
			if ((r = xfs_rtallocate_extent_block(tp, i, maxlen, maxlen, len, &n, rbpp, rsb, prod)) != NULLRTBLOCK)
				return r;
			if (XFS_BITTOBLOCK(mp, n) > i + 1)
				i = XFS_BITTOBLOCK(mp, n) - 1;
		}
	}
	if (minlen > --maxlen)
		return NULLRTBLOCK;
	for (l = xfs_highbit32(maxlen); l >= xfs_highbit32(minlen); l--) {
		for (i = 0; i < mp->m_sb.sb_rbmblocks; i++) {
			if (!xfs_rtget_summary(mp, tp, l, i, rbpp, rsb))
				continue;
			if ((r = xfs_rtallocate_extent_block(tp, i, XFS_RTMAX(minlen, 1 << l), XFS_RTMIN(maxlen, (1 << (l + 1)) - 1), len, &n, rbpp, rsb, prod)) != NULLRTBLOCK)
				return r;
			if (XFS_BITTOBLOCK(mp, n) > i + 1)
				i = XFS_BITTOBLOCK(mp, n) - 1;
		}
	}
	return NULLRTBLOCK;
}

STATIC void
xfs_rtallocate_range(
	xfs_trans_t	*tp,
	xfs_rtblock_t	start,
	xfs_extlen_t	len,
	buf_t		**rbpp,
	xfs_fsblock_t	*rsb)
{
	xfs_rtblock_t	end;
	xfs_mount_t	*mp;
	xfs_rtblock_t	postblock;
	xfs_rtblock_t	preblock;

	mp = tp->t_mountp;
	end = start + len - 1;
	preblock = xfs_rtfind_back(mp, tp, start, 0);
	postblock = xfs_rtfind_forw(mp, tp, end, mp->m_sb.sb_rextents - 1);
	xfs_rtmodify_summary(tp, XFS_RTBLOCKLOG(postblock + 1 - preblock), XFS_BITTOBLOCK(mp, preblock), -1, rbpp, rsb);
	if (preblock < start)
		xfs_rtmodify_summary(tp, XFS_RTBLOCKLOG(start - preblock), XFS_BITTOBLOCK(mp, preblock), 1, rbpp, rsb);
	if (postblock > end)
		xfs_rtmodify_summary(tp, XFS_RTBLOCKLOG(postblock - end), XFS_BITTOBLOCK(mp, end + 1), 1, rbpp, rsb);
	xfs_rtmodify_range(tp, start, len, 0);
}

STATIC int
xfs_rtany_summary(
	xfs_mount_t	*mp,
	xfs_trans_t	*tp,
	int		low,
	int		high,
	xfs_rtblock_t	bbno,
	buf_t		**rbpp,
	xfs_fsblock_t	*rsb)
{
	int		log;

	for (log = low; log <= high; log++) {
		if (xfs_rtget_summary(mp, tp, log, bbno, rbpp, rsb))
			return 1;
	}
	return 0;
}
#endif	/* !SIM */

STATIC buf_t *
xfs_rtbuf_get(
	xfs_mount_t	*mp,
	xfs_trans_t	*tp,
	xfs_rtblock_t	block,
	int		issum)
{
	buf_t		*bp;
	daddr_t		d;
	xfs_bmap_free_t	*flist;
	xfs_inode_t	*ip;
	xfs_bmbt_irec_t	map;
	int		nmap;

	nmap = 1;
	flist = NULL;
	ip = issum ? mp->m_rsumip : mp->m_rbmip;
	(void)xfs_bmapi(tp, ip, block, 1, 0, NULLFSBLOCK, 0, &map, &nmap, flist);
	ASSERT(nmap == 1);
	ASSERT(flist == NULL);
	d = XFS_FSB_TO_DADDR(mp, map.br_startblock);
	bp = xfs_trans_read_buf(tp, mp->m_dev, d, mp->m_bsize, 0);
	ASSERT(bp && !geterror(bp));
	return bp;
}

STATIC int
xfs_rtcheck_alloc_range(
	xfs_mount_t	*mp,
	xfs_trans_t	*tp,
	xfs_rtblock_t	bno,
	xfs_extlen_t	len)
{
	xfs_rtblock_t	new;

	return xfs_rtcheck_range(mp, tp, bno, len, 0, &new);
}

#ifdef XFSDEBUG
STATIC int
xfs_rtcheck_bit(
	xfs_mount_t	*mp,
	xfs_trans_t	*tp,
	xfs_rtblock_t	start,
	int		val)
{
	int		bit;
	xfs_rtblock_t	block;
	buf_t		*bp;
	xfs_rtword_t	*bufp;
	xfs_rtword_t	wdiff;
	int		word;
	xfs_rtword_t	wval;

	block = XFS_BITTOBLOCK(mp, start);
	bp = xfs_rtbuf_get(mp, tp, block, 0);
	bufp = (xfs_rtword_t *)bp->b_un.b_addr;
	word = XFS_BITTOWORD(mp, start);
	bit = start & (XFS_NBWORD - 1);
	wval = bufp[word];
	xfs_trans_brelse(tp, bp);
	wdiff = (wval ^ -val) & ((xfs_rtword_t)1 << bit);
	return !wdiff;
}
#endif	/* XFSDEBUG */

STATIC int
xfs_rtcheck_free_range(
	xfs_mount_t	*mp,
	xfs_trans_t	*tp,
	xfs_rtblock_t	bno,
	xfs_extlen_t	len)
{
	xfs_rtblock_t	new;

	return xfs_rtcheck_range(mp, tp, bno, len, 1, &new);
}

STATIC int
xfs_rtcheck_range(
	xfs_mount_t	*mp,
	xfs_trans_t	*tp,
	xfs_rtblock_t	start,
	xfs_extlen_t	len,
	int		val,
	xfs_rtblock_t	*new)
{
	xfs_rtword_t	*b;
	int		bit;
	xfs_rtblock_t	block;
	buf_t		*bp;
	xfs_rtword_t	*bufp;
	xfs_rtblock_t	i;
	xfs_rtblock_t	lastbit;
	xfs_rtword_t	mask;
	xfs_rtword_t	wdiff;
	int		word;

	block = XFS_BITTOBLOCK(mp, start);
	bp = xfs_rtbuf_get(mp, tp, block, 0);
	bufp = (xfs_rtword_t *)bp->b_un.b_addr;
	word = XFS_BITTOWORD(mp, start);
	b = &bufp[word];
	bit = start & (XFS_NBWORD - 1);
	val = -val;
	if (bit) {
		lastbit = XFS_RTMIN(bit + len, XFS_NBWORD);
		mask = (((xfs_rtword_t)1 << (lastbit - bit)) - 1) << bit;
		if (wdiff = (*b ^ val) & mask) {
			xfs_trans_brelse(tp, bp);
			i = XFS_RTLOBIT(wdiff) - bit;
			*new = start + i;
			return 0;
		}
		i = lastbit - bit;
		if (++word == XFS_BLOCKWSIZE(mp) && i < len) {
			xfs_trans_brelse(tp, bp);
			bp = xfs_rtbuf_get(mp, tp, ++block, 0);
			b = bufp = (xfs_rtword_t *)bp->b_un.b_addr;
			word = 0;
		} else
			b++;
	} else
		i = 0;
	while (len - i >= XFS_NBWORD) {
		if (wdiff = *b ^ val) {
			xfs_trans_brelse(tp, bp);
			i += XFS_RTLOBIT(wdiff);
			*new = start + i;
			return 0;
		}
		i += XFS_NBWORD;
		if (++word == XFS_BLOCKWSIZE(mp) && i < len) {
			xfs_trans_brelse(tp, bp);
			bp = xfs_rtbuf_get(mp, tp, ++block, 0);
			b = bufp = (xfs_rtword_t *)bp->b_un.b_addr;
			word = 0;
		} else
			b++;
	}
	if (lastbit = len - i) {
		mask = ((xfs_rtword_t)1 << lastbit) - 1;
		if (wdiff = (*b ^ val) & mask) {
			xfs_trans_brelse(tp, bp);
			i += XFS_RTLOBIT(wdiff);
			*new = start + i;
			return 0;
		} else
			i = len;
	}
	xfs_trans_brelse(tp, bp);
	*new = start + i;
	return 1;
}

STATIC xfs_rtblock_t
xfs_rtfind_back(
	xfs_mount_t	*mp,
	xfs_trans_t	*tp,
	xfs_rtblock_t	start,
	xfs_rtblock_t	limit)
{
	xfs_rtword_t	*b;
	int		bit;
	xfs_rtblock_t	block;
	buf_t		*bp;
	xfs_rtword_t	*bufp;
	xfs_rtblock_t	firstbit;
	xfs_rtblock_t	i;
	xfs_rtblock_t	len;
	xfs_rtword_t	mask;
	xfs_rtword_t	want;
	xfs_rtword_t	wdiff;
	int		word;

	block = XFS_BITTOBLOCK(mp, start);
	bp = xfs_rtbuf_get(mp, tp, block, 0);
	bufp = (xfs_rtword_t *)bp->b_un.b_addr;
	word = XFS_BITTOWORD(mp, start);
	b = &bufp[word];
	bit = start & (XFS_NBWORD - 1);
	len = start - limit + 1;
	want = (*b & ((xfs_rtword_t)1 << bit)) ? -1 : 0;
	if (bit < XFS_NBWORD - 1) {
		firstbit = XFS_RTMAX((xfs_srtblock_t)(bit - len + 1), 0);
		mask = (((xfs_rtword_t)1 << (bit - firstbit + 1)) - 1) << firstbit;
		if (wdiff = (*b ^ want) & mask) {
			xfs_trans_brelse(tp, bp);
			i = bit - XFS_RTHIBIT(wdiff);
			return start - i + 1;
		}
		i = bit - firstbit + 1;
		if (--word == -1 && i < len) {
			xfs_trans_brelse(tp, bp);
			bp = xfs_rtbuf_get(mp, tp, --block, 0);
			bufp = (xfs_rtword_t *)bp->b_un.b_addr;
			word = XFS_BLOCKWMASK(mp);
			b = &bufp[word];
		} else
			b--;
	} else
		i = 0;
	while (len - i >= XFS_NBWORD) {
		if (wdiff = *b ^ want) {
			xfs_trans_brelse(tp, bp);
			i += XFS_NBWORD - 1 - XFS_RTHIBIT(wdiff);
			return start - i + 1;
		}
		i += XFS_NBWORD;
		if (--word == -1 && i < len) {
			xfs_trans_brelse(tp, bp);
			bp = xfs_rtbuf_get(mp, tp, --block, 0);
			bufp = (xfs_rtword_t *)bp->b_un.b_addr;
			word = XFS_BLOCKWMASK(mp);
			b = &bufp[word];
		} else
			b--;
	}
	if (len - i) {
		firstbit = XFS_NBWORD - (len - i);
		mask = (((xfs_rtword_t)1 << (len - i)) - 1) << firstbit;
		if (wdiff = (*b ^ want) & mask) {
			xfs_trans_brelse(tp, bp);
			i += XFS_NBWORD - 1 - XFS_RTHIBIT(wdiff);
			return start - i + 1;
		} else
			i = len;
	}
	xfs_trans_brelse(tp, bp);
	return start - i + 1;
}

STATIC xfs_rtblock_t
xfs_rtfind_forw(
	xfs_mount_t	*mp,
	xfs_trans_t	*tp,
	xfs_rtblock_t	start,
	xfs_rtblock_t	limit)
{
	xfs_rtword_t	*b;
	int		bit;
	xfs_rtblock_t	block;
	buf_t		*bp;
	xfs_rtword_t	*bufp;
	xfs_rtblock_t	i;
	xfs_rtblock_t	lastbit;
	xfs_rtblock_t	len;
	xfs_rtword_t	mask;
	xfs_rtword_t	want;
	xfs_rtword_t	wdiff;
	int		word;

	block = XFS_BITTOBLOCK(mp, start);
	bp = xfs_rtbuf_get(mp, tp, block, 0);
	bufp = (xfs_rtword_t *)bp->b_un.b_addr;
	word = XFS_BITTOWORD(mp, start);
	b = &bufp[word];
	bit = start & (XFS_NBWORD - 1);
	want = (*b & ((xfs_rtword_t)1 << bit)) ? -1 : 0;
	len = limit - start + 1;
	if (bit) {
		lastbit = XFS_RTMIN(bit + len, XFS_NBWORD);
		mask = (((xfs_rtword_t)1 << (lastbit - bit)) - 1) << bit;
		if (wdiff = (*b ^ want) & mask) {
			xfs_trans_brelse(tp, bp);
			i = XFS_RTLOBIT(wdiff) - bit;
			return start + i - 1;
		}
		i = lastbit - bit;
		if (++word == XFS_BLOCKWSIZE(mp) && i < len) {
			xfs_trans_brelse(tp, bp);
			bp = xfs_rtbuf_get(mp, tp, ++block, 0);
			b = bufp = (xfs_rtword_t *)bp->b_un.b_addr;
			word = 0;
		} else
			b++;
	} else
		i = 0;
	while (len - i >= XFS_NBWORD) {
		if (wdiff = *b ^ want) {
			xfs_trans_brelse(tp, bp);
			i += XFS_RTLOBIT(wdiff);
			return start + i - 1;
		}
		i += XFS_NBWORD;
		if (++word == XFS_BLOCKWSIZE(mp) && i < len) {
			xfs_trans_brelse(tp, bp);
			bp = xfs_rtbuf_get(mp, tp, ++block, 0);
			b = bufp = (xfs_rtword_t *)bp->b_un.b_addr;
			word = 0;
		} else
			b++;
	}
	if (lastbit = len - i) {
		mask = ((xfs_rtword_t)1 << lastbit) - 1;
		if (wdiff = (*b ^ want) & mask) {
			xfs_trans_brelse(tp, bp);
			i += XFS_RTLOBIT(wdiff);
			return start + i - 1;
		} else
			i = len;
	}
	xfs_trans_brelse(tp, bp);
	return start + i - 1;
}

STATIC void
xfs_rtfree_range(
	xfs_trans_t	*tp,
	xfs_rtblock_t	start,
	xfs_extlen_t	len,
	buf_t		**rbpp,
	xfs_fsblock_t	*rsb)
{
	xfs_rtblock_t	end;
	xfs_mount_t	*mp;
	xfs_rtblock_t	postblock;
	xfs_rtblock_t	preblock;

	mp = tp->t_mountp;
	end = start + len - 1;
	xfs_rtmodify_range(tp, start, len, 1);
	preblock = xfs_rtfind_back(mp, tp, start, 0);
	postblock = xfs_rtfind_forw(mp, tp, end, mp->m_sb.sb_rextents - 1);
	if (preblock < start)
		xfs_rtmodify_summary(tp, XFS_RTBLOCKLOG(start - preblock), XFS_BITTOBLOCK(mp, preblock), -1, rbpp, rsb);
	if (postblock > end)
		xfs_rtmodify_summary(tp, XFS_RTBLOCKLOG(postblock - end), XFS_BITTOBLOCK(mp, end + 1), -1, rbpp, rsb);
	xfs_rtmodify_summary(tp, XFS_RTBLOCKLOG(postblock + 1 - preblock), XFS_BITTOBLOCK(mp, preblock), 1, rbpp, rsb);
}

#if defined(XFSDEBUG) || !defined(SIM)
STATIC xfs_suminfo_t
xfs_rtget_summary(
	xfs_mount_t	*mp,
	xfs_trans_t	*tp,
	int		log,
	xfs_rtblock_t	bbno,
	buf_t		**rbpp,
	xfs_fsblock_t	*rsb)
{
	buf_t		*bp;
	xfs_suminfo_t	rval;
	xfs_fsblock_t	sb;
	int		so;
	xfs_suminfo_t	*sp;

	so = XFS_SUMOFFS(mp, log, bbno);
	sb = XFS_SUMOFFSTOBLOCK(mp, so);
	if (rbpp && *rbpp && *rsb == sb)
		bp = *rbpp;
	else {
		if (rbpp && *rbpp)
			xfs_trans_brelse(tp, *rbpp);
		bp = xfs_rtbuf_get(mp, tp, sb, 1);
		if (rbpp) {
			*rbpp = bp;
			*rsb = sb;
		}
	}
	sp = XFS_SUMPTR(mp, bp, so);
	rval = *sp;
	if (!rbpp)
		xfs_trans_brelse(tp, bp);
	return rval;
}
#endif	/* XFSDEBUG || !SIM */

STATIC void
xfs_rtmodify_range(
	xfs_trans_t	*tp,
	xfs_rtblock_t	start,
	xfs_extlen_t	len,
	int		val)
{
	xfs_rtword_t	*b;
	int		bit;
	xfs_rtblock_t	block;
	buf_t		*bp;
	xfs_rtword_t	*bufp;
	xfs_rtword_t	*first;
	int		i;
	int		lastbit;
	xfs_rtword_t	mask;
	xfs_mount_t	*mp;
	int		word;
	int		wval;

	mp = tp->t_mountp;
	block = XFS_BITTOBLOCK(mp, start);
	bp = xfs_rtbuf_get(mp, tp, block, 0);
	bufp = (xfs_rtword_t *)bp->b_un.b_addr;
	word = XFS_BITTOWORD(mp, start);
	first = b = &bufp[word];
	bit = start & (XFS_NBWORD - 1);
	val = -val;
	if (bit) {
		lastbit = XFS_RTMIN(bit + len, XFS_NBWORD);
		mask = (((xfs_rtword_t)1 << (lastbit - bit)) - 1) << bit;
		if (val)
			*b |= mask;
		else
			*b &= ~mask;
		i = lastbit - bit;
		if (++word == XFS_BLOCKWSIZE(mp) && i < len) {
			xfs_trans_log_buf(tp, bp, (char *)first - (char *)bufp, (char *)b - (char *)bufp);
			bp = xfs_rtbuf_get(mp, tp, ++block, 0);
			first = b = bufp = (xfs_rtword_t *)bp->b_un.b_addr;
			word = 0;
		} else
			b++;
	} else
		i = 0;
	while (len - i >= XFS_NBWORD) {
		*b = val;
		i += XFS_NBWORD;
		if (++word == XFS_BLOCKWSIZE(mp) && i < len) {
			xfs_trans_log_buf(tp, bp, (char *)first - (char *)bufp, (char *)b - (char *)bufp);
			bp = xfs_rtbuf_get(mp, tp, ++block, 0);
			first = b = bufp = (xfs_rtword_t *)bp->b_un.b_addr;
			word = 0;
		} else
			b++;
	}
	if (lastbit = len - i) {
		bit = 0;
		mask = ((xfs_rtword_t)1 << lastbit) - 1;
		if (val)
			*b |= mask;
		else
			*b &= ~mask;
		b++;
	}
	if (b > first)
		xfs_trans_log_buf(tp, bp, (char *)first - (char *)bufp, (char *)b - (char *)bufp - 1);
}

STATIC void
xfs_rtmodify_summary(
	xfs_trans_t	*tp,
	int		log,
	xfs_rtblock_t	bbno,
	int		delta,
	buf_t		**rbpp,
	xfs_fsblock_t	*rsb)
{
	buf_t		*bp;
	xfs_mount_t	*mp;
	xfs_fsblock_t	sb;
	int		so;
	xfs_suminfo_t	*sp;

	mp = tp->t_mountp;
	so = XFS_SUMOFFS(mp, log, bbno);
	sb = XFS_SUMOFFSTOBLOCK(mp, so);
	if (rbpp && *rbpp && *rsb == sb)
		bp = *rbpp;
	else {
		if (rbpp && *rbpp)
			xfs_trans_brelse(tp, *rbpp);
		bp = xfs_rtbuf_get(mp, tp, sb, 1);
		if (rbpp) {
			*rbpp = bp;
			*rsb = sb;
		}
	}
	sp = XFS_SUMPTR(mp, bp, so);
	*sp += delta;
	xfs_trans_log_buf(tp, bp, (char *)sp - (char *)bp->b_un.b_addr, (char *)sp - (char *)bp->b_un.b_addr + sizeof(*sp) - 1);
}

/* 
 * Visible (exported) functions.
 */

#ifndef SIM
xfs_rtblock_t
xfs_rtallocate_extent(
	xfs_trans_t	*tp,
	xfs_rtblock_t	bno,
	xfs_extlen_t	minlen,
	xfs_extlen_t	maxlen,
	xfs_extlen_t	*len,
	xfs_alloctype_t	type,
	int		wasdel,
	xfs_extlen_t	prod)
{
	xfs_mount_t	*mp;
	xfs_rtblock_t	r;
	xfs_fsblock_t	sb;
	buf_t		*sumbp = 0;

	ASSERT(minlen > 0 && minlen <= maxlen);
	mp = tp->t_mountp;
	if (prod > 1) {
		xfs_extlen_t	i;

		if (i = maxlen % prod)
			maxlen -= i;
		if (i = minlen % prod)
			minlen += prod - i;
		if (maxlen < minlen)
			return NULLRTBLOCK;
	}
	(void)xfs_trans_iget(mp, tp, mp->m_sb.sb_rbmino, XFS_ILOCK_EXCL);
	switch (type) {
	case XFS_ALLOCTYPE_ANY_AG:
		r = xfs_rtallocate_extent_size(tp, minlen, maxlen, len, &sumbp,
			&sb, prod);
		break;
	case XFS_ALLOCTYPE_NEAR_BNO:
		r = xfs_rtallocate_extent_near(tp, bno, minlen, maxlen, len,
			&sumbp, &sb, prod);
		break;
	case XFS_ALLOCTYPE_THIS_BNO:
		r = xfs_rtallocate_extent_exact(tp, bno, minlen, maxlen, len,
			&sumbp, &sb, prod);
		break;
	}
	if (r != NULLRTBLOCK) {
		int slen = (int)*len;

		ASSERT(*len >= minlen && *len <= maxlen);
		if (wasdel)
			xfs_trans_mod_sb(tp, XFS_TRANS_SB_RES_FREXTENTS, -slen);
		else
			xfs_trans_mod_sb(tp, XFS_TRANS_SB_FREXTENTS, -slen);
	}
	return r;
}
#endif	/* !SIM */

void
xfs_rtfree_extent(
	xfs_trans_t	*tp,
	xfs_rtblock_t	bno,
	xfs_extlen_t	len)
{
	xfs_mount_t	*mp;
	xfs_fsblock_t	sb;
	buf_t		*sumbp = 0;

	mp = tp->t_mountp;
	(void)xfs_trans_iget(mp, tp, mp->m_sb.sb_rbmino, XFS_ILOCK_EXCL);
	ASSERT(xfs_rtcheck_alloc_range(mp, tp, bno, len));
	xfs_rtfree_range(tp, bno, len, &sumbp, &sb);
	xfs_trans_mod_sb(tp, XFS_TRANS_SB_FREXTENTS, (int)len);
}

#ifdef XFSDEBUG
void
xfs_rtprint_range(
	xfs_mount_t	*mp,
	xfs_trans_t	*tp,
	xfs_rtblock_t	start,
	xfs_extlen_t	len)
{
	xfs_extlen_t	i;

	printf("%lld: ", start);
	for (i = 0; i < len; i++)
		printf("%d", xfs_rtcheck_bit(mp, tp, start + i, 1));
	printf("\n");
}

void
xfs_rtprint_summary(
	xfs_mount_t	*mp,
	xfs_trans_t	*tp)
{
	xfs_suminfo_t	c;
	xfs_rtblock_t	i;
	int		l;
	int		p;
	xfs_fsblock_t	sb;
	buf_t		*sumbp = NULL;

	for (l = 0; l < mp->m_rsumlevels; l++) {
		for (p = 0, i = 0; i < mp->m_sb.sb_rbmblocks; i++) {
			if (c = xfs_rtget_summary(mp, tp, l, i, &sumbp, &sb)) {
				if (!p) {
					printf("%lld-%lld:", 1LL << l, XFS_RTMIN((1LL << l) + ((1LL << l) - 1LL), mp->m_sb.sb_rextents));
					p = 1;
				}
				printf(" %lld:%d", i, c);
			}
		}
		if (p)
			printf("\n");
	}
	if (sumbp)
		xfs_trans_brelse(tp, sumbp);
}
#endif	/* XFSDEBUG */
