#ifndef _FS_XFS_DA_BTREE_H
#define	_FS_XFS_DA_BTREE_H

#ident	"$Revision: 1.26 $"

/*
 * xfs_da_btree.h
 */

struct buf;
struct xfs_bmap_free;
struct xfs_inode;
struct xfs_mount;
struct xfs_trans;
struct zone;

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
	xfs_dablk_t forw;			/* previous block in list */
	xfs_dablk_t back;			/* following block in list */
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
		xfs_dahash_t hashval;	/* hash value for this descendant */
		xfs_dablk_t before;	/* Btree block before this key */
	} btree[1];			/* variable sized array of keys */
} xfs_da_intnode_t;
typedef struct xfs_da_node_hdr xfs_da_node_hdr_t;
typedef struct xfs_da_node_entry xfs_da_node_entry_t;

#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_BUF_TO_DA_INTNODE)
xfs_da_intnode_t *xfs_buf_to_da_intnode(struct buf *bp);
#define	XFS_BUF_TO_DA_INTNODE(bp)	xfs_buf_to_da_intnode(bp)
#else
#define	XFS_BUF_TO_DA_INTNODE(bp) ((xfs_da_intnode_t *)((bp)->b_un.b_addr))
#endif

#define XFS_DA_NODE_ENTSIZE_BYNAME	/* space a name uses */ \
	(sizeof(xfs_da_node_entry_t))
#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_DA_NODE_ENTRIES)
int xfs_da_node_entries(struct xfs_mount *mp);
#define XFS_DA_NODE_ENTRIES(mp)		xfs_da_node_entries(mp)
#else
#define	XFS_DA_NODE_ENTRIES(mp)		((mp)->m_da_node_ents)
#endif

#define	XFS_DA_MAXHASH	((xfs_dahash_t)-1) /* largest valid hash value */

/*
 * Macros used by directory code to interface to the filesystem.
 */
#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_LBSIZE)
int xfs_lbsize(struct xfs_mount *mp);
#define	XFS_LBSIZE(mp)			xfs_lbsize(mp)
#else
#define	XFS_LBSIZE(mp)	((mp)->m_sb.sb_blocksize)
#endif
#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_LBLOG)
int xfs_lblog(struct xfs_mount *mp);
#define	XFS_LBLOG(mp)			xfs_lblog(mp)
#else
#define	XFS_LBLOG(mp)	((mp)->m_sb.sb_blocklog)
#endif

/*
 * Macros used by directory code to interface to the kernel
 */

/*
 * Macros used to manipulate directory off_t's
 */
#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_DA_MAKE_BNOENTRY)
__uint32_t xfs_da_make_bnoentry(struct xfs_mount *mp, xfs_dablk_t bno,
				int entry);
#define	XFS_DA_MAKE_BNOENTRY(mp,bno,entry)	\
	xfs_da_make_bnoentry(mp,bno,entry)
#else
#define	XFS_DA_MAKE_BNOENTRY(mp,bno,entry) \
	(((bno) << (mp)->m_dircook_elog) | (entry))
#endif
#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_DA_MAKE_COOKIE)
off_t xfs_da_make_cookie(struct xfs_mount *mp, xfs_dablk_t bno, int entry,
				xfs_dahash_t hash);
#define	XFS_DA_MAKE_COOKIE(mp,bno,entry,hash)	\
	xfs_da_make_cookie(mp,bno,entry,hash)
#else
#define	XFS_DA_MAKE_COOKIE(mp,bno,entry,hash) \
	(((off_t)XFS_DA_MAKE_BNOENTRY(mp, bno, entry) << 32) | (hash))
#endif
#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_DA_COOKIE_HASH)
xfs_dahash_t xfs_da_cookie_hash(struct xfs_mount *mp, off_t cookie);
#define	XFS_DA_COOKIE_HASH(mp,cookie)		xfs_da_cookie_hash(mp,cookie)
#else
#define	XFS_DA_COOKIE_HASH(mp,cookie)	((xfs_dahash_t)(cookie))
#endif
#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_DA_COOKIE_BNO)
xfs_dablk_t xfs_da_cookie_bno(struct xfs_mount *mp, off_t cookie);
#define	XFS_DA_COOKIE_BNO(mp,cookie)		xfs_da_cookie_bno(mp,cookie)
#else
#define	XFS_DA_COOKIE_BNO(mp,cookie) \
	((xfs_dablk_t)(((off_t)(cookie)) >> ((mp)->m_dircook_elog + 32)))
#endif
#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_DA_COOKIE_ENTRY)
int xfs_da_cookie_entry(struct xfs_mount *mp, off_t cookie);
#define	XFS_DA_COOKIE_ENTRY(mp,cookie)		xfs_da_cookie_entry(mp,cookie)
#else
#define	XFS_DA_COOKIE_ENTRY(mp,cookie) \
	((xfs_dablk_t)((off_t)(cookie) >> 32) & \
	 ((1 << (mp)->m_dircook_elog) - 1))
#endif


/*========================================================================
 * Btree searching and modification structure definitions.
 *========================================================================*/

/*
 * Structure to ease passing around component names.
 */
typedef struct xfs_da_args {
	char		*name;		/* string (maybe not NULL terminated) */
	int		namelen;	/* length of string (maybe no NULL) */
	char		*value;		/* set of bytes (maybe contain NULLs) */
	int		valuelen;	/* length of value */
	int		flags;		/* argument flags (eg: ATTR_NOCREATE) */
	xfs_dahash_t	hashval;	/* hash value of name */
	xfs_ino_t	inumber;	/* input/output inode number */
	struct xfs_inode *dp;		/* directory inode to manipulate */
	xfs_fsblock_t	*firstblock;	/* ptr to firstblock for bmap calls */
	struct xfs_bmap_free *flist;	/* ptr to freelist for bmap_finish */
	struct xfs_trans *trans;	/* current trans (changes over time) */
	xfs_extlen_t	total;		/* total blocks needed, for 1st bmap */
	int		whichfork;	/* data or attribute fork */
	xfs_dablk_t	blkno;		/* blkno of attr leaf of interest */
	int		index;		/* index of attr of interest in blk */
	xfs_dablk_t	rmtblkno;	/* remote attr value starting blkno */
	int		rmtblkcnt;	/* remote attr value block count */
	int		rename;		/* T/F: this is an atomic rename op */
	xfs_dablk_t	blkno2;		/* blkno of 2nd attr leaf of interest */
	int		index2;		/* index of 2nd attr in blk */
	xfs_dablk_t	rmtblkno2;	/* remote attr value starting blkno */
	int		rmtblkcnt2;	/* remote attr value block count */
} xfs_da_args_t;

/*
 * Storage for holding state during Btree searches and split/join ops.
 *
 * Only need space for 5 intermediate nodes.  With a minimum of 62-way
 * fanout to the Btree, we can support over 900 million directory blocks,
 * which is slightly more than enough.
 */
typedef struct xfs_da_state_blk {
	struct buf	*bp;		/* buffer containing block */
	xfs_dablk_t	blkno;		/* filesystem blkno of buffer */
	daddr_t		disk_blkno;	/* on-disk blkno (in BBs) of buffer */
	int		index;		/* relevant index into block */
	xfs_dahash_t	hashval;	/* last hash value in block */
	int		magic;		/* blk's magic number, ie: blk type */
} xfs_da_state_blk_t;

typedef struct xfs_da_state_path {
	int			active;		/* number of active levels */
	struct xfs_da_state_blk	blk[XFS_DA_NODE_MAXDEPTH];
} xfs_da_state_path_t;

typedef struct xfs_da_state {
	struct xfs_da_args	 *args;		/* filename arguments */
	struct xfs_mount	 *mp;		/* filesystem mount point */
	int			 blocksize;	/* logical block size */
	int			 inleaf;	/* insert into 1->lf, 0->splf */
	struct xfs_da_state_path path;		/* search/split paths */
	struct xfs_da_state_path altpath;	/* alternate path for join */
	int			 extravalid;	/* T/F: extrablk is in use */
	int			 extraafter;	/* T/F: extrablk is after new */
	struct xfs_da_state_blk	 extrablk;	/* for double-splits on leafs */
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
int	xfs_da_node_create(struct xfs_da_args *args, xfs_dablk_t blkno,
				  int level, struct buf **bpp, int whichfork);
int	xfs_da_split(struct xfs_da_state *state);

/*
 * Routines used for shrinking the Btree.
 */
int	xfs_da_join(struct xfs_da_state *state);
void	xfs_da_fixhashpath(struct xfs_da_state *state,
				  struct xfs_da_state_path *path_to_to_fix);

/*
 * Routines used for finding things in the Btree.
 */
int	xfs_da_node_lookup_int(struct xfs_da_state *state, int *result);
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
int	xfs_da_grow_inode(struct xfs_da_args *args, int length,
				 xfs_dablk_t *new_blkno);
int	xfs_da_get_buf(struct xfs_trans *trans, struct xfs_inode *dp,
			      xfs_dablk_t bno, struct buf **bp, int whichfork);
int	xfs_da_read_buf(struct xfs_trans *trans, struct xfs_inode *dp,
			       xfs_dablk_t bno, daddr_t mappedbno,
			       struct buf **bpp, int whichfork);
#ifndef SIM
daddr_t	xfs_da_reada_buf(struct xfs_trans *trans, struct xfs_inode *dp,
				xfs_dablk_t bno, int whichfork);
#endif	/* !SIM */
int	xfs_da_shrink_inode(struct xfs_da_args *args, xfs_dablk_t dead_blkno,
				   int length, struct buf *dead_buf);

uint xfs_da_hashname(char *name_string, int name_length);
uint xfs_da_log2_roundup(uint i);
struct xfs_da_state *xfs_da_state_alloc(void);
void xfs_da_state_free(struct xfs_da_state *state);

#ifdef _KERNEL
extern struct zone *xfs_da_state_zone;
#endif /* _KERNEL */

#endif	/* !FS_XFS_DA_BTREE_H */
