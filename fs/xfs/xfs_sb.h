#ifndef _FS_XFS_SB_H
#define	_FS_XFS_SB_H

#ident	"$Revision$"

/*
 * Super block
 * Fits into a 512-byte buffer at daddr_t 0 of each allocation group.
 * Only the first of these is ever updated except during growfs.
 */

#define	XFS_SB_MAGIC	0x58465342	/* 'XFSB' */
#define	XFS_SB_VERSION	1

typedef struct xfs_sb
{
	uuid_t		sb_uuid;	/* file system unique id */
	xfs_fsblock_t	sb_dblocks;	/* number of data blocks */
	__uint32_t	sb_blocksize;	/* logical block size, bytes */
	/*
	 * sb_magic is at offset 28 to be at the same location as fs_magic
	 * in an EFS filesystem, thus ensuring there is no confusion.
	 */
	__uint32_t	sb_magicnum;	/* magic number == XFS_SB_MAGIC */
	xfs_fsblock_t	sb_rblocks;	/* number of realtime blocks */
	xfs_fsblock_t	sb_rbitmap;	/* bitmap for realtime blocks */
	xfs_fsblock_t	sb_rsummary;	/* summary for xfs_rbitmap */
	xfs_ino_t	sb_rootino;	/* root inode number */
	xfs_agblock_t	sb_rextsize;	/* realtime extent size, blocks */
	xfs_agblock_t	sb_agblocks;	/* size of an allocation group */
	xfs_agnumber_t	sb_agcount;	/* number of allocation groups */
	__uint16_t	sb_versionnum;	/* header version == XFS_SB_VERSION */
	__uint16_t	sb_sectsize;	/* volume sector size, bytes */
	__uint16_t	sb_inodesize;	/* inode size, bytes */
	__uint16_t	sb_inopblock;	/* inodes per block */
	char		sb_fname[6];	/* file system name */
	char		sb_fpack[6];	/* file system pack name */
	__uint8_t	sb_blocklog;	/* log2 of sb_blocksize */
	__uint8_t	sb_sectlog;	/* log2 of sb_sectsize */
	__uint8_t	sb_inodelog;	/* log2 of sb_inodesize */
	__uint8_t	sb_inopblog;	/* log2 of sb_inopblock */
	__uint8_t	sb_agblklog;	/* log2 of sb_agblocks (rounded up) */
					/* other inode config information? */
					/* statistics */
	/*
	 * These fields must remain contiguous.  If you really
	 * want to change their layout, make sure you fix the
	 * code in xfs_trans_apply_sb_deltas().
	 */
	__uint64_t	sb_icount;	/* allocated inodes */
	__uint64_t	sb_ifree;	/* free inodes */
	__uint64_t	sb_fdblocks;	/* free data blocks */
	__uint32_t	sb_frextents;	/* free realtime extents */
} xfs_sb_t;


#define	XFS_SB_UUID		0x0000001
#define	XFS_SB_DBLOCKS		0x0000002
#define	XFS_SB_BLOCKSIZE	0x0000004
#define	XFS_SB_MAGICNUM		0x0000008
#define	XFS_SB_RBLOCKS		0x0000010
#define	XFS_SB_RBITMAP		0x0000020
#define	XFS_SB_RSUMMARY		0x0000040
#define	XFS_SB_ROOTINO		0x0000080
#define	XFS_SB_REXTSIZE		0x0000100
#define	XFS_SB_AGBLOCKS		0x0000200
#define	XFS_SB_AGCOUNT		0x0000400
#define	XFS_SB_VERSIONNUM	0x0000800
#define	XFS_SB_SECTSIZE		0x0001000
#define	XFS_SB_INODESIZE	0x0002000
#define	XFS_SB_INOPBLOCK	0x0004000
#define	XFS_SB_FNAME		0x0008000
#define	XFS_SB_FPACK		0x0010000
#define	XFS_SB_BLOCKLOG		0x0020000
#define	XFS_SB_SECTLOG		0x0040000
#define	XFS_SB_INODELOG		0x0080000
#define	XFS_SB_INOPBLOG		0x0100000
#define	XFS_SB_AGBLKLOG		0x0200000
#define	XFS_SB_ICOUNT		0x0400000
#define	XFS_SB_IFREE		0x0800000
#define	XFS_SB_FDBLOCKS		0x1000000
#define	XFS_SB_FREXTENTS	0x2000000
#define	XFS_SB_NUM_BITS		26
#define	XFS_SB_ALL_BITS		((1 << XFS_SB_NUM_BITS) - 1)

#define	XFS_SB_DADDR	((daddr_t)0)		/* daddr in filesystem/ag */
#define	XFS_SB_BLOCK(s)	xfs_hdr_block(s, XFS_SB_DADDR)

#define	xfs_hdr_block(s,d)	((xfs_agblock_t)(xfs_dtobt(s,d)))
#define	xfs_daddr_to_fsb(s,d) \
	xfs_agb_to_fsb(s, xfs_daddr_to_agno(s,d), xfs_daddr_to_agbno(s,d))
#define	xfs_fsb_to_daddr(s,fsbno) \
	xfs_agb_to_daddr(s, xfs_fsb_to_agno(s,fsbno), xfs_fsb_to_agbno(s,fsbno))
#define	xfs_btod(s,l)	((daddr_t)((l) << ((s)->sb_blocklog - BBSHIFT)))
#define	xfs_dtobt(s,l)	((l) >> ((s)->sb_blocklog - BBSHIFT))

#define	xfs_buf_to_sbp(buf)	((xfs_sb_t *)(buf)->b_un.b_addr)

#endif	/* !_FS_XFS_SB_H */
