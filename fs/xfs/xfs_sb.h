#ifndef _FS_XFS_SB_H
#define	_FS_XFS_SB_H

#ident	"$Revision: 1.27 $"

/*
 * Super block
 * Fits into a 512-byte buffer at daddr_t 0 of each allocation group.
 * Only the first of these is ever updated except during growfs.
 */

struct buf;
struct xfs_mount;

#define	XFS_SB_MAGIC		0x58465342	/* 'XFSB' */
#define	XFS_SB_VERSION_1	1		/* 5.3, 6.0.1, 6.1 */
#define	XFS_SB_VERSION_2	2		/* 6.2 - attributes */
#define	XFS_SB_VERSION_3	3		/* 6.2 - new inode version */
#define	XFS_SB_VERSION_4	4		/* 6.2 - disk quotas version */
#define	XFS_SB_VERSION_LOW	XFS_SB_VERSION_1
#define	XFS_SB_VERSION_HIGH	XFS_SB_VERSION_4
#define	XFS_SB_VERSION_HASATTR	XFS_SB_VERSION_2
#define	XFS_SB_VERSION_HASNLINK	XFS_SB_VERSION_3
#define XFS_SB_VERSION_HASQUOTA XFS_SB_VERSION_4
#define	XFS_SB_VERSION		XFS_SB_VERSION_4

#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_SB_GOOD_VERSION)
int xfs_sb_good_version(unsigned v);
#define	XFS_SB_GOOD_VERSION(v)	xfs_sb_good_version(v)
#else
#define	XFS_SB_GOOD_VERSION(v)	\
	((v) >= XFS_SB_VERSION_LOW && (v) <= XFS_SB_VERSION_HIGH)
#endif

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
	__uint8_t	sb_imax_pct;	/* max % of fs for inode space */
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
	
	/*
	 * XFS_SB_VERSION_4 - quota support
	 */
	xfs_ino_t	sb_uquotino;	/* user quota inode */
	xfs_ino_t	sb_pquotino;	/* project quota inode */
	__uint16_t	sb_qflags;	/* quota flags */
} xfs_sb_t;

/*
 * Sequence number values for the fields.
 */
typedef enum {
	XFS_SBS_MAGICNUM, XFS_SBS_BLOCKSIZE, XFS_SBS_DBLOCKS, XFS_SBS_RBLOCKS,
	XFS_SBS_REXTENTS, XFS_SBS_UUID, XFS_SBS_LOGSTART, XFS_SBS_ROOTINO,
	XFS_SBS_RBMINO, XFS_SBS_RSUMINO, XFS_SBS_REXTSIZE, XFS_SBS_AGBLOCKS,
	XFS_SBS_AGCOUNT, XFS_SBS_RBMBLOCKS, XFS_SBS_LOGBLOCKS,
	XFS_SBS_VERSIONNUM, XFS_SBS_SECTSIZE, XFS_SBS_INODESIZE,
	XFS_SBS_INOPBLOCK, XFS_SBS_FNAME, XFS_SBS_FPACK, XFS_SBS_BLOCKLOG,
	XFS_SBS_SECTLOG, XFS_SBS_INODELOG, XFS_SBS_INOPBLOG, XFS_SBS_AGBLKLOG,
	XFS_SBS_REXTSLOG, XFS_SBS_INPROGRESS, XFS_SBS_IMAX_PCT, XFS_SBS_ICOUNT,
	XFS_SBS_IFREE, XFS_SBS_FDBLOCKS, XFS_SBS_FREXTENTS, 
	XFS_SBS_UQUOTINO, XFS_SBS_PQUOTINO, XFS_SBS_QFLAGS,
	XFS_SBS_FIELDCOUNT
} xfs_sb_field_t;

/*
 * Mask values, defined based on the xfs_sb_field_t values.
 * Only define the ones we're using.
 */
#define	XFS_SB_MVAL(x)		(1LL << XFS_SBS_ ## x)
#define	XFS_SB_ROOTINO		XFS_SB_MVAL(ROOTINO)
#define	XFS_SB_RBMINO		XFS_SB_MVAL(RBMINO)
#define	XFS_SB_RSUMINO		XFS_SB_MVAL(RSUMINO)
#define	XFS_SB_VERSIONNUM	XFS_SB_MVAL(VERSIONNUM)
#define XFS_SB_UQUOTINO		XFS_SB_MVAL(UQUOTINO)
#define XFS_SB_PQUOTINO		XFS_SB_MVAL(PQUOTINO)
#define XFS_SB_QFLAGS		XFS_SB_MVAL(QFLAGS)
#define	XFS_SB_NUM_BITS		((int)XFS_SBS_FIELDCOUNT)
#define	XFS_SB_ALL_BITS		((1LL << XFS_SB_NUM_BITS) - 1)

#define	XFS_SB_DADDR	((daddr_t)0)		/* daddr in filesystem/ag */
#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_SB_BLOCK)
xfs_agblock_t xfs_sb_block(struct xfs_mount *mp);
#define	XFS_SB_BLOCK(mp)	xfs_sb_block(mp)
#else
#define	XFS_SB_BLOCK(mp)	XFS_HDR_BLOCK(mp, XFS_SB_DADDR)
#endif

#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_HDR_BLOCK)
xfs_agblock_t xfs_hdr_block(struct xfs_mount *mp, daddr_t d);
#define	XFS_HDR_BLOCK(mp,d)	xfs_hdr_block(mp,d)
#else
#define	XFS_HDR_BLOCK(mp,d)	((xfs_agblock_t)(XFS_BB_TO_FSBT(mp,d)))
#endif
#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_DADDR_TO_FSB)
xfs_fsblock_t xfs_daddr_to_fsb(struct xfs_mount *mp, daddr_t d);
#define	XFS_DADDR_TO_FSB(mp,d)		xfs_daddr_to_fsb(mp,d)
#else
#define	XFS_DADDR_TO_FSB(mp,d) \
	XFS_AGB_TO_FSB(mp, XFS_DADDR_TO_AGNO(mp,d), XFS_DADDR_TO_AGBNO(mp,d))
#endif
#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_FSB_TO_DADDR)
daddr_t xfs_fsb_to_daddr(struct xfs_mount *mp, xfs_fsblock_t fsbno);
#define	XFS_FSB_TO_DADDR(mp,fsbno)	xfs_fsb_to_daddr(mp,fsbno)
#else
#define	XFS_FSB_TO_DADDR(mp,fsbno) \
	XFS_AGB_TO_DADDR(mp, XFS_FSB_TO_AGNO(mp,fsbno), \
			 XFS_FSB_TO_AGBNO(mp,fsbno))
#endif

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

#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_BUF_TO_SBP)
xfs_sb_t *xfs_buf_to_sbp(struct buf *bp);
#define	XFS_BUF_TO_SBP(bp)	xfs_buf_to_sbp(bp)
#else
#define	XFS_BUF_TO_SBP(bp)	((xfs_sb_t *)(bp)->b_un.b_addr)
#endif

#endif	/* !_FS_XFS_SB_H */
