#ifndef _FS_XFS_AG_H
#define	_FS_XFS_AG_H

#ident	"$Revision: 1.9 $"

/*
 * Allocation group header
 * This is divided into two structures, placed in sequential 512-byte 
 * buffers after a copy of the superblock (also in a 512-byte buffer).
 */

#define	XFS_AGF_MAGIC	0x58414746	/* 'XAGF' */
#define	XFS_AGI_MAGIC	0x58414749	/* 'XAGI' */
#define	XFS_AGF_VERSION	1
#define	XFS_AGI_VERSION	1

typedef struct xfs_agf
{
	/*
	 * Common allocation group header information
	 */
	__uint32_t	agf_magicnum;	/* magic number == XFS_AGF_MAGIC */
	__uint32_t	agf_versionnum;	/* header version == XFS_AGF_VERSION */
	xfs_agnumber_t	agf_seqno;	/* sequence # starting from 0 */
	xfs_agblock_t	agf_length;	/* size in blocks of a.g. */
	/*
	 * Freespace information
	 */
	xfs_agblock_t	agf_roots[XFS_BTNUM_MAX - 1];
	__uint32_t	agf_levels[XFS_BTNUM_MAX - 1];
	xfs_agblock_t	agf_freelist;	/* free blocks */
	xfs_extlen_t	agf_freecount;	/* #blocks on agf_freelist */
	xfs_extlen_t	agf_freeblks;	/* total free blocks */
	xfs_extlen_t	agf_longest;	/* longest free space */
} xfs_agf_t;

#define	XFS_AGF_MAGICNUM	0x00000001
#define	XFS_AGF_VERSIONNUM	0x00000002
#define	XFS_AGF_SEQNO		0x00000004
#define	XFS_AGF_LENGTH		0x00000008
#define	XFS_AGF_ROOTS		0x00000010
#define	XFS_AGF_LEVELS		0x00000020
#define	XFS_AGF_FREELIST	0x00000040
#define	XFS_AGF_FREECOUNT	0x00000080
#define	XFS_AGF_FREEBLKS	0x00000100
#define	XFS_AGF_LONGEST		0x00000200
#define	XFS_AGF_NUM_BITS	10
#define	XFS_AGF_ALL_BITS	((1 << XFS_AGF_NUM_BITS) - 1)

/* disk block (daddr_t) in the AG */
#define	XFS_AGF_DADDR		((daddr_t)1)
#define	XFS_AGF_BLOCK(mp)	XFS_HDR_BLOCK(mp, XFS_AGF_DADDR)

typedef struct xfs_agi
{
	/*
	 * Common allocation group header information
	 */
	__uint32_t	agi_magicnum;	/* magic number == XFS_AGI_MAGIC */
	__uint32_t	agi_versionnum;	/* header version == XFS_AGI_VERSION */
	xfs_agnumber_t	agi_seqno;	/* sequence # starting from 0 */
	xfs_agblock_t	agi_length;	/* size in blocks of a.g. */
	/*
	 * Inode information
	 * Inodes are mapped by interpreting the inode number, so no
	 * mapping data is needed here.
	 */
	xfs_agino_t	agi_count;	/* count of allocated inodes */
	xfs_agino_t	agi_first;	/* first allocated inode */
	xfs_agino_t	agi_last;	/* last allocated inode */
	xfs_agino_t	agi_freelist;	/* first free inode */
	xfs_agino_t	agi_freecount;	/* number of free inodes */
} xfs_agi_t;

#define	XFS_AGI_MAGICNUM	0x00000001
#define	XFS_AGI_VERSIONNUM	0x00000002
#define	XFS_AGI_SEQNO		0x00000004
#define	XFS_AGI_LENGTH		0x00000008
#define	XFS_AGI_COUNT		0x00000010
#define	XFS_AGI_FIRST		0x00000020
#define	XFS_AGI_LAST		0x00000040
#define	XFS_AGI_FREELIST	0x00000080
#define	XFS_AGI_FREECOUNT	0x00000100
#define	XFS_AGI_NUM_BITS	9
#define	XFS_AGI_ALL_BITS	((1 << XFS_AGI_NUM_BITS) - 1)

/* disk block (daddr_t) in the AG */
#define	XFS_AGI_DADDR		((daddr_t)2)
#define	XFS_AGI_BLOCK(mp)	XFS_HDR_BLOCK(mp, XFS_AGI_DADDR)

#define	XFS_AG_MIN_BYTES	(1LL << 24)	/* 16 MB */
#define	XFS_AG_MAX_BYTES	(1LL << 32)	/*  4 GB */

#define	XFS_AG_MIN_BLOCKS(bl)	((xfs_extlen_t)(XFS_AG_MIN_BYTES >> bl))
#define	XFS_AG_MAX_BLOCKS(bl)	((xfs_extlen_t)(XFS_AG_MAX_BYTES >> bl))

#define	XFS_AG_MIN(a,b)		((a) < (b) ? (a) : (b))
#define	XFS_AG_MAXLEVELS(mp)	((mp)->m_ag_maxlevels)
#define	XFS_MIN_FREELIST(a,mp)	\
	(XFS_AG_MIN((a)->agf_levels[XFS_BTNUM_BNOi] + 1, \
		    XFS_AG_MAXLEVELS(mp)) + \
	 XFS_AG_MIN((a)->agf_levels[XFS_BTNUM_CNTi] + 1, \
		    XFS_AG_MAXLEVELS(mp)))

#define	XFS_AGB_MASK(k)	((1 << (k)) - 1)
#define	XFS_AGB_TO_FSB(mp,agno,agbno) \
	(((xfs_fsblock_t)(agno) << (mp)->m_sb.sb_agblklog) | (agbno))
#define	XFS_FSB_TO_AGNO(mp,fsbno) \
	((xfs_agnumber_t)((fsbno) >> (mp)->m_sb.sb_agblklog))
#define	XFS_FSB_TO_AGBNO(mp,fsbno) \
	((xfs_agblock_t)((fsbno) & XFS_AGB_MASK((mp)->m_sb.sb_agblklog)))

#define	XFS_AGB_TO_DADDR(mp,agno,agbno) \
	(XFS_BTOD(mp, (xfs_fsblock_t)(agno) * (mp)->m_sb.sb_agblocks + (agbno)))
#define	XFS_DADDR_TO_AGNO(mp,d) \
	((xfs_agnumber_t)(XFS_DTOBT(mp, d) / (mp)->m_sb.sb_agblocks))
#define	XFS_DADDR_TO_AGBNO(mp,d) \
	((xfs_agblock_t)(XFS_DTOBT(mp, d) % (mp)->m_sb.sb_agblocks))

#define	XFS_AG_DADDR(mp,agno,d)	(XFS_AGB_TO_DADDR(mp, agno, 0) + (d))

#define	XFS_BUF_TO_AGF(buf)	((xfs_agf_t *)(buf)->b_un.b_addr)
#define	XFS_BUF_TO_AGI(buf)	((xfs_agi_t *)(buf)->b_un.b_addr)

#endif	/* !_FS_XFS_AG_H */
