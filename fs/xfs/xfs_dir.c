#include <sys/errno.h>
#include <sys/types.h>
#include <sys/param.h>
#include <sys/buf.h>
#include <sys/vnode.h>
#include <sys/uuid.h>
#include "xfs_types.h"
#include "xfs_inum.h"
#include "xfs_log.h"
#include "xfs_trans.h"
#include "xfs_sb.h"
#include "xfs_mount.h"
#include "xfs_dir.h"
#include "xfs_dir_btree.h"

/*
 * xfs_dir.c
 *
 * Provide the external interfaces to manage directories,
 * and implement the in-inode shortform of a directory.
 */


/*========================================================================
 * Overall external interface routines.
 *========================================================================*/

/*
 * Generic handler routine to add a name to a directory.
 * Transitions directory from shortform to Btree as necessary.
 */
xfs_dir_createname(char *name, int namelen, xfs_ino_t *inum)
{
	struct xfs_dir_name args;
	int retval, newsize;
	xfs_trans_t *trans;

	if (namelen >= MAXNAMELEN)
		return(EINVAL);

	/*
	 * Fill in the arg structure for this request.
	 */
	args.name = name;
	args.namelen = namelen;
	args.hashval = xfs_dir_hashname(name, namelen);
	args.inumber = *inum = ++global_inum;

	/*
	 * Decide on what work routines to call based on the inode size.
	 */
	trans = xfs_trans_alloc(mp, 0);
	if (inodesize <= XFS_LITINO(fs)) {
		newsize = XFS_DIR_SF_ENTSIZE_BYNAME(args.namelen);
		if ((inodesize + newsize) <= XFS_LITINO(fs)) {
			retval = xfs_dir_shortform_addname(trans, &args);
		} else {
			retval = xfs_dir_shortform_to_leaf(trans);
			if (retval == 0) {
				retval = xfs_dir_leaf_addname(trans, &args);
			}
		}
	} else if (inodesize == XFS_LBSIZE(fs)) {
		retval = xfs_dir_leaf_addname(trans, &args);
		if (retval == ENOSPC) {
			retval = xfs_dir_leaf_to_node(trans);
			if (retval == 0) {
				retval = xfs_dir_node_addname(trans, &args);
			}
		}
	} else {
		retval = xfs_dir_node_addname(trans, &args);
	}
	xfs_trans_commit(trans);
	return(retval);
}

/*
 * Generic handler routine to remove a name from a directory.
 * Transitions directory from Btree to shortform as necessary.
 */
int
xfs_dir_removename(char *name, int namelen)
{
	struct xfs_dir_name args;
	int count, totallen, newsize, retval;
	xfs_trans_t *trans;

	if (namelen >= MAXNAMELEN)
		return(EINVAL);

	/*
	 * Fill in the arg structure for this request.
	 */
	args.name = name;
	args.namelen = namelen;
	args.hashval = xfs_dir_hashname(name, namelen);
	args.inumber = 0;

	/*
	 * Decide on what work routines to call based on the inode size.
	 */
	trans = xfs_trans_alloc();
	if (inodesize <= XFS_LITINO(fs)) {
		retval = xfs_dir_shortform_removename(trans, &args);
	} else if (inodesize == XFS_LBSIZE(fs)) {
		retval = xfs_dir_leaf_removename(trans, &args,
							&count, &totallen);
		if (retval == 0) {
			newsize = XFS_DIR_SF_ALLFIT(count, totallen);
			if (newsize <= XFS_LITINO(fs)) {
				retval = xfs_dir_leaf_to_shortform(trans);
			}
		}
	} else {
		retval = xfs_dir_node_removename(trans, &args);
	}
	xfs_trans_commit(trans);
	return(retval);
}

int
xfs_dir_lookup(char *name, int namelen, xfs_ino_t *inum)
{
	struct xfs_dir_name args;
	xfs_trans_t *trans;
	int retval;

	if (namelen >= MAXNAMELEN)
		return(EINVAL);

	/*
	 * Fill in the arg structure for this request.
	 */
	args.name = name;
	args.namelen = namelen;
	args.hashval = xfs_dir_hashname(name, namelen);
	args.inumber = 0;

	/*
	 * Decide on what work routines to call based on the inode size.
	 */
	if (inodesize <= XFS_LITINO(fs)) {
		retval = xfs_dir_shortform_lookup(&args);
	} else if (inodesize == XFS_LBSIZE(fs)) {
		retval = xfs_dir_leaf_lookup(&args);
	} else {
		trans = xfs_trans_alloc();
		retval = xfs_dir_node_lookup(trans, &args);
		xfs_trans_commit(trans);
	}
	if (retval == EEXIST)
		retval = 0;

	*inum = args.inumber;

	return(retval);
}

int
xfs_dir_getdents()	/* GROT: finish this */
{
	return(0);
}

/*========================================================================
 * External routines when dirsize < XFS_LITINO(fs).
 *========================================================================*/

/*
 * Create the initial contents of a shortform directory.
 */
int
xfs_dir_shortform_create(xfs_trans_t *trans, xfs_ino_t parent)
{
	struct xfs_dir_sf_hdr *hdr;
	xfs_fsblock_t blkno;
	int retval;
	buf_t *bp;

	xfs_trans_iget(trans, 0, &bp);
	bzero(bp->buffer, XFS_LITINO(fs));
	hdr = (struct xfs_dir_sf_hdr *)bp->buffer;
	bcopy((char *)&parent, hdr->parent, sizeof(xfs_ino_t));
	hdr->count = 0;
	retval = xfs_dir_grow_inode(trans, sizeof(struct xfs_dir_sf_hdr),
					   &blkno);

	if (!retval)
		xfs_trans_log_inode(trans, bp);
	return(retval);
}

/*
 * Add a name to the shortform directory structure.
 * Overflow from the inode has already been checked for.
 */
xfs_dir_shortform_addname(xfs_trans_t *trans, struct xfs_dir_name *args)
{
	struct xfs_dir_shortform *sf;
	struct xfs_dir_sf_entry *sfe;
	int retval, i;
	xfs_fsblock_t blkno;
	buf_t *bp;

	xfs_trans_iget(trans, 0, &bp);
	sf = (struct xfs_dir_shortform *)bp->buffer;
	sfe = &sf->list[0];
	for (i = sf->hdr.count-1; i >= 0; i--) {
		if (sfe->namelen == args->namelen) {
			if (bcmp(args->name, sfe->name, args->namelen) == 0) {
				return(EEXIST);
			}
		}
		sfe = XFS_DIR_SF_NEXTENTRY(sfe);
	}

	bcopy((char *)&args->inumber, sfe->inumber, sizeof(xfs_ino_t));
	sfe->namelen = args->namelen;
	bcopy(args->name, sfe->name, sfe->namelen);
	sf->hdr.count++;

	retval = xfs_dir_grow_inode(trans,
				    XFS_DIR_SF_ENTSIZE_BYNAME(args->namelen),
				    &blkno);

	if (!retval)
		xfs_trans_log_inode(trans, bp);
	return(retval);
}

/*
 * Remove a name from the shortform directory structure.
 */
xfs_dir_shortform_removename(xfs_trans_t *trans, struct xfs_dir_name *args)
{
	struct xfs_dir_shortform *sf;
	struct xfs_dir_sf_entry *sfe;
	int base, size, retval, i;
	xfs_fsblock_t blkno;
	buf_t *bp;

	xfs_trans_iget(trans, 0, &bp);
	base = sizeof(struct xfs_dir_sf_hdr);
	sf = (struct xfs_dir_shortform *)bp->buffer;
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
		return(ENOENT);

	if ((base + size) != inodesize) {
		bcopy(&bp->buffer[base+size], &bp->buffer[base],
					      inodesize - (base+size));
	}
	bzero(&bp->buffer[inodesize - size], size);
	sf->hdr.count--;

	retval = xfs_dir_shrink_inode(trans, size, &blkno);

	if (!retval)
		xfs_trans_log_inode(trans, bp);
	return(retval);
}

/*
 * Look up a name in a shortform directory structure.
 */
xfs_dir_shortform_lookup(struct xfs_dir_name *args)
{
	struct xfs_dir_shortform *sf;
	struct xfs_dir_sf_entry *sfe;
	buf_t *bp;
	int i;

	xfs_trans_iget(NULL, 0, &bp);
	sf = (struct xfs_dir_shortform *)bp->buffer;
	sfe = &sf->list[0];
	for (i = sf->hdr.count-1; i >= 0; i--) {
		if (sfe->namelen == args->namelen) {
			if (bcmp(args->name, sfe->name, args->namelen) == 0) {
				bcopy(sfe->inumber, (char *)&args->inumber,
						    sizeof(xfs_ino_t));
				return(0);
			}
		}
		sfe = XFS_DIR_SF_NEXTENTRY(sfe);
	}
	return(ENOENT);
}

/*
 * Convert from using the shortform to the leaf.
 */
int
xfs_dir_shortform_to_leaf(xfs_trans_t *trans)
{
	struct xfs_dir_shortform *sf;
	struct xfs_dir_sf_entry *sfe;
	struct xfs_dir_name args;
	xfs_ino_t inumber;
	char *tmpbuffer;
	int retval, i;
	xfs_fsblock_t blkno;
	buf_t *bp;

	tmpbuffer = malloc(XFS_LITINO(fs));
	if (tmpbuffer == NULL) {
		perror("malloc failed");
		return(ENOMEM);
	}

	xfs_trans_iget(trans, 0, &bp);
	bcopy(bp->buffer, tmpbuffer, XFS_LITINO(fs));

	sf = (struct xfs_dir_shortform *)tmpbuffer;
	bcopy(sf->hdr.parent, (char *)&inumber, sizeof(xfs_ino_t));

	/* GROT: change all XFS_LBSIZE into state->blocksize */
	retval = xfs_dir_grow_inode(trans, XFS_LBSIZE(fs) - inodesize, &blkno);
	if (retval)
		goto out;

	bp = xfs_dir_leaf_create(trans, blkno);

	args.name = ".";
	args.namelen = 1;
	args.hashval = xfs_dir_hashname(".", 1);
	args.inumber = inumber;
	retval = xfs_dir_leaf_addname(trans, &args);
	if (retval)
		goto out;

	args.name = "..";
	args.namelen = 2;
	args.hashval = xfs_dir_hashname("..", 2);
	args.inumber = inumber;
	retval = xfs_dir_leaf_addname(trans, &args);
	if (retval)
		goto out;

	sfe = &sf->list[0];
	for (i = sf->hdr.count-1; i >= 0; i--) {
		args.name = sfe->name;
		args.namelen = sfe->namelen;
		args.hashval = xfs_dir_hashname(sfe->name, sfe->namelen);
		bcopy(sfe->inumber, (char *)&args.inumber, sizeof(xfs_ino_t));
		retval = xfs_dir_leaf_addname(trans, &args);
		if (retval)
			goto out;
		sfe = XFS_DIR_SF_NEXTENTRY(sfe);
	}
	retval = 0;

out:
	free(tmpbuffer);
	return(retval);
}

/*========================================================================
 * External routines when dirsize == XFS_LBSIZE(fs).
 *========================================================================*/

/*
 * Create the initial contents of a leaf directory.
 */
buf_t *
xfs_dir_leaf_create(xfs_trans_t *trans, xfs_fsblock_t blkno)
{
	struct xfs_dir_leafblock *leaf;
	struct xfs_dir_leaf_hdr *hdr;
	buf_t *bp;

	bp = xfs_trans_getblk(trans, blkno, XFS_LBSIZE(fs));
	bzero(bp->buffer, XFS_LBSIZE(fs));
	leaf = (struct xfs_dir_leafblock *)bp->buffer;
	hdr = &leaf->hdr;
	hdr->info.magic = XFS_DIR_LEAF_MAGIC;
	hdr->firstused = XFS_LBSIZE(fs);
	hdr->freemap[0].base = sizeof(struct xfs_dir_leaf_hdr);
	hdr->freemap[0].size = XFS_LBSIZE(fs) - hdr->freemap[0].base;

	xfs_trans_log_buf(trans, bp, 0, XFS_LBSIZE(fs));
	return(bp);
}

/*
 * Add a name to the leaf directory structure
 * This is the external routine.
 */
int
xfs_dir_leaf_addname(xfs_trans_t *trans, struct xfs_dir_name *args)
{
	int index, retval;
	buf_t *bp;

	bp = xfs_trans_bread(trans, 0, XFS_LBSIZE(fs));

	retval = xfs_dir_leaf_lookup_int(bp, args, &index);
	if (retval != ENOENT)
		return(retval);

	retval = xfs_dir_leaf_add(trans, bp, args, index);
	return(retval);
}

/*
 * Remove a name from the leaf directory structure
 * This is the external routine.
 */
xfs_dir_leaf_removename(xfs_trans_t *trans, struct xfs_dir_name *args,
				    int *count, int *totallen)
{
	struct xfs_dir_leafblock *leaf;
	int index, retval;
	buf_t *bp;

	bp = xfs_trans_bread(trans, 0, XFS_LBSIZE(fs));
	leaf = (struct xfs_dir_leafblock *)bp->buffer;
	ASSERT(leaf->hdr.info.magic == XFS_DIR_LEAF_MAGIC);
	retval = xfs_dir_leaf_lookup_int(bp, args, &index);
	if (retval != EEXIST)
		return(retval);

	xfs_dir_leaf_remove(trans, bp, index);
	*count = leaf->hdr.count;
	*totallen = leaf->hdr.namebytes;
	return(0);
}

/*
 * Look up a name in a leaf directory structure.
 * This is the external routine.
 */
xfs_dir_leaf_lookup(struct xfs_dir_name *args)
{
	int index, retval;
	buf_t *bp;

	bp = xfs_trans_bread(NULL, 0, XFS_LBSIZE(fs));
	retval = xfs_dir_leaf_lookup_int(bp, args, &index);
	xfs_trans_brelse(NULL, bp);
	return(retval);
}

/*
 * Convert a leaf directory to shortform structure
 */
xfs_dir_leaf_to_shortform(xfs_trans_t *trans)
{
	struct xfs_dir_leafblock *leaf;
	struct xfs_dir_leaf_hdr *hdr;
	struct xfs_dir_leaf_entry *entry;
	struct xfs_dir_leaf_name *namest;
	struct xfs_dir_name args;
	xfs_ino_t parent;
	xfs_fsblock_t blkno;
	char *tmpbuffer;
	int retval, i;
	buf_t *bp;

	tmpbuffer = malloc(XFS_LBSIZE(fs));
	if (tmpbuffer == NULL) {
		perror("malloc failed");
		exit(1);
	}

	bp = xfs_trans_bread(trans, 0, XFS_LBSIZE(fs));
	bcopy(bp->buffer, tmpbuffer, XFS_LBSIZE(fs));
	leaf = (struct xfs_dir_leafblock *)tmpbuffer;
	ASSERT(leaf->hdr.info.magic == XFS_DIR_LEAF_MAGIC);
	bzero(bp->buffer, XFS_LBSIZE(fs));

	/*
	 * Find and special case the parent inode number
	 */
	hdr = &leaf->hdr;
	entry = &leaf->leaves[0];
	for (i = hdr->count-1; i >= 0; entry++, i--) {
		namest = XFS_DIR_LEAF_NAMESTRUCT(leaf, entry->nameidx);
		if ((namest->namelen == 2) &&
		    (namest->name[0] == '.') &&
		    (namest->name[1] == '.')) {
			bcopy(namest->inumber, (char *)&parent,
					       sizeof(xfs_ino_t));
			entry->nameidx = 0;
		}
		if ((namest->namelen == 1) && (namest->name[0] == '.')) {
			entry->nameidx = 0;
		}
	}
	retval = xfs_dir_shrink_inode(trans, inodesize, &blkno);
	if (retval)
		goto out;
	retval = xfs_dir_shortform_create(trans, parent);
	if (retval)
		goto out;

	/*
	 * Copy the rest of the filenames
	 */
	entry = &leaf->leaves[0];
	for (i = hdr->count-1; i >= 0; entry++, i--) {
		if (entry->nameidx == 0)
			continue;
		namest = XFS_DIR_LEAF_NAMESTRUCT(leaf, entry->nameidx);
		args.name = namest->name;
		args.namelen = namest->namelen;
		args.hashval = entry->hashval;
		bcopy(namest->inumber, (char *)&args.inumber, sizeof(xfs_ino_t));
		xfs_dir_shortform_addname(trans, &args);
	}

out:
	free(tmpbuffer);
	return(retval);
}

/*
 * Convert from using a single leaf to a root node and a leaf.
 */
xfs_dir_leaf_to_node(xfs_trans_t *trans)
{
	struct xfs_dir_leafblock *leaf;
	struct xfs_dir_intnode *node;
	buf_t *bp1, *bp2;
	xfs_fsblock_t blkno;
	int retval;

	retval = xfs_dir_grow_inode(trans, XFS_LBSIZE(fs), &blkno);
	if (retval)
		return(retval);
	bp1 = xfs_trans_bread(trans, 0, XFS_LBSIZE(fs));
	bp2 = xfs_trans_getblk(trans, blkno, XFS_LBSIZE(fs));
	bcopy(bp1->buffer, bp2->buffer, XFS_LBSIZE(fs));
	xfs_trans_log_buf(trans, bp2, 0, XFS_LBSIZE(fs));

	/*
	 * Set up the new root node.
	 */
	bp1 = xfs_dir_node_create(trans, 0, 1);
	node = (struct xfs_dir_intnode *)bp1->buffer;
	leaf = (struct xfs_dir_leafblock *)bp2->buffer;
	ASSERT(leaf->hdr.info.magic == XFS_DIR_LEAF_MAGIC);
	node->btree[0].hashval = leaf->leaves[ leaf->hdr.count-1 ].hashval;
	node->btree[0].before = blkno;
	node->hdr.count = 1;
	xfs_trans_log_buf(trans, bp1, 0, XFS_LBSIZE(FS));

	return(retval);
}

/*========================================================================
 * External routines when dirsize > XFS_LBSIZE(fs).
 *========================================================================*/

/*
 * Create the initial contents of an intermediate node.
 */
buf_t *
xfs_dir_node_create(xfs_trans_t *trans, xfs_fsblock_t blkno, int level)
{
	struct xfs_dir_intnode *node;
	buf_t *bp;

	bp = xfs_trans_bread(trans, blkno, XFS_LBSIZE(fs));
	bzero((char *)bp->buffer, XFS_LBSIZE(fs));
	node = (struct xfs_dir_intnode *)bp->buffer;
	node->hdr.info.magic = XFS_DIR_NODE_MAGIC;
	node->hdr.level = level;

	xfs_trans_log_buf(trans, bp, 0, XFS_LBSIZE(fs));

	return(bp);
}

/*
 * Add a name to a int directory.
 *
 * This will involve walking down the Btree, and may involve splitting
 * leaf nodes and even splitting intermediate nodes up to and including
 * the root node (a special case of an intermediate node).
 */
xfs_dir_node_addname(xfs_trans_t *trans, struct xfs_dir_name *args)
{
	struct xfs_dir_state state;
	struct xfs_dir_state_blk *blk;
	int retval;

	/*
	 * Fill in bucket of arguments/results/context to carry around.
	 */
	bzero((char *)&state, sizeof(struct xfs_dir_state));
	state.args = args;
	state.trans = trans;
	state.blocksize = blocksize;

	/*
	 * Search to see if name already exists, and get back a pointer
	 * to where it should go.
	 */
	retval = xfs_dir_node_lookup_int(&state);
	if (retval != ENOENT)
		return(retval);
		
	blk = &state.path.blk[ state.path.active-1 ];
	ASSERT(blk->leafblk == 1);
	retval = xfs_dir_leaf_add(state.trans, blk->bp, state.args, blk->index);
	if (retval == 0) {
		/*
		 * Addition succeeded, update Btree hashvals.
		 * GROT: clean this up
		 */
		xfs_dir_fixhashpath(&state, &state.path, state.path.active-1);
	} else if (retval == ENOSPC) {
		/*
		 * Addition failed, split as many Btree elements as required.
		 */
		retval = xfs_dir_split(&state);
	}

	return(retval);
}

/*
 * Remove a name from a B-tree directory.
 *
 * This will involve walking down the Btree, and may involve joining
 * leaf nodes and even joining intermediate nodes up to and including
 * the root node (a special case of an intermediate node).
 */
xfs_dir_node_removename(xfs_trans_t *trans, struct xfs_dir_name *args)
{
	struct xfs_dir_state state;
	struct xfs_dir_state_blk *blk;
	int retval;

	bzero((char *)&state, sizeof(struct xfs_dir_state));
	state.args = args;
	state.trans = trans;
	state.blocksize = blocksize;

	/*
	 * Search to see if name exists, and get back a pointer to it.
	 */
	retval = xfs_dir_node_lookup_int(&state);
	if (retval != EEXIST)
		return(retval);

	/*
	 * Check to see if the tree needs to be collapsed.
	 */
	xfs_dir_join(&state);
	return(0);
}

/*
 * Look up a filename in a int directory.
 * Use an internal routine to actually do all the work.
 */
xfs_dir_node_lookup(xfs_trans_t *trans, struct xfs_dir_name *args)
{
	struct xfs_dir_state state;
	int retval;

	bzero((char *)&state, sizeof(struct xfs_dir_state));
	state.args = args;
	state.trans = trans;
	state.blocksize = blocksize;

	/*
	 * Search to see if name exists,
	 * and get back a pointer to it.
	 */
	retval = xfs_dir_node_lookup_int(&state);
	return(retval);
}

/*========================================================================
 * Utility routines.
 *========================================================================*/

/*
 * Implement a simple hash on a character string.
 * Rotate the hash value by 7 bits, then XOR each character in.
 */
uint
xfs_dir_hashname(register char *name, register int namelen)
{
	register uint hash;

	hash = 0;
	for (  ; namelen > 0; namelen--) {
		hash = *name++ ^ ((hash << 7) | (hash >> (32-7)));
	}
	return(hash);
}

int
xfs_dir_grow_inode(xfs_trans_t *trans, int delta, xfs_fsblock_t *new_blkno)
{
	xfs_fsblock_t newsize;
	int remain;

	newsize = (inodesize + delta) / XFS_LBSIZE(fs);
	if (newsize >= XFS_DIR_MAXBLK) {
		return(ENOSPC);
	}

	/* GROT: fill this in */

	inodesize += delta;
	*new_blkno = (newsize > 0) ? newsize-1 : 0;

	return(0);
}

int
xfs_dir_shrink_inode(xfs_trans_t *trans, int delta, xfs_fsblock_t *new_blkno)
{
	xfs_fsblock_t newsize;

	inodesize -= delta;

	/* GROT: fill this in */

	newsize = inodesize / XFS_LBSIZE(fs);
	*new_blkno = (newsize > 0) ? newsize-1 : 0;
	return(0);
}
