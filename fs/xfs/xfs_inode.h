
#ifndef	_XFS_INODE_H
#define	_XFS_INODE_H


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
struct xfs_iunlink_log_item;
#define	XFS_INLINE_EXTS	2
#define	XFS_INLINE_DATA	32
typedef struct xfs_inode {
	/* Inode linking and identification information. */
	struct xfs_ihash	*i_hash;	/* pointer to hash header */
	struct xfs_inode	*i_next;	/* inode hash link forw */
	struct xfs_inode	**i_prevp;	/* ptr to prev i_next */
	struct xfs_mount	*i_mount;	/* fs mount struct ptr */
	struct xfs_inode	*i_mnext;	/* next inode in mount list */
	struct xfs_inode	**i_mprevp;	/* ptr to prev i_next */
	struct vnode		*i_vnode;	/* ptr to associated vnode */
	grio_ticket_t		*i_ticket;	/* grio ticket list */
	mrlock_t		i_ticketlock;
	dev_t			i_dev;		/* dev for this inode */
	xfs_ino_t		i_ino;		/* inode number (agno/agino)*/

	/* Transaction and locking information. */
	xfs_trans_t		*i_transp;	/* ptr to owning transaction*/
	xfs_inode_log_item_t	i_item;		/* logging information */
	mrlock_t		i_lock;		/* inode lock */
	mrlock_t		i_iolock;	/* inode IO lock */
	sema_t			i_flock;	/* inode flush lock */
	unsigned int		i_pincount;	/* inode pin count */
	sema_t			i_pinsema;	/* inode pin sema */
	struct xfs_iunlink_log_item	*i_iui;	/* unlink log item */

	/* I/O state */
	off_t			i_next_offset;	/* seq read detector */
	off_t			i_io_offset;	/* last buf offset */
	xfs_fsblock_t		i_reada_blkno;	/* next blk to start ra */
	unsigned int		i_io_size;	/* file io buffer len */
	unsigned int		i_last_req_sz;	/* last read size */
	unsigned int		i_num_readaheads; /* # read ahead bufs */
	__int64_t		i_new_size;	/* sz when write completes */

	/* Miscellaneous state. */
	unsigned short		i_flags;	/* see defined flags below */
	unsigned long		i_vcode;	/* version code token (RFS) */
	unsigned long		i_mapcnt;	/* count of mapped pages */
	unsigned long		i_update_core;	/* timestamps are dirty */
	unsigned long		i_gen;		/* generation count */

	/* File incore extent information. */
	size_t			i_bytes; 	/* bytes in i_u1 */
	size_t			i_real_bytes;	/* bytes allocated in i_u1 */
	xfs_extnum_t		i_lastex;	/* last iu_extents used */
	union {
		xfs_bmbt_rec_t	*iu_extents;	/* linear map of file exts */
		char		*iu_data;	/* inline file data */
	} i_u1;
	xfs_btree_lblock_t	*i_broot;	/* file's incore btree root */
	size_t			i_broot_bytes;	/* bytes allocated for root */
	union {
		xfs_bmbt_rec_t	iu_inline_ext[XFS_INLINE_EXTS];
						/* very small file extents */
		char		iu_inline_data[XFS_INLINE_DATA];
						/* very small file data */
		dev_t		iu_rdev;	/* dev number if special*/
		uuid_t		iu_uuid;	/* mount point value */
	} i_u2;

	/* File incore attribute extent information. */
	size_t			i_abytes;	/* bytes in i_u3 */
	xfs_extnum_t		i_alastex;	/* last iu_aextents used */
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
 * Flags for inode locking.
 */
#define	XFS_IOLOCK_EXCL		0x1
#define	XFS_IOLOCK_SHARED	0x2
#define	XFS_ILOCK_EXCL		0x4
#define	XFS_ILOCK_SHARED	0x8

/*
 * Maximum number of extent pointers in i_u1.iu_extents.
 */
#define	XFS_MAX_INCORE_EXTENTS	32768

/*
 * Maximum file size.
 * 2^32 -1 for now.
 */
#define XFS_MAX_FILE_OFFSET	0x7fffffff

#define	XFS_ITOV(ip)	((vnode_t*)((ip)->i_vnode))
#define	XFS_VTOI(vp)	((xfs_inode_t*)((vp)->v_data))

/*
 * Value for inode buffers' b_ref field.
 */
#define XFS_INOREF	1

/*
 * Log reservation value required by xfs_itruncate().
 */
#define	XFS_ITRUNCATE_LOG_RES	BBTOB(128)


/*
 * XFS file identifier.
 */
typedef struct xfs_fid {
	u_short		fid_len;       /* length of remainder */
        u_short		fid_pad;       /* padding, must be zero */
	u_long		fid_gen;       /* generation number */
        xfs_ino_t	fid_ino;       /* inode number */
} xfs_fid_t;

/*
 * xfs_iget.c prototypes.
 */
void		xfs_ihash_init(xfs_mount_t *);
xfs_inode_t	*xfs_inode_incore(xfs_mount_t *, xfs_ino_t, xfs_trans_t *);
xfs_inode_t	*xfs_iget(xfs_mount_t *, xfs_trans_t *, xfs_ino_t,uint);
void		xfs_iput(xfs_inode_t *, uint);
void		xfs_ilock(xfs_inode_t *, uint);
int		xfs_ilock_nowait(xfs_inode_t *, uint);
void		xfs_iunlock(xfs_inode_t *, uint);
void		xfs_iflock(xfs_inode_t *);
int		xfs_iflock_nowait(xfs_inode_t *);
void		xfs_ifunlock(xfs_inode_t *);
void		xfs_ireclaim(xfs_inode_t *);

/*
 * xfs_inode.c prototypes.
 */
xfs_inode_t	*xfs_iread(xfs_mount_t *, xfs_trans_t *, xfs_ino_t);
void		xfs_iread_extents(xfs_trans_t *, xfs_inode_t *);
xfs_inode_t	*xfs_ialloc(xfs_trans_t	*, xfs_inode_t *, mode_t, ushort,
			    dev_t, struct cred *, buf_t **, boolean_t *);
void		xfs_ifree(xfs_trans_t *, xfs_inode_t *);
void		xfs_itruncate(xfs_trans_t **, xfs_inode_t *, __int64_t);

void		xfs_idestroy(xfs_inode_t *);
void		xfs_idata_realloc(xfs_inode_t *, int);
void		xfs_iext_realloc(xfs_inode_t *, int);
void		xfs_iroot_realloc(xfs_inode_t *, int);
void		xfs_ipin(xfs_inode_t *);
void		xfs_iunpin(xfs_inode_t *);
void		xfs_iflush(xfs_inode_t *, uint);
void		xfs_iflush_all(xfs_mount_t *);
void		xfs_iprint(xfs_inode_t *);
int		xfs_iaccess(xfs_inode_t *, mode_t, struct cred *);

extern struct zone	*xfs_inode_zone;

#endif	/* _XFS_INODE_H */
