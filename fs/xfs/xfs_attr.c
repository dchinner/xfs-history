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
#include "xfs_ag.h"
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
 */
#define MAX_EXT_NEEDED 9

/*========================================================================
 * Function prototypes for the kernel.
 *========================================================================*/

/*
 * Internal routines when attribute list fits inside the inode.
 */
STATIC int xfs_attr_shortform_addname(xfs_trans_t *trans, xfs_da_args_t *args);

/*
 * Internal routines when attribute list is one block.
 */
STATIC int xfs_attr_leaf_get(xfs_trans_t *trans, xfs_da_args_t *args);
STATIC int xfs_attr_leaf_removename(xfs_trans_t *trans, xfs_da_args_t *args,
						buf_t **bpp);
STATIC int xfs_attr_leaf_list(xfs_inode_t *dp, attrlist_t *alist, int flags,
					  attrlist_cursor_kern_t *cursor);

/*
 * Internal routines when attribute list is more than one block.
 */
STATIC int xfs_attr_node_addname(xfs_trans_t *trans, xfs_da_args_t *args);
STATIC int xfs_attr_node_get(xfs_trans_t *trans, xfs_da_args_t *args);
STATIC int xfs_attr_node_removename(xfs_trans_t *trans, xfs_da_args_t *args);
STATIC int xfs_attr_node_list(xfs_inode_t *dp, attrlist_t *alist, int flags,
					  attrlist_cursor_kern_t *cursor);
/*
 * Routines to manipulate out-of-line attribute values.
 */
STATIC int xfs_attr_rmtval_get(xfs_da_args_t *args);
STATIC int xfs_attr_rmtval_set(xfs_da_args_t *args);
STATIC int xfs_attr_rmtval_remove(xfs_da_args_t *args);

#define ATTR_RMTVALUE_MAPSIZE	1	/* # of map entries at once */
#define ATTR_RMTVALUE_TRANSBLKS	8	/* max # of blks in a transaction */



/*========================================================================
 * Overall external interface routines.
 *========================================================================*/

/*ARGSUSED*/
int								/* error */
xfs_attr_get(vnode_t *vp, char *name, char *value, int *valuelenp, int flags,
		     struct cred *cred)
{
	xfs_da_args_t args;
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
	} else {
		if (xfs_bmap_one_block(args.dp, XFS_ATTR_FORK))
			error = xfs_attr_leaf_get(NULL, &args);
		else
			error = xfs_attr_node_get(NULL, &args);
		if ((error == 0) && (args.aleaf_rmtblkno > 0)) {
			error = xfs_attr_rmtval_get(&args);
		}
	}
	xfs_iunlock(args.dp, XFS_ILOCK_SHARED);

	/*
	 * Return the number of bytes in the value to the caller.
	 */
	*valuelenp = args.valuelen;

	if (error == EEXIST)
		error = 0;
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
	xfs_da_args_t args;
	int error, retval, committed;

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
	 * If the inode doesn't have an attribute fork, add one.
	 */
	if (XFS_IFORK_Q(dp) == 0) {
		error = xfs_bmap_add_attrfork(dp);
		if (error)
			return(error);
	}

	/*
	 * Set up the transaction envelope.
	 */
	trans = xfs_trans_alloc(dp->i_mount, XFS_TRANS_ATTR_SET);
	if (error = xfs_trans_reserve(trans, 10,
					     XFS_SETATTR_LOG_RES(dp->i_mount),
					     0, XFS_TRANS_PERM_LOG_RES,
					     XFS_SETATTR_LOG_COUNT)) {
		xfs_trans_cancel(trans, XFS_TRANS_RELEASE_LOG_RES);
		return(error);
	}
	xfs_ilock(dp, XFS_ILOCK_EXCL);
	xfs_trans_ijoin(trans, dp, XFS_ILOCK_EXCL);
	xfs_trans_ihold(trans, dp);
	XFS_BMAP_INIT(&flist, &firstblock);

	/*
	 * Decide on what work routines to call.
	 *
	 * GROT: make a pure replace not have a "hole" in the middle where
	 * GROT: there is no attribute, do an atomic "rename".
	 */
	if (dp->i_afp->if_bytes == 0) {
		(void)xfs_attr_shortform_create(trans, dp);
	}
	if (dp->i_d.di_aformat == XFS_DINODE_FMT_LOCAL) {
		retval = xfs_attr_shortform_addname(trans, &args);
		if (retval == ENOSPC) {
			retval = xfs_attr_shortform_to_leaf(trans, &args);
			if (retval != 0)
				goto out1;
/* GROT: another possible req'mt for a double-split btree operation */
			retval = xfs_attr_leaf_addname(trans, &args);
			if (retval == ENOSPC) {
				retval = xfs_attr_leaf_to_node(trans, &args);
				if (retval != 0)
					goto out1;
				retval = xfs_attr_node_addname(trans, &args);
			}
		}
	} else if (xfs_bmap_one_block(dp, XFS_ATTR_FORK)) {
		retval = xfs_attr_leaf_addname(trans, &args);
		if (retval == ENOSPC) {
			retval = xfs_attr_leaf_to_node(trans, &args);
			if (retval != 0)
				goto out1;
			retval = xfs_attr_node_addname(trans, &args);
		}
	} else {
		retval = xfs_attr_node_addname(trans, &args);
	}

out1:
	error = xfs_bmap_finish(&trans, &flist, firstblock, &committed);
	if (error) {
		xfs_bmap_cancel(&flist);
		xfs_trans_cancel(trans, XFS_TRANS_RELEASE_LOG_RES);
		return(error);
	} else {
		xfs_trans_commit(trans, XFS_TRANS_RELEASE_LOG_RES);
	}

	/*
	 * If there was an out-of-line value, allocate the blocks we
	 * identified for its storage and copy the value.  This is done
	 * after we create the attribute so that we don't overflow the
	 * maximum size of a transaction and/or hit a deadlock.
	 */
	if ((retval == 0) && (args.aleaf_rmtblkno > 0)) {
		error = xfs_attr_rmtval_set(&args);
		if (error)
			goto out2;
		error = xfs_attr_leaf_clearflag(&args);
		if (error)
			goto out2;
	}
out2:
	xfs_iunlock(dp, XFS_ILOCK_EXCL);
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
	xfs_da_args_t args;
	int error, retval, committed;
	buf_t *bp;

	xfsda_t_reinit("attr_remove", __FILE__, __LINE__);

	XFSSTATS.xs_attr_remove++;
	dp = XFS_VTOI(vp);
	if (XFS_IFORK_Q(dp) == 0) {
		return(XFS_ERROR(ENXIO));
	}

	/*
	 * Fill in the arg structure for this request.
	 */
	bzero((char *)&args, sizeof(args));
	args.name = name;
	args.namelen = strlen(name);
	args.flags = flags | XFS_ATTR_INCOMPLETE; /* bits must not collide! */
	args.hashval = xfs_da_hashname(args.name, args.namelen);
	args.dp = dp;
	args.firstblock = &firstblock;
	args.flist = &flist;
	args.total = 0;
	args.whichfork = XFS_ATTR_FORK;

	/*
	 * If there is an out-of-line value, de-allocate the blocks.
	 * This is done before we remove the attribute so that we don't
	 * overflow the maximum size of a transaction and/or hit a deadlock.
	 */
	xfs_ilock(dp, XFS_ILOCK_EXCL);
	if (args.dp->i_d.di_aformat != XFS_DINODE_FMT_LOCAL) {
		if (xfs_bmap_one_block(args.dp, XFS_ATTR_FORK))
			error = xfs_attr_leaf_get(NULL, &args);
		else
			error = xfs_attr_node_get(NULL, &args);
		if ((error == E2BIG) && (args.aleaf_rmtblkno > 0)) {
			error = xfs_attr_leaf_setflag(&args);
			if (error)
				return(error);
			error = xfs_attr_rmtval_remove(&args);
			if (error)
				return(error);
		}
	}

	/*
	 * Set up the transaction envelope for the rest of the removal.
	 */
	trans = xfs_trans_alloc(dp->i_mount, XFS_TRANS_ATTR_RM);
	if (error = xfs_trans_reserve(trans, 10,
					     XFS_RMATTR_LOG_RES(dp->i_mount),
					     0, XFS_TRANS_PERM_LOG_RES,
					     XFS_RMATTR_LOG_COUNT)) {
		xfs_trans_cancel(trans, XFS_TRANS_RELEASE_LOG_RES);
		return(error);
	}
	VN_HOLD(vp);
	xfs_trans_ijoin(trans, dp, XFS_ILOCK_EXCL);
	XFS_BMAP_INIT(&flist, &firstblock);

	/*
	 * Decide on what work routines to call based on the inode size.
	 * GROT: make use of btree-locate operation above if we can.
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
		return(error);
	} else {
		xfs_trans_commit(trans, XFS_TRANS_RELEASE_LOG_RES);
	}
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
	dp = XFS_VTOI(vp);

	/*
	 * Validate the cursor.
	 */
	if ((cursor->initted > 1) || cursor->pad1 || cursor->pad2) {
		return(XFS_ERROR(EINVAL));
	}
	if ((!cursor->initted) &&
	    (cursor->blkno || cursor->hashval || cursor->index)) {
		return(XFS_ERROR(EINVAL));
	}

	/*
	 * Initialize the output buffer.
	 */
	alist = (attrlist_t *)buffer;
	alist->al_count = 0;
	alist->al_more = 0;
	alist->al_offset[0] = bufsize;

	/*
	 * Decide on what work routines to call based on the inode size.
	 */
	xfs_ilock(dp, XFS_ILOCK_SHARED);
	if (XFS_IFORK_Q(dp) == 0) {
		error = 0;
	} else if (dp->i_d.di_aformat == XFS_DINODE_FMT_LOCAL) {
		error = xfs_attr_shortform_list(dp, alist, flags, cursor);
	} else if (xfs_bmap_one_block(dp, XFS_ATTR_FORK)) {
		error = xfs_attr_leaf_list(dp, alist, flags, cursor);
	} else {
		error = xfs_attr_node_list(dp, alist, flags, cursor);
	}
	xfs_iunlock(dp, XFS_ILOCK_SHARED);

	return(error);
}


/*========================================================================
 * External routines when attribute list is inside the inode
 *========================================================================*/

/*
 * Add a name to the shortform attribute list structure
 * This is the external routine.
 */
int
xfs_attr_shortform_addname(xfs_trans_t *trans, xfs_da_args_t *args)
{
	int newsize, retval;

	retval = xfs_attr_shortform_lookup(trans, args);
	if ((args->flags & ATTR_REPLACE) && (retval == ENXIO)) {
		return(ENXIO);
	} else if (retval == EEXIST) {
		if (args->flags & ATTR_CREATE)
			return(EEXIST);
		retval = xfs_attr_shortform_removename(trans, args);
		ASSERT(retval == 0);
	}

	newsize = XFS_ATTR_SF_TOTSIZE(args->dp);
	newsize += XFS_ATTR_SF_ENTSIZE_BYNAME(args->namelen, args->valuelen);
	if ((newsize <= XFS_IFORK_ASIZE(args->dp)) &&
	    (args->namelen < XFS_ATTR_SF_ENTSIZE_MAX) &&
	    (args->valuelen < XFS_ATTR_SF_ENTSIZE_MAX)) {
		retval = xfs_attr_shortform_add(trans, args);
		ASSERT(retval == 0);
	} else {
		return(ENOSPC);
	}
	return(0);
}


/*========================================================================
 * External routines when attribute list is one block
 *========================================================================*/

/*
 * Add a name to the leaf attribute list structure
 * This is the external routine.
 */
int
xfs_attr_leaf_addname(xfs_trans_t *trans, xfs_da_args_t *args)
{
	int retval, error;
	buf_t *bp;

	args->aleaf_blkno = 0;
	error = xfs_da_read_buf(trans, args->dp, args->aleaf_blkno, &bp,
				       XFS_ATTR_FORK);
	if (error)
		return(error);
	ASSERT(bp != NULL);

	retval = xfs_attr_leaf_lookup_int(bp, args);
	if ((args->flags & ATTR_REPLACE) && (retval == ENXIO)) {
		return(ENXIO);
	} else if (retval == EEXIST) {
		if (args->flags & ATTR_CREATE)		/* pure create op */
			return(EEXIST);
		(void)xfs_attr_leaf_remove(trans, bp, args);
	}
	retval = xfs_attr_leaf_add(trans, bp, args);
	return(retval);
}

/*
 * Remove a name from the leaf attribute list structure
 * This is the external routine.
 */
STATIC int
xfs_attr_leaf_removename(xfs_trans_t *trans, xfs_da_args_t *args, buf_t **bpp)
{
	buf_t *bp;
	int error;

	args->aleaf_blkno = 0;
	error = xfs_da_read_buf(trans, args->dp, args->aleaf_blkno, &bp,
				       XFS_ATTR_FORK);
	if (error)
		return(error);

	ASSERT(bp != NULL);
	error = xfs_attr_leaf_lookup_int(bp, args);
	if (error == ENXIO)
		return(ENXIO);

	(void)xfs_attr_leaf_remove(trans, bp, args);
	*bpp = bp;
	return(0);
}

/*
 * Look up a name in a leaf attribute list structure.
 * This is the external routine.
 */
STATIC int
xfs_attr_leaf_get(xfs_trans_t *trans, xfs_da_args_t *args)
{
	buf_t *bp;
	int error;

	args->aleaf_blkno = 0;
	error = xfs_da_read_buf(trans, args->dp, args->aleaf_blkno, &bp,
				       XFS_ATTR_FORK);
	if (error)
		return(error);
	ASSERT(bp != NULL);

	error = xfs_attr_leaf_lookup_int(bp, args);
	if (error == EEXIST)  {
		error = xfs_attr_leaf_getvalue(bp, args);
	}
	xfs_trans_brelse(trans, bp);
	return(error);
}

/*
 * Copy out attribute entries for attr_list(), for leaf attribute lists.
 */
STATIC int
xfs_attr_leaf_list(xfs_inode_t *dp, attrlist_t *alist, int flags,
			       attrlist_cursor_kern_t *cursor)
{
	buf_t *bp;
	int error;

	if (cursor->blkno > 0)
		cursor->blkno = cursor->index = 0;
	error = xfs_da_read_buf(NULL, dp, 0, &bp, XFS_ATTR_FORK);
	if (error)
		return(error);
	ASSERT(bp != NULL);

	(void)xfs_attr_leaf_list_int(bp, alist, flags, cursor);
	xfs_trans_brelse(NULL, bp);
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
xfs_attr_node_addname(xfs_trans_t *trans, xfs_da_args_t *args)
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
		(void)xfs_attr_leaf_remove(trans, blk->bp, state->args);
		xfs_da_fixhashpath(state, &state->path);
	}
		
	retval = xfs_attr_leaf_add(state->trans, blk->bp, state->args);
	if (retval == ENOSPC) {
		if (state->path.active == 1) {
			/*
			 * It is really a single leaf node, but it has
			 * out-of-line values so it looked like it might
			 * have been a b-tree.
			 */
			retval = xfs_attr_leaf_to_node(trans, args);
			if (retval != 0)
				goto out;
			retval = xfs_attr_node_addname(trans, args);
		} else {
			/*
			 * Split as many Btree elements as required.
			 */
			retval = xfs_da_split(state);
		}
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
xfs_attr_node_removename(xfs_trans_t *trans, xfs_da_args_t *args)
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
	retval = xfs_attr_leaf_remove(state->trans, blk->bp, state->args);
	xfs_da_fixhashpath(state, &state->path);

	/*
	 * Check to see if the tree needs to be collapsed.
	 */
	if (retval && (state->path.active > 1))
		/* GROT: deal with INCOMPLETE entries, don't copy them */
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
xfs_attr_node_get(xfs_trans_t *trans, xfs_da_args_t *args)
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
xfs_attr_node_list(xfs_inode_t *dp, attrlist_t *alist, int flags,
			       attrlist_cursor_kern_t *cursor)
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
		error = xfs_da_read_buf(NULL, dp, cursor->blkno, &bp,
					      XFS_ATTR_FORK);
		if (error)
			return(error);
		if (bp) {
			info = (xfs_da_blkinfo_t *)bp->b_un.b_addr;
			if (info->magic != XFS_ATTR_LEAF_MAGIC) {
				xfs_trans_brelse(NULL, bp);
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

			error = xfs_da_read_buf(NULL, dp, cursor->blkno, &bp,
						      XFS_ATTR_FORK);
			if (error)
				return(error);
			ASSERT(bp != NULL);
			info = (xfs_da_blkinfo_t *)bp->b_un.b_addr;
			if (info->magic != XFS_DA_NODE_MAGIC)
				break;
			node = (xfs_da_intnode_t *)info;
			btree = node->btree;
			for (i = 0; i < node->hdr.count; btree++, i++) {
				if (cursor->hashval < btree->hashval) {
					cursor->blkno = btree->before;
					break;
				}
			}
			xfs_trans_brelse(NULL, bp);
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
		xfs_trans_brelse(NULL, bp);
		error = xfs_da_read_buf(NULL, dp, cursor->blkno, &bp,
					      XFS_ATTR_FORK);
		if (error)
			return(error);
		ASSERT(bp != NULL);
	}
	xfs_trans_brelse(NULL, bp);
	return(0);
}


/*========================================================================
 * External routines for manipulating out-of-line attribute values.
 *========================================================================*/

/*
 * Read the value associated with an attribute from the out-of-line buffer
 * that we stored it in.
 */
STATIC int
xfs_attr_rmtval_get(xfs_da_args_t *args)
{
	xfs_bmbt_irec_t map[ATTR_RMTVALUE_MAPSIZE];
	xfs_fsblock_t firstblock;
	xfs_mount_t *mp;
	daddr_t dblkno, radblkno;
	caddr_t dst;
	buf_t *bp;
	int nmap, error, tmp, valuelen, lblkno, blkcnt, bbsperblk, i, j;

	mp = args->dp->i_mount;
	bbsperblk = XFS_FSB_TO_BB(mp, 1);
	dst = args->value;
	valuelen = args->valuelen;
	lblkno = args->aleaf_rmtblkno;
	while (valuelen > 0) {
		firstblock = NULLFSBLOCK;	/* GROT: is this right? */
		nmap = ATTR_RMTVALUE_MAPSIZE;
		error = xfs_bmapi(NULL, args->dp, lblkno,
				  XFS_B_TO_FSB(mp, valuelen),
				  XFS_BMAPI_ATTRFORK | XFS_BMAPI_METADATA,
				  &firstblock, 0, map, &nmap, 0);
		if (error)
			return(error);
		ASSERT(nmap >= 1);

		for (i = 0; (i < nmap) && (valuelen > 0); i++) {
			ASSERT((map[i].br_startblock != DELAYSTARTBLOCK) &&
			       (map[i].br_startblock != HOLESTARTBLOCK));
			dblkno = XFS_FSB_TO_DADDR(mp, map[i].br_startblock);
			radblkno = XFS_FSB_TO_DADDR(mp, map[i].br_startblock+1);
			blkcnt = map[i].br_blockcount;
			for (j = 0; j < blkcnt; j++) {
				if (j < blkcnt-1) {
					bp = breada(mp->m_dev,
						    dblkno, bbsperblk,
						    radblkno, bbsperblk);
				} else {
					bp = bread(mp->m_dev,
						   dblkno, bbsperblk);
				}
				if (error = geterror(bp))
					return(error);

				tmp = (valuelen < bp->b_bufsize)
					? valuelen : bp->b_bufsize;
				bcopy(bp->b_un.b_addr, dst, tmp);
				brelse(bp);
				dst += tmp;
				valuelen -= tmp;
				dblkno += bbsperblk;
				radblkno += bbsperblk;
			}
			lblkno += blkcnt;
		}
	}
	ASSERT(valuelen == 0);
	return(0);
}

/*
 * Write the value associated with an attribute into the out-of-line buffer
 * that we have defined for it.
 */
STATIC int
xfs_attr_rmtval_set(xfs_da_args_t *args)
{
	xfs_trans_t *trans;
	xfs_mount_t *mp;
	xfs_bmbt_irec_t map;
	xfs_fsblock_t firstblock;
	xfs_bmap_free_t flist;
	daddr_t dblkno;
	caddr_t src;
	buf_t *bp;
	int lblkno, blkcnt, bbsperblk, valuelen;
	int nmap, error, tmp, committed, j;

	mp = args->dp->i_mount;
	src = args->value;

	/*
	 * Roll through the "value", allocating blocks on disk as required.
	 */
	lblkno = args->aleaf_rmtblkno;
	blkcnt = XFS_B_TO_FSB(mp, args->valuelen);
	while (blkcnt > 0) {
		/*
		 * Start a new transaction.
		 */
		trans = xfs_trans_alloc(mp, XFS_TRANS_ATTR_SET);
		if (error = xfs_trans_reserve(trans, 16+blkcnt,
					      XFS_SETATTR_LOG_RES(mp),
					      0, XFS_TRANS_PERM_LOG_RES,
					      XFS_SETATTR_LOG_COUNT)) {
			goto out2;
		}
		xfs_trans_ijoin(trans, args->dp, XFS_ILOCK_EXCL);
		xfs_trans_ihold(trans, args->dp);

		/*
		 * Allocate a single extent, up to the size of the value.
		 */
		XFS_BMAP_INIT(&flist, &firstblock);
		nmap = 1;
		error = xfs_bmapi(trans, args->dp, lblkno, blkcnt,
				  XFS_BMAPI_ATTRFORK | XFS_BMAPI_METADATA |
					XFS_BMAPI_WRITE,
				  &firstblock, 0, &map, &nmap, 0);
		if (error)
			goto out1;
		ASSERT(nmap == 1);
		ASSERT((map.br_startblock != DELAYSTARTBLOCK) &&
		       (map.br_startblock != HOLESTARTBLOCK));
		lblkno += map.br_blockcount;
		blkcnt -= map.br_blockcount;

		/*
		 * Commit the current transaction.
		 */
		error = xfs_bmap_finish(&trans, &flist, firstblock, &committed);
		if (error)
			goto out1;
		xfs_trans_commit(trans, XFS_TRANS_RELEASE_LOG_RES);
	}

	/*
	 * Roll through the "value", copying the attribute value to the
	 * already-allocated blocks.  Start/commit transactions as required
	 * to keep the number of blocks logged in a single transaction to a
	 * small value.
	 */
	bbsperblk = XFS_FSB_TO_BB(mp, 1);
	lblkno = args->aleaf_rmtblkno;
	valuelen = args->valuelen;
	while (valuelen > 0) {
		/*
		 * Try to remember where we decided to put the value.
		 */
		XFS_BMAP_INIT(&flist, &firstblock);
		nmap = 1;
		error = xfs_bmapi(NULL, args->dp, lblkno,
					XFS_B_TO_FSB(mp, valuelen),
					XFS_BMAPI_ATTRFORK | XFS_BMAPI_METADATA,
					&firstblock, 0, &map, &nmap, 0);
		if (error)
			goto out3;
		ASSERT(nmap == 1);
		ASSERT((map.br_startblock != DELAYSTARTBLOCK) &&
		       (map.br_startblock != HOLESTARTBLOCK));

		/*
		 * Loop over all filesystem blocks dedicated to the
		 * "value".  They must all be logged separately so
		 * that we know what block size to use when we
		 * invalidate them on a "remove" operation.
		 */
		dblkno = XFS_FSB_TO_DADDR(mp, map.br_startblock),
		blkcnt = XFS_FSB_TO_BB(mp, map.br_blockcount);
		while (blkcnt > 0){
			/*
			 * Start a new transaction.
			 */
			trans = xfs_trans_alloc(mp, XFS_TRANS_ATTR_SET);
			if (error = xfs_trans_reserve(trans,
				      ATTR_RMTVALUE_TRANSBLKS,
				      XFS_FSB_TO_B(mp, ATTR_RMTVALUE_TRANSBLKS),
				      0, XFS_TRANS_PERM_LOG_RES,
				      XFS_SETATTR_LOG_COUNT)) {
				goto out2;
			}

			/*
			 * Copy the value into the buffers, one at a time.
			 * We do this so that VOP_INACTIVE() will work.
			 */
			for (j = 0; (j < ATTR_RMTVALUE_TRANSBLKS) &&
				    (blkcnt > 0); j++){
				bp = xfs_trans_get_buf(trans, mp->m_dev, dblkno,
							      bbsperblk, 0);
				ASSERT(bp);
				ASSERT(!geterror(bp));

				tmp = (valuelen < bp->b_bufsize)
						? valuelen : bp->b_bufsize;
				bcopy(src, bp->b_un.b_addr, tmp);
				if (tmp < bp->b_bufsize)
					bzero(bp->b_un.b_addr + tmp,
					      bp->b_bufsize - tmp);
				xfs_trans_log_buf(trans, bp, 0,
							 XFS_FSB_TO_B(mp, 1)-1);
				src += tmp;
				valuelen -= tmp;
				dblkno += bbsperblk;
				blkcnt -= bbsperblk;
			}

			/*
			 * Commit the current transaction.
			 */
			xfs_trans_commit(trans, XFS_TRANS_RELEASE_LOG_RES);
		}
		lblkno += map.br_blockcount;
	}
	ASSERT(valuelen == 0);
	return(0);

out1:
	xfs_bmap_cancel(&flist);
out2:
	xfs_trans_cancel(trans, XFS_TRANS_RELEASE_LOG_RES);
out3:
	return(error);
}

/*
 * Remove the value associated with an attribute by deleting the
 * out-of-line buffer that it is stored on.
 */
STATIC int
xfs_attr_rmtval_remove(xfs_da_args_t *args)
{
	xfs_trans_t *trans;
	xfs_mount_t *mp;
	xfs_bmbt_irec_t map;
	xfs_fsblock_t firstblock;
	xfs_bmap_free_t flist;
	buf_t *bp;
	daddr_t dblkno;
	int blkcnt, nmap, error, done, committed, j;
	int bbsperblk, lblkno, valuelen;

	mp = args->dp->i_mount;

	/*
	 * Roll through the "value", invalidating the attribute value's
	 * blocks. Start/commit transactions are required in order to keep
	 * the number of blocks logged in a single transaction small.
	 */
	bbsperblk = XFS_FSB_TO_BB(mp, 1);
	lblkno = args->aleaf_rmtblkno;
	valuelen = args->valuelen;
	while (valuelen > 0) {
		/*
		 * Try to remember where we decided to put the value.
		 */
		XFS_BMAP_INIT(&flist, &firstblock);
		nmap = 1;
		error = xfs_bmapi(NULL, args->dp, lblkno,
					XFS_B_TO_FSB(mp, valuelen),
					XFS_BMAPI_ATTRFORK | XFS_BMAPI_METADATA,
					&firstblock, 0, &map, &nmap, 0);
		if (error)
			goto out3;
		ASSERT(nmap == 1);
		ASSERT((map.br_startblock != DELAYSTARTBLOCK) &&
		       (map.br_startblock != HOLESTARTBLOCK));

		/*
		 * Loop over all filesystem blocks dedicated to the
		 * "value".  They must all be logged separately as they
		 * were created that way.
		 */
		dblkno = XFS_FSB_TO_DADDR(mp, map.br_startblock),
		blkcnt = XFS_FSB_TO_BB(mp, map.br_blockcount);
		while (blkcnt > 0){
			/*
			 * Start a new transaction.
			 */
			trans = xfs_trans_alloc(mp, XFS_TRANS_ATTR_SET);
			if (error = xfs_trans_reserve(trans,
				      ATTR_RMTVALUE_TRANSBLKS,
				      XFS_FSB_TO_B(mp, ATTR_RMTVALUE_TRANSBLKS),
				      0, XFS_TRANS_PERM_LOG_RES,
				      XFS_SETATTR_LOG_COUNT)) {
				goto out2;
			}

			/*
			 * Invalidate the value into the buffers, one at a
			 * time.  We do this so that VOP_INACTIVE() will work.
			 */
			for (j = 0; (j < ATTR_RMTVALUE_TRANSBLKS) &&
				    (blkcnt > 0); j++){
				bp = xfs_trans_get_buf(trans, mp->m_dev, dblkno,
							      bbsperblk, 0);
				ASSERT(bp);
				ASSERT(!geterror(bp));

				xfs_trans_binval(trans, bp);
				valuelen -= bp->b_bufsize;
				dblkno += bbsperblk;
				blkcnt -= bbsperblk;
			}

			/*
			 * Commit the current transaction.
			 */
			xfs_trans_commit(trans, XFS_TRANS_RELEASE_LOG_RES);
		}
		lblkno += map.br_blockcount;
	}

	/*
	 * Keep de-allocating extents until the remote-value region is gone.
	 */
	lblkno = args->aleaf_rmtblkno;
	blkcnt = XFS_B_TO_FSB(mp, args->valuelen);
	done = 0;
	while (!done) {
		trans = xfs_trans_alloc(mp, XFS_TRANS_ATTR_RM);
		if (error = xfs_trans_reserve(trans, 16,
					      XFS_AINVAL_LOG_RES(mp),
					      0, XFS_TRANS_PERM_LOG_RES,
					      XFS_RMATTR_LOG_COUNT)) {
			goto out2;
		}
		xfs_trans_ijoin(trans, args->dp, XFS_ILOCK_EXCL);
		xfs_trans_ihold(trans, args->dp);

		XFS_BMAP_INIT(&flist, &firstblock);
		error = xfs_bunmapi(trans, args->dp, lblkno, blkcnt,
				    XFS_BMAPI_ATTRFORK | XFS_BMAPI_METADATA,
				    1, &firstblock, &flist, &done);
		if (error)
			goto out1;

		error = xfs_bmap_finish(&trans, &flist, firstblock, &committed);
		if (error)
			goto out1;
		xfs_trans_commit(trans, XFS_TRANS_RELEASE_LOG_RES);
	}
	return(0);

out1:
	xfs_bmap_cancel(&flist);
out2:
	xfs_trans_cancel(trans, XFS_TRANS_RELEASE_LOG_RES);
out3:
	return(error);
}
