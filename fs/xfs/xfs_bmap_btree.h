#ifndef _FS_XFS_BMAP_BTREE_H
#define	_FS_XFS_BMAP_BTREE_H

#ident "$Revision: 1.6 $"

#define	XFS_BMAP_MAGIC	0x424d4150	/* 'BMAP' */

struct xfs_btree_cur;
struct xfs_btree_lblock;

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

#define	NULLSTARTBLOCK	((xfs_fsblock_t)((1LL << 52) - 1))

/*
 * Incore version of above.
 */
typedef struct xfs_bmbt_irec
{
	xfs_fsblock_t	br_startoff;	/* starting file offset */
	xfs_fsblock_t	br_startblock;	/* starting block number */
	xfs_extlen_t	br_blockcount;	/* number of block */
} xfs_bmbt_irec_t;

/*
 * Key structure for non-leaf levels of the tree.
 */
typedef struct xfs_bmbt_key
{
	xfs_fsblock_t	br_startoff;	/* starting file offset */
} xfs_bmbt_key_t;

typedef xfs_fsblock_t xfs_bmbt_ptr_t;	/* btree pointer type */

#ifndef XFSDEBUG

#define	XFS_BMAP_RBLOCK_DSIZE(lev,cur) \
	((cur)->bc_private.b.inodesize - (int)sizeof(xfs_dinode_core_t))
#define	XFS_BMAP_RBLOCK_ISIZE(lev,cur) \
	((int)(cur)->bc_private.b.ip->i_broot_bytes)
#define	XFS_BMAP_IBLOCK_SIZE(lev,cur) (1 << (cur)->bc_blocklog)
#define	XFS_BMAP_BLOCK_DSIZE(lev,cur) \
	((lev) == (cur)->bc_nlevels - 1 ? \
		XFS_BMAP_RBLOCK_DSIZE(lev,cur) : XFS_BMAP_IBLOCK_SIZE(lev,cur))
#define	XFS_BMAP_BLOCK_ISIZE(lev,cur) \
	((lev) == (cur)->bc_nlevels - 1 ? \
		XFS_BMAP_RBLOCK_ISIZE(lev,cur) : XFS_BMAP_IBLOCK_SIZE(lev,cur))

#define	XFS_BMAP_BLOCK_DMAXRECS(lev,cur) \
	XFS_BTREE_BLOCK_MAXRECS(XFS_BMAP_BLOCK_DSIZE(lev,cur), xfs_bmbt, \
				lev == 0)
#define	XFS_BMAP_BLOCK_IMAXRECS(lev,cur) \
	XFS_BTREE_BLOCK_MAXRECS(XFS_BMAP_BLOCK_ISIZE(lev,cur), xfs_bmbt, \
				lev == 0)
#define	XFS_BMAP_BLOCK_DMINRECS(lev,cur) \
	XFS_BTREE_BLOCK_MINRECS(XFS_BMAP_BLOCK_DSIZE(lev,cur), xfs_bmbt, \
				lev == 0)
#define	XFS_BMAP_BLOCK_IMINRECS(lev,cur) \
	XFS_BTREE_BLOCK_MINRECS(XFS_BMAP_BLOCK_ISIZE(lev,cur), xfs_bmbt, \
				lev == 0)

#define	XFS_BMAP_REC_DADDR(bb,i,cur) \
	XFS_BTREE_REC_ADDR(XFS_BMAP_BLOCK_DSIZE((bb)->bb_level,cur), xfs_bmbt, \
			   bb, i)
#define	XFS_BMAP_REC_IADDR(bb,i,cur) \
	XFS_BTREE_REC_ADDR(XFS_BMAP_BLOCK_ISIZE((bb)->bb_level,cur), xfs_bmbt, \
			   bb, i)

#define	XFS_BMAP_KEY_DADDR(bb,i,cur) \
	XFS_BTREE_KEY_ADDR(XFS_BMAP_BLOCK_DSIZE((bb)->bb_level,cur), xfs_bmbt, \
			   bb, i)
#define	XFS_BMAP_KEY_IADDR(bb,i,cur) \
	XFS_BTREE_KEY_ADDR(XFS_BMAP_BLOCK_ISIZE((bb)->bb_level,cur), xfs_bmbt, \
			   bb, i)

#define	XFS_BMAP_PTR_DADDR(bb,i,cur) \
	XFS_BTREE_PTR_ADDR(XFS_BMAP_BLOCK_DSIZE((bb)->bb_level,cur), xfs_bmbt, \
			   bb, i)
#define	XFS_BMAP_PTR_IADDR(bb,i,cur) \
	XFS_BTREE_PTR_ADDR(XFS_BMAP_BLOCK_ISIZE((bb)->bb_level,cur), xfs_bmbt, \
			   bb, i)

/*
 * These are to be used when we know the size of the block and
 * we don't have a cursor.
 */
#define	XFS_BMAP_BROOT_SIZE(isz) ((isz) - sizeof(xfs_dinode_core_t)) 
#define	XFS_BMAP_BROOT_REC_ADDR(bb,i,isz) XFS_BTREE_REC_ADDR(isz,xfs_bmbt,bb,i)
#define	XFS_BMAP_BROOT_KEY_ADDR(bb,i,isz) XFS_BTREE_KEY_ADDR(isz,xfs_bmbt,bb,i)
#define XFS_BMAP_BROOT_PTR_ADDR(bb,i,isz) XFS_BTREE_PTR_ADDR(isz,xfs_bmbt,bb,i)

#define	XFS_BMAP_BROOT_NUMRECS(bb) ((bb)->bb_numrecs)
#define	XFS_BMAP_BROOT_MAXRECS(sz) XFS_BTREE_BLOCK_MAXRECS(sz,xfs_bmbt,0)
#define	XFS_BMAP_BROOT_SPACE_CALC(nrecs) \
	((int)(sizeof(xfs_btree_lblock_t) + \
	       ((nrecs) * (sizeof(xfs_bmbt_key_t) + sizeof(xfs_bmbt_ptr_t)))))
#define	XFS_BMAP_BROOT_SPACE(bb) XFS_BMAP_BROOT_SPACE_CALC((bb)->bb_numrecs)

/*
 * Number of extent records that fit in the inode.
 */
#define	XFS_BMAP_EXT_MAXRECS(isz)	\
	(XFS_BMAP_BROOT_SIZE(isz) / sizeof(xfs_bmbt_rec_t))

#define	xfs_bmbt_get_block(cur, level, bpp) \
	((level) < (cur)->bc_nlevels - 1 ? \
	 ((*bpp = (cur)->bc_bufs[level]), xfs_buf_to_lblock(*bpp)) : \
	 ((*bpp = 0), (cur)->bc_private.b.ip->i_broot))

#else	/* XFSDEBUG */

int
XFS_BMAP_RBLOCK_DSIZE(
	int			lev,
	struct xfs_btree_cur	*cur);

int
XFS_BMAP_RBLOCK_ISIZE(
	int			lev,
	struct xfs_btree_cur	*cur);

int
XFS_BMAP_IBLOCK_SIZE(
	int			lev,
	struct xfs_btree_cur	*cur);

int
XFS_BMAP_BLOCK_DSIZE(
	int			lev,
	struct xfs_btree_cur	*cur);

int
XFS_BMAP_BLOCK_ISIZE(
	int			lev,
	struct xfs_btree_cur	*cur);

int
XFS_BMAP_BLOCK_DMAXRECS(
	int			lev,
	struct xfs_btree_cur	*cur);

int
XFS_BMAP_BLOCK_IMAXRECS(
	int			lev,
	struct xfs_btree_cur	*cur);

int
XFS_BMAP_BLOCK_DMINRECS(
	int			lev,
	struct xfs_btree_cur	*cur);

int
XFS_BMAP_BLOCK_IMINRECS(
	int			lev,
	struct xfs_btree_cur	*cur);

xfs_bmbt_rec_t *
XFS_BMAP_REC_DADDR(
	struct xfs_btree_lblock	*bb,
	int			i,
	struct xfs_btree_cur	*cur);

xfs_bmbt_rec_t *
XFS_BMAP_REC_IADDR(
	struct xfs_btree_lblock	*bb,
	int			i,
	struct xfs_btree_cur	*cur);

xfs_bmbt_key_t *
XFS_BMAP_KEY_DADDR(
	struct xfs_btree_lblock	*bb,
	int			i,
	struct xfs_btree_cur	*cur);

xfs_bmbt_key_t *
XFS_BMAP_KEY_IADDR(
	struct xfs_btree_lblock	*bb,
	int			i,
	struct xfs_btree_cur	*cur);

xfs_fsblock_t *
XFS_BMAP_PTR_DADDR(
	struct xfs_btree_lblock	*bb,
	int			i,
	struct xfs_btree_cur	*cur);

xfs_fsblock_t *
XFS_BMAP_PTR_IADDR(
	struct xfs_btree_lblock	*bb,
	int			i,
	struct xfs_btree_cur	*cur);

/*
 * These are to be used when we know the size of the block and
 * we don't have a cursor.
 */
int
XFS_BMAP_BROOT_SIZE(
	int	isz);

xfs_bmbt_rec_t *
XFS_BMAP_BROOT_REC_ADDR(
	struct xfs_btree_lblock	*bb,
	int			i,
	int			isz);

xfs_bmbt_key_t *
XFS_BMAP_BROOT_KEY_ADDR(
	struct xfs_btree_lblock	*bb,
	int			i,
	int			isz);

xfs_fsblock_t *
XFS_BMAP_BROOT_PTR_ADDR(
	struct xfs_btree_lblock	*bb,
	int			i,
	int			isz);

int
XFS_BMAP_BROOT_NUMRECS(
	struct xfs_btree_lblock	*bb);

int
XFS_BMAP_BROOT_MAXRECS(
	int	sz);

int
XFS_BMAP_BROOT_SPACE_CALC(
	int	nrecs);

int
XFS_BMAP_BROOT_SPACE(
	struct xfs_btree_lblock	*bb);

/*
 * Number of extent records that fit in the inode.
 */
int
XFS_BMAP_EXT_MAXRECS(
	int	isz);

struct xfs_btree_lblock *
xfs_bmbt_get_block(
	struct xfs_btree_cur	*cur,
	int			level,
	buf_t			**bpp);

#endif	/* !XFSDEBUG */

/*
 * Prototypes for xfs_bmap.c to call.
 */

int
xfs_bmbt_decrement(
	struct xfs_btree_cur *,
	int);

int
xfs_bmbt_delete(
	struct xfs_btree_cur *);

int
xfs_bmbt_increment(
	struct xfs_btree_cur *,
	int);

int
xfs_bmbt_insert(
	struct xfs_btree_cur *);

#ifdef XFSDEBUG
void
xfs_bmbt_kcheck(
	struct xfs_btree_cur *);
#else
#define	xfs_bmbt_kcheck(a)
#endif	/* XFSDEBUG */

void
xfs_bmbt_log_block(
	struct xfs_btree_cur *,
	buf_t *,
	int);

void
xfs_bmbt_log_recs(
	struct xfs_btree_cur *,
	buf_t *,
	int,
	int);

int
xfs_bmbt_lookup_eq(
	struct xfs_btree_cur *,
	xfs_fsblock_t,
	xfs_fsblock_t,
	xfs_extlen_t);

int
xfs_bmbt_lookup_ge(
	struct xfs_btree_cur *,
	xfs_fsblock_t,
	xfs_fsblock_t,
	xfs_extlen_t);

int
xfs_bmbt_lookup_le(
	struct xfs_btree_cur *,
	xfs_fsblock_t,
	xfs_fsblock_t,
	xfs_extlen_t);

#ifdef XFSDEBUG
void
xfs_bmbt_rcheck(
	struct xfs_btree_cur *);
#else
#define	xfs_bmbt_rcheck(a)
#endif

int
xfs_bmbt_update(
	struct xfs_btree_cur *,
	xfs_fsblock_t,
	xfs_fsblock_t,
	xfs_extlen_t);

#endif	/* _FS_XFS_BMAP_BTREE_H */
