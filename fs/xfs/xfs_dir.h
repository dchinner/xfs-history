#ifndef _FS_XFS_DIR_H
#define	_FS_XFS_DIR_H

#ident	"$Revision$"

/*
 * xfs_dir.h
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

/*========================================================================
 * Function prototypes for the kernel.
 *========================================================================*/

struct uio;
struct xfs_bmap_free;
struct xfs_inode;
struct xfs_mount;
struct xfs_trans;

/*
 * Overall external interface routines.
 */
void	xfs_dir_startup(void);

void	xfs_dir_mount(struct xfs_mount *mp);

int	xfs_dir_isempty(struct xfs_inode *dp);

int	xfs_dir_init(struct xfs_trans *trans, struct xfs_inode *dir,
			    struct xfs_inode *parent_dir);

int	xfs_dir_createname(struct xfs_trans *trans, struct xfs_inode *dp,
				  char *name_string,
				  xfs_ino_t inode_number,
				  xfs_fsblock_t *firstblock,
				  struct xfs_bmap_free *flist,
				  xfs_extlen_t total);

int	xfs_dir_lookup(struct xfs_trans *tp, struct xfs_inode *dp,
			      char *name_string, int name_length,
			      xfs_ino_t *inode_number);

#ifndef SIM
int	xfs_dir_removename(struct xfs_trans *trans, struct xfs_inode *dp,
				  char *name_string,
				  xfs_fsblock_t *firstblock,
				  struct xfs_bmap_free *flist,
				  xfs_extlen_t total);

int	xfs_dir_getdents(struct xfs_trans *tp, struct xfs_inode *dp,
				struct uio *uiop, int *eofp);

int	xfs_dir_replace(struct xfs_trans *tp, struct xfs_inode *dp,
			       char *name_string, int name_length,
			       xfs_ino_t inode_number);
#endif	/* !SIM */

#endif	/* !_FS_XFS_DIR_H */
