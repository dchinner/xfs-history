#ident	"$Revision: 1.5 $"

#include <sys/param.h>
#include <sys/buf.h>
#include <sys/vnode.h>
#include <sys/uuid.h>
#include <sys/debug.h>
#include <stddef.h>
#ifdef SIM
#include <bstring.h>
#else
#include <sys/kmem.h>
#include <sys/systm.h>
#endif
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
#endif

STATIC xfs_btree_cur_t *xfs_btree_curfreelist;

__uint32_t xfs_magics[XFS_BTNUM_MAX] =
{
	XFS_ABTB_MAGIC, XFS_ABTC_MAGIC, XFS_BMAP_MAGIC
};

/*
 * Get a buffer for the block, return it.
 */
buf_t *
xfs_btree_bread(xfs_mount_t *mp, xfs_trans_t *tp, xfs_agnumber_t agno, xfs_agblock_t agbno)
{
	daddr_t d;
	xfs_sb_t *sbp;

	ASSERT(agno != NULLAGNUMBER);
	ASSERT(agbno != NULLAGBLOCK);
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
	case XFS_BTNUM_BMAP: {
		xfs_bmbt_rec_t *r1 = ar1, *r2 = ar2;
		ASSERT(xfs_bmbt_get_startoff(r1) + xfs_bmbt_get_blockcount(r1) <= xfs_bmbt_get_startoff(r2));
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

void
xfs_btree_log_ag(xfs_trans_t *tp, buf_t *buf, int fields)
{
	int first;
	int last;
	static const int offsets[] = {
		offsetof(xfs_aghdr_t, ag_magic),
		offsetof(xfs_aghdr_t, ag_version),
		offsetof(xfs_aghdr_t, ag_seqno),
		offsetof(xfs_aghdr_t, ag_length),
		offsetof(xfs_aghdr_t, ag_roots[0]),
		offsetof(xfs_aghdr_t, ag_freelist),
		offsetof(xfs_aghdr_t, ag_levels[0]),
		offsetof(xfs_aghdr_t, ag_flist_count),
		offsetof(xfs_aghdr_t, ag_freeblks),
		offsetof(xfs_aghdr_t, ag_longest),
		offsetof(xfs_aghdr_t, ag_icount),
		offsetof(xfs_aghdr_t, ag_ifirst),
		offsetof(xfs_aghdr_t, ag_ilast),
		offsetof(xfs_aghdr_t, ag_iflist),
		offsetof(xfs_aghdr_t, ag_ifcount),
		sizeof(xfs_aghdr_t)
	};

	xfs_btree_offsets(fields, offsets, XFS_AG_NUM_BITS, &first, &last);
	xfs_trans_log_buf(tp, buf, first, last);
}

/*
 * Log btree blocks (headers)
 */
void
xfs_btree_log_block(xfs_trans_t *tp, buf_t *buf, int fields)
{
	int first;
	int last;
	static const int offsets[] = {
		offsetof(xfs_btree_block_t, bb_magic),
		offsetof(xfs_btree_block_t, bb_level),
		offsetof(xfs_btree_block_t, bb_numrecs),
		offsetof(xfs_btree_block_t, bb_leftsib),
		offsetof(xfs_btree_block_t, bb_rightsib),
		sizeof(xfs_btree_block_t)
	};

	xfs_btree_offsets(fields, offsets, XFS_BB_NUM_BITS, &first, &last);
	xfs_trans_log_buf(tp, buf, first, last);
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
	case XFS_BTNUM_BMAP:
		maxrecs = XFS_BMAP_BLOCK_IMAXRECS(block->bb_level, cur);
		break;
	}
	return maxrecs;
}

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
