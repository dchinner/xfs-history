/*
 * High level interface routines for log manager
 */

#include <sys/types.h>

#include <sys/param.h>
#define _KERNEL
#include <sys/sysmacros.h>
#include <sys/buf.h>
#ifdef SIM
#undef _KERNEL
#include <bstring.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#endif

#include <sys/kmem.h>
#include <sys/sema.h>
#include <sys/uuid.h>
#include <sys/vnode.h>

#include "xfs_inum.h"
#include "xfs.h"
#include "xfs_types.h"
#include "xfs_sb.h"		/* depends on xfs_types.h & xfs_inum.h */
#include "xfs_trans.h"
#include "xfs_mount.h"		/* depends on xfs_trans.h & xfs_sb.h */
#include "xfs_log.h"		/* depends on xfs_mount.h */

#ifndef LOG_DEBUG
int
xfs_log_reserve(struct xfs_mount *mp, int reserve, int flags)
/* ARGSUSED */
{
        return (1);
}

#else


#include "xfs_log_priv.h"

/* Local function prototypes */
STATIC void log_alloc(xfs_mount_t *mp, dev_t log_dev);
STATIC void log_clean(log_t *log);
STATIC void log_commit_record(xfs_mount_t *mp,
#ifdef TICKET_INT
			      int slot
#else
			      log_ticket_t *ticket
#endif
			      );
STATIC void log_copy(in_core_log_t *, xfs_log_iovec_t region[], int i, int n,
		     int offset, xfs_lsn_t *lsn);
STATIC void log_sync(log_t *log, xfs_lsn_t *lsn, uint flags);
STATIC void log_unalloc(void);
STATIC int  log_write(xfs_mount_t *mp, xfs_log_iovec_t	region[], int nentries,
#ifdef TICKET_INT
		      int slot,
#else
		      xfs_log_ticket_t	tic,
#endif
		      int commit);


/*
 * NOTES:
 *
 *	1. currblock field gets updated at startup and after in-core logs
 *		are written out to the disk.
 *
 */

/*
 * name:	xfs_log_notify()
 * purpose: This function will take a log sequence number and check to
 *	see if that lsn has been flushed to disk.  If it has, then the
 *	callback function is called with the callback argument.
 */
void
xfs_log_notify(xfs_mount_t *mp,		/* mount of partition */
	       xfs_lsn_t   lsn,		/* lsn looking for */
	       void	   callback_func(void *),
	       void	   *callback_arg)
{
    log_t *log = mp->m_log;
    
    if (log->l_iclog->ic_state == LOG_CURRENT ||
	log->l_iclog->ic_state == LOG_SYNCING) {
    }
}	/* xfs_log_notify */


void
xfs_log_done(xfs_mount_t	*mp,
#ifdef TICKET_INT
	     int		slot)
#else
	     xfs_log_ticket_t	tic)
#endif
{
    log_t *log = mp->m_log;
    log_ticket_t *ticket = (xfs_log_ticket_t) tic;

    log_commit_record(mp, ticket);
    log_putticket(log, ticket);
}	/* xfs_log_done */


/*
 * name:	xfs_log_force()
 * purpose: Force the in-core log to disk.  If flags == XFS_LOG_SYNC,
 *	the force is done synchronously.
 */
int
xfs_log_force(xfs_mount_t *mp,
#ifdef TICKET_INT
	      int	  slot,
#else
	      xfs_log_ticket_t ticket,
#endif
	      uint	  flags)
{
    xfs_lsn_t lsn;
    log_t *log = mp->m_log;

    ASSIGN_LSN(lsn, log->l_cycle, log->l_currblock);

    if (flags & XFS_LOG_FORCE)
	log_sync(log, &lsn, flags);
    else if (flags & XFS_LOG_URGE)
	return -1;	/* not implemented yet */
    else
	return -1;	/* XFS_LOG_FORCE | XFS_LOG_URGE must be set */
	
}	/* xfs_log_force */


/*
 * name:	xfs_log_new_transaction()
 * purpose: Given an old transaction id for a <mp, slot> pair, replace
 *	it with a new transaction id.  Do not change the reservation.
 */
int
xfs_log_new_transaction(xfs_mount_t *mp,	/* mount point */
#ifdef TICKET_INT
			int	    slot,	/* ticket # */
#else
			xfs_log_ticket_t tic,
#endif
			xfs_tid_t   otid,	/* old tid */
			xfs_tid_t   ntid)	/* new tid */
{
    log_ticket_t *ticket = (log_ticket_t *)tic;
#ifdef TICKET_INT
    int		 error;

    if ((error = log_getticket(mp->m_log, slot, &ticket)) != 0) {
	log_panic("xfs_log_new_transaction: log_getticket");
    }
#endif
    if (ticket->t_tid != otid)
	return (-1);
    ticket->t_tid = ntid;
}


/*
 * Initialize log manager data.
 */
int
xfs_log_init()
{
}


/*
 *  1. Reserve an amount of on-disk log space and return a ticket corresponding
 *	to the reservation.
 *  2. Potentially, push buffers at tail of log to disk.
 */
int
xfs_log_reserve(xfs_mount_t	 *mp,
		xfs_tid_t	 tid,
		uint		 len,
#ifdef TICKET_INT
		int		 *slot,
#else
		xfs_log_ticket_t *x_ticket,
#endif
		char		 log_client,
		uint		 flags)
{
    log_t *log = mp->m_log;

    if (log_client != XFS_TRANSACTION_MANAGER)
	return -1;

    if (flags & XFS_LOG_SLEEP)
	return XFS_ENOTSUP;
    if (flags & XFS_LOG_PERM_RESERV)
	return XFS_ENOTSUP;
    
    /* Eventually force out buffers */
    if (log->l_logreserved + len > log->l_logsize)
	return XFS_ENOLOGSPACE;
    log->l_logreserved += len;
#ifdef TICKET_INT
    *slot =
#else
    *x_ticket = (xfs_log_ticket_t)
#endif
	log_maketicket(mp->m_log, tid, len, log_client);

    return 0;
}	/* log_reserve */


#ifdef SIM
#include <sys/stat.h>
int
log_findlogsize(dev_t log_dev)
{
    struct stat buf;

    if (fstat(bmajor(log_dev), &buf) == -1)
	return -1;

    return buf.st_size;
}
#endif /* SIM */


/*
 * Mount a log filesystem.
 *
 * mp	   -
 * log_dev - device number of on-disk log device
 * flags   -
 *
 */
int
xfs_log_mount(xfs_mount_t *mp, dev_t log_dev, uint flags)
{
    uint err;
    log_t *log;

    if ((flags & XFS_LOG_RECOVER) && log_recover(mp, log_dev) != 0) {
	return XFS_ERECOVER;
    }
    log_alloc(mp, log_dev);
    return 0;
}	/* log_mount */


int
xfs_log_unmount(xfs_mount_t *mp)
{
    log_unalloc();
}

int
xfs_log_write(xfs_mount_t *	mp,
	      xfs_log_iovec_t	reg[],
	      int		nentries,
#ifdef TICKET_INT
	      int		slot)
#else
	      xfs_log_ticket_t	tic)
#endif
{
#ifdef TICKET_INT
    log_write(mp, reg, nentries, slot, 0);
#else
    log_write(mp, reg, nentries, tic, 0);
#endif
}	/* xfs_log_write */


/*
 * ERRORS:
 *	Return error at any time if reservation is overrun.
 */
int
log_write(xfs_mount_t *		mp,
	  xfs_log_iovec_t	reg[],
	  int			nentries,
#ifdef TICKET_INT
	  int			slot,
#else
	  xfs_log_ticket_t	tic,
#endif
	  int			commit)
{
    int len, i, error, log_offset;
    log_ticket_t	*ticket;
    log_op_header_t	logop_head;
    in_core_log_t	*ic_log;
    xfs_lsn_t		lsn;
    log_t		*log = mp->m_log;

    for (len=0, i=0; i<nentries; i++) {
	len += reg[i].i_len;
    }

#ifdef TICKET_INT
    error = log_getticket(log, slot, &ticket);
#else
    ticket = (log_ticket_t *)tic;
#endif
    if (ticket->t_reservation < len) {
	return -1;
    } else {
	ticket->t_reservation -= len;
    }
#ifdef TICKET_INT
    log_relticket(ticket);
#endif

    log_clean(log);
    ic_log		   = log->l_iclog;
    logop_head.oh_tid	   = ticket->t_tid;
    logop_head.oh_clientid = ticket->t_clientid;
    logop_head.oh_len	   = 0;
    if (commit)
	logop_head.oh_flags = LOG_COMMIT;
    else
	logop_head.oh_flags = 0;
	

    /* Create log sequence number */
    ASSIGN_LSN(lsn, log->l_cycle, log->l_currblock);

    i = 0;
    while (i < nentries) {
	if ((len + sizeof(log_op_header_t)) < ICLOG_LEFT(ic_log)) {
	    if (ticket->t_reservation < sizeof(log_op_header_t)) {
		return -1;
	    } else {
		ticket->t_reservation -= sizeof(log_op_header_t);
	    }

	    log_offset = ic_log->ic_offset + sizeof(log_op_header_t);
	    ic_log->ic_offset += (len + sizeof(log_op_header_t));
	    logop_head.oh_len += len;
	    bcopy(&logop_head, ic_log->ic_data, sizeof(log_op_header_t));

	    log_copy(ic_log, reg, i, nentries, log_offset, &lsn);
	    log_sync(log, &lsn, XFS_LOG_SYNC);
	    return 0;
	} else {	/* can't fit all regions */
	    log_panic("help!");
	}
    }
}	/* log_write */


void
log_alloc(xfs_mount_t *mp, dev_t log_dev)
{
    register log_t	*log;
    log_rec_header_t	*head;

    log = mp->m_log = (void *)kmem_zalloc(sizeof(log_t), 0);
    log_alloc_tickets(log);

    log->l_dev = log_dev;
    if ((log->l_logsize = log_findlogsize(log_dev)) == -1)
	log_panic("log_findlogsize");
    log->l_logreserved = 0;
    log->l_currblock = 0;

    /* Assign 1st in-core log */
    log->l_iclog = (in_core_log_t *)kmem_zalloc(sizeof(in_core_log_t), 0);
    head = &log->l_iclog->ic_header;
    head->h_magicno = LOG_HEADER_MAGIC_NUM;
    head->h_version = 1;
    head->h_lsn = 0;
    log->l_iclog->ic_size = LOG_RECORD_SIZE-LOG_HEADER_SIZE;
    log->l_iclog->ic_state = LOG_CURRENT;
    log->l_iclog->ic_bp = getrbuf(0);

    /* Assign backup in-core log */
    log->l_iclog2 = (in_core_log_t *)kmem_zalloc(sizeof(in_core_log_t), 0);
    head = &log->l_iclog2->ic_header;
    head->h_magicno = LOG_HEADER_MAGIC_NUM;
    head->h_version = 1;
    log->l_iclog2->ic_size = LOG_RECORD_SIZE-LOG_HEADER_SIZE;
    log->l_iclog2->ic_state = LOG_NOTUSED;
    log->l_iclog2->ic_bp = getrbuf(0);
}	/* log_alloc */


void
log_unalloc(void)
{
}


void
log_clean(log_t *log)
{
    switch (log->l_iclog->ic_state) {
	case LOG_CURRENT: {
	    /* do nothing */
	    break;
	};
	case LOG_SYNCING: {
	    log_panic("log_clean: log_sync");
	    break;
	};
	case LOG_NOTUSED: {
	    log_panic("log_clean: log_notused");
	    break;
	};
	case LOG_DIRTY: {
	    bzero((void *)&log->l_iclog->ic_header, sizeof(log_rec_header_t));
	    break;
	};
	default: {
	    log_panic("log_clean: Illegal state");
	}
    };
}	/* log_clean */



void
log_commit_record(xfs_mount_t *mp,
#ifdef TICKET_INT
		  int slot)
#else
		  log_ticket_t *ticket)
#endif
{
    int			error;
    xfs_log_iovec_t	reg[1];
    int			nentries = 0;

#ifdef TICKET_INT
    error = log_write(mp, reg, nentries, slot, 1);
#else
    error = log_write(mp, reg, nentries, ticket, 1);
#endif
    if (error)
	log_panic("log_commit_record");
}	/* log_commit_record */


void
log_copy(in_core_log_t *iclog,
	 xfs_log_iovec_t reg[],
	 int i,
	 int n,
	 int offset,
	 xfs_lsn_t *lsn)
{
    char *ptr = &iclog->ic_data[offset];

    iclog->ic_header.h_num_logops++;
    for ( ; i < n; i++) {
	bcopy(reg[i].i_addr, ptr, reg[i].i_len);
	reg[i].i_lsn = *lsn;
	ptr += reg[i].i_len;
    }
}	/* log_copy */


/*
 * name:	log_iodone()
 * purpose: Function which is called when an io completes.  The log manager
 *	needs its own routine, in order to control what happens with the buffer
 *	after the write completes.
 */
void
log_iodone(buf_t *bp)
{
    if ((bp->b_flags & B_ASYNC) == 0)
	vsema(&bp->b_iodonesema);
}	/* log_iodone */


/*
 * name:	log_sync()
 * purpose: Flush out the in-core log to the on-disk log in a synchronous or
 *	asynchronous fashion.  The current log to write out should always be
 *	l_iclog.  The two logs are switched, so another thread can begin
 *	writing to the non-syncing in-core log.  Before an in-core log can
 *	be written out, the data section must be scanned to make sure there
 *	are no occurrences of the log header magic number at log block
 *	boundaries.
 */
void
log_sync(log_t *log, xfs_lsn_t *lsn, uint flags)
{
    in_core_log_t *tic_log = log->l_iclog;
    uint	  *dptr;	/* pointer to integer sized element */
    buf_t	  *bp;
    int		  i = 0;
    uint	  count;

    if (flags != 0 && ((flags & XFS_LOG_SYNC) != XFS_LOG_SYNC))
	log_panic("log_sync: illegal flag");

    /* swap in-core logs */
    log->l_iclog = log->l_iclog2;
    log->l_iclog2 = tic_log;
    tic_log->ic_state = LOG_SYNCING;
    if (log->l_iclog->ic_state == LOG_NOTUSED)
	log->l_iclog->ic_state = LOG_CURRENT;
    
    tic_log->ic_header.h_lsn = *lsn;
    for (dptr = (uint *)tic_log->ic_data + (LOG_BBSIZE - LOG_HEADER_SIZE);
	 dptr < (uint *)tic_log->ic_data + tic_log->ic_offset;
	 dptr += 1024) {
	if (*dptr == LOG_HEADER_MAGIC_NUM) {
	    tic_log->ic_header.h_blocks_col[i] = ((uint)dptr >> LOG_BBSHIFT);
	    *dptr = 0;
	    i++;
	}
    }
    tic_log->ic_header.h_len = tic_log->ic_offset;

    bp = tic_log->ic_bp;
    bp->b_blkno = LOGBB_TO_BB(log->l_currblock);

    /* Round byte count up to a LOG_BBSIZE chunk */
    count = bp->b_bcount = (tic_log->ic_offset + LOG_BBSIZE - 1) & ~LOG_BBMASK;
    bp->b_dmaaddr = (caddr_t) tic_log;
    if (flags & XFS_LOG_SYNC)
	bp->b_flags |= (B_BUSY | B_HOLD);
    else
	bp->b_flags |= (B_BUSY | B_ASYNC);
    bp->b_bufsize = count;
    bp->b_iodone = log_iodone;
    bp->b_edev = log->l_dev;
    psema(&bp->b_lock, PINOD);

    bwrite(bp);

    if (bp->b_flags & B_ERROR == B_ERROR) {
	log_panic("log_sync: buffer error");
    } else {
	log->l_currblock += NBLOCKS(count);	/* 4k blocks */
    }
    tic_log->ic_state = LOG_DIRTY;
}	/* log_sync */


/*****************************************************************************
 *
 *		TICKET functions
 *
 *****************************************************************************
 */
/*
 *	Algorithm doesn't take into account page size. ;-(
 */
void
log_alloc_tickets(log_t *log)
{
    caddr_t buf;
    log_ticket_t *t_list;
    uint i = LOG_TICKET_TABLE_SIZE-1;


    /*
     * XXXmiken: may want to account for differing sizes of pointers
     * or allocation one page at a time.
     */
    buf = (caddr_t) kmem_zalloc(LOG_TICKET_TABLE_SIZE *
				sizeof(log_ticket_t), 0);

    t_list = log->l_freelist = (log_ticket_t *)buf;
    do {
#ifdef TICKET_INT
	t_list->t_slot = i;
#endif
	t_list->t_next = t_list+1;
	t_list = t_list->t_next;
    } while (i-- > 0);
    /* t_list->t_slot = 0; => zalloc() did this! */
    t_list->t_next = 0;
}	/* log_alloc_tickets */


#ifdef TICKET_INT
/*
 * name:	log_geticket()
 * purpose: Look through the ticket hash table for this log and look for
 *	the one with the slot number which matches.  On success, return
 *	with the lock held for the ticket.
 */
int
log_getticket(log_t *log, int slot, log_ticket_t **ticket)
{
    log_ticket_t *t_list = log->l_hash[slot & LOG_TICKET_MASK];

    while (t_list) {
	if (t_list->t_slot == slot) {
	    LOG_LOCK(t_list->t_lock);
	    *ticket = t_list;
	    return (0);
	} else if (t_list->t_next == NULL) {
	    return -1;
	} else {
	    t_list = t_list->t_next;
	}
    }
    return -1;
}	/* log_getticket */
#endif


/*
 *
 */
#ifdef TICKET_INT
void log_putticket(log_t *log, int slot)
#else
void log_putticket(log_t *log, log_ticket_t *ticket)
#endif
{
    log_ticket_t *t_list;

    ticket->t_next = log->l_freelist;
    log->l_freelist = ticket;
    /* no need to clear fields */
}	/* log_putticket */


/*
 *
 */
void
log_relticket(log_ticket_t *tic)
{
#ifdef TICKET_INT
    LOG_UNLOCK(tic->t_lock);
#endif
}	/* log_relticket */


#ifdef TICKET_INT
int
#else
log_ticket_t *
#endif
log_maketicket(log_t		*log,
	       xfs_tid_t	tid,
	       int		len,
	       char		log_clientid)
{
    log_ticket_t *tic;

#ifdef TICKET_INT
    LOG_LOCK(log->l_hash_lock);
#endif
    if (log->l_freelist == NULL) {
	/* do something here */
    }
    tic = log->l_freelist;
    log->l_freelist = tic->t_next;
    tic->t_reservation = len;
    tic->t_tid = tid;
    tic->t_clientid = log_clientid;
#ifdef TICKET_INT
    log_puthash(log->l_hash, tic);
    LOG_UNLOCK(log->l_hash_lock);
    return(tic->t_slot);
#else
    return(tic);
#endif
}	/* log_maketicket */

#ifdef TICKET_INT
/*
 *
 */
void
log_puthash(log_ticket_t *hash[], log_ticket_t *tic)
{
    register uint slot = tic->t_slot;

    if (hash[slot] == NULL) {
	hash[slot] = tic;
	tic->t_next = NULL;
    } else {
	tic->t_next = hash[slot];
	hash[slot] = tic;
    }
}	/* log_puthash */
#else
#endif

/******************************************************************************
 *
 *		Log recover routines
 *
 ******************************************************************************
 */
uint xfs_log_end(struct xfs_mount *, dev_t);

int
log_recover(struct xfs_mount *mp, dev_t log_dev)
{
    return 0;
#if XXX
    blkno = xfs_log_end(mp, log_dev);
    xfs_log_read(blkno, log_dev);
#endif
}

#if XXX
uint
log_end(struct xfs_mount *mp, dev_t log_dev)
{
    struct stat buf;
    int err, log_size, log_blks;

    if ((err = fstat(major(log_dev), &buf)) != 0)
	return ERROR;

    log_size = buf.st_size;
    log_blks = log_size / 512;

}
#endif


/******************************************************************************
 *
 *		Log print routines
 *
 ******************************************************************************
 */

#ifndef _KERNEL
void print_lsn(caddr_t string, xfs_lsn_t *lsn)
{
    printf("%s: %x,%x", string, ((uint *)lsn)[0], ((uint *)lsn)[1]);
}


#if SIM
void print_tid(caddr_t string, xfs_tid_t *tid)
{
    printf("%s: %x", string, ((uint *)tid)[0]);
}
#else
void print_tid(caddr_t string, xfs_tid_t *tid)
{
    printf("%s: %x,%x,%x,%x", string,
	   ((uint *)tid)[0], ((uint *)tid)[1],
	   ((uint *)tid)[2], ((uint *)tid)[3]);
}
#endif

uint log_print_head(log_rec_header_t *head)
{
    uint *uint_ptr;

    if (head->h_magicno != LOG_HEADER_MAGIC_NUM) {
	printf("Bad log record header or end of log\n");
	exit(1);
    }
    printf("version: %d	", head->h_version);
    print_lsn("	lsn", &head->h_lsn);
    print_lsn("	sync_lsn", &head->h_sync_lsn);
    printf("\n");
    printf("length of LR: %d	prev offset: %d		num ops: %d\n",
	   head->h_len, head->h_prev_offset, head->h_num_logops);
    uint_ptr = head->h_blocks_col;
    while (uint_ptr) {
	printf("block no: %d\n", uint_ptr);
	uint_ptr++;
    }
    return(head->h_len);
}


void log_print_record(int fd, uint len)
{
    caddr_t buf = (caddr_t) kmem_zalloc(len, 0);
    caddr_t ptr = buf;
    log_op_header_t *head;
    int i = 1, n, read_len;

    /* read_len must read up to some block boundary */
    read_len = ((len >> 12) + LOG_BBSIZE-LOG_HEADER_SIZE);
    if (read(fd, buf, read_len) == -1) {
	printf("log_print_record: read error\n");
	exit(1);
    }
    do {
	head = (log_op_header_t *)ptr;
	printf("Operation: %d\n", i);
	print_tid("tid", &head->oh_tid);
	printf("	len: %d	clientid: %s\n",
	       head->oh_len, (head->oh_clientid == XFS_TRANSACTION_MANAGER ?
			      "XFS_TRANSACTION_MANAGER" : "ERROR"));
	ptr += sizeof(log_op_header_t);
	len -= sizeof(log_op_header_t);
	for (n = 0; n < head->oh_len; n++) {
	    printf("%c", *ptr);
	    ptr++; len--;
	}
	printf("\n");
    } while (len > 0);
}


void xfs_log_print(xfs_mount_t *mp, dev_t log_dev)
{
    int fd = bmajor(log_dev);
    char buf[512];
    int done = 0;
    uint len;

    do {
	if ((len=read(fd, buf, 512)) == -1) {
	    printf("xfs_log_print end of log\n");
	    done++;
	    continue;
	}
	len = log_print_head((log_rec_header_t *)buf);
	log_print_record(fd, len);
	printf("=================================\n");
    } while (!done);
}
#endif /* !_KERNEL */






#endif /* LOG_DEBUG */
