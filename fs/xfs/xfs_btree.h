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
	xfs_dfsbno_t	bb_leftsib;	/* left sibling block or NULLDFSBNO */
	xfs_dfsbno_t	bb_rightsib;	/* right sibling block or NULLDFSBNO */
} xfs_btree_lblock_t;

/*
 * Combined header and structure, used by common code.
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
			xfs_dfsbno_t	bb_leftsib;
			xfs_dfsbno_t	bb_rightsib;
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

/*
 * Boolean to select which form of xfs_btree_block_t.bb_u to use.
 */
#define	XFS_BTREE_LONG_PTRS(btnum)	((btnum) == XFS_BTNUM_BMAP)

/*
 * Magic numbers for btree blocks.
 */
extern __uint32_t	xfs_magics[];

/*
 * Maximum and minimum recorcs in a btree block.
 * Given block size, type prefix, and leaf flag.
 */
#define	XFS_BTREE_BLOCK_MAXRECS(bsz,t,lf)	\
	((((int)(bsz)) - (int)(sizeof(t ## _block_t))) / ((lf) ? sizeof(t ## _rec_t) : (sizeof(t ## _key_t) + sizeof(t ## _ptr_t))))
#define	XFS_BTREE_BLOCK_MINRECS(bsz,t,lf)	\
	(XFS_BTREE_BLOCK_MAXRECS(bsz,t,lf) / 2)

/*
 * Record, key, and pointer address calculation macros.
 * Given block size, type prefix, block pointer, and index of requested entry
 * (first entry numbered 1).
 */
#define	XFS_BTREE_REC_ADDR(bsz,t,bb,i)	\
	((t ## _rec_t *)((char *)(bb) + sizeof(t ## _block_t) + \
	 ((i) - 1) * sizeof(t ## _rec_t)))
#define	XFS_BTREE_KEY_ADDR(bsz,t,bb,i)	\
	((t ## _key_t *)((char *)(bb) + sizeof(t ## _block_t) + \
	 ((i) - 1) * sizeof(t ## _key_t)))
#define	XFS_BTREE_PTR_ADDR(bsz,t,bb,i)	\
	((t ## _ptr_t *)((char *)(bb) + sizeof(t ## _block_t) + \
	 XFS_BTREE_BLOCK_MAXRECS(bsz,t,0) * sizeof(t ## _key_t) + \
	 ((i) - 1) * sizeof(t ## _ptr_t)))

#define	XFS_BTREE_MAXLEVELS	8	/* max of all btrees */

/*
 * Btree cursor structure.
 * This collects all information needed by the btree code in one place.
 */
typedef struct xfs_btree_cur
{
	xfs_trans_t	*bc_tp;		/* transaction we're in, if any */
	xfs_mount_t	*bc_mp;		/* file system mount struct */
	union {
		xfs_alloc_rec_t		a;
		xfs_bmbt_irec_t		b;
	}		bc_rec;		/* current insert/search record value */
	buf_t		*bc_bufs[XFS_BTREE_MAXLEVELS];	/* buf ptr per level */
	int		bc_ptrs[XFS_BTREE_MAXLEVELS];	/* key/record # */
	int		bc_nlevels;	/* number of levels in the tree */
	xfs_btnum_t	bc_btnum;	/* identifies which btree type */
	int		bc_blocklog;	/* log2(blocksize) of btree blocks */
	union {
		struct {			/* needed for BNO, CNT */
			buf_t		*agbp;	/* agf buffer pointer */
			xfs_agnumber_t	agno;	/* ag number */
		} a;
		struct {			/* needed for BMAP */
			int		inodesize;	/* size of inodes */
			struct xfs_inode *ip;	/* pointer to our inode */
			xfs_fsblock_t	firstblock;	/* 1st blk allocated */
			struct xfs_bmap_free *flist;	/* list to free after */
			int		allocated;	/* count of alloced */
		} b;
	}		bc_private;	/* per-btree type data */
} xfs_btree_cur_t;

/*
 * Convert from buffer to btree block header.
 */
#define	XFS_BUF_TO_BLOCK(bp)	((xfs_btree_block_t *)((bp)->b_un.b_addr))
#define	XFS_BUF_TO_LBLOCK(bp)	((xfs_btree_lblock_t *)((bp)->b_un.b_addr))
#define	XFS_BUF_TO_SBLOCK(bp)	((xfs_btree_sblock_t *)((bp)->b_un.b_addr))

#ifdef DEBUG
/*
 * Debug routine: check that block header is ok.
 */
void
xfs_btree_check_block(
	xfs_btree_cur_t		*cur,	/* btree cursor */
	xfs_btree_block_t	*block,	/* generic btree block pointer */
	int			level);	/* level of the btree block */

/*
 * Debug routine: check that keys are in the right order.
 */
void
xfs_btree_check_key(
	xfs_btnum_t		btnum,	/* btree identifier */
	void			*ak1,	/* pointer to left (lower) key */
	void			*ak2);	/* pointer to right (higher) key */

/*
 * Debug routine: check that long form block header is ok.
 */
void
xfs_btree_check_lblock(
	xfs_btree_cur_t		*cur,	/* btree cursor */
	xfs_btree_lblock_t	*block,	/* btree long form block pointer */
	int			level);	/* level of the btree block */

/*
 * Debug routine: check that (long) pointer is ok.
 */
void
xfs_btree_check_lptr(
	xfs_btree_cur_t		*cur,	/* btree cursor */
	xfs_dfsbno_t		ptr,	/* btree block disk address */
	int			level);	/* btree block level */

/*
 * Debug routine: check that records are in the right order.
 */
void
xfs_btree_check_rec(
	xfs_btnum_t		btnum,	/* btree identifier */
	void			*ar1,	/* pointer to left (lower) record */
	void			*ar2);	/* pointer to right (higher) record */

/*
 * Debug routine: check that short form block header is ok.
 */
void
xfs_btree_check_sblock(
	xfs_btree_cur_t		*cur,	/* btree cursor */
	xfs_btree_sblock_t	*block,	/* btree short form block pointer */
	int			level);	/* level of the btree block */

/*
 * Debug routine: check that (short) pointer is ok.
 */
void
xfs_btree_check_sptr(
	xfs_btree_cur_t		*cur,	/* btree cursor */
	xfs_agblock_t		ptr,	/* btree block disk address */
	int			level);	/* btree block level */
#else
#define	xfs_btree_check_block(a,b,c)
#define	xfs_btree_check_key(a,b,c)
#define	xfs_btree_check_lblock(a,b,c)
#define	xfs_btree_check_lptr(a,b,c)
#define	xfs_btree_check_rec(a,b,c)
#define	xfs_btree_check_sblock(a,b,c)
#define	xfs_btree_check_sptr(a,b,c)
#endif	/* DEBUG */

/*
 * Delete the btree cursor.
 */
void
xfs_btree_del_cursor(
	xfs_btree_cur_t		*cur);	/* btree cursor */

/*
 * Duplicate the btree cursor.
 * Allocate a new one, copy the record, re-get the buffers.
 */
xfs_btree_cur_t *			/* new btree cursor */
xfs_btree_dup_cursor(
	xfs_btree_cur_t		*cur);	/* btree cursor */

/*
 * Change the cursor to point to the first record in the current block
 * at the given level.  Other levels are unaffected.
 */
int					/* success=1, failure=0 */
xfs_btree_firstrec(
	xfs_btree_cur_t		*cur,	/* btree cursor */
	int			level);	/* level to change */

/*
 * Retrieve the block pointer from the cursor at the given level.
 * This may be a bmap btree root or from a buffer.
 */
xfs_btree_block_t *			/* generic btree block pointer */
xfs_btree_get_block(
	xfs_btree_cur_t		*cur,	/* btree cursor */
	int			level);	/* level in btree */

/*
 * Get a buffer for the block, return it with no data read.
 * Long-form addressing.
 */
buf_t *					/* buffer for fsbno */
xfs_btree_get_bufl(
	xfs_mount_t		*mp,	/* file system mount point */
	xfs_trans_t		*tp,	/* transaction pointer */
	xfs_fsblock_t		fsbno,	/* file system block number */
	uint			lock);	/* lock flags for get_buf */

/*
 * Get a buffer for the block, return it with no data read.
 * Short-form addressing.
 */
buf_t *					/* buffer for agno/agbno */
xfs_btree_get_bufs(
	xfs_mount_t		*mp,	/* file system mount point */
	xfs_trans_t		*tp,	/* transaction pointer */
	xfs_agnumber_t		agno,	/* allocation group number */
	xfs_agblock_t		agbno,	/* allocation group block number */
	uint			lock);	/* lock flags for get_buf */

/* 
 * Allocate a new btree cursor.
 * The cursor is either for allocation (A) or bmap (B).
 */
xfs_btree_cur_t *			/* new btree cursor */
xfs_btree_init_cursor(
	xfs_mount_t		*mp,	/* file system mount point */
	xfs_trans_t		*tp,	/* transaction pointer */
	buf_t			*agbp,	/* (A only) buffer for agf structure */
	xfs_agnumber_t		agno,	/* (A only) allocation group number */
	xfs_btnum_t		btnum,	/* btree identifier */
	struct xfs_inode	*ip);	/* (B only) inode owning the btree */

/*
 * Check for the cursor referring to the last block at the given level.
 */
int					/* 1=is last block, 0=not last block */
xfs_btree_islastblock(
	xfs_btree_cur_t		*cur,	/* btree cursor */
	int			level);	/* level to check */

/*
 * Change the cursor to point to the last record in the current block
 * at the given level.  Other levels are unaffected.
 */
int					/* success=1, failure=0 */
xfs_btree_lastrec(
	xfs_btree_cur_t		*cur,	/* btree cursor */
	int			level);	/* level to change */

/*
 * Compute first and last byte offsets for the fields given.
 * Interprets the offsets table, which contains struct field offsets.
 */
void
xfs_btree_offsets(
	int			fields,	/* bitmask of fields */
	const int		*offsets,/* table of field offsets */
	int			nbits,	/* number of bits to inspect */
	int			*first,	/* output: first byte offset */
	int			*last);	/* output: last byte offset */

/*
 * Get a buffer for the block, return it read in.
 * Long-form addressing.
 */
buf_t *					/* buffer for fsbno */
xfs_btree_read_bufl(
	xfs_mount_t		*mp,	/* file system mount point */
	xfs_trans_t		*tp,	/* transaction pointer */
	xfs_fsblock_t		fsbno,	/* file system block number */
	uint			lock);	/* lock flags for read_buf */

/*
 * Get a buffer for the block, return it read in.
 * Short-form addressing.
 */
buf_t *					/* buffer for agno/agbno */
xfs_btree_read_bufs(
	xfs_mount_t		*mp,	/* file system mount point */
	xfs_trans_t		*tp,	/* transaction pointer */
	xfs_agnumber_t		agno,	/* allocation group number */
	xfs_agblock_t		agbno,	/* allocation group block number */
	uint			lock);	/* lock flags for read_buf */

/*
 * Set the buffer for level "lev" in the cursor to bp, releasing
 * any previous buffer.
 */
void
xfs_btree_setbuf(
	xfs_btree_cur_t		*cur,	/* btree cursor */
	int			lev,	/* level in btree */
	buf_t			*bp);	/* new buffer to set */

/*
 * Min and max functions for extlen, agblock, and fileoff types.
 */
#define	XFS_EXTLEN_MIN(a,b)	\
	((xfs_extlen_t)(a) < (xfs_extlen_t)(b) ? \
	 (xfs_extlen_t)(a) : (xfs_extlen_t)(b))
#define	XFS_EXTLEN_MAX(a,b)	\
	((xfs_extlen_t)(a) > (xfs_extlen_t)(b) ? \
	 (xfs_extlen_t)(a) : (xfs_extlen_t)(b))

#define	XFS_AGBLOCK_MIN(a,b)	\
	((xfs_agblock_t)(a) < (xfs_agblock_t)(b) ? \
	 (xfs_agblock_t)(a) : (xfs_agblock_t)(b))
#define	XFS_AGBLOCK_MAX(a,b)	\
	((xfs_agblock_t)(a) > (xfs_agblock_t)(b) ? \
	 (xfs_agblock_t)(a) : (xfs_agblock_t)(b))

#define	XFS_FILEOFF_MIN(a,b)	\
	((xfs_fileoff_t)(a) < (xfs_fileoff_t)(b) ? \
	 (xfs_fileoff_t)(a) : (xfs_fileoff_t)(b))
#define	XFS_FILEOFF_MAX(a,b)	\
	((xfs_fileoff_t)(a) > (xfs_fileoff_t)(b) ? \
	 (xfs_fileoff_t)(a) : (xfs_fileoff_t)(b))

#endif	/* !_FS_XFS_BTREE_H */
