#ifndef _FS_XFS_BMAP_H
#define	_FS_XFS_BMAP_H

#ident "$Revision: 1.6 $"

#define	XFS_BMAP_MAGIC	0x424d4150	/* 'BMAP' */

/*
 * Bmap btree record and extent descriptor.
 */
typedef struct xfs_bmbt_rec
{
	__uint32_t	l0, l1, l2, l3;
} xfs_bmbt_rec_t;

/*
 * l0:0-31 and l1:9-31 are startoff.
 * l1:0-8, l2:0-31, and l3:21-31 are startblock.
 * l3:0-20 are blockcount.
 */
#define	xfs_bmbt_get_startoff(r)	\
	((((xfs_fsblock_t)((r)->l0)) << 23) | \
	 (((xfs_fsblock_t)((r)->l1)) >> 9))
#define	xfs_bmbt_get_startblock(r)	\
	((((xfs_fsblock_t)((r)->l1 & 0x000001ff)) << 43) | \
	 (((xfs_fsblock_t)((r)->l2)) << 11) | \
	 (((xfs_fsblock_t)((r)->l3)) >> 21))
#define	xfs_bmbt_get_blockcount(r)	\
	((xfs_extlen_t)((r)->l3 & 0x001fffff))
#define	xfs_bmbt_get_all(r,s)	\
	(((s).br_startoff = xfs_bmbt_get_startoff(r)), \
	 ((s).br_startblock = xfs_bmbt_get_startblock(r)), \
	 ((s).br_blockcount = xfs_bmbt_get_blockcount(r)))
#define	xfs_bmbt_set_startoff(r,v)	\
	(((r)->l0 = (__uint32_t)((v) >> 23)), \
	 ((r)->l1 = ((r)->l1 & 0x000001ff) | (((__uint32_t)(v)) << 9)))
#define	xfs_bmbt_set_startblock(r,v)	\
	(((r)->l1 = ((r)->l1 & 0xfffffe00) | (__uint32_t)((v) >> 43)), \
	 ((r)->l2 = (__uint32_t)((v) >> 11)), \
	 ((r)->l3 = ((r)->l3 & 0x001fffff) | (((__uint32_t)(v)) << 21)))
#define	xfs_bmbt_set_blockcount(r,v)	\
	((r)->l3 = ((r)->l3 & 0xffe00000) | ((__uint32_t)(v) & 0x001fffff))
#define	xfs_bmbt_set_all(r,s)	\
	(((r)->l0 = (__uint32_t)((s).br_startoff >> 23)), \
	 ((r)->l1 = ((((__uint32_t)(s).br_startoff) << 9) | \
		    ((__uint32_t)((s).br_startblock >> 43)))), \
	 ((r)->l2 = (__uint32_t)((s).br_startblock >> 11)), \
	 ((r)->l3 = ((((__uint32_t)(s).br_startblock) << 21) | \
		    ((__uint32_t)((s).br_blockcount & 0x001fffff)))))

/*
 * Incore version of above.
 */
typedef struct xfs_bmbt_irec
{
	xfs_fsblock_t	br_startoff;	/* starting file offset */
	xfs_fsblock_t	br_startblock;	/* starting block number */
	xfs_extlen_t	br_blockcount;	/* number of block */
} xfs_bmbt_irec_t;

#define	XFS_BMAP_RBLOCK_DSIZE(lev,cur) ((cur)->bc_private.b.inodesize - (int)sizeof(xfs_dinode_core_t))
#define	XFS_BMAP_RBLOCK_ISIZE(lev,cur) ((int)(cur)->bc_private.b.ip->i_broot_bytes)
#define	XFS_BMAP_IBLOCK_SIZE(lev,cur) (1 << (cur)->bc_blocklog)
#define	XFS_BMAP_BLOCK_DSIZE(lev,cur) ((lev) == (cur)->bc_nlevels - 1 ? XFS_BMAP_RBLOCK_DSIZE(lev,cur) : XFS_BMAP_IBLOCK_SIZE(lev,cur))
#define	XFS_BMAP_BLOCK_ISIZE(lev,cur) ((lev) == (cur)->bc_nlevels - 1 ? XFS_BMAP_RBLOCK_ISIZE(lev,cur) : XFS_BMAP_IBLOCK_SIZE(lev,cur))

#define	XFS_BMAP_BLOCK_DMAXRECS(lev,cur) XFS_BTREE_BLOCK_MAXRECS(XFS_BMAP_BLOCK_DSIZE(lev,cur), xfs_bmbt_rec_t, lev)
#define	XFS_BMAP_BLOCK_IMAXRECS(lev,cur) XFS_BTREE_BLOCK_MAXRECS(XFS_BMAP_BLOCK_ISIZE(lev,cur), xfs_bmbt_rec_t, lev)
#define	XFS_BMAP_BLOCK_DMINRECS(lev,cur) XFS_BTREE_BLOCK_MINRECS(XFS_BMAP_BLOCK_DSIZE(lev,cur), xfs_bmbt_rec_t, lev)
#define	XFS_BMAP_BLOCK_IMINRECS(lev,cur) XFS_BTREE_BLOCK_MINRECS(XFS_BMAP_BLOCK_ISIZE(lev,cur), xfs_bmbt_rec_t, lev)

#define	XFS_BMAP_REC_DADDR(bb,i,cur) XFS_BTREE_REC_ADDR(XFS_BMAP_BLOCK_DSIZE((bb)->bb_level,cur), xfs_bmbt_rec_t, bb, i)
#define	XFS_BMAP_REC_IADDR(bb,i,cur) XFS_BTREE_REC_ADDR(XFS_BMAP_BLOCK_ISIZE((bb)->bb_level,cur), xfs_bmbt_rec_t, bb, i)

#define	XFS_BMAP_PTR_DADDR(bb,i,cur) XFS_BTREE_PTR_ADDR(XFS_BMAP_BLOCK_DSIZE((bb)->bb_level,cur), xfs_bmbt_rec_t, bb, i)
#define	XFS_BMAP_PTR_IADDR(bb,i,cur) XFS_BTREE_PTR_ADDR(XFS_BMAP_BLOCK_ISIZE((bb)->bb_level,cur), xfs_bmbt_rec_t, bb, i)

/*
 * These are to be used when we know the size of the block and
 * we don't have a cursor.
 */
#define	XFS_BMAP_BROOT_SIZE(isz) ((isz) - sizeof(xfs_dinode_core_t)) 
#define	XFS_BMAP_BROOT_REC_ADDR(bb,i,isz) XFS_BTREE_REC_ADDR(isz,xfs_bmbt_rec_t,bb,i)
#define XFS_BMAP_BROOT_PTR_ADDR(bb,i,isz) XFS_BTREE_ROOT_PTR_ADDR(isz,xfs_bmbt_rec_t,bb,i)

#define	XFS_BMAP_BROOT_NUMRECS(bb) ((bb)->bb_numrecs)
#define	XFS_BMAP_BROOT_MAXRECS(sz) XFS_BTREE_BLOCK_MAXRECS(sz,xfs_bmbt_rec_t,1)
#define	XFS_BMAP_BROOT_SPACE(bb) ((int)(sizeof(xfs_btree_block_t) + ((bb)->bb_numrecs * (sizeof(xfs_bmbt_rec_t) + sizeof(xfs_agblock_t)))))
#define	XFS_BMAP_BROOT_SPACE_CALC(nrecs) ((int)(sizeof(xfs_btree_block_t) + ((nrecs) * (sizeof(xfs_bmbt_rec_t) + sizeof(xfs_agblock_t)))))

/*
 * Number of extent records that fit in the inode.
 */
#define	XFS_BMAP_EXT_MAXRECS(isz)	\
	(XFS_BMAP_BROOT_SIZE(isz) / sizeof(xfs_bmbt_rec_t))

#define	XFS_BMAP_MAXLEVELS	5	/* ??? */
#define	XFS_BMAP_MAX_NMAP	4

#define	xfs_bmbt_get_block(cur, level, bpp) \
	((level) < (cur)->bc_nlevels - 1 ? \
	 ((*bpp = (cur)->bc_bufs[level]), xfs_buf_to_block(*bpp)) : \
	 ((*bpp = 0), (cur)->bc_private.b.ip->i_broot))

void xfs_bmapi(xfs_mount_t *, xfs_trans_t *, struct xfs_inode *, xfs_fsblock_t, xfs_extlen_t, int, xfs_bmbt_irec_t *, int *);
void xfs_bmap_read_extents(xfs_mount_t *, xfs_trans_t *, struct xfs_inode *);

#endif	/* _FS_XFS_BMAP_H */
