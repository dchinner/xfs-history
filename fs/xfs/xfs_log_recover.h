#ifndef	_XFS_LOG_RECOVER_H
#define _XFS_LOG_RECOVER_H

/*
 * Macros, structures, prototypes for internal log manager use.
 */
typedef struct xlog_recover_item {
	struct xlog_recover_item *ri_next;
	struct xlog_recover_item *ri_prev;
	int			 ri_type;
	int			 ri_cnt;
	int			 ri_total;	/* total regions */
	void *			 ri_desc;
	int			 ri_desc_len;
	void *			 ri_buf1;
	int			 ri_buf1_len;
	void *			 ri_buf2;
	int			 ri_buf2_len;
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


#define XLOG_RHASH_BITS  4
#define XLOG_RHASH_SIZE	16
#define XLOG_RHASH_SHIFT 2
#define XLOG_RHASH(tid)	\
	((((uint)tid)>>XLOG_RHASH_SHIFT) & (XLOG_RHASH_SIZE-1))

#endif /* _XFS_LOG_RECOVER_H */
