#ifndef _FS_XFS_INUM_H
#define	_FS_XFS_INUM_H

#ident	"$Revision$"

#define	XFS_INODE_LOWBITS	32
#define	XFS_INODE_HIGHBITS	32

typedef	__uint64_t	xfs_ino_t;	/* inode number */
typedef	__uint32_t	xfs_agino_t;	/* within allocation grp inode number */

#define	NULLFSINO	((xfs_ino_t)-1)
#define	NULLAGINO	((xfs_agino_t)-1)

#define xfs_ino_to_agno(i)	((xfs_agnumber_t)((i) >> XFS_INODE_LOWBITS))
#define	xfs_ino_to_agino(i)	((xfs_agino_t)((i) & ((1LL << XFS_INODE_LOWBITS) - 1LL)))
#define	xfs_agino_to_ino(a,i)	((((xfs_ino_t)(a)) << XFS_INODE_LOWBITS) | (i))

#endif	/* !_FS_XFS_INUM_H */
