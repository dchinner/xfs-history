#ifndef _FS_XFS_IALLOC_H
#define	_FS_XFS_IALLOC_H

#ident	"$Revision$"

/*
 * Allocation parameters.
 * These control how many inodes are allocated at once.
 */
#define	XFS_IALLOC_MAX_EVER_BLOCKS	16
#define	XFS_IALLOC_MAX_EVER_INODES	256
#define	XFS_IALLOC_MAX_EVER(s,a)	xfs_extlen_min(XFS_IALLOC_MAX_EVER_BLOCKS, XFS_IALLOC_MAX_EVER_INODES >> (s)->sb_inopblog)

#define	XFS_IALLOC_MIN_ALLOC(s,a)	1
#define XFS_IALLOC_MAX_ALLOC(s,a)	\
	(((a)->agi_count >> (s)->sb_inopblog) >= XFS_IALLOC_MAX_EVER(s,a) ? \
		XFS_IALLOC_MAX_EVER(s,a) : \
		((a)->agi_count ? ((a)->agi_count >> (s)->sb_inopblog) : 1))

/*
 * Make an inode pointer out of the buffer/offset.
 */
#define	xfs_make_iptr(s,b,o) \
	((xfs_dinode_t *)((caddr_t)xfs_buf_to_block(b) + \
			  ((o) << (s)->sb_inodelog)))

/*
 * Prototypes for per-fs routines.
 */
xfs_ino_t					/* inode number allocated */
xfs_dialloc(xfs_trans_t *tp,			/* transaction pointer */
	    xfs_ino_t parent,			/* parent inode (directory) */
	    int sameag,				/* 1 => must be in same a.g. */
	    mode_t mode);			/* mode bits for new inode */

xfs_agino_t					/* ag.inode next on freelist */
xfs_dialloc_next_free(xfs_mount_t *mp,		/* filesystem mount structure */
		      xfs_trans_t *tp,		/* transaction pointer */
		      buf_t *agbuf,		/* buffer for ag.inode header */
		      xfs_agino_t agino);	/* inode to get next free for */

xfs_agino_t				/* next value to be stored in di_un */
xfs_difree(xfs_trans_t *tp,		/* transaction pointer */
	   xfs_ino_t inode);		/* inode to be freed */

void
xfs_dilocate(xfs_mount_t *mp,		/* file system mount structure */
	     xfs_trans_t *tp,		/* transaction pointer */
	     xfs_ino_t ino,		/* inode to locate */
	     xfs_fsblock_t *bno,	/* output: block containing inode */
	     int *off);			/* output: index in block of inode */

#endif	/* !_FS_XFS_IALLOC_H */
