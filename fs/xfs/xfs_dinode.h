#ifndef _FS_XFS_DINODE_H
#define	_FS_XFS_DINODE_H

#ident "$Revision: 1.19 $"

#define	XFS_DINODE_VERSION	1
#define	XFS_DINODE_MAGIC	0x494e	/* 'IN' */

/*
 * Disk inode structure.
 * This is just the header; the inode is expanded to fill a variable size
 * with the last field expanding.  It is split into the core and "other"
 * because we only need the core part in the in-core inode.
 */
typedef struct xfs_timestamp {
	__int32_t	t_sec;		/* timestamp seconds */
	__int32_t	t_nsec;		/* timestamp nanoseconds */
} xfs_timestamp_t;

typedef struct xfs_dinode_core
{
	__uint16_t	di_magic;	/* inode magic # = XFS_DINODE_MAGIC */
	__uint16_t	di_mode;	/* mode and type of file */
	__int8_t	di_version;	/* inode version */
	__int8_t	di_format;	/* format of di_c data */
	__int16_t	di_nlink;	/* number of links to file */
	__uint16_t	di_uid;		/* owner's user id */
	__uint16_t	di_gid;		/* owner's group id */
	xfs_extnum_t	di_nextents;	/* number of extents in file */
	uuid_t		di_uuid;	/* file unique id */
	xfs_fsize_t	di_size;	/* number of bytes in file */
	/*
	 * While these fields hold 64 bit values, we will only
	 * be using the upper 32 bits for now.  The t_nsec
	 * portion of the fields should always be zero.  This
	 * leaves room for expansion in the future if necessary.
	 */
	xfs_timestamp_t	di_atime;	/* time last accessed */
	xfs_timestamp_t	di_mtime;	/* time last modified */
	xfs_timestamp_t	di_ctime;	/* time created/inode modified */
	/*
	 * Should this be 64 bits? What does nfs3.0 want?
	 */
	__uint32_t	di_gen;		/* generation number */
	xfs_extlen_t	di_extsize;	/* basic/minimum extent size for file */
	__uint32_t	di_flags;	/* random flags, XFS_DIFLAG_... */
	xfs_agino_t	di_nexti;	/* next allocated inode in ag */
	xfs_drfsbno_t	di_nblocks;	/* # of direct & btree blocks used */
} xfs_dinode_core_t;

typedef struct xfs_dinode
{
	xfs_dinode_core_t	di_core;
	union {
		xfs_agino_t	di_next;/* next inode for freelist inodes */
		dev_t		di_dev;	/* device for IFCHR/IFBLK */
		char		di_c[1];/* local contents */
		xfs_bmbt_rec_t	di_bmx[1];/* extent list */
		xfs_bmdr_block_t di_bmbt;/* btree root block */
		uuid_t		di_muuid;/* mount point value */
	}		di_u;
} xfs_dinode_t;

/*
 * Bit names for logging disk inodes only
 */
#define	XFS_DI_MAGIC	0x00001
#define	XFS_DI_MODE	0x00002
#define	XFS_DI_VERSION	0x00004
#define	XFS_DI_FORMAT	0x00008
#define	XFS_DI_NLINK	0x00010
#define	XFS_DI_UID	0x00020
#define	XFS_DI_GID	0x00040
#define	XFS_DI_NEXTENTS	0x00080
#define	XFS_DI_UUID	0x00100
#define	XFS_DI_SIZE	0x00200
#define	XFS_DI_ATIME	0x00400
#define	XFS_DI_MTIME	0x00800
#define	XFS_DI_CTIME	0x01000
#define	XFS_DI_GEN	0x02000
#define	XFS_DI_EXTSIZE	0x04000
#define	XFS_DI_FLAGS	0x08000
#define	XFS_DI_NEXTI	0x10000
#define	XFS_DI_NBLOCKS	0x20000
#define	XFS_DI_U	0x40000
#define	XFS_DI_NUM_BITS	19
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
 * Inode minimum and maximum sizes.
 */
#define	XFS_DINODE_MIN_LOG	7
#define	XFS_DINODE_MAX_LOG	11
#define	XFS_DINODE_MIN_SIZE	(1 << XFS_DINODE_MIN_LOG)
#define	XFS_DINODE_MAX_SIZE	(1 << XFS_DINODE_MAX_LOG)

/*
 * Inode size for given fs.
 */
#define	XFS_LITINO(mp)	((mp)->m_sb.sb_inodesize - sizeof(xfs_dinode_core_t))

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

/*
 * Values for di_flags
 */
#define	XFS_DIFLAG_REALTIME	0x1	/* file's blocks come from rt area */

#define	XFS_DIFLAG_ALL	(XFS_DIFLAG_REALTIME)

#define	XFS_BUF_TO_DINODE(buf)	((xfs_dinode_t *)(buf->b_un.b_addr))

#endif	/* _FS_XFS_DINODE_H */
