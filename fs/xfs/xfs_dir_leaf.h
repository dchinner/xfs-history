#ifndef _FS_XFS_DIR_LEAF_H
#define	_FS_XFS_DIR_LEAF_H

#ident	"$Revision: 1.12 $"

/*
 * xfs_dir_leaf.h
 *
 * Directory layout, internal structure, access macros, etc.
 *
 * Large directories are structured around Btrees where all the data
 * elements are in the leaf nodes.  Filenames are hashed into an int,
 * then that int is used as the index into the Btree.  Since the hashval
 * of a filename may not be unique, we may have duplicate keys.  The
 * internal links in the Btree are logical block offsets into the file.
 */

/*========================================================================
 * Directory Structure when equal to XFS_LBSIZE(mp) bytes.
 *========================================================================*/

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

typedef struct xfs_dir_leafblock {
	struct xfs_dir_leaf_hdr {	/* constant-structure header block */
		xfs_da_blkinfo_t info;	/* block type, links, etc. */
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
	} entries[1];			/* var sized array */
	struct xfs_dir_leaf_name {
		xfs_dir_ino_t inumber;	/* inode number for this key */
		__uint8_t name[1];	/* name string itself */
	} namelist[1];			/* grows from bottom of buf */
} xfs_dir_leafblock_t;
typedef struct xfs_dir_leaf_hdr xfs_dir_leaf_hdr_t;
typedef struct xfs_dir_leaf_map xfs_dir_leaf_map_t;
typedef struct xfs_dir_leaf_entry xfs_dir_leaf_entry_t;
typedef struct xfs_dir_leaf_name xfs_dir_leaf_name_t;

#define XFS_DIR_LEAF_ENTSIZE_BYNAME(LEN)	/* space a name will use */ \
	(sizeof(xfs_dir_leaf_name_t)-1 + LEN)
#define XFS_DIR_LEAF_ENTSIZE_BYENTRY(ENTRY)	/* space an entry will use */ \
	(sizeof(xfs_dir_leaf_name_t)-1 + (ENTRY)->namelen)
#define XFS_DIR_LEAF_NAMESTRUCT(LEAFP, OFFSET)	/* point to name struct */ \
	((xfs_dir_leaf_name_t *)&((char *)(LEAFP))[OFFSET])

/*========================================================================
 * Function prototypes for the kernel.
 *========================================================================*/

struct xfs_da_name;
struct uio;
struct dirent;
struct xfs_bmap_free;

/*
 * Internal routines when dirsize < XFS_LITINO(mp).
 */
int xfs_dir_shortform_create(xfs_trans_t *trans, xfs_inode_t *dp,
					 xfs_ino_t parent_inumber);
int xfs_dir_shortform_addname(xfs_trans_t *trans, xfs_da_name_t *add);
int xfs_dir_shortform_lookup(xfs_trans_t *trans, xfs_da_name_t *args);
int xfs_dir_shortform_to_leaf(xfs_trans_t *trans, xfs_da_name_t *args);
#ifndef SIM
int xfs_dir_shortform_removename(xfs_trans_t *trans, xfs_da_name_t *remove);
void xfs_dir_shortform_print(xfs_trans_t *trans, xfs_inode_t *dp);
int xfs_dir_shortform_getdents(xfs_trans_t *trans, xfs_inode_t *dp,
					   uio_t *uio, int *eofp,
					   dirent_t *dbp);
int xfs_dir_shortform_replace(xfs_trans_t *trans, xfs_da_name_t *args);
#endif	/* !SIM */

/*
 * Internal routines when dirsize == XFS_LBSIZE(mp).
 */
int xfs_dir_leaf_to_node(xfs_trans_t *trans, xfs_da_name_t *args);
#ifndef SIM
int xfs_dir_leaf_to_shortform(xfs_trans_t *trans, xfs_da_name_t *args);
#endif	/* !SIM */

/*
 * Routines used for growing the Btree.
 */
int	xfs_dir_leaf_create(xfs_trans_t *trans, xfs_inode_t *dp,
					xfs_fileoff_t which_block,
					buf_t **bpp);
int	xfs_dir_leaf_split(xfs_da_state_t *state,
					  xfs_da_state_blk_t *oldblk,
					  xfs_da_state_blk_t *newblk);
int	xfs_dir_leaf_add(xfs_trans_t *trans, buf_t *leaf_buffer,
				     xfs_da_name_t *args,
				     int insertion_index);
int	xfs_dir_leaf_addname(xfs_trans_t *trans, xfs_da_name_t *args);
int	xfs_dir_leaf_lookup_int(buf_t *leaf_buffer, xfs_da_name_t *args,
				      int *index_found_at);
#ifndef SIM
int	xfs_dir_leaf_remove(xfs_trans_t *trans, buf_t *leaf_buffer,
					int index_to_remove);
int	xfs_dir_leaf_getdents_int(buf_t *bp, xfs_inode_t *dp, uio_t *uio,
					int *eobp, dirent_t *dbp);
void	xfs_dir_leaf_print_int(buf_t *leaf_buffer, xfs_inode_t *dir_inode);
/*
 * Routines used for shrinking the Btree.
 */
int	xfs_dir_leaf_toosmall(xfs_da_state_t *state, int *retval);
void	xfs_dir_leaf_unbalance(xfs_da_state_t *state,
					      xfs_da_state_blk_t *drop_blk,
					      xfs_da_state_blk_t *save_blk);
#endif	/* !SIM */

/*
 * Utility routines.
 */
uint	xfs_dir_leaf_lasthash(buf_t *bp, int *count);
int	xfs_dir_leaf_order(buf_t *leaf1_bp, buf_t *leaf2_bp);
int	xfs_dir_put_dirent(xfs_mount_t *mp, dirent_t *dbp, xfs_ino_t ino,
				       char *name, int namelen, off_t doff,
				       uio_t *uio, int *done);

#endif	/* !FS_XFS_DIR_BTREE_H */
