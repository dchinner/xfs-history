#ident	"$Revision: 1.15 $"

/*
 * This file contains common code for the space manager's btree implementations.
 */

#include <sys/param.h>
#include <sys/buf.h>
#include <sys/vnode.h>
#include <sys/uuid.h>
#include <sys/debug.h>
#include <sys/kmem.h>
#ifdef SIM
#include <bstring.h>
#else
#include <sys/systm.h>
#endif
#include "xfs_types.h"
#include "xfs_inum.h"
#include "xfs_log.h"
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
#endif

/*
 * Cursor freelist.
 */
STATIC xfs_btree_cur_t *xfs_btree_curfreelist;

/*
 * Btree magic numbers.
 */
__uint32_t xfs_magics[XFS_BTNUM_MAX] =
{
	XFS_ABTB_MAGIC, XFS_ABTC_MAGIC, XFS_BMAP_MAGIC
};

/*
 * Get a buffer for the block, return it read in.
 */
buf_t *
xfs_btree_read_bufl(xfs_mount_t *mp, xfs_trans_t *tp, xfs_fsblock_t fsbno,
		    uint lock_flag)
{
	daddr_t		d;		/* disk block address */
	xfs_sb_t	*sbp;		/* superblock structure */

	ASSERT(fsbno != NULLFSBLOCK);
	sbp = &mp->m_sb;
	d = xfs_fsb_to_daddr(sbp, fsbno);
	return xfs_trans_read_buf(tp, mp->m_dev, d, mp->m_bsize, lock_flag);
}

/*
 * Get a buffer for the block, return it read in.
 */
buf_t *
xfs_btree_read_bufs(xfs_mount_t *mp, xfs_trans_t *tp, xfs_agnumber_t agno,
		 xfs_agblock_t agbno, uint lock_flag)
{
	daddr_t		d;		/* disk block address */
	xfs_sb_t	*sbp;		/* superblock structure */

	ASSERT(agno != NULLAGNUMBER);
	ASSERT(agbno != NULLAGBLOCK);
	sbp = &mp->m_sb;
	d = xfs_agb_to_daddr(sbp, agno, agbno);
	return xfs_trans_read_buf(tp, mp->m_dev, d, mp->m_bsize, lock_flag);
}

#ifdef XFSDEBUG
/*
 * Debug routine: check that block header is ok.
 */
void
xfs_btree_check_block(xfs_btree_cur_t *cur, xfs_btree_block_t *block, int level)
{
	if (xfs_btree_long_ptrs(cur->bc_btnum))
		xfs_btree_check_lblock(cur, (xfs_btree_lblock_t *)block, level);
	else
		xfs_btree_check_sblock(cur, (xfs_btree_sblock_t *)block, level);
}

/*
 * Debug routine: check that keys are in the right order.
 */
void
xfs_btree_check_key(xfs_btnum_t btnum, void *ak1, void *ak2)
{
	switch (btnum) {
	case XFS_BTNUM_BNO: {
		xfs_alloc_key_t *k1 = ak1, *k2 = ak2;
		ASSERT(k1->ar_startblock < k2->ar_startblock);
		break;
	    }
	case XFS_BTNUM_CNT: {
		xfs_alloc_key_t *k1 = ak1, *k2 = ak2;
		ASSERT(k1->ar_blockcount < k2->ar_blockcount ||
		       (k1->ar_blockcount == k2->ar_blockcount &&
			k1->ar_startblock < k2->ar_startblock));
		break;
	    }
	case XFS_BTNUM_BMAP: {
		xfs_bmbt_key_t *k1 = ak1, *k2 = ak2;
		ASSERT(k1->br_startoff < k2->br_startoff);
		break;
	    }
	}
}

/*
 * Debug routine: check that block header is ok.
 */
void
xfs_btree_check_lblock(xfs_btree_cur_t *cur, xfs_btree_lblock_t *block, int level)
{
	xfs_sb_t *sbp;

	ASSERT(block->bb_magic == xfs_magics[cur->bc_btnum]);
	ASSERT(block->bb_level == level);
	ASSERT(block->bb_numrecs <= xfs_btree_maxrecs(cur, (xfs_btree_block_t *)block));
	sbp = &cur->bc_mp->m_sb;
	ASSERT(block->bb_leftsib == NULLFSBLOCK || 
	       (xfs_fsb_to_agno(sbp, block->bb_leftsib) < sbp->sb_agcount &&
		xfs_fsb_to_agbno(sbp, block->bb_leftsib) < sbp->sb_agblocks));
	ASSERT(block->bb_rightsib == NULLFSBLOCK || 
	       (xfs_fsb_to_agno(sbp, block->bb_rightsib) < sbp->sb_agcount &&
		xfs_fsb_to_agbno(sbp, block->bb_rightsib) < sbp->sb_agblocks));
}

/*
 * Debug routine: check that (long) pointer is ok.
 */
void
xfs_btree_check_lptr(xfs_btree_cur_t *cur, xfs_fsblock_t ptr, int level)
{
	xfs_sb_t *sbp;

	ASSERT(level > 0);
	sbp = &cur->bc_mp->m_sb;
	ASSERT(ptr != NULLFSBLOCK &&
	       xfs_fsb_to_agno(sbp, ptr) < sbp->sb_agcount &&
	       xfs_fsb_to_agbno(sbp, ptr) < sbp->sb_agblocks);
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
	case XFS_BTNUM_BMAP: {
		xfs_bmbt_rec_t *r1 = ar1, *r2 = ar2;
		ASSERT(xfs_bmbt_get_startoff(r1) + xfs_bmbt_get_blockcount(r1) <= xfs_bmbt_get_startoff(r2));
		break;
	    }
	}
}

/*
 * Debug routine: check that block header is ok.
 */
void
xfs_btree_check_sblock(xfs_btree_cur_t *cur, xfs_btree_sblock_t *block, int level)
{
	buf_t *agbuf;
	xfs_agf_t *agf;

	ASSERT(block->bb_magic == xfs_magics[cur->bc_btnum]);
	ASSERT(block->bb_level == level);
	ASSERT(block->bb_numrecs <= xfs_btree_maxrecs(cur, (xfs_btree_block_t *)block));
	agbuf = cur->bc_private.a.agbuf;
	agf = xfs_buf_to_agf(agbuf);
	ASSERT(block->bb_leftsib == NULLAGBLOCK || 
	       block->bb_leftsib < agf->agf_length);
	ASSERT(block->bb_rightsib == NULLAGBLOCK || 
	       block->bb_rightsib < agf->agf_length);
}

/*
 * Debug routine: check that (short) pointer is ok.
 */
void
xfs_btree_check_sptr(xfs_btree_cur_t *cur, xfs_agblock_t ptr, int level)
{
	buf_t *agbuf;
	xfs_agf_t *agf;

	ASSERT(level > 0);
	agbuf = cur->bc_private.a.agbuf;
	agf = xfs_buf_to_agf(agbuf);
	ASSERT(ptr != NULLAGBLOCK && ptr < agf->agf_length);
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
	newcur = xfs_btree_init_cursor(mp, tp, cur->bc_private.a.agbuf,
				       cur->bc_private.a.agno, cur->bc_btnum,
				       cur->bc_private.b.ip);
	newcur->bc_rec = cur->bc_rec;
	for (i = 0; i < newcur->bc_nlevels; i++) {
		newcur->bc_ptrs[i] = cur->bc_ptrs[i];
		if (buf = cur->bc_bufs[i])
			newcur->bc_bufs[i] = xfs_trans_read_buf(tp, mp->m_dev, buf->b_blkno, mp->m_bsize, 0);
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
	if (!block->bb_h.bb_numrecs)
		return 0;
	cur->bc_ptrs[level] = 1;
	return 1;
}

/*
 * Get a buffer for the block, return it with no data read.
 */
buf_t *
xfs_btree_get_bufl(xfs_mount_t *mp, xfs_trans_t *tp, xfs_fsblock_t fsbno,
		   uint lock_flag)
{
	daddr_t d;
	xfs_sb_t *sbp;

	ASSERT(fsbno != NULLFSBLOCK);
	sbp = &mp->m_sb;
	d = xfs_fsb_to_daddr(sbp, fsbno);
	return xfs_trans_get_buf(tp, mp->m_dev, d, mp->m_bsize, lock_flag);
}

/*
 * Get a buffer for the block, return it with no data read.
 */
buf_t *
xfs_btree_get_bufs(xfs_mount_t *mp, xfs_trans_t *tp, xfs_agnumber_t agno,
		  xfs_agblock_t agbno, uint lock_flag)
{
	daddr_t d;
	xfs_sb_t *sbp;

	ASSERT(agno != NULLAGNUMBER);
	ASSERT(agbno != NULLAGBLOCK);
	sbp = &mp->m_sb;
	d = xfs_agb_to_daddr(sbp, agno, agbno);
	return xfs_trans_get_buf(tp, mp->m_dev, d, mp->m_bsize, lock_flag);
}

/*
 * Allocate a new cursor.
 */
xfs_btree_cur_t *
xfs_btree_init_cursor(xfs_mount_t *mp, xfs_trans_t *tp, buf_t *agbuf, xfs_agnumber_t agno, xfs_btnum_t btnum, xfs_inode_t *ip)
{
	xfs_agf_t *agf;
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
	switch (btnum) {
	case XFS_BTNUM_BNO:
	case XFS_BTNUM_CNT:
		agf = xfs_buf_to_agf(agbuf);
		nlevels = agf->agf_levels[btnum];
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
	case XFS_BTNUM_BNO:
	case XFS_BTNUM_CNT:
		cur->bc_private.a.agbuf = agbuf;
		cur->bc_private.a.agno = agno;
		break;
	case XFS_BTNUM_BMAP:
		cur->bc_private.b.inodesize = sbp->sb_inodesize;
		cur->bc_private.b.ip = ip;
		break;
	default:
		ASSERT(0);
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
	if (xfs_btree_long_ptrs(cur->bc_btnum))
		return block->bb_u.l.bb_rightsib == NULLFSBLOCK;
	else
		return block->bb_u.s.bb_rightsib == NULLAGBLOCK;
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
	if (!block->bb_h.bb_numrecs)
		return 0;
	cur->bc_ptrs[level] = block->bb_h.bb_numrecs;
	return 1;
}

#ifdef XFSDEBUG
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
		maxrecs = XFS_ALLOC_BLOCK_MAXRECS(block->bb_h.bb_level, cur);
		break;
	case XFS_BTNUM_BMAP:
		maxrecs = XFS_BMAP_BLOCK_IMAXRECS(block->bb_h.bb_level, cur);
		break;
	}
	return maxrecs;
}
#endif	/* XFSDEBUG */

/*
 * Compute byte offsets for the fields given.
 */
void
xfs_btree_offsets(int fields, const int *offsets, int nbits, int *first, int *last)
{
	int i;
	int imask;

	ASSERT(fields != 0);
	for (i = 0, imask = 1; ; i++, imask <<= 1) {
		if (imask & fields) {
			*first = offsets[i];
			break;
		}
	}
	for (i = nbits - 1, imask = 1 << i; ; i--, imask >>= 1) {
		if (imask & fields) {
			*last = offsets[i + 1] - 1;
			break;
		}
	}
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
