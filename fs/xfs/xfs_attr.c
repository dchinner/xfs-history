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
#define MAX_EXT_NEEDED 9	/* GROT: what is this for? */

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
STATIC int xfs_attr_leaf_get(xfs_da_args_t *args);
STATIC int xfs_attr_leaf_removename(xfs_da_args_t *args);
STATIC int xfs_attr_leaf_list(xfs_inode_t *dp, attrlist_t *alist, int flags,
					  attrlist_cursor_kern_t *cursor);

/*
 * Internal routines when attribute list is more than one block.
 */
STATIC int xfs_attr_node_addname(xfs_da_args_t *args);
STATIC int xfs_attr_node_get(xfs_da_args_t *args);
STATIC int xfs_attr_node_removename(xfs_da_args_t *args);
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
	 * Do we answer them, or ignore them?
	 */
	xfs_ilock(args.dp, XFS_ILOCK_SHARED);
	if (error = xfs_iaccess(XFS_VTOI(vp), IREAD, cred)) {
		xfs_iunlock(args.dp, XFS_ILOCK_SHARED);
                return(XFS_ERROR(error));
	}

	/*
	 * Decide on what work routines to call based on the inode size.
	 */
	if (XFS_IFORK_Q(args.dp) == 0) {
		error = ENOATTR;
	} else if (args.dp->i_d.di_aformat == XFS_DINODE_FMT_LOCAL) {
		error = xfs_attr_shortform_getvalue(&args);
	} else if (xfs_bmap_one_block(args.dp, XFS_ATTR_FORK)) {
		error = xfs_attr_leaf_get(&args);
	} else {
		error = xfs_attr_node_get(&args);
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
	dp = XFS_VTOI(vp);

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
	args.dp = dp;
	args.firstblock = &firstblock;
	args.flist = &flist;
	args.total = MAX_EXT_NEEDED;
	args.whichfork = XFS_ATTR_FORK;

	/*
	 * Do we answer them, or ignore them?
	 */
	xfs_ilock(dp, XFS_ILOCK_SHARED);
	if (error = xfs_iaccess(dp, IWRITE, cred)) {
		xfs_iunlock(dp, XFS_ILOCK_SHARED);
                return(XFS_ERROR(error));
	}
	xfs_iunlock(dp, XFS_ILOCK_SHARED);

	/*
	 * If the inode doesn't have an attribute fork, add one.
	 * (inode must not be locked when we call this routine)
	 */
	if (XFS_IFORK_Q(dp) == 0) {
		error = xfs_bmap_add_attrfork(dp);
		if (error)
			return(error);
	}

	/*
	 * Decide on what work routines to call.
	 */
	xfs_ilock(dp, XFS_ILOCK_EXCL);
	if ((dp->i_afp->if_bytes == 0) ||
	    (dp->i_d.di_aformat == XFS_DINODE_FMT_LOCAL)) {
		/*
		 * We're very small at this point.
		 */
		trans = xfs_trans_alloc(dp->i_mount, XFS_TRANS_ATTR_SET);
		if (error = xfs_trans_reserve(trans, 10,
					     XFS_SETATTR_LOG_RES(dp->i_mount),
					     0, XFS_TRANS_PERM_LOG_RES,
					     XFS_SETATTR_LOG_COUNT)) {
			xfs_trans_cancel(trans, XFS_TRANS_RELEASE_LOG_RES);
			xfs_iunlock(dp, XFS_ILOCK_SHARED);
			return(error);
		}
		xfs_trans_ijoin(trans, dp, XFS_ILOCK_EXCL);
		xfs_trans_ihold(trans, dp);
		XFS_BMAP_INIT(&flist, &firstblock);

		/* 
		 * Build initial attribute list (if required).
		 */
		if (dp->i_afp->if_bytes == 0)
			(void)xfs_attr_shortform_create(trans, dp);

		/*
		 * Try to add the attr to the attribute list in the inode.
		 */
		retval = xfs_attr_shortform_addname(trans, &args);
		if (retval == ENOSPC) {
			/*
			 * If it won't fit, transform to a leaf block.
			 */
			error = xfs_attr_shortform_to_leaf(trans, &args);
/* GROT: another possible req'mt for a double-split btree operation */
			if (error == 0) {
				error = xfs_bmap_finish(&trans, &flist,
							firstblock, &committed);
			}
			if (error) {
				xfs_bmap_cancel(&flist);
				xfs_trans_cancel(trans,
						 XFS_TRANS_RELEASE_LOG_RES);
				xfs_iunlock(dp, XFS_ILOCK_EXCL);
				return(error);
			}
		}

		/*
		 * Commit either the shortform add or the leaf transformation.
		 */
		xfs_trans_commit(trans, XFS_TRANS_RELEASE_LOG_RES);

		/*
		 * If we did the leaf transformation, shove the whole problem
		 * of adding the attr to the leaf off on leaf_addname().
		 */
		if (retval == ENOSPC) {
			retval = xfs_attr_leaf_addname(&args);
		}

	} else if (xfs_bmap_one_block(dp, XFS_ATTR_FORK)) {
		retval = xfs_attr_leaf_addname(&args);
	} else {
		retval = xfs_attr_node_addname(&args);
	}
	xfs_iunlock(dp, XFS_ILOCK_EXCL);

	/*
	 * Hit the inode change time.
	 */
	if ((retval == 0) && ((flags & ATTR_KERNOTIME) == 0)) {
		xfs_ichgtime(dp, XFS_ICHGTIME_CHG);
	}

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
	int error;

	xfsda_t_reinit("attr_remove", __FILE__, __LINE__);
	XFSSTATS.xs_attr_remove++;
	dp = XFS_VTOI(vp);

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
	 * Do we answer them, or ignore them?
	 */
	xfs_ilock(dp, XFS_ILOCK_EXCL);
	if (error = xfs_iaccess(dp, IWRITE, cred)) {
		xfs_iunlock(dp, XFS_ILOCK_EXCL);	
                return(XFS_ERROR(error));
	}

	/*
	 * Decide on what work routines to call based on the inode size.
	 */
	if (XFS_IFORK_Q(dp) == 0) {
		error = ENOATTR;
	} else if (dp->i_d.di_aformat == XFS_DINODE_FMT_LOCAL) {
		/*
		 * Set up the transaction envelope for the removal.
		 */
		ASSERT(dp->i_afp->if_flags & XFS_IFINLINE);
		trans = xfs_trans_alloc(dp->i_mount, XFS_TRANS_ATTR_RM);
		if (error = xfs_trans_reserve(trans, 0,
/* GROT: should be smaller */		 XFS_RMATTR_LOG_RES(dp->i_mount),
					 0, XFS_TRANS_PERM_LOG_RES,
					 XFS_RMATTR_LOG_COUNT)) {
			xfs_trans_cancel(trans, XFS_TRANS_RELEASE_LOG_RES);
			xfs_iunlock(dp, XFS_ILOCK_EXCL);	
			return(error);
		}
		xfs_trans_ijoin(trans, dp, XFS_ILOCK_EXCL);
		xfs_trans_ihold(trans, dp);

		error = xfs_attr_shortform_remove(trans, &args);
		if (error) {
			xfs_trans_cancel(trans, XFS_TRANS_RELEASE_LOG_RES);
		} else {
			xfs_trans_commit(trans, XFS_TRANS_RELEASE_LOG_RES);
		}
	} else if (xfs_bmap_one_block(dp, XFS_ATTR_FORK)) {
		error = xfs_attr_leaf_removename(&args);
	} else {
		error = xfs_attr_node_removename(&args);
	}
	xfs_iunlock(dp, XFS_ILOCK_EXCL);

	/*
	 * Hit the inode change time.
	 */
	if ((error == 0) && ((flags & ATTR_KERNOTIME) == 0)) {
		xfs_ichgtime(dp, XFS_ICHGTIME_CHG);
	}

	return(error);
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
	 * Do we answer them, or ignore them?
	 */
	xfs_ilock(dp, XFS_ILOCK_SHARED);
	if (error = xfs_iaccess(dp, IREAD, cred)) {
                return(XFS_ERROR(error));
	}

	/*
	 * Decide on what work routines to call based on the inode size.
	 */
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

int								/* error */
xfs_attr_inactive(xfs_inode_t *dp)
{
	int error;

	xfsda_t_reinit("attr_inactive", __FILE__, __LINE__);

	/*
	 * Decide on what work routines to call based on the inode size.
	 */
	if ((XFS_IFORK_Q(dp) == 0) ||
	    (dp->i_d.di_aformat == XFS_DINODE_FMT_LOCAL)) {
		error = 0;
	} else {
		error = xfs_attr_root_inactive(dp);
	}

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
	if ((args->flags & ATTR_REPLACE) && (retval == ENOATTR)) {
		return(ENOATTR);
	} else if (retval == EEXIST) {
		if (args->flags & ATTR_CREATE)
			return(EEXIST);
		retval = xfs_attr_shortform_remove(trans, args);
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
 *
 * This leaf block cannot have a "remote" value, we only call this routine
 * if bmap_one_block() says there is only one block (ie: no remote blks).
 */
int
xfs_attr_leaf_addname(xfs_da_args_t *args)
{
	xfs_trans_t *trans;
	xfs_inode_t *dp;
	int retval, error, committed;
	buf_t *bp;

	/*
	 * Set up the transaction envelope for the basic operation,
	 * more complicated things commit this one and use their own.
	 */
	dp = args->dp;
	trans = xfs_trans_alloc(dp->i_mount, XFS_TRANS_ATTR_SET);
	if (error = xfs_trans_reserve(trans, 10,
					     XFS_SETATTR_LOG_RES(dp->i_mount),
					     0, XFS_TRANS_PERM_LOG_RES,
					     XFS_SETATTR_LOG_COUNT)) {
		xfs_trans_cancel(trans, XFS_TRANS_RELEASE_LOG_RES);
		return(error);
	}
	XFS_BMAP_INIT(args->flist, args->firstblock);

	/*
	 * Read the (only) block in the attribute list in.
	 */
	args->blkno = 0;
	error = xfs_da_read_buf(trans, dp, args->blkno, -1, &bp, XFS_ATTR_FORK);
	if (error)
		goto out;
	ASSERT(bp != NULL);

	/*
	 * Look up the given attribute in the leaf block.  Figure out if
	 * the given flags produce an error or call for an atomic rename.
	 */
	retval = xfs_attr_leaf_lookup_int(bp, args);
	if ((args->flags & ATTR_REPLACE) && (retval == ENOATTR)) {
		error = XFS_ERROR(ENOATTR);
		goto out;
	} else if (retval == EEXIST) {
		if (args->flags & ATTR_CREATE) {	/* pure create op */
			error = XFS_ERROR(EEXIST);
			goto out;
		}
		args->rename = 1;			/* an atomic rename */
		args->blkno2 = args->blkno;		/* set 2nd entry info */
		args->index2 = args->index;
		args->rmtblkno2 = args->rmtblkno;
		args->rmtblkcnt2 = args->rmtblkcnt;
	}

	/*
	 * Add the attribute to the leaf block, transitioning to a Btree
	 * if required.
	 */
	retval = xfs_attr_leaf_add(trans, bp, args);
	if (retval == ENOSPC) {
		xfs_trans_ijoin(trans, dp, XFS_ILOCK_EXCL);
		xfs_trans_ihold(trans, dp);

		/*
		 * Promote the attribute list to the Btree format, then
		 * Commit that transaction so that the node_addname() call
		 * can manage its own transactions.
		 */
		error = xfs_attr_leaf_to_node(trans, args);
		if (!error) {
			error = xfs_bmap_finish(&trans, args->flist,
						*args->firstblock, &committed);
		}
		if (error) {
			xfs_bmap_cancel(args->flist);
			xfs_trans_cancel(trans, XFS_TRANS_RELEASE_LOG_RES);
			return(error);
		} else {
			xfs_trans_commit(trans, XFS_TRANS_RELEASE_LOG_RES);
		}

		/*
		 * Fob the whole rest of the problem off on the Btree code.
		 */
		error = xfs_attr_node_addname(args);
		return(error);
	}

	/*
	 * Commit the transaction that added the attr name so that
	 * later routines can manage their own transactions.
	 */
	xfs_trans_commit(trans, XFS_TRANS_RELEASE_LOG_RES);

	/*
	 * If there was an out-of-line value, allocate the blocks we
	 * identified for its storage and copy the value.  This is done
	 * after we create the attribute so that we don't overflow the
	 * maximum size of a transaction and/or hit a deadlock.
	 */
	if (args->rmtblkno > 0) {
		error = xfs_attr_rmtval_set(args);
		if (error)
			return(error);
	}

	/*
	 * If this is an atomic rename operation, we must "flip" the
	 * incomplete flags on the "new" and "old" attribute/value pairs
	 * so that one disappears and one appears atomically.  Then we
	 * must remove the "old" attribute/value pair.
	 */
	if (args->rename) {
		/*
		 * In a separate transaction, set the incomplete flag on the
		 * "old" attr and clear the incomplete flag on the "new" attr.
		 */
		error = xfs_attr_leaf_flipflags(args);
		if (error)
			return(error);

		/*
		 * Dismantle the "old" attribute/value pair by removing
		 * a "remote" value (if it exists).
		 */
		args->index = args->index2;
		args->blkno = args->blkno2;
		args->rmtblkno = args->rmtblkno2;
		args->rmtblkcnt = args->rmtblkcnt2;
		if (args->rmtblkno) {
			error = xfs_attr_rmtval_remove(args);
			if (error)
				return(error);
		}

		/*
		 * Set up the transaction envelope for the remove.
		 */
		trans = xfs_trans_alloc(dp->i_mount, XFS_TRANS_ATTR_SET);
		if (error = xfs_trans_reserve(trans, 10,
					     XFS_SETATTR_LOG_RES(dp->i_mount),
					     0, XFS_TRANS_PERM_LOG_RES,
					     XFS_SETATTR_LOG_COUNT)) {
			xfs_trans_cancel(trans, XFS_TRANS_RELEASE_LOG_RES);
			return(error);
		}
		XFS_BMAP_INIT(args->flist, args->firstblock);

		/*
		 * Read in the block containing the "old" attr, then
		 * remove the "old" attr from that block (neat, huh!)
		 */
		error = xfs_da_read_buf(trans, dp, args->blkno, -1, &bp,
					       XFS_ATTR_FORK);
		if (error)
			goto out;
		ASSERT(bp != NULL);
		(void)xfs_attr_leaf_remove(trans, bp, args);

		/*
		 * If the result is small enough, shrink it all into the inode.
		 */
		if (xfs_attr_shortform_allfit(bp, dp)) {
			xfs_trans_ijoin(trans, dp, XFS_ILOCK_EXCL);
			xfs_trans_ihold(trans, dp);

			error = xfs_attr_leaf_to_shortform(trans, bp, args);
/* GROT: should we commit this trans, what choice do we have? */
		}
		xfs_trans_commit(trans, XFS_TRANS_RELEASE_LOG_RES);

	} else if (args->rmtblkno > 0) {
		/*
		 * Added a "remote" value, just clear the incomplete flag.
		 */
		error = xfs_attr_leaf_clearflag(args);
	}
	return(error);

out:
	xfs_trans_cancel(trans, XFS_TRANS_RELEASE_LOG_RES);
	return(error);
}

/*
 * Remove a name from the leaf attribute list structure
 *
 * This leaf block cannot have a "remote" value, we only call this routine
 * if bmap_one_block() says there is only one block (ie: no remote blks).
 */
STATIC int
xfs_attr_leaf_removename(xfs_da_args_t *args)
{
	xfs_trans_t *trans;
	xfs_inode_t *dp;
	buf_t *bp;
	int error;

	/*
	 * Set up the transaction envelope for the rest of the removal.
	 */
	dp = args->dp;
	trans = xfs_trans_alloc(dp->i_mount, XFS_TRANS_ATTR_RM);
	if (error = xfs_trans_reserve(trans, 16,
					     XFS_RMATTR_LOG_RES(dp->i_mount),
					     0, XFS_TRANS_PERM_LOG_RES,
					     XFS_RMATTR_LOG_COUNT)) {
		xfs_trans_cancel(trans, XFS_TRANS_RELEASE_LOG_RES);
		return(error);
	}

	/*
	 * Remove the attribute.
	 */
	args->blkno = 0;
	error = xfs_da_read_buf(trans, dp, args->blkno, -1, &bp, XFS_ATTR_FORK);
	if (error) {
		xfs_trans_cancel(trans, XFS_TRANS_RELEASE_LOG_RES);
		return(error);
	}

	ASSERT(bp != NULL);
	error = xfs_attr_leaf_lookup_int(bp, args);
	if (error == ENOATTR) {
		xfs_trans_cancel(trans, XFS_TRANS_RELEASE_LOG_RES);
		return(XFS_ERROR(ENOATTR));
	}

	(void)xfs_attr_leaf_remove(trans, bp, args);

	/*
	 * If the result is small enough, shrink it all into the inode.
	 */
	if (xfs_attr_shortform_allfit(bp, dp)) {
		xfs_trans_ijoin(trans, dp, XFS_ILOCK_EXCL);
		xfs_trans_ihold(trans, dp);
		XFS_BMAP_INIT(args->flist, args->firstblock);

		error = xfs_attr_leaf_to_shortform(trans, bp, args);
/* GROT: should we commit this trans, what choice do we have? */
	}

	/*
	 * Commit the transaction.
	 */
	xfs_trans_commit(trans, XFS_TRANS_RELEASE_LOG_RES);
	return(0);
}

/*
 * Look up a name in a leaf attribute list structure.
 *
 * This leaf block cannot have a "remote" value, we only call this routine
 * if bmap_one_block() says there is only one block (ie: no remote blks).
 */
STATIC int
xfs_attr_leaf_get(xfs_da_args_t *args)
{
	buf_t *bp;
	int error;

	args->blkno = 0;
	error = xfs_da_read_buf(NULL, args->dp, args->blkno, -1, &bp,
				       XFS_ATTR_FORK);
	if (error)
		return(error);
	ASSERT(bp != NULL);

	error = xfs_attr_leaf_lookup_int(bp, args);
	if (error == EEXIST)  {
		error = xfs_attr_leaf_getvalue(bp, args);
	}
	xfs_trans_brelse(NULL, bp);
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
	error = xfs_da_read_buf(NULL, dp, 0, -1, &bp, XFS_ATTR_FORK);
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
 *
 * "Remote" attribute values confuse the issue and atomic rename operations
 * add a whole extra layer of confusion on top of that.
 */
STATIC int
xfs_attr_node_addname(xfs_da_args_t *args)
{
	xfs_da_state_t *state;
	xfs_da_state_blk_t *blk;
	xfs_trans_t *trans;
	xfs_inode_t *dp;
	xfs_mount_t *mp;
	int retval, error;

	/*
	 * Fill in bucket of arguments/results/context to carry around.
	 */
	dp = args->dp;
	mp = dp->i_mount;
	state = xfs_da_state_alloc();
	state->args = args;
	state->mp = mp;
	state->blocksize = state->mp->m_sb.sb_blocksize;

	/*
	 * Set up the transaction envelope for the remove.
	 */
restart:
	trans = xfs_trans_alloc(mp, XFS_TRANS_ATTR_SET);
	state->trans = trans;
	if (error = xfs_trans_reserve(trans, 10,
				     XFS_SETATTR_LOG_RES(mp),
				     0, XFS_TRANS_PERM_LOG_RES,
				     XFS_SETATTR_LOG_COUNT)) {
		goto out;
	}
	xfs_trans_ijoin(trans, dp, XFS_ILOCK_EXCL);
	xfs_trans_ihold(trans, dp);
	XFS_BMAP_INIT(args->flist, args->firstblock);

	/*
	 * Search to see if name already exists, and get back a pointer
	 * to where it should go.
	 */
	error = xfs_da_node_lookup_int(state, &retval);
	if (error)
		goto out;
	blk = &state->path.blk[ state->path.active-1 ];
	ASSERT(blk->magic == XFS_ATTR_LEAF_MAGIC);
	if ((args->flags & ATTR_REPLACE) && (retval == ENOATTR)) {
		goto out;
	} else if (retval == EEXIST) {
		if (args->flags & ATTR_CREATE)
			goto out;
		args->rename = 1;			/* atomic rename op */
		args->blkno2 = args->blkno;		/* set 2nd entry info */
		args->index2 = args->index;
		args->rmtblkno2 = args->rmtblkno;
		args->rmtblkcnt2 = args->rmtblkcnt;
	}
		
	retval = xfs_attr_leaf_add(state->trans, blk->bp, state->args);
	if (retval == ENOSPC) {
		if (state->path.active == 1) {
			/*
			 * Its really a single leaf node, but it had
			 * out-of-line values so it looked like it *might*
			 * have been a b-tree.
			 */
			retval = xfs_attr_leaf_to_node(trans, args);
			if (retval)
				goto out;
			xfs_trans_commit(trans, XFS_TRANS_RELEASE_LOG_RES);
			goto restart;
		} else {
			/*
			 * Split as many Btree elements as required.
			 * This code tracks the new and old attr's location
			 * in the index/blkno/rmtblkno/rmtblkcnt fields and
			 * in the index2/blkno2/rmtblkno2/rmtblkcnt2 fields.
			 */
			error = xfs_da_split(state);
			if (error)
				goto out;
		}
	} else {
		/*
		 * Addition succeeded, update Btree hashvals.
		 */
		xfs_da_fixhashpath(state, &state->path);
	}
	xfs_trans_commit(trans, XFS_TRANS_RELEASE_LOG_RES);

	/*
	 * If there was an out-of-line value, allocate the blocks we
	 * identified for its storage and copy the value.  This is done
	 * after we create the attribute so that we don't overflow the
	 * maximum size of a transaction and/or hit a deadlock.
	 */
	if (args->rmtblkno > 0) {
		error = xfs_attr_rmtval_set(args);
		if (error)
			return(error);
	}

	/*
	 * If this is an atomic rename operation, we must "flip" the
	 * incomplete flags on the "new" and "old" attribute/value pairs
	 * so that one disappears and one appears atomically.  Then we
	 * must remove the "old" attribute/value pair.
	 */
	if (args->rename) {
		/*
		 * In a separate transaction, set the incomplete flag on the
		 * "old" attr and clear the incomplete flag on the "new" attr.
		 */
		error = xfs_attr_leaf_flipflags(args);
		if (error)
			goto out;

		/*
		 * Dismantle the "old" attribute/value pair by removing
		 * a "remote" value (if it exists).
		 */
		args->index = args->index2;
		args->blkno = args->blkno2;
		args->rmtblkno = args->rmtblkno2;
		args->rmtblkcnt = args->rmtblkcnt2;
		if (args->rmtblkno) {
			error = xfs_attr_rmtval_remove(args);
			if (error)
				return(error);
		}

		/*
		 * Set up the transaction envelope for the remove.
		 */
		trans = xfs_trans_alloc(mp, XFS_TRANS_ATTR_SET);
		state->trans = trans;
		if (error = xfs_trans_reserve(trans, 10,
				     XFS_SETATTR_LOG_RES(mp),
				     0, XFS_TRANS_PERM_LOG_RES,
				     XFS_SETATTR_LOG_COUNT)) {
			goto out;
		}
		xfs_trans_ijoin(trans, dp, XFS_ILOCK_EXCL);
		xfs_trans_ihold(trans, dp);
		XFS_BMAP_INIT(args->flist, args->firstblock);

		/*
		 * Re-find the "old" attribute entry after any split ops.
		 * The INCOMPLETE flag means that we will find the "old"
		 * attr, not the "new" one.
		 */
		args->flags |= XFS_ATTR_INCOMPLETE;
		state->inleaf = 0;
		error = xfs_da_node_lookup_int(state, &retval);
		if (error)
			goto out;

		/*
		 * Remove the name and update the hashvals in the tree.
		 */
		blk = &state->path.blk[ state->path.active-1 ];
		ASSERT(blk->magic == XFS_ATTR_LEAF_MAGIC);
		error = xfs_attr_leaf_remove(state->trans, blk->bp, args);
		xfs_da_fixhashpath(state, &state->path);

		/*
		 * Check to see if the tree needs to be collapsed.
		 */
		if (retval && (state->path.active > 1)) {
			error = xfs_da_join(state);
			if (error)
				goto out;
		}
		xfs_trans_commit(trans, XFS_TRANS_RELEASE_LOG_RES);

	} else if (args->rmtblkno > 0) {
		/*
		 * Added a "remote" value, just clear the incomplete flag.
		 */
		error = xfs_attr_leaf_clearflag(args);
		if (error)
			goto out;
	}
	xfs_da_state_free(state);
	return(0);

out:
	xfs_trans_cancel(trans, XFS_TRANS_RELEASE_LOG_RES);
	xfs_da_state_free(state);
	if (error)
		return(error);
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
xfs_attr_node_removename(xfs_da_args_t *args)
{
	xfs_trans_t *trans;
	xfs_da_state_t *state;
	xfs_da_state_blk_t *blk;
	xfs_inode_t *dp;
	int retval, error, committed;
	buf_t *bp;

	/*
	 * Set up the transaction envelope for the rest of the removal.
	 */
	dp = args->dp;
	trans = xfs_trans_alloc(dp->i_mount, XFS_TRANS_ATTR_RM);
	if (error = xfs_trans_reserve(trans, 16,
					     XFS_RMATTR_LOG_RES(dp->i_mount),
					     0, XFS_TRANS_PERM_LOG_RES,
					     XFS_RMATTR_LOG_COUNT)) {
		xfs_trans_cancel(trans, XFS_TRANS_RELEASE_LOG_RES);
		return(error);
	}
	xfs_trans_ijoin(trans, dp, XFS_ILOCK_EXCL);
	xfs_trans_ihold(trans, dp);
	XFS_BMAP_INIT(args->flist, args->firstblock);

	/*
	 * Tie a string around our finger to remind us where we are.
	 */
	state = xfs_da_state_alloc();
	state->args = args;
	state->mp = dp->i_mount;
	state->trans = trans;
	state->blocksize = state->mp->m_sb.sb_blocksize;

	/*
	 * Search to see if name exists, and get back a pointer to it.
	 */
	error = xfs_da_node_lookup_int(state, &retval);
	if (error) {
		goto out1;
	}
	if (retval != EEXIST) {
		error = retval;
		goto out1;
	}

	/*
	 * If there is an out-of-line value, de-allocate the blocks.
	 * This is done before we remove the attribute so that we don't
	 * overflow the maximum size of a transaction and/or hit a deadlock.
	 */
	blk = &state->path.blk[ state->path.active-1 ];
	ASSERT(blk->bp != NULL);
	ASSERT(blk->magic == XFS_ATTR_LEAF_MAGIC);
	if (args->rmtblkno > 0) {
		/*
		 * Cancel our earlier transaction, can't use it here.
		 */
		xfs_trans_cancel(trans, XFS_TRANS_RELEASE_LOG_RES);

		/*
		 * Mark the attribute as INCOMPLETE, then bunmapi() the
		 * remote value.
		 */
		error = xfs_attr_leaf_setflag(args);
		if (error)
			goto out2;
		error = xfs_attr_rmtval_remove(args);
		if (error)
			goto out2;

		/*
		 * Start a new transaction for the rest of the removal.
		 */
		trans = xfs_trans_alloc(dp->i_mount, XFS_TRANS_ATTR_RM);
		if (error = xfs_trans_reserve(trans, 16,
					     XFS_RMATTR_LOG_RES(dp->i_mount),
					     0, XFS_TRANS_PERM_LOG_RES,
					     XFS_RMATTR_LOG_COUNT)) {
			xfs_trans_cancel(trans, XFS_TRANS_RELEASE_LOG_RES);
			goto out2;
		}
		xfs_trans_ijoin(trans, dp, XFS_ILOCK_EXCL);
		xfs_trans_ihold(trans, dp);
		XFS_BMAP_INIT(args->flist, args->firstblock);

		state->trans = trans;

		/*
		 * Re-find the name, the buffers may have moved around during
		 * the transactions to remove the "remote" value.
		 */
		args->flags |= XFS_ATTR_INCOMPLETE;
		error = xfs_da_node_lookup_int(state, &retval);
		if (error)
			goto out2;
	}

	/*
	 * Remove the name and update the hashvals in the tree.
	 */
	blk = &state->path.blk[ state->path.active-1 ];
	ASSERT(blk->magic == XFS_ATTR_LEAF_MAGIC);
	retval = xfs_attr_leaf_remove(state->trans, blk->bp, args);
	xfs_da_fixhashpath(state, &state->path);

	/*
	 * Check to see if the tree needs to be collapsed.
	 */
	if (retval && (state->path.active > 1)) {
		error = xfs_da_join(state);
		if (error)
			goto out1;
	}

	/*
	 * If the result is small enough, push it all into the inode.
	 */
	if (xfs_bmap_one_block(dp, XFS_ATTR_FORK)) {
		error = xfs_da_read_buf(trans, dp, 0, -1, &bp, XFS_ATTR_FORK);
		if (error)
			goto out1;
		ASSERT(((xfs_attr_leafblock_t *)bp->b_un.b_addr)->hdr.info.magic
		       == XFS_ATTR_LEAF_MAGIC);
		if (xfs_attr_shortform_allfit(bp, dp)) {
			error = xfs_attr_leaf_to_shortform(trans, bp, args);
			if (error)
				goto out1;
		}
	}
	error = xfs_bmap_finish(&trans, args->flist, *args->firstblock,
					&committed);

out1:
	if (error) {
		xfs_bmap_cancel(args->flist);
		xfs_trans_cancel(trans, XFS_TRANS_RELEASE_LOG_RES);
	} else {
		xfs_trans_commit(trans, XFS_TRANS_RELEASE_LOG_RES);
	}

out2:
	xfs_da_state_free(state);
	if (error)
		return(error);
	return(0);
}

/*
 * Look up a filename in a node attribute list.
 *
 * This routine gets called for any attribute fork that has more than one
 * block, ie: both true Btree attr lists and for single-leaf-blocks with
 * "remote" values taking up more blocks.
 */
STATIC int
xfs_attr_node_get(xfs_da_args_t *args)
{
	xfs_da_state_t *state;
	xfs_da_state_blk_t *blk;
	int error, retval;
	int i;

	state = xfs_da_state_alloc();
	state->args = args;
	state->mp = args->dp->i_mount;
	state->blocksize = state->mp->m_sb.sb_blocksize;

	/*
	 * Search to see if name exists, and get back a pointer to it.
	 */
	error = xfs_da_node_lookup_int(state, &retval);
	if (error) {
		retval = error;
	} else if (retval == EEXIST) {
		blk = &state->path.blk[ state->path.active-1 ];
		ASSERT(blk->bp != NULL);
		ASSERT(blk->magic == XFS_ATTR_LEAF_MAGIC);

		/*
		 * Get the value, local or "remote"
		 */
		retval = xfs_attr_leaf_getvalue(blk->bp, args);
		if ((retval == 0) && (args->rmtblkno > 0)) {
			retval = xfs_attr_rmtval_get(args);
		}
	}

	/* 
	 * If not in a transaction, we have to release all the buffers.
	 */
	for (i = 0; i < state->path.active; i++)
		xfs_trans_brelse(NULL, state->path.blk[i].bp);

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
		error = xfs_da_read_buf(NULL, dp, cursor->blkno, -1, &bp,
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

			error = xfs_da_read_buf(NULL, dp, cursor->blkno, -1,
						      &bp, XFS_ATTR_FORK);
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
		error = xfs_da_read_buf(NULL, dp, cursor->blkno, -1, &bp,
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
	daddr_t dblkno;
	caddr_t dst;
	buf_t *bp;
	int nmap, error, tmp, valuelen, lblkno, blkcnt, i;

	mp = args->dp->i_mount;
	dst = args->value;
	valuelen = args->valuelen;
	lblkno = args->rmtblkno;
	while (valuelen > 0) {
		firstblock = NULLFSBLOCK;
		nmap = ATTR_RMTVALUE_MAPSIZE;
		error = xfs_bmapi(NULL, args->dp, lblkno, args->rmtblkcnt,
				  XFS_BMAPI_ATTRFORK | XFS_BMAPI_METADATA,
				  &firstblock, 0, map, &nmap, NULL);
		if (error)
			return(error);
		ASSERT(nmap >= 1);

		for (i = 0; (i < nmap) && (valuelen > 0); i++) {
			ASSERT((map[i].br_startblock != DELAYSTARTBLOCK) &&
			       (map[i].br_startblock != HOLESTARTBLOCK));
			dblkno = XFS_FSB_TO_DADDR(mp, map[i].br_startblock);
			blkcnt = XFS_FSB_TO_BB(mp, map[i].br_blockcount);
			bp = read_buf(mp->m_dev, dblkno, blkcnt, 0);
			if (error = geterror(bp))
				return(error);

			tmp = (valuelen < bp->b_bufsize)
				? valuelen : bp->b_bufsize;
			bcopy(bp->b_un.b_addr, dst, tmp);
			brelse(bp);
			dst += tmp;
			valuelen -= tmp;

			lblkno += map[i].br_blockcount;
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
	xfs_fileoff_t lblkno;
	int blkcnt, valuelen, nmap, error, tmp, committed;

	mp = args->dp->i_mount;
	src = args->value;

	/*
	 * Roll through the "value", allocating blocks on disk as required.
	 */
	lblkno = 0;
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
			xfs_trans_cancel(trans, XFS_TRANS_RELEASE_LOG_RES);
			return(error);
		}
		xfs_trans_ijoin(trans, args->dp, XFS_ILOCK_EXCL);
		xfs_trans_ihold(trans, args->dp);

		/*
		 * Decide where we are going to put the "remote" value.
		 */
		if (lblkno == 0) {
			error = xfs_bmap_first_unused(trans, args->dp, blkcnt,
						     &lblkno, XFS_ATTR_FORK);
			if (error)
				return(error);
			args->rmtblkno = lblkno;
			args->rmtblkcnt = blkcnt;
		}


		/*
		 * Allocate a single extent, up to the size of the value.
		 */
		XFS_BMAP_INIT(&flist, &firstblock);
		nmap = 1;
		error = xfs_bmapi(trans, args->dp, lblkno, blkcnt,
				  XFS_BMAPI_ATTRFORK | XFS_BMAPI_METADATA |
					XFS_BMAPI_WRITE,
				  &firstblock, blkcnt, &map, &nmap, &flist);
		if (error) {
			xfs_bmap_cancel(&flist);
			xfs_trans_cancel(trans, XFS_TRANS_RELEASE_LOG_RES);
			return(error);
		}
		ASSERT(nmap == 1);
		ASSERT((map.br_startblock != DELAYSTARTBLOCK) &&
		       (map.br_startblock != HOLESTARTBLOCK));
		lblkno += map.br_blockcount;
		blkcnt -= map.br_blockcount;

		/*
		 * Commit the current transaction.
		 */
		error = xfs_bmap_finish(&trans, &flist, firstblock, &committed);
		if (error) {
			xfs_bmap_cancel(&flist);
			xfs_trans_cancel(trans, XFS_TRANS_RELEASE_LOG_RES);
			return(error);
		}
		xfs_trans_commit(trans, XFS_TRANS_RELEASE_LOG_RES);
	}

	/*
	 * Roll through the "value", copying the attribute value to the
	 * already-allocated blocks.  Blocks are written synchronously
	 * so that we can know they are all on disk before the turn off
	 * the INCOMPLETE flag.
	 */
	lblkno = args->rmtblkno;
	valuelen = args->valuelen;
	while (valuelen > 0) {
		/*
		 * Try to remember where we decided to put the value.
		 */
		XFS_BMAP_INIT(&flist, &firstblock);
		nmap = 1;
		error = xfs_bmapi(NULL, args->dp, lblkno, args->rmtblkcnt,
					XFS_BMAPI_ATTRFORK | XFS_BMAPI_METADATA,
					&firstblock, 0, &map, &nmap, NULL);
		if (error)
			return(error);
		ASSERT(nmap == 1);
		ASSERT((map.br_startblock != DELAYSTARTBLOCK) &&
		       (map.br_startblock != HOLESTARTBLOCK));

		dblkno = XFS_FSB_TO_DADDR(mp, map.br_startblock),
		blkcnt = XFS_FSB_TO_BB(mp, map.br_blockcount);

		bp = get_buf(mp->m_dev, dblkno, blkcnt, 0);
		ASSERT(bp);
		ASSERT(!geterror(bp));

		tmp = (valuelen < bp->b_bufsize) ? valuelen : bp->b_bufsize;
		bcopy(src, bp->b_un.b_addr, tmp);
		if (tmp < bp->b_bufsize)
			bzero(bp->b_un.b_addr + tmp, bp->b_bufsize - tmp);
		bwrite(bp);		/* NOTE: synchronous write */
		src += tmp;
		valuelen -= tmp;

		lblkno += map.br_blockcount;
	}
	ASSERT(valuelen == 0);
	return(0);
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
	int lblkno, valuelen, blkcnt, nmap, error, done, committed;

	mp = args->dp->i_mount;

	/*
	 * Roll through the "value", invalidating the attribute value's
	 * blocks.
	 */
	lblkno = args->rmtblkno;
	valuelen = args->rmtblkcnt;
	while (valuelen > 0) {
		/*
		 * Try to remember where we decided to put the value.
		 */
		XFS_BMAP_INIT(&flist, &firstblock);
		nmap = 1;
		error = xfs_bmapi(NULL, args->dp, lblkno, args->rmtblkcnt,
					XFS_BMAPI_ATTRFORK | XFS_BMAPI_METADATA,
					&firstblock, 0, &map, &nmap, &flist);
		if (error)
			return(error);
		ASSERT(nmap == 1);
		ASSERT((map.br_startblock != DELAYSTARTBLOCK) &&
		       (map.br_startblock != HOLESTARTBLOCK));

		dblkno = XFS_FSB_TO_DADDR(mp, map.br_startblock),
		blkcnt = XFS_FSB_TO_BB(mp, map.br_blockcount);

		/*
		 * If the "remote" value is in the cache, remove it.
		 */
		bp = incore(mp->m_dev, dblkno, blkcnt, 1);
		if (bp) {
			bp->b_flags |= B_STALE;
			bp->b_flags &= ~B_DELWRI;
			brelse(bp);
			bp = NULL;
		}

		valuelen -= map.br_blockcount;

		lblkno += map.br_blockcount;
	}

	/*
	 * Keep de-allocating extents until the remote-value region is gone.
	 */
	lblkno = args->rmtblkno;
	blkcnt = args->rmtblkcnt;
	done = 0;
	while (!done) {
		trans = xfs_trans_alloc(mp, XFS_TRANS_ATTR_RM);
		if (error = xfs_trans_reserve(trans, 16,
					      XFS_RMATTR_LOG_RES(mp),
					      0, XFS_TRANS_PERM_LOG_RES,
					      XFS_RMATTR_LOG_COUNT)) {
			xfs_trans_cancel(trans, XFS_TRANS_RELEASE_LOG_RES);
			return(error);
		}
		xfs_trans_ijoin(trans, args->dp, XFS_ILOCK_EXCL);
		xfs_trans_ihold(trans, args->dp);

		XFS_BMAP_INIT(&flist, &firstblock);
		error = xfs_bunmapi(trans, args->dp, lblkno, blkcnt,
				    XFS_BMAPI_ATTRFORK | XFS_BMAPI_METADATA,
				    1, &firstblock, &flist, &done);
		if (error) {
			xfs_bmap_cancel(&flist);
			xfs_trans_cancel(trans, XFS_TRANS_RELEASE_LOG_RES);
			return(error);
		}

		error = xfs_bmap_finish(&trans, &flist, firstblock, &committed);
		if (error) {
			xfs_bmap_cancel(&flist);
			xfs_trans_cancel(trans, XFS_TRANS_RELEASE_LOG_RES);
			return(error);
		}
		xfs_trans_commit(trans, XFS_TRANS_RELEASE_LOG_RES);
	}
	return(0);
}
