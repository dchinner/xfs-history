#ifndef _FS_XFS_ALLOC_H
#define	_FS_XFS_ALLOC_H

#ident	"$Revision$"

/*
 * Freespace allocation types.  Argument to xfs_alloc_[v]extent.
 * Nonsense with double declaration is for -wlint.
 */
typedef enum xfs_alloctype
{
	XFS_ALLOCTYPE_ANY_AGi,		/* allocate anywhere */
	XFS_ALLOCTYPE_START_AGi,	/* anywhere, start in this a.g. */
	XFS_ALLOCTYPE_THIS_AGi,		/* anywhere in this a.g. */
	XFS_ALLOCTYPE_START_BNOi,	/* near this block else anywhere */
	XFS_ALLOCTYPE_NEAR_BNOi,	/* in this a.g. and near this block */
	XFS_ALLOCTYPE_THIS_BNOi		/* at exactly this block */
} xfs_alloctype_t;
#define	XFS_ALLOCTYPE_ANY_AG	((xfs_alloctype_t)XFS_ALLOCTYPE_ANY_AGi)
#define	XFS_ALLOCTYPE_START_AG	((xfs_alloctype_t)XFS_ALLOCTYPE_START_AGi)
#define	XFS_ALLOCTYPE_THIS_AG	((xfs_alloctype_t)XFS_ALLOCTYPE_THIS_AGi)
#define	XFS_ALLOCTYPE_START_BNO	((xfs_alloctype_t)XFS_ALLOCTYPE_START_BNOi)
#define	XFS_ALLOCTYPE_NEAR_BNO	((xfs_alloctype_t)XFS_ALLOCTYPE_NEAR_BNOi)
#define	XFS_ALLOCTYPE_THIS_BNO	((xfs_alloctype_t)XFS_ALLOCTYPE_THIS_BNOi)

/*
 * Flags for xfs_alloc_ag_freeblks, xfs_alloc_fix_freelist
 */
#define	XFS_ALLOC_FLAG_TRYLOCK	0x00000001  /* use trylock for buffer locking */

/*
 * Prototypes for visible xfs_alloc.c routines
 */

/*
 * Return the number of free blocks left in the allocation group.
 */
xfs_extlen_t				/* number of remaining free blocks */
xfs_alloc_ag_freeblks(
	xfs_mount_t	*mp,		/* file system mount structure */
	xfs_trans_t	*tp,		/* transaction pointer */
	xfs_agnumber_t	agno,		/* allocation group number */
	int		flags);		/* XFS_ALLOC_FLAG_... */

/*
 * Allocate an extent (fixed-size).
 * Return the extent's starting block, NULLFSBLOCK on failure.
 */
xfs_fsblock_t				/* extent's starting block */
xfs_alloc_extent(
	xfs_trans_t	*tp,		/* transaction pointer */
	xfs_fsblock_t	bno,		/* requested starting block */
	xfs_extlen_t	len,		/* requested length */
	xfs_alloctype_t	type,		/* allocation type, see above defn */
	xfs_extlen_t	total,		/* total blocks needed in transaction */
	int		wasdel);	/* extent was previously del-alloced */

/*
 * Decide whether to use this allocation group for this allocation.
 * If so, fix up the btree freelist's size.
 * This is external so mkfs can call it, too.
 */
buf_t *				/* buffer for the a.g. freelist header */
xfs_alloc_fix_freelist(
	xfs_trans_t	*tp,	/* transaction pointer */
	xfs_agnumber_t	agno,	/* allocation group number */
	xfs_extlen_t	minlen,	/* minimum length extent needed now */
	xfs_extlen_t	total,	/* maximum blocks needed during transaction */
	int		flags);	/* XFS_ALLOC_FLAG_... */

/*
 * Get a block from the freelist.
 * Returns with the buffer for the block gotten.
 */
xfs_agblock_t			/* block address retrieved from freelist */
xfs_alloc_get_freelist(
	xfs_trans_t	*tp,	/* transaction pointer */
	buf_t		*agbuf,	/* buffer containing the agf structure */
	buf_t		**bufp);/* out: buffer pointer for the free block */

/*
 * Log the given fields from the agf structure.
 */
void
xfs_alloc_log_agf(
	xfs_trans_t	*tp,	/* transaction pointer */
	buf_t		*buf,	/* buffer for a.g. freelist header */
	int		fields);/* mask of fields to be logged (XFS_AGF_...) */

/*
 * Find the next freelist block number.
 */
xfs_agblock_t			/* a.g.-relative block number for btree list */
xfs_alloc_next_free(
	xfs_mount_t	*mp,	/* file system mount structure */
	xfs_trans_t	*tp,	/* transaction pointer */
	buf_t		*agbuf,	/* buffer for a.g. freelist header */
	xfs_agblock_t	bno);	/* current freelist block number */

/*
 * Put the buffer on the freelist for the allocation group.
 */
void
xfs_alloc_put_freelist(
	xfs_trans_t	*tp,	/* transaction pointer */
	buf_t		*agbuf,	/* buffer for a.g. freelist header */
	buf_t		*buf);	/* buffer for the block being freed */

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
	xfs_extlen_t	prod);	/* there's nothing as big as mod */

/*
 * Free an extent.
 */
int				/* success/failure; will become void */
xfs_free_extent(
	xfs_trans_t	*tp,	/* transaction pointer */
	xfs_fsblock_t	bno,	/* starting block number of extent */
	xfs_extlen_t	len);	/* length of extent */

#endif	/* !_FS_XFS_ALLOC_H */
