#ifndef _FS_XFS_DINODE_H
#define	_FS_XFS_DINODE_H

#ident "$Revision: 1.32 $"

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

/*
 * Note: Coordinate changes to this structure with the XFS_DI_* #defines
 * below and the offsets table in xfs_ialloc_log_di().
 */
typedef struct xfs_dinode_core
{
	__uint16_t	di_magic;	/* inode magic # = XFS_DINODE_MAGIC */
	__uint16_t	di_mode;	/* mode and type of file */
	__int8_t	di_version;	/* inode version */
	__int8_t	di_format;	/* format of di_c data */
	__int16_t	di_nlink;	/* number of links to file */
	__uint32_t	di_uid;		/* owner's user id */
	__uint32_t	di_gid;		/* owner's group id */
	uuid_t		di_uuid;	/* file unique id */
	xfs_timestamp_t	di_atime;	/* time last accessed */
	xfs_timestamp_t	di_mtime;	/* time last modified */
	xfs_timestamp_t	di_ctime;	/* time created/inode modified */
	xfs_fsize_t	di_size;	/* number of bytes in file */
	xfs_drfsbno_t	di_nblocks;	/* # of direct & btree blocks used */
	xfs_extlen_t	di_extsize;	/* basic/minimum extent size for file */
	xfs_extnum_t	di_nextents;	/* number of extents in data fork */
	xfs_aextnum_t	di_anextents;	/* number of extents in attribute fork*/
	__uint8_t	di_forkoff;	/* attr fork offs, <<3 for 64b align */
	__int8_t	di_aformat;	/* format of attr fork's data */
	__uint32_t	di_dmevmask;	/* DMIG event mask */
	__uint16_t	di_dmstate;	/* DMIG state info */
	__uint16_t	di_flags;	/* random flags, XFS_DIFLAG_... */
	__uint32_t	di_gen;		/* generation number */
} xfs_dinode_core_t;

typedef struct xfs_dinode
{
	xfs_dinode_core_t	di_core;
	/*
	 * In adding anything between the core and the union, be
	 * sure to update the macros like XFS_LITINO below and
	 * XFS_BMAP_RBLOCK_DSIZE in xfs_bmap_btree.h.
	 */
	xfs_agino_t		di_next_unlinked;/* agi unlinked list ptr */
	union {
		xfs_bmdr_block_t di_bmbt;	/* btree root block */
		xfs_bmbt_rec_32_t di_bmx[1];	/* extent list */
		xfs_dir_shortform_t di_dirsf;	/* shortform directory */
		xfs_attr_shortform_t di_attrsf;	/* shortform attribute list */
		char		di_c[1];	/* local contents */
		dev_t		di_dev;		/* device for IFCHR/IFBLK */
		uuid_t		di_muuid;	/* mount point value */
		char		di_symlink[1];	/* local symbolic link */
	}		di_u;
} xfs_dinode_t;

/*
 * Bit names for logging disk inodes only
 */
#define	XFS_DI_MAGIC		0x000001
#define	XFS_DI_MODE		0x000002
#define	XFS_DI_VERSION		0x000004
#define	XFS_DI_FORMAT		0x000008
#define	XFS_DI_NLINK		0x000010
#define	XFS_DI_UID		0x000020
#define	XFS_DI_GID		0x000040
#define	XFS_DI_UUID		0x000080
#define	XFS_DI_ATIME		0x000100
#define	XFS_DI_MTIME		0x000200
#define	XFS_DI_CTIME		0x000400
#define	XFS_DI_SIZE		0x000800
#define	XFS_DI_NBLOCKS		0x001000
#define	XFS_DI_EXTSIZE		0x002000
#define	XFS_DI_NEXTENTS		0x004000
#define	XFS_DI_NAEXTENTS	0x008000
#define	XFS_DI_FORKOFF		0x010000
#define	XFS_DI_AFORMAT		0x020000
#define	XFS_DI_DMEVMASK		0x040000
#define	XFS_DI_DMSTATE		0x080000
#define	XFS_DI_FLAGS		0x100000
#define	XFS_DI_GEN		0x200000
#define	XFS_DI_NEXT_UNLINKED	0x400000
#define	XFS_DI_U		0x800000
#define	XFS_DI_NUM_BITS		24
#define	XFS_DI_ALL_BITS		((1 << XFS_DI_NUM_BITS) - 1)
#define	XFS_DI_CORE_BITS	(XFS_DI_ALL_BITS & ~XFS_DI_U)

/*
 * Values for di_format
 */
typedef enum xfs_dinode_fmt
{
	XFS_DINODE_FMT_DEV,		/* CHR, BLK: di_dev */
	XFS_DINODE_FMT_LOCAL,		/* DIR, REG: di_c */
					/* LNK: di_symlink */
	XFS_DINODE_FMT_EXTENTS,		/* DIR, REG, LNK: di_bmx */
	XFS_DINODE_FMT_BTREE,		/* DIR, REG, LNK: di_bmbt */
	XFS_DINODE_FMT_UUID 		/* MNT: di_uuid */
} xfs_dinode_fmt_t;

/*
 * Inode minimum and maximum sizes.
 */
#define	XFS_DINODE_MIN_LOG	8
#define	XFS_DINODE_MAX_LOG	11
#define	XFS_DINODE_MIN_SIZE	(1 << XFS_DINODE_MIN_LOG)
#define	XFS_DINODE_MAX_SIZE	(1 << XFS_DINODE_MAX_LOG)

/*
 * Inode size for given fs.
 */
#define	XFS_LITINO(mp)	((mp)->m_sb.sb_inodesize - \
			 (sizeof(xfs_dinode_core_t) + sizeof(xfs_agino_t)))
#define	XFS_BROOT_SIZE_ADJ	\
	(sizeof(xfs_bmbt_block_t) - sizeof(xfs_bmdr_block_t))

/*
 * Fork identifiers.  Here so utilities can use them without including
 * xfs_inode.h.
 */
#define	XFS_DATA_FORK	0
#define	XFS_ATTR_FORK	1

/*
 * Inode data & attribute fork sizes, per inode.
 */
#define	XFS_CFORK_Q(dcp)	((dcp)->di_forkoff != 0)
#define	XFS_CFORK_BOFF(dcp)	((dcp)->di_forkoff << 3)
#define	XFS_CFORK_DSIZE(dcp,mp) \
	(XFS_CFORK_Q(dcp) ? XFS_CFORK_BOFF(dcp) : XFS_LITINO(mp))
#define	XFS_CFORK_ASIZE(dcp,mp) \
	(XFS_CFORK_Q(dcp) ? XFS_LITINO(mp) - XFS_CFORK_BOFF(dcp) : 0)
#define	XFS_CFORK_SIZE(dcp,mp,w) \
	((w) == XFS_DATA_FORK ? \
		XFS_CFORK_DSIZE(dcp, mp) : XFS_CFORK_ASIZE(dcp, mp))

#define	XFS_DFORK_DSIZE(dip,mp)		XFS_CFORK_DSIZE(&dip->di_core, mp)
#define	XFS_DFORK_ASIZE(dip,mp)		XFS_CFORK_ASIZE(&dip->di_core, mp)
#define	XFS_DFORK_SIZE(dip,mp,w)	XFS_CFORK_SIZE(&dip->di_core, mp, w)

/*
 * Macros for accessing per-fork disk inode information.
 */
#define	XFS_DFORK_Q(dip)	XFS_CFORK_Q(&dip->di_core)
#define	XFS_DFORK_BOFF(dip)	XFS_CFORK_BOFF(&(dip)->di_core)
#define	XFS_DFORK_PTR(dip,w)	\
	((w) == XFS_DATA_FORK ? \
		(dip)->di_u.di_c : \
		(dip)->di_u.di_c + XFS_DFORK_BOFF(dip))
#define	XFS_CFORK_FORMAT(dcp,w) \
	((w) == XFS_DATA_FORK ? (dcp)->di_format : (dcp)->di_aformat)
#define	XFS_CFORK_FMT_SET(dcp,w,n) \
	((w) == XFS_DATA_FORK ? \
		((dcp)->di_format = (n)) : \
		((dcp)->di_aformat = (n)))
#define	XFS_CFORK_NEXTENTS(dcp,w) \
	((w) == XFS_DATA_FORK ? (dcp)->di_nextents : (dcp)->di_anextents)
#define	XFS_CFORK_NEXT_SET(dcp,w,n) \
	((w) == XFS_DATA_FORK ? \
		((dcp)->di_nextents = (n)) : \
		((dcp)->di_anextents = (n)))
#define	XFS_DFORK_FORMAT(dip,w) 	XFS_CFORK_FORMAT(&dip->di_core, w)
#define	XFS_DFORK_FMT_SET(dip,w,n)	XFS_CFORK_FMT_SET(&dip->di_core, w, n)
#define	XFS_DFORK_NEXTENTS(dip,w)	XFS_CFORK_NEXTENTS(&dip->di_core, w)
#define	XFS_DFORK_NEXT_SET(dip,w,n)	XFS_CFORK_NEXT_SET(&dip->di_core, w, n)

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

#define	XFS_BUF_TO_DINODE(bp)	((xfs_dinode_t *)(bp->b_un.b_addr))

/*
 * Values for di_flags
 * There should be a one-to-one correspondence between these flags and the
 * XFS_XFLAG_s.
 */
#define XFS_DIFLAG_REALTIME_BIT	0	/* file's blocks come from rt area */
#define XFS_DIFLAG_PREALLOC_BIT	1	/* file space has been preallocated */
#define XFS_DIFLAG_REALTIME     (1 << XFS_DIFLAG_REALTIME_BIT)
#define XFS_DIFLAG_PREALLOC	(1 << XFS_DIFLAG_PREALLOC_BIT)
#define XFS_DIFLAG_ALL  (XFS_DIFLAG_REALTIME|XFS_DIFLAG_PREALLOC)

#endif	/* _FS_XFS_DINODE_H */
