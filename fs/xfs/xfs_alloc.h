#ifndef _FS_XFS_ALLOC_H
#define	_FS_XFS_ALLOC_H

#ident	"$Revision$"

#include "xfs_types.h"
#include <sys/pfdat.h>
#include <sys/uio.h>
#include <sys/buf.h>
#include <sys/vnode.h>
#include "xfs.h"
#include "xfs_trans.h"

/*
 * Freespace on-disk structures
 */

/*
 * There are two on-disk btrees, one sorted by blockno and one sorted
 * by blockcount and blockno.  All blocks look the same to make the code
 * simpler; if we have time later, we'll make the optimizations.
 */

/*
 * Generic block structure.
 * Maximum record counts are found in a table indexed by
 * log2(blocksize).
 */
#define	XFS_ABTB_MAGIC	0x41425442	/* 'ABTB' for bno tree */
#define	XFS_ABTC_MAGIC	0x41425443	/* 'ABTC' for cnt tree */
typedef struct xfs_alloc_block
{
	__uint32_t	magic;		/* XFS_ABT?_MAGIC */
	__uint16_t	level;		/* 0 is a leaf */
	__uint16_t	numrecs;	/* current # of data records */
	xfs_agblock_t	leftsib;	/* left sibling block */
	xfs_agblock_t	rightsib;	/* right sibling block */
					/* maxrecs data records (recs) */
					/* maxrecs block pointers (ptrs) */
					/* pointers omitted for leaves */
} xfs_alloc_block_t;

/*
 * Data record/key structure
 */
typedef struct xfs_alloc_rec
{
	xfs_agblock_t	startblock;	/* starting block number */
	xfs_extlen_t	blockcount;	/* count of free blocks */
} xfs_alloc_rec_t;

/*
 * Real structures have a size equal to the disk block size.
 */

#define	XFS_ALLOC_BLOCK_MAXRECS(n,l) \
	(((1 << (n)) - sizeof(xfs_alloc_block_t)) / (sizeof(xfs_alloc_rec_t) + sizeof(xfs_agblock_t) * ((l) > 0)))
#define	XFS_ALLOC_BLOCK_MINRECS(n,l) (XFS_ALLOC_BLOCK_MAXRECS(n,l) / 2)

#define	XFS_MIN_BLOCKSIZE_LOG	9
#define	XFS_MAX_BLOCKSIZE_LOG	16
#define	XFS_MIN_BLOCKSIZE	(1 << XFS_MIN_BLOCKSIZE_LOG)
#define	XFS_MAX_BLOCKSIZE	(1 << XFS_MAX_BLOCKSIZE_LOG)

/* block numbers in the AG; SB is block 0, AGH is block 1 */
#define	XFS_BNO_BLOCK	((xfs_agblock_t)(XFS_AGH_BLOCK + 1))
#define	XFS_CNT_BLOCK	((xfs_agblock_t)(XFS_BNO_BLOCK + 1))

#define	XFS_ALLOC_REC_ADDR(b,i,n)	((xfs_alloc_rec_t *)((char *)(b) + sizeof(xfs_alloc_block_t) + ((i) - 1) * sizeof(xfs_alloc_rec_t)))
#define	XFS_ALLOC_PTR_ADDR(b,i,n)	((xfs_agblock_t *)((char *)(b) + sizeof(xfs_alloc_block_t) + XFS_ALLOC_BLOCK_MAXRECS(n,1) * sizeof(xfs_alloc_rec_t) + ((i) - 1) * sizeof(xfs_agblock_t)))

typedef enum xfs_alloctype
{
	XFS_ALLOCTYPE_ANY_AG,
	XFS_ALLOCTYPE_START_AG,
	XFS_ALLOCTYPE_THIS_AG,
	XFS_ALLOCTYPE_NEAR_BNO,
	XFS_ALLOCTYPE_THIS_BNO
} xfs_alloctype_t;

#define	XFS_ALLOC_MAXLEVELS	5
typedef struct xfs_alloc_cur
{
	xfs_trans_t	*tp;		/* links cursors on freelist */
	buf_t		*agbuf;
	xfs_agnumber_t	agno;
	xfs_alloc_rec_t	rec;
	buf_t		*bufs[XFS_ALLOC_MAXLEVELS];
	int		ptrs[XFS_ALLOC_MAXLEVELS];	
	int		nlevels;
	xfs_btnum_t	btnum;
#ifdef XFSADEBUG
	int		lineno;
	struct xfs_alloc_cur *next;	/* on all cursors list */
#endif
} xfs_alloc_cur_t;

#define	xfs_buf_to_ablock(buf)	((xfs_alloc_block_t *)(buf->b_un.b_addr))

/*
 * Prototypes for per-ag allocation routines
 */
xfs_agblock_t xfs_alloc_ag_extent(xfs_trans_t *, buf_t *, xfs_agnumber_t, xfs_agblock_t, xfs_extlen_t, xfs_alloctype_t);
xfs_agblock_t xfs_alloc_ag_vextent(xfs_trans_t *, buf_t *, xfs_agnumber_t, xfs_agblock_t, xfs_extlen_t, xfs_extlen_t, xfs_extlen_t *, xfs_alloctype_t);
xfs_agblock_t xfs_alloc_next_free(xfs_trans_t *, buf_t *, xfs_agblock_t);
buf_t *xfs_alloc_fix_freelist(xfs_trans_t *, xfs_agnumber_t);
int xfs_free_ag_extent(xfs_trans_t *, buf_t *, xfs_agnumber_t, xfs_agblock_t, xfs_extlen_t);

/*
 * Prototypes for per-fs allocation routines
 */
xfs_fsblock_t xfs_alloc_extent(xfs_trans_t *, xfs_fsblock_t, xfs_extlen_t, xfs_alloctype_t);
xfs_fsblock_t xfs_alloc_vextent(xfs_trans_t *, xfs_fsblock_t, xfs_extlen_t, xfs_extlen_t, xfs_extlen_t *, xfs_alloctype_t);
int xfs_free_extent(xfs_trans_t *, xfs_fsblock_t, xfs_extlen_t);

extern __uint32_t xfs_magics[];

#endif	/* !_FS_XFS_ALLOC_H */
