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

/*
 * Flags to log operation header
 *
 * The first write of a new transaction will be preceded with a start
 * record, LOG_START_TRANS.  Once a transaction is committed, a commit
 * record is written, LOG_COMMIT_TRANS.  If a single region can not fit into
 * the remainder of the current active in-core log, it is split up into
 * multiple regions.  Each partial region will be marked with a
 * LOG_CONTINUE_TRANS until the last one, which gets marked with LOG_END_TRANS.
 *
 */
#define LOG_START_TRANS		0x1	/* Start a new transaction */
#define LOG_COMMIT_TRANS	0x2	/* Commit this transaction */
#define LOG_CONTINUE_TRANS	0x4	/* Cont this trans into new region */
#define LOG_END_TRANS		0x8	/* End a continued transaction */

/*
 * Flags to log ticket
 */
#define LOG_TIC_INITED		0x1	/* has been initialized */
#define LOG_TIC_PERM_RESERV	0x2	/* permanent reservation */

typedef void * log_tid_t;

typedef struct log_ticket {
	struct log_ticket *t_next;	/*			      4/8 b */
	log_tid_t	  t_tid;	/* Transaction identifier	8 b */
	uint		  t_reservation;/* Reservation in bytes;	4 b */
	char		  t_clientid;	/* Who does this belong to;	1 b */
	char		  t_flags;	/* 				1 b */
} log_ticket_t;


typedef struct log_op_header {
	log_tid_t  oh_tid;	/* transaction id of operation	:  4 b */
	int	   oh_len;	/* bytes in data region		:  4 b */
	char	   oh_clientid;	/* who sent me this		:  1 b */
	char	   oh_flags;	/* 				:  1 b */
	ushort	   oh_res2;	/* 32 bit align			:  2 b */
} log_op_header_t;


typedef struct log_rec_header {
	uint	  h_magicno;	/* log record (LR) identifier		:  4 */
	uint	  h_cycle;	/* write cycle of log			:  4 */
	int	  h_version;	/* LR version				:  4 */
	xfs_lsn_t h_lsn;	/* lsn of this LR			:  8 */
	xfs_lsn_t h_sync_lsn;	/* lsn of last LR with buffers committed:  8 */
	int	  h_len;	/* len in bytes; should be 64-bit aligned: 4 */
	uint	  h_chksum;	/* may not be used; non-zero if used	:  4 */
	int	  h_prev_offset;/* offset in bytes to previous LR	:  4 */
	int	  h_num_logops;	/* number of log operations in this LR	:  4 */
	uint	  h_cycle_data[LOG_RECORD_BSIZE / BBSIZE];
} log_rec_header_t;


typedef struct log_in_core {
	union {
		log_rec_header_t hic_header;
		char		 hic_sector[LOG_HEADER_SIZE];
	} ic_h;
	char			ic_data[LOG_RECORD_BSIZE-LOG_HEADER_SIZE];
	struct log_in_core	*ic_next;
	buf_t	  		*ic_bp;
	struct log		*ic_log;	/* back ptr to log */
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



#endif	/* _XFS_LOG_PRIV_H */
