#ifndef _FS_XFS_BTREE_H
#define	_FS_XFS_BTREE_H

typedef struct xfs_btree_block
{
	__uint32_t	bb_magic;	/* magic number for block type */
	__uint16_t	bb_level;	/* 0 is a leaf */
	__uint16_t	bb_numrecs;	/* current # of data records */
	xfs_agblock_t	bb_leftsib;	/* left sibling block or NULLAGBLOCK */
	xfs_agblock_t	bb_rightsib;	/* right sibling block or NULLAGBLOCK */
} xfs_btree_block_t;

#define	XFS_BTREE_BLOCK_MAXRECS(bsz,t,lev)	\
	(((bsz) - sizeof(xfs_btree_block_t)) / \
	 (sizeof(t) + sizeof(xfs_agblock_t) * ((lev) > 0)))
#define	XFS_BTREE_BLOCK_MINRECS(bsz,t,lev)	\
	(XFS_BTREE_BLOCK_MAXRECS(bsz,t,lev) / 2)

#define	XFS_BTREE_REC_ADDR(bsz,t,bb,i)	((t *)((char *)(bb) + sizeof(xfs_btree_block_t) + ((i) - 1) * sizeof(t)))
#define	XFS_BTREE_PTR_ADDR(bsz,t,bb,i)	((xfs_agblock_t *)((char *)(bb) + sizeof(xfs_btree_block_t) + XFS_BTREE_BLOCK_MAXRECS(bsz,t,(bb)->bb_level) * sizeof(t) + ((i) - 1) * sizeof(xfs_agblock_t)))

#define	XFS_BTREE_MAXLEVELS	5	/* should be max of all */
typedef struct xfs_btree_cur
{
	xfs_trans_t	*bc_tp;	/* links cursors on freelist */
	buf_t		*bc_agbuf;	/* ag buffer */
	xfs_agnumber_t	bc_agno;	/* ag number */
	union {
		xfs_alloc_rec_t		a;
		xfs_bmbt_irec_t		b;
		xfs_ialloc_rec_t	i;
	}		bc_rec;
	buf_t		*bc_bufs[XFS_BTREE_MAXLEVELS];
	int		bc_ptrs[XFS_BTREE_MAXLEVELS];
	int		bc_nlevels;
	xfs_btnum_t	bc_btnum;
	union {
		struct {
			int		inodesize;	/* needed for BMAP */
			struct xfs_inode *ip;		/* needed for BMAP */
		} b;
	}		bc_private;
} xfs_btree_cur_t;

#define	xfs_buf_to_block(buf)	((xfs_btree_block_t *)(buf->b_un.b_addr))

#ifdef XFSDEBUG
void xfs_btree_check_block(xfs_btree_cur_t *, xfs_btree_block_t *, int);
void xfs_btree_check_ptr(xfs_btree_cur_t *, xfs_agblock_t, int);
void xfs_btree_check_rec(xfs_btnum_t, void *, void *);
#else
#define	xfs_btree_check_block(a,b,c)
#define	xfs_btree_check_ptr(a,b,c)
#define	xfs_btree_check_rec(a,b,c)
#endif

buf_t *xfs_btree_bread(xfs_trans_t *, xfs_agnumber_t, xfs_agblock_t);
void xfs_btree_del_cursor(xfs_btree_cur_t *);
xfs_btree_cur_t *xfs_btree_dup_cursor(xfs_btree_cur_t *);
int xfs_btree_firstrec(xfs_btree_cur_t *, int);
xfs_btree_cur_t *xfs_btree_init_cursor(xfs_trans_t *, buf_t *, xfs_agnumber_t, xfs_btnum_t, struct xfs_inode *);
int xfs_btree_islastblock(xfs_btree_cur_t *, int);
int xfs_btree_lastrec(xfs_btree_cur_t *, int);
int xfs_btree_maxrecs(xfs_btree_cur_t *, xfs_btree_block_t *);

extern __uint32_t xfs_magics[];

#define	xfs_btree_get_block(cur, level) \
	((cur)->bc_btnum == XFS_BTNUM_BMAP && \
	 (level) == (cur)->bc_nlevels - 1 ? \
	(cur)->bc_private.b.ip->i_broot : \
	xfs_buf_to_block((cur)->bc_bufs[level]))

#endif	/* !_FS_XFS_BTREE_H */
