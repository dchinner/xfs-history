#ifndef _FS_XFS_DINODE_H
#define	_FS_XFS_DINODE_H

#ident "$Revision: 1.5 $"

#define	XFS_DINODE_VERSION	1
#define	XFS_DINODE_MAGIC	0x494e4f44	/* 'INOD' */

/*
 * Disk inode structure.
 * This is just the header; the inode is expanded to fill a variable size
 * with the last field expanding.  It is split into the core and "other"
 * because we only need the core part in the in-core inode.
 */
typedef struct xfs_dinode_core
{
	__uint32_t	di_magic;	/* inode magic # = XFS_DINODE_MAGIC */
	__uint16_t	di_mode;	/* mode and type of file */
	__int8_t	di_version;	/* inode version */
	__int8_t	di_format;	/* format of di_c data */
	__int16_t	di_nlink;	/* number of links to file */
	__uint16_t	di_uid;		/* owner's user id */
	__uint16_t	di_gid;		/* owner's group id */
	__int64_t	di_size;	/* number of bytes in file */
	uuid_t		di_uuid;	/* file unique id */
	__int64_t	di_nextents;	/* number of extents in file */
	/*
	 * Should these be timestruc_t's??
	 * efs makes them __int32_t's.
	 */
	time_t		di_atime;	/* time last accessed */
	time_t		di_mtime;	/* time last modified */
	time_t		di_ctime;	/* time created/inode modified */
	/*
	 * Should this be 64 bits? What does nfs3.0 want?
	 */
	__uint32_t	di_gen;		/* generation number */
} xfs_dinode_core_t;

typedef struct xfs_dinode
{
	xfs_dinode_core_t	di_core;
	union {
		xfs_agino_t	di_next;/* next inode for freelist inodes */
		dev_t		di_dev;	/* device for IFCHR/IFBLK */
		char		di_c[1];/* local contents */
		xfs_bmbt_rec_t	di_bmx[1];/* extent list */
		xfs_btree_block_t di_bmbt;/* btree root */
		uuid_t		di_muuid;/* mount point value */
	}		di_u;
} xfs_dinode_t;

/*
 * Bit names for logging disk inodes only
 */
#define	XFS_DI_MAGIC	0x0001
#define	XFS_DI_MODE	0x0002
#define	XFS_DI_VERSION	0x0004
#define	XFS_DI_FORMAT	0x0008
#define	XFS_DI_NLINK	0x0010
#define	XFS_DI_UID	0x0020
#define	XFS_DI_GID	0x0040
#define	XFS_DI_SIZE	0x0080
#define	XFS_DI_UUID	0x0100
#define	XFS_DI_NEXTENTS	0x0200
#define	XFS_DI_ATIME	0x0400
#define	XFS_DI_MTIME	0x0800
#define	XFS_DI_CTIME	0x1000
#define	XFS_DI_GEN	0x2000
#define	XFS_DI_U	0x4000
#define	XFS_DI_NUM_BITS	15
#define	XFS_DI_ALL_BITS	((1 << XFS_DI_NUM_BITS) - 1)

/*
 * Values for di_format
 */
typedef enum xfs_dinode_fmt
{
	XFS_DINODE_FMT_AGINO,		/* free inodes: di_next */
	XFS_DINODE_FMT_DEV,		/* CHR, BLK: di_dev */
	XFS_DINODE_FMT_LOCAL,		/* DIR, REG, LNK: di_c */
	XFS_DINODE_FMT_EXTENTS,		/* DIR, REG, LNK: di_bmx */
	XFS_DINODE_FMT_BTREE,		/* DIR, REG, LNK: di_bmbt */
	XFS_DINODE_FMT_UUID 		/* MNT: di_uuid */
} xfs_dinode_fmt_t;

/*
 * File types (mode field)
 */
#define	IFMT		0170000		/* type of file */
#define	IFIFO		0010000		/* named pipe (fifo) */
#define	IFCHR		0020000		/* character special */
#define	IFDIR		0040000		/* directory */
#define	IFBLK		0060000		/* block special */
#define	IFREG		0100000		/* regular */
#define	IFLNK		0120000		/* symbolic link */
#define	IFSOCK		0140000		/* socket */
#define	IFMNT		0160000		/* mount point */

/*
 * File execution and access modes.
 */
#define	ISUID		04000		/* set user id on execution */
#define	ISGID		02000		/* set group id on execution */
#define	ISVTX		01000		/* sticky directory */
#define	IREAD		0400		/* read, write, execute permissions */
#define	IWRITE		0200
#define	IEXEC		0100

#define	xfs_buf_to_dinode(buf)	((xfs_dinode_t *)(buf->b_un.b_addr))

#endif	/* _FS_XFS_DINODE_H */
