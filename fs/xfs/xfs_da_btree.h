#ifndef _FS_XFS_DA_BTREE_H
#define	_FS_XFS_DA_BTREE_H

#ident	"$Revision: 1.17 $"

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

struct buf;
struct xfs_bmap_free;
struct xfs_inode;
struct xfs_mount;
struct xfs_trans;
struct zone;

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
	struct xfs_inode *dp;		/* directory inode to manipulate */
	xfs_fsblock_t	*firstblock;	/* ptr to firstblock for bmap calls */
	struct xfs_bmap_free *flist;	/* ptr to freelist for bmap_finish */
	xfs_extlen_t	total;		/* total blocks needed, for 1st bmap */
	int		whichfork;	/* data or attribute fork */
} xfs_da_name_t;

/*
 * Storage for holding state during Btree searches and split/join ops.
 *
 * Only need space for 5 intermediate nodes.  With a minimum of 62-way
 * fanout to the Btree, we can support over 900 million directory blocks,
 * which is slightly more than enough.
 */
typedef struct xfs_da_state_blk {
	struct buf	*bp;		/* buffer containing block */
	xfs_fileoff_t	blkno;		/* blkno of buffer */
	int		index;		/* relevant index into block */
	uint		hashval;	/* last hash value in block */
	int		magic;		/* blk's magic number, ie: blk type */
} xfs_da_state_blk_t;
typedef struct xfs_da_state_path {
	int			active;		/* number of active levels */
	struct xfs_da_state_blk	blk[XFS_DA_NODE_MAXDEPTH];
} xfs_da_state_path_t;
typedef struct xfs_da_state {
	struct xfs_da_name	 *args;		/* filename arguments */
	struct xfs_mount	 *mp;		/* filesystem mount point */
	struct xfs_trans	 *trans;	/* transaction context */
	int			 blocksize;	/* logical block size */
	int			 inleaf;	/* insert into 1->lf, 0->splf */
	struct xfs_da_state_path path;		/* search/split paths */
	struct xfs_da_state_path altpath;	/* alternate path for join */
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
int	xfs_da_node_create(struct xfs_trans *trans, struct xfs_inode *dp,
				  xfs_fileoff_t which_block, int blkno,
				  struct buf **bpp, int whichfork);
int	xfs_da_split(struct xfs_da_state *state);

/*
 * Routines used for shrinking the Btree.
 */
#ifndef SIM
int	xfs_da_join(struct xfs_da_state *state);
#endif	/* !SIM */
void	xfs_da_fixhashpath(struct xfs_da_state *state,
				  struct xfs_da_state_path *path_to_to_fix);

/*
 * Routines used for finding things in the Btree.
 */
int	xfs_da_node_lookup_int(struct xfs_da_state *state, int *result);
int	xfs_da_node_getvalue(struct xfs_da_state *state);
int	xfs_da_path_shift(struct xfs_da_state *state,
				 struct xfs_da_state_path *path,
				 int forward, int release, int *result);
/*
 * Utility routines.
 */
int	xfs_da_blk_unlink(struct xfs_da_state *state,
				 struct xfs_da_state_blk *drop_blk,
				 struct xfs_da_state_blk *save_blk);
int	xfs_da_blk_link(struct xfs_da_state *state,
			       struct xfs_da_state_blk *old_blk,
			       struct xfs_da_state_blk *new_blk);

/*
 * Utility routines.
 */
int	xfs_da_grow_inode(struct xfs_trans *trans, struct xfs_da_name *args,
				 int length, xfs_fileoff_t *new_blkno);
int	xfs_da_get_buf(struct xfs_trans *trans, struct xfs_inode *dp,
			      xfs_fileoff_t bno, struct buf **bp,
			      int whichfork);
int	xfs_da_read_buf(struct xfs_trans *trans, struct xfs_inode *dp,
			       xfs_fileoff_t bno, struct buf **bpp,
			       int whichfork);
#ifndef SIM
int	xfs_da_shrink_inode(struct xfs_trans *trans, struct xfs_da_name *args,
				   xfs_fileoff_t dead_blkno,
				   int length, struct buf *dead_buf);
#endif	/* !SIM */

uint xfs_da_hashname(char *name_string, int name_length);
uint xfs_da_log2_roundup(uint i);
struct xfs_da_state *xfs_da_state_alloc(void);
void xfs_da_state_free(struct xfs_da_state *state);

#ifdef _KERNEL
extern struct zone *xfs_da_state_zone;
#endif /* _KERNEL */


#ifdef XFSDADEBUG
#ifdef XFSDABDEBUG
#define xfs_trans_binval(T,B)	xfsda_t_binval(T,B,__FILE__,__LINE__)
#define xfs_trans_brelse(T,B)	xfsda_t_brelse(T,B,__FILE__,__LINE__)
#define xfs_trans_log_buf(T,B,F,L) xfsda_t_log_buf(T,B,F L,__FILE__,__LINE__)
#define xfs_trans_log_inode(T,I,F) xfsda_t_log_inode(T,I,F,__FILE__,__LINE__)
#define xfs_da_read_buf(T,I,B,W) xfsda_t_da_read_buf(T,I,B,W,__FILE__,__LINE__)
#endif /* !XFSDABDEBUG */
void xfsda_t_reinit(char *description, char *file, int line);
void xfsda_t_binval(struct xfs_trans *tp, struct buf *bp, char *file, int line);
void xfsda_t_brelse(struct xfs_trans *tp, struct buf *bp, char *file, int line);
void xfsda_t_log_buf(struct xfs_trans *tp, struct buf *bp, uint first,
			    uint last, char *file, int line);
void xfsda_t_log_inode(struct xfs_trans *tp, struct xfs_inode *ip, uint flags,
			      char *file, int line);
struct buf *xfsda_t_da_read_buf(struct xfs_trans *trans, struct xfs_inode *dp,
				       xfs_fileoff_t bno, int whichfork,
				       char *file, int line);
#define BUFTRACEMAX	128
typedef struct xfsda_buftrace {
	short		which;
	short		line;
	char		*file;
	struct buf	*bp;
} xfsda_buftrace_t;
#else /* !XFSDADEBUG */
#define	xfsda_t_reinit(D,F,L)
#endif /* !XFSDADEBUG */

#endif	/* !FS_XFS_DA_BTREE_H */
