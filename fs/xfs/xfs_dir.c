#ident "$Revision$"

#ifdef SIM
#define _KERNEL 1
#endif /* SIM */
#include <sys/param.h>
#include <sys/buf.h>
#ifdef SIM
#undef _KERNEL
#endif /* SIM */
#include <sys/errno.h>
#include <sys/vnode.h>
#include <sys/kmem.h>
#include <sys/uio.h>
#include <sys/debug.h>
#include <sys/proc.h>
#ifdef SIM
#define _KERNEL 1
#endif /* SIM */
#include <sys/dirent.h>
#include <sys/user.h>
#include <sys/grio.h>
#include <sys/ktrace.h>
#include <sys/sysinfo.h>
#include <sys/ksa.h>
#include <sys/fcntl.h>
#ifdef SIM
#undef _KERNEL
#include <bstring.h>
#include <stdio.h>
#else
#include <sys/systm.h>
#endif
#include <string.h>
#include "xfs_macros.h"
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
#include "xfs_attr_sf.h"
#include "xfs_dir_sf.h"
#include "xfs_dinode.h"
#include "xfs_inode.h"
#include "xfs_bmap.h"
#include "xfs_da_btree.h"
#include "xfs_dir.h"
#include "xfs_dir_leaf.h"
#include "xfs_error.h"
#ifdef SIM
#include "sim.h"
#endif

/*
 * xfs_dir.c
 *
 * Provide the external interfaces to manage directories.
 */

/*========================================================================
 * Function prototypes for the kernel.
 *========================================================================*/

/*
 * Internal routines when dirsize == XFS_LBSIZE(mp).
 */
STATIC int xfs_dir_leaf_lookup(xfs_trans_t *trans, xfs_da_args_t *args);
#ifndef SIM
STATIC int xfs_dir_leaf_removename(xfs_trans_t *trans, xfs_da_args_t *args,
					       int *number_entries,
					       int *total_namebytes);
STATIC int xfs_dir_leaf_getdents(xfs_trans_t *trans, xfs_inode_t *dp,
					     uio_t *uio, int *eofp,
					     dirent_t *dbp,
					     xfs_dir_put_t *putp);
STATIC int xfs_dir_leaf_replace(xfs_trans_t *trans, xfs_da_args_t *args);
#endif	/* !SIM */

/*
 * Internal routines when dirsize > XFS_LBSIZE(mp).
 */
STATIC int xfs_dir_node_addname(xfs_trans_t *trans, xfs_da_args_t *args);
STATIC int xfs_dir_node_lookup(xfs_trans_t *trans, xfs_da_args_t *args);
#ifndef SIM
STATIC int xfs_dir_node_removename(xfs_trans_t *trans, xfs_da_args_t *args);
STATIC int xfs_dir_node_getdents(xfs_trans_t *trans, xfs_inode_t *dp,
					     uio_t *uio, int *eofp,
					     dirent_t *dbp,
					     xfs_dir_put_t *putp);
STATIC int xfs_dir_node_replace(xfs_trans_t *trans, xfs_da_args_t *args);
#endif	/* !SIM */

/*
 * Utility routines.
 */
xfs_dahash_t xfs_dir_hashname(char *name_string, int name_length);
uint xfs_dir_log2_roundup(uint i);
xfs_da_state_t *xfs_da_state_alloc(void);
void xfs_da_state_free(xfs_da_state_t *state);

#if defined(DEBUG) && !defined(SIM)
ktrace_t *xfs_dir_trace_buf;
#endif


/*========================================================================
 * Overall external interface routines.
 *========================================================================*/

xfs_dahash_t	xfs_dir_hash_dot, xfs_dir_hash_dotdot;

/*
 * One-time startup routine called from xfs_init().
 */
void
xfs_dir_startup(void)
{
	xfs_dir_hash_dot = xfs_da_hashname(".", 1);
	xfs_dir_hash_dotdot = xfs_da_hashname("..", 2);
}

/*
 * Initialize directory-related fields in the mount structure.
 */
void
xfs_dir_mount(xfs_mount_t *mp)
{
	uint shortcount, leafcount, count;

	shortcount = (mp->m_attroffset - sizeof(xfs_dir_sf_hdr_t)) /
		     sizeof(xfs_dir_sf_entry_t);
	leafcount = (XFS_LBSIZE(mp) - sizeof(xfs_dir_leaf_hdr_t)) /
		    (sizeof(xfs_dir_leaf_entry_t) +
		     sizeof(xfs_dir_leaf_name_t));
	count = shortcount > leafcount ? shortcount : leafcount;
	mp->m_dircook_elog = xfs_da_log2_roundup(count);
	ASSERT(mp->m_dircook_elog <= mp->m_sb.sb_blocklog);
	mp->m_da_node_ents =
		(XFS_LBSIZE(mp) - sizeof(xfs_da_node_hdr_t)) /
		sizeof(xfs_da_node_entry_t);
}

/*
 * Return 1 if directory contains only "." and "..".
 */
int
xfs_dir_isempty(xfs_inode_t *dp)
{
	xfs_dir_sf_hdr_t *hdr;

	ASSERT((dp->i_d.di_mode & IFMT) == IFDIR);
	if (dp->i_d.di_size == 0)
		return(1);
	if (dp->i_d.di_size > XFS_IFORK_DSIZE(dp))
		return(0);
	hdr = (xfs_dir_sf_hdr_t *)dp->i_df.if_u1.if_data;
	return(hdr->count == 0);
}

/*
 * Initialize a directory with its "." and ".." entries.
 */
int
xfs_dir_init(xfs_trans_t *trans, xfs_inode_t *dir, xfs_inode_t *parent_dir)
{
	ASSERT((dir->i_d.di_mode & IFMT) == IFDIR);
	return(xfs_dir_shortform_create(trans, dir, parent_dir->i_ino));
}

/*
 * Generic handler routine to add a name to a directory.
 * Transitions directory from shortform to Btree as necessary.
 */
int							/* error */
xfs_dir_createname(xfs_trans_t *trans, xfs_inode_t *dp, char *name,
		   xfs_ino_t inum, xfs_fsblock_t *firstblock,
		   xfs_bmap_free_t *flist, xfs_extlen_t total)
{
	xfs_da_args_t args;
	int retval, newsize, namelen;

	ASSERT((dp->i_d.di_mode & IFMT) == IFDIR);
	namelen = strlen(name);
	if (namelen >= MAXNAMELEN) {
		return(XFS_ERROR(EINVAL));
	}

	XFSSTATS.xs_dir_create++;
	/*
	 * Fill in the arg structure for this request.
	 */
	args.name = name;
	args.namelen = namelen;
	args.hashval = xfs_da_hashname(name, namelen);
	args.inumber = inum;
	args.dp = dp;
	args.firstblock = firstblock;
	args.flist = flist;
	args.total = total;
	args.whichfork = XFS_DATA_FORK;

	/*
	 * Decide on what work routines to call based on the inode size.
	 */
	if (dp->i_d.di_format == XFS_DINODE_FMT_LOCAL) {
		newsize = XFS_DIR_SF_ENTSIZE_BYNAME(args.namelen);
		if ((dp->i_d.di_size + newsize) <= XFS_IFORK_DSIZE(dp)) {
			retval = xfs_dir_shortform_addname(trans, &args);
		} else {
			retval = xfs_dir_shortform_to_leaf(trans, &args);
			if (retval == 0) {
				retval = xfs_dir_leaf_addname(trans, &args);
			}
		}
	} else if (xfs_bmap_one_block(dp, XFS_DATA_FORK)) {
		retval = xfs_dir_leaf_addname(trans, &args);
		if (retval == ENOSPC) {
			retval = xfs_dir_leaf_to_node(trans, &args);
			if (retval == 0) {
				retval = xfs_dir_node_addname(trans, &args);
			}
		}
	} else {
		retval = xfs_dir_node_addname(trans, &args);
	}
	return(retval);
}

#ifndef SIM
/*
 * Generic handler routine to remove a name from a directory.
 * Transitions directory from Btree to shortform as necessary.
 */
int							/* error */
xfs_dir_removename(xfs_trans_t *trans, xfs_inode_t *dp, char *name,
		   xfs_fsblock_t *firstblock, xfs_bmap_free_t *flist,
		   xfs_extlen_t total)
{
	xfs_da_args_t args;
	int count, totallen, newsize, retval, namelen;

	ASSERT((dp->i_d.di_mode & IFMT) == IFDIR);
	namelen = strlen(name);
	if (namelen >= MAXNAMELEN) {
		return(XFS_ERROR(EINVAL));
	}

	XFSSTATS.xs_dir_remove++;
	/*
	 * Fill in the arg structure for this request.
	 */
	args.name = name;
	args.namelen = namelen;
	args.hashval = xfs_da_hashname(name, namelen);
	args.inumber = 0;
	args.dp = dp;
	args.firstblock = firstblock;
	args.flist = flist;
	args.total = total;
	args.whichfork = XFS_DATA_FORK;

	/*
	 * Decide on what work routines to call based on the inode size.
	 */
	if (dp->i_d.di_format == XFS_DINODE_FMT_LOCAL) {
		retval = xfs_dir_shortform_removename(trans, &args);
	} else if (xfs_bmap_one_block(dp, XFS_DATA_FORK)) {
		retval = xfs_dir_leaf_removename(trans, &args,
							&count, &totallen);
		if (retval == 0) {
			newsize = XFS_DIR_SF_ALLFIT(count, totallen);
			if (newsize <= XFS_IFORK_DSIZE(dp)) {
				retval = xfs_dir_leaf_to_shortform(trans,
								   &args);
			}
		}
	} else {
		retval = xfs_dir_node_removename(trans, &args);
	}
	return(retval);
}
#endif	/* !SIM */

int							/* error */
xfs_dir_lookup(xfs_trans_t *trans, xfs_inode_t *dp, char *name, int namelen,
				   xfs_ino_t *inum)
{
	xfs_da_args_t args;
	int retval;

	ASSERT((dp->i_d.di_mode & IFMT) == IFDIR);
	if (namelen >= MAXNAMELEN) {
		return(XFS_ERROR(EINVAL));
	}

	XFSSTATS.xs_dir_lookup++;
	/*
	 * Fill in the arg structure for this request.
	 */
	args.name = name;
	args.namelen = namelen;
	args.hashval = xfs_da_hashname(name, namelen);
	args.inumber = 0;
	args.dp = dp;
	args.firstblock = NULL;
	args.flist = NULL;
	args.total = 0;
	args.whichfork = XFS_DATA_FORK;

	/*
	 * Decide on what work routines to call based on the inode size.
	 */
	if (dp->i_d.di_format == XFS_DINODE_FMT_LOCAL) {
		retval = xfs_dir_shortform_lookup(trans, &args);
	} else if (xfs_bmap_one_block(dp, XFS_DATA_FORK)) {
		retval = xfs_dir_leaf_lookup(trans, &args);
	} else {
		retval = xfs_dir_node_lookup(trans, &args);
	}
	if (retval == EEXIST)
		retval = 0;
	*inum = args.inumber;
	return(retval);
}

#ifndef SIM
/*
 * Implement readdir.
 */
int							/* error */
xfs_dir_getdents(xfs_trans_t *trans, xfs_inode_t *dp, uio_t *uio, int *eofp)
{
	dirent_t *dbp;
	caddr_t lockaddr;
	int locklen = 0, alignment, retval, is32;
	xfs_dir_put_t put, oput;

	XFSSTATS.xs_dir_getdents++;
	ASSERT((dp->i_d.di_mode & IFMT) == IFDIR);

	/*
	 * If our caller has given us a single contiguous memory buffer,
	 * just work directly within that buffer.  If it's in user memory,
	 * lock it down first.
	 */
	is32 = ABI_IS_IRIX5(GETDENTS_ABI(curprocp->p_abi, uio));
	alignment = (is32 ? sizeof(irix5_off_t) : sizeof(off_t)) - 1;
	if ((uio->uio_iovcnt == 1) &&
	    (((__psint_t)uio->uio_iov[0].iov_base & alignment) == 0) &&
	    ((uio->uio_iov[0].iov_len & alignment) == 0)) {
		dbp = NULL;
		lockaddr = uio->uio_iov[0].iov_base;
		locklen = uio->uio_iov[0].iov_len;
		if (uio->uio_segflg == UIO_SYSSPACE) {
			ASSERT(!is32);
			oput = put = xfs_dir_put_dirent64_rest;
		} else
			oput = put = is32 ?
					xfs_dir_put_dirent32_first :
					xfs_dir_put_dirent64_first;
	} else {
		dbp = kmem_alloc(sizeof(*dbp) + MAXNAMELEN, KM_SLEEP);
		put = is32 ?
			xfs_dir_put_dirent32_uio :
			xfs_dir_put_dirent64_uio;
	}

	/*
	 * Decide on what work routines to call based on the inode size.
	 */
	*eofp = 0;
	if (dp->i_d.di_format == XFS_DINODE_FMT_LOCAL) {
		retval = xfs_dir_shortform_getdents(trans, dp, uio, eofp, dbp,
						    &put);
	} else if (xfs_bmap_one_block(dp, XFS_DATA_FORK)) {
		retval = xfs_dir_leaf_getdents(trans, dp, uio, eofp, dbp, &put);
	} else {
		retval = xfs_dir_node_getdents(trans, dp, uio, eofp, dbp, &put);
	}

	if (dbp != NULL)
		kmem_free(dbp, sizeof(*dbp) + MAXNAMELEN);
	else if (locklen && oput != put)
		unuseracc(lockaddr, locklen, B_READ);
	return(retval);
}

int							/* error */
xfs_dir_replace(xfs_trans_t *trans, xfs_inode_t *dp, char *name, int namelen,
				    xfs_ino_t inum)
{
	xfs_da_args_t args;
	int retval;

	ASSERT((dp->i_d.di_mode & IFMT) == IFDIR);
	if (namelen >= MAXNAMELEN) {
		return(XFS_ERROR(EINVAL));
	}

	/*
	 * Fill in the arg structure for this request.
	 */
	args.name = name;
	args.namelen = namelen;
	args.hashval = xfs_da_hashname(name, namelen);
	args.inumber = inum;
	args.dp = dp;
	args.firstblock = NULL;
	args.flist = NULL;
	args.total = 0;
	args.whichfork = XFS_DATA_FORK;

	/*
	 * Decide on what work routines to call based on the inode size.
	 */
	if (dp->i_d.di_format == XFS_DINODE_FMT_LOCAL) {
		retval = xfs_dir_shortform_replace(trans, &args);
	} else if (xfs_bmap_one_block(dp, XFS_DATA_FORK)) {
		retval = xfs_dir_leaf_replace(trans, &args);
	} else {
		retval = xfs_dir_node_replace(trans, &args);
	}

	return(retval);
}
#endif	/* !SIM */


/*========================================================================
 * External routines when dirsize == XFS_LBSIZE(dp->i_mount).
 *========================================================================*/

/*
 * Add a name to the leaf directory structure
 * This is the external routine.
 */
int
xfs_dir_leaf_addname(xfs_trans_t *trans, xfs_da_args_t *args)
{
	int index, retval;
	buf_t *bp;

	retval = xfs_da_read_buf(trans, args->dp, 0, -1, &bp, XFS_DATA_FORK);
	if (retval)
		return(retval);
	ASSERT(bp != NULL);

	retval = xfs_dir_leaf_lookup_int(bp, args, &index);
	if (retval != ENOENT)
		return(retval);

	retval = xfs_dir_leaf_add(trans, bp, args, index);
	return(retval);
}

#ifndef SIM
/*
 * Remove a name from the leaf directory structure
 * This is the external routine.
 */
STATIC int
xfs_dir_leaf_removename(xfs_trans_t *trans, xfs_da_args_t *args,
				    int *count, int *totallen)
{
	xfs_dir_leafblock_t *leaf;
	int index, retval;
	buf_t *bp;

	retval = xfs_da_read_buf(trans, args->dp, 0, -1, &bp, XFS_DATA_FORK);
	if (retval)
		return(retval);
	ASSERT(bp != NULL);
	leaf = (xfs_dir_leafblock_t *)bp->b_un.b_addr;
	ASSERT(leaf->hdr.info.magic == XFS_DIR_LEAF_MAGIC);
	retval = xfs_dir_leaf_lookup_int(bp, args, &index);
	if (retval != EEXIST)
		return(retval);

	(void)xfs_dir_leaf_remove(trans, bp, index);
	*count = leaf->hdr.count;
	*totallen = leaf->hdr.namebytes;
	return(0);
}
#endif	/* !SIM */

/*
 * Look up a name in a leaf directory structure.
 * This is the external routine.
 */
STATIC int
xfs_dir_leaf_lookup(xfs_trans_t *trans, xfs_da_args_t *args)
{
	int index, retval;
	buf_t *bp;

	retval = xfs_da_read_buf(trans, args->dp, 0, -1, &bp, XFS_DATA_FORK);
	if (retval)
		return(retval);
	ASSERT(bp != NULL);
	retval = xfs_dir_leaf_lookup_int(bp, args, &index);
	xfs_trans_brelse(trans, bp);
	return(retval);
}

#ifndef SIM
/*
 * Copy out directory entries for getdents(), for leaf directories.
 */
STATIC int
xfs_dir_leaf_getdents(xfs_trans_t *trans, xfs_inode_t *dp, uio_t *uio,
				  int *eofp, dirent_t *dbp, xfs_dir_put_t *putp)
{
	buf_t *bp;
	int retval, eob;

	retval = xfs_da_read_buf(trans, dp, 0, -1, &bp, XFS_DATA_FORK);
	if (retval)
		return(retval);
	ASSERT(bp != NULL);
	retval = xfs_dir_leaf_getdents_int(bp, dp, 0, uio, &eob, dbp, putp, -1);
	xfs_trans_brelse(trans, bp);
	*eofp = (eob == 0);
	return(retval);
}

/*
 * Look up a name in a leaf directory structure, replace the inode number.
 * This is the external routine.
 */
STATIC int
xfs_dir_leaf_replace(xfs_trans_t *trans, xfs_da_args_t *args)
{
	int index, retval;
	buf_t *bp;
	xfs_ino_t inum;
	xfs_dir_leafblock_t *leaf;
	xfs_dir_leaf_entry_t *entry;
	xfs_dir_leaf_name_t *namest;

	inum = args->inumber;
	retval = xfs_da_read_buf(trans, args->dp, 0, -1, &bp, XFS_DATA_FORK);
	if (retval)
		return(retval);
	ASSERT(bp != NULL);
	retval = xfs_dir_leaf_lookup_int(bp, args, &index);
	if (retval == EEXIST) {
		leaf = (xfs_dir_leafblock_t *)bp->b_un.b_addr;
		entry = &leaf->entries[index];
		namest = XFS_DIR_LEAF_NAMESTRUCT(leaf, entry->nameidx);
		ASSERT(bcmp((char *)&inum, (char *)&namest->inumber,
			sizeof(inum)));
		namest->inumber = *(xfs_dir_ino_t *)&inum;
		xfs_trans_log_buf(trans, bp, 
		    XFS_DA_LOGRANGE(leaf, namest, sizeof(namest->inumber)));
		retval = 0;
	} else
		xfs_trans_brelse(trans, bp);
	return(retval);
}
#endif	/* !SIM */


/*========================================================================
 * External routines when dirsize > XFS_LBSIZE(mp).
 *========================================================================*/

/*
 * Add a name to a Btree-format directory.
 *
 * This will involve walking down the Btree, and may involve splitting
 * leaf nodes and even splitting intermediate nodes up to and including
 * the root node (a special case of an intermediate node).
 */
STATIC int
xfs_dir_node_addname(xfs_trans_t *trans, xfs_da_args_t *args)
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
	if (error)
		retval = error;
	if (retval != ENOENT) {
		xfs_da_state_free(state);
		return(retval);
	}
		
	blk = &state->path.blk[ state->path.active-1 ];
	ASSERT(blk->magic == XFS_DIR_LEAF_MAGIC);
	retval = xfs_dir_leaf_add(state->trans, blk->bp, state->args,
						blk->index);
	if (retval == 0) {
		/*
		 * Addition succeeded, update Btree hashvals.
		 */
		xfs_da_fixhashpath(state, &state->path);
	} else {
		/*
		 * Addition failed, split as many Btree elements as required.
		 */
		retval = xfs_da_split(state);
	}
	xfs_da_state_free(state);

	return(retval);
}

#ifndef SIM
/*
 * Remove a name from a B-tree directory.
 *
 * This will involve walking down the Btree, and may involve joining
 * leaf nodes and even joining intermediate nodes up to and including
 * the root node (a special case of an intermediate node).
 */
STATIC int
xfs_dir_node_removename(xfs_trans_t *trans, xfs_da_args_t *args)
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
		retval = error;
	if (retval != EEXIST) {
		xfs_da_state_free(state);
		return(retval);
	}

	/*
	 * Remove the name and update the hashvals in the tree.
	 */
	blk = &state->path.blk[ state->path.active-1 ];
	ASSERT(blk->magic == XFS_DIR_LEAF_MAGIC);
	retval = xfs_dir_leaf_remove(state->trans, blk->bp, blk->index);
	xfs_da_fixhashpath(state, &state->path);

	/*
	 * Check to see if the tree needs to be collapsed.
	 */
	error = 0;
	if (retval) {
		error = xfs_da_join(state);
	}

	xfs_da_state_free(state);
	if (error)
		return(error);
	return(0);
}
#endif	/* !SIM */

/*
 * Look up a filename in a int directory.
 * Use an internal routine to actually do all the work.
 */
STATIC int
xfs_dir_node_lookup(xfs_trans_t *trans, xfs_da_args_t *args)
{
	xfs_da_state_t *state;
	int retval, error, i;

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
	}

	/* 
	 * If not in a transaction, we have to release all the buffers.
	 */
	for (i = 0; i < state->path.active; i++)
		xfs_trans_brelse(trans, state->path.blk[i].bp);

	xfs_da_state_free(state);
	return(retval);
}

#ifndef SIM
STATIC int
xfs_dir_node_getdents(xfs_trans_t *trans, xfs_inode_t *dp, uio_t *uio,
				  int *eofp, dirent_t *dbp, xfs_dir_put_t *putp)
{
	xfs_da_intnode_t *node;
	xfs_da_node_entry_t *btree;
	xfs_dir_leafblock_t *leaf;
	xfs_dablk_t bno, nextbno;
	xfs_dahash_t cookhash;
	xfs_mount_t *mp;
	int error, eob, i;
	buf_t *bp;
	daddr_t nextda;

	/*
	 * Pick up our context.
	 */
	mp = dp->i_mount;
	bp = NULL;
	bno = XFS_DA_COOKIE_BNO(mp, uio->uio_offset);
	cookhash = XFS_DA_COOKIE_HASH(mp, uio->uio_offset);

	xfs_dir_trace_g_du("node: start", dp, uio);

	/*
	 * Re-find our place, even if we're confused about what our place is.
	 *
	 * First we check the block number from the magic cookie, it is a
	 * cache of where we ended last time.  If we find a leaf block, and
	 * the starting hashval in that block is less than our desired
	 * hashval, then we run with it.
	 */
	if (bno > 0) {
		error = xfs_da_read_buf(trans, dp, bno, -1, &bp, XFS_DATA_FORK);
		if (error)
			return(error);
		if (bp) {
			leaf = (xfs_dir_leafblock_t *)bp->b_un.b_addr;
			if (leaf->hdr.info.magic != XFS_DIR_LEAF_MAGIC) {
				xfs_dir_trace_g_dub("node: block not a leaf",
							   dp, uio, bno);
				xfs_trans_brelse(trans, bp);
				bp = NULL;
			}
			if (leaf->entries[0].hashval >= cookhash) {
				xfs_dir_trace_g_dub("node: leaf hash too small",
							   dp, uio, bno);
				xfs_trans_brelse(trans, bp);
				bp = NULL;
			}
		}
	}

	/*
	 * If we did not find a leaf block from the blockno in the cookie,
	 * or we there was no blockno in the cookie (eg: first time thru),
	 * the we start at the top of the Btree and re-find our hashval.
	 */
	if (bp == NULL) {
		xfs_dir_trace_g_du("node: start at root" , dp, uio);
		bno = 0;
		for (;;) {
			error = xfs_da_read_buf(trans, dp, bno, -1, &bp,
						       XFS_DATA_FORK);
			if (error)
				return(error);
			ASSERT(bp != NULL);
			node = (xfs_da_intnode_t *)bp->b_un.b_addr;
			if (node->hdr.info.magic != XFS_DA_NODE_MAGIC)
				break;
			btree = &node->btree[0];
			xfs_dir_trace_g_dun("node: node detail", dp, uio, node);
			if (uio->uio_offset == (off_t)0) {
				bno = btree->before;
				xfs_trans_brelse(trans, bp);
				continue;
			}
			for (i = 0; i < node->hdr.count; btree++, i++) {
				if (btree->hashval >= cookhash) {
					bno = btree->before;
					break;
				}
			}
			if (i == node->hdr.count) {
				xfs_trans_brelse(trans, bp);
				xfs_dir_trace_g_du("node: hash beyond EOF",
							  dp, uio);
				uio->uio_offset = XFS_DA_MAKE_COOKIE(mp, 0, 0,
							     XFS_DA_MAXHASH);
				*eofp = 1;
				return(0);
			}
			xfs_dir_trace_g_dub("node: going to block",
						   dp, uio, bno);
			xfs_trans_brelse(trans, bp);
		}
	}
	ASSERT(cookhash != XFS_DA_MAXHASH);

	/*
	 * We've dropped down to the (first) leaf block that contains the
	 * hashval we are interested in.  Continue rolling upward thru the
	 * leaf blocks until we fill up our buffer.
	 */
	for (;;) {
		leaf = (xfs_dir_leafblock_t *)bp->b_un.b_addr;
		if (leaf->hdr.info.magic != XFS_DIR_LEAF_MAGIC) {
			xfs_dir_trace_g_dul("node: not a leaf", dp, uio, leaf);
			xfs_trans_brelse(trans, bp);
			return XFS_ERROR(EDIRCORRUPTED);
		}
		xfs_dir_trace_g_dul("node: leaf detail", dp, uio, leaf);
		if (nextbno = leaf->hdr.info.forw) {
			nextda = xfs_da_reada_buf(trans, dp, nextbno,
						  XFS_DATA_FORK);
		} else
			nextda = -1;
		error = xfs_dir_leaf_getdents_int(bp, dp, bno, uio, &eob, dbp,
						  putp, nextda);
		xfs_trans_brelse(trans, bp);
		bno = nextbno;
		if (eob) {
			xfs_dir_trace_g_dub("node: E-O-B", dp, uio, bno);
			*eofp = 0;
			return(error);
		}
		if (bno == 0)
			break;
		error = xfs_da_read_buf(trans, dp, bno, nextda, &bp,
					XFS_DATA_FORK);
		if (error)
			return(error);
		ASSERT(bp != NULL);
	}
	*eofp = 1;
	xfs_dir_trace_g_du("node: E-O-F", dp, uio);
	return(0);
}

/*
 * Look up a filename in an int directory, replace the inode number.
 * Use an internal routine to actually do the lookup.
 */
STATIC int
xfs_dir_node_replace(xfs_trans_t *trans, xfs_da_args_t *args)
{
	xfs_da_state_t *state;
	xfs_da_state_blk_t *blk;
	xfs_dir_leafblock_t *leaf;
	xfs_dir_leaf_entry_t *entry;
	xfs_dir_leaf_name_t *namest;
	xfs_ino_t inum;
	int retval, error, i;
	buf_t *bp;

	state = xfs_da_state_alloc();
	state->args = args;
	state->mp = args->dp->i_mount;
	state->trans = trans;
	state->blocksize = state->mp->m_sb.sb_blocksize;
	inum = args->inumber;

	/*
	 * Search to see if name exists,
	 * and get back a pointer to it.
	 */
	error = xfs_da_node_lookup_int(state, &retval);
	if (error) {
		retval = error;
	}

	if (retval == EEXIST) {
		blk = &state->path.blk[state->path.active - 1];
		ASSERT(blk->magic == XFS_DIR_LEAF_MAGIC);
		bp = blk->bp;
		leaf = (xfs_dir_leafblock_t *)bp->b_un.b_addr;
		entry = &leaf->entries[blk->index];
		namest = XFS_DIR_LEAF_NAMESTRUCT(leaf, entry->nameidx);
		ASSERT(bcmp((char *)&inum, (char *)&namest->inumber,
			sizeof(inum)));
		namest->inumber = *(xfs_dir_ino_t *)&inum;
		xfs_trans_log_buf(trans, bp,
		    XFS_DA_LOGRANGE(leaf, namest, sizeof(namest->inumber)));
		retval = 0;
	} else {
		i = state->path.active - 1;
		xfs_trans_brelse(trans, state->path.blk[i].bp);
	}
	for (i = 0; i < state->path.active - 1; i++)
		xfs_trans_brelse(trans, state->path.blk[i].bp);

	xfs_da_state_free(state);
	return(retval);
}

#if defined(DEBUG)
/*
 * Add a trace buffer entry for an inode and a uio.
 */
void
xfs_dir_trace_g_du(char *where, xfs_inode_t *dp, uio_t *uio)
{
	xfs_dir_trace_enter(XFS_DIR_KTRACE_G_DU, where,
		     (__psunsigned_t)dp, (__psunsigned_t)dp->i_mount,
		     (__psunsigned_t)(uio->uio_offset >> 32),
		     (__psunsigned_t)(uio->uio_offset & 0xFFFFFFFF),
		     (__psunsigned_t)uio->uio_resid,
		     NULL, NULL, NULL, NULL, NULL, NULL, NULL);
}

/*
 * Add a trace buffer entry for an inode and a uio.
 */
void
xfs_dir_trace_g_dub(char *where, xfs_inode_t *dp, uio_t *uio, xfs_dablk_t bno)
{
	xfs_dir_trace_enter(XFS_DIR_KTRACE_G_DUB, where,
		     (__psunsigned_t)dp, (__psunsigned_t)dp->i_mount,
		     (__psunsigned_t)(uio->uio_offset >> 32),
		     (__psunsigned_t)(uio->uio_offset & 0xFFFFFFFF),
		     (__psunsigned_t)uio->uio_resid,
		     (__psunsigned_t)bno,
		     NULL, NULL, NULL, NULL, NULL, NULL);
}

/*
 * Add a trace buffer entry for an inode and a uio.
 */
void
xfs_dir_trace_g_dun(char *where, xfs_inode_t *dp, uio_t *uio,
			xfs_da_intnode_t *node)
{
	xfs_dir_trace_enter(XFS_DIR_KTRACE_G_DUN, where,
		     (__psunsigned_t)dp, (__psunsigned_t)dp->i_mount,
		     (__psunsigned_t)(uio->uio_offset >> 32),
		     (__psunsigned_t)(uio->uio_offset & 0xFFFFFFFF),
		     (__psunsigned_t)uio->uio_resid,
		     (__psunsigned_t)node->hdr.info.forw,
		     (__psunsigned_t)node->hdr.count,
		     (__psunsigned_t)node->btree[0].hashval,
		     (__psunsigned_t)node->btree[node->hdr.count-1].hashval,
		     NULL, NULL, NULL);
}

/*
 * Add a trace buffer entry for an inode and a uio.
 */
void
xfs_dir_trace_g_dul(char *where, xfs_inode_t *dp, uio_t *uio,
			xfs_dir_leafblock_t *leaf)
{
	xfs_dir_trace_enter(XFS_DIR_KTRACE_G_DUL, where,
		     (__psunsigned_t)dp, (__psunsigned_t)dp->i_mount,
		     (__psunsigned_t)(uio->uio_offset >> 32),
		     (__psunsigned_t)(uio->uio_offset & 0xFFFFFFFF),
		     (__psunsigned_t)uio->uio_resid,
		     (__psunsigned_t)leaf->hdr.info.forw,
		     (__psunsigned_t)leaf->hdr.count,
		     (__psunsigned_t)leaf->entries[0].hashval,
		     (__psunsigned_t)leaf->entries[ leaf->hdr.count-1 ].hashval,
		     NULL, NULL, NULL);
}

/*
 * Add a trace buffer entry for an inode and a uio.
 */
void
xfs_dir_trace_g_due(char *where, xfs_inode_t *dp, uio_t *uio,
			xfs_dir_leaf_entry_t *entry)
{
	xfs_dir_trace_enter(XFS_DIR_KTRACE_G_DUE, where,
		     (__psunsigned_t)dp, (__psunsigned_t)dp->i_mount,
		     (__psunsigned_t)(uio->uio_offset >> 32),
		     (__psunsigned_t)(uio->uio_offset & 0xFFFFFFFF),
		     (__psunsigned_t)uio->uio_resid,
		     (__psunsigned_t)entry->hashval,
		     NULL, NULL, NULL, NULL, NULL, NULL);
}

/*
 * Add a trace buffer entry for an inode and a uio.
 */
void
xfs_dir_trace_g_duc(char *where, xfs_inode_t *dp, uio_t *uio, off_t cookie)
{
	xfs_dir_trace_enter(XFS_DIR_KTRACE_G_DUC, where,
		     (__psunsigned_t)dp, (__psunsigned_t)dp->i_mount,
		     (__psunsigned_t)(uio->uio_offset >> 32),
		     (__psunsigned_t)(uio->uio_offset & 0xFFFFFFFF),
		     (__psunsigned_t)uio->uio_resid,
		     (__psunsigned_t)(cookie >> 32),
		     (__psunsigned_t)(cookie & 0xFFFFFFFF),
		     NULL, NULL, NULL, NULL, NULL);
}

/*
 * Add a trace buffer entry for the arguments given to the routine,
 * generic form.
 */
void
xfs_dir_trace_enter(int type, char *where,
			__psunsigned_t a0, __psunsigned_t a1,
			__psunsigned_t a2, __psunsigned_t a3,
			__psunsigned_t a4, __psunsigned_t a5,
			__psunsigned_t a6, __psunsigned_t a7,
			__psunsigned_t a8, __psunsigned_t a9,
			__psunsigned_t a10, __psunsigned_t a11)
{
	ASSERT(xfs_dir_trace_buf);
	ktrace_enter(xfs_dir_trace_buf, (void *)((__psunsigned_t)type),
					(void *)where,
					(void *)a0, (void *)a1, (void *)a2,
					(void *)a3, (void *)a4, (void *)a5,
					(void *)a6, (void *)a7, (void *)a8,
					(void *)a9, (void *)a10, (void *)a11,
					NULL, NULL);
}
#endif	/* DEBUG */
#endif	/* !SIM */
