#ifndef _FS_XFS_RTALLOC_H
#define	_FS_XFS_RTALLOC_H

#ident	"$Revision$"

/* Min and max rt extent sizes, specified in bytes */
#define	XFS_MAX_RTEXTSIZE	(1024 * 1024 * 1024)	/* 1GB */
#define	XFS_MIN_RTEXTSIZE	(64 * 1024)		/* 64KB */

/*
 * Constants for bit manipulations.
 */
#define	XFS_NBBYLOG	3		/* log2(NBBY) */
#if XFS_RTWORD_LL
#define	XFS_WORDLOG	3		/* log2(sizeof(xfs_rtword_t)) */
#else
#define	XFS_WORDLOG	2		/* log2(sizeof(xfs_rtword_t)) */
#endif
#define	XFS_NBWORDLOG	(XFS_NBBYLOG + XFS_WORDLOG)
#define	XFS_NBWORD	(1 << XFS_NBWORDLOG)
#define	XFS_WORDMASK	((1 << XFS_WORDLOG) - 1)

#define	XFS_BLOCKSIZE(mp)	((mp)->m_sb.sb_blocksize)
#define	XFS_BLOCKMASK(mp)	((mp)->m_blockmask)
#define	XFS_BLOCKWSIZE(mp)	((mp)->m_blockwsize)
#define	XFS_BLOCKWMASK(mp)	((mp)->m_blockwmask)

/*
 * Summary and bit manipulation macros.
 */
#define	XFS_SUMOFFS(mp,ls,bb)	((ls) * (mp)->m_sb.sb_rbmblocks + (bb))
#define	XFS_SUMOFFSTOBLOCK(mp,s)	\
	(((s) * sizeof(xfs_suminfo_t)) >> (mp)->m_sb.sb_blocklog)
#define	XFS_SUMPTR(mp,bp,so)	\
	((xfs_suminfo_t *)((char *)bp->b_un.b_addr + \
		(((so) * sizeof(xfs_suminfo_t)) & XFS_BLOCKMASK(mp))))

#define	XFS_BITTOBLOCK(mp,bi)	((bi) >> (mp)->m_blkbit_log)
#define	XFS_BLOCKTOBIT(mp,bb)	((bb) << (mp)->m_blkbit_log)
#define	XFS_BITTOWORD(mp,bi)	(((bi) >> XFS_NBWORDLOG) & XFS_BLOCKWMASK(mp))

#define	XFS_RTMIN(a,b)	((a) < (b) ? (a) : (b))
#define	XFS_RTMAX(a,b)	((a) > (b) ? (a) : (b))

#if XFS_RTWORD_LL
#define	XFS_RTLOBIT(w)	xfs_lowbit64(w)
#define	XFS_RTHIBIT(w)	xfs_highbit64(w)
#else
#define	XFS_RTLOBIT(w)	xfs_lowbit32(w)
#define	XFS_RTHIBIT(w)	xfs_highbit32(w)
#endif

#if XFS_BIG_FILESYSTEMS
#define	XFS_RTBLOCKLOG(b)	xfs_highbit64(b)
#else
#define	XFS_RTBLOCKLOG(b)	xfs_highbit32(b)
#endif

/*
 * Function prototypes for exported functions.
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
	xfs_extlen_t	prod);
#endif	/* !SIM */

void
xfs_rtfree_extent(
	xfs_trans_t	*tp,
	xfs_rtblock_t	bno,
	xfs_extlen_t	len);

#ifdef XFSDEBUG
void
xfs_rtprint_range(
	xfs_mount_t	*mp,
	xfs_trans_t	*tp,
	xfs_rtblock_t	start,
	xfs_extlen_t	len);

void
xfs_rtprint_summary(
	xfs_mount_t	*mp,
	xfs_trans_t	*tp);
#endif	/* XFSDEBUG */

#endif	/* !_FS_XFS_RTALLOC_H */
