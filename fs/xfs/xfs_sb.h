#ifndef _FS_XFS_SB_H
#define	_FS_XFS_SB_H

#ident	"$Revision: 1.13 $"

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
	xfs_drfsbno_t	sb_dblocks;	/* number of data blocks */
	__uint32_t	sb_blocksize;	/* logical block size, bytes */
	/*
	 * sb_magicnum is at offset 28 to be at the same location as fs_magic
	 * in an EFS filesystem, thus ensuring there is no confusion.
	 */
	__uint32_t	sb_magicnum;	/* magic number == XFS_SB_MAGIC */
	xfs_drfsbno_t	sb_rblocks;	/* number of realtime blocks */
	xfs_drtbno_t	sb_rextents;	/* number of realtime extents */
	xfs_dfsbno_t	sb_logstart;	/* starting block of log if internal */
	xfs_ino_t	sb_rootino;	/* root inode number */
	xfs_ino_t	sb_rbmino;	/* bitmap inode for realtime extents */
	xfs_ino_t	sb_rsumino;	/* summary inode for rt bitmap */
	xfs_agblock_t	sb_rextsize;	/* realtime extent size, blocks */
	xfs_agblock_t	sb_agblocks;	/* size of an allocation group */
	xfs_agnumber_t	sb_agcount;	/* number of allocation groups */
	xfs_extlen_t	sb_rbmblocks;	/* number of rt bitmap blocks */
	xfs_extlen_t	sb_logblocks;	/* number of log blocks */
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
	__uint8_t	sb_rextslog;	/* log2 of sb_rextents */
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
	__uint64_t	sb_frextents;	/* free realtime extents */
} xfs_sb_t;


#define	XFS_SB_UUID		0x00000001
#define	XFS_SB_DBLOCKS		0x00000002
#define	XFS_SB_BLOCKSIZE	0x00000004
#define	XFS_SB_MAGICNUM		0x00000008
#define	XFS_SB_RBLOCKS		0x00000010
#define	XFS_SB_REXTENTS		0x00000020
#define	XFS_SB_LOGSTART		0x00000040
#define	XFS_SB_ROOTINO		0x00000080
#define	XFS_SB_RBMINO		0x00000100
#define	XFS_SB_RSUMINO		0x00000200
#define	XFS_SB_REXTSIZE		0x00000400
#define	XFS_SB_AGBLOCKS		0x00000800
#define	XFS_SB_AGCOUNT		0x00001000
#define	XFS_SB_RBMBLOCKS	0x00002000
#define	XFS_SB_LOGBLOCKS	0x00004000
#define	XFS_SB_VERSIONNUM	0x00008000
#define	XFS_SB_SECTSIZE		0x00010000
#define	XFS_SB_INODESIZE	0x00020000
#define	XFS_SB_INOPBLOCK	0x00040000
#define	XFS_SB_FNAME		0x00080000
#define	XFS_SB_FPACK		0x00100000
#define	XFS_SB_BLOCKLOG		0x00200000
#define	XFS_SB_SECTLOG		0x00400000
#define	XFS_SB_INODELOG		0x00800000
#define	XFS_SB_INOPBLOG		0x01000000
#define	XFS_SB_AGBLKLOG		0x02000000
#define	XFS_SB_REXTSLOG		0x04000000
#define	XFS_SB_ICOUNT		0x08000000
#define	XFS_SB_IFREE		0x10000000
#define	XFS_SB_FDBLOCKS		0x20000000
#define	XFS_SB_FREXTENTS	0x40000000
#define	XFS_SB_NUM_BITS		31
#define	XFS_SB_ALL_BITS		((1 << XFS_SB_NUM_BITS) - 1)

#define	XFS_SB_DADDR	((daddr_t)0)		/* daddr in filesystem/ag */
#define	XFS_SB_BLOCK(mp)	xfs_hdr_block(mp, XFS_SB_DADDR)

#define	xfs_hdr_block(mp,d)	((xfs_agblock_t)(xfs_dtobt(mp,d)))
#define	xfs_daddr_to_fsb(mp,d) \
	xfs_agb_to_fsb(mp, xfs_daddr_to_agno(mp,d), xfs_daddr_to_agbno(mp,d))
#define	xfs_fsb_to_daddr(mp,fsbno) \
	xfs_agb_to_daddr(mp, xfs_fsb_to_agno(mp,fsbno), \
			 xfs_fsb_to_agbno(mp,fsbno))

/*
 * File system block to basic block conversions.
 */
#define	xfs_fsb_to_bb(mp,fsbno)	((fsbno) << (mp)->m_blkbb_log)
#define	xfs_bb_to_fsb(mp,bb)	\
	(((bb) + (xfs_fsb_to_bb(mp,1) - 1)) >> (mp)->m_blkbb_log)
#define	xfs_bb_to_fsbt(mp,bb)	((bb) >> (mp)->m_blkbb_log)

/*
 * File system block to byte conversions.
 */
#define	xfs_fsb_to_b(mp,fsbno)	((fsbno) << (mp)->m_sb.sb_blocklog)
#define	xfs_b_to_fsb(mp,b)	\
	(((b) + (mp)->m_blockmask) >> (mp)->m_sb.sb_blocklog)
#define	xfs_b_to_fsbt(s,b)	((b) >> (mp)->m_sb.sb_blocklog)
     
#define	xfs_btod(mp,l)	((daddr_t)((l) << (mp)->m_blkbb_log))
#define	xfs_dtobt(mp,l)	((l) >> (mp)->m_blkbb_log)

#define	xfs_buf_to_sbp(buf)	((xfs_sb_t *)(buf)->b_un.b_addr)

#endif	/* !_FS_XFS_SB_H */
