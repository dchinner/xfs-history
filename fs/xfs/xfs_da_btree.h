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
	char *name;			/* string (maybe not NULL terminated) */
	int namelen;			/* length of string (maybe no NULL) */
	uint hashval;			/* hash value of name */
	xfs_ino_t inumber;		/* input/output inode number */
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
#define XFS_DIR_LOGSTART(BASE, FIELD)	((char *)&(FIELD) - (char *)(BASE))
#define XFS_DIR_LOGSIZE(FIELD)		sizeof(FIELD)
#define XFS_DIR_LOGRANGE(B1, F1, F2)	( XFS_DIR_LOGSTART(B1, F2) + \
					  XFS_DIR_LOGSIZE(F2) - \
					  XFS_DIR_LOGSTART(B1, F1) )

/*========================================================================
 * Function prototypes for the kernel.
 *========================================================================*/

/*
 * Overall external interface routines.
 */
int	xfs_dir_createname(char *name_string, int name_length,
				xfs_ino_t *inode_number);
int	xfs_dir_removename(char *name_string, int name_length);
int	xfs_dir_lookup(char *name_string, int name_length,
			    xfs_ino_t *inode_number);
int	xfs_dir_getdents();

/*
 * Interface routines when dirsize < XFS_LITINO(fs).
 */
int	xfs_dir_shortform_create(xfs_trans_t *, xfs_ino_t parent_inumber);
int	xfs_dir_shortform_addname(xfs_trans_t *, struct xfs_dir_name *add);
int	xfs_dir_shortform_removename(xfs_trans_t *, struct xfs_dir_name *remove);
int	xfs_dir_shortform_lookup(struct xfs_dir_name *lookup);
int	xfs_dir_shortform_to_leaf(xfs_trans_t *);

/*
 * Interface routines when dirsize == XFS_LBSIZE(fs).
 */
buf_t	*xfs_dir_leaf_create(xfs_trans_t *, xfs_fsblock_t which_block);
int	xfs_dir_leaf_addname(xfs_trans_t *, struct xfs_dir_name *args);
int	xfs_dir_leaf_removename(xfs_trans_t *, struct xfs_dir_name *args,
					    int *number_entries,
					    int *total_namebytes);
int	xfs_dir_leaf_lookup(struct xfs_dir_name *args);
int	xfs_dir_leaf_to_shortform(xfs_trans_t *);
int	xfs_dir_leaf_to_node(xfs_trans_t *);

/*
 * Interface routines when dirsize > XFS_LBSIZE(fs).
 */
buf_t	*xfs_dir_node_create(xfs_trans_t *, xfs_fsblock_t which_block,
					 int leaf_block_next);
int	xfs_dir_node_addname(xfs_trans_t *, struct xfs_dir_name *args);
int	xfs_dir_node_removename(xfs_trans_t *, struct xfs_dir_name *args);
int	xfs_dir_node_lookup(xfs_trans_t *, struct xfs_dir_name *args);
int	xfs_dir_node_to_leaf(xfs_trans_t *);

/*
 * Routines used for growing the Btree.
 */
int	xfs_dir_split(struct xfs_dir_state *state);
int	xfs_dir_root_split(struct xfs_dir_state *state,
				  struct xfs_dir_state_blk *existing_root,
				  struct xfs_dir_state_blk *new_child);
int	xfs_dir_leaf_split(struct xfs_dir_state *state,
				  struct xfs_dir_state_blk *oldblk,
				  struct xfs_dir_state_blk *newblk);
int	xfs_dir_leaf_add(xfs_trans_t *, buf_t *leaf_buffer,
				     struct xfs_dir_name *args,
				     int insertion_index);
void	xfs_dir_leaf_add_work(xfs_trans_t *, buf_t *leaf_buffer,
					  struct xfs_dir_name *args,
					  int insertion_index,
					  int freemap_index);
void	xfs_dir_leaf_compact(xfs_trans_t *, buf_t *leaf_buffer);
void	xfs_dir_leaf_rebalance(struct xfs_dir_state *state,
				      struct xfs_dir_state_blk *blk1,
				      struct xfs_dir_state_blk *blk2);
int	xfs_dir_leaf_figure_balance(struct xfs_dir_state *state,
					   struct xfs_dir_state_blk *leaf_blk_1,
					   struct xfs_dir_state_blk *leaf_blk_2,
					   int *number_entries_in_blk1,
					   int *number_namebytes_in_blk1);
int	xfs_dir_node_split(struct xfs_dir_state *state,
				  struct xfs_dir_state_blk *existing_blk,
				  struct xfs_dir_state_blk *split_blk,
				  struct xfs_dir_state_blk *blk_to_add,
				  int treelevel);
void	xfs_dir_node_rebalance(struct xfs_dir_state *state,
				      struct xfs_dir_state_blk *node_blk_1,
				      struct xfs_dir_state_blk *node_blk_2,
				      uint hashval_of_new_node);
void	xfs_dir_node_add(struct xfs_dir_state *state,
				struct xfs_dir_state_blk *old_node_blk,
				struct xfs_dir_state_blk *new_node_blk);

/*
 * Routines used for shrinking the Btree.
 */
void	xfs_dir_join(struct xfs_dir_state *state);
int	xfs_dir_root_join(struct xfs_dir_state *state,
				 struct xfs_dir_state_blk *root_blk);
int	xfs_dir_blk_toosmall(struct xfs_dir_state *state, int level);
void	xfs_dir_fixhashpath(struct xfs_dir_state *state,
				   struct xfs_dir_state_path *path_to_to_fix,
				   int level_in_path);
void	xfs_dir_leaf_remove(xfs_trans_t *, buf_t *leaf_buffer,
					int index_to_remove);
void	xfs_dir_leaf_unbalance(struct xfs_dir_state *state,
				      struct xfs_dir_state_blk *drop_blk,
				      struct xfs_dir_state_blk *save_blk);
void	xfs_dir_node_remove(struct xfs_dir_state *state,
				   struct xfs_dir_state_blk *drop_blk);
void	xfs_dir_node_unbalance(struct xfs_dir_state *state,
				      struct xfs_dir_state_blk *src_node_blk,
				      struct xfs_dir_state_blk *dst_node_blk);

/*
 * Routines used for finding things in the Btree.
 */
int	xfs_dir_leaf_lookup_int(buf_t *leaf_buffer, struct xfs_dir_name *args,
				      int *index_found_at);
int	xfs_dir_node_lookup_int(struct xfs_dir_state *state);
void	xfs_dir_findpath(struct xfs_dir_state *state,
				struct xfs_dir_state_path *path_to_fill_in,
				uint hashval_to_find,
				xfs_fsblock_t blkno_to_find);

/*
 * Utility routines.
 */
void	xfs_dir_leaf_moveents(struct xfs_dir_leafblock *src_leaf,
				     int src_start,
				     struct xfs_dir_leafblock *dst_leaf,
				     int dst_start, int move_count);
int	xfs_dir_leaf_refind(struct xfs_dir_leafblock *leaf,
				   int likely_index, uint hashval);
int	xfs_dir_node_refind(struct xfs_dir_intnode *node,
				   int likely_index, uint hashval);
void	xfs_dir_blk_unlink(struct xfs_dir_state *state,
				  struct xfs_dir_state_blk *drop_blk,
				  struct xfs_dir_state_blk *save_blk);
void	xfs_dir_blk_link(struct xfs_dir_state *state,
				struct xfs_dir_state_blk *old_blk,
				struct xfs_dir_state_blk *new_blk);

uint	xfs_dir_hashname(char *name_string, int name_length);
int	xfs_dir_grow_inode(xfs_trans_t *, int delta_in_bytes,
				       xfs_fsblock_t *last_logblk_in_inode);
int	xfs_dir_shrink_inode(xfs_trans_t *, int delta_in_bytes,
					 xfs_fsblock_t *last_logblk_in_inode);
