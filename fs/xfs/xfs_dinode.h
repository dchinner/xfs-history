#ifndef _FS_XFS_DINODE_H
#define	_FS_XFS_DINODE_H

#ident "$Revision$"

#include "xfs_types.h"
#include "xfs_inum.h"

/* 
 * Dummy freelist inode structure.
 */
typedef struct xfs_inode_free
{
	__uint32_t	di_magic;
	__uint16_t	di_mode;
	__int16_t	di_nlink;
	xfs_agino_t	di_next;	/* next free inode, or NULLAGINO */
} xfs_inode_free_t;

#define	XFS_DINODE_VERSION	1
#define	XFS_DINODE_MAGIC	0x494e4f44	/* 'INOD' */

/*
 * Disk inode structure.
 * This is just the header; the inode is expanded to fill a variable size
 * with the last field expanding.
 */
typedef struct xfs_dinode
{
	__uint32_t	di_magic;	/* inode magic # = XFS_DINODE_MAGIC */
	__uint16_t	di_mode;	/* mode and type of file */
	__int16_t	di_nlink;	/* number of links to file */
	__int8_t	di_version;	/* inode version */
	__int8_t	di_pad;		/* unused */
	__uint16_t	di_uid;		/* owner's user id */
	__uint16_t	di_gid;		/* owner's group id */
	__int64_t	di_size;	/* number of bytes in file */
	xfs_uuid_t	di_uuid;	/* file unique id */
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
	union di_addr {
		dev_t	di_dev;		/* device for IFCHR/IFBLK */
		xfs_uuid_t	di_mp;	/* mount point value */
		char	di_c[1];	/* extensible information */
	}		di_u;
} xfs_dinode_t;

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
