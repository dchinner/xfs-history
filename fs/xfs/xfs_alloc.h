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
} xfs_alloc_rec_t;

/*
 * Real structures have a size equal to the disk block size.
 */

#define	XFS_ALLOC_BLOCK_SIZE(lev,cur)	(1 << (cur)->bc_blocklog)

#define	XFS_ALLOC_BLOCK_MAXRECS(lev,cur)	\
	XFS_BTREE_BLOCK_MAXRECS(XFS_ALLOC_BLOCK_SIZE(lev,cur), \
				xfs_alloc_rec_t, lev)
#define	XFS_ALLOC_BLOCK_MINRECS(lev,cur)	\
	XFS_BTREE_BLOCK_MINRECS(XFS_ALLOC_BLOCK_SIZE(lev,cur), \
				xfs_alloc_rec_t, lev)

#define	XFS_MIN_BLOCKSIZE_LOG	9
#define	XFS_MAX_BLOCKSIZE_LOG	16
#define	XFS_MIN_BLOCKSIZE	(1 << XFS_MIN_BLOCKSIZE_LOG)
#define	XFS_MAX_BLOCKSIZE	(1 << XFS_MAX_BLOCKSIZE_LOG)

/* block numbers in the AG; SB is block 0, AGH is block 1 */
#define	XFS_BNO_BLOCK	((xfs_agblock_t)(XFS_AGH_BLOCK + 1))
#define	XFS_CNT_BLOCK	((xfs_agblock_t)(XFS_BNO_BLOCK + 1))
#define	XFS_PREALLOC_BLOCKS	((xfs_agblock_t)(XFS_CNT_BLOCK + 1))

#define	XFS_ALLOC_REC_ADDR(bb,i,cur)	\
	XFS_BTREE_REC_ADDR(XFS_ALLOC_BLOCK_SIZE((bb)->bb_level,cur), \
			   xfs_alloc_rec_t, bb, i)

#define	XFS_ALLOC_PTR_ADDR(bb,i,cur)	\
	XFS_BTREE_PTR_ADDR(XFS_ALLOC_BLOCK_SIZE((bb)->bb_level,cur), \
			   xfs_alloc_rec_t, bb, i)

typedef enum xfs_alloctype
{
	XFS_ALLOCTYPE_ANY_AG,
	XFS_ALLOCTYPE_START_AG,
	XFS_ALLOCTYPE_THIS_AG,
	XFS_ALLOCTYPE_NEAR_BNO,
	XFS_ALLOCTYPE_THIS_BNO
} xfs_alloctype_t;

/*
 * Prototypes for per-ag allocation routines
 */
xfs_agblock_t xfs_alloc_ag_extent(xfs_trans_t *, buf_t *, xfs_agnumber_t, xfs_agblock_t, xfs_extlen_t, xfs_alloctype_t);
xfs_agblock_t xfs_alloc_ag_vextent(xfs_trans_t *, buf_t *, xfs_agnumber_t, xfs_agblock_t, xfs_extlen_t, xfs_extlen_t, xfs_extlen_t *, xfs_alloctype_t);
buf_t *xfs_alloc_fix_freelist(xfs_trans_t *, xfs_agnumber_t);
int xfs_free_ag_extent(xfs_trans_t *, buf_t *, xfs_agnumber_t, xfs_agblock_t, xfs_extlen_t);

/*
 * Prototypes for per-fs allocation routines
 */
xfs_fsblock_t xfs_alloc_extent(xfs_trans_t *, xfs_fsblock_t, xfs_extlen_t, xfs_alloctype_t);
xfs_agblock_t xfs_alloc_next_free(xfs_mount_t *, xfs_trans_t *, buf_t *, xfs_agblock_t);
xfs_fsblock_t xfs_alloc_vextent(xfs_trans_t *, xfs_fsblock_t, xfs_extlen_t, xfs_extlen_t, xfs_extlen_t *, xfs_alloctype_t);
int xfs_free_extent(xfs_trans_t *, xfs_fsblock_t, xfs_extlen_t);

#endif	/* !_FS_XFS_ALLOC_H */
