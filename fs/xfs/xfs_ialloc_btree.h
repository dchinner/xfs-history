#ifndef _FS_XFS_IALLOC_BTREE_H
#define	_FS_XFS_IALLOC_BTREE_H

#ident	"$Revision$"

/*
 * Inode map on-disk structures
 */

struct xfs_btree_sblock_t;
struct xfs_btree_cur;

/*
 * There is a btree for the inode map per allocation group.
 */
#define	XFS_IBT_MAGIC	0x49414254	/* 'IABT' */

typedef	__uint64_t	xfs_inofree_t;
#define	XFS_INODES_PER_CHUNK	(NBBY * sizeof(xfs_inofree_t))
#define	XFS_INODES_PER_CHUNK_LOG	(XFS_NBBYLOG + 3)
#define	XFS_INOBT_ALL_FREE	((xfs_inofree_t)-1)

/*
 * Bit manipulations for ir_free.
 */
#define	XFS_INOBT_MASK(i)		((xfs_inofree_t)1 << (i))
#define	XFS_INOBT_IS_FREE(rp, i)	((rp)->ir_free & XFS_INOBT_MASK(i))
#define	XFS_INOBT_SET_FREE(rp, i)	((rp)->ir_free |= XFS_INOBT_MASK(i))
#define	XFS_INOBT_CLR_FREE(rp, i)	((rp)->ir_free &= ~XFS_INOBT_MASK(i))

/*
 * Data record structure
 */
typedef struct xfs_inobt_rec
{
	xfs_agino_t	ir_startino;	/* starting inode number */
	__int32_t	ir_freecount;	/* count of free inodes (set bits) */
	xfs_inofree_t	ir_free;	/* free inode mask */
} xfs_inobt_rec_t;

/*
 * Key structure
 */
typedef struct xfs_inobt_key
{
	xfs_agino_t	ir_startino;	/* starting inode number */
} xfs_inobt_key_t;

typedef xfs_agblock_t xfs_inobt_ptr_t;	/* btree pointer type */
					/* btree block header type */
typedef	struct xfs_btree_sblock xfs_inobt_block_t;

#define	XFS_BUF_TO_INOBT_BLOCK(bp) ((xfs_inobt_block_t *)((bp)->b_un.b_addr))

/*
 * Real block structures have a size equal to the disk block size.
 */

#define	XFS_INOBT_BLOCK_SIZE(lev,cur)	(1 << (cur)->bc_blocklog)

#define	XFS_INOBT_BLOCK_MAXRECS(lev,cur)	\
	((cur)->bc_mp->m_inobt_mxr[lev != 0])
#define	XFS_INOBT_BLOCK_MINRECS(lev,cur)	\
	((cur)->bc_mp->m_inobt_mnr[lev != 0])

/*
 * Maximum number of inode btree levels.
 */
#define	XFS_IN_MAXLEVELS(mp)		((mp)->m_in_maxlevels)

/*
 * block numbers in the AG.
 */
#define	XFS_IBT_BLOCK(mp)	((xfs_agblock_t)(XFS_CNT_BLOCK(mp) + 1))
#define	XFS_PREALLOC_BLOCKS(mp)	((xfs_agblock_t)(XFS_IBT_BLOCK(mp) + 1))

/*
 * Record, key, and pointer address macros for btree blocks.
 */
#define	XFS_INOBT_REC_ADDR(bb,i,cur)	\
	XFS_BTREE_REC_ADDR(XFS_INOBT_BLOCK_SIZE(0,cur), xfs_inobt, bb, i, \
		XFS_INOBT_BLOCK_MAXRECS(0, cur))

#define	XFS_INOBT_KEY_ADDR(bb,i,cur)	\
	XFS_BTREE_KEY_ADDR(XFS_INOBT_BLOCK_SIZE(1,cur), xfs_inobt, bb, i, \
		XFS_INOBT_BLOCK_MAXRECS(1, cur))

#define	XFS_INOBT_PTR_ADDR(bb,i,cur)	\
	XFS_BTREE_PTR_ADDR(XFS_INOBT_BLOCK_SIZE(1,cur), xfs_inobt, bb, i, \
		XFS_INOBT_BLOCK_MAXRECS(1, cur))

/*
 * Prototypes for externally visible routines.
 */

/*
 * Decrement cursor by one record at the level.
 * For nonzero levels the leaf-ward information is untouched.
 */
int					/* success/failure */
xfs_inobt_decrement(
	struct xfs_btree_cur	*cur,	/* btree cursor */
	int			level);	/* level in btree, 0 is leaf */

/*
 * Delete the record pointed to by cur.
 * The cursor refers to the place where the record was (could be inserted)
 * when the operation returns.
 */
int					/* success/failure */
xfs_inobt_delete(
	struct xfs_btree_cur	*cur);	/* btree cursor */

/*
 * Get the data from the pointed-to record.
 */
int					/* success/failure */
xfs_inobt_get_rec(
	struct xfs_btree_cur	*cur,	/* btree cursor */
	xfs_agino_t		*ino,	/* output: starting inode of chunk */
	__int32_t		*fcnt,	/* output: number of free inodes */
	xfs_inofree_t		*free);	/* output: free inode mask */

/*
 * Increment cursor by one record at the level.
 * For nonzero levels the leaf-ward information is untouched.
 */
int					/* success/failure */
xfs_inobt_increment(
	struct xfs_btree_cur	*cur,	/* btree cursor */
	int			level);	/* level in btree, 0 is leaf */

/*
 * Insert the current record at the point referenced by cur.
 * The cursor may be inconsistent on return if splits have been done.
 */
int					/* success/failure */
xfs_inobt_insert(
	struct xfs_btree_cur	*cur);	/* btree cursor */

#ifdef XFSDEBUG
/*
 * Check key consistency in the btree given by cur.
 */
void
xfs_inobt_kcheck(
	struct xfs_btree_cur	*cur);	/* btree cursor */
#else
#define	xfs_inobt_kcheck(a)
#endif

/*
 * Lookup the record equal to ino in the btree given by cur.
 */
int					/* success/failure */
xfs_inobt_lookup_eq(
	struct xfs_btree_cur	*cur,	/* btree cursor */
	xfs_agino_t		ino,	/* starting inode of chunk */
	__int32_t		fcnt,	/* free inode count */
	xfs_inofree_t		free);	/* free inode mask */

/*
 * Lookup the first record greater than or equal to ino
 * in the btree given by cur.
 */
int					/* success/failure */
xfs_inobt_lookup_ge(
	struct xfs_btree_cur	*cur,	/* btree cursor */
	xfs_agino_t		ino,	/* starting inode of chunk */
	__int32_t		fcnt,	/* free inode count */
	xfs_inofree_t		free);	/* free inode mask */

/*
 * Lookup the first record less than or equal to ino
 * in the btree given by cur.
 */
int					/* success/failure */
xfs_inobt_lookup_le(
	struct xfs_btree_cur	*cur,	/* btree cursor */
	xfs_agino_t		ino,	/* starting inode of chunk */
	__int32_t		fcnt,	/* free inode count */
	xfs_inofree_t		free);	/* free inode mask */
 
#ifdef XFSDEBUG
/*
 * Check consistency in the given btree.
 * Checks header consistency and that keys/records are in the right order.
 */
void
xfs_inobt_rcheck(
	struct xfs_btree_cur	*cur);	/* btree cursor */
#else
#define	xfs_inobt_rcheck(a)
#endif	/* XFSDEBUG */

/*
 * Update the record referred to by cur, to the value given
 * by [ino, fcnt, free].
 */
int					/* success/failure */
xfs_inobt_update(
	struct xfs_btree_cur	*cur,	/* btree cursor */
	xfs_agino_t		ino,	/* starting inode of chunk */
	__int32_t		fcnt,	/* free inode count */
	xfs_inofree_t		free);	/* free inode mask */

#endif	/* !_FS_XFS_IALLOC_BTREE_H */
