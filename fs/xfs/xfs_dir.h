#ifndef _FS_XFS_DIR_H
#define	_FS_XFS_DIR_H

#ident	"$Revision: 1.19 $"

/*
 * xfs_dir.h
 *
 * Directory layout, internal structure, access macros, etc.
 *
 * Large directories are structured around Btrees where all the data
 * elements are in the leaf nodes.  Filenames are hashed into an int,
 * then that int is used as the index into the Btree.  Since the hashval
 * of a filename may not be unique, we may have duplicate keys.  The
 * internal links in the Btree are logical block offsets into the file.
 *
 * Small directories use a different format and are packed as tightly
 * as possible so as to fit into the literal area of the inode.
 */

typedef	__uint8_t xfs_dir_ino_t[sizeof(xfs_ino_t)];

/*========================================================================
 * Directory Structure when smaller than XFS_LITINO(mp) bytes.
 *========================================================================*/

/*
 * The parent directory has a dedicated field, and the self-pointer must
 * be calculated on the fly.
 *
 * Entries are packed toward the top as tight as possible.  The header
 * and the elements much be bcopy()'d out into a work area to get correct
 * alignment for the inode number fields.
 */
struct xfs_dir_shortform {
	struct xfs_dir_sf_hdr {		/* constant-structure header block */
		xfs_dir_ino_t parent;	/* parent dir inode number */
		__uint8_t count;	/* count of active entries */
	} hdr;
	struct xfs_dir_sf_entry {
		xfs_dir_ino_t inumber;	/* referenced inode number */
		__uint8_t namelen;	/* actual length of name (no NULL) */
		__uint8_t name[1];	/* name */
	} list[1];			/* variable sized array */
};

#define XFS_DIR_SF_ENTSIZE_BYNAME(LEN)		/* space a name uses */ \
	(sizeof(struct xfs_dir_sf_entry)-1 + (LEN))
#define XFS_DIR_SF_ENTSIZE_BYENTRY(SFEP)	/* space an entry uses */ \
	(sizeof(struct xfs_dir_sf_entry)-1 + (SFEP)->namelen)
#define XFS_DIR_SF_NEXTENTRY(SFEP)		/* next entry in struct */ \
	((struct xfs_dir_sf_entry *) \
		((char *)(SFEP) + XFS_DIR_SF_ENTSIZE_BYENTRY(SFEP)))
#define XFS_DIR_SF_ALLFIT(COUNT, TOTALLEN)	/* will all entries fit? */ \
	(sizeof(struct xfs_dir_sf_hdr) + \
	       (sizeof(struct xfs_dir_sf_entry)-1)*(COUNT) + (TOTALLEN))

/*========================================================================
 * Directory Structure when equal to XFS_LBSIZE(mp) bytes.
 *========================================================================*/

/*
 * This structure is common to both leaf nodes and non-leaf nodes in the Btree.
 *
 * Is is used to manage a doubly linked list of all blocks at the same
 * level in the Btree, and to identify which type of block this is.
 */
#define XFS_DIR_LEAF_MAGIC	0xfeeb	/* magic number for leaf blocks */
#define XFS_DIR_NODE_MAGIC	0xfebe	/* magic number for non-leaf blocks */

struct xfs_dir_blkinfo {
	__uint32_t forw;			/* previous block in list */
	__uint32_t back;			/* following block in list */
	__uint16_t magic;			/* validity check on block */
};

/*
 * This is the structure of the leaf nodes in the Btree.
 *
 * Struct leaf_entry's are packed from the top.  Names grow from the bottom
 * but are not packed.  The freemap contains run-length-encoded entries
 * for the free bytes after the leaf_entry's, but only the N largest such,
 * smaller runs are dropped.  When the freemap doesn't show enough space
 * for an allocation, we compact the namelist area and try again.  If we
 * still don't have enough space, then we have to split the block.
 *
 * Since we have duplicate hash keys, for each key that matches, compare
 * the actual string.  The root and intermediate node search always takes
 * the first-in-the-block key match found, so we should only have to work
 * "forw"ard.  If none matches, continue with the "forw"ard leaf nodes
 * until the hash key changes or the filename is found.
 *
 * The parent directory and the self-pointer are explicitly represented
 * (ie: there are entries for "." and "..").
 *
 * Note that the count being a __uint16_t limits us to something like a 
 * blocksize of 1.3MB in the face of worst case (short) filenames.
 */
#define XFS_DIR_LEAF_MAPSIZE	3	/* how many freespace slots */

struct xfs_dir_leafblock {
	struct xfs_dir_leaf_hdr {	/* constant-structure header block */
		struct xfs_dir_blkinfo info;	/* block type, links, etc. */
		__uint16_t count;	/* count of active leaf_entry's */
		__uint16_t namebytes;	/* num bytes of name strings stored */
		__uint16_t firstused;	/* first used byte in name area */
		__uint8_t  holes;	/* != 0 if blk needs compaction */
		__uint8_t  pad1;
		struct xfs_dir_leaf_map {/* RLE map of free bytes */
			__uint16_t base; /* base of free region */
			__uint16_t size; /* run length of free region */
		} freemap[XFS_DIR_LEAF_MAPSIZE]; /* N largest free regions */
	} hdr;
	struct xfs_dir_leaf_entry {	/* sorted on key, not name */
		__uint32_t hashval;	/* hash value of name */
		__uint16_t nameidx;	/* index into buffer of name */
		__uint8_t namelen;	/* length of name string */
		__uint8_t pad2;
	} leaves[1];			/* var sized array */
	struct xfs_dir_leaf_name {
		xfs_dir_ino_t inumber;	/* inode number for this key */
		__uint8_t name[1];	/* name string itself */
	} namelist[1];			/* grows from bottom of buf */
};

#define XFS_DIR_LEAF_ENTSIZE_BYNAME(LEN)	/* space a name will use */ \
	(sizeof(struct xfs_dir_leaf_name)-1 + LEN)
#define XFS_DIR_LEAF_ENTSIZE_BYENTRY(ENTRY)	/* space an entry will use */ \
	(sizeof(struct xfs_dir_leaf_name)-1 + (ENTRY)->namelen)
#define XFS_DIR_LEAF_NAMESTRUCT(LEAFP, OFFSET)	/* point to name struct */ \
	((struct xfs_dir_leaf_name *)&((char *)(LEAFP))[OFFSET])

/*========================================================================
 * Directory Structure when greater than XFS_LBSIZE(mp) bytes.
 *========================================================================*/

/*
 * This is the structure of the root and intermediate nodes in the Btree.
 * The leaf nodes are defined above.
 *
 * Entries are not packed.
 *
 * Since we have duplicate keys, use a binary search but always follow
 * all match in the block, not just the first match found.
 */
#define	XFS_DIR_NODE_MAXDEPTH	5	/* max depth of Btree */

struct xfs_dir_intnode {
	struct xfs_dir_node_hdr {	/* constant-structure header block */
		struct xfs_dir_blkinfo info;	/* block type, links, etc. */
		__uint16_t count;	/* count of active entries */
		__uint16_t level;	/* level above leaves (leaf == 0) */
	} hdr;
	struct xfs_dir_node_entry {
		__uint32_t hashval;	/* hash value for this descendant */
		__uint32_t before;	/* Btree block before this key */
	} btree[1];			/* variable sized array of keys */
};

#define XFS_DIR_NODE_ENTSIZE_BYNAME()	/* space a name uses */ \
	(sizeof(struct xfs_dir_node_entry))
#define XFS_DIR_NODE_ENTRIES(mp)	/* how many entries in this block? */ \
	((XFS_LBSIZE(mp) - sizeof(struct xfs_dir_node_hdr)) \
		   / sizeof(struct xfs_dir_node_entry))
#define XFS_DIR_MAXBLK		0x10000000	/* max hash value */

/*
 * Macros used by directory code to interface to the filesystem.
 */
#define	XFS_LBSIZE(mp)	((mp)->m_sb.sb_blocksize)
#define	XFS_LBLOG(mp)	((mp)->m_sb.sb_blocklog)

/*
 * Macros used to manipulate directory off_t's
 */
#define	XFS_DIR_MAKE_COOKIE(mp, bno, entry) \
	(((bno) << (mp)->m_dircook_elog) | (entry))
#define	XFS_DIR_COOKIE_BNO(mp, cookie) \
	((cookie) >> (mp)->m_dircook_elog)
#define	XFS_DIR_COOKIE_ENTRY(mp, cookie) \
	((cookie) & ((1 << (mp)->m_dircook_elog) - 1))


/*========================================================================
 * Function prototypes for the kernel.
 *========================================================================*/

struct xfs_dir_name;
struct uio;
struct dirent;
struct xfs_bmap_free;

/*
 * Internal routines when dirsize == XFS_LBSIZE(mp).
 */
buf_t	*xfs_dir_leaf_create(xfs_trans_t *trans, xfs_inode_t *dp,
				xfs_fileoff_t which_block);

/*
 * Internal routines when dirsize > XFS_LBSIZE(mp).
 */
buf_t	*xfs_dir_node_create(xfs_trans_t *trans, xfs_inode_t *dp,
				xfs_fileoff_t which_block, int leaf_block_next);

/*
 * Utility routines.
 */
int	xfs_dir_grow_inode(xfs_trans_t *trans, struct xfs_dir_name *args,
				xfs_fileoff_t *new_logblock);
#ifndef SIM
int	xfs_dir_shrink_inode(xfs_trans_t *trans, struct xfs_dir_name *args,
				xfs_fileoff_t dead_logblock, buf_t *dead_buf);
#endif	/* !SIM */
buf_t	*xfs_dir_get_buf(xfs_trans_t *trans, xfs_inode_t *dp,
				xfs_fileoff_t bno);
buf_t	*xfs_dir_read_buf(xfs_trans_t *trans, xfs_inode_t *dp,
				xfs_fileoff_t bno);
#ifndef SIM
int	xfs_dir_put_dirent(xfs_mount_t *mp, struct dirent *dbp, xfs_ino_t ino,
				char *name, int namelen, off_t nextcook,
				uio_t *uio, int *done);
#endif	/* !SIM */

/*
 * Overall external interface routines.
 */
void	xfs_dir_mount(xfs_mount_t *mp);

int	xfs_dir_isempty(xfs_inode_t *dp);

int	xfs_dir_init(xfs_trans_t *trans,
		     xfs_inode_t *dir,
		     xfs_inode_t *parent_dir);

int	xfs_dir_createname(xfs_trans_t *trans,
			   xfs_inode_t *dp,
			   char *name_string,
			   xfs_ino_t inode_number,
			   xfs_fsblock_t *firstblock,
			   struct xfs_bmap_free *flist,
			   xfs_extlen_t total);

#ifndef SIM
int	xfs_dir_removename(xfs_trans_t *trans,
			   xfs_inode_t *dp,
			   char *name_string,
			   xfs_fsblock_t *firstblock,
			   struct xfs_bmap_free *flist,
			   xfs_extlen_t total);
#endif	 /* !SIM */

int	xfs_dir_lookup(xfs_trans_t *tp,
		       xfs_inode_t *dp,
		       char *name_string,
		       int name_length,
		       xfs_ino_t *inode_number);

#ifndef SIM
void	xfs_dir_print(xfs_trans_t *tp,
		      xfs_inode_t *dp);

int	xfs_dir_getdents(xfs_trans_t *tp,
			 xfs_inode_t *dp,
			 struct uio *uiop,
			 int *eofp);

int	xfs_dir_replace(xfs_trans_t *tp,
			xfs_inode_t *dp,
			char *name_string,
			int name_length,
			xfs_ino_t inode_number);
#endif	/* !SIM */

#endif	/* !_FS_XFS_DIR_H */
