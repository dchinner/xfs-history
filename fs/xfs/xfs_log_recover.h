#ifndef	_XFS_LOG_RECOVER_H
#define _XFS_LOG_RECOVER_H

#ident	"$Revision: 1.2 $"

/*
 * Macros, structures, prototypes for internal log manager use.
 */

#define XLOG_RHASH_BITS  4
#define XLOG_RHASH_SIZE	16
#define XLOG_RHASH_SHIFT 2
#define XLOG_RHASH(tid)	\
	((((uint)tid)>>XLOG_RHASH_SHIFT) & (XLOG_RHASH_SIZE-1))

#define XLOG_MAX_REGIONS_IN_ITEM	(NBPP / XFS_BLI_CHUNK / 2 + 1)


/*
 * item headers are in ri_buf[0].  Additional buffers follow.
 */
typedef struct xlog_recover_item {
	struct xlog_recover_item *ri_next;
	struct xlog_recover_item *ri_prev;
	int			 ri_type;
	int			 ri_cnt;	/* count of regions found */
	int			 ri_total;	/* total regions */
	xfs_log_iovec_t		 ri_buf[XLOG_MAX_REGIONS_IN_ITEM];
} xlog_recover_item_t;

typedef struct xlog_recover {
	struct xlog_recover *r_next;
	xlog_tid_t	    r_tid;
	uint		    r_type;
	int		    r_items;		/* number of items */
	xfs_trans_id_t	    r_trans_tid;	/* internal transaction tid */
	uint		    r_state;		/* not needed */
	xlog_recover_item_t *r_transq;
} xlog_recover_t;


#endif /* _XFS_LOG_RECOVER_H */
