#ifndef _FS_XFS_BMAP_H
#define	_FS_XFS_BMAP_H

#ident "$Revision$"

#define	XFS_BMAP_MAGIC	0x424d4150	/* 'BMAP' */

/*
 * Bmap extent descriptor.
 */
typedef struct xfs_extdesc
{
	xfs_fsblock_t	ed_startoff;	/* starting file offset */
	xfs_fsblock_t	ed_startblock;	/* starting block number */
	xfs_extlen_t	ed_blockcount;	/* number of blocks */
} xfs_extdesc_t;

/*
 * Extent list for inodes.
 */
typedef struct xfs_bmx
{
	__uint16_t	dix_count;	/* number of extents */
	xfs_extdesc_t	dix_ed[1];	/* extent descriptors */
} xfs_bmx_t;

/*
 * Bmap btree record.
 */
typedef struct xfs_bmbt_rec
{
	/* should be off_t if it were 64 bits, fix soon */
	xfs_fsblock_t	br_startoff;	/* starting file offset */
	xfs_fsblock_t	br_startblock;	/* starting block number */
	xfs_extlen_t	br_blockcount;	/* number of blocks */
} xfs_bmbt_rec_t;

#define	XFS_BMAP_RBLOCK_SIZE(bl,lev,cur) ((cur)->bc_private.b.inodesize - offsetof(xfs_dinode_t, di_u))
#define	XFS_BMAP_IBLOCK_SIZE(bl,lev,cur) (1 << (bl))
#define	XFS_BMAP_BLOCK_SIZE(bl,lev,cur) ((lev) == (cur)->bc_nlevels - 1 ? XFS_BMAP_RBLOCK_SIZE(bl,lev,cur) : XFS_BMAP_IBLOCK_SIZE(bl,lev,cur))

#define	XFS_BMAP_BLOCK_MAXRECS(bl,lev,cur) XFS_BTREE_BLOCK_MAXRECS(XFS_BMAP_BLOCK_SIZE(bl,lev,cur), xfs_bmbt_rec_t, lev)
#define	XFS_BMAP_BLOCK_MINRECS(bl,lev,cur) XFS_BTREE_BLOCK_MINRECS(XFS_BMAP_BLOCK_SIZE(bl,lev,cur), xfs_bmbt_rec_t, lev)

#define	XFS_BMAP_REC_ADDR(bb,i,bl,cur) XFS_BTREE_REC_ADDR(XFS_BMAP_BLOCK_SIZE(bl,(bb)->bb_level,cur), xfs_bmbt_rec_t, bb, i)

#define	XFS_BMAP_PTR_ADDR(bb,i,bl,cur) XFS_BTREE_PTR_ADDR(XFS_BMAP_BLOCK_SIZE(bl,(bb)->bb_level,cur), xfs_bmbt_rec_t, bb, i)

#define	XFS_BMAP_MAXLEVELS	5	/* ??? */

#define	xfs_bmbt_get_block(cur, level)	\
	((level) < (cur)->bc_nlevels - 1 ? \
	 xfs_buf_to_block((cur)->bc_bufs[level]) : \
	 &(cur)->bc_private.b.ip->i_d.di_u.di_bmbt)

#endif	/* _FS_XFS_BMAP_H */
