
#ifndef	_XFS_INODE_ITEM_H
#define	_XFS_INODE_ITEM_H



/*
 * Flags for xfs_trans_log_inode flags field.
 */
#define	XFS_ILOG_META	0x001	/* log standard inode fields */
#define	XFS_ILOG_DATA	0x002	/* log iu_data */
#define	XFS_ILOG_EXT	0x004	/* log iu_extents */
#define	XFS_ILOG_BROOT	0x008	/* log i_broot */




#endif	/* _XFS_INODE_ITEM_H */
