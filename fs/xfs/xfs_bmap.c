#ident	"$Revision: 1.41 $"

#include <sys/param.h>
#include <sys/buf.h>
#include <sys/vnode.h>
#include <sys/uuid.h>
#include <sys/debug.h>
#include <sys/kmem.h>
#include <stddef.h>
#ifdef SIM
#include <bstring.h>
#else
#include <sys/systm.h>
#endif
#include "xfs_types.h"
#include "xfs_inum.h"
#include <sys/grio.h>
#include "xfs_log.h"
#include "xfs_trans.h"
#include "xfs_sb.h"
#include "xfs_ag.h"
#include "xfs_mount.h"
#include "xfs_alloc_btree.h"
#include "xfs_ialloc.h"
#include "xfs_bmap_btree.h"
#include "xfs_btree.h"
#include "xfs_dinode.h"
#include "xfs_inode_item.h"
#include "xfs_inode.h"
#include "xfs_extfree_item.h"
#include "xfs_bmap.h"
#include "xfs_alloc.h"
#include "xfs_rtalloc.h"
#ifdef SIM
#include "sim.h"
#endif

#if !defined(SIM) || !defined(XFSDEBUG)
#define	kmem_check()	/* dummy for memory-allocation checking */
#endif

zone_t *xfs_bmap_free_item_zone;

/*
 * Prototypes for internal bmap routines.
 */

STATIC int				/* inode logging flags */
xfs_bmap_add_extent(
	xfs_inode_t		*ip,	/* incore inode pointer */
	xfs_extnum_t		idx,	/* extent number to update/insert */
	xfs_btree_cur_t		*cur,	/* if null, not a btree */
	xfs_bmbt_irec_t		*new);	/* new data to put in extent list */

STATIC int
xfs_bmap_add_extent_delay_real(
	xfs_inode_t		*ip,	/* incore inode pointer */
	xfs_extnum_t		idx,	/* extent number to update/insert */
	xfs_btree_cur_t		*cur,	/* if null, not a btree */
	xfs_bmbt_irec_t		*new);	/* new data to put in extent list */

STATIC int
xfs_bmap_add_extent_hole_delay(
	xfs_inode_t		*ip,	/* incore inode pointer */
	xfs_extnum_t		idx,	/* extent number to update/insert */
	xfs_btree_cur_t		*cur,	/* if null, not a btree */
	xfs_bmbt_irec_t		*new);	/* new data to put in extent list */

STATIC int
xfs_bmap_add_extent_hole_real(
	xfs_inode_t		*ip,	/* incore inode pointer */
	xfs_extnum_t		idx,	/* extent number to update/insert */
	xfs_btree_cur_t		*cur,	/* if null, not a btree */
	xfs_bmbt_irec_t		*new);	/* new data to put in extent list */

STATIC xfs_fsblock_t
xfs_bmap_alloc(
	xfs_trans_t		*tp,
	xfs_inode_t		*ip,
	int			eof,
	xfs_bmbt_irec_t		*prevp,
	xfs_bmbt_irec_t		*gotp,
	xfs_fsblock_t		*firstblock,
	xfs_extlen_t		*alen,
	xfs_extlen_t		total,
	xfs_fsblock_t		off,
	int			wasdel);

STATIC int
xfs_bmap_btree_to_extents(
	xfs_trans_t		*tp,
	xfs_inode_t		*ip,
	xfs_btree_cur_t		*cur);

#ifdef XFSDEBUG
STATIC void
xfs_bmap_check_extents(
	xfs_inode_t		*ip);
#else
#define	xfs_bmap_check_extents(a)
#endif

STATIC int				/* inode logging flags */
xfs_bmap_del_extent(
	xfs_inode_t		*ip,	/* incore inode pointer */
	xfs_extnum_t		idx,	/* extent number to update/insert */
	xfs_bmap_free_t		*flist,	/* list of extents to be freed */
	xfs_btree_cur_t		*cur,	/* if null, not a btree */
	xfs_bmbt_irec_t		*new);	/* new data to put in extent list */

STATIC void
xfs_bmap_del_free(
	xfs_bmap_free_t		*flist,
	xfs_bmap_free_item_t	*prev,
	xfs_bmap_free_item_t	*free);

STATIC void
xfs_bmap_delete_exlist(
	xfs_inode_t	*ip,
	xfs_extnum_t	idx,
	xfs_extnum_t	count);

STATIC xfs_fsblock_t
xfs_bmap_extents_to_btree(
	xfs_trans_t	*tp,
	xfs_inode_t	*ip,
	xfs_fsblock_t	firstblock,
	xfs_bmap_free_t	*flist);

STATIC void
xfs_bmap_insert_exlist(
	xfs_inode_t	*ip,
	xfs_extnum_t	idx,
	xfs_extnum_t	count,
	xfs_bmbt_irec_t	*new);

STATIC xfs_fsblock_t
xfs_bmap_local_to_extents(
	xfs_trans_t	*tp,
	xfs_inode_t	*ip,
	xfs_fsblock_t	firstblock,
	xfs_extlen_t	total);
 
STATIC xfs_bmbt_rec_t *
xfs_bmap_search_extents(
	xfs_inode_t	*ip,
	xfs_fsblock_t	bno,
	int		*eofp,
	xfs_extnum_t	*lastxp,
	xfs_bmbt_irec_t	*gotp,
	xfs_bmbt_irec_t	*prevp);

/*
 * Bmap internal routines.
 */

/*
 * Called by xfs_bmapi to update extent list structure and the btree
 * after allocating space (or doing a delayed allocation).
 */
STATIC int				/* inode logging flags */
xfs_bmap_add_extent(
	xfs_inode_t		*ip,	/* incore inode pointer */
	xfs_extnum_t		idx,	/* extent number to update/insert */
	xfs_btree_cur_t		*cur,	/* if null, not a btree */
	xfs_bmbt_irec_t		*new)	/* new data to put in extent list */
{
	xfs_extnum_t		nextents;
	xfs_bmbt_irec_t		prev;

	nextents = ip->i_bytes / sizeof(xfs_bmbt_rec_t);
	ASSERT(idx <= nextents);
	/*
	 * This is the first extent added to a new/empty file.
	 * Special case this one, so other routines get to assume there are
	 * already extents in the list.
	 */
	if (nextents == 0) {
		xfs_bmap_insert_exlist(ip, 0, 1, new);
		ASSERT(cur == NULL);
		ip->i_lastex = 0;
		kmem_check();
		if (new->br_startblock != NULLSTARTBLOCK) {
			ip->i_d.di_nextents = 1;
			return XFS_ILOG_CORE | XFS_ILOG_EXT;
		} else
			return 0;
	}
	/*
	 * Any kind of new delayed allocation goes here.
	 */
	if (new->br_startblock == NULLSTARTBLOCK)
		return xfs_bmap_add_extent_hole_delay(ip, idx, cur, new);
	/*
	 * Real allocation off the end of the file.
	 */
	if (idx == nextents)
		return xfs_bmap_add_extent_hole_real(ip, idx, cur, new);
	/*
	 * Get the record referred to by idx.
	 */
	xfs_bmbt_get_all(&ip->i_u1.iu_extents[idx], prev);
	/*
	 * If it's a real allocation record, and the new allocation ends
	 * after the start of the referred to record, then we're filling
	 * in a delayed allocation with a real one.
	 */
	if (new->br_startblock != NULLSTARTBLOCK &&
	    new->br_startoff + new->br_blockcount > prev.br_startoff)
		return xfs_bmap_add_extent_delay_real(ip, idx, cur, new);
	/*
	 * Otherwise we're filling in a hole with an allocation.
	 */
	return xfs_bmap_add_extent_hole_real(ip, idx, cur, new);
}

/*
 * Called by xfs_bmap_add_extent to handle cases converting a delayed
 * allocation to a real allocation.
 */
STATIC int
xfs_bmap_add_extent_delay_real(
	xfs_inode_t		*ip,	/* incore inode pointer */
	xfs_extnum_t		idx,	/* extent number to update/insert */
	xfs_btree_cur_t		*cur,	/* if null, not a btree */
	xfs_bmbt_irec_t		*new)	/* new data to put in extent list */
{
	xfs_bmbt_rec_t		*base;
	int			btree;
	int			end_eq;
	xfs_bmbt_rec_t		*ep;
	int			filling_in_all;
	int			filling_in_first;
	int			filling_in_last;
	int			filling_in_middle;
	xfs_bmbt_irec_t		left;
	int			left_contig;
	int			left_delay;
	xfs_fsblock_t		left_endblock;
	xfs_fsblock_t		left_endoff;
	int			left_valid;
	xfs_fsblock_t		new_endblock;
	xfs_fsblock_t		new_endoff;
	xfs_extnum_t		nextents;
	xfs_bmbt_irec_t		prev;
	xfs_fsblock_t		prev_endoff;
	xfs_bmbt_irec_t		right;
	int			right_contig;
	int			right_delay;
	int			right_valid;
	int			start_eq;

	nextents = ip->i_bytes / sizeof(xfs_bmbt_rec_t);
	btree = cur != NULL;
	/*
	 * Set up a bunch of variables to make the tests simpler.
	 */
	base = ip->i_u1.iu_extents;
	ep = &base[idx];
	xfs_bmbt_get_all(ep, prev);
	prev_endoff = prev.br_startoff + prev.br_blockcount;
	new_endoff = new->br_startoff + new->br_blockcount;
	new_endblock = new->br_startblock + new->br_blockcount;
	/*
	 * Check and set flags if this segment has a left neighbor
	 */
	if (left_valid = ep > base) {
		xfs_bmbt_get_all(ep - 1, left);
		if (!(left_delay = left.br_startblock == NULLSTARTBLOCK))
			left_endblock = left.br_startblock + left.br_blockcount;
		left_endoff = left.br_startoff + left.br_blockcount;
	}
	left_contig = left_valid && !left_delay &&
		      left_endoff == new->br_startoff &&
		      left_endblock == new->br_startblock;
	/*
	 * Check and set flags if this segment has a right neighbor
	 */
	if (right_valid = ep < &base[nextents - 1]) {
		xfs_bmbt_get_all(ep + 1, right);
		right_delay = right.br_startblock == NULLSTARTBLOCK;
	}
	right_contig = right_valid && !right_delay &&
		       new_endoff == right.br_startoff &&
		       new_endblock == right.br_startblock;
	/*
	 * Set flags determining what part of the previous delayed allocation
	 * extent is being replaced by a real allocation.
	 */
	start_eq = prev.br_startoff == new->br_startoff;
	end_eq = prev_endoff == new_endoff;
	filling_in_all = start_eq & end_eq;
	filling_in_first = start_eq & !end_eq;
	filling_in_last = !start_eq & end_eq;
	filling_in_middle = !start_eq & !end_eq;
	/*
	 * Filling in all of a previously delayed allocation extent.
	 * The left and right neighbors are both contiguous with new.
	 */
	if (filling_in_all && left_contig && right_contig) {
		xfs_bmbt_set_blockcount(ep - 1,
			left.br_blockcount + prev.br_blockcount +
			right.br_blockcount);
		xfs_bmap_delete_exlist(ip, idx, 2);
		ip->i_lastex = idx - 1;
		ip->i_d.di_nextents--;
		kmem_check();
		if (!btree)
			return XFS_ILOG_CORE | XFS_ILOG_EXT;
		xfs_bmbt_lookup_eq(cur, right.br_startoff, right.br_startblock,
			right.br_blockcount);
		xfs_bmbt_delete(cur);
		xfs_bmbt_decrement(cur, 0);
		xfs_bmbt_update(cur, left.br_startoff, left.br_startblock,
			left.br_blockcount + prev.br_blockcount +
			right.br_blockcount);
		kmem_check();
		return XFS_ILOG_CORE;
	}
	/*
	 * Filling in all of a previously delayed allocation extent.
	 * The left neighbor is contiguous, the right is not.
	 */
	if (filling_in_all && left_contig && !right_contig) {
		xfs_bmbt_set_blockcount(ep - 1,
			left.br_blockcount + prev.br_blockcount);
		ip->i_lastex = idx - 1;
		xfs_bmap_delete_exlist(ip, idx, 1);
		kmem_check();
		if (!btree)
			return XFS_ILOG_EXT;
		xfs_bmbt_lookup_eq(cur, left.br_startoff, left.br_startblock,
			left.br_blockcount);
		xfs_bmbt_update(cur, left.br_startoff, left.br_startblock,
			left.br_blockcount + prev.br_blockcount);
		kmem_check();
		return 0;
	}
	/*
	 * Filling in all of a previously delayed allocation extent.
	 * The right neighbor is contiguous, the left is not.
	 */
	if (filling_in_all && !left_contig && right_contig) {
		xfs_bmbt_set_startblock(ep, new->br_startblock);
		xfs_bmbt_set_blockcount(ep,
			prev.br_blockcount + right.br_blockcount);
		ip->i_lastex = idx;
		xfs_bmap_delete_exlist(ip, idx + 1, 1);
		kmem_check();
		if (!btree)
			return XFS_ILOG_EXT;
		xfs_bmbt_lookup_eq(cur, right.br_startoff, right.br_startblock,
			right.br_blockcount);
		xfs_bmbt_update(cur, prev.br_startoff, new->br_startblock,
			prev.br_blockcount + right.br_blockcount);
		kmem_check();
		return 0;
	}
	/*
	 * Filling in all of a previously delayed allocation extent.
	 * Neither the left nor right neighbors are contiguous with the new one.
	 */
	if (filling_in_all && !left_contig && !right_contig) {
		xfs_bmbt_set_startblock(ep, new->br_startblock);
		ip->i_lastex = idx;
		ip->i_d.di_nextents++;
		kmem_check();
		if (!btree)
			return XFS_ILOG_CORE | XFS_ILOG_EXT;
		xfs_bmbt_lookup_eq(cur, new->br_startoff, new->br_startblock,
			new->br_blockcount);
		xfs_bmbt_insert(cur);
		kmem_check();
		return XFS_ILOG_CORE;
	}
	/*
	 * Filling in the first part of a previous delayed allocation.
	 * The left neighbor is contiguous.
	 */
	if (filling_in_first && left_contig) {
		xfs_bmbt_set_blockcount(ep - 1,
			left.br_blockcount + new->br_blockcount);
		xfs_bmbt_set_startoff(ep,
			prev.br_startoff + new->br_blockcount);
		xfs_bmbt_set_blockcount(ep,
			prev.br_blockcount - new->br_blockcount);
		ip->i_lastex = idx;
		kmem_check();
		if (!btree)
			return XFS_ILOG_EXT;
		xfs_bmbt_lookup_eq(cur, left.br_startoff, left.br_startblock,
			left.br_blockcount);
		xfs_bmbt_update(cur, left.br_startoff, left.br_startblock,
			left.br_blockcount + new->br_blockcount);
		kmem_check();
		return 0;
	}
	/*
	 * Filling in the first part of a previous delayed allocation.
	 * The left neighbor is not contiguous.
	 */
	if (filling_in_first && !left_contig) {
		xfs_bmbt_set_startoff(ep, new_endoff);
		xfs_bmbt_set_blockcount(ep,
			prev.br_blockcount - new->br_blockcount);
		xfs_bmap_insert_exlist(ip, idx, 1, new);
		ip->i_lastex = idx;
		ip->i_d.di_nextents++;
		kmem_check();
		if (!btree)
			return XFS_ILOG_CORE | XFS_ILOG_EXT;
		xfs_bmbt_lookup_eq(cur, new->br_startoff, new->br_startblock,
			new->br_blockcount);
		xfs_bmbt_insert(cur);
		kmem_check();
		return XFS_ILOG_CORE;
	}
	/*
	 * Filling in the last part of a previous delayed allocation.
	 * The right neighbor is contiguous with the new allocation.
	 */
	if (filling_in_last && right_contig) {
		xfs_bmbt_set_blockcount(ep,
			prev.br_blockcount - new->br_blockcount);
		xfs_bmbt_set_startoff(ep + 1, new->br_startoff);
		xfs_bmbt_set_startblock(ep + 1, new->br_startblock);
		xfs_bmbt_set_blockcount(ep + 1,
			new->br_blockcount + right.br_blockcount);
		ip->i_lastex = idx + 1;
		kmem_check();
		if (!btree)
			return XFS_ILOG_EXT;
		xfs_bmbt_lookup_eq(cur, right.br_startoff, right.br_startblock,
			right.br_blockcount);
		xfs_bmbt_update(cur, new->br_startoff, new->br_startblock,
			new->br_blockcount + right.br_blockcount);
		kmem_check();
		return 0;
	}
	/*
	 * Filling in the last part of a previous delayed allocation.
	 * The right neighbor is not contiguous.
	 */
	if (filling_in_last && !right_contig) {
		xfs_bmbt_set_blockcount(ep,
			prev.br_blockcount - new->br_blockcount);
		xfs_bmap_insert_exlist(ip, idx + 1, 1, new);
		ip->i_lastex = idx + 1;
		ip->i_d.di_nextents++;
		kmem_check();
		if (!btree)
			return XFS_ILOG_CORE | XFS_ILOG_EXT;
		xfs_bmbt_lookup_eq(cur, new->br_startoff, new->br_startblock, new->br_blockcount);
		xfs_bmbt_insert(cur);
		kmem_check();
		return XFS_ILOG_CORE;
	}
	/*
	 * Filling in the middle part of a previous delayed allocation.
	 * Contiguity is impossible here.
	 */
	if (filling_in_middle) {
		xfs_bmbt_irec_t temp[2];

		xfs_bmbt_set_blockcount(ep,
			new->br_startoff - prev.br_startoff);
		temp[0] = *new;
		temp[1].br_startoff = new_endoff;
		temp[1].br_startblock = NULLSTARTBLOCK;
		temp[1].br_blockcount = prev_endoff - new_endoff;
		xfs_bmap_insert_exlist(ip, idx + 1, 2, temp);
		ip->i_lastex = idx + 1;
		ip->i_d.di_nextents++;
		kmem_check();
		if (!btree)
			return XFS_ILOG_CORE | XFS_ILOG_EXT;
		xfs_bmbt_lookup_eq(cur, new->br_startoff, new->br_startblock,
			new->br_blockcount);
		xfs_bmbt_insert(cur);
		kmem_check();
		return XFS_ILOG_CORE;
	}
	ASSERT(0);
}

/*
 * Called by xfs_bmap_add_extent to handle cases converting a hole
 * to a delayed allocation.
 */
STATIC int
xfs_bmap_add_extent_hole_delay(
	xfs_inode_t		*ip,	/* incore inode pointer */
	xfs_extnum_t		idx,	/* extent number to update/insert */
	xfs_btree_cur_t		*cur,	/* if null, not a btree */
	xfs_bmbt_irec_t		*new)	/* new data to put in extent list */
{
	xfs_bmbt_rec_t		*base;
	xfs_bmbt_rec_t		*ep;
	xfs_bmbt_irec_t		left;
	int			left_contig;
	int			left_delay;
	xfs_fsblock_t		left_endoff;
	int			left_valid;
	xfs_fsblock_t		new_endoff;
	xfs_extnum_t		nextents;
	xfs_bmbt_irec_t		right;
	int			right_contig;
	int			right_delay;
	int			right_valid;

	nextents = ip->i_bytes / sizeof(xfs_bmbt_rec_t);
	ASSERT(idx <= nextents);
	base = ip->i_u1.iu_extents;
	new_endoff = new->br_startoff + new->br_blockcount;
	/*
	 * Deal with the end-of-file cases first.
	 */
	if (idx == nextents) {
		ep = &base[idx - 1];
		xfs_bmbt_get_all(ep, left);
		left_endoff = left.br_startoff + left.br_blockcount;
		left_delay = left.br_startblock == NULLSTARTBLOCK;
		/*
		 * At end of file, contiguous to old delayed allocation
		 */
		if (left_delay && left_endoff == new->br_startoff) {
			xfs_bmbt_set_blockcount(ep,
				left.br_blockcount + new->br_blockcount);
			ip->i_lastex = idx - 1;
			kmem_check();
			return 0;
		}
		/*
		 * At end of file, not contiguous to an old delayed allocation
		 */
		xfs_bmap_insert_exlist(ip, idx, 1, new);
		ip->i_lastex = idx;
		kmem_check();
		return 0;
	}
	ep = &base[idx];
	xfs_bmbt_get_all(ep, right);
	right_delay = right.br_startblock == NULLSTARTBLOCK;
	/*
	 * Check and set flags if this segment has a left neighbor
	 */
	if (left_valid = ep > base) {
		xfs_bmbt_get_all(ep - 1, left);
		left_delay = left.br_startblock == NULLSTARTBLOCK;
		left_endoff = left.br_startoff + left.br_blockcount;
	}
	/*
	 * We're inserting a delayed allocation between "left" and "right".
	 */
	left_contig = left_valid && left_delay &&
		      left_endoff == new->br_startoff;
	right_contig = right_delay && new_endoff == right.br_startoff;
	/*
	 * New allocation is contiguous with delayed allocations on the
	 * left and on the right.
	 * Merge all three into a single extent list entry.
	 */
	if (left_contig && right_contig) {
		xfs_bmbt_set_blockcount(ep - 1,
			left.br_blockcount + new->br_blockcount +
			right.br_blockcount);
		xfs_bmap_delete_exlist(ip, idx, 1);
		ip->i_lastex = idx - 1;
		kmem_check();
		return 0;
	}
	/*
	 * New allocation is contiguous with a delayed allocation on the left.
	 * Merge the new allocation with the left neighbor.
	 */
	if (left_contig && !right_contig) {
		xfs_bmbt_set_blockcount(ep - 1,
			left.br_blockcount + new->br_blockcount);
		ip->i_lastex = idx - 1;
		kmem_check();
		return 0;
	}
	/*
	 * New allocation is contiguous with a delayed allocation on the right.
	 * Merge the new allocation with the right neighbor.
	 */
	if (!left_contig && right_contig) {
		xfs_bmbt_set_startoff(ep, new->br_startoff);
		xfs_bmbt_set_blockcount(ep,
			new->br_blockcount + right.br_blockcount);
		ip->i_lastex = idx;
		kmem_check();
		return 0;
	}
	/*
	 * New allocation is not contiguous with another delayed allocation.
	 * Insert a new entry.
	 */
	if (!left_contig && !right_contig) {
		xfs_bmap_insert_exlist(ip, idx, 1, new);
		ip->i_lastex = idx;
		kmem_check();
		return 0;
	}
	ASSERT(0);
}

/*
 * Called by xfs_bmap_add_extent to handle cases converting a hole
 * to a real allocation.
 */
STATIC int
xfs_bmap_add_extent_hole_real(
	xfs_inode_t		*ip,	/* incore inode pointer */
	xfs_extnum_t		idx,	/* extent number to update/insert */
	xfs_btree_cur_t		*cur,	/* if null, not a btree */
	xfs_bmbt_irec_t		*new)	/* new data to put in extent list */
{
	xfs_bmbt_rec_t		*base;
	int			btree;
	xfs_bmbt_rec_t		*ep;
	xfs_bmbt_irec_t		left;
	int			left_contig;
	int			left_delay;
	xfs_fsblock_t		left_endblock;
	xfs_fsblock_t		left_endoff;
	int			left_valid;
	xfs_fsblock_t		new_endblock;
	xfs_fsblock_t		new_endoff;
	xfs_extnum_t		nextents;
	xfs_bmbt_irec_t		right;
	int			right_contig;
	int			right_delay;
	int			right_valid;

	nextents = ip->i_bytes / sizeof(xfs_bmbt_rec_t);
	ASSERT(idx <= nextents);
	base = ip->i_u1.iu_extents;
	btree = cur != NULL;
	new_endoff = new->br_startoff + new->br_blockcount;
	new_endblock = new->br_startblock + new->br_blockcount;
	/*
	 * Deal with the end-of-file cases first.
	 */
	if (idx == nextents) {
		ep = &base[idx - 1];
		xfs_bmbt_get_all(ep, left);
		left_endoff = left.br_startoff + left.br_blockcount;
		if (!(left_delay = left.br_startblock == NULLSTARTBLOCK))
			left_endblock = left.br_startblock + left.br_blockcount;
		/*
		 * At end of file, contiguous to old real allocation
		 */
		if (!left_delay && left_endoff == new->br_startoff &&
		    left_endblock == new->br_startblock) {
			xfs_bmbt_set_blockcount(ep,
				left.br_blockcount + new->br_blockcount);
			ip->i_lastex = idx - 1;
			kmem_check();
			if (!btree)
				return XFS_ILOG_EXT;
			xfs_bmbt_lookup_eq(cur, left.br_startoff,
				left.br_startblock, left.br_blockcount);
			xfs_bmbt_update(cur, left.br_startoff,
				left.br_startblock,
				left.br_blockcount + new->br_blockcount);
			kmem_check();
			return 0;
		}
		/*
		 * At end of file, not contiguous to an old real allocation
		 */
		xfs_bmap_insert_exlist(ip, idx, 1, new);
		ip->i_lastex = idx;
		ip->i_d.di_nextents++;
		kmem_check();
		if (!btree)
			return XFS_ILOG_CORE | XFS_ILOG_EXT;
		xfs_bmbt_lookup_eq(cur, new->br_startoff, new->br_startblock,
			new->br_blockcount);
		xfs_bmbt_insert(cur);
		kmem_check();
		return XFS_ILOG_CORE;
	}
	ep = &base[idx];
	xfs_bmbt_get_all(ep, right);
	right_delay = right.br_startblock == NULLSTARTBLOCK;
	/*
	 * Check and set flags if this segment has a left neighbor
	 */
	if (left_valid = ep > base) {
		xfs_bmbt_get_all(ep - 1, left);
		if (!(left_delay = left.br_startblock == NULLSTARTBLOCK))
			left_endblock = left.br_startblock + left.br_blockcount;
		left_endoff = left.br_startoff + left.br_blockcount;
	}
	/*
	 * We're inserting a real allocation between "left" and "right".
	 */
	left_contig = left_valid && !left_delay &&
		      left_endoff == new->br_startoff &&
		      left_endblock == new->br_startblock;
	right_contig = !right_delay && new_endoff == right.br_startoff &&
		       new_endblock == right.br_startblock;
	/*
	 * New allocation is contiguous with real allocations on the
	 * left and on the right.
	 * Merge all three into a single extent list entry.
	 */
	if (left_contig && right_contig) {
		xfs_bmbt_set_blockcount(ep - 1,
			left.br_blockcount + new->br_blockcount +
			right.br_blockcount);
		xfs_bmap_delete_exlist(ip, idx, 1);
		ip->i_lastex = idx - 1;
		ip->i_d.di_nextents--;
		kmem_check();
		if (!btree)
			return XFS_ILOG_CORE | XFS_ILOG_EXT;
		xfs_bmbt_lookup_eq(cur, right.br_startoff, right.br_startblock,
			right.br_blockcount);
		xfs_bmbt_delete(cur);
		xfs_bmbt_decrement(cur, 0);
		xfs_bmbt_update(cur, left.br_startoff, left.br_startblock, left.br_blockcount + new->br_blockcount + right.br_blockcount);
		kmem_check();
		return XFS_ILOG_CORE;
	}
	/*
	 * New allocation is contiguous with a real allocation on the left.
	 * Merge the new allocation with the left neighbor.
	 */
	if (left_contig && !right_contig) {
		xfs_bmbt_set_blockcount(ep - 1, left.br_blockcount + new->br_blockcount);
		ip->i_lastex = idx - 1;
		kmem_check();
		if (!btree)
			return XFS_ILOG_EXT;
		xfs_bmbt_lookup_eq(cur, left.br_startoff, left.br_startblock,
			left.br_blockcount);
		xfs_bmbt_update(cur, left.br_startoff, left.br_startblock,
			left.br_blockcount + new->br_blockcount);
		kmem_check();
		return 0;
	}
	/*
	 * New allocation is contiguous with a real allocation on the right.
	 * Merge the new allocation with the right neighbor.
	 */
	if (!left_contig && right_contig) {
		xfs_bmbt_set_startoff(ep, new->br_startoff);
		xfs_bmbt_set_startblock(ep, new->br_startblock);
		xfs_bmbt_set_blockcount(ep,
			new->br_blockcount + right.br_blockcount);
		ip->i_lastex = idx;
		kmem_check();
		if (!btree)
			return XFS_ILOG_EXT;
		xfs_bmbt_lookup_eq(cur, right.br_startoff, right.br_startblock,
			right.br_blockcount);
		xfs_bmbt_update(cur, new->br_startoff, new->br_startblock,
			new->br_blockcount + right.br_blockcount);
		kmem_check();
		return 0;
	}
	/*
	 * New allocation is not contiguous with another real allocation.
	 * Insert a new entry.
	 */
	if (!left_contig && !right_contig) {
		xfs_bmap_insert_exlist(ip, idx, 1, new);
		ip->i_lastex = idx;
		ip->i_d.di_nextents++;
		kmem_check();
		if (!btree)
			return XFS_ILOG_CORE | XFS_ILOG_EXT;
		xfs_bmbt_lookup_eq(cur, new->br_startoff, new->br_startblock,
			new->br_blockcount);
		xfs_bmbt_insert(cur);
		kmem_check();
		return XFS_ILOG_CORE;
	}
}

STATIC xfs_fsblock_t
xfs_bmap_alloc(
	xfs_trans_t	*tp,
	xfs_inode_t	*ip,
	int		eof,
	xfs_bmbt_irec_t	*prevp,
	xfs_bmbt_irec_t	*gotp,
	xfs_fsblock_t	*firstblock,
	xfs_extlen_t	*alen,
	xfs_extlen_t	total,
	xfs_fsblock_t	off,
	int		wasdel)
{
	xfs_fsblock_t	abno;
	xfs_fsblock_t	askbno;
	xfs_extlen_t	asklen;
	xfs_fsblock_t	bno;
	xfs_agnumber_t	fb_agno;
	xfs_fsblock_t	firstb;
	xfs_mount_t	*mp;
	int		nullfb;
	xfs_extlen_t	prod;
	int		rt;
	xfs_sb_t	*sbp;
	xfs_alloctype_t	type;

	asklen = *alen;
	firstb = *firstblock;
	mp = ip->i_mount;
	sbp = &mp->m_sb;
	nullfb = firstb == NULLFSBLOCK;
	rt = ip->i_d.di_flags & XFS_DIFLAG_REALTIME;
	fb_agno = nullfb ? NULLAGNUMBER : xfs_fsb_to_agno(sbp, firstb);
	bno = rt ? 0 : (nullfb ? xfs_ino_to_fsb(sbp, ip->i_ino) : firstb);
	if (eof && prevp->br_startoff != NULLFSBLOCK &&
	    prevp->br_startblock != NULLSTARTBLOCK) {
		bno = prevp->br_startblock + prevp->br_blockcount;
		bno += off - (prevp->br_startoff + prevp->br_blockcount);
		if ((!rt &&
		     (xfs_fsb_to_agno(sbp, bno) !=
		      xfs_fsb_to_agno(sbp, prevp->br_startblock) ||
		      xfs_fsb_to_agbno(sbp, bno) >= sbp->sb_agblocks)) ||
		    (rt && bno >= sbp->sb_rextents))
			bno = prevp->br_startblock + prevp->br_blockcount;
	} else if (!eof) {
		xfs_fsblock_t	gotbno;
		xfs_fsblock_t	gotdiff;
		xfs_fsblock_t	prevbno;
		xfs_fsblock_t	prevdiff;

		if (prevp->br_startoff != NULLFSBLOCK &&
		    prevp->br_startblock != NULLSTARTBLOCK) {
			prevdiff = off - (prevp->br_startoff + prevp->br_blockcount);
			prevbno = prevp->br_startblock + prevp->br_blockcount + prevdiff;
			if (prevdiff > 2 * asklen ||
			    (!rt &&
			     (xfs_fsb_to_agno(sbp, prevbno) !=
			      xfs_fsb_to_agno(sbp, prevp->br_startblock) ||
			      xfs_fsb_to_agbno(sbp, prevbno) >=
			      sbp->sb_agblocks)) ||
			    (rt && prevbno >= sbp->sb_rextents))
				prevbno = prevp->br_startblock + prevp->br_blockcount;
			if (!rt && !nullfb &&
			    xfs_fsb_to_agno(sbp, prevbno) != fb_agno)
				prevbno = NULLFSBLOCK;
		} else
			prevbno = NULLFSBLOCK;
		if (gotp->br_startblock != NULLSTARTBLOCK) {
			gotdiff = gotp->br_startoff - off;
			gotbno = gotp->br_startblock - gotdiff;
			if (gotdiff > 2 * asklen ||
			    (!rt &&
			     (xfs_fsb_to_agno(sbp, gotbno) !=
			      xfs_fsb_to_agno(sbp, gotp->br_startblock))) ||
			    (rt && gotbno >= sbp->sb_rextents))
				gotbno = gotp->br_startblock - asklen;
			if (!rt && !nullfb &&
			    xfs_fsb_to_agno(sbp, gotbno) != fb_agno)
				gotbno = NULLFSBLOCK;
		} else
			gotbno = NULLFSBLOCK;
		if (prevbno != NULLFSBLOCK && gotbno != NULLFSBLOCK)
			bno = prevdiff <= gotdiff ? prevbno : gotbno;
		else if (prevbno != NULLFSBLOCK)
			bno = prevbno;
		else if (gotbno != NULLFSBLOCK)
			bno = gotbno;
	}
	if (nullfb || rt || xfs_fsb_to_agno(sbp, bno) == fb_agno)
		askbno = bno;
	else
		askbno = firstb;
	if (rt) {
		type = askbno == 0 ?
			XFS_ALLOCTYPE_ANY_AG : XFS_ALLOCTYPE_NEAR_BNO;
		if (ip->i_d.di_extsize)
			prod = ip->i_d.di_extsize / sbp->sb_rextsize;
		else
			prod = 1;
		abno = xfs_rtallocate_extent(tp, askbno, 1, asklen, alen, type,
			wasdel, prod);
	} else {
		xfs_extlen_t	mod;

		type = nullfb ?
			XFS_ALLOCTYPE_START_BNO : XFS_ALLOCTYPE_NEAR_BNO;
		if (ip->i_d.di_extsize) {
			prod = ip->i_d.di_extsize;
			mod = 0;
		} else if (sbp->sb_blocksize >= NBPP) {
			prod = 1;
			mod = 0;
		} else {
			prod = NBPP >> sbp->sb_blocklog;
			if (mod = off % prod)
				mod = prod - mod;
		}
		abno = xfs_alloc_vextent(tp, askbno, 1, asklen, alen, type,
			total, wasdel, mod, prod);
		if (nullfb)
			*firstblock = abno;
	}
	/* for debugging */ ASSERT(abno != NULLFSBLOCK);
	kmem_check();
	return abno;
}

STATIC int
xfs_bmap_btree_to_extents(
	xfs_trans_t		*tp,
	xfs_inode_t		*ip,
	xfs_btree_cur_t		*cur)
{
	/* return logflags */
	return 0;
}

#ifdef XFSDEBUG
STATIC void
xfs_bmap_check_extents(
	xfs_inode_t		*ip)
{
	xfs_bmbt_rec_t		*base;
	xfs_extnum_t		nextents;
	xfs_bmbt_rec_t		*rp;

	ASSERT(ip->i_flags & XFS_IEXTENTS);
	base = ip->i_u1.iu_extents;
	nextents = ip->i_bytes / sizeof(xfs_bmbt_rec_t);
	for (rp = base; rp < &base[nextents - 1]; rp++)
		xfs_btree_check_rec(XFS_BTNUM_BMAP, (void *)rp, (void *)(rp + 1));
}
#endif

/*
 * Called by xfs_bmapi to update extent list structure and the btree
 * after removing space (or undoing a delayed allocation).
 */
STATIC int				/* inode logging flags */
xfs_bmap_del_extent(
	xfs_inode_t		*ip,	/* incore inode pointer */
	xfs_extnum_t		idx,	/* extent number to update/delete */
	xfs_bmap_free_t		*flist,	/* list of extents to be freed */
	xfs_btree_cur_t		*cur,	/* if null, not a btree */
	xfs_bmbt_irec_t		*del)	/* data to remove from extent list */
{
	xfs_fsblock_t		del_endblock;
	xfs_fsblock_t		del_endoff;
	int			delay;
	xfs_bmbt_rec_t		*ep;
	xfs_bmbt_irec_t		got;
	xfs_fsblock_t		got_endoff;
	xfs_extnum_t		nextents;

	nextents = ip->i_bytes / sizeof(xfs_bmbt_rec_t);
	ASSERT(idx >= 0 && idx < nextents);
	ep = &ip->i_u1.iu_extents[idx];
	xfs_bmbt_get_all(ep, got);
	del_endoff = del->br_startoff + del->br_blockcount;
	got_endoff = got.br_startoff + got.br_blockcount;
	ASSERT((del->br_startblock == NULLSTARTBLOCK) ==
	       (got.br_startblock == NULLSTARTBLOCK));
	delay = got.br_startblock == NULLSTARTBLOCK;
	if (!delay) {
		if (ip->i_d.di_flags & XFS_DIFLAG_REALTIME)
			xfs_rtfree_extent(ip->i_transp, del->br_startblock, del->br_blockcount);
		else
			xfs_bmap_add_free(del->br_startblock, del->br_blockcount, flist);
		del_endblock = del->br_startblock + del->br_blockcount;
		if (cur)
			xfs_bmbt_lookup_eq(cur, got.br_startoff, got.br_startblock, got.br_blockcount);
	}
	/*
	 * Matches the whole extent.  Delete the entry.
	 */
	if (got.br_startoff == del->br_startoff && got_endoff == del_endoff) {
		xfs_bmap_delete_exlist(ip, idx, 1);
		kmem_check();
		if (delay)
			return 0;
		ip->i_d.di_nextents--;
		if (!cur)
			return XFS_ILOG_CORE | XFS_ILOG_EXT;
		xfs_bmbt_delete(cur);
		kmem_check();
		return XFS_ILOG_CORE;
	/*
	 * Deleting the first part of the extent.
	 */
	} else if (got.br_startoff == del->br_startoff &&
		   got_endoff > del_endoff) {
		xfs_bmbt_set_startoff(ep, del_endoff);
		xfs_bmbt_set_blockcount(ep, got.br_blockcount - del->br_blockcount);
		kmem_check();
		if (delay)
			return 0;
		xfs_bmbt_set_startblock(ep, del_endblock);
		if (!cur)
			return XFS_ILOG_EXT;
		xfs_bmbt_update(cur, del_endoff, del_endblock, got.br_blockcount - del->br_blockcount);
		kmem_check();
		return 0;
	/*
	 * Deleting the last part of the extent.
	 */
	} else if (got.br_startoff < del->br_startoff &&
		   got_endoff == del_endoff) {
		xfs_bmbt_set_blockcount(ep, got.br_blockcount - del->br_blockcount);
		kmem_check();
		if (delay)
			return 0;
		if (!cur)
			return XFS_ILOG_EXT;
		xfs_bmbt_update(cur, got.br_startoff, got.br_startblock, got.br_blockcount - del->br_blockcount);
		kmem_check();
		return 0;
	} else if (got.br_startoff < del->br_startoff &&
		   got_endoff > del_endoff) {
		xfs_bmbt_irec_t new;

		xfs_bmbt_set_blockcount(ep, del->br_startoff - got.br_startoff);
		new.br_startoff = del_endoff;
		if (delay)
			new.br_startblock = NULLSTARTBLOCK;
		else
			new.br_startblock = del_endblock;
		new.br_blockcount = got_endoff - new.br_startoff;
		xfs_bmap_insert_exlist(ip, idx + 1, 1, &new);
		kmem_check();
		if (delay)
			return 0;
		ip->i_d.di_nextents++;
		if (!cur)
			return XFS_ILOG_CORE | XFS_ILOG_EXT;
		xfs_bmbt_update(cur, got.br_startoff, got.br_startblock, del->br_startoff - got.br_startoff);
		xfs_bmbt_increment(cur, 0);
		cur->bc_rec.b = new;
		xfs_bmbt_insert(cur);
		kmem_check();
		return XFS_ILOG_CORE;
	} else
		ASSERT(0);
}

STATIC void
xfs_bmap_del_free(
	xfs_bmap_free_t		*flist,
	xfs_bmap_free_item_t	*prev,
	xfs_bmap_free_item_t	*free)
{
	if (prev)
		prev->xbfi_next = free->xbfi_next;
	else
		flist->xbf_first = free->xbfi_next;
	flist->xbf_count--;
	kmem_zone_free(xfs_bmap_free_item_zone, free);
	kmem_check();
}

STATIC void
xfs_bmap_delete_exlist(
	xfs_inode_t	*ip,
	xfs_extnum_t	idx,
	xfs_extnum_t	count)
{
	xfs_bmbt_rec_t	*ep;
	xfs_extnum_t	from;
	xfs_extnum_t	nextents;
	xfs_extnum_t	to;

	ASSERT(ip->i_flags & XFS_IEXTENTS);
	ep = ip->i_u1.iu_extents;
	nextents = ip->i_bytes / sizeof(xfs_bmbt_rec_t) - count;
	for (to = idx, from = to + count; to < nextents; from++, to++)
		ep[to] = ep[from];
	xfs_iext_realloc(ip, -count);
	kmem_check();
}

STATIC xfs_fsblock_t
xfs_bmap_extents_to_btree(
	xfs_trans_t		*tp,
	xfs_inode_t		*ip,
	xfs_fsblock_t		firstblock,
	xfs_bmap_free_t		*flist)
{
	xfs_btree_lblock_t	*ablock;
	xfs_fsblock_t		abno;
	buf_t			*abuf;
	xfs_bmbt_rec_t		*arp;
	xfs_btree_lblock_t	*block;
	xfs_btree_cur_t		*cur;
	xfs_bmbt_rec_t		*ep;
	xfs_extnum_t		i;
	xfs_bmbt_key_t		*kp;
	xfs_mount_t		*mp;
	xfs_extnum_t		nextents;
	xfs_bmbt_ptr_t		*pp;
	xfs_sb_t		*sbp;
	xfs_alloctype_t		type;

	ASSERT(ip->i_d.di_format == XFS_DINODE_FMT_EXTENTS);
	/*
	 * Make space in the inode incore.
	 */
	xfs_iroot_realloc(ip, 1);
	ip->i_flags |= XFS_IBROOT;
	/*
	 * Fill in the root.
	 */
	block = ip->i_broot;
	block->bb_magic = XFS_BMAP_MAGIC;
	block->bb_level = 1;
	block->bb_numrecs = 1;
	block->bb_leftsib = block->bb_rightsib = NULLFSBLOCK;
	/*
	 * Need a cursor.  Can't allocate until bb_level is filled in.
	 */
	mp = ip->i_mount;
	sbp = &mp->m_sb;
	cur = xfs_btree_init_cursor(mp, tp, NULL, 0, XFS_BTNUM_BMAP, ip);
	cur->bc_private.b.firstblock = firstblock;
	cur->bc_private.b.flist = flist;
	/*
	 * Convert to a btree with two levels, one record in root.
	 */
	ip->i_d.di_format = XFS_DINODE_FMT_BTREE;
	if (firstblock == NULLFSBLOCK) {
		type = XFS_ALLOCTYPE_START_BNO;
		abno = xfs_ino_to_fsb(sbp, ip->i_ino);
	} else {
		type = XFS_ALLOCTYPE_NEAR_BNO;
		abno = firstblock;
	}
	abno = xfs_alloc_extent(tp, abno, 1, type, 0, 0);
	/*
	 * Allocation can't fail, the space was reserved.
	 */
	ASSERT(abno != NULLFSBLOCK);
	if (firstblock == NULLFSBLOCK)	
		firstblock = abno;
	abuf = xfs_btree_get_bufl(mp, tp, abno, 0);
	/*
	 * Fill in the child block.
	 */
	ablock = xfs_buf_to_lblock(abuf);
	ablock->bb_magic = XFS_BMAP_MAGIC;
	ablock->bb_level = 0;
	ablock->bb_numrecs = 0;
	ablock->bb_leftsib = ablock->bb_rightsib = NULLFSBLOCK;
	arp = XFS_BMAP_REC_IADDR(ablock, 1, cur);
	nextents = ip->i_bytes / sizeof(xfs_bmbt_rec_t);
	for (ep = ip->i_u1.iu_extents, i = 0; i < nextents; i++, ep++) {
		if (xfs_bmbt_get_startblock(ep) != NULLSTARTBLOCK) {
			*arp++ = *ep;
			ablock->bb_numrecs++;
		}
	}
	ASSERT(ablock->bb_numrecs == ip->i_d.di_nextents);
	/*
	 * Fill in the root key and pointer.
	 */
	kp = XFS_BMAP_KEY_IADDR(block, 1, cur);
	arp = XFS_BMAP_REC_IADDR(ablock, 1, cur);
	kp->br_startoff = xfs_bmbt_get_startoff(arp);
	pp = XFS_BMAP_PTR_IADDR(block, 1, cur);
	*pp = abno;
	/*
	 * Do all this logging at the end so that 
	 * the root is at the right level.
	 */
	xfs_bmbt_log_block(cur, abuf, XFS_BB_ALL_BITS);
	xfs_bmbt_log_recs(cur, abuf, 1, ablock->bb_numrecs);
	xfs_trans_log_inode(tp, ip, XFS_ILOG_BROOT);
	xfs_bmbt_rcheck(cur);
	xfs_bmbt_kcheck(cur);
	xfs_btree_del_cursor(cur);
	kmem_check();
	return firstblock;
}

STATIC void
xfs_bmap_insert_exlist(
	xfs_inode_t	*ip,
	xfs_extnum_t	idx,
	xfs_extnum_t	count,
	xfs_bmbt_irec_t	*new)
{
	xfs_bmbt_rec_t	*ep;
	xfs_extnum_t	from;
	xfs_extnum_t	nextents;
	xfs_extnum_t	to;

	ASSERT(ip->i_flags & XFS_IEXTENTS);
	xfs_iext_realloc(ip, count);
	ep = ip->i_u1.iu_extents;
	nextents = ip->i_bytes / sizeof(xfs_bmbt_rec_t);
	for (to = nextents - 1, from = to - count; from >= idx; from--, to--)
		ep[to] = ep[from];
	for (to = idx; to < idx + count; to++, new++)
		xfs_bmbt_set_all(&ep[to], *new);
	kmem_check();
}

/*
 * Convert a local file to an extents file.
 * This code is sort of bogus, since the file data needs to get 
 * logged so it won't be lost.
 */
STATIC xfs_fsblock_t
xfs_bmap_local_to_extents(
	xfs_trans_t	*tp,
	xfs_inode_t	*ip,
	xfs_fsblock_t	firstblock,
	xfs_extlen_t	total)
{
	xfs_fsblock_t	askbno;
	xfs_fsblock_t	bno;
	buf_t		*buf;
	char		*cp;
	xfs_bmbt_rec_t	*ep;
	int		flags = 0;
	xfs_mount_t	*mp;
	xfs_sb_t	*sbp;
	xfs_alloctype_t	type;

	ASSERT(ip->i_d.di_format == XFS_DINODE_FMT_LOCAL);
	mp = ip->i_mount;
	sbp = &mp->m_sb;
	if (ip->i_bytes) {
		ASSERT(ip->i_flags & XFS_IINLINE);
		/*
		 * Allocate a block.  We know we need only one, since the
		 * file currently fits in an inode.
		 */
		if (firstblock == NULLFSBLOCK) {
			askbno = xfs_ino_to_fsb(sbp, ip->i_ino);
			type = XFS_ALLOCTYPE_START_BNO;
			total++;
		} else {
			askbno = firstblock;
			type = XFS_ALLOCTYPE_NEAR_BNO;
			total = 0;
		}
		bno = xfs_alloc_extent(tp, askbno, 1, type, total, 0);
		/* 
		 * Can't fail, the space was reserved.
		 */
		ASSERT(bno != NULLFSBLOCK);
		if (firstblock == NULLFSBLOCK)
			firstblock = bno;
		buf = xfs_btree_get_bufl(mp, tp, bno, 0);
		cp = (char *)buf->b_un.b_addr;
		bcopy(ip->i_u1.iu_data, cp, ip->i_bytes);
		xfs_idata_realloc(ip, -ip->i_bytes);
		xfs_iext_realloc(ip, 1);
		ep = ip->i_u1.iu_extents;
		xfs_bmbt_set_startoff(ep, 0);
		xfs_bmbt_set_startblock(ep, bno);
		xfs_bmbt_set_blockcount(ep, 1);
		ip->i_d.di_nextents = 1;
		flags |= XFS_ILOG_EXT;
	} else
		ASSERT(ip->i_d.di_nextents == 0);
	ip->i_flags &= ~XFS_IINLINE;
	ip->i_flags |= XFS_IEXTENTS;
	ip->i_d.di_format = XFS_DINODE_FMT_EXTENTS;
	flags |= XFS_ILOG_CORE;
	xfs_trans_log_inode(tp, ip, flags);
	kmem_check();
	return firstblock;
}

/*
 * Search the extents list for the inode, for the extent containing bno.
 * If bno lies in a hole, point to the next entry.  If bno lies past eof,
 * *eofp will be set, and *prevp will contain the last entry (null if none).
 * Else, *lastxp will be set to the index of the found
 * entry; *gotp will contain the entry.
 */
STATIC xfs_bmbt_rec_t *
xfs_bmap_search_extents(
	xfs_inode_t	*ip,
	xfs_fsblock_t	bno,
	int		*eofp,
	xfs_extnum_t	*lastxp,
	xfs_bmbt_irec_t	*gotp,
	xfs_bmbt_irec_t	*prevp)
{
	xfs_bmbt_rec_t	*base;
	xfs_bmbt_rec_t	*ep;
	xfs_bmbt_irec_t	got;
	int		high;
	xfs_extnum_t	lastx;
	int		low;
	xfs_extnum_t	nextents;

	lastx = ip->i_lastex;
	nextents = ip->i_bytes / sizeof(xfs_bmbt_rec_t);
	base = &ip->i_u1.iu_extents[0];
	if (lastx != NULLEXTNUM && lastx < nextents) {
		ep = base + lastx;
	} else
		ep = NULL;
	prevp->br_startoff = NULLFSBLOCK;
	if (ep && bno >= (got.br_startoff = xfs_bmbt_get_startoff(ep)) &&
	    bno < got.br_startoff +
		  (got.br_blockcount = xfs_bmbt_get_blockcount(ep))) {
		*eofp = 0;
	} else if (ep && lastx < nextents - 1 &&
		   bno >= (got.br_startoff = xfs_bmbt_get_startoff(ep + 1)) &&
		   bno < got.br_startoff +
			(got.br_blockcount = xfs_bmbt_get_blockcount(ep + 1))) {
		lastx++;
		ep++;
		*eofp = 0;
	} else if (nextents == 0) {
		*eofp = 1;
	} else {
		/* binary search the extents array */
		low = 0;
		high = nextents - 1;
		while (low <= high) {
			lastx = (low + high) >> 1;
			ep = base + lastx;
			got.br_startoff = xfs_bmbt_get_startoff(ep);
			got.br_blockcount = xfs_bmbt_get_blockcount(ep);
			if (bno < got.br_startoff)
				high = lastx - 1;
			else if (bno >= got.br_startoff + got.br_blockcount)
				low = lastx + 1;
			else {
				got.br_startblock = xfs_bmbt_get_startblock(ep);
				*eofp = 0;
				*lastxp = lastx;
				*gotp = got;
				kmem_check();
				return ep;
			}
		}
		if (bno >= got.br_startoff + got.br_blockcount) {
			lastx++;
			if (lastx == nextents) {
				*eofp = 1;
				got.br_startblock = xfs_bmbt_get_startblock(ep);
				*prevp = got;
				ep = NULL;
			} else {
				*eofp = 0;
				xfs_bmbt_get_all(ep, *prevp);
				ep++;
				got.br_startoff = xfs_bmbt_get_startoff(ep);
				got.br_blockcount = xfs_bmbt_get_blockcount(ep);
			}
		} else {
			*eofp = 0;
			if (ep > base)
				xfs_bmbt_get_all(ep - 1, *prevp);
		}
	}
	if (ep)
		got.br_startblock = xfs_bmbt_get_startblock(ep);
	*lastxp = lastx;
	*gotp = got;
	kmem_check();
	return ep;
}

/*
 * Add the extent to the list of extents to be free at transaction end.
 * The list is maintained sorted (by block number).
 */
void
xfs_bmap_add_free(
	xfs_fsblock_t		bno,		/* fs block number of extent */
	xfs_extlen_t		len,		/* length of extent */
	xfs_bmap_free_t		*flist)		/* list of extents */
{
	xfs_bmap_free_item_t	*cur;		/* current (next) element */
	xfs_bmap_free_item_t	*new;		/* new element */
	xfs_bmap_free_item_t	*prev;		/* previous element */

	if (!xfs_bmap_free_item_zone)
		xfs_bmap_free_item_zone = kmem_zone_init(sizeof(*cur), "xfs_bmap_free_item");
	new = kmem_zone_alloc(xfs_bmap_free_item_zone, KM_SLEEP);
	new->xbfi_startblock = bno;
	new->xbfi_blockcount = len;
	for (prev = NULL, cur = flist->xbf_first; cur != NULL; prev = cur, cur = cur->xbfi_next) {
		if (cur->xbfi_startblock >= bno)
			break;
	}
	if (prev)
		prev->xbfi_next = new;
	else
		flist->xbf_first = new;
	new->xbfi_next = cur;
	flist->xbf_count++;
	kmem_check();
}

/*
 * Routine to be called at transaction's end by xfs_bmapi, xfs_bunmapi 
 * caller.  Frees all the extents that need freeing, which must be done
 * last due to locking considerations.
 */
void
xfs_bmap_finish(
	xfs_trans_t		**tp,		/* transaction pointer addr */
	xfs_bmap_free_t		*flist,		/* i/o: list extents to free */
	xfs_fsblock_t		firstblock)	/* controlled ag for allocs */
{
	unsigned int		blkres;
	xfs_efd_log_item_t	*efd;
	xfs_efi_log_item_t	*efi;
	xfs_agnumber_t		firstag;
	xfs_bmap_free_item_t	*free;
	int			i;
	unsigned int		logres;
	xfs_mount_t		*mp;
	xfs_bmap_free_item_t	*next;
	xfs_trans_t		*ntp;
	xfs_bmap_free_item_t	*prev;
	xfs_sb_t		*sbp;

	if (flist->xbf_count == 0)
		return;
	ntp = *tp;
	mp = ntp->t_mountp;
	sbp = &mp->m_sb;
	firstag = xfs_fsb_to_agno(sbp, firstblock);
	for (prev = NULL, free = flist->xbf_first; free != NULL; free = next) {
		next = free->xbfi_next;
		if (xfs_fsb_to_agno(sbp, free->xbfi_startblock) < firstag)
			continue;
		xfs_free_extent(ntp, free->xbfi_startblock,
			free->xbfi_blockcount);
		xfs_bmap_del_free(flist, prev, free);
	}
	if (flist->xbf_count == 0)
		return;
	efi = xfs_trans_get_efi(ntp, flist->xbf_count);
	for (free = flist->xbf_first, i = 0; free; free = free->xbfi_next, i++)
		xfs_trans_log_efi_extent(ntp, efi + i, free->xbfi_startblock,
			free->xbfi_blockcount);
	logres = ntp->t_log_res;
	blkres = ntp->t_blk_res - ntp->t_blk_res_used;
	xfs_trans_commit(ntp, 0);
	ntp = xfs_trans_alloc(mp, 0);
	xfs_trans_reserve(ntp, blkres, logres, 0, 0);
	efd = xfs_trans_get_efd(ntp, efi, flist->xbf_count);
	for (free = flist->xbf_first, i = 0; free != NULL; free = next, i++) {
		next = free->xbfi_next;
		xfs_free_extent(ntp, free->xbfi_startblock, free->xbfi_blockcount);
		xfs_trans_log_efd_extent(ntp, efd + i, free->xbfi_startblock, free->xbfi_blockcount);
		xfs_bmap_del_free(flist, NULL, free);
	}
	*tp = ntp;
	kmem_check();
}

/*
 * Read in the extents to iu_extents.
 * All inode fields are set up by caller, we just traverse the btree
 * and copy the records in.
 */
void
xfs_bmap_read_extents(
	xfs_trans_t		*tp,	/* transaction pointer */
	xfs_inode_t		*ip)	/* incore inode */
{
	xfs_btree_lblock_t	*block;	/* current btree block */
	xfs_bmbt_ptr_t		bno;	/* block # of "block" */
	buf_t			*buf;	/* buffer for "block" */
	xfs_extnum_t		i;	/* index into the extents list */
	xfs_mount_t		*mp;	/* file system mount structure */
	xfs_extnum_t		room;	/* number of entries there's room for */
	xfs_sb_t		*sbp;	/* super block structure */
	xfs_bmbt_rec_t		*trp;	/* target record pointer */

	bno = NULLFSBLOCK;
	mp = ip->i_mount;
	sbp = &mp->m_sb;
	block = ip->i_broot;
	/*
	 * Root level must use BMAP_BROOT_PTR_ADDR macro to get ptr out.
	 */
	if (block->bb_level)
		bno = *XFS_BMAP_BROOT_PTR_ADDR(block, 1, sbp->sb_inodesize);
	/*
	 * Go down the tree until leaf level is reached, following the first
	 * pointer (leftmost) at each level.
	 */
	while (block->bb_level) {
		buf = xfs_btree_read_bufl(mp, tp, bno, 0);
		block = xfs_buf_to_lblock(buf);
		if (block->bb_level == 0)
			break;
		bno = *XFS_BTREE_PTR_ADDR(sbp->sb_blocksize, xfs_bmbt,
					  block, 1);
		xfs_trans_brelse(tp, buf);
	}
	/*
	 * Here with buf and block set to the leftmost leaf node in the tree.
	 */
	trp = ip->i_u1.iu_extents;
	room = ip->i_bytes / sizeof(*trp);
	i = 0;
	/*
	 * Loop over all leaf nodes.  Copy information to the extent list.
	 */
	for (;;) {
		xfs_bmbt_rec_t	*frp;
		xfs_bmbt_ptr_t	nextbno;

		ASSERT(i + block->bb_numrecs <= room);
		/*
		 * Copy records into the extent list.
		 */
		frp = XFS_BTREE_REC_ADDR(sbp->sb_blocksize, xfs_bmbt, block, 1);
		bcopy(frp, trp, block->bb_numrecs * sizeof(*frp));
		trp += block->bb_numrecs;
		i += block->bb_numrecs;
		/*
		 * Get the next leaf block's address.
		 */
		nextbno = block->bb_rightsib;
		/*
		 * bno can only be NULLFSBLOCK if the root block is also a leaf;
		 * we could make this illegal.
		 */
		if (bno != NULLFSBLOCK)
			xfs_trans_brelse(tp, buf);
		bno = nextbno;
		/*
		 * If we've reached the end, stop.
		 */
		if (bno == NULLFSBLOCK)
			break;
		buf = xfs_btree_read_bufl(mp, tp, bno, 0);
		block = xfs_buf_to_lblock(buf);
	}
	kmem_check();
}

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
	xfs_inode_t		*ip,		/* incore inode */
	xfs_fsblock_t		bno,		/* starting file offs. mapped */
	xfs_extlen_t		len,		/* length to map in file */
	int			flags,		/* XFS_BMAPI_... */
	xfs_fsblock_t		firstblock,	/* controls a.g. for allocs */
	xfs_extlen_t		total,		/* total blocks needed */
	xfs_bmbt_irec_t		*mval,		/* output: map values */
	int			*nmap,		/* i/o: mval size/count */
	xfs_bmap_free_t		*flist)		/* i/o: list extents to free */
{
	xfs_fsblock_t		abno;
	xfs_extlen_t		alen;
	xfs_fsblock_t		aoff;
	xfs_fsblock_t		askbno;
	xfs_extlen_t		asklen;
	xfs_btree_cur_t		*cur;
	int			delay;
	xfs_fsblock_t		end;
	int			eof;
	xfs_bmbt_rec_t		*ep;
	xfs_bmbt_irec_t		got;
	xfs_extnum_t		i;
	int			inhole;
	xfs_extnum_t		lastx;
	int			logflags;
	xfs_mount_t		*mp;
	int			n;
	xfs_extnum_t		nextents;
	xfs_bmbt_irec_t		prev;
	xfs_sb_t		*sbp;
	int			trim;
	xfs_alloctype_t		type;
	int			wasdelay;
	int			wr;

	ASSERT(*nmap >= 1 && *nmap <= XFS_BMAP_MAX_NMAP);
	ASSERT(ip->i_d.di_format == XFS_DINODE_FMT_BTREE ||
	       ip->i_d.di_format == XFS_DINODE_FMT_EXTENTS ||
	       ip->i_d.di_format == XFS_DINODE_FMT_LOCAL);
	wr = (flags & XFS_BMAPI_WRITE) != 0;
	delay = (flags & XFS_BMAPI_DELAY) != 0;
	trim = (flags & XFS_BMAPI_ENTIRE) == 0;
	ASSERT(wr || !delay);
	if (ip->i_d.di_format == XFS_DINODE_FMT_LOCAL) {
		if (!wr) {
			/* change to assert later */
			*nmap = 0;
			kmem_check();
			return firstblock;
		}
		firstblock = xfs_bmap_local_to_extents(tp, ip, firstblock, total);
		logflags |= XFS_ILOG_CORE;
	}
	if (firstblock == NULLFSBLOCK) {
		type = XFS_ALLOCTYPE_START_BNO;
		if (ip->i_d.di_format == XFS_DINODE_FMT_BTREE)
			total += ip->i_broot->bb_level + 1;
		else
			total++;
	} else {
		type = XFS_ALLOCTYPE_NEAR_BNO;
		total = 0;
	}
	if (!(ip->i_flags & XFS_IEXTENTS))
		xfs_iread_extents(tp, ip);
	ep = xfs_bmap_search_extents(ip, bno, &eof, &lastx, &got, &prev);
	nextents = ip->i_bytes / sizeof(xfs_bmbt_rec_t);
	mp = ip->i_mount;
	sbp = &mp->m_sb;
	logflags = n = 0;
	end = bno + len;
	cur = NULL;
	while (bno < end && n < *nmap) {
		/*
		 * Reading past eof, act as though there's a hole
		 * up to end.
		 */
		if (eof && !wr)
			got.br_startoff = end;
		inhole = eof || got.br_startoff > bno;
		wasdelay = !inhole && !delay &&
			got.br_startblock == NULLSTARTBLOCK;
		/*
		 * First, deal with the hole before the allocated space 
		 * that we found, if any.
		 */
		if ((inhole || wasdelay) && wr) {
			/*
			 * For the wasdelay case, we could also just
			 * allocate the stuff asked for in this bmap call
			 * but that wouldn't be as good.
			 */
			alen = eof ? len :
				(wasdelay ? got.br_blockcount :
				 xfs_extlen_min(got.br_startoff - bno, len));
			aoff = wasdelay ? got.br_startoff : bno;
			if (delay) {
				if (xfs_mod_incore_sb(mp, XFS_SB_FDBLOCKS,
						      -alen))
					break;
				abno = NULLSTARTBLOCK;
			} else {
				abno = xfs_bmap_alloc(tp, ip, eof, &prev, &got,
					&firstblock, &alen, total, aoff,
					wasdelay);
				if (abno == NULLFSBLOCK)
					break;
				if ((ip->i_flags & XFS_IBROOT) && !cur) {
					cur = xfs_btree_init_cursor(mp, tp,
						NULL, 0, XFS_BTNUM_BMAP, ip);
					cur->bc_private.b.firstblock =
						firstblock;
					cur->bc_private.b.flist = flist;
				}
			}
			got.br_startoff = aoff;
			got.br_startblock = abno;
			got.br_blockcount = alen;
			logflags |= xfs_bmap_add_extent(ip, lastx, cur, &got);
			lastx = ip->i_lastex;
			ep = &ip->i_u1.iu_extents[lastx];
			nextents = ip->i_bytes / sizeof(xfs_bmbt_rec_t);
			xfs_bmbt_get_all(ep, got);
			/*
			 * Fall down into the found allocated space case.
			 */
		} else if (inhole) {
			/*
			 * Reading in a hole.
			 */
			mval->br_startoff = bno;
			mval->br_startblock = HOLESTARTBLOCK;
			mval->br_blockcount = xfs_extlen_min(len, got.br_startoff - bno);
			bno += mval->br_blockcount;
			len -= mval->br_blockcount;
			mval++;
			n++;
			continue;
		}
		/*
		 * Then deal with the allocated space we found.
		 */
		ASSERT(ep != NULL);
		if (trim) {
			mval->br_startoff = bno;
			if (got.br_startblock == NULLSTARTBLOCK)
				mval->br_startblock = DELAYSTARTBLOCK;
			else
				mval->br_startblock = got.br_startblock +
					(bno - got.br_startoff);
			mval->br_blockcount =
				xfs_extlen_min(len, got.br_blockcount -
					(bno - got.br_startoff));
		} else {
			*mval = got;
			if (mval->br_startblock == NULLSTARTBLOCK)
				mval->br_startblock = DELAYSTARTBLOCK;
		}
		bno = mval->br_startoff + mval->br_blockcount;
		len = end - bno;
		if (n > 0 && mval->br_startblock != DELAYSTARTBLOCK &&
		    mval[-1].br_startblock != DELAYSTARTBLOCK &&
		    mval[-1].br_startblock != HOLESTARTBLOCK &&
		    mval->br_startblock ==
		    mval[-1].br_startblock + mval[-1].br_blockcount) {
			ASSERT(mval->br_startoff ==
			       mval[-1].br_startoff + mval[-1].br_blockcount);
			mval[-1].br_blockcount += mval->br_blockcount;
		} else if (n > 0 &&
			   mval->br_startblock == DELAYSTARTBLOCK &&
			   mval[-1].br_startblock == DELAYSTARTBLOCK &&
			   mval->br_startoff ==
			   mval[-1].br_startoff + mval[-1].br_blockcount) {
			mval[-1].br_blockcount += mval->br_blockcount;
		} else {
			mval++;
			n++;
		}
		/*
		 * If we're done, stop now.
		 */
		if (bno >= end || n >= *nmap)
			break;
		/*
		 * Else go on to the next record.
		 */
		ep++;
		lastx++;
		if (lastx >= nextents) {
			eof = 1;
			prev = got;
		} else
			xfs_bmbt_get_all(ep, got);
	}
	ip->i_lastex = lastx;
	*nmap = n;
	/*
	 * Convert to a btree if necessary.
	 */
	if (ip->i_d.di_format == XFS_DINODE_FMT_EXTENTS &&
	    ip->i_d.di_nextents > XFS_BMAP_EXT_MAXRECS(sbp->sb_inodesize)) {
		firstblock = xfs_bmap_extents_to_btree(tp, ip, firstblock,
			flist);
		logflags |= XFS_ILOG_CORE;
	}
	/*
	 * Log everything.  Do this after conversion, there's no point in
	 * logging the extent list if we've converted to btree format.
	 */
	if ((logflags & XFS_ILOG_EXT) &&
	    ip->i_d.di_format != XFS_DINODE_FMT_EXTENTS)
		logflags &= ~XFS_ILOG_EXT;
	if (logflags)
		xfs_trans_log_inode(tp, ip, logflags);
	/* logging of the BROOT happens elsewhere */
	if (cur)
		xfs_btree_del_cursor(cur);
	kmem_check();
	return firstblock;
}

/*
 * Unmap (remove) blocks from a file.
 */
xfs_fsblock_t					/* first allocated block */
xfs_bunmapi(
	xfs_trans_t		*tp,		/* transaction pointer */
	struct xfs_inode	*ip,		/* incore inode */
	xfs_fsblock_t		bno,		/* starting offset to unmap */
	xfs_extlen_t		len,		/* length to unmap in file */
	xfs_fsblock_t		firstblock,	/* controls a.g. for allocs */
	xfs_bmap_free_t		*flist)		/* i/o: list extents to free */
{
	xfs_btree_cur_t		*cur;
	xfs_bmbt_irec_t		del;
	int			eof;
	xfs_bmbt_rec_t		*ep;
	xfs_bmbt_irec_t		got;
	xfs_extnum_t		i;
	xfs_extnum_t		lastx;
	int			logflags;
	xfs_mount_t		*mp;
	xfs_extnum_t		nextents;
	xfs_bmbt_irec_t		prev;
	xfs_sb_t		*sbp;
	xfs_fsblock_t		start;

	ASSERT(ip->i_d.di_format == XFS_DINODE_FMT_BTREE ||
	       ip->i_d.di_format == XFS_DINODE_FMT_EXTENTS);
	if (!(ip->i_flags & XFS_IEXTENTS))
		xfs_iread_extents(tp, ip);
	nextents = ip->i_bytes / sizeof(xfs_bmbt_rec_t);
	if (nextents == 0)
		return firstblock;
	start = bno;
	bno = start + len - 1;
	ep = xfs_bmap_search_extents(ip, bno, &eof, &lastx, &got, &prev);
	/*
	 * Check to see if the given block number is past the end of the
	 * file, back up to the last block if so...
	 * Do we want to reject this instead?
	 */
	if (eof) {
		lastx--;
		ep = &ip->i_u1.iu_extents[lastx];
		xfs_bmbt_get_all(ep, got);
		bno = got.br_startoff + got.br_blockcount - 1;
	}
	mp = ip->i_mount;
	sbp = &mp->m_sb;
	logflags = 0;
	if (ip->i_flags & XFS_IBROOT) {
		cur = xfs_btree_init_cursor(mp, tp, NULL, 0, XFS_BTNUM_BMAP, ip);
		cur->bc_private.b.firstblock = firstblock;
		cur->bc_private.b.flist = flist;
	} else
		cur = NULL;
	while (bno >= start && lastx >= 0) {
		/*
		 * Is the found extent after a hole in which bno lives?
		 * Just back up to the previous extent, if so.
		 */
		if (got.br_startoff > bno) {
			if (--lastx < 0)
				break;
			ep--;
			xfs_bmbt_get_all(ep, got);
			bno = got.br_startoff + got.br_blockcount - 1;
			if (bno < start)
				break;
		}
		/*
		 * Then deal with the (possibly delayed) allocated space
		 * we found.
		 */
		ASSERT(ep != NULL);
		del = got;
		if (got.br_startoff < start) {
			del.br_startoff = start;
			del.br_blockcount -= start - got.br_startoff;
			if (del.br_startblock != NULLSTARTBLOCK)
				del.br_startblock += start - got.br_startoff;
		}
		if (del.br_startoff + del.br_blockcount > start + len)
			del.br_blockcount = start + len - del.br_startoff;
		if (del.br_startblock == NULLSTARTBLOCK)
			xfs_mod_incore_sb(mp, XFS_SB_FDBLOCKS, del.br_blockcount);
			/* could fail?? */
		logflags |= xfs_bmap_del_extent(ip, lastx, flist, cur, &del);
		bno = del.br_startoff - 1;
		/*
		 * If not done go on to the next (previous) record.
		 */
		if (bno >= start && --lastx >= 0) {
			ep--;
			xfs_bmbt_get_all(ep, got);
		}
	}
	ip->i_lastex = lastx;
	/*
	 * Convert to a btree if necessary.
	 */
	if (ip->i_d.di_format == XFS_DINODE_FMT_EXTENTS &&
	    ip->i_d.di_nextents > XFS_BMAP_EXT_MAXRECS(sbp->sb_inodesize)) {
		firstblock = xfs_bmap_extents_to_btree(tp, ip, firstblock, flist);
		logflags |= XFS_ILOG_CORE;
	} 
	/* transform from btree to extents, give it cur */
	else if (ip->i_d.di_format == XFS_DINODE_FMT_BTREE &&
		 ip->i_d.di_nextents <= XFS_BMAP_EXT_MAXRECS(sbp->sb_inodesize)) {
		xfs_bmap_btree_to_extents(tp, ip, cur);
		logflags |= XFS_ILOG_CORE | XFS_ILOG_EXT;
	}
	/* transform from extents to local */
	if (cur)
		firstblock = cur->bc_private.b.firstblock;
	/*
	 * Log everything.  Do this after conversion, there's no point in
	 * logging the extent list if we've converted to btree format.
	 */
	if ((logflags & XFS_ILOG_EXT) &&
	    ip->i_d.di_format != XFS_DINODE_FMT_EXTENTS)
		logflags &= ~XFS_ILOG_EXT;
	if (logflags)
		xfs_trans_log_inode(tp, ip, logflags);
	/* logging of the BROOT happens elsewhere */
	if (cur)
		xfs_btree_del_cursor(cur);
	kmem_check();
	return firstblock;
}
