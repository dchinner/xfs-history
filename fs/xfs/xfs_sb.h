#ifndef _FS_XFS_SB_H
#define	_FS_XFS_SB_H

#ident	"$Revision: 1.21 $"

/*
 * Super block
 * Fits into a 512-byte buffer at daddr_t 0 of each allocation group.
 * Only the first of these is ever updated except during growfs.
 */

#define	XFS_SB_MAGIC	0x58465342	/* 'XFSB' */
#define	XFS_SB_VERSION	1

typedef struct xfs_sb
{
	__uint32_t	sb_magicnum;	/* magic number == XFS_SB_MAGIC */
	__uint32_t	sb_blocksize;	/* logical block size, bytes */
	xfs_drfsbno_t	sb_dblocks;	/* number of data blocks */
	xfs_drfsbno_t	sb_rblocks;	/* number of realtime blocks */
	xfs_drtbno_t	sb_rextents;	/* number of realtime extents */
	uuid_t		sb_uuid;	/* file system unique id */
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
	__uint8_t	sb_inprogress;	/* mkfs is in progress, don't mount */
					/* 1 byte of padding */
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


#define	XFS_SB_MAGICNUM		0x00000001
#define	XFS_SB_BLOCKSIZE	0x00000002
#define	XFS_SB_DBLOCKS		0x00000004
#define	XFS_SB_RBLOCKS		0x00000008
#define	XFS_SB_REXTENTS		0x00000010
#define	XFS_SB_UUID		0x00000020
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
#define	XFS_SB_INPROGRESS	0x08000000
#define	XFS_SB_ICOUNT		0x10000000
#define	XFS_SB_IFREE		0x20000000
#define	XFS_SB_FDBLOCKS		0x40000000
#define	XFS_SB_FREXTENTS	0x80000000
#define	XFS_SB_NUM_BITS		32
#define	XFS_SB_ALL_BITS		((1 << XFS_SB_NUM_BITS) - 1)

#define	XFS_SB_DADDR	((daddr_t)0)		/* daddr in filesystem/ag */
#define	XFS_SB_BLOCK(mp)	XFS_HDR_BLOCK(mp, XFS_SB_DADDR)

#define	XFS_HDR_BLOCK(mp,d)	((xfs_agblock_t)(XFS_BB_TO_FSBT(mp,d)))
#define	XFS_DADDR_TO_FSB(mp,d) \
	XFS_AGB_TO_FSB(mp, XFS_DADDR_TO_AGNO(mp,d), XFS_DADDR_TO_AGBNO(mp,d))
#define	XFS_FSB_TO_DADDR(mp,fsbno) \
	XFS_AGB_TO_DADDR(mp, XFS_FSB_TO_AGNO(mp,fsbno), \
			 XFS_FSB_TO_AGBNO(mp,fsbno))

/*
 * File system block to basic block conversions.
 */
#define	XFS_FSB_TO_BB(mp,fsbno)	((fsbno) << (mp)->m_blkbb_log)
#define	XFS_BB_TO_FSB(mp,bb)	\
	(((bb) + (XFS_FSB_TO_BB(mp,1) - 1)) >> (mp)->m_blkbb_log)
#define	XFS_BB_TO_FSBT(mp,bb)	((bb) >> (mp)->m_blkbb_log)
#define	XFS_BB_FSB_OFFSET(mp,bb) ((bb) & ((mp)->m_bsize - 1))

/*
 * File system block to byte conversions.
 */
#define	XFS_FSB_TO_B(mp,fsbno)	((xfs_fsize_t)(fsbno) << \
				 (mp)->m_sb.sb_blocklog)
#define	XFS_B_TO_FSB(mp,b)	\
	((((__uint64_t)(b)) + (mp)->m_blockmask) >> (mp)->m_sb.sb_blocklog)
#define	XFS_B_TO_FSBT(mp,b)	(((__uint64_t)(b)) >> (mp)->m_sb.sb_blocklog)
#define	XFS_B_FSB_OFFSET(mp,b)	((b) & (mp)->m_blockmask)     
     
#define	XFS_BUF_TO_SBP(bp)	((xfs_sb_t *)(bp)->b_un.b_addr)

#endif	/* !_FS_XFS_SB_H */
