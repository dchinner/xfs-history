#ifndef _FS_XFS_IALLOC_H
#define	_FS_XFS_IALLOC_H

#ident	"$Revision: 1.13 $"

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
 * Prototypes for visible xfs_ialloc.c routines.
 */

/*
 * Allocate an inode on disk.
 * Mode is used to tell whether the new inode will need space, and whether
 * it is a directory.
 * The sameag flag is used by mkfs only, to force the root directory
 * inode into the first allocation group.
 *
 * To work within the constraint of one allocation per transaction,
 * xfs_dialloc() is designed to be called twice if it has to do an
 * allocation to replenish the inode freelist.  If an inode is 
 * available without an allocation, agbuf would be set to NULL and
 * alloc_done set to false.
 * If an allocation needed to be done, agbuf would be set to the
 * header of the freelist and alloc_done set to true.  The caller
 * should then commit the current transaction and allocate a new
 * transaction.  xfs_dialloc() should then be called again with
 * the agbuf value returned from the previous call.
 *
 * agbuf should be set to NULL on the first call.
 */
xfs_ino_t				/* inode number allocated */
xfs_dialloc(
	xfs_trans_t	*tp,		/* transaction pointer */
	xfs_ino_t	parent,		/* parent inode (directory) */
	int		sameag,		/* 1 => must be in same a.g. */
	mode_t		mode,		/* mode bits for new inode */
	buf_t		**agbuf,	/* buf for ag.inode freelist header */
	boolean_t	*alloc_done);	/* an allocation was done to replenish
					   the inode freelist. */

/*
 * Return the next (past agino) inode on the freelist for this allocation group
 * (given by agbuf).  Used to traverse the whole list, e.g. for printing.
 */
xfs_agino_t				/* a.g. inode next on freelist */
xfs_dialloc_next_free(
	xfs_mount_t	*mp,		/* filesystem mount structure */
	xfs_trans_t	*tp,		/* transaction pointer */
	buf_t		*agbuf,		/* buffer for ag.inode header */
	xfs_agino_t	agino);		/* inode to get next free for */

/*
 * Free disk inode.  Carefully avoids touching the incore inode, all
 * manipulations incore are the caller's responsibility.
 */
xfs_agino_t				/* next value to be stored in di_un */
xfs_difree(
	xfs_trans_t	*tp,		/* transaction pointer */
	xfs_ino_t	inode);		/* inode to be freed */

/*
 * Return the location of the inode in bno/off, for mapping it into a buffer.
 */
void
xfs_dilocate(
	xfs_mount_t	*mp,		/* file system mount structure */
	xfs_trans_t	*tp,		/* transaction pointer */
	xfs_ino_t	ino,		/* inode to locate */
	xfs_fsblock_t	*bno,		/* output: block containing inode */
	int		*off);		/* output: index in block of inode */

#endif	/* !_FS_XFS_IALLOC_H */
