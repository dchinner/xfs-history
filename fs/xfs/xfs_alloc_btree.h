#ifndef _FS_XFS_ALLOC_BTREE_H
#define	_FS_XFS_ALLOC_BTREE_H

#ident	"$Revision: 1.8 $"

/*
 * Freespace on-disk structures
 */

struct xfs_btree_sblock_t;
struct xfs_btree_cur;

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
					/* btree block header type */
typedef	struct xfs_btree_sblock xfs_alloc_block_t;

#define	XFS_BUF_TO_ALLOC_BLOCK(bp) ((xfs_alloc_block_t *)((bp)->b_un.b_addr))

/*
 * Real block structures have a size equal to the disk block size.
 */

#define	XFS_ALLOC_BLOCK_SIZE(lev,cur)	(1 << (cur)->bc_blocklog)

#define	XFS_ALLOC_BLOCK_MAXRECS(lev,cur)	\
	((cur)->bc_mp->m_alloc_mxr[lev != 0])
#define	XFS_ALLOC_BLOCK_MINRECS(lev,cur)	\
	((cur)->bc_mp->m_alloc_mnr[lev != 0])

/*
 * Minimum and maximum blocksize.
 * The blocksize upper limit is pretty much arbitrary.
 */
#define	XFS_MIN_BLOCKSIZE_LOG	9	/* i.e. 512 bytes */
#define	XFS_MAX_BLOCKSIZE_LOG	16	/* i.e. 65536 bytes */
#define	XFS_MIN_BLOCKSIZE	(1 << XFS_MIN_BLOCKSIZE_LOG)
#define	XFS_MAX_BLOCKSIZE	(1 << XFS_MAX_BLOCKSIZE_LOG)

/*
 * block numbers in the AG; SB is BB 0, AGF is BB 1, AGI is BB 2, AGFL is BB 3
 */
#define	XFS_BNO_BLOCK(mp)	((xfs_agblock_t)(XFS_AGFL_BLOCK(mp) + 1))
#define	XFS_CNT_BLOCK(mp)	((xfs_agblock_t)(XFS_BNO_BLOCK(mp) + 1))

/*
 * Record, key, and pointer address macros for btree blocks.
 */
#define	XFS_ALLOC_REC_ADDR(bb,i,cur)	\
	XFS_BTREE_REC_ADDR(XFS_ALLOC_BLOCK_SIZE(0,cur), xfs_alloc, bb, i, \
		XFS_ALLOC_BLOCK_MAXRECS(0, cur))

#define	XFS_ALLOC_KEY_ADDR(bb,i,cur)	\
	XFS_BTREE_KEY_ADDR(XFS_ALLOC_BLOCK_SIZE(1,cur), xfs_alloc, bb, i, \
		XFS_ALLOC_BLOCK_MAXRECS(1, cur))

#define	XFS_ALLOC_PTR_ADDR(bb,i,cur)	\
	XFS_BTREE_PTR_ADDR(XFS_ALLOC_BLOCK_SIZE(1,cur), xfs_alloc, bb, i, \
		XFS_ALLOC_BLOCK_MAXRECS(1, cur))

/*
 * Prototypes for externally visible routines.
 */

/*
 * Decrement cursor by one record at the level.
 * For nonzero levels the leaf-ward information is untouched.
 */
int					/* success/failure */
xfs_alloc_decrement(
	struct xfs_btree_cur	*cur,	/* btree cursor */
	int			level);	/* level in btree, 0 is leaf */

/*
 * Delete the record pointed to by cur.
 * The cursor refers to the place where the record was (could be inserted)
 * when the operation returns.
 */
int					/* success/failure */
xfs_alloc_delete(
	struct xfs_btree_cur	*cur);	/* btree cursor */

/*
 * Get the data from the pointed-to record.
 */
int					/* success/failure */
xfs_alloc_get_rec(
	struct xfs_btree_cur	*cur,	/* btree cursor */
	xfs_agblock_t		*bno,	/* output: starting block of extent */
	xfs_extlen_t		*len);	/* output: length of extent */

/*
 * Increment cursor by one record at the level.
 * For nonzero levels the leaf-ward information is untouched.
 */
int					/* success/failure */
xfs_alloc_increment(
	struct xfs_btree_cur	*cur,	/* btree cursor */
	int			level);	/* level in btree, 0 is leaf */

/*
 * Insert the current record at the point referenced by cur.
 * The cursor may be inconsistent on return if splits have been done.
 */
int					/* success/failure */
xfs_alloc_insert(
	struct xfs_btree_cur	*cur);	/* btree cursor */

#ifdef XFSDEBUG
/*
 * Check key consistency in the btree given by cur.
 */
void
xfs_alloc_kcheck(
	struct xfs_btree_cur	*cur);	/* btree cursor */
#else
#define	xfs_alloc_kcheck(a)
#endif

/*
 * Lookup the record equal to [bno, len] in the btree given by cur.
 */
int					/* success/failure */
xfs_alloc_lookup_eq(
	struct xfs_btree_cur	*cur,	/* btree cursor */
	xfs_agblock_t		bno,	/* starting block of extent */
	xfs_extlen_t		len);	/* length of extent */

/*
 * Lookup the first record greater than or equal to [bno, len]
 * in the btree given by cur.
 */
int					/* success/failure */
xfs_alloc_lookup_ge(
	struct xfs_btree_cur	*cur,	/* btree cursor */
	xfs_agblock_t		bno,	/* starting block of extent */
	xfs_extlen_t		len);	/* length of extent */

/*
 * Lookup the first record less than or equal to [bno, len]
 * in the btree given by cur.
 */
int					/* success/failure */
xfs_alloc_lookup_le(
	struct xfs_btree_cur	*cur,	/* btree cursor */
	xfs_agblock_t		bno,	/* starting block of extent */
	xfs_extlen_t		len);	/* length of extent */
 
#ifdef XFSDEBUG
/*
 * Check consistency in the given btree.
 * Checks header consistency and that keys/records are in the right order.
 */
void
xfs_alloc_rcheck(
	struct xfs_btree_cur	*cur);	/* btree cursor */
#else
#define	xfs_alloc_rcheck(a)
#endif	/* XFSDEBUG */

/*
 * Update the record referred to by cur, to the value given by [bno, len].
 */
int					/* success/failure */
xfs_alloc_update(
	struct xfs_btree_cur	*cur,	/* btree cursor */
	xfs_agblock_t		bno,	/* starting block of extent */
	xfs_extlen_t		len);	/* length of extent */

#endif	/* !_FS_XFS_ALLOC_BTREE_H */
