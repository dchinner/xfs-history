#include <sys/param.h>
#include <sys/errno.h>
#include <sys/buf.h>
#include <sys/kmem.h>
#include <sys/uio.h>
#include <sys/debug.h>
#include <sys/proc.h>
#include <sys/vnode.h>
#include <sys/dirent.h>
#include <sys/user.h>
#include <sys/grio.h>
#include <sys/sysinfo.h>
#include <sys/ksa.h>
#include <sys/systm.h>
#include <sys/attributes.h>
#include "xfs_types.h"
#include "xfs_inum.h"
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
#include "xfs_bmap.h"
#include "xfs_attr_sf.h"
#include "xfs_dir_sf.h"
#include "xfs_dinode.h"
#include "xfs_inode_item.h"
#include "xfs_inode.h"
#include "xfs_da_btree.h"
#include "xfs_attr.h"
#include "xfs_attr_leaf.h"
#include "xfs_error.h"

/*
 * xfs_attr.c
 *
 * Provide the external interfaces to manage attribute lists.
 */

/*
 * Max number of extents needed for directory create + symlink.
 * GROT: make new log reservation #defines to attribute operations.
 */
#define MAX_EXT_NEEDED 9

/*========================================================================
 * Function prototypes for the kernel.
 *========================================================================*/

/*
 * Internal routines when attribute list size == XFS_LBSIZE(mp).
 */
STATIC int xfs_attr_leaf_get(xfs_trans_t *trans, xfs_da_name_t *args);
STATIC int xfs_attr_leaf_removename(xfs_trans_t *trans, xfs_da_name_t *args,
						buf_t **bpp);
STATIC int xfs_attr_leaf_list(xfs_trans_t *trans, xfs_inode_t *dp,
					  attrlist_t *alist, int flags,
					  attrlist_cursor_kern_t *cursor);

/*
 * Internal routines when attribute list size > XFS_LBSIZE(mp).
 */
STATIC int xfs_attr_node_addname(xfs_trans_t *trans, xfs_da_name_t *args);
STATIC int xfs_attr_node_get(xfs_trans_t *trans, xfs_da_name_t *args);
STATIC int xfs_attr_node_removename(xfs_trans_t *trans, xfs_da_name_t *args);
STATIC int xfs_attr_node_list(xfs_trans_t *trans, xfs_inode_t *dp,
					   attrlist_t *alist, int flags,
					   attrlist_cursor_kern_t *cursor);



/*========================================================================
 * Overall external interface routines.
 *========================================================================*/

/*ARGSUSED*/
int								/* error */
xfs_attr_get(vnode_t *vp, char *name, char *value, int *valuelenp, int flags,
		     struct cred *cred)
{
	xfs_da_name_t args;
	int error;

	xfsda_t_reinit("attr_get", __FILE__, __LINE__);

	XFSSTATS.xs_attr_get++;
	/*
	 * Fill in the arg structure for this request.
	 */
	bzero((char *)&args, sizeof(args));
	args.name = name;
	args.namelen = strlen(name);
	args.value = value;
	args.valuelen = *valuelenp;
	args.flags = flags;
	args.hashval = xfs_da_hashname(args.name, args.namelen);
	args.dp = XFS_VTOI(vp);
	args.whichfork = XFS_ATTR_FORK;

	/*
	 * Decide on what work routines to call based on the inode size.
	 */
	xfs_ilock(args.dp, XFS_ILOCK_SHARED);
	if (XFS_IFORK_Q(args.dp) == 0) {
		error = ENXIO;
	} else if (args.dp->i_d.di_aformat == XFS_DINODE_FMT_LOCAL) {
		error = xfs_attr_shortform_getvalue(NULL, &args);
	} else if (xfs_bmap_one_block(args.dp, XFS_ATTR_FORK)) {
		error = xfs_attr_leaf_get(NULL, &args);
	} else {
		error = xfs_attr_node_get(NULL, &args);
	}
	xfs_iunlock(args.dp, XFS_ILOCK_SHARED);

	/*
	 * Return the number of bytes in the value to the caller.
	 */
	*valuelenp = args.valuelen;

	if (error == EEXIST)
		error = 0;
	xfsda_t_reinit("attr_get", "return value", error);
	return(error);
}

/*ARGSUSED*/
int								/* error */
xfs_attr_set(vnode_t *vp, char *name, char *value, int valuelen, int flags,
		     struct cred *cred)
{
	xfs_trans_t *trans;
	xfs_inode_t *dp;
	xfs_fsblock_t firstblock;
	xfs_bmap_free_t flist;
	xfs_da_name_t args;
	int error, retval, newsize, committed;

	xfsda_t_reinit("attr_set", __FILE__, __LINE__);

	XFSSTATS.xs_attr_set++;
	/*
	 * Fill in the arg structure for this request.
	 */
	bzero((char *)&args, sizeof(args));
	args.name = name;
	args.namelen = strlen(name);
	args.value = value;
	args.valuelen = valuelen;
	args.flags = flags;
	args.hashval = xfs_da_hashname(args.name, args.namelen);
	args.dp = dp = XFS_VTOI(vp);
	args.firstblock = &firstblock;
	args.flist = &flist;
	args.total = MAX_EXT_NEEDED;
	args.whichfork = XFS_ATTR_FORK;

	/*
	 * Set up the transaction envelope.
	 */
	trans = xfs_trans_alloc(dp->i_mount, XFS_TRANS_MKDIR);
	if (error = xfs_trans_reserve(trans, 10,
					     XFS_MKDIR_LOG_RES(dp->i_mount),
					     0, XFS_TRANS_PERM_LOG_RES,
					     XFS_MKDIR_LOG_COUNT)) {
		xfs_trans_cancel(trans, XFS_TRANS_RELEASE_LOG_RES);
		xfsda_t_reinit("attr_set", "return value-1", error);
		return(error);
	}
	VN_HOLD(vp);
	xfs_ilock(dp, XFS_ILOCK_EXCL);
	xfs_trans_ijoin(trans, dp, XFS_ILOCK_EXCL);
	XFS_BMAP_INIT(&flist, &firstblock);

	/*
	 * Decide on what work routines to call.
	 */
	if (XFS_IFORK_Q(dp) == 0) {
		(void)xfs_attr_shortform_create(trans, dp);
	}
	if (dp->i_d.di_aformat == XFS_DINODE_FMT_LOCAL) {
		retval = xfs_attr_shortform_lookup(trans, &args);
		if ((flags & ATTR_REPLACE) && (retval == ENXIO)) {
			goto out;
		} else if (retval == EEXIST) {
			if (flags & ATTR_CREATE)
				goto out;
			retval = xfs_attr_shortform_removename(trans, &args);
		}

		newsize = XFS_ATTR_SF_TOTSIZE(dp);
		newsize += XFS_ATTR_SF_ENTSIZE_BYNAME(args.namelen,
						      args.valuelen);
		if ((newsize <= XFS_IFORK_ASIZE(dp)) &&
		    (args.namelen < XFS_ATTR_SF_ENTSIZE_MAX) &&
		    (args.valuelen < XFS_ATTR_SF_ENTSIZE_MAX)) {
			retval = xfs_attr_shortform_addname(trans, &args);
			ASSERT(retval == 0);
		} else {
			retval = xfs_attr_shortform_to_leaf(trans, &args);
			if (retval != 0)
				goto out;
			retval = xfs_attr_leaf_addname(trans, &args);
/* GROT: another possible req'mt for a double-split btree operation */
		}
	} else if (xfs_bmap_one_block(dp, XFS_ATTR_FORK)) {
		retval = xfs_attr_leaf_addname(trans, &args);
		if (retval == ENOSPC) {
			retval = xfs_attr_leaf_to_node(trans, &args);
			if (retval != 0)
				goto out;
			retval = xfs_attr_node_addname(trans, &args);
		}
	} else {
		retval = xfs_attr_node_addname(trans, &args);
	}

out:
	error = xfs_bmap_finish(&trans, &flist, firstblock, &committed);
	if (error) {
		xfs_bmap_cancel(&flist);
		xfs_trans_cancel(trans, XFS_TRANS_RELEASE_LOG_RES);
		xfsda_t_reinit("attr_set", "return value-2", error);
		return(error);
	} else {
		xfs_trans_commit(trans, XFS_TRANS_RELEASE_LOG_RES);
	}
	xfsda_t_reinit("attr_set", "return value-3", retval);
	return(retval);
}

/*
 * Generic handler routine to remove a name from an attribute list.
 * Transitions attribute list from Btree to shortform as necessary.
 */
/*ARGSUSED*/
int								/* error */
xfs_attr_remove(vnode_t *vp, char *name, int flags, struct cred *cred)
{
	xfs_trans_t *trans;
	xfs_inode_t *dp;
	xfs_fsblock_t firstblock;
	xfs_bmap_free_t flist;
	xfs_da_name_t args;
	int error, retval, committed;
	buf_t *bp;

	xfsda_t_reinit("attr_remove", __FILE__, __LINE__);

	XFSSTATS.xs_attr_remove++;
	dp = XFS_VTOI(vp);
	if (XFS_IFORK_Q(dp) == 0) {
		xfsda_t_reinit("attr_remove", "return value-1", ENXIO);
		return(XFS_ERROR(ENXIO));
	}

	/*
	 * Fill in the arg structure for this request.
	 */
	bzero((char *)&args, sizeof(args));
	args.name = name;
	args.namelen = strlen(name);
	args.flags = flags;
	args.hashval = xfs_da_hashname(args.name, args.namelen);
	args.dp = dp;
	args.firstblock = &firstblock;
	args.flist = &flist;
	args.total = 0;
	args.whichfork = XFS_ATTR_FORK;

	/*
	 * Set up the transaction envelope.
	 */
	trans = xfs_trans_alloc(dp->i_mount, XFS_TRANS_RMDIR);
	if (error = xfs_trans_reserve(trans, 10,
					     XFS_REMOVE_LOG_RES(dp->i_mount),
					     0, XFS_TRANS_PERM_LOG_RES,
					     XFS_DEFAULT_LOG_COUNT)) {
		xfs_trans_cancel(trans, XFS_TRANS_RELEASE_LOG_RES);
		xfsda_t_reinit("attr_remove", "return value-2", error);
		return(error);
	}
	VN_HOLD(vp);
	xfs_ilock(dp, XFS_ILOCK_EXCL);
	xfs_trans_ijoin(trans, dp, XFS_ILOCK_EXCL);
	XFS_BMAP_INIT(&flist, &firstblock);

	/*
	 * Decide on what work routines to call based on the inode size.
	 */
	if (XFS_IFORK_Q(args.dp) == 0) {
		error = ENXIO;
	} else if (dp->i_d.di_aformat == XFS_DINODE_FMT_LOCAL) {
		retval = xfs_attr_shortform_removename(trans, &args);
	} else if (xfs_bmap_one_block(dp, XFS_ATTR_FORK)) {
		retval = xfs_attr_leaf_removename(trans, &args, &bp);
		if (retval)
			goto out;
		if (xfs_attr_shortform_allfit(bp, args.dp))
			retval = xfs_attr_leaf_to_shortform(trans, bp, &args);
	} else {
		retval = xfs_attr_node_removename(trans, &args);
	}

out:
	error = xfs_bmap_finish(&trans, &flist, firstblock, &committed);
	if (error) {
		xfs_bmap_cancel(&flist);
		xfs_trans_cancel(trans, XFS_TRANS_RELEASE_LOG_RES);
		xfsda_t_reinit("attr_remove", "return value-3", error);
		return(error);
	} else {
		xfs_trans_commit(trans, XFS_TRANS_RELEASE_LOG_RES);
	}
	xfsda_t_reinit("attr_remove", "return value-4", retval);
	return(retval);
}

/*ARGSUSED*/
int								/* error */
xfs_attr_list(vnode_t *vp, char *buffer, int bufsize, int flags,
		      attrlist_cursor_kern_t *cursor, struct cred *cred)
{
	attrlist_t *alist;
	xfs_inode_t *dp;
	int error;

	xfsda_t_reinit("attr_list", __FILE__, __LINE__);

	XFSSTATS.xs_attr_list++;
	alist = (attrlist_t *)buffer;
	alist->al_count = 0;
	alist->al_more = 0;
	alist->al_offset[0] = bufsize;
	dp = XFS_VTOI(vp);
	if (XFS_IFORK_Q(dp) == 0) {
		xfsda_t_reinit("attr_list", "return value-1", 0);
		return(0);
	}

	/*
	 * Validate the cursor.
	 */
	if ((cursor->initted > 1) || cursor->pad1 || cursor->pad2) {
		xfsda_t_reinit("attr_list", "return value-2", EINVAL);
		return(XFS_ERROR(EINVAL));
	}
	if ((!cursor->initted) &&
	    (cursor->blkno || cursor->hashval || cursor->index)) {
		xfsda_t_reinit("attr_list", "return value-3", EINVAL);
		return(XFS_ERROR(EINVAL));
	}

	/*
	 * Decide on what work routines to call based on the inode size.
	 */
	xfs_ilock(dp, XFS_ILOCK_SHARED);
	if (XFS_IFORK_Q(dp) == 0) {
		error = 0;
	} else if (dp->i_d.di_aformat == XFS_DINODE_FMT_LOCAL) {
		error = xfs_attr_shortform_list(dp, alist, flags, cursor);
	} else if (xfs_bmap_one_block(dp, XFS_ATTR_FORK)) {
		error = xfs_attr_leaf_list(NULL, dp, alist, flags, cursor);
	} else {
		error = xfs_attr_node_list(NULL, dp, alist, flags, cursor);
	}
	xfs_iunlock(dp, XFS_ILOCK_SHARED);

	xfsda_t_reinit("attr_list", "return value-4", error);
	return(error);
}


/*========================================================================
 * External routines when attribute list is one block
 *========================================================================*/

/*
 * Add a name to the leaf attribute list structure
 * This is the external routine.
 */
int
xfs_attr_leaf_addname(xfs_trans_t *trans, xfs_da_name_t *args)
{
	int index, retval, error;
	buf_t *bp;

	error = xfs_da_read_buf(trans, args->dp, 0, &bp, XFS_ATTR_FORK);
	if (error)
		return(error);
	ASSERT(bp != NULL);

	retval = xfs_attr_leaf_lookup_int(bp, args, &index);
	if ((args->flags & ATTR_REPLACE) && (retval == ENXIO)) {
		return(ENXIO);
	} else if (retval == EEXIST) {
		if (args->flags & ATTR_CREATE)		/* pure create op */
			return(EEXIST);
		error = xfs_attr_leaf_remove(trans, bp, index, &retval);
		if (error)
			return(error);
	}
	retval = xfs_attr_leaf_add(trans, bp, args, index);
	return(retval);
}

/*
 * Remove a name from the leaf attribute list structure
 * This is the external routine.
 */
STATIC int
xfs_attr_leaf_removename(xfs_trans_t *trans, xfs_da_name_t *args, buf_t **bpp)
{
	int index, retval, error;
	buf_t *bp;

	error = xfs_da_read_buf(trans, args->dp, 0, &bp, XFS_ATTR_FORK);
	if (error)
		return(error);

	ASSERT(bp != NULL);
	error = xfs_attr_leaf_lookup_int(bp, args, &index);
	if (error == ENXIO)
		return(ENXIO);

	error = xfs_attr_leaf_remove(trans, bp, index, &retval);
	if (error)
		return(error);
	*bpp = bp;
	return(0);
}

/*
 * Look up a name in a leaf attribute list structure.
 * This is the external routine.
 */
STATIC int
xfs_attr_leaf_get(xfs_trans_t *trans, xfs_da_name_t *args)
{
	int index, error;
	buf_t *bp;

	error = xfs_da_read_buf(trans, args->dp, 0, &bp, XFS_ATTR_FORK);
	if (error)
		return(error);
	ASSERT(bp != NULL);
	error = xfs_attr_leaf_lookup_int(bp, args, &index);
	if (error == EEXIST) {
		error = xfs_attr_leaf_getvalue(trans, bp, args, index);
	}
	xfs_trans_brelse(trans, bp);
	return(error);
}

/*
 * Copy out attribute entries for attr_list(), for leaf attribute lists.
 */
STATIC int
xfs_attr_leaf_list(xfs_trans_t *trans, xfs_inode_t *dp, attrlist_t *alist,
			       int flags, attrlist_cursor_kern_t *cursor)
{
	buf_t *bp;
	int error;

	if (cursor->blkno > 0)
		cursor->blkno = cursor->index = 0;
	error = xfs_da_read_buf(trans, dp, 0, &bp, XFS_ATTR_FORK);
	if (error)
		return(error);
	ASSERT(bp != NULL);
	(void)xfs_attr_leaf_list_int(bp, alist, flags, cursor);
	xfs_trans_brelse(trans, bp);
	return(0);
}


/*========================================================================
 * External routines when attribute list size > XFS_LBSIZE(mp).
 *========================================================================*/

/*
 * Add a name to a Btree-format attribute list.
 *
 * This will involve walking down the Btree, and may involve splitting
 * leaf nodes and even splitting intermediate nodes up to and including
 * the root node (a special case of an intermediate node).
 */
STATIC int
xfs_attr_node_addname(xfs_trans_t *trans, xfs_da_name_t *args)
{
	xfs_da_state_t *state;
	xfs_da_state_blk_t *blk;
	int retval, error;

	/*
	 * Fill in bucket of arguments/results/context to carry around.
	 */
	state = xfs_da_state_alloc();
	state->args = args;
	state->mp = args->dp->i_mount;
	state->trans = trans;
	state->blocksize = state->mp->m_sb.sb_blocksize;

	/*
	 * Search to see if name already exists, and get back a pointer
	 * to where it should go.
	 */
	error = xfs_da_node_lookup_int(state, &retval);
	if (error) {
		retval = error;
		goto out;
	}
	blk = &state->path.blk[ state->path.active-1 ];
	ASSERT(blk->magic == XFS_ATTR_LEAF_MAGIC);
	if ((args->flags & ATTR_REPLACE) && (retval == ENXIO)) {
		goto out;
	} else if (retval == EEXIST) {
		if (args->flags & ATTR_CREATE)
			goto out;
		error = xfs_attr_leaf_remove(trans, blk->bp, blk->index,
						    &retval);
		if (error) {
			retval = error;
			goto out;
		}
		xfs_da_fixhashpath(state, &state->path);
	}
		
	retval = xfs_attr_leaf_add(state->trans, blk->bp, state->args,
						 blk->index);
	if (retval == ENOSPC) {
		/*
		 * Addition failed, split as many Btree elements as required.
		 */
		retval = xfs_da_split(state);
	} else {
		/*
		 * Addition succeeded, update Btree hashvals.
		 */
		xfs_da_fixhashpath(state, &state->path);
	}

out:
	xfs_da_state_free(state);
	return(retval);
}

/*
 * Remove a name from a B-tree attribute list.
 *
 * This will involve walking down the Btree, and may involve joining
 * leaf nodes and even joining intermediate nodes up to and including
 * the root node (a special case of an intermediate node).
 */
STATIC int
xfs_attr_node_removename(xfs_trans_t *trans, xfs_da_name_t *args)
{
	xfs_da_state_t *state;
	xfs_da_state_blk_t *blk;
	int retval, error;

	state = xfs_da_state_alloc();
	state->args = args;
	state->mp = args->dp->i_mount;
	state->trans = trans;
	state->blocksize = state->mp->m_sb.sb_blocksize;

	/*
	 * Search to see if name exists, and get back a pointer to it.
	 */
	error = xfs_da_node_lookup_int(state, &retval);
	if (error)
		goto out;
	if (retval != EEXIST) {
		error = retval;
		goto out;
	}

	/*
	 * Remove the name and update the hashvals in the tree.
	 */
	blk = &state->path.blk[ state->path.active-1 ];
	ASSERT(blk->magic == XFS_ATTR_LEAF_MAGIC);
	error = xfs_attr_leaf_remove(state->trans, blk->bp, blk->index,
						   &retval);
	if (error)
		goto out;
	xfs_da_fixhashpath(state, &state->path);

	/*
	 * Check to see if the tree needs to be collapsed.
	 */
	if (retval)
		error = xfs_da_join(state);
	retval = 0;

out:
	xfs_da_state_free(state);
	if (error)
		return(error);
	return(0);
}

/*
 * Look up a filename in a node attribute list.
 * Use an internal routine to actually do all the work.
 */
STATIC int
xfs_attr_node_get(xfs_trans_t *trans, xfs_da_name_t *args)
{
	xfs_da_state_t *state;
	int error, retval;
	int i;

	state = xfs_da_state_alloc();
	state->args = args;
	state->mp = args->dp->i_mount;
	state->trans = trans;
	state->blocksize = state->mp->m_sb.sb_blocksize;

	/*
	 * Search to see if name exists,
	 * and get back a pointer to it.
	 */
	error = xfs_da_node_lookup_int(state, &retval);
	if (error) {
		retval = error;
	} else if (retval == EEXIST) {
		retval = xfs_da_node_getvalue(state);
	}

	/* 
	 * If not in a transaction, we have to release all the buffers.
	 */
	for (i = 0; i < state->path.active; i++)
		xfs_trans_brelse(trans, state->path.blk[i].bp);

	xfs_da_state_free(state);
	return(retval);
}

STATIC int							/* error */
xfs_attr_node_list(xfs_trans_t *trans, xfs_inode_t *dp, attrlist_t *alist,
			       int flags, attrlist_cursor_kern_t *cursor)
{
	xfs_da_blkinfo_t *info;
	int error, i;
	buf_t *bp;

	/*
	 * Do all sorts of validation on the passed-in cursor structure.
	 * If anything is amiss, ignore the cursor and look up the hashval
	 * starting from the btree root.
	 * GROT: this change-attr-list recovery code needs looking at.
	 */
	bp = NULL;
	if (cursor->blkno > 0) {
		error = xfs_da_read_buf(trans, dp, cursor->blkno, &bp,
					       XFS_ATTR_FORK);
		if (error)
			return(error);
		if (bp) {
			info = (xfs_da_blkinfo_t *)bp->b_un.b_addr;
			if (info->magic != XFS_ATTR_LEAF_MAGIC) {
				xfs_trans_brelse(trans, bp);
				bp = NULL;
				cursor->blkno = cursor->index = 0;
			}
		} else {
			cursor->blkno = cursor->index = 0;
		}
	}
	if (cursor->blkno == 0) {
		cursor->initted = 1;
		for (;;) {
			xfs_da_intnode_t *node;
			xfs_da_node_entry_t *btree;

			error = xfs_da_read_buf(trans, dp, cursor->blkno, &bp,
						       XFS_ATTR_FORK);
			if (error)
				return(error);
			ASSERT(bp != NULL);
			node = (xfs_da_intnode_t *)bp->b_un.b_addr;
			if (node->hdr.info.magic != XFS_DA_NODE_MAGIC)
				break;
			btree = node->btree;
			for (i = 0; i < node->hdr.count; btree++, i++) {
				if (cursor->hashval < btree->hashval) {
					cursor->blkno = btree->before;
					break;
				}
			}
			xfs_trans_brelse(trans, bp);
			if (i == node->hdr.count)
				return(0);
		}
	}

	/*
	 * Roll upward through the blocks, processing each leaf block in
	 * order.  As long as there is space in the result buffer, keep
	 * adding the information.
	 */
	for (;;) {
		error = xfs_attr_leaf_list_int(bp, alist, flags, cursor);
		if (error)
			break;
		info = (xfs_da_blkinfo_t *)bp->b_un.b_addr;
		ASSERT(info->magic == XFS_ATTR_LEAF_MAGIC);
		if (info->forw == 0)
			break;
		cursor->blkno = info->forw;
		cursor->index = 0;
		xfs_trans_brelse(trans, bp);
		error = xfs_da_read_buf(trans, dp, cursor->blkno, &bp,
					       XFS_ATTR_FORK);
		if (error)
			return(error);
		ASSERT(bp != NULL);
	}
	xfs_trans_brelse(trans, bp);
	return(0);
}
