
#ifndef	_XFS_INODE_H
#define	_XFS_INODE_H


struct xfs_inode;
struct ktrace;
struct xfs_gap;
	
/*
 * This is the type used in the xfs inode hash table.
 * An array of these is allocated for each mounted
 * file system to hash the inodes for that file system.
 */
typedef struct xfs_ihash {
	struct xfs_inode	*ih_next;	
	sema_t			ih_lock;
	uint			ih_version;
} xfs_ihash_t;

#ifdef NOTYET
/*
 * The range structure is used to describe a locked range
 * within a file.  It is used in conjunction with the
 * xfs_range_lock_t defined below.
 */
typedef struct xfs_range {
	struct xfs_range	*r_forw;	/* range list pointer */
	struct xfs_range	*r_back;	/* range list pointer */
	xfs_fileoff_t		r_off;		/* start of range */
	xfs_fileoff_t		r_len;		/* length of range */
	int			r_sleepers;	/* count of sleepers */
} xfs_range_t;

/*
 * This is a structure embedded in the incore inode for
 * tracking range locks over the file.  The semaphore is
 * dynamically allocated to reduce our memory consumption.
 */
typedef struct xfs_range_lock {
	lock_t		r_splock;	/* lock to make sleeps atomic */
	sema_t		*r_sleep;	/* semaphore for sleeping on */
	xfs_range_t	*r_range_list;	/* list of locked ranges */
} xfs_range_lock_t;
#endif /* NOTYET */

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
	struct xfs_inode	*i_mnext;	/* next inode in mount list */
	struct xfs_inode	*i_mprev;	/* ptr to prev inode */
	struct vnode		*i_vnode;	/* ptr to associated vnode */
	struct grio_ticket	*i_ticket;	/* grio ticket list */
	lock_t			i_ticketlock;

	/* Inode location stuff */
	dev_t			i_dev;		/* dev for this inode */
	xfs_ino_t		i_ino;		/* inode number (agno/agino)*/
	daddr_t			i_blkno;	/* blkno of inode buffer */
	int			i_len;		/* len of inode buffer */
	short			i_boffset;	/* off of inode in buffer */

	/* Transaction and locking information. */
	xfs_trans_t		*i_transp;	/* ptr to owning transaction*/
	xfs_inode_log_item_t	i_item;		/* logging information */
	mrlock_t		i_lock;		/* inode lock */
	mrlock_t		i_iolock;	/* inode IO lock */
	sema_t			i_flock;	/* inode flush lock */
	unsigned int		i_pincount;	/* inode pin count */
	sema_t			i_pinsema;	/* inode pin sema */
#ifdef NOTYET
	xfs_range_lock_t	i_range_lock;	/* range lock base */
#endif /* NOTYET */

	/* I/O state */
	off_t			i_next_offset;	/* seq read detector */
	off_t			i_io_offset;	/* last buf offset */
	xfs_fileoff_t		i_reada_blkno;	/* next blk to start ra */
	unsigned int		i_io_size;	/* file io buffer len */
	unsigned int		i_last_req_sz;	/* last read size */
	unsigned int		i_num_readaheads; /* # read ahead bufs */
	xfs_fsize_t		i_new_size;	/* sz when write completes */
	off_t			i_write_offset;	/* start off of curr write */
	struct xfs_gap		*i_gap_list;	/* hole list in write range */

	/* Miscellaneous state. */
	unsigned short		i_flags;	/* see defined flags below */
	unsigned long		i_vcode;	/* version code token (RFS) */
	unsigned long		i_mapcnt;	/* count of mapped pages */
	unsigned int		i_update_core;	/* timestamps are dirty */
	unsigned int		i_gen;		/* generation count */
	unsigned int		i_delayed_blks;	/* count of delay alloc blks */
	int			i_queued_bufs;	/* count of xfsd queued bufs*/

	/* File incore extent information. */
	size_t			i_bytes; 	/* bytes in i_u1 */
	size_t			i_real_bytes;	/* bytes allocated in i_u1 */
	xfs_extnum_t		i_lastex;	/* last iu_extents used */
	union {
		xfs_bmbt_rec_t	*iu_extents;	/* linear map of file exts */
		char		*iu_data;	/* inline file data */
	} i_u1;
	xfs_bmbt_block_t	*i_broot;	/* file's incore btree root */
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

	/* Trace buffers per inode. */
	struct ktrace		*i_xtrace;	/* inode extent list trace */
	struct ktrace		*i_btrace;	/* inode bmap btree trace */
	struct ktrace		*i_rwtrace;	/* inode read/write trace */
	struct ktrace		*i_strat_trace;	/* inode strat_write trace */

	/* DMI state */
	unsigned int		i_dmevents;	/* events enabled on file */

	xfs_dinode_core_t	i_d;		/* most of ondisk inode */
} xfs_inode_t;

/*
 * In-core inode flags.
 */
#define	XFS_IINLINE	0x0001	/* Inline data is read in */
#define	XFS_IEXTENTS	0x0002	/* All extent pointers are read in */
#define	XFS_IBROOT	0x0004	/* i_broot points to the bmap b-tree root */
#define XFS_IGRIO	0x0008  /* inode will be used for guaranteed rate i/o */

/*
 * Flags for inode locking.
 */
#define	XFS_IOLOCK_EXCL		0x01
#define	XFS_IOLOCK_SHARED	0x02
#define	XFS_ILOCK_EXCL		0x04
#define	XFS_ILOCK_SHARED	0x08
#define	XFS_IUNLOCK_NONOTIFY	0x10

/*
 * Flags for xfs_iflush()
 */
#define	XFS_IFLUSH_DELWRI_ELSE_SYNC	1
#define	XFS_IFLUSH_DELWRI_ELSE_ASYNC	2
#define	XFS_IFLUSH_SYNC			3
#define	XFS_IFLUSH_ASYNC		4
#define	XFS_IFLUSH_DELWRI		5

/*
 * Flags for xfs_iflush_all.
 */
#define	XFS_FLUSH_ALL		0x1

/*
 * Flags for xfs_itruncate_start().
 */
#define	XFS_ITRUNC_DEFINITE	0x1
#define	XFS_ITRUNC_MAYBE	0x2

/*
 * Flags for xfs_ichgtime().
 */
#define	XFS_ICHGTIME_MOD	0x1	/* data fork modification timestamp */
#define	XFS_ICHGTIME_ACC	0x2	/* data fork access timestamp */
#define	XFS_ICHGTIME_CHG	0x4	/* inode field change timestamp */

/*
 * Flags for xfs_imap() and xfs_dilocate().
 */
#define	XFS_IMAP_LOOKUP		0x1

/*
 * Maximum number of extent pointers in i_u1.iu_extents.
 */
#define	XFS_MAX_INCORE_EXTENTS	32768

/*
 * Maximum file size.
 * if XFS_BIG_FILES 2^63 - 1 (largest positive value of xfs_fsize_t)
 * else 2^40 - 1 (40=31+9) (might be an int holding a block #)
 */
#if XFS_BIG_FILES
#define XFS_MAX_FILE_OFFSET	((long long)((1ULL<<63)-1ULL))
#else
#define	XFS_MAX_FILE_OFFSET	((1LL<<40)-1LL)
#endif

/*
 * This is used to figure out what to pass to flush/inval
 * routines since the write code allocates pages beyond the
 * end of the file.
 */
#define	XFS_ISIZE_MAX(ip)	((ip)->i_d.di_size + \
				 (1 << (ip)->i_mount->m_writeio_log))

#define	XFS_ITOV(ip)	((vnode_t*)((ip)->i_vnode))
#define	XFS_VTOI(vp)	((xfs_inode_t*)((vp)->v_data))

/*
 * Clear out the read-ahead state in the in-core inode.
 */
#define	XFS_INODE_CLEAR_READ_AHEAD(ip)	{	\
		ip->i_next_offset = 0;		\
		ip->i_io_offset = 0;		\
		ip->i_reada_blkno = 0;		\
		ip->i_io_size = 0;		\
		ip->i_last_req_sz = 0;		\
		ip->i_num_readaheads = 0; }
     
/*
 * Value for inode buffers' b_ref field.
 */
#define XFS_INOREF	1

/*
 * XFS file identifier.
 */
typedef __uint32_t	xfs_fid_ino_t;
typedef struct xfs_fid {
	u_short		fid_len;       /* length of remainder */
        u_short		fid_pad;       /* padding, must be zero */
	__uint32_t		fid_gen;       /* generation number */
        xfs_fid_ino_t	fid_ino;       /* inode number */
} xfs_fid_t;

/*
 * xfs_iget.c prototypes.
 */
void		xfs_ihash_init(xfs_mount_t *);
void		xfs_ihash_free(xfs_mount_t *);
xfs_inode_t	*xfs_inode_incore(xfs_mount_t *, xfs_ino_t, xfs_trans_t *);
xfs_inode_t	*xfs_iget(xfs_mount_t *, xfs_trans_t *, xfs_ino_t, uint);
void		xfs_iput(xfs_inode_t *, uint);
void		xfs_ilock(xfs_inode_t *, uint);
int		xfs_ilock_nowait(xfs_inode_t *, uint);
void		xfs_iunlock(xfs_inode_t *, uint);
void		xfs_iflock(xfs_inode_t *);
int		xfs_iflock_nowait(xfs_inode_t *);
uint		xfs_ilock_map_shared(xfs_inode_t *);
void		xfs_iunlock_map_shared(xfs_inode_t *, uint);
void		xfs_ifunlock(xfs_inode_t *);
void		xfs_ireclaim(xfs_inode_t *);

/*
 * xfs_inode.c prototypes.
 */
buf_t		*xfs_inotobp(xfs_mount_t *, xfs_trans_t *, xfs_ino_t,
			     xfs_dinode_t **);
xfs_inode_t	*xfs_iread(xfs_mount_t *, xfs_trans_t *, xfs_ino_t);
void		xfs_iread_extents(xfs_trans_t *, xfs_inode_t *);
xfs_inode_t	*xfs_ialloc(xfs_trans_t	*, xfs_inode_t *, mode_t, ushort,
			    dev_t, struct cred *, buf_t **, boolean_t *);
#ifndef SIM
void		xfs_ifree(xfs_trans_t *, xfs_inode_t *);
void		xfs_itruncate_start(xfs_inode_t *, uint, xfs_fsize_t);
void		xfs_itruncate_finish(xfs_trans_t **, xfs_inode_t *,
				     xfs_fsize_t);
void		xfs_iunlink(xfs_trans_t *, xfs_inode_t *);
#endif	/* !SIM */
void		xfs_igrow_start(xfs_inode_t *, xfs_fsize_t, struct cred *);
void		xfs_igrow_finish(xfs_trans_t *, xfs_inode_t *,
				 xfs_fsize_t);

void		xfs_idestroy(xfs_inode_t *);
void		xfs_idata_realloc(xfs_inode_t *, int);
void		xfs_iext_realloc(xfs_inode_t *, int);
void		xfs_iroot_realloc(xfs_inode_t *, int);
void		xfs_ipin(xfs_inode_t *);
void		xfs_iunpin(xfs_inode_t *);
int		xfs_iextents_copy(xfs_inode_t *, char *);
void		xfs_iflush(xfs_inode_t *, uint);
int		xfs_iflush_all(xfs_mount_t *, int);
#ifdef SIM
void		xfs_iprint(xfs_inode_t *);
#endif
int		xfs_iaccess(xfs_inode_t *, mode_t, struct cred *);
uint		xfs_iroundup(uint);
void		xfs_ichgtime(xfs_inode_t *, int);

#ifdef DEBUG
void		xfs_isize_check(xfs_mount_t *, xfs_inode_t *, xfs_fsize_t);
void		xfs_inobp_check(xfs_mount_t *, buf_t *);
#else	/* DEBUG */
#define xfs_isize_check(mp, ip, isize)
#define	xfs_inobp_check(mp, bp)
#endif	/* DEBUG */

extern struct zone	*xfs_inode_zone;

#endif	/* _XFS_INODE_H */
