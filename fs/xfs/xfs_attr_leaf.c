/*
 * xfs_attr_leaf.c
 *
 * GROT: figure out how to recover gracefully when bmap returns ENOSPC.
 */

#include <sys/param.h>
#include <sys/errno.h>
#include <sys/buf.h>
#include <sys/vnode.h>
#include <sys/kmem.h>
#include <sys/debug.h>
#include <sys/systm.h>
#include <sys/attributes.h>
#include "xfs_types.h"
#include "xfs_inum.h"
#include <sys/grio.h>
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
#include "xfs_attr.h"
#include "xfs_attr_leaf.h"
#include "xfs_error.h"

/*
 * xfs_attr_leaf.c
 *
 * Routines to implement leaf blocks of attributes as Btrees of hashed names.
 */

/*========================================================================
 * Function prototypes for the kernel.
 *========================================================================*/

/*
 * Routines used for growing the Btree.
 */
STATIC int xfs_attr_leaf_add_work(xfs_trans_t *trans, buf_t *leaf_buffer,
					      xfs_da_args_t *args,
					      int freemap_index);
STATIC void xfs_attr_leaf_compact(xfs_trans_t *trans, buf_t *leaf_buffer);
STATIC void xfs_attr_leaf_rebalance(xfs_da_state_t *state,
						   xfs_da_state_blk_t *blk1,
						   xfs_da_state_blk_t *blk2);
STATIC int xfs_attr_leaf_figure_balance(xfs_da_state_t *state,
					   xfs_da_state_blk_t *leaf_blk_1,
					   xfs_da_state_blk_t *leaf_blk_2,
					   int *number_entries_in_blk1,
					   int *number_usedbytes_in_blk1);

/*
 * Utility routines.
 */
STATIC void xfs_attr_leaf_moveents(xfs_attr_leafblock_t *src_leaf,
					 int src_start,
					 xfs_attr_leafblock_t *dst_leaf,
					 int dst_start, int move_count,
					 xfs_mount_t *mp);


/*========================================================================
 * External routines when dirsize < XFS_LITINO(mp).
 *========================================================================*/

/*
 * Create the initial contents of a shortform attribute list.
 */
int
xfs_attr_shortform_create(xfs_trans_t *trans, xfs_inode_t *dp)
{
	xfs_attr_sf_hdr_t *hdr;

	ASSERT(dp->i_afp->if_bytes == 0);
	if (dp->i_d.di_aformat == XFS_DINODE_FMT_EXTENTS) {
		dp->i_afp->if_flags &= ~XFS_IFEXTENTS;	/* just in case */
		dp->i_d.di_aformat = XFS_DINODE_FMT_LOCAL;
		xfs_trans_log_inode(trans, dp, XFS_ILOG_CORE);
		dp->i_afp->if_flags |= XFS_IFINLINE;
	}
	ASSERT(dp->i_afp->if_flags & XFS_IFINLINE);
	xfs_idata_realloc(dp, sizeof(*hdr), XFS_ATTR_FORK);
	hdr = (xfs_attr_sf_hdr_t *)dp->i_afp->if_u1.if_data;
	hdr->count = 0;
	hdr->totsize = sizeof(*hdr);
	xfs_trans_log_inode(trans, dp, XFS_ILOG_CORE | XFS_ILOG_ADATA);
	return(0);
}

/*
 * Add a name/value pair to the shortform attribute list.
 * Overflow from the inode has already been checked for.
 */
int
xfs_attr_shortform_add(xfs_trans_t *trans, xfs_da_args_t *args)
{
	xfs_attr_shortform_t *sf;
	xfs_attr_sf_entry_t *sfe;
	int i, offset, size;
	xfs_inode_t *dp;

	dp = args->dp;
	ASSERT(dp->i_afp->if_flags & XFS_IFINLINE);
	sf = (xfs_attr_shortform_t *)dp->i_afp->if_u1.if_data;
	sfe = &sf->list[0];
	for (i = 0; i < sf->hdr.count; sfe = XFS_ATTR_SF_NEXTENTRY(sfe), i++) {
		if (sfe->namelen != args->namelen)
			continue;
		if (bcmp(args->name, sfe->nameval, args->namelen) != 0)
			continue;
		if (((args->flags & ATTR_ROOT) != 0) !=
		    ((sfe->flags & XFS_ATTR_ROOT) != 0))
			continue;
		return(XFS_ERROR(EEXIST));
	}

	offset = (char *)sfe - (char *)sf;
	size = XFS_ATTR_SF_ENTSIZE_BYNAME(args->namelen, args->valuelen);
	xfs_idata_realloc(dp, size, XFS_ATTR_FORK);
	sf = (xfs_attr_shortform_t *)dp->i_afp->if_u1.if_data;
	sfe = (xfs_attr_sf_entry_t *)((char *)sf + offset);

	sfe->namelen = args->namelen;
	sfe->valuelen = args->valuelen;
	sfe->flags = (args->flags & ATTR_ROOT) ? XFS_ATTR_ROOT : 0;
	bcopy(args->name, sfe->nameval, args->namelen);
	bcopy(args->value, &sfe->nameval[args->namelen], args->valuelen);
	sf->hdr.count++;
	sf->hdr.totsize += size;
	xfs_trans_log_inode(trans, dp, XFS_ILOG_CORE | XFS_ILOG_ADATA);

	return(0);
}

/*
 * Remove a name from the shortform attribute list structure.
 */
int
xfs_attr_shortform_remove(xfs_trans_t *trans, xfs_da_args_t *args)
{
	xfs_attr_shortform_t *sf;
	xfs_attr_sf_entry_t *sfe;
	int base, size, end, totsize, i;

	/*
	 * Remove the attribute.
	 */
	base = sizeof(xfs_attr_sf_hdr_t);
	sf = (xfs_attr_shortform_t *)args->dp->i_afp->if_u1.if_data;
	sfe = &sf->list[0];
	for (i = 0; i < sf->hdr.count; sfe = XFS_ATTR_SF_NEXTENTRY(sfe),
					base += size, i++) {
		size = XFS_ATTR_SF_ENTSIZE(sfe);
		if (sfe->namelen != args->namelen)
			continue;
		if (bcmp(sfe->nameval, args->name, args->namelen) != 0)
			continue;
		if (((args->flags & ATTR_ROOT) != 0) !=
		    ((sfe->flags & XFS_ATTR_ROOT) != 0))
			continue;
		break;
	}
	if (i == sf->hdr.count)
		return(XFS_ERROR(ENOATTR));

	end = base + size;
	totsize = sf->hdr.totsize;
	if (end != totsize) {
		bcopy(&((char *)sf)[end], &((char *)sf)[base], totsize - end);
	}
	sf->hdr.count--;
	sf->hdr.totsize -= size;
	xfs_idata_realloc(args->dp, -size, XFS_ATTR_FORK);
	xfs_trans_log_inode(trans, args->dp, XFS_ILOG_CORE | XFS_ILOG_ADATA);

	return(0);
}

/*
 * Look up a name in a shortform attribute list structure.
 */
/*ARGSUSED*/
int
xfs_attr_shortform_lookup(xfs_trans_t *trans, xfs_da_args_t *args)
{
	xfs_attr_shortform_t *sf;
	xfs_attr_sf_entry_t *sfe;
	int i;
	xfs_inode_t *dp;

	dp = args->dp;
	ASSERT(dp->i_afp->if_flags & XFS_IFINLINE);
	sf = (xfs_attr_shortform_t *)dp->i_afp->if_u1.if_data;
	sfe = &sf->list[0];
	for (i = 0; i < sf->hdr.count; sfe = XFS_ATTR_SF_NEXTENTRY(sfe), i++) {
		if (sfe->namelen != args->namelen)
			continue;
		if (bcmp(args->name, sfe->nameval, args->namelen) != 0)
			continue;
		if (((args->flags & ATTR_ROOT) != 0) !=
		    ((sfe->flags & XFS_ATTR_ROOT) != 0))
			continue;
		return(XFS_ERROR(EEXIST));
	}
	return(XFS_ERROR(ENOATTR));
}

/*
 * Look up a name in a shortform attribute list structure.
 */
/*ARGSUSED*/
int
xfs_attr_shortform_getvalue(xfs_da_args_t *args)
{
	xfs_attr_shortform_t *sf;
	xfs_attr_sf_entry_t *sfe;
	int i;
	xfs_inode_t *dp;

	dp = args->dp;
	ASSERT(XFS_IFORK_FORMAT(dp, XFS_ATTR_FORK) == XFS_IFINLINE);
	sf = (xfs_attr_shortform_t *)dp->i_afp->if_u1.if_data;
	sfe = &sf->list[0];
	for (i = 0; i < sf->hdr.count; sfe = XFS_ATTR_SF_NEXTENTRY(sfe), i++) {
		if (sfe->namelen != args->namelen)
			continue;
		if (bcmp(args->name, sfe->nameval, args->namelen) != 0)
			continue;
		if (((args->flags & ATTR_ROOT) != 0) !=
		    ((sfe->flags & XFS_ATTR_ROOT) != 0))
			continue;
		args->valuelen = sfe->valuelen;
		bcopy(&sfe->nameval[args->namelen], args->value, args->valuelen);
		return(XFS_ERROR(EEXIST));
	}
	return(XFS_ERROR(ENOATTR));
}

/*
 * Convert from using the shortform to the leaf.
 */
int
xfs_attr_shortform_to_leaf(xfs_trans_t *trans, xfs_da_args_t *iargs)
{
	xfs_inode_t *dp;
	xfs_attr_shortform_t *sf;
	xfs_attr_sf_entry_t *sfe;
	xfs_da_args_t args;
	char *tmpbuffer;
	int error, i, size;
	xfs_fileoff_t blkno;
	buf_t *bp;

	dp = iargs->dp;
	sf = (xfs_attr_shortform_t *)dp->i_afp->if_u1.if_data;
	size = sf->hdr.totsize;
	tmpbuffer = kmem_alloc(size, KM_SLEEP);
	ASSERT(tmpbuffer != NULL);
	bcopy(dp->i_afp->if_u1.if_data, tmpbuffer, size);
	sf = (xfs_attr_shortform_t *)tmpbuffer;

	xfs_idata_realloc(dp, -size, XFS_ATTR_FORK);
	error = xfs_da_grow_inode(trans, iargs, 1, &blkno);
	if (error) {
		xfs_idata_realloc(dp, size, XFS_ATTR_FORK);	/* try to put */
		bcopy(tmpbuffer, dp->i_afp->if_u1.if_data, size);	/* it back */
		goto out;
	}

	ASSERT(blkno == 0);
	error = xfs_attr_leaf_create(trans, dp, blkno, &bp);
	if (error) {
		error = xfs_da_shrink_inode(trans, iargs, 0, 1, bp);
		if (error)
			goto out;
		xfs_idata_realloc(dp, size, XFS_ATTR_FORK);	/* try to put */
		bcopy(tmpbuffer, dp->i_afp->if_u1.if_data, size);	/* it back */
		goto out;
	}

	bzero((char *)&args, sizeof(args));
	args.dp = iargs->dp;
	args.firstblock = iargs->firstblock;
	args.flist = iargs->flist;
	args.total = iargs->total;
	args.whichfork = XFS_ATTR_FORK;

	sfe = &sf->list[0];
	for (i = 0; i < sf->hdr.count; i++) {
		args.name = (char *)sfe->nameval;
		args.namelen = sfe->namelen;
		args.value = (char *)&sfe->nameval[args.namelen];
		args.valuelen = sfe->valuelen;
		args.hashval = xfs_da_hashname((char *)sfe->nameval,
					       sfe->namelen);
		args.flags = (sfe->flags & XFS_ATTR_ROOT) ? ATTR_ROOT : 0;
		error = xfs_attr_leaf_add(trans, bp, &args);
		ASSERT(error != ENOSPC);
		if (error)
			goto out;
		sfe = XFS_ATTR_SF_NEXTENTRY(sfe);
	}
	error = 0;

out:
	kmem_free(tmpbuffer, size);
	return(error);
}

/*
 * Copy out entries for attr_list(), for shortform attribute lists.
 */
/*ARGSUSED*/
int
xfs_attr_shortform_list(xfs_inode_t *dp, attrlist_t *alist, int flags,
				    attrlist_cursor_kern_t *cursor)
{
	xfs_attr_shortform_t *sf;
	xfs_attr_sf_entry_t *sfe;
	int i;

	sf = (xfs_attr_shortform_t *)dp->i_afp->if_u1.if_data;
	if (sf->hdr.count == 0)
		return(0);
	sfe = &sf->list[0];
	if (cursor->initted == 0) {
		cursor->initted = 1;
		cursor->blkno = cursor->index = i = 0;
	} else if ((cursor->blkno != 0) || (cursor->index >= sf->hdr.count)) {
		cursor->initted = 1;
		cursor->blkno = cursor->index = i = 0;
	} else {		/* GROT: check this code */
		for (i = 0; i < cursor->index-1; i++) {
			sfe = XFS_ATTR_SF_NEXTENTRY(sfe);
		}
		if (cursor->hashval != xfs_da_hashname((char *)sfe->nameval,
						       sfe->namelen)) {
			sfe = &sf->list[0];
			for (i = 0; i < sf->hdr.count; i++) {
				if (cursor->hashval == 
					xfs_da_hashname((char *)sfe->nameval,
							sfe->namelen)) {
					break;
				}
				sfe = XFS_ATTR_SF_NEXTENTRY(sfe);
			}
			if (i == sf->hdr.count) {
				sfe = &sf->list[0];
				for (i = 0; i < cursor->index-1; i++) {
					sfe = XFS_ATTR_SF_NEXTENTRY(sfe);
				}
			}
		}
		sfe = XFS_ATTR_SF_NEXTENTRY(sfe);
		i++;
	}

	for (  ; i < sf->hdr.count; i++, cursor->index++) {
		cursor->hashval = xfs_da_hashname((char *)sfe->nameval,
						  sfe->namelen);
		if (((flags & ATTR_ROOT) != 0) !=
		    ((sfe->flags & XFS_ATTR_ROOT) != 0)) {
			;
		} else if (xfs_attr_put_listent(alist, (char *)sfe->nameval, 
						(int)sfe->namelen,
						(int)sfe->valuelen,
						cursor)) {
			return(0);
		}
		sfe = XFS_ATTR_SF_NEXTENTRY(sfe);
	}
	return(0);
}

/*
 * Check a leaf attribute block to see if all the entries would fit into
 * a shortform attribute list.
 */
int
xfs_attr_shortform_allfit(buf_t *bp, xfs_inode_t *dp)
{
	xfs_attr_leafblock_t *leaf;
	xfs_attr_leaf_entry_t *entry;
	xfs_attr_leaf_name_local_t *name_loc;
	int bytes, i;

	leaf = (xfs_attr_leafblock_t *)bp->b_un.b_addr;
	ASSERT(leaf->hdr.info.magic == XFS_ATTR_LEAF_MAGIC);

	entry = &leaf->entries[0];
	bytes = sizeof(struct xfs_attr_sf_hdr);
	for (i = 0; i < leaf->hdr.count; entry++, i++) {
		if (entry->flags & XFS_ATTR_INCOMPLETE)
			continue;		/* don't copy partial entries */
		if (!(entry->flags & XFS_ATTR_LOCAL))
			return(0);
		name_loc = XFS_ATTR_LEAF_NAME_LOCAL(leaf, i);
		bytes += sizeof(struct xfs_attr_sf_entry)-1 + 
				name_loc->namelen + name_loc->valuelen;
	}
	return( bytes < XFS_IFORK_ASIZE(dp) );
}

/*
 * Convert a leaf attribute list to shortform attribute list
 */
int
xfs_attr_leaf_to_shortform(xfs_trans_t *trans, buf_t *bp, xfs_da_args_t *iargs)
{
	xfs_attr_leafblock_t *leaf;
	xfs_attr_leaf_entry_t *entry;
	xfs_attr_leaf_name_local_t *name_loc;
	xfs_da_args_t args;
	xfs_inode_t *dp;
	char *tmpbuffer;
	int error, i;

	dp = iargs->dp;
	tmpbuffer = kmem_alloc(XFS_LBSIZE(dp->i_mount), KM_SLEEP);
	ASSERT(tmpbuffer != NULL);

	ASSERT(bp != NULL);
	bcopy(bp->b_un.b_addr, tmpbuffer, XFS_LBSIZE(dp->i_mount));
	leaf = (xfs_attr_leafblock_t *)tmpbuffer;
	ASSERT(leaf->hdr.info.magic == XFS_ATTR_LEAF_MAGIC);
	bzero(bp->b_un.b_addr, XFS_LBSIZE(dp->i_mount));

	/*
	 * Clean out the prior contents of the attribute list.
	 */
	error = xfs_da_shrink_inode(trans, iargs, 0, 1, bp);
	if (error)
		goto out;
	error = xfs_attr_shortform_create(trans, dp);
	if (error)
		goto out;	

	/*
	 * Copy the attributes
	 */
	bzero((char *)&args, sizeof(args));
	args.dp = dp;
	args.firstblock = iargs->firstblock;
	args.flist = iargs->flist;
	args.total = iargs->total;
	args.whichfork = XFS_ATTR_FORK;
	entry = &leaf->entries[0];
	for (i = 0; i < leaf->hdr.count; entry++, i++) {
		if (entry->flags & XFS_ATTR_INCOMPLETE)
			continue;		/* don't copy partial entries */
		if (entry->nameidx == 0)
			continue;
		ASSERT(entry->flags & XFS_ATTR_LOCAL);
		name_loc = XFS_ATTR_LEAF_NAME_LOCAL(leaf, i);
		args.name = (char *)name_loc->nameval;
		args.namelen = name_loc->namelen;
		args.value = (char *)&name_loc->nameval[args.namelen];
		args.valuelen = name_loc->valuelen;
		args.hashval = entry->hashval;
		args.flags = (entry->flags & XFS_ATTR_ROOT) ? ATTR_ROOT : 0;
		xfs_attr_shortform_add(trans, &args);
	}
	error = 0;

out:
	kmem_free(tmpbuffer, XFS_LBSIZE(dp->i_mount));
	return(error);
}

/*
 * Convert from using a single leaf to a root node and a leaf.
 */
int
xfs_attr_leaf_to_node(xfs_trans_t *trans, xfs_da_args_t *args)
{
	xfs_attr_leafblock_t *leaf;
	xfs_da_intnode_t *node;
	xfs_inode_t *dp;
	buf_t *bp1, *bp2;
	xfs_fileoff_t blkno;
	int error;

	dp = args->dp;
	error = xfs_da_grow_inode(trans, args, 1, &blkno);
	if (error)
		return(error);
	error = xfs_da_read_buf(trans, dp, 0, &bp1, XFS_ATTR_FORK);
	if (error)
		return(error);
	ASSERT(bp1 != NULL);
	error = xfs_da_get_buf(trans, dp, blkno, &bp2, XFS_ATTR_FORK);
	if (error)
		return(error);
	ASSERT(bp2 != NULL);
	bcopy(bp1->b_un.b_addr, bp2->b_un.b_addr, XFS_LBSIZE(dp->i_mount));
	xfs_trans_log_buf(trans, bp2, 0, XFS_LBSIZE(dp->i_mount) - 1);

	/*
	 * Set up the new root node.
	 */
	error = xfs_da_node_create(trans, dp, 0, 1, &bp1, XFS_ATTR_FORK);
	if (error)
		return(error);
	node = (xfs_da_intnode_t *)bp1->b_un.b_addr;
	leaf = (xfs_attr_leafblock_t *)bp2->b_un.b_addr;
	ASSERT(leaf->hdr.info.magic == XFS_ATTR_LEAF_MAGIC);
	node->btree[0].hashval = leaf->entries[ leaf->hdr.count-1 ].hashval;
	node->btree[0].before = blkno;
	node->hdr.count = 1;
	xfs_trans_log_buf(trans, bp1, 0, XFS_LBSIZE(dp->i_mount) - 1);

	return(0);
}


/*========================================================================
 * Routines used for growing the Btree.
 *========================================================================*/

/*
 * Create the initial contents of a leaf attribute list
 * or a leaf in a node attribute list.
 */
int
xfs_attr_leaf_create(xfs_trans_t *trans, xfs_inode_t *dp, xfs_fileoff_t blkno,
				 buf_t **bpp)
{
	xfs_attr_leafblock_t *leaf;
	xfs_attr_leaf_hdr_t *hdr;
	buf_t *bp;
	int error;

	error = xfs_da_get_buf(trans, dp, blkno, &bp, XFS_ATTR_FORK);
	if (error)
		return(error);
	ASSERT(bp != NULL);
	leaf = (xfs_attr_leafblock_t *)bp->b_un.b_addr;
	bzero((char *)leaf, XFS_LBSIZE(dp->i_mount));
	hdr = &leaf->hdr;
	hdr->info.magic = XFS_ATTR_LEAF_MAGIC;
	hdr->firstused = XFS_LBSIZE(dp->i_mount);
	if (hdr->firstused == 0)
		hdr->firstused = XFS_LBSIZE(dp->i_mount) - 1;
	hdr->freemap[0].base = sizeof(xfs_attr_leaf_hdr_t);
	hdr->freemap[0].size = hdr->firstused - hdr->freemap[0].base;

	xfs_trans_log_buf(trans, bp, 0, XFS_LBSIZE(dp->i_mount) - 1);

	*bpp = bp;
	return(0);
}

/*
 * Split the leaf node, rebalance, then add the new entry.
 */
int
xfs_attr_leaf_split(xfs_da_state_t *state, xfs_da_state_blk_t *oldblk,
				   xfs_da_state_blk_t *newblk)
{
	xfs_fileoff_t blkno;
	int error;

	/*
	 * Allocate space for a new leaf node.
	 */
	ASSERT(oldblk->magic == XFS_ATTR_LEAF_MAGIC);
	error = xfs_da_grow_inode(state->trans, state->args, 1, &blkno);
	if (error)
		return(error);
	error = xfs_attr_leaf_create(state->trans, state->args->dp, blkno,
						   &newblk->bp);
	if (error)
		return(error);
	newblk->blkno = blkno;
	newblk->magic = XFS_ATTR_LEAF_MAGIC;

	/*
	 * Rebalance the entries across the two leaves.
	 * NOTE: rebalance() currently depends on the 2nd block being empty.
	 */
	xfs_attr_leaf_rebalance(state, oldblk, newblk);
	error = xfs_da_blk_link(state, oldblk, newblk);
	if (error)
		return(error);

	/*
	 * Save info on "old" attribute for "atomic rename" ops, leaf_add()
	 * modifies the index/blkno/rmtblk/rmtblkcnt fields to show the
	 * "new" attrs info.  Will need the "old" info to remove it later.
	 *
	 * Insert the "new" entry in the correct block.
	 */
	if (state->inleaf)
		error = xfs_attr_leaf_add(state->trans, oldblk->bp, state->args);
	else
		error = xfs_attr_leaf_add(state->trans, newblk->bp, state->args);
	ASSERT(!error);

	/*
	 * Update last hashval in each block since we added the name.
	 */
	oldblk->hashval = xfs_attr_leaf_lasthash(oldblk->bp, NULL);
	newblk->hashval = xfs_attr_leaf_lasthash(newblk->bp, NULL);
	return(0);
}

/*
 * Add a name to the leaf attribute list structure.
 */
int
xfs_attr_leaf_add(xfs_trans_t *trans, buf_t *bp, xfs_da_args_t *args)
{
	xfs_attr_leafblock_t *leaf;
	xfs_attr_leaf_hdr_t *hdr;
	xfs_attr_leaf_map_t *map;
	int tablesize, i, entsize, tmp;

	leaf = (xfs_attr_leafblock_t *)bp->b_un.b_addr;
	ASSERT(leaf->hdr.info.magic == XFS_ATTR_LEAF_MAGIC);
	ASSERT((args->index >= 0) && (args->index <= leaf->hdr.count));
	hdr = &leaf->hdr;

	/*
	 * Search through freemap for first-fit on new name length.
	 * (may need to figure in size of entry struct too)
	 */
	entsize = xfs_attr_leaf_newentsize(args,
					   trans->t_mountp->m_sb.sb_blocksize,
					   NULL);
	tablesize = (hdr->count + 1) * sizeof(xfs_attr_leaf_entry_t)
			+ sizeof(xfs_attr_leaf_hdr_t);
	map = &hdr->freemap[XFS_ATTR_LEAF_MAPSIZE-1];
	for (i = XFS_ATTR_LEAF_MAPSIZE-1; i >= 0; map--, i--) {
		if (map->size == 0)
			continue;	/* no space in this map */
		tmp = entsize;
		if (map->base <= hdr->firstused)
			tmp += sizeof(xfs_attr_leaf_entry_t);
		if ((map->size >= tmp) && (tablesize <= hdr->firstused)) {
			tmp = xfs_attr_leaf_add_work(trans, bp, args, i);
			return(tmp);
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
	xfs_attr_leaf_compact(trans, bp);

	/*
	 * Search through freemap for first-fit on new name length.
	 * (this is an exact duplicate of the above code segment)
	 */
	map = &hdr->freemap[XFS_ATTR_LEAF_MAPSIZE-1];
	for (i = XFS_ATTR_LEAF_MAPSIZE-1; i >= 0; map--, i--) {
		if (map->size == 0)
			continue;	/* no space in this map */
		tmp = entsize;
		if (map->base <= hdr->firstused)
			tmp += sizeof(xfs_attr_leaf_entry_t);
		if ((map->size >= tmp) && (tablesize <= hdr->firstused)) {
			tmp = xfs_attr_leaf_add_work(trans, bp, args, i);
			return(tmp);
		}
	}

	return(XFS_ERROR(ENOSPC));
}

/*
 * Add a name to a leaf attribute list structure.
 */
STATIC int
xfs_attr_leaf_add_work(xfs_trans_t *trans, buf_t *bp, xfs_da_args_t *args,
				   int mapindex)
{
	xfs_attr_leafblock_t *leaf;
	xfs_attr_leaf_hdr_t *hdr;
	xfs_attr_leaf_entry_t *entry;
	xfs_attr_leaf_name_local_t *name_loc;
	xfs_attr_leaf_name_remote_t *name_rmt;
	xfs_attr_leaf_map_t *map;
	xfs_mount_t *mp;
	int tmp, i;

	leaf = (xfs_attr_leafblock_t *)bp->b_un.b_addr;
	ASSERT(leaf->hdr.info.magic == XFS_ATTR_LEAF_MAGIC);
	hdr = &leaf->hdr;
	ASSERT((mapindex >= 0) && (mapindex < XFS_ATTR_LEAF_MAPSIZE));
	ASSERT((args->index >= 0) && (args->index <= hdr->count));

	/*
	 * Force open some space in the entry array and fill it in.
	 */
	entry = &leaf->entries[args->index];
	if (args->index < hdr->count) {
		tmp  = hdr->count - args->index;
		tmp *= sizeof(xfs_attr_leaf_entry_t);
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
	ASSERT((map->base & 0x3) == 0);
	ASSERT(map->size >= xfs_attr_leaf_newentsize(args,
				 trans->t_mountp->m_sb.sb_blocksize, NULL));
	ASSERT(map->size < XFS_LBSIZE(mp));
	ASSERT((map->size & 0x3) == 0);
	map->size -= xfs_attr_leaf_newentsize(args,
			  trans->t_mountp->m_sb.sb_blocksize, &tmp);
	entry->nameidx = map->base + map->size;
	entry->hashval = args->hashval;
	entry->flags = tmp ? XFS_ATTR_LOCAL : 0;
	entry->flags |= (args->flags & ATTR_ROOT) ? XFS_ATTR_ROOT : 0;
	if (args->rename) {
		entry->flags |= XFS_ATTR_INCOMPLETE;
		if ((args->blkno2 == args->blkno) &&
		    (args->index2 <= args->index)) {
			args->index2++;
		}
	}
	xfs_trans_log_buf(trans, bp, XFS_DA_LOGRANGE(leaf, entry,
							   sizeof(*entry)));

	/*
	 * Copy the attribute name and value into the new space.
	 *
	 * For "remote" attribute values, simply note that we need to 
	 * allocate space for the "remote" value.  We can't actually
	 * allocate the extents in this transaction, and we can't decide
	 * which blocks they should be as we might allocate more blocks
	 * as part of this transaction (a split operation for example).
	 */
	if (entry->flags & XFS_ATTR_LOCAL) {
		name_loc = XFS_ATTR_LEAF_NAME_LOCAL(leaf, args->index);
		name_loc->namelen = args->namelen;
		name_loc->valuelen = args->valuelen;
		bcopy(args->name, (char *)name_loc->nameval, args->namelen);
		bcopy(args->value, (char *)&name_loc->nameval[args->namelen],
				   name_loc->valuelen);
	} else {
		name_rmt = XFS_ATTR_LEAF_NAME_REMOTE(leaf, args->index);
		name_rmt->namelen = args->namelen;
		bcopy(args->name, (char *)name_rmt->name, args->namelen);
		entry->flags |= XFS_ATTR_INCOMPLETE;
		name_rmt->valuelen = 0;	/* just in case */
		name_rmt->valueblk = 0;
		args->rmtblkno = 1;
		args->rmtblkcnt = XFS_B_TO_FSB(mp, args->valuelen);
	}
	xfs_trans_log_buf(trans, bp,
	     XFS_DA_LOGRANGE(leaf, XFS_ATTR_LEAF_NAME(leaf, args->index),
				   xfs_attr_leaf_entsize(leaf, args->index)));

	/*
	 * Update the control info for this leaf node
	 */
	if (entry->nameidx < hdr->firstused)
		hdr->firstused = entry->nameidx;
	ASSERT(hdr->firstused >= ((hdr->count*sizeof(*entry))+sizeof(*hdr)));
	tmp = (hdr->count-1) * sizeof(xfs_attr_leaf_entry_t)
			+ sizeof(xfs_attr_leaf_hdr_t);
	map = &hdr->freemap[0];
	for (i = 0; i < XFS_ATTR_LEAF_MAPSIZE; map++, i++) {
		if (map->base == tmp) {
			map->base += sizeof(xfs_attr_leaf_entry_t);
			map->size -= sizeof(xfs_attr_leaf_entry_t);
		}
	}
	hdr->usedbytes += xfs_attr_leaf_entsize(leaf, args->index);
	xfs_trans_log_buf(trans, bp, XFS_DA_LOGRANGE(leaf, hdr, sizeof(*hdr)));
	return(0);
}

/*
 * Garbage collect a leaf attribute list block by copying it to a new buffer.
 */
STATIC void
xfs_attr_leaf_compact(xfs_trans_t *trans, buf_t *bp)
{
	xfs_attr_leafblock_t *leaf_s, *leaf_d;
	xfs_attr_leaf_hdr_t *hdr_s, *hdr_d;
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
	leaf_s = (xfs_attr_leafblock_t *)tmpbuffer;
	leaf_d = (xfs_attr_leafblock_t *)bp->b_un.b_addr;
	hdr_s = &leaf_s->hdr;
	hdr_d = &leaf_d->hdr;
	hdr_d->info = hdr_s->info;	/* struct copy */
	hdr_d->firstused = XFS_LBSIZE(mp);
	if (hdr_d->firstused == 0)
		hdr_d->firstused = XFS_LBSIZE(mp) - 1;
	hdr_d->usedbytes = 0;
	hdr_d->count = 0;
	hdr_d->holes = 0;
	hdr_d->freemap[0].base = sizeof(xfs_attr_leaf_hdr_t);
	hdr_d->freemap[0].size = hdr_d->firstused - hdr_d->freemap[0].base;

	/*
	 * Copy all entry's in the same (sorted) order,
	 * but allocate name/value pairs packed and in sequence.
	 */
    { int tmp;
	tmp = hdr_s->usedbytes;
	xfs_attr_leaf_moveents(leaf_s, 0, leaf_d, 0, (int)hdr_s->count, mp);
	ASSERT(hdr_s->usedbytes == 0);
	ASSERT(hdr_d->usedbytes == tmp);
    }

	xfs_trans_log_buf(trans, bp, 0, XFS_LBSIZE(mp) - 1);

	kmem_free(tmpbuffer, XFS_LBSIZE(mp));
}

/*
 * Redistribute the attribute list entries between two leaf nodes,
 * taking into account the size of the new entry.
 *
 * NOTE: if new block is empty, then it will get the upper half of the
 * old block.  At present, all (one) callers pass in an empty second block.
 *
 * This code adjusts the args->index/blkno and args->index2/blkno2 fields
 * to match what it is doing in splitting the attribute leaf block.  Those
 * values are used in "atomic rename" operations on attributes.  Note that
 * the "new" and "old" values can end up in different blocks.
 */
STATIC void
xfs_attr_leaf_rebalance(xfs_da_state_t *state, xfs_da_state_blk_t *blk1,
				       xfs_da_state_blk_t *blk2)
{
	xfs_da_state_blk_t *tmp_blk;
	xfs_attr_leafblock_t *leaf1, *leaf2;
	xfs_attr_leaf_hdr_t *hdr1, *hdr2;
	int count, totallen, max, space, swap;

	/*
	 * Set up environment.
	 */
	ASSERT(blk1->magic == XFS_ATTR_LEAF_MAGIC);
	ASSERT(blk2->magic == XFS_ATTR_LEAF_MAGIC);
	leaf1 = (xfs_attr_leafblock_t *)blk1->bp->b_un.b_addr;
	leaf2 = (xfs_attr_leafblock_t *)blk2->bp->b_un.b_addr;
	ASSERT(leaf1->hdr.info.magic == XFS_ATTR_LEAF_MAGIC);
	ASSERT(leaf2->hdr.info.magic == XFS_ATTR_LEAF_MAGIC);

	/*
	 * Check ordering of blocks, reverse if it makes things simpler.
	 *
	 * NOTE: Given that all (current) callers pass in an empty
	 * second block, this code should never set "swap".
	 */
	swap = 0;
	if (xfs_attr_leaf_order(blk1->bp, blk2->bp)) {
		tmp_blk = blk1;
		blk1 = blk2;
		blk2 = tmp_blk;
		leaf1 = (xfs_attr_leafblock_t *)blk1->bp->b_un.b_addr;
		leaf2 = (xfs_attr_leafblock_t *)blk2->bp->b_un.b_addr;
		swap = 1;
	}
	hdr1 = &leaf1->hdr;
	hdr2 = &leaf2->hdr;

	/*
	 * Examine entries until we reduce the absolute difference in
	 * byte usage between the two blocks to a minimum.  Then get
	 * the direction to copy and the number of elements to move.
	 *
	 * "inleaf" is true if the new entry should be inserted into blk1.
	 * If "swap" is also true, then reverse the sense of "inleaf".
	 */
	state->inleaf = xfs_attr_leaf_figure_balance(state, blk1, blk2,
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
		space  = hdr1->usedbytes - totallen;
		space += count * sizeof(xfs_attr_leaf_entry_t);

		/*
		 * leaf2 is the destination, compact it if it looks tight.
		 */
		max  = hdr2->firstused - sizeof(xfs_attr_leaf_hdr_t);
		max -= hdr2->count * sizeof(xfs_attr_leaf_entry_t);
		if (space > max) {
			xfs_attr_leaf_compact(state->trans, blk2->bp);
		}

		/*
		 * Move high entries from leaf1 to low end of leaf2.
		 */
		xfs_attr_leaf_moveents(leaf1, hdr1->count - count,
					     leaf2, 0, count, state->mp);

		xfs_trans_log_buf(state->trans, blk1->bp, 0, state->blocksize-1);
		xfs_trans_log_buf(state->trans, blk2->bp, 0, state->blocksize-1);
	} else if (count > hdr1->count) {
		/*
		 * Figure the total bytes to be added to the destination leaf.
		 */
		count -= hdr1->count;		/* number entries being moved */
		space  = totallen - hdr1->usedbytes;
		space += count * sizeof(xfs_attr_leaf_entry_t);

		/*
		 * leaf1 is the destination, compact it if it looks tight.
		 */
		max  = hdr1->firstused - sizeof(xfs_attr_leaf_hdr_t);
		max -= hdr1->count * sizeof(xfs_attr_leaf_entry_t);
		if (space > max) {
			xfs_attr_leaf_compact(state->trans, blk1->bp);
		}

		/*
		 * Move low entries from leaf2 to high end of leaf1.
		 */
		xfs_attr_leaf_moveents(leaf2, 0, leaf1, (int)hdr1->count,
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
	 * NOTE: this code depends on the (current) situation that the
	 * second block was originally empty.
	 *
	 * If the insertion point moved to the 2nd block, we must adjust
	 * the index.  We must also track the entry just following the
	 * new entry for use in an "atomic rename" operation, that entry
	 * is always the "old" entry and the "new" entry is what we are
	 * inserting.  The index/blkno fields refer to the "old" entry,
	 * while the index2/blkno2 fields refer to the "new" entry.
	 */
	if (blk1->index > leaf1->hdr.count) {
		ASSERT(state->inleaf == 0);
		blk2->index = blk1->index - leaf1->hdr.count;
		state->args->index = state->args->index2 = blk2->index;
		state->args->blkno = state->args->blkno2 = blk2->blkno;
	} else if (blk1->index == leaf1->hdr.count) {
		if (state->inleaf) {
			state->args->index = blk1->index;
			state->args->blkno = blk1->blkno;
			state->args->index2 = 0;
			state->args->blkno2 = blk2->blkno;
		} else {
			blk2->index = blk1->index - leaf1->hdr.count;
			state->args->index = state->args->index2 = blk2->index;
			state->args->blkno = state->args->blkno2 = blk2->blkno;
		}
	} else {
		ASSERT(state->inleaf == 1);
		state->args->index = state->args->index2 = blk1->index;
		state->args->blkno = state->args->blkno2 = blk1->blkno;
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
xfs_attr_leaf_figure_balance(xfs_da_state_t *state,
				    xfs_da_state_blk_t *blk1,
				    xfs_da_state_blk_t *blk2,
				    int *countarg, int *usedbytesarg)
{
	xfs_attr_leafblock_t *leaf1, *leaf2;
	xfs_attr_leaf_hdr_t *hdr1, *hdr2;
	xfs_attr_leaf_entry_t *entry;
	int count, max, index, totallen, half;
	int lastdelta, foundit, tmp;

	/*
	 * Set up environment.
	 */
	leaf1 = (xfs_attr_leafblock_t *)blk1->bp->b_un.b_addr;
	leaf2 = (xfs_attr_leafblock_t *)blk2->bp->b_un.b_addr;
	hdr1 = &leaf1->hdr;
	hdr2 = &leaf2->hdr;
	foundit = 0;
	totallen = 0;

	/*
	 * Examine entries until we reduce the absolute difference in
	 * byte usage between the two blocks to a minimum.
	 */
	max = hdr1->count + hdr2->count;
	half  = (max+1) * sizeof(*entry);
	half += hdr1->usedbytes + hdr2->usedbytes + 
		    xfs_attr_leaf_newentsize(state->args,
					     state->blocksize, NULL);
	half /= 2;
	lastdelta = state->blocksize;
	entry = &leaf1->entries[0];
	for (count = index = 0; count < max; entry++, index++, count++) {

#define XFS_ATTR_ABS(A)	(((A) < 0) ? -(A) : (A))
		/*
		 * The new entry is in the first block, account for it.
		 */
		if (count == blk1->index) {
			tmp = totallen + sizeof(*entry) +
				xfs_attr_leaf_newentsize(state->args,
							 state->blocksize,
							 NULL);
			if (XFS_ATTR_ABS(half - tmp) > lastdelta)
				break;
			lastdelta = XFS_ATTR_ABS(half - tmp);
			totallen = tmp;
			foundit = 1;
		}

		/*
		 * Wrap around into the second block if necessary.
		 */
		if (count == hdr1->count) {
			leaf1 = leaf2;
			entry = &leaf1->entries[0];
			index = 0;
		}

		/*
		 * Figure out if next leaf entry would be too much.
		 */
		tmp = totallen + sizeof(*entry) + xfs_attr_leaf_entsize(leaf1,
									index);
		if (XFS_ATTR_ABS(half - tmp) > lastdelta)
			break;
		lastdelta = XFS_ATTR_ABS(half - tmp);
		totallen = tmp;
#undef XFS_ATTR_ABS
	}

	/*
	 * Calculate the number of usedbytes that will end up in lower block.
	 * If new entry not in lower block, fix up the count.
	 */
	totallen -= count * sizeof(*entry);
	if (foundit) {
		totallen -= sizeof(*entry) + 
				xfs_attr_leaf_newentsize(state->args,
							 state->blocksize,
							 NULL);
	}

	*countarg = count;
	*usedbytesarg = totallen;
	return(foundit);
}

/*========================================================================
 * Routines used for shrinking the Btree.
 *========================================================================*/

/*
 * Check a leaf block and its neighbors to see if the block should be
 * collapsed into one or the other neighbor.  Always keep the block
 * with the smaller block number.
 * If the current block is over 50% full, don't try to join it, return 0.
 * If the block is empty, fill in the state structure and return 2.
 * If it can be collapsed, fill in the state structure and return 1.
 * If nothing can be done, return 0.
 *
 * GROT: allow for INCOMPLETE entries in calculation.
 */
int
xfs_attr_leaf_toosmall(xfs_da_state_t *state, int *action)
{
	xfs_attr_leafblock_t *leaf;
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
	ASSERT(info->magic == XFS_ATTR_LEAF_MAGIC);
	leaf = (xfs_attr_leafblock_t *)info;
	count = leaf->hdr.count;
	bytes = sizeof(xfs_attr_leaf_hdr_t) +
		count * sizeof(xfs_attr_leaf_entry_t) +
		leaf->hdr.usedbytes;
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
	 * to shrink an attribute list over time.
	 */
	forward = (info->forw < info->back);	/* start with smaller blk num */
	for (i = 0; i < 2; forward = !forward, i++) {
		if (forward)
			blkno = info->forw;
		else
			blkno = info->back;
		if (blkno == 0)
			continue;
		error = xfs_da_read_buf(state->trans, state->args->dp, blkno,
						      &bp, XFS_ATTR_FORK);
		if (error)
			return(error);
		ASSERT(bp != NULL);

		leaf = (xfs_attr_leafblock_t *)info;
		count  = leaf->hdr.count;
		bytes  = state->blocksize - (state->blocksize>>2);
		bytes -= leaf->hdr.usedbytes;
		leaf = (xfs_attr_leafblock_t *)bp->b_un.b_addr;
		ASSERT(leaf->hdr.info.magic == XFS_ATTR_LEAF_MAGIC);
		count += leaf->hdr.count;
		bytes -= leaf->hdr.usedbytes;
		bytes -= count * sizeof(xfs_attr_leaf_entry_t);
		bytes -= sizeof(xfs_attr_leaf_hdr_t);
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
 * Remove a name from the leaf attribute list structure.
 *
 * Return 1 if leaf is less than 37% full, 0 if >= 37% full.
 * If two leaves are 37% full, when combined they will leave 25% free.
 */
int
xfs_attr_leaf_remove(xfs_trans_t *trans, buf_t *bp, xfs_da_args_t *args)
{
	xfs_attr_leafblock_t *leaf;
	xfs_attr_leaf_hdr_t *hdr;
	xfs_attr_leaf_map_t *map;
	xfs_attr_leaf_entry_t *entry;
	int before, after, smallest, entsize;
	int tablesize, tmp, i;
	xfs_mount_t *mp;

	leaf = (xfs_attr_leafblock_t *)bp->b_un.b_addr;
	ASSERT(leaf->hdr.info.magic == XFS_ATTR_LEAF_MAGIC);
	hdr = &leaf->hdr;
	mp = trans->t_mountp;
	ASSERT((hdr->count > 0) && (hdr->count < (XFS_LBSIZE(mp)/8)));
	ASSERT((args->index >= 0) && (args->index < hdr->count));
	ASSERT(hdr->firstused >= ((hdr->count*sizeof(*entry))+sizeof(*hdr)));
	entry = &leaf->entries[args->index];
	ASSERT(entry->nameidx >= hdr->firstused);
	ASSERT(entry->nameidx < XFS_LBSIZE(mp));

	/*
	 * Scan through free region table:
	 *    check for adjacency of free'd entry with an existing one,
	 *    find smallest free region in case we need to replace it,
	 *    adjust any map that borders the entry table,
	 */
	tablesize = hdr->count * sizeof(xfs_attr_leaf_entry_t)
			+ sizeof(xfs_attr_leaf_hdr_t);
	map = &hdr->freemap[0];
	tmp = map->size;
	before = after = -1;
	smallest = XFS_ATTR_LEAF_MAPSIZE - 1;
	entsize = xfs_attr_leaf_entsize(leaf, args->index);
	for (i = 0; i < XFS_ATTR_LEAF_MAPSIZE; map++, i++) {
		ASSERT(map->base < XFS_LBSIZE(mp));
		ASSERT(map->size < XFS_LBSIZE(mp));
		if (map->base == tablesize) {
			map->base -= sizeof(xfs_attr_leaf_entry_t);
			map->size += sizeof(xfs_attr_leaf_entry_t);
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
	bzero(XFS_ATTR_LEAF_NAME(leaf, args->index), entsize);
	hdr->usedbytes -= entsize;
	xfs_trans_log_buf(trans, bp,
	     XFS_DA_LOGRANGE(leaf, XFS_ATTR_LEAF_NAME(leaf, args->index),
				   entsize));

	tmp = (hdr->count - args->index) * sizeof(xfs_attr_leaf_entry_t);
	bcopy((char *)(entry+1), (char *)entry, tmp);
	hdr->count--;
	xfs_trans_log_buf(trans, bp,
	    XFS_DA_LOGRANGE(leaf, entry, tmp + sizeof(*entry)));
	entry = &leaf->entries[hdr->count];
	bzero((char *)entry, sizeof(xfs_attr_leaf_entry_t));

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
	tmp  = sizeof(xfs_attr_leaf_hdr_t);
	tmp += leaf->hdr.count * sizeof(xfs_attr_leaf_entry_t);
	tmp += leaf->hdr.usedbytes;
	return(tmp < (XFS_LBSIZE(mp)*37/100)); /* leaf is < 37% full */
}

/*
 * Move all the attribute list entries from drop_leaf into save_leaf.
 */
void
xfs_attr_leaf_unbalance(xfs_da_state_t *state, xfs_da_state_blk_t *drop_blk,
				       xfs_da_state_blk_t *save_blk)
{
	xfs_attr_leafblock_t *drop_leaf, *save_leaf, *tmp_leaf;
	xfs_attr_leaf_hdr_t *drop_hdr, *save_hdr, *tmp_hdr;
	xfs_mount_t *mp;
	char *tmpbuffer;

	/*
	 * Set up environment.
	 */
	mp = state->mp;
	ASSERT(drop_blk->magic == XFS_ATTR_LEAF_MAGIC);
	ASSERT(save_blk->magic == XFS_ATTR_LEAF_MAGIC);
	drop_leaf = (xfs_attr_leafblock_t *)drop_blk->bp->b_un.b_addr;
	save_leaf = (xfs_attr_leafblock_t *)save_blk->bp->b_un.b_addr;
	ASSERT(drop_leaf->hdr.info.magic == XFS_ATTR_LEAF_MAGIC);
	ASSERT(save_leaf->hdr.info.magic == XFS_ATTR_LEAF_MAGIC);
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
		if (xfs_attr_leaf_order(save_blk->bp, drop_blk->bp)) {
			xfs_attr_leaf_moveents(drop_leaf, 0, save_leaf, 0,
						 (int)drop_hdr->count, mp);
		} else {
			xfs_attr_leaf_moveents(drop_leaf, 0,
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
		tmp_leaf = (xfs_attr_leafblock_t *)tmpbuffer;
		tmp_hdr = &tmp_leaf->hdr;
		tmp_hdr->info = save_hdr->info;	/* struct copy */
		tmp_hdr->count = 0;
		tmp_hdr->firstused = state->blocksize;
		if (tmp_hdr->firstused == 0)
			tmp_hdr->firstused = state->blocksize - 1;
		tmp_hdr->usedbytes = 0;
		if (xfs_attr_leaf_order(save_blk->bp, drop_blk->bp)) {
			xfs_attr_leaf_moveents(drop_leaf, 0, tmp_leaf, 0,
						 (int)drop_hdr->count, mp);
			xfs_attr_leaf_moveents(save_leaf, 0,
					      tmp_leaf, tmp_leaf->hdr.count,
					      (int)save_hdr->count, mp);
		} else {
			xfs_attr_leaf_moveents(save_leaf, 0, tmp_leaf, 0,	
						 (int)save_hdr->count, mp);
			xfs_attr_leaf_moveents(drop_leaf, 0,
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

/*========================================================================
 * Routines used for finding things in the Btree.
 *========================================================================*/

/*
 * Look up a name in a leaf attribute list structure.
 * This is the internal routine, it uses the caller's buffer.
 *
 * Note that duplicate keys are allowed, but only check within the
 * current leaf node.  The Btree code must check in adjacent leaf nodes.
 *
 * Return in args->index the index into the entry[] array of either
 * the found entry, or where the entry should have been (insert before
 * that entry).
 *
 * Don't change the args->value unless we find the attribute.
 */
int
xfs_attr_leaf_lookup_int(buf_t *bp, xfs_da_args_t *args)
{
	xfs_attr_leafblock_t *leaf;
	xfs_attr_leaf_entry_t *entry;
	xfs_attr_leaf_name_local_t *name_loc;
	xfs_attr_leaf_name_remote_t *name_rmt;
	int probe, span;
	uint hashval;

	leaf = (xfs_attr_leafblock_t *)bp->b_un.b_addr;
	ASSERT(leaf->hdr.info.magic == XFS_ATTR_LEAF_MAGIC);
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
		args->index = probe;
		return(ENOATTR);
	}

	/*
	 * Duplicate keys may be present, so search all of them for a match.
	 */
	for (  ; (probe < leaf->hdr.count) && (entry->hashval == hashval);
			entry++, probe++) {
/*
 * GROT: Add code to remove incomplete entries.
 */
		/*
		 * If we are looking for INCOMPLETE entries, show only those.
		 * If we are looking for complete entries, show only those.
		 */
		if ((args->flags & XFS_ATTR_INCOMPLETE) != 
		    (entry->flags & XFS_ATTR_INCOMPLETE)) {
			continue;
		}
		if (entry->flags & XFS_ATTR_LOCAL) {
			name_loc = XFS_ATTR_LEAF_NAME_LOCAL(leaf, probe);
			if (name_loc->namelen != args->namelen)
				continue;
			if (bcmp(args->name, (char *)name_loc->nameval,
					     args->namelen) != 0)
				continue;
			if (((args->flags & ATTR_ROOT) != 0) !=
			    ((entry->flags & XFS_ATTR_ROOT) != 0))
				continue;
			args->index = probe;
			return(EEXIST);
		} else {
			name_rmt = XFS_ATTR_LEAF_NAME_REMOTE(leaf, probe);
			if (name_rmt->namelen != args->namelen)
				continue;
			if (bcmp(args->name, (char *)name_rmt->name,
					     args->namelen) != 0)
				continue;
			if (((args->flags & ATTR_ROOT) != 0) !=
			    ((entry->flags & XFS_ATTR_ROOT) != 0))
				continue;
			args->index = probe;
			args->rmtblkno = name_rmt->valueblk;
			args->rmtblkcnt = XFS_B_TO_FSB(args->dp->i_mount,
						       name_rmt->valuelen);
			return(EEXIST);
		}
	}
	args->index = probe;
	return(ENOATTR);
}

/*
 * Get the value associated with an attribute name from a leaf attribute
 * list structure.
 */
int
xfs_attr_leaf_getvalue(buf_t *bp, xfs_da_args_t *args)
{
	xfs_attr_leafblock_t *leaf;
	xfs_attr_leaf_entry_t *entry;
	xfs_attr_leaf_name_local_t *name_loc;
	xfs_attr_leaf_name_remote_t *name_rmt;

	leaf = (xfs_attr_leafblock_t *)bp->b_un.b_addr;
	ASSERT(leaf->hdr.info.magic == XFS_ATTR_LEAF_MAGIC);
	ASSERT((((int)leaf->hdr.count) >= 0) && \
	       (leaf->hdr.count < (XFS_LBSIZE(args->dp->i_mount)/8)));
	ASSERT(args->index < ((int)leaf->hdr.count));

	entry = &leaf->entries[args->index];
	if (entry->flags & XFS_ATTR_LOCAL) {
		name_loc = XFS_ATTR_LEAF_NAME_LOCAL(leaf, args->index);
		ASSERT(name_loc->namelen == args->namelen);
		ASSERT(bcmp(args->name, name_loc->nameval, args->namelen) == 0);
		if (args->valuelen < name_loc->valuelen) {
			args->valuelen = name_loc->valuelen;
			return(E2BIG);
		}
		args->valuelen = name_loc->valuelen;
		bcopy(&name_loc->nameval[args->namelen], args->value,
							 args->valuelen);
	} else {
		name_rmt = XFS_ATTR_LEAF_NAME_REMOTE(leaf, args->index);
		ASSERT(name_rmt->namelen == args->namelen);
		ASSERT(bcmp(args->name, name_rmt->name, args->namelen) == 0);
		args->rmtblkno = name_rmt->valueblk;
		args->rmtblkcnt = XFS_B_TO_FSB(args->dp->i_mount,
					       name_rmt->valuelen);
		if (args->valuelen < name_rmt->valuelen) {
			args->valuelen = name_rmt->valuelen;
			return(E2BIG);
		}
		args->valuelen = name_rmt->valuelen;
	}
	return(0);
}

/*========================================================================
 * Utility routines.
 *========================================================================*/

/*
 * Move the indicated entries from one leaf to another.
 * NOTE: this routine modifies both source and destination leaves.
 */
/*ARGSUSED*/
STATIC void
xfs_attr_leaf_moveents(xfs_attr_leafblock_t *leaf_s, int start_s,
			xfs_attr_leafblock_t *leaf_d, int start_d,
			int count, xfs_mount_t *mp)
{
	xfs_attr_leaf_hdr_t *hdr_s, *hdr_d;
	xfs_attr_leaf_entry_t *entry_s, *entry_d;
	int tmp, i;

	/*
	 * Check for nothing to do.
	 */
	if (count == 0)
		return;

	/*
	 * Set up environment.
	 */
	ASSERT(leaf_s->hdr.info.magic == XFS_ATTR_LEAF_MAGIC);
	ASSERT(leaf_d->hdr.info.magic == XFS_ATTR_LEAF_MAGIC);
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
		tmp *= sizeof(xfs_attr_leaf_entry_t);
		entry_s = &leaf_d->entries[start_d];
		entry_d = &leaf_d->entries[start_d + count];
		bcopy((char *)entry_s, (char *)entry_d, tmp);
	}

	/*
	 * Copy all entry's in the same (sorted) order,
	 * but allocate attribute info packed and in sequence.
	 */
	entry_s = &leaf_s->entries[start_s];
	entry_d = &leaf_d->entries[start_d];
	for (i = 0; i < count; entry_s++, entry_d++, i++) {
		ASSERT(entry_s->nameidx >= hdr_s->firstused);
		tmp = xfs_attr_leaf_entsize(leaf_s, start_s + i);
		if (entry_s->flags & XFS_ATTR_INCOMPLETE) { /* skip partials? */
			bzero(XFS_ATTR_LEAF_NAME(leaf_s, start_s + i), tmp);
			hdr_s->usedbytes -= tmp;
			hdr_s->count--;
			entry_d--;	/* to compensate for ++ in loop hdr */
		} else {
			hdr_d->firstused -= tmp;
			entry_d->hashval = entry_s->hashval;
			entry_d->nameidx = hdr_d->firstused;
			entry_d->flags = entry_s->flags;
			ASSERT(entry_d->nameidx + tmp <= XFS_LBSIZE(mp));
			bcopy(XFS_ATTR_LEAF_NAME(leaf_s, start_s + i),
			      XFS_ATTR_LEAF_NAME(leaf_d, start_d + i), tmp);
			ASSERT(entry_s->nameidx + tmp <= XFS_LBSIZE(mp));
			bzero(XFS_ATTR_LEAF_NAME(leaf_s, start_s + i), tmp);
			hdr_s->usedbytes -= tmp;
			hdr_d->usedbytes += tmp;
			hdr_s->count--;
			hdr_d->count++;
			tmp = hdr_d->count * sizeof(xfs_attr_leaf_entry_t)
					   + sizeof(xfs_attr_leaf_hdr_t);
			ASSERT(hdr_d->firstused >= tmp);
		}

	}

	/*
	 * Zero out the entries we just copied.
	 */
	if (start_s == hdr_s->count) {
		tmp = count * sizeof(xfs_attr_leaf_entry_t);
		entry_s = &leaf_s->entries[start_s];
		ASSERT(((char *)entry_s + tmp) <=
		       ((char *)leaf_s + XFS_LBSIZE(mp)));
		bzero((char *)entry_s, tmp);
	} else {
		/*
		 * Move the remaining entries down to fill the hole,
		 * then zero the entries at the top.
		 */
		tmp  = hdr_s->count - count;
		tmp *= sizeof(xfs_attr_leaf_entry_t);
		entry_s = &leaf_s->entries[start_s + count];
		entry_d = &leaf_s->entries[start_s];
		bcopy((char *)entry_s, (char *)entry_d, tmp);

		tmp = count * sizeof(xfs_attr_leaf_entry_t);
		entry_s = &leaf_s->entries[hdr_s->count];
		ASSERT(((char *)entry_s + tmp) <=
		       ((char *)leaf_s + XFS_LBSIZE(mp)));
		bzero((char *)entry_s, tmp);
	}

	/*
	 * Fill in the freemap information
	 */
	hdr_d->freemap[0].base = sizeof(xfs_attr_leaf_hdr_t);
	hdr_d->freemap[0].base += hdr_d->count*sizeof(xfs_attr_leaf_entry_t);
	hdr_d->freemap[0].size = hdr_d->firstused - hdr_d->freemap[0].base;
	hdr_d->freemap[1].base = hdr_d->freemap[2].base = 0;
	hdr_d->freemap[1].size = hdr_d->freemap[2].size = 0;
	hdr_s->holes = 1;	/* leaf may not be compact */
}

/*
 * Compare two leaf blocks "order".
 * Return 0 unless leaf2 should go before leaf1.
 */
int
xfs_attr_leaf_order(buf_t *leaf1_bp, buf_t *leaf2_bp)
{
	xfs_attr_leafblock_t *leaf1, *leaf2;

	leaf1 = (xfs_attr_leafblock_t *)leaf1_bp->b_un.b_addr;
	leaf2 = (xfs_attr_leafblock_t *)leaf2_bp->b_un.b_addr;
	ASSERT((leaf1->hdr.info.magic == XFS_ATTR_LEAF_MAGIC) && \
	       (leaf2->hdr.info.magic == XFS_ATTR_LEAF_MAGIC));
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
xfs_attr_leaf_lasthash(buf_t *bp, int *count)
{
	xfs_attr_leafblock_t *leaf;

	leaf = (xfs_attr_leafblock_t *)bp->b_un.b_addr;
	ASSERT(leaf->hdr.info.magic == XFS_ATTR_LEAF_MAGIC);
	if (count) {
		*count = leaf->hdr.count;
	}
	return(leaf->entries[ leaf->hdr.count-1 ].hashval);
}

/*
 * Calculate the number of bytes used to store the indicated attribute
 * (whether local or remote only calculate bytes in this block).
 */
int
xfs_attr_leaf_entsize(xfs_attr_leafblock_t *leaf, int index)
{
	xfs_attr_leaf_name_local_t *name_loc;
	xfs_attr_leaf_name_remote_t *name_rmt;
	int size;

	ASSERT(leaf->hdr.info.magic == XFS_ATTR_LEAF_MAGIC);
	if (leaf->entries[index].flags & XFS_ATTR_LOCAL) {
		name_loc = XFS_ATTR_LEAF_NAME_LOCAL(leaf, index);
		size = XFS_ATTR_LEAF_ENTSIZE_LOCAL(name_loc->namelen,
						   name_loc->valuelen);
	} else {
		name_rmt = XFS_ATTR_LEAF_NAME_REMOTE(leaf, index);
		size = XFS_ATTR_LEAF_ENTSIZE_REMOTE(name_rmt->namelen);
	}
	return(size);
}

/*
 * Calculate the number of bytes that would be required to store the new
 * attribute (whether local or remote only calculate bytes in this block).
 * This routine decides as a side effect whether the attribute will be
 * a "local" or a "remote" attribute.
 */
int
xfs_attr_leaf_newentsize(xfs_da_args_t *args, int blocksize, int *local)
{
	int size;

	if (args->valuelen < XFS_ATTR_LEAF_ENTSIZE_LOCAL_MAX(blocksize)) { 
		size = XFS_ATTR_LEAF_ENTSIZE_LOCAL(args->namelen,
						   args->valuelen);
		if (local) {
			*local = 1;
		}
	} else {
		size = XFS_ATTR_LEAF_ENTSIZE_REMOTE(args->namelen);
		if (local) {
			*local = 0;
		}
	}
	return(size);
}

/*
 * Copy out attribute list entries for attr_list(), for leaf attribute lists.
 */
int
xfs_attr_leaf_list_int(buf_t *bp, attrlist_t *alist, int flags,
			     attrlist_cursor_kern_t *cursor)
{
	xfs_attr_leafblock_t *leaf;
	xfs_attr_leaf_entry_t *entry;
	xfs_attr_leaf_name_local_t *name_loc;
	xfs_attr_leaf_name_remote_t *name_rmt;
	int retval, i;

	ASSERT(bp != NULL);
	leaf = (xfs_attr_leafblock_t *)bp->b_un.b_addr;
	ASSERT(leaf->hdr.info.magic == XFS_ATTR_LEAF_MAGIC);
	if (cursor->index >= leaf->hdr.count)
		cursor->index = 0;
	cursor->initted = 1;
	entry = &leaf->entries[ cursor->index ];
	retval = 0;
	for (i = cursor->index; i < leaf->hdr.count; entry++, i++) {
		if (entry->hashval < cursor->hashval)
			continue;
		if (entry->flags & XFS_ATTR_INCOMPLETE)
			continue;		/* skip incomplete entries */
		cursor->hashval = entry->hashval;
		cursor->index = i;
/* GROT: check this changed-attr-list recovery code */
		if (((flags & ATTR_ROOT) != 0) !=
		    ((entry->flags & XFS_ATTR_ROOT) != 0))
			continue;

		if (entry->flags & XFS_ATTR_LOCAL) {
			name_loc = XFS_ATTR_LEAF_NAME_LOCAL(leaf, i);
			retval = xfs_attr_put_listent(alist,
					(char *)name_loc->nameval,
					(int)name_loc->namelen, 
					(int)name_loc->valuelen, cursor);
		} else {
			name_rmt = XFS_ATTR_LEAF_NAME_REMOTE(leaf, i);
			retval = xfs_attr_put_listent(alist,
					(char *)name_rmt->name,
					(int)name_rmt->namelen, 
					(int)name_rmt->valuelen, cursor);
		}
		if (retval)
			break;
	}
	return(retval);
}

#define	ATTR_ENTBASESIZE		/* minimum bytes used by an attr */ \
	(((struct attrlist_ent *) 0)->a_name - (char *) 0)
#define	ATTR_ENTSIZE(namelen)		/* actual bytes used by an attr */ \
	((ATTR_ENTBASESIZE + (namelen) + 1 + sizeof(u_int32_t)-1) \
	 & ~(sizeof(u_int32_t)-1))

/*
 * Format an attribute and copy it out the the user's buffer.
 */
/*ARGSUSED*/
int
xfs_attr_put_listent(attrlist_t *alist, char *name, int namelen, int valuelen,
				attrlist_cursor_kern_t *cursor)
{
	attrlist_ent_t *aep;
	int firstused, arraytop;

	ASSERT(alist->al_count >= 0);
	ASSERT(alist->al_count < (ATTR_MAX_VALUELEN/8));
	if (alist->al_count == 0) {
		firstused = alist->al_offset[ 0 ];
	} else {
		firstused = alist->al_offset[ alist->al_count-1 ];
	}
	ASSERT(firstused > sizeof(*alist));
	ASSERT(firstused <= alist->al_offset[0]);
	arraytop = sizeof(*alist) +
		   (alist->al_count-1) * sizeof(alist->al_offset[0]);
	ASSERT(arraytop > 0);
	ASSERT(arraytop <= firstused);
	firstused -= ATTR_ENTSIZE(namelen);
	if (firstused <= arraytop) {
		alist->al_more = 1;
		return(1);
	}
	ASSERT(firstused > 0);
	aep = (attrlist_ent_t *)&(((char *)alist)[firstused]);
	aep->a_valuelen = valuelen;
	bcopy(name, aep->a_name, namelen);
	aep->a_name[ namelen ] = 0;
	alist->al_offset[ alist->al_count++ ] = firstused;
	return(0);
}

/*
 * Clear the INCOMPLETE flag on an entry in a leaf block.
 */
int
xfs_attr_leaf_clearflag(xfs_da_args_t *args)
{
	xfs_attr_leafblock_t *leaf;
	xfs_attr_leaf_entry_t *entry;
	xfs_attr_leaf_name_remote_t *name_rmt;
	xfs_trans_t *trans;
	xfs_inode_t *dp;
	buf_t *bp;
	int error;
#ifdef DEBUG
	xfs_attr_leaf_name_local_t *name_loc;
	int namelen;
	char *name;
#endif /* DEBUG */

	/*
	 * Set up the transaction envelope.
	 */
	dp = args->dp;
	trans = xfs_trans_alloc(dp->i_mount, XFS_TRANS_ATTR_FLAG);
	if (error = xfs_trans_reserve(trans, 0,
				      XFS_ATTRFLAG_LOG_RES(dp->i_mount),
				      0, XFS_TRANS_PERM_LOG_RES,
				      XFS_ATTRFLAG_LOG_COUNT)) {
		xfs_trans_cancel(trans, XFS_TRANS_RELEASE_LOG_RES);
		return(error);
	}
	error = xfs_da_read_buf(trans, dp, args->blkno, &bp,
				       XFS_ATTR_FORK);
	if (error) {
		xfs_trans_cancel(trans, XFS_TRANS_RELEASE_LOG_RES);
		return(error);
	}
	ASSERT(bp != NULL);

	leaf = (xfs_attr_leafblock_t *)bp->b_un.b_addr;
	ASSERT(leaf->hdr.info.magic == XFS_ATTR_LEAF_MAGIC);
	ASSERT(args->index < leaf->hdr.count);
	ASSERT(args->index >= 0);
	entry = &leaf->entries[ args->index ];
	ASSERT(entry->flags & XFS_ATTR_INCOMPLETE);

#ifdef DEBUG
	if (entry->flags & XFS_ATTR_LOCAL) {
		name_loc = XFS_ATTR_LEAF_NAME_LOCAL(leaf, args->index);
		namelen = name_loc->namelen;
		name = (char *)name_loc->nameval;
	} else {
		name_rmt = XFS_ATTR_LEAF_NAME_REMOTE(leaf, args->index);
		namelen = name_rmt->namelen;
		name = (char *)name_rmt->name;
	}
	ASSERT(entry->hashval == args->hashval);
	ASSERT(namelen == args->namelen);
	ASSERT(bcmp(name, args->name, namelen) == 0);
#endif /* DEBUG */

	entry->flags &= ~XFS_ATTR_INCOMPLETE;
	xfs_trans_log_buf(trans, bp,
			 XFS_DA_LOGRANGE(leaf, entry, sizeof(*entry)));

	if (args->rmtblkno) {
		ASSERT((entry->flags & XFS_ATTR_LOCAL) == 0);
		name_rmt = XFS_ATTR_LEAF_NAME_REMOTE(leaf, args->index);
		name_rmt->valueblk = args->rmtblkno;
		name_rmt->valuelen = args->valuelen;
		xfs_trans_log_buf(trans, bp,
			 XFS_DA_LOGRANGE(leaf, name_rmt, sizeof(*name_rmt)));
	}

	xfs_trans_commit(trans, XFS_TRANS_RELEASE_LOG_RES);
	return(error);
}

/*
 * Set the INCOMPLETE flag on an entry in a leaf block.
 */
int
xfs_attr_leaf_setflag(xfs_da_args_t *args)
{
	xfs_attr_leafblock_t *leaf;
	xfs_attr_leaf_entry_t *entry;
	xfs_trans_t *trans;
	xfs_inode_t *dp;
	buf_t *bp;
	int error;

	/*
	 * Set up the transaction envelope.
	 */
	dp = args->dp;
	trans = xfs_trans_alloc(dp->i_mount, XFS_TRANS_ATTR_FLAG);
	if (error = xfs_trans_reserve(trans, 0,
				      XFS_ATTRFLAG_LOG_RES(dp->i_mount),
				      0, XFS_TRANS_PERM_LOG_RES,
				      XFS_ATTRFLAG_LOG_COUNT)) {
		xfs_trans_cancel(trans, XFS_TRANS_RELEASE_LOG_RES);
		return(error);
	}
	error = xfs_da_read_buf(trans, dp, args->blkno, &bp,
				       XFS_ATTR_FORK);
	if (error) {
		xfs_trans_cancel(trans, XFS_TRANS_RELEASE_LOG_RES);
		return(error);
	}
	ASSERT(bp != NULL);

	leaf = (xfs_attr_leafblock_t *)bp->b_un.b_addr;
	ASSERT(leaf->hdr.info.magic == XFS_ATTR_LEAF_MAGIC);
	ASSERT(args->index < leaf->hdr.count);
	ASSERT(args->index >= 0);
	entry = &leaf->entries[ args->index ];

	ASSERT((entry->flags & XFS_ATTR_INCOMPLETE) == 0);
	entry->flags |= XFS_ATTR_INCOMPLETE;

	xfs_trans_log_buf(trans, bp,
			 XFS_DA_LOGRANGE(leaf, entry, sizeof(*entry)));

	xfs_trans_commit(trans, XFS_TRANS_RELEASE_LOG_RES);
	return(error);
}

/*
 * In a single transaction, clear the INCOMPLETE flag on the leaf entry
 * given by args->blkno/index and set the INCOMPLETE flag on the leaf
 * entry given by args->blkno2/index2.
 *
 * Note that they could be in different blocks, or in the same block.
 */
int
xfs_attr_leaf_flipflags(xfs_da_args_t *args)
{
	xfs_attr_leafblock_t *leaf1, *leaf2;
	xfs_attr_leaf_entry_t *entry1, *entry2;
	xfs_attr_leaf_name_remote_t *name_rmt;
	xfs_trans_t *trans;
	xfs_inode_t *dp;
	buf_t *bp1, *bp2;
	int error;
#ifdef DEBUG
	xfs_attr_leaf_name_local_t *name_loc;
	int namelen1, namelen2;
	char *name1, *name2;
#endif /* DEBUG */

	/*
	 * Set up the transaction envelope.
	 */
	dp = args->dp;
	trans = xfs_trans_alloc(dp->i_mount, XFS_TRANS_ATTR_FLAG);
	if (error = xfs_trans_reserve(trans, 0,
					     XFS_ATTRFLAG_LOG_RES(dp->i_mount),
					     0, XFS_TRANS_PERM_LOG_RES,
					     XFS_ATTRFLAG_LOG_COUNT)) {
		xfs_trans_cancel(trans, XFS_TRANS_RELEASE_LOG_RES);
		return(error);
	}

	/*
	 * Read the block containing the "old" attr
	 */
	error = xfs_da_read_buf(trans, dp, args->blkno, &bp1, XFS_ATTR_FORK);
	if (error) {
		xfs_trans_cancel(trans, XFS_TRANS_RELEASE_LOG_RES);
		return(error);
	}
	ASSERT(bp1 != NULL);

	/*
	 * Read the block containing the "new" attr, if it is different
	 */
	if (args->blkno2 != args->blkno) {
		error = xfs_da_read_buf(trans, dp, args->blkno2, &bp2,
					       XFS_ATTR_FORK);
		if (error) {
			xfs_trans_cancel(trans, XFS_TRANS_RELEASE_LOG_RES);
			return(error);
		}
		ASSERT(bp2 != NULL);
	} else {
		bp2 = bp1;
	}

	leaf1 = (xfs_attr_leafblock_t *)bp1->b_un.b_addr;
	ASSERT(leaf1->hdr.info.magic == XFS_ATTR_LEAF_MAGIC);
	ASSERT(args->index < leaf1->hdr.count);
	ASSERT(args->index >= 0);
	entry1 = &leaf1->entries[ args->index ];

	leaf2 = (xfs_attr_leafblock_t *)bp2->b_un.b_addr;
	ASSERT(leaf2->hdr.info.magic == XFS_ATTR_LEAF_MAGIC);
	ASSERT(args->index2 < leaf2->hdr.count);
	ASSERT(args->index2 >= 0);
	entry2 = &leaf2->entries[ args->index2 ];

#ifdef DEBUG
	if (entry1->flags & XFS_ATTR_LOCAL) {
		name_loc = XFS_ATTR_LEAF_NAME_LOCAL(leaf1, args->index);
		namelen1 = name_loc->namelen;
		name1 = (char *)name_loc->nameval;
	} else {
		name_rmt = XFS_ATTR_LEAF_NAME_REMOTE(leaf1, args->index);
		namelen1 = name_rmt->namelen;
		name1 = (char *)name_rmt->name;
	}
	if (entry2->flags & XFS_ATTR_LOCAL) {
		name_loc = XFS_ATTR_LEAF_NAME_LOCAL(leaf2, args->index2);
		namelen2 = name_loc->namelen;
		name2 = (char *)name_loc->nameval;
	} else {
		name_rmt = XFS_ATTR_LEAF_NAME_REMOTE(leaf2, args->index2);
		namelen2 = name_rmt->namelen;
		name2 = (char *)name_rmt->name;
	}
	ASSERT(entry1->hashval == entry2->hashval);
	ASSERT(namelen1 == namelen2);
	ASSERT(bcmp(name1, name2, namelen1) == 0);
#endif /* DEBUG */

	ASSERT(entry1->flags & XFS_ATTR_INCOMPLETE);
	ASSERT((entry2->flags & XFS_ATTR_INCOMPLETE) == 0);

	entry1->flags &= ~XFS_ATTR_INCOMPLETE;
	xfs_trans_log_buf(trans, bp1,
			 XFS_DA_LOGRANGE(leaf1, entry1, sizeof(*entry1)));

	if (args->rmtblkno) {
		ASSERT((entry1->flags & XFS_ATTR_LOCAL) == 0);
		name_rmt = XFS_ATTR_LEAF_NAME_REMOTE(leaf1, args->index);
		name_rmt->valueblk = args->rmtblkno;
		name_rmt->valuelen = args->valuelen;
		xfs_trans_log_buf(trans, bp1,
			 XFS_DA_LOGRANGE(leaf1, name_rmt, sizeof(*name_rmt)));
	}

	entry2->flags |=  XFS_ATTR_INCOMPLETE;
	xfs_trans_log_buf(trans, bp2,
			 XFS_DA_LOGRANGE(leaf2, entry2, sizeof(*entry2)));

	xfs_trans_commit(trans, XFS_TRANS_RELEASE_LOG_RES);
	return(error);
}
