#ifndef	_XFS_LOG_PRIV_H
#define _XFS_LOG_PRIV_H
#ident	"$Revision: 1.25 $"

#include <sys/cmn_err.h>

/*
 * Macros, structures, prototypes for internal log manager use.
 */

#define XLOG_NUM_ICLOGS		8
#define XLOG_CALLBACK_SIZE	10
#define XLOG_HEADER_MAGIC_NUM	0xFEEDbabe	/* need to outlaw as cycle XXX*/
#define XLOG_RECORD_BSIZE	(32*1024)	/* eventually 32k */
#define XLOG_RECORD_BSHIFT	15		/* 4096 == 1 << 12 */
#define XLOG_BTOLRBB(b)		((b)+XLOG_RECORD_BSIZE-1 >> XLOG_RECORD_BSHIFT)

#define XLOG_HEADER_SIZE	512
#if 0
#define XLOG_BBSHIFT		12
#define XLOG_BBSIZE		(1<<XLOG_BBSHIFT)	/* 4096 */
#define XLOG_BBMASK		(XLOG_BBSIZE-1)
#define XLOGBB_TO_BB(x)		(((x) << XLOG_BBSHIFT) >> BBSHIFT)

#define XLOG_NBLKS(size)	((size)+XLOG_BBSIZE-1 >> XLOG_BBSHIFT)
#endif /* 0 */

#define ASSIGN_LSN(lsn,log)	{ ((uint *)&(lsn))[0] = (log)->l_curr_cycle; \
				  ((uint *)&(lsn))[1] = (log)->l_curr_block; }
#define CYCLE_LSN(lsn)		(((uint *)&(lsn))[0])
#define BLOCK_LSN(lsn)		(((uint *)&(lsn))[1])
#define XLOG_SET(f,b)		(((f) & (b)) == (b))
#define GET_CYCLE(ptr)	(*(uint *)(ptr) == XLOG_HEADER_MAGIC_NUM ? *((uint *)(ptr)+1) : *(uint *)(ptr))
#define XLOG_GRANT_SUB_SPACE(log, bytes, type)				\
    {									\
	if (type == 'w') {						\
		(log)->l_grant_write_bytes -= (bytes);			\
		if ((log)->l_grant_write_bytes < 0) {			\
			(log)->l_grant_write_bytes += (log)->l_logsize;	\
			(log)->l_grant_write_cycle--;			\
		}							\
	} else {							\
		(log)->l_grant_reserve_bytes -= (bytes);		\
		if ((log)->l_grant_reserve_bytes < 0) {			\
			(log)->l_grant_reserve_bytes += (log)->l_logsize;\
			(log)->l_grant_reserve_cycle--;			\
		}							\
	 }								\
    }
#define XLOG_GRANT_ADD_SPACE(l, bytes, type)				\
    {									\
	if (type == 'w') {						\
		(log)->l_grant_write_bytes += (bytes);			\
		if ((log)->l_grant_write_bytes > (log)->l_logsize) {	\
			(log)->l_grant_write_bytes -= (log)->l_logsize;	\
			(log)->l_grant_write_cycle++;			\
		}							\
	} else {							\
		(log)->l_grant_reserve_bytes += (bytes);		\
		if ((log)->l_grant_reserve_bytes > (log)->l_logsize) {	\
			(log)->l_grant_reserve_bytes -= (log)->l_logsize;\
			(log)->l_grant_reserve_cycle++;			\
		}							\
	 }								\
    }
#define XLOG_INS_TICKETQ(q, tic)			\
    {							\
	if (q) {					\
		(tic)->t_next	    = (q);		\
		(tic)->t_prev	    = (q)->t_prev;	\
		(q)->t_prev->t_next = (tic);		\
		(q)->t_prev	    = (tic);		\
	} else {					\
		(tic)->t_prev = (tic)->t_next = (tic);	\
		(q) = (tic);				\
	}						\
	(tic)->t_flags |= XLOG_TIC_IN_Q;		\
    }
#define XLOG_DEL_TICKETQ(q, tic)			\
    {							\
	if ((tic) == (tic)->t_next) {			\
		(q) = NULL;				\
	} else {					\
		(q) = (tic)->t_next;			\
		(tic)->t_next->t_prev = (tic)->t_prev;	\
		(tic)->t_prev->t_next = (tic)->t_next;	\
	}						\
	(tic)->t_next = (tic)->t_prev = NULL;		\
	(tic)->t_flags &= ~XLOG_TIC_IN_Q;		\
    }

#define BLK_AVG(blk1, blk2)	((blk1+blk2) >> 1)

#define GRANT_LOCK(log)		splockspl((log)->l_grant_lock, splhi)
#define GRANT_UNLOCK(log, s)	spunlockspl((log)->l_grant_lock, s)
#define LOG_LOCK(log)		splockspl((log)->l_icloglock, splhi)
#define LOG_UNLOCK(log, s)	spunlockspl((log)->l_icloglock, s)

#ifdef _KERNEL
#define xlog_panic(s)		{cmn_err(CE_PANIC, s); }
#define xlog_exit(s)		{cmn_err(CE_PANIC, s); }
#define xlog_warning(s)		{cmn_err(CE_WARN, s); }
#else
#define xlog_panic(s)		{printf("%s\n", s); abort();}
#define xlog_exit(s)		{printf("%s\n", s); exit(1);}
#define xlog_warning(s)		{printf("%s\n", s); }
#endif


/*
 * In core log state
 */
#define XLOG_STATE_ACTIVE    0x01 /* Current IC log being written to */
#define XLOG_STATE_WANT_SYNC 0x02 /* Want to sync this iclog; no more writes */
#define XLOG_STATE_SYNCING   0x04 /* This IC log is syncing */
#define XLOG_STATE_DONE_SYNC 0x08 /* Done syncing to disk */
#define XLOG_STATE_CALLBACK  0x10 /* Callback functions now */
#define XLOG_STATE_DIRTY     0x20 /* Dirty IC log, not ready for ACTIVE status*/
#define XLOG_STATE_ALL	     0x3F /* All possible valid flags */
#define XLOG_STATE_NOTUSED   0x40 /* This IC log not being used */

/*
 * Flags to log operation header
 *
 * The first write of a new transaction will be preceded with a start
 * record, XLOG_START_TRANS.  Once a transaction is committed, a commit
 * record is written, XLOG_COMMIT_TRANS.  If a single region can not fit into
 * the remainder of the current active in-core log, it is split up into
 * multiple regions.  Each partial region will be marked with a
 * XLOG_CONTINUE_TRANS until the last one, which gets marked with XLOG_END_TRANS.
 *
 */
#define XLOG_START_TRANS	0x01	/* Start a new transaction */
#define XLOG_COMMIT_TRANS	0x02	/* Commit this transaction */
#define XLOG_CONTINUE_TRANS	0x04	/* Cont this trans into new region */
#define XLOG_WAS_CONT_TRANS	0x08	/* Cont this trans into new region */
#define XLOG_END_TRANS		0x10	/* End a continued transaction */
#define XLOG_UNMOUNT_TRANS	0x20	/* Unmount a filesystem transaction */
#define XLOG_SKIP_TRANS		(XLOG_COMMIT_TRANS | XLOG_CONTINUE_TRANS | \
				 XLOG_WAS_CONT_TRANS | XLOG_END_TRANS | \
				 XLOG_UNMOUNT_TRANS)

/*
 * Flags to log ticket
 */
#define XLOG_TIC_INITED		0x1	/* has been initialized */
#define XLOG_TIC_PERM_RESERV	0x2	/* permanent reservation */
#define XLOG_TIC_IN_Q		0x4

#define XLOG_UNMOUNT_TYPE	0x556e	/* Un for Unmount */

typedef void * xlog_tid_t;

typedef struct xlog_ticket {
	sema_t		   t_sema;	 /* sleep on this semaphore	 :20 */
	struct xlog_ticket *t_next;	 /*			         : 4 */
	struct xlog_ticket *t_prev;	 /*				 : 4 */
	xlog_tid_t	   t_tid;	 /* transaction identifier	 : 4 */
	int		   t_curr_res;	 /* current reservation in bytes : 4 */
	int		   t_unit_res;	 /* unit reservation in bytes    : 4 */
	char		   t_ocnt;	 /* original count		 : 1 */
	char		   t_cnt;	 /* current count		 : 1 */
	char		   t_clientid;	 /* who does this belong to;	 : 1 */
	char		   t_flags;	 /* properties of reservation	 : 1 */
} xlog_ticket_t;


typedef struct xlog_op_header {
	xlog_tid_t oh_tid;	/* transaction id of operation	:  4 b */
	int	   oh_len;	/* bytes in data region		:  2 b */
	char	   oh_clientid;	/* who sent me this		:  1 b */
	char	   oh_flags;	/* 				:  1 b */
	ushort	   oh_res2;	/* 32 bit align			:  2 b */
} xlog_op_header_t;


typedef struct xlog_rec_header {
	uint	  h_magicno;	/* log record (LR) identifier		:  4 */
	uint	  h_cycle;	/* write cycle of log			:  4 */
	int	  h_version;	/* LR version				:  4 */
	int	  h_len;	/* len in bytes; should be 64-bit aligned: 4 */
	xfs_lsn_t h_lsn;	/* lsn of this LR			:  8 */
	xfs_lsn_t h_tail_lsn;	/* lsn of 1st LR w/ buffers not committed: 8 */
	uint	  h_chksum;	/* may not be used; non-zero if used	:  4 */
	int	  h_prev_block; /* block number to previous LR		:  4 */
	int	  h_num_logops;	/* number of log operations in this LR	:  4 */
	uint	  h_cycle_data[XLOG_RECORD_BSIZE / BBSIZE];
} xlog_rec_header_t;


/*
 * - A log record header is 512 bytes.  There is plenty of room to grow the
 *	xlog_rec_header_t into the reserved space.
 * - ic_data follows, so a write to disk can start at the beginning of
 *	the iclog.
 * - ic_forcesema is used to implement synchronous forcing of the iclog to disk.
 * - ic_next is the pointer to the next iclog in the ring.
 * - ic_bp is a pointer to the buffer used to write this incore log to disk.
 * - ic_log is a pointer back to the global log structure.
 * - ic_callback is a linked list of callback function/argument pairs to be
 *	called after an iclog finishes writing.
 * - ic_size is the full size of the header plus data.
 * - ic_offset is the current number of bytes written to in this iclog.
 * - ic_refcnt is bumped when someone is writing to the log.
 * - ic_state is the state of the iclog.
 */
typedef struct xlog_in_core {
	union {
		xlog_rec_header_t hic_header;
		char		  hic_sector[XLOG_HEADER_SIZE];
	} ic_h;
	char			ic_data[XLOG_RECORD_BSIZE-XLOG_HEADER_SIZE];
	sema_t			ic_forcesema;
	struct xlog_in_core	*ic_next;
	struct xlog_in_core	*ic_prev;
	buf_t	  		*ic_bp;
	struct log		*ic_log;
	xfs_log_callback_t	*ic_callback;
	xfs_log_callback_t	**ic_callback_tail;
	ktrace_t		*ic_trace;
	int	  		ic_size;
	int	  		ic_offset;
	int	  		ic_refcnt;
	int			ic_roundoff;
	int			ic_bwritecnt;
	uchar_t	  		ic_state;
} xlog_in_core_t;

#define ic_header	ic_h.hic_header


/*
 * The reservation head lsn is not made up of a cycle number and block number.
 * Instead, it uses a cycle number and byte number.  Logs don't expect to
 * overflow 31 bits worth of byte offset, so using a byte number will mean
 * that round off problems won't occur when releasing partial reservations.
 */
struct xlog_recover_item;
typedef struct log {
    xlog_ticket_t	*l_freelist;    /* free list of tickets */
    xlog_ticket_t	*l_tail;        /* free list of tickets */
    xlog_in_core_t	*l_iclog;       /* head log queue	*/
    sema_t		l_flushsema;    /* iclog flushing semaphore */
    lock_t		l_icloglock;    /* grab to change iclog state */
    xfs_lsn_t		l_tail_lsn;     /* lsn of 1st LR w/ unflush buffers */
    xfs_lsn_t		l_last_sync_lsn;/* lsn of last LR on disk */
    xfs_mount_t		*l_mp;	        /* mount point */
    buf_t		*l_xbuf;        /* extra buffer for log wrapping */
    dev_t		l_dev;	        /* dev_t of log */
    int			l_logBBstart;   /* start block of log */
    int			l_logsize;      /* size of log in bytes */
    int			l_logBBsize;    /* size of log in 512 byte chunks */
    int			l_roundoff;	/* round off error of all iclogs */
    int			l_curr_cycle;   /* Cycle number of log writes */
    int			l_prev_cycle;   /* Cycle # b4 last block increment */
    int			l_curr_block;   /* current logical block of log */
    int			l_prev_block;   /* previous logical block of log */
    struct xlog_recover_item *l_recover_extq;       /* recover q for extents */
    xlog_in_core_t	*l_iclog_bak[XLOG_NUM_ICLOGS];/* for debuggin */
    int			l_iclog_size;   /* size of log in bytes; repeat */

    lock_t		l_grant_lock;		/* protects below fields */
    xlog_ticket_t	*l_reserve_headq;	/* */
    xlog_ticket_t	*l_write_headq;		/* */
    int			l_grant_reserve_cycle;	/* */
    int			l_grant_reserve_bytes;	/* */
    int			l_grant_write_cycle;	/* */
    int			l_grant_write_bytes;	/* */

    ktrace_t		*l_trace;
    ktrace_t		*l_grant_trace;
} xlog_t;


/* common routines */
extern daddr_t	xlog_find_head(xlog_t *log);
extern daddr_t  xlog_print_find_oldest(xlog_t *log);
extern int	xlog_recover(xlog_t *log);
extern void	xlog_pack_data(xlog_t *log, xlog_in_core_t *iclog);
extern buf_t *	xlog_get_bp(int);
extern void	xlog_put_bp(buf_t *);
extern void	xlog_bread(xlog_t *, daddr_t blkno, int bblks, buf_t *bp);

#define XLOG_TRACE_GRAB_FLUSH  1
#define XLOG_TRACE_REL_FLUSH   2
#define XLOG_TRACE_SLEEP_FLUSH 3
#define XLOG_TRACE_WAKE_FLUSH  4

#endif	/* _XFS_LOG_PRIV_H */
