#ifndef _FS_XFS_BMAP_H
#define	_FS_XFS_BMAP_H

#ident "$Revision: 1.38 $"

struct getbmap;
struct xfs_bmbt_rec;
struct xfs_btcur;
struct xfs_inode;
struct xfs_trans;

/*
 * List of extents to be free "later".
 * The list is kept sorted on xbf_startblock.
 */
typedef struct xfs_bmap_free_item
{
	xfs_fsblock_t		xbfi_startblock;/* starting fs block number */
	xfs_extlen_t		xbfi_blockcount;/* number of blocks in extent */
	struct xfs_bmap_free_item *xbfi_next;	/* link to next entry */
} xfs_bmap_free_item_t;

/*
 * Header for free extent list.
 */
typedef	struct xfs_bmap_free
{
	xfs_bmap_free_item_t	*xbf_first;
	int			xbf_count;
} xfs_bmap_free_t;

#define	XFS_BMAP_MAX_NMAP	4

/*
 * Flags for xfs_bmapi
 */
#define	XFS_BMAPI_WRITE		0x01	/* write operation: allocate space */
#define	XFS_BMAPI_DELAY		0x02	/* delayed write operation */
#define	XFS_BMAPI_ENTIRE	0x04	/* return entire extent, not trimmed */
#define	XFS_BMAPI_METADATA	0x08	/* mapping metadata not user data */
#define	XFS_BMAPI_EXACT		0x10	/* allocate only to spec'd bounds */

/*
 * Special values for xfs_bmbt_irec_t br_startblock field.
 */
#define	DELAYSTARTBLOCK		((xfs_fsblock_t)-1LL)
#define	HOLESTARTBLOCK		((xfs_fsblock_t)-2LL)

/*
 * Trace operations for bmap extent tracing
 */
#define	XFS_BMAP_KTRACE_DELETE	1
#define	XFS_BMAP_KTRACE_INSERT	2
#define	XFS_BMAP_KTRACE_PRE_UP	3
#define	XFS_BMAP_KTRACE_POST_UP	4

#define	XFS_BMAP_TRACE_SIZE	4096	/* size of global trace buffer */
#define	XFS_BMAP_KTRACE_SIZE	32	/* size of per-inode trace buffer */

#define	XFS_BMAP_INIT(flp, fbp)	\
	((flp)->xbf_first = NULL, (flp)->xbf_count = 0, *(fbp) = NULLFSBLOCK)

/*
 * Argument structure for xfs_bmap_alloc.
 * This would be hidden, but we want idbg to be able to see it.
 */
typedef struct xfs_bmalloca {
	struct xfs_trans	*tp;	/* transaction pointer */
	struct xfs_inode	*ip;	/* incore inode pointer */
	int			eof;	/* set if allocating past last extent */
	struct xfs_bmbt_irec	*prevp;	/* extent before the new one */
	struct xfs_bmbt_irec	*gotp;	/* extent after, or delayed */
	xfs_fsblock_t		firstblock; /* i/o first block allocated */
	xfs_extlen_t		alen;	/* i/o length asked/allocated */
	xfs_extlen_t		total;	/* total blocks needed for xaction */
	xfs_fsblock_t		off;	/* offset in file filling in */
	int			wasdel;	/* replacing a delayed allocation */
	int			userdata;/* set if is user data */
	xfs_extlen_t		minlen;	/* mininum allocation size (blocks) */
	xfs_extlen_t		minleft; /* amount must be left after alloc */
	int			low;	/* low on space, using seq'l ags */
	xfs_fsblock_t		rval;	/* starting block of new extent */
} xfs_bmalloca_t;

/*
 * Locals structure for xfs_bmapi.
 * This would be hidden, but we want idbg to be able to see it.
 */
typedef struct xfs_bmapi_locals {
	int			orig_nmap; /* original value of *nmap */
	char			delay;	/* this request is for delayed alloc */
	char			inhole;	/* current location is hole in file */
	char			lowspace; /* using the low-space algorithm */
	char			trim;	/* output trimmed to match range */
	char			userdata; /* allocating non-metadata */
	char			wasdelay; /* old extent was delayed */
	char			wr;	/* this is a write request */
	int			eof;	/* we've hit the end of extent list */
	int			logflags; /* flags for transaction logging */
	int			n;	/* current extent index */
	int			orig_flags; /* original flags arg value */
	struct xfs_bmbt_irec	*orig_mval; /* original value of mval */
	struct xfs_bmbt_rec	*ep;	/* extent list entry pointer */
	struct xfs_btree_cur	*cur;	/* bmap btree cursor */
	xfs_alloctype_t		type;	/* allocation type chosen */
	xfs_bmbt_irec_t		got;	/* current extent list record */
	xfs_bmbt_irec_t		prev;	/* previous extent list record */
	xfs_extlen_t		alen;	/* allocated extent length */
	xfs_extlen_t		indlen;	/* indirect blocks length */
	xfs_extlen_t		minleft; /* min blocks left after allocation */
	xfs_extlen_t		minlen;	/* min allocation size */
	xfs_extlen_t		orig_len; /* original value of len arg */
	xfs_extnum_t		lastx;	/* last useful extent number */
	xfs_extnum_t		nextents; /* number of extents in file */
	xfs_fileoff_t		aoff;	/* allocated file offset */
	xfs_fileoff_t		end;	/* end of mapped file region */
	xfs_fileoff_t		obno;	/* old block number (offset) */
	xfs_fileoff_t		orig_bno; /* original block number value */
	xfs_fsblock_t		abno;	/* allocated block number */
} xfs_bmapi_locals_t;

/*
 * Add the extent to the list of extents to be free at transaction end.
 * The list is maintained sorted (by block number).
 */
void
xfs_bmap_add_free(
	xfs_fsblock_t		bno,		/* fs block number of extent */
	xfs_extlen_t		len,		/* length of extent */
	xfs_bmap_free_t		*flist,		/* list of extents */
	xfs_mount_t		*mp);		/* mount point structure */

/* 
 * Compute and fill in the value of the maximum depth of a bmap btree
 * in this filesystem.  Done once, during mount.
 */
void
xfs_bmap_compute_maxlevels(
	xfs_mount_t	*mp);		/* file system mount structure */

/*
 * Routine to be called at transaction's end by xfs_bmapi, xfs_bunmapi 
 * caller.  Frees all the extents that need freeing, which must be done
 * last due to locking considerations.
 *
 * Return 1 if the given transaction was committed and a new one allocated,
 * and 0 otherwise.
 */
int
xfs_bmap_finish(
	xfs_trans_t		**tp,		/* transaction pointer addr */
	xfs_bmap_free_t		*flist,		/* i/o: list extents to free */
	xfs_fsblock_t		firstblock);	/* controlled a.g. for allocs */

/*
 * Returns the file-relative block number of the first unused block in the file.
 * This is the lowest-address hole if the file has holes, else the first block
 * past the end of file.
 */
xfs_fileoff_t
xfs_bmap_first_unused(
	xfs_trans_t		*tp,		/* transaction pointer */
	struct xfs_inode	*ip);		/* incore inode */

/*
 * Returns the file-relative block number of the first block past eof in
 * the file.  This is not based on i_size, it is based on the extent list.
 * Returns 0 for local files, as they do not have an extent list.
 */
xfs_fileoff_t
xfs_bmap_last_offset(
	xfs_trans_t		*tp,		/* transaction pointer */
	struct xfs_inode	*ip);		/* incore inode */

/*
 * Read in the extents to iu_extents.
 * All inode fields are set up by caller, we just traverse the btree
 * and copy the records in.
 */
void
xfs_bmap_read_extents(
	xfs_trans_t		*tp,		/* transaction pointer */
	struct xfs_inode	*ip);		/* incore inode */

#if defined(DEBUG) && !defined(SIM)
/*
 * Add bmap trace insert entries for all the contents of the extent list.
 */
void
xfs_bmap_trace_exlist(
	char			*fname,		/* function name */
	struct xfs_inode	*ip,		/* incore inode pointer */
	xfs_extnum_t		cnt);		/* count of entries in list */
#else
#define	xfs_bmap_trace_exlist(f,ip,c)
#endif

/*
 * Map file blocks to filesystem blocks.
 * File range is given by the bno/len pair.
 * Adds blocks to file if a write ("flags & XFS_BMAPI_WRITE" set)
 * into a hole or past eof.
 * Only allocates blocks from a single allocation group,
 * to avoid locking problems.
 * The return value from the first call in a transaction must be remembered
 * and presented to subsequent calls in "firstblock".  An upper bound for
 * the number of blocks to be allocated is supplied to the first call 
 * in "total"; if no allocation group has that many free blocks then 
 * the call will fail (return NULLFSBLOCK).
 */
xfs_fsblock_t					/* first allocated block */
xfs_bmapi(
	xfs_trans_t		*tp,		/* transaction pointer */
	struct xfs_inode	*ip,		/* incore inode */
	xfs_fileoff_t		bno,		/* starting file offs. mapped */
	xfs_extlen_t		len,		/* length to map in file */
	int			flags,		/* XFS_BMAPI_... */
	xfs_fsblock_t		firstblock,	/* controls a.g. for allocs */
	xfs_extlen_t		total,		/* total blocks needed */
	xfs_bmbt_irec_t		*mval,		/* output: map values */
	int			*nmap,		/* i/o: mval size/count */
	xfs_bmap_free_t		*flist);	/* i/o: list extents to free */

/*
 * Unmap (remove) blocks from a file.
 * If nexts is nonzero then the number of extents to remove is limited to
 * that value.  If not all extents in the block range can be removed then
 * *done is set.
 */
xfs_fsblock_t					/* first allocated block */
xfs_bunmapi(
	xfs_trans_t		*tp,		/* transaction pointer */
	struct xfs_inode	*ip,		/* incore inode */
	xfs_fileoff_t		bno,		/* starting offset to unmap */
	xfs_extlen_t		len,		/* length to unmap in file */
	xfs_extnum_t		nexts,		/* number of extents max */
	xfs_fsblock_t		firstblock,	/* controls a.g. for allocs */
	xfs_bmap_free_t		*flist,		/* i/o: list extents to free */
	int			*done);		/* set if not done yet */

/*
 * Fcntl interface to xfs_bmapi.
 */
int						/* error code */
xfs_getbmap(
	vnode_t			*vp,		/* vnode pointer */
	struct getbmap		*bmv,		/* user bmap structure */
	void			*ap);		/* pointer to user's array */

#endif	/* _FS_XFS_BMAP_H */
