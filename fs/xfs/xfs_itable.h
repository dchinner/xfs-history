#ifndef _FS_XFS_ITABLE_H
#define	_FS_XFS_ITABLE_H

#ident	"$Revision: 1.5 $"

/*
 * Structures returned from xfs_bulkstat syssgi routine.
 */
typedef struct xfs_bstat
{
	ino64_t		bs_ino;		/* inode number */
	mode_t		bs_mode;	/* type and mode */
	nlink_t		bs_nlink;	/* number of links */
	uid_t		bs_uid;		/* user id */
	gid_t		bs_gid;		/* group id */
	dev_t		bs_rdev;	/* device value */
	__int32_t	bs_blksize;	/* block size */		
	off64_t		bs_size;	/* file size */
	timestruc_t	bs_atime;	/* access time */
	timestruc_t	bs_mtime;	/* modify time */
	timestruc_t	bs_ctime;	/* inode change time */
	__int64_t	bs_blocks;	/* number of blocks */
	__uint32_t	bs_xflags;	/* extended flags */
	__int32_t	bs_extsize;	/* extent size */
	__int32_t	bs_extents;	/* number of extents */
	__uint32_t	bs_gen;		/* generation count */
	uuid_t		bs_uuid;	/* unique id of file */
	__uint32_t	bs_dmevmask;	/* DMIG event mask */
	ushort_t	bs_dmstate;	/* DMIG state info */
	ushort_t	bs_padding;	/* not used */
} xfs_bstat_t;

/*
 * Structures returned from xfs_inumbers syssgi routine.
 */
typedef struct xfs_inogrp
{
	ino64_t		xi_startino;	/* starting inode number */
	int		xi_alloccount;	/* count of bits set in allocmask */
	__uint64_t	xi_allocmask;	/* mask of allocated inodes */
} xfs_inogrp_t;

#ifdef _KERNEL
/*
 * Prototypes for visible xfs_itable.c routines.
 */

/*
 * Return stat information in bulk (by-inode) for the filesystem.
 */
int					/* error status */
xfs_bulkstat(
	xfs_mount_t	*mp,		/* mount point for filesystem */
	xfs_trans_t	*tp,		/* transaction pointer */
	ino64_t		*lastino,	/* last inode returned */
	int		*count,		/* size of buffer/count returned */
	caddr_t		ubuffer);	/* buffer with inode stats */

/*
 * Return inode number table for the filesystem.
 */
int					/* error status */
xfs_inumbers(
	xfs_mount_t	*mp,		/* mount point for filesystem */
	xfs_trans_t	*tp,		/* transaction pointer */
	ino64_t		*lastino,	/* last inode returned */
	int		*count,		/* size of buffer/count returned */
	caddr_t		ubuffer);	/* buffer with inode descriptions */
#endif	/* _KERNEL */

#endif	/* !_FS_XFS_ITABLE_H */
