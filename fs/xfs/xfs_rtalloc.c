#ident	"$Revision: 1.27 $"

/*
 * Free realtime space allocation for XFS.
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
#include "xfs_attr_sf.h"
#include "xfs_dir_sf.h"
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
STATIC int
xfs_rtallocate_extent_block(
	xfs_trans_t	*tp,
	xfs_rtblock_t	bbno,
	xfs_extlen_t	minlen,
	xfs_extlen_t	maxlen,
	xfs_extlen_t	*len,
	xfs_rtblock_t	*nextp,
	buf_t		**rbpp,
	xfs_fsblock_t	*rsb,
	xfs_extlen_t	prod,
	xfs_rtblock_t	*rtblock);

STATIC int
xfs_rtallocate_extent_exact(
	xfs_trans_t	*tp,
	xfs_rtblock_t	bno,
	xfs_extlen_t	minlen,
	xfs_extlen_t	maxlen,
	xfs_extlen_t	*len,
	buf_t		**rbpp,
	xfs_fsblock_t	*rsb,
	xfs_extlen_t	prod,
	xfs_rtblock_t	*rtblock);


STATIC int
xfs_rtallocate_extent_near(
	xfs_trans_t	*tp,
	xfs_rtblock_t	bno,
	xfs_extlen_t	minlen,
	xfs_extlen_t	maxlen,
	xfs_extlen_t	*len,
	buf_t		**rbpp,
	xfs_fsblock_t	*rsb,
	xfs_extlen_t	prod,
	xfs_rtblock_t	*rtblock);

STATIC int
xfs_rtallocate_extent_size(
	xfs_trans_t	*tp,
	xfs_extlen_t	minlen,
	xfs_extlen_t	maxlen,
	xfs_extlen_t	*len,
	buf_t		**rbpp,
	xfs_fsblock_t	*rsb,
	xfs_extlen_t	prod,
	xfs_rtblock_t	*rtblock);

STATIC int
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
	xfs_fsblock_t	*rsb,
	int		*stat);
#endif	/* !SIM */

STATIC int
xfs_rtbuf_get(
	xfs_mount_t	*mp,
	xfs_trans_t	*tp,
	xfs_rtblock_t	block,
	int		issum,
	buf_t		**bpp);

#ifdef DEBUG
STATIC int
xfs_rtcheck_alloc_range(
	xfs_mount_t	*mp,
	xfs_trans_t	*tp,
	xfs_rtblock_t	bno,
	xfs_extlen_t	len,
	int		*stat);
#endif

#ifdef XFSDEBUG
STATIC int
xfs_rtcheck_bit(
	xfs_mount_t	*mp,
	xfs_trans_t	*tp,
	xfs_rtblock_t	start,
	int		val);
#endif	/* XFSDEBUG */

#if 0
STATIC int
xfs_rtcheck_free_range(
	xfs_mount_t	*mp,
	xfs_trans_t	*tp,
	xfs_rtblock_t	bno,
	xfs_extlen_t	len,
	int		*stat);
#endif

STATIC int
xfs_rtcheck_range(
	xfs_mount_t	*mp,
	xfs_trans_t	*tp,
	xfs_rtblock_t	start,
	xfs_extlen_t	len,
	int		val,
	xfs_rtblock_t	*new,
	int		*stat);

STATIC int
xfs_rtfind_back(
	xfs_mount_t	*mp,
	xfs_trans_t	*tp,
	xfs_rtblock_t	start,
	xfs_rtblock_t	limit,
	xfs_rtblock_t	*rtblock);

STATIC int
xfs_rtfind_forw(
	xfs_mount_t	*mp,
	xfs_trans_t	*tp,
	xfs_rtblock_t	start,
	xfs_rtblock_t	limit,
	xfs_rtblock_t	*rtblock);

STATIC int
xfs_rtfree_range(
	xfs_trans_t	*tp,
	xfs_rtblock_t	start,
	xfs_extlen_t	len,
	buf_t		**rbpp,
	xfs_fsblock_t	*rsb);

#if defined(XFSDEBUG) || !defined(SIM)
STATIC int
xfs_rtget_summary(
	xfs_mount_t	*mp,
	xfs_trans_t	*tp,
	int		log,
	xfs_rtblock_t	bbno,
	buf_t		**rbpp,
	xfs_fsblock_t	*rsb,
	xfs_suminfo_t	*sum);
#endif	/* XFSDEBUG || !SIM */

STATIC int
xfs_rtmodify_range(
	xfs_trans_t	*tp,
	xfs_rtblock_t	start,
	xfs_extlen_t	len,
	int		val);

STATIC int
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
STATIC int				/* error */
xfs_rtallocate_extent_block(
	xfs_trans_t	*tp,
	xfs_rtblock_t	bbno,
	xfs_extlen_t	minlen,
	xfs_extlen_t	maxlen,
	xfs_extlen_t	*len,
	xfs_rtblock_t	*nextp,
	buf_t		**rbpp,
	xfs_fsblock_t	*rsb,
	xfs_extlen_t	prod,
	xfs_rtblock_t	*rtblock)
{
	xfs_rtblock_t	base;
	xfs_rtblock_t	besti = -1;
	xfs_rtblock_t	bestlen;
	xfs_rtblock_t	end;
	int		error;
	xfs_rtblock_t	i;
	xfs_mount_t	*mp;
	xfs_rtblock_t	next;
	int		stat;

	ASSERT(minlen % prod == 0 && maxlen % prod == 0);
	mp = tp->t_mountp;
	base = XFS_BLOCKTOBIT(mp, bbno);
	end = XFS_BLOCKTOBIT(mp, bbno + 1) - 1;
	for (i = base; i <= end; i++) {
		error = xfs_rtcheck_range(mp, tp, i, maxlen, 1, &next, &stat);
		if (error) {
			return error;
		}
		if (stat) {
			error = xfs_rtallocate_range(tp, i, maxlen, rbpp, rsb);
			if (error) {
				return error;
			}
			*len = maxlen;
			*rtblock = i;
			return 0;
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
		if (next < end) {
			error = xfs_rtfind_forw(mp, tp, next, end, &i);
			if (error) {
				return error;
			}
		} else
			break;
	}
	if (minlen < maxlen && besti != -1) {
		xfs_extlen_t	p;

		if (prod > 1 && (p = bestlen % prod))
			bestlen -= p;
		error = xfs_rtallocate_range(tp, besti, bestlen, rbpp, rsb);
		if (error) {
			return error;
		}
		*len = bestlen;
		*rtblock = besti;
		return 0;
	}
	*nextp = next;
	*rtblock = NULLRTBLOCK;
	return 0;
}

STATIC int				/* error */
xfs_rtallocate_extent_exact(
	xfs_trans_t	*tp,
	xfs_rtblock_t	bno,
	xfs_extlen_t	minlen,
	xfs_extlen_t	maxlen,
	xfs_extlen_t	*len,
	buf_t		**rbpp,
	xfs_fsblock_t	*rsb,
	xfs_extlen_t	prod,
	xfs_rtblock_t	*rtblock)
{
	xfs_rtblock_t	base;
	int		error;
	xfs_extlen_t	i;
	xfs_mount_t	*mp;
	xfs_rtblock_t	next;
	int		stat;

	ASSERT(minlen % prod == 0 && maxlen % prod == 0);
	mp = tp->t_mountp;
	base = bno;
	error = xfs_rtcheck_range(mp, tp, base, maxlen, 1, &next, &stat);
	if (error) {
		return error;
	}
	if (stat) {
		error = xfs_rtallocate_range(tp, base, maxlen, rbpp, rsb);
		if (error) {
			return error;
		}
		*len = maxlen;
		*rtblock = bno;
		return 0;
	}
	maxlen = next - base;
	if (maxlen < minlen) {
		*rtblock = NULLRTBLOCK;
		return 0;
	}
	if (prod > 1 && (i = maxlen % prod)) {
		maxlen -= i;
		if (maxlen < minlen) {
			*rtblock = NULLRTBLOCK;
			return 0;
		}
	}
	error = xfs_rtallocate_range(tp, base, maxlen, rbpp, rsb);
	if (error) {
		return error;
	}
	*len = maxlen;
	*rtblock = bno;
	return 0;
}

STATIC int				/* error */
xfs_rtallocate_extent_near(
	xfs_trans_t	*tp,
	xfs_rtblock_t	bno,
	xfs_extlen_t	minlen,
	xfs_extlen_t	maxlen,
	xfs_extlen_t	*len,
	buf_t		**rbpp,
	xfs_fsblock_t	*rsb,
	xfs_extlen_t	prod,
	xfs_rtblock_t	*rtblock)
{
	xfs_rtblock_t	bbno;
	int		error;
	int		i;
	int		j;
	int		log2len;
	xfs_mount_t	*mp;
	xfs_rtblock_t	n;
	xfs_rtblock_t	r;
	int		stat;

	ASSERT(minlen % prod == 0 && maxlen % prod == 0);
	mp = tp->t_mountp;
	if (bno >= mp->m_sb.sb_rextents)
		bno = mp->m_sb.sb_rextents - 1;
	error = xfs_rtallocate_extent_exact(tp, bno, minlen, maxlen, len,
			rbpp, rsb, prod, &r);
	if (error) {
		return error;
	}
	if (r != NULLRTBLOCK) {
		*rtblock = r;
		return 0;
	}
	bbno = XFS_BITTOBLOCK(mp, bno);
	i = 0;
	log2len = xfs_highbit32(minlen);
	while (1) {
		error = xfs_rtany_summary(mp, tp, log2len,
				mp->m_rsumlevels - 1, bbno + i, rbpp, rsb,
				&stat);
		if (error) {
			return error;
		}
		if (stat) {
			if (i >= 0) {
				error = xfs_rtallocate_extent_block(tp,
						bbno + i, minlen, maxlen,
						len, &n, rbpp, rsb, prod, &r);
				if (error) {
					return error;
				}
				if (r != NULLRTBLOCK) {
					*rtblock = r;
					return 0;
				}
			} else if (i < 0) {
				for (j = -1; j >= i; j--) {
					error = xfs_rtany_summary(mp, tp, log2len, mp->m_rsumlevels - 1, bbno + j, rbpp, rsb, &stat);
					if (error) {
						return error;
					}
					if (!stat) {
						error = xfs_rtallocate_extent_block(tp, bbno + j, minlen, maxlen, len, &n, rbpp, rsb, prod, &r);
						if (error) {
							return error;
						}
						if (r != NULLRTBLOCK) {
							*rtblock = r;
							return 0;
						}
					}
					error = xfs_rtallocate_extent_block(tp, bbno + i, minlen, maxlen, len, &n, rbpp, rsb, prod, &r);
					if (error) {
						return error;
					}
					if (r != NULLRTBLOCK) {
						*rtblock = r;
						return 0;
					}
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
	*rtblock = NULLRTBLOCK;
	return 0;
}

STATIC int				/* error */
xfs_rtallocate_extent_size(
	xfs_trans_t	*tp,
	xfs_extlen_t	minlen,
	xfs_extlen_t	maxlen,
	xfs_extlen_t	*len,
	buf_t		**rbpp,
	xfs_fsblock_t	*rsb,
	xfs_extlen_t	prod,
	xfs_rtblock_t	*rtblock)
{
	int		error;
	int		i;
	int		l;
	xfs_mount_t	*mp;
	xfs_rtblock_t	n;
	xfs_rtblock_t	r;
	int		stat;

	ASSERT(minlen % prod == 0 && maxlen % prod == 0);
	mp = tp->t_mountp;
	for (l = xfs_highbit32(maxlen); l < mp->m_rsumlevels; l++) {
		for (i = 0; i < mp->m_sb.sb_rbmblocks; i++) {
			error = xfs_rtget_summary(mp, tp, l, i, rbpp, rsb,
						  &stat);
			if (error) {
				return error;
			}
			if (!stat)
				continue;
			error = xfs_rtallocate_extent_block(tp, i, maxlen,
					maxlen, len, &n, rbpp, rsb, prod, &r);
			if (error) {
				return error;
			}
			if (r != NULLRTBLOCK) {
				*rtblock = r;
				return 0;
			}
			if (XFS_BITTOBLOCK(mp, n) > i + 1)
				i = XFS_BITTOBLOCK(mp, n) - 1;
		}
	}
	if (minlen > --maxlen) {
		*rtblock = NULLRTBLOCK;
		return 0;
	}
	for (l = xfs_highbit32(maxlen); l >= xfs_highbit32(minlen); l--) {
		for (i = 0; i < mp->m_sb.sb_rbmblocks; i++) {
			error =	xfs_rtget_summary(mp, tp, l, i, rbpp, rsb,
						  &stat);
			if (error) {
				return error;
			}
			if (!stat)
				continue;
			error = xfs_rtallocate_extent_block(tp, i,
					XFS_RTMAX(minlen, 1 << l),
					XFS_RTMIN(maxlen, (1 << (l + 1)) - 1),
					len, &n, rbpp, rsb, prod, &r);
			if (error) {
				return error;
			}
			if (r != NULLRTBLOCK) {
				*rtblock = r;
				return 0;
			}
			if (XFS_BITTOBLOCK(mp, n) > i + 1)
				i = XFS_BITTOBLOCK(mp, n) - 1;
		}
	}
	*rtblock = NULLRTBLOCK;
	return 0;
}

STATIC int				/* error */
xfs_rtallocate_range(
	xfs_trans_t	*tp,
	xfs_rtblock_t	start,
	xfs_extlen_t	len,
	buf_t		**rbpp,
	xfs_fsblock_t	*rsb)
{
	xfs_rtblock_t	end;
	int		error;
	xfs_mount_t	*mp;
	xfs_rtblock_t	postblock;
	xfs_rtblock_t	preblock;

	mp = tp->t_mountp;
	end = start + len - 1;
	error = xfs_rtfind_back(mp, tp, start, 0, &preblock);
	if (error) {
		return error;
	}
	error = xfs_rtfind_forw(mp, tp, end, mp->m_sb.sb_rextents - 1, &postblock);
	if (error) {
		return error;
	}
	error = xfs_rtmodify_summary(tp, XFS_RTBLOCKLOG(postblock + 1 - preblock), XFS_BITTOBLOCK(mp, preblock), -1, rbpp, rsb);
	if (error) {
		return error;
	}
	if (preblock < start) {
		error = xfs_rtmodify_summary(tp, XFS_RTBLOCKLOG(start - preblock), XFS_BITTOBLOCK(mp, preblock), 1, rbpp, rsb);
		if (error) {
			return error;
		}
	}
	if (postblock > end) {
		error = xfs_rtmodify_summary(tp, XFS_RTBLOCKLOG(postblock - end), XFS_BITTOBLOCK(mp, end + 1), 1, rbpp, rsb);
		if (error) {
			return error;
		}
	}
	error = xfs_rtmodify_range(tp, start, len, 0);
	return error;
}

STATIC int				/* error */
xfs_rtany_summary(
	xfs_mount_t	*mp,
	xfs_trans_t	*tp,
	int		low,
	int		high,
	xfs_rtblock_t	bbno,
	buf_t		**rbpp,
	xfs_fsblock_t	*rsb,
	int		*stat)
{
	int		error;
	int		log;

	for (log = low; log <= high; log++) {
		error = xfs_rtget_summary(mp, tp, log, bbno, rbpp, rsb, stat);
		if (error) {
			return error;
		}
		if (*stat) {
			return 0;
		}
	}
	ASSERT(*stat == 0);
	return 0;
}
#endif	/* !SIM */

STATIC int				/* error */
xfs_rtbuf_get(
	xfs_mount_t	*mp,
	xfs_trans_t	*tp,
	xfs_rtblock_t	block,
	int		issum,
	buf_t		**bpp)
{
	buf_t		*bp;
	daddr_t		d;
	int		error;
	xfs_fsblock_t	firstblock;
	xfs_bmap_free_t	*flist;
	xfs_inode_t	*ip;
	xfs_bmbt_irec_t	map;
	int		nmap;

	nmap = 1;
	flist = NULL;
	ip = issum ? mp->m_rsumip : mp->m_rbmip;
	firstblock = NULLFSBLOCK;
	error = xfs_bmapi(tp, ip, block, 1, 0, &firstblock, 0, &map, &nmap,
			  flist);
	if (error) {
		return error;
	}
	ASSERT(nmap == 1);
	ASSERT(flist == NULL);
	d = XFS_FSB_TO_DADDR(mp, map.br_startblock);
	error = xfs_trans_read_buf(tp, mp->m_dev, d, mp->m_bsize, 0, &bp);
	if (error) {
		return error;
	}
	ASSERT(bp && !geterror(bp));
	*bpp = bp;
	return 0;
}

#ifdef DEBUG
STATIC int				/* error */
xfs_rtcheck_alloc_range(
	xfs_mount_t	*mp,
	xfs_trans_t	*tp,
	xfs_rtblock_t	bno,
	xfs_extlen_t	len,
	int		*stat)
{
	xfs_rtblock_t	new;

	return xfs_rtcheck_range(mp, tp, bno, len, 0, &new, stat);
}
#endif

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

#if 0
STATIC int				/* error */
xfs_rtcheck_free_range(
	xfs_mount_t	*mp,
	xfs_trans_t	*tp,
	xfs_rtblock_t	bno,
	xfs_extlen_t	len,
	int		*stat)
{
	xfs_rtblock_t	new;

	return xfs_rtcheck_range(mp, tp, bno, len, 1, &new, stat);
}
#endif

STATIC int				/* error */
xfs_rtcheck_range(
	xfs_mount_t	*mp,
	xfs_trans_t	*tp,
	xfs_rtblock_t	start,
	xfs_extlen_t	len,
	int		val,
	xfs_rtblock_t	*new,
	int		*stat)
{
	xfs_rtword_t	*b;
	int		bit;
	xfs_rtblock_t	block;
	buf_t		*bp;
	xfs_rtword_t	*bufp;
	int		error;
	xfs_rtblock_t	i;
	xfs_rtblock_t	lastbit;
	xfs_rtword_t	mask;
	xfs_rtword_t	wdiff;
	int		word;

	block = XFS_BITTOBLOCK(mp, start);
	error = xfs_rtbuf_get(mp, tp, block, 0, &bp);
	if (error) {
		return error;
	}
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
			*stat = 0;
			return 0;
		}
		i = lastbit - bit;
		if (++word == XFS_BLOCKWSIZE(mp) && i < len) {
			xfs_trans_brelse(tp, bp);
			error = xfs_rtbuf_get(mp, tp, ++block, 0, &bp);
			if (error) {
				return error;
			}
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
			*stat = 0;
			return 0;
		}
		i += XFS_NBWORD;
		if (++word == XFS_BLOCKWSIZE(mp) && i < len) {
			xfs_trans_brelse(tp, bp);
			error = xfs_rtbuf_get(mp, tp, ++block, 0, &bp);
			if (error) {
				return error;
			}
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
			*stat = 0;
			return 0;
		} else
			i = len;
	}
	xfs_trans_brelse(tp, bp);
	*new = start + i;
	*stat = 1;
	return 0;
}

STATIC int				/* error */
xfs_rtfind_back(
	xfs_mount_t	*mp,
	xfs_trans_t	*tp,
	xfs_rtblock_t	start,
	xfs_rtblock_t	limit,
	xfs_rtblock_t	*rtblock)
{
	xfs_rtword_t	*b;
	int		bit;
	xfs_rtblock_t	block;
	buf_t		*bp;
	xfs_rtword_t	*bufp;
	int		error;
	xfs_rtblock_t	firstbit;
	xfs_rtblock_t	i;
	xfs_rtblock_t	len;
	xfs_rtword_t	mask;
	xfs_rtword_t	want;
	xfs_rtword_t	wdiff;
	int		word;

	block = XFS_BITTOBLOCK(mp, start);
	error = xfs_rtbuf_get(mp, tp, block, 0, &bp);
	if (error) {
		return error;
	}
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
			*rtblock = start - i + 1;
			return 0;
		}
		i = bit - firstbit + 1;
		if (--word == -1 && i < len) {
			xfs_trans_brelse(tp, bp);
			error = xfs_rtbuf_get(mp, tp, --block, 0, &bp);
			if (error) {
				return error;
			}
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
			*rtblock = start - i + 1;
			return 0;
		}
		i += XFS_NBWORD;
		if (--word == -1 && i < len) {
			xfs_trans_brelse(tp, bp);
			error = xfs_rtbuf_get(mp, tp, --block, 0, &bp);
			if (error) {
				return error;
			}
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
			*rtblock = start - i + 1;
			return 0;
		} else
			i = len;
	}
	xfs_trans_brelse(tp, bp);
	*rtblock = start - i + 1;
	return 0;
}

STATIC int				/* error */
xfs_rtfind_forw(
	xfs_mount_t	*mp,
	xfs_trans_t	*tp,
	xfs_rtblock_t	start,
	xfs_rtblock_t	limit,
	xfs_rtblock_t	*rtblock)		
{
	xfs_rtword_t	*b;
	int		bit;
	xfs_rtblock_t	block;
	buf_t		*bp;
	xfs_rtword_t	*bufp;
	int		error;
	xfs_rtblock_t	i;
	xfs_rtblock_t	lastbit;
	xfs_rtblock_t	len;
	xfs_rtword_t	mask;
	xfs_rtword_t	want;
	xfs_rtword_t	wdiff;
	int		word;

	block = XFS_BITTOBLOCK(mp, start);
	error = xfs_rtbuf_get(mp, tp, block, 0, &bp);
	if (error) {
		return error;
	}
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
			*rtblock = start + i - 1;
			return 0;
		}
		i = lastbit - bit;
		if (++word == XFS_BLOCKWSIZE(mp) && i < len) {
			xfs_trans_brelse(tp, bp);
			error = xfs_rtbuf_get(mp, tp, ++block, 0, &bp);
			if (error) {
				return error;
			}
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
			*rtblock = start + i - 1;
			return 0;
		}
		i += XFS_NBWORD;
		if (++word == XFS_BLOCKWSIZE(mp) && i < len) {
			xfs_trans_brelse(tp, bp);
			error = xfs_rtbuf_get(mp, tp, ++block, 0, &bp);
			if (error) {
				return error;
			}
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
			*rtblock = start + i - 1;
			return 0;
		} else
			i = len;
	}
	xfs_trans_brelse(tp, bp);
	*rtblock = start + i - 1;
	return 0;
}

STATIC int				/* error */
xfs_rtfree_range(
	xfs_trans_t	*tp,
	xfs_rtblock_t	start,
	xfs_extlen_t	len,
	buf_t		**rbpp,
	xfs_fsblock_t	*rsb)
{
	xfs_rtblock_t	end;
	int		error;
	xfs_mount_t	*mp;
	xfs_rtblock_t	postblock;
	xfs_rtblock_t	preblock;

	mp = tp->t_mountp;
	end = start + len - 1;
	error = xfs_rtmodify_range(tp, start, len, 1);
	if (error) {
		return error;
	}
	error = xfs_rtfind_back(mp, tp, start, 0, &preblock);
	if (error) {
		return error;
	}
	error = xfs_rtfind_forw(mp, tp, end, mp->m_sb.sb_rextents - 1, &postblock);
	if (preblock < start) {
		error = xfs_rtmodify_summary(tp, XFS_RTBLOCKLOG(start - preblock), XFS_BITTOBLOCK(mp, preblock), -1, rbpp, rsb);
		if (error) {
			return error;
		}
	}
	if (postblock > end) {
		error = xfs_rtmodify_summary(tp, XFS_RTBLOCKLOG(postblock - end), XFS_BITTOBLOCK(mp, end + 1), -1, rbpp, rsb);
		if (error) {
			return error;
		}
	}
	error = xfs_rtmodify_summary(tp, XFS_RTBLOCKLOG(postblock + 1 - preblock), XFS_BITTOBLOCK(mp, preblock), 1, rbpp, rsb);
	return error;
}

#if defined(XFSDEBUG) || !defined(SIM)
STATIC int				/* error */
xfs_rtget_summary(
	xfs_mount_t	*mp,
	xfs_trans_t	*tp,
	int		log,
	xfs_rtblock_t	bbno,
	buf_t		**rbpp,
	xfs_fsblock_t	*rsb,
	xfs_suminfo_t	*sum)
{
	buf_t		*bp;
	int		error;
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
		error = xfs_rtbuf_get(mp, tp, sb, 1, &bp);
		if (error) {
			return error;
		}
		if (rbpp) {
			*rbpp = bp;
			*rsb = sb;
		}
	}
	sp = XFS_SUMPTR(mp, bp, so);
	*sum = *sp;
	if (!rbpp)
		xfs_trans_brelse(tp, bp);
	return 0;
}
#endif	/* XFSDEBUG || !SIM */

STATIC int				/* error */
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
	int		error;
	xfs_rtword_t	*first;
	int		i;
	int		lastbit;
	xfs_rtword_t	mask;
	xfs_mount_t	*mp;
	int		word;

	mp = tp->t_mountp;
	block = XFS_BITTOBLOCK(mp, start);
	error = xfs_rtbuf_get(mp, tp, block, 0, &bp);
	if (error) {
		return error;
	}
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
			error = xfs_rtbuf_get(mp, tp, ++block, 0, &bp);
			if (error) {
				return error;
			}
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
			error = xfs_rtbuf_get(mp, tp, ++block, 0, &bp);
			if (error) {
				return error;
			}
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
	return 0;
}

STATIC int				/* error */
xfs_rtmodify_summary(
	xfs_trans_t	*tp,
	int		log,
	xfs_rtblock_t	bbno,
	int		delta,
	buf_t		**rbpp,
	xfs_fsblock_t	*rsb)
{
	buf_t		*bp;
	int		error;
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
		error = xfs_rtbuf_get(mp, tp, sb, 1, &bp);
		if (error) {
			return error;
		}
		if (rbpp) {
			*rbpp = bp;
			*rsb = sb;
		}
	}
	sp = XFS_SUMPTR(mp, bp, so);
	*sp += delta;
	xfs_trans_log_buf(tp, bp, (char *)sp - (char *)bp->b_un.b_addr, (char *)sp - (char *)bp->b_un.b_addr + sizeof(*sp) - 1);
	return 0;
}

/* 
 * Visible (exported) functions.
 */

#ifndef SIM
int					/* error */
xfs_rtallocate_extent(
	xfs_trans_t	*tp,
	xfs_rtblock_t	bno,
	xfs_extlen_t	minlen,
	xfs_extlen_t	maxlen,
	xfs_extlen_t	*len,
	xfs_alloctype_t	type,
	int		wasdel,
	xfs_extlen_t	prod,
	xfs_rtblock_t	*rtblock)
{
	int		error;
	xfs_inode_t	*ip;
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
		if (maxlen < minlen) {
			*rtblock = NULLRTBLOCK;
			return 0;
		}
	}
	error = xfs_trans_iget(mp, tp, mp->m_sb.sb_rbmino, XFS_ILOCK_EXCL, &ip);
	if (error) {
		return error;
	}
	switch (type) {
	case XFS_ALLOCTYPE_ANY_AG:
		error = xfs_rtallocate_extent_size(tp, minlen, maxlen, len,
				&sumbp,	&sb, prod, &r);
		break;
	case XFS_ALLOCTYPE_NEAR_BNO:
		error = xfs_rtallocate_extent_near(tp, bno, minlen, maxlen,
				len, &sumbp, &sb, prod, &r);
		break;
	case XFS_ALLOCTYPE_THIS_BNO:
		error = xfs_rtallocate_extent_exact(tp, bno, minlen, maxlen,
				len, &sumbp, &sb, prod, &r);
		break;
	}
	if (error) {
		return error;
	}
	if (r != NULLRTBLOCK) {
		int slen = (int)*len;

		ASSERT(*len >= minlen && *len <= maxlen);
		if (wasdel)
			xfs_trans_mod_sb(tp, XFS_TRANS_SB_RES_FREXTENTS, -slen);
		else
			xfs_trans_mod_sb(tp, XFS_TRANS_SB_FREXTENTS, -slen);
	}
	*rtblock = r;
	return 0;
}
#endif	/* !SIM */

int					/* error */
xfs_rtfree_extent(
	xfs_trans_t	*tp,
	xfs_rtblock_t	bno,
	xfs_extlen_t	len)
{
	int		error;
	xfs_inode_t	*ip;
	xfs_mount_t	*mp;
	xfs_fsblock_t	sb;
#ifdef DEBUG
	int		stat;
#endif
	buf_t		*sumbp = 0;

	mp = tp->t_mountp;
	error = xfs_trans_iget(mp, tp, mp->m_sb.sb_rbmino, XFS_ILOCK_EXCL, &ip);
	if (error) {
		return error;
	}
#ifdef DEBUG
	error = xfs_rtcheck_alloc_range(mp, tp, bno, len, &stat);
	if (error) {
		return error;
	}
	ASSERT(stat);
#endif
	error = xfs_rtfree_range(tp, bno, len, &sumbp, &sb);
	if (error) {
		return error;
	}
	xfs_trans_mod_sb(tp, XFS_TRANS_SB_FREXTENTS, (int)len);
	return 0;
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
