#ifndef _FS_XFS_IALLOC_H
#define	_FS_XFS_IALLOC_H

#ident	"$Revision$"

#include <sys/types.h>
#include <sys/pfdat.h>
#include <sys/buf.h>
#include "xfs_types.h"
#include "xfs.h"
#include "xfs_trans.h"

#define	XFS_INODE_LOWBITS	32
#define	XFS_INODE_HIGHBITS	32

#define xfs_ino_to_agno(i)	((xfs_agnumber_t)((i) >> XFS_INODE_LOWBITS))
#define	xfs_ino_to_agino(i)	((xfs_agino_t)((i) & ((1LL << XFS_INODE_LOWBITS) - 1LL)))
#define	xfs_agino_to_ino(a,i)	((((xfs_ino_t)(a)) << XFS_INODE_LOWBITS) | (i))

/*
 * block numbers in the AG; SB is block 0, AGH is block 1, free btree roots
 * are 2 and 3.
 */
#define	XFS_IBT_BLOCK	((xfs_agblock_t)(XFS_CNT_BLOCK + 1))

#define	XFS_PREALLOC_BLOCKS	((xfs_agblock_t)(XFS_IBT_BLOCK + 1))

#define	XFS_IALLOC_MAX_EVER	1024
#define	XFS_IALLOC_MIN_ALLOC(s,a)	((s)->xfsb_inopblock)
#define XFS_IALLOC_MAX_ALLOC(s,a)	\
	((a)->xfsag_icount > XFS_IALLOC_MAX_EVER ? \
	XFS_IALLOC_MAX_EVER : \
	((a)->xfsag_icount) ? (a)->xfsag_icount : XFS_IALLOC_MIN_ALLOC(s,a))

/*
 * Structures for inode mapping
 */
#define	XFS_IBT_MAGIC	0x58494254	/* 'XIBT' */
typedef struct xfs_ialloc_block
{
	u_int32_t	magic;		/* XFS_IBT_MAGIC */
	u_int16_t	level;		/* 0 is a leaf */
	u_int16_t	numrecs;	/* current # of data records */
	xfs_agblock_t	leftsib;	/* left sibling block */
	xfs_agblock_t	rightsib;	/* right sibling block */
} xfs_ialloc_block_t;

/*
 * Data record/key structure
 */
typedef struct xfs_ialloc_rec
{
	xfs_agino_t	startinode;	/* starting inode number */
	xfs_agblock_t	startblock;	/* starting block number */
	xfs_agino_t	inodecount;	/* number of inodes */
} xfs_ialloc_rec_t;

/*
 * Real block structures have a size equal to the file system block size.
 */

#define	XFS_IALLOC_BLOCK_MAXRECS(n,l)	\
	(((1 << (n)) - sizeof(xfs_ialloc_block_t)) / (sizeof(xfs_ialloc_rec_t) + sizeof(xfs_agblock_t) * ((l) > 0)))
#define	XFS_IALLOC_BLOCK_MINRECS(n,l)	(XFS_IALLOC_BLOCK_MAXRECS(n,l) / 2)

#define	XFS_IALLOC_REC_ADDR(b,i,n)	((xfs_ialloc_rec_t *)((char *)(b) + sizeof(xfs_ialloc_block_t) + ((i) - 1) * sizeof(xfs_ialloc_rec_t)))
#define	XFS_IALLOC_PTR_ADDR(b,i,n)	((xfs_agblock_t *)((char *)(b) + sizeof(xfs_ialloc_block_t) + XFS_IALLOC_BLOCK_MAXRECS(n,1) * sizeof(xfs_ialloc_rec_t) + ((i) - 1) * sizeof(xfs_agblock_t)))

#define	XFS_IALLOC_MAXLEVELS	5 /* ??? */

typedef struct xfs_ialloc_cur
{
	xfs_trans_t		*tp;	/* links cursors on freelist */
	buf_t			*agbuf;
	xfs_agnumber_t		agno;
	xfs_ialloc_rec_t	rec;
	buf_t			*bufs[XFS_IALLOC_MAXLEVELS];
	int			ptrs[XFS_IALLOC_MAXLEVELS];
	int			nlevels;
#ifdef XFSIDEBUG
	int			lineno;
	struct xfs_ialloc_cur	*next;	/* on all cursors list */
#endif
} xfs_ialloc_cur_t;

/* 
 * Dummy freelist inode structure.
 */
typedef struct xfs_inode_free
{
	xfs_agino_t	i_next;		/* next free inode, or NULLAGINO */
	int		i_mode;
} xfs_inode_free_t;

#define	xfs_buf_to_iblock(buf)	((xfs_ialloc_block_t *)(buf->b_un.b_addr))

#define	xfs_make_iptr(s,b,o) \
	((xfs_inode_free_t *)((caddr_t)xfs_buf_to_iblock(b) + (o) * (s)->xfsb_inodesize))

/*
 * Prototypes for per-ag routines.
 */
int xfs_ialloc_ag_alloc(xfs_ialloc_cur_t *);
int xfs_ialloc_ag_locate(xfs_ialloc_cur_t *, xfs_agino_t, xfs_agblock_t *, int *);
buf_t *xfs_ialloc_ag_select(xfs_trans_t *, xfs_ino_t, int, int);

/*
 * Prototypes for per-fs routines.
 */
xfs_ino_t xfs_ialloc(xfs_trans_t *, xfs_ino_t, int, int);
xfs_agino_t xfs_ialloc_next_free(xfs_trans_t *, buf_t *, xfs_agino_t);
int xfs_ifree(xfs_trans_t *, xfs_ino_t);

#endif	/* !_FS_XFS_IALLOC_H */
