#ifndef _FS_XFS_ALLOC_BTREE_H
#define	_FS_XFS_ALLOC_BTREE_H

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

struct xfs_btree_cur;

/*
 * Prototypes for externally visible routines.
 */

int
xfs_alloc_decrement(
	struct xfs_btree_cur	*cur,
	int			level);

int
xfs_alloc_delete(
	struct xfs_btree_cur	*cur);

int
xfs_alloc_get_rec(
	struct xfs_btree_cur	*cur,
	xfs_agblock_t		*bno,
	xfs_extlen_t		*len);

int
xfs_alloc_increment(
	struct xfs_btree_cur	*cur,
	int			level);

int
xfs_alloc_insert(
	struct xfs_btree_cur	*cur);

#ifdef XFSDEBUG
/*
 * Check key consistency in the btree given by cur.
 */
void
xfs_alloc_kcheck(
	struct xfs_btree_cur	*cur);		/* btree cursor */
#else
#define	xfs_alloc_kcheck(a)
#endif

int
xfs_alloc_lookup_eq(
	struct xfs_btree_cur	*cur,
	xfs_agblock_t		bno,
	xfs_extlen_t		len);

int
xfs_alloc_lookup_ge(
	struct xfs_btree_cur	*cur,
	xfs_agblock_t		bno,
	xfs_extlen_t		len);

int
xfs_alloc_lookup_le(
	struct xfs_btree_cur	*cur,
	xfs_agblock_t		bno,
	xfs_extlen_t		len);
 
#ifdef XFSDEBUG
/*
 * Check consistency in the given btree.
 * Checks header consistency and that keys/records are in the right order.
 */
void
xfs_alloc_rcheck(
	struct xfs_btree_cur	*cur);		/* btree cursor */
#else
#define	xfs_alloc_rcheck(a)
#endif	/* XFSDEBUG */

int
xfs_alloc_update(
	struct xfs_btree_cur	*cur,
	xfs_agblock_t		bno,
	xfs_extlen_t		len);

#endif	/* !_FS_XFS_ALLOC_BTREE_H */
