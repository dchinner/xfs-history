#ifndef _FS_XFS_IALLOC_H
#define	_FS_XFS_IALLOC_H

#ident	"$Revision: 1.19 $"

/*
 * Allocation parameters for inode allocation.
 */
#define	XFS_IALLOC_MAX(a,b)	((a) > (b) ? (a) : (b))
#define	XFS_IALLOC_INODES(mp)	\
	XFS_IALLOC_MAX(XFS_INODES_PER_CHUNK, (mp)->m_sb.sb_inopblock)
#define	XFS_IALLOC_BLOCKS(mp)	\
	(XFS_IALLOC_INODES(mp) >> (mp)->m_sb.sb_inopblog)

/*
 * Make an inode pointer out of the buffer/offset.
 */
#define	XFS_MAKE_IPTR(mp,b,o) \
	((xfs_dinode_t *)((b)->b_un.b_addr + ((o) << (mp)->m_sb.sb_inodelog)))

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
 * allocation to make more free inodes.  If an inode is 
 * available without an allocation, agbp would be set to the current
 * agbp and alloc_done set to false.
 * If an allocation needed to be done, agbp would be set to the
 * inode header of the allocation group and alloc_done set to true.
 * The caller should then commit the current transaction and allocate a new
 * transaction.  xfs_dialloc() should then be called again with
 * the agbp value returned from the previous call.
 *
 * Once we successfully pick an inode its number is returned and the
 * on-disk data structures are updated.  The inode itself is not read
 * in, since doing so would break ordering constraints with xfs_reclaim.
 *
 * *agbp should be set to NULL on the first call, *alloc_done set to FALSE.
 */
xfs_ino_t				/* inode number allocated */
xfs_dialloc(
	xfs_trans_t	*tp,		/* transaction pointer */
	xfs_ino_t	parent,		/* parent inode (directory) */
	int		sameag,		/* 1 => must be in same a.g. */
	mode_t		mode,		/* mode bits for new inode */
	buf_t		**agbp,		/* buf for a.g. inode header */
	boolean_t	*alloc_done);	/* an allocation was done to replenish
					   the free inodes */
/*
 * Free disk inode.  Carefully avoids touching the incore inode, all
 * manipulations incore are the caller's responsibility.
 * The on-disk inode is not changed by this operation, only the
 * btree (free inode mask) is changed.
 */
void
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

/*
 * Compute and fill in value of m_in_maxlevels.
 */
void
xfs_ialloc_compute_maxlevels(
	xfs_mount_t	*mp);		/* file system mount structure */

/*
 * Find a free inode in the bitmask for the inode chunk.
 */
int
xfs_ialloc_find_free(
	xfs_inofree_t	*fp);		/* free inode mask */

/*
 * Log specified fields for the ag hdr (inode section)
 */
void
xfs_ialloc_log_agi(
	xfs_trans_t	*tp,		/* transaction pointer */
	buf_t		*bp,		/* allocation group header buffer */
	int		fields);	/* bitmask of fields to log */

#endif	/* !_FS_XFS_IALLOC_H */
