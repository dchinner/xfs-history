#ifndef _FS_XFS_IALLOC_H
#define	_FS_XFS_IALLOC_H

#ident	"$Revision$"

/*
 * block numbers in the AG; SB is block 0, AGH is block 1, free btree roots
 * are 2 and 3.
 */
#define	XFS_IBT_BLOCK	((xfs_agblock_t)(XFS_CNT_BLOCK + 1))

#define	XFS_PREALLOC_BLOCKS	((xfs_agblock_t)(XFS_IBT_BLOCK + 1))

#define	XFS_IALLOC_MAX_EVER	1024
#define	XFS_IALLOC_MIN_ALLOC(s,a)	((s)->xfsb_inopblock)
#define XFS_IALLOC_MAX_ALLOC(s,a)	\
	((a)->xfsag_icount > XFS_IALLOC_MAX_EVER ? \
	XFS_IALLOC_MAX_EVER : \
	((a)->xfsag_icount) ? (a)->xfsag_icount : XFS_IALLOC_MIN_ALLOC(s,a))

/*
 * Structures for inode mapping
 */
#define	XFS_IBT_MAGIC	0x58494254	/* 'XIBT' */

/*
 * Data record/key structure
 */
typedef struct xfs_ialloc_rec
{
	xfs_agino_t	startinode;	/* starting inode number */
	xfs_agblock_t	startblock;	/* starting block number */
	xfs_agino_t	inodecount;	/* number of inodes */
} xfs_ialloc_rec_t;

/*
 * Real block structures have a size equal to the file system block size.
 */

#define	XFS_IALLOC_BLOCK_SIZE(bl,lev,cur)	(1 << (bl))

#define	XFS_IALLOC_BLOCK_MAXRECS(bl,lev,cur)	\
	XFS_BTREE_BLOCK_MAXRECS(XFS_IALLOC_BLOCK_SIZE(bl,lev,cur), \
				xfs_ialloc_rec_t, lev)
#define	XFS_IALLOC_BLOCK_MINRECS(bl,lev,cur)	\
	XFS_BTREE_BLOCK_MINRECS(XFS_IALLOC_BLOCK_SIZE(bl,lev,cur), \
				xfs_ialloc_rec_t, lev)

#define	XFS_IALLOC_REC_ADDR(bb,i,bl,cur)	\
	XFS_BTREE_REC_ADDR(XFS_IALLOC_BLOCK_SIZE(bl,(bb)->level,cur), \
			   xfs_ialloc_rec_t, bb, i)

#define	XFS_IALLOC_PTR_ADDR(bb,i,bl,cur)	\
	XFS_BTREE_PTR_ADDR(XFS_IALLOC_BLOCK_SIZE(bl,(bb)->level,cur), \
			   xfs_ialloc_rec_t, bb, i)

#define	XFS_IALLOC_MAXLEVELS	5 /* ??? */

typedef struct xfs_ialloc_cur
{
	xfs_trans_t		*tp;	/* links cursors on freelist */
	buf_t			*agbuf;
	xfs_agnumber_t		agno;
	xfs_ialloc_rec_t	rec;
	buf_t			*bufs[XFS_IALLOC_MAXLEVELS];
	int			ptrs[XFS_IALLOC_MAXLEVELS];
	int			nlevels;
	xfs_btnum_t		btnum;	/* == XFS_BTNUM_IBT */
#ifdef XFSIDEBUG
	int			lineno;
	struct xfs_ialloc_cur	*next;	/* on all cursors list */
#endif
} xfs_ialloc_cur_t;

#define	xfs_make_iptr(s,b,o) \
	((xfs_dinode_t *)((caddr_t)xfs_buf_to_block(b) + (o) * (s)->xfsb_inodesize))

/*
 * Prototypes for per-ag routines.
 */
int xfs_ialloc_ag_alloc(xfs_ialloc_cur_t *);
int xfs_ialloc_ag_locate(xfs_ialloc_cur_t *, xfs_agino_t, xfs_agblock_t *, int *);
buf_t *xfs_ialloc_ag_select(xfs_trans_t *, xfs_ino_t, int, int);

/*
 * Prototypes for per-fs routines.
 */
xfs_ino_t xfs_ialloc(xfs_trans_t *, xfs_ino_t, int, int);
xfs_agino_t xfs_ialloc_next_free(xfs_trans_t *, buf_t *, xfs_agino_t);
int xfs_ifree(xfs_trans_t *, xfs_ino_t);

#endif	/* !_FS_XFS_IALLOC_H */
