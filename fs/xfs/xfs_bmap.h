#ifndef _FS_XFS_BMAP_H
#define	_FS_XFS_BMAP_H

#ident "$Revision: 1.6 $"

#define	XFS_BMAP_MAGIC	0x424d4150	/* 'BMAP' */

typedef	__uint8_t	xfs_offset_p[7];
typedef	__uint8_t	xfs_blkno_p[6];
typedef	__uint8_t	xfs_extlen_p[3];

/*
 * Bmap btree record and extent descriptor.
 */
typedef struct xfs_bmbt_rec
{
	xfs_offset_p	br_startoff;	/* starting file offset */
	xfs_blkno_p	br_startblock;	/* starting block number */
	xfs_extlen_p	br_blockcount;	/* number of blocks */
} xfs_bmbt_rec_t;

/*
 * Incore version of above.
 */
typedef struct xfs_bmbt_irec
{
	xfs_fsblock_t	br_startoff;	/* starting file offset */
	xfs_fsblock_t	br_startblock;	/* starting block number */
	xfs_extlen_t	br_blockcount;	/* number of block */
} xfs_bmbt_irec_t;

#define	xfs_bl(p,i)	((unsigned long long)(*(unsigned char *)(p+i) << (8 * i)))
#define	xfs_sl(p,i)	((unsigned long long)(*(unsigned short *)(p+i) << (8 * i)))
#define	xfs_ll(p,i)	((unsigned long long)(*(unsigned long *)(p+i) << (8 * i)))
#define	xfs_dl(p,i)	((unsigned long long)(*(unsigned long long *)(p+i) << (8 * i)))
#define	xfs_bs(p,i)	((unsigned long)(*(unsigned char *)(p+i) << (8 * i)))
#define	xfs_ss(p,i)	((unsigned long)(*(unsigned short *)(p+i) << (8 * i)))
#define	xfs_ls(p,i)	((unsigned long)(*(unsigned long *)(p+i) << (8 * i)))

#define	xfs_offset_ptox(p)	(xfs_dl(p,0) >> 8)
#define	xfs_blkno_ptox(p)	(xfs_bl(p,0) | (xfs_dl(p,1) >> 24))
#define	xfs_extlen_ptox(p)	(xfs_ls(p,0) & 0xffffff)

#define	xfs_ptoxrec(p,x)	\
	((x)->br_startoff = xfs_offset_ptox((p)->br_startoff), \
	 (x)->br_startblock = xfs_blkno_ptox((p)->br_startblock), \
	 (x)->br_blockcount = xfs_extlen_ptox((p)->br_blockcount))

#define	xfs_pb(p,i,v)	(*(unsigned char *)(p+i) = (unsigned char)(v >> (8 * i)))
#define	xfs_ps(p,i,v)	(*(unsigned short *)(p+i) = (unsigned short)(v >> (8 * i)))
#define	xfs_pl(p,i,v)	(*(unsigned long *)(p+i) = (unsigned long)(v >> (8 * i)))

#define	xfs_offset_xtop(p,v)	(xfs_pl(p,0,v), xfs_ps(p,4,v), xfs_pb(p,6,v))
#define	xfs_blkno_xtop(p,v)	(xfs_pb(p,0,v), xfs_pl(p,1,v), xfs_pb(p,5,v))
#define	xfs_extlen_xtop(p,v)	(xfs_pb(p,0,v), xfs_ps(p,1,v))

#define	xfs_xtoprec(x,p)	\
	(xfs_offset_xtop((p)->br_startoff, (x)->br_startoff), \
	 xfs_blkno_xtop((p)->br_startblock, (x)->br_startblock), \
	 xfs_extlen_xtop((p)->br_blockcount, (x)->br_blockcount))

#define	XFS_BMAP_RBLOCK_DSIZE(lev,cur) ((cur)->bc_private.b.inodesize - (int)offsetof(xfs_dinode_t, di_u))
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
#define	XFS_BMAP_BROOT_SIZE(isz) ((isz) - offsetof(xfs_dinode_t, di_u)) 
#define	XFS_BMAP_BROOT_REC_ADDR(bb,i,isz) XFS_BTREE_REC_ADDR(isz,xfs_bmbt_rec_t,bb,i)
#define XFS_BMAP_BROOT_PTR_ADDR(bb,i,isz) XFS_BTREE_ROOT_PTR_ADDR(isz,xfs_bmbt_rec_t,bb,i)

#define	XFS_BMAP_BROOT_NUMRECS(bb) ((bb)->bb_numrecs)
#define	XFS_BMAP_BROOT_MAXRECS(sz) XFS_BTREE_BLOCK_MAXRECS(sz,xfs_bmbt_rec_t,1)
#define	XFS_BMAP_BROOT_SPACE(bb) ((int)(sizeof(xfs_btree_block_t) + ((bb)->bb_numrecs * (sizeof(xfs_bmbt_rec_t) + sizeof(xfs_agblock_t)))))
#define	XFS_BMAP_BROOT_SPACE_CALC(nrecs) ((int)(sizeof(xfs_btree_block_t) + ((nrecs) * (sizeof(xfs_bmbt_rec_t) + sizeof(xfs_agblock_t)))))

#define	XFS_BMAP_MAXLEVELS	5	/* ??? */

#define	xfs_bmbt_get_block(cur, level)	\
	((level) < (cur)->bc_nlevels - 1 ? \
	 xfs_buf_to_block((cur)->bc_bufs[level]) : \
	 (cur)->bc_private.b.ip->i_broot)

void xfs_bmapi(xfs_trans_t *, struct xfs_inode *, xfs_fsblock_t, xfs_extlen_t, int, xfs_bmbt_irec_t *, int *);

#endif	/* _FS_XFS_BMAP_H */
