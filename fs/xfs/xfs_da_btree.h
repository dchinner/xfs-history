#ifndef _FS_XFS_DA_BTREE_H
#define	_FS_XFS_DA_BTREE_H

#ident	"$Revision: 1.12 $"

/*
 * xfs_da_btree.h
 */

/*========================================================================
 * Directory Structure when greater than XFS_LBSIZE(mp) bytes.
 *========================================================================*/

/*
 * This structure is common to both leaf nodes and non-leaf nodes in the Btree.
 *
 * Is is used to manage a doubly linked list of all blocks at the same
 * level in the Btree, and to identify which type of block this is.
 */
#define XFS_DA_NODE_MAGIC	0xfebe	/* magic number: non-leaf blocks */
#define XFS_DIR_LEAF_MAGIC	0xfeeb	/* magic number: directory leaf blks */
#define XFS_ATTR_LEAF_MAGIC	0xfbee	/* magic number: attribute leaf blks */

typedef struct xfs_da_blkinfo {
	__uint32_t forw;			/* previous block in list */
	__uint32_t back;			/* following block in list */
	__uint16_t magic;			/* validity check on block */
} xfs_da_blkinfo_t;

/*
 * This is the structure of the root and intermediate nodes in the Btree.
 * The leaf nodes are defined above.
 *
 * Entries are not packed.
 *
 * Since we have duplicate keys, use a binary search but always follow
 * all match in the block, not just the first match found.
 */
#define	XFS_DA_NODE_MAXDEPTH	5	/* max depth of Btree */

typedef struct xfs_da_intnode {
	struct xfs_da_node_hdr {	/* constant-structure header block */
		xfs_da_blkinfo_t info;	/* block type, links, etc. */
		__uint16_t count;	/* count of active entries */
		__uint16_t level;	/* level above leaves (leaf == 0) */
	} hdr;
	struct xfs_da_node_entry {
		__uint32_t hashval;	/* hash value for this descendant */
		__uint32_t before;	/* Btree block before this key */
	} btree[1];			/* variable sized array of keys */
} xfs_da_intnode_t;
typedef struct xfs_da_node_hdr xfs_da_node_hdr_t;
typedef struct xfs_da_node_entry xfs_da_node_entry_t;

#define XFS_DA_NODE_ENTSIZE_BYNAME()	/* space a name uses */ \
	(sizeof(xfs_da_node_entry_t))
#define XFS_DA_NODE_ENTRIES(mp)		/* how many entries in this block? */ \
	((XFS_LBSIZE(mp) - sizeof(xfs_da_node_hdr_t)) \
		   / sizeof(xfs_da_node_entry_t))
#define XFS_DA_MAXBLK		0x10000000	/* max hash value */

/*
 * Macros used by directory code to interface to the filesystem.
 */
#define	XFS_LBSIZE(mp)	((mp)->m_sb.sb_blocksize)
#define	XFS_LBLOG(mp)	((mp)->m_sb.sb_blocklog)

/*
 * Macros used to manipulate directory off_t's
 */
#define	XFS_DA_MAKE_COOKIE(mp, bno, entry) \
	(((bno) << (mp)->m_dircook_elog) | (entry))
#define	XFS_DA_COOKIE_BNO(mp, cookie) \
	((cookie) >> (mp)->m_dircook_elog)
#define	XFS_DA_COOKIE_ENTRY(mp, cookie) \
	((cookie) & ((1 << (mp)->m_dircook_elog) - 1))


/*========================================================================
 * Btree searching and modification structure definitions.
 *========================================================================*/

/*
 * Structure to ease passing around component names.
 */
typedef struct xfs_da_name {
	char		*name;		/* string (maybe not NULL terminated) */
	int		namelen;	/* length of string (maybe no NULL) */
	char		*value;		/* set of bytes (maybe contain NULLs) */
	int		valuelen;	/* length of value */
	int		flags;		/* argument flags (eg: ATTR_NOCREATE) */
	uint		hashval;	/* hash value of name */
	xfs_ino_t	inumber;	/* input/output inode number */
	xfs_inode_t	*dp;		/* directory inode to manipulate */
	xfs_fsblock_t	*firstblock;	/* ptr to firstblock for bmap calls */
	xfs_bmap_free_t	*flist;		/* ptr to freelist for bmap_finish */
	xfs_extlen_t	total;		/* total blocks needed, for 1st bmap */
} xfs_da_name_t;

/*
 * Storage for holding state during Btree searches and split/join ops.
 *
 * Only need space for 5 intermediate nodes.  With a minimum of 62-way
 * fanout to the Btree, we can support over 900 million directory blocks,
 * which is slightly more than enough.
 */
typedef struct xfs_da_state_blk {
	buf_t		*bp;		/* buffer containing block */
	xfs_fileoff_t	blkno;		/* blkno of buffer */
	int		index;		/* relevant index into block */
	uint		hashval;	/* last hash value in block */
	int		magic;		/* blk's magic number, ie: blk type */
} xfs_da_state_blk_t;
typedef struct xfs_da_state_path {
	int			active;		/* number of active levels */
	xfs_da_state_blk_t	blk[XFS_DA_NODE_MAXDEPTH];
} xfs_da_state_path_t;
typedef struct xfs_da_state {
	xfs_da_name_t		 *args;		/* filename arguments */
	xfs_mount_t		 *mp;		/* filesystem mount point */
	xfs_trans_t		 *trans;	/* transaction context */
	int			 blocksize;	/* logical block size */
	int			 inleaf;	/* insert into 1->lf, 0->splf */
	xfs_da_state_path_t	 path;		/* search/split paths */
	xfs_da_state_path_t	 altpath;	/* alternate path for join */
} xfs_da_state_t;

/*
 * Utility macros to aid in logging changed structure fields.
 */
#define XFS_DA_LOGOFF(BASE, ADDR)	((char *)(ADDR) - (char *)(BASE))
#define XFS_DA_LOGRANGE(BASE, ADDR, SIZE)	\
		XFS_DA_LOGOFF(BASE, ADDR), XFS_DA_LOGOFF(BASE, ADDR)+(SIZE)-1

/*========================================================================
 * Function prototypes for the kernel.
 *========================================================================*/

/*
 * Routines used for growing the Btree.
 */
int	xfs_da_node_create(xfs_trans_t *trans, xfs_inode_t *dp,
					xfs_fileoff_t which_block,
					int leaf_block_next,
					buf_t **bpp);
int	xfs_da_split(xfs_da_state_t *state);

/*
 * Routines used for shrinking the Btree.
 */
#ifndef SIM
int	xfs_da_join(xfs_da_state_t *state);
#endif	/* !SIM */
void	xfs_da_fixhashpath(xfs_da_state_t *state,
			   xfs_da_state_path_t *path_to_to_fix);

/*
 * Routines used for finding things in the Btree.
 */
int	xfs_da_node_lookup_int(xfs_da_state_t *state, int *result);
int	xfs_da_node_getvalue(xfs_da_state_t *state);
int	xfs_da_path_shift(xfs_da_state_t *state, xfs_da_state_path_t *path,
					 int forward, int release,
					 int *result);
/*
 * Utility routines.
 */
int	xfs_da_blk_unlink(xfs_da_state_t *state,
					 xfs_da_state_blk_t *drop_blk,
					 xfs_da_state_blk_t *save_blk);
int	xfs_da_blk_link(xfs_da_state_t *state,
				       xfs_da_state_blk_t *old_blk,
				       xfs_da_state_blk_t *new_blk);

/*
 * Utility routines.
 */
int	xfs_da_grow_inode(xfs_trans_t *trans, xfs_da_name_t *args,
				      xfs_fileoff_t *new_blkno);
int	xfs_da_get_buf(xfs_trans_t *trans, xfs_inode_t *dp, xfs_fileoff_t bno,
				   buf_t **bp);
int	xfs_da_read_buf(xfs_trans_t *trans, xfs_inode_t *dp, xfs_fileoff_t bno,
				    buf_t **bpp);
#ifndef SIM
int	xfs_da_shrink_inode(xfs_trans_t *trans, xfs_da_name_t *args,
					xfs_fileoff_t dead_blkno,
					buf_t *dead_buf);
#endif	/* !SIM */

uint xfs_da_hashname(char *name_string, int name_length);
uint xfs_da_log2_roundup(uint i);
xfs_da_state_t *xfs_da_state_alloc(void);
void xfs_da_state_free(xfs_da_state_t *state);

#ifdef _KERNEL
extern zone_t *xfs_da_state_zone;
#endif /* _KERNEL */


#ifdef XFSDADEBUG
#ifdef XFSDABDEBUG
#define xfs_trans_binval(T,B)	xfsda_t_binval(T,B,__FILE__,__LINE__)
#define xfs_trans_brelse(T,B)	xfsda_t_brelse(T,B,__FILE__,__LINE__)
#define xfs_trans_log_buf(T,B,F,L) xfsda_t_log_buf(T,B,F L,__FILE__,__LINE__)
#define xfs_trans_log_inode(T,I,F) xfsda_t_log_inode(T,I,F,__FILE__,__LINE__)
#define xfs_da_read_buf(T,I,B) xfsda_t_da_read_buf(T,I,B,__FILE__,__LINE__)
#endif /* !XFSDABDEBUG */
void xfsda_t_reinit(char *description, char *file, int line);
void xfsda_t_binval(xfs_trans_t *tp, buf_t *bp, char *file, int line);
void xfsda_t_brelse(xfs_trans_t *tp, buf_t *bp, char *file, int line);
void xfsda_t_log_buf(xfs_trans_t *tp, buf_t *bp, uint first, uint last,
				  char *file, int line);
void xfsda_t_log_inode(xfs_trans_t *tp, xfs_inode_t *ip, uint flags,
				    char *file, int line);
buf_t *xfsda_t_da_read_buf(xfs_trans_t *trans, xfs_inode_t *dp,
					 xfs_fileoff_t bno,
					 char *file, int line);
#define BUFTRACEMAX	128
typedef struct xfsda_buftrace {
	short	which;
	short	line;
	char	*file;
	buf_t	*bp;
} xfsda_buftrace_t;
#else /* !XFSDADEBUG */
#define	xfsda_t_reinit(D,F,L)
#endif /* !XFSDADEBUG */

#endif	/* !FS_XFS_DA_BTREE_H */
