
#ifndef	_XFS_INODE_ITEM_H
#define	_XFS_INODE_ITEM_H

struct xfs_inode;

typedef struct xfs_inode_log_item {
	xfs_log_item_t		ili_item;
	struct xfs_inode	*ili_inode;
	xfs_lsn_t		ili_flush_lsn;
	unsigned short		ili_recur;
	unsigned short		ili_flags;
	unsigned short		ili_ref;
	unsigned int		ili_fields;
} xfs_inode_log_item_t;

#define	XFS_ILI_HOLD	0x1

/*
 * Flags for xfs_trans_log_inode flags field.
 */
#define	XFS_ILOG_CORE	0x001	/* log standard inode fields */
#define	XFS_ILOG_DATA	0x002	/* log iu_data */
#define	XFS_ILOG_EXT	0x004	/* log iu_extents */
#define	XFS_ILOG_BROOT	0x008	/* log i_broot */
#define	XFS_ILOG_DEV	0x010	/* log the dev field */

#define	XFS_ILOG_NONCORE	(XFS_ILOG_DATA | XFS_ILOG_EXT | \
				 XFS_ILOG_BROOT | XFS_ILOG_DEV)

/*
 * This is the structure used to lay out an inode log item in the
 * log.  The size of the inline data/extents/b-tree root to be logged
 * (if any) is indicated in the ifl_dsize field.
 */
typedef struct xfs_inode_log_format {
	unsigned int		ilf_type;	/* inode log item type */
	unsigned int		ilf_size;	/* size of this item */
	dev_t			ilf_dev;	/* dev inode resides on */
	xfs_agblock_t		ilf_blkno;	/* block on dev inode is in */
	int			ilf_index;	/* index into block of inode */
	uint			ilf_fields;	/* flags for fields logged */
	uint			ilf_dsize;	/* size of data/ext/root */
	union {
		dev_t		ilfu_rdev;	/* rdev value for dev inodes */
		uuid_t		ilfu_uuid;	/* mount point value */
	} ilf_u;
} xfs_inode_log_format_t;
	
extern void	xfs_inode_item_init(struct xfs_inode *, xfs_mount_t *);
extern void	xfs_iflush_done(buf_t *, xfs_inode_log_item_t *);

#endif	/* _XFS_INODE_ITEM_H */
