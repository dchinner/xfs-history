#ifndef	_XFS_LOG_PRIV_H
#define _XFS_LOG_PRIV_H
/*
 * Macros, structures, prototypes for internal log manager use.
 */



#define LOG_TICKET_TABLE_SIZE	16		/* eventuall 512 */
#define LOG_TICKET_MASK		0x1ff
#define LOG_HEADER_MAGIC_NUM	0xBADbabe
#define LOG_RECORD_SIZE		8*1024		/* eventually 32k */
#define LOG_HEADER_SIZE		512
#define LOG_BBSHIFT		12
#define LOG_BBSIZE		(1<<LOG_BBSHIFT)	/* 4096 */
#define LOG_BBMASK		(LOG_BBSIZE-1)
#define LOGBB_TO_BB(x)		(((x) << LOG_BBSHIFT) >> BBSHIFT)

#define NBLOCKS(size)		((size)+LOG_BBSIZE-1 >> LOG_BBSHIFT)
#if XXX
#define LOG_LOCK(x)		{while ((x) > 0) sleep(1); x++;}
#define LOG_UNLOCK(x)		(x--)
#else
#define LOG_LOCK(x)		{}
#define LOG_UNLOCK(x)		{}
#endif
#define ICLOG_LEFT(x)		((x)->ic_size - (x)->ic_offset)

#define ASSIGN_LSN(lsn,x1,x2)	{ ((uint *)&(lsn))[0] = (x1); \
				  ((uint *)&(lsn))[1] = (x2); }
#define log_panic(s)		{printf("%s\n", s); abort();}


/*
 * In core log state
 */
#define LOG_CURRENT		1	/* Current IC log being written to */
#define LOG_SYNCING		2	/* This IC log is syncing */
#define LOG_NOTUSED		3	/* This IC log not being used */
#define LOG_DIRTY		4	/* Need to clean this IC log */

/*
 * Flags to log operation header
 */
#define LOG_COMMIT		0x1	/* Commit this transaction id */


struct log_ticket {
    struct log_ticket	*t_next;	/* 4 or 8 b */
    xfs_tid_t		t_tid;		/* 8 b */
    uint		t_reservation;	/* Reservation in bytes;	4 b */
#ifdef TICKET_INT
    ushort		t_slot;		/* Slot in ticket table;	2 b */
#endif
    char		t_clientid;	/* Who does this belong to;	1 b */
    char		reserved;	/* 32bit align;			1 b */
};
typedef struct log_ticket log_ticket_t;


struct log_op_header {
    xfs_tid_t	oh_tid;		/* transaction id of operation	: 128 bits */
    int		oh_len;		/* bytes in data region		:  32 bits */
    char	oh_clientid;	/* who sent me this		:   8 bits */
    char	oh_flags;	/* 16 bit align			:   8 bits */
    ushort	oh_res2;	/* 32 bit align			:  16 bits */
};
typedef struct log_op_header log_op_header_t;

struct log_rec_header {
    uint	h_magicno;	/* log record (LR) identifier		:  32 */
    int		h_version;	/* LR version				:  32 */
    xfs_lsn_t	h_lsn;		/* lsn of this LR			: 128 */
    xfs_lsn_t	h_sync_lsn;	/* lsn of last LR with buffers committed: 128 */
    int		h_len;		/* len in bytes; should be 64-bit aligned: 32 */
    uint	h_chksum;	/* may not be used; non-zero if used	:  32 */
    int		h_prev_offset;	/* offset in bytes to previous LR	:  32 */
    int		h_num_logops;	/* number of log operations in this LR	:  32 */
    uint	*h_blocks_col;	/* blocks which collide with LR magic # :  32 */
};
typedef struct log_rec_header log_rec_header_t;


struct in_core_log {
    union {
	log_rec_header_t	sic_header;
	char			sic_sector[LOG_HEADER_SIZE];
    } ic_s;
    char	ic_data[LOG_RECORD_SIZE-LOG_HEADER_SIZE];
    xfs_lsn_t	ic_curr_lsn;
    int		ic_size;
    uint	ic_offset;
    char	ic_state;
    buf_t	*ic_bp;
};
typedef struct in_core_log in_core_log_t;

#define ic_header ic_s.sic_header


/*
 * l_hash	: the hash table used to store tickets.
 * l_freelist	: the freelist of unused tickets.
 * l_iclog	: the current active IC write log.
 * l_iclog2	: the IC log being synced to disk or in inactive state.
 * l_dev	: device of log partition.
 */
struct log {
#ifdef TICKET_INT
    sema_t		l_hash_lock;				/* 20b */
    log_ticket_t	*l_hash[LOG_TICKET_TABLE_SIZE];		/* 4b */
#endif
    log_ticket_t	*l_freelist;				/* 4b */
    in_core_log_t	*l_iclog;				/* 4b */
    in_core_log_t	*l_iclog2;				/* 4b */
    sema_t		l_iclog_lock;				/* 20b */
    dev_t		l_dev;		/* dev_t of log */
    int			l_logsize;	/* size in bytes of log */
    int			l_cycle;	/* Cycle number of log writes */
    int			l_currblock;	/* current logical block of log */
    int			l_logreserved;	/* log space reserved */
    xfs_lsn_t		h_sync_lsn;	/* lsn of last LR w/ buffers committed */
};
typedef struct log log_t;


#ifdef TICKET_INT
int log_maketicket(log_t *log, xfs_tid_t tid, int len, char clientid);
void log_alloc_tickets(log_t *log);
int  log_getticket(log_t *log, int slot, log_ticket_t **ticket);
void log_putticket(log_t *log, int slot);
void log_relticket(log_ticket_t *ticket);
int  log_recover(struct xfs_mount *mp, dev_t log_dev);
#else
log_ticket_t *log_maketicket(log_t *log, xfs_tid_t tid, int len, char clientid);
void	     log_alloc_tickets(log_t *log);
void	     log_putticket(log_t *log, log_ticket_t *ticket);
void	     log_relticket(log_ticket_t *ticket);
int	     log_recover(struct xfs_mount *mp, dev_t log_dev);
#endif

#endif	/* _XFS_LOG_PRIV_H */

