#ifndef _FS_XFS_ATTR_LEAF_H
#define	_FS_XFS_ATTR_LEAF_H

#ident	"$Revision: 1.12 $"

/*
 * xfs_attr_leaf.h
 *
 * Attribute storage layout, internal structure, access macros, etc.
 *
 * Attribute lists are structured around Btrees where all the data
 * elements are in the leaf nodes.  Attribute names are hashed into an int,
 * then that int is used as the index into the Btree.  Since the hashval
 * of an attribute name may not be unique, we may have duplicate keys.  The
 * internal links in the Btree are logical block offsets into the file.
 */

/*========================================================================
 * Attribute structure when equal to XFS_LBSIZE(mp) bytes.
 *========================================================================*/

/*
 * This is the structure of the leaf nodes in the Btree.
 *
 * Struct leaf_entry's are packed from the top.  Name/values grow from the
 * bottom but are not packed.  The freemap contains run-length-encoded entries
 * for the free bytes after the leaf_entry's, but only the N largest such,
 * smaller runs are dropped.  When the freemap doesn't show enough space
 * for an allocation, we compact the name/value area and try again.  If we
 * still don't have enough space, then we have to split the block.  The
 * name/value structs (both local and remote versions) must be 32bit aligned.
 *
 * Since we have duplicate hash keys, for each key that matches, compare
 * the actual name string.  The root and intermediate node search always
 * takes the first-in-the-block key match found, so we should only have
 * to work "forw"ard.  If none matches, continue with the "forw"ard leaf
 * nodes until the hash key changes or the attribute name is found.
 */
#define XFS_ATTR_LEAF_MAPSIZE	3	/* how many freespace slots */

typedef struct xfs_attr_leafblock {
	struct xfs_attr_leaf_hdr {	/* constant-structure header block */
		xfs_da_blkinfo_t info;	/* block type, links, etc. */
		__uint16_t count;	/* count of active leaf_entry's */
		__uint16_t usedbytes;	/* num bytes of names/values stored */
		__uint16_t firstused;	/* first used byte in name area */
		__uint8_t  holes;	/* != 0 if blk needs compaction */
		__uint8_t  pad1;
		struct xfs_attr_leaf_map {	  /* RLE map of free bytes */
			__uint16_t base;	  /* base of free region */
			__uint16_t size;	  /* length of free region */
		} freemap[XFS_ATTR_LEAF_MAPSIZE]; /* N largest free regions */
	} hdr;
	struct xfs_attr_leaf_entry {	/* sorted on key, not name */
		__uint32_t hashval;	/* hash value of name */
		__uint16_t nameidx;	/* index into buffer of name/value */
		__uint8_t local;	/* 1=>struct local; 0=>struct remote */
		__uint8_t pad2;
	} entries[1];			/* variable sized array */
	struct xfs_attr_leaf_name_local {
		__uint16_t valuelen;	/* number of bytes in value */
		__uint8_t namelen;	/* length of name bytes */
		__uint8_t nameval[1];	/* name/value bytes */
	} namelist;			/* grows from bottom of buf */
	struct xfs_attr_leaf_name_remote {
		__uint32_t valueblk;	/* block number of value bytes */
		__uint32_t valuelen;	/* number of bytes in value */
		__uint8_t namelen;	/* length of name bytes */
		__uint8_t name[1];	/* name bytes */
	} valuelist;			/* grows from bottom of buf */
} xfs_attr_leafblock_t;
typedef struct xfs_attr_leaf_hdr xfs_attr_leaf_hdr_t;
typedef struct xfs_attr_leaf_map xfs_attr_leaf_map_t;
typedef struct xfs_attr_leaf_entry xfs_attr_leaf_entry_t;
typedef struct xfs_attr_leaf_name_local xfs_attr_leaf_name_local_t;
typedef struct xfs_attr_leaf_name_remote xfs_attr_leaf_name_remote_t;

/*
 * Cast typed pointers for "local" and "remote" name/value structs.
 */
 /* GROT: fix all uses of ENTRY_NAME to know about local vs. remote */
#define XFS_ATTR_LEAF_NAME_REMOTE(LEAFP, IDX)	/* remote name struct ptr */ \
	((xfs_attr_leaf_name_remote_t *)		\
	 &((char *)(LEAFP))[ (LEAFP)->entries[IDX].nameidx ])
#define XFS_ATTR_LEAF_NAME_LOCAL(LEAFP, IDX)	/* local name struct ptr */ \
	((xfs_attr_leaf_name_local_t *)		\
	 &((char *)(LEAFP))[ (LEAFP)->entries[IDX].nameidx ])
#define XFS_ATTR_LEAF_NAME(LEAFP, IDX)		/* generic name struct ptr */ \
	(&((char *)(LEAFP))[ (LEAFP)->entries[IDX].nameidx ])

/*
 * Calculate total bytes used (including trailing pad for alignment) for
 * a "local" name/value structure, a "remote" name/value structure, and
 * a pointer which might be either.
 */
#define XFS_ATTR_LEAF_ENTSIZE_REMOTE(NLEN)	/* space for remote struct */ \
	(((sizeof(xfs_attr_leaf_name_remote_t)-1 + (NLEN)) +3)&~0x3)
#define XFS_ATTR_LEAF_ENTSIZE_LOCAL(NLEN,VLEN)	/* space for local struct */ \
	(((sizeof(xfs_attr_leaf_name_local_t)-1 + (NLEN)+(VLEN)) +3)&~0x3)
#define XFS_ATTR_LEAF_ENTSIZE_LOCAL_MAX(BSIZE)	/* max local struct size */ \
	((BSIZE) >> 1)


/*========================================================================
 * Function prototypes for the kernel.
 *========================================================================*/

struct xfs_da_name;
struct uio;
struct xfs_bmap_free;

/*
 * Internal routines when dirsize < XFS_LITINO(mp).
 */
int	xfs_attr_shortform_create(xfs_trans_t *trans, xfs_inode_t *dp);
int	xfs_attr_shortform_addname(xfs_trans_t *trans, xfs_da_name_t *add);
int	xfs_attr_shortform_lookup(xfs_trans_t *trans, xfs_da_name_t *args);
int	xfs_attr_shortform_getvalue(xfs_trans_t *trans, xfs_da_name_t *args);
int	xfs_attr_shortform_to_leaf(xfs_trans_t *trans, xfs_da_name_t *args);
int	xfs_attr_shortform_removename(xfs_trans_t *trans,
						  xfs_da_name_t *remove);
void	xfs_attr_shortform_print(xfs_trans_t *trans, xfs_inode_t *dp);
int	xfs_attr_shortform_list(xfs_inode_t *dp, attrlist_t *alist,
					    attrlist_cursor_kern_t *cursor);
int	xfs_attr_shortform_replace(xfs_trans_t *trans, xfs_da_name_t *args);
int	xfs_attr_shortform_allfit(buf_t *bp, xfs_inode_t *dp);

/*
 * Internal routines when dirsize == XFS_LBSIZE(mp).
 */
int	xfs_attr_leaf_to_node(xfs_trans_t *trans, xfs_da_name_t *args);
int	xfs_attr_leaf_to_shortform(xfs_trans_t *trans, xfs_da_name_t *args);

/*
 * Routines used for growing the Btree.
 */
int	xfs_attr_leaf_create(xfs_trans_t *trans, xfs_inode_t *dp,
					 xfs_fileoff_t which_block,
					 buf_t **bpp);
int	xfs_attr_leaf_split(xfs_da_state_t *state, xfs_da_state_blk_t *oldblk,
					   xfs_da_state_blk_t *newblk);
int	xfs_attr_leaf_lookup_int(buf_t *leaf_buffer, xfs_da_name_t *args,
				       int *index_found_at);
int	xfs_attr_leaf_getvalue(buf_t *bp, xfs_da_name_t *args, int index);
int	xfs_attr_leaf_add(xfs_trans_t *trans, buf_t *leaf_buffer,
				      xfs_da_name_t *args, int insertion_index);
int	xfs_attr_leaf_addname(xfs_trans_t *trans, xfs_da_name_t *args);
int	xfs_attr_leaf_remove(xfs_trans_t *trans, buf_t *leaf_buffer,
					int index_to_remove, int *result);
int	xfs_attr_leaf_list_int(buf_t *bp, attrlist_t *alist,
				     attrlist_cursor_kern_t *cursor);
void	xfs_attr_leaf_print_int(buf_t *leaf_buffer, xfs_inode_t *attr_inode);
/*
 * Routines used for shrinking the Btree.
 */
int	xfs_attr_leaf_toosmall(xfs_da_state_t *state, int *retval);
void	xfs_attr_leaf_unbalance(xfs_da_state_t *state,
					       xfs_da_state_blk_t *drop_blk,
					       xfs_da_state_blk_t *save_blk);

/*
 * Utility routines.
 */
uint	xfs_attr_leaf_lasthash(buf_t *bp, int *count);
int	xfs_attr_leaf_order(buf_t *leaf1_bp, buf_t *leaf2_bp);
int	xfs_attr_leaf_newentsize(xfs_da_name_t *args, int blocksize,
					       int *local);
int	xfs_attr_leaf_entsize(xfs_attr_leafblock_t *leaf, int index);
int	xfs_attr_put_listent(attrlist_t *alist, char *name, int namelen,
				int valuelen, attrlist_cursor_kern_t *cursor);

#endif	/* !FS_XFS_ATTR_LEAE_H */
