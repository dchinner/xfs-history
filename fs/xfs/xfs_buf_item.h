
#ifndef	_XFS_BUF_ITEM_H
#define	_XFS_BUF_ITEM_H

/*
 * This is the structure used to lay out a buf log item in the
 * log.  The data map describes which 128 byte chunks of the buffer
 * have been logged.
 */
typedef struct xfs_buf_log_format {
	unsigned short	blf_type;	/* buf log item type indicator */
	unsigned short	blf_size;	/* size of this item */
	daddr_t		blf_blkno;	/* starting blkno of this buf */
	uint		blf_len;	/* number of blocks in this buf */
	unsigned int	blf_map_size;	/* size of data bitmap in words */
	unsigned int	blf_data_map[1];/* variable size bitmap of */
					/*   regions of buffer in this item */
} xfs_buf_log_format_t;

#define	XFS_BLI_CHUNK		128
#define	XFS_BLI_SHIFT		7
#define	BIT_TO_WORD_SHIFT	5
#define	NBWORD			32

/*
 * This is the in core log item structure used to track information
 * needed to log buffers.  It tracks how many times the lock has been
 * locked, and which 128 byte chunks of the buffer are dirty.
 */
typedef struct xfs_buf_log_item {
	xfs_log_item_t		bli_item;	/* common item structure */
	buf_t			*bli_buf;	/* real buffer pointer */
	unsigned int		bli_flags;	/* misc flags */
	unsigned int		bli_recur;	/* lock recursion count */
#ifdef XFSDEBUG
	char			*bli_orig;	/* original buffer copy */
	char			*bli_logged;	/* bytes to be logged */
#endif
	xfs_buf_log_format_t	bli_format;	/* in-log header */
} xfs_buf_log_item_t;

/*
 * buf log item flags
 */
#define	XFS_BLI_HOLD	0x1
#define	XFS_BLI_DIRTY	0x2

void	xfs_buf_item_init(buf_t *, struct xfs_mount *);
void	xfs_buf_item_relse(buf_t *);
void	xfs_buf_item_log(xfs_buf_log_item_t *, uint, uint);
uint	xfs_buf_item_dirty(xfs_buf_log_item_t *);
int	xfs_buf_item_bits(uint *, uint, uint);
int	xfs_buf_item_next_bit(uint *, uint, uint);
void	xfs_buf_attach_iodone(buf_t *, void(*)(buf_t*, xfs_log_item_t *),
			      xfs_log_item_t *);
void	xfs_buf_iodone_callbacks(buf_t *);
void	xfs_buf_iodone(buf_t *, xfs_buf_log_item_t *);

#endif	/* _XFS_BUF_ITEM_H */
