/*
 * xfs_da_btree.c
 *
 * GROT: figure out how to recover gracefully when bmap returns ENOSPC.
 */

#ifdef SIM
#define _KERNEL 1
#endif
#include <sys/param.h>
#ifdef SIM
#undef _KERNEL
#endif
#include <sys/errno.h>
#include <sys/buf.h>
#include <sys/vnode.h>
#include <sys/kmem.h>
#include <sys/debug.h>
#include <sys/dirent.h>
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
#else
#include <sys/attributes.h>
#endif
#include "xfs_log.h"
#include "xfs_trans.h"
#include "xfs_sb.h"
#include "xfs_ag.h"
#include "xfs_mount.h"
#include "xfs_alloc_btree.h"
#include "xfs_bmap_btree.h"
#include "xfs_ialloc_btree.h"
#include "xfs_alloc.h"
#include "xfs_bmap.h"
#include "xfs_btree.h"
#include "xfs_attr_sf.h"
#include "xfs_dir_sf.h"
#include "xfs_dinode.h"
#include "xfs_inode_item.h"
#include "xfs_inode.h"
#include "xfs_da_btree.h"
#ifndef SIM
#include "xfs_attr.h"
#include "xfs_attr_leaf.h"
#endif
#include "xfs_dir.h"
#include "xfs_dir_leaf.h"
#include "xfs_error.h"
#ifdef SIM
#include "sim.h"
#endif

#ifdef XFSDADEBUG
int xfsda_debug = 1;		/* interval for fsck at split/join ops */
int xfsda_debug_cnt = 1;	/* counter for fsck at split/join ops */
int xfsda_check(xfs_inode_t *dp, int whichfork);
int xfsda_loaddir(xfs_inode_t *dp);
int xfsda_loadattr(xfs_inode_t *dp);
#endif /* XFSDADEBUG */

/*
 * xfs_da_btree.c
 *
 * Routines to implement directories as Btrees of hashed names.
 */

/*========================================================================
 * Function prototypes for the kernel.
 *========================================================================*/

/*
 * Routines used for growing the Btree.
 */
STATIC int xfs_da_root_split(xfs_da_state_t *state,
					    xfs_da_state_blk_t *existing_root,
					    xfs_da_state_blk_t *new_child);
STATIC int xfs_da_node_split(xfs_da_state_t *state,
					    xfs_da_state_blk_t *existing_blk,
					    xfs_da_state_blk_t *split_blk,
					    xfs_da_state_blk_t *blk_to_add,
					    int treelevel,
					    int *result);
STATIC void xfs_da_node_rebalance(xfs_da_state_t *state,
					 xfs_da_state_blk_t *node_blk_1,
					 xfs_da_state_blk_t *node_blk_2);
STATIC void xfs_da_node_add(xfs_da_state_t *state,
				   xfs_da_state_blk_t *old_node_blk,
				   xfs_da_state_blk_t *new_node_blk);

#ifndef SIM
/*
 * Routines used for shrinking the Btree.
 */
STATIC int xfs_da_root_join(xfs_da_state_t *state,
					   xfs_da_state_blk_t *root_blk);
STATIC int xfs_da_node_toosmall(xfs_da_state_t *state, int *retval);
STATIC void xfs_da_node_remove(xfs_da_state_t *state,
					      xfs_da_state_blk_t *drop_blk);
STATIC void xfs_da_node_unbalance(xfs_da_state_t *state,
					 xfs_da_state_blk_t *src_node_blk,
					 xfs_da_state_blk_t *dst_node_blk);
#endif	/* !SIM */

/*
 * Utility routines.
 */
uint	xfs_da_node_lasthash(buf_t *bp, int *count);
int	xfs_da_node_order(buf_t *node1_bp, buf_t *node2_bp);


/*========================================================================
 * Routines used for growing the Btree.
 *========================================================================*/

/*
 * Create the initial contents of an intermediate node.
 */
int
xfs_da_node_create(xfs_trans_t *trans, xfs_inode_t *dp, xfs_fileoff_t blkno,
			       int level, buf_t **bpp, int whichfork)
{
	xfs_da_intnode_t *node;
	buf_t *bp;
	int error;

	error = xfs_da_get_buf(trans, dp, blkno, &bp, whichfork);
	if (error)
		return(error);
	ASSERT(bp != NULL);
	bzero((char *)bp->b_un.b_addr, XFS_LBSIZE(dp->i_mount));
	node = (xfs_da_intnode_t *)bp->b_un.b_addr;
	node->hdr.info.magic = XFS_DA_NODE_MAGIC;
	node->hdr.level = level;

	xfs_trans_log_buf(trans, bp, 0, XFS_LBSIZE(dp->i_mount) - 1);

	*bpp = bp;
	return(0);
}

/*
 * Split a leaf node, rebalance, then possibly split
 * intermediate nodes, rebalance, etc.
 */
int							/* error */
xfs_da_split(xfs_da_state_t *state)
{
	xfs_da_state_blk_t *oldblk, *newblk, *addblk;
	xfs_da_intnode_t *node;
	int max, action, error, i;

	/*
	 * Walk back up the tree splitting/inserting/adjusting as necessary.
	 * If we need to insert and there isn't room, split the node, then
	 * decide which fragment to insert the new block from below into.
	 * Note that we may split the root this way, but we need more fixup.
	 */
	max = state->path.active - 1;
	ASSERT((max >= 0) && (max < XFS_DA_NODE_MAXDEPTH));
	ASSERT((state->path.blk[max].magic == XFS_ATTR_LEAF_MAGIC) || \
	       (state->path.blk[max].magic == XFS_DIR_LEAF_MAGIC));
	addblk = &state->path.blk[max];		/* initial dummy value */
	for (i = max; (i >= 0) && addblk; state->path.active--, i--) {
		oldblk = &state->path.blk[i];
		newblk = &state->altpath.blk[i];

		/*
		 * If a leaf node then
		 *     Allocate a new leaf node, then rebalance across them.
		 * else if an intermediate node then
		 *     We split on the last layer, must we split the node?
		 */
		switch (oldblk->magic) {
		case XFS_ATTR_LEAF_MAGIC:
#ifdef SIM
			error = ENOTTY;
#else
			error = xfs_attr_leaf_split(state, oldblk, newblk);
#endif
			if (error)
				return(error);	/* GROT: dir is inconsistent */
			addblk = newblk;
			break;
		case XFS_DIR_LEAF_MAGIC:
			error = xfs_dir_leaf_split(state, oldblk, newblk);
			if (error)
				return(error);	/* GROT: dir is inconsistent */
			addblk = newblk;
			break;
		case XFS_DA_NODE_MAGIC:
			error = xfs_da_node_split(state, oldblk, newblk, addblk,
							 max - i, &action);
			if (error)
				return(error);	/* GROT: dir is inconsistent */
			/*
			 * Record the newly split block for the next time thru?
			 */
			if (action)
				addblk = newblk;
			else
				addblk = NULL;
			break;
		}

		/*
		 * Update the btree to show the new hashval for this child.
		 */
		xfs_da_fixhashpath(state, &state->path);
	}
	if (!addblk) {
#ifdef XFSDADEBUG
		if (xfsda_debug && ((xfsda_debug_cnt % xfsda_debug) == 0))
			xfsda_check(state->args->dp, state->args->whichfork);
		xfsda_debug_cnt++;
#endif /* XFSDADEBUG */
		return(0);
	}

	/*
	 * Split the root node.
	 */
	oldblk = &state->path.blk[0];
	ASSERT(oldblk->bp->b_offset == 0);	/* root must be at block 0 */
	ASSERT(oldblk->blkno == 0);
	error = xfs_da_root_split(state, oldblk, addblk);
	if (error)
		return(error);	/* GROT: dir is inconsistent */

	/*
	 * Update the backward pointer in the second child node.  It was
	 * set to zero in node_link() because it WAS block 0.
	 */
	node = (xfs_da_intnode_t *)addblk->bp->b_un.b_addr;
	node->hdr.info.back = oldblk->blkno;
	xfs_trans_log_buf(state->trans, addblk->bp,
	    XFS_DA_LOGRANGE(node, &node->hdr.info, sizeof(node->hdr.info)));

#ifdef XFSDADEBUG
	if (xfsda_debug && ((xfsda_debug_cnt % xfsda_debug) == 0))
		xfsda_check(state->args->dp, state->args->whichfork);
	xfsda_debug_cnt++;
#endif /* XFSDADEBUG */
	return(0);
}

/*
 * Split the root.  We have to create a new root and point to the two
 * parts (the split old root) that we just created.  Copy block zero to
 * the EOF, extending the inode in process.
 */
STATIC int						/* error */
xfs_da_root_split(xfs_da_state_t *state, xfs_da_state_blk_t *blk1,
				 xfs_da_state_blk_t *blk2)
{
	xfs_da_intnode_t *node, *oldroot;
	xfs_fileoff_t blkno;
	buf_t *bp;
	int error;

	/*
	 * Copy the existing (incorrect) block from the root node position
	 * to a free space somewhere.
	 */
	error = xfs_da_grow_inode(state->trans, state->args, 1, &blkno);
	if (error)
		return(error);
	error = xfs_da_get_buf(state->trans, state->args->dp, blkno, &bp,
					     state->args->whichfork);
	if (error)
		return(error);
	ASSERT(bp != NULL);
	bcopy(blk1->bp->b_un.b_addr, bp->b_un.b_addr, state->blocksize);
	blk1->bp = bp;
	blk1->blkno = blkno;
	xfs_trans_log_buf(state->trans, bp, 0, state->blocksize - 1);

	/*
	 * Set up the new root node.
	 */
	oldroot = (xfs_da_intnode_t *)blk1->bp->b_un.b_addr;
	error = xfs_da_node_create(state->trans, state->args->dp, 0,
						 oldroot->hdr.level + 1, &bp,
						 state->args->whichfork);
	if (error)
		return(error);
	node = (xfs_da_intnode_t *)bp->b_un.b_addr;
	node->btree[0].hashval = blk1->hashval;
	node->btree[0].before = blk1->blkno;
	node->btree[1].hashval = blk2->hashval;
	node->btree[1].before = blk2->blkno;
	node->hdr.count = 2;
	xfs_trans_log_buf(state->trans, bp, 0, state->blocksize - 1);

	return(0);
}

/*
 * Split the node, rebalance, then add the new entry.
 */
STATIC int						/* error */
xfs_da_node_split(xfs_da_state_t *state, xfs_da_state_blk_t *oldblk,
				 xfs_da_state_blk_t *newblk,
				 xfs_da_state_blk_t *addblk,
				 int treelevel, int *result)
{
	xfs_da_intnode_t *node;
	xfs_fileoff_t blkno;
	int error;

	node = (xfs_da_intnode_t *)oldblk->bp->b_un.b_addr;
	ASSERT(node->hdr.info.magic == XFS_DA_NODE_MAGIC);

	/*
	 * Do we have to split the node?
	 */
	if (node->hdr.count >= XFS_DA_NODE_ENTRIES(state->mp)) {
		/*
		 * Allocate a new node, add to the doubly linked chain of
		 * nodes, then move some of our excess entries into it.
		 */
		error = xfs_da_grow_inode(state->trans, state->args, 1, &blkno);
		if (error)
			return(error);	/* GROT: dir is inconsistent */
		
		error = xfs_da_node_create(state->trans, state->args->dp,
						 blkno, treelevel, &newblk->bp,
						 state->args->whichfork);
		if (error)
			return(error);	/* GROT: dir is inconsistent */
		newblk->blkno = blkno;
		newblk->magic = XFS_DA_NODE_MAGIC;
		xfs_da_node_rebalance(state, oldblk, newblk);
		error = xfs_da_blk_link(state, oldblk, newblk);
		if (error)
			return(error);
		*result = 1;
	} else {
		*result = 0;
	}

	/*
	 * Insert the new entry into the correct block
	 * (updating last hashval in the process).
	 *
	 * xfs_da_node_add() inserts BEFORE the given index,
	 * and as a result of using node_lookup_int() we always
	 * point to a valid entry (not after one), but a split
	 * operation always results in a new block whose hashvals
	 * FOLLOW the current block.
	 */
	node = (xfs_da_intnode_t *)oldblk->bp->b_un.b_addr;
	if (oldblk->index <= node->hdr.count) {
		oldblk->index++;
		xfs_da_node_add(state, oldblk, addblk);
	} else {
		newblk->index++;
		xfs_da_node_add(state, newblk, addblk);
	}

	return(0);
}

/*
 * Balance the btree elements between two intermediate nodes,
 * usually one full and one empty.
 *
 * NOTE: if blk2 is empty, then it will get the upper half of blk1.
 */
STATIC void
xfs_da_node_rebalance(xfs_da_state_t *state, xfs_da_state_blk_t *blk1,
				     xfs_da_state_blk_t *blk2)
{
	xfs_da_intnode_t *node1, *node2, *tmpnode;
	xfs_da_node_entry_t *btree_s, *btree_d;
	int count, tmp;

	node1 = (xfs_da_intnode_t *)blk1->bp->b_un.b_addr;
	node2 = (xfs_da_intnode_t *)blk2->bp->b_un.b_addr;
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
	ASSERT(node1->hdr.info.magic == XFS_DA_NODE_MAGIC);
	ASSERT(node2->hdr.info.magic == XFS_DA_NODE_MAGIC);
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
			tmp *= sizeof(xfs_da_node_entry_t);
			btree_s = &node2->btree[0];
			btree_d = &node2->btree[count];
			bcopy((char *)btree_s, (char *)btree_d, tmp);
		}

		/*
		 * Move the req'd B-tree elements from high in node1 to
		 * low in node2.
		 */
		node2->hdr.count += count;
		tmp = count * sizeof(xfs_da_node_entry_t);
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
		tmp = count * sizeof(xfs_da_node_entry_t);
		btree_s = &node2->btree[0];
		btree_d = &node1->btree[node1->hdr.count];
		bcopy((char *)btree_s, (char *)btree_d, tmp);
		node1->hdr.count += count;

		/*
		 * Move elements in node2 down to fill the hole.
		 */
		tmp  = node2->hdr.count - count;
		tmp *= sizeof(xfs_da_node_entry_t);
		btree_s = &node2->btree[count];
		btree_d = &node2->btree[0];
		bcopy((char *)btree_s, (char *)btree_d, tmp);
		node2->hdr.count -= count;

		btree_s = &node2->btree[node2->hdr.count];
		tmp = count * sizeof(xfs_da_node_entry_t);
		bzero((char *)btree_s, tmp);
	}


	xfs_trans_log_buf(state->trans, blk1->bp, 0, state->blocksize - 1);
	xfs_trans_log_buf(state->trans, blk2->bp, 0, state->blocksize - 1);

	/*
	 * Record the last hashval from each block for upward propagation.
	 * (note: don't use the swapped node pointers)
	 */
	node1 = (xfs_da_intnode_t *)blk1->bp->b_un.b_addr;
	node2 = (xfs_da_intnode_t *)blk2->bp->b_un.b_addr;
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
xfs_da_node_add(xfs_da_state_t *state, xfs_da_state_blk_t *oldblk,
			       xfs_da_state_blk_t *newblk)
{
	xfs_da_intnode_t *node;
	xfs_da_node_entry_t *btree;
	int tmp;

	node = (xfs_da_intnode_t *)oldblk->bp->b_un.b_addr;
	ASSERT(node->hdr.info.magic == XFS_DA_NODE_MAGIC);
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
		XFS_DA_LOGRANGE(node, btree, tmp + sizeof(*btree)));
	node->hdr.count++;
	xfs_trans_log_buf(state->trans, oldblk->bp,
		XFS_DA_LOGRANGE(node, &node->hdr, sizeof(node->hdr)));

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
xfs_da_join(xfs_da_state_t *state)
{
	xfs_da_state_blk_t *drop_blk, *save_blk;
	int action, error;

	action = 0;
	drop_blk = &state->path.blk[ state->path.active-1 ];
	save_blk = &state->altpath.blk[ state->path.active-1 ];
	ASSERT(state->path.blk[0].magic == XFS_DA_NODE_MAGIC);
	ASSERT((drop_blk->magic == XFS_ATTR_LEAF_MAGIC) || \
	       (drop_blk->magic == XFS_DIR_LEAF_MAGIC));

	/*
	 * Walk back up the tree joining/deallocating as necessary.
	 * When we stop dropping blocks, break out.
	 */
	for (  ; state->path.active >= 2; drop_blk--, save_blk--,
		 state->path.active--) {
		/*
		 * See if we can combine the block with a neighbor.
		 *   (action == 0) => no options, just leave
		 *   (action == 1) => coalesce, then unlink
		 *   (action == 2) => block empty, unlink it
		 */
		switch (drop_blk->magic) {
		case XFS_ATTR_LEAF_MAGIC:
#ifdef SIM
			error = ENOTTY;
#else
			error = xfs_attr_leaf_toosmall(state, &action);
#endif
			if (error)
				return(error);
			if (action == 0)
				return(0);
#ifndef SIM
			xfs_attr_leaf_unbalance(state, drop_blk, save_blk);
#endif
			break;
		case XFS_DIR_LEAF_MAGIC:
			error = xfs_dir_leaf_toosmall(state, &action);
			if (error)
				return(error);
			if (action == 0)
				return(0);
			xfs_dir_leaf_unbalance(state, drop_blk, save_blk);
			break;
		case XFS_DA_NODE_MAGIC:
			/*
			 * Remove the offending node, fixup hashvals,
			 * check for a toosmall neighbor.
			 */
			xfs_da_node_remove(state, drop_blk);
			xfs_da_fixhashpath(state, &state->path);
			error = xfs_da_node_toosmall(state, &action);
			if (error)
				return(error);
			if (action == 0)
				goto out;
			xfs_da_node_unbalance(state, drop_blk, save_blk);
			break;
		}
		xfs_da_fixhashpath(state, &state->altpath);
		error = xfs_da_blk_unlink(state, drop_blk, save_blk);
		if (error)
			return(error);
		error = xfs_da_shrink_inode(state->trans, state->args,
					    drop_blk->blkno, 1, drop_blk->bp);
		if (error)
			return(error);
	}

	/*
	 * We joined all the way to the top.  If it turns out that
	 * we only have one entry in the root, make the child block
	 * the new root.
	 */
	xfs_da_node_remove(state, drop_blk);
	xfs_da_fixhashpath(state, &state->path);
	error = xfs_da_root_join(state, &state->path.blk[0]);
	if (error)
		return(error);

out:
#ifdef XFSDADEBUG
	if (xfsda_debug && ((xfsda_debug_cnt % xfsda_debug) == 0))
		xfsda_check(state->args->dp, state->args->whichfork);
	xfsda_debug_cnt++;
#endif /* XFSDADEBUG */
	return(0);
}

/*
 * We have only one entry in the root.  Copy the only remaining child of
 * the old root to block 0 as the new root node.
 */
STATIC int
xfs_da_root_join(xfs_da_state_t *state, xfs_da_state_blk_t *root_blk)
{
	xfs_da_intnode_t *oldroot;
	xfs_da_blkinfo_t *blkinfo;
	xfs_fileoff_t child;
	buf_t *bp;
	int error;

	ASSERT(root_blk->bp->b_offset == 0);
	ASSERT(root_blk->blkno == 0);
	ASSERT(root_blk->magic == XFS_DA_NODE_MAGIC);
	oldroot = (xfs_da_intnode_t *)root_blk->bp->b_un.b_addr;
	ASSERT(oldroot->hdr.info.magic == XFS_DA_NODE_MAGIC);
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
	error = xfs_da_read_buf(state->trans, state->args->dp, child, &bp,
					      state->args->whichfork);
	if (error)
		return(error);
	ASSERT(bp != NULL);
	blkinfo = (xfs_da_blkinfo_t *)bp->b_un.b_addr;
	if (oldroot->hdr.level == 1) {
		ASSERT((blkinfo->magic == XFS_DIR_LEAF_MAGIC) || \
		       (blkinfo->magic == XFS_ATTR_LEAF_MAGIC));
	} else {
		ASSERT(blkinfo->magic == XFS_DA_NODE_MAGIC);
	}
	ASSERT(blkinfo->forw == 0);
	ASSERT(blkinfo->back == 0);
	bcopy(bp->b_un.b_addr, root_blk->bp->b_un.b_addr, state->blocksize);
	xfs_trans_log_buf(state->trans, root_blk->bp, 0, state->blocksize - 1);
	error = xfs_da_shrink_inode(state->trans, state->args, child, 1, bp);
	return(error);
}

/*
 * Check a node block and its neighbors to see if the block should be
 * collapsed into one or the other neighbor.  Always keep the block
 * with the smaller block number.
 * If the current block is over 50% full, don't try to join it, return 0.
 * If the block is empty, fill in the state structure and return 2.
 * If it can be collapsed, fill in the state structure and return 1.
 * If nothing can be done, return 0.
 */
STATIC int
xfs_da_node_toosmall(xfs_da_state_t *state, int *action)
{
	xfs_da_intnode_t *node;
	xfs_da_state_blk_t *blk;
	xfs_da_blkinfo_t *info;
	int count, forward, error, retval, i;
	xfs_fileoff_t blkno;
	buf_t *bp;

	/*
	 * Check for the degenerate case of the block being over 50% full.
	 * If so, it's not worth even looking to see if we might be able
	 * to coalesce with a sibling.
	 */
	blk = &state->path.blk[ state->path.active-1 ];
	info = (xfs_da_blkinfo_t *)blk->bp->b_un.b_addr;
	ASSERT(info->magic == XFS_DA_NODE_MAGIC);
	node = (xfs_da_intnode_t *)info;
	count = node->hdr.count;
	if (count > (XFS_DA_NODE_ENTRIES(state->mp) >> 1)) {
		*action = 0;	/* blk over 50%, dont try to join */
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
		error = xfs_da_path_shift(state, &state->altpath, forward,
						 0, &retval);
		if (error)
			return(error);
		if (retval) {
			*action = 0;
		} else {
			*action = 2;
		}
		return(0);
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
		error = xfs_da_read_buf(state->trans, state->args->dp,
						      blkno, &bp,
						      state->args->whichfork);
		if (error)
			return(error);
		ASSERT(bp != NULL);

		node = (xfs_da_intnode_t *)info;
		count  = XFS_DA_NODE_ENTRIES(state->mp);
		count -= XFS_DA_NODE_ENTRIES(state->mp) >> 2;
		count -= node->hdr.count;
		node = (xfs_da_intnode_t *)bp->b_un.b_addr;
		ASSERT(node->hdr.info.magic == XFS_DA_NODE_MAGIC);
		count -= node->hdr.count;
		if (count >= 0)
			break;	/* fits with at least 25% to spare */

		xfs_trans_brelse(state->trans, bp);
	}
	if (i >= 2) {
		*action = 0;
		return(0);
	}

	/*
	 * Make altpath point to the block we want to keep (the lower
	 * numbered block) and path point to the block we want to drop.
	 */
	bcopy(&state->path, &state->altpath, sizeof(state->path));
	if (blkno < blk->blkno) {
		error = xfs_da_path_shift(state, &state->altpath, forward,
						 0, &retval);
		if (error) {
			return(error);
		}
		if (retval) {
			*action = 0;
			return(0);
		}
	} else {
		error = xfs_da_path_shift(state, &state->path, forward,
						 0, &retval);
		if (error) {
			return(error);
		}
		if (retval) {
			*action = 0;
			return(0);
		}
	}
	*action = 1;
	return(0);
}
#endif	/* !SIM */

/*
 * Walk back up the tree adjusting hash values as necessary,
 * when we stop making changes, return.
 */
void
xfs_da_fixhashpath(xfs_da_state_t *state, xfs_da_state_path_t *path)
{
	xfs_da_state_blk_t *blk;
	xfs_da_intnode_t *node;
	xfs_da_node_entry_t *btree;
	uint lasthash;
	int level, count;

	level = path->active-1;
	blk = &path->blk[ level ];
	switch (blk->magic) {
#ifndef SIM
	case XFS_ATTR_LEAF_MAGIC:
		lasthash = xfs_attr_leaf_lasthash(blk->bp, &count);
		if (count == 0)
			return;
		break;
#endif
	case XFS_DIR_LEAF_MAGIC:
		lasthash = xfs_dir_leaf_lasthash(blk->bp, &count);
		if (count == 0)
			return;
		break;
	case XFS_DA_NODE_MAGIC:
		lasthash = xfs_da_node_lasthash(blk->bp, &count);
		if (count == 0)
			return;
		break;
	}
	for (blk--, level--; level >= 0; blk--, level--) {
		node = (xfs_da_intnode_t *)blk->bp->b_un.b_addr;
		ASSERT(node->hdr.info.magic == XFS_DA_NODE_MAGIC);
		btree = &node->btree[ blk->index ];
		if (btree->hashval == lasthash)
			break;
		blk->hashval = btree->hashval = lasthash;
		xfs_trans_log_buf(state->trans, blk->bp,
				  XFS_DA_LOGRANGE(node, btree, sizeof(*btree)));

		lasthash = node->btree[ node->hdr.count-1 ].hashval;
	}
}

#ifndef SIM
/*
 * Remove an entry from an intermediate node.
 */
STATIC void
xfs_da_node_remove(xfs_da_state_t *state, xfs_da_state_blk_t *drop_blk)
{
	xfs_da_intnode_t *node;
	xfs_da_node_entry_t *btree;
	int tmp;

	node = (xfs_da_intnode_t *)drop_blk->bp->b_un.b_addr;
	ASSERT(drop_blk->index < node->hdr.count);
	ASSERT(drop_blk->index >= 0);

	/*
	 * Copy over the offending entry, or just zero it out.
	 */
	btree = &node->btree[drop_blk->index];
	if (drop_blk->index < (node->hdr.count-1)) {
		tmp  = node->hdr.count - drop_blk->index - 1;
		tmp *= sizeof(xfs_da_node_entry_t);
		bcopy((char *)(btree+1), (char *)btree, tmp);
		xfs_trans_log_buf(state->trans, drop_blk->bp,
		    XFS_DA_LOGRANGE(node, btree, tmp));
		btree = &node->btree[ node->hdr.count-1 ];
	}
	bzero((char *)btree, sizeof(xfs_da_node_entry_t));
	xfs_trans_log_buf(state->trans, drop_blk->bp,
	    XFS_DA_LOGRANGE(node, btree, sizeof(*btree)));
	node->hdr.count--;
	xfs_trans_log_buf(state->trans, drop_blk->bp,
	    XFS_DA_LOGRANGE(node, &node->hdr, sizeof(node->hdr)));

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
xfs_da_node_unbalance(xfs_da_state_t *state, xfs_da_state_blk_t *drop_blk,
				     xfs_da_state_blk_t *save_blk)
{
	xfs_da_intnode_t *drop_node, *save_node;
	xfs_da_node_entry_t *btree;
	int tmp;

	drop_node = (xfs_da_intnode_t *)drop_blk->bp->b_un.b_addr;
	save_node = (xfs_da_intnode_t *)save_blk->bp->b_un.b_addr;
	ASSERT(drop_node->hdr.info.magic == XFS_DA_NODE_MAGIC);
	ASSERT(save_node->hdr.info.magic == XFS_DA_NODE_MAGIC);

	/*
	 * If the dying block has lower hashvals, then move all the
	 * elements in the remaining block up to make a hole.
	 */
	if ((drop_node->btree[ 0 ].hashval < save_node->btree[ 0 ].hashval) ||
	    (drop_node->btree[ drop_node->hdr.count-1 ].hashval <
	     save_node->btree[ save_node->hdr.count-1 ].hashval))
	{
		btree = &save_node->btree[ drop_node->hdr.count ];
		tmp = save_node->hdr.count * sizeof(xfs_da_node_entry_t);
		bcopy((char *)&save_node->btree[0], (char *)btree, tmp);
		btree = &save_node->btree[0];
	} else {
		btree = &save_node->btree[ save_node->hdr.count ];
	}

	/*
	 * Move all the B-tree elements from drop_blk to save_blk.
	 */
	tmp = drop_node->hdr.count * sizeof(xfs_da_node_entry_t);
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
int							/* error */
xfs_da_node_lookup_int(xfs_da_state_t *state, int *result)
{
	xfs_da_state_blk_t *blk;
	xfs_da_blkinfo_t *current;
	xfs_da_intnode_t *node;
	xfs_da_node_entry_t *btree;
	xfs_fileoff_t blkno;
	int probe, span, max, error, retval;
	uint hashval;

	/*
	 * Descend thru the B-tree searching each level for the right
	 * node to use, until the right hashval is found.
	 */
	blkno = 0;
	for (blk = &state->path.blk[0], state->path.active = 1;
			 state->path.active <= XFS_DA_NODE_MAXDEPTH;
			 blk++, state->path.active++) {
		/*
		 * Read the next node down in the tree.
		 */
		blk->blkno = blkno;
		error = xfs_da_read_buf(state->trans, state->args->dp,
						      blkno, &blk->bp,
						      state->args->whichfork);
		if (error) {
			blk->blkno = 0;
			state->path.active--;
			return(error);
		}
		ASSERT(blk->bp != NULL);
		current = (xfs_da_blkinfo_t *)blk->bp->b_un.b_addr;
		ASSERT((current->magic == XFS_DA_NODE_MAGIC) || \
		       (current->magic == XFS_DIR_LEAF_MAGIC) || \
		       (current->magic == XFS_ATTR_LEAF_MAGIC));

		/*
		 * Search an intermediate node for a match.
		 */
		blk->magic = current->magic;
		if (current->magic == XFS_DA_NODE_MAGIC) {
			node = (xfs_da_intnode_t *)blk->bp->b_un.b_addr;
			blk->hashval = node->btree[ node->hdr.count-1 ].hashval;

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
#ifndef SIM
		} else if (current->magic == XFS_ATTR_LEAF_MAGIC) {
			blk->hashval = xfs_attr_leaf_lasthash(blk->bp, NULL);
			break;
#endif
		} else if (current->magic == XFS_DIR_LEAF_MAGIC) {
			blk->hashval = xfs_dir_leaf_lasthash(blk->bp, NULL);
			break;
		}
	}

	/*
	 * A leaf block that ends in the hashval that we are interested in
	 * (final hashval == search hashval) means that the next block may
	 * contain more entries with the same hashval, shift upward to the
	 * next leaf and keep searching.
	 */
	for (;;) {
		if (blk->magic == XFS_DIR_LEAF_MAGIC) {
			retval = xfs_dir_leaf_lookup_int(blk->bp, state->args,
								 &blk->index);
#ifndef SIM
		} else if (blk->magic == XFS_ATTR_LEAF_MAGIC) {
			retval = xfs_attr_leaf_lookup_int(blk->bp, state->args);
			blk->index = state->args->aleaf_index;
			state->args->aleaf_blkno = blk->blkno;
#endif
		}
		if (((retval == ENOENT) || (retval == ENXIO)) &&
		    (blk->hashval == state->args->hashval)) {
			error = xfs_da_path_shift(state, &state->path, 1, 1,
							 &retval);
			if (error)
				return(error);
			if (retval == 0) {
				continue;
#ifndef SIM
			} else if (blk->magic == XFS_ATTR_LEAF_MAGIC) {
				retval = ENXIO;	/* path_shift() gives ENOENT */
#endif
			}
		}
		break;
	}
	*result = retval;
	return(0);	
}

#ifndef SIM
/*
 * Drop to the bottom of the btree and pick out the indicated
 * attribute's value.  Actually use an attribute work routine.
 */
int
xfs_da_node_getvalue(xfs_da_state_t *state)
{
	xfs_da_state_blk_t *blk;
	int retval;

	blk = &state->path.blk[ state->path.active-1 ];
	ASSERT(blk->bp != NULL);
	ASSERT(blk->magic == XFS_ATTR_LEAF_MAGIC);

	retval = xfs_attr_leaf_getvalue(blk->bp, state->args);

	return(retval);	
}
#endif /* SIM */

/*========================================================================
 * Utility routines.
 *========================================================================*/

/*
 * Link a new block into a doubly linked list of blocks (of whatever type).
 */
int							/* error */
xfs_da_blk_link(xfs_da_state_t *state, xfs_da_state_blk_t *old_blk,
			       xfs_da_state_blk_t *new_blk)
{
	xfs_da_blkinfo_t *old_info, *new_info, *tmp_info;
	int before, error;
	buf_t *bp;

	/*
	 * Set up environment.
	 */
	old_info = (xfs_da_blkinfo_t *)old_blk->bp->b_un.b_addr;
	new_info = (xfs_da_blkinfo_t *)new_blk->bp->b_un.b_addr;
	ASSERT((old_blk->magic == XFS_DA_NODE_MAGIC) || \
	       (old_blk->magic == XFS_DIR_LEAF_MAGIC) || \
	       (old_blk->magic == XFS_ATTR_LEAF_MAGIC));
	ASSERT(old_blk->magic == old_info->magic);
	ASSERT(new_blk->magic == new_info->magic);
	ASSERT(old_blk->magic == new_blk->magic);

	switch (old_blk->magic) {
#ifndef SIM
	case XFS_ATTR_LEAF_MAGIC:
		before = xfs_attr_leaf_order(old_blk->bp, new_blk->bp);
		break;
#endif
	case XFS_DIR_LEAF_MAGIC:
		before = xfs_dir_leaf_order(old_blk->bp, new_blk->bp);
		break;
	case XFS_DA_NODE_MAGIC:
		before = xfs_da_node_order(old_blk->bp, new_blk->bp);
		break;
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
			error = xfs_da_read_buf(state->trans, state->args->dp,
						      old_info->back, &bp,
						      state->args->whichfork);
			if (error)
				return(error);
			ASSERT(bp != NULL);
			tmp_info = (xfs_da_blkinfo_t *)bp->b_un.b_addr;
			ASSERT(tmp_info->magic == old_info->magic);
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
			error = xfs_da_read_buf(state->trans, state->args->dp,
						      old_info->forw, &bp,
						      state->args->whichfork);
			if (error)
				return(error);
			ASSERT(bp != NULL);
			tmp_info = (xfs_da_blkinfo_t *)bp->b_un.b_addr;
			ASSERT(tmp_info->magic == old_info->magic);
			ASSERT(tmp_info->back == old_blk->blkno);
			tmp_info->back = new_blk->blkno;
			xfs_trans_log_buf(state->trans, bp, 0,
							sizeof(*tmp_info)-1);
		}
		old_info->forw = new_blk->blkno;
	}

	xfs_trans_log_buf(state->trans, old_blk->bp, 0, sizeof(*tmp_info) - 1);
	xfs_trans_log_buf(state->trans, new_blk->bp, 0, sizeof(*tmp_info) - 1);
	return(0);
}

/*
 * Compare two intermediate nodes for "order".
 */
int
xfs_da_node_order(buf_t *node1_bp, buf_t *node2_bp)
{
	xfs_da_intnode_t *node1, *node2;

	node1 = (xfs_da_intnode_t *)node1_bp->b_un.b_addr;
	node2 = (xfs_da_intnode_t *)node2_bp->b_un.b_addr;
	ASSERT((node1->hdr.info.magic == XFS_DA_NODE_MAGIC) && \
	       (node2->hdr.info.magic == XFS_DA_NODE_MAGIC));
	if ((node1->hdr.count > 0) && (node2->hdr.count > 0) && 
	    ((node2->btree[ 0 ].hashval <
	      node1->btree[ 0 ].hashval) ||
	     (node2->btree[ node2->hdr.count-1 ].hashval <
	      node1->btree[ node1->hdr.count-1 ].hashval))) {
		return(1);
	}
	return(0);
}

/*
 * Pick up the last hashvalue from an intermediate node.
 */
uint
xfs_da_node_lasthash(buf_t *bp, int *count)
{
	xfs_da_intnode_t *node;

	node = (xfs_da_intnode_t *)bp->b_un.b_addr;
	ASSERT(node->hdr.info.magic == XFS_DA_NODE_MAGIC);
	if (count) {
		*count = node->hdr.count;
	}
	return(node->btree[ node->hdr.count-1 ].hashval);
}

/*
 * Unlink a block from a doubly linked list of blocks.
 */
int							/* error */
xfs_da_blk_unlink(xfs_da_state_t *state, xfs_da_state_blk_t *drop_blk,
				 xfs_da_state_blk_t *save_blk)
{
	xfs_da_blkinfo_t *drop_info, *save_info, *tmp_info;
	buf_t *bp;
	int error;

	/*
	 * Set up environment.
	 */
	save_info = (xfs_da_blkinfo_t *)save_blk->bp->b_un.b_addr;
	drop_info = (xfs_da_blkinfo_t *)drop_blk->bp->b_un.b_addr;
	ASSERT((save_blk->magic == XFS_DA_NODE_MAGIC) || \
	       (save_blk->magic == XFS_DIR_LEAF_MAGIC) || \
	       (save_blk->magic == XFS_ATTR_LEAF_MAGIC));
	ASSERT(save_blk->magic == save_info->magic);
	ASSERT(drop_blk->magic == drop_info->magic);
	ASSERT(save_blk->magic == drop_blk->magic);
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
			error = xfs_da_read_buf(state->trans, state->args->dp,
						      drop_info->back, &bp,
						      state->args->whichfork);
			if (error)
				return(error);
			ASSERT(bp != NULL);
			tmp_info = (xfs_da_blkinfo_t *)bp->b_un.b_addr;
			ASSERT(tmp_info->magic == save_info->magic);
			ASSERT(tmp_info->forw == drop_blk->blkno);
			tmp_info->forw = save_blk->blkno;
			xfs_trans_log_buf(state->trans, bp, 0,
							sizeof(*tmp_info) - 1);
		}
	} else {
		save_info->forw = drop_info->forw;
		if (drop_info->forw) {
			error = xfs_da_read_buf(state->trans, state->args->dp,
						      drop_info->forw, &bp,
						      state->args->whichfork);
			if (error)
				return(error);
			ASSERT(bp != NULL);
			tmp_info = (xfs_da_blkinfo_t *)bp->b_un.b_addr;
			ASSERT(tmp_info->magic == save_info->magic);
			ASSERT(tmp_info->back == drop_blk->blkno);
			tmp_info->back = save_blk->blkno;
			xfs_trans_log_buf(state->trans, bp, 0,
							sizeof(*tmp_info) - 1);
		}
	}

	xfs_trans_log_buf(state->trans, save_blk->bp, 0,
					sizeof(*save_info) - 1);
	return(0);
}

/*
 * Move a path "forward" or "!forward" one block at the current level.
 *
 * This routine will adjust a "path" to point to the next block
 * "forward" (higher hashvalues) or "!forward" (lower hashvals) in the
 * Btree, including updating pointers to the intermediate nodes between
 * the new bottom and the root.
 */
int							/* error */
xfs_da_path_shift(xfs_da_state_t *state, xfs_da_state_path_t *path,
				 int forward, int release, int *result)
{
	xfs_da_state_blk_t *blk;
	xfs_da_blkinfo_t *info;
	xfs_da_intnode_t *node;
	xfs_fileoff_t blkno;
	int level, error;

	/*
	 * Roll up the Btree looking for the first block where our
	 * current index is not at the edge of the block.  Note that
	 * we skip the bottom layer because we want the sibling block.
	 */
	ASSERT(path != NULL);
	ASSERT((path->active > 0) && (path->active < XFS_DA_NODE_MAXDEPTH));
	level = (path->active-1) - 1;	/* skip bottom layer in path */
	for (blk = &path->blk[level]; level >= 0; blk--, level--) {
		ASSERT(blk->bp != NULL);
		node = (xfs_da_intnode_t *)blk->bp->b_un.b_addr;
		ASSERT(node->hdr.info.magic == XFS_DA_NODE_MAGIC);
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
	if (level < 0) {
		*result = XFS_ERROR(ENOENT);	/* we're out of our tree */
		return(0);
	}

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
		error = xfs_da_read_buf(state->trans, state->args->dp,
						      blkno, &blk->bp,
						      state->args->whichfork);
		if (error)
			return(error);
		ASSERT(blk->bp != NULL);
		info = (xfs_da_blkinfo_t *)blk->bp->b_un.b_addr;
		ASSERT((info->magic == XFS_DA_NODE_MAGIC) || \
		       (info->magic == XFS_DIR_LEAF_MAGIC) || \
		       (info->magic == XFS_ATTR_LEAF_MAGIC));
		blk->magic = info->magic;
		if (info->magic == XFS_DA_NODE_MAGIC) {
			node = (xfs_da_intnode_t *)info;
			blk->hashval = node->btree[ node->hdr.count-1 ].hashval;
			if (forward)
				blk->index = 0;
			else
				blk->index = node->hdr.count-1;
			blkno = node->btree[ blk->index ].before;
		} else {
			ASSERT(level == path->active-1);
			blk->index = 0;
			switch(blk->magic) {
#ifndef SIM
			case XFS_ATTR_LEAF_MAGIC:
				blk->hashval = xfs_attr_leaf_lasthash(blk->bp,
								      NULL);
				break;
#endif
			case XFS_DIR_LEAF_MAGIC:
				blk->hashval = xfs_dir_leaf_lasthash(blk->bp,
								     NULL);
				break;
			default:
				ASSERT((blk->magic == XFS_ATTR_LEAF_MAGIC) || \
				       (blk->magic == XFS_DIR_LEAF_MAGIC));
				break;
			}
		}
	}
	*result = 0;
	return(0);
}


/*========================================================================
 * Utility routines.
 *========================================================================*/

/*
 * Implement a simple hash on a character string.
 * Rotate the hash value by 7 bits, then XOR each character in.
 */
uint
xfs_da_hashname(register char *name, register int namelen)
{
	register uint hash;

	hash = 0;
	for (  ; namelen > 0; namelen--) {
		hash = *name++ ^ ((hash << 7) | (hash >> (32-7)));
	}
	return(hash);
}

int
xfs_da_grow_inode(xfs_trans_t *trans, xfs_da_args_t *args, int length,
			      xfs_fileoff_t *new_blkno)
{
	xfs_fileoff_t bno;
	xfs_bmbt_irec_t map;
	xfs_inode_t *dp;
	int nmap, error;

	dp = args->dp;
	error = xfs_bmap_first_unused(trans, dp, (xfs_extlen_t)length, &bno,
					     args->whichfork);
	if (error) {
		return(error);
	}
	nmap = 1;	/* GROT: max of 1 extent is not enough */
	ASSERT(args->firstblock != NULL);
	error = xfs_bmapi(trans, dp, bno, (xfs_filblks_t)length,
			 XFS_BMAPI_AFLAG(args->whichfork) |
			 XFS_BMAPI_WRITE|XFS_BMAPI_METADATA,
			 args->firstblock, args->total, &map, &nmap,
			 args->flist);
	if (error) {
		return(error);
	}
	if (nmap < 1) {
		ASSERT(0);
		return(XFS_ERROR(ENOSPC));
	}
	*new_blkno = bno;
	if (args->whichfork == XFS_DATA_FORK) {
		error = xfs_bmap_last_offset(trans, dp, &bno, args->whichfork);
		if (error) {
			return(error);
		}
		dp->i_d.di_size = XFS_FSB_TO_B(dp->i_mount, bno);
	}

	xfs_trans_log_inode(trans, dp, XFS_ILOG_CORE);

	return(0);
}

#ifndef SIM
int
xfs_da_shrink_inode(xfs_trans_t *trans, xfs_da_args_t *args,
				xfs_fileoff_t dead_blkno, int length,
				buf_t *dead_buf)
{
	xfs_inode_t *dp;
	int done, error;
	xfs_fileoff_t bno;

	dp = args->dp;
	error = xfs_bunmapi(trans, dp, dead_blkno, (xfs_filblks_t)length,
				   XFS_BMAPI_AFLAG(args->whichfork) |
				   XFS_BMAPI_METADATA,
				   1, args->firstblock, args->flist, &done);
	if (error) {
		return(error);
	}
	ASSERT(done);
#ifdef GROT
	if ((bp = incore(dead_blkno)) != NULL) {
	}
#endif
	xfs_trans_binval(trans, dead_buf);
	if (args->whichfork == XFS_DATA_FORK) {
		error = xfs_bmap_last_offset(trans, dp, &bno, args->whichfork);
		if (error)
			return(error);
		dp->i_d.di_size = XFS_FSB_TO_B(dp->i_mount, bno);
	}

	xfs_trans_log_inode(trans, dp, XFS_ILOG_CORE);

	return(0);
}
#endif	/* !SIM */

int
xfs_da_get_buf(xfs_trans_t *trans, xfs_inode_t *dp, xfs_fileoff_t bno,
			   buf_t **bpp, int whichfork)
{
	xfs_bmbt_irec_t map;
	int nmap, error;
	xfs_fsblock_t firstblock;
	buf_t *bp;

	nmap = 1;
	firstblock = NULLFSBLOCK;
	error = xfs_bmapi(trans, dp, bno, 1, XFS_BMAPI_AFLAG(whichfork),
				 &firstblock, 0, &map, &nmap, 0);
	if (error) {
		return(error);
	}
	ASSERT(nmap == 1);
	ASSERT((map.br_startblock != DELAYSTARTBLOCK) &&
	       (map.br_startblock != HOLESTARTBLOCK));

	ASSERT(map.br_startblock != NULLFSBLOCK);
	bp = xfs_trans_get_buf(trans, dp->i_mount->m_dev,
			      XFS_FSB_TO_DADDR(dp->i_mount, map.br_startblock),
			      dp->i_mount->m_bsize, 0);
	ASSERT(bp);
	ASSERT(!geterror(bp));
	*bpp = bp;
	return(0);
}

#ifdef XFSDADEBUG
#undef xfs_da_read_buf
#endif /* XFSDADEBUG */
int
xfs_da_read_buf(xfs_trans_t *trans, xfs_inode_t *dp, xfs_fileoff_t bno,
			    buf_t **bpp, int whichfork)
{
	xfs_fsblock_t firstblock;
	xfs_da_blkinfo_t *info;
	xfs_bmbt_irec_t map;
	int nmap, error;

	nmap = 1;
	firstblock = NULLFSBLOCK;
	error = xfs_bmapi(trans, dp, bno, 1, XFS_BMAPI_AFLAG(whichfork),
				 &firstblock, 0, &map, &nmap, 0);
	if (error) {
		return(error);
	}
	if (nmap == 0) {
		*bpp = NULL;
		return(0);
	}
	ASSERT(nmap >= 1);
	ASSERT(map.br_startblock != DELAYSTARTBLOCK);
	if (map.br_startblock == HOLESTARTBLOCK) {
		*bpp = NULL;
		return(0);
	}

	ASSERT(map.br_startblock != NULLFSBLOCK);
	error = xfs_trans_read_buf(trans, dp->i_mount->m_dev, 
			      XFS_FSB_TO_DADDR(dp->i_mount, map.br_startblock),
			      dp->i_mount->m_bsize, 0, bpp);
	if (error)
		return error;
	ASSERT(!*bpp || !geterror(*bpp));
	if (*bpp != NULL)
		(*bpp)->b_ref = XFS_GEN_LBTREE_REF;

	info = (xfs_da_blkinfo_t *)((*bpp)->b_un.b_addr);
	if ((info->magic != XFS_DA_NODE_MAGIC) &&
	    (info->magic != XFS_DIR_LEAF_MAGIC) &&
	    (info->magic != XFS_ATTR_LEAF_MAGIC)) {
		(*bpp)->b_flags |= B_ERROR;
		xfs_trans_brelse(trans, *bpp);
		return(EIO);
	}
	return(0);
}

/*
 * Calculate the number of bits needed to hold i different values.
 */
uint
xfs_da_log2_roundup(uint i)
{
	uint rval;

	for (rval = 0; rval < NBBY * sizeof(i); rval++) {
		if ((1 << rval) >= i)
			break;
	}
	return(rval);
}

zone_t *xfs_da_state_zone;	/* anchor for state struct zone */

/*
 * Allocate a dir-state structure.
 * We don't put them on the stack since they're large.
 */
xfs_da_state_t *
xfs_da_state_alloc()
{
	return kmem_zone_zalloc(xfs_da_state_zone, KM_SLEEP);
}

/*
 * Free a da-state structure.
 */
void
xfs_da_state_free(xfs_da_state_t *state)
{
	kmem_zone_free(xfs_da_state_zone, state);
}


#ifdef XFSDADEBUG

/*========================================================================
 * Transaction tracing routines.
 *========================================================================*/

#undef xfs_trans_binval
#undef xfs_trans_brelse
#undef xfs_trans_log_buf
#undef xfs_trans_log_inode
#undef xfs_da_read_buf
xfsda_buftrace_t xfsda_tracebuf[BUFTRACEMAX];
int xfsda_trc_head = 0;
int xfsda_trc_count = 0;
void
xfsda_t_reinit(char *description, char *file, int line)
{
	if (xfsda_debug) {
		xfsda_tracebuf[ xfsda_trc_head ].which = 1;
		xfsda_tracebuf[ xfsda_trc_head ].line = line;
		xfsda_tracebuf[ xfsda_trc_head ].file = file;
		xfsda_tracebuf[ xfsda_trc_head ].bp = (buf_t *)description;
		xfsda_trc_head = (xfsda_trc_head + 1) % BUFTRACEMAX;
		xfsda_trc_count++;
	}
}
void
xfsda_t_binval(xfs_trans_t *tp, buf_t *bp, char *file, int line)
{
	if (xfsda_debug) {
		xfsda_tracebuf[ xfsda_trc_head ].which = 2;
		xfsda_tracebuf[ xfsda_trc_head ].line = line;
		xfsda_tracebuf[ xfsda_trc_head ].file = file;
		xfsda_tracebuf[ xfsda_trc_head ].bp = bp;
		xfsda_trc_head = (xfsda_trc_head + 1) % BUFTRACEMAX;
		xfsda_trc_count++;
	}
	xfs_trans_binval(tp, bp);
}
void
xfsda_t_brelse(xfs_trans_t *tp, buf_t *bp, char *file, int line)
{
	if (xfsda_debug) {
		xfsda_tracebuf[ xfsda_trc_head ].which = 3;
		xfsda_tracebuf[ xfsda_trc_head ].line = line;
		xfsda_tracebuf[ xfsda_trc_head ].file = file;
		xfsda_tracebuf[ xfsda_trc_head ].bp = bp;
		xfsda_trc_head = (xfsda_trc_head + 1) % BUFTRACEMAX;
		xfsda_trc_count++;
	}
	xfs_trans_brelse(tp, bp);
}
void
xfsda_t_log_buf(xfs_trans_t *tp, buf_t *bp, uint first, uint last,
			     char *file, int line)
{
	if (xfsda_debug) {
		xfsda_tracebuf[ xfsda_trc_head ].which = 4;
		xfsda_tracebuf[ xfsda_trc_head ].line = line;
		xfsda_tracebuf[ xfsda_trc_head ].file = file;
		xfsda_tracebuf[ xfsda_trc_head ].bp = bp;
		xfsda_trc_head = (xfsda_trc_head + 1) % BUFTRACEMAX;
		xfsda_trc_count++;
	}
	xfs_trans_log_buf(tp, bp, first, last);
}
void
xfsda_t_log_inode(xfs_trans_t *tp, xfs_inode_t *ip, uint flags,
			       char *file, int line)
{
	if (xfsda_debug) {
		xfsda_tracebuf[ xfsda_trc_head ].which = 5;
		xfsda_tracebuf[ xfsda_trc_head ].line = line;
		xfsda_tracebuf[ xfsda_trc_head ].file = file;
		xfsda_tracebuf[ xfsda_trc_head ].bp = (buf_t *)ip;
		xfsda_trc_head = (xfsda_trc_head + 1) % BUFTRACEMAX;
		xfsda_trc_count++;
	}
	xfs_trans_log_inode(tp, ip, flags);
}
buf_t *
xfsda_t_dir_read_buf(xfs_trans_t *trans, xfs_inode_t *ip, xfs_fileoff_t bno,
				 int whichfork, char *file, int line)
{
	buf_t *bp;

	(void) xfs_da_read_buf(trans, ip, bno, &bp, whichfork);
	if (xfsda_debug) {
		xfsda_tracebuf[ xfsda_trc_head ].which = 6;
		xfsda_tracebuf[ xfsda_trc_head ].line = line;
		xfsda_tracebuf[ xfsda_trc_head ].file = file;
		xfsda_tracebuf[ xfsda_trc_head ].bp = bp;
		xfsda_trc_head = (xfsda_trc_head + 1) % BUFTRACEMAX;
		xfsda_trc_count++;
	}
	return(bp);
}
xfsda_dumptrace()
{
	xfsda_buftrace_t *btp;
	int count;

	btp = &xfsda_tracebuf[ xfsda_trc_head ];
	for (count = 0; count < BUFTRACEMAX; count++, btp++) {
		if (btp == &xfsda_tracebuf[ BUFTRACEMAX ])
			btp = &xfsda_tracebuf[ 0 ];
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
	printf("%d total operations\n", xfsda_trc_count);
	return(0);
}

/*========================================================================
 * Load all of the block in a directory into memory so they can be seen.
 *========================================================================*/

int xfsda_load_leaf(xfs_inode_t *dp, xfs_fileoff_t blkno, int whichfork);
int xfsda_load_node(xfs_inode_t *dp, xfs_fileoff_t blkno, int whichfork);

int
xfsda_loaddir(xfs_inode_t *dp)
{
	int retval;

	if (dp->i_d.di_format == XFS_DINODE_FMT_LOCAL) {
		retval = 0;
	} else if (xfs_bmap_one_block(dp, XFS_DATA_FORK)) {
		retval = xfsda_load_leaf(dp, 0, XFS_DATA_FORK);
	} else {
		retval = xfsda_load_node(dp, 0, XFS_DATA_FORK);
	}
	return(retval);
}

int
xfsda_loadattr(xfs_inode_t *dp)
{
	int retval;

	if (dp->i_d.di_aformat == XFS_DINODE_FMT_LOCAL) {
		retval = 0;
	} else if (xfs_bmap_one_block(dp, XFS_ATTR_FORK)) {
		retval = xfsda_load_leaf(dp, 0, XFS_ATTR_FORK);
	} else {
		retval = xfsda_load_node(dp, 0, XFS_ATTR_FORK);
	}
	return(retval);
}

int
xfsda_load_leaf(xfs_inode_t *dp, xfs_fileoff_t blkno, int whichfork)
{
	xfs_attr_leafblock_t *aleaf;
	xfs_dir_leafblock_t *dleaf;
	xfs_da_blkinfo_t *info;
	buf_t *bp;

	(void) xfs_da_read_buf(dp->i_transp, dp, blkno, &bp, whichfork);
	info = (xfs_da_blkinfo_t *)bp->b_un.b_addr;
	printf("leaf[0x%x]  bp 0x%x  forw 0x%x  back 0x%x  ",
			blkno, bp, info->forw, info->back);
	switch(info->magic) {
	case XFS_ATTR_LEAF_MAGIC:
		aleaf = (xfs_attr_leafblock_t *)info;
		printf("count %d\n", aleaf->hdr.count);
	case XFS_DIR_LEAF_MAGIC:
		dleaf = (xfs_dir_leafblock_t *)info;
		printf("count %d\n", dleaf->hdr.count);
	}
	return(0);
}

int
xfsda_load_node(xfs_inode_t *dp, xfs_fileoff_t blkno, int whichfork)
{
	xfs_da_intnode_t *node;
	xfs_da_node_entry_t *btree;
	int retval, i;
	buf_t *bp;

	(void) xfs_da_read_buf(dp->i_transp, dp, blkno, &bp, whichfork);
	node = (xfs_da_intnode_t *)bp->b_un.b_addr;
	printf("node[0x%x]  bp 0x%x  forw 0x%x  back 0x%x  count %d  level %d\n",
			    blkno, bp,
			    node->hdr.info.forw, node->hdr.info.back,
			    node->hdr.count, node->hdr.level);
	btree = &node->btree[0];
	for (i = 0; i < node->hdr.count; btree++, i++) {
		if (node->hdr.level == 1) {
			retval += xfsda_load_leaf(dp, btree->before, whichfork);
		} else {
			retval += xfsda_load_node(dp, btree->before, whichfork);
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
 * Directory/attribute structure checking data, context for each process.
 */
typedef struct xfsda_context {
	xfs_inode_t	*dp;		/* directory inode */
	int		whichfork;	/* checking directory or attrributes? */
	uint		hashval;	/* last hashval seen in tree */
	int		maxblockmap;	/* size of block-use map */
	char		*blockmap;	/* map of blocks and their uses */
	int		maxlinkmap;	/* size of forw/back link map */
	xfs_fileoff_t	*forw_links;	/* map of forward chain links */
	xfs_fileoff_t	*back_links;	/* map of backward chain links */
	int		bytemapsize;	/* size of used/free bytemap */
	char		*bytemap;	/* used/free bytes in leaf blk */
	int		usedbytes;	/* total bytes used in entries */
	int		freebytes;	/* total free bytes for filenames */
	int		holeblks;	/* total leaves with holes */
	int		entries;	/* total directory entries */
	int		nodes;		/* node blocks seen */
	int		leaves;		/* leaf blocks seen */
} xfsda_context_t;


int xfsda_leaf_check(xfsda_context_t *con, xfs_fileoff_t blkno, int firstlast);
int xfsda_attr_leaf_check(xfsda_context_t *con, buf_t *bp, xfs_fileoff_t blkno,
					  int firstlast);
int xfsda_dir_leaf_check(xfsda_context_t *con, buf_t *bp, xfs_fileoff_t blkno,
					 int firstlast);
int xfsda_node_check(xfsda_context_t *con, xfs_fileoff_t blkno,
				     int depth, int firstlast);
int xfsda_check_blockuse(xfsda_context_t *con);
int xfsda_checkchain(xfsda_context_t *con, buf_t *bp,
				     xfs_fileoff_t parentblk,
				     xfs_fileoff_t blkno, int forward);
int xfsda_endchain(xfsda_context_t *con, buf_t *bp, xfs_fileoff_t blkno);
int xfsda_checkblock(xfsda_context_t *con, buf_t *bp,
				     xfs_fileoff_t blkno, int level);
int xfsda_checkname(buf_t *bp, char *name, int namelen, int index,
			  uint hashval);
void xfsda_printbytes(int bytes);

extern uint xfs_da_hashname(register char *name, register int namelen);

int
xfsda_checkdir(xfs_inode_t *dp)
{
	return(xfsda_check(dp, XFS_DATA_FORK));
}
int
xfsda_checkattr(xfs_inode_t *dp)
{
	return(xfsda_check(dp, XFS_ATTR_FORK));
}

int
xfsda_check(xfs_inode_t *dp, int whichfork)
{
	xfsda_context_t *con;
	int numblks, tmp, retval;

	con = (xfsda_context_t *)kmem_zalloc(sizeof(*con), KM_SLEEP);
	numblks = 1000;	/* GROT: this should be a calculation, not a constant */
	con->maxblockmap = numblks;
	con->blockmap = (char *)
		kmem_zalloc(numblks * sizeof(*con->blockmap), KM_SLEEP);
	con->maxlinkmap = numblks;
	con->forw_links = (xfs_fileoff_t *)
		kmem_zalloc(numblks * sizeof(*con->forw_links), KM_SLEEP);
	con->back_links = (xfs_fileoff_t *)
		kmem_zalloc(numblks * sizeof(*con->back_links), KM_SLEEP);
	con->bytemapsize = XFS_LBSIZE(dp->i_mount);
	con->bytemap = (char *)
		kmem_zalloc(con->bytemapsize * sizeof(*con->bytemap), KM_SLEEP);
	con->usedbytes = con->freebytes = con->holeblks = con->entries = 0;

	/*
	 * Decide on what work routines to call based on the inode size.
	 */
	printf("XFSDA_CHECK: ");
	con->whichfork = whichfork;
	con->dp = dp;
	retval = 0;
	if (dp->i_d.di_size <= XFS_LITINO(dp->i_mount)) {
		retval = 0; /* xfsda_shortform_check(con); */
	} else if (dp->i_d.di_size == XFS_LBSIZE(dp->i_mount)) {
		retval = xfsda_leaf_check(con, 0, -2);
	} else {
		retval = xfsda_node_check(con, (xfs_fileoff_t)0, -1, -2);
		retval += xfsda_check_blockuse(con);
	}
	printf("%d nodes, %d leaves, %d entries, ",
		   con->nodes, con->leaves, con->entries);
	xfsda_printbytes(con->usedbytes);
	printf(" used, ");
	xfsda_printbytes(con->freebytes);
	tmp = con->usedbytes + con->freebytes;
	printf(" (%d%%) free\n", (tmp>0)?((con->freebytes*100)/tmp):0);

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
xfsda_shortform_check(xfsda_context_t *con)
{
	xfs_dir_shortform_t *sf;
	xfs_dir_sf_entry_t *sfe;
	int retval, total, i;
	buf_t *bp;

	sf = (xfs_dir_shortform_t *)bp->buffer;
	if (sf->hdr.count > (XFS_LITINO(fs)/sizeof(xfs_dir_sf_entry_t)))
		BADNEWS1("shortform entry count bad: count %d\n",
				    sf->hdr.count);

	sfe = &sf->list[0];
	total = sizeof(xfs_dir_sf_hdr_t);
	for (i = sf->hdr.count-1; i >= 0; i--) {
		if ((sfe->namelen == 0) || (sfe->namelen > MAXNAMELEN) ||
		    (sfe->namelen > XFS_LITINO(fs)) ||
		    ((sfe->namelen + total) > dp->i_d.di_size))
			BADNEWS1("bad shortform name length: entry %d\n", i);

		con->hashval = xfs_da_hashname(sfe->name, sfe->namelen);
		retval += xfsda_checkname(sfe->name, sfe->namelen,
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
xfsda_leaf_check(xfsda_context_t *con, xfs_fileoff_t blkno, int firstlast)
{
	xfs_da_blkinfo_t *info;
	int retval;
	buf_t *bp;

	/*
	 * Read in the leaf block.
	 */
	(void) xfs_da_read_buf(con->dp->i_transp, con->dp, blkno, &bp,
						  con->whichfork);
	info = (xfs_da_blkinfo_t *)bp->b_un.b_addr;

	/*
	 * Check the header fields.
	 */
	retval = 0;
	if ((info->magic != XFS_ATTR_LEAF_MAGIC) &&
	    (info->magic != XFS_DIR_LEAF_MAGIC)) {
		BADNEWS1("not a leaf node: blkno 0x%x\n", blkno);
		return(retval);
	}

	/*
	 * Check the chain of leaves
	 */
	if (firstlast == 0) {
		retval += xfsda_checkchain(con, bp, blkno, info->forw, 1);
		retval += xfsda_checkchain(con, bp, blkno, info->back, 0);
	} else {
		retval += xfsda_endchain(con, bp, blkno);
		if (firstlast < 0) {
			if (firstlast == -1)
				retval += xfsda_checkchain(con, bp, blkno,
						      info->forw, 1);
			if (info->back != 0)
				BADNEWS2("backward ref to leaf 0x%x in leaf 0x%x\n",
						   info->back, blkno);
		}
		if (firstlast > 0) {
			retval += xfsda_checkchain(con, bp, blkno, info->back,
							0);
			if (info->forw != 0)
				BADNEWS2("forward ref to leaf 0x%x in leaf 0x%x\n",
						  info->forw, blkno);
		}
	}

	if (info->magic == XFS_ATTR_LEAF_MAGIC) {
		retval += xfsda_attr_leaf_check(con, bp, blkno, firstlast);
	} else if (info->magic == XFS_DIR_LEAF_MAGIC) {
		retval += xfsda_dir_leaf_check(con, bp, blkno, firstlast);
	}
	return(retval);
}

/*ARGSUSED*/
int
xfsda_attr_leaf_check(xfsda_context_t *con, buf_t *bp, xfs_fileoff_t blkno,
				      int firstlast)
{
	xfs_attr_leafblock_t *leaf;
	xfs_attr_leaf_entry_t *entry;
	xfs_attr_leaf_map_t *map;
	xfs_attr_leaf_name_local_t *localp;
	xfs_attr_leaf_name_remote_t *remotep;
	int lowest, bytes, retval, i, j, k;
	char *ch;

	retval = 0;
	leaf = (xfs_attr_leafblock_t *)bp->b_un.b_addr;
	if (leaf->hdr.pad1 != 0)
		BADNEWS1("pad1 is non-zero: leaf 0x%x\n", blkno);

	/*
	 * Set up the used/free byte map for this block.
	 */
	bzero(con->bytemap, con->bytemapsize);
	j = leaf->hdr.count * sizeof(xfs_attr_leaf_entry_t)
				+ sizeof(xfs_attr_leaf_hdr_t);
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
	entry = &leaf->entries[0];
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

			if (entry->flags & XFS_ATTR_LOCAL) {
				localp = XFS_ATTR_LEAF_NAME_LOCAL(leaf, i);
				retval += xfsda_checkname(bp,
							  (char *)localp->nameval,
							  localp->namelen, i,
							  entry->hashval);
				k = XFS_ATTR_LEAF_ENTSIZE_LOCAL(localp->namelen, localp->valuelen);
				bytes += k;
				ch = &con->bytemap[entry->nameidx];
				for (j = 0; j < k; j++) {
					*ch++ = 1;
				}
			} else {
				remotep = XFS_ATTR_LEAF_NAME_REMOTE(leaf, i);
				retval += xfsda_checkname(bp,
							  (char *)remotep->name,
							  remotep->namelen, i,
							  entry->hashval);
				k = XFS_ATTR_LEAF_ENTSIZE_REMOTE(remotep->namelen);
				bytes += k;
				ch = &con->bytemap[entry->nameidx];
				for (j = 0; j < k; j++) {
					*ch++ = 1;
				}
/* GROT: add code to check remote block usage (overlaps, dangling, etc) */
			}
		}
		if (entry->pad2 != 0)
			BADNEWS1("pad2 is non-zero: leaf 0x%x\n", blkno);
	}

	/*
	 * Check more header fields after looking at each entry.
	 */
	if (leaf->hdr.usedbytes != bytes)
		BADNEWS3("usedbytes (0x%x) not right (0x%x): leaf 0x%x\n",
				    (int)leaf->hdr.usedbytes, bytes, blkno);
	con->usedbytes += bytes;

	map = &leaf->hdr.freemap[0];
	for (i = 0; i < XFS_ATTR_LEAF_MAPSIZE; map++, i++) {
		if (map->base >= XFS_LBSIZE(con->dp->i_mount)) {
			BADNEWS2("freemap[%d].base is too large: leaf 0x%x\n",
						   i, blkno);
			continue;
		}
		j = sizeof(xfs_attr_leaf_hdr_t) +
			leaf->hdr.count * sizeof(xfs_attr_leaf_entry_t);
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

/*ARGSUSED*/
int
xfsda_dir_leaf_check(xfsda_context_t *con, buf_t *bp, xfs_fileoff_t blkno,
				     int firstlast)
{
	xfs_dir_leafblock_t *leaf;
	xfs_dir_leaf_entry_t *entry;
	xfs_dir_leaf_name_t *namest;
	xfs_dir_leaf_map_t *map;
	int lowest, bytes, retval, i, j, k;
	char *ch;

	retval = 0;
	leaf = (xfs_dir_leafblock_t *)bp->b_un.b_addr;
	if (leaf->hdr.pad1 != 0)
		BADNEWS1("pad1 is non-zero: leaf 0x%x\n", blkno);

	/*
	 * Set up the used/free byte map for this block.
	 */
	bzero(con->bytemap, con->bytemapsize);
	j = leaf->hdr.count * sizeof(xfs_dir_leaf_entry_t)
				+ sizeof(xfs_dir_leaf_hdr_t);
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
	entry = &leaf->entries[0];
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
			retval += xfsda_checkname(bp, (char *)namest->name,
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
	con->usedbytes += bytes;

	map = &leaf->hdr.freemap[0];
	for (i = 0; i < XFS_DIR_LEAF_MAPSIZE; map++, i++) {
		if (map->base >= XFS_LBSIZE(con->dp->i_mount)) {
			BADNEWS2("freemap[%d].base is too large: leaf 0x%x\n",
						   i, blkno);
			continue;
		}
		j = sizeof(xfs_dir_leaf_hdr_t) +
			leaf->hdr.count * sizeof(xfs_dir_leaf_entry_t);
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
xfsda_node_check(xfsda_context_t *con, xfs_fileoff_t blkno,
				 int depth, int firstlast)
{
	xfs_da_intnode_t *node;
	xfs_da_blkinfo_t *info;
	xfs_da_node_entry_t *btree;
	int retval, count, tmp, i;
	buf_t *bp;

	/*
	 * Read the next node down in the tree.
	 */
	(void) xfs_da_read_buf(con->dp->i_transp, con->dp, blkno, &bp,
						  con->whichfork);
	info = (xfs_da_blkinfo_t *)bp->b_un.b_addr;
	node = (xfs_da_intnode_t *)bp->b_un.b_addr;

	/*
	 * Check the header fields.
	 */
	retval = 0;
	if (info->magic != XFS_DA_NODE_MAGIC) {
		BADNEWS1("not an int node: blkno 0x%x\n", blkno);
		return(retval);
	}
	if (depth >= XFS_DA_NODE_MAXDEPTH)
		BADNEWS1("tree too deep: node 0x%x\n", blkno);
	if ((depth >= 0) && (node->hdr.level != depth))
		BADNEWS1("level not right: node 0x%x\n", blkno);
	depth = node->hdr.level;
	if (node->hdr.count == 0)
		BADNEWS1("zero entry count: node 0x%x\n", blkno);
	if (node->hdr.count > XFS_DA_NODE_ENTRIES(con->dp->i_mount)) {
		BADNEWS1("bad node entry count: node 0x%x\n", blkno);
		count = XFS_DA_NODE_ENTRIES(con->dp->i_mount);
	} else
		count = node->hdr.count;

	/*
	 * Check the chain of nodes
	 */
	if (firstlast == 0) {
		retval += xfsda_checkchain(con, bp, blkno, info->forw, 1);
		retval += xfsda_checkchain(con, bp, blkno, info->back, 0);
	} else {
		retval += xfsda_endchain(con, bp, blkno);
		if (firstlast < 0) {
			if (firstlast == -1)
				retval += xfsda_checkchain(con, bp, blkno,
						      info->forw, 1);
			if (info->back != 0)
				BADNEWS2("backward ref to node 0x%x in node 0x%x\n",
						   info->back, blkno);
		}
		if (firstlast > 0) {
			retval += xfsda_checkchain(con, bp, blkno, info->back,
							0);
			if (info->forw != 0)
				BADNEWS2("forward ref to node 0x%x in node 0x%x\n",
						  info->forw, blkno);
		}
	}

	/*
	 * Do a linear pass through the entries.
	 */
	btree = &node->btree[0];
	for (i = 0; i < XFS_DA_NODE_ENTRIES(con->dp->i_mount); btree++, i++) {
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

		if (tmp = xfsda_checkblock(con, bp, btree->before,
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
				retval += xfsda_leaf_check(con, btree->before,
								tmp);
			} else {
				retval += xfsda_node_check(con, btree->before,
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
xfsda_checkname(buf_t *bp, char *name, int namelen, int index, uint hashval)
{
	int retval;

	retval = 0;
	if (xfs_da_hashname(name, namelen) != hashval)
		BADNEWS2("hashval doesn't match name at index %d: \"%s\"\n",
				  index, name);
	return(retval);
}

int
xfsda_checkblock(xfsda_context_t *con, buf_t *bp,
				 xfs_fileoff_t blkno, int level)
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
xfsda_endchain(xfsda_context_t *con, buf_t *bp, xfs_fileoff_t blkno)
{
	int retval;

	retval = 0;
	if (con->blockmap[blkno] & 0x20)
		BADNEWS1("blkno 0x%x is already a start/end blk\n", blkno);
	con->blockmap[blkno] |= 0x20;
	return(retval);
}

int
xfsda_checkchain(xfsda_context_t *con, buf_t *bp,
			 xfs_fileoff_t parentblk, xfs_fileoff_t blkno,
			 int forward)
{
	int retval, tmp, i;
	xfs_fileoff_t *links;

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
xfsda_check_blockuse(xfsda_context_t *con)
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

void
xfsda_printbytes(int bytes)
{
	if (bytes >= 1024*1024) {
		printf("%d.%dMB", bytes/(1024*1024), (bytes%(1024*1024))/10240);
	} else if (bytes >= 1024) {
		printf("%d.%dKB", bytes/1024, (bytes%1024)/102);
	} else {
		printf("%dB", bytes);
	}
}
#endif /* XFSDADEBUG */
