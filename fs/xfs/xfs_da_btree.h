#ifndef _FS_XFS_DIR_BTREE_H
#define	_FS_XFS_DIR_BTREE_H

#ident	"$Revision: 1.9 $"

/*
 * xfs_dir_btree.h
 */

/*========================================================================
 * Btree searching and modification structure definitions.
 *========================================================================*/

/*
 * Structure to ease passing around component names.
 */
struct xfs_dir_name {
	char		*name;		/* string (maybe not NULL terminated) */
	int		namelen;	/* length of string (maybe no NULL) */
	uint		hashval;	/* hash value of name */
	xfs_ino_t	inumber;	/* input/output inode number */
	xfs_inode_t	*dp;		/* directory inode to manipulate */
	xfs_fsblock_t	*firstblock;	/* ptr to firstblock for bmap calls */
	xfs_bmap_free_t	*flist;		/* ptr to freelist for bmap_finish */
	xfs_extlen_t	total;		/* total blocks needed, for 1st bmap */
};

/*
 * Storage for holding state during Btree searches and split/join ops.
 *
 * Only need space for 5 intermediate nodes.  With a minimum of 62-way
 * fanout to the Btree, we can support over 900 million directory blocks,
 * which is slightly more than enough.
 */
struct xfs_dir_state_blk {
	buf_t		*bp;		/* buffer containing block */
	xfs_fsblock_t	blkno;		/* blkno of buffer */
	int		index;		/* relevant index into block */
	uint		hashval;	/* last hash value in block */
	int		leafblk;	/* 1->blk is a leaf, 0->blk is a node */
};
struct xfs_dir_state_path {
	int			active;		/* number of active levels */
	struct xfs_dir_state_blk blk[XFS_DIR_NODE_MAXDEPTH];
};
struct xfs_dir_state {
	struct xfs_dir_name	  *args;	/* filename arguments */
	xfs_mount_t		  *mp;		/* filesystem mount point */
	xfs_trans_t		  *trans;	/* transaction context */
	int			  blocksize;	/* logical block size */
	int			  inleaf;	/* insert into 1->lf, 0->splf */
	struct xfs_dir_state_path path;		/* search/split paths */
	struct xfs_dir_state_path altpath;	/* alternate path for join */
};

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
int	xfs_dir_split(struct xfs_dir_state *state);
int	xfs_dir_leaf_add(xfs_trans_t *trans, buf_t *leaf_buffer,
				     struct xfs_dir_name *args,
				     int insertion_index);

/*
 * Routines used for shrinking the Btree.
 */
int	xfs_dir_join(struct xfs_dir_state *state);
void	xfs_dir_fixhashpath(struct xfs_dir_state *state,
				   struct xfs_dir_state_path *path_to_to_fix);
int	xfs_dir_leaf_remove(xfs_trans_t *trans, buf_t *leaf_buffer,
					int index_to_remove);

/*
 * Routines used for finding things in the Btree.
 */
int	xfs_dir_leaf_lookup_int(buf_t *leaf_buffer, struct xfs_dir_name *args,
				      int *index_found_at);
int	xfs_dir_node_lookup_int(struct xfs_dir_state *state);

/*
 * Routines used to print, getdents things from the Btree.
 */
void	xfs_dir_leaf_print_int(buf_t *leaf_buffer, xfs_inode_t *dir_inode);
int	xfs_dir_leaf_getdents_int(buf_t *bp, xfs_inode_t *dp, uio_t *uio,
					int *eobp, dirent_t *dbp);

#ifdef XFSDIRDEBUG
#define xfs_trans_binval(T,B)	xfsdir_t_binval(T,B,__FILE__,__LINE__)
#define xfs_trans_brelse(T,B)	xfsdir_t_brelse(T,B,__FILE__,__LINE__)
#define xfs_trans_log_buf(T,B,F,L) xfsdir_t_log_buf(T,B,F,L,__FILE__,__LINE__)
#define xfs_trans_log_inode(T,I,F) xfsdir_t_log_inode(T,I,F,__FILE__,__LINE__)
#define xfs_dir_read_buf(T,I,B) xfsdir_t_dir_read_buf(T,I,B,__FILE__,__LINE__)
void xfsdir_t_reinit(char *description, char *file, int line);
void xfsdir_t_binval(xfs_trans_t *tp, buf_t *bp, char *file, int line);
void xfsdir_t_brelse(xfs_trans_t *tp, buf_t *bp, char *file, int line);
void xfsdir_t_log_buf(xfs_trans_t *tp, buf_t *bp, uint first, uint last,
				  char *file, int line);
void xfsdir_t_log_inode(xfs_trans_t *tp, xfs_inode_t *ip, uint flags,
				    char *file, int line);
buf_t *xfsdir_t_dir_read_buf(xfs_trans_t *trans, xfs_inode_t *dp,
					 xfs_fsblock_t bno,
					 char *file, int line);
#define BUFTRACEMAX	128
struct xfsdir_buftrace {
	short	which;
	short	line;
	char	*file;
	buf_t	*bp;
};
#endif /* XFSDIRDEBUG */

#endif	/* !FS_XFS_DIR_BTREE_H */
