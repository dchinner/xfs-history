#ifndef _FS_XFS_ATTR_LEAF_H
#define	_FS_XFS_ATTR_LEAF_H

#ident	"$Revision: 1.5 $"

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
 *
 * We store the fact that an attribute is a ROOT versus USER attribute in
 * the leaf_entry.  The namespaces are independent only because we also look
 * at the root/user bit when we are looking for a matching attribute name.
 *
 * We also store a "incomplete" bit in the leaf_entry.  It shows that an
 * attribute is in the middle of being created and should not be shown to
 * the user if we crash during the time that the bit is set.  We clear the
 * bit when we have finished setting up the attribute.  We do this because
 * we cannot create some large attributes inside a single transaction, and we
 * need some indication that we weren't finished if we crash in the middle.
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
		__uint8_t flags;	/* LOCAL, ROOT and INCOMPLETE flags */
		__uint8_t pad2;		/* unused pad byte */
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
 * Flags used in the leaf_entry[i].flags field.
 * NOTE: the INCOMPLETE bit must not collide with the flags bits specified
 * on the system call, they are "or"ed together for various operations.
 */
#define	XFS_ATTR_LOCAL_BIT	0	/* attr is stored locally */
#define	XFS_ATTR_ROOT_BIT	1	/* limit access to attr to userid 0 */
#define	XFS_ATTR_INCOMPLETE_BIT	7	/* attr in middle of create/delete */
#define XFS_ATTR_LOCAL		(1 << XFS_ATTR_LOCAL_BIT)
#define XFS_ATTR_ROOT		(1 << XFS_ATTR_ROOT_BIT)
#define XFS_ATTR_INCOMPLETE	(1 << XFS_ATTR_INCOMPLETE_BIT)

/*
 * Cast typed pointers for "local" and "remote" name/value structs.
 */
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

struct attrlist;
struct attrlist_cursor_kern;
struct buf;
struct xfs_da_args;
struct xfs_da_state;
struct xfs_da_state_blk;
struct xfs_inode;
struct xfs_trans;

/*
 * Internal routines when dirsize < XFS_LITINO(mp).
 */
int	xfs_attr_shortform_create(struct xfs_trans *trans,
					 struct xfs_inode *dp);
int	xfs_attr_shortform_add(struct xfs_trans *trans,
				      struct xfs_da_args *add);
int	xfs_attr_shortform_lookup(struct xfs_trans *trans,
					 struct xfs_da_args *args);
int	xfs_attr_shortform_getvalue(struct xfs_da_args *args);
int	xfs_attr_shortform_to_leaf(struct xfs_trans *trans,
					  struct xfs_da_args *args);
int	xfs_attr_shortform_remove(xfs_trans_t *trans,
					      struct xfs_da_args *remove);
int	xfs_attr_shortform_list(struct xfs_inode *dp, struct attrlist *alist,
				       int flags,
				       struct attrlist_cursor_kern *cursor);
int	xfs_attr_shortform_replace(struct xfs_trans *trans,
					  struct xfs_da_args *args);
int	xfs_attr_shortform_allfit(struct buf *bp, struct xfs_inode *dp);

/*
 * Internal routines when dirsize == XFS_LBSIZE(mp).
 */
int	xfs_attr_leaf_to_node(struct xfs_trans *trans,
				     struct xfs_da_args *args);
int	xfs_attr_leaf_to_shortform(struct xfs_trans *trans, buf_t *bp,
					  struct xfs_da_args *args);
int	xfs_attr_leaf_clearflag(struct xfs_da_args *args);
int	xfs_attr_leaf_setflag(struct xfs_da_args *args);
int	xfs_attr_leaf_flipflags(xfs_da_args_t *args);

/*
 * Routines used for growing the Btree.
 */
int	xfs_attr_leaf_create(struct xfs_trans *trans, struct xfs_inode *dp,
				    xfs_fileoff_t which_block,
				    struct buf **bpp);
int	xfs_attr_leaf_split(struct xfs_da_state *state,
				   struct xfs_da_state_blk *oldblk,
				   struct xfs_da_state_blk *newblk);
int	xfs_attr_leaf_lookup_int(struct buf *leaf, struct xfs_da_args *args);
int	xfs_attr_leaf_getvalue(struct buf *bp, struct xfs_da_args *args);
int	xfs_attr_leaf_add(struct xfs_trans *trans, struct buf *leaf_buffer,
				 struct xfs_da_args *args);
int	xfs_attr_leaf_addname(struct xfs_da_args *args);
int	xfs_attr_leaf_remove(struct xfs_trans *trans, struct buf *leaf_buffer,
				    struct xfs_da_args *args);
int	xfs_attr_leaf_list_int(struct buf *bp, struct attrlist *alist,
				      int flags,
				      struct attrlist_cursor_kern *cursor);

/*
 * Routines used for shrinking the Btree.
 */
int	xfs_attr_leaf_toosmall(struct xfs_da_state *state, int *retval);
void	xfs_attr_leaf_unbalance(struct xfs_da_state *state,
				       struct xfs_da_state_blk *drop_blk,
				       struct xfs_da_state_blk *save_blk);

/*
 * Utility routines.
 */
uint	xfs_attr_leaf_lasthash(struct buf *bp, int *count);
int	xfs_attr_leaf_order(struct buf *leaf1_bp, struct buf *leaf2_bp);
int	xfs_attr_leaf_newentsize(struct xfs_da_args *args, int blocksize,
					int *local);
int	xfs_attr_leaf_entsize(struct xfs_attr_leafblock *leaf, int index);
int	xfs_attr_put_listent(struct attrlist *alist, char *name, int namelen,
				    int valuelen,
				    struct attrlist_cursor_kern *cursor);

#endif	/* !FS_XFS_ATTR_LEAE_H */
