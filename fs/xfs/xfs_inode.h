
#ifndef	_XFS_INODE_H
#define	_XFS_INODE_H

struct xfs_inode;

typedef struct xfs_inode_log_item {
	xfs_log_item_t		ili_item;
	struct xfs_inode	*ili_inode;
	unsigned int		ili_recur;
	unsigned int		ili_flags;
	unsigned int		ili_field_mask;
} xfs_inode_log_item_t;

#define	XFS_ILI_HOLD	0x1

/*
 * Flags to indicate fields to be logged for xfs_trans_log_inode().
 */
#define	XFS_I_MODE	0x0001
#define	XFS_I_FORMAT	0x0002
#define	XFS_I_NLINK	0x0004
#define	XFS_I_UID	0x0008
#define	XFS_I_GID	0x0010
#define	XFS_I_SIZE	0x0020
#define	XFS_I_UUID	0x0040
#define	XFS_I_ATIME	0x0080
#define	XFS_I_MTIME	0x0100
#define	XFS_I_CTIME	0x0200
#define	XFS_I_GEN	0x0400

struct xfs_inode;
	
/*
 * This is the type used in the xfs inode hash table.
 * An array of these is allocated for each mounted
 * file system to hash the inodes for that file system.
 */
typedef struct xfs_ihash {
	struct xfs_inode	*ih_next;	
	sema_t			ih_lock;
	ulong			ih_version;
} xfs_ihash_t;

/*
 * This is the xfs in-core inode structure.
 * Most of the on-disk inode is embedded in the i_d field.
 *
 * The extent pointers/inline file space, however, are managed
 * separately.  The memory for this information is pointed to by
 * the i_u1 and i_u3 unions depending on the type of the data.
 * This is used to linearize the array of extents for fast in-core
 * access.  This is used until the file's number of extents
 * surpasses XFS_MAX_INCORE_EXTENTS, at which point all extent pointers
 * are accessed through the buffer cache.
 *
 * Other state kept in the in-core inode is used for identification,
 * locking, transactional updating, etc of the inode.
 */
#define	XFS_INLINE_EXTS	2
#define	XFS_INLINE_DATA	32
typedef struct xfs_inode {
	/* Inode linking and identification information. */
	struct xfs_ihash	*i_hash;	/* pointer to hash header */
	struct xfs_inode	*i_next;	/* inode hash link forw */
	struct xfs_inode	**i_prevp;	/* ptr to prev i_next */
	struct xfs_mount	*i_mount;	/* fs mount struct ptr */
	struct xfs_inode	*i_mnext;	/* next inode in mount's list */
	struct xfs_inode	**i_mprevp;	/* ptr to prev i_next */
	struct vnode		*i_vnode;	/* ptr to associated vnode */
	dev_t			i_dev;		/* dev containing this inode */
	xfs_ino_t		i_ino;		/* inode number (agno/agino) */
	xfs_agblock_t		i_bno;		/* ag block # of inode */
	int			i_index;	/* which inode in block */

	/* Transaction and locking information. */
	xfs_trans_t		*i_transp;	/* ptr to owning transaction */
	xfs_inode_log_item_t	i_item;		/* logging information */
	mrlock_t		i_lock;		/* inode lock */

	/* Miscellaneous state. */
	unsigned short		i_flags;	/* see defined flags below */
	unsigned long		i_vcode;	/* version code token (RFS) */
	unsigned long		i_mapcnt;	/* count of mapped pages */

	/* File incore extent information. */
	size_t			i_bytes; 	/* bytes in i_u1 */	
	union {
		xfs_bmbt_rec_t	*iu_extents;	/* linear map of file extents */
		char		*iu_data;	/* inline file data */
	} i_u1;
	xfs_btree_block_t	*i_broot;	/* file's incore btree root */
	size_t			i_broot_bytes;	/* bytes allocated for root */
	union {
		xfs_bmbt_rec_t	iu_inline_ext[XFS_INLINE_EXTS];
						/* very small file extents */
		char		iu_inline_data[XFS_INLINE_DATA];
						/* very small file data */
		dev_t		iu_rdev;	/* dev number if special */
		xfs_uuid_t	iu_uuid;	/* mount point value */
	} i_u2;

	/* File incore attribute extent information. */
	unsigned short		i_num_aextents;	/* number of attr extents */
	size_t			i_abytes;	/* bytes in i_u3 */
	union {
		xfs_bmbt_rec_t	*iu_aextents;	/* map of attr extents */
		char		*iu_adata;	/* inline attribute data */
	} i_u3;
	
	xfs_dinode_core_t	i_d;		/* most of ondisk inode */
} xfs_inode_t;

/*
 * In-core inode flags.
 */
#define	XFS_IINLINE	0x0001	/* Inline data is read in */
#define	XFS_IEXTENTS	0x0002	/* All extent pointers are read in */
#define	XFS_IBROOT	0x0004	/* i_broot points to the bmap b-tree root */

/*
 * Flags for iget.
 */
#define	XFS_ILOCK_EXCL		MR_UPDATE
#define	XFS_ILOCK_SHARED	MR_ACCESS

/*
 * Maximum number of extent pointers in i_u1.iu_extents.
 */
#define	XFS_MAX_INCORE_EXTENTS	32768

#define	XFS_ITOV(ip)	((ip)->i_vnode)

/*
 * Value for inode buffers' b_ref field.
 */
#define XFS_INOREF	1


/*
 * xfs_iget.c prototypes.
 */
extern xfs_inode_t	*xfs_inode_incore(xfs_mount_t *, xfs_ino_t, void *);
extern xfs_inode_t	*xfs_iget(xfs_mount_t *, xfs_trans_t *, xfs_ino_t,uint);
extern void		xfs_ilock(xfs_inode_t *, int);
extern int		xfs_ilock_nowait(xfs_inode_t *, int);
extern void		xfs_iunlock(xfs_inode_t *);

/*
 * xfs_inode.c prototypes.
 */
extern xfs_inode_t	*xfs_iread(xfs_mount_t *, xfs_trans_t *, xfs_ino_t);
extern void		xfs_idestroy(xfs_inode_t *);
extern void		xfs_idata_realloc(xfs_inode_t *, int);
extern void		xfs_iext_realloc(xfs_inode_t *, int);
extern void		xfs_iroot_realloc(xfs_inode_t *, int);

#endif	/* _XFS_INODE_H */
