#ifndef _FS_XFS_ALLOC_H
#define	_FS_XFS_ALLOC_H

#ident	"$Revision: 1.27 $"

/*
 * Freespace allocation types.  Argument to xfs_alloc_[v]extent.
 * Nonsense with double declaration is for -wlint.
 */
typedef enum xfs_alloctype
{
	XFS_ALLOCTYPE_ANY_AGi,		/* allocate anywhere, use rotor */
	XFS_ALLOCTYPE_FIRST_AGi,	/* ... start at ag 0 */
	XFS_ALLOCTYPE_START_AGi,	/* anywhere, start in this a.g. */
	XFS_ALLOCTYPE_THIS_AGi,		/* anywhere in this a.g. */
	XFS_ALLOCTYPE_START_BNOi,	/* near this block else anywhere */
	XFS_ALLOCTYPE_NEAR_BNOi,	/* in this a.g. and near this block */
	XFS_ALLOCTYPE_THIS_BNOi		/* at exactly this block */
} xfs_alloctype_t;
#define	XFS_ALLOCTYPE_ANY_AG	((xfs_alloctype_t)XFS_ALLOCTYPE_ANY_AGi)
#define	XFS_ALLOCTYPE_FIRST_AG	((xfs_alloctype_t)XFS_ALLOCTYPE_FIRST_AGi)
#define	XFS_ALLOCTYPE_START_AG	((xfs_alloctype_t)XFS_ALLOCTYPE_START_AGi)
#define	XFS_ALLOCTYPE_THIS_AG	((xfs_alloctype_t)XFS_ALLOCTYPE_THIS_AGi)
#define	XFS_ALLOCTYPE_START_BNO	((xfs_alloctype_t)XFS_ALLOCTYPE_START_BNOi)
#define	XFS_ALLOCTYPE_NEAR_BNO	((xfs_alloctype_t)XFS_ALLOCTYPE_NEAR_BNOi)
#define	XFS_ALLOCTYPE_THIS_BNO	((xfs_alloctype_t)XFS_ALLOCTYPE_THIS_BNOi)

/*
 * Flags for xfs_alloc_ag_freeblks, xfs_alloc_fix_freelist
 */
#define	XFS_ALLOC_FLAG_TRYLOCK	0x00000001  /* use trylock for buffer locking */

/*
 * Argument structure for xfs_alloc routines.
 * This is turned into a structure to avoid having 12 arguments passed
 * down several levels of the stack.
 */
typedef struct xfs_alloc_arg {
	xfs_trans_t	*tp;		/* transaction pointer */
	xfs_mount_t	*mp;		/* file system mount point */
	xfs_fsblock_t	fsbno;		/* file system block number */
	buf_t		*agbp;		/* buffer for a.g. freelist header */
	xfs_agnumber_t	agno;		/* allocation group number */
	xfs_agblock_t	agbno;		/* allocation group-relative block # */
	xfs_extlen_t	minlen;		/* minimum size of extent */
	xfs_extlen_t	maxlen;		/* maximum size of extent */
	xfs_extlen_t	mod;		/* mod value for extent size */
	xfs_extlen_t	prod;		/* prod value for extent size */
	xfs_extlen_t	minleft;	/* min blocks must be left after us */
	xfs_extlen_t	total;		/* total blocks needed in xaction */
	xfs_extlen_t	len;		/* output: actual size of extent */
	xfs_alloctype_t	type;		/* allocation type XFS_ALLOCTYPE_... */
	short		wasdel;		/* set if allocation was prev delayed */
	char		isfl;		/* set if is freelist blocks - !actg */
	char		userdata;	/* set if this is user data */
} xfs_alloc_arg_t;

extern struct zone	*xfs_alloc_arg_zone;	/* zone for alloc args */

/*
 * Types for alloc tracing.
 */
#define	XFS_ALLOC_KTRACE_ALLOC	1
#define	XFS_ALLOC_KTRACE_FREE	2
#define	XFS_ALLOC_KTRACE_MODAGF	3

/*
 * Allocation tracing buffer size.
 */
#define	XFS_ALLOC_TRACE_SIZE	10240

/*
 * Prototypes for visible xfs_alloc.c routines
 */

/*
 * Return the number of free blocks left in the allocation group.
 */
xfs_extlen_t				/* number of remaining free blocks */
xfs_alloc_ag_freeblks(
	xfs_mount_t	*mp,		/* file system mount structure */
	xfs_trans_t	*tp,		/* transaction pointer */
	xfs_agnumber_t	agno,		/* allocation group number */
	int		flags);		/* XFS_ALLOC_FLAG_... */

/*
 * Allocate an alloc_arg structure.
 */
xfs_alloc_arg_t *
xfs_alloc_arg_alloc(void);

/*
 * Free an alloc_arg structure.
 */
void
xfs_alloc_arg_free(
	xfs_alloc_arg_t	*args);		/* alloc argument structure */

/*
 * Compute and fill in value of m_ag_maxlevels.
 */
void
xfs_alloc_compute_maxlevels(
	xfs_mount_t	*mp);		/* file system mount structure */

/*
 * Decide whether to use this allocation group for this allocation.
 * If so, fix up the btree freelist's size.
 * This is external so mkfs can call it, too.
 */
buf_t *				/* buffer for the a.g. freelist header */
xfs_alloc_fix_freelist(
	xfs_trans_t	*tp,	/* transaction pointer */
	xfs_agnumber_t	agno,	/* allocation group number */
	xfs_extlen_t	minlen,	/* minimum extent length, else reject */
	xfs_extlen_t	total,	/* total free blocks, else reject */
	xfs_extlen_t	minleft,/* min blocks must be left afterwards */
	int		flags);	/* XFS_ALLOC_FLAG_... */

/*
 * Get a block from the freelist.
 * Returns with the buffer for the block gotten.
 */
xfs_agblock_t			/* block address retrieved from freelist */
xfs_alloc_get_freelist(
	xfs_trans_t	*tp,	/* transaction pointer */
	buf_t		*agbp);	/* buffer containing the agf structure */

/*
 * Log the given fields from the agf structure.
 */
void
xfs_alloc_log_agf(
	xfs_trans_t	*tp,	/* transaction pointer */
	buf_t		*bp,	/* buffer for a.g. freelist header */
	int		fields);/* mask of fields to be logged (XFS_AGF_...) */

/*
 * Put the block on the freelist for the allocation group.
 */
void
xfs_alloc_put_freelist(
	xfs_trans_t	*tp,	/* transaction pointer */
	buf_t		*agbp,	/* buffer for a.g. freelist header */
	buf_t		*agflbp,/* buffer for a.g. free block array */
	xfs_agblock_t	bno);	/* block being freed */

/*
 * Allocate an extent (variable-size).
 */
void
xfs_alloc_vextent(
	xfs_alloc_arg_t	*args);	/* allocation argument structure */

/*
 * Free an extent.
 */
int				/* success/failure; will become void */
xfs_free_extent(
	xfs_trans_t	*tp,	/* transaction pointer */
	xfs_fsblock_t	bno,	/* starting block number of extent */
	xfs_extlen_t	len);	/* length of extent */

#endif	/* !_FS_XFS_ALLOC_H */
