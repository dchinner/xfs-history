#ifndef _FS_XFS_BTREE_H
#define	_FS_XFS_BTREE_H

typedef struct xfs_btree_block
{
	__uint32_t	magic;		/* magic number for block type */
	__uint16_t	level;		/* 0 is a leaf */
	__uint16_t	numrecs;	/* current # of data records */
	xfs_agblock_t	leftsib;	/* left sibling block or NULLAGBLOCK */
	xfs_agblock_t	rightsib;	/* right sibling block or NULLAGBLOCK */
} xfs_btree_block_t;

#define	XFS_BTREE_BLOCK_MAXRECS(bsz,t,lev)	\
	(((bsz) - sizeof(xfs_btree_block_t)) / \
	 (sizeof(t) + sizeof(xfs_agblock_t) * ((lev) > 0)))
#define	XFS_BTREE_BLOCK_MINRECS(bsz,t,lev)	\
	(XFS_BTREE_BLOCK_MAXRECS(bsz,t,lev) / 2)

#define	XFS_BTREE_REC_ADDR(bsz,t,bb,i)	((t *)((char *)(bb) + sizeof(xfs_btree_block_t) + ((i) - 1) * sizeof(t)))
#define	XFS_BTREE_PTR_ADDR(bsz,t,bb,i)	((xfs_agblock_t *)((char *)(bb) + sizeof(xfs_btree_block_t) + XFS_BTREE_BLOCK_MAXRECS(bsz,t,(bb)->level) * sizeof(t) + ((i) - 1) * sizeof(xfs_agblock_t)))

#define	xfs_buf_to_block(buf)	((xfs_btree_block_t *)(buf->b_un.b_addr))

#endif	/* !_FS_XFS_BTREE_H */
