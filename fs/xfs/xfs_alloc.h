#ifndef _FS_XFS_ALLOC_H
#define	_FS_XFS_ALLOC_H

#ident	"$Revision$"

/*
 * Freespace on-disk structures
 */

/*
 * There are two on-disk btrees, one sorted by blockno and one sorted
 * by blockcount and blockno.  All blocks look the same to make the code
 * simpler; if we have time later, we'll make the optimizations.
 */
#define	XFS_ABTB_MAGIC	0x41425442	/* 'ABTB' for bno tree */
#define	XFS_ABTC_MAGIC	0x41425443	/* 'ABTC' for cnt tree */

/*
 * Data record/key structure
 */
typedef struct xfs_alloc_rec
{
	xfs_agblock_t	ar_startblock;	/* starting block number */
	xfs_extlen_t	ar_blockcount;	/* count of free blocks */
} xfs_alloc_rec_t, xfs_alloc_key_t;

typedef xfs_agblock_t xfs_alloc_ptr_t;	/* btree pointer type */

/*
 * Real block structures have a size equal to the disk block size.
 */

#define	XFS_ALLOC_BLOCK_SIZE(lev,cur)	(1 << (cur)->bc_blocklog)

#define	XFS_ALLOC_BLOCK_MAXRECS(lev,cur)	\
	XFS_BTREE_BLOCK_MAXRECS(XFS_ALLOC_BLOCK_SIZE(lev,cur), xfs_alloc, lev)
#define	XFS_ALLOC_BLOCK_MINRECS(lev,cur)	\
	XFS_BTREE_BLOCK_MINRECS(XFS_ALLOC_BLOCK_SIZE(lev,cur), xfs_alloc, lev)

#define	XFS_MIN_BLOCKSIZE_LOG	9	/* i.e. 512 bytes */
#define	XFS_MAX_BLOCKSIZE_LOG	16	/* i.e. 65536 bytes */
#define	XFS_MIN_BLOCKSIZE	(1 << XFS_MIN_BLOCKSIZE_LOG)
#define	XFS_MAX_BLOCKSIZE	(1 << XFS_MAX_BLOCKSIZE_LOG)

/* block numbers in the AG; SB is BB 0, AGF is BB 1, AGI is BB 2 */
#define	XFS_BNO_BLOCK(s)	((xfs_agblock_t)(XFS_AGI_BLOCK(s) + 1))
#define	XFS_CNT_BLOCK(s)	((xfs_agblock_t)(XFS_BNO_BLOCK(s) + 1))
#define	XFS_PREALLOC_BLOCKS(s)	((xfs_agblock_t)(XFS_CNT_BLOCK(s) + 1))

/*
 * Record, key, and pointer address macros for btree blocks.
 */
#define	XFS_ALLOC_REC_ADDR(bb,i,cur)	\
	XFS_BTREE_REC_ADDR(XFS_ALLOC_BLOCK_SIZE((bb)->bb_level,cur), \
			   xfs_alloc, bb, i)

#define	XFS_ALLOC_KEY_ADDR(bb,i,cur)	\
	XFS_BTREE_KEY_ADDR(XFS_ALLOC_BLOCK_SIZE((bb)->bb_level,cur), \
			   xfs_alloc, bb, i)

#define	XFS_ALLOC_PTR_ADDR(bb,i,cur)	\
	XFS_BTREE_PTR_ADDR(XFS_ALLOC_BLOCK_SIZE((bb)->bb_level,cur), \
			   xfs_alloc, bb, i)
 
/*
 * Freespace allocation types.  Argument to xfs_alloc_[v]extent.
 */
typedef enum xfs_alloctype
{
	XFS_ALLOCTYPE_ANY_AG,	/* allocate anywhere */
	XFS_ALLOCTYPE_START_AG,	/* anywhere, start in this a.g. */
	XFS_ALLOCTYPE_THIS_AG,	/* anywhere in this a.g. */
	XFS_ALLOCTYPE_START_BNO,/* near this block (in a.g.) else anywhere */
	XFS_ALLOCTYPE_NEAR_BNO,	/* in this a.g. and near this block */
	XFS_ALLOCTYPE_THIS_BNO	/* at exactly this block */
} xfs_alloctype_t;

/*
 * Flags for xfs_alloc_ag_freeblks, xfs_alloc_fix_freelist
 */
#define	XFS_ALLOC_FLAG_TRYLOCK	0x1	/* use trylock for buffer locking */

/*
 * Prototypes for visible xfs_alloc.c routines
 */

/*
 * Return the number of free blocks left in the allocation group.
 */
xfs_extlen_t			/* number of remaining free blocks */
xfs_alloc_ag_freeblks(
	xfs_mount_t	*mp,	/* file system mount structure */
	xfs_trans_t	*tp,	/* transaction pointer */
	xfs_agnumber_t	agno,	/* allocation group number */
	int		flags);	/* XFS_ALLOC_FLAG_... */

/*
 * Allocate an extent (fixed-size).
 */
xfs_fsblock_t			/* extent's starting block, or NULLFSBLOCK */
xfs_alloc_extent(
	xfs_trans_t	*tp,	/* transaction pointer */
	xfs_fsblock_t	bno,	/* requested starting block */
	xfs_extlen_t	len,	/* requested length */
	xfs_alloctype_t	type,	/* allocation type, see above definition */
	xfs_extlen_t	total);	/* total blocks needed in transaction */

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
 * Find the next freelist block number.
 */
xfs_agblock_t			/* a.g.-relative block number for btree list */
xfs_alloc_next_free(
	xfs_mount_t	*mp,	/* file system mount structure */
	xfs_trans_t	*tp,	/* transaction pointer */
	buf_t		*agbuf,	/* buffer for a.g. freelist header */
	xfs_agblock_t	bno);	/* current freelist block number */

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
	xfs_extlen_t	total);	/* total blocks needed in transaction */

/*
 * Free an extent.
 */
int				/* success/failure; will become void */
xfs_free_extent(
	xfs_trans_t	*tp,	/* transaction pointer */
	xfs_fsblock_t	bno,	/* starting block number of extent */
	xfs_extlen_t	len);	/* length of extent */

#endif	/* !_FS_XFS_ALLOC_H */

