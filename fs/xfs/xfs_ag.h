#ifndef _FS_XFS_AG_H
#define	_FS_XFS_AG_H

#ident	"$Revision: 1.3 $"

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
	__uint32_t	xfsag_magic;	/* magic number == XFS_AGH_MAGIC */
	__uint16_t	xfsag_version;	/* header version == XFS_AGH_VERSION */
	__uint16_t	xfsag_seqno;	/* sequence # starting from 0 */
	xfs_agblock_t	xfsag_length;	/* size in blocks of a.g. */
	/*
	 * Freespace information
	 */
	xfs_agblock_t	xfsag_roots[XFS_BTNUM_MAX - 2];
	xfs_agblock_t	xfsag_freelist;	/* free blocks */
	__uint16_t	xfsag_levels[XFS_BTNUM_MAX - 2];
	xfs_extlen_t	xfsag_flist_count;	/* #blocks */
	xfs_extlen_t	xfsag_freeblks;	/* total free blocks */
	xfs_extlen_t	xfsag_longest;	/* longest free space */
	/*
	 * We may really want to put the inode btree root here instead
	 * of pointing at it, but for now it simplifies things to not
	 * have to special case the root block of the Btree.
	 */
	xfs_agblock_t	xfsag_iroot;	/* root block of B-tree for inode map */
	xfs_agino_t	xfsag_icount;	/* count of allocated inodes */
	__uint16_t	xfsag_ilevels;	/* # levels in the B-tree */
	xfs_agino_t	xfsag_iflist;	/* first free inode */
	xfs_agino_t	xfsag_ifcount;	/* number of free inodes */
} xfs_aghdr_t;

/* block number in the AG; SB is block 0 */
#define	XFS_AGH_BLOCK	((xfs_agblock_t)(XFS_SB_BLOCK + 1))

#define	XFS_AG_MIN_SIZE	(1<<24)	/* bytes */
#define	XFS_AG_MAX_SIZE	(1<<30)	/* bytes */

#define	XFS_MIN_FREELIST(a)	(2 * (((a)->xfsag_levels[XFS_BTNUM_BNO]) + ((a)->xfsag_levels[XFS_BTNUM_CNT]) + 1))

#define	xfs_agb_to_fsb(s,agno,agbno) \
	((xfs_fsblock_t)((agno) * (s)->xfsb_agblocks + (agbno)))
#define	xfs_fsb_to_agno(s,fsbno) \
	((xfs_agnumber_t)((fsbno) / (s)->xfsb_agblocks))
#define	xfs_fsb_to_agbno(s,fsbno) \
	((xfs_agblock_t)((fsbno) % (s)->xfsb_agblocks))

#define	xfs_daddr_to_agno(s,d) \
	xfs_fsb_to_agno(s,xfs_daddr_to_fsb(s,d))
#define	xfs_daddr_to_agbno(s,d) \
	xfs_fsb_to_agbno(s,xfs_daddr_to_fsb(s,d))
#define	xfs_agb_to_daddr(s,agno,agbno) \
	xfs_fsb_to_daddr(s,xfs_agb_to_fsb(s,agno,agbno))

#define	xfs_buf_to_agp(buf)	((xfs_aghdr_t *)(buf)->b_un.b_addr)

#endif	/* !_FS_XFS_AG_H */
