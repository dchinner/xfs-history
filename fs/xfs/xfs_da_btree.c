/*
 * xfs_dir_btree.c
 *
 * GROT: figure out how to recover gracefully when bmap returns ENOSPC.
 */

#include <sys/param.h>
#include <sys/errno.h>
#include <sys/buf.h>
#include <sys/vnode.h>
#include <sys/kmem.h>
#include <sys/dirent.h>
#include <sys/debug.h>
#ifdef SIM
#include <bstring.h>
#include <stdio.h>
#else
#include <sys/systm.h>
#endif
#include "xfs_types.h"
#include "xfs_inum.h"
#ifdef SIM
#define _KERNEL
#endif
#include <sys/grio.h>
#ifdef SIM
#undef _KERNEL
#endif
#include "xfs_log.h"
#include "xfs_trans.h"
#include "xfs_sb.h"
#include "xfs_mount.h"
#include "xfs_alloc_btree.h"
#include "xfs_bmap_btree.h"
#include "xfs_ialloc_btree.h"
#include "xfs_alloc.h"
#include "xfs_bmap.h"
#include "xfs_btree.h"
#include "xfs_dinode.h"
#include "xfs_inode_item.h"
#include "xfs_inode.h"
#include "xfs_dir.h"
#include "xfs_dir_btree.h"
#include "xfs_error.h"
#ifdef SIM
#include "sim.h"
#endif

#ifdef XFSDIRDEBUG
int xfsdir_debug = 0;		/* interval for fsck at split/join ops */
int xfsdir_debug_cnt = 0;	/* counter for fsck at split/join ops */
int xfsdir_check(xfs_inode_t *dp);
int xfsdir_loaddir(xfs_inode_t *dp);
#endif /* XFSDIRDEBUG */

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
					  struct xfs_dir_state_blk *node_blk_2);
STATIC void xfs_dir_node_add(struct xfs_dir_state *state,
				    struct xfs_dir_state_blk *old_node_blk,
				    struct xfs_dir_state_blk *new_node_blk);

#ifndef SIM
/*
 * Routines used for shrinking the Btree.
 */
STATIC int xfs_dir_root_join(struct xfs_dir_state *state,
				    struct xfs_dir_state_blk *root_blk);
STATIC int xfs_dir_blk_toosmall(struct xfs_dir_state *state);
STATIC void xfs_dir_leaf_unbalance(struct xfs_dir_state *state,
					  struct xfs_dir_state_blk *drop_blk,
					  struct xfs_dir_state_blk *save_blk);
STATIC void xfs_dir_node_remove(struct xfs_dir_state *state,
				       struct xfs_dir_state_blk *drop_blk);
STATIC void xfs_dir_node_unbalance(struct xfs_dir_state *state,
					struct xfs_dir_state_blk *src_node_blk,
					struct xfs_dir_state_blk *dst_node_blk);
#endif	/* !SIM */

/*
 * Routines used for finding things in the Btree.
 */
STATIC int xfs_dir_path_shift(struct xfs_dir_state *state,
				     struct xfs_dir_state_path *path,
				     int forward, int release);

/*
 * Utility routines.
 */
STATIC void xfs_dir_leaf_moveents(struct xfs_dir_leafblock *src_leaf,
					 int src_start,
					 struct xfs_dir_leafblock *dst_leaf,
					 int dst_start, int move_count,
					 xfs_mount_t *mp);
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
	ASSERT((max >= 0) && (max < XFS_DIR_NODE_MAXDEPTH));
	ASSERT(state->path.blk[max].leafblk == 1);
	addblk = &state->path.blk[max];			/* initial dummy value */
	for (i = max; (i >= 0) && addblk; state->path.active--, i--) {
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
			 */
			retval = xfs_dir_node_split(state, oldblk, newblk,
							   addblk, max - i);
		}
		if (retval > 0)
			return(retval);	/* GROT: dir is inconsistent */

		/*
		 * Update the btree to show the new hashval for this child.
		 */
		xfs_dir_fixhashpath(state, &state->path);

		/*
		 * Record the newly split block for the next time thru?
		 */
		if (retval < 0)
			addblk = newblk;
		else
			addblk = NULL;
	}
	if (!addblk) {
#ifdef XFSDIRDEBUG
		if (xfsdir_debug && ((xfsdir_debug_cnt % xfsdir_debug) == 0))
			xfsdir_check(state->args->dp);
		xfsdir_debug_cnt++;
#endif /* XFSDIRDEBUG */
		return(0);
	}

	/*
	 * Split the root node.
	 */
	oldblk = &state->path.blk[0];
	ASSERT(oldblk->bp->b_offset == 0);	/* root must be at block 0 */
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
	xfs_trans_log_buf(state->trans, addblk->bp,
	    XFS_DIR_LOGRANGE(node, &node->hdr.info, sizeof(node->hdr.info)));

#ifdef XFSDIRDEBUG
	if (xfsdir_debug && ((xfsdir_debug_cnt % xfsdir_debug) == 0))
		xfsdir_check(state->args->dp);
	xfsdir_debug_cnt++;
#endif /* XFSDIRDEBUG */
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

	/*
	 * Copy the existing (incorrect) block from the root node position
	 * to a free space somewhere.
	 */
	retval = xfs_dir_grow_inode(state->trans, state->args, &blkno);
	if (retval)
		return(retval);
	bp = xfs_dir_get_buf(state->trans, state->args->dp, blkno);
	ASSERT(bp != NULL);
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
	 */
	ASSERT(oldblk->leafblk == 1);
	retval = xfs_dir_grow_inode(state->trans, state->args, &blkno);
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
	oldblk->hashval = leaf->leaves[ leaf->hdr.count-1 ].hashval;
	leaf = (struct xfs_dir_leafblock *)newblk->bp->b_un.b_addr;
	newblk->hashval = leaf->leaves[ leaf->hdr.count-1 ].hashval;

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

	leaf = (struct xfs_dir_leafblock *)bp->b_un.b_addr;
	ASSERT(leaf->hdr.info.magic == XFS_DIR_LEAF_MAGIC);
	ASSERT((index >= 0) && (index <= leaf->hdr.count));
	hdr = &leaf->hdr;

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
		return(XFS_ERROR(ENOSPC));

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

	return(XFS_ERROR(ENOSPC));
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
	xfs_mount_t *mp;

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
		xfs_trans_log_buf(trans, bp,
		    XFS_DIR_LOGRANGE(leaf, entry, tmp + sizeof(*entry)));
	}
	hdr->count++;

	/*
	 * Allocate space for the new string (at the end of the run).
	 */
	map = &hdr->freemap[mapindex];
	mp = trans->t_mountp;
	ASSERT(map->base < XFS_LBSIZE(mp));
	ASSERT(map->size >= XFS_DIR_LEAF_ENTSIZE_BYNAME(args->namelen));
	ASSERT(map->size < XFS_LBSIZE(mp));
	map->size -= XFS_DIR_LEAF_ENTSIZE_BYNAME(args->namelen);
	entry->nameidx = map->base + map->size;
	entry->hashval = args->hashval;
	entry->namelen = args->namelen;
	xfs_trans_log_buf(trans, bp,
	    XFS_DIR_LOGRANGE(leaf, entry, sizeof(*entry)));

	/*
	 * Copy the string and inode number into the new space.
	 */
	namest = XFS_DIR_LEAF_NAMESTRUCT(leaf, entry->nameidx);
	bcopy((char *)&args->inumber, namest->inumber, sizeof(xfs_ino_t));
	bcopy(args->name, namest->name, args->namelen);
	xfs_trans_log_buf(trans, bp,
	    XFS_DIR_LOGRANGE(leaf, namest, XFS_DIR_LEAF_ENTSIZE_BYENTRY(entry)));

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
	xfs_trans_log_buf(trans, bp, XFS_DIR_LOGRANGE(leaf, hdr, sizeof(*hdr)));
}

/*
 * Garbage collect a leaf directory block by copying it to a new buffer.
 */
STATIC void
xfs_dir_leaf_compact(xfs_trans_t *trans, buf_t *bp)
{
	struct xfs_dir_leafblock *leaf_s, *leaf_d;
	struct xfs_dir_leaf_hdr *hdr_s, *hdr_d;
	xfs_mount_t *mp;
	char *tmpbuffer;

	mp = trans->t_mountp;
	tmpbuffer = kmem_alloc(XFS_LBSIZE(mp), KM_SLEEP);
	ASSERT(tmpbuffer != NULL);
	bcopy(bp->b_un.b_addr, tmpbuffer, XFS_LBSIZE(mp));
	bzero(bp->b_un.b_addr, XFS_LBSIZE(mp));

	/*
	 * Copy basic information
	 */
	leaf_s = (struct xfs_dir_leafblock *)tmpbuffer;
	leaf_d = (struct xfs_dir_leafblock *)bp->b_un.b_addr;
	hdr_s = &leaf_s->hdr;
	hdr_d = &leaf_d->hdr;
	hdr_d->info = hdr_s->info;	/* struct copy */
	hdr_d->firstused = XFS_LBSIZE(mp);
	if (hdr_d->firstused == 0)
		hdr_d->firstused = XFS_LBSIZE(mp) - 1;
	hdr_d->namebytes = 0;
	hdr_d->count = 0;
	hdr_d->holes = 0;
	hdr_d->freemap[0].base = sizeof(struct xfs_dir_leaf_hdr);
	hdr_d->freemap[0].size = hdr_d->firstused - hdr_d->freemap[0].base;

	/*
	 * Copy all entry's in the same (sorted) order,
	 * but allocate filenames packed and in sequence.
	 */
	xfs_dir_leaf_moveents(leaf_s, 0, leaf_d, 0, (int)hdr_s->count, mp);

	xfs_trans_log_buf(trans, bp, 0, XFS_LBSIZE(mp) - 1);

	kmem_free(tmpbuffer, XFS_LBSIZE(mp));
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
	int count, totallen, max, space, swap;

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
	    ((leaf2->leaves[ 0 ].hashval < leaf1->leaves[ 0 ].hashval) ||
	     (leaf2->leaves[ leaf2->hdr.count-1 ].hashval <
	      leaf1->leaves[ leaf1->hdr.count-1 ].hashval))) {
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
	if (swap)
		state->inleaf = !state->inleaf;

	/*
	 * Move any entries required from leaf to leaf:
	 */
	if (count < hdr1->count) {
		/*
		 * Figure the total bytes to be added to the destination leaf.
		 */
		count = hdr1->count - count;	/* number entries being moved */
		space  = hdr1->namebytes - totallen;
		space += count * (sizeof(struct xfs_dir_leaf_name)-1);
		space += count * sizeof(struct xfs_dir_leaf_entry);

		/*
		 * leaf2 is the destination, compact it if it looks tight.
		 */
		max  = hdr2->firstused - sizeof(struct xfs_dir_leaf_hdr);
		max -= hdr2->count * sizeof(struct xfs_dir_leaf_entry);
		if (space > max) {
			xfs_dir_leaf_compact(state->trans, blk2->bp);
		}

		/*
		 * Move high entries from leaf1 to low end of leaf2.
		 */
		xfs_dir_leaf_moveents(leaf1, hdr1->count - count,
					     leaf2, 0, count, state->mp);

		xfs_trans_log_buf(state->trans, blk1->bp, 0, state->blocksize-1);
		xfs_trans_log_buf(state->trans, blk2->bp, 0, state->blocksize-1);

	} else if (count > hdr1->count) {
		/*
		 * Figure the total bytes to be added to the destination leaf.
		 */
		count -= hdr1->count;		/* number entries being moved */
		space  = totallen - hdr1->namebytes;
		space += count * (sizeof(struct xfs_dir_leaf_name)-1);
		space += count * sizeof(struct xfs_dir_leaf_entry);

		/*
		 * leaf1 is the destination, compact it if it looks tight.
		 */
		max  = hdr1->firstused - sizeof(struct xfs_dir_leaf_hdr);
		max -= hdr1->count * sizeof(struct xfs_dir_leaf_entry);
		if (space > max) {
			xfs_dir_leaf_compact(state->trans, blk1->bp);
		}

		/*
		 * Move low entries from leaf2 to high end of leaf1.
		 */
		xfs_dir_leaf_moveents(leaf2, 0, leaf1, (int)hdr1->count,
					     count, state->mp);

		xfs_trans_log_buf(state->trans, blk1->bp, 0, state->blocksize-1);
		xfs_trans_log_buf(state->trans, blk2->bp, 0, state->blocksize-1);
	}

	/*
	 * Copy out last hashval in each block for B-tree code.
	 */
	blk1->hashval = leaf1->leaves[ leaf1->hdr.count-1 ].hashval;
	blk2->hashval = leaf2->leaves[ leaf2->hdr.count-1 ].hashval;

	/*
	 * Adjust the expected index for insertion.
	 * GROT: this doesn't work unless blk2 was originally empty.
	 */
	if (!state->inleaf) {
		blk2->index = blk1->index - leaf1->hdr.count;
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
	totallen = 0;

	/*
	 * Examine entries until we reduce the absolute difference in
	 * byte usage between the two blocks to a minimum.
	 */
	max = hdr1->count + hdr2->count;
	half  = (max+1) * (sizeof(*entry)+sizeof(struct xfs_dir_leaf_entry)-1);
	half += hdr1->namebytes + hdr2->namebytes + state->args->namelen;
	half /= 2;
	lastdelta = state->blocksize;
	entry = &leaf1->leaves[0];
	for (count = 0; count < max; entry++, count++) {

#define XFS_DIR_ABS(A)	(((A) < 0) ? -(A) : (A))
		/*
		 * The new entry is in the first block, account for it.
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
		tmp = totallen + sizeof(*entry)
				+ XFS_DIR_LEAF_ENTSIZE_BYENTRY(entry);
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
	totallen -= count * (sizeof(*entry)+sizeof(struct xfs_dir_leaf_entry)-1);
	if (foundit) {
		totallen -= ( (sizeof(*entry)+sizeof(struct xfs_dir_leaf_entry)-1) +
			      state->args->namelen );
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

	node = (struct xfs_dir_intnode *)oldblk->bp->b_un.b_addr;
	ASSERT(node->hdr.info.magic == XFS_DIR_NODE_MAGIC);

	/*
	 * Do we have to split the node?
	 */
	retval = 0;
	if (node->hdr.count >= XFS_DIR_NODE_ENTRIES(state->mp)) {
		/*
		 * Allocate a new node, add to the doubly linked chain of
		 * nodes, then move some of our excess entries into it.
		 */
		retval = xfs_dir_grow_inode(state->trans, state->args, &blkno);
		if (retval)
			return(retval);	/* GROT: dir is inconsistent */
		
		newblk->bp = xfs_dir_node_create(state->trans, state->args->dp,
							       blkno, treelevel);
		newblk->blkno = blkno;
		xfs_dir_node_rebalance(state, oldblk, newblk);
		xfs_dir_blk_link(state, oldblk, newblk);
		retval = -1;
	}

	/*
	 * Insert the new entry into the correct block
	 * (updating last hashval in the process).
	 *
	 * xfs_dir_node_add() inserts BEFORE the given index,
	 * and as a result of using node_lookup_int() we always
	 * point to a valid entry (not after one), but a split
	 * operation always results in a new block whose hashvals
	 * FOLLOW the current block.
	 */
	node = (struct xfs_dir_intnode *)oldblk->bp->b_un.b_addr;
	if (oldblk->index <= node->hdr.count) {
		oldblk->index++;
		xfs_dir_node_add(state, oldblk, addblk);
	} else {
		newblk->index++;
		xfs_dir_node_add(state, newblk, addblk);
	}

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
			      struct xfs_dir_state_blk *blk2)
{
	struct xfs_dir_intnode *node1, *node2, *tmpnode;
	struct xfs_dir_node_entry *btree_s, *btree_d;
	int count, tmp;

	node1 = (struct xfs_dir_intnode *)blk1->bp->b_un.b_addr;
	node2 = (struct xfs_dir_intnode *)blk2->bp->b_un.b_addr;
	/*
	 * Figure out how many entries need to move, and in which direction.
	 * Swap the nodes around if that makes it simpler.
	 */
	if ((node1->hdr.count > 0) && (node2->hdr.count > 0) &&
	    ((node2->btree[ 0 ].hashval < node1->btree[ 0 ].hashval) ||
	     (node2->btree[ node2->hdr.count-1 ].hashval <
	      node1->btree[ node1->hdr.count-1 ].hashval))) {
		tmpnode = node1;
		node1 = node2;
		node2 = tmpnode;
	}
	ASSERT(node1->hdr.info.magic == XFS_DIR_NODE_MAGIC);
	ASSERT(node2->hdr.info.magic == XFS_DIR_NODE_MAGIC);
	count = (node1->hdr.count - node2->hdr.count) / 2;
	if (count == 0)
		return;

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
	 * (note: don't use the swapped node pointers)
	 */
	node1 = (struct xfs_dir_intnode *)blk1->bp->b_un.b_addr;
	node2 = (struct xfs_dir_intnode *)blk2->bp->b_un.b_addr;
	blk1->hashval = node1->btree[ node1->hdr.count-1 ].hashval;
	blk2->hashval = node2->btree[ node2->hdr.count-1 ].hashval;

	/*
	 * Adjust the expected index for insertion.
	 */
	if (blk1->index >= node1->hdr.count) {
		blk2->index = blk1->index - node1->hdr.count;
		blk1->index = node1->hdr.count + 1;	/* make it invalid */
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
	tmp = 0;
	btree = &node->btree[ oldblk->index ];
	if (oldblk->index < node->hdr.count) {
		tmp = (node->hdr.count - oldblk->index) * sizeof(*btree);
		bcopy((char *)btree, (char *)(btree+1), tmp);
	}
	btree->hashval = newblk->hashval;
	btree->before = newblk->blkno;
	xfs_trans_log_buf(state->trans, oldblk->bp,
	    XFS_DIR_LOGRANGE(node, btree, tmp + sizeof(*btree)));
	node->hdr.count++;
	xfs_trans_log_buf(state->trans, oldblk->bp,
	    XFS_DIR_LOGRANGE(node, &node->hdr, sizeof(node->hdr)));

	/*
	 * Copy the last hash value from the oldblk to propagate upwards.
	 */
	oldblk->hashval = node->btree[ node->hdr.count-1 ].hashval;
}

/*========================================================================
 * Routines used for shrinking the Btree.
 *========================================================================*/

#ifndef SIM
/*
 * Deallocate an empty leaf node, remove it from its parent,
 * possibly deallocating that block, etc...
 */
int
xfs_dir_join(struct xfs_dir_state *state)
{
	struct xfs_dir_state_blk *drop_blk, *save_blk;
	int retval;

	retval = 0;
	drop_blk = &state->path.blk[ state->path.active-1 ];
	save_blk = &state->altpath.blk[ state->path.active-1 ];
	ASSERT(state->path.blk[0].leafblk == 0);
	ASSERT(drop_blk->leafblk == 1);

	/*
	 * Walk back up the tree joining/deallocating as necessary.
	 * When we stop dropping blocks, break out.
	 */
	for (  ; state->path.active > 0; drop_blk--, save_blk--,
		 state->path.active--) {
		/*
		 * Remove the offending node and fixup hashvals.
		 */
		if (!drop_blk->leafblk) {
			xfs_dir_node_remove(state, drop_blk);
			xfs_dir_fixhashpath(state, &state->path);
		}

		/*
		 * See if we can combine the block with a neighbor.
		 */
		switch (xfs_dir_blk_toosmall(state)) {
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
		retval = xfs_dir_shrink_inode(state->trans, state->args,
					    drop_blk->blkno, drop_blk->bp);
		xfs_dir_fixhashpath(state, &state->altpath);
	}
out:
	if (state->path.active == 1) {
		/*
		 * We joined all the way to the top.  If it turns out that
		 * we only have one entry in the root, make the child block
		 * the new root.
		 */
		retval = xfs_dir_root_join(state, &state->path.blk[0]);
	}
#ifdef XFSDIRDEBUG
	if (xfsdir_debug && ((xfsdir_debug_cnt % xfsdir_debug) == 0))
		xfsdir_check(state->args->dp);
	xfsdir_debug_cnt++;
#endif /* XFSDIRDEBUG */
	return(retval);
}

/*
 * We have only one entry in the root.  Copy the only remaining child of
 * the old root to block 0 as the new root node.
 */
STATIC int
xfs_dir_root_join(struct xfs_dir_state *state,
			 struct xfs_dir_state_blk *root_blk)
{
	struct xfs_dir_intnode *oldroot, *newroot;
	struct xfs_dir_leafblock *leaf;
	xfs_fsblock_t child;
	buf_t *bp;

	ASSERT(root_blk->bp->b_offset == 0);
	ASSERT(root_blk->blkno == 0);
	ASSERT(root_blk->leafblk == 0);
	oldroot = (struct xfs_dir_intnode *)root_blk->bp->b_un.b_addr;
	ASSERT(oldroot->hdr.info.magic == XFS_DIR_NODE_MAGIC);
	ASSERT(oldroot->hdr.info.forw == 0);
	ASSERT(oldroot->hdr.info.back == 0);

	/*
	 * If the root has more than one child, then don't do anything.
	 */
	if (oldroot->hdr.count > 1)
		return(0);

	/*
	 * Read in the (only) child block, then copy those bytes into
	 * the root block's buffer and free the original child block.
	 */
	child = oldroot->btree[ 0 ].before;
	ASSERT(child != 0);
	bp = xfs_dir_read_buf(state->trans, state->args->dp, child);
	ASSERT(bp != NULL);
	if (oldroot->hdr.level == 1) {
		leaf = (struct xfs_dir_leafblock *)bp->b_un.b_addr;
		ASSERT(leaf->hdr.info.magic == XFS_DIR_LEAF_MAGIC);
		ASSERT(leaf->hdr.info.forw == 0);
		ASSERT(leaf->hdr.info.back == 0);
	} else {
		newroot = (struct xfs_dir_intnode *)bp->b_un.b_addr;
		ASSERT(newroot->hdr.info.magic == XFS_DIR_NODE_MAGIC);
		ASSERT(newroot->hdr.info.forw == 0);
		ASSERT(newroot->hdr.info.back == 0);
	}
	bcopy(bp->b_un.b_addr, root_blk->bp->b_un.b_addr, state->blocksize);
	xfs_trans_log_buf(state->trans, root_blk->bp, 0, state->blocksize - 1);
	return( xfs_dir_shrink_inode(state->trans, state->args, child, bp) );
}

/*
 * Check a block and its neighbors to see if the block should be
 * collapsed into one or the other neighbor.  Always keep the block
 * with the smaller block number.
 * If the current block is over 50% full, don't try to join it, return 0.
 * If the block is empty, fill in the state structure and return 2.
 * If it can be collapsed, fill in the state structure and return 1.
 * If nothing can be done, return 0.
 */
STATIC int
xfs_dir_blk_toosmall(struct xfs_dir_state *state)
{
	struct xfs_dir_leafblock *leaf;
	struct xfs_dir_intnode *node;
	struct xfs_dir_state_blk *blk;
	struct xfs_dir_blkinfo *info;
	int count, bytes, forward, i;
	xfs_fsblock_t blkno;
	buf_t *bp;

	/*
	 * Check for the degenerate case of the block being over 50% full.
	 * If so, it's not worth even looking to see if we might be able
	 * to coalesce with a sibling.
	 */
	blk = &state->path.blk[ state->path.active-1 ];
	info = (struct xfs_dir_blkinfo *)blk->bp->b_un.b_addr;
	if (blk->leafblk) {
		ASSERT(info->magic == XFS_DIR_LEAF_MAGIC);
		leaf = (struct xfs_dir_leafblock *)info;
		count = leaf->hdr.count;
		bytes = sizeof(struct xfs_dir_leaf_hdr) +
			count * sizeof(struct xfs_dir_leaf_entry) +
			count * (sizeof(struct xfs_dir_leaf_name)-1) +
			leaf->hdr.namebytes;
		if (bytes > (state->blocksize >> 1))
			return(0);	/* blk over 50%, dont try to join */
	} else {
		ASSERT(info->magic == XFS_DIR_NODE_MAGIC);
		node = (struct xfs_dir_intnode *)info;
		count = node->hdr.count;
		if (count > (XFS_DIR_NODE_ENTRIES(state->mp) >> 1))
			return(0);	/* blk over 50%, dont try to join */
	}

	/*
	 * Check for the degenerate case of the block being empty.
	 * If the block is empty, we'll simply delete it, no need to
	 * coalesce it with a sibling block.  We choose (aribtrarily)
	 * to merge with the forward block unless it is NULL.
	 */
	if (count == 0) {
		/*
		 * Make altpath point to the block we want to keep and
		 * path point to the block we want to drop (this one).
		 */
		forward = (info->forw != 0);
		bcopy(&state->path, &state->altpath, sizeof(state->path));
		if (xfs_dir_path_shift(state, &state->altpath, forward, 0))
			return(0);
		return(2);
	}

	/*
	 * Examine each sibling block to see if we can coalesce with
	 * at least 25% free space to spare.  We need to figure out
	 * whether to merge with the forward or the backward block.
	 * We prefer coalescing with the lower numbered sibling so as
	 * to shrink a directory over time.
	 */
	forward = (info->forw < info->back);	/* start with smaller blk num */
	for (i = 0; i < 2; forward = !forward, i++) {
		if (forward)
			blkno = info->forw;
		else
			blkno = info->back;
		if (blkno == 0)
			continue;
		bp = xfs_dir_read_buf(state->trans, state->args->dp, blkno);
		ASSERT(bp != NULL);

		if (blk->leafblk) {
			leaf = (struct xfs_dir_leafblock *)info;
			count  = leaf->hdr.count;
			bytes  = state->blocksize - (state->blocksize>>2);
			bytes -= leaf->hdr.namebytes;
			leaf = (struct xfs_dir_leafblock *)bp->b_un.b_addr;
			ASSERT(leaf->hdr.info.magic == XFS_DIR_LEAF_MAGIC);
			count += leaf->hdr.count;
			bytes -= leaf->hdr.namebytes;
			bytes -= count * (sizeof(struct xfs_dir_leaf_name) - 1);
			bytes -= count * sizeof(struct xfs_dir_leaf_entry);
			bytes -= sizeof(struct xfs_dir_leaf_hdr);
			if (bytes >= 0)
				break;	/* fits with at least 25% to spare */
		} else {
			node = (struct xfs_dir_intnode *)info;
			count  = XFS_DIR_NODE_ENTRIES(state->mp);
			count -= XFS_DIR_NODE_ENTRIES(state->mp) >> 2;
			count -= node->hdr.count;
			node = (struct xfs_dir_intnode *)bp->b_un.b_addr;
			ASSERT(node->hdr.info.magic == XFS_DIR_NODE_MAGIC);
			count -= node->hdr.count;
			if (count >= 0)
				break;	/* fits with at least 25% to spare */
		}

		xfs_trans_brelse(state->trans, bp);
	}
	if (i >= 2)
		return(0);

	/*
	 * Make altpath point to the block we want to keep (the lower
	 * numbered block) and path point to the block we want to drop.
	 */
	bcopy(&state->path, &state->altpath, sizeof(state->path));
	if (blkno < blk->blkno) {
		if (xfs_dir_path_shift(state, &state->altpath, forward, 0))
			return(0);
	} else {
		if (xfs_dir_path_shift(state, &state->path, forward, 0))
			return(0);
	}
	return(1);
}
#endif	/* !SIM */

/*
 * Walk back up the tree adjusting hash values as necessary,
 * when we stop making changes, return.
 */
void
xfs_dir_fixhashpath(struct xfs_dir_state *state,
			   struct xfs_dir_state_path *path)
{
	struct xfs_dir_state_blk *blk;
	struct xfs_dir_intnode *node;
	struct xfs_dir_node_entry *btree;
	struct xfs_dir_leafblock *leaf;
	uint lasthash;
	int level;

	level = path->active-1;
	blk = &path->blk[ level ];
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
		btree = &node->btree[ blk->index ];
		if (btree->hashval == lasthash)
			break;
		blk->hashval = btree->hashval = lasthash;
		xfs_trans_log_buf(state->trans, blk->bp,
		    XFS_DIR_LOGRANGE(node, btree, sizeof(*btree)));

		lasthash = node->btree[ node->hdr.count-1 ].hashval;
	}
}

#ifndef SIM
/*
 * Remove a name from the leaf directory structure.
 *
 * Return 1 if leaf is less than 50% full, 0 if >= 50% full.
 */
int
xfs_dir_leaf_remove(xfs_trans_t *trans, buf_t *bp, int index)
{
	struct xfs_dir_leafblock *leaf;
	struct xfs_dir_leaf_hdr *hdr;
	struct xfs_dir_leaf_map *map;
	struct xfs_dir_leaf_entry *entry;
	struct xfs_dir_leaf_name *namest;
	int before, after, smallest, entsize;
	int tablesize, tmp, i;
	xfs_mount_t *mp;

	leaf = (struct xfs_dir_leafblock *)bp->b_un.b_addr;
	ASSERT(leaf->hdr.info.magic == XFS_DIR_LEAF_MAGIC);
	hdr = &leaf->hdr;
	mp = trans->t_mountp;
	ASSERT((hdr->count > 0) && (hdr->count < (XFS_LBSIZE(mp)/8)));
	ASSERT((index >= 0) && (index < hdr->count));
	ASSERT(hdr->firstused >= ((hdr->count*sizeof(*entry))+sizeof(*hdr)));
	entry = &leaf->leaves[index];
	ASSERT(entry->nameidx >= hdr->firstused);
	ASSERT(entry->nameidx < XFS_LBSIZE(mp));

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
	entsize = XFS_DIR_LEAF_ENTSIZE_BYENTRY(entry);
	for (i = 0; i < XFS_DIR_LEAF_MAPSIZE; map++, i++) {
		ASSERT(map->base < XFS_LBSIZE(mp));
		ASSERT(map->size < XFS_LBSIZE(mp));
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
	namest = XFS_DIR_LEAF_NAMESTRUCT(leaf, entry->nameidx);
	bzero((char *)namest, entsize);
	xfs_trans_log_buf(trans, bp, XFS_DIR_LOGRANGE(leaf, namest, entsize));

	hdr->namebytes -= entry->namelen;
	tmp = (hdr->count - index) * sizeof(struct xfs_dir_leaf_entry);
	bcopy((char *)(entry+1), (char *)entry, tmp);
	hdr->count--;
	xfs_trans_log_buf(trans, bp,
	    XFS_DIR_LOGRANGE(leaf, entry, tmp + sizeof(*entry)));
	entry = &leaf->leaves[hdr->count];
	bzero((char *)entry, sizeof(struct xfs_dir_leaf_entry));

	/*
	 * If we removed the first entry, re-find the first used byte
	 * in the name area.  Note that if the entry was the "firstused",
	 * then we don't have a "hole" in our block resulting from
	 * removing the name.
	 */
	if (smallest) {
		tmp = XFS_LBSIZE(mp);
		entry = &leaf->leaves[0];
		for (i = hdr->count-1; i >= 0; entry++, i--) {
			ASSERT(entry->nameidx >= hdr->firstused);
			ASSERT(entry->nameidx < XFS_LBSIZE(mp));
			if (entry->nameidx < tmp)
				tmp = entry->nameidx;
		}
		hdr->firstused = tmp;
		if (hdr->firstused == 0)
			hdr->firstused = tmp - 1;
	} else {
		hdr->holes = 1;		/* mark as needing compaction */
	}
	xfs_trans_log_buf(trans, bp, XFS_DIR_LOGRANGE(leaf, hdr, sizeof(*hdr)));

	/*
	 * Check if leaf is less than 50% full, caller may want to
	 * "join" the leaf with a sibling if so.
	 */
	tmp  = sizeof(struct xfs_dir_leaf_hdr);
	tmp += leaf->hdr.count * sizeof(struct xfs_dir_leaf_entry);
	tmp += leaf->hdr.count * (sizeof(struct xfs_dir_leaf_name) - 1);
	tmp += leaf->hdr.namebytes;
	if (tmp < (XFS_LBSIZE(mp) >> 1))
		return(1);	/* leaf is less than 50% of capacity */
	return(0);
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
	xfs_mount_t *mp;
	char *tmpbuffer;

	/*
	 * Set up environment.
	 */
	mp = state->mp;
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
	drop_blk->hashval = drop_leaf->leaves[ drop_leaf->hdr.count-1 ].hashval;

	/*
	 * Check if we need a temp buffer, or can we do it in place.
	 * Note that we don't check "leaf" for holes because we will
	 * always be dropping it, toosmall() decided that for us already.
	 */
	if (save_hdr->holes == 0) {
		/*
		 * dest leaf has no holes, so we add there.  May need
		 * to make some room in the entry array.
		 */
		if ((drop_leaf->leaves[ 0 ].hashval <
		     save_leaf->leaves[ 0 ].hashval) ||
		    (drop_leaf->leaves[ drop_leaf->hdr.count-1 ].hashval <
		     save_leaf->leaves[ save_leaf->hdr.count-1 ].hashval))
		{
			xfs_dir_leaf_moveents(drop_leaf, 0, save_leaf, 0,
						 (int)drop_hdr->count, mp);
		} else {
			xfs_dir_leaf_moveents(drop_leaf, 0,
					      save_leaf, save_hdr->count,
					      (int)drop_hdr->count, mp);
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
		if (tmp_hdr->firstused == 0)
			tmp_hdr->firstused = state->blocksize - 1;
		tmp_hdr->namebytes = 0;
		if ((drop_leaf->leaves[ 0 ].hashval <
		     save_leaf->leaves[ 0 ].hashval) ||
		    (drop_leaf->leaves[ drop_leaf->hdr.count-1 ].hashval <
		     save_leaf->leaves[ save_leaf->hdr.count-1 ].hashval))
		{
			xfs_dir_leaf_moveents(drop_leaf, 0, tmp_leaf, 0,
						 (int)drop_hdr->count, mp);
			xfs_dir_leaf_moveents(save_leaf, 0,
					      tmp_leaf, tmp_leaf->hdr.count,
					      (int)save_hdr->count, mp);
		} else {
			xfs_dir_leaf_moveents(save_leaf, 0, tmp_leaf, 0,	
						 (int)save_hdr->count, mp);
			xfs_dir_leaf_moveents(drop_leaf, 0,
					      tmp_leaf, tmp_leaf->hdr.count,
					      (int)drop_hdr->count, mp);
		}
		bcopy((char *)tmp_leaf, (char *)save_leaf, state->blocksize);
		kmem_free(tmpbuffer, state->blocksize);
	}

	xfs_trans_log_buf(state->trans, save_blk->bp, 0, state->blocksize - 1);

	/*
	 * Copy out last hashval in each block for B-tree code.
	 */
	save_blk->hashval = save_leaf->leaves[ save_leaf->hdr.count-1 ].hashval;
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
	ASSERT(drop_blk->index < node->hdr.count);
	ASSERT(drop_blk->index >= 0);

	/*
	 * Copy over the offending entry, or just zero it out.
	 */
	btree = &node->btree[drop_blk->index];
	if (drop_blk->index < (node->hdr.count-1)) {
		tmp  = node->hdr.count - drop_blk->index - 1;
		tmp *= sizeof(struct xfs_dir_node_entry);
		bcopy((char *)(btree+1), (char *)btree, tmp);
		xfs_trans_log_buf(state->trans, drop_blk->bp,
		    XFS_DIR_LOGRANGE(node, btree, tmp));
		btree = &node->btree[ node->hdr.count-1 ];
	}
	bzero((char *)btree, sizeof(struct xfs_dir_node_entry));
	xfs_trans_log_buf(state->trans, drop_blk->bp,
	    XFS_DIR_LOGRANGE(node, btree, sizeof(*btree)));
	node->hdr.count--;
	xfs_trans_log_buf(state->trans, drop_blk->bp,
	    XFS_DIR_LOGRANGE(node, &node->hdr, sizeof(node->hdr)));

	/*
	 * Copy the last hash value from the block to propagate upwards.
	 */
	btree--;
	drop_blk->hashval = btree->hashval;
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
	if ((drop_node->btree[ 0 ].hashval < save_node->btree[ 0 ].hashval) ||
	    (drop_node->btree[ drop_node->hdr.count-1 ].hashval <
	     save_node->btree[ save_node->hdr.count-1 ].hashval))
	{
		btree = &save_node->btree[ drop_node->hdr.count ];
		tmp = save_node->hdr.count * sizeof(struct xfs_dir_node_entry);
		bcopy((char *)&save_node->btree[0], (char *)btree, tmp);
		btree = &save_node->btree[0];
	} else {
		btree = &save_node->btree[ save_node->hdr.count ];
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
	save_blk->hashval = save_node->btree[ save_node->hdr.count-1 ].hashval;
}
#endif	/* !SIM */

/*========================================================================
 * Routines used for finding things in the Btree.
 *========================================================================*/

/*
 * Look up a name in a leaf directory structure.
 * This is the internal routine, it uses the caller's buffer.
 *
 * Note that duplicate keys are allowed, but only check within the
 * current leaf node.  The Btree code must check in adjacent leaf nodes.
 *
 * Return in *index the index into the entry[] array of either the found
 * entry, or where the entry should have been (insert before that entry).
 *
 * Don't change the args->inumber unless we find the filename.
 */
int
xfs_dir_leaf_lookup_int(buf_t *bp, struct xfs_dir_name *args, int *index)
{
	struct xfs_dir_leafblock *leaf;
	struct xfs_dir_leaf_entry *entry;
	struct xfs_dir_leaf_name *namest;
	int probe, span;
	uint hashval;

	leaf = (struct xfs_dir_leafblock *)bp->b_un.b_addr;
	ASSERT(leaf->hdr.info.magic == XFS_DIR_LEAF_MAGIC);
	ASSERT((((int)leaf->hdr.count) >= 0) && \
	       (leaf->hdr.count < (XFS_LBSIZE(args->dp->i_mount)/8)));

	/*
	 * Binary search.  (note: small blocks will skip this loop)
	 */
	hashval = args->hashval;
	probe = span = leaf->hdr.count / 2;
	for (entry = &leaf->leaves[probe]; span > 4; entry = &leaf->leaves[probe]) {
		span /= 2;
		if (entry->hashval < hashval)
			probe += span;
		else if (entry->hashval > hashval)
			probe -= span;
		else
			break;
	}
	ASSERT((probe >= 0) && ((leaf->hdr.count == 0) || (probe < leaf->hdr.count)));
	ASSERT((span <= 4) || (entry->hashval == hashval));

	/*
	 * Since we may have duplicate hashval's, find the first matching
	 * hashval in the leaf.
	 */
	while ((probe > 0) && (entry->hashval >= hashval)) {
		entry--;
		probe--;
	}
	while ((probe < leaf->hdr.count) && (entry->hashval < hashval)) {
		entry++;
		probe++;
	}
	if ((probe == leaf->hdr.count) || (entry->hashval != hashval)) {
		*index = probe;
		return(ENOENT);
	}

	/*
	 * Duplicate keys may be present, so search all of them for a match.
	 */
	while ((probe < leaf->hdr.count) && (entry->hashval == hashval)) {
		namest = XFS_DIR_LEAF_NAMESTRUCT(leaf, entry->nameidx);
		if ((entry->namelen == args->namelen) &&
		    (bcmp(args->name, namest->name, args->namelen) == 0)) {
			bcopy(namest->inumber, (char *)&args->inumber,
					       sizeof(xfs_ino_t));
			*index = probe;
			return(XFS_ERROR(EEXIST));
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
 *
 * We support duplicate hashval's so for each entry in the current
 * node that could contain the desired hashval, descend.  This is a
 * pruned depth-first tree search.
 */
int
xfs_dir_node_lookup_int(struct xfs_dir_state *state)
{
	struct xfs_dir_state_blk *blk;
	struct xfs_dir_blkinfo *current;
	struct xfs_dir_intnode *node;
	struct xfs_dir_node_entry *btree;
	struct xfs_dir_leafblock *leaf;
	xfs_fsblock_t blkno;
	int probe, span, max;
	uint hashval;

	/*
	 * Descend thru the B-tree searching each level for the right
	 * node to use, until the right hashval is found.
	 */
	blkno = 0;
	for (blk = &state->path.blk[0], state->path.active = 1;
			 state->path.active <= XFS_DIR_NODE_MAXDEPTH;
			 blk++, state->path.active++) {
		/*
		 * Read the next node down in the tree.
		 */
		blk->blkno = blkno;
		blk->bp = xfs_dir_read_buf(state->trans, state->args->dp, blkno);
		ASSERT(blk->bp != NULL);
		current = (struct xfs_dir_blkinfo *)blk->bp->b_un.b_addr;
		ASSERT((current->magic == XFS_DIR_NODE_MAGIC) || \
		       (current->magic == XFS_DIR_LEAF_MAGIC));

		/*
		 * Search an intermediate node for a match.
		 */
		if (current->magic == XFS_DIR_NODE_MAGIC) {
			node = (struct xfs_dir_intnode *)blk->bp->b_un.b_addr;
			blk->hashval = node->btree[ node->hdr.count-1 ].hashval;
			blk->leafblk = 0;

			/*
			 * Binary search.  (note: small blocks will skip loop)
			 */
			max = node->hdr.count;
			probe = span = max / 2;
			hashval = state->args->hashval;
			for (btree = &node->btree[probe]; span > 4;
				   btree = &node->btree[probe]) {
				span /= 2;
				if (btree->hashval < hashval)
					probe += span;
				else if (btree->hashval > hashval)
					probe -= span;
				else
					break;
			}
			ASSERT((probe >= 0) && (probe < max));
			ASSERT((span <= 4) || (btree->hashval == hashval));

			/*
			 * Since we may have duplicate hashval's, find the first
			 * matching hashval in the node.
			 */
			while ((probe > 0) && (btree->hashval >= hashval)) {
				btree--;
				probe--;
			}
			while ((probe < max) && (btree->hashval < hashval)) {
				btree++;
				probe++;
			}

			/*
			 * Pick the right block to descend on.
			 */
			if (probe == max) {
				blk->index = max-1;
				blkno = node->btree[ max-1 ].before;
			} else {
				blk->index = probe;
				blkno = btree->before;	
			}
		} else {
			leaf = (struct xfs_dir_leafblock *)blk->bp->b_un.b_addr;
			blk->hashval = leaf->leaves[ leaf->hdr.count-1 ].hashval;
			blk->leafblk = 1;
			break;
		}
	}

	/*
	 * A leaf block that ends in the hashval that we are interested in
	 * (final hashval == search hashval) means that the next block may
	 * contain more entries with the same hashval, shift upward to the
	 * next leaf and keep searching.
	 */
	while (1) {
		max = xfs_dir_leaf_lookup_int(blk->bp, state->args,
						       &blk->index);
		if ((max == ENOENT) && (blk->hashval == state->args->hashval)) {
			if (!xfs_dir_path_shift(state, &state->path, 1, 1)) {
				continue;
			}
		}
		break;
	}
	return(XFS_ERROR(max));	
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
			     int count, xfs_mount_t *mp)
{
	struct xfs_dir_leaf_hdr *hdr_s, *hdr_d;
	struct xfs_dir_leaf_entry *entry_s, *entry_d;
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
	ASSERT((hdr_s->count > 0) && (hdr_s->count < (XFS_LBSIZE(mp)/8)));
	ASSERT(hdr_s->firstused >= 
		((hdr_s->count*sizeof(*entry_s))+sizeof(*hdr_s)));
	ASSERT(((int)(hdr_d->count) >= 0) && 
		(hdr_d->count < (XFS_LBSIZE(mp)/8)));
	ASSERT(hdr_d->firstused >= 
		((hdr_d->count*sizeof(*entry_d))+sizeof(*hdr_d)));

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
		ASSERT(entry_s->namelen < MAXNAMELEN);
		tmp = XFS_DIR_LEAF_ENTSIZE_BYENTRY(entry_s);
		hdr_d->firstused -= tmp;
		entry_d->hashval = entry_s->hashval;
		entry_d->nameidx = hdr_d->firstused;
		entry_d->namelen = entry_s->namelen;
		ASSERT(entry_d->nameidx + tmp <= XFS_LBSIZE(mp));
		bcopy((char *)XFS_DIR_LEAF_NAMESTRUCT(leaf_s, entry_s->nameidx),
		      (char *)XFS_DIR_LEAF_NAMESTRUCT(leaf_d, entry_d->nameidx),
		      tmp);
		ASSERT(entry_s->nameidx + tmp <= XFS_LBSIZE(mp));
		bzero((char *)XFS_DIR_LEAF_NAMESTRUCT(leaf_s, entry_s->nameidx),
		      tmp);
		hdr_s->namebytes -= entry_d->namelen;
		hdr_d->namebytes += entry_d->namelen;
		hdr_s->count--;
		hdr_d->count++;
		tmp  = hdr_d->count * sizeof(struct xfs_dir_leaf_entry)
				+ sizeof(struct xfs_dir_leaf_hdr);
		ASSERT(hdr_d->firstused >= tmp);

	}

	/*
	 * Zero out the entries we just copied.
	 */
	if (start_s == hdr_s->count) {
		tmp = count * sizeof(struct xfs_dir_leaf_entry);
		entry_s = &leaf_s->leaves[start_s];
		ASSERT((char *)entry_s + tmp <= (char *)leaf_s + XFS_LBSIZE(mp));
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
		ASSERT((char *)entry_s + tmp <= (char *)leaf_s + XFS_LBSIZE(mp));
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
		    ((new_leaf->leaves[ 0 ].hashval <
		      old_leaf->leaves[ 0 ].hashval) ||
		     (new_leaf->leaves[ new_leaf->hdr.count-1 ].hashval <
		      old_leaf->leaves[ old_leaf->hdr.count-1 ].hashval))) {
			before = 1;
		} else {
			before = 0;
		}
	} else {
		old_node = (struct xfs_dir_intnode *)old_blk->bp->b_un.b_addr;
		new_node = (struct xfs_dir_intnode *)new_blk->bp->b_un.b_addr;
		if ((old_node->hdr.count > 0) && (new_node->hdr.count > 0) && 
		    ((new_node->btree[ 0 ].hashval <
		      old_node->btree[ 0 ].hashval) ||
		     (new_node->btree[ new_node->hdr.count-1 ].hashval <
		      old_node->btree[ old_node->hdr.count-1 ].hashval))) {
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
			ASSERT(bp != NULL);
			tmp_info = (struct xfs_dir_blkinfo *)bp->b_un.b_addr;
			if (old_blk->leafblk) {
				ASSERT(tmp_info->magic == XFS_DIR_LEAF_MAGIC);
			} else {
				ASSERT(tmp_info->magic == XFS_DIR_NODE_MAGIC);
			}
			ASSERT(tmp_info->forw == old_blk->blkno);
			tmp_info->forw = new_blk->blkno;
			xfs_trans_log_buf(state->trans, bp, 0,
							sizeof(*tmp_info)-1);
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
			ASSERT(bp != NULL);
			tmp_info = (struct xfs_dir_blkinfo *)bp->b_un.b_addr;
			if (old_blk->leafblk) {
				ASSERT(tmp_info->magic == XFS_DIR_LEAF_MAGIC);
			} else {
				ASSERT(tmp_info->magic == XFS_DIR_NODE_MAGIC);
			}
			ASSERT(tmp_info->back == old_blk->blkno);
			tmp_info->back = new_blk->blkno;
			xfs_trans_log_buf(state->trans, bp, 0,
							sizeof(*tmp_info)-1);
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
			ASSERT(bp != NULL);
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
			ASSERT(bp != NULL);
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

/*
 * Move a path "forward" or "!forward" one block at the current level.
 *
 * This routine will adjust a "path" to point to the next block
 * "forward" (higher hashvalues) or "!forward" (lower hashvals) in the Btree,
 * including updating pointers to the intermediate nodes between the new
 * bottom and the root.
 */
STATIC int
xfs_dir_path_shift(struct xfs_dir_state *state, struct xfs_dir_state_path *path,
			  int forward, int release)
{
	struct xfs_dir_state_blk *blk;
	struct xfs_dir_blkinfo *info;
	struct xfs_dir_intnode *node;
	struct xfs_dir_leafblock *leaf;
	xfs_fsblock_t blkno;
	int level;

	/*
	 * Roll up the Btree looking for the first block where our
	 * current index is not at the edge of the block.  Note that
	 * we skip the bottom layer because we want the sibling block.
	 */
	ASSERT(path != NULL);
	ASSERT((path->active > 0) && (path->active < XFS_DIR_NODE_MAXDEPTH));
	level = path->active-2;		/* skip bottom layer in path */
	for (blk = &path->blk[level]; level >= 0; blk--, level--) {
		ASSERT(blk->bp != NULL);
		node = (struct xfs_dir_intnode *)blk->bp->b_un.b_addr;
		ASSERT(node->hdr.info.magic == XFS_DIR_NODE_MAGIC);
		if (forward && (blk->index < node->hdr.count-1)) {
			blk->index++;
			blkno = node->btree[ blk->index ].before;
			break;
		} else if (!forward && (blk->index > 0)) {
			blk->index--;
			blkno = node->btree[ blk->index ].before;
			break;
		}
	}
	if (level < 0)
		return(XFS_ERROR(ENOENT));	/* we're out of our tree */

	/*
	 * Roll down the edge of the subtree until we reach the
	 * same depth we were at originally.
	 */
	for (blk++, level++; level < path->active; blk++, level++) {
		/*
		 * Release the old block.
		 * (if it's dirty, trans won't actually let go)
		 */
		if (release)
			xfs_trans_brelse(state->trans, blk->bp);

		/*
		 * Read the next child block.
		 */
		blk->blkno = blkno;
		blk->bp = xfs_dir_read_buf(state->trans, state->args->dp, blkno);
		ASSERT(blk->bp != NULL);
		info = (struct xfs_dir_blkinfo *)blk->bp->b_un.b_addr;
		ASSERT((info->magic == XFS_DIR_LEAF_MAGIC) || \
		       (info->magic == XFS_DIR_NODE_MAGIC));
		if (info->magic == XFS_DIR_NODE_MAGIC) {
			blk->leafblk = 0;
			node = (struct xfs_dir_intnode *)info;
			blk->hashval = node->btree[ node->hdr.count-1 ].hashval;
			if (forward)
				blk->index = 0;
			else
				blk->index = node->hdr.count-1;
			blkno = node->btree[ blk->index ].before;
		} else {
			blk->leafblk = 1;
			leaf = (struct xfs_dir_leafblock *)info;
			blk->hashval = leaf->leaves[ leaf->hdr.count-1 ].hashval;
			blk->index = 0;
			ASSERT(level == path->active-1);
		}
	}
	return(0);
}

#ifndef SIM
/*
 * Print the contents of a leaf block.
 */
void
xfs_dir_leaf_print_int(buf_t *bp, xfs_inode_t *dp)
{
	struct xfs_dir_leafblock *leaf;
	struct xfs_dir_leaf_entry *entry;
	struct xfs_dir_leaf_name *namest;
	xfs_ino_t ino;
	int i;

	leaf = (struct xfs_dir_leafblock *)bp->b_un.b_addr;
	ASSERT(leaf->hdr.info.magic == XFS_DIR_LEAF_MAGIC);
	entry = &leaf->leaves[0];
	for (i = 0; i < leaf->hdr.count; entry++, i++) {
		namest = XFS_DIR_LEAF_NAMESTRUCT(leaf, entry->nameidx);
		bcopy(namest->inumber, (char *)&ino, sizeof(ino));
		printf("%20lld  %*.*s\n", ino,
				entry->namelen, entry->namelen, namest->name);
	}
}

/*
 * Copy out directory entries for getdents(), for leaf directories.
 */
int
xfs_dir_leaf_getdents_int(buf_t *bp, xfs_inode_t *dp, uio_t *uio, int *eobp,
				dirent_t *dbp)
{
	struct xfs_dir_leafblock *leaf;
	struct xfs_dir_leaf_entry *entry;
	struct xfs_dir_leaf_name *namest;
	int retval, done, entno, i;
	xfs_mount_t *mp;
	xfs_ino_t ino;
	__uint32_t bno;
	off_t nextcook;

	mp = dp->i_mount;
	bno = (__uint32_t)XFS_DIR_COOKIE_BNO(mp, uio->uio_offset);
	entno = XFS_DIR_COOKIE_ENTRY(mp, uio->uio_offset);
	leaf = (struct xfs_dir_leafblock *)bp->b_un.b_addr;
	ASSERT(leaf->hdr.info.magic == XFS_DIR_LEAF_MAGIC);
	if (entno >= leaf->hdr.count) {
		*eobp = 0;
		return(XFS_ERROR(ENOENT));
	}
	entry = &leaf->leaves[entno];
	for (i = entno; i < leaf->hdr.count; entry++, i++) {
		namest = XFS_DIR_LEAF_NAMESTRUCT(leaf, entry->nameidx);
		bcopy(namest->inumber, (char *)&ino, sizeof(ino));
		if (i == leaf->hdr.count - 1) {
			if (leaf->hdr.info.forw)
				nextcook = XFS_DIR_MAKE_COOKIE(mp, leaf->hdr.info.forw, 0);
			else
				nextcook = XFS_DIR_MAKE_COOKIE(mp, XFS_B_TO_FSBT(mp, dp->i_d.di_size), 0);
		} else
			nextcook = XFS_DIR_MAKE_COOKIE(mp, bno, i + 1);
		retval = xfs_dir_put_dirent(mp, dbp, ino, 
				(char *)(namest->name), entry->namelen, 
				nextcook, uio, &done);
		if (!done) {
			uio->uio_offset = XFS_DIR_MAKE_COOKIE(mp, bno, i);
			*eobp = 0;
			return(retval);
		}
	}
	uio->uio_offset = XFS_DIR_MAKE_COOKIE(mp, bno, i);
	*eobp = 1;
	return(0);
}
#endif	/* !SIM */

#ifdef XFSDIRDEBUG


/*========================================================================
 * Transaction tracing routines.
 *========================================================================*/

#undef xfs_trans_binval
#undef xfs_trans_brelse
#undef xfs_trans_log_buf
#undef xfs_trans_log_inode
#undef xfs_dir_read_buf
struct xfsdir_buftrace xfsdir_tracebuf[BUFTRACEMAX];
int xfsdir_trc_head = 0;
int xfsdir_trc_count = 0;
void
xfsdir_t_reinit(char *description, char *file, int line)
{
	if (xfsdir_debug) {
		xfsdir_tracebuf[ xfsdir_trc_head ].which = 1;
		xfsdir_tracebuf[ xfsdir_trc_head ].line = line;
		xfsdir_tracebuf[ xfsdir_trc_head ].file = file;
		xfsdir_tracebuf[ xfsdir_trc_head ].bp = (buf_t *)description;
		xfsdir_trc_head = (xfsdir_trc_head + 1) % BUFTRACEMAX;
		xfsdir_trc_count++;
	}
}
void
xfsdir_t_binval(xfs_trans_t *tp, buf_t *bp, char *file, int line)
{
	if (xfsdir_debug) {
		xfsdir_tracebuf[ xfsdir_trc_head ].which = 2;
		xfsdir_tracebuf[ xfsdir_trc_head ].line = line;
		xfsdir_tracebuf[ xfsdir_trc_head ].file = file;
		xfsdir_tracebuf[ xfsdir_trc_head ].bp = bp;
		xfsdir_trc_head = (xfsdir_trc_head + 1) % BUFTRACEMAX;
		xfsdir_trc_count++;
	}
	xfs_trans_binval(tp, bp);
}
void
xfsdir_t_brelse(xfs_trans_t *tp, buf_t *bp, char *file, int line)
{
	if (xfsdir_debug) {
		xfsdir_tracebuf[ xfsdir_trc_head ].which = 3;
		xfsdir_tracebuf[ xfsdir_trc_head ].line = line;
		xfsdir_tracebuf[ xfsdir_trc_head ].file = file;
		xfsdir_tracebuf[ xfsdir_trc_head ].bp = bp;
		xfsdir_trc_head = (xfsdir_trc_head + 1) % BUFTRACEMAX;
		xfsdir_trc_count++;
	}
	xfs_trans_brelse(tp, bp);
}
void
xfsdir_t_log_buf(xfs_trans_t *tp, buf_t *bp, uint first, uint last,
			     char *file, int line)
{
	if (xfsdir_debug) {
		xfsdir_tracebuf[ xfsdir_trc_head ].which = 4;
		xfsdir_tracebuf[ xfsdir_trc_head ].line = line;
		xfsdir_tracebuf[ xfsdir_trc_head ].file = file;
		xfsdir_tracebuf[ xfsdir_trc_head ].bp = bp;
		xfsdir_trc_head = (xfsdir_trc_head + 1) % BUFTRACEMAX;
		xfsdir_trc_count++;
	}
	xfs_trans_log_buf(tp, bp, first, last);
}
void
xfsdir_t_log_inode(xfs_trans_t *tp, xfs_inode_t *ip, uint flags,
			       char *file, int line)
{
	if (xfsdir_debug) {
		xfsdir_tracebuf[ xfsdir_trc_head ].which = 5;
		xfsdir_tracebuf[ xfsdir_trc_head ].line = line;
		xfsdir_tracebuf[ xfsdir_trc_head ].file = file;
		xfsdir_tracebuf[ xfsdir_trc_head ].bp = (buf_t *)ip;
		xfsdir_trc_head = (xfsdir_trc_head + 1) % BUFTRACEMAX;
		xfsdir_trc_count++;
	}
	xfs_trans_log_inode(tp, ip, flags);
}
buf_t *
xfsdir_t_dir_read_buf(xfs_trans_t *trans, xfs_inode_t *ip, xfs_fsblock_t bno,
				  char *file, int line)
{
	buf_t *bp;

	bp = xfs_dir_read_buf(trans, ip, bno);
	if (xfsdir_debug) {
		xfsdir_tracebuf[ xfsdir_trc_head ].which = 6;
		xfsdir_tracebuf[ xfsdir_trc_head ].line = line;
		xfsdir_tracebuf[ xfsdir_trc_head ].file = file;
		xfsdir_tracebuf[ xfsdir_trc_head ].bp = bp;
		xfsdir_trc_head = (xfsdir_trc_head + 1) % BUFTRACEMAX;
		xfsdir_trc_count++;
	}
	return(bp);
}
xfsdir_dumptrace()
{
	struct xfsdir_buftrace *btp;
	int count;

	btp = &xfsdir_tracebuf[ xfsdir_trc_head ];
	for (count = 0; count < BUFTRACEMAX; count++, btp++) {
		if (btp == &xfsdir_tracebuf[ BUFTRACEMAX ])
			btp = &xfsdir_tracebuf[ 0 ];
		switch(btp->which) {
		case 0:
			break;
		case 1:
			printf("%d NEWOP  \"%s\" at %s line %d\n",
				   count, btp->bp, btp->file, btp->line);
			break;
		case 2:
			printf("%d BINVAL 0x%x (0x%x) at %s line %d\n",
				   count, btp->bp, btp->bp->b_blkno,
				   btp->file, btp->line);
			break;
		case 3:
			printf("%d BRELSE 0x%x (0x%x) at %s line %d\n",
				   count, btp->bp, btp->bp->b_blkno,
				   btp->file, btp->line);
			break;
		case 4:
			printf("%d LOGBUF 0x%x (0x%x) at %s line %d\n",
				   count, btp->bp, btp->bp->b_blkno,
				   btp->file, btp->line);
			break;
		case 5:
			printf("%d LOGINO 0x%x at %s line %d\n",
				   count, btp->bp, btp->file, btp->line);
			break;
		case 6:
			printf("%d READ   0x%x (0x%x) at %s line %d\n",
				   count, btp->bp, btp->bp->b_blkno,
				   btp->file, btp->line);
			break;
		default:
			printf("%d ?????? 0x%x at %s line %d\n",
				   count, btp->bp, btp->file, btp->line);
			break;
		}
	}
	printf("%d total operations\n", xfsdir_trc_count);
	return(0);
}

/*========================================================================
 * Load all of the block in a directory into memory so they can be seen.
 *========================================================================*/

int xfsdir_load_leaf(xfs_inode_t *dp, xfs_fsblock_t blkno);
int xfsdir_load_node(xfs_inode_t *dp, xfs_fsblock_t blkno);

int
xfsdir_loaddir(xfs_inode_t *dp)
{
	int retval;

	if (dp->i_d.di_size <= XFS_LITINO(dp->i_mount)) {
		retval = 0;
	} else if (dp->i_d.di_size == XFS_LBSIZE(dp->i_mount)) {
		retval = xfsdir_load_leaf(dp, 0);
	} else {
		retval = xfsdir_load_node(dp, 0);
	}
	return(retval);
}

int
xfsdir_load_leaf(xfs_inode_t *dp, xfs_fsblock_t blkno)
{
	struct xfs_dir_leafblock *leaf;
	buf_t *bp;

	bp = xfs_dir_read_buf(dp->i_transp, dp, blkno);
	leaf = (struct xfs_dir_leafblock *)bp->b_un.b_addr;
	printf("leaf[0x%x]  bp 0x%x  forw 0x%x  back 0x%x  count %d\n",
			    blkno, bp,
			    leaf->hdr.info.forw, leaf->hdr.info.back,
			    leaf->hdr.count);
	return(0);
}

int
xfsdir_load_node(xfs_inode_t *dp, xfs_fsblock_t blkno)
{
	struct xfs_dir_intnode *node;
	struct xfs_dir_node_entry *btree;
	int retval, i;
	buf_t *bp;


	bp = xfs_dir_read_buf(dp->i_transp, dp, blkno);
	node = (struct xfs_dir_intnode *)bp->b_un.b_addr;
	printf("node[0x%x]  bp 0x%x  forw 0x%x  back 0x%x  count %d  level %d\n",
			    blkno, bp,
			    node->hdr.info.forw, node->hdr.info.back,
			    node->hdr.count, node->hdr.level);
	btree = &node->btree[0];
	for (i = 0; i < node->hdr.count; btree++, i++) {
		if (node->hdr.level == 1) {
			retval += xfsdir_load_leaf(dp, btree->before);
		} else {
			retval += xfsdir_load_node(dp, btree->before);
		}
	}
	return(retval);
}


/*========================================================================
 * Check a directory for internal inconsistencies
 *========================================================================*/

#define BADNEWS0(MSG)			BADNEWSN(MSG, 0, 0, 0)
#define BADNEWS1(MSG, VAL1)		BADNEWSN(MSG, VAL1, 0, 0)
#define BADNEWS2(MSG, VAL1, VAL2)	BADNEWSN(MSG, VAL1, VAL2, 0)
#define BADNEWS3(MSG, VAL1, VAL2, VAL3)	BADNEWSN(MSG, VAL1, VAL2, VAL3)
#define BADNEWSN(MSG, VAL1, VAL2, VAL3)	retval++, printf("bp 0x%x " MSG, \
					     bp, VAL1, VAL2, VAL3)

/*
 * Directory structure checking data, context for each process.
 */
struct xfsdir_context {
	xfs_inode_t	*dp;		/* directory inode */
	uint		hashval;	/* last hashval seen in tree */
	int		maxblockmap;	/* size of block-use map */
	char		*blockmap;	/* map of blocks and their uses */
	int		maxlinkmap;	/* size of forw/back link map */
	xfs_fsblock_t	*forw_links;	/* map of forward chain links */
	xfs_fsblock_t	*back_links;	/* map of backward chain links */
	int		bytemapsize;	/* size of used/free bytemap */
	char		*bytemap;	/* used/free bytes in leaf blk */
	int		namebytes;	/* total bytes used in filenames */
	int		freebytes;	/* total free bytes for filenames */
	int		holeblks;	/* total leaves with holes */
	int		entries;	/* total directory entries */
	int		nodes;		/* node blocks seen */
	int		leaves;		/* leaf blocks seen */
};


int xfsdir_leaf_check(struct xfsdir_context *con, xfs_fsblock_t blkno,
			     int firstlast);
int xfsdir_node_check(struct xfsdir_context *con, xfs_fsblock_t blkno,
			     int depth, int firstlast);
int xfsdir_check_blockuse(struct xfsdir_context *con);
int xfsdir_checkchain(struct xfsdir_context *con, buf_t *bp,
			     xfs_fsblock_t parentblk,
			     xfs_fsblock_t blkno, int forward);
int xfsdir_endchain(struct xfsdir_context *con, buf_t *bp, xfs_fsblock_t blkno);
int xfsdir_checkblock(struct xfsdir_context *con, buf_t *bp,
			     xfs_fsblock_t blkno, int level);
int xfsdir_checkname(buf_t *bp, char *name, int namelen, int index,
			   uint hashval);
int xfsdir_printbytes(int bytes);

extern uint xfs_dir_hashname(register char *name, register int namelen);


int
xfsdir_check(xfs_inode_t *dp)
{
	struct xfsdir_context *con;
	int numblks, tmp, retval;
	uint hashval;

	con = (struct xfsdir_context *)kmem_zalloc(sizeof(*con), KM_SLEEP);
	numblks = (dp->i_d.di_size + XFS_LBSIZE(dp->i_mount) - 1) /
					XFS_LBSIZE(dp->i_mount);
	con->maxblockmap = numblks;
	con->blockmap = (char *)
		kmem_zalloc(numblks * sizeof(*con->blockmap), KM_SLEEP);
	con->maxlinkmap = numblks;
	con->forw_links = (xfs_fsblock_t *)
		kmem_zalloc(numblks * sizeof(*con->forw_links), KM_SLEEP);
	con->back_links = (xfs_fsblock_t *)
		kmem_zalloc(numblks * sizeof(*con->back_links), KM_SLEEP);
	con->bytemapsize = XFS_LBSIZE(dp->i_mount);
	con->bytemap = (char *)
		kmem_zalloc(con->bytemapsize * sizeof(*con->bytemap), KM_SLEEP);
	con->namebytes = con->freebytes = con->holeblks = con->entries = 0;

	/*
	 * Decide on what work routines to call based on the inode size.
	 */
	printf("XFSDIR_CHECK: ");
	con->dp = dp;
	retval = 0;
	if (dp->i_d.di_size <= XFS_LITINO(dp->i_mount)) {
		retval = 0; /* xfsdir_shortform_check(con); */
	} else if (dp->i_d.di_size == XFS_LBSIZE(dp->i_mount)) {
		retval = xfsdir_leaf_check(con, 0, -2);
	} else {
		retval = xfsdir_node_check(con, (xfs_fsblock_t)0, -1, -2);
		retval += xfsdir_check_blockuse(con);
		printf("%d nodes, %d leaves, %d files, ",
			   con->nodes, con->leaves, con->entries);
		xfsdir_printbytes(con->namebytes);
		printf(" used, ");
		xfsdir_printbytes(con->freebytes);
		tmp = con->namebytes + con->freebytes;
		printf(" (%d%%) free\n", (tmp>0)?((con->freebytes*100)/tmp):0);
	}

	kmem_free(con->bytemap, con->bytemapsize * sizeof(*con->bytemap));
	kmem_free(con->blockmap, con->maxblockmap * sizeof(*con->blockmap));
	kmem_free(con->forw_links, con->maxlinkmap * sizeof(*con->forw_links));
	kmem_free(con->back_links, con->maxlinkmap * sizeof(*con->back_links));
	kmem_free(con, sizeof(*con));

	if (retval) {
		debug("directory check");
	}
	return(retval);
}

/*========================================================================
 * Short form checking routines.
 *========================================================================*/

#ifdef NOTYET
int
xfsdir_shortform_check(struct xfsdir_context *con)
{
	struct xfs_dir_shortform *sf;
	struct xfs_dir_sf_entry *sfe;
	int retval, total, i;
	buf_t *bp;

	sf = (struct xfs_dir_shortform *)bp->buffer;
	if (sf->hdr.count > (XFS_LITINO(fs)/sizeof(struct xfs_dir_sf_entry)))
		BADNEWS1("shortform entry count bad: count %d\n",
				    sf->hdr.count);

	sfe = &sf->list[0];
	total = sizeof(struct xfs_dir_sf_hdr);
	for (i = sf->hdr.count-1; i >= 0; i--) {
		if ((sfe->namelen == 0) || (sfe->namelen > MAXNAMELEN) ||
		    (sfe->namelen > XFS_LITINO(fs)) ||
		    ((sfe->namelen + total) > dp->i_d.di_size))
			BADNEWS1("bad shortform name length: entry %d\n", i);

		con->hashval = xfsdir_hashname(sfe->name, sfe->namelen);
		retval += xfsdir_checkname(sfe->name, sfe->namelen,
						      con->hashval);

		total += XFS_DIR_SF_ENTSIZE_BYENTRY(sfe);
		sfe = XFS_DIR_SF_NEXTENTRY(sfe);
	}
	if (total != dp->i_d.di_size)
		BADNEWS0("more stuff in inode than should be\n");
	xfs_trans_irelse(NULL, bp);
	return(retval);
}
#endif /* NOTYET */

/*========================================================================
 * Long form manipulation routines.
 *========================================================================*/

int
xfsdir_leaf_check(struct xfsdir_context *con, xfs_fsblock_t blkno,
			 int firstlast)
{
	struct xfs_dir_leafblock *leaf;
	struct xfs_dir_leaf_entry *entry;
	struct xfs_dir_leaf_name *namest;
	struct xfs_dir_leaf_map *map;
	int lowest, bytes, retval, i, j, k;
	buf_t *bp;
	char *ch;

	/*
	 * Read in the leaf block.
	 */
	bp = xfs_dir_read_buf(con->dp->i_transp, con->dp, blkno);
	leaf = (struct xfs_dir_leafblock *)bp->b_un.b_addr;

	/*
	 * Check the header fields.
	 */
	retval = 0;
	if (leaf->hdr.info.magic != XFS_DIR_LEAF_MAGIC) {
		BADNEWS1("not a leaf node: blkno 0x%x\n", blkno);
		return(retval);
	}
	if (leaf->hdr.pad1 != 0)
		BADNEWS1("pad1 is non-zero: leaf 0x%x\n", blkno);

	/*
	 * Check the chain of leaves
	 */
	if (firstlast == 0) {
		retval += xfsdir_checkchain(con, bp, blkno,
						 leaf->hdr.info.forw, 1);
		retval += xfsdir_checkchain(con, bp, blkno,
						 leaf->hdr.info.back, 0);
	} else {
		retval += xfsdir_endchain(con, bp, blkno);
		if (firstlast < 0) {
			if (firstlast == -1)
				retval += xfsdir_checkchain(con, bp, blkno,
						      leaf->hdr.info.forw, 1);
			if (leaf->hdr.info.back != 0)
				BADNEWS2("backward ref to leaf 0x%x in leaf 0x%x\n",
						   leaf->hdr.info.back, blkno);
		}
		if (firstlast > 0) {
			retval += xfsdir_checkchain(con, bp, blkno,
						 leaf->hdr.info.back, 0);
			if (leaf->hdr.info.forw != 0)
				BADNEWS2("forward ref to leaf 0x%x in leaf 0x%x\n",
						  leaf->hdr.info.forw, blkno);
		}
	}

	/*
	 * Set up the used/free byte map for this block.
	 */
	bzero(con->bytemap, con->bytemapsize);
	j = leaf->hdr.count * sizeof(struct xfs_dir_leaf_entry)
				+ sizeof(struct xfs_dir_leaf_hdr);
	for (ch = con->bytemap, i = 0; i < j; i++)
		*ch++ = 1;

	/*
	 * Check the entries
	 * XXX: check the count.
	 */
	bytes = 0;
	lowest = XFS_LBSIZE(con->dp->i_mount);
	if (lowest == 64*1024)
		lowest = XFS_LBSIZE(con->dp->i_mount) - 1;
	entry = &leaf->leaves[0];
	if (leaf->hdr.count == 0)
		BADNEWS1("zero entry count: leaf 0x%x\n", blkno);
	con->entries += leaf->hdr.count;
	for (i = 0; i < leaf->hdr.count; entry++, i++) {
		if (entry->hashval < con->hashval) {
			BADNEWS2("hashval is smaller than last: leaf 0x%x, index %d\n",
					  blkno, i);
		}
		con->hashval = entry->hashval;
		if (entry->nameidx >= XFS_LBSIZE(con->dp->i_mount)) {
			BADNEWS2("nameidx is too large: leaf 0x%x, index %d\n",
					  blkno, i);
		} else {
			if (entry->nameidx < lowest)
				lowest = entry->nameidx;

			namest = XFS_DIR_LEAF_NAMESTRUCT(leaf, entry->nameidx);
			retval += xfsdir_checkname(bp, namest->name,
						       entry->namelen, i,
						       entry->hashval);
			bytes += entry->namelen;

			ch = &con->bytemap[entry->nameidx];
			for (j = 0; j < XFS_DIR_LEAF_ENTSIZE_BYENTRY(entry); j++) {
				*ch++ = 1;
			}
		}
		if (entry->pad2 != 0)
			BADNEWS1("pad2 is non-zero: leaf 0x%x\n", blkno);
	}

	/*
	 * Check more header fields after looking at each entry.
	 */
	if (leaf->hdr.namebytes != bytes)
		BADNEWS3("namebytes (0x%x) not right (0x%x): leaf 0x%x\n",
				    (int)leaf->hdr.namebytes, bytes, blkno);
	con->namebytes += bytes;

	map = &leaf->hdr.freemap[0];
	for (i = 0; i < XFS_DIR_LEAF_MAPSIZE; map++, i++) {
		if (map->base >= XFS_LBSIZE(con->dp->i_mount)) {
			BADNEWS2("freemap[%d].base is too large: leaf 0x%x\n",
						   i, blkno);
			continue;
		}
		j = sizeof(struct xfs_dir_leaf_hdr) +
			leaf->hdr.count * sizeof(struct xfs_dir_leaf_entry);
		if ((map->base < j) && (map->size > 0)) {
			BADNEWS2("freemap[%d].base is too small: leaf 0x%x\n",
						   i, blkno);
			continue;
		}
		if (map->size >= XFS_LBSIZE(con->dp->i_mount)) {
			BADNEWS2("freemap[%d].size is too large: leaf 0x%x\n",
						   i, blkno);
			continue;
		}

		for (ch = &con->bytemap[map->base], j = k = 0; j < map->size; j++) {
			if ((*ch != 0) && (k == 0)) {
				BADNEWS2("freemap[%d] overlaps used space: leaf 0x%x\n",
						      i, blkno);
			      k = 1;	/* don't print more than one message */
			}
			*ch++ = 1;
		}
		con->freebytes += map->size;
	}

	if (leaf->hdr.holes == 0) {
		for (ch = con->bytemap, i = 0; i < con->bytemapsize; i++) {
			if (*ch++ == 0) {
				BADNEWS1("space not mapped but holes == 0: leaf 0x%x\n",
						blkno);
				break;
			}
		}
	}

	if (leaf->hdr.holes) {
		con->holeblks++;
		if (leaf->hdr.firstused > lowest)
			BADNEWS3("firstused (0x%x) too high (0x%x), holes: leaf 0x%x\n",
					    (int)leaf->hdr.firstused,
					    lowest, blkno);
	} else {
		if (leaf->hdr.firstused != lowest)
			BADNEWS3("firstused (0x%x) not right (0x%x), no holes: leaf 0x%x\n",
					    (int)leaf->hdr.firstused,
					    lowest, blkno);
	}

	if (retval == 0)
		xfs_trans_brelse(con->dp->i_transp, bp);
	return(retval);
}

/*========================================================================
 * Huge form manipulation routines.
 *========================================================================*/

int
xfsdir_node_check(struct xfsdir_context *con, xfs_fsblock_t blkno,
			 int depth, int firstlast)
{
	struct xfs_dir_intnode *node;
	struct xfs_dir_node_entry *btree;
	int retval, count, tmp, i;
	buf_t *bp;

	/*
	 * Read the next node down in the tree.
	 */
	bp = xfs_dir_read_buf(con->dp->i_transp, con->dp, blkno);
	node = (struct xfs_dir_intnode *)bp->b_un.b_addr;

	/*
	 * Check the header fields.
	 */
	retval = 0;
	if (node->hdr.info.magic != XFS_DIR_NODE_MAGIC) {
		BADNEWS1("not an int node: blkno 0x%x\n", blkno);
		return(retval);
	}
	if (depth >= XFS_DIR_NODE_MAXDEPTH)
		BADNEWS1("tree too deep: node 0x%x\n", blkno);
	if ((depth >= 0) && (node->hdr.level != depth))
		BADNEWS1("level not right: node 0x%x\n", blkno);
	depth = node->hdr.level;
	if (node->hdr.count == 0)
		BADNEWS1("zero entry count: node 0x%x\n", blkno);
	if (node->hdr.count > XFS_DIR_NODE_ENTRIES(con->dp->i_mount)) {
		BADNEWS1("bad node entry count: node 0x%x\n", blkno);
		count = XFS_DIR_NODE_ENTRIES(con->dp->i_mount);
	} else
		count = node->hdr.count;

	/*
	 * Check the chain of nodes
	 */
	if (firstlast == 0) {
		retval += xfsdir_checkchain(con, bp, blkno,
						 node->hdr.info.forw, 1);
		retval += xfsdir_checkchain(con, bp, blkno,
						 node->hdr.info.back, 0);
	} else {
		retval += xfsdir_endchain(con, bp, blkno);
		if (firstlast < 0) {
			if (firstlast == -1)
				retval += xfsdir_checkchain(con, bp, blkno,
						      node->hdr.info.forw, 1);
			if (node->hdr.info.back != 0)
				BADNEWS2("backward ref to node 0x%x in node 0x%x\n",
						   node->hdr.info.back, blkno);
		}
		if (firstlast > 0) {
			retval += xfsdir_checkchain(con, bp, blkno,
						 node->hdr.info.back, 0);
			if (node->hdr.info.forw != 0)
				BADNEWS2("forward ref to node 0x%x in node 0x%x\n",
						  node->hdr.info.forw, blkno);
		}
	}

	/*
	 * Do a linear pass through the entries.
	 */
	btree = &node->btree[0];
	for (i = 0; i < XFS_DIR_NODE_ENTRIES(con->dp->i_mount); btree++, i++) {
		if (i < count) {
			if ((btree->hashval == 0) && (btree->before == 0)) {
				BADNEWS1("zero entry, count too large: node 0x%x\n",
					       blkno);
				continue;
			}
		} else {
			if ((btree->hashval == 0) && (btree->before == 0)) {
				continue;
			}
			BADNEWS1("non-zero entry, count too small: node 0x%x\n",
					   blkno);
		}

		if (btree->hashval < con->hashval) {
			BADNEWS2("hashval is smaller than last: node 0x%x, index %d\n",
					  blkno, i);
		}

		if (tmp = xfsdir_checkblock(con, bp, btree->before,
						 node->hdr.level-1)) {
			retval += tmp;
			BADNEWS1(" in: node 0x%x\n", blkno);
			retval--;
		} else {
			/*
			 * Track first/last on each level of tree.
			 */
			tmp = 0;
			if (firstlast == -2) {
				if (i == 0)
					tmp = -1;
				else if (i == count-1)
					tmp = 1;
			} else if ((firstlast < 0) && (i == 0))
				tmp = -1;
			else if ((firstlast > 0) && (i == (count-1)))
				tmp = 1;

			if (node->hdr.level == 1) {
				retval += xfsdir_leaf_check(con, btree->before,
							     tmp);
			} else {
				retval += xfsdir_node_check(con, btree->before,
							     depth-1, tmp);
			}
		}

		con->hashval = btree->hashval;
	}

	if (retval == 0)
		xfs_trans_brelse(con->dp->i_transp, bp);
	return(retval);
}

/*========================================================================
 * Utility routines.
 *========================================================================*/

int
xfsdir_checkname(buf_t *bp, char *name, int namelen, int index, uint hashval)
{
	int retval;

	retval = 0;
	if (xfs_dir_hashname(name, namelen) != hashval)
		BADNEWS2("hashval doesn't match name at index %d: \"%s\"\n",
				  index, name);
	return(retval);
}

int
xfsdir_checkblock(struct xfsdir_context *con, buf_t *bp,
			 xfs_fsblock_t blkno, int level)
{
	int retval;

	retval = 0;
	if ((blkno == 0) || (blkno >= con->maxblockmap))
		BADNEWS1("block number out of range: blkno 0x%x", blkno);
	else if (con->blockmap[blkno] & 0x01)
		BADNEWS1("block multiply used: blkno 0x%x", blkno);
	else {
		con->blockmap[blkno] |= 0x01;
		con->blockmap[blkno] |= (level == 0) ? 0x02 : 0x04;
	}

	return(retval);
}

int
xfsdir_endchain(struct xfsdir_context *con, buf_t *bp, xfs_fsblock_t blkno)
{
	int retval;

	retval = 0;
	if (con->blockmap[blkno] & 0x20)
		BADNEWS1("blkno 0x%x is already a start/end blk\n", blkno);
	con->blockmap[blkno] |= 0x20;
	return(retval);
}

int
xfsdir_checkchain(struct xfsdir_context *con, buf_t *bp,
			 xfs_fsblock_t parentblk, xfs_fsblock_t blkno,
			 int forward)
{
	int retval, tmp, i;
	xfs_fsblock_t *links;

	retval = 0;
	if (blkno >= con->maxblockmap) {
		BADNEWS2("block number 0x%x out of range in blkno 0x%x\n",
				blkno, parentblk);
		return(retval);
	} else if (blkno == 0) {
		BADNEWS2("zero %s reference from blkno 0x%x\n",
			       forward ? "forward" : "backward",
			       parentblk);
		return(retval);
	}

	if (forward) {
		links = con->forw_links;
		tmp = 0x80;
	} else {
		links = con->back_links;
		tmp = 0x40;
	}

	if (con->blockmap[blkno] & tmp) {
		BADNEWS3("multiple %s references to blkno 0x%x; from blkno 0x%x",
				   forward ? "forward" : "backward",
				   blkno, parentblk);
		for (i = 0; i < con->maxlinkmap; i++) {
			if (links[i] == blkno) {
				printf(" and blkno 0x%x", i);
			}
		}
		printf("\n");
	}
	con->blockmap[blkno] |= tmp;
	links[parentblk] = blkno;

	return(retval);
}

int
xfsdir_check_blockuse(struct xfsdir_context *con)
{
	int leaves, nodes, retval, i;
	buf_t *bp;

	bp = NULL;
	leaves = nodes = retval = 0;
	for (i = 0; i < con->maxblockmap; i++) {
		if ((con->blockmap[i] & 0x02) && (con->blockmap[i] & 0x04)) {
			BADNEWS1("block 0x%x is both a leaf and a node\n", i);
		} else if ((con->blockmap[i] & 0x02) != 0) {
			leaves++;
		} else if (((con->blockmap[i] & 0x04) != 0) || (i == 0)) {
			nodes++;
		}

		if ((con->blockmap[i] & 0x01) == 0) {
#ifdef XXX
			if (i != 0)
				printf("block 0x%x not referenced\n", i);
#endif
			if ((con->blockmap[i] & 0x80) == 1)
				BADNEWS1("block 0x%x is someone's \"forward\"\n", i);
			if ((con->blockmap[i] & 0x40) == 1)
				BADNEWS1("block 0x%x is someone's \"backward\"\n", i);
		} else if ((con->blockmap[i] & 0x20) == 0) {
			if ((con->blockmap[i] & 0x80) == 0)
				BADNEWS1("block 0x%x not someone's \"forward\"\n", i);
			else if ((con->blockmap[i] & 0x40) == 0)
				BADNEWS1("block 0x%x not someone's \"backward\"\n", i);
		} else {
			if ((con->blockmap[i] & 0x80) == 1)
				BADNEWS1("block 0x%x a start/end blk but also someone's \"forward\"\n", i);
			else if ((con->blockmap[i] & 0x40) == 1)
				BADNEWS1("block 0x%x a start/end blk but also someone's \"backward\"\n", i);
		}
	}
	con->nodes = nodes;
	con->leaves = leaves;
	return(retval);
}

int
xfsdir_printbytes(int bytes)
{
	if (bytes >= 1024*1024) {
		printf("%d.%dMB", bytes/(1024*1024), (bytes%(1024*1024))/10240);
	} else if (bytes >= 1024) {
		printf("%d.%dKB", bytes/1024, (bytes%1024)/102);
	} else {
		printf("%dB", bytes);
	}
}
#endif /* XFSDIRDEBUG */
