#ident	"$Revision: 1.46 $"

/*
 * This file contains common code for the space manager's btree implementations.
 */

#ifdef SIM
#define _KERNEL 1
#endif
#include <sys/param.h>
#include <sys/buf.h>
#include <sys/uuid.h>
#include <sys/vnode.h>
#include <sys/grio.h>
#ifdef SIM
#undef _KERNEL
#endif
#include <sys/debug.h>
#include <sys/kmem.h>
#ifdef SIM
#include <bstring.h>
#else
#include <sys/systm.h>
#endif
#include "xfs_macros.h"
#include "xfs_types.h"
#include "xfs_inum.h"
#include "xfs_log.h"
#include "xfs_trans.h"
#include "xfs_sb.h"
#include "xfs_ag.h"
#include "xfs_mount.h"
#include "xfs_alloc_btree.h"
#include "xfs_bmap_btree.h"
#include "xfs_ialloc_btree.h"
#include "xfs_btree.h"
#include "xfs_ialloc.h"
#include "xfs_attr_sf.h"
#include "xfs_dir_sf.h"
#include "xfs_dinode.h"
#include "xfs_inode.h"
#include "xfs_bit.h"
#ifdef SIM
#include "sim.h"
#endif

/*
 * Cursor allocation zone.
 */
zone_t	*xfs_btree_cur_zone;

/*
 * Btree magic numbers.
 */
const __uint32_t xfs_magics[XFS_BTNUM_MAX] =
{
	XFS_ABTB_MAGIC, XFS_ABTC_MAGIC, XFS_BMAP_MAGIC, XFS_IBT_MAGIC
};

/* 
 * Prototypes for internal routines.
 */

#ifdef DEBUG
/*
 * Debug routine: return maxrecs for the block.
 */
STATIC int				/* number of records fitting in block */
xfs_btree_maxrecs(
	xfs_btree_cur_t		*cur,	/* btree cursor */
	xfs_btree_block_t	*block);/* generic btree block pointer */
#endif	/* DEBUG */

/*
 * Internal routines.
 */

#ifdef DEBUG
/*
 * Debug routine: return maxrecs for the block.
 */
STATIC int				/* number of records fitting in block */
xfs_btree_maxrecs(
	xfs_btree_cur_t		*cur,	/* btree cursor */
	xfs_btree_block_t	*block)	/* generic btree block pointer */
{
	int			maxrecs;

	switch (cur->bc_btnum) {
	case XFS_BTNUM_BNO:
	case XFS_BTNUM_CNT:
		maxrecs = XFS_ALLOC_BLOCK_MAXRECS(block->bb_h.bb_level, cur);
		break;
	case XFS_BTNUM_BMAP:
		maxrecs = XFS_BMAP_BLOCK_IMAXRECS(block->bb_h.bb_level, cur);
		break;
	case XFS_BTNUM_INO:
		maxrecs = XFS_INOBT_BLOCK_MAXRECS(block->bb_h.bb_level, cur);
		break;
	}
	return maxrecs;
}
#endif	/* DEBUG */

/*
 * External routines.
 */

#ifdef DEBUG
/*
 * Debug routine: check that block header is ok.
 */
void
xfs_btree_check_block(
	xfs_btree_cur_t		*cur,	/* btree cursor */
	xfs_btree_block_t	*block,	/* generic btree block pointer */
	int			level)	/* level of the btree block */
{
	if (XFS_BTREE_LONG_PTRS(cur->bc_btnum))
		xfs_btree_check_lblock(cur, (xfs_btree_lblock_t *)block, level);
	else
		xfs_btree_check_sblock(cur, (xfs_btree_sblock_t *)block, level);
}

/*
 * Debug routine: check that keys are in the right order.
 */
void
xfs_btree_check_key(
	xfs_btnum_t	btnum,		/* btree identifier */
	void		*ak1,		/* pointer to left (lower) key */
	void		*ak2)		/* pointer to right (higher) key */
{
	switch (btnum) {
	case XFS_BTNUM_BNO: {
		xfs_alloc_key_t	*k1;
		xfs_alloc_key_t	*k2;

		k1 = ak1;
		k2 = ak2;
		ASSERT(k1->ar_startblock < k2->ar_startblock);
		break;
	    }
	case XFS_BTNUM_CNT: {
		xfs_alloc_key_t	*k1;
		xfs_alloc_key_t	*k2;

		k1 = ak1;
		k2 = ak2;
		ASSERT(k1->ar_blockcount < k2->ar_blockcount ||
		       (k1->ar_blockcount == k2->ar_blockcount &&
			k1->ar_startblock < k2->ar_startblock));
		break;
	    }
	case XFS_BTNUM_BMAP: {
		xfs_bmbt_key_t	*k1;
		xfs_bmbt_key_t	*k2;

		k1 = ak1; 
		k2 = ak2;
		ASSERT(k1->br_startoff < k2->br_startoff);
		break;
	    }
	case XFS_BTNUM_INO: {
		xfs_inobt_key_t	*k1;
		xfs_inobt_key_t	*k2;

		k1 = ak1;
		k2 = ak2;
		ASSERT(k1->ir_startino < k2->ir_startino);
		break;
	    }
	}
}

/*
 * Debug routine: check that long form block header is ok.
 */
void
xfs_btree_check_lblock(
	xfs_btree_cur_t		*cur,	/* btree cursor */
	xfs_btree_lblock_t	*block,	/* btree long form block pointer */
	int			level)	/* level of the btree block */
{
	xfs_mount_t		*mp;	/* file system mount point */

	mp = cur->bc_mp;
	ASSERT(block->bb_magic == xfs_magics[cur->bc_btnum]);
	ASSERT(block->bb_level == level);
	ASSERT(block->bb_numrecs <=
	       xfs_btree_maxrecs(cur, (xfs_btree_block_t *)block));
	ASSERT(block->bb_leftsib == NULLDFSBNO || 
	       (XFS_FSB_TO_AGNO(mp, block->bb_leftsib) < mp->m_sb.sb_agcount &&
		XFS_FSB_TO_AGBNO(mp, block->bb_leftsib) <
		mp->m_sb.sb_agblocks));
	ASSERT(block->bb_rightsib == NULLDFSBNO || 
	       (XFS_FSB_TO_AGNO(mp, block->bb_rightsib) < mp->m_sb.sb_agcount &&
		XFS_FSB_TO_AGBNO(mp, block->bb_rightsib) <
		mp->m_sb.sb_agblocks));
}

/*
 * Debug routine: check that (long) pointer is ok.
 */
void
xfs_btree_check_lptr(
	xfs_btree_cur_t	*cur,		/* btree cursor */
	xfs_dfsbno_t	ptr,		/* btree block disk address */
	int		level)		/* btree block level */
{
	xfs_mount_t	*mp;		/* file system mount point */

	ASSERT(level > 0);
	mp = cur->bc_mp;
	ASSERT(ptr != NULLDFSBNO);
	ASSERT(XFS_FSB_TO_AGNO(mp, ptr) < mp->m_sb.sb_agcount);
	ASSERT(XFS_FSB_TO_AGBNO(mp, ptr) < mp->m_sb.sb_agblocks);
}

/*
 * Debug routine: check that records are in the right order.
 */
void
xfs_btree_check_rec(
	xfs_btnum_t	btnum,		/* btree identifier */
	void		*ar1,		/* pointer to left (lower) record */
	void		*ar2)		/* pointer to right (higher) record */
{
	switch (btnum) {
	case XFS_BTNUM_BNO: {
		xfs_alloc_rec_t	*r1;
		xfs_alloc_rec_t	*r2;

		r1 = ar1;
		r2 = ar2;
		ASSERT(r1->ar_startblock + r1->ar_blockcount <=
		       r2->ar_startblock);
		break;
	    }
	case XFS_BTNUM_CNT: {
		xfs_alloc_rec_t	*r1;
		xfs_alloc_rec_t	*r2;
		
		r1 = ar1;
		r2 = ar2;
		ASSERT(r1->ar_blockcount < r2->ar_blockcount ||
		       (r1->ar_blockcount == r2->ar_blockcount &&
			r1->ar_startblock < r2->ar_startblock));
		break;
	    }
	case XFS_BTNUM_BMAP: {
		xfs_bmbt_rec_t	*r1;
		xfs_bmbt_rec_t	*r2;

		r1 = ar1;
		r2 = ar2;
		ASSERT(xfs_bmbt_get_startoff(r1) +
		        xfs_bmbt_get_blockcount(r1) <=
		       xfs_bmbt_get_startoff(r2));
		break;
	    }
	case XFS_BTNUM_INO: {
		xfs_inobt_rec_t	*r1;
		xfs_inobt_rec_t	*r2;

		r1 = ar1;
		r2 = ar2;
		ASSERT(r1->ir_startino + XFS_INODES_PER_CHUNK <=
		       r2->ir_startino);
		break;
	    }
	}
}

/*
 * Debug routine: check that block header is ok.
 */
void
xfs_btree_check_sblock(
	xfs_btree_cur_t		*cur,	/* btree cursor */
	xfs_btree_sblock_t	*block,	/* btree short form block pointer */
	int			level)	/* level of the btree block */
{
	buf_t			*agbp;	/* buffer for ag. freespace struct */
	xfs_agf_t		*agf;	/* ag. freespace structure */

	ASSERT(block->bb_magic == xfs_magics[cur->bc_btnum]);
	ASSERT(block->bb_level == level);
	ASSERT(block->bb_numrecs <=
	       xfs_btree_maxrecs(cur, (xfs_btree_block_t *)block));
	agbp = cur->bc_private.a.agbp;
	agf = XFS_BUF_TO_AGF(agbp);
	ASSERT(block->bb_leftsib == NULLAGBLOCK || 
	       block->bb_leftsib < agf->agf_length);
	ASSERT(block->bb_leftsib != 0);
	ASSERT(block->bb_rightsib == NULLAGBLOCK || 
	       block->bb_rightsib < agf->agf_length);
	ASSERT(block->bb_rightsib != 0);
}

/*
 * Debug routine: check that (short) pointer is ok.
 */
void
xfs_btree_check_sptr(
	xfs_btree_cur_t	*cur,		/* btree cursor */
	xfs_agblock_t	ptr,		/* btree block disk address */
	int		level)		/* btree block level */
{
	buf_t		*agbp;		/* buffer for ag. freespace struct */
	xfs_agf_t	*agf;		/* ag. freespace structure */

	ASSERT(level > 0);
	agbp = cur->bc_private.a.agbp;
	agf = XFS_BUF_TO_AGF(agbp);
	ASSERT(ptr != NULLAGBLOCK);
	ASSERT(ptr != 0);
	ASSERT(ptr < agf->agf_length);
}
#endif	/* DEBUG */

/*
 * Delete the btree cursor.
 */
void
xfs_btree_del_cursor(
	xfs_btree_cur_t	*cur)		/* btree cursor */
{
	int		i;		/* btree level */

	/*
	 * Clear the buffer pointers, and release the buffers.
	 */
	for (i = 0; i < cur->bc_nlevels; i++) {
		if (cur->bc_bufs[i])
			xfs_btree_setbuf(cur, i, NULL);
		else
			break;
	}
	/*
	 * Can't free a bmap cursor without having dealt with the 
	 * allocated indirect blocks' accounting.
	 */
	ASSERT(cur->bc_btnum != XFS_BTNUM_BMAP ||
	       cur->bc_private.b.allocated == 0);
	/*
	 * Free the cursor.
	 */
	kmem_zone_free(xfs_btree_cur_zone, cur);
}

/*
 * Duplicate the btree cursor.
 * Allocate a new one, copy the record, re-get the buffers.
 */
xfs_btree_cur_t *			/* new btree cursor */
xfs_btree_dup_cursor(
	xfs_btree_cur_t	*cur)		/* btree cursor */
{
	buf_t		*bp;		/* btree block's buffer pointer */
	int		i;		/* level number of btree block */
	xfs_mount_t	*mp;		/* mount structure for filesystem */
	xfs_btree_cur_t	*ncur;		/* return value */
	xfs_trans_t	*tp;		/* transaction pointer, can be NULL */

	tp = cur->bc_tp;
	mp = cur->bc_mp;
	/*
	 * Allocate a new cursor like the old one.
	 */
	ncur = xfs_btree_init_cursor(mp, tp, cur->bc_private.a.agbp,
				     cur->bc_private.a.agno, cur->bc_btnum,
				     cur->bc_private.b.ip,
				     cur->bc_private.b.whichfork);
	/*
	 * Copy the record currently in the cursor.
	 */
	ncur->bc_rec = cur->bc_rec;
	/*
	 * For each level current, re-get the buffer and copy the ptr value.
	 */
	for (i = 0; i < ncur->bc_nlevels; i++) {
		ncur->bc_ptrs[i] = cur->bc_ptrs[i];
		ncur->bc_ra[i] = cur->bc_ra[i];
		if (bp = cur->bc_bufs[i]) {
			(void) xfs_trans_read_buf(tp, mp->m_dev, bp->b_blkno,
						  mp->m_bsize, 0, &bp);
			ncur->bc_bufs[i] = bp;
			ASSERT(bp);
			ASSERT(!geterror(bp));
		} else
			ncur->bc_bufs[i] = NULL;
	}
	/*
	 * For bmap btrees, copy the firstblock, flist, and flags values,
	 * since init cursor doesn't get them.
	 */
	if (ncur->bc_btnum == XFS_BTNUM_BMAP) {
		ncur->bc_private.b.firstblock = cur->bc_private.b.firstblock;
		ncur->bc_private.b.flist = cur->bc_private.b.flist;
		ncur->bc_private.b.flags = cur->bc_private.b.flags;
	}
	return ncur;
}

/*
 * Change the cursor to point to the first record at the given level.
 * Other levels are unaffected.
 */
int					/* success=1, failure=0 */
xfs_btree_firstrec(
	xfs_btree_cur_t		*cur,	/* btree cursor */
	int			level)	/* level to change */
{
	xfs_btree_block_t	*block;	/* generic btree block pointer */

	/*
	 * Get the block pointer for this level.
	 */
	block = xfs_btree_get_block(cur, level);
	xfs_btree_check_block(cur, block, level);
	/*
	 * It's empty, there is no such record.
	 */
	if (block->bb_h.bb_numrecs == 0)
		return 0;
	/*
	 * Set the ptr value to 1, that's the first record/key.
	 */
	cur->bc_ptrs[level] = 1;
	return 1;
}

/* 
 * Retrieve the block pointer from the cursor at the given level.
 * This may be a bmap btree root or from a buffer.
 */
xfs_btree_block_t *			/* generic btree block pointer */
xfs_btree_get_block(
	xfs_btree_cur_t		*cur,	/* btree cursor */
	int			level)	/* level in btree */
{
	xfs_btree_block_t	*block;	/* return value */
	xfs_ifork_t		*ifp;	/* inode fork pointer */
	int			whichfork; /* data or attr fork */

	if (cur->bc_btnum == XFS_BTNUM_BMAP && level == cur->bc_nlevels - 1) {
		whichfork = cur->bc_private.b.whichfork;
		ifp = XFS_IFORK_PTR(cur->bc_private.b.ip, whichfork);
		block = (xfs_btree_block_t *)ifp->if_broot;
	} else
		block = XFS_BUF_TO_BLOCK(cur->bc_bufs[level]);
	ASSERT(block != NULL);
	return block;
}

/*
 * Get a buffer for the block, return it with no data read.
 * Long-form addressing.
 */
buf_t *					/* buffer for fsbno */
xfs_btree_get_bufl(
	xfs_mount_t	*mp,		/* file system mount point */
	xfs_trans_t	*tp,		/* transaction pointer */
	xfs_fsblock_t	fsbno,		/* file system block number */
	uint		lock)		/* lock flags for get_buf */
{
	buf_t		*bp;		/* buffer pointer (return value) */
	daddr_t		d;		/* real disk block address */

	ASSERT(fsbno != NULLFSBLOCK);
	d = XFS_FSB_TO_DADDR(mp, fsbno);
	bp = xfs_trans_get_buf(tp, mp->m_dev, d, mp->m_bsize, lock);
	ASSERT(bp);
	ASSERT(!geterror(bp));
	return bp;
}

/*
 * Get a buffer for the block, return it with no data read.
 * Short-form addressing.
 */
buf_t *					/* buffer for agno/agbno */
xfs_btree_get_bufs(
	xfs_mount_t	*mp,		/* file system mount point */
	xfs_trans_t	*tp,		/* transaction pointer */
	xfs_agnumber_t	agno,		/* allocation group number */
	xfs_agblock_t	agbno,		/* allocation group block number */
	uint		lock)		/* lock flags for get_buf */
{
	buf_t		*bp;		/* buffer pointer (return value) */
	daddr_t		d;		/* real disk block address */

	ASSERT(agno != NULLAGNUMBER);
	ASSERT(agbno != NULLAGBLOCK);
	d = XFS_AGB_TO_DADDR(mp, agno, agbno);
	bp = xfs_trans_get_buf(tp, mp->m_dev, d, mp->m_bsize, lock);
	ASSERT(bp);
	ASSERT(!geterror(bp));
	return bp;
}

/*
 * Allocate a new btree cursor.
 * The cursor is either for allocation (A) or bmap (B) or inodes (I).
 */
xfs_btree_cur_t *			/* new btree cursor */
xfs_btree_init_cursor(
	xfs_mount_t	*mp,		/* file system mount point */
	xfs_trans_t	*tp,		/* transaction pointer */
	buf_t		*agbp,		/* (A only) buffer for agf structure */
					/* (I only) buffer for agi structure */
	xfs_agnumber_t	agno,		/* (AI only) allocation group number */
	xfs_btnum_t	btnum,		/* btree identifier */
	xfs_inode_t	*ip,		/* (B only) inode owning the btree */
	int		whichfork)	/* (B only) data or attr fork */
{
	xfs_agf_t	*agf;		/* (A) allocation group freespace */
	xfs_agi_t	*agi;		/* (I) allocation group inodespace */
	xfs_btree_cur_t	*cur;		/* return value */
	xfs_ifork_t	*ifp;		/* (I) inode fork pointer */
	int		nlevels;	/* number of levels in the btree */

	ASSERT(xfs_btree_cur_zone != NULL);
	/*
	 * Allocate a new cursor.
	 */
	cur = kmem_zone_zalloc(xfs_btree_cur_zone, KM_SLEEP);
	/* 
	 * Deduce the number of btree levels from the arguments.
	 */
	switch (btnum) {
	case XFS_BTNUM_BNO:
	case XFS_BTNUM_CNT:
		agf = XFS_BUF_TO_AGF(agbp);
		nlevels = agf->agf_levels[btnum];
		break;
	case XFS_BTNUM_BMAP:
		ifp = XFS_IFORK_PTR(ip, whichfork);
		nlevels = ifp->if_broot->bb_level + 1;
		break;
	case XFS_BTNUM_INO:
		agi = XFS_BUF_TO_AGI(agbp);
		nlevels = agi->agi_level;
		break;
	}
	/*
	 * Fill in the common fields.
	 */
	cur->bc_tp = tp;
	cur->bc_mp = mp;
	cur->bc_nlevels = nlevels;
	cur->bc_btnum = btnum;
	cur->bc_blocklog = mp->m_sb.sb_blocklog;
	/*
	 * Fill in private fields.
	 */
	switch (btnum) {
	case XFS_BTNUM_BNO:
	case XFS_BTNUM_CNT:
		/*
		 * Allocation btree fields.
		 */
		cur->bc_private.a.agbp = agbp;
		cur->bc_private.a.agno = agno;
		break;
	case XFS_BTNUM_BMAP:
		/*
		 * Bmap btree fields.
		 */
		cur->bc_private.b.forksize = XFS_IFORK_SIZE(ip, whichfork);
		cur->bc_private.b.ip = ip;
		cur->bc_private.b.firstblock = NULLFSBLOCK;
		cur->bc_private.b.flist = NULL;
		cur->bc_private.b.allocated = 0;
		cur->bc_private.b.flags = 0;
		cur->bc_private.b.whichfork = whichfork;
		break;
	case XFS_BTNUM_INO:
		/*
		 * Inode allocation btree fields.
		 */
		cur->bc_private.i.agbp = agbp;
		cur->bc_private.i.agno = agno;
		break;
	default:
		ASSERT(0);
	}
	return cur;
}

/*
 * Check for the cursor referring to the last block at the given level.
 */
int					/* 1=is last block, 0=not last block */
xfs_btree_islastblock(
	xfs_btree_cur_t		*cur,	/* btree cursor */
	int			level)	/* level to check */
{
	xfs_btree_block_t	*block;	/* generic btree block pointer */

	block = xfs_btree_get_block(cur, level);
	xfs_btree_check_block(cur, block, level);
	if (XFS_BTREE_LONG_PTRS(cur->bc_btnum))
		return block->bb_u.l.bb_rightsib == NULLDFSBNO;
	else
		return block->bb_u.s.bb_rightsib == NULLAGBLOCK;
}

/*
 * Change the cursor to point to the last record in the current block
 * at the given level.  Other levels are unaffected.
 */
int					/* success=1, failure=0 */
xfs_btree_lastrec(
	xfs_btree_cur_t		*cur,	/* btree cursor */
	int			level)	/* level to change */
{
	xfs_btree_block_t	*block;	/* generic btree block pointer */

	/*
	 * Get the block pointer for this level.
	 */
	block = xfs_btree_get_block(cur, level);
	xfs_btree_check_block(cur, block, level);
	/*
	 * It's empty, there is no such record.
	 */
	if (block->bb_h.bb_numrecs == 0)
		return 0;
	/*
	 * Set the ptr value to numrecs, that's the last record/key.
	 */
	cur->bc_ptrs[level] = block->bb_h.bb_numrecs;
	return 1;
}

/*
 * Compute first and last byte offsets for the fields given.
 * Interprets the offsets table, which contains struct field offsets.
 */
void
xfs_btree_offsets(
	__int64_t	fields,		/* bitmask of fields */
	const short	*offsets,	/* table of field offsets */
	int		nbits,		/* number of bits to inspect */
	int		*first,		/* output: first byte offset */
	int		*last)		/* output: last byte offset */
{
	int		i;		/* current bit number */
	__int64_t	imask;		/* mask for current bit number */

	ASSERT(fields != 0);
	/*
	 * Find the lowest bit, so the first byte offset.
	 */
	for (i = 0, imask = 1LL; ; i++, imask <<= 1) {
		if (imask & fields) {
			*first = offsets[i];
			break;
		}
	}
	/*
	 * Find the highest bit, so the last byte offset.
	 */
	for (i = nbits - 1, imask = 1LL << i; ; i--, imask >>= 1) {
		if (imask & fields) {
			*last = offsets[i + 1] - 1;
			break;
		}
	}
}

/*
 * Get a buffer for the block, return it read in.
 * Long-form addressing.
 */
int					/* error */
xfs_btree_read_bufl(
	xfs_mount_t	*mp,		/* file system mount point */
	xfs_trans_t	*tp,		/* transaction pointer */
	xfs_fsblock_t	fsbno,		/* file system block number */
	uint		lock,		/* lock flags for read_buf */
	buf_t		**bpp)		/* buffer for fsbno */
{
	buf_t		*bp;		/* return value */
	daddr_t		d;		/* real disk block address */
	int		error;

	ASSERT(fsbno != NULLFSBLOCK);
	d = XFS_FSB_TO_DADDR(mp, fsbno);
	error = xfs_trans_read_buf(tp, mp->m_dev, d, mp->m_bsize, lock, &bp);
	if (error) {
		return error;
	}
	ASSERT(!bp || !geterror(bp));
	if (bp != NULL) {
		bp->b_ref = XFS_GEN_LBTREE_REF;
	}
	*bpp = bp;
	return 0;
}

/*
 * Get a buffer for the block, return it read in.
 * Short-form addressing.
 */
int					/* error */
xfs_btree_read_bufs(
	xfs_mount_t	*mp,		/* file system mount point */
	xfs_trans_t	*tp,		/* transaction pointer */
	xfs_agnumber_t	agno,		/* allocation group number */
	xfs_agblock_t	agbno,		/* allocation group block number */
	uint		lock,		/* lock flags for read_buf */
	buf_t		**bpp)		/* buffer for agno/agbno */
{
	buf_t		*bp;		/* return value */
	daddr_t		d;		/* real disk block address */
	int		error;

	ASSERT(agno != NULLAGNUMBER);
	ASSERT(agbno != NULLAGBLOCK);
	d = XFS_AGB_TO_DADDR(mp, agno, agbno);
	error = xfs_trans_read_buf(tp, mp->m_dev, d, mp->m_bsize, lock, &bp);
	if (error) {
		return error;
	}
	ASSERT(!bp || !geterror(bp));
	if (bp != NULL) {
		bp->b_ref = XFS_GEN_SBTREE_REF;
	}
	*bpp = bp;
	return 0;
}

/*
 * Read-ahead the block, don't wait for it, don't return a buffer.
 * Long-form addressing.
 */
void
xfs_btree_reada_bufl(
	xfs_mount_t	*mp,		/* file system mount point */
	xfs_fsblock_t	fsbno,		/* file system block number */
	xfs_extlen_t	count)		/* count of filesystem blocks */
{
	daddr_t		d;

	ASSERT(fsbno != NULLFSBLOCK);
#ifndef SIM
	d = XFS_FSB_TO_DADDR(mp, fsbno);
	baread(mp->m_dev, d, mp->m_bsize * count);
#endif
}

/*
 * Read-ahead the block, don't wait for it, don't return a buffer.
 * Short-form addressing.
 */
void
xfs_btree_reada_bufs(
	xfs_mount_t	*mp,		/* file system mount point */
	xfs_agnumber_t	agno,		/* allocation group number */
	xfs_agblock_t	agbno,		/* allocation group block number */
	xfs_extlen_t	count)		/* count of filesystem blocks */
{
	daddr_t		d;

	ASSERT(agno != NULLAGNUMBER);
	ASSERT(agbno != NULLAGBLOCK);
#ifndef SIM
	d = XFS_AGB_TO_DADDR(mp, agno, agbno);
	baread(mp->m_dev, d, mp->m_bsize * count);
#endif
}

/*
 * Read-ahead btree blocks, at the given level.
 * Bits in lr are set from XFS_BTCUR_{LEFT,RIGHT}RA.
 */
int
xfs_btree_readahead(
	xfs_btree_cur_t		*cur,		/* btree cursor */
	int			lev,		/* level in btree */
	int			lr)		/* left/right bits */
{
	xfs_alloc_block_t	*a;
	xfs_bmbt_block_t	*b;
	xfs_inobt_block_t	*i;
	int			rval = 0;

	ASSERT(cur->bc_bufs[lev] != NULL);
	if ((cur->bc_ra[lev] | lr) == cur->bc_ra[lev])
		return 0;
	cur->bc_ra[lev] |= lr;
	switch (cur->bc_btnum) {
	case XFS_BTNUM_BNO:
	case XFS_BTNUM_CNT:
		a = XFS_BUF_TO_ALLOC_BLOCK(cur->bc_bufs[lev]);
		if ((lr & XFS_BTCUR_LEFTRA) && a->bb_leftsib != NULLAGBLOCK) {
			xfs_btree_reada_bufs(cur->bc_mp, cur->bc_private.a.agno,
				a->bb_leftsib, 1);
			rval++;
		}
		if ((lr & XFS_BTCUR_RIGHTRA) && a->bb_rightsib != NULLAGBLOCK) {
			xfs_btree_reada_bufs(cur->bc_mp, cur->bc_private.a.agno,
				a->bb_rightsib, 1);
			rval++;
		}
		break;
	case XFS_BTNUM_BMAP:
		b = XFS_BUF_TO_BMBT_BLOCK(cur->bc_bufs[lev]);
		if ((lr & XFS_BTCUR_LEFTRA) && b->bb_leftsib != NULLDFSBNO) {
			xfs_btree_reada_bufl(cur->bc_mp, b->bb_leftsib, 1);
			rval++;
		}
		if ((lr & XFS_BTCUR_RIGHTRA) && b->bb_rightsib != NULLDFSBNO) {
			xfs_btree_reada_bufl(cur->bc_mp, b->bb_rightsib, 1);
			rval++;
		}
		break;
	case XFS_BTNUM_INO:
		i = XFS_BUF_TO_INOBT_BLOCK(cur->bc_bufs[lev]);
		if ((lr & XFS_BTCUR_LEFTRA) && i->bb_leftsib != NULLAGBLOCK) {
			xfs_btree_reada_bufs(cur->bc_mp, cur->bc_private.i.agno,
				i->bb_leftsib, 1);
			rval++;
		}
		if ((lr & XFS_BTCUR_RIGHTRA) && i->bb_rightsib != NULLAGBLOCK) {
			xfs_btree_reada_bufs(cur->bc_mp, cur->bc_private.i.agno,
				i->bb_rightsib, 1);
			rval++;
		}
		break;
	}
	return rval;
}

/*
 * Set the buffer for level "lev" in the cursor to bp, releasing
 * any previous buffer.
 */
void
xfs_btree_setbuf(
	xfs_btree_cur_t		*cur,	/* btree cursor */
	int			lev,	/* level in btree */
	buf_t			*bp)	/* new buffer to set */
{
	xfs_btree_block_t	*b;	/* btree block */
	buf_t			*obp;	/* old buffer pointer */

	obp = cur->bc_bufs[lev];
	if (obp)
		xfs_trans_brelse(cur->bc_tp, obp);
	cur->bc_bufs[lev] = bp;
	cur->bc_ra[lev] = 0;
	if (!bp)
		return;
	b = XFS_BUF_TO_BLOCK(bp);
	if (XFS_BTREE_LONG_PTRS(cur->bc_btnum)) {
		if (b->bb_u.l.bb_leftsib == NULLDFSBNO)
			cur->bc_ra[lev] |= XFS_BTCUR_LEFTRA;
		if (b->bb_u.l.bb_rightsib == NULLDFSBNO)
			cur->bc_ra[lev] |= XFS_BTCUR_RIGHTRA;
	} else {
		if (b->bb_u.s.bb_leftsib == NULLAGBLOCK)
			cur->bc_ra[lev] |= XFS_BTCUR_LEFTRA;
		if (b->bb_u.s.bb_rightsib == NULLAGBLOCK)
			cur->bc_ra[lev] |= XFS_BTCUR_RIGHTRA;
	}
}
