#ifndef	_XFS_LOG_PRIV_H
#define _XFS_LOG_PRIV_H
/*
 * Macros, structures, prototypes for internal log manager use.
 */



#define LOG_NUM_ICLOGS		2
#define LOG_CALLBACK_SIZE	10
#define LOG_HEADER_MAGIC_NUM	0xBADbabe
#define LOG_RECORD_ISIZE	1024
#define LOG_RECORD_BSIZE	(4*(LOG_RECORD_ISIZE))	/* eventually 32k */
#define LOG_HEADER_SIZE		512
#define LOG_BBSHIFT		12
#define LOG_BBSIZE		(1<<LOG_BBSHIFT)	/* 4096 */
#define LOG_BBMASK		(LOG_BBSIZE-1)
#define XLOGBB_TO_BB(x)		(((x) << LOG_BBSHIFT) >> BBSHIFT)

#define NBLOCKS(size)		((size)+LOG_BBSIZE-1 >> LOG_BBSHIFT)
#if XXX
#define LOG_LOCK(x)		{while ((x) > 0) sleep(1); x++;}
#define LOG_UNLOCK(x)		(x--)
#else
#define LOG_LOCK(x)		{}
#define LOG_UNLOCK(x)		{}
#endif
#define ICLOG_LEFT(x)		((x)->ic_size - (x)->ic_offset)

#define ASSIGN_LSN(lsn,log)	{ ((uint *)&(lsn))[0] = (log)->l_cycle; \
				  ((uint *)&(lsn))[1] = (log)->l_currblock; }
#define log_panic(s)		{printf("%s\n", s); abort();}


/*
 * In core log state
 */
#define LOG_ACTIVE	1	/* Current IC log being written to */
#define LOG_WANT_SYNC	2	/* Want to sync this iclog; no more writes */
#define LOG_SYNCING	3	/* This IC log is syncing */
#define LOG_CALLBACK	4	/* Callback functions now */
#define LOG_DIRTY	5	/* Need to clean this IC log */
#define LOG_NOTUSED	6	/* This IC log not being used */

#define LOG_CH_SYNC	7


/*
 * Flags to log operation header
 */
#define LOG_COMMIT		0x1	/* Commit this transaction id */
#define LOG_CONTINUE		0x2	/* Continue this transaction id */
#define LOG_END			0x4	/* End of this transaction id */


typedef struct log_ticket {
	struct log_ticket *t_next;	/*			      4/8 b */
	xfs_tid_t	  t_tid;	/* Transaction identifier	8 b */
	uint		  t_reservation;/* Reservation in bytes;	4 b */
	char		  t_clientid;	/* Who does this belong to;	1 b */
	char		  reserved;	/* 32bit align;			1 b */
} log_ticket_t ;

typedef struct log_op_header {
	xfs_tid_t  oh_tid;	/* transaction id of operation	:  4 b */
	int	   oh_len;	/* bytes in data region		:  4 b */
	char	   oh_clientid;	/* who sent me this		:  1 b */
	char	   oh_flags;	/* 				:  1 b */
	ushort	   oh_res2;	/* 32 bit align			:  2 b */
} log_op_header_t;


typedef struct log_rec_header {
	uint	  h_cycle;	/* write cycle of log			:  4 */
	uint	  h_magicno;	/* log record (LR) identifier		:  4 */
	int	  h_version;	/* LR version				:  4 */
	xfs_lsn_t h_lsn;	/* lsn of this LR			:  8 */
	xfs_lsn_t h_sync_lsn;	/* lsn of last LR with buffers committed:  8 */
	int	  h_len;	/* len in bytes; should be 64-bit aligned: 4 */
	uint	  h_chksum;	/* may not be used; non-zero if used	:  4 */
	int	  h_prev_offset;/* offset in bytes to previous LR	:  4 */
	int	  h_num_logops;	/* number of log operations in this LR	:  4 */
	uint	  h_cycle_data[LOG_RECORD_BSIZE / BBSIZE];
	uint	  h_blocks_col[1];/* blocks which collide with LR magic #:4/8 */
} log_rec_header_t;



typedef struct log_callback {
	void (*cb_func)(void *);
	void  *cb_arg;
} log_callback_t;


typedef struct log_callback_list {
	struct log_callback_list *cb_next;
	log_callback_t		 cb_chunk[LOG_CALLBACK_SIZE];
} log_callback_list_t;



typedef struct log_in_core {
	union {
		log_rec_header_t hic_header;
		char		 hic_sector[LOG_HEADER_SIZE];
	} ic_h;
	char			ic_data[LOG_RECORD_BSIZE-LOG_HEADER_SIZE];
	struct log_in_core	*ic_next;
	buf_t	  		*ic_bp;
	int	  		ic_size;
	int	  		ic_offset;
	int	  		ic_refcnt;
	char	  		ic_state;
} log_in_core_t;


#define ic_header	ic_h.hic_header


/*
 * l_freelist	: the freelist of unused tickets.
 * l_iclog	: the current active IC write log.
 * l_iclog2	: the IC log being synced to disk or in inactive state.
 * l_dev	: device of log partition.
 */
typedef struct log {
	log_ticket_t	*l_freelist;	/* free list of tickets:	4b */
	log_in_core_t	*l_iclog;	/* head log queue:		4b */
	sema_t		l_flushsema;	/* iclog flushing semaphore	20b */
	lock_t		l_icloglock;	/* grab to change iclog state:	 b */
	dev_t		l_dev;		/* dev_t of log */
	int		l_logsize;	/* size in bytes of log */
	int		l_cycle;	/* Cycle number of log writes */
	int		l_currblock;	/* current logical block of log */
	int		l_logreserved;	/* log space reserved */
	xfs_lsn_t	l_sync_lsn;	/* lsn of last LR w/ buffers committed*/
} log_t;


log_ticket_t *log_maketicket(log_t *log, xfs_tid_t tid, int len, char clientid);
void	     log_alloc_tickets(log_t *log);
void	     log_putticket(log_t *log, log_ticket_t *ticket);
void	     log_relticket(log_ticket_t *ticket);
int	     log_recover(struct xfs_mount *mp, dev_t log_dev);

#endif	/* _XFS_LOG_PRIV_H */
