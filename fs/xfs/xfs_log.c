/*
 * High level interface routines for log manager
 */

#include <sys/types.h>

#include <sys/param.h>
#ifdef SIM
#define _KERNEL
#endif
#include <sys/sysmacros.h>
#include <sys/buf.h>
#ifdef SIM
#undef _KERNEL
#include <bstring.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#else
#include <sys/kmem.h>
#endif

#include <sys/debug.h>
#include <sys/sema.h>
#include <sys/uuid.h>
#include <sys/vnode.h>

#include "xfs_inum.h"
#include "xfs_types.h"
#include "xfs_sb.h"		/* depends on xfs_types.h & xfs_inum.h */
#include "xfs_log.h"
#include "xfs_trans.h"
#include "xfs_mount.h"		/* depends on xfs_trans.h & xfs_sb.h */

#ifdef SIM
#include "sim.h"		/* must be last include file */
#endif

#ifndef _LOG_DEBUG
int
xfs_log_reserve(xfs_mount_t	 *mp,
		uint		 len,
		xfs_log_ticket_t *x_ticket,
		char		 log_client,
		uint		 flags)
{
        return (1);
}

int
xfs_log_write(xfs_mount_t *	mp,
	      xfs_log_iovec_t	reg[],
	      int		nentries,
	      xfs_log_ticket_t	tic)
{
	return 0;
}	/* xfs_log_write */

void
xfs_log_done(xfs_mount_t	*mp,
	     xfs_log_ticket_t	tic,
	     uint		flags)
{
	return;
}	/* xfs_log_done */
#else


#include "xfs_log_priv.h"

#define log_write_adv_cnt(ptr, len, off, bytes) \
	{ (ptr) += (bytes); \
	  (len) -= (bytes); \
	  (off) += (bytes);}


/* Local function prototypes */
STATIC void log_alloc(xfs_mount_t *mp, dev_t log_dev);
STATIC void log_commit_record(xfs_mount_t *mp, log_ticket_t *ticket);
STATIC void log_copy(log_in_core_t *, xfs_log_iovec_t region[], int i, int n,
		     int offset, xfs_lsn_t *lsn);
STATIC void log_sync(log_t *log, log_in_core_t *iclog, uint flags);
STATIC void log_unalloc(void);
STATIC int  log_write(xfs_mount_t *mp, xfs_log_iovec_t	region[], int nentries,
		      xfs_log_ticket_t	tic, int commit);

/* local state machine functions */
STATIC void log_state_done_syncing(log_in_core_t *iclog);
STATIC void log_state_finish_copy(log_t *log, log_in_core_t *iclog, int bytes);
STATIC int  log_state_get_iclog_space(log_t *log, int len,
				      log_in_core_t **iclog, int *last_write);
STATIC void log_state_release_iclog(log_t *log,	log_in_core_t *iclog);
STATIC void log_state_sync_iclog(log_t *log);
STATIC void log_state_want_sync(log_t *log, log_in_core_t *iclog, uint flags);

/* local ticket functions */
STATIC xfs_log_ticket_t *log_maketicket(log_t *log, int len, char clientid);
STATIC void		log_alloc_tickets(log_t *log);
STATIC void		log_putticket(log_t *log, log_ticket_t *ticket);
STATIC void		log_relticket(log_ticket_t *ticket);

STATIC int	     log_recover(struct xfs_mount *mp, dev_t log_dev);

/*
 * NOTES:
 *
 *	1. currblock field gets updated at startup and after in-core logs
 *		are written out to the disk.
 *
 */

/*
 * purpose: This function will take a log sequence number and check to
 *	see if that lsn has been flushed to disk.  If it has, then the
 *	callback function is called with the callback argument.
 */
void
xfs_log_notify(xfs_mount_t	  *mp,		/* mount of partition */
	       xfs_lsn_t	  lsn,		/* lsn looking for */
	       xfs_log_callback_t *cb)
{
	log_t *log = mp->m_log;
	
	if (log->l_iclog->ic_state == LOG_ACTIVE ||
	    log->l_iclog->ic_state == LOG_SYNCING) {
		printf("help");
	}
}	/* xfs_log_notify */


/*
 *
 */
void
xfs_log_done(xfs_mount_t	*mp,
	     xfs_log_ticket_t	tic,
	     uint		flags)
{
	log_t		*log    = mp->m_log;
	log_ticket_t	*ticket = (xfs_log_ticket_t) tic;
	
	log_commit_record(mp, ticket);
	log_putticket(log, ticket);
}	/* xfs_log_done */


/*
 * purpose: Force the in-core log to disk.  If flags == XFS_LOG_SYNC,
 *	the force is done synchronously.
 */
int
xfs_log_force(xfs_mount_t *mp,
	      xfs_lsn_t	  lsn,
	      uint	  flags)
{
	log_t		*log = mp->m_log;
	
	if (flags & XFS_LOG_FORCE) {
		log_panic("xfs_log_force: not done");
		log_state_want_sync(log, 0, flags);
		log_sync(log, 0, flags);
	} else if (flags & XFS_LOG_URGE) {
		log_panic("xfs_log_force: not yet implemented");
		return -1;
	} else
		return -1;	/* XFS_LOG_FORCE | XFS_LOG_URGE must be set */
	
}	/* xfs_log_force */


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
 *
 * Each reservation is going to reserve extra space for a log record header.
 * When writes happen to the on-disk log, we don't subtract from any
 * reservation.  Log space is wasted in order to insure that deadlock
 * never happens.
 */
int
xfs_log_reserve(xfs_mount_t	 *mp,
		uint		 len,
		xfs_log_ticket_t *ticket,
		char		 log_client,
		uint		 flags)
{
	log_t *log = mp->m_log;
	
	if (log_client != XFS_TRANSACTION_MANAGER)
		return -1;
	
	if (flags & XFS_LOG_SLEEP)
		return XFS_ENOTSUP;
	
	len += LOG_HEADER_SIZE;

	/* Eventually force out buffers */
	if (log->l_logreserved + len > log->l_logsize)
		return XFS_ENOLOGSPACE;
	log->l_logreserved += len;
	*ticket = log_maketicket(mp->m_log, len, log_client);
	if (flags & XFS_LOG_PERM_RESERV)
		((log_ticket_t *)ticket)->t_flags |= LOG_TIC_PERM_RESERV;

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
xfs_log_mount(xfs_mount_t	*mp,
	      dev_t		log_dev,
	      uint		flags)
{
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
	      xfs_log_ticket_t	tic)
{
	log_write(mp, reg, nentries, tic, 0);
}	/* xfs_log_write */


/******************************************************************************
 *
 *	local routines
 *
 ******************************************************************************
 */


/*
 *
 */
void
log_alloc(xfs_mount_t	*mp,
	  dev_t		log_dev)
{
	log_t			*log;
	log_rec_header_t	*head;
	log_in_core_t		**iclogp;
	log_in_core_t		*iclog;
	int i;
	
	/* LOG_RECORD_BSIZE must be multiple of BBSIZE; see log_rec_header_t */
	ASSERT((LOG_RECORD_BSIZE & BBMASK) == 0);

	log = mp->m_log = (void *)kmem_zalloc(sizeof(log_t), 0);
	log_alloc_tickets(log);
	
	log->l_dev = log_dev;
	log->l_logreserved = 0;
	log->l_currblock = 0;
	initnlock(&log->l_icloglock, "iclog");
	initnsema(&log->l_flushsema, LOG_NUM_ICLOGS, "iclog");

	if ((log->l_logsize = log_findlogsize(log_dev)) == -1)
		log_panic("log_findlogsize");
	
	iclogp = &log->l_iclog;
	for (i=0; i < LOG_NUM_ICLOGS; i++) {
		*iclogp =(log_in_core_t *)kmem_zalloc(sizeof(log_in_core_t), 0);
		iclog = *iclogp;

		head = &iclog->ic_header;
		head->h_magicno = LOG_HEADER_MAGIC_NUM;
		head->h_version = 1;
		head->h_lsn = 0;

/* XXXmiken: Need to make the size of an iclog at least 2x the size of
 *		a filesystem block.  This means some code will not be
 *		compilable.  Additional fields may be needed to precompute
 *		values.
 */
		iclog->ic_size = LOG_RECORD_BSIZE-LOG_HEADER_SIZE;
		iclog->ic_state = LOG_ACTIVE;
		iclog->ic_log = log;
/*		iclog->ic_refcnt = 0;	*/
		iclog->ic_bp = getrbuf(0);
		psema(&iclog->ic_bp->b_lock, PINOD);	/* it's mine */

		iclogp = &iclog->ic_next;
	}
	*iclogp = log->l_iclog;		/* complete ring */
}	/* log_alloc */


void
log_commit_record(xfs_mount_t  *mp,
		  log_ticket_t *ticket)
{
	int		error;
	xfs_log_iovec_t	reg[1];
	
	reg[0].i_addr = 0;
	reg[0].i_len = 0;

	error = log_write(mp, reg, 1, ticket, 1);
	if (error)
		log_panic("log_commit_record");
}	/* log_commit_record */


void
log_copy(log_in_core_t	 *iclog,
	 xfs_log_iovec_t reg[],
	 int		 i,
	 int		 n,
	 int		 offset,
	 xfs_lsn_t	 *lsn)
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
 * purpose: Function which is called when an io completes.  The log manager
 *	needs its own routine, in order to control what happens with the buffer
 *	after the write completes.
 */
void
log_iodone(buf_t *bp)
{
	log_state_done_syncing((log_in_core_t *)(bp->b_dmaaddr));
	if ( !(bp->b_flags & B_ASYNC) ) {
		/* Corresponding psema() will be done in bwrite().  If we don't
		 * vsema() here, panic.
		 */
		vsema(&bp->b_iodonesema);
	}
}	/* log_iodone */


/*
 * purpose: Flush out the in-core log to the on-disk log in a synchronous or
 *	asynchronous fashion.  The current log to write out should always be
 *	l_iclog.  The two logs are switched, so another thread can begin
 *	writing to the non-syncing in-core log.  Before an in-core log can
 *	be written out, the data section must be scanned to make sure there
 *	are no occurrences of the log header magic number at log block
 *	boundaries.
 */
void
log_sync(log_t		*log,
	 log_in_core_t	*iclog,
	 uint		flags)
{
	caddr_t		dptr;		/* pointer to byte sized element */
	buf_t		*bp;
	int		i;
	uint		count;
	
	if (flags != 0 && ((flags & XFS_LOG_SYNC) != XFS_LOG_SYNC))
		log_panic("log_sync: illegal flag");
	
	for (i = 0,
	     dptr = (caddr_t)iclog->ic_data;
	     dptr < (caddr_t)iclog->ic_data + iclog->ic_offset;
	     dptr += BBSIZE, i++) {
		iclog->ic_header.h_cycle_data[i] = *(uint *)dptr;
		*(uint *)dptr = log->l_cycle;
	}
	iclog->ic_header.h_len = iclog->ic_offset;

	bp = iclog->ic_bp;
	bp->b_blkno = ((uint *)&iclog->ic_header.h_lsn)[1];

	/* Round byte count up to a LOG_BBSIZE chunk */
	count =	bp->b_bcount = BBTOB(BTOBB(iclog->ic_offset)) + LOG_HEADER_SIZE;  
	bp->b_dmaaddr = (caddr_t) iclog;
	if (flags & XFS_LOG_SYNC) {
		bp->b_flags |= (B_BUSY | B_HOLD);
	} else {
		bp->b_flags |= (B_BUSY | B_ASYNC);
	}
	bp->b_bufsize = count;
	bp->b_iodone = log_iodone;
	bp->b_edev = log->l_dev;

	bwrite(bp);

	if (bp->b_flags & B_ERROR == B_ERROR) {
		log_panic("log_sync: buffer error");
	}
}	/* log_sync */


void
log_unalloc(void)
{
}	/* log_unalloc */


/*
 * 1.  Tickets are single threaded structures.
 *
 * ERRORS:
 *	Return error at any time if reservation is overrun.
 */
int
log_write(xfs_mount_t *		mp,
	  xfs_log_iovec_t	reg[],
	  int			nentries,
	  xfs_log_ticket_t	tic,
	  int			commit)
{
	log_t		*log	= mp->m_log;
	log_ticket_t	*ticket = (log_ticket_t *)tic;
	log_op_header_t	*logop_head;	/* ptr to log operation header */
	log_in_core_t	*iclog;		/* ptr to current in-core log */
	caddr_t		ptr;		/* copy address into data region */
	int		len;		/* len of bytes to still copy */
	int		index;		/* region index currently copying */
	int		log_offset;	/* offset (from 0) into data region */
	int		start_rec_copy;	/* # of bytes to copy for start rec */
	int		remains_to_copy;/* remainder to copy if split region */
	int		need_copy;	/* # of bytes needed to bcopy */
	int		copy_len;	/* # of bytes actually bcopy'ing */
	int		lastwr;		/* last write of in-core log? */

	/* calculate potential maximum space */
	for (len=0, index=0; index < nentries; index++) {
		len += reg[index].i_len;
		len += sizeof(log_op_header_t);
	}

	if (ticket->t_flags & LOG_TIC_INITED)
		len += sizeof(log_op_header_t);	    /* acct for start record */

	if (ticket->t_reservation < len)
		log_panic("xfs_log_write: reservation ran out")
	else if ((ticket->t_flags & LOG_TIC_PERM_RESERV) == 0)
		ticket->t_reservation -= len;

	start_rec_copy = remains_to_copy = 0;
	for (index = 0; index < nentries; ) {
	    log_offset = log_state_get_iclog_space(log, len, &iclog, &lastwr);

	    ptr = &iclog->ic_data[log_offset];
	    for ( ;index < nentries; ) {
		ASSERT(reg[index].i_len % sizeof(long) == 0);

		/*
		 * If first write for transaction, insert start record.
		 * We can't be trying to commit if we are inited.  We
		 * can't have any "remains_to_copy" if we are inited.
		 */
		if (ticket->t_flags & LOG_TIC_INITED) {
			logop_head		= (log_op_header_t *)ptr;
			logop_head->oh_tid	= ticket->t_tid;
			logop_head->oh_clientid	= ticket->t_clientid;
			logop_head->oh_len	= 0;
			logop_head->oh_flags	= LOG_START_TRANS;
			ticket->t_flags &= ~LOG_TIC_INITED;	/* clear bit */

			start_rec_copy = sizeof(log_op_header_t);
			log_write_adv_cnt(ptr, len, log_offset, start_rec_copy);
		}

		/* Copy log operation header directly into data section */
		logop_head		= (log_op_header_t *)ptr;
		logop_head->oh_tid	= ticket->t_tid;
		logop_head->oh_clientid	= ticket->t_clientid;

		/* header copied directly */
		log_write_adv_cnt(ptr,len, log_offset, sizeof(log_op_header_t));

		/* commit record? */
		logop_head->oh_flags = (commit ? LOG_COMMIT_TRANS : 0);

		/* Partial write last time? => (remains_to_copy != 0) */
		need_copy =
		   reg[index].i_len - remains_to_copy + sizeof(log_op_header_t);

		/* what type of write */
		if (need_copy <= iclog->ic_size - log_offset) {/*comple write */
		    logop_head->oh_len = need_copy - sizeof(log_op_header_t);
		    copy_len = reg[index].i_len;
		    if (remains_to_copy)
			logop_head->oh_flags |= LOG_END_TRANS;
		    remains_to_copy = 0;
	        } else if (iclog->ic_size - log_offset < BBSIZE) { /* no room */
		    log_state_want_sync(log, iclog, 0);
		    log_state_release_iclog(log, iclog);
		    remains_to_copy = 0;
		    continue;
	        } else {				/* partial write */
		    remains_to_copy = copy_len = logop_head->oh_len =
			  iclog->ic_size - log_offset - sizeof(log_op_header_t);
		    logop_head->oh_flags |= LOG_CONTINUE_TRANS;
	        }

		/* copy region */
		bcopy(reg[index].i_addr, ptr, copy_len);
		log_write_adv_cnt(ptr, len, log_offset, copy_len);
		log_state_finish_copy(log, iclog,
			(lastwr ?
			 copy_len + start_rec_copy + sizeof(log_op_header_t) :
			 0));

		start_rec_copy = 0;
		if (remains_to_copy) {		/* copied partial region */
		    /* already marked WANT_SYNC */
		    log_state_release_iclog(log, iclog);
		    continue;
	        } else {		/* copied entire region */
		    index++;
		}
	    }
	}

	log_state_want_sync(log, iclog, 0);
	log_state_release_iclog(log, iclog);
}	/* log_write */


/*****************************************************************************
 *
 *		State Machine functions
 *
 *****************************************************************************
 */

void
log_state_done_syncing(log_in_core_t	*iclog)
{
	int spl;
	log_t *log = iclog->ic_log;

	spl = splockspl(log->l_icloglock, 0);

	ASSERT(iclog->ic_state == LOG_SYNCING);

	iclog->ic_state = LOG_CALLBACK;
	vsema(&log->l_flushsema);
	spunlockspl(log->l_icloglock, spl);

	/* perform callbacks XXXmiken */

	spl = splockspl(log->l_icloglock, 0);

	ASSERT(iclog->ic_state == LOG_CALLBACK);

	iclog->ic_state = LOG_DIRTY;
	spunlockspl(log->l_icloglock, spl);

}	/* log_state_done_syncing */


/*
 * If the head of the in-core log ring is not (ACTIVE or DIRTY), then we must
 * sleep.  The flush semaphore is set to the number of in-core buffers and
 * decremented around disk syncing.  Therefore, if all buffers are syncing,
 * this semaphore will cause new writes to sleep until a write completes.
 * Otherwise, this code just does p() followed by v().  This approximates
 * a sleep/wakeup except we can't race.
 *
 * The in-core logs are used in a circular fashion. They are not used
 * out-of-order even when an iclog past the head is free.
 *
 * return:
 *	* log_offset where log_write() can start writing into the in-core
 *		log's data space.
 *	* in-core log pointer to which log_write() should write.
 *	* boolean indicating whether this is the last write to an in-core log.
 *		If this is the last write, then the in-core log's offset field
 *		needs to be incremented, depending on the amount of data which
 *		is copied.
 */
int
log_state_get_iclog_space(log_t		*log,
			  int		len,
			  log_in_core_t **iclogp,
			  int		*last_write)
{
	int		spl;
	int		log_offset;
	log_rec_header_t *head;
	log_in_core_t	*iclog;

restart:
	spl = splockspl(log->l_icloglock, 0);

	iclog = log->l_iclog;
	if ((iclog->ic_state != LOG_ACTIVE && iclog->ic_state != LOG_DIRTY) ) {
		spunlockspl(log->l_icloglock, spl);
		psema(&log->l_flushsema, PINOD);
		vsema(&log->l_flushsema);
		goto restart;
	}

	head = &iclog->ic_header;
	if (iclog->ic_state == LOG_DIRTY) {	/* make ACTIVE */
		head->h_num_logops = 0;
		bzero(head->h_cycle_data, sizeof(head->h_cycle_data));
		iclog->ic_state = LOG_ACTIVE;
		iclog->ic_offset = 0;
	}

	iclog->ic_refcnt++;   /* prevents sync */
	log_offset = iclog->ic_offset;

	/* On the 1st write to an iclog, figure out lsn.  This works
	 * if iclogs marked LOG_WANT_SYNC always write out what they are
	 * committing to.  If the offset is set, that's how many blocks
	 * must be written.
	 */
	if (log_offset == 0) {
		head->h_cycle = log->l_cycle;
		ASSIGN_LSN(head->h_lsn, log);
	}

	/* If there is enough room to write everything, then do it.
	 * Otherwise, claim the rest of the region and make sure the
	 * LOG_WANT_SYNC bit is on, so this will get flushed out.
	 */
	if (len < iclog->ic_size - iclog->ic_offset) {
		iclog->ic_offset += len;
		*last_write = 0;
	} else {
		*last_write = 1;
		if (iclog->ic_state != LOG_WANT_SYNC) {
		    iclog->ic_state = LOG_WANT_SYNC;
		    log->l_currblock +=	BTOBB(iclog->ic_size);
		    log->l_iclog = iclog->ic_next;
		}
		
		/* log_write() algorithm assumes that at least 2
		 * log_op_header_t's can fit into remaining data section.
		 */
		if (iclog->ic_size - iclog->ic_offset <
		    2*sizeof(log_op_header_t)) {
			iclog->ic_refcnt--;
			spunlockspl(log->l_icloglock, spl);
			goto restart;
		}
	}
	*iclogp = iclog;

	spunlockspl(log->l_icloglock, spl);
	return log_offset;
}	/* log_state_get_iclog_space */


void
log_state_release_iclog(log_t		*log,
			log_in_core_t	*iclog)
{
	int spl;
	int sync = 0;

	spl = splockspl(log->l_icloglock, 0);

	ASSERT(iclog->ic_refcnt > 0);

	if (--iclog->ic_refcnt == 0 && iclog->ic_state == LOG_WANT_SYNC) {
		ASSERT(valusema(&log->l_flushsema) > 0);
		sync++;
		iclog->ic_state = LOG_SYNCING;
		psema(&log->l_flushsema, PINOD);	/* won't sleep! */
	}

	spunlockspl(log->l_icloglock, spl);

	if (sync)
		log_sync(log, iclog, 0);

}	/* log_state_release_iclog */


void
log_state_sync_iclog(log_t *log)
{
	int spl;

	spl = splockspl(log->l_icloglock, 0);

	spunlockspl(log->l_icloglock, spl);
}	/* log_state_sync_iclog */


void
log_state_finish_copy(log_t		*log,
		      log_in_core_t	*iclog,
		      int		copy_bytes)
{
	int spl;

	spl = splockspl(log->l_icloglock, 0);

	iclog->ic_header.h_num_logops++;
	iclog->ic_offset += copy_bytes;

	spunlockspl(log->l_icloglock, spl);
}	/* log_state_finish_copy */


void
log_state_want_sync(log_t *log, log_in_core_t *iclog, uint flags)
{
	int spl;

	spl = splockspl(log->l_icloglock, 0);

	if (flags == XFS_LOG_SYNC)
		log_panic("log_state_want_sync: not yet implemented");

	if (iclog->ic_state == LOG_ACTIVE) {
		iclog->ic_state = LOG_WANT_SYNC;
		log->l_currblock +=
			((iclog->ic_offset + (BBSIZE-1)) >> BBSHIFT) + 1;
		log->l_iclog = log->l_iclog->ic_next;
	} else if (iclog->ic_state != LOG_WANT_SYNC)
		log_panic("log_state_want_sync: bad state");

	spunlockspl(log->l_icloglock, spl);
}	/* log_state_sync_iclog */



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
	uint i = (4096 / sizeof(log_ticket_t))-1;	/* XXXmiken */

	/*
	 * XXXmiken: may want to account for differing sizes of pointers
	 * or allocate one page at a time.
	 */
	buf = (caddr_t) kmem_zalloc(4096, 0);

	t_list = log->l_freelist = (log_ticket_t *)buf;
	for ( ; i > 0; i--) {
		t_list->t_next = t_list+1;
		t_list = t_list->t_next;
	}
	/* t_list->t_slot = 0; => zalloc() did this! */
	t_list->t_next = 0;

}	/* log_alloc_tickets */


/*
 *
 */
void log_putticket(log_t *log,
		   log_ticket_t *ticket)
{
	log_ticket_t *t_list;

	ticket->t_next = log->l_freelist;
	log->l_freelist = ticket;
	/* no need to clear fields */
}	/* log_putticket */


xfs_log_ticket_t *
log_maketicket(log_t		*log,
	       int		len,
	       char		log_clientid)
{
	log_ticket_t *tic;

	if (log->l_freelist == NULL) {
		/* do something here */
	}
	tic = log->l_freelist;
	log->l_freelist = tic->t_next;
	tic->t_reservation = len;
	tic->t_tid = (log_tid_t)tic;
	tic->t_clientid = log_clientid;
	tic->t_flags = LOG_TIC_INITED;
	return (xfs_log_ticket_t)tic;
}	/* log_maketicket */


/******************************************************************************
 *
 *		Log recover routines
 *
 ******************************************************************************
 */
uint
xfs_log_end(struct xfs_mount *, dev_t);

int
	log_recover(struct xfs_mount *mp, dev_t log_dev)
{
	return 0;
#if XXXmiken
	blkno = xfs_log_end(mp, log_dev);
	xfs_log_read(blkno, log_dev);
#endif
}

#if XXXmiken
uint
log_end(struct xfs_mount *mp, dev_t log_dev)
{
	struct stat buf;
	int err, log_size, log_blks;
	
	if ((err = fstat(major(log_dev), &buf)) != 0)
		return ERROR;
	
	log_size = buf.st_size;
	log_blks = log_size / BBSIZE;
	
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
void print_tid(caddr_t string, log_tid_t *tid)
{
	printf("%s: %x", string, ((uint *)tid)[0]);
}
#else
void print_tid(caddr_t string, log_tid_t *tid)
{
	printf("%s: %x,%x,%x,%x", string,
	       ((uint *)tid)[0], ((uint *)tid)[1],
	       ((uint *)tid)[2], ((uint *)tid)[3]);
}
#endif


uint log_print_head(log_rec_header_t *head)
{
	uint *uint_ptr;
	int i;
	
	if (head->h_magicno != LOG_HEADER_MAGIC_NUM) {
		printf("Bad log record header or end of log\n");
		exit(1);
	}
	printf("cycle: %d	version: %d	", head->h_cycle, head->h_version);
	print_lsn("	lsn", &head->h_lsn);
	print_lsn("	sync_lsn", &head->h_sync_lsn);
	printf("\n");
	printf("length of Log Record: %d	prev offset: %d		num ops: %d\n",
	       head->h_len, head->h_prev_offset, head->h_num_logops);
	
	printf("cycle num overwrites: ");
	for (i=0; i< LOG_RECORD_BSIZE/BBSIZE; i++) {
		printf("%d  ", head->h_cycle_data[i]);
	}
	printf("\n");

	return(head->h_len);
}


void log_print_record(int fd, int len, log_rec_header_t *rhead)
{
	caddr_t buf, ptr;
	log_op_header_t *op_head;
	int n, i = 1;
	int read_len;
	
	/* read_len must read up to some block boundary */
	read_len = BBTOB(BTOBB(len + sizeof(log_op_header_t)));
	ptr = buf = (caddr_t) kmem_alloc(read_len, 0);
	if (read(fd, buf, read_len) == -1) {
		printf("log_print_record: read error\n");
		exit(1);
	}
	for (i = 0, ptr = buf;
	     ptr < buf + read_len;
	     ptr += BBSIZE, i++) {
		*(uint *)ptr = rhead->h_cycle_data[i];
	}
	ptr = buf;
	for (i=1; len > 0; i++) {
		op_head = (log_op_header_t *)ptr;
		printf("Operation (%d): ", i);
		print_tid("tid", &op_head->oh_tid);
		printf("	len: %d	clientid: %s\n",
		       op_head->oh_len,
		       (op_head->oh_clientid == XFS_TRANSACTION_MANAGER ?
			"TRANS_MANAGER" : "ERROR"));
		printf("flags: ");
		if (op_head->oh_flags) {
			if (op_head->oh_flags & LOG_START_TRANS)
				printf("START ");
			if (op_head->oh_flags & LOG_COMMIT_TRANS)
				printf("COMMIT ");
			if (op_head->oh_flags & LOG_CONTINUE_TRANS)
				printf("CONTINUE ");
			if (op_head->oh_flags & LOG_END_TRANS)
				printf("END ");
			printf("\n");
		} else {
			printf("none\n");
		}

		ptr += sizeof(log_op_header_t);
		for (n = 0; n < op_head->oh_len; n++) {
			printf("%c", *ptr);
			ptr++;
		}
		printf("\n");
		len -= sizeof(log_op_header_t) + op_head->oh_len;
	}
	printf("\n");
}


void xfs_log_print(xfs_mount_t *mp, dev_t log_dev)
{
	int fd = bmajor(log_dev);
	char hbuf[LOG_HEADER_SIZE];
	int done = 0;
	uint len;
	
	do {
		if ((len=read(fd, hbuf, 512)) == -1) {
			printf("xfs_log_print end of log\n");
			done++;
			continue;
		}
		len = log_print_head((log_rec_header_t *)hbuf);
		log_print_record(fd, len, (log_rec_header_t *)hbuf);
		printf("=================================\n");
	} while (!done);
}
#endif /* !_KERNEL */






#endif /* _LOG_DEBUG */
