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
#define	XFS_IALLOC_MIN_ALLOC(s,a)	((s)->sb_inopblock)
#define XFS_IALLOC_MAX_ALLOC(s,a)	\
	((a)->ag_icount > XFS_IALLOC_MAX_EVER ? \
	XFS_IALLOC_MAX_EVER : \
	((a)->ag_icount) ? (a)->ag_icount : XFS_IALLOC_MIN_ALLOC(s,a))

/*
 * Structures for inode mapping
 */
#define	XFS_IBT_MAGIC	0x58494254	/* 'XIBT' */

/*
 * Data record/key structure
 */
typedef struct xfs_ialloc_rec
{
	xfs_agino_t	ir_startinode;	/* starting inode number */
	xfs_agblock_t	ir_startblock;	/* starting block number */
	xfs_agino_t	ir_inodecount;	/* number of inodes */
} xfs_ialloc_rec_t;

/*
 * Real block structures have a size equal to the file system block size.
 */

#define	XFS_IALLOC_BLOCK_SIZE(lev,cur)	(1 << (cur)->bc_blocklog)

#define	XFS_IALLOC_BLOCK_MAXRECS(lev,cur)	\
	XFS_BTREE_BLOCK_MAXRECS(XFS_IALLOC_BLOCK_SIZE(lev,cur), \
				xfs_ialloc_rec_t, lev)
#define	XFS_IALLOC_BLOCK_MINRECS(lev,cur)	\
	XFS_BTREE_BLOCK_MINRECS(XFS_IALLOC_BLOCK_SIZE(lev,cur), \
				xfs_ialloc_rec_t, lev)

#define	XFS_IALLOC_REC_ADDR(bb,i,cur)	\
	XFS_BTREE_REC_ADDR(XFS_IALLOC_BLOCK_SIZE((bb)->bb_level,cur), \
			   xfs_ialloc_rec_t, bb, i)

#define	XFS_IALLOC_PTR_ADDR(bb,i,cur)	\
	XFS_BTREE_PTR_ADDR(XFS_IALLOC_BLOCK_SIZE((bb)->bb_level,cur), \
			   xfs_ialloc_rec_t, bb, i)

#define	XFS_IALLOC_MAXLEVELS	5 /* ??? */

#define	xfs_make_iptr(s,b,o) \
	((xfs_dinode_t *)((caddr_t)xfs_buf_to_block(b) + ((o) << (s)->sb_inodelog)))

/*
 * Prototypes for per-fs routines.
 */
xfs_ino_t xfs_dialloc(xfs_trans_t *, xfs_ino_t, int, int);
xfs_agino_t xfs_dialloc_next_free(xfs_mount_t *, xfs_trans_t *, buf_t *, xfs_agino_t);
int xfs_difree(xfs_trans_t *, xfs_ino_t);
int xfs_dilocate(xfs_mount_t *, xfs_trans_t *, xfs_ino_t, xfs_fsblock_t *, int *);

#endif	/* !_FS_XFS_IALLOC_H */
