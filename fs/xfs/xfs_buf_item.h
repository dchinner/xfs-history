
#ifndef	_XFS_BUF_ITEM_H
#define	_XFS_BUF_ITEM_H


/*
 * This is the in core log item structure used to track information
 * needed to log buffers.  It tracks how many times the lock has been
 * locked, and which 128 byte chunks of the buffer are dirty.
 */
typedef struct xfs_buf_log_item {
	xfs_log_item_t	bli_item;	/* common item structure */
	buf_t		*bli_buf;	/* real buffer pointer */
	unsigned int	bli_flags;	/* misc flags */
	unsigned int	bli_recur;	/* lock recursion count */
	unsigned int	bli_map_size;	/* size of dirty bitmap in words */
	unsigned int	bli_dirty_map[1]; /* variable sized bitmap of dirty */
					/* 128 byte regions in the buffer */
					/* to be logged */
} xfs_buf_log_item_t;

/*
 * Defines for accessing members of the common log item struct.
 */
#define	bli_mountp	bli_item.li_mountp
#define	bli_lsn		bli_item.li_lsn
#define	bli_ops		bli_item.li_ops
#define bli_type	bli_item.li_type

/*
 * buf log item flags
 */
#define	XFS_BLI_HOLD	0x1
#define	XFS_BLI_DIRTY	0x2

/*
 * This is the structure used to lay out a buf log item in the
 * log.  The data map describes which 128 byte chunks of the buffer
 * have been logged.  The chunks start on the next 32 byte boundary
 * after the end of the data map.
 */
typedef struct xfs_buf_log_format {
	unsigned int	blf_type;	/* buf log item type indicator */
	unsigned int	blf_size;	/* size of this item */
	daddr_t		blf_blkno;	/* starting blkno of this buf */
	dev_t		blf_dev;	/* dev this buf is for */
	unsigned int	blf_map_size;	/* size of data bitmap in words */
	unsigned int	blf_data_map[1]; /* variable size bitmap of */
					/* regions of buffer in this item */
} xfs_buf_log_format_t;

#define	XFS_BLI_CHUNK		128
#define	XFS_BLI_SHIFT		7
#define	BIT_TO_WORD_SHIFT	5
#define	NBWORD			32

extern void	xfs_buf_item_init(buf_t *, struct xfs_mount *);
extern void	xfs_buf_item_relse(buf_t *);
extern void	xfs_buf_item_log(xfs_buf_log_item_t *, uint, uint);
extern uint	xfs_buf_item_dirty(xfs_buf_log_item_t *);
extern void	xfs_buf_iodone(buf_t *);

#endif	/* _XFS_BUF_ITEM_H */
