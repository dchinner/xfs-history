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
	xfs_uuid_t	xfsb_uuid;	/* file system unique id */
	xfs_fsblock_t	xfsb_dblocks;	/* number of data blocks */
	__uint32_t	xfsb_blocksize;	/* logical block size, bytes */
	/*
	 * xfsb_magic is at offset 28 to be at the same location as fs_magic
	 * in an EFS filesystem, thus ensuring there is no confusion.
	 */
	__uint32_t	xfsb_magic;	/* magic number == XFS_SB_MAGIC */
	xfs_fsblock_t	xfsb_rblocks;	/* number of realtime blocks */
	xfs_fsblock_t	xfsb_rbitmap;	/* bitmap for realtime blocks */
	xfs_fsblock_t	xfsb_rsummary;	/* summary for xfs_rbitmap */
	xfs_ino_t	xfsb_rootino;	/* root inode number */
	xfs_agblock_t	xfsb_rextsize;	/* realtime extent size, blocks */
	xfs_agblock_t	xfsb_agblocks;	/* size of an allocation group */
	xfs_agnumber_t	xfsb_agcount;	/* number of allocation groups */
	__uint16_t	xfsb_version;	/* header version == XFS_SB_VERSION */
	__uint16_t	xfsb_sectsize;	/* volume sector size, bytes */
	__uint16_t	xfsb_inodesize;	/* inode size, bytes */
	__uint16_t	xfsb_inopblock;	/* inodes per block */
	char		xfsb_fname[6];	/* file system name */
	char		xfsb_fpack[6];	/* file system pack name */
	__uint8_t	xfsb_blocklog;	/* log2 of xfs_blocksize */
	__uint8_t	xfsb_sectlog;	/* log2 of xfs_sectsize */
	__uint8_t	xfsb_smallfiles;/* set if small files in inodes */
					/* other inode config information? */
					/* statistics */
	xfs_ino_t	xfsb_icount;	/* allocated inodes */
	xfs_ino_t	xfsb_ifree;	/* free inodes */
	xfs_fsblock_t	xfsb_fdblocks;	/* free data blocks */
	__uint32_t	xfsb_frextents;	/* free realtime extents */
} xfs_sb_t;

#define	XFS_SB_DADDR	((daddr_t)0)	/* daddr in filesystem */
#define	XFS_SB_FSB	((xfs_fsblock_t)0)	/* fsblock in filesystem */
#define	XFS_SB_BLOCK	((xfs_agblock_t)0) /* block number in the AG */

#define	xfs_daddr_to_fsb(s,d) \
	((xfs_fsblock_t)((d) >> ((s)->xfsb_blocklog - BBSHIFT)))
#define	xfs_fsb_to_daddr(s,fsbno) \
	((daddr_t)((fsbno) << ((s)->xfsb_blocklog - BBSHIFT)))
#define	xfs_btod(s,l)	((l) << ((s)->xfsb_blocklog - BBSHIFT))

#define	xfs_buf_to_sbp(buf)	((xfs_sb_t *)(buf)->b_un.b_addr)

#endif	/* !_FS_XFS_SB_H */
