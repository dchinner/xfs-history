#ifndef _FS_XFS_RTALLOC_H
#define	_FS_XFS_RTALLOC_H

#ident	"$Revision$"

/* Min and max rt extent sizes, specified in bytes */
#define	XFS_MAX_RTEXTSIZE	(16 * 1024 * 1024)	/* 16MB */
#define	XFS_MIN_RTEXTSIZE	(64 * 1024)		/* 64KB */

/*
 * Constants for bit manipulations.
 */
#define	XFS_NBBYLOG	3		/* log2(NBBY) */
#define	XFS_WORDLOG	2		/* log2(sizeof(xfs_rtword_t)) */
#define	XFS_NBWORDLOG	(XFS_NBBYLOG + XFS_WORDLOG)
#define	XFS_NBWORD	(1 << XFS_NBWORDLOG)
#define	XFS_WORDMASK	((1 << XFS_WORDLOG) - 1)

#define	XFS_BLOCKSIZE(sbp)	(1 << (sbp)->sb_blocklog)
#define	XFS_BLOCKMASK(sbp)	(XFS_BLOCKSIZE(sbp) - 1)
#define	XFS_BLOCKWSIZE(sbp)	(1 << ((sbp)->sb_blocklog - XFS_WORDLOG))
#define	XFS_BLOCKWMASK(sbp)	(XFS_BLOCKWSIZE(sbp) - 1)

/*
 * Summary and bit manipulation macros.
 */
#define	XFS_SUMOFFS(sbp,ls,bb)	((ls) + (sbp)->sb_rbmblocks * (bb))
#define	XFS_SUMOFFSTOBLOCK(sbp,s)	\
	(((s) * sizeof(xfs_suminfo_t)) >> (sbp)->sb_blocklog)
#define	XFS_SUMPTR(sbp,bp,so)	\
	((xfs_suminfo_t *)((char *)bp->b_un.b_addr + (((so) * sizeof(xfs_suminfo_t)) & XFS_BLOCKMASK(sbp))))

#define	XFS_BITTOBLOCK(sbp,bi)	((bi) >> (sbp->sb_blocklog + XFS_NBBYLOG))
#define	XFS_BLOCKTOBIT(sbp,bb)	((bb) << (sbp->sb_blocklog + XFS_NBBYLOG))
#define	XFS_BITTOWORD(sbp,bi)	(((bi) >> XFS_NBWORDLOG) & XFS_BLOCKWMASK(sbp))

#define	XFS_RTMIN(a,b)	((a) < (b) ? (a) : (b))
#define	XFS_RTMAX(a,b)	((a) > (b) ? (a) : (b))

/*
 * Function prototypes for exported functions.
 */

xfs_rtblock_t
xfs_rtallocate_extent(
	xfs_mount_t	*mp,
	xfs_trans_t	*tp,
	xfs_rtblock_t	bno,
	xfs_extlen_t	minlen,
	xfs_extlen_t	maxlen,
	xfs_extlen_t	*len,
	xfs_alloctype_t	type,
	int		wasdel);

void
xfs_rtfree_extent(
	xfs_mount_t	*mp,
	xfs_trans_t	*tp,
	xfs_rtblock_t	bno,
	xfs_extlen_t	len);

#ifdef SIM
int
xfs_rthibit(
	xfs_rtword_t	w);
#endif

void
xfs_rtprint_range(
	xfs_mount_t	*mp,
	xfs_trans_t	*tp,
	xfs_rtbit_t	start,
	xfs_rtbit_t	len);

void
xfs_rtprint_summary(
	xfs_mount_t	*mp,
	xfs_trans_t	*tp);

#endif	/* !_FS_XFS_RTALLOC_H */
