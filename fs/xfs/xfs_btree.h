#ifndef _FS_XFS_BTREE_H
#define	_FS_XFS_BTREE_H

/*
 * This nonsense is to make -wlint happy.
 */
#define	XFS_LOOKUP_EQ	((xfs_lookup_t)XFS_LOOKUP_EQi)
#define	XFS_LOOKUP_LE	((xfs_lookup_t)XFS_LOOKUP_LEi)
#define	XFS_LOOKUP_GE	((xfs_lookup_t)XFS_LOOKUP_GEi)

#define	XFS_BTNUM_BNO	((xfs_btnum_t)XFS_BTNUM_BNOi)
#define	XFS_BTNUM_CNT	((xfs_btnum_t)XFS_BTNUM_CNTi)
#define	XFS_BTNUM_BMAP	((xfs_btnum_t)XFS_BTNUM_BMAPi)

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
#define	XFS_BB_MAGIC		0x01
#define	XFS_BB_LEVEL		0x02
#define	XFS_BB_NUMRECS		0x04
#define	XFS_BB_LEFTSIB		0x08
#define	XFS_BB_RIGHTSIB		0x10
#define	XFS_BB_NUM_BITS		5
#define	XFS_BB_ALL_BITS		((1 << XFS_BB_NUM_BITS) - 1)

#define	XFS_BTREE_BLOCK_MAXRECS(bsz,t,lf)	\
	((((int)(bsz)) - (int)(sizeof(xfs_btree_hdr_t) + 2 * sizeof(t ## _ptr_t))) / ((lf) ? sizeof(t ## _rec_t) : (sizeof(t ## _key_t) + sizeof(t ## _ptr_t))))
#define	XFS_BTREE_BLOCK_MINRECS(bsz,t,lf)	\
	(XFS_BTREE_BLOCK_MAXRECS(bsz,t,lf) / 2)

#define	XFS_BTREE_REC_ADDR(bsz,t,bb,i)	\
	((t ## _rec_t *)((char *)(bb) + sizeof(xfs_btree_hdr_t) + 2 * sizeof(t ## _ptr_t) + ((i) - 1) * sizeof(t ## _rec_t)))
#define	XFS_BTREE_KEY_ADDR(bsz,t,bb,i)	\
	((t ## _key_t *)((char *)(bb) + sizeof(xfs_btree_hdr_t) + 2 * sizeof(t ## _ptr_t) + ((i) - 1) * sizeof(t ## _key_t)))
#define	XFS_BTREE_PTR_ADDR(bsz,t,bb,i)	\
	((t ## _ptr_t *)((char *)(bb) + sizeof(xfs_btree_hdr_t) + 2 * sizeof(t ## _ptr_t) + XFS_BTREE_BLOCK_MAXRECS(bsz,t,0) * sizeof(t ## _key_t) + ((i) - 1) * sizeof(t ## _ptr_t)))

#define	XFS_BTREE_MAXLEVELS	8	/* max of all btrees */
typedef struct xfs_btree_cur
{
	xfs_trans_t	*bc_tp;	/* links cursors on freelist */
	xfs_mount_t	*bc_mp;		/* mount struct */
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
		struct {			/* needed for BNO, CNT */
			buf_t		*agbuf;
			xfs_agnumber_t	agno;
		} a;
		struct {			/* needed for BMAP */
			int		inodesize;
			struct xfs_inode *ip;
			xfs_fsblock_t	firstblock;
			struct xfs_bmap_free **flist;
		} b;
	}		bc_private;
} xfs_btree_cur_t;

#define	xfs_buf_to_block(buf)	((xfs_btree_block_t *)((buf)->b_un.b_addr))
#define	xfs_buf_to_sblock(buf)	((xfs_btree_sblock_t *)((buf)->b_un.b_addr))
#define	xfs_buf_to_lblock(buf)	((xfs_btree_lblock_t *)((buf)->b_un.b_addr))

#ifdef XFSDEBUG
void
xfs_btree_check_block(
	xfs_btree_cur_t		*cur,
	xfs_btree_block_t	*block,
	int			level);

void
xfs_btree_check_key(
	xfs_btnum_t	btnum,
	void		*ak1,
	void		*ak2);

void
xfs_btree_check_lblock(
	xfs_btree_cur_t		*cur,
	xfs_btree_lblock_t	*block,
	int			level);

void
xfs_btree_check_lptr(
	xfs_btree_cur_t	*cur,
	xfs_fsblock_t	ptr,
	int		level);

void
xfs_btree_check_rec(
	xfs_btnum_t	btnum,
	void		*ar1,
	void		*ar2);

void
xfs_btree_check_sblock(
	xfs_btree_cur_t		*cur,
	xfs_btree_sblock_t	*block,
	int			level);

void
xfs_btree_check_sptr(
	xfs_btree_cur_t	*cur,
	xfs_agblock_t	ptr,
	int		level);

int
xfs_btree_maxrecs(
	xfs_btree_cur_t		*cur,
	xfs_btree_block_t	*block);
#else
#define	xfs_btree_check_block(a,b,c)
#define	xfs_btree_check_key(a,b,c)
#define	xfs_btree_check_lblock(a,b,c)
#define	xfs_btree_check_lptr(a,b,c)
#define	xfs_btree_check_rec(a,b,c)
#define	xfs_btree_check_sblock(a,b,c)
#define	xfs_btree_check_sptr(a,b,c)
#endif

buf_t *
xfs_btree_read_bufl(
	xfs_mount_t	*mp,
	xfs_trans_t	*tp,
	xfs_fsblock_t	fsbno,
	uint		lock_flag);

buf_t *
xfs_btree_read_bufs(
	xfs_mount_t	*mp,
	xfs_trans_t	*tp,
	xfs_agnumber_t	agno,
	xfs_agblock_t	agbno,
	uint		lock_flag);

void
xfs_btree_del_cursor(
	xfs_btree_cur_t	*cur);

xfs_btree_cur_t *
xfs_btree_dup_cursor(
	xfs_btree_cur_t	*cur);

int
xfs_btree_firstrec(
	xfs_btree_cur_t	*cur,
	int		level);

buf_t *
xfs_btree_get_bufl(
	xfs_mount_t	*mp,
	xfs_trans_t	*tp,
	xfs_fsblock_t	fsbno,
	uint		lock_flag);

buf_t *
xfs_btree_get_bufs(
	xfs_mount_t	*mp,
	xfs_trans_t	*tp,
	xfs_agnumber_t	agno,
	xfs_agblock_t	agbno,
	uint		lock_flag);

xfs_btree_cur_t *
xfs_btree_init_cursor(
	xfs_mount_t		*mp,
	xfs_trans_t		*tp,
	buf_t			*agbuf,
	xfs_agnumber_t		agno,
	xfs_btnum_t		btnum,
	struct xfs_inode	*ip);

int
xfs_btree_islastblock(
	xfs_btree_cur_t	*cur,
	int		level);

int
xfs_btree_lastrec(
	xfs_btree_cur_t	*cur,
	int		level);

void
xfs_btree_offsets(
	int		fields,
	const int	*offsets,
	int		nbits,
	int		*first,
	int		*last);

void
xfs_btree_setbuf(
	xfs_btree_cur_t	*cur,
	int		lev,
	buf_t		*buf);

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
