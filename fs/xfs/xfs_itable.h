#ifndef _FS_XFS_ITABLE_H
#define	_FS_XFS_ITABLE_H

#ident	"$Revision: 1.7 $"

struct xfs_mount;

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
 * Convert file descriptor of a file in the filesystem to
 * a mount structure pointer.
 */
int					/* error status */
xfs_fd_to_mp(
	int			fd,	/* file descriptor */
	struct xfs_mount	**mpp);	/* output: mount structure pointer */

/*
 * Syssgi interface for bulkstat and inode-table.
 */
int					/* error status */
xfs_itable(
	int		opc,		/* op code */
	int		fd,		/* file descriptor of file in fs. */
	caddr_t		lastip,		/* last inode number pointer */
	int		icount,		/* count of entries in buffer */
	caddr_t		ubuffer,	/* buffer with inode descriptions */
	caddr_t		ocount);	/* output count */
#endif	/* _KERNEL */

/*
 * Values for di_flags
 */
#define XFS_DIFLAG_REALTIME     0x1     /* file's blocks come from rt area */
#define XFS_DIFLAG_ALL  (XFS_DIFLAG_REALTIME)


#endif	/* !_FS_XFS_ITABLE_H */
