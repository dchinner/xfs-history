/*
 * xfs_dir_leaf.c
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
#include <sys/dirent.h>
#include <sys/debug.h>
#include <sys/proc.h>
#include <sys/user.h>
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
#include "xfs_attr_sf.h"
#include "xfs_dir_sf.h"
#include "xfs_dinode.h"
#include "xfs_inode_item.h"
#include "xfs_inode.h"
#include "xfs_da_btree.h"
#include "xfs_dir.h"
#include "xfs_dir_leaf.h"
#include "xfs_error.h"
#ifdef SIM
#include "sim.h"
#endif

/*
 * xfs_dir_leaf.c
 *
 * Routines to implement leaf blocks of directories as Btrees of hashed names.
 */

/*========================================================================
 * Function prototypes for the kernel.
 *========================================================================*/

/*
 * Routines used for growing the Btree.
 */
STATIC void xfs_dir_leaf_add_work(xfs_trans_t *trans, buf_t *leaf_buffer,
					      xfs_da_name_t *args,
					      int insertion_index,
					      int freemap_index);
STATIC void xfs_dir_leaf_compact(xfs_trans_t *trans, buf_t *leaf_buffer);
STATIC void xfs_dir_leaf_rebalance(xfs_da_state_t *state,
						  xfs_da_state_blk_t *blk1,
						  xfs_da_state_blk_t *blk2);
STATIC int xfs_dir_leaf_figure_balance(xfs_da_state_t *state,
					  xfs_da_state_blk_t *leaf_blk_1,
					  xfs_da_state_blk_t *leaf_blk_2,
					  int *number_entries_in_blk1,
					  int *number_namebytes_in_blk1);

/*
 * Utility routines.
 */
STATIC void xfs_dir_leaf_moveents(xfs_dir_leafblock_t *src_leaf,
					      int src_start,
					      xfs_dir_leafblock_t *dst_leaf,
					      int dst_start, int move_count,
					      xfs_mount_t *mp);


/*========================================================================
 * External routines when dirsize < XFS_LITINO(mp).
 *========================================================================*/

/*
 * Create the initial contents of a shortform directory.
 */
int
xfs_dir_shortform_create(xfs_trans_t *trans, xfs_inode_t *dp, xfs_ino_t parent)
{
	xfs_dir_sf_hdr_t *hdr;

	ASSERT(dp->i_d.di_size == 0);
	if (dp->i_d.di_format == XFS_DINODE_FMT_EXTENTS) {
		dp->i_flags &= ~XFS_IEXTENTS;	/* just in case */
		dp->i_d.di_format = XFS_DINODE_FMT_LOCAL;
		xfs_trans_log_inode(trans, dp, XFS_ILOG_CORE);
		dp->i_flags |= XFS_IINLINE;
	}
	ASSERT(dp->i_flags & XFS_IINLINE);
	ASSERT(dp->i_bytes == 0);
	xfs_idata_realloc(dp, sizeof(*hdr));
	hdr = (xfs_dir_sf_hdr_t *)dp->i_u1.iu_data;
	bcopy((char *)&parent, hdr->parent, sizeof(xfs_ino_t));
	hdr->count = 0;
	dp->i_d.di_size = sizeof(*hdr);
	xfs_trans_log_inode(trans, dp, XFS_ILOG_CORE | XFS_ILOG_DATA);
	return(0);
}

/*
 * Add a name to the shortform directory structure.
 * Overflow from the inode has already been checked for.
 */
int
xfs_dir_shortform_addname(xfs_trans_t *trans, xfs_da_name_t *args)
{
	xfs_dir_shortform_t *sf;
	xfs_dir_sf_entry_t *sfe;
	int i, offset, size;
	xfs_inode_t *dp;

	dp = args->dp;
	ASSERT(dp->i_flags & XFS_IINLINE);
	sf = (xfs_dir_shortform_t *)dp->i_u1.iu_data;
	sfe = &sf->list[0];
	for (i = sf->hdr.count-1; i >= 0; i--) {
		if (sfe->namelen == args->namelen) {
			if (bcmp(args->name, sfe->name, args->namelen) == 0) {
				return(XFS_ERROR(EEXIST));
			}
		}
		sfe = XFS_DIR_SF_NEXTENTRY(sfe);
	}

	offset = (char *)sfe - (char *)sf;
	size = XFS_DIR_SF_ENTSIZE_BYNAME(args->namelen);
	xfs_idata_realloc(dp, size);
	sf = (xfs_dir_shortform_t *)dp->i_u1.iu_data;
	sfe = (xfs_dir_sf_entry_t *)((char *)sf + offset);

	bcopy((char *)&args->inumber, sfe->inumber, sizeof(xfs_ino_t));
	sfe->namelen = args->namelen;
	bcopy(args->name, sfe->name, sfe->namelen);
	sf->hdr.count++;

	dp->i_d.di_size += size;
	xfs_trans_log_inode(trans, dp, XFS_ILOG_CORE | XFS_ILOG_DATA);

	return(0);
}

#ifndef SIM
/*
 * Remove a name from the shortform directory structure.
 */
int
xfs_dir_shortform_removename(xfs_trans_t *trans, xfs_da_name_t *args)
{
	xfs_dir_shortform_t *sf;
	xfs_dir_sf_entry_t *sfe;
	int base, size, i;
	xfs_inode_t *dp;

	dp = args->dp;
	ASSERT(dp->i_flags & XFS_IINLINE);
	base = sizeof(xfs_dir_sf_hdr_t);
	sf = (xfs_dir_shortform_t *)dp->i_u1.iu_data;
	sfe = &sf->list[0];
	for (i = sf->hdr.count-1; i >= 0; i--) {
		size = XFS_DIR_SF_ENTSIZE_BYENTRY(sfe);
		if (sfe->namelen == args->namelen) {
			if (bcmp(sfe->name, args->name, args->namelen) == 0) {
				break;
			}
		}
		base += size;
		sfe = XFS_DIR_SF_NEXTENTRY(sfe);
	}
	if (i < 0)
		return(XFS_ERROR(ENOENT));

	if ((base + size) != dp->i_d.di_size) {
		bcopy(&((char *)sf)[base+size], &((char *)sf)[base],
					      dp->i_d.di_size - (base+size));
	}
	sf->hdr.count--;

	xfs_idata_realloc(dp, -size);
	dp->i_d.di_size -= size;
	xfs_trans_log_inode(trans, dp, XFS_ILOG_CORE | XFS_ILOG_DATA);

	return(0);
}
#endif	/* !SIM */

/*
 * Look up a name in a shortform directory structure.
 */
/*ARGSUSED*/
int
xfs_dir_shortform_lookup(xfs_trans_t *trans, xfs_da_name_t *args)
{
	xfs_dir_shortform_t *sf;
	xfs_dir_sf_entry_t *sfe;
	int i;
	xfs_inode_t *dp;

	dp = args->dp;
	ASSERT(dp->i_flags & XFS_IINLINE);
	sf = (xfs_dir_shortform_t *)dp->i_u1.iu_data;
	if (args->namelen == 2 &&
	    args->name[0] == '.' && args->name[1] == '.') {
		bcopy(sf->hdr.parent, (char *)&args->inumber, sizeof(xfs_ino_t));
		return(XFS_ERROR(EEXIST));
	}
	if (args->namelen == 1 && args->name[0] == '.') {
		args->inumber = dp->i_ino;
		return(XFS_ERROR(EEXIST));
	}
	sfe = &sf->list[0];
	for (i = sf->hdr.count-1; i >= 0; i--) {
		if (sfe->namelen == args->namelen) {
			if (bcmp(args->name, sfe->name, args->namelen) == 0) {
				bcopy(sfe->inumber, (char *)&args->inumber,
						    sizeof(xfs_ino_t));
				return(XFS_ERROR(EEXIST));
			}
		}
		sfe = XFS_DIR_SF_NEXTENTRY(sfe);
	}
	return(XFS_ERROR(ENOENT));
}

/*
 * Convert from using the shortform to the leaf.
 */
int
xfs_dir_shortform_to_leaf(xfs_trans_t *trans, xfs_da_name_t *iargs)
{
	xfs_inode_t *dp;
	xfs_dir_shortform_t *sf;
	xfs_dir_sf_entry_t *sfe;
	xfs_da_name_t args;
	xfs_ino_t inumber;
	char *tmpbuffer;
	int retval, i, size;
	xfs_fileoff_t blkno;
	buf_t *bp;

	dp = iargs->dp;
	size = dp->i_bytes;
	tmpbuffer = kmem_alloc(size, KM_SLEEP);
	ASSERT(tmpbuffer != NULL);

	bcopy(dp->i_u1.iu_data, tmpbuffer, size);

	sf = (xfs_dir_shortform_t *)tmpbuffer;
	bcopy(sf->hdr.parent, (char *)&inumber, sizeof(xfs_ino_t));

	xfs_idata_realloc(dp, -size);
	dp->i_d.di_size = 0;
	retval = xfs_da_grow_inode(trans, iargs, &blkno);
	if (retval) {
		dp->i_d.di_size = size;
		xfs_idata_realloc(dp, size);
		bcopy(tmpbuffer, dp->i_u1.iu_data, size);
		goto out;
	}

	ASSERT(blkno == 0);
	retval = xfs_dir_leaf_create(trans, dp, blkno, &bp);
	if (retval)
		goto out;

	args.name = ".";
	args.namelen = 1;
	args.hashval = xfs_da_hashname(".", 1);
	args.inumber = dp->i_ino;
	args.dp = dp;
	args.firstblock = iargs->firstblock;
	args.flist = iargs->flist;
	args.total = iargs->total;
	retval = xfs_dir_leaf_addname(trans, &args);
	if (retval)
		goto out;

	args.name = "..";
	args.namelen = 2;
	args.hashval = xfs_da_hashname("..", 2);
	args.inumber = inumber;
	retval = xfs_dir_leaf_addname(trans, &args);
	if (retval)
		goto out;

	sfe = &sf->list[0];
	for (i = 0; i < sf->hdr.count; i++) {
		args.name = (char *)(sfe->name);
		args.namelen = sfe->namelen;
		args.hashval = xfs_da_hashname((char *)(sfe->name),
					       sfe->namelen);
		bcopy(sfe->inumber, (char *)&args.inumber, sizeof(xfs_ino_t));
		retval = xfs_dir_leaf_addname(trans, &args);
		if (retval)
			goto out;
		sfe = XFS_DIR_SF_NEXTENTRY(sfe);
	}
	retval = 0;

out:
	kmem_free(tmpbuffer, size);
	return(retval);
}

#ifndef SIM
/*
 * Print the shortform directory.
 */
/*ARGSUSED*/
void
xfs_dir_shortform_print(xfs_trans_t *trans, xfs_inode_t *dp)
{
	xfs_dir_shortform_t *sf;
	xfs_dir_sf_entry_t *sfe;
	xfs_ino_t ino;
	int i;

	sf = (xfs_dir_shortform_t *)dp->i_u1.iu_data;
	printf("%20lld  .\n", dp->i_ino);
	bcopy(sf->hdr.parent, (char *)&ino, sizeof(ino));
	printf("%20lld  ..\n", ino);

	sfe = &sf->list[0];
	for (i = sf->hdr.count-1; i >= 0; i--) {
		bcopy(sfe->inumber, (char *)&ino, sizeof(ino));
		printf("%20lld  %*.*s\n", ino, sfe->namelen, sfe->namelen, sfe->name);
		sfe = XFS_DIR_SF_NEXTENTRY(sfe);
	}
}

/*
 * Copy out directory entries for getdents(), for shortform directories.
 */
/*ARGSUSED*/
int
xfs_dir_shortform_getdents(xfs_trans_t *trans, xfs_inode_t *dp, uio_t *uio,
				       int *eofp, dirent_t *dbp)
{
	xfs_dir_shortform_t *sf;
	xfs_dir_sf_entry_t *sfe;
	int entry, retval, done, i;
	xfs_mount_t *mp;
	xfs_ino_t ino;
	__uint32_t bno;
	off_t nextcook;

	mp = dp->i_mount;
	sf = (xfs_dir_shortform_t *)dp->i_u1.iu_data;
	if (uio->uio_offset == XFS_DA_MAKE_COOKIE(mp, 0, sf->hdr.count + 2)) {
		*eofp = 1;
		return(0);
	}
	bno = (__uint32_t)XFS_DA_COOKIE_BNO(mp, uio->uio_offset);
	if (bno != 0)
		return(XFS_ERROR(ENOENT));

	entry = XFS_DA_COOKIE_ENTRY(mp, uio->uio_offset);
	if (entry >= sf->hdr.count + 2)
		return(XFS_ERROR(ENOENT));

	/*
	 * Special case fakery for first 2 entries: "." and ".."
	 */
	if (entry == 0) {
		nextcook = XFS_DA_MAKE_COOKIE(mp, 0, 1);
		retval = xfs_dir_put_dirent(mp, dbp, dp->i_ino, ".", 1,
			nextcook, uio, &done);
		if (!done)
			return(retval);
		entry++;
	}
	if (entry == 1) {
		bcopy(sf->hdr.parent, (char *)&ino, sizeof(ino));
		nextcook = XFS_DA_MAKE_COOKIE(mp, 0, 2);
		retval = xfs_dir_put_dirent(mp, dbp, ino, "..", 2, nextcook,
						uio, &done);
		if (!done) {
			uio->uio_offset = XFS_DA_MAKE_COOKIE(mp, bno, entry);
			return(retval);
		}
		entry++;
	}

	/*
	 * Collect the rest of the directory entries.
	 */
	sfe = &sf->list[0];
	for (i = 0; i < entry - 2; i++)
		sfe = XFS_DIR_SF_NEXTENTRY(sfe);
	for (; i < sf->hdr.count; i++, entry++) {
		bcopy(sfe->inumber, (char *)&ino, sizeof(ino));
		nextcook = XFS_DA_MAKE_COOKIE(mp, 0, entry + 1);
		retval = xfs_dir_put_dirent(mp, dbp, ino, (char *)(sfe->name), 
				sfe->namelen, nextcook, uio, &done);
		if (!done) {
			uio->uio_offset = XFS_DA_MAKE_COOKIE(mp, bno, entry);
			return(retval);
		}
		sfe = XFS_DIR_SF_NEXTENTRY(sfe);
	}
	*eofp = 1;
	uio->uio_offset = XFS_DA_MAKE_COOKIE(mp, 0, sf->hdr.count + 2);
	return(0);
}

/*
 * Look up a name in a shortform directory structure, replace the inode number.
 */
int
xfs_dir_shortform_replace(xfs_trans_t *trans, xfs_da_name_t *args)
{
	xfs_dir_shortform_t *sf;
	xfs_dir_sf_entry_t *sfe;
	xfs_inode_t *dp;
	int i;

	dp = args->dp;
	ASSERT(dp->i_flags & XFS_IINLINE);
	sf = (xfs_dir_shortform_t *)dp->i_u1.iu_data;
	if (args->namelen == 2 &&
	    args->name[0] == '.' && args->name[1] == '.') {
		ASSERT(bcmp((char *)&args->inumber, sf->hdr.parent,
			sizeof(xfs_ino_t)));
		bcopy((char *)&args->inumber, sf->hdr.parent,
			sizeof(xfs_ino_t));
		xfs_trans_log_inode(trans, dp, XFS_ILOG_DATA);
		return(0);
	}
	ASSERT(args->namelen != 1 || args->name[0] != '.');
	sfe = &sf->list[0];
	for (i = sf->hdr.count-1; i >= 0; i--) {
		if (sfe->namelen == args->namelen) {
			if (bcmp(args->name, sfe->name, args->namelen) == 0) {
				ASSERT(bcmp((char *)&args->inumber,
					sfe->inumber, sizeof(xfs_ino_t)));
				bcopy((char *)&args->inumber, sfe->inumber,
					sizeof(xfs_ino_t));
				xfs_trans_log_inode(trans, dp, XFS_ILOG_DATA);
				return(0);
			}
		}
		sfe = XFS_DIR_SF_NEXTENTRY(sfe);
	}
	return(XFS_ERROR(ENOENT));
}

/*
 * Convert a leaf directory to shortform structure
 */
int
xfs_dir_leaf_to_shortform(xfs_trans_t *trans, xfs_da_name_t *iargs)
{
	xfs_dir_leafblock_t *leaf;
	xfs_dir_leaf_hdr_t *hdr;
	xfs_dir_leaf_entry_t *entry;
	xfs_dir_leaf_name_t *namest;
	xfs_da_name_t args;
	xfs_inode_t *dp;
	xfs_ino_t parent;
	char *tmpbuffer;
	int retval, i;
	buf_t *bp;

	dp = iargs->dp;
	tmpbuffer = kmem_alloc(XFS_LBSIZE(dp->i_mount), KM_SLEEP);
	ASSERT(tmpbuffer != NULL);

	retval = xfs_da_read_buf(trans, dp, 0, &bp);
	if (retval)
		return(retval);
	ASSERT(bp != NULL);
	bcopy(bp->b_un.b_addr, tmpbuffer, XFS_LBSIZE(dp->i_mount));
	leaf = (xfs_dir_leafblock_t *)tmpbuffer;
	ASSERT(leaf->hdr.info.magic == XFS_DIR_LEAF_MAGIC);
	bzero(bp->b_un.b_addr, XFS_LBSIZE(dp->i_mount));

	/*
	 * Find and special case the parent inode number
	 */
	hdr = &leaf->hdr;
	entry = &leaf->entries[0];
	for (i = hdr->count-1; i >= 0; entry++, i--) {
		namest = XFS_DIR_LEAF_NAMESTRUCT(leaf, entry->nameidx);
		if ((entry->namelen == 2) &&
		    (namest->name[0] == '.') &&
		    (namest->name[1] == '.')) {
			bcopy(namest->inumber, (char *)&parent,
					       sizeof(xfs_ino_t));
			entry->nameidx = 0;
		}
		if ((entry->namelen == 1) && (namest->name[0] == '.')) {
			entry->nameidx = 0;
		}
	}
	retval = xfs_da_shrink_inode(trans, iargs, 0, bp);
	if (retval)
		goto out;
	retval = xfs_dir_shortform_create(trans, dp, parent);
	if (retval)
		goto out;

	/*
	 * Copy the rest of the filenames
	 */
	entry = &leaf->entries[0];
	args.dp = dp;
	args.firstblock = iargs->firstblock;
	args.flist = iargs->flist;
	args.total = iargs->total;
	for (i = 0; i < hdr->count; entry++, i++) {
		if (entry->nameidx == 0)
			continue;
		namest = XFS_DIR_LEAF_NAMESTRUCT(leaf, entry->nameidx);
		args.name = (char *)(namest->name);
		args.namelen = entry->namelen;
		args.hashval = entry->hashval;
		bcopy(namest->inumber, (char *)&args.inumber, sizeof(xfs_ino_t));
		xfs_dir_shortform_addname(trans, &args);
	}

out:
	kmem_free(tmpbuffer, XFS_LBSIZE(dp->i_mount));
	return(retval);
}
#endif	/* !SIM */

/*
 * Convert from using a single leaf to a root node and a leaf.
 */
int
xfs_dir_leaf_to_node(xfs_trans_t *trans, xfs_da_name_t *args)
{
	xfs_dir_leafblock_t *leaf;
	xfs_da_intnode_t *node;
	xfs_inode_t *dp;
	buf_t *bp1, *bp2;
	xfs_fileoff_t blkno;
	int retval;

	dp = args->dp;
	retval = xfs_da_grow_inode(trans, args, &blkno);
	ASSERT(blkno == 1);
	if (retval)
		return(retval);
	retval = xfs_da_read_buf(trans, dp, 0, &bp1);
	if (retval)
		return(retval);
	ASSERT(bp1 != NULL);
	retval = xfs_da_get_buf(trans, dp, 1, &bp2);
	if (retval)
		return(retval);
	ASSERT(bp2 != NULL);
	bcopy(bp1->b_un.b_addr, bp2->b_un.b_addr, XFS_LBSIZE(dp->i_mount));
	xfs_trans_log_buf(trans, bp2, 0, XFS_LBSIZE(dp->i_mount) - 1);

	/*
	 * Set up the new root node.
	 */
	retval = xfs_da_node_create(trans, dp, 0, 1, &bp1);
	if (retval)
		return(retval);
	node = (xfs_da_intnode_t *)bp1->b_un.b_addr;
	leaf = (xfs_dir_leafblock_t *)bp2->b_un.b_addr;
	ASSERT(leaf->hdr.info.magic == XFS_DIR_LEAF_MAGIC);
	node->btree[0].hashval = leaf->entries[ leaf->hdr.count-1 ].hashval;
	node->btree[0].before = blkno;
	node->hdr.count = 1;
	xfs_trans_log_buf(trans, bp1, 0, XFS_LBSIZE(dp->i_mount) - 1);

	return(retval);
}


/*========================================================================
 * Routines used for growing the Btree.
 *========================================================================*/

/*
 * Create the initial contents of a leaf directory
 * or a leaf in a node directory.
 */
int
xfs_dir_leaf_create(xfs_trans_t *trans, xfs_inode_t *dp, xfs_fileoff_t blkno,
				buf_t **bpp)
{
	xfs_dir_leafblock_t *leaf;
	xfs_dir_leaf_hdr_t *hdr;
	buf_t *bp;
	int retval;

	retval = xfs_da_get_buf(trans, dp, blkno, &bp);
	if (retval)
		return(retval);
	ASSERT(bp != NULL);
	leaf = (xfs_dir_leafblock_t *)bp->b_un.b_addr;
	bzero((char *)leaf, XFS_LBSIZE(dp->i_mount));
	hdr = &leaf->hdr;
	hdr->info.magic = XFS_DIR_LEAF_MAGIC;
	hdr->firstused = XFS_LBSIZE(dp->i_mount);
	if (hdr->firstused == 0)
		hdr->firstused = XFS_LBSIZE(dp->i_mount) - 1;
	hdr->freemap[0].base = sizeof(xfs_dir_leaf_hdr_t);
	hdr->freemap[0].size = hdr->firstused - hdr->freemap[0].base;

	xfs_trans_log_buf(trans, bp, 0, XFS_LBSIZE(dp->i_mount) - 1);

	*bpp = bp;
	return(0);
}

/*
 * Split the leaf node, rebalance, then add the new entry.
 */
int
xfs_dir_leaf_split(xfs_da_state_t *state, xfs_da_state_blk_t *oldblk,
				  xfs_da_state_blk_t *newblk)
{
	xfs_fileoff_t blkno;
	int error;

	/*
	 * Allocate space for a new leaf node.
	 */
	ASSERT(oldblk->magic == XFS_DIR_LEAF_MAGIC);
	error = xfs_da_grow_inode(state->trans, state->args, &blkno);
	if (error)
		return(error);
	error = xfs_dir_leaf_create(state->trans, state->args->dp, blkno,
						   &newblk->bp);
	if (error)
		return(error);
	newblk->blkno = blkno;
	newblk->magic = XFS_DIR_LEAF_MAGIC;

	/*
	 * Rebalance the entries across the two leaves.
	 */
	xfs_dir_leaf_rebalance(state, oldblk, newblk);
	error = xfs_da_blk_link(state, oldblk, newblk);
	if (error)
		return(error);

	/*
	 * Insert the new entry in the correct block.
	 */
	if (state->inleaf) {
		error = xfs_dir_leaf_add(state->trans, oldblk->bp, state->args,
						       oldblk->index);
	} else {
		error = xfs_dir_leaf_add(state->trans, newblk->bp, state->args,
						       newblk->index);
	}
	ASSERT(!error);

	/*
	 * Update last hashval in each block since we added the name.
	 */
	oldblk->hashval = xfs_dir_leaf_lasthash(oldblk->bp, NULL);
	newblk->hashval = xfs_dir_leaf_lasthash(newblk->bp, NULL);
	return(0);
}

/*
 * Add a name to the leaf directory structure.
 */
int
xfs_dir_leaf_add(xfs_trans_t *trans, buf_t *bp, xfs_da_name_t *args, int index)
{
	xfs_dir_leafblock_t *leaf;
	xfs_dir_leaf_hdr_t *hdr;
	xfs_dir_leaf_map_t *map;
	int tablesize, i, tmp;

	leaf = (xfs_dir_leafblock_t *)bp->b_un.b_addr;
	ASSERT(leaf->hdr.info.magic == XFS_DIR_LEAF_MAGIC);
	ASSERT((index >= 0) && (index <= leaf->hdr.count));
	hdr = &leaf->hdr;

	/*
	 * Search through freemap for first-fit on new name length.
	 * (may need to figure in size of entry struct too)
	 */
	tablesize = (hdr->count + 1) * sizeof(xfs_dir_leaf_entry_t)
			+ sizeof(xfs_dir_leaf_hdr_t);
	map = &hdr->freemap[XFS_DIR_LEAF_MAPSIZE-1];
	for (i = XFS_DIR_LEAF_MAPSIZE-1; i >= 0; map--, i--) {
		if (map->size == 0)
			continue;	/* no space in this map */
		tmp = XFS_DIR_LEAF_ENTSIZE_BYNAME(args->namelen);
		if (map->base <= hdr->firstused)
			tmp += sizeof(xfs_dir_leaf_entry_t);
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
			tmp += sizeof(xfs_dir_leaf_entry_t);
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
xfs_dir_leaf_add_work(xfs_trans_t *trans, buf_t *bp, xfs_da_name_t *args,
				  int index, int mapindex)
{
	xfs_dir_leafblock_t *leaf;
	xfs_dir_leaf_hdr_t *hdr;
	xfs_dir_leaf_entry_t *entry;
	xfs_dir_leaf_name_t *namest;
	xfs_dir_leaf_map_t *map;
	xfs_mount_t *mp;
	int tmp, i;

	leaf = (xfs_dir_leafblock_t *)bp->b_un.b_addr;
	ASSERT(leaf->hdr.info.magic == XFS_DIR_LEAF_MAGIC);
	hdr = &leaf->hdr;
	ASSERT((mapindex >= 0) && (mapindex < XFS_DIR_LEAF_MAPSIZE));
	ASSERT((index >= 0) && (index <= hdr->count));

	/*
	 * Force open some space in the entry array and fill it in.
	 */
	entry = &leaf->entries[index];
	if (index < hdr->count) {
		tmp  = hdr->count - index;
		tmp *= sizeof(xfs_dir_leaf_entry_t);
		bcopy((char *)entry, (char *)(entry+1), tmp);
		xfs_trans_log_buf(trans, bp,
		    XFS_DA_LOGRANGE(leaf, entry, tmp + sizeof(*entry)));
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
	    XFS_DA_LOGRANGE(leaf, entry, sizeof(*entry)));

	/*
	 * Copy the string and inode number into the new space.
	 */
	namest = XFS_DIR_LEAF_NAMESTRUCT(leaf, entry->nameidx);
	bcopy((char *)&args->inumber, namest->inumber, sizeof(xfs_ino_t));
	bcopy(args->name, namest->name, args->namelen);
	xfs_trans_log_buf(trans, bp,
	    XFS_DA_LOGRANGE(leaf, namest, XFS_DIR_LEAF_ENTSIZE_BYENTRY(entry)));

	/*
	 * Update the control info for this leaf node
	 */
	if (entry->nameidx < hdr->firstused)
		hdr->firstused = entry->nameidx;
	ASSERT(hdr->firstused >= ((hdr->count*sizeof(*entry))+sizeof(*hdr)));
	tmp = (hdr->count-1) * sizeof(xfs_dir_leaf_entry_t)
			+ sizeof(xfs_dir_leaf_hdr_t);
	map = &hdr->freemap[0];
	for (i = 0; i < XFS_DIR_LEAF_MAPSIZE; map++, i++) {
		if (map->base == tmp) {
			map->base += sizeof(xfs_dir_leaf_entry_t);
			map->size -= sizeof(xfs_dir_leaf_entry_t);
		}
	}
	hdr->namebytes += args->namelen;
	xfs_trans_log_buf(trans, bp, XFS_DA_LOGRANGE(leaf, hdr, sizeof(*hdr)));
}

/*
 * Garbage collect a leaf directory block by copying it to a new buffer.
 */
STATIC void
xfs_dir_leaf_compact(xfs_trans_t *trans, buf_t *bp)
{
	xfs_dir_leafblock_t *leaf_s, *leaf_d;
	xfs_dir_leaf_hdr_t *hdr_s, *hdr_d;
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
	leaf_s = (xfs_dir_leafblock_t *)tmpbuffer;
	leaf_d = (xfs_dir_leafblock_t *)bp->b_un.b_addr;
	hdr_s = &leaf_s->hdr;
	hdr_d = &leaf_d->hdr;
	hdr_d->info = hdr_s->info;	/* struct copy */
	hdr_d->firstused = XFS_LBSIZE(mp);
	if (hdr_d->firstused == 0)
		hdr_d->firstused = XFS_LBSIZE(mp) - 1;
	hdr_d->namebytes = 0;
	hdr_d->count = 0;
	hdr_d->holes = 0;
	hdr_d->freemap[0].base = sizeof(xfs_dir_leaf_hdr_t);
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
xfs_dir_leaf_rebalance(xfs_da_state_t *state, xfs_da_state_blk_t *blk1,
				      xfs_da_state_blk_t *blk2)
{
	xfs_da_state_blk_t *tmp_blk;
	xfs_dir_leafblock_t *leaf1, *leaf2;
	xfs_dir_leaf_hdr_t *hdr1, *hdr2;
	int count, totallen, max, space, swap;

	/*
	 * Set up environment.
	 */
	ASSERT(blk1->magic == XFS_DIR_LEAF_MAGIC);
	ASSERT(blk2->magic == XFS_DIR_LEAF_MAGIC);
	leaf1 = (xfs_dir_leafblock_t *)blk1->bp->b_un.b_addr;
	leaf2 = (xfs_dir_leafblock_t *)blk2->bp->b_un.b_addr;
	ASSERT(leaf1->hdr.info.magic == XFS_DIR_LEAF_MAGIC);
	ASSERT(leaf2->hdr.info.magic == XFS_DIR_LEAF_MAGIC);

	/*
	 * Check ordering of blocks, reverse if it makes things simpler.
	 */
	swap = 0;
	if (xfs_dir_leaf_order(blk1->bp, blk2->bp)) {
		tmp_blk = blk1;
		blk1 = blk2;
		blk2 = tmp_blk;
		leaf1 = (xfs_dir_leafblock_t *)blk1->bp->b_un.b_addr;
		leaf2 = (xfs_dir_leafblock_t *)blk2->bp->b_un.b_addr;
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
		space += count * (sizeof(xfs_dir_leaf_name_t)-1);
		space += count * sizeof(xfs_dir_leaf_entry_t);

		/*
		 * leaf2 is the destination, compact it if it looks tight.
		 */
		max  = hdr2->firstused - sizeof(xfs_dir_leaf_hdr_t);
		max -= hdr2->count * sizeof(xfs_dir_leaf_entry_t);
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
		space += count * (sizeof(xfs_dir_leaf_name_t)-1);
		space += count * sizeof(xfs_dir_leaf_entry_t);

		/*
		 * leaf1 is the destination, compact it if it looks tight.
		 */
		max  = hdr1->firstused - sizeof(xfs_dir_leaf_hdr_t);
		max -= hdr1->count * sizeof(xfs_dir_leaf_entry_t);
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
	blk1->hashval = leaf1->entries[ leaf1->hdr.count-1 ].hashval;
	blk2->hashval = leaf2->entries[ leaf2->hdr.count-1 ].hashval;

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
xfs_dir_leaf_figure_balance(xfs_da_state_t *state,
					   xfs_da_state_blk_t *blk1,
					   xfs_da_state_blk_t *blk2,
					   int *countarg, int *namebytesarg)
{
	xfs_dir_leafblock_t *leaf1, *leaf2;
	xfs_dir_leaf_hdr_t *hdr1, *hdr2;
	xfs_dir_leaf_entry_t *entry;
	int count, max, totallen, half;
	int lastdelta, foundit, tmp;

	/*
	 * Set up environment.
	 */
	leaf1 = (xfs_dir_leafblock_t *)blk1->bp->b_un.b_addr;
	leaf2 = (xfs_dir_leafblock_t *)blk2->bp->b_un.b_addr;
	hdr1 = &leaf1->hdr;
	hdr2 = &leaf2->hdr;
	foundit = 0;
	totallen = 0;

	/*
	 * Examine entries until we reduce the absolute difference in
	 * byte usage between the two blocks to a minimum.
	 */
	max = hdr1->count + hdr2->count;
	half  = (max+1) * (sizeof(*entry)+sizeof(xfs_dir_leaf_entry_t)-1);
	half += hdr1->namebytes + hdr2->namebytes + state->args->namelen;
	half /= 2;
	lastdelta = state->blocksize;
	entry = &leaf1->entries[0];
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
			entry = &leaf1->entries[0];
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
	totallen -= count * (sizeof(*entry)+sizeof(xfs_dir_leaf_entry_t)-1);
	if (foundit) {
		totallen -= ( (sizeof(*entry)+sizeof(xfs_dir_leaf_entry_t)-1) +
			      state->args->namelen );
	}

	*countarg = count;
	*namebytesarg = totallen;
	return(foundit);
}

/*========================================================================
 * Routines used for shrinking the Btree.
 *========================================================================*/

#ifndef SIM
/*
 * Check a leaf block and its neighbors to see if the block should be
 * collapsed into one or the other neighbor.  Always keep the block
 * with the smaller block number.
 * If the current block is over 50% full, don't try to join it, return 0.
 * If the block is empty, fill in the state structure and return 2.
 * If it can be collapsed, fill in the state structure and return 1.
 * If nothing can be done, return 0.
 */
int
xfs_dir_leaf_toosmall(xfs_da_state_t *state, int *action)
{
	xfs_dir_leafblock_t *leaf;
	xfs_da_state_blk_t *blk;
	xfs_da_blkinfo_t *info;
	int count, bytes, forward, error, retval, i;
	xfs_fileoff_t blkno;
	buf_t *bp;

	/*
	 * Check for the degenerate case of the block being over 50% full.
	 * If so, it's not worth even looking to see if we might be able
	 * to coalesce with a sibling.
	 */
	blk = &state->path.blk[ state->path.active-1 ];
	info = (xfs_da_blkinfo_t *)blk->bp->b_un.b_addr;
	ASSERT(info->magic == XFS_DIR_LEAF_MAGIC);
	leaf = (xfs_dir_leafblock_t *)info;
	count = leaf->hdr.count;
	bytes = sizeof(xfs_dir_leaf_hdr_t) +
		count * sizeof(xfs_dir_leaf_entry_t) +
		count * (sizeof(xfs_dir_leaf_name_t)-1) +
		leaf->hdr.namebytes;
	if (bytes > (state->blocksize >> 1)) {
		*action = 0;	/* blk over 50%, dont try to join */
		return(0);
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
						      blkno, &bp);
		if (error)
			return(error);
		ASSERT(bp != NULL);

		leaf = (xfs_dir_leafblock_t *)info;
		count  = leaf->hdr.count;
		bytes  = state->blocksize - (state->blocksize>>2);
		bytes -= leaf->hdr.namebytes;
		leaf = (xfs_dir_leafblock_t *)bp->b_un.b_addr;
		ASSERT(leaf->hdr.info.magic == XFS_DIR_LEAF_MAGIC);
		count += leaf->hdr.count;
		bytes -= leaf->hdr.namebytes;
		bytes -= count * (sizeof(xfs_dir_leaf_name_t) - 1);
		bytes -= count * sizeof(xfs_dir_leaf_entry_t);
		bytes -= sizeof(xfs_dir_leaf_hdr_t);
		if (bytes >= 0)
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
	} else {
		error = xfs_da_path_shift(state, &state->path, forward,
						 0, &retval);
	}
	if (error)
		return(error);
	if (retval) {
		*action = 0;
	} else {
		*action = 1;
	}
	return(0);
}

/*
 * Remove a name from the leaf directory structure.
 *
 * Return 1 if leaf is less than 37% full, 0 if >= 37% full.
 * If two leaves are 37% full, when combined they will leave 25% free.
 */
int
xfs_dir_leaf_remove(xfs_trans_t *trans, buf_t *bp, int index)
{
	xfs_dir_leafblock_t *leaf;
	xfs_dir_leaf_hdr_t *hdr;
	xfs_dir_leaf_map_t *map;
	xfs_dir_leaf_entry_t *entry;
	xfs_dir_leaf_name_t *namest;
	int before, after, smallest, entsize;
	int tablesize, tmp, i;
	xfs_mount_t *mp;

	leaf = (xfs_dir_leafblock_t *)bp->b_un.b_addr;
	ASSERT(leaf->hdr.info.magic == XFS_DIR_LEAF_MAGIC);
	hdr = &leaf->hdr;
	mp = trans->t_mountp;
	ASSERT((hdr->count > 0) && (hdr->count < (XFS_LBSIZE(mp)/8)));
	ASSERT((index >= 0) && (index < hdr->count));
	ASSERT(hdr->firstused >= ((hdr->count*sizeof(*entry))+sizeof(*hdr)));
	entry = &leaf->entries[index];
	ASSERT(entry->nameidx >= hdr->firstused);
	ASSERT(entry->nameidx < XFS_LBSIZE(mp));

	/*
	 * Scan through free region table:
	 *    check for adjacency of free'd entry with an existing one,
	 *    find smallest free region in case we need to replace it,
	 *    adjust any map that borders the entry table,
	 */
	tablesize = hdr->count * sizeof(xfs_dir_leaf_entry_t)
			+ sizeof(xfs_dir_leaf_hdr_t);
	map = &hdr->freemap[0];
	tmp = map->size;
	before = after = -1;
	smallest = XFS_DIR_LEAF_MAPSIZE - 1;
	entsize = XFS_DIR_LEAF_ENTSIZE_BYENTRY(entry);
	for (i = 0; i < XFS_DIR_LEAF_MAPSIZE; map++, i++) {
		ASSERT(map->base < XFS_LBSIZE(mp));
		ASSERT(map->size < XFS_LBSIZE(mp));
		if (map->base == tablesize) {
			map->base -= sizeof(xfs_dir_leaf_entry_t);
			map->size += sizeof(xfs_dir_leaf_entry_t);
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
	xfs_trans_log_buf(trans, bp, XFS_DA_LOGRANGE(leaf, namest, entsize));

	hdr->namebytes -= entry->namelen;
	tmp = (hdr->count - index) * sizeof(xfs_dir_leaf_entry_t);
	bcopy((char *)(entry+1), (char *)entry, tmp);
	hdr->count--;
	xfs_trans_log_buf(trans, bp,
	    XFS_DA_LOGRANGE(leaf, entry, tmp + sizeof(*entry)));
	entry = &leaf->entries[hdr->count];
	bzero((char *)entry, sizeof(xfs_dir_leaf_entry_t));

	/*
	 * If we removed the first entry, re-find the first used byte
	 * in the name area.  Note that if the entry was the "firstused",
	 * then we don't have a "hole" in our block resulting from
	 * removing the name.
	 */
	if (smallest) {
		tmp = XFS_LBSIZE(mp);
		entry = &leaf->entries[0];
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
	xfs_trans_log_buf(trans, bp, XFS_DA_LOGRANGE(leaf, hdr, sizeof(*hdr)));

	/*
	 * Check if leaf is less than 50% full, caller may want to
	 * "join" the leaf with a sibling if so.
	 */
	tmp  = sizeof(xfs_dir_leaf_hdr_t);
	tmp += leaf->hdr.count * sizeof(xfs_dir_leaf_entry_t);
	tmp += leaf->hdr.count * (sizeof(xfs_dir_leaf_name_t) - 1);
	tmp += leaf->hdr.namebytes;
	if (tmp < (XFS_LBSIZE(mp)*37/100))
		return(1);			/* leaf is < 37% full */
	return(0);
}

/*
 * Move all the directory entries from drop_leaf into save_leaf.
 */
void
xfs_dir_leaf_unbalance(xfs_da_state_t *state, xfs_da_state_blk_t *drop_blk,
				      xfs_da_state_blk_t *save_blk)
{
	xfs_dir_leafblock_t *drop_leaf, *save_leaf, *tmp_leaf;
	xfs_dir_leaf_hdr_t *drop_hdr, *save_hdr, *tmp_hdr;
	xfs_mount_t *mp;
	char *tmpbuffer;

	/*
	 * Set up environment.
	 */
	mp = state->mp;
	ASSERT(drop_blk->magic == XFS_DIR_LEAF_MAGIC);
	ASSERT(save_blk->magic == XFS_DIR_LEAF_MAGIC);
	drop_leaf = (xfs_dir_leafblock_t *)drop_blk->bp->b_un.b_addr;
	save_leaf = (xfs_dir_leafblock_t *)save_blk->bp->b_un.b_addr;
	ASSERT(drop_leaf->hdr.info.magic == XFS_DIR_LEAF_MAGIC);
	ASSERT(save_leaf->hdr.info.magic == XFS_DIR_LEAF_MAGIC);
	drop_hdr = &drop_leaf->hdr;
	save_hdr = &save_leaf->hdr;

	/*
	 * Save last hashval from dying block for later Btree fixup.
	 */
	drop_blk->hashval = drop_leaf->entries[ drop_leaf->hdr.count-1 ].hashval;

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
		if (xfs_dir_leaf_order(save_blk->bp, drop_blk->bp)) {
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
		tmp_leaf = (xfs_dir_leafblock_t *)tmpbuffer;
		tmp_hdr = &tmp_leaf->hdr;
		tmp_hdr->info = save_hdr->info;	/* struct copy */
		tmp_hdr->count = 0;
		tmp_hdr->firstused = state->blocksize;
		if (tmp_hdr->firstused == 0)
			tmp_hdr->firstused = state->blocksize - 1;
		tmp_hdr->namebytes = 0;
		if (xfs_dir_leaf_order(save_blk->bp, drop_blk->bp)) {
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
	save_blk->hashval = save_leaf->entries[ save_leaf->hdr.count-1 ].hashval;
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
xfs_dir_leaf_lookup_int(buf_t *bp, xfs_da_name_t *args, int *index)
{
	xfs_dir_leafblock_t *leaf;
	xfs_dir_leaf_entry_t *entry;
	xfs_dir_leaf_name_t *namest;
	int probe, span;
	uint hashval;

	leaf = (xfs_dir_leafblock_t *)bp->b_un.b_addr;
	ASSERT(leaf->hdr.info.magic == XFS_DIR_LEAF_MAGIC);
	ASSERT((((int)leaf->hdr.count) >= 0) && \
	       (leaf->hdr.count < (XFS_LBSIZE(args->dp->i_mount)/8)));

	/*
	 * Binary search.  (note: small blocks will skip this loop)
	 */
	hashval = args->hashval;
	probe = span = leaf->hdr.count / 2;
	for (entry = &leaf->entries[probe]; span > 4;
		   entry = &leaf->entries[probe]) {
		span /= 2;
		if (entry->hashval < hashval)
			probe += span;
		else if (entry->hashval > hashval)
			probe -= span;
		else
			break;
	}
	ASSERT((probe >= 0) && \
	       ((leaf->hdr.count == 0) || (probe < leaf->hdr.count)));
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

/*========================================================================
 * Utility routines.
 *========================================================================*/

/*
 * Move the indicated entries from one leaf to another.
 * NOTE: this routine modifies both source and destination leaves.
 */
/* ARGSUSED */
STATIC void
xfs_dir_leaf_moveents(xfs_dir_leafblock_t *leaf_s, int start_s,
		      xfs_dir_leafblock_t *leaf_d, int start_d,
		      int count, xfs_mount_t *mp)
{
	xfs_dir_leaf_hdr_t *hdr_s, *hdr_d;
	xfs_dir_leaf_entry_t *entry_s, *entry_d;
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
		tmp *= sizeof(xfs_dir_leaf_entry_t);
		entry_s = &leaf_d->entries[start_d];
		entry_d = &leaf_d->entries[start_d + count];
		bcopy((char *)entry_s, (char *)entry_d, tmp);
	}

	/*
	 * Copy all entry's in the same (sorted) order,
	 * but allocate filenames packed and in sequence.
	 */
	entry_s = &leaf_s->entries[start_s];
	entry_d = &leaf_d->entries[start_d];
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
		tmp  = hdr_d->count * sizeof(xfs_dir_leaf_entry_t)
				+ sizeof(xfs_dir_leaf_hdr_t);
		ASSERT(hdr_d->firstused >= tmp);

	}

	/*
	 * Zero out the entries we just copied.
	 */
	if (start_s == hdr_s->count) {
		tmp = count * sizeof(xfs_dir_leaf_entry_t);
		entry_s = &leaf_s->entries[start_s];
		ASSERT((char *)entry_s + tmp <= (char *)leaf_s + XFS_LBSIZE(mp));
		bzero((char *)entry_s, tmp);
	} else {
		/*
		 * Move the remaining entries down to fill the hole,
		 * then zero the entries at the top.
		 */
		tmp  = hdr_s->count - count;
		tmp *= sizeof(xfs_dir_leaf_entry_t);
		entry_s = &leaf_s->entries[start_s + count];
		entry_d = &leaf_s->entries[start_s];
		bcopy((char *)entry_s, (char *)entry_d, tmp);

		tmp = count * sizeof(xfs_dir_leaf_entry_t);
		entry_s = &leaf_s->entries[hdr_s->count];
		ASSERT((char *)entry_s + tmp <= (char *)leaf_s + XFS_LBSIZE(mp));
		bzero((char *)entry_s, tmp);
	}

	/*
	 * Fill in the freemap information
	 */
	hdr_d->freemap[0].base = hdr_d->count*sizeof(xfs_dir_leaf_entry_t);
	hdr_d->freemap[0].base += sizeof(xfs_dir_leaf_hdr_t);
	hdr_d->freemap[0].size = hdr_d->firstused - hdr_d->freemap[0].base;
	hdr_s->holes = 1;	/* leaf may not be compact */
}

/*
 * Compare two leaf blocks "order".
 */
int
xfs_dir_leaf_order(buf_t *leaf1_bp, buf_t *leaf2_bp)
{
	xfs_dir_leafblock_t *leaf1, *leaf2;
	
	leaf1 = (xfs_dir_leafblock_t *)leaf1_bp->b_un.b_addr;
	leaf2 = (xfs_dir_leafblock_t *)leaf2_bp->b_un.b_addr;
	ASSERT((leaf1->hdr.info.magic == XFS_DIR_LEAF_MAGIC) && \
	       (leaf2->hdr.info.magic == XFS_DIR_LEAF_MAGIC));
	if ((leaf1->hdr.count > 0) && (leaf2->hdr.count > 0) && 
	    ((leaf2->entries[ 0 ].hashval <
	      leaf1->entries[ 0 ].hashval) ||
	     (leaf2->entries[ leaf2->hdr.count-1 ].hashval <
	      leaf1->entries[ leaf1->hdr.count-1 ].hashval))) {
		return(1);
	}
	return(0);
}

/*
 * Pick up the last hashvalue from a leaf block.
 */
uint
xfs_dir_leaf_lasthash(buf_t *bp, int *count)
{
	xfs_dir_leafblock_t *leaf;

	leaf = (xfs_dir_leafblock_t *)bp->b_un.b_addr;
	ASSERT(leaf->hdr.info.magic == XFS_DIR_LEAF_MAGIC);
	if (count) {
		*count = leaf->hdr.count;
	}
	return(leaf->entries[ leaf->hdr.count-1 ].hashval);
}

#ifndef SIM
/*
 * Print the contents of a leaf block.
 */
/*ARGSUSED*/
void
xfs_dir_leaf_print_int(buf_t *bp, xfs_inode_t *dp)
{
	xfs_dir_leafblock_t *leaf;
	xfs_dir_leaf_entry_t *entry;
	xfs_dir_leaf_name_t *namest;
	xfs_ino_t ino;
	int i;

	leaf = (xfs_dir_leafblock_t *)bp->b_un.b_addr;
	ASSERT(leaf->hdr.info.magic == XFS_DIR_LEAF_MAGIC);
	entry = &leaf->entries[0];
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
	xfs_dir_leafblock_t *leaf;
	xfs_dir_leaf_entry_t *entry;
	xfs_dir_leaf_name_t *namest;
	int retval, done, entno, i;
	xfs_mount_t *mp;
	xfs_ino_t ino;
	__uint32_t bno;
	off_t nextcook;

	mp = dp->i_mount;
	bno = (__uint32_t)XFS_DA_COOKIE_BNO(mp, uio->uio_offset);
	entno = XFS_DA_COOKIE_ENTRY(mp, uio->uio_offset);
	leaf = (xfs_dir_leafblock_t *)bp->b_un.b_addr;
	ASSERT(leaf->hdr.info.magic == XFS_DIR_LEAF_MAGIC);
	if (entno >= leaf->hdr.count) {
		*eobp = 0;
		return(XFS_ERROR(ENOENT));
	}
	entry = &leaf->entries[entno];
	for (i = entno; i < leaf->hdr.count; entry++, i++) {
		namest = XFS_DIR_LEAF_NAMESTRUCT(leaf, entry->nameidx);
		bcopy(namest->inumber, (char *)&ino, sizeof(ino));
		if (i == leaf->hdr.count - 1) {
			if (leaf->hdr.info.forw)
				nextcook = XFS_DA_MAKE_COOKIE(mp, leaf->hdr.info.forw, 0);
			else
				nextcook = XFS_DA_MAKE_COOKIE(mp, XFS_B_TO_FSBT(mp, dp->i_d.di_size), 0);
		} else
			nextcook = XFS_DA_MAKE_COOKIE(mp, bno, i + 1);
		retval = xfs_dir_put_dirent(mp, dbp, ino, 
				(char *)(namest->name), entry->namelen, 
				nextcook, uio, &done);
		if (!done) {
			uio->uio_offset = XFS_DA_MAKE_COOKIE(mp, bno, i);
			*eobp = 0;
			return(retval);
		}
	}
	uio->uio_offset = XFS_DA_MAKE_COOKIE(mp, bno, i);
	*eobp = 1;
	return(0);
}

/*
 * Format a dirent structure and copy it out the the user's buffer.
 * A 32-bit process has a differently sized dirent structure than
 * the kernel does, so do the translation here based on the process'
 * abi.
 */
/*ARGSUSED*/
int
xfs_dir_put_dirent(xfs_mount_t *mp, dirent_t *dbp, xfs_ino_t ino,
			       char *name, int namelen, off_t doff,
			       uio_t *uio, int *done)
{
	irix5_dirent_t	*i5_dbp;
	int		retval;
	int		target_abi;
	int		reclen;
	iovec_t		*iovp;

	/*
	 * If it's a kernel request, then the target abi is
	 * IRIX5_64.
	 */
	if (uio->uio_segflg == UIO_USERSPACE) {
		target_abi = u.u_procp->p_abi;
	} else {
		target_abi = ABI_IRIX5_64;
	}

#ifdef REDWOOD
	if (ABI_IS_IRIX5_64(target_abi)) {
#else
	if (ABI_IS(ABI_IRIX5_64 | ABI_IRIX5_N32, target_abi)) {
#endif
		reclen = DIRENTSIZE(namelen);
		if (reclen > uio->uio_resid) {
			*done = 0;
			retval = 0;
		} else {
			if (dbp != NULL) {
				dbp->d_reclen = reclen;
				dbp->d_ino = ino;
				bcopy(name, dbp->d_name, namelen);
				dbp->d_name[namelen] = '\0';
				dbp->d_off = doff;
				retval = uiomove((caddr_t)dbp, dbp->d_reclen,
						 UIO_READ, uio);
				*done = (retval == 0);
			} else {
				/*
				 * Our caller is in the kernel, probably
				 * NFS, so work directly in its buffer.
				 */
				ASSERT(uio->uio_segflg == UIO_SYSSPACE);
				iovp = uio->uio_iov;
				dbp = (dirent_t *)iovp->iov_base;
				dbp->d_reclen = reclen;
				dbp->d_ino = ino;
				bcopy(name, dbp->d_name, namelen);
				dbp->d_name[namelen] = '\0';
				dbp->d_off = doff;
				iovp->iov_base += reclen;
				iovp->iov_len -= reclen;
				uio->uio_resid -= reclen;
				*done = 1;
				retval = 0;
			}
		}
	} else {
		i5_dbp = (irix5_dirent_t *)dbp;
		if ((i5_dbp->d_reclen = IRIX5_DIRENTSIZE(namelen)) >
		    uio->uio_resid) {
			*done = 0;
			retval = 0;
		} else {
			i5_dbp->d_ino = ino;
			bcopy(name, i5_dbp->d_name, namelen);
			i5_dbp->d_name[namelen] = '\0';
			i5_dbp->d_off = doff;
			retval = uiomove((caddr_t)i5_dbp, i5_dbp->d_reclen,
					 UIO_READ, uio);
			*done = (retval == 0);
		}
	}
	return(retval);
}
#endif	/* !SIM */
