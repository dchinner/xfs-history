#ident	"$Revision: 1.7 $"

/*
 * Free realtime space allocation for xFS.
 */

#include <sys/param.h>
#ifdef SIM
#define _KERNEL
#endif
#include <sys/buf.h>
#include <sys/grio.h>
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
#include "xfs_ialloc.h"
#include "xfs_bmap_btree.h"
#include "xfs_btree.h"
#include "xfs_dinode.h"
#include "xfs_inode_item.h"
#include "xfs_inode.h"
#include "xfs_alloc.h"
#include "xfs_bmap.h"
#include "xfs_rtalloc.h"
#ifdef SIM
#include "sim.h"
#endif

/*
 * Prototypes for internal functions.
 */

STATIC xfs_rtblock_t
xfs_rtallocate_extent_block(
	xfs_trans_t	*tp,
	xfs_rtblock_t	bbno,
	xfs_extlen_t	minlen,
	xfs_extlen_t	maxlen,
	xfs_extlen_t	*len,
	xfs_rtblock_t	*nextp,
	buf_t		**rbuf,
	xfs_fsblock_t	*rsb,
	xfs_extlen_t	prod);

STATIC xfs_rtblock_t
xfs_rtallocate_extent_exact(
	xfs_trans_t	*tp,
	xfs_rtblock_t	bno,
	xfs_extlen_t	minlen,
	xfs_extlen_t	maxlen,
	xfs_extlen_t	*len,
	buf_t		**rbuf,
	xfs_fsblock_t	*rsb,
	xfs_extlen_t	prod);

STATIC xfs_rtblock_t
xfs_rtallocate_extent_near(
	xfs_trans_t	*tp,
	xfs_rtblock_t	bno,
	xfs_extlen_t	minlen,
	xfs_extlen_t	maxlen,
	xfs_extlen_t	*len,
	buf_t		**rbuf,
	xfs_fsblock_t	*rsb,
	xfs_extlen_t	prod);

STATIC xfs_rtblock_t
xfs_rtallocate_extent_size(
	xfs_trans_t	*tp,
	xfs_extlen_t	minlen,
	xfs_extlen_t	maxlen,
	xfs_extlen_t	*len,
	buf_t		**rbuf,
	xfs_fsblock_t	*rsb,
	xfs_extlen_t	prod);

STATIC void
xfs_rtallocate_range(
	xfs_trans_t	*tp,
	xfs_rtblock_t	start,
	xfs_extlen_t	len,
	buf_t		**rbuf,
	xfs_fsblock_t	*rsb);

STATIC int
xfs_rtany_summary(
	xfs_mount_t	*mp,
	xfs_trans_t	*tp,
	int		low,
	int		high,
	xfs_rtblock_t	bbno,
	buf_t		**rbuf,
	xfs_fsblock_t	*rsb);

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

STATIC int
xfs_rtcheck_bit(
	xfs_mount_t	*mp,
	xfs_trans_t	*tp,
	xfs_rtblock_t	start,
	int		val);

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
	buf_t		**rbuf,
	xfs_fsblock_t	*rsb);

STATIC xfs_suminfo_t
xfs_rtget_summary(
	xfs_mount_t	*mp,
	xfs_trans_t	*tp,
	int		log,
	xfs_rtblock_t	bbno,
	buf_t		**rbuf,
	xfs_fsblock_t	*rsb);

#ifndef SIM
STATIC int
xfs_rthibit(
	xfs_rtword_t	w);
#endif

STATIC int
xfs_rtlobit(
	xfs_rtword_t	w);

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
	buf_t		**rbuf,
	xfs_fsblock_t	*rsb);

/*
 * Internal functions.
 */

STATIC xfs_rtblock_t
xfs_rtallocate_extent_block(
	xfs_trans_t	*tp,
	xfs_rtblock_t	bbno,
	xfs_extlen_t	minlen,
	xfs_extlen_t	maxlen,
	xfs_extlen_t	*len,
	xfs_rtblock_t	*nextp,
	buf_t		**rbuf,
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
			xfs_rtallocate_range(tp, i, maxlen, rbuf, rsb);
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
		xfs_rtallocate_range(tp, besti, bestlen, rbuf, rsb);
		*len = bestlen;
		return besti;
	}
	*nextp = next;
	return NULLFSBLOCK;
}

STATIC xfs_rtblock_t
xfs_rtallocate_extent_exact(
	xfs_trans_t	*tp,
	xfs_rtblock_t	bno,
	xfs_extlen_t	minlen,
	xfs_extlen_t	maxlen,
	xfs_extlen_t	*len,
	buf_t		**rbuf,
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
		xfs_rtallocate_range(tp, base, maxlen, rbuf, rsb);
		*len = maxlen;
		return bno;
	}
	maxlen = next - base;
	if (maxlen < minlen)
		return NULLFSBLOCK;
	if (prod > 1 && (i = maxlen % prod)) {
		maxlen -= i;
		if (maxlen < minlen)
			return NULLFSBLOCK;
	}
	xfs_rtallocate_range(tp, base, maxlen, rbuf, rsb);
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
	buf_t		**rbuf,
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
	r = xfs_rtallocate_extent_exact(tp, bno, minlen, maxlen, len, rbuf,
		rsb, prod);
	if (r != NULLFSBLOCK)
		return r;
	bbno = XFS_BITTOBLOCK(mp, bno);
	i = 0;
	log2len = xfs_rthibit(minlen);
	while (1) {
		if (xfs_rtany_summary(mp, tp, log2len, mp->m_rsumlevels - 1,
			bbno + i, rbuf, rsb)) {
			if (i >= 0 &&
			    (r = xfs_rtallocate_extent_block(tp, bbno + i,
					minlen, maxlen, len, &n, rbuf, rsb,
					prod)) !=
					NULLFSBLOCK)
				return r;
			else if (i < 0) {
				for (j = -1; j > i; j--) {
					if (!xfs_rtany_summary(mp, tp, log2len, mp->m_rsumlevels - 1, bbno + j, rbuf, rsb)) {
						if ((r = xfs_rtallocate_extent_block(tp, bbno + j, minlen, maxlen, len, &n, rbuf, rsb, prod)) != NULLFSBLOCK)
							return r;
					}
					if ((r = xfs_rtallocate_extent_block(tp, bbno + i, minlen, maxlen, len, &n, rbuf, rsb, prod)) != NULLFSBLOCK)
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
	return NULLFSBLOCK;
}

STATIC xfs_rtblock_t
xfs_rtallocate_extent_size(
	xfs_trans_t	*tp,
	xfs_extlen_t	minlen,
	xfs_extlen_t	maxlen,
	xfs_extlen_t	*len,
	buf_t		**rbuf,
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
	for (l = xfs_rthibit(maxlen); l < mp->m_rsumlevels; l++) {
		for (i = 0; i < mp->m_sb.sb_rbmblocks; i++) {
			if (!xfs_rtget_summary(mp, tp, l, i, rbuf, rsb))
				continue;
			if ((r = xfs_rtallocate_extent_block(tp, i, maxlen, maxlen, len, &n, rbuf, rsb, prod)) != NULLFSBLOCK)
				return r;
			if (XFS_BITTOBLOCK(mp, n) > i + 1)
				i = XFS_BITTOBLOCK(mp, n) - 1;
		}
	}
	if (minlen > --maxlen)
		return NULLFSBLOCK;
	for (l = xfs_rthibit(maxlen); l >= xfs_rthibit(minlen); l--) {
		for (i = 0; i < mp->m_sb.sb_rbmblocks; i++) {
			if (!xfs_rtget_summary(mp, tp, l, i, rbuf, rsb))
				continue;
			if ((r = xfs_rtallocate_extent_block(tp, i, XFS_RTMAX(minlen, 1 << l), XFS_RTMIN(maxlen, (1 << (l + 1)) - 1), len, &n, rbuf, rsb, prod)) != NULLFSBLOCK)
				return r;
			if (XFS_BITTOBLOCK(mp, n) > i + 1)
				i = XFS_BITTOBLOCK(mp, n) - 1;
		}
	}
	return NULLFSBLOCK;
}

STATIC void
xfs_rtallocate_range(
	xfs_trans_t	*tp,
	xfs_rtblock_t	start,
	xfs_extlen_t	len,
	buf_t		**rbuf,
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
	xfs_rtmodify_summary(tp, xfs_rthibit(postblock + 1 - preblock), XFS_BITTOBLOCK(mp, preblock), -1, rbuf, rsb);
	if (preblock < start)
		xfs_rtmodify_summary(tp, xfs_rthibit(start - preblock), XFS_BITTOBLOCK(mp, preblock), 1, rbuf, rsb);
	if (postblock > end)
		xfs_rtmodify_summary(tp, xfs_rthibit(postblock - end), XFS_BITTOBLOCK(mp, end + 1), 1, rbuf, rsb);
	xfs_rtmodify_range(tp, start, len, 0);
}

STATIC int
xfs_rtany_summary(
	xfs_mount_t	*mp,
	xfs_trans_t	*tp,
	int		low,
	int		high,
	xfs_rtblock_t	bbno,
	buf_t		**rbuf,
	xfs_fsblock_t	*rsb)
{
	int		log;

	for (log = low; log <= high; log++) {
		if (xfs_rtget_summary(mp, tp, log, bbno, rbuf, rsb))
			return 1;
	}
	return 0;
}

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
	d = xfs_fsb_to_daddr(mp, map.br_startblock);
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

STATIC int
xfs_rtcheck_bit(
	xfs_mount_t	*mp,
	xfs_trans_t	*tp,
	xfs_rtblock_t	start,
	int		val)
{
	int		bit;
	xfs_rtblock_t	block;
	buf_t		*buf;
	xfs_rtword_t	*bufp;
	xfs_rtword_t	wdiff;
	int		word;
	xfs_rtword_t	wval;

	block = XFS_BITTOBLOCK(mp, start);
	buf = xfs_rtbuf_get(mp, tp, block, 0);
	bufp = (xfs_rtword_t *)buf->b_un.b_addr;
	word = XFS_BITTOWORD(mp, start);
	bit = start & (XFS_NBWORD - 1);
	wval = bufp[word];
	xfs_trans_brelse(tp, buf);
	wdiff = (wval ^ -val) & (1 << bit);
	return !wdiff;
}

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
	int		bit;
	xfs_rtblock_t	block;
	xfs_rtword_t	*bp;
	buf_t		*buf;
	xfs_rtword_t	*bufp;
	xfs_rtblock_t	i;
	xfs_rtblock_t	lastbit;
	xfs_rtword_t	mask;
	xfs_rtword_t	wdiff;
	int		word;

	block = XFS_BITTOBLOCK(mp, start);
	buf = xfs_rtbuf_get(mp, tp, block, 0);
	bufp = (xfs_rtword_t *)buf->b_un.b_addr;
	word = XFS_BITTOWORD(mp, start);
	bp = &bufp[word];
	bit = start & (XFS_NBWORD - 1);
	val = -val;
	if (bit) {
		lastbit = XFS_RTMIN(bit + len, XFS_NBWORD);
		mask = ((1 << (lastbit - bit)) - 1) << bit;
		if (wdiff = (*bp ^ val) & mask) {
			xfs_trans_brelse(tp, buf);
			i = xfs_rtlobit(wdiff) - bit;
			*new = start + i;
			return 0;
		}
		i = lastbit - bit;
		if (++word == XFS_BLOCKWSIZE(mp) && i < len) {
			xfs_trans_brelse(tp, buf);
			buf = xfs_rtbuf_get(mp, tp, ++block, 0);
			bp = bufp = (xfs_rtword_t *)buf->b_un.b_addr;
			word = 0;
		} else
			bp++;
	} else
		i = 0;
	while (len - i >= XFS_NBWORD) {
		if (wdiff = *bp ^ val) {
			xfs_trans_brelse(tp, buf);
			i += xfs_rtlobit(wdiff);
			*new = start + i;
			return 0;
		}
		i += XFS_NBWORD;
		if (++word == XFS_BLOCKWSIZE(mp) && i < len) {
			xfs_trans_brelse(tp, buf);
			buf = xfs_rtbuf_get(mp, tp, ++block, 0);
			bp = bufp = (xfs_rtword_t *)buf->b_un.b_addr;
			word = 0;
		} else
			bp++;
	}
	if (lastbit = len - i) {
		mask = (1 << lastbit) - 1;
		if (wdiff = (*bp ^ val) & mask) {
			xfs_trans_brelse(tp, buf);
			i += xfs_rtlobit(wdiff);
			*new = start + i;
			return 0;
		} else
			i = len;
	}
	xfs_trans_brelse(tp, buf);
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
	int		bit;
	xfs_rtblock_t	block;
	xfs_rtword_t	*bp;
	buf_t		*buf;
	xfs_rtword_t	*bufp;
	xfs_rtblock_t	firstbit;
	xfs_rtblock_t	i;
	xfs_rtblock_t	len;
	xfs_rtword_t	mask;
	xfs_rtword_t	want;
	xfs_rtword_t	wdiff;
	int		word;

	block = XFS_BITTOBLOCK(mp, start);
	buf = xfs_rtbuf_get(mp, tp, block, 0);
	bufp = (xfs_rtword_t *)buf->b_un.b_addr;
	word = XFS_BITTOWORD(mp, start);
	bp = &bufp[word];
	bit = start & (XFS_NBWORD - 1);
	len = start - limit + 1;
	want = (*bp & (1 << bit)) ? -1 : 0;
	if (bit < XFS_NBWORD - 1) {
		firstbit = XFS_RTMAX(bit - len + 1, 0);
		mask = ((1 << (bit - firstbit + 1)) - 1) << firstbit;
		if (wdiff = (*bp ^ want) & mask) {
			xfs_trans_brelse(tp, buf);
			i = bit - xfs_rthibit(wdiff);
			return start - i + 1;
		}
		i = bit - firstbit + 1;
		if (--word == -1 && i < len) {
			xfs_trans_brelse(tp, buf);
			buf = xfs_rtbuf_get(mp, tp, --block, 0);
			bufp = (xfs_rtword_t *)buf->b_un.b_addr;
			word = XFS_BLOCKWMASK(mp);
			bp = &bufp[word];
		} else
			bp--;
	} else
		i = 0;
	while (len - i >= XFS_NBWORD) {
		if (wdiff = *bp ^ want) {
			xfs_trans_brelse(tp, buf);
			i += XFS_NBWORD - 1 - xfs_rthibit(wdiff);
			return start - i + 1;
		}
		i += XFS_NBWORD;
		if (--word == -1 && i < len) {
			xfs_trans_brelse(tp, buf);
			buf = xfs_rtbuf_get(mp, tp, --block, 0);
			bufp = (xfs_rtword_t *)buf->b_un.b_addr;
			word = XFS_BLOCKWMASK(mp);
			bp = &bufp[word];
		} else
			bp--;
	}
	if (len - i) {
		firstbit = XFS_NBWORD - (len - i);
		mask = ((1 << (len - i)) - 1) << firstbit;
		if (wdiff = (*bp ^ want) & mask) {
			xfs_trans_brelse(tp, buf);
			i += XFS_NBWORD - 1 - xfs_rthibit(wdiff);
			return start - i + 1;
		} else
			i = len;
	}
	xfs_trans_brelse(tp, buf);
	return start - i + 1;
}

STATIC xfs_rtblock_t
xfs_rtfind_forw(
	xfs_mount_t	*mp,
	xfs_trans_t	*tp,
	xfs_rtblock_t	start,
	xfs_rtblock_t	limit)
{
	int		bit;
	xfs_rtblock_t	block;
	xfs_rtword_t	*bp;
	buf_t		*buf;
	xfs_rtword_t	*bufp;
	xfs_rtblock_t	i;
	xfs_rtblock_t	lastbit;
	xfs_rtblock_t	len;
	xfs_rtword_t	mask;
	xfs_rtword_t	want;
	xfs_rtword_t	wdiff;
	int		word;

	block = XFS_BITTOBLOCK(mp, start);
	buf = xfs_rtbuf_get(mp, tp, block, 0);
	bufp = (xfs_rtword_t *)buf->b_un.b_addr;
	word = XFS_BITTOWORD(mp, start);
	bp = &bufp[word];
	bit = start & (XFS_NBWORD - 1);
	want = (*bp & (1 << bit)) ? -1 : 0;
	len = limit - start + 1;
	if (bit) {
		lastbit = XFS_RTMIN(bit + len, XFS_NBWORD);
		mask = ((1 << (lastbit - bit)) - 1) << bit;
		if (wdiff = (*bp ^ want) & mask) {
			xfs_trans_brelse(tp, buf);
			i = xfs_rtlobit(wdiff) - bit;
			return start + i - 1;
		}
		i = lastbit - bit;
		if (++word == XFS_BLOCKWSIZE(mp) && i < len) {
			xfs_trans_brelse(tp, buf);
			buf = xfs_rtbuf_get(mp, tp, ++block, 0);
			bp = bufp = (xfs_rtword_t *)buf->b_un.b_addr;
			word = 0;
		} else
			bp++;
	} else
		i = 0;
	while (len - i >= XFS_NBWORD) {
		if (wdiff = *bp ^ want) {
			xfs_trans_brelse(tp, buf);
			i += xfs_rtlobit(wdiff);
			return start + i - 1;
		}
		i += XFS_NBWORD;
		if (++word == XFS_BLOCKWSIZE(mp) && i < len) {
			xfs_trans_brelse(tp, buf);
			buf = xfs_rtbuf_get(mp, tp, ++block, 0);
			bp = bufp = (xfs_rtword_t *)buf->b_un.b_addr;
			word = 0;
		} else
			bp++;
	}
	if (lastbit = len - i) {
		mask = (1 << lastbit) - 1;
		if (wdiff = (*bp ^ want) & mask) {
			xfs_trans_brelse(tp, buf);
			i += xfs_rtlobit(wdiff);
			return start + i - 1;
		} else
			i = len;
	}
	xfs_trans_brelse(tp, buf);
	return start + i - 1;
}

STATIC void
xfs_rtfree_range(
	xfs_trans_t	*tp,
	xfs_rtblock_t	start,
	xfs_extlen_t	len,
	buf_t		**rbuf,
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
		xfs_rtmodify_summary(tp, xfs_rthibit(start - preblock), XFS_BITTOBLOCK(mp, preblock), -1, rbuf, rsb);
	if (postblock > end)
		xfs_rtmodify_summary(tp, xfs_rthibit(postblock - end), XFS_BITTOBLOCK(mp, end + 1), -1, rbuf, rsb);
	xfs_rtmodify_summary(tp, xfs_rthibit(postblock + 1 - preblock), XFS_BITTOBLOCK(mp, preblock), 1, rbuf, rsb);
}

STATIC xfs_suminfo_t
xfs_rtget_summary(
	xfs_mount_t	*mp,
	xfs_trans_t	*tp,
	int		log,
	xfs_rtblock_t	bbno,
	buf_t		**rbuf,
	xfs_fsblock_t	*rsb)
{
	buf_t		*buf;
	xfs_suminfo_t	rval;
	xfs_fsblock_t	sb;
	int		so;
	xfs_suminfo_t	*sp;

	so = XFS_SUMOFFS(mp, log, bbno);
	sb = XFS_SUMOFFSTOBLOCK(mp, so);
	if (rbuf && *rbuf && *rsb == sb)
		buf = *rbuf;
	else {
		if (rbuf && *rbuf)
			xfs_trans_brelse(tp, *rbuf);
		buf = xfs_rtbuf_get(mp, tp, sb, 1);
		if (rbuf) {
			*rbuf = buf;
			*rsb = sb;
		}
	}
	sp = XFS_SUMPTR(mp, buf, so);
	rval = *sp;
	if (!rbuf)
		xfs_trans_brelse(tp, buf);
	return rval;
}

#ifndef SIM
STATIC		/* make this external for mkfs */
#endif
int
xfs_rthibit(
	xfs_rtword_t	w)
{
#define	M(s,l)	(((1L<<(l))-1L)<<(s))

	if (w & M(16,16)) {
		if (w & M(24,8)) {
			if (w & M(28,4)) {
				if (w & M(30,2))
					return w & M(31,1) ? 31 : 30;
				else
					return w & M(29,1) ? 29 : 28;
			} else {
				if (w & M(26,2))
					return w & M(27,1) ? 27 : 26;
				else
					return w & M(25,1) ? 25 : 24;
			}
		} else {
			if (w & M(20,4)) {
				if (w & M(22,2))
					return w & M(23,1) ? 23 : 22;
				else
					return w & M(21,1) ? 21 : 20;
			} else {
				if (w & M(18,2))
					return w & M(19,1) ? 19 : 18;
				else
					return w & M(17,1) ? 17 : 16;
			}
		}
	} else {
		if (w & M(8,8)) {
			if (w & M(12,4)) {
				if (w & M(14,2))
					return w & M(15,1) ? 15 : 14;
				else
					return w & M(13,1) ? 13 : 12;
			} else {
				if (w & M(10,2))
					return w & M(11,1) ? 11 : 10;
				else
					return w & M(9,1) ? 9 : 8;
			}
		} else {
			if (w & M(4,4)) {
				if (w & M(6,2))
					return w & M(7,1) ? 7 : 6;
				else
					return w & M(5,1) ? 5 : 4;
			} else {
				if (w & M(2,2))
					return w & M(3,1) ? 3 : 2;
				else
					return w & M(1,1) ? 1 : 0;
			}
		}
	}
	return -1;
#undef M
}

STATIC int
xfs_rtlobit(
	xfs_rtword_t	w)
{
#define	M(s,l)	(((1L<<(l))-1L)<<(s))

	if (w & M(0,16)) {
		if (w & M(0,8)) {
			if (w & M(0,4)) {
				if (w & M(0,2))
					return w & M(0,1) ? 0 : 1;
				else
					return w & M(2,1) ? 2 : 3;
			} else {
				if (w & M(4,2))
					return w & M(4,1) ? 4 : 5;
				else
					return w & M(6,1) ? 6 : 7;
			}
		} else {
			if (w & M(8,4)) {
				if (w & M(8,2))
					return w & M(8,1) ? 8 : 9;
				else
					return w & M(10,1) ? 10 : 11;
			} else {
				if (w & M(12,2))
					return w & M(12,1) ? 12 : 13;
				else
					return w & M(14,1) ? 14 : 15;
			}
		}
	} else {
		if (w & M(16,8)) {
			if (w & M(16,4)) {
				if (w & M(16,2))
					return w & M(16,1) ? 16 : 17;
				else
					return w & M(18,1) ? 18 : 19;
			} else {
				if (w & M(20,2))
					return w & M(20,1) ? 20 : 21;
				else
					return w & M(22,1) ? 22 : 23;
			}
		} else {
			if (w & M(24,4)) {
				if (w & M(24,2))
					return w & M(24,1) ? 24 : 25;
				else
					return w & M(26,1) ? 26 : 27;
			} else {
				if (w & M(28,2))
					return w & M(28,1) ? 28 : 29;
				else
					return w & M(30,1) ? 30 : 31;
			}
		}
	}
	return -1;
#undef M
}

STATIC void
xfs_rtmodify_range(
	xfs_trans_t	*tp,
	xfs_rtblock_t	start,
	xfs_extlen_t	len,
	int		val)
{
	int		bit;
	xfs_rtblock_t	block;
	int		*bp;
	buf_t		*buf;
	int		*bufp;
	int		*first;
	int		i;
	int		lastbit;
	int		mask;
	xfs_mount_t	*mp;
	int		word;
	int		wval;

	mp = tp->t_mountp;
	block = XFS_BITTOBLOCK(mp, start);
	buf = xfs_rtbuf_get(mp, tp, block, 0);
	bufp = (xfs_rtword_t *)buf->b_un.b_addr;
	word = XFS_BITTOWORD(mp, start);
	first = bp = &bufp[word];
	bit = start & (XFS_NBWORD - 1);
	val = -val;
	if (bit) {
		lastbit = XFS_RTMIN(bit + len, XFS_NBWORD);
		mask = ((1 << (lastbit - bit)) - 1) << bit;
		if (val)
			*bp |= mask;
		else
			*bp &= ~mask;
		i = lastbit - bit;
		if (++word == XFS_BLOCKWSIZE(mp) && i < len) {
			xfs_trans_log_buf(tp, buf, (char *)first - (char *)bufp, (char *)bp - (char *)bufp);
			buf = xfs_rtbuf_get(mp, tp, ++block, 0);
			first = bp = bufp = (xfs_rtword_t *)buf->b_un.b_addr;
			word = 0;
		} else
			bp++;
	} else
		i = 0;
	while (len - i >= XFS_NBWORD) {
		*bp = val;
		i += XFS_NBWORD;
		if (++word == XFS_BLOCKWSIZE(mp) && i < len) {
			xfs_trans_log_buf(tp, buf, (char *)first - (char *)bufp, (char *)bp - (char *)bufp);
			buf = xfs_rtbuf_get(mp, tp, ++block, 0);
			first = bp = bufp = (xfs_rtword_t *)buf->b_un.b_addr;
			word = 0;
		} else
			bp++;
	}
	if (lastbit = len - i) {
		bit = 0;
		mask = (1 << lastbit) - 1;
		if (val)
			*bp |= mask;
		else
			*bp &= ~mask;
		bp++;
	}
	if (bp > first)
		xfs_trans_log_buf(tp, buf, (char *)first - (char *)bufp, (char *)bp - (char *)bufp - 1);
}

STATIC void
xfs_rtmodify_summary(
	xfs_trans_t	*tp,
	int		log,
	xfs_rtblock_t	bbno,
	int		delta,
	buf_t		**rbuf,
	xfs_fsblock_t	*rsb)
{
	buf_t		*buf;
	xfs_mount_t	*mp;
	xfs_fsblock_t	sb;
	int		so;
	xfs_suminfo_t	*sp;

	mp = tp->t_mountp;
	so = XFS_SUMOFFS(mp, log, bbno);
	sb = XFS_SUMOFFSTOBLOCK(mp, so);
	if (rbuf && *rbuf && *rsb == sb)
		buf = *rbuf;
	else {
		if (rbuf && *rbuf)
			xfs_trans_brelse(tp, *rbuf);
		buf = xfs_rtbuf_get(mp, tp, sb, 1);
		if (rbuf) {
			*rbuf = buf;
			*rsb = sb;
		}
	}
	sp = XFS_SUMPTR(mp, buf, so);
	*sp += delta;
	xfs_trans_log_buf(tp, buf, (char *)sp - (char *)buf->b_un.b_addr, (char *)sp - (char *)buf->b_un.b_addr + sizeof(*sp) - 1);
}

/* 
 * Visible (exported) functions.
 */

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
	buf_t		*sumbuf = 0;

	ASSERT(minlen > 0 && minlen <= maxlen);
	mp = tp->t_mountp;
	if (prod > 1) {
		xfs_extlen_t	i;

		if (i = maxlen % prod)
			maxlen -= i;
		if (i = minlen % prod)
			minlen += prod - i;
		if (maxlen < minlen)
			return NULLFSBLOCK;
	}
	xfs_ilock(mp->m_rbmip, XFS_ILOCK_EXCL);
	switch (type) {
	case XFS_ALLOCTYPE_ANY_AG:
		r = xfs_rtallocate_extent_size(tp, minlen, maxlen, len, &sumbuf,
			&sb, prod);
		break;
	case XFS_ALLOCTYPE_NEAR_BNO:
		r = xfs_rtallocate_extent_near(tp, bno, minlen, maxlen, len,
			&sumbuf, &sb, prod);
		break;
	case XFS_ALLOCTYPE_THIS_BNO:
		r = xfs_rtallocate_extent_exact(tp, bno, minlen, maxlen, len,
			&sumbuf, &sb, prod);
		break;
	}
	xfs_iunlock(mp->m_rbmip, XFS_ILOCK_EXCL);
	if (r != NULLFSBLOCK) {
		int slen = (int)*len;

		ASSERT(*len >= minlen && *len <= maxlen);
		if (wasdel)
			xfs_trans_mod_sb(tp, XFS_TRANS_SB_RES_FREXTENTS, -slen);
		else
			xfs_trans_mod_sb(tp, XFS_TRANS_SB_FREXTENTS, -slen);
	}
	return r;
}

void
xfs_rtfree_extent(
	xfs_trans_t	*tp,
	xfs_rtblock_t	bno,
	xfs_extlen_t	len)
{
	xfs_mount_t	*mp;
	xfs_fsblock_t	sb;
	buf_t		*sumbuf = 0;

	mp = tp->t_mountp;
	xfs_ilock(mp->m_rbmip, XFS_ILOCK_EXCL);
	ASSERT(xfs_rtcheck_alloc_range(mp, tp, bno, len));
	xfs_rtfree_range(tp, bno, len, &sumbuf, &sb);
	xfs_iunlock(mp->m_rbmip, XFS_ILOCK_EXCL);
	xfs_trans_mod_sb(tp, XFS_TRANS_SB_FREXTENTS, (int)len);
}

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
	buf_t		*sumbuf = NULL;

	for (l = 0; l < mp->m_rsumlevels; l++) {
		for (p = 0, i = 0; i < mp->m_sb.sb_rbmblocks; i++) {
			if (c = xfs_rtget_summary(mp, tp, l, i, &sumbuf, &sb)) {
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
	if (sumbuf)
		xfs_trans_brelse(tp, sumbuf);
}
