#ifndef _FS_XFS_BMAP_BTREE_H
#define	_FS_XFS_BMAP_BTREE_H

#ident "$Revision: 1.8 $"

#define	XFS_BMAP_MAGIC	0x424d4150	/* 'BMAP' */

struct xfs_btree_cur;
struct xfs_btree_lblock;

/*
 * Bmap root header, on-disk form only.
 */
typedef struct xfs_bmdr_block_t
{
	__uint16_t	bb_level;	/* 0 is a leaf */
	__uint16_t	bb_numrecs;	/* current # of data records */
} xfs_bmdr_block_t;

/*
 * Bmap btree record and extent descriptor.
 * l0:0-31 and l1:9-31 are startoff.
 * l1:0-8, l2:0-31, and l3:21-31 are startblock.
 * l3:0-20 are blockcount.
 */
typedef struct xfs_bmbt_rec
{
	__uint32_t	l0, l1, l2, l3;
} xfs_bmbt_rec_t, xfs_bmdr_rec_t;

/*
 * Values and macros for delayed-allocation startblock fields.
 */
#define	STARTBLOCKVALBITS	16
#define	STARTBLOCKMASKBITS	(16 + XFS_BIG_FILESYSTEMS * 20)
#define	DSTARTBLOCKMASKBITS	(16 + 20)
#define	STARTBLOCKMASK		\
	(((((xfs_fsblock_t)1) << STARTBLOCKMASKBITS) - 1) << STARTBLOCKVALBITS)
#define	DSTARTBLOCKMASK		\
	(((((xfs_dfsbno_t)1) << DSTARTBLOCKMASKBITS) - 1) << STARTBLOCKVALBITS)
#define	ISNULLSTARTBLOCK(x)	(((x) & STARTBLOCKMASK) == STARTBLOCKMASK)
#define	ISNULLDSTARTBLOCK(x)	(((x) & DSTARTBLOCKMASK) == DSTARTBLOCKMASK)
#define	NULLSTARTBLOCK(k)	\
	((ASSERT(k < (1 << STARTBLOCKVALBITS))), (STARTBLOCKMASK | (k)))
#define	STARTBLOCKVAL(x)	((xfs_extlen_t)((x) & ~STARTBLOCKMASK))

/*
 * Incore version of above.
 */
typedef struct xfs_bmbt_irec
{
	xfs_fileoff_t	br_startoff;	/* starting file offset */
	xfs_fsblock_t	br_startblock;	/* starting block number */
	xfs_extlen_t	br_blockcount;	/* number of block */
} xfs_bmbt_irec_t;

/*
 * Key structure for non-leaf levels of the tree.
 */
typedef struct xfs_bmbt_key
{
	xfs_dfiloff_t	br_startoff;	/* starting file offset */
} xfs_bmbt_key_t, xfs_bmdr_key_t;

typedef xfs_dfsbno_t xfs_bmbt_ptr_t, xfs_bmdr_ptr_t;	/* btree pointer type */
					/* btree block header type */
typedef	struct xfs_btree_lblock xfs_bmbt_block_t;

#define	XFS_BUF_TO_BMBT_BLOCK(bp) ((xfs_bmbt_block_t *)((bp)->b_un.b_addr))

#define	XFS_BMAP_RBLOCK_DSIZE(lev,cur) \
	((cur)->bc_private.b.inodesize - (int)sizeof(xfs_dinode_core_t))
#define	XFS_BMAP_RBLOCK_ISIZE(lev,cur) \
	((int)(cur)->bc_private.b.ip->i_broot_bytes)
#define	XFS_BMAP_IBLOCK_SIZE(lev,cur) (1 << (cur)->bc_blocklog)

#define	XFS_BMAP_BLOCK_DSIZE(lev,cur) \
	((lev) == (cur)->bc_nlevels - 1 ? \
		XFS_BMAP_RBLOCK_DSIZE(lev,cur) : \
		XFS_BMAP_IBLOCK_SIZE(lev,cur))
#define	XFS_BMAP_BLOCK_ISIZE(lev,cur) \
	((lev) == (cur)->bc_nlevels - 1 ? \
		XFS_BMAP_RBLOCK_ISIZE(lev,cur) : \
		XFS_BMAP_IBLOCK_SIZE(lev,cur))

#define	XFS_BMAP_BLOCK_DMAXRECS(lev,cur) \
	((cur)->bc_mp->m_bmap_dmxr[((lev) != 0) + \
				   (((lev) == (cur)->bc_nlevels - 1) << 1)])
#define	XFS_BMAP_BLOCK_IMAXRECS(lev,cur) \
	((lev) == (cur)->bc_nlevels - 1 ? \
		XFS_BTREE_BLOCK_MAXRECS(XFS_BMAP_RBLOCK_ISIZE(lev,cur), \
			xfs_bmbt, lev == 0) : \
		((cur)->bc_mp->m_bmap_dmxr[(lev) != 0]))

#define	XFS_BMAP_BLOCK_DMINRECS(lev,cur) \
	((cur)->bc_mp->m_bmap_dmnr[((lev) != 0) + \
				   (((lev) == (cur)->bc_nlevels - 1) << 1)])
#define	XFS_BMAP_BLOCK_IMINRECS(lev,cur) \
	((lev) == (cur)->bc_nlevels - 1 ? \
		XFS_BTREE_BLOCK_MINRECS(XFS_BMAP_RBLOCK_ISIZE(lev,cur), \
			xfs_bmbt, lev == 0) : \
		((cur)->bc_mp->m_bmap_dmnr[(lev) != 0]))

#define	XFS_BMAP_REC_DADDR(bb,i,cur) \
	XFS_BTREE_REC_ADDR(XFS_BMAP_BLOCK_DSIZE((bb)->bb_level,cur), xfs_bmbt, \
			   bb, i, XFS_BMAP_BLOCK_DMAXRECS((bb)->bb_level, cur))
#define	XFS_BMAP_REC_IADDR(bb,i,cur) \
	XFS_BTREE_REC_ADDR(XFS_BMAP_BLOCK_ISIZE((bb)->bb_level,cur), xfs_bmbt, \
			   bb, i, XFS_BMAP_BLOCK_IMAXRECS((bb)->bb_level, cur))

#define	XFS_BMAP_KEY_DADDR(bb,i,cur) \
	XFS_BTREE_KEY_ADDR(XFS_BMAP_BLOCK_DSIZE((bb)->bb_level,cur), xfs_bmbt, \
			   bb, i, XFS_BMAP_BLOCK_DMAXRECS((bb)->bb_level, cur))
#define	XFS_BMAP_KEY_IADDR(bb,i,cur) \
	XFS_BTREE_KEY_ADDR(XFS_BMAP_BLOCK_ISIZE((bb)->bb_level,cur), xfs_bmbt, \
			   bb, i, XFS_BMAP_BLOCK_IMAXRECS((bb)->bb_level, cur))

#define	XFS_BMAP_PTR_DADDR(bb,i,cur) \
	XFS_BTREE_PTR_ADDR(XFS_BMAP_BLOCK_DSIZE((bb)->bb_level,cur), xfs_bmbt, \
			   bb, i, XFS_BMAP_BLOCK_DMAXRECS((bb)->bb_level, cur))
#define	XFS_BMAP_PTR_IADDR(bb,i,cur) \
	XFS_BTREE_PTR_ADDR(XFS_BMAP_BLOCK_ISIZE((bb)->bb_level,cur), xfs_bmbt, \
			   bb, i, XFS_BMAP_BLOCK_IMAXRECS((bb)->bb_level, cur))

/*
 * These are to be used when we know the size of the block and
 * we don't have a cursor.
 */
#define	XFS_BMAP_BROOT_SIZE(isz) ((isz) - sizeof(xfs_dinode_core_t)) 
#define	XFS_BMAP_BROOT_REC_ADDR(bb,i,sz) \
	XFS_BTREE_REC_ADDR(sz,xfs_bmbt,bb,i,XFS_BMAP_BROOT_MAXRECS(sz))
#define	XFS_BMAP_BROOT_KEY_ADDR(bb,i,sz) \
	XFS_BTREE_KEY_ADDR(sz,xfs_bmbt,bb,i,XFS_BMAP_BROOT_MAXRECS(sz))
#define XFS_BMAP_BROOT_PTR_ADDR(bb,i,sz) \
	XFS_BTREE_PTR_ADDR(sz,xfs_bmbt,bb,i,XFS_BMAP_BROOT_MAXRECS(sz))

#define	XFS_BMAP_BROOT_NUMRECS(bb) ((bb)->bb_numrecs)
#define	XFS_BMAP_BROOT_MAXRECS(sz) XFS_BTREE_BLOCK_MAXRECS(sz,xfs_bmbt,0)
#define	XFS_BMAP_BROOT_SPACE_CALC(nrecs) \
	((int)(sizeof(xfs_bmbt_block_t) + \
	       ((nrecs) * (sizeof(xfs_bmbt_key_t) + sizeof(xfs_bmbt_ptr_t)))))
#define	XFS_BMAP_BROOT_SPACE(bb) XFS_BMAP_BROOT_SPACE_CALC((bb)->bb_numrecs)

/*
 * Number of extent records that fit in the inode.
 */
#define	XFS_BMAP_EXT_MAXRECS(mp)	((mp)->m_bmap_ext_mxr)
/*
 * Maximum number of bmap btree levels.
 */
#define	XFS_BM_MAXLEVELS(mp)		((mp)->m_bm_maxlevels)

/*
 * Prototypes for xfs_bmap.c to call.
 */

void
xfs_bmdr_to_bmbt(
	xfs_bmdr_block_t *,
	int,
	xfs_bmbt_block_t *,
	int);

int
xfs_bmbt_decrement(
	struct xfs_btree_cur *,
	int);

int
xfs_bmbt_delete(
	struct xfs_btree_cur *);

void
xfs_bmbt_get_all(
	xfs_bmbt_rec_t	*r,
	xfs_bmbt_irec_t	*s);

xfs_bmbt_block_t *
xfs_bmbt_get_block(
	struct xfs_btree_cur	*cur,
	int			level,
	buf_t			**bpp);

xfs_extlen_t
xfs_bmbt_get_blockcount(
	xfs_bmbt_rec_t	*r);

xfs_fsblock_t
xfs_bmbt_get_startblock(
	xfs_bmbt_rec_t	*r);

xfs_fileoff_t
xfs_bmbt_get_startoff(
	xfs_bmbt_rec_t	*r);

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
	xfs_fileoff_t,
	xfs_fsblock_t,
	xfs_extlen_t);

int
xfs_bmbt_lookup_ge(
	struct xfs_btree_cur *,
	xfs_fileoff_t,
	xfs_fsblock_t,
	xfs_extlen_t);

int
xfs_bmbt_lookup_le(
	struct xfs_btree_cur *,
	xfs_fileoff_t,
	xfs_fsblock_t,
	xfs_extlen_t);

#ifdef XFSDEBUG
void
xfs_bmbt_rcheck(
	struct xfs_btree_cur *);
#else
#define	xfs_bmbt_rcheck(a)
#endif

void
xfs_bmbt_set_all(
	xfs_bmbt_rec_t	*r,
	xfs_bmbt_irec_t	*s);

void
xfs_bmbt_set_blockcount(
	xfs_bmbt_rec_t	*r,
	xfs_extlen_t	v);

void
xfs_bmbt_set_startblock(
	xfs_bmbt_rec_t	*r,
	xfs_fsblock_t	v);

void
xfs_bmbt_set_startoff(
	xfs_bmbt_rec_t	*r,
	xfs_fileoff_t	v);

void
xfs_bmbt_to_bmdr(
	xfs_bmbt_block_t *,
	int,
	xfs_bmdr_block_t *,
	int);

int
xfs_bmbt_update(
	struct xfs_btree_cur *,
	xfs_fileoff_t,
	xfs_fsblock_t,
	xfs_extlen_t);

#endif	/* _FS_XFS_BMAP_BTREE_H */
