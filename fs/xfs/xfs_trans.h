
#ifndef	_XFS_TRANS_H
#define	_XFS_TRANS_H

struct xfs_item_ops;
struct xfs_log_item_desc;
struct xfs_mount;
struct xfs_log_item;

typedef struct xfs_ail_entry {
	struct xfs_log_item	*ail_forw;	/* AIL forw pointer */
	struct xfs_log_item	*ail_back;	/* AIL back pointer */
} xfs_ail_entry_t;

/*
 * This structure is passed as a parameter to xfs_trans_push_ail()
 * and is used to track the what LSN the waiting processes are
 * waiting to become unused.
 */
typedef struct xfs_ail_ticket {
	xfs_lsn_t		at_lsn;		/* lsn waitin for */
	struct xfs_ail_ticket	*at_forw;	/* wait list ptr */
	struct xfs_ail_ticket	*at_back;	/* wait list ptr */
	sema_t			at_sema;	/* wait sema */
} xfs_ail_ticket_t;
	

typedef struct xfs_log_item {
	xfs_ail_entry_t			li_ail;		/* AIL pointers */
	xfs_lsn_t			li_lsn;		/* last on-disk lsn */
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

/*
 * Log item types.
 */
#define	XFS_LI_OBUF	0x1234
#define	XFS_LI_OINODE	0x1235
#define	XFS_LI_EFI	0x1236
#define	XFS_LI_EFD	0x1237
#define	XFS_LI_IUNLINK	0x1238
#define	XFS_LI_INODE	0x1239
#define	XFS_LI_BUF	0x123a

/*
 * Transaction types.  Used to distinguish types of buffers.
 */
#define XFS_TRANS_SETATTR_NOT_SIZE	1
#define XFS_TRANS_SETATTR_SIZE		2
#define XFS_TRANS_INACTIVE		3
#define XFS_TRANS_CREATE		4
#define XFS_TRANS_CREATE_TRUNC		5
#define XFS_TRANS_TRUNCATE_FILE		6
#define XFS_TRANS_REMOVE		7
#define XFS_TRANS_LINK			8
#define XFS_TRANS_RENAME		9
#define XFS_TRANS_MKDIR			10
#define XFS_TRANS_RMDIR			11
#define XFS_TRANS_SYMLINK		12
#define XFS_TRANS_SET_DMATTRS		13
#define XFS_TRANS_GROWFS		14
#define XFS_TRANS_STRAT_WRITE		15
#define XFS_TRANS_DIOSTRAT		16
#define	XFS_TRANS_WRITE_SYNC		17


typedef struct xfs_item_ops {
	uint(*iop_size)(xfs_log_item_t*);
	void(*iop_format)(xfs_log_item_t*, xfs_log_iovec_t*);
	void(*iop_pin)(xfs_log_item_t*);	
	void(*iop_unpin)(xfs_log_item_t*);	
	uint(*iop_trylock)(xfs_log_item_t*);	
	void(*iop_unlock)(xfs_log_item_t*);	
	xfs_lsn_t(*iop_committed)(xfs_log_item_t*, xfs_lsn_t);	
	void(*iop_push)(xfs_log_item_t*);	
} xfs_item_ops_t;

#define	IOP_SIZE(ip)		(*(ip)->li_ops->iop_size)(ip)
#define	IOP_FORMAT(ip,vp)	(*(ip)->li_ops->iop_format)(ip, vp)
#define	IOP_PIN(ip)		(*(ip)->li_ops->iop_pin)(ip)
#define	IOP_UNPIN(ip)		(*(ip)->li_ops->iop_unpin)(ip)
#define	IOP_TRYLOCK(ip)		(*(ip)->li_ops->iop_trylock)(ip)
#define	IOP_UNLOCK(ip)		(*(ip)->li_ops->iop_unlock)(ip)
#define	IOP_COMMITTED(ip, lsn)	(*(ip)->li_ops->iop_committed)(ip, lsn)
#define	IOP_PUSH(ip)		(*(ip)->li_ops->iop_push)(ip)

/*
 * Return values for the IOP_TRYLOCK() routines.
 */
#define	XFS_ITEM_SUCCESS	0
#define	XFS_ITEM_PINNED		1
#define	XFS_ITEM_LOCKED		2
#define	XFS_ITEM_FLUSHING	3
 

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

#define	XFS_LID_DIRTY		0x1
#define	XFS_LID_PINNED		0x2
#define	XFS_LID_SYNC_UNLOCK	0x4

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
 * This is the structure written in the log at the head of
 * every transaction. It identifies the type and id of the
 * transaction, and contains the number of items logged by
 * the transaction so we know how many to expect during recovery.
 *
 * Do not change the below structure without redoing the code in
 * xlog_recover_add_to_trans() and xlog_recover_add_to_cont_trans().
 */
typedef struct xfs_trans_header {
	uint		th_magic;		/* magic number */
	uint		th_type;		/* transaction type */
	xfs_trans_id_t	th_tid;			/* transaction id */
	uint		th_num_items;		/* num items logged by trans */
} xfs_trans_header_t;

#define	XFS_TRANS_HEADER_MAGIC	0x5452414e	/* TRAN */

/*
 * This is the structure maintained for every active transaction.
 */
typedef struct xfs_trans {
	unsigned int		t_magic;	/* magic number */
	xfs_trans_id_t		t_tid;		/* transaction id */
	xfs_log_callback_t	t_logcb;	/* log callback struct */
	struct xfs_trans	*t_forw;	/* async list pointers */
	struct xfs_trans	*t_back;	/* async list pointers */
	unsigned int		t_type;		/* transaction type */
	unsigned int		t_log_res;	/* amt of log space resvd */
	unsigned int		t_log_count;	/* count for perm log res */
	unsigned int		t_blk_res;	/* # of blocks resvd */
	unsigned int		t_blk_res_used;	/* # of resvd blocks used */
	unsigned int		t_rtx_res;	/* # of rt extents resvd */
	unsigned int		t_rtx_res_used;	/* # of resvd rt extents used */
	xfs_log_ticket_t	t_ticket;	/* log mgr ticket */
	sema_t			t_sema;		/* sema for commit completion */
	xfs_lsn_t		t_lsn;		/* log seq num of trans commit*/
	struct xfs_mount	*t_mountp;	/* ptr to fs mount struct */
	xfs_trans_callback_t	t_callback;	/* transaction callback */
	void			*t_callarg;	/* callback arg */
	unsigned int		t_flags;	/* misc flags */
	long			t_icount_delta;	/* superblock icount change */
	long			t_ifree_delta;	/* superblock ifree change */
	long			t_fdblocks_delta; /* superblock fdblocks chg */
	long			t_res_fdblocks_delta; /* on-disk only chg */
	long			t_frextents_delta;/* superblock freextents chg*/
	long			t_res_frextents_delta; /* on-disk only chg */
	long			t_ag_freeblks_delta; /* debugging counter */
	long			t_ag_flist_delta; /* debugging counter */
	long			t_ag_btree_delta; /* debugging counter */
	long			t_dblocks_delta;/* superblock dblocks change */
	long			t_agcount_delta;/* superblock agcount change */
	unsigned int		t_items_free;	/* log item descs free */
	xfs_log_item_chunk_t	t_items;	/* first log item desc chunk */
	xfs_trans_header_t	t_header;	/* header for in-log trans */
} xfs_trans_t;

#define	XFS_TRANS_MAGIC		0x5452414E	/* 'TRAN' */
/*
 * Values for t_flags.
 */
#define	XFS_TRANS_DIRTY		0x01	/* something needs to be logged */
#define	XFS_TRANS_SB_DIRTY	0x02	/* superblock is modified */
#define	XFS_TRANS_PERM_LOG_RES	0x04	/* xact took a permanent log res */
#define	XFS_TRANS_SYNC		0x08	/* make commit synchronous */

/*
 * Values for call flags parameter.
 */
#define	XFS_TRANS_NOSLEEP		0x1
#define	XFS_TRANS_WAIT			0x2
#define	XFS_TRANS_RELEASE_LOG_RES	0x4

/*
 * Field values for xfs_trans_mod_sb.
 */
#define	XFS_TRANS_SB_ICOUNT		0x00000001
#define	XFS_TRANS_SB_IFREE		0x00000002
#define	XFS_TRANS_SB_FDBLOCKS		0x00000004
#define	XFS_TRANS_SB_RES_FDBLOCKS	0x00000008
#define	XFS_TRANS_SB_FREXTENTS		0x00000010
#define	XFS_TRANS_SB_RES_FREXTENTS	0x00000020
#define	XFS_TRANS_SB_DBLOCKS		0x00000040
#define	XFS_TRANS_SB_AGCOUNT		0x00000080

/*
 * Various log reservation values.
 * These are based on the size of the file system block
 * because that is what most transactions manipulate.
 * Each adds in an additional 128 bytes per item logged to
 * try to account for the overhead of the transaction mechanism.
 *
 * Note:
 * Most of the reservations underestimate the number of allocation
 * groups into which they could free extents in the xfs_bmap_finish()
 * call.  This is because the number in the worst case is quite high
 * and quite unusual.  In order to fix this we need to change
 * xfs_bmap_finish() to free extents in only a single AG at a time.
 * This will require changes to the EFI code as well, however, so that
 * the EFI for the extents not freed is logged again in each transaction.
 */

/*
 * In a write transaction we can allocate a maximum of 2
 * extents.  This gives:
 *    the inode getting the new extents: inode size
 *    the inode\'s bmap btree: max depth * block size
 *    the agfs of the ags from which the extents are allocate: 2 * sector
 *    the superblock free block counter: sector size
 *    the allocation btrees: 2 * 2 * max depth * block size
 * And the bmap_finish transaction can free bmap blocks in a join:
 *    the agfs of the ags containing the blocks: 2 * sector size
 *    the super block free block counter: sector size
 *    the allocation btrees: 2 * 2 * max depth * block size
 */
#define XFS_WRITE_LOG_RES(mp) \
	(MAX( \
	 ((mp)->m_sb.sb_inodesize + \
	  XFS_FSB_TO_B((mp), XFS_BM_MAXLEVELS(mp)) + \
	  (2 * (mp)->m_sb.sb_sectsize) + \
	  (mp)->m_sb.sb_sectsize + \
	  (2 * 2 * XFS_FSB_TO_B((mp), XFS_AG_MAXLEVELS(mp))) + \
	  (128 * (4 + XFS_BM_MAXLEVELS(mp) + (4 * XFS_AG_MAXLEVELS(mp))))),\
	 ((2 * (mp)->m_sb.sb_sectsize) + \
	  (mp)->m_sb.sb_sectsize + \
	  (2 * 2 * XFS_FSB_TO_B((mp), XFS_AG_MAXLEVELS(mp))) + \
	  (128 * (3 + (4 * XFS_AG_MAXLEVELS(mp)))))))
     
/*
 * In truncating a file we can modify:
 *    the inode being truncated: inode size
 *    the inode\'s bmap btree: max depth * block size
 * And the bmap_finish transaction can free the blocks and bmap blocks:
 *    the agf for each of the ags: 4 * sector size
 *    the flist block for each of the ags: 4 * sector size
 *    the super block to reflect the freed blocks: sector size
 *    worst case split in allocation btrees per extent assuming 4 extents:
 *		4 exts * 2 trees * 2 * max depth * block size
 */
#define	XFS_ITRUNCATE_LOG_RES(mp) \
	(MAX( \
	 ((mp)->m_sb.sb_inodesize + \
	  XFS_FSB_TO_B((mp), XFS_BM_MAXLEVELS(mp)) + \
	  (128 * (1 + XFS_BM_MAXLEVELS(mp)))), \
	 ((4 * (mp)->m_sb.sb_sectsize) + \
	  (4 * (mp)->m_sb.sb_sectsize) + \
	  (mp)->m_sb.sb_sectsize + \
	  (4 * 2 * 2 * XFS_FSB_TO_B((mp), XFS_AG_MAXLEVELS(mp))) + \
	  (128 * (5 + (4 * XFS_AG_MAXLEVELS(mp)))))))
     
/*
 * In renaming a files we can modify:
 *    the four inodes involved: 4 * inode size
 *    the two directory btrees: 2 * max depth * block size
 *    the two directory bmap btrees: 2 * max depth * block size
 * And the bmap_finish transaction can free dir and bmap blocks giving:
 *    the agf for the ag in which the blocks live: 2 * sector size
 *    the superblock for the free block count: sector size
 *    the allocation btrees: 2 * 2 * max depth * block size
 */
#define	XFS_RENAME_LOG_RES(mp) \
	(MAX( \
	 ((4 * (mp)->m_sb.sb_inodesize) + \
	  (2 * XFS_FSB_TO_B((mp), XFS_DIR_NODE_MAXDEPTH)) + \
	  (2 * XFS_FSB_TO_B((mp), XFS_BM_MAXLEVELS(mp))) + \
	  (128 * (4 + (2 * XFS_DIR_NODE_MAXDEPTH) + \
	   (2 * XFS_BM_MAXLEVELS(mp))))), \
	 ((2 * (mp)->m_sb.sb_sectsize) + \
	  (mp)->m_sb.sb_sectsize + \
	  (2 * 2 * XFS_FSB_TO_B((mp), XFS_AG_MAXLEVELS(mp))) + \
	  (128 * (3 + (4 * XFS_AG_MAXLEVELS(mp)))))))

/*
 * For creating a link to an inode:
 *    the parent directory inode: inode size
 *    the linked inode: inode size
 *    the directory btree could split: max depth * block size
 *    the directory bmap btree could join or split: max depth * block size
 * And the bmap_finish transaction can free some bmap blocks giving:
 *    the agf for the ag in which the blocks live
 *    the superblock for the free block count: sector size
 *    the allocation btrees: 2 * max depth * block size
 */
#define	XFS_LINK_LOG_RES(mp) \
	(MAX( \
	 ((mp)->m_sb.sb_inodesize + \
 	  (mp)->m_sb.sb_inodesize + \
	  XFS_FSB_TO_B((mp), XFS_DIR_NODE_MAXDEPTH) + \
	  XFS_FSB_TO_B((mp), XFS_BM_MAXLEVELS(mp)) + \
	  (128 * (2 + XFS_DIR_NODE_MAXDEPTH + XFS_BM_MAXLEVELS(mp)))), \
	 ((mp)->m_sb.sb_sectsize + \
	  (mp)->m_sb.sb_sectsize + \
	  (2 * XFS_FSB_TO_B((mp), XFS_AG_MAXLEVELS(mp)))) + \
	  (128 * (2 + (2 * XFS_AG_MAXLEVELS(mp))))))


/*
 * For removing a directory entry we can modify:
 *    the parent directory inode: inode size
 *    the removed inode: inode size
 *    the directory btree could join: max depth * block size
 *    the directory bmap btree could join or split : max depth * block size
 * And the bmap_finish transaction can free the dir blocks freed giving:
 *    the agf for the ag in which the blocks live
 *    the superblock for the free block count: sector size
 *    the allocation btrees: 2 * max depth * block size
 */
#define	XFS_REMOVE_LOG_RES(mp)	\
	(MAX( \
	 ((mp)->m_sb.sb_inodesize + \
 	  (mp)->m_sb.sb_inodesize + \
	  XFS_FSB_TO_B((mp), XFS_DIR_NODE_MAXDEPTH) + \
	  XFS_FSB_TO_B((mp), XFS_BM_MAXLEVELS(mp)) + \
	  (128 * (2 + XFS_DIR_NODE_MAXDEPTH + XFS_BM_MAXLEVELS(mp)))), \
	 ((mp)->m_sb.sb_sectsize + \
	  (mp)->m_sb.sb_sectsize + \
	  (2 * XFS_FSB_TO_B((mp), XFS_AG_MAXLEVELS(mp)))) + \
	  (128 * (2 + (2 * XFS_AG_MAXLEVELS(mp))))))
		  
/*
 * For symlink we can modify:
 *    the parent directory inode: inode size
 *    the new inode: inode size
 *    the inode btree entry: 1 block
 *    the directory btree: max depth * block size
 *    the directory inode\'s bmap btree: max depth * block size
 *    the blocks for the symlink: 1 KB
 * Or in the first xact we allocate some inodes giving:
 *    the agi and agf of the ag getting the new inodes: 2 * sectorsize
 *    the inode blocks allocated: XFS_IALLOC_BLOCKS * blocksize
 *    the inode btree: max depth * blocksize
 *    the allocation btrees: 2 * max depth * block size
 */
#define	XFS_SYMLINK_LOG_RES(mp)		\
	(MAX( \
	 ((mp)->m_sb.sb_inodesize + \
	  (mp)->m_sb.sb_inodesize + \
	  XFS_FSB_TO_B(mp, 1) + \
	  XFS_FSB_TO_B((mp), XFS_DIR_NODE_MAXDEPTH) + \
	  XFS_FSB_TO_B((mp), XFS_BM_MAXLEVELS(mp)) + \
	  1024 + \
	  (128 * (5 + XFS_DIR_NODE_MAXDEPTH + XFS_BM_MAXLEVELS(mp)))), \
	 (2 * (mp)->m_sb.sb_sectsize + \
	  XFS_FSB_TO_B((mp), XFS_IALLOC_BLOCKS((mp))) + \
	  XFS_FSB_TO_B((mp), XFS_IN_MAXLEVELS(mp)) + \
	  (2 * XFS_FSB_TO_B((mp), XFS_AG_MAXLEVELS(mp)))) + \
	  (128 * (2 + XFS_IALLOC_BLOCKS(mp) + XFS_IN_MAXLEVELS(mp) + \
	   (2 * XFS_AG_MAXLEVELS(mp))))))

/*
 * For create we can modify:
 *    the parent directory inode: inode size
 *    the new inode: inode size
 *    the inode btree entry: block size
 *    the directory btree: max depth * block size
 *    the directory inode\'s bmap btree: max depth * block size
 * Or in the first xact we allocate some inodes giving:
 *    the agi and agf of the ag getting the new inodes: 2 * sectorsize
 *    the inode blocks allocated: XFS_IALLOC_BLOCKS * blocksize
 *    the inode btree: max depth * blocksize
 *    the allocation btrees: 2 * max depth * block size
 */
#define	XFS_CREATE_LOG_RES(mp)		\
	(MAX( \
	 ((mp)->m_sb.sb_inodesize + \
	  (mp)->m_sb.sb_inodesize + \
	  XFS_FSB_TO_B(mp, 1) + \
	  XFS_FSB_TO_B((mp), XFS_DIR_NODE_MAXDEPTH) + \
	  XFS_FSB_TO_B((mp), XFS_BM_MAXLEVELS(mp)) + \
	  (128 * (3 + XFS_DIR_NODE_MAXDEPTH + XFS_BM_MAXLEVELS(mp)))), \
	 (2 * (mp)->m_sb.sb_sectsize + \
	  XFS_FSB_TO_B((mp), XFS_IALLOC_BLOCKS((mp))) + \
	  XFS_FSB_TO_B((mp), XFS_IN_MAXLEVELS(mp)) + \
	  (2 * XFS_FSB_TO_B((mp), XFS_AG_MAXLEVELS(mp)))) + \
	  (128 * (2 + XFS_IALLOC_BLOCKS(mp) + XFS_IN_MAXLEVELS(mp) + \
	   (2 * XFS_AG_MAXLEVELS(mp))))))

/*
 * Making a new directory is the same as creating a new file.
 */
#define	XFS_MKDIR_LOG_RES(mp)		XFS_CREATE_LOG_RES(mp)

/*
 * In freeing an inode we can modify:
 *    the inode being freed: inode size
 *    the super block free inode counter: sector size
 *    the agi hash list and counters: sector size
 *    the inode btree entry: block size
 *    the on disk inode before ours in the agi hash list: block size
 */
#define	XFS_IFREE_LOG_RES(mp) \
	((mp)->m_sb.sb_inodesize + \
	 (mp)->m_sb.sb_sectsize + \
	 (mp)->m_sb.sb_sectsize + \
	 XFS_FSB_TO_B((mp), 1) + \
	 XFS_FSB_TO_B((mp), 1) + \
	 (128 * 5))

/*
 * When only changeing the inode we log the inode and we add a
 * bit of slop for the transaction stuff.
 */
#define	XFS_ICHANGE_LOG_RES(mp)		((mp)->m_sb.sb_inodesize + 512)     


/*
 * Growing the data section of the filesystem.
 *	superblock
 *	agi and agf
 *	allocation btrees
 */
#define	XFS_GROWDATA_LOG_RES(mp) \
	((mp)->m_sb.sb_sectsize * 3 + \
	 (2 * XFS_FSB_TO_B((mp), XFS_AG_MAXLEVELS(mp))) + \
	 (128 * (3 + (2 * XFS_AG_MAXLEVELS(mp)))))

/*
 * Logging the inode modification timestamp on a synchronous write.
 *	inode
 */
#define	XFS_SWRITE_LOG_RES(mp) \
     	((mp)->m_sb.sb_inodesize + 128)

/*
 * Various log count values.
 */
#define	XFS_DEFAULT_LOG_COUNT		1
#define	XFS_DEFAULT_PERM_LOG_COUNT	2     
#define	XFS_ITRUNCATE_LOG_COUNT		2
#define	XFS_CREATE_LOG_COUNT		2
#define	XFS_MKDIR_LOG_COUNT		3
#define	XFS_SYMLINK_LOG_COUNT		3
#define	XFS_REMOVE_LOG_COUNT		2
#define	XFS_LINK_LOG_COUNT		2
#define	XFS_RENAME_LOG_COUNT		2
#define	XFS_WRITE_LOG_COUNT		2     
     
struct xfs_inode;
struct xfs_mount;
struct xfs_efi_log_item;
struct xfs_efd_log_item;

/*
 * xFS transaction mechanism exported interfaces that are
 * actually macros.
 */
#define	xfs_trans_get_log_res(tp)	((tp)->t_log_res)
#define	xfs_trans_get_log_count(tp)	((tp)->t_log_count)
#define	xfs_trans_get_block_res(tp)	((tp)->t_blk_res)
#define	xfs_trans_set_sync(tp)		((tp)->t_flags |= XFS_TRANS_SYNC)

#ifdef DEBUG
#define	xfs_trans_agblocks_delta(tp, d)	((tp)->t_ag_freeblks_delta += (long)d)
#define	xfs_trans_agflist_delta(tp, d)	((tp)->t_ag_flist_delta += (long)d)
#define	xfs_trans_agbtree_delta(tp, d)	((tp)->t_ag_btree_delta += (long)d)
#else
#define	xfs_trans_agblocks_delta(tp, d)
#define	xfs_trans_agflist_delta(tp, d)
#define	xfs_trans_agbtree_delta(tp, d)
#endif

/*
 * xFS transaction mechanism exported interfaces.
 */
xfs_trans_t	*xfs_trans_alloc(struct xfs_mount *, uint);
xfs_trans_t	*xfs_trans_dup(xfs_trans_t *);
int		xfs_trans_reserve(xfs_trans_t *, uint, uint, uint,
				  uint, uint);
void		xfs_trans_callback(xfs_trans_t *,
				   void(*)(xfs_trans_t*, void*), void *);
void		xfs_trans_mod_sb(xfs_trans_t *, uint, long);
buf_t		*xfs_trans_get_buf(xfs_trans_t *, dev_t, daddr_t, int, uint);
buf_t		*xfs_trans_getsb(xfs_trans_t *, int);
buf_t		*xfs_trans_read_buf(xfs_trans_t *, dev_t, daddr_t, int, uint);
void		xfs_trans_brelse(xfs_trans_t *, buf_t *);
void		xfs_trans_bjoin(xfs_trans_t *, buf_t *);
void		xfs_trans_bhold(xfs_trans_t *, buf_t *);
void		xfs_trans_bhold_until_committed(xfs_trans_t *, buf_t *);
void		xfs_trans_binval(xfs_trans_t *, buf_t *);
void		xfs_trans_inode_buf(xfs_trans_t *, buf_t *);
void		xfs_trans_inode_alloc_buf(xfs_trans_t *, buf_t *);
struct xfs_inode	*xfs_trans_iget(struct xfs_mount *, xfs_trans_t *,
					xfs_ino_t , uint);
void		xfs_trans_iput(xfs_trans_t *, struct xfs_inode *, uint);
void		xfs_trans_ijoin(xfs_trans_t *, struct xfs_inode *,uint);
void		xfs_trans_ihold(xfs_trans_t *, struct xfs_inode *);
void		xfs_trans_log_buf(xfs_trans_t *, buf_t *, uint, uint);
void		xfs_trans_log_inode(xfs_trans_t *, struct xfs_inode *, uint);
struct xfs_efi_log_item	*xfs_trans_get_efi(xfs_trans_t *, uint);
void		xfs_trans_log_efi_extent(xfs_trans_t *,
					 struct xfs_efi_log_item *,
					 xfs_fsblock_t,
					 xfs_extlen_t);
struct xfs_efd_log_item	*xfs_trans_get_efd(xfs_trans_t *,
				  struct xfs_efi_log_item *,
				  uint);
void		xfs_trans_log_efd_extent(xfs_trans_t *,
					 struct xfs_efd_log_item *,
					 xfs_fsblock_t,
					 xfs_extlen_t);
void		xfs_trans_log_iui(xfs_trans_t *, struct xfs_inode *);
void		xfs_trans_log_iui_done(xfs_trans_t *, struct xfs_inode *);
xfs_trans_id_t	xfs_trans_id(xfs_trans_t *);
void		xfs_trans_commit(xfs_trans_t *, uint flags);
void		xfs_trans_commit_async(struct xfs_mount *);
void		xfs_trans_cancel(xfs_trans_t *, int);
void		xfs_trans_ail_init(struct xfs_mount *);
xfs_lsn_t	xfs_trans_push_ail(struct xfs_mount *, xfs_lsn_t);
xfs_lsn_t	xfs_trans_tail_ail(struct xfs_mount *);
void		xfs_trans_unlocked_item(struct xfs_mount *,
					xfs_log_item_t *);

/*
 * Not necessarily exported, but used outside a single file.
 */
int		xfs_trans_lsn_danger(struct xfs_mount *, xfs_lsn_t);

#endif	/* _XFS_TRANS_H */
