#include <sys/param.h>
#include <sys/errno.h>
#include <sys/buf.h>
#include <sys/vnode.h>
#include <sys/uuid.h>
#include <sys/kmem.h>
#include <sys/debug.h>
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
#include "xfs_mount.h"
#include "xfs_alloc_btree.h"
#include "xfs_bmap_btree.h"
#include "xfs_bmap.h"
#include "xfs_btree.h"
#include "xfs_dinode.h"
#include "xfs_inode_item.h"
#include "xfs_inode.h"
#include "xfs_dir.h"
#include "xfs_dir_btree.h"
#ifdef SIM
#include "sim.h"
#endif

/*
 * xfs_dir_btree.c
 *
 * Routines to implement directories as Btrees of hashed names.
 */

/*========================================================================
 * Function prototypes for the kernel.
 *========================================================================*/

/*
 * Routines used for growing the Btree.
 */
STATIC int xfs_dir_root_split(struct xfs_dir_state *state,
				     struct xfs_dir_state_blk *existing_root,
				     struct xfs_dir_state_blk *new_child);
STATIC int xfs_dir_leaf_split(struct xfs_dir_state *state,
				     struct xfs_dir_state_blk *oldblk,
				     struct xfs_dir_state_blk *newblk);
STATIC void xfs_dir_leaf_add_work(xfs_trans_t *trans, buf_t *leaf_buffer,
					      struct xfs_dir_name *args,
					      int insertion_index,
					      int freemap_index);
STATIC void xfs_dir_leaf_compact(xfs_trans_t *trans, buf_t *leaf_buffer);
STATIC void xfs_dir_leaf_rebalance(struct xfs_dir_state *state,
					  struct xfs_dir_state_blk *blk1,
					  struct xfs_dir_state_blk *blk2);
STATIC int xfs_dir_leaf_figure_balance(struct xfs_dir_state *state,
					   struct xfs_dir_state_blk *leaf_blk_1,
					   struct xfs_dir_state_blk *leaf_blk_2,
					   int *number_entries_in_blk1,
					   int *number_namebytes_in_blk1);
STATIC int xfs_dir_node_split(struct xfs_dir_state *state,
				     struct xfs_dir_state_blk *existing_blk,
				     struct xfs_dir_state_blk *split_blk,
				     struct xfs_dir_state_blk *blk_to_add,
				     int treelevel);
STATIC void xfs_dir_node_rebalance(struct xfs_dir_state *state,
					  struct xfs_dir_state_blk *node_blk_1,
					  struct xfs_dir_state_blk *node_blk_2,
					  uint hashval_of_new_node);
STATIC void xfs_dir_node_add(struct xfs_dir_state *state,
				    struct xfs_dir_state_blk *old_node_blk,
				    struct xfs_dir_state_blk *new_node_blk);

/*
 * Routines used for shrinking the Btree.
 */
STATIC int xfs_dir_root_join(struct xfs_dir_state *state,
				    struct xfs_dir_state_blk *root_blk);
STATIC int xfs_dir_blk_toosmall(struct xfs_dir_state *state, int level);
STATIC void xfs_dir_leaf_unbalance(struct xfs_dir_state *state,
					  struct xfs_dir_state_blk *drop_blk,
					  struct xfs_dir_state_blk *save_blk);
STATIC void xfs_dir_node_remove(struct xfs_dir_state *state,
				       struct xfs_dir_state_blk *drop_blk);
STATIC void xfs_dir_node_unbalance(struct xfs_dir_state *state,
					struct xfs_dir_state_blk *src_node_blk,
					struct xfs_dir_state_blk *dst_node_blk);

/*
 * Routines used for finding things in the Btree.
 */
STATIC void xfs_dir_findpath(struct xfs_dir_state *state,
				    struct xfs_dir_state_path *path_to_fill_in,
				    uint hashval_to_find,
				    xfs_fsblock_t blkno_to_find);

/*
 * Utility routines.
 */
STATIC void xfs_dir_leaf_moveents(struct xfs_dir_leafblock *src_leaf,
					 int src_start,
					 struct xfs_dir_leafblock *dst_leaf,
					 int dst_start, int move_count,
					 xfs_sb_t *sbp);
STATIC int xfs_dir_leaf_refind(struct xfs_dir_leafblock *leaf, int likely_index,
				      uint hashval, xfs_sb_t *sbp);
STATIC int xfs_dir_node_refind(struct xfs_dir_intnode *node,
				      int likely_index, uint hashval);
STATIC void xfs_dir_blk_unlink(struct xfs_dir_state *state,
				      struct xfs_dir_state_blk *drop_blk,
				      struct xfs_dir_state_blk *save_blk);
STATIC void xfs_dir_blk_link(struct xfs_dir_state *state,
				    struct xfs_dir_state_blk *old_blk,
				    struct xfs_dir_state_blk *new_blk);


/*========================================================================
 * Routines used for growing the Btree.
 *========================================================================*/

/*
 * Split a leaf node, rebalance, then possibly split
 * intermediate nodes, rebalance, etc.
 */
int
xfs_dir_split(struct xfs_dir_state *state)
{
	struct xfs_dir_state_blk *oldblk, *newblk, *addblk;
	struct xfs_dir_intnode *node;
	int max, retval, i;

	/*
	 * Walk back up the tree splitting/inserting/adjusting as necessary.
	 * If we need to insert and there isn't room, split the node, then
	 * decide which fragment to insert the new block from below into.
	 * Note that we may split the root this way, but we need more fixup.
	 */
	max = state->path.active - 1;
	ASSERT(state->path.blk[max].leafblk == 1);
	for (i = max; (i >= 0) && addblk; i--) {
		oldblk = &state->path.blk[i];
		newblk = &state->altpath.blk[i];
		if (oldblk->leafblk) {
			/*
			 * Allocate a new leaf node, then rebalance across them.
			 */
			retval = xfs_dir_leaf_split(state, oldblk, newblk);
		} else {
			/*
			 * We split on the last layer, must we split the node?
			 * GROT: fix up this interface.
			 */
			retval = xfs_dir_node_split(state, oldblk, newblk,
							   addblk, max - i);
		}
		if (retval > 0)
			return(retval);	/* GROT: dir is inconsistent */

		/*
		 * Update the btree to show the new hashval for this child.
		 */
		xfs_dir_fixhashpath(state, &state->path, i);

		/*
		 * Record the newly split block for the next time thru?
		 */
		if (retval < 0)
			addblk = newblk;
		else
			addblk = NULL;
	}
	if (!addblk)
		return(0);

	/*
	 * Split the root node.
	 */
	oldblk = &state->path.blk[0];
	ASSERT(oldblk->bp->b_un.b_addr == 0);	/* root must be at block 0 */
	ASSERT(oldblk->blkno == 0);
	retval = xfs_dir_root_split(state, oldblk, addblk);
	if (retval)	
		return(retval);	/* GROT: dir is inconsistent */

	/*
	 * Update the backward pointer in the second child node.  It was
	 * set to zero in node_link() because it WAS block 0.
	 */
	node = (struct xfs_dir_intnode *)addblk->bp->b_un.b_addr;
	node->hdr.info.back = oldblk->blkno;
	xfs_trans_log_buf(state->trans, addblk->bp, 0,
					sizeof(node->hdr.info) - 1);

	return(0);
}

/*
 * Split the root.  We have to create a new root and point to the two
 * parts (the split old root) that we just created.  Copy block zero to
 * the EOF, extending the inode in process.
 */
STATIC int
xfs_dir_root_split(struct xfs_dir_state *state,
			  struct xfs_dir_state_blk *blk1,
			  struct xfs_dir_state_blk *blk2)
{
	struct xfs_dir_intnode *node, *oldroot;
	xfs_fsblock_t blkno;
	buf_t *bp;
	int retval;

	retval = xfs_dir_grow_inode(state->trans, state->args, state->blocksize,
				    &blkno);
	if (retval)
		return(retval);
	bp = xfs_dir_get_buf(state->trans, state->args->dp, blkno);
	bcopy(blk1->bp->b_un.b_addr, bp->b_un.b_addr, state->blocksize);
	blk1->bp = bp;
	blk1->blkno = blkno;
	xfs_trans_log_buf(state->trans, bp, 0, state->blocksize - 1);

	/*
	 * Set up the new root node.
	 */
	oldroot = (struct xfs_dir_intnode *)blk1->bp->b_un.b_addr;
	bp = xfs_dir_node_create(state->trans, state->args->dp, 0,
					       oldroot->hdr.level + 1);
	node = (struct xfs_dir_intnode *)bp->b_un.b_addr;
	node->btree[0].hashval = blk1->hashval;
	node->btree[0].before = blk1->blkno;
	node->btree[1].hashval = blk2->hashval;
	node->btree[1].before = blk2->blkno;
	node->hdr.count = 2;
#ifdef GROT
	node->hdr.freecount = oldroot->hdr.freecount;
	node->hdr.freechain = oldroot->hdr.freechain;
	oldroot->hdr.freecount = 0;
	oldroot->hdr.freechain = 0;
#endif
	xfs_trans_log_buf(state->trans, bp, 0, state->blocksize - 1);

	return(0);
}

/*
 * Split the leaf node, rebalance, then add the new entry.
 */
STATIC int
xfs_dir_leaf_split(struct xfs_dir_state *state,
			  struct xfs_dir_state_blk *oldblk,
			  struct xfs_dir_state_blk *newblk)
{
	struct xfs_dir_leafblock *leaf;
	xfs_fsblock_t blkno;
	int retval;

	/*
	 * Allocate space for a new leaf node.
	 * GROT: use a block off the freelist if possible.
	 */
	ASSERT(oldblk->leafblk == 1);
	retval = xfs_dir_grow_inode(state->trans, state->args, state->blocksize,
				    &blkno);
	if (retval)
		return(retval);
	newblk->bp = xfs_dir_leaf_create(state->trans, state->args->dp, blkno);
	newblk->blkno = blkno;
	newblk->leafblk = 1;

	/*
	 * Rebalance the entries across the two leaves.
	 */
	xfs_dir_leaf_rebalance(state, oldblk, newblk);
	xfs_dir_blk_link(state, oldblk, newblk);

	/*
	 * Insert the new entry in the correct block.
	 */
	if (state->inleaf) {
		retval = xfs_dir_leaf_add(state->trans, oldblk->bp, state->args,
							oldblk->index);
	} else {
		retval = xfs_dir_leaf_add(state->trans, newblk->bp, state->args,
							newblk->index);
	}
	if (retval)
		return(retval);

	/*
	 * Update last hashval in each block since we added the name.
	 */
	leaf = (struct xfs_dir_leafblock *)oldblk->bp->b_un.b_addr;
	oldblk->hashval = leaf->leaves[leaf->hdr.count-1].hashval;
	leaf = (struct xfs_dir_leafblock *)newblk->bp->b_un.b_addr;
	newblk->hashval = leaf->leaves[leaf->hdr.count-1].hashval;

	return(-1);
}

/*
 * Add a name to the leaf directory structure.
 */
int
xfs_dir_leaf_add(xfs_trans_t *trans, buf_t *bp, struct xfs_dir_name *args,
			     int index)
{
	struct xfs_dir_leafblock *leaf;
	struct xfs_dir_leaf_hdr *hdr;
	struct xfs_dir_leaf_map *map;
	int tablesize, i, tmp, retval;
	xfs_sb_t *sbp;

	leaf = (struct xfs_dir_leafblock *)bp->b_un.b_addr;
	ASSERT(leaf->hdr.info.magic == XFS_DIR_LEAF_MAGIC);
	ASSERT((index >= 0) && (index <= leaf->hdr.count));
	hdr = &leaf->hdr;

	/*
	 * Ensure that we have the right insertion index.
	 */
	sbp = &trans->t_mountp->m_sb;
	index = xfs_dir_leaf_refind(leaf, index, args->hashval, sbp);

	/*
	 * Search through freemap for first-fit on new name length.
	 * (may need to figure in size of entry struct too)
	 */
	tablesize = (hdr->count + 1) * sizeof(struct xfs_dir_leaf_entry)
			+ sizeof(struct xfs_dir_leaf_hdr);
	map = &hdr->freemap[XFS_DIR_LEAF_MAPSIZE-1];
	for (i = XFS_DIR_LEAF_MAPSIZE-1; i >= 0; map--, i--) {
		if (map->size == 0)
			continue;	/* no space in this map */
		tmp = XFS_DIR_LEAF_ENTSIZE_BYNAME(args->namelen);
		if (map->base <= hdr->firstused)
			tmp += sizeof(struct xfs_dir_leaf_entry);
		if ((map->size >= tmp) && (tablesize <= hdr->firstused)) {
			xfs_dir_leaf_add_work(trans, bp, args, index, i);
			return(0);
		}
	}

	/*
	 * If there are no holes in the address space of the block
	 * where we put names, then compaction will do us no good
	 * and we should just give up.
	 */
	if (!hdr->holes)
		return(ENOSPC);

	/*
	 * Compact the entries to coalesce free space.
	 */
	xfs_dir_leaf_compact(trans, bp);

	/*
	 * Search through freemap for first-fit on new name length.
	 * (this is an exact duplicate of the above code segment)
	 */
	map = &hdr->freemap[XFS_DIR_LEAF_MAPSIZE-1];
	for (i = XFS_DIR_LEAF_MAPSIZE-1; i >= 0; map--, i--) {
		if (map->size == 0)
			continue;	/* no space in this map */
		tmp = XFS_DIR_LEAF_ENTSIZE_BYNAME(args->namelen);
		if (map->base <= hdr->firstused)
			tmp += sizeof(struct xfs_dir_leaf_entry);
		if ((map->size >= tmp) && (tablesize <= hdr->firstused)) {
			xfs_dir_leaf_add_work(trans, bp, args, index, i);
			return(0);
		}
	}

	return(ENOSPC);
}

/*
 * Add a name to a leaf directory structure.
 */
STATIC void
xfs_dir_leaf_add_work(xfs_trans_t *trans, buf_t *bp,
				  struct xfs_dir_name *args,
				  int index, int mapindex)
{
	struct xfs_dir_leafblock *leaf;
	struct xfs_dir_leaf_hdr *hdr;
	struct xfs_dir_leaf_entry *entry;
	struct xfs_dir_leaf_name *namest;
	struct xfs_dir_leaf_map *map;
	int tmp, i;
	xfs_sb_t *sbp;

	leaf = (struct xfs_dir_leafblock *)bp->b_un.b_addr;
	ASSERT(leaf->hdr.info.magic == XFS_DIR_LEAF_MAGIC);
	hdr = &leaf->hdr;
	ASSERT((mapindex >= 0) && (mapindex < XFS_DIR_LEAF_MAPSIZE));
	ASSERT((index >= 0) && (index <= hdr->count));

	/*
	 * Force open some space in the entry array and fill it in.
	 */
	entry = &leaf->leaves[index];
	if (index < hdr->count) {
		tmp  = hdr->count - index;
		tmp *= sizeof(struct xfs_dir_leaf_entry);
		bcopy((char *)entry, (char *)(entry+1), tmp);
	}
	hdr->count++;

	/*
	 * Allocate space for the new string (at the end of the run).
	 */
	/* GROT: change all XFS_LBSIZE into state->blocksize */
	map = &hdr->freemap[mapindex];
	sbp = &trans->t_mountp->m_sb;
	ASSERT(map->base < XFS_LBSIZE(sbp));
	ASSERT(map->size >= XFS_DIR_LEAF_ENTSIZE_BYNAME(args->namelen));
	ASSERT(map->size < XFS_LBSIZE(sbp));
	map->size -= XFS_DIR_LEAF_ENTSIZE_BYNAME(args->namelen);
	entry->nameidx = map->base + map->size;
	entry->hashval = args->hashval;

	/*
	 * Copy the string and inode number into the new space.
	 */
	namest = XFS_DIR_LEAF_NAMESTRUCT(leaf, entry->nameidx);
	bcopy((char *)&args->inumber, namest->inumber, sizeof(xfs_ino_t));
	namest->namelen = args->namelen;
	bcopy(args->name, namest->name, args->namelen);

	/*
	 * Update the control info for this leaf node
	 */
	if (entry->nameidx < hdr->firstused)
		hdr->firstused = entry->nameidx;
	ASSERT(hdr->firstused >= ((hdr->count*sizeof(*entry))+sizeof(*hdr)));
	tmp = (hdr->count-1) * sizeof(struct xfs_dir_leaf_entry)
			+ sizeof(struct xfs_dir_leaf_hdr);
	map = &hdr->freemap[0];
	for (i = 0; i < XFS_DIR_LEAF_MAPSIZE; map++, i++) {
		if (map->base == tmp) {
			map->base += sizeof(struct xfs_dir_leaf_entry);
			map->size -= sizeof(struct xfs_dir_leaf_entry);
		}
	}
	hdr->namebytes += args->namelen;

	xfs_trans_log_buf(trans, bp, 0, XFS_LBSIZE(sbp) - 1);
}

/*
 * Garbage collect a leaf directory block by copying it to a new buffer.
 */
STATIC void
xfs_dir_leaf_compact(xfs_trans_t *trans, buf_t *bp)
{
	struct xfs_dir_leafblock *leaf_s, *leaf_d;
	struct xfs_dir_leaf_hdr *hdr_s, *hdr_d;
	char *tmpbuffer;
	xfs_sb_t *sbp;

	sbp = &trans->t_mountp->m_sb;
	tmpbuffer = kmem_alloc(XFS_LBSIZE(sbp), KM_SLEEP);
	ASSERT(tmpbuffer != NULL);
	bcopy(bp->b_un.b_addr, tmpbuffer, XFS_LBSIZE(sbp));
	bzero(bp->b_un.b_addr, XFS_LBSIZE(sbp));

	/*
	 * Copy basic information
	 */
	leaf_s = (struct xfs_dir_leafblock *)tmpbuffer;
	leaf_d = (struct xfs_dir_leafblock *)bp->b_un.b_addr;
	hdr_s = &leaf_s->hdr;
	hdr_d = &leaf_d->hdr;
	hdr_d->info = hdr_s->info;	/* struct copy */
	hdr_d->firstused = XFS_LBSIZE(sbp);
	hdr_d->namebytes = 0;
	hdr_d->count = 0;
	hdr_d->holes = 0;
	hdr_d->freemap[0].base = sizeof(struct xfs_dir_leaf_hdr);
	hdr_d->freemap[0].size = hdr_d->firstused - hdr_d->freemap[0].base;

	/*
	 * Copy all entry's in the same (sorted) order,
	 * but allocate filenames packed and in sequence.
	 */
	xfs_dir_leaf_moveents(leaf_s, 0, leaf_d, 0, (int)hdr_s->count, sbp);

	xfs_trans_log_buf(trans, bp, 0, XFS_LBSIZE(sbp) - 1);

	kmem_free(tmpbuffer, XFS_LBSIZE(sbp));
}

/*
 * Redistribute the directory entries between two leaf nodes,
 * taking into account the size of the new entry.
 *
 * NOTE: if new block is empty, then it will get the upper half of old block.
 */
STATIC void
xfs_dir_leaf_rebalance(struct xfs_dir_state *state,
			      struct xfs_dir_state_blk *blk1,
			      struct xfs_dir_state_blk *blk2)
{
	struct xfs_dir_state_blk *tmp_blk;
	struct xfs_dir_leafblock *leaf1, *leaf2;
	struct xfs_dir_leaf_hdr *hdr1, *hdr2;
	int hightolow, count, totallen;
	int retval, max, swap, tmp;
	xfs_sb_t *sbp;

	/*
	 * Set up environment.
	 */
	ASSERT(blk1->leafblk == 1);
	ASSERT(blk2->leafblk == 1);
	leaf1 = (struct xfs_dir_leafblock *)blk1->bp->b_un.b_addr;
	leaf2 = (struct xfs_dir_leafblock *)blk2->bp->b_un.b_addr;
	ASSERT(leaf1->hdr.info.magic == XFS_DIR_LEAF_MAGIC);
	ASSERT(leaf2->hdr.info.magic == XFS_DIR_LEAF_MAGIC);

	/*
	 * Check ordering of blocks, reverse if it makes things simpler.
	 */
	swap = 0;
	if ((leaf1->hdr.count > 0) && (leaf2->hdr.count > 0) &&
	    (leaf2->leaves[0].hashval < leaf1->leaves[0].hashval)) {
		tmp_blk = blk1;
		blk1 = blk2;
		blk2 = tmp_blk;
		leaf1 = (struct xfs_dir_leafblock *)blk1->bp->b_un.b_addr;
		leaf2 = (struct xfs_dir_leafblock *)blk2->bp->b_un.b_addr;
		swap = 1;
	}
	hdr1 = &leaf1->hdr;
	hdr2 = &leaf2->hdr;

	/*
	 * Examine entries until we reduce the absolute difference in
	 * byte usage between the two blocks to a minimum.  Then get
	 * the direction to copy and the number of elements to move.
	 */
	state->inleaf = xfs_dir_leaf_figure_balance(state, blk1, blk2,
							   &count, &totallen);
	if (count == hdr1->count) {
		goto outahere;
	} else if (count < hdr1->count) {
		count = hdr1->count - count;
		hightolow = 1;
	} else {
		count -= hdr1->count;
		hightolow = 0;
	}

	/*
	 * Figure the total bytes to be added to the destination leaf.
	 */
	tmp  = hdr1->namebytes - totallen;
	tmp  = (tmp < 0) ? -tmp : tmp;
	tmp += count * (sizeof(struct xfs_dir_leaf_name)-1);
	tmp += count * sizeof(struct xfs_dir_leaf_entry);

	sbp = &state->mp->m_sb;
	/*
	 * Move any entries required from leaf to leaf:
	 */
	if (hightolow) {
		/*
		 * leaf2 is the destination, compact it if req'd.
		 */
		max  = hdr2->firstused - sizeof(struct xfs_dir_leaf_hdr);
		max -= hdr2->count * sizeof(struct xfs_dir_leaf_entry);
		if (tmp > max) {
			xfs_dir_leaf_compact(state->trans, blk2->bp);
		}

		/*
		 * Move high entries from leaf1 to low end of leaf2.
		 */
		xfs_dir_leaf_moveents(leaf1, hdr1->count - count,
					     leaf2, 0, count, sbp);
	} else {
		/*
		 * leaf1 is the destination, compact it if req'd.
		 */
		max  = hdr1->firstused - sizeof(struct xfs_dir_leaf_hdr);
		max -= hdr1->count * sizeof(struct xfs_dir_leaf_entry);
		if (tmp > max) {
			xfs_dir_leaf_compact(state->trans, blk1->bp);
		}

		/*
		 * Move low entries from leaf2 to high end of leaf1.
		 */
		xfs_dir_leaf_moveents(leaf2, 0, leaf1, hdr1->count, count, sbp);
	}

	xfs_trans_log_buf(state->trans, blk1->bp, 0, state->blocksize - 1);
	xfs_trans_log_buf(state->trans, blk2->bp, 0, state->blocksize - 1);

outahere:
	/*
	 * Copy out last hashval in each block for B-tree code.
	 */
	blk1->hashval = leaf1->leaves[leaf1->hdr.count-1].hashval;
	blk2->hashval = leaf2->leaves[leaf2->hdr.count-1].hashval;
	if (swap)
		state->inleaf = !state->inleaf;

	/*
	 * Adjust the expected index for insertion.
	 */
	if (state->inleaf) {
		blk1->index = xfs_dir_leaf_refind(leaf1, blk1->index,
							 state->args->hashval,
							 sbp);
	} else {
		blk2->index = blk1->index - leaf1->hdr.count;
		blk2->index = xfs_dir_leaf_refind(leaf2, blk2->index,
							 state->args->hashval,
							 sbp);
	}
}

/*
 * Examine entries until we reduce the absolute difference in
 * byte usage between the two blocks to a minimum.
 * GROT: Is this really necessary?  With other than a 512 byte blocksize,
 * GROT: there will always be enough room in either block for a new entry.
 * GROT: Do a double-split for this case?
 */
STATIC int
xfs_dir_leaf_figure_balance(struct xfs_dir_state *state,
				   struct xfs_dir_state_blk *blk1,
				   struct xfs_dir_state_blk *blk2,
				   int *countarg, int *namebytesarg)
{
	struct xfs_dir_leafblock *leaf1, *leaf2;
	struct xfs_dir_leaf_hdr *hdr1, *hdr2;
	struct xfs_dir_leaf_entry *entry;
	struct xfs_dir_leaf_name *namest;
	int count, max, totallen, half;
	int lastdelta, foundit, tmp;

	/*
	 * Set up environment.
	 */
	leaf1 = (struct xfs_dir_leafblock *)blk1->bp->b_un.b_addr;
	leaf2 = (struct xfs_dir_leafblock *)blk2->bp->b_un.b_addr;
	hdr1 = &leaf1->hdr;
	hdr2 = &leaf2->hdr;
	foundit = 0;

	/*
	 * Examine entries until we reduce the absolute difference in
	 * byte usage between the two blocks to a minimum.
	 */
	max = hdr1->count + hdr2->count;
	half  = (max+1) * (sizeof(*entry)+sizeof(*namest)-1);
	half += hdr1->namebytes + hdr2->namebytes + state->args->namelen;
	half /= 2;
	lastdelta = state->blocksize;
	entry = &leaf1->leaves[0];
	for (count = 0; count < max; entry++, count++) {

#define XFS_DIR_ABS(A)	(((A) < 0) ? -(A) : (A))
		/*
		 * The new entry is in the new block, account for it.
		 */
		if (count == blk1->index) {
			tmp = totallen + sizeof(*entry)
				+ XFS_DIR_LEAF_ENTSIZE_BYNAME(state->args->namelen);
			if (XFS_DIR_ABS(half - tmp) > lastdelta)
				break;
			lastdelta = XFS_DIR_ABS(half - tmp);
			totallen = tmp;
			foundit = 1;
		}

		/*
		 * Wrap around into the second block if necessary.
		 */
		if (count == hdr1->count) {
			leaf1 = leaf2;
			entry = &leaf1->leaves[0];
		}

		/*
		 * Figure out if next leaf entry would be too much.
		 */
		namest = XFS_DIR_LEAF_NAMESTRUCT(leaf1, entry->nameidx);
		tmp = totallen + sizeof(*entry)
				+ XFS_DIR_LEAF_ENTSIZE_BYENTRY(namest);
		if (XFS_DIR_ABS(half - tmp) > lastdelta)
			break;
		lastdelta = XFS_DIR_ABS(half - tmp);
		totallen = tmp;
#undef XFS_DIR_ABS
	}

	/*
	 * Calculate the number of namebytes that will end up in lower block.
	 * If new entry not in lower block, fix up the count.
	 */
	totallen -= count * (sizeof(*entry)+sizeof(*namest)-1);
	if (foundit) {
		totallen -= (sizeof(*entry)+sizeof(*namest)-1) + state->args->namelen;
	}

	*countarg = count;
	*namebytesarg = totallen;
	return(foundit);
}

/*
 * Split the node, rebalance, then add the new entry.
 */
STATIC int
xfs_dir_node_split(struct xfs_dir_state *state,
			  struct xfs_dir_state_blk *oldblk,
			  struct xfs_dir_state_blk *newblk,
			  struct xfs_dir_state_blk *addblk,
			  int treelevel)
{
	struct xfs_dir_intnode *node;
	xfs_fsblock_t blkno;
	int retval;
	xfs_sb_t *sbp;

	node = (struct xfs_dir_intnode *)oldblk->bp->b_un.b_addr;
	ASSERT(node->hdr.info.magic == XFS_DIR_NODE_MAGIC);
	oldblk->index = xfs_dir_node_refind(node, oldblk->index,
						  addblk->hashval);

	/*
	 * Do we have to split the node?
	 */
	retval = 0;
	sbp = &state->mp->m_sb;
	if (node->hdr.count >= XFS_DIR_NODE_ENTRIES(sbp)) {
		/*
		 * Allocate a new node, add to the doubly linked chain of
		 * nodes, then move some of our excess entries into it.
		 * GROT: use a freelist block.
		 */
		retval = xfs_dir_grow_inode(state->trans, state->args,
							  state->blocksize,
							  &blkno);
		if (retval)
			return(retval);	/* GROT: dir is inconsistent */
		
		newblk->bp = xfs_dir_node_create(state->trans, state->args->dp,
							       blkno,
							       treelevel);
		newblk->blkno = blkno;
		xfs_dir_node_rebalance(state, oldblk, newblk,
					      state->args->hashval);
		xfs_dir_blk_link(state, oldblk, newblk);
		retval = -1;
	}

	/*
	 * Insert the new entry into the correct block
	 * (updating last hashval in the process).
	 */
	node = (struct xfs_dir_intnode *)oldblk->bp->b_un.b_addr;
	if (oldblk->index <= node->hdr.count)
		xfs_dir_node_add(state, oldblk, addblk);
	else
		xfs_dir_node_add(state, newblk, addblk);

	return(retval);
}

/*
 * Balance the btree elements between two intermediate nodes,
 * usually one full and one empty.
 *
 * NOTE: if blk2 is empty, then it will get the upper half of blk1.
 */
STATIC void
xfs_dir_node_rebalance(struct xfs_dir_state *state,
			      struct xfs_dir_state_blk *blk1,
			      struct xfs_dir_state_blk *blk2,
			      uint hashval)
{
	struct xfs_dir_intnode *node1, *node2;
	struct xfs_dir_node_entry *btree_s, *btree_d;
	int count, tmp;

	/*
	 * Figure out how many entries need to move, and in which direction.
	 */
	node1 = (struct xfs_dir_intnode *)blk1->bp->b_un.b_addr;
	node2 = (struct xfs_dir_intnode *)blk2->bp->b_un.b_addr;
	ASSERT(node1->hdr.info.magic == XFS_DIR_NODE_MAGIC);
	ASSERT(node2->hdr.info.magic == XFS_DIR_NODE_MAGIC);
	count = (node1->hdr.count - node2->hdr.count) / 2;
	if (count == 0)
		return;

	/*
	 * Swap the leaves around if that makes it simpler.
	 */
	if ((node1->hdr.count > 0) && (node2->hdr.count > 0) &&
	    (node2->btree[0].hashval < node1->btree[0].hashval)) {
		node1 = (struct xfs_dir_intnode *)blk2->bp->b_un.b_addr;
		node2 = (struct xfs_dir_intnode *)blk1->bp->b_un.b_addr;
	}

	/*
	 * Two cases: high-to-low and low-to-high.
	 */
	if (count > 0) {
		/*
		 * Move elements in node2 up to make a hole.
		 */
		if (node2->hdr.count > 0) {
			tmp  = node2->hdr.count;
			tmp *= sizeof(struct xfs_dir_node_entry);
			btree_s = &node2->btree[0];
			btree_d = &node2->btree[count];
			bcopy((char *)btree_s, (char *)btree_d, tmp);
		}

		/*
		 * Move the req'd B-tree elements from high in node1 to
		 * low in node2.
		 */
		node2->hdr.count += count;
		tmp = count * sizeof(struct xfs_dir_node_entry);
		btree_s = &node1->btree[node1->hdr.count - count];
		btree_d = &node2->btree[0];
		bcopy((char *)btree_s, (char *)btree_d, tmp);
		bzero((char *)btree_s, tmp);
		node1->hdr.count -= count;
	} else {
		/*
		 * Move the req'd B-tree elements from low in node2 to
		 * high in node1.
		 */
		count = -count;
		tmp = count * sizeof(struct xfs_dir_node_entry);
		btree_s = &node2->btree[0];
		btree_d = &node1->btree[node1->hdr.count];
		bcopy((char *)btree_s, (char *)btree_d, tmp);
		node1->hdr.count += count;

		/*
		 * Move elements in node2 down to fill the hole.
		 */
		tmp  = node2->hdr.count - count;
		tmp *= sizeof(struct xfs_dir_node_entry);
		btree_s = &node2->btree[count];
		btree_d = &node2->btree[0];
		bcopy((char *)btree_s, (char *)btree_d, tmp);
		node2->hdr.count -= count;

		btree_s = &node2->btree[node2->hdr.count];
		tmp = count * sizeof(struct xfs_dir_node_entry);
		bzero((char *)btree_s, tmp);
	}


	xfs_trans_log_buf(state->trans, blk1->bp, 0, state->blocksize - 1);
	xfs_trans_log_buf(state->trans, blk2->bp, 0, state->blocksize - 1);

	/*
	 * Record the last hashval from each block for upward propagation.
	 */
	node1 = (struct xfs_dir_intnode *)blk1->bp->b_un.b_addr;
	node2 = (struct xfs_dir_intnode *)blk2->bp->b_un.b_addr;
	blk1->hashval = node1->btree[node1->hdr.count-1].hashval;
	blk2->hashval = node2->btree[node2->hdr.count-1].hashval;

	/*
	 * Adjust the expected index for insertion.
	 */
	if (blk1->index < node1->hdr.count) {
		blk1->index = xfs_dir_node_refind(node1, blk1->index, hashval);
	} else {
		blk2->index = blk1->index - node1->hdr.count;
		blk2->index = xfs_dir_node_refind(node2, blk2->index, hashval);
	}
}

/*
 * Add a new entry to an intermediate node.
 */
STATIC void
xfs_dir_node_add(struct xfs_dir_state *state,
			struct xfs_dir_state_blk *oldblk,
			struct xfs_dir_state_blk *newblk)
{
	struct xfs_dir_intnode *node;
	struct xfs_dir_node_entry *btree;
	int tmp;

	node = (struct xfs_dir_intnode *)oldblk->bp->b_un.b_addr;
	ASSERT(node->hdr.info.magic == XFS_DIR_NODE_MAGIC);
	ASSERT((oldblk->index >= 0) && (oldblk->index <= node->hdr.count));
	ASSERT(newblk->blkno != 0);

	/*
	 * We may need to make some room before we insert the new node.
	 */
	oldblk->index = xfs_dir_node_refind(node, oldblk->index,
						  newblk->hashval);
	btree = &node->btree[oldblk->index];
	if (oldblk->index < node->hdr.count) {
		tmp = (node->hdr.count - oldblk->index) * sizeof(*btree);
		bcopy((char *)btree, (char *)(btree+1), tmp);
	}
	btree->hashval = newblk->hashval;
	btree->before = newblk->blkno;
	node->hdr.count++;

	/*
	 * Copy the last hash value from the oldblk to propagate upwards.
	 */
	oldblk->hashval = node->btree[node->hdr.count-1].hashval;
	xfs_trans_log_buf(state->trans, oldblk->bp, 0, state->blocksize - 1);
}

/*========================================================================
 * Routines used for shrinking the Btree.
 *========================================================================*/

/*
 * Deallocate an empty leaf node, remove it from its parent,
 * possibly deallocating that block, etc...
 */
void
xfs_dir_join(struct xfs_dir_state *state)
{
	struct xfs_dir_state_blk *drop_blk, *save_blk;
	struct xfs_dir_intnode *node;
	uint lasthash;
	int retval, tmp, i;

	drop_blk = &state->path.blk[ state->path.active-1 ];
	save_blk = &state->altpath.blk[ state->path.active-1 ];
	ASSERT(drop_blk->leafblk == 1);

	/*
	 * Walk back up the tree joining/deallocating as necessary.
	 * When we stop dropping blocks, break out.
	 */
	for (i = state->path.active-1; i >= 0; drop_blk--, save_blk--, i--) {
		/*
		 * Remove the offending entry and fixup hashvals.
		 */
		if (drop_blk->leafblk)
			xfs_dir_leaf_remove(state->trans, drop_blk->bp,
							  drop_blk->index);
		else
			xfs_dir_node_remove(state, drop_blk);
		xfs_dir_fixhashpath(state, &state->path, i);

		/*
		 * See if we can combine the block with a neighbor.
		 */
		switch(xfs_dir_blk_toosmall(state, i)) {
		case 0:				/* no options, just leave */
			goto out;
		case 1:				/* coalesce, then unlink */
			if (drop_blk->leafblk)
				xfs_dir_leaf_unbalance(state, drop_blk,
							      save_blk);
			else
				xfs_dir_node_unbalance(state, drop_blk,
							      save_blk);
			break;
		case 2:				 /* block empty, unlink it */
			break;
		}
		xfs_dir_blk_unlink(state, drop_blk, save_blk);
		xfs_dir_fixhashpath(state, &state->altpath, i);
	}
out:
	if (i <= 0) {
		/*
		 * We joined all the way to the top.  If we only have one
		 * entry in the root, make the child block the new root.
		 */
		drop_blk = &state->path.blk[0];
		ASSERT(drop_blk->bp->b_un.b_addr == 0);
		ASSERT(drop_blk->blkno == 0);
		ASSERT(drop_blk->leafblk == 0);
		xfs_dir_root_join(state, drop_blk);
	}
}

/*
 * We have only one entry in the root.  Copy the only remaining child of
 * the old root to block 0 as the new root node, but copy the freelist
 * from the old root before overwriting it.
 */
STATIC int
xfs_dir_root_join(struct xfs_dir_state *state,
			 struct xfs_dir_state_blk *drop_blk)
{
	struct xfs_dir_intnode *oldroot, *newroot;
	struct xfs_dir_leafblock *leaf;
	xfs_fsblock_t blkno;
	int retval;
	buf_t *bp;
	xfs_sb_t *sbp;

	retval = 0;
	oldroot = (struct xfs_dir_intnode *)drop_blk->bp->b_un.b_addr;
	ASSERT(oldroot->hdr.info.magic == XFS_DIR_NODE_MAGIC);
	if (oldroot->hdr.count > 1)
		return(0);

	blkno = oldroot->btree[ oldroot->hdr.count-1 ].before;
	bp = xfs_dir_read_buf(state->trans, state->args->dp, blkno);
	if (oldroot->hdr.level == 1) {
		leaf = (struct xfs_dir_leafblock *)bp->b_un.b_addr;
		ASSERT(leaf->hdr.info.magic == XFS_DIR_LEAF_MAGIC);
		ASSERT(leaf->hdr.info.forw == 0);
		ASSERT(leaf->hdr.info.back == 0);
		bcopy(bp->b_un.b_addr, drop_blk->bp->b_un.b_addr, state->blocksize);
		xfs_trans_log_buf(state->trans, drop_blk->bp, 0,
						state->blocksize - 1);
		sbp = &state->mp->m_sb;
		retval = xfs_dir_shrink_inode(state->trans, state->args,
					      sbp->sb_inodesize - state->blocksize,
					      &blkno);
	} else {
		ASSERT(oldroot->hdr.info.forw == 0);
		ASSERT(oldroot->hdr.info.back == 0);
		newroot = (struct xfs_dir_intnode *)bp->b_un.b_addr;
		ASSERT(newroot->hdr.info.magic == XFS_DIR_NODE_MAGIC);
#ifdef GROT
		newroot->hdr.freecount = oldroot->hdr.freecount;
		newroot->hdr.freechain = oldroot->hdr.freechain;
#endif
		bcopy(bp->b_un.b_addr, drop_blk->bp->b_un.b_addr, state->blocksize);
#ifdef GROT
		xfs_dir_node_addfreelist(state, drop_blk);
#endif
		xfs_trans_log_buf(state->trans, drop_blk->bp, 0,
						state->blocksize - 1);
	}
	return(retval);
}

/*
 * Check a leaf block and its neighbors to see if the block should be
 * collapsed into one or the other neighbor.  If so, fill in the state
 * structure and return 1.  Always keep the leaf with the smaller
 * block number.  If the block is empty, don't check siblings, return 2.
 * If nothing can be done, return 0.
 */
STATIC int
xfs_dir_blk_toosmall(struct xfs_dir_state *state, int level)
{
	struct xfs_dir_leafblock *leaf, *tmp_leaf;
	struct xfs_dir_intnode *node, *tmp_node;
	struct xfs_dir_state_blk *blk;
	struct xfs_dir_blkinfo *info;
	int count, bytes, i;
	xfs_fsblock_t blkno[2];
	uint lasthash;
	buf_t *bp;
	xfs_sb_t *sbp;

	blk = &state->path.blk[ level ];
	info = (struct xfs_dir_blkinfo *)blk->bp->b_un.b_addr;
	ASSERT((info->magic == XFS_DIR_NODE_MAGIC) ||
	       (info->magic == XFS_DIR_LEAF_MAGIC));

	/*
	 * Look preferentially at coalescing with the lower numbered sibling.
	 * This will shrink a directory over time.
	 */
	if (info->back < info->forw) {
		blkno[0] = info->back;
		blkno[1] = info->forw;
	} else {
		blkno[0] = info->forw;
		blkno[1] = info->back;
	}

	/*
	 * Check for the degenerate case of the block being empty.
	 * If the block is empty, we'll simply delete it, no need to
	 * coalesce it with a sibling block.
	 */
	if (blk->leafblk) {
		leaf = (struct xfs_dir_leafblock *)blk->bp->b_un.b_addr;
		ASSERT(leaf->hdr.info.magic == XFS_DIR_LEAF_MAGIC);
		count = leaf->hdr.count;
	} else {
		node = (struct xfs_dir_intnode *)blk->bp->b_un.b_addr;
		ASSERT(node->hdr.info.magic == XFS_DIR_NODE_MAGIC);
		count = node->hdr.count;
	}
	if (count == 0) {
		if (blkno[0] != 0)
			i = 0;
		else if (blkno[1] != 0)
			i = 1;
		else
			return(0);

		/*
		 * Fill the alternate path with this block's sibling info.
		 */
		bp = xfs_dir_read_buf(state->trans, state->args->dp, blkno[i]);
		if (blk->leafblk) {
			tmp_leaf = (struct xfs_dir_leafblock *)bp->b_un.b_addr;
			ASSERT(tmp_leaf->hdr.info.magic == XFS_DIR_LEAF_MAGIC);
			lasthash = tmp_leaf->leaves[ tmp_leaf->hdr.count-1 ].hashval;
		} else {
			tmp_node = (struct xfs_dir_intnode *)bp->b_un.b_addr;
			ASSERT(tmp_node->hdr.info.magic == XFS_DIR_NODE_MAGIC);
			lasthash = tmp_node->btree[ tmp_node->hdr.count-1 ].hashval;
		}
		xfs_dir_findpath(state, &state->altpath, lasthash, blkno[i]);
		return(2);
	}

	/*
	 * Check to see if coalescing is a good idea.
	 */
	for (i = 0; i < 2; i++) {
		if (blkno[i] == 0)
			continue;
		bp = xfs_dir_read_buf(state->trans, state->args->dp, blkno[i]);

		if (blk->leafblk) {
			tmp_leaf = (struct xfs_dir_leafblock *)bp->b_un.b_addr;
			ASSERT(tmp_leaf->hdr.info.magic == XFS_DIR_LEAF_MAGIC);
			count = leaf->hdr.count + tmp_leaf->hdr.count;
			bytes  = state->blocksize - (state->blocksize>>2);
			bytes -= leaf->hdr.namebytes;
			bytes -= tmp_leaf->hdr.namebytes;
			bytes -= count * (sizeof(struct xfs_dir_leaf_name) - 1);
			bytes -= count * sizeof(struct xfs_dir_leaf_entry);
			bytes -= sizeof(struct xfs_dir_leaf_hdr);
			if (bytes >= 0) {
				lasthash = tmp_leaf->leaves[ tmp_leaf->hdr.count-1 ].hashval;
				break;	/* fits with at least 25% to spare */
			}
		} else {
			tmp_node = (struct xfs_dir_intnode *)bp->b_un.b_addr;
			ASSERT(tmp_node->hdr.info.magic == XFS_DIR_NODE_MAGIC);
			sbp = &state->mp->m_sb;
			count  = XFS_DIR_NODE_ENTRIES(sbp);
			count -= XFS_DIR_NODE_ENTRIES(sbp) >> 2;
			count -= node->hdr.count + tmp_node->hdr.count;
			if (count >= 0) {
				lasthash = tmp_node->btree[ tmp_node->hdr.count-1 ].hashval;
				break;	/* fits with at least 25% to spare */
			}
		}

		xfs_trans_brelse(state->trans, bp);
	}
	if (i >= 2)
		return(0);

	/*
	 * Find the path to the new block from the root, but always keep the
	 * lower numbered directory block; compact a directory over time.
	 */
	if (blkno[i] < blk->blkno) {
		xfs_dir_findpath(state, &state->altpath, lasthash, blkno[i]);
	} else {
		bcopy(&state->path, &state->altpath, sizeof(state->path));
		xfs_dir_findpath(state, &state->path, lasthash, blkno[i]);
	}
	return(1);
}

/*
 * Walk back up the tree adjusting hash values as necessary.
 * When we stop making changes, return.
 * GROT: assumes hashvals are unique. Find old-hashval/blkno pair, then update.
 */
void
xfs_dir_fixhashpath(struct xfs_dir_state *state,
			   struct xfs_dir_state_path *path,
			   int level)
{
	struct xfs_dir_state_blk *blk;
	struct xfs_dir_intnode *node;
	struct xfs_dir_node_entry *btree;
	struct xfs_dir_leafblock *leaf;
	uint lasthash;

	blk = &path->blk[level];
	if (blk->leafblk) {
		leaf = (struct xfs_dir_leafblock *)blk->bp->b_un.b_addr;
		ASSERT(leaf->hdr.info.magic == XFS_DIR_LEAF_MAGIC);
		if (leaf->hdr.count == 0)
			return;
		lasthash = leaf->leaves[ leaf->hdr.count-1 ].hashval;
	} else {
		node = (struct xfs_dir_intnode *)blk->bp->b_un.b_addr;
		ASSERT(node->hdr.info.magic == XFS_DIR_NODE_MAGIC);
		if (node->hdr.count == 0)
			return;
		lasthash = node->btree[ node->hdr.count-1 ].hashval;
	}
	for (blk--, level--; level >= 0; blk--, level--) {
		node = (struct xfs_dir_intnode *)blk->bp->b_un.b_addr;
		ASSERT(node->hdr.info.magic == XFS_DIR_NODE_MAGIC);
		btree = &node->btree[blk->index];
		if (btree->hashval == lasthash)
			break;
		blk->hashval = btree->hashval = lasthash;
		xfs_trans_log_buf(state->trans, blk->bp,
					 XFS_DIR_LOGSTART(node, btree),
					 XFS_DIR_LOGSTART(node, btree) +
					 XFS_DIR_LOGSIZE(btree->hashval) - 1);

		lasthash = node->btree[ node->hdr.count-1 ].hashval;
	}
}

/*
 * Remove a name from the leaf directory structure.
 */
void
xfs_dir_leaf_remove(xfs_trans_t *trans, buf_t *bp, int index)
{
	struct xfs_dir_leafblock *leaf;
	struct xfs_dir_leaf_hdr *hdr;
	struct xfs_dir_leaf_map *map;
	struct xfs_dir_leaf_entry *entry;
	struct xfs_dir_leaf_name *namest;
	int before, after, smallest, entsize;
	int tablesize, tmp, i;
	xfs_sb_t *sbp;

	leaf = (struct xfs_dir_leafblock *)bp->b_un.b_addr;
	ASSERT(leaf->hdr.info.magic == XFS_DIR_LEAF_MAGIC);
	hdr = &leaf->hdr;
	sbp = &trans->t_mountp->m_sb;
	ASSERT((hdr->count > 0) && (hdr->count < (XFS_LBSIZE(sbp)/8)));
	ASSERT((index >= 0) && (index < hdr->count));
	ASSERT(hdr->firstused >= ((hdr->count*sizeof(*entry))+sizeof(*hdr)));
	entry = &leaf->leaves[index];
	ASSERT(entry->nameidx >= hdr->firstused);
	ASSERT(entry->nameidx < XFS_LBSIZE(sbp));

	/*
	 * Scan through free region table:
	 *    check for adjacency of free'd entry with an existing one,
	 *    find smallest free region in case we need to replace it,
	 *    adjust any map that borders the entry table,
	 */
	tablesize = hdr->count * sizeof(struct xfs_dir_leaf_entry)
			+ sizeof(struct xfs_dir_leaf_hdr);
	map = &hdr->freemap[0];
	tmp = map->size;
	before = after = -1;
	smallest = XFS_DIR_LEAF_MAPSIZE - 1;
	namest = XFS_DIR_LEAF_NAMESTRUCT(leaf, entry->nameidx);
	entsize = XFS_DIR_LEAF_ENTSIZE_BYENTRY(namest);
	for (i = 0; i < XFS_DIR_LEAF_MAPSIZE; map++, i++) {
		ASSERT(map->base < XFS_LBSIZE(sbp));
		ASSERT(map->size < XFS_LBSIZE(sbp));
		if (map->base == tablesize) {
			map->base -= sizeof(struct xfs_dir_leaf_entry);
			map->size += sizeof(struct xfs_dir_leaf_entry);
		}

		if ((map->base + map->size) == entry->nameidx) {
			before = i;
		} else if (map->base == (entry->nameidx + entsize)) {
			after = i;
		} else if (map->size < tmp) {
			tmp = map->size;
			smallest = i;
		}
	}

	/*
	 * Coalesce adjacent freemap regions,
	 * or replace the smallest region.
	 */
	if ((before >= 0) || (after >= 0)) {
		if ((before >= 0) && (after >= 0)) {
			map = &hdr->freemap[before];
			map->size += entsize;
			map->size += hdr->freemap[after].size;
			hdr->freemap[after].base = 0;
			hdr->freemap[after].size = 0;
		} else if (before >= 0) {
			map = &hdr->freemap[before];
			map->size += entsize;
		} else {
			map = &hdr->freemap[after];
			map->base = entry->nameidx;
			map->size += entsize;
		}
	} else {
		/*
		 * Replace smallest region (if it is smaller than free'd entry)
		 */
		map = &hdr->freemap[smallest];
		if (map->size < entsize) {
			map->base = entry->nameidx;
			map->size = entsize;
		}
	}

	/*
	 * Did we remove the first entry?
	 */
	if (entry->nameidx == hdr->firstused)
		smallest = 1;
	else
		smallest = 0;

	/*
	 * Compress the remaining entries and zero out the removed stuff.
	 */
	tmp = (hdr->count - index) * sizeof(struct xfs_dir_leaf_entry);
	bcopy((char *)(entry+1), (char *)entry, tmp);
	hdr->count--;
	entry = &leaf->leaves[hdr->count];
	bzero((char *)entry, sizeof(struct xfs_dir_leaf_entry));
	hdr->namebytes -= namest->namelen;
	bzero((char *)namest, entsize);

	/*
	 * If we removed the first entry, re-find the first used byte
	 * in the name area.  Note that if the entry was the "firstused",
	 * then we don't have a "hole" in our block resulting from
	 * removing the name.
	 */
	if (smallest) {
		tmp = XFS_LBSIZE(sbp);
		entry = &leaf->leaves[0];
		for (i = hdr->count-1; i >= 0; entry++, i--) {
			ASSERT(entry->nameidx >= hdr->firstused);
			ASSERT(entry->nameidx < XFS_LBSIZE(sbp));
			if (entry->nameidx < tmp)
				tmp = entry->nameidx;
		}
		hdr->firstused = tmp;
	} else {
		hdr->holes = 1;		/* mark as needing compaction */
	}

	xfs_trans_log_buf(trans, bp, 0, XFS_LBSIZE(sbp) - 1);
}

/*
 * Move all the directory entries from drop_leaf into save_leaf.
 */
STATIC void
xfs_dir_leaf_unbalance(struct xfs_dir_state *state,
			      struct xfs_dir_state_blk *drop_blk,
			      struct xfs_dir_state_blk *save_blk)
{
	struct xfs_dir_leafblock *drop_leaf, *save_leaf, *tmp_leaf;
	struct xfs_dir_leaf_hdr *drop_hdr, *save_hdr, *tmp_hdr;
	char *tmpbuffer;
	xfs_sb_t *sbp;

	/*
	 * Set up environment.
	 */
	ASSERT(drop_blk->leafblk == 1);
	ASSERT(save_blk->leafblk == 1);
	drop_leaf = (struct xfs_dir_leafblock *)drop_blk->bp->b_un.b_addr;
	save_leaf = (struct xfs_dir_leafblock *)save_blk->bp->b_un.b_addr;
	ASSERT(drop_leaf->hdr.info.magic == XFS_DIR_LEAF_MAGIC);
	ASSERT(save_leaf->hdr.info.magic == XFS_DIR_LEAF_MAGIC);
	drop_hdr = &drop_leaf->hdr;
	save_hdr = &save_leaf->hdr;

	/*
	 * Save last hashval from dying block for later Btree fixup.
	 */
	drop_blk->hashval = drop_leaf->leaves[drop_leaf->hdr.count-1].hashval;

	sbp = &state->mp->m_sb;
	/*
	 * Check if we need a temp buffer, or can we do it in place.
	 * Note that we don't check leaf for holes because we will
	 * always be dropping it, toosmall() decided that for us already.
	 */
	if (save_hdr->holes == 0) {
		/*
		 * dest leaf has no holes, so we add there.  May need
		 * to make some room in the entry array.
		 */
		if (drop_leaf->leaves[0].hashval < save_leaf->leaves[0].hashval) {
			xfs_dir_leaf_moveents(drop_leaf, 0, save_leaf, 0,
						 (int)drop_hdr->count, sbp);
		} else {
			xfs_dir_leaf_moveents(drop_leaf, 0,
					      save_leaf, save_hdr->count,
					      (int)drop_hdr->count, sbp);
		}
	} else {
		/*
		 * Destination has holes, so we make a temporary copy
		 * of the leaf and add them both to that.
		 */
		tmpbuffer = kmem_alloc(state->blocksize, KM_SLEEP);
		ASSERT(tmpbuffer != NULL);
		bzero(tmpbuffer, state->blocksize);
		tmp_leaf = (struct xfs_dir_leafblock *)tmpbuffer;
		tmp_hdr = &tmp_leaf->hdr;
		tmp_hdr->info = save_hdr->info;	/* struct copy */
		tmp_hdr->count = 0;
		tmp_hdr->firstused = state->blocksize;
		tmp_hdr->namebytes = 0;
		if (drop_leaf->leaves[0].hashval < save_leaf->leaves[0].hashval) {
			xfs_dir_leaf_moveents(drop_leaf, 0, tmp_leaf, 0,
						 (int)drop_hdr->count, sbp);
			xfs_dir_leaf_moveents(save_leaf, 0,
					      tmp_leaf, tmp_leaf->hdr.count,
					      (int)save_hdr->count, sbp);
		} else {
			xfs_dir_leaf_moveents(save_leaf, 0, tmp_leaf, 0,	
						 (int)save_hdr->count, sbp);
			xfs_dir_leaf_moveents(drop_leaf, 0,
					      tmp_leaf, tmp_leaf->hdr.count,
					      (int)drop_hdr->count, sbp);
		}
		bcopy((char *)tmp_leaf, (char *)save_leaf, state->blocksize);
		kmem_free(tmpbuffer, state->blocksize);
	}

	xfs_trans_log_buf(state->trans, save_blk->bp, 0, state->blocksize - 1);

	/*
	 * Copy out last hashval in each block for B-tree code.
	 */
	save_blk->hashval = save_leaf->leaves[save_leaf->hdr.count-1].hashval;
}

/*
 * Remove an entry from an intermediate node.
 */
STATIC void
xfs_dir_node_remove(struct xfs_dir_state *state,
			   struct xfs_dir_state_blk *drop_blk)
{
	struct xfs_dir_intnode *node;
	struct xfs_dir_node_entry *btree;
	int tmp;

	node = (struct xfs_dir_intnode *)drop_blk->bp->b_un.b_addr;
	ASSERT(drop_blk->index <= node->hdr.count);
	ASSERT(drop_blk->index >= 0);

	/*
	 * Copy over the offending entry, or just zero it out.
	 */
	btree = &node->btree[drop_blk->index];
	if (drop_blk->index < node->hdr.count) {
		tmp  = node->hdr.count - drop_blk->index - 1;
		tmp *= sizeof(struct xfs_dir_node_entry);
		bcopy((char *)(btree+1), (char *)btree, tmp);
		btree = &node->btree[node->hdr.count-1];
	}
	bzero((char *)btree, sizeof(struct xfs_dir_node_entry));
	node->hdr.count--;
#ifdef GROT
	xfs_dir_node_addfreelist(state, nilner);
#endif

	/*
	 * Copy the last hash value from the block to propagate upwards.
	 */
	btree--;
	drop_blk->hashval = btree->hashval;

	xfs_trans_log_buf(state->trans, drop_blk->bp, 0, state->blocksize - 1);
}

/*
 * Unbalance the btree elements between two intermediate nodes,
 * move all Btree elements from one node into another.
 */
STATIC void
xfs_dir_node_unbalance(struct xfs_dir_state *state,
			      struct xfs_dir_state_blk *drop_blk,
			      struct xfs_dir_state_blk *save_blk)
{
	struct xfs_dir_intnode *drop_node, *save_node;
	struct xfs_dir_node_entry *btree;
	int tmp;

	drop_node = (struct xfs_dir_intnode *)drop_blk->bp->b_un.b_addr;
	save_node = (struct xfs_dir_intnode *)save_blk->bp->b_un.b_addr;
	ASSERT(drop_node->hdr.info.magic == XFS_DIR_NODE_MAGIC);
	ASSERT(save_node->hdr.info.magic == XFS_DIR_NODE_MAGIC);

	/*
	 * If the dying block has lower hashvals, then move all the
	 * elements in the remaining block up to make a hole.
	 */
	if (drop_node->btree[0].hashval < save_node->btree[0].hashval) {
		btree = &save_node->btree[drop_node->hdr.count];
		tmp = save_node->hdr.count * sizeof(struct xfs_dir_node_entry);
		bcopy((char *)&save_node->btree[0], (char *)btree, tmp);
		btree = &save_node->btree[0];
	} else {
		btree = &save_node->btree[save_node->hdr.count];
	}

	/*
	 * Move all the B-tree elements from drop_blk to save_blk.
	 */
	tmp = drop_node->hdr.count * sizeof(struct xfs_dir_node_entry);
	bcopy((char *)&drop_node->btree[0], (char *)btree, tmp);
	save_node->hdr.count += drop_node->hdr.count;

	xfs_trans_log_buf(state->trans, save_blk->bp, 0, state->blocksize - 1);

	/*
	 * Save the last hashval in the remaining block for upward propagation.
	 */
	save_blk->hashval = save_node->btree[save_node->hdr.count-1].hashval;
}

/*========================================================================
 * Routines used for finding things in the Btree.
 *========================================================================*/

/*
 * Look up a name in a leaf directory structure.
 * This is the internal routine, it uses the caller's buffer.
 *
 * Note that duplicate keys are allowed, but only within a single
 * leaf node.  The Btree split operation must know to keep entries
 * with the same key in the same leaf node.
 *
 * Return in *index the index into the entry[] array of either the found
 * entry, or where the entry should have been (insert before that entry).
 *
 * Don't change the args->inumber unless we find the filename.
 *
 * GROT: what about duplicate hashval's being split into two nodes?
 */
int
xfs_dir_leaf_lookup_int(buf_t *bp, struct xfs_dir_name *args, int *index)
{
	struct xfs_dir_leafblock *leaf;
	struct xfs_dir_leaf_entry *entry;
	struct xfs_dir_leaf_name *namest;
	int probe, span, i;
	xfs_sb_t *sbp;

	leaf = (struct xfs_dir_leafblock *)bp->b_un.b_addr;
	ASSERT(leaf->hdr.info.magic == XFS_DIR_LEAF_MAGIC);
	sbp = &args->dp->i_mount->m_sb;
	ASSERT((leaf->hdr.count >= 0) && (leaf->hdr.count < (XFS_LBSIZE(sbp)/8)));

	/*
	 * Only bother with a binary search if more than a few entries.
	 * GROT: should structure this so that when we get down to a list of
	 * GROT: less that 16 elements, just drop into a sequential search.
	 */
	if (leaf->hdr.count < 64) {
		/*
		 * Linear search...
		 */
		entry = &leaf->leaves[0];
		for (i = 0; i < leaf->hdr.count; entry++, i++) {
			if (entry->hashval > args->hashval)
				break;
			if (entry->hashval == args->hashval) {
				namest = XFS_DIR_LEAF_NAMESTRUCT(leaf,
							     entry->nameidx);
				if ((namest->namelen == args->namelen) &&
				    (bcmp(args->name, namest->name,
						      args->namelen) == 0)) {
					*index = i;
					bcopy(namest->inumber,
					      (char *)&args->inumber,
					      sizeof(xfs_ino_t));
					return(EEXIST);
				}
			}
		}
		*index = i;
		return(ENOENT);
	}

	/*
	 * Binary search...
	 */
	probe = span = leaf->hdr.count / 2;
	do {
		span /= 2;
		entry = &leaf->leaves[probe];
		if (entry->hashval < args->hashval)
			probe += span;
		else if (entry->hashval > args->hashval)
			probe -= span;
		else
			break;
	} while (span > 4);
	entry = &leaf->leaves[probe];

	/*
	 * Binary search on a random number of elements will only get
	 * you close, so we must search around a bit more by hand.
	 */
	while ((probe > 0) && (entry->hashval > args->hashval)) {
		entry--;
		probe--;
	}
	while ((probe < leaf->hdr.count) && (entry->hashval < args->hashval)) {
		entry++;
		probe++;
	}
	if ((probe == leaf->hdr.count) || (entry->hashval != args->hashval)) {
		*index = probe;
		return(ENOENT);
	}

	/*
	 * Duplicate keys may be present, so search all of them for a match.
	 */
	while ((probe >= 0) && (entry->hashval == args->hashval)) {
		entry--;
		probe--;
	}
	entry++;			/* loop left us 1 below last match */
	probe++;
	while ((probe < leaf->hdr.count) && (entry->hashval == args->hashval)) {
		namest = XFS_DIR_LEAF_NAMESTRUCT(leaf, entry->nameidx);
		if ((namest->namelen == args->namelen) &&
		    (bcmp(args->name, namest->name, args->namelen) == 0)) {
			bcopy(namest->inumber, (char *)&args->inumber,
					       sizeof(xfs_ino_t));
			*index = probe;
			return(EEXIST);
		}
		entry++;
		probe++;
	}
	*index = probe;
	return(ENOENT);
}

/*
 * Walk down the Btree looking for a particular filename, filling
 * in the state structure as we go.
 *
 * We will set the state structure to point to each of the elements
 * in each of the nodes where either the hashval is or should be.
 * GROT: what about multiple intermediate nodes with the same hashval?
 */
int
xfs_dir_node_lookup_int(struct xfs_dir_state *state)
{
	struct xfs_dir_state_blk *blk;
	int retval, i;

	/*
	 * Descend thru the B-tree searching each level for the
	 * right node to use, until a leaf node is found.
	 */
	xfs_dir_findpath(state, &state->path, state->args->hashval,
				XFS_DIR_MAXBLK);
	if (state->path.active >= XFS_DIR_NODE_MAXDEPTH)
		return(EFBIG);

	/*
	 * Read up the leaf node and have it searched,
	 * returning the inode number if we find the name of interest.
	 */
	blk = &state->path.blk[ state->path.active-1 ];
	ASSERT(blk->leafblk == 1);
	retval = xfs_dir_leaf_lookup_int(blk->bp, state->args, &blk->index);
	return(retval);
}

/*
 * Find a particular hashval or blkno in the tree.
 * GROT: assumes hashvals are unique. Find old-hashval/blkno pair, then update.
 */
STATIC void
xfs_dir_findpath(struct xfs_dir_state *state,
			struct xfs_dir_state_path *path,
			uint hashval, xfs_fsblock_t find_blkno)
{
	struct xfs_dir_state_blk *blk;
	struct xfs_dir_blkinfo *info;
	struct xfs_dir_leafblock *leaf;
	struct xfs_dir_intnode *node;
	struct xfs_dir_node_entry *btree;
	int active, max, i;
	xfs_fsblock_t cur_blkno;

	/*
	 * Descend thru the B-tree searching each level for the right
	 * node to use, until the right blkno or hashval is found.
	 */
	cur_blkno = 0;
	bzero((char *)path, sizeof(*path));
	for (blk = &path->blk[0], active = 1;
			  active <= XFS_DIR_NODE_MAXDEPTH;
			  blk++, active++) {
		/*
		 * Read the next node down in the tree.
		 */
		blk->bp = xfs_dir_read_buf(state->trans, state->args->dp,
							 cur_blkno);
		blk->blkno = cur_blkno;

		/*
		 * Do the right thing depending on block type.
		 */
		info = (struct xfs_dir_blkinfo *)blk->bp->b_un.b_addr;
		switch (info->magic) {
		case XFS_DIR_LEAF_MAGIC:
			leaf = (struct xfs_dir_leafblock *)blk->bp->b_un.b_addr;
			blk->hashval = leaf->leaves[leaf->hdr.count-1].hashval;
			blk->leafblk = 1;
			goto out;

		case XFS_DIR_NODE_MAGIC:
			node = (struct xfs_dir_intnode *)blk->bp->b_un.b_addr;
			blk->hashval = node->btree[node->hdr.count-1].hashval;
			blk->leafblk = 0;
			if (cur_blkno == find_blkno)
				goto out;

			/*
			 * Do a linear search for now, should be binary.
			 * GROT: sequential when we have < 16 elements left.
			 */
			max = node->hdr.count;
			btree = &node->btree[0];
			for (i = 0; i < max; btree++, i++) {
				if (btree->hashval >= hashval) {
					cur_blkno = btree->before;	
					blk->index = i;
					break;
				}
			}
			if (i == max) {
				blk->index = max-1;
				cur_blkno = node->btree[ max-1 ].before;
			}
			break;

		default:
			ASSERT(0);
		}
	}

out:
	path->active = active;
}

/*========================================================================
 * Utility routines.
 *========================================================================*/

/*
 * Move the indicated entries from one leaf to another.
 * NOTE: this routine modifies both source and destination leaves.
 */
STATIC void
xfs_dir_leaf_moveents(struct xfs_dir_leafblock *leaf_s, int start_s,
			     struct xfs_dir_leafblock *leaf_d, int start_d,
			     int count, xfs_sb_t *sbp)
{
	struct xfs_dir_leaf_hdr *hdr_s, *hdr_d;
	struct xfs_dir_leaf_entry *entry_s, *entry_d;
	struct xfs_dir_leaf_name *namest_s, *namest_d;
	int tmp, i;

	/*
	 * Check for nothing to do.
	 */
	if (count == 0)
		return;

	/*
	 * Set up environment.
	 */
	ASSERT(leaf_s->hdr.info.magic == XFS_DIR_LEAF_MAGIC);
	ASSERT(leaf_d->hdr.info.magic == XFS_DIR_LEAF_MAGIC);
	hdr_s = &leaf_s->hdr;
	hdr_d = &leaf_d->hdr;
	ASSERT((hdr_s->count > 0) && (hdr_s->count < (XFS_LBSIZE(sbp)/8)));
	ASSERT(hdr_s->firstused >= ((hdr_s->count*sizeof(*entry_s))+sizeof(*hdr_s)));
	ASSERT((hdr_d->count >= 0) && (hdr_d->count < (XFS_LBSIZE(sbp)/8)));
	ASSERT(hdr_d->firstused >= ((hdr_d->count*sizeof(*entry_d))+sizeof(*hdr_d)));

	ASSERT(start_s < hdr_s->count);
	ASSERT(start_d <= hdr_d->count);
	ASSERT(count <= hdr_s->count);

	/*
	 * Move the entries in the destination leaf up to make a hole?
	 */
	if (start_d < hdr_d->count) {
		tmp  = hdr_d->count - start_d;
		tmp *= sizeof(struct xfs_dir_leaf_entry);
		entry_s = &leaf_d->leaves[start_d];
		entry_d = &leaf_d->leaves[start_d + count];
		bcopy((char *)entry_s, (char *)entry_d, tmp);
	}

	/*
	 * Copy all entry's in the same (sorted) order,
	 * but allocate filenames packed and in sequence.
	 */
	entry_s = &leaf_s->leaves[start_s];
	entry_d = &leaf_d->leaves[start_d];
	for (i = 0; i < count; entry_s++, entry_d++, i++) {
		ASSERT(entry_s->nameidx >= hdr_s->firstused);
		ASSERT(entry_s->nameidx < XFS_LBSIZE(sbp));
		namest_s = XFS_DIR_LEAF_NAMESTRUCT(leaf_s, entry_s->nameidx);
		ASSERT(namest_s->namelen < MAXNAMELEN);
		tmp = XFS_DIR_LEAF_ENTSIZE_BYENTRY(namest_s);
		hdr_d->firstused -= tmp;
		entry_d->hashval = entry_s->hashval;
		entry_d->nameidx = hdr_d->firstused;
		namest_d = XFS_DIR_LEAF_NAMESTRUCT(leaf_d, entry_d->nameidx);
		bcopy((char *)namest_s, (char *)namest_d, tmp);
		bzero((char *)namest_s, tmp);
		hdr_s->namebytes -= namest_d->namelen;
		hdr_d->namebytes += namest_d->namelen;
		hdr_s->count--;
		hdr_d->count++;
	}

	/*
	 * Zero out the entries we just copied.
	 */
	if (start_s == hdr_s->count) {
		tmp = count * sizeof(struct xfs_dir_leaf_entry);
		entry_s = &leaf_s->leaves[start_s];
		bzero((char *)entry_s, tmp);
	} else {
		/*
		 * Move the remaining entries down to fill the hole,
		 * then zero the entries at the top.
		 */
		tmp  = hdr_s->count - count;
		tmp *= sizeof(struct xfs_dir_leaf_entry);
		entry_s = &leaf_s->leaves[start_s + count];
		entry_d = &leaf_s->leaves[start_s];
		bcopy((char *)entry_s, (char *)entry_d, tmp);

		tmp = count * sizeof(struct xfs_dir_leaf_entry);
		entry_s = &leaf_s->leaves[hdr_s->count];
		bzero((char *)entry_s, tmp);
	}

	/*
	 * Fill in the freemap information
	 */
	hdr_d->freemap[0].base = hdr_d->count*sizeof(struct xfs_dir_leaf_entry);
	hdr_d->freemap[0].base += sizeof(struct xfs_dir_leaf_hdr);
	hdr_d->freemap[0].size = hdr_d->firstused - hdr_d->freemap[0].base;
	hdr_s->holes = 1;	/* leaf may not be compact */
}

/*
 * (Re)find the correct insertion point in a leaf for a hashval.
 * GROT: allow multiple duplicate hashvals.
 */
STATIC int
xfs_dir_leaf_refind(struct xfs_dir_leafblock *leaf, int start, uint hashval,
			xfs_sb_t *sbp)
{
	struct xfs_dir_leaf_entry *entry;
	int max, i;

	/*
	 * Walk upward and downward from starting point,
	 * looking for the right place for hashval.
	 */
	ASSERT(leaf->hdr.info.magic == XFS_DIR_LEAF_MAGIC);
	max = leaf->hdr.count - 1;
	if (max < 0)
		return(0);
	if (start > max)
		start = max;
	entry = &leaf->leaves[start];
	if (hashval == entry->hashval)
		return(start);
	for (i = start; i > 0; entry--, i--) {
		ASSERT(entry->nameidx >= leaf->hdr.firstused);
		ASSERT(entry->nameidx < XFS_LBSIZE(sbp));
		if (hashval >= entry->hashval)
			break;
	}
	for (  ; i <= max; entry++, i++) {
		ASSERT(entry->nameidx >= leaf->hdr.firstused);
		ASSERT(entry->nameidx < XFS_LBSIZE(sbp));
		if (hashval < entry->hashval)
			break;
	}
	return(i);
}

/*
 * (Re)find the correct insertion point in a node for a hashval.
 * GROT: allow for several duplicate hashvals.
 */
STATIC int
xfs_dir_node_refind(struct xfs_dir_intnode *node, int start, uint hashval)
{
	struct xfs_dir_node_entry *entry;
	int max, i;

	/*
	 * Walk upward and downward from starting point,
	 * looking for the right place for hashval.
	 */
	ASSERT(node->hdr.info.magic == XFS_DIR_NODE_MAGIC);
	max = node->hdr.count - 1;
	if (max < 0)
		return(0);
	if (start > max)
		start = max;
	entry = &node->btree[start];
	if (hashval == entry->hashval)
		return(start);
	for (i = start; i > 0; entry--, i--) {
		if (hashval >= entry->hashval)
			break;
	}
	for (  ; i <= max; entry++, i++) {
		if (hashval < entry->hashval)
			break;
	}
	return(i);
}

/*
 * Link a new block into a doubly linked list of blocks.
 */
STATIC void
xfs_dir_blk_link(struct xfs_dir_state *state,
			struct xfs_dir_state_blk *old_blk,
			struct xfs_dir_state_blk *new_blk)
{
	struct xfs_dir_blkinfo *old_info, *new_info, *tmp_info;
	struct xfs_dir_intnode *old_node, *new_node, *tmp_node;
	struct xfs_dir_leafblock *old_leaf, *new_leaf, *tmp_leaf;
	buf_t *bp;
	int before;

	/*
	 * Set up environment.
	 */
	ASSERT(old_blk->leafblk == new_blk->leafblk);
	old_info = (struct xfs_dir_blkinfo *)old_blk->bp->b_un.b_addr;
	new_info = (struct xfs_dir_blkinfo *)new_blk->bp->b_un.b_addr;
	if (old_blk->leafblk) {
		ASSERT(old_info->magic == XFS_DIR_LEAF_MAGIC);
	} else {
		ASSERT(old_info->magic == XFS_DIR_NODE_MAGIC);
	}
	ASSERT(old_info->magic == new_info->magic);

	if (old_blk->leafblk) {
		old_leaf = (struct xfs_dir_leafblock *)old_blk->bp->b_un.b_addr;
		new_leaf = (struct xfs_dir_leafblock *)new_blk->bp->b_un.b_addr;
		if ((old_leaf->hdr.count > 0) && (new_leaf->hdr.count > 0) && 
		    (new_leaf->leaves[0].hashval < old_leaf->leaves[0].hashval)) {
			before = 1;
		} else {
			before = 0;
		}
	} else {
		old_node = (struct xfs_dir_intnode *)old_blk->bp->b_un.b_addr;
		new_node = (struct xfs_dir_intnode *)new_blk->bp->b_un.b_addr;
		if ((old_node->hdr.count > 0) && (new_node->hdr.count > 0) && 
		    (new_node->btree[0].hashval < old_node->btree[0].hashval)) {
			before = 1;
		} else {
			before = 0;
		}
	}

	/*
	 * Link blocks in appropriate order.
	 */
	if (before) {
		/*
		 * Link new block in before existing block.
		 */
		new_info->forw = old_blk->blkno;
		new_info->back = old_info->back;
		if (old_info->back) {
			bp = xfs_dir_read_buf(state->trans, state->args->dp,
							    old_info->back);
			tmp_info = (struct xfs_dir_blkinfo *)bp->b_un.b_addr;
			if (old_blk->leafblk) {
				ASSERT(tmp_info->magic == XFS_DIR_LEAF_MAGIC);
			} else {
				ASSERT(tmp_info->magic == XFS_DIR_NODE_MAGIC);
			}
			ASSERT(tmp_info->forw == old_blk->blkno);
			tmp_info->forw = new_blk->blkno;
			xfs_trans_log_buf(state->trans, bp, 0,
							sizeof(*tmp_info) - 1);
		}
		old_info->back = new_blk->blkno;
	} else {
		/*
		 * Link new block in after existing block.
		 */
		new_info->forw = old_info->forw;
		new_info->back = old_blk->blkno;
		if (old_info->forw) {
			bp = xfs_dir_read_buf(state->trans, state->args->dp,
							    old_info->forw);
			tmp_info = (struct xfs_dir_blkinfo *)bp->b_un.b_addr;
			if (old_blk->leafblk) {
				ASSERT(tmp_info->magic == XFS_DIR_LEAF_MAGIC);
			} else {
				ASSERT(tmp_info->magic == XFS_DIR_NODE_MAGIC);
			}
			ASSERT(tmp_info->back == old_blk->blkno);
			tmp_info->back = new_blk->blkno;
			xfs_trans_log_buf(state->trans, bp, 0,
							sizeof(*tmp_info) - 1);
		}
		old_info->forw = new_blk->blkno;
	}

	xfs_trans_log_buf(state->trans, old_blk->bp, 0, sizeof(*tmp_info) - 1);
	xfs_trans_log_buf(state->trans, new_blk->bp, 0, sizeof(*tmp_info) - 1);
}

/*
 * Unlink a block from a doubly linked list of blocks.
 */
STATIC void
xfs_dir_blk_unlink(struct xfs_dir_state *state,
			  struct xfs_dir_state_blk *drop_blk,
			  struct xfs_dir_state_blk *save_blk)
{
	struct xfs_dir_blkinfo *drop_info, *save_info, *tmp_info;
	buf_t *bp;

	/*
	 * Set up environment.
	 */
	ASSERT(save_blk->leafblk == drop_blk->leafblk);
	save_info = (struct xfs_dir_blkinfo *)save_blk->bp->b_un.b_addr;
	drop_info = (struct xfs_dir_blkinfo *)drop_blk->bp->b_un.b_addr;
	if (save_blk->leafblk) {
		ASSERT(save_info->magic == XFS_DIR_LEAF_MAGIC);
	} else {
		ASSERT(save_info->magic == XFS_DIR_NODE_MAGIC);
	}
	ASSERT(save_info->magic == drop_info->magic);

	ASSERT((save_info->forw == drop_blk->blkno) ||
	       (save_info->back == drop_blk->blkno));
	ASSERT((drop_info->forw == save_blk->blkno) ||
	       (drop_info->back == save_blk->blkno));

	/*
	 * Unlink the leaf block from the doubly linked chain of leaves.
	 */
	if (save_info->back == drop_blk->blkno) {
		save_info->back = drop_info->back;
		if (drop_info->back) {
			bp = xfs_dir_read_buf(state->trans, state->args->dp,
							    drop_info->back);
			tmp_info = (struct xfs_dir_blkinfo *)bp->b_un.b_addr;
			if (save_blk->leafblk) {
				ASSERT(tmp_info->magic == XFS_DIR_LEAF_MAGIC);
			} else {
				ASSERT(tmp_info->magic == XFS_DIR_NODE_MAGIC);
			}
			ASSERT(tmp_info->forw == drop_blk->blkno);
			tmp_info->forw = save_blk->blkno;
			xfs_trans_log_buf(state->trans, bp, 0,
							sizeof(*tmp_info) - 1);
		}
	} else {
		save_info->forw = drop_info->forw;
		if (drop_info->forw) {
			bp = xfs_dir_read_buf(state->trans, state->args->dp,
							    drop_info->forw);
			tmp_info = (struct xfs_dir_blkinfo *)bp->b_un.b_addr;
			if (save_blk->leafblk) {
				ASSERT(tmp_info->magic == XFS_DIR_LEAF_MAGIC);
			} else {
				ASSERT(tmp_info->magic == XFS_DIR_NODE_MAGIC);
			}
			ASSERT(tmp_info->back == drop_blk->blkno);
			tmp_info->back = save_blk->blkno;
			xfs_trans_log_buf(state->trans, bp, 0,
							sizeof(*tmp_info) - 1);
		}
	}

	xfs_trans_log_buf(state->trans, save_blk->bp, 0,
					sizeof(*save_info) - 1);
}
