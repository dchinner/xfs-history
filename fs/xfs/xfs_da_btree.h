#ifndef _FS_XFS_DIR_BTREE_H
#define	_FS_XFS_DIR_BTREE_H

#ident	"$Revision: 1.12 $"

/*
 * xfs_dir_btree.h
 */

struct buf;
struct dirent;
struct uio;
struct xfs_bmap_free;
struct xfs_inode;
struct xfs_mount;
struct xfs_trans;

/*========================================================================
 * Btree searching and modification structure definitions.
 *========================================================================*/

/*
 * Structure to ease passing around component names.
 */
typedef struct xfs_dir_name {
	char		*name;		/* string (maybe not NULL terminated) */
	int		namelen;	/* length of string (maybe no NULL) */
	uint		hashval;	/* hash value of name */
	xfs_ino_t	inumber;	/* input/output inode number */
	struct xfs_inode *dp;		/* directory inode to manipulate */
	xfs_fsblock_t	*firstblock;	/* ptr to firstblock for bmap calls */
	struct xfs_bmap_free *flist;	/* ptr to freelist for bmap_finish */
	xfs_extlen_t	total;		/* total blocks needed, for 1st bmap */
} xfs_dir_name_t;

/*
 * Storage for holding state during Btree searches and split/join ops.
 *
 * Only need space for 5 intermediate nodes.  With a minimum of 62-way
 * fanout to the Btree, we can support over 900 million directory blocks,
 * which is slightly more than enough.
 */
typedef struct xfs_dir_state_blk {
	struct buf	*bp;		/* buffer containing block */
	xfs_fileoff_t	blkno;		/* blkno of buffer */
	int		index;		/* relevant index into block */
	uint		hashval;	/* last hash value in block */
	int		leafblk;	/* 1->blk is a leaf, 0->blk is a node */
} xfs_dir_state_blk_t;
typedef struct xfs_dir_state_path {
	int			active;		/* number of active levels */
	xfs_dir_state_blk_t	blk[XFS_DIR_NODE_MAXDEPTH];
} xfs_dir_state_path_t;
typedef struct xfs_dir_state {
	xfs_dir_name_t		*args;		/* filename arguments */
	struct xfs_mount	*mp;		/* filesystem mount point */
	struct xfs_trans	*trans;		/* transaction context */
	int			blocksize;	/* logical block size */
	int			inleaf;		/* insert into 1->lf, 0->splf */
	xfs_dir_state_path_t	path;		/* search/split paths */
	xfs_dir_state_path_t	altpath;	/* alternate path for join */
} xfs_dir_state_t;

/*
 * Utility macros to aid in logging changed structure fields.
 */
#define XFS_DIR_LOGOFF(BASE, ADDR)	((char *)(ADDR) - (char *)(BASE))
#define XFS_DIR_LOGRANGE(BASE, ADDR, SIZE)	\
					XFS_DIR_LOGOFF(BASE, ADDR), \
					XFS_DIR_LOGOFF(BASE, ADDR)+(SIZE)-1

/*========================================================================
 * Function prototypes for the kernel.
 *========================================================================*/

/*
 * Routines used for growing the Btree.
 */
int	xfs_dir_split(xfs_dir_state_t *state);
int	xfs_dir_leaf_add(struct xfs_trans *trans, struct buf *leaf_buffer,
			     xfs_dir_name_t *args, int insertion_index);

/*
 * Routines used for shrinking the Btree.
 */
#ifndef SIM
int	xfs_dir_join(xfs_dir_state_t *state);
#endif	/* !SIM */
void	xfs_dir_fixhashpath(xfs_dir_state_t *state,
				   xfs_dir_state_path_t *path_to_to_fix);
#ifndef SIM
int	xfs_dir_leaf_remove(struct xfs_trans *trans, struct buf *leaf_buffer,
				int index_to_remove);
#endif	/* !SIM */

/*
 * Routines used for finding things in the Btree.
 */
int	xfs_dir_leaf_lookup_int(struct buf *leaf_buffer, xfs_dir_name_t *args,
				      int *index_found_at);
int	xfs_dir_node_lookup_int(xfs_dir_state_t *state);

#ifndef SIM
/*
 * Routines used to print, getdents things from the Btree.
 */
void	xfs_dir_leaf_print_int(struct buf *leaf_buffer,
				struct xfs_inode *dir_inode);
int	xfs_dir_leaf_getdents_int(struct buf *bp, struct xfs_inode *dp,
					struct uio *uio, int *eobp,
					struct dirent *dbp);
#endif	/* !SIM */

#ifdef XFSDIRDEBUG
#define xfs_trans_binval(T,B)	xfsdir_t_binval(T,B,__FILE__,__LINE__)
#define xfs_trans_brelse(T,B)	xfsdir_t_brelse(T,B,__FILE__,__LINE__)
#define xfs_trans_log_buf(T,B,F,L) xfsdir_t_log_buf(T,B,F,L,__FILE__,__LINE__)
#define xfs_trans_log_inode(T,I,F) xfsdir_t_log_inode(T,I,F,__FILE__,__LINE__)
#define xfs_dir_read_buf(T,I,B) xfsdir_t_dir_read_buf(T,I,B,__FILE__,__LINE__)
void xfsdir_t_reinit(char *description, char *file, int line);
void xfsdir_t_binval(struct xfs_trans *tp, struct buf *bp,
			char *file, int line);
void xfsdir_t_brelse(struct xfs_trans *tp, struct buf *bp,
			char *file, int line);
void xfsdir_t_log_buf(struct xfs_trans *tp, struct buf *bp,
			uint first, uint last, char *file, int line);
void xfsdir_t_log_inode(struct xfs_trans *tp, struct xfs_inode *ip, uint flags,
				    char *file, int line);
struct buf *xfsdir_t_dir_read_buf(struct xfs_trans *trans, struct xfs_inode *dp,
					 xfs_fileoff_t bno,
					 char *file, int line);
#define BUFTRACEMAX	128
typedef struct xfsdir_buftrace {
	short		which;
	short		line;
	char		*file;
	struct buf	*bp;
} xfsdir_buftrace_t;
#endif /* XFSDIRDEBUG */

#endif	/* !FS_XFS_DIR_BTREE_H */
