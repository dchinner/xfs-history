#ifndef _FS_XFS_BTREE_H
#define	_FS_XFS_BTREE_H

/*
 * Short form header: space allocation btrees.
 */
typedef struct xfs_btree_sblock
{
	__uint32_t	bb_magic;	/* magic number for block type */
	__uint16_t	bb_level;	/* 0 is a leaf */
	__uint16_t	bb_numrecs;	/* current # of data records */
	xfs_agblock_t	bb_leftsib;	/* left sibling block or NULLAGBLOCK */
	xfs_agblock_t	bb_rightsib;	/* right sibling block or NULLAGBLOCK */
} xfs_btree_sblock_t;

/*
 * Long form header: bmap btrees.
 */
typedef struct xfs_btree_lblock
{
	__uint32_t	bb_magic;	/* magic number for block type */
	__uint16_t	bb_level;	/* 0 is a leaf */
	__uint16_t	bb_numrecs;	/* current # of data records */
	xfs_fsblock_t	bb_leftsib;	/* left sibling block or NULLAGBLOCK */
	xfs_fsblock_t	bb_rightsib;	/* right sibling block or NULLAGBLOCK */
} xfs_btree_lblock_t;

/*
 * Combined header, used by common code.
 */
typedef struct xfs_btree_hdr
{
	__uint32_t	bb_magic;	/* magic number for block type */
	__uint16_t	bb_level;	/* 0 is a leaf */
	__uint16_t	bb_numrecs;	/* current # of data records */
} xfs_btree_hdr_t;

typedef struct xfs_btree_block
{
	xfs_btree_hdr_t	bb_h;		/* header */
	union		{
		struct	{
			xfs_agblock_t	bb_leftsib;
			xfs_agblock_t	bb_rightsib;
		}	s;		/* short form pointers */
		struct	{
			xfs_fsblock_t	bb_leftsib;
			xfs_fsblock_t	bb_rightsib;
		}	l;		/* long form pointers */
	}		bb_u;		/* rest */
} xfs_btree_block_t;

/*
 * For logging record fields.
 */
#define	XFS_BB_MAGIC		0x1
#define	XFS_BB_LEVEL		0x2
#define	XFS_BB_NUMRECS		0x4
#define	XFS_BB_LEFTSIB		0x8
#define	XFS_BB_RIGHTSIB		0x10
#define	XFS_BB_NUM_BITS		5
#define	XFS_BB_ALL_BITS		((1 << XFS_BB_NUM_BITS) - 1)

#define	XFS_BTREE_SBLOCK_MAXRECS(bsz,t,lev)	\
	((((int)(bsz)) - (int)sizeof(xfs_btree_sblock_t)) / \
	 ((int)sizeof(t) + (int)sizeof(xfs_agblock_t) * ((lev) > 0)))
#define	XFS_BTREE_SBLOCK_MINRECS(bsz,t,lev)	\
	(XFS_BTREE_SBLOCK_MAXRECS(bsz,t,lev) / 2)

#define	XFS_BTREE_LBLOCK_MAXRECS(bsz,t,lev)	\
	((((int)(bsz)) - (int)sizeof(xfs_btree_lblock_t)) / \
	 ((int)sizeof(t) + (int)sizeof(xfs_fsblock_t) * ((lev) > 0)))
#define	XFS_BTREE_LBLOCK_MINRECS(bsz,t,lev)	\
	(XFS_BTREE_LBLOCK_MAXRECS(bsz,t,lev) / 2)

#define	XFS_BTREE_BLOCK_MAXRECS(bsz,t,lev,pt)	\
	((((int)(bsz)) - (int)(sizeof(xfs_btree_hdr_t) + 2 * sizeof(pt))) / \
	 ((int)sizeof(t) + (int)sizeof(pt) * ((lev) > 0)))
#define	XFS_BTREE_BLOCK_MINRECS(bsz,t,lev,pt)	\
	(XFS_BTREE_BLOCK_MAXRECS(bsz,t,lev,pt) / 2)

#define	XFS_BTREE_SREC_ADDR(bsz,t,bb,i)	\
	((t *)((char *)(bb) + sizeof(xfs_btree_sblock_t) + \
		((i) - 1) * sizeof(t)))
#define	XFS_BTREE_SPTR_ADDR(bsz,t,bb,i)	\
	((xfs_agblock_t *)((char *)(bb) + sizeof(xfs_btree_sblock_t) + \
		XFS_BTREE_SBLOCK_MAXRECS(bsz,t,(bb)->bb_level) * sizeof(t) + \
		((i) - 1) * sizeof(xfs_agblock_t)))

#define	XFS_BTREE_LREC_ADDR(bsz,t,bb,i)	\
	((t *)((char *)(bb) + sizeof(xfs_btree_lblock_t) + \
		((i) - 1) * sizeof(t)))
#define	XFS_BTREE_LPTR_ADDR(bsz,t,bb,i)	\
	((xfs_fsblock_t *)((char *)(bb) + sizeof(xfs_btree_lblock_t) + \
		XFS_BTREE_LBLOCK_MAXRECS(bsz,t,(bb)->bb_level) * sizeof(t) + \
		((i) - 1) * sizeof(xfs_fsblock_t)))

#define	XFS_BTREE_REC_ADDR(bsz,t,bb,i,pt)	\
	((t *)((char *)(bb) + sizeof(xfs_btree_hdr_t) + 2 * sizeof(pt) + \
		((i) - 1) * sizeof(t)))
#define	XFS_BTREE_PTR_ADDR(bsz,t,bb,i,pt)	\
	((pt *)((char *)(bb) + sizeof(xfs_btree_hdr_t) + 2 * sizeof(pt) + \
		XFS_BTREE_BLOCK_MAXRECS(bsz,t,(bb)->bb_level,pt) * sizeof(t) + \
		((i) - 1) * sizeof(pt)))

#define	XFS_BTREE_ROOT_LREC_ADDR(bsz,t,bb,i)	\
	((t *)((char *)(bb) + sizeof(xfs_btree_lblock_t) + \
		((i) - 1) * sizeof(t)))
#define	XFS_BTREE_ROOT_LPTR_ADDR(bsz,t,bb,i)	\
	((xfs_fsblock_t *)((char *)(bb) + sizeof(xfs_btree_lblock_t) + \
		XFS_BTREE_LBLOCK_MAXRECS(bsz,t,1) * sizeof(t) + \
		((i) - 1) * sizeof(xfs_fsblock_t)))

#define	XFS_BTREE_MAXLEVELS	9	/* max of all btrees */
typedef struct xfs_btree_cur
{
	xfs_trans_t	*bc_tp;	/* links cursors on freelist */
	xfs_mount_t	*bc_mp;		/* mount struct */
	buf_t		*bc_agbuf;	/* agf buffer */
	xfs_agnumber_t	bc_agno;	/* ag number */
	union {
		xfs_alloc_rec_t		a;
		xfs_bmbt_irec_t		b;
	}		bc_rec;
	buf_t		*bc_bufs[XFS_BTREE_MAXLEVELS];
	int		bc_ptrs[XFS_BTREE_MAXLEVELS];
	int		bc_nlevels;
	xfs_btnum_t	bc_btnum;
	int		bc_blocklog;
	union {
		struct {
			int		inodesize;	/* needed for BMAP */
			struct xfs_inode *ip;		/* needed for BMAP */
		} b;
	}		bc_private;
} xfs_btree_cur_t;

#define	xfs_buf_to_block(buf)	((xfs_btree_block_t *)((buf)->b_un.b_addr))
#define	xfs_buf_to_sblock(buf)	((xfs_btree_sblock_t *)((buf)->b_un.b_addr))
#define	xfs_buf_to_lblock(buf)	((xfs_btree_lblock_t *)((buf)->b_un.b_addr))

#ifdef XFSDEBUG
void xfs_btree_check_block(xfs_btree_cur_t *, xfs_btree_block_t *, int);
void xfs_btree_check_lblock(xfs_btree_cur_t *, xfs_btree_lblock_t *, int);
void xfs_btree_check_sblock(xfs_btree_cur_t *, xfs_btree_sblock_t *, int);
void xfs_btree_check_lptr(xfs_btree_cur_t *, xfs_fsblock_t, int);
void xfs_btree_check_rec(xfs_btnum_t, void *, void *);
void xfs_btree_check_sptr(xfs_btree_cur_t *, xfs_agblock_t, int);
int xfs_btree_maxrecs(xfs_btree_cur_t *, xfs_btree_block_t *);
#else
#define	xfs_btree_check_block(a,b,c)
#define	xfs_btree_check_lblock(a,b,c)
#define	xfs_btree_check_sblock(a,b,c)
#define	xfs_btree_check_lptr(a,b,c)
#define	xfs_btree_check_rec(a,b,c)
#define	xfs_btree_check_sptr(a,b,c)
#endif

buf_t *xfs_btree_breadl(xfs_mount_t *, xfs_trans_t *, xfs_fsblock_t);
buf_t *xfs_btree_breads(xfs_mount_t *, xfs_trans_t *, xfs_agnumber_t, xfs_agblock_t);
void xfs_btree_del_cursor(xfs_btree_cur_t *);
xfs_btree_cur_t *xfs_btree_dup_cursor(xfs_btree_cur_t *);
int xfs_btree_firstrec(xfs_btree_cur_t *, int);
buf_t *xfs_btree_getblkl(xfs_mount_t *, xfs_trans_t *, xfs_fsblock_t);
buf_t *xfs_btree_getblks(xfs_mount_t *, xfs_trans_t *, xfs_agnumber_t, xfs_agblock_t);
xfs_btree_cur_t *xfs_btree_init_cursor(xfs_mount_t *, xfs_trans_t *, buf_t *, xfs_agnumber_t, xfs_btnum_t, struct xfs_inode *);
int xfs_btree_islastblock(xfs_btree_cur_t *, int);
int xfs_btree_lastrec(xfs_btree_cur_t *, int);
void xfs_btree_offsets(int, const int *, int, int *, int *);
void xfs_btree_setbuf(xfs_btree_cur_t *, int, buf_t *);

extern __uint32_t xfs_magics[];

#define	xfs_btree_long_ptrs(btnum)	((btnum) == XFS_BTNUM_BMAP)
#define	xfs_btree_get_block(cur, level) \
	((cur)->bc_btnum == XFS_BTNUM_BMAP && \
	 (level) == (cur)->bc_nlevels - 1 ? \
	((xfs_btree_block_t *)(cur)->bc_private.b.ip->i_broot) : \
	xfs_buf_to_block((cur)->bc_bufs[level]))

#define	xfs_extlen_min(a,b)	((xfs_extlen_t)(a) < (xfs_extlen_t)(b) ? (xfs_extlen_t)(a) : (xfs_extlen_t)(b))
#define	xfs_extlen_max(a,b)	((xfs_extlen_t)(a) > (xfs_extlen_t)(b) ? (xfs_extlen_t)(a) : (xfs_extlen_t)(b))

#define	xfs_fsbno_min(a,b)	((xfs_fsblock_t)(a) < (xfs_fsblock_t)(b) ? (xfs_fsblock_t)(a) : (xfs_fsblock_t)(b))
#define	xfs_fsbno_max(a,b)	((xfs_fsblock_t)(a) > (xfs_fsblock_t)(b) ? (xfs_fsblock_t)(a) : (xfs_fsblock_t)(b))

#endif	/* !_FS_XFS_BTREE_H */
