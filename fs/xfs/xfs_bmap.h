#ifndef _FS_XFS_BMAP_H
#define	_FS_XFS_BMAP_H

#ident "$Revision$"

/*
 * List of extents to be free "later".
 * The list is kept sorted on xbf_startblock.
 */
typedef	struct xfs_bmap_free
{
	xfs_fsblock_t		xbf_startblock;	/* starting fs block number */
	xfs_extlen_t		xbf_blockcount;	/* number of blocks in extent */
	struct xfs_bmap_free	*xbf_next;	/* link to next entry */
	struct xfs_efi_log_item	*xbf_efip;	/* pointer to efi entry */
} xfs_bmap_free_t;

#define	XFS_BMAP_MAX_NMAP	4

/*
 * Flags for xfs_bmapi
 */
#define	XFS_BMAPI_WRITE		0x1	/* write operation: allocate space */
#define	XFS_BMAPI_DELAY		0x2	/* delayed write operation */
#define	XFS_BMAPI_ENTIRE	0x4	/* return entire extent, not trimmed */

/*
 * Add the extent to the list of extents to be free at transaction end.
 */
void
xfs_bmap_add_free(
	xfs_fsblock_t		bno,		/* fs block number of extent */
	xfs_extlen_t		len,		/* length of extent */
	xfs_bmap_free_t		**flist);	/* list of extents */

void
xfs_bmap_finish(
	xfs_trans_t		**tp,		/* transaction pointer addr */
	xfs_bmap_free_t		**flist,	/* i/o: list extents to free */
	xfs_fsblock_t		firstblock);	/* controlled a.g. for allocs */

void
xfs_bmap_read_extents(
	xfs_trans_t		*tp,		/* transaction pointer */
	struct xfs_inode	*ip);		/* incore inode */

xfs_fsblock_t					/* first allocated block */
xfs_bmapi(
	xfs_trans_t		*tp,		/* transaction pointer */
	struct xfs_inode	*ip,		/* incore inode */
	xfs_fsblock_t		bno,		/* starting file offs. mapped */
	xfs_extlen_t		len,		/* length to map in file */
	int			flags,		/* XFS_BMAPI_... */
	xfs_fsblock_t		firstblock,	/* controls a.g. for allocs */
	xfs_extlen_t		total,		/* total blocks needed */
	xfs_bmbt_irec_t		*mval,		/* output: map values */
	int			*nmap,		/* i/o: mval size/count */
	xfs_bmap_free_t		**flist);	/* i/o: list extents to free */

xfs_fsblock_t					/* first allocated block */
xfs_bunmapi(
	xfs_trans_t		*tp,		/* transaction pointer */
	struct xfs_inode	*ip,		/* incore inode */
	xfs_fsblock_t		bno,		/* starting offset to unmap */
	xfs_extlen_t		len,		/* length to unmap in file */
	xfs_fsblock_t		firstblock,	/* controls a.g. for allocs */
	xfs_bmap_free_t		**flist);	/* i/o: list extents to free */

#endif	/* _FS_XFS_BMAP_H */
