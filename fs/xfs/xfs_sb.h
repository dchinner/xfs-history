#ifndef _FS_XFS_SB_H
#define	_FS_XFS_SB_H

#ident	"$Revision: 1.3 $"

/*
 * Super block
 */

#define	XFS_SB_MAGIC	0x58465342	/* 'XFSB' */
#define	XFS_SB_VERSION	1

typedef struct xfs_sb
{
	xfs_uuid_t	sb_uuid;	/* file system unique id */
	xfs_fsblock_t	sb_dblocks;	/* number of data blocks */
	__uint32_t	sb_blocksize;	/* logical block size, bytes */
	/*
	 * sb_magic is at offset 28 to be at the same location as fs_magic
	 * in an EFS filesystem, thus ensuring there is no confusion.
	 */
	__uint32_t	sb_magic;	/* magic number == XFS_SB_MAGIC */
	xfs_fsblock_t	sb_rblocks;	/* number of realtime blocks */
	xfs_fsblock_t	sb_rbitmap;	/* bitmap for realtime blocks */
	xfs_fsblock_t	sb_rsummary;	/* summary for xfs_rbitmap */
	xfs_ino_t	sb_rootino;	/* root inode number */
	xfs_agblock_t	sb_rextsize;	/* realtime extent size, blocks */
	xfs_agblock_t	sb_agblocks;	/* size of an allocation group */
	xfs_agnumber_t	sb_agcount;	/* number of allocation groups */
	__uint16_t	sb_version;	/* header version == XFS_SB_VERSION */
	__uint16_t	sb_sectsize;	/* volume sector size, bytes */
	__uint16_t	sb_inodesize;	/* inode size, bytes */
	__uint16_t	sb_inopblock;	/* inodes per block */
	char		sb_fname[6];	/* file system name */
	char		sb_fpack[6];	/* file system pack name */
	__uint8_t	sb_blocklog;	/* log2 of xfs_blocksize */
	__uint8_t	sb_sectlog;	/* log2 of xfs_sectsize */
	__uint8_t	sb_smallfiles;	/* set if small files in inodes */
					/* other inode config information? */
					/* statistics */
	xfs_ino_t	sb_icount;	/* allocated inodes */
	xfs_ino_t	sb_ifree;	/* free inodes */
	xfs_fsblock_t	sb_fdblocks;	/* free data blocks */
	__uint32_t	sb_frextents;	/* free realtime extents */
} xfs_sb_t;

#define	XFS_SB_DADDR	((daddr_t)0)	/* daddr in filesystem */
#define	XFS_SB_FSB	((xfs_fsblock_t)0)	/* fsblock in filesystem */
#define	XFS_SB_BLOCK	((xfs_agblock_t)0) /* block number in the AG */

#define	xfs_daddr_to_fsb(s,d) \
	((xfs_fsblock_t)((d) >> ((s)->sb_blocklog - BBSHIFT)))
#define	xfs_fsb_to_daddr(s,fsbno) \
	((daddr_t)((fsbno) << ((s)->sb_blocklog - BBSHIFT)))
#define	xfs_btod(s,l)	((l) << ((s)->sb_blocklog - BBSHIFT))

#define	xfs_buf_to_sbp(buf)	((xfs_sb_t *)(buf)->b_un.b_addr)

#endif	/* !_FS_XFS_SB_H */
