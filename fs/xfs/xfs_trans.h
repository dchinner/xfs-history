
#ifndef	_XFS_TRANS_H
#define	_XFS_TRANS_H

struct xfs_item_ops;
struct xfs_log_item_desc;
struct xfs_mount;

typedef struct xfs_log_item {
	xfs_lsn_t			li_lsn;		/* last on-disk lsn */
	struct xfs_log_item		*li_forw;	/* AIL pointer */
	struct xfs_log_item		*li_back;	/* AIL pointer */
	struct xfs_log_item_desc	*li_desc;	/* ptr to current desc*/
	struct xfs_mount		*li_mountp;	/* ptr to fs mount */
	uint				li_type;	/* item type */
	uint				li_flags;	/* misc flags */
	struct xfs_log_item		*li_bio_list;	/* buffer item list */
	void				(*li_cb)(buf_t*, struct xfs_log_item*); 
							/* buffer item iodone */
							/* callback func */
	struct xfs_item_ops		*li_ops;	/* function list */
} xfs_log_item_t;

#define	XFS_LI_IN_AIL	0x1
#define	XFS_LI_LEFTSIB	0x2
#define	XFS_LI_RIGHTSIB	0x4

/*
 * Log item types.
 */
#define	XFS_LI_BUF	0x1
#define	XFS_LI_INODE	0x2



typedef struct xfs_item_ops {
	uint(*iop_size)(xfs_log_item_t*);
	uint(*iop_format)(xfs_log_item_t*, caddr_t, uint, int*);
	void(*iop_pin)(xfs_log_item_t*);	
	void(*iop_unpin)(xfs_log_item_t*);	
	uint(*iop_trylock)(xfs_log_item_t*);	
	void(*iop_unlock)(xfs_log_item_t*);	
	xfs_lsn_t(*iop_committed)(xfs_log_item_t*, xfs_lsn_t);	
	void(*iop_push)(xfs_log_item_t*);	
} xfs_item_ops_t;

#define	IOP_SIZE(ip)		(*(ip)->li_ops->iop_size)(ip)
#define	IOP_FORMAT(ip,bp,sz,kp)	(*(ip)->li_ops->iop_format)(ip, bp, sz, kp)
#define	IOP_PIN(ip)		(*(ip)->li_ops->iop_pin)(ip)
#define	IOP_UNPIN(ip)		(*(ip)->li_ops->iop_unpin)(ip)
#define	IOP_TRYLOCK(ip)		(*(ip)->li_ops->iop_trylock)(ip)
#define	IOP_UNLOCK(ip)		(*(ip)->li_ops->iop_unlock)(ip)
#define	IOP_COMMITTED(ip, lsn)	(*(ip)->li_ops->iop_committed)(ip, lsn)
#define	IOP_PUSH(ip)		(*(ip)->li_ops->iop_push)(ip)


/*
 * This structure is used to track log items associated with
 * a transaction.  It points to the log item and keeps some
 * flags to track the state of the log item.  It also tracks
 * the amount of space needed to log the item it describes
 * once we get to commit processing (see xfs_trans_commit()).
 */
typedef struct xfs_log_item_desc {
	xfs_log_item_t	*lid_item;
	ushort		lid_size;
	unsigned char	lid_flags;
	unsigned char	lid_index;
} xfs_log_item_desc_t;

#define	XFS_LID_DIRTY	0x1
#define	XFS_LID_PINNED	0x2

/*
 * This structure is used to maintain a chunk list of log_item_desc
 * structures. The free field is a bitmask indicating which descriptors
 * in this chunk's array are free.
 */
typedef struct xfs_log_item_chunk {
	struct xfs_log_item_chunk	*lic_next;
	unsigned int			lic_free;
	xfs_log_item_desc_t		lic_descs[15];
} xfs_log_item_chunk_t;

#define	XFS_LIC_MAX_SLOT	14
#define	XFS_LIC_NUM_SLOTS	15
#define	XFS_LIC_FREEMASK	0x7fff
/*
 * Initialize the given chunk.  For each descriptor, set its index
 * to where it is in the chunk and its size to 0.  Set the chunk's
 * free descriptor mask to indicate that all descriptors are free.
 */
#define	XFS_LIC_INIT(cp)	{ \
					int			x; \
					xfs_log_item_desc_t	*dp; \
					dp = (cp)->lic_descs; \
					for (x = 0; x <= XFS_LIC_MAX_SLOT; \
					     x++) { \
						dp->lid_index = (unsigned char)x; \
						dp++; \
					} \
					(cp)->lic_free = XFS_LIC_FREEMASK; \
				}
#define	XFS_LIC_VACANCY(cp)		(((cp)->lic_free) & XFS_LIC_FREEMASK)	
#define	XFS_LIC_ALL_FREE(cp)		((cp)->lic_free |= XFS_LIC_FREEMASK)
#define	XFS_LIC_ARE_ALL_FREE(cp)	(((cp)->lic_free & XFS_LIC_FREEMASK) ==\
					XFS_LIC_FREEMASK)
#define	XFS_LIC_ISFREE(cp, slot)	((cp)->lic_free & (1 << (slot)))
#define	XFS_LIC_CLAIM(cp, slot)		((cp)->lic_free &= ~(1 << (slot)))
#define	XFS_LIC_RELSE(cp, slot)		((cp)->lic_free |= 1 << (slot))
#define	XFS_LIC_SLOT(cp, slot)		(&((cp)->lic_descs[slot]))
#define	XFS_LIC_DESC_TO_SLOT(dp)	((uint)((dp)->lid_index))
/*
 * Calculate the address of a chunk given a descriptor pointer:
 * dp - dp->lid_index give the address of the start of the lic_descs array.
 * From this we subtract the offset of the lic_descs field in a chunk.
 * All of this yields the address of the chunk, which is
 * cast to a chunk pointer.
 */
#define	XFS_LIC_DESC_TO_CHUNK(dp)	((xfs_log_item_chunk_t*) \
					(((caddr_t)((dp) - (dp)->lid_index)) -\
					(caddr_t)(((xfs_log_item_chunk_t*) \
					0)->lic_descs)))
/*
 * This is the unique transaction identifier.
 * 32 bits should be plenty given that transactions only
 * last so long.
 */
typedef uint	xfs_trans_id_t;

/*
 * This is the type of function which can be given to xfs_trans_callback()
 * to be called upon the transaction's commit to disk.
 */
struct xfs_trans;
typedef void(*xfs_trans_callback_t)(struct xfs_trans*, void*);

/*
 * This is the structure maintained for every active transaction.
 */
typedef struct xfs_trans {
	xfs_trans_id_t		t_tid;		/* transaction id */
	struct xfs_trans	*t_forw;	/* forw link ptr */
	struct xfs_trans	*t_back;	/* back link ptr */
	unsigned int		t_type;		/* transaction type */
	unsigned int		t_log_res;	/* amt of log space resvd */	
	unsigned int		t_blk_res;	/* # of blocks resvd */
	unsigned int		t_blk_res_used;	/* # of resvd blocks used */
	sema_t			t_sema;		/* sema for commit completion */
	xfs_lsn_t		t_lsn;		/* log seq num of trans commit*/
	struct xfs_mount	*t_mountp;	/* ptr to fs mount struct */
	xfs_trans_callback_t	t_callback;	/* transaction callback */
	void			*t_callarg;	/* callback arg */
	unsigned int		t_flags;	/* misc flags */
	int			t_icount_delta;	/* superblock icount change */
	int			t_ifree_delta;	/* superblock ifree change */
	int			t_fdblocks_delta; /* superblock fdblocks chg */
	int			t_frextents_delta;/* superblock freextents chg*/
	unsigned int		t_items_free;	/* log item descs free */
	xfs_log_item_chunk_t	t_items;	/* first log item desc chunk */
} xfs_trans_t;

/*
 * Values for t_flags.
 */
#define	XFS_TRANS_DIRTY		0x1
#define	XFS_TRANS_SB_DIRTY	0x2

/*
 * Values for call flags parameter.
 */
#define	XFS_TRANS_NOSLEEP	0x1
#define	XFS_TRANS_WAIT		0x2
#define	XFS_TRANS_SYNC		0x4

/*
 * This has not been thought through.
 * It is just to get the code to compile.
 */
typedef struct xfs_trans_header {
	xfs_lsn_t	th_lsn;
	uint		th_type;
	xfs_trans_id_t	th_tid;
} xfs_trans_header_t;
/*
 * This has not been thought through.
 * It is just to get the code to compile.
 */
typedef struct xfs_trans_commit {
	uint		tc_type;
	xfs_trans_id_t	tc_tid;
} xfs_trans_commit_t;

struct xfs_inode;
struct xfs_mount;

/*
 * xFS transaction mechanism exported interfaces.
 */
extern xfs_trans_t	*xfs_trans_alloc(struct xfs_mount *, uint);
extern int		xfs_trans_reserve(xfs_trans_t *, uint, uint, uint);
extern void		xfs_trans_callback(xfs_trans_t *,
					   void(*)(xfs_trans_t*, void*),
					   void *);
extern void		xfs_trans_mod_sb(xfs_trans_t *, uint, int);
extern buf_t		*xfs_trans_getblk(xfs_trans_t *, dev_t, daddr_t, int);
extern buf_t		*xfs_trans_getsb(xfs_trans_t *);
extern buf_t		*xfs_trans_bread(xfs_trans_t *, dev_t, daddr_t, int);
extern buf_t		*xfs_trans_getchunk(xfs_trans_t *, vnode_t *,
					    struct bmapval *, struct cred *);
extern buf_t		*xfs_trans_chunkread(xfs_trans_t *, vnode_t *,
					     struct bmapval *, struct cred *);
extern void		xfs_trans_brelse(xfs_trans_t *, buf_t *);
extern void		xfs_trans_bjoin(xfs_trans_t *, buf_t *);
extern void		xfs_trans_bhold(xfs_trans_t *, buf_t *);
extern struct xfs_inode	*xfs_trans_iget(struct xfs_mount *, xfs_trans_t *,
					xfs_ino_t , uint);
extern void		xfs_trans_iput(xfs_trans_t *, struct xfs_inode *);
extern void		xfs_trans_ijoin(xfs_trans_t *, struct xfs_inode *);
extern void		xfs_trans_ihold(xfs_trans_t *, struct xfs_inode *);
extern void		xfs_trans_log_buf(xfs_trans_t *, buf_t *, uint, uint);
extern void		xfs_trans_log_inode(xfs_trans_t *, struct xfs_inode *,
					    uint);
extern void		xfs_trans_log_op(xfs_trans_t *, xfs_log_item_t *);
extern xfs_trans_id_t	xfs_trans_id(xfs_trans_t *);
extern void		xfs_trans_commit(xfs_trans_t *, uint flags);
void			xfs_trans_commit_async(struct xfs_mount *);
extern void		xfs_trans_cancel(xfs_trans_t *);

/*
 * Not necessarily exported, but used outside a single file.
 */
int			xfs_trans_lsn_danger(struct xfs_mount *, xfs_lsn_t);

#endif	/* _XFS_TRANS_H */
