#ifndef _FS_XFS_BMAP_H
#define	_FS_XFS_BMAP_H

#ident "$Revision$"

#define	XFS_BMAP_MAGIC	0x424d4150	/* 'BMAP' */

/*
 * Bmap extent descriptor.
 */
typedef struct xfs_extdesc
{
	xfs_fsblock_t	ed_start;	/* starting block number */
	xfs_extlen_t	ed_len;		/* number of blocks */
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
typedef struct xfs_bmap_rec
{
	/* should be off_t if it were 64 bits, fix soon */
	xfs_fsblock_t	startoff;	/* starting file offset */
	xfs_fsblock_t	startblock;	/* starting block number */
	xfs_extlen_t	blockcount;	/* number of blocks */
} xfs_bmap_rec_t;

#define	XFS_BMAP_RBLOCK_SIZE(bl,lev,cur) ((cur)->inodesize - offsetof(xfs_dinode_t, di_u))
#define	XFS_BMAP_IBLOCK_SIZE(bl,lev,cur) (1 << (bl))
#define	XFS_BMAP_BLOCK_SIZE(bl,lev,cur) ((lev) == (cur)->nlevels - 1 ? XFS_BMAP_RBLOCK_SIZE(bl,lev,cur) : XFS_BMAP_IBLOCK_SIZE(bl,lev,cur))

#define	XFS_BMAP_BLOCK_MAXRECS(bl,lev,cur) XFS_BTREE_BLOCK_MAXRECS(XFS_BMAP_BLOCK_SIZE(bl,lev,cur), xfs_bmap_rec_t, lev)
#define	XFS_BMAP_BLOCK_MINRECS(bl,lev,cur) XFS_BTREE_BLOCK_MINRECS(XFS_BMAP_BLOCK_SIZE(bl,lev,cur), xfs_bmap_rec_t, lev)

#define	XFS_BMAP_REC_ADDR(bb,i,bl,cur) XFS_BTREE_REC_ADDR(XFS_BMAP_BLOCK_SIZE(bl,(bb)->level,cur), xfs_bmap_rec_t, bb, i)

#define	XFS_BMAP_PTR_ADDR(bb,i,bl,cur) XFS_BTREE_PTR_ADDR(XFS_BMAP_BLOCK_SIZE(bl,(bb)->level,cur), xfs_bmap_rec_t, bb, i)

#define	XFS_BMAP_MAXLEVELS	5	/* ??? */

typedef struct xfs_bmap_cur
{
	xfs_trans_t		*tp;	/* links cursors on freelist */
	buf_t			*agbuf;
	xfs_agnumber_t		agno;	/* xfs_buf_to_agp(agbuf)->xfsag_seqno */
	xfs_bmap_rec_t		rec;
	buf_t			*bufs[XFS_BMAP_MAXLEVELS];
	int			ptrs[XFS_BMAP_MAXLEVELS];
	int			nlevels;
	xfs_btnum_t		btnum;	/* == XFS_BTNUM_BMAP */
	int			inodesize;	/* tp->t_mountp->m_sb->xfsb_inodesize */
	struct xfs_inode	*ip;
#ifdef XFSBMDEBUG
	int			lineno;
	struct xfs_bmap_cur	*next;	/* on all cursors list */
#endif
} xfs_bmap_cur_t;

#define	xfs_bmap_get_block(cur, level)	\
	((level) == (cur)->nlevels - 1 ? \
	 xfs_buf_to_block((cur)->bufs[level]) : \
	 &(cur)->ip->i_d.di_u.di_bmbt)

#endif	/* _FS_XFS_BMAP_H */
