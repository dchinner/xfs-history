#ident	"$Revision: 1.4 $"

#include <sys/param.h>
#include <sys/buf.h>
#include <sys/vnode.h>
#include "xfs_types.h"
#include "xfs_inum.h"
#include "xfs.h"
#include "xfs_trans.h"
#include "xfs_sb.h"
#include "xfs_ag.h"
#include "xfs_mount.h"
#include "xfs_alloc.h"
#include "xfs_ialloc.h"
#include "xfs_bmap.h"
#include "xfs_btree.h"
#include "xfs_dinode.h"
#include "xfs_inode_item.h"
#include "xfs_inode.h"

#ifdef SIM
#include "sim.h"
#include <stddef.h>
#include <bstring.h>
#ifndef XFSDEBUG
#define	NDEBUG
#endif
#include <assert.h>
#define	ASSERT(x)	assert(x)
#endif

xfs_btree_cur_t *xfs_btree_curfreelist;

__uint32_t xfs_magics[XFS_BTNUM_MAX] =
{
	XFS_ABTB_MAGIC, XFS_ABTC_MAGIC, XFS_IBT_MAGIC, XFS_BMAP_MAGIC
};

/*
 * Get a buffer for the block, return it.
 */
buf_t *
xfs_btree_bread(xfs_mount_t *mp, xfs_trans_t *tp, xfs_agnumber_t agno, xfs_agblock_t agbno)
{
	daddr_t d;
	xfs_sb_t *sbp;

	sbp = &mp->m_sb;
	d = xfs_agb_to_daddr(sbp, agno, agbno);
	return xfs_trans_bread(tp, mp->m_dev, d, mp->m_bsize);
}

#ifdef XFSDEBUG
/*
 * Debug routine: check that block header is ok.
 */
void
xfs_btree_check_block(xfs_btree_cur_t *cur, xfs_btree_block_t *block, int level)
{
	buf_t *agbuf;
	xfs_aghdr_t *agp;

	agbuf = cur->bc_agbuf;
	agp = xfs_buf_to_agp(agbuf);
	ASSERT(block->bb_magic == xfs_magics[cur->bc_btnum]);
	ASSERT(block->bb_level == level);
	ASSERT(block->bb_numrecs <= xfs_btree_maxrecs(cur, block));
	ASSERT(block->bb_leftsib == NULLAGBLOCK || 
	       block->bb_leftsib < agp->ag_length);
	ASSERT(block->bb_rightsib == NULLAGBLOCK || 
	       block->bb_rightsib < agp->ag_length);
}

/*
 * Debug routine: check that pointer is ok.
 */
void
xfs_btree_check_ptr(xfs_btree_cur_t *cur, xfs_agblock_t ptr, int level)
{
	buf_t *agbuf;
	xfs_aghdr_t *agp;

	ASSERT(level > 0);
	agbuf = cur->bc_agbuf;
	agp = xfs_buf_to_agp(agbuf);
	ASSERT(ptr != NULLAGBLOCK && ptr < agp->ag_length);
}

/*
 * Debug routine: check that records are in the right order.
 */
void
xfs_btree_check_rec(xfs_btnum_t btnum, void *ar1, void *ar2)
{
	switch (btnum) {
	case XFS_BTNUM_BNO: {
		xfs_alloc_rec_t *r1 = ar1, *r2 = ar2;
		ASSERT(r1->ar_startblock + r1->ar_blockcount <= r2->ar_startblock);
		break;
	    }
	case XFS_BTNUM_CNT: {
		xfs_alloc_rec_t *r1 = ar1, *r2 = ar2;
		ASSERT(r1->ar_blockcount < r2->ar_blockcount ||
		       (r1->ar_blockcount == r2->ar_blockcount &&
			r1->ar_startblock < r2->ar_startblock));
		break;
	    }
	case XFS_BTNUM_IBT: {
		xfs_ialloc_rec_t *r1 = ar1, *r2 = ar2;
		ASSERT(r1->ir_startinode + r1->ir_inodecount <= r2->ir_startinode);
		break;
	    }
	case XFS_BTNUM_BMAP: {
		xfs_bmbt_rec_t *r1 = ar1, *r2 = ar2;
		ASSERT(xfs_offset_ptox(r1->br_startoff) + xfs_extlen_ptox(r1->br_blockcount) <= xfs_offset_ptox(r2->br_startoff));
		break;
	    }
	}
}
#endif

/*
 * Delete the cursor.
 */
void
xfs_btree_del_cursor(xfs_btree_cur_t *cur)
{
	buf_t *buf;
	int i;

	for (i = 0; i < cur->bc_nlevels; i++) {
		if (buf = cur->bc_bufs[i])
			xfs_btree_setbuf(cur, i, 0);
		else
			break;
	}
	cur->bc_tp = (xfs_trans_t *)xfs_btree_curfreelist;
	xfs_btree_curfreelist = cur;
}

/*
 * Duplicate the cursor.
 * Allocate a new one, copy record, re-get the buffers.
 */
xfs_btree_cur_t *
xfs_btree_dup_cursor(xfs_btree_cur_t *cur)
{
	buf_t *buf;
	int i;
	xfs_mount_t *mp;
	xfs_btree_cur_t *newcur;
	xfs_trans_t *tp;

	tp = cur->bc_tp;
	mp = cur->bc_mp;
	newcur = xfs_btree_init_cursor(mp, tp, cur->bc_agbuf, cur->bc_agno, cur->bc_btnum, cur->bc_private.b.ip);
	newcur->bc_rec = cur->bc_rec;
	for (i = 0; i < newcur->bc_nlevels; i++) {
		newcur->bc_ptrs[i] = cur->bc_ptrs[i];
		if (buf = cur->bc_bufs[i])
			newcur->bc_bufs[i] = xfs_trans_bread(tp, mp->m_dev, buf->b_blkno, mp->m_bsize);
		else
			newcur->bc_bufs[i] = 0;
	}
	return newcur;
}

/*
 * Point to the first record at that level.
 * Other levels are unaffected.
 */
int
xfs_btree_firstrec(xfs_btree_cur_t *cur, int level)
{
	xfs_btree_block_t *block;

	block = xfs_btree_get_block(cur, level);
	xfs_btree_check_block(cur, block, level);
	if (!block->bb_numrecs)
		return 0;
	cur->bc_ptrs[level] = 1;
	return 1;
}

/*
 * Allocate a new cursor.
 */
xfs_btree_cur_t *
xfs_btree_init_cursor(xfs_mount_t *mp, xfs_trans_t *tp, buf_t *agbuf, xfs_agnumber_t agno, xfs_btnum_t btnum, xfs_inode_t *ip)
{
	xfs_aghdr_t *agp;
	xfs_btree_cur_t *cur;
	int nlevels;
	xfs_sb_t *sbp;

	if (xfs_btree_curfreelist) {
		cur = xfs_btree_curfreelist;
		xfs_btree_curfreelist = (xfs_btree_cur_t *)cur->bc_tp;
	} else
		cur = (xfs_btree_cur_t *)kmem_zalloc(sizeof(*cur), 0);
	cur->bc_tp = tp;
	cur->bc_mp = mp;
	cur->bc_agbuf = agbuf;
	cur->bc_agno = agno;
	agp = xfs_buf_to_agp(agbuf);
	switch (btnum) {
	case XFS_BTNUM_BNO:
	case XFS_BTNUM_CNT:
		nlevels = agp->ag_levels[btnum];
		break;
	case XFS_BTNUM_IBT:
		nlevels = agp->ag_ilevels;
		break;
	case XFS_BTNUM_BMAP:
		nlevels = ip->i_broot->bb_level + 1;
		break;
	}
	cur->bc_nlevels = nlevels;
	cur->bc_btnum = btnum;
	sbp = &mp->m_sb;
	cur->bc_blocklog = sbp->sb_blocklog;
	switch (btnum) {
	case XFS_BTNUM_BMAP:
		cur->bc_private.b.inodesize = sbp->sb_inodesize;
		cur->bc_private.b.ip = ip;
		break;
	default:
		break;
	}
	return cur;
}

/*
 * Check for last block at the level.
 */
int
xfs_btree_islastblock(xfs_btree_cur_t *cur, int level)
{
	xfs_btree_block_t *block;

	block = xfs_btree_get_block(cur, level);
	xfs_btree_check_block(cur, block, level);
	return block->bb_rightsib == NULLAGBLOCK;
}

/*
 * Point to the last record in the current block at the given level.
 */
int
xfs_btree_lastrec(xfs_btree_cur_t *cur, int level)
{
	xfs_btree_block_t *block;

	block = xfs_btree_get_block(cur, level);
	xfs_btree_check_block(cur, block, level);
	if (!block->bb_numrecs)
		return 0;
	cur->bc_ptrs[level] = block->bb_numrecs;
	return 1;
}

/*
 * Return maxrecs for the block.
 */
int
xfs_btree_maxrecs(xfs_btree_cur_t *cur, xfs_btree_block_t *block)
{
	int maxrecs;

	switch (cur->bc_btnum) {
	case XFS_BTNUM_BNO:
	case XFS_BTNUM_CNT:
		maxrecs = XFS_ALLOC_BLOCK_MAXRECS(block->bb_level, cur);
		break;
	case XFS_BTNUM_IBT:
		maxrecs = XFS_IALLOC_BLOCK_MAXRECS(block->bb_level, cur);
		break;
	case XFS_BTNUM_BMAP:
		maxrecs = XFS_BMAP_BLOCK_IMAXRECS(block->bb_level, cur);
		break;
	}
	return maxrecs;
}

void
xfs_btree_setbuf(xfs_btree_cur_t *cur, int lev, buf_t *buf)
{
	buf_t *obuf;

	obuf = cur->bc_bufs[lev];
	if (obuf)
		xfs_trans_brelse(cur->bc_tp, obuf);
	cur->bc_bufs[lev] = buf;
}
