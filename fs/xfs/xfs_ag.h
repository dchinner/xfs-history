#ifndef _FS_XFS_AG_H
#define	_FS_XFS_AG_H

#ident	"$Revision$"

/*
 * Allocation group header
 */

#define	XFS_AGH_MAGIC	0x58414748	/* 'XAGH' */
#define	XFS_AGH_VERSION	1

typedef enum
{
	XFS_BTNUM_BNO, XFS_BTNUM_CNT, XFS_BTNUM_IBT, XFS_BTNUM_BMAP,
	XFS_BTNUM_MAX
} xfs_btnum_t;

typedef struct xfs_aghdr
{
	__uint32_t	ag_magic;	/* magic number == XFS_AGH_MAGIC */
	__uint16_t	ag_version;	/* header version == XFS_AGH_VERSION */
	xfs_agnumber_t	ag_seqno;	/* sequence # starting from 0 */
	xfs_agblock_t	ag_length;	/* size in blocks of a.g. */
	/*
	 * Freespace information
	 */
	xfs_agblock_t	ag_roots[XFS_BTNUM_MAX - 2];
	xfs_agblock_t	ag_freelist;	/* free blocks */
	__uint16_t	ag_levels[XFS_BTNUM_MAX - 2];
	xfs_extlen_t	ag_flist_count;	/* #blocks */
	xfs_extlen_t	ag_freeblks;	/* total free blocks */
	xfs_extlen_t	ag_longest;	/* longest free space */
	/*
	 * We may really want to put the inode btree root here instead
	 * of pointing at it, but for now it simplifies things to not
	 * have to special case the root block of the Btree.
	 */
	xfs_agblock_t	ag_iroot;	/* root block of B-tree for inode map */
	xfs_agino_t	ag_icount;	/* count of allocated inodes */
	__uint16_t	ag_ilevels;	/* # levels in the B-tree */
	xfs_agino_t	ag_iflist;	/* first free inode */
	xfs_agino_t	ag_ifcount;	/* number of free inodes */
} xfs_aghdr_t;

#define	XFS_AG_MAGIC		0x0001
#define	XFS_AG_VERSION		0x0002
#define	XFS_AG_SEQNO		0x0004
#define	XFS_AG_LENGTH		0x0008
#define	XFS_AG_ROOTS		0x0010
#define	XFS_AG_FREELIST		0x0020
#define	XFS_AG_LEVELS		0x0040
#define	XFS_AG_FLIST_COUNT	0x0080
#define	XFS_AG_FREEBLKS		0x0100
#define	XFS_AG_LONGEST		0x0200
#define	XFS_AG_IROOT		0x0400
#define	XFS_AG_ICOUNT		0x0800
#define	XFS_AG_ILEVELS		0x1000
#define	XFS_AG_IFLIST		0x2000
#define	XFS_AG_IFCOUNT		0x4000
#define	XFS_AG_NUM_BITS		15
#define	XFS_AG_ALL_BITS		((1 << XFS_AG_NUM_BITS) - 1)

/* block number in the AG; SB is block 0 */
#define	XFS_AGH_BLOCK	((xfs_agblock_t)(XFS_SB_BLOCK + 1))

#define	XFS_AG_MIN_SIZE	(1<<24)	/* bytes */
#define	XFS_AG_MAX_SIZE	(1<<30)	/* bytes */

#define	XFS_MIN_FREELIST(a)	(2 * (((a)->ag_levels[XFS_BTNUM_BNO]) + ((a)->ag_levels[XFS_BTNUM_CNT]) + 1))

#define	xfs_agb_to_fsb(s,agno,agbno) \
	((xfs_fsblock_t)((agno) * (s)->sb_agblocks + (agbno)))
#define	xfs_fsb_to_agno(s,fsbno) \
	((xfs_agnumber_t)((fsbno) / (s)->sb_agblocks))
#define	xfs_fsb_to_agbno(s,fsbno) \
	((xfs_agblock_t)((fsbno) % (s)->sb_agblocks))

#define	xfs_daddr_to_agno(s,d) \
	xfs_fsb_to_agno(s,xfs_daddr_to_fsb(s,d))
#define	xfs_daddr_to_agbno(s,d) \
	xfs_fsb_to_agbno(s,xfs_daddr_to_fsb(s,d))
#define	xfs_agb_to_daddr(s,agno,agbno) \
	xfs_fsb_to_daddr(s,xfs_agb_to_fsb(s,agno,agbno))

#define	xfs_buf_to_agp(buf)	((xfs_aghdr_t *)(buf)->b_un.b_addr)

#endif	/* !_FS_XFS_AG_H */
