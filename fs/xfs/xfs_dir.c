#include <sys/param.h>
#include <sys/errno.h>
#include <sys/buf.h>
#include <sys/vnode.h>
#include <sys/uuid.h>
#include <sys/kmem.h>
#include <sys/uio.h>
#include <sys/debug.h>
#ifdef SIM
#define _KERNEL 1
#endif /* SIM */
#include <sys/dirent.h>
#include <sys/user.h>
#ifdef SIM
#undef _KERNEL
#include <bstring.h>
#include <stdio.h>
#else
#include <sys/systm.h>
#endif
#include <string.h>
#include "xfs_types.h"
#include "xfs_inum.h"
#ifdef SIM
#define _KERNEL
#endif
#include <sys/uuid.h>
#include <sys/grio.h>
#ifdef SIM
#undef _KERNEL
#endif
#include <sys/proc.h>
#include "xfs_log.h"
#include "xfs_trans.h"
#include "xfs_sb.h"
#include "xfs_mount.h"
#include "xfs_alloc_btree.h"
#include "xfs_bmap_btree.h"
#include "xfs_ialloc_btree.h"
#include "xfs_bmap.h"
#include "xfs_btree.h"
#include "xfs_dinode.h"
#include "xfs_inode_item.h"
#include "xfs_inode.h"
#include "xfs_bmap.h"
#include "xfs_dir.h"
#include "xfs_dir_btree.h"
#include "xfs_error.h"
#ifdef SIM
#include "sim.h"
#endif

/*
 * xfs_dir.c
 *
 * Provide the external interfaces to manage directories,
 * and implement the in-inode shortform of a directory.
 */

/*========================================================================
 * Function prototypes for the kernel.
 *========================================================================*/

/*
 * Internal routines when dirsize < XFS_LITINO(mp).
 */
STATIC int xfs_dir_shortform_create(xfs_trans_t *trans, xfs_inode_t *dp,
					xfs_ino_t parent_inumber);
STATIC int xfs_dir_shortform_addname(xfs_trans_t *trans,
					struct xfs_dir_name *add);
STATIC int xfs_dir_shortform_removename(xfs_trans_t *trans,
					struct xfs_dir_name *remove);
STATIC int xfs_dir_shortform_lookup(xfs_trans_t *trans,
					struct xfs_dir_name *args);
STATIC int xfs_dir_shortform_to_leaf(xfs_trans_t *trans,
					struct xfs_dir_name *args);
STATIC void xfs_dir_shortform_print(xfs_trans_t *trans, xfs_inode_t *dp);
STATIC int xfs_dir_shortform_getdents(xfs_trans_t *trans, xfs_inode_t *dp,
					uio_t *uio, int *eofp, dirent_t *dbp);
STATIC int xfs_dir_shortform_replace(xfs_trans_t *trans,
					struct xfs_dir_name *args);

/*
 * Internal routines when dirsize == XFS_LBSIZE(mp).
 */
STATIC int xfs_dir_leaf_addname(xfs_trans_t *trans, struct xfs_dir_name *args);
STATIC int xfs_dir_leaf_removename(xfs_trans_t *trans,
					struct xfs_dir_name *args,
					int *number_entries,
					int *total_namebytes);
STATIC int xfs_dir_leaf_lookup(xfs_trans_t *trans, struct xfs_dir_name *args);
STATIC int xfs_dir_leaf_to_shortform(xfs_trans_t *trans,
					struct xfs_dir_name *args);
STATIC int xfs_dir_leaf_to_node(xfs_trans_t *trans, struct xfs_dir_name *args);
STATIC void xfs_dir_leaf_print(xfs_trans_t *trans, xfs_inode_t *dp);
STATIC int xfs_dir_leaf_getdents(xfs_trans_t *trans, xfs_inode_t *dp,
					uio_t *uio, int *eofp, dirent_t *dbp);
STATIC int xfs_dir_leaf_replace(xfs_trans_t *trans, struct xfs_dir_name *args);

/*
 * Internal routines when dirsize > XFS_LBSIZE(mp).
 */
STATIC int xfs_dir_node_addname(xfs_trans_t *trans, struct xfs_dir_name *args);
STATIC int xfs_dir_node_removename(xfs_trans_t *trans,
					struct xfs_dir_name *args);
STATIC int xfs_dir_node_lookup(xfs_trans_t *trans, struct xfs_dir_name *args);
#if 0
STATIC int xfs_dir_node_to_leaf(xfs_trans_t *trans, struct xfs_dir_name *args);
#endif
STATIC void xfs_dir_node_print(xfs_trans_t *trans, xfs_inode_t *dp);
STATIC int xfs_dir_node_getdents(xfs_trans_t *trans, xfs_inode_t *dp,
					uio_t *uio, int *eofp, dirent_t *dbp);
STATIC int xfs_dir_node_replace(xfs_trans_t *trans, struct xfs_dir_name *args);

/*
 * Utility routines.
 */
STATIC uint xfs_dir_hashname(char *name_string, int name_length);
STATIC uint xfs_dir_log2_roundup(uint i);
STATIC struct xfs_dir_state *xfs_dir_state_alloc(void);
STATIC void xfs_dir_state_free(struct xfs_dir_state *state);

zone_t	*xfs_dir_state_zone;


/*========================================================================
 * Overall external interface routines.
 *========================================================================*/

/*
 * Initialize directory-related fields in the mount structure.
 */
void
xfs_dir_mount(xfs_mount_t *mp)
{
	uint shortcount, leafcount, count;

	shortcount = (XFS_LITINO(mp) - sizeof(struct xfs_dir_sf_hdr)) /
		     sizeof(struct xfs_dir_sf_entry);
	leafcount = (XFS_LBSIZE(mp) - sizeof(struct xfs_dir_leaf_hdr)) /
		    (sizeof(struct xfs_dir_leaf_entry) +
		     sizeof(struct xfs_dir_leaf_name));
	count = shortcount > leafcount ? shortcount : leafcount;
	mp->m_dircook_elog = xfs_dir_log2_roundup(count);
}

/*
 * Return 1 if directory contains only "." and "..".
 */
xfs_dir_isempty(xfs_inode_t *dp)
{
	struct xfs_dir_sf_hdr *hdr;
	xfs_mount_t *mp;

	ASSERT((dp->i_d.di_mode & IFMT) == IFDIR);
	if (dp->i_d.di_size == 0)
		return(1);
	mp = dp->i_mount;
	if (dp->i_d.di_size > XFS_LITINO(mp))
		return(0);
	hdr = (struct xfs_dir_sf_hdr *)dp->i_u1.iu_data;
	return(hdr->count == 0);
}

/*
 * Initialize a directory with its "." and ".." entries.
 */
xfs_dir_init(xfs_trans_t *trans, xfs_inode_t *dir, xfs_inode_t *parent_dir)
{
	ASSERT((dir->i_d.di_mode & IFMT) == IFDIR);
	return(xfs_dir_shortform_create(trans, dir, parent_dir->i_ino));
}

/*
 * Generic handler routine to add a name to a directory.
 * Transitions directory from shortform to Btree as necessary.
 */
xfs_dir_createname(xfs_trans_t *trans, xfs_inode_t *dp, char *name,
		   xfs_ino_t inum, xfs_fsblock_t *firstblock,
		   xfs_bmap_free_t *flist, xfs_extlen_t total)
{
	struct xfs_dir_name args;
	int retval, newsize, namelen;
	xfs_mount_t *mp;

	ASSERT((dp->i_d.di_mode & IFMT) == IFDIR);
	namelen = strlen(name);
	if (namelen >= MAXNAMELEN)
		return(XFS_ERROR(EINVAL));

	/*
	 * Fill in the arg structure for this request.
	 */
	args.name = name;
	args.namelen = namelen;
	args.hashval = xfs_dir_hashname(name, namelen);
	args.inumber = inum;
	args.dp = dp;
	args.firstblock = firstblock;
	args.flist = flist;
	args.total = total;

	mp = dp->i_mount;
	/*
	 * Decide on what work routines to call based on the inode size.
	 */
	if (dp->i_d.di_size <= XFS_LITINO(mp)) {
		newsize = XFS_DIR_SF_ENTSIZE_BYNAME(args.namelen);
		if ((dp->i_d.di_size + newsize) <= XFS_LITINO(mp)) {
			retval = xfs_dir_shortform_addname(trans, &args);
		} else {
			retval = xfs_dir_shortform_to_leaf(trans, &args);
			if (retval == 0) {
				retval = xfs_dir_leaf_addname(trans, &args);
			}
		}
	} else if (dp->i_d.di_size == XFS_LBSIZE(mp)) {
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

/*
 * Generic handler routine to remove a name from a directory.
 * Transitions directory from Btree to shortform as necessary.
 */
int
xfs_dir_removename(xfs_trans_t *trans, xfs_inode_t *dp, char *name,
		   xfs_fsblock_t *firstblock, xfs_bmap_free_t *flist,
		   xfs_extlen_t total)
{
	struct xfs_dir_name args;
	int count, totallen, newsize, retval, namelen;
	xfs_mount_t *mp;

	ASSERT((dp->i_d.di_mode & IFMT) == IFDIR);
	namelen = strlen(name);
	if (namelen >= MAXNAMELEN)
		return(XFS_ERROR(EINVAL));

	/*
	 * Fill in the arg structure for this request.
	 */
	args.name = name;
	args.namelen = namelen;
	args.hashval = xfs_dir_hashname(name, namelen);
	args.inumber = 0;
	args.dp = dp;
	args.firstblock = firstblock;
	args.flist = flist;
	args.total = total;

	mp = dp->i_mount;
	/*
	 * Decide on what work routines to call based on the inode size.
	 */
	if (dp->i_d.di_size <= XFS_LITINO(mp)) {
		retval = xfs_dir_shortform_removename(trans, &args);
	} else if (dp->i_d.di_size == XFS_LBSIZE(mp)) {
		retval = xfs_dir_leaf_removename(trans, &args,
							&count, &totallen);
		if (retval == 0) {
			newsize = XFS_DIR_SF_ALLFIT(count, totallen);
			if (newsize <= XFS_LITINO(mp)) {
				retval = xfs_dir_leaf_to_shortform(trans,
								   &args);
			}
		}
	} else {
		retval = xfs_dir_node_removename(trans, &args);
	}
	return(retval);
}

int
xfs_dir_lookup(xfs_trans_t *trans, xfs_inode_t *dp, char *name, int namelen,
				   xfs_ino_t *inum)
{
	struct xfs_dir_name args;
	int retval;
	xfs_mount_t *mp;

	ASSERT((dp->i_d.di_mode & IFMT) == IFDIR);
	if (namelen >= MAXNAMELEN)
		return(XFS_ERROR(EINVAL));

	/*
	 * Fill in the arg structure for this request.
	 */
	args.name = name;
	args.namelen = namelen;
	args.hashval = xfs_dir_hashname(name, namelen);
	args.inumber = 0;
	args.dp = dp;
	args.firstblock = NULL;
	args.flist = NULL;
	args.total = 0;

	mp = dp->i_mount;
	/*
	 * Decide on what work routines to call based on the inode size.
	 */
	if (dp->i_d.di_size <= XFS_LITINO(mp)) {
		retval = xfs_dir_shortform_lookup(trans, &args);
	} else if (dp->i_d.di_size == XFS_LBSIZE(mp)) {
		retval = xfs_dir_leaf_lookup(trans, &args);
	} else {
		retval = xfs_dir_node_lookup(trans, &args);
	}
	if (retval == EEXIST)
		retval = 0;

	*inum = args.inumber;

	return(retval);
}

/*
 * Print a directory's contents.
 * For debugging.
 */
void
xfs_dir_print(xfs_trans_t *trans, xfs_inode_t *dp)
{
	xfs_mount_t *mp;

	ASSERT((dp->i_d.di_mode & IFMT) == IFDIR);
	mp = dp->i_mount;
	/*
	 * Decide on what work routines to call based on the inode size.
	 */
	if (dp->i_d.di_size <= XFS_LITINO(mp)) {
		xfs_dir_shortform_print(trans, dp);
	} else if (dp->i_d.di_size == XFS_LBSIZE(mp)) {
		xfs_dir_leaf_print(trans, dp);
	} else {
		xfs_dir_node_print(trans, dp);
	}
}

/*
 * Implement readdir.
 */
int
xfs_dir_getdents(xfs_trans_t *trans, xfs_inode_t *dp, uio_t *uio, int *eofp)
{
	xfs_mount_t *mp;
	int retval;
	dirent_t *dbp;

	ASSERT((dp->i_d.di_mode & IFMT) == IFDIR);
	mp = dp->i_mount;
	dbp = kmem_alloc(sizeof(*dbp) + MAXNAMELEN, KM_SLEEP);
	*eofp = 0;
	/*
	 * Decide on what work routines to call based on the inode size.
	 */
	if (dp->i_d.di_size <= XFS_LITINO(mp)) {
		retval = xfs_dir_shortform_getdents(trans, dp, uio, eofp, dbp);
	} else if (dp->i_d.di_size == XFS_LBSIZE(mp)) {
		retval = xfs_dir_leaf_getdents(trans, dp, uio, eofp, dbp);
	} else {
		retval = xfs_dir_node_getdents(trans, dp, uio, eofp, dbp);
	}
	kmem_free(dbp, sizeof(*dbp) + MAXNAMELEN);
	return(retval);
}

int
xfs_dir_replace(xfs_trans_t *trans, xfs_inode_t *dp, char *name, int namelen,
				    xfs_ino_t inum)
{
	struct xfs_dir_name args;
	int retval;
	xfs_mount_t *mp;

	ASSERT((dp->i_d.di_mode & IFMT) == IFDIR);
	if (namelen >= MAXNAMELEN)
		return(XFS_ERROR(EINVAL));

	/*
	 * Fill in the arg structure for this request.
	 */
	args.name = name;
	args.namelen = namelen;
	args.hashval = xfs_dir_hashname(name, namelen);
	args.inumber = inum;
	args.dp = dp;
	args.firstblock = NULL;
	args.flist = NULL;
	args.total = 0;

	mp = dp->i_mount;
	/*
	 * Decide on what work routines to call based on the inode size.
	 */
	if (dp->i_d.di_size <= XFS_LITINO(mp)) {
		retval = xfs_dir_shortform_replace(trans, &args);
	} else if (dp->i_d.di_size == XFS_LBSIZE(mp)) {
		retval = xfs_dir_leaf_replace(trans, &args);
	} else {
		retval = xfs_dir_node_replace(trans, &args);
	}

	return(retval);
}


/*========================================================================
 * External routines when dirsize < XFS_LITINO(mp).
 *========================================================================*/

/*
 * Create the initial contents of a shortform directory.
 */
STATIC int
xfs_dir_shortform_create(xfs_trans_t *trans, xfs_inode_t *dp, xfs_ino_t parent)
{
	struct xfs_dir_sf_hdr *hdr;

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
	hdr = (struct xfs_dir_sf_hdr *)dp->i_u1.iu_data;
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
STATIC int
xfs_dir_shortform_addname(xfs_trans_t *trans, struct xfs_dir_name *args)
{
	struct xfs_dir_shortform *sf;
	struct xfs_dir_sf_entry *sfe;
	int i, offset, size;
	xfs_inode_t *dp;

	dp = args->dp;
	ASSERT(dp->i_flags & XFS_IINLINE);
	sf = (struct xfs_dir_shortform *)dp->i_u1.iu_data;
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
	sf = (struct xfs_dir_shortform *)dp->i_u1.iu_data;
	sfe = (struct xfs_dir_sf_entry *)((char *)sf + offset);

	bcopy((char *)&args->inumber, sfe->inumber, sizeof(xfs_ino_t));
	sfe->namelen = args->namelen;
	bcopy(args->name, sfe->name, sfe->namelen);
	sf->hdr.count++;

	dp->i_d.di_size += size;
	xfs_trans_log_inode(trans, dp, XFS_ILOG_CORE | XFS_ILOG_DATA);

	return(0);
}

/*
 * Remove a name from the shortform directory structure.
 */
STATIC int
xfs_dir_shortform_removename(xfs_trans_t *trans, struct xfs_dir_name *args)
{
	struct xfs_dir_shortform *sf;
	struct xfs_dir_sf_entry *sfe;
	int base, size, i;
	xfs_inode_t *dp;

	dp = args->dp;
	ASSERT(dp->i_flags & XFS_IINLINE);
	base = sizeof(struct xfs_dir_sf_hdr);
	sf = (struct xfs_dir_shortform *)dp->i_u1.iu_data;
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

/*
 * Look up a name in a shortform directory structure.
 */
STATIC int
xfs_dir_shortform_lookup(xfs_trans_t *trans, struct xfs_dir_name *args)
{
	struct xfs_dir_shortform *sf;
	struct xfs_dir_sf_entry *sfe;
	int i;
	xfs_inode_t *dp;

	dp = args->dp;
	ASSERT(dp->i_flags & XFS_IINLINE);
	sf = (struct xfs_dir_shortform *)dp->i_u1.iu_data;
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
STATIC int
xfs_dir_shortform_to_leaf(xfs_trans_t *trans, struct xfs_dir_name *iargs)
{
	xfs_inode_t *dp;
	struct xfs_dir_shortform *sf;
	struct xfs_dir_sf_entry *sfe;
	struct xfs_dir_name args;
	xfs_ino_t inumber;
	char *tmpbuffer;
	int retval, i, size;
	xfs_fsblock_t blkno;
	buf_t *bp;

	dp = iargs->dp;
	size = dp->i_bytes;
	tmpbuffer = kmem_alloc(size, KM_SLEEP);
	ASSERT(tmpbuffer != NULL);

	bcopy(dp->i_u1.iu_data, tmpbuffer, size);

	sf = (struct xfs_dir_shortform *)tmpbuffer;
	bcopy(sf->hdr.parent, (char *)&inumber, sizeof(xfs_ino_t));

	xfs_idata_realloc(dp, -size);
	dp->i_d.di_size = 0;
	retval = xfs_dir_grow_inode(trans, iargs, &blkno);
	if (retval) {
		dp->i_d.di_size = size;
		xfs_idata_realloc(dp, size);
		bcopy(tmpbuffer, dp->i_u1.iu_data, size);
		goto out;
	}

	ASSERT(blkno == 0);
	bp = xfs_dir_leaf_create(trans, dp, blkno);

	args.name = ".";
	args.namelen = 1;
	args.hashval = xfs_dir_hashname(".", 1);
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
	kmem_free(tmpbuffer, size);
	return(retval);
}

/*
 * Print the shortform directory.
 */
STATIC void
xfs_dir_shortform_print(xfs_trans_t *trans, xfs_inode_t *dp)
{
	struct xfs_dir_shortform *sf;
	struct xfs_dir_sf_entry *sfe;
	xfs_ino_t ino;
	int i;

	sf = (struct xfs_dir_shortform *)dp->i_u1.iu_data;
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
STATIC int
xfs_dir_shortform_getdents(xfs_trans_t *trans, xfs_inode_t *dp, uio_t *uio,
				int *eofp, dirent_t *dbp)
{
	xfs_mount_t *mp;
	struct xfs_dir_shortform *sf;
	struct xfs_dir_sf_entry *sfe;
	xfs_ino_t ino;
	int i;
	__uint32_t bno;
	int entry;
	int retval;
	int done;

	mp = dp->i_mount;
	bno = (__uint32_t)XFS_DIR_COOKIE_BNO(mp, uio->uio_offset);
	if (bno != 0)
		return(XFS_ERROR(ENOENT));
	entry = XFS_DIR_COOKIE_ENTRY(mp, uio->uio_offset);
	sf = (struct xfs_dir_shortform *)dp->i_u1.iu_data;
	if (entry >= sf->hdr.count + 2)
		return(XFS_ERROR(ENOENT));

	if (entry == 0) {
		retval = xfs_dir_put_dirent(mp, dbp, dp->i_ino, ".", 1, bno,
						entry, uio, &done);
		if (!done)
			return(retval);
		entry++;
	}
	if (entry == 1) {
		bcopy(sf->hdr.parent, (char *)&ino, sizeof(ino));
		retval = xfs_dir_put_dirent(mp, dbp, ino, "..", 2, bno, entry,
						uio, &done);
		if (!done) {
			uio->uio_offset = XFS_DIR_MAKE_COOKIE(mp, bno, entry);
			return(retval);
		}
		entry++;
	}
	sfe = &sf->list[0];
	for (i = 0; i < entry - 2; i++)
		sfe = XFS_DIR_SF_NEXTENTRY(sfe);
	for (; i < sf->hdr.count; i++, entry++) {
		bcopy(sfe->inumber, (char *)&ino, sizeof(ino));
		retval = xfs_dir_put_dirent(mp, dbp, ino, sfe->name,
						sfe->namelen, bno, entry, uio,
						&done);
		if (!done) {
			uio->uio_offset = XFS_DIR_MAKE_COOKIE(mp, bno, entry);
			return(retval);
		}
		sfe = XFS_DIR_SF_NEXTENTRY(sfe);
	}
	*eofp = 1;
	uio->uio_offset = XFS_DIR_MAKE_COOKIE(mp, bno, entry);
	return(0);
}

/*
 * Look up a name in a shortform directory structure, replace the inode number.
 */
STATIC int
xfs_dir_shortform_replace(xfs_trans_t *trans, struct xfs_dir_name *args)
{
	struct xfs_dir_shortform *sf;
	struct xfs_dir_sf_entry *sfe;
	int i;
	xfs_inode_t *dp;

	dp = args->dp;
	ASSERT(dp->i_flags & XFS_IINLINE);
	sf = (struct xfs_dir_shortform *)dp->i_u1.iu_data;
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


/*========================================================================
 * External routines when dirsize == XFS_LBSIZE(mp).
 *========================================================================*/

/*
 * Create the initial contents of a leaf directory
 * or a leaf in a node directory.
 */
buf_t *
xfs_dir_leaf_create(xfs_trans_t *trans, xfs_inode_t *dp, xfs_fsblock_t blkno)
{
	struct xfs_dir_leafblock *leaf;
	struct xfs_dir_leaf_hdr *hdr;
	buf_t *bp;
	xfs_mount_t *mp;

	bp = xfs_dir_get_buf(trans, dp, blkno);
	ASSERT(bp != NULL);
	leaf = (struct xfs_dir_leafblock *)bp->b_un.b_addr;
	mp = dp->i_mount;
	bzero((char *)leaf, XFS_LBSIZE(mp));
	hdr = &leaf->hdr;
	hdr->info.magic = XFS_DIR_LEAF_MAGIC;
	hdr->firstused = XFS_LBSIZE(mp);
	hdr->freemap[0].base = sizeof(struct xfs_dir_leaf_hdr);
	hdr->freemap[0].size = XFS_LBSIZE(mp) - hdr->freemap[0].base;

	xfs_trans_log_buf(trans, bp, 0, XFS_LBSIZE(mp) - 1);
	return(bp);
}

/*
 * Add a name to the leaf directory structure
 * This is the external routine.
 */
STATIC int
xfs_dir_leaf_addname(xfs_trans_t *trans, struct xfs_dir_name *args)
{
	int index, retval;
	buf_t *bp;

	bp = xfs_dir_read_buf(trans, args->dp, 0);
	ASSERT(bp != NULL);

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
STATIC int
xfs_dir_leaf_removename(xfs_trans_t *trans, struct xfs_dir_name *args,
			    int *count, int *totallen)
{
	struct xfs_dir_leafblock *leaf;
	int index, retval;
	buf_t *bp;

	bp = xfs_dir_read_buf(trans, args->dp, 0);
	ASSERT(bp != NULL);
	leaf = (struct xfs_dir_leafblock *)bp->b_un.b_addr;
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
STATIC int
xfs_dir_leaf_lookup(xfs_trans_t *trans, struct xfs_dir_name *args)
{
	int index, retval;
	buf_t *bp;

	bp = xfs_dir_read_buf(trans, args->dp, 0);
	ASSERT(bp != NULL);
	retval = xfs_dir_leaf_lookup_int(bp, args, &index);
	xfs_trans_brelse(trans, bp);
	return(retval);
}

/*
 * Convert a leaf directory to shortform structure
 */
STATIC int
xfs_dir_leaf_to_shortform(xfs_trans_t *trans, struct xfs_dir_name *iargs)
{
	xfs_inode_t *dp;
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
	xfs_mount_t *mp;

	dp = iargs->dp;
	mp = dp->i_mount;
	tmpbuffer = kmem_alloc(XFS_LBSIZE(mp), KM_SLEEP);
	ASSERT(tmpbuffer != NULL);

	bp = xfs_dir_read_buf(trans, dp, 0);
	ASSERT(bp != NULL);
	bcopy(bp->b_un.b_addr, tmpbuffer, XFS_LBSIZE(mp));
	leaf = (struct xfs_dir_leafblock *)tmpbuffer;
	ASSERT(leaf->hdr.info.magic == XFS_DIR_LEAF_MAGIC);
	bzero(bp->b_un.b_addr, XFS_LBSIZE(mp));

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
	retval = xfs_dir_shrink_inode(trans, iargs, 0, bp);
	if (retval)
		goto out;
	retval = xfs_dir_shortform_create(trans, dp, parent);
	if (retval)
		goto out;

	/*
	 * Copy the rest of the filenames
	 */
	entry = &leaf->leaves[0];
	args.dp = dp;
	args.firstblock = iargs->firstblock;
	args.flist = iargs->flist;
	args.total = iargs->total;
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
	kmem_free(tmpbuffer, XFS_LBSIZE(mp));
	return(retval);
}

/*
 * Convert from using a single leaf to a root node and a leaf.
 */
STATIC int
xfs_dir_leaf_to_node(xfs_trans_t *trans, struct xfs_dir_name *args)
{
	xfs_inode_t *dp;
	struct xfs_dir_leafblock *leaf;
	struct xfs_dir_intnode *node;
	buf_t *bp1, *bp2;
	xfs_fsblock_t blkno;
	int retval;
	xfs_mount_t *mp;

	dp = args->dp;
	mp = dp->i_mount;
	retval = xfs_dir_grow_inode(trans, args, &blkno);
	ASSERT(blkno == 1);
	if (retval)
		return(retval);
	bp1 = xfs_dir_read_buf(trans, dp, 0);
	ASSERT(bp1 != NULL);
	bp2 = xfs_dir_get_buf(trans, dp, 1);
	ASSERT(bp2 != NULL);
	bcopy(bp1->b_un.b_addr, bp2->b_un.b_addr, XFS_LBSIZE(mp));
	xfs_trans_log_buf(trans, bp2, 0, XFS_LBSIZE(mp) - 1);

	/*
	 * Set up the new root node.
	 */
	bp1 = xfs_dir_node_create(trans, dp, 0, 1);
	node = (struct xfs_dir_intnode *)bp1->b_un.b_addr;
	leaf = (struct xfs_dir_leafblock *)bp2->b_un.b_addr;
	ASSERT(leaf->hdr.info.magic == XFS_DIR_LEAF_MAGIC);
	node->btree[0].hashval = leaf->leaves[ leaf->hdr.count-1 ].hashval;
	node->btree[0].before = blkno;
	node->hdr.count = 1;
	xfs_trans_log_buf(trans, bp1, 0, XFS_LBSIZE(mp) - 1);

	return(retval);
}

/*
 * Print the leaf directory.
 */
STATIC void
xfs_dir_leaf_print(xfs_trans_t *trans, xfs_inode_t *dp)
{
	buf_t *bp;

	bp = xfs_dir_read_buf(trans, dp, 0);
	ASSERT(bp != NULL);
	xfs_dir_leaf_print_int(bp, dp);
	xfs_trans_brelse(trans, bp);
}

/*
 * Copy out directory entries for getdents(), for leaf directories.
 */
STATIC int
xfs_dir_leaf_getdents(xfs_trans_t *trans, xfs_inode_t *dp, uio_t *uio,
			int *eofp, dirent_t *dbp)
{
	buf_t *bp;
	int retval, eob;

	bp = xfs_dir_read_buf(trans, dp, 0);
	ASSERT(bp != NULL);
	retval = xfs_dir_leaf_getdents_int(bp, dp, uio, &eob, dbp);
	xfs_trans_brelse(trans, bp);
	*eofp = eob;
	return(retval);
}

/*
 * Look up a name in a leaf directory structure, replace the inode number.
 * This is the external routine.
 */
STATIC int
xfs_dir_leaf_replace(xfs_trans_t *trans, struct xfs_dir_name *args)
{
	int index, retval;
	buf_t *bp;
	xfs_ino_t inum;
	struct xfs_dir_leafblock *leaf;
	struct xfs_dir_leaf_entry *entry;
	struct xfs_dir_leaf_name *namest;

	inum = args->inumber;
	bp = xfs_dir_read_buf(trans, args->dp, 0);
	ASSERT(bp != NULL);
	retval = xfs_dir_leaf_lookup_int(bp, args, &index);
	if (retval == EEXIST) {
		leaf = (struct xfs_dir_leafblock *)bp->b_un.b_addr;
		entry = &leaf->leaves[index];
		namest = XFS_DIR_LEAF_NAMESTRUCT(leaf, entry->nameidx);
		ASSERT(bcmp((char *)&inum, namest->inumber, sizeof(inum)));
		bcopy((char *)&inum, namest->inumber, sizeof(inum));
		xfs_trans_log_buf(trans, bp,
			(char *)namest->inumber - (char *)leaf,
			(char *)namest->inumber - (char *)leaf +
			sizeof(namest->inumber) - 1);
		retval = 0;
	} else
		xfs_trans_brelse(trans, bp);
	return(retval);
}


/*========================================================================
 * External routines when dirsize > XFS_LBSIZE(mp).
 *========================================================================*/

/*
 * Create the initial contents of an intermediate node.
 */
buf_t *
xfs_dir_node_create(xfs_trans_t *trans, xfs_inode_t *dp, xfs_fsblock_t blkno,
			int level)
{
	struct xfs_dir_intnode *node;
	buf_t *bp;
	xfs_mount_t *mp;

	bp = xfs_dir_get_buf(trans, dp, blkno);
	ASSERT(bp != NULL);
	mp = dp->i_mount;
	bzero((char *)bp->b_un.b_addr, XFS_LBSIZE(mp));
	node = (struct xfs_dir_intnode *)bp->b_un.b_addr;
	node->hdr.info.magic = XFS_DIR_NODE_MAGIC;
	node->hdr.level = level;

	xfs_trans_log_buf(trans, bp, 0, XFS_LBSIZE(mp) - 1);

	return(bp);
}

/*
 * Add a name to a int directory.
 *
 * This will involve walking down the Btree, and may involve splitting
 * leaf nodes and even splitting intermediate nodes up to and including
 * the root node (a special case of an intermediate node).
 */
STATIC int
xfs_dir_node_addname(xfs_trans_t *trans, struct xfs_dir_name *args)
{
	struct xfs_dir_state *state;
	struct xfs_dir_state_blk *blk;
	int retval;

	/*
	 * Fill in bucket of arguments/results/context to carry around.
	 */
	state = xfs_dir_state_alloc();
	state->args = args;
	state->mp = args->dp->i_mount;
	state->trans = trans;
	state->blocksize = state->mp->m_sb.sb_blocksize;

	/*
	 * Search to see if name already exists, and get back a pointer
	 * to where it should go.
	 */
	retval = xfs_dir_node_lookup_int(state);
	if (retval != ENOENT) {
		xfs_dir_state_free(state);
		return(retval);
	}
		
	blk = &state->path.blk[ state->path.active-1 ];
	ASSERT(blk->leafblk == 1);
	retval = xfs_dir_leaf_add(state->trans, blk->bp, state->args, blk->index);
	if (retval == 0) {
		/*
		 * Addition succeeded, update Btree hashvals.
		 * GROT: clean this up
		 */
		xfs_dir_fixhashpath(state, &state->path, state->path.active-1);
	} else if (retval == ENOSPC) {
		/*
		 * Addition failed, split as many Btree elements as required.
		 */
		retval = xfs_dir_split(state);
	}
	xfs_dir_state_free(state);

	return(retval);
}

/*
 * Remove a name from a B-tree directory.
 *
 * This will involve walking down the Btree, and may involve joining
 * leaf nodes and even joining intermediate nodes up to and including
 * the root node (a special case of an intermediate node).
 */
STATIC int
xfs_dir_node_removename(xfs_trans_t *trans, struct xfs_dir_name *args)
{
	struct xfs_dir_state *state;
	struct xfs_dir_state_blk *blk;
	int retval;

	state = xfs_dir_state_alloc();
	state->args = args;
	state->mp = args->dp->i_mount;
	state->trans = trans;
	state->blocksize = state->mp->m_sb.sb_blocksize;

	/*
	 * Search to see if name exists, and get back a pointer to it.
	 */
	retval = xfs_dir_node_lookup_int(state);
	if (retval != EEXIST) {
		xfs_dir_state_free(state);
		return(retval);
	}

	/*
	 * Check to see if the tree needs to be collapsed.
	 */
	retval = xfs_dir_join(state);
	xfs_dir_state_free(state);
	return(retval);
}

/*
 * Look up a filename in a int directory.
 * Use an internal routine to actually do all the work.
 */
STATIC int
xfs_dir_node_lookup(xfs_trans_t *trans, struct xfs_dir_name *args)
{
	struct xfs_dir_state *state;
	int retval;
	int i;

	state = xfs_dir_state_alloc();
	state->args = args;
	state->mp = args->dp->i_mount;
	state->trans = trans;
	state->blocksize = state->mp->m_sb.sb_blocksize;

	/*
	 * Search to see if name exists,
	 * and get back a pointer to it.
	 */
	retval = xfs_dir_node_lookup_int(state);

	/* 
	 * If not in a transaction, we have to release all the buffers.
	 */
	for (i = 0; i < state->path.active; i++)
		xfs_trans_brelse(trans, state->path.blk[i].bp);

	xfs_dir_state_free(state);
	return(retval);
}

/*
 * Print the B-tree directory.
 */
STATIC void
xfs_dir_node_print(xfs_trans_t *trans, xfs_inode_t *dp)
{
	__uint32_t bno;
	buf_t *bp;

	bno = 0;
	for (;;) {
		struct xfs_dir_intnode *node;
		struct xfs_dir_node_entry *btree;

		bp = xfs_dir_read_buf(trans, dp, bno);
		ASSERT(bp != NULL);
		node = (struct xfs_dir_intnode *)bp->b_un.b_addr;
		if (node->hdr.info.magic != XFS_DIR_NODE_MAGIC)
			break;
		btree = &node->btree[0];
		bno = btree->before;
		xfs_trans_brelse(trans, bp);
	}
	for (;;) {
		struct xfs_dir_leafblock *leaf;

		xfs_dir_leaf_print_int(bp, dp);
		leaf = (struct xfs_dir_leafblock *)bp->b_un.b_addr;
		bno = leaf->hdr.info.forw;
		xfs_trans_brelse(trans, bp);
		if (bno == 0)
			break;
		bp = xfs_dir_read_buf(trans, dp, bno);
		ASSERT(bp != NULL);
	}
}

STATIC int
xfs_dir_node_getdents(xfs_trans_t *trans, xfs_inode_t *dp, uio_t *uio,
			int *eofp, dirent_t *dbp)
{
	__uint32_t bno;
	buf_t *bp;
	xfs_mount_t *mp;
	int retval, eob;
	struct xfs_dir_leafblock *leaf;

	mp = dp->i_mount;
	if (uio->uio_offset == 0) {
		bno = 0;
		for (;;) {
			struct xfs_dir_intnode *node;
			struct xfs_dir_node_entry *btree;

			bp = xfs_dir_read_buf(trans, dp, bno);
			ASSERT(bp != NULL);
			node = (struct xfs_dir_intnode *)bp->b_un.b_addr;
			if (node->hdr.info.magic != XFS_DIR_NODE_MAGIC)
				break;
			btree = &node->btree[0];
			bno = btree->before;
			xfs_trans_brelse(trans, bp);
		}
	} else {
		bno = XFS_DIR_COOKIE_BNO(mp, uio->uio_offset);
		bp = xfs_dir_read_buf(trans, dp, bno);
		if (bp == NULL)
			return(XFS_ERROR(ENOENT));
		leaf = (struct xfs_dir_leafblock *)bp->b_un.b_addr;
		if (leaf->hdr.info.magic != XFS_DIR_LEAF_MAGIC) {
			xfs_trans_brelse(trans, bp);
			return(XFS_ERROR(ENOENT));
		}
	}
	for (;;) {
		retval = xfs_dir_leaf_getdents_int(bp, dp, uio, &eob, dbp);
		if (!eob) {
			*eofp = 0;
			xfs_trans_brelse(trans, bp);
			return(retval);
		}
		leaf = (struct xfs_dir_leafblock *)bp->b_un.b_addr;
		bno = leaf->hdr.info.forw;
		xfs_trans_brelse(trans, bp);
		if (bno == 0)
			break;
		uio->uio_offset = XFS_DIR_MAKE_COOKIE(mp, bno, 0);
		bp = xfs_dir_read_buf(trans, dp, bno);
		ASSERT(bp != NULL);
	}
	*eofp = 1;
	return(0);
}

/*
 * Look up a filename in a int directory, replace the inode number.
 * Use an internal routine to actually do the lookup.
 */
STATIC int
xfs_dir_node_replace(xfs_trans_t *trans, struct xfs_dir_name *args)
{
	struct xfs_dir_state *state;
	int retval;
	int i;
	struct xfs_dir_state_blk *blk;
	buf_t *bp;
	xfs_ino_t inum;
	struct xfs_dir_leafblock *leaf;
	struct xfs_dir_leaf_entry *entry;
	struct xfs_dir_leaf_name *namest;

	state = xfs_dir_state_alloc();
	state->args = args;
	state->mp = args->dp->i_mount;
	state->trans = trans;
	state->blocksize = state->mp->m_sb.sb_blocksize;
	inum = args->inumber;

	/*
	 * Search to see if name exists,
	 * and get back a pointer to it.
	 */
	retval = xfs_dir_node_lookup_int(state);

	if (retval == EEXIST) {
		blk = &state->path.blk[state->path.active - 1];
		ASSERT(blk->leafblk);
		bp = blk->bp;
		leaf = (struct xfs_dir_leafblock *)bp->b_un.b_addr;
		entry = &leaf->leaves[blk->index];
		namest = XFS_DIR_LEAF_NAMESTRUCT(leaf, entry->nameidx);
		ASSERT(bcmp((char *)&inum, namest->inumber, sizeof(inum)));
		bcopy((char *)&inum, namest->inumber, sizeof(inum));
		xfs_trans_log_buf(trans, bp,
			(char *)namest->inumber - (char *)leaf,
			(char *)namest->inumber - (char *)leaf +
			sizeof(namest->inumber) - 1);
		retval = 0;
	} else {
		i = state->path.active - 1;
		xfs_trans_brelse(trans, state->path.blk[i].bp);
	}
	for (i = 0; i < state->path.active - 1; i++)
		xfs_trans_brelse(trans, state->path.blk[i].bp);

	xfs_dir_state_free(state);
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
xfs_dir_grow_inode(xfs_trans_t *trans, struct xfs_dir_name *args,
			xfs_fsblock_t *new_blkno)
{
	xfs_inode_t *dp;
	xfs_fsblock_t oldsize, newsize;
	xfs_mount_t *mp;
	xfs_bmbt_irec_t map;
	int nmap;
	xfs_fsblock_t bno;

	dp = args->dp;
	mp = dp->i_mount;
	bno = xfs_bmap_first_unused(trans, dp);
	nmap = 1;
	ASSERT(args->firstblock != NULL);
	*(args->firstblock) = xfs_bmapi(trans, dp, bno, 1,
		XFS_BMAPI_WRITE | XFS_BMAPI_METADATA, *(args->firstblock),
		args->total, &map, &nmap, args->flist);
	if (nmap < 1)
		return(XFS_ERROR(ENOSPC));
	*new_blkno = bno;
	dp->i_d.di_size = XFS_FSB_TO_B(mp, xfs_bmap_last_offset(trans, dp));

	xfs_trans_log_inode(trans, dp, XFS_ILOG_CORE);

	return(0);
}

int
xfs_dir_shrink_inode(xfs_trans_t *trans, struct xfs_dir_name *args,
			xfs_fsblock_t dead_blkno, buf_t *dead_buf)
{
	xfs_inode_t *dp;
	xfs_mount_t *mp;
	int done;

	dp = args->dp;
	mp = dp->i_mount;
	*(args->firstblock) = xfs_bunmapi(trans, dp, dead_blkno, 1, 1,
					  *(args->firstblock), args->flist,
					  &done);
	ASSERT(done);
	xfs_trans_binval(trans, dead_buf);
	dp->i_d.di_size = XFS_FSB_TO_B(mp, xfs_bmap_last_offset(trans, dp));

	xfs_trans_log_inode(trans, dp, XFS_ILOG_CORE);

	return(0);
}

buf_t *
xfs_dir_get_buf(xfs_trans_t *trans, xfs_inode_t *dp, xfs_fsblock_t bno)
{
	xfs_bmbt_irec_t map;
	int nmap;

	nmap = 1;
	(void)xfs_bmapi(trans, dp, bno, 1, 0, NULLFSBLOCK, 0, &map, &nmap, 0);
	ASSERT(nmap == 1);
	ASSERT((map.br_startblock != DELAYSTARTBLOCK) &&
	       (map.br_startblock != HOLESTARTBLOCK));
	return(xfs_btree_get_bufl(dp->i_mount, trans, map.br_startblock, 0));
}

buf_t *
xfs_dir_read_buf(xfs_trans_t *trans, xfs_inode_t *dp, xfs_fsblock_t bno)
{
	xfs_bmbt_irec_t map;
	int nmap;

	nmap = 1;
	(void)xfs_bmapi(trans, dp, bno, 1, 0, NULLFSBLOCK, 0, &map, &nmap, 0);
	if (nmap == 0)
		return NULL;
	ASSERT(nmap == 1);
	ASSERT(map.br_startblock != DELAYSTARTBLOCK);
	if (map.br_startblock == HOLESTARTBLOCK)
		return NULL;
	return(xfs_btree_read_bufl(dp->i_mount, trans, map.br_startblock, 0));
}

/*
 * Calculate the number of bits needed to hold i different values.
 */
STATIC uint
xfs_dir_log2_roundup(uint i)
{
	uint rval;

	for (rval = 0; rval < NBBY * sizeof(i); rval++) {
		if ((1 << rval) >= i)
			break;
	}
	return(rval);
}

/*
 * Format a dirent structure and copy it out the the user's buffer.
 * A 32-bit process has a differently sized dirent structure than
 * the kernel does, so do the translation here based on the process'
 * abi.
 */
int
xfs_dir_put_dirent(
	xfs_mount_t	*mp,
	dirent_t	*dbp,
	xfs_ino_t	ino,
	char		*name,
	int		namelen,
	__uint32_t	bno,
	int		entry,
	uio_t		*uio,
	int		*done)
{
	irix5_dirent_t	*i5_dbp;
	int		retval;
	int		target_abi;

	/*
	 * If it's a kernel request, then the target abi is
	 * IRIX5_64.
	 */
	if (uio->uio_segflg == UIO_USERSPACE) {
		target_abi = u.u_procp->p_abi;
	} else {
		target_abi = ABI_IRIX5_64;
	}

	if (ABI_IS_IRIX5_64(target_abi)) {
		if ((dbp->d_reclen = DIRENTSIZE(namelen)) > uio->uio_resid) {
			*done = 0;
			retval = 0;
		} else {
			dbp->d_ino = ino;
			bcopy(name, dbp->d_name, namelen);
			dbp->d_name[namelen] = '\0';
			dbp->d_off = XFS_DIR_MAKE_COOKIE(mp, bno, entry);
			retval = uiomove((caddr_t)dbp, dbp->d_reclen,
					 UIO_READ, uio);
			*done = (retval == 0);
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
			i5_dbp->d_off = XFS_DIR_MAKE_COOKIE(mp, bno, entry);
			retval = uiomove((caddr_t)i5_dbp, i5_dbp->d_reclen,
					 UIO_READ, uio);
			*done = (retval == 0);
		}
	}
	return(retval);
}

/*
 * Allocate a dir-state structure.
 * We don't put them on the stack since they're large.
 */
STATIC struct xfs_dir_state *
xfs_dir_state_alloc()
{
	return kmem_zone_zalloc(xfs_dir_state_zone, KM_SLEEP);
}

/*
 * Free a dir-state structure.
 */
STATIC void
xfs_dir_state_free(struct xfs_dir_state *state)
{
	kmem_zone_free(xfs_dir_state_zone, state);
}
