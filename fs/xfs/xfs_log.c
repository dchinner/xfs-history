#ident	"$Revision: 1.39 $"

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
#include <sys/systm.h>
#include <sys/conf.h>
#endif

#include <sys/kmem.h>
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
#include "xfs_inode_item.h"
#include "xfs_log_priv.h"	/* depends on all above */


#ifdef SIM
#include "sim.h"		/* must be last include file */
#endif


#define xlog_write_adv_cnt(ptr, len, off, bytes) \
	{ (ptr) += (bytes); \
	  (len) -= (bytes); \
	  (off) += (bytes);}

#define GET_CYCLE(ptr)	(*(uint *)(ptr) == XLOG_HEADER_MAGIC_NUM ? *((uint *)(ptr)+1) : *(uint *)(ptr))

#define BLK_AVG(blk1, blk2)	((blk1+blk2) >> 1)

/* Local miscellaneous function prototypes */
STATIC void	 xlog_alloc(xlog_t *log);
STATIC xfs_lsn_t xlog_commit_record(xfs_mount_t *mp, xlog_ticket_t *ticket);
STATIC daddr_t	 xlog_find_head(xlog_t *log);
STATIC int	 xlog_find_zeroed(xlog_t *log, daddr_t* blk_no);
STATIC xlog_t *  xlog_init_log(xfs_mount_t *mp, dev_t log_dev,
			       daddr_t blk_offset, int num_bblks);
STATIC void	 xlog_push_buffers_to_disk(xfs_mount_t *mp, xlog_t *log);
STATIC void	 xlog_sync(xlog_t *log, xlog_in_core_t *iclog, uint flags);
STATIC void	 xlog_unalloc(void);
STATIC int	 xlog_write(xfs_mount_t *mp, xfs_log_iovec_t region[],
			    int nentries, xfs_log_ticket_t tic,
			    xfs_lsn_t *start_lsn, int commit);

/* local state machine functions */
STATIC void		xlog_state_done_syncing(xlog_in_core_t *iclog);
STATIC void		xlog_state_finish_copy(xlog_t *log,
					       xlog_in_core_t *iclog,
					       int first_write, int bytes);
STATIC int		xlog_state_get_iclog_space(xlog_t *log, int len,
						   xlog_in_core_t **iclog,
						   int *continued_write);
STATIC xfs_log_ticket_t xlog_state_get_ticket(xlog_t *log, int len,
					      char log_client, uint flags);
STATIC int		xlog_state_lsn_is_synced(xlog_t *log, xfs_lsn_t lsn,
						 xfs_log_callback_t *cb);
STATIC void		xlog_state_put_ticket(xlog_t *log, xlog_ticket_t *tic);
STATIC void		xlog_state_release_iclog(xlog_t *log,
						 xlog_in_core_t *iclog);
static void		xlog_state_switch_iclogs(xlog_t *log,
						 xlog_in_core_t *iclog);
STATIC int		xlog_state_sync(xlog_t *log, xfs_lsn_t lsn, uint flags);
STATIC void		xlog_state_want_sync(xlog_t *log,
					     xlog_in_core_t *iclog);

/* local ticket functions */
STATIC xfs_log_ticket_t *xlog_maketicket(xlog_t *log, int len, char clientid);
STATIC void		xlog_alloc_tickets(xlog_t *log);
STATIC void		xlog_putticket(xlog_t *log, xlog_ticket_t *ticket);
STATIC void		xlog_relticket(xlog_ticket_t *ticket);

STATIC int		xlog_recover(xlog_t *log);

STATIC void		xlog_verify_dest_ptr(xlog_t *log, psint ptr);
STATIC void		xlog_verify_iclog(xlog_t *log, xlog_in_core_t *iclog,
					  int count, boolean_t syncing);


/*
 * 0 => disable log manager
 * 1 => enable log manager
 * 2 => enable log manager and log debugging
 */
int xlog_debug = 0;


#ifdef DEBUG
int bytes_of_ticket_used;
#endif

/*
 * NOTES:
 *
 *	1. currblock field gets updated at startup and after in-core logs
 *		marked as with WANT_SYNC.
 */

/*
 * This routine is called when a user of a log manager ticket is done with
 * the reservation.  If the ticket was ever used, then a commit record for
 * the associated transaction is written out as a log operation header with
 * no data.  The flag XLOG_TIC_INITED is set when the first write occurs with
 * a given ticket.  If the ticket was one with a permanent reservation, then
 * a few operations are done differently.  Permanent reservation tickets by
 * default don't release the reservation.  They just commit the current
 * transaction with the belief that the reservation is still needed.  A flag
 * must be passed in before permanent reservations are actually released.
 * When these type of tickets are not released, they need to be set into
 * the inited state again.  By doing this, a start record will be written
 * out when the next write occurs.
 */
xfs_lsn_t
xfs_log_done(xfs_mount_t	*mp,
	     xfs_log_ticket_t	tic,
	     uint		flags)
{
	xlog_t		*log    = mp->m_log;
	xlog_ticket_t	*ticket = (xfs_log_ticket_t) tic;
	xfs_lsn_t	lsn;
	
	if (! xlog_debug)
		return 0;

	/* If nothing was ever written, don't write out commit record */
	if ((ticket->t_flags & XLOG_TIC_INITED) == 0)
		lsn = xlog_commit_record(mp, ticket);

	/* Release ticket if not permanent reservation or a specifc
	 * request has been made to release a permanent reservation.
	 */
	if ((ticket->t_flags & XLOG_TIC_PERM_RESERV) == 0 ||
	    (flags & XFS_LOG_REL_PERM_RESERV))
		xlog_state_put_ticket(log, ticket);

	/* If this ticket was a permanent reservation and we aren't
	 * trying to release it, reset the inited flags; so next time
	 * we write, a start record will be written out.
	 */
	if ((ticket->t_flags & XLOG_TIC_PERM_RESERV) &&
	    (flags & XFS_LOG_REL_PERM_RESERV) == 0)
		ticket->t_flags |= XLOG_TIC_INITED;

	return lsn;
}	/* xfs_log_done */


/*
 * Force the in-core log to disk.  If flags == XFS_LOG_SYNC,
 *	the force is done synchronously.
 *
 * Asynchronous forces are implemented by setting the WANT_SYNC
 * bit in the appropriate in-core log and then returning.
 *
 * Synchronous forces are implemented with a semaphore.  All callers
 * to force a given lsn to disk will wait on a semaphore attached to the
 * specific in-core log.  When given in-core log finally completes its
 * write to disk, that thread will wake up all threads waiting on the
 * semaphore.
 */
int
xfs_log_force(xfs_mount_t *mp,
	      xfs_lsn_t	  lsn,
	      uint	  flags)
{
	xlog_t *log = mp->m_log;
	
	if (flags & XFS_LOG_FORCE) {
		return(xlog_state_sync(log, lsn, flags));
	} else if (flags & XFS_LOG_URGE) {
		xlog_panic("xfs_log_force: not yet implemented");
		return -1;
	} else
		xlog_panic("xfs_log_force: illegal flags");
	
}	/* xfs_log_force */


/*
 * This function will take a log sequence number and check to see if that
 * lsn has been flushed to disk.  If it has, then the callback function is
 * called with the callback argument.  If the relevant in-core log has not
 * been synced to disk, we add the callback to the callback list of the
 * in-core log.
 */
void
xfs_log_notify(xfs_mount_t	  *mp,		/* mount of partition */
	       xfs_lsn_t	  lsn,		/* lsn looking for */
	       xfs_log_callback_t *cb)
{
	xlog_t *log = mp->m_log;
	
	cb->cb_next = 0;
	if (xlog_state_lsn_is_synced(log, lsn, cb))
		cb->cb_func(cb->cb_arg);
}	/* xfs_log_notify */


/*
 * Initialize log manager data.  This routine is intended to be called when
 * a system boots up.  It is not a per filesystem initialization.
 *
 * As you can see, we currently do nothing.
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
 * When writes happen to the on-disk log, we don't subtract the length of the
 * log record header from any reservation.  By wasting space in each
 * reservation, we prevent over allocation problems.
 */
int
xfs_log_reserve(xfs_mount_t	 *mp,
		uint		 len,
		xfs_log_ticket_t *ticket,
		char		 client,
		uint		 flags)
{
	xlog_t	  *log = mp->m_log;
	xfs_lsn_t tail_lsn;
	
	if (! xlog_debug)
		return 0;

	if (client != XFS_TRANSACTION)
		return -1;
	
	if (flags & XFS_LOG_SLEEP) {
		xlog_panic("xfs_log_reserve: not implemented");
		return XFS_ENOTSUP;
	}
	
	/*
	 * Permanent reservations always have at least two active log
	 * operations in the log.  Other reservations may need one log
	 * record header for each part of an operation which falls in
	 * a different log record.  This is a gross over estimate.
	 */
	if (flags & XFS_LOG_PERM_RESERV)
		len += 2*XLOG_HEADER_SIZE;
	else {
		len += XLOG_HEADER_SIZE *
			((len + XLOG_RECORD_BSIZE - 1) >> XLOG_RECORD_BSHIFT);
	}

	if ((tail_lsn = xfs_trans_tail_ail(mp)) != 0)
	    log->l_tail_lsn = tail_lsn;
	xlog_push_buffers_to_disk(mp, log);

	if ((int)(*ticket = xlog_state_get_ticket(log,len,client,flags)) == -1)
		return XFS_ENOLOGSPACE;

	if (flags & XFS_LOG_PERM_RESERV)
		((xlog_ticket_t *)ticket)->t_flags |= XLOG_TIC_PERM_RESERV;

	return 0;
}	/* xfs_log_reserve */


/*
 * Mount a log filesystem.
 *
 * mp		- ubiquitous xfs mount point structure
 * log_dev	- device number of on-disk log device
 * blk_offset	- Start block # where block size is 512 bytes (BBSIZE)
 * num_bblocks	- Number of BBSIZE blocks in on-disk log
 * flags	-
 *
 */
int
xfs_log_mount(xfs_mount_t	*mp,
	      dev_t		log_dev,
	      daddr_t		blk_offset,
	      int		num_bblks,
	      uint		flags)
{
	xlog_t *log;
	
	if (! xlog_debug)
		return 0;

	num_bblks <<= (mp->m_sb.sb_blocksize >> BBSHIFT);
	blk_offset <<= (mp->m_sb.sb_blocksize >> BBSHIFT);
	log = xlog_init_log(mp, log_dev, blk_offset, num_bblks);
	if (xlog_recover(log) != 0) {
		return XFS_ERECOVER;
	}
	xlog_alloc(log);
	return 0;
}	/* xfs_log_mount */


/*
 * Unmount a log filesystem.
 *
 * Mark the filesystem clean as unmount happens.
 */
int
xfs_log_unmount(xfs_mount_t *mp)
{
	xlog_unalloc();

	return 0;
}


/*
 * Write region vectors to log.  The write happens using the space reservation
 * of the ticket (tic).  It is not a requirement that all writes for a given
 * transaction occur with one call to xfs_log_write().
 */
int
xfs_log_write(xfs_mount_t *	mp,
	      xfs_log_iovec_t	reg[],
	      int		nentries,
	      xfs_log_ticket_t	tic,
	      xfs_lsn_t		*start_lsn)
{
	if (! xlog_debug) {
		*start_lsn = 0;
		return 0;
	}

	return xlog_write(mp, reg, nentries, tic, start_lsn, 0);
}	/* xfs_log_write */


/******************************************************************************
 *
 *	local routines
 *
 ******************************************************************************
 */

/*
 * Log function which is called when an io completes.
 *
 * The log manager needs its own routine, in order to control what
 * happens with the buffer after the write completes.
 */
void
xlog_iodone(buf_t *bp)
{
	xlog_state_done_syncing((xlog_in_core_t *)(bp->b_fsprivate));
	if ( !(bp->b_flags & B_ASYNC) ) {
		/* Corresponding psema() will be done in bwrite().  If we don't
		 * vsema() here, panic.
		 */
		vsema(&bp->b_iodonesema);
	}
}	/* xlog_iodone */



static xlog_t *
xlog_init_log(xfs_mount_t	*mp,
	      dev_t		log_dev,
	      daddr_t		blk_offset,
	      int		num_bblks)
{
	xlog_t *log;

	log = mp->m_log = (void *)kmem_zalloc(sizeof(xlog_t), 0);
	
	log->l_mp	   = mp;
	log->l_dev	   = log_dev;
	log->l_logsize     = BBTOB(num_bblks);
	log->l_logBBstart  = blk_offset;
	log->l_logBBsize   = num_bblks;
}	/* xlog_init */


/*
 * Allocate a log
 *
 * Perform all the specific initialization required during a log mount.
 * Values passed down from xfs_log_mount() were placed in log structure
 * in xlog_init_log().
 */
static void
xlog_alloc(xlog_t *log)
{
	xlog_rec_header_t	*head;
	xlog_in_core_t		**iclogp;
	xlog_in_core_t		*iclog;
	caddr_t			unaligned;
	buf_t			*bp;
	dev_t			log_dev = log->l_dev;
	int			i;
	
	/* XLOG_RECORD_BSIZE must be mult of BBSIZE; see xlog_rec_header_t */
	ASSERT((XLOG_RECORD_BSIZE & BBMASK) == 0);

	xlog_alloc_tickets(log);
	
	log->l_logreserved = 0;
	log->l_prev_block  = -1;
	log->l_tail_lsn    = 0x100000000LL;  /* cycle = 1; current block = 0 */
	log->l_curr_cycle  = 1;	      /* 0 is bad since this is initial value */

	bp = log->l_xbuf   = getrbuf(0);	/* get my locked buffer */
	bp->b_edev	   = log_dev;
	bp->b_iodone	   = xlog_iodone;
	log->l_curr_block  = xlog_find_head(log);
	ASSERT(log->l_xbuf->b_flags & B_BUSY);
	ASSERT(valusema(&log->l_xbuf->b_lock) <= 0);
	initnlock(&log->l_icloglock, "iclog");
	initnsema(&log->l_flushsema, XLOG_NUM_ICLOGS, "iclog-flush");

	iclogp = &log->l_iclog;
	for (i=0; i < XLOG_NUM_ICLOGS; i++) {
		unaligned =
			(caddr_t)kmem_zalloc(sizeof(xlog_in_core_t)+NBPP, 0);
		*iclogp = (xlog_in_core_t *)
			((psint)(unaligned+NBPP-1) & ~(NBPP-1));
		iclog = *iclogp;
		log->l_iclog_bak[i] = iclog;

		head = &iclog->ic_header;
		head->h_magicno = XLOG_HEADER_MAGIC_NUM;
		head->h_version = 1;
/*		head->h_lsn = 0;*/
		head->h_tail_lsn = 0x100000000LL;

/* XXXmiken: Need to make the size of an iclog at least 2x the size of
 *		a filesystem block.  This means some code will not be
 *		compilable.  Additional fields may be needed to precompute
 *		values.
 */
		iclog->ic_size = XLOG_RECORD_BSIZE - XLOG_HEADER_SIZE;
		iclog->ic_state = XLOG_STATE_ACTIVE;
		iclog->ic_log = log;
/*		iclog->ic_refcnt = 0;	*/
		iclog->ic_bwritecnt = 0;
/*		iclog->ic_callback = 0; */
		iclog->ic_callback_tail = &(iclog->ic_callback);
		bp = iclog->ic_bp = getrbuf(0);		/* my locked buffer */
		bp->b_edev = log_dev;
		bp->b_iodone = xlog_iodone;
		ASSERT(iclog->ic_bp->b_flags & B_BUSY);
		ASSERT(valusema(&iclog->ic_bp->b_lock) <= 0);
		initnsema(&iclog->ic_forcesema, 0, "iclog-force");

		iclogp = &iclog->ic_next;
	}
	log->l_iclog_bak[i] = 0;
	log->l_iclog_size = XLOG_RECORD_BSIZE;
	*iclogp = log->l_iclog;		/* complete ring */
}	/* xlog_alloc */


/*
 * Write out the commit record of a transaction associated with the given
 * ticket.  Return the lsn of the commit record.
 */
static xfs_lsn_t
xlog_commit_record(xfs_mount_t  *mp,
		   xlog_ticket_t *ticket)
{
	int		error;
	xfs_log_iovec_t	reg[1];
	xfs_lsn_t	commit_lsn;
	
	reg[0].i_addr = 0;
	reg[0].i_len = 0;

	error = xlog_write(mp, reg, 1, ticket, &commit_lsn, 1);
	if (error)
		xlog_panic("xlog_commit_record: Can't commit transaction");

	return commit_lsn;
}	/* xlog_commit_record */


static buf_t *
xlog_get_bp(int num_bblks)
{
	buf_t   *bp;
	daddr_t unaligned;

	ASSERT(num_bblks > 0);

	bp = getrbuf(0);
	unaligned = (daddr_t)kmem_alloc(BBTOB(num_bblks+1), 0);
	bp->b_dmaaddr = (caddr_t)((unaligned+BBSIZE-1) & ~(BBSIZE-1));
	bp->b_bcount = BBTOB(num_bblks);
	bp->b_private = (caddr_t)unaligned;
	return bp;
}	/* xlog_get_bp */


static void
xlog_put_bp(buf_t *bp, int num_bblks)
{
	kmem_free(bp->b_private, BBTOB(num_bblks+1));
	freerbuf(bp);
}	/* xlog_get_bp */


/*
 * nbblks should be uint, but oh well.  Just want to catch that 32-bit length.
 */
static void
xlog_bread(xlog_t	*log,
	   daddr_t	blk_no,
	   int		nbblks,
	   buf_t	*bp)
{
	ASSERT(nbblks > 0);

	bp->b_blkno	= blk_no + log->l_logBBstart;
	bp->b_flags	= B_READ|B_BUSY;
	bp->b_bcount	= BBTOB(nbblks);
	bp->b_edev	= log->l_dev;

	bdstrat(bmajor(bp->b_edev), bp);
	iowait(bp);

	if (bp->b_flags & B_ERROR) {
		brelse(bp);
		xlog_panic("xlog_bread: bread error");
	}
}	/* xlog_bread */


/*
 * This routine finds (to an approximation) the first block in the physical
 * log which contains the given cycle.  It uses a binary search algorithm.
 * Note that the algorithm can not be perfect because the disk will not
 * necessarily be perfect.
 */
static daddr_t
xlog_find_cycle_start(xlog_t	*log,
		      buf_t	*bp,
		      daddr_t	first_blk,
		      daddr_t	last_blk,
		      uint	cycle)
{
	daddr_t mid_blk;
	uint	mid_cycle;

	mid_blk = BLK_AVG(first_blk, last_blk);
	while (mid_blk != first_blk && mid_blk != last_blk) {
		xlog_bread(log, mid_blk, 1, bp);
		mid_cycle = GET_CYCLE(bp->b_dmaaddr);
		if (mid_cycle == cycle) {
			last_blk = mid_blk;
			/* last_half_cycle == mid_cycle */
		} else {
			first_blk = mid_blk;
			/* first_half_cycle == mid_cycle */
		}
		mid_blk = BLK_AVG(first_blk, last_blk);
	}
	ASSERT((mid_blk == first_blk && mid_blk+1 == last_blk) ||
	       (mid_blk == last_blk && mid_blk-1 == first_blk));

	return last_blk;
}	/* xlog_find_cycle_start */

/*
 * Check that the range of blocks given all start with the cycle number
 * given.  The scan needs to occur from front to back and the ptr into the
 * region must be updated since a later routine will need to perform another
 * test.  If the region is completely good, we end up returning the same
 * last block number.
 */
static daddr_t
xlog_find_verify_cycle(caddr_t	*bap,		/* update ptr as we go */
		       daddr_t	start_blk,
		       int	nbblks,
		       uint	verify_cycle_no)
{
	int	i;
	uint	cycle;
	daddr_t last_blk = start_blk + nbblks;

	for (i=0; i<nbblks; i++) {
		cycle = GET_CYCLE(*bap);
		if (cycle == verify_cycle_no) {
			(*bap) += BBSIZE;
		} else {
			last_blk = start_blk+i;
			break;
		}
	}
	return last_blk;
}	/* xlog_find_verify_cycle */


static daddr_t
xlog_find_verify_log_record(caddr_t	ba,	/* update ptr as we go */
			    daddr_t	start_blk,
			    daddr_t	last_blk)
{
	xlog_rec_header_t *rhead;
	int		  i;

	if (last_blk == start_blk)
		xlog_panic("xlog_find_verify_log_record: need to backup");

	ba -= BBSIZE;
	for (i=last_blk-1; i>=0; i--) {
		if (*(uint *)ba == XLOG_HEADER_MAGIC_NUM)
			break;
		else {
		    if (i < start_blk)
		       xlog_panic("xlog_find_verify_log_record: need to backup");
		    ba -= BBSIZE;
		}
	}
	rhead = (xlog_rec_header_t *)ba;
	if (last_blk - i != BTOBB(rhead->h_len)+1)
		last_blk = i;
	return last_blk;
}	/* xlog_find_verify_log_record */


/*
 * Head is defined to be the point of the log where the next log write
 * write would go.  This means that incomplete writes at the end are eliminated
 * when calculating the head.
 */
static daddr_t
xlog_find_head(xlog_t	*log)
{
	buf_t	*bp, *big_bp;
	daddr_t	first_blk, mid_blk, last_blk, num_scan_bblks;
	daddr_t	start_blk, saved_last_blk;
	uint	first_half_cycle, mid_cycle, last_half_cycle, cycle;
	caddr_t	ba;
	int	i, log_bbnum = log->l_logBBsize;

	/* special case freshly mkfs'ed filesystem */
	if (xlog_find_zeroed(log, &first_blk))
		return first_blk;

	first_blk = 0;					/* read first block */
	bp = xlog_get_bp(1);
	xlog_bread(log, 0, 1, bp);
	first_half_cycle = GET_CYCLE(bp->b_dmaaddr);

	last_blk = log->l_logBBsize-1;			/* read last block */
	xlog_bread(log, last_blk, 1, bp);
	last_half_cycle = GET_CYCLE(bp->b_dmaaddr);
	ASSERT(last_half_cycle != 0);

	if (first_half_cycle == last_half_cycle) {/* all cycle nos are same */
		last_blk = 0;
	} else {		/* have 1st and last; look for middle cycle */
		last_blk = xlog_find_cycle_start(log, bp, first_blk,
						 last_blk, last_half_cycle);
	}

	/* Now validate the answer */
	num_scan_bblks = BTOBB(XLOG_NUM_ICLOGS<<XLOG_RECORD_BSHIFT);
	big_bp = xlog_get_bp(num_scan_bblks);
	if (last_blk >= num_scan_bblks) {
		start_blk = last_blk - num_scan_bblks;
		xlog_bread(log, start_blk, num_scan_bblks, big_bp);
		ba = big_bp->b_dmaaddr;
		last_blk = xlog_find_verify_cycle(&ba, start_blk,
						  num_scan_bblks,
						  first_half_cycle);
	} else {	/* need to read 2 parts of log */
		/* scan end of physical log */
		start_blk = log_bbnum - num_scan_bblks + last_blk;
		xlog_bread(log, start_blk, num_scan_bblks - last_blk, big_bp);
		ba = big_bp->b_dmaaddr;
		saved_last_blk = last_blk;
		if ((last_blk =
		     xlog_find_verify_cycle(&ba, start_blk,
					    num_scan_bblks - last_blk,
					    last_half_cycle)) != saved_last_blk)
			goto bad_blk;

		/* scan beginning of physical log */
		start_blk = 0;
		xlog_bread(log, start_blk, last_blk, big_bp);
		ba = big_bp->b_dmaaddr;
		last_blk = xlog_find_verify_cycle(&ba, start_blk, last_blk,
						  first_half_cycle);
	}

bad_blk:
	/* Potentially backup over partial log record write */
	last_blk = xlog_find_verify_log_record(ba, start_blk, last_blk);
	
	xlog_put_bp(big_bp, num_scan_bblks);
	xlog_put_bp(bp, 1);
	return last_blk;
}	/* xlog_find_head */


/*
 * Start is defined to be the block pointing to the oldest valid log record.
 */
uint
xlog_find_oldest(dev_t log_dev,
		 uint  log_bbnum)
{
	xlog_rec_header_t	*head;
	int			block_start = 0;
	int			block_no = 0;
	int			cycle_no = 0;
	int			binary_block_start = 0;
	buf_t		*bp;
	
	/* read through all blocks to find start of on-disk log */
	while (block_no < log_bbnum) {
		bp = bread(log_dev, block_no, 1);
		if (bp->b_flags & B_ERROR) {
			brelse(bp);
			xlog_panic("xlog_find_oldest");
		}
		head = (xlog_rec_header_t *)bp->b_dmaaddr;
		if (head->h_magicno != XLOG_HEADER_MAGIC_NUM) {
			block_no++;
			brelse(bp);
			continue;
		}
		if (cycle_no == 0) {
			cycle_no	= CYCLE_LSN(head->h_lsn);
			block_start = block_no;
		} else if (CYCLE_LSN(head->h_lsn) < cycle_no) {
			cycle_no	= CYCLE_LSN(head->h_lsn);
			block_start	= block_no;
			brelse(bp);
			break;
		}
		block_no++;
		brelse(bp);
	}
	
	return (uint)block_start;
}	/* xlog_find_oldest */


/*
 * Find the sync block number or the tail of the log.
 *
 * This will be the block number of the last record to have its
 * associated buffers synced to disk.  Every log record header has
 * a sync lsn embedded in it.  LSNs hold block numbers, so it is easy
 * to get a sync block number.  The only concern is to figure out which
 * log record header to believe.
 *
 * The following algorithm uses the log record header with the largest
 * lsn.  The entire log record does not need to be valid.  We only care
 * that the header is valid.
 *
 * NOTE: we also scan the log sequentially.  this will need to be changed
 * to a binary search.
 *
 * We could speed up search by using current head_blk buffer, but it is not
 * available.
 */
daddr_t
xlog_find_tail(xlog_t  *log,
	       daddr_t *head_blk)
{
	xlog_rec_header_t *rhead;
	daddr_t		  tail_blk;
	buf_t		  *bp;
	int		  i, found = 0;
	int		  zeroed_log = 0;
	

	/* find previous log record */
	*head_blk = xlog_find_head(log);
	bp = xlog_get_bp(1);
	if (*head_blk == 0) {
		xlog_bread(log, 0, 1, bp);
		if (*(uint *)(bp->b_dmaaddr) == 0) {
			tail_blk = 0;
			goto exit;
		}
	}
	for (i=(*head_blk)-1; i>=0; i--) {
		xlog_bread(log, i, 1, bp);
		if (*(uint *)(bp->b_dmaaddr) == XLOG_HEADER_MAGIC_NUM) {
			found = 1;
			break;
		} else if (*(uint *)(bp->b_dmaaddr == 0)) {
			if (zeroed_log)
				goto exit;
			zeroed_log = 1;
			tail_blk = i;
		}
	}
	if (!found) {		/* search from end of log */
		for (i=log->l_logBBsize-1; i>=(*head_blk); i--) {
			xlog_bread(log, i, 1, bp);
			if (*(uint *)(bp->b_dmaaddr) == XLOG_HEADER_MAGIC_NUM) {
				found = 1;
				break;
			} else if (*(uint *)(bp->b_dmaaddr == 0)) {
				if (zeroed_log)
					goto exit;
				zeroed_log = 1;
				tail_blk = i;
			}
		}
	}
	if (!found)
		xlog_panic("xlog_find_tail: counldn't find sync record");

	/* find blk_no of tail of log */
	rhead = (xlog_rec_header_t *)bp->b_dmaaddr;
	tail_blk = BLOCK_LSN(rhead->h_tail_lsn);
exit:
	xlog_put_bp(bp, 1);

	return tail_blk;
#ifdef LINEAR_SCAN
	xlog_rec_header_t	*head;
	uint			block_start = 0;
	uint			block_no = 0;
	uint			cycle_no = 0;
	uint			binary_block_start = 0;
	uint			last_log_rec_blk = 0;
	buf_t			*bp;
	daddr_t			blk_offset = log->l_logBBstart;
	dev_t			log_dev = log->l_dev;
	int			log_bbnum = log->l_logBBsize;
	
	/* read through all blocks to find start of on-disk log */
	while (block_no < log_bbnum) {
		bp = bread(log_dev, block_no+blk_offset, 1);
		if (bp->b_flags & B_ERROR) {
			brelse(bp);
			xlog_panic("xlog_find_sync");
		}
		head = (xlog_rec_header_t *)bp->b_dmaaddr;
		if (head->h_magicno != XLOG_HEADER_MAGIC_NUM) {
			block_no++;
			if (cycle_no == 0) {
				cycle_no = *(uint *)head;
			} else if (*(uint *)head < cycle_no) {
				brelse(bp);
				break;
			}
		} else {
			if (cycle_no == 0) {
				cycle_no         = CYCLE_LSN(head->h_lsn);
				last_log_rec_blk = block_no;
			} else if (CYCLE_LSN(head->h_lsn) < cycle_no) {
				brelse(bp);
				break;
			} else {
				last_log_rec_blk = block_no;
			}
		}
		block_no++;
		brelse(bp);
	}
	
	bp = bread(log_dev, last_log_rec_blk+blk_offset, 1);
	if (bp->b_flags & B_ERROR) {
		brelse(bp);
		xlog_panic("xlog_find_sync");
	}
	brelse(bp);

	head = (xlog_rec_header_t *)bp->b_dmaaddr;
	return (uint)BLOCK_LSN(head->h_tail_lsn);
#endif
}	/* xlog_find_sync */


/*
 * Is the log zeroed at all?
 *
 * The last binary search should be changed to perform an X block read
 * once X becomes small enough.  You can then search linearly through
 * the X blocks.  This will cut down on the number of reads we need to do.
 */
int
xlog_find_zeroed(xlog_t	 *log,
		 daddr_t *blk_no)
{
	buf_t	*bp, *big_bp;
	uint	first_cycle, mid_cycle, last_cycle, cycle;
	daddr_t	first_blk, mid_blk, last_blk, start_blk, num_scan_bblks;
	caddr_t	ba;
	int	i, log_bbnum = log->l_logBBsize;

	/* check totally zeroed log */
	bp = xlog_get_bp(1);
	xlog_bread(log, 0, 1, bp);
	first_cycle = GET_CYCLE(bp->b_dmaaddr);
	if (first_cycle == 0) {		/* completely zeroed log */
		*blk_no = 0;
		xlog_put_bp(bp, 1);
		return 1;
	}

	/* check not zeroed log */
	xlog_bread(log, log_bbnum-1, 1, bp);
	last_cycle = GET_CYCLE(bp->b_dmaaddr);
	if (last_cycle != 0) {		/* log completely written to */
		xlog_put_bp(bp, 1);
		return 0;
	}
	ASSERT(first_cycle == 1);

	/* we have a partially zeroed log */
	last_blk = xlog_find_cycle_start(log, bp, 0, log_bbnum-1, 0);

	/* Validate the answer */
	num_scan_bblks = BTOBB(XLOG_NUM_ICLOGS<<XLOG_RECORD_BSHIFT);
	big_bp = xlog_get_bp(num_scan_bblks);
	if (last_blk < num_scan_bblks)
		num_scan_bblks = last_blk;
	start_blk = last_blk - num_scan_bblks;
	xlog_bread(log, start_blk, num_scan_bblks, big_bp);
	ba = big_bp->b_dmaaddr;
	last_blk = xlog_find_verify_cycle(&ba, start_blk, num_scan_bblks, 1);

	/* Potentially backup over partial log record write */
	last_blk = xlog_find_verify_log_record(ba, start_blk, last_blk);

	*blk_no = last_blk;
	xlog_put_bp(big_bp, num_scan_bblks);
	xlog_put_bp(bp, 1);
	return 1;
}	/* xlog_find_zeroed */


/*
 * Push on the buffer cache code if we ever use more than 75% of the on-disk
 * log space.  This code pushes on the lsn which would supposedly free up
 * the 25% which we want to leave free.  We may need to adopt a policy which
 * pushes on an lsn which is further along in the log once we reach the high
 * water mark.  In this manner, we would be creating a low water mark.
 */
void
xlog_push_buffers_to_disk(xfs_mount_t *mp, xlog_t *log)
{
    int		blocks;		/* valid blocks left to write to */
    xfs_lsn_t	tail_lsn;
    xfs_lsn_t	threshhold_lsn;
    int		threshhold_block;

    tail_lsn = log->l_tail_lsn;
    if (CYCLE_LSN(tail_lsn) == log->l_curr_cycle) {
	blocks = log->l_logBBsize - (log->l_curr_block - BLOCK_LSN(tail_lsn));
    } else {
	ASSERT(CYCLE_LSN(tail_lsn) + 1 == log->l_curr_cycle);
	blocks = BLOCK_LSN(tail_lsn) - log->l_curr_block;
    }

    if (blocks < (log->l_logBBsize >> 2)) {
	threshhold_block = BLOCK_LSN(tail_lsn) + (log->l_logBBsize >> 2);
	if (threshhold_block >= log->l_logBBsize) {
	    threshhold_block -= log->l_logBBsize;
	    ((uint *)&(threshhold_lsn))[0] = CYCLE_LSN(tail_lsn) +1;
	} else {
	    ((uint *)&(threshhold_lsn))[0] = CYCLE_LSN(tail_lsn);
	}
	((uint *)&(threshhold_lsn))[1] = threshhold_block;
	xfs_trans_push_ail(mp, threshhold_lsn);
    }
}	/* xlog_push_buffers_to_disk */


/*
 * Flush out the in-core log (iclog) to the on-disk log in a synchronous or
 * asynchronous fashion.  Previously, we should have moved the current iclog
 * ptr in the log to point to the next available iclog.  This allows further
 * write to continue while this code syncs out an iclog ready to go.
 * Before an in-core log can be written out, the data section must be scanned
 * to save away the 1st word of each BBSIZE block into the header.  We replace
 * it with the current cycle count.  Each BBSIZE block is tagged with the
 * cycle count because there in an implicit assumption that drives will
 * guarantee that entire 512 byte blocks get written at once.  In other words,
 * we can't have part of a 512 byte block written and part not written.  By
 * tagging each block, we will know which blocks are valid when recovering
 * after an unclean shutdown.
 *
 * This routine is single threaded on the iclog.  No other thread can be in
 * this routine with the same iclog.  Changing contents of iclog can there-
 * fore be done without grabbing the state machine lock.  Updating the global
 * log will require grabbing the lock though.
 *
 * The entire log manager uses a logical block numbering scheme.  Only
 * log_sync (and then only bwrite()) know about the fact that the log may
 * not start with block zero on a given device.  The log block start offset
 * is added immediately before calling bwrite().
 */
void
xlog_sync(xlog_t		*log,
	  xlog_in_core_t	*iclog,
	  uint			flags)
{
	caddr_t		dptr;		/* pointer to byte sized element */
	buf_t		*bp;
	int		i;
	uint		count;		/* byte count of bwrite */
	int		split = 0;	/* split write into two regions */
	
	ASSERT(iclog->ic_refcnt == 0);

	if (flags != 0 && ((flags & XFS_LOG_SYNC) != XFS_LOG_SYNC))
		xlog_panic("xlog_sync: illegal flag");
	
	/* put cycle number in every block */
	for (i = 0,
	     dptr = iclog->ic_data;
	     dptr < iclog->ic_data + iclog->ic_offset;
	     dptr += BBSIZE, i++) {
		iclog->ic_header.h_cycle_data[i] = *(uint *)dptr;
		*(uint *)dptr = log->l_curr_cycle;
	}
	iclog->ic_header.h_len = iclog->ic_offset;	/* real byte length */

	bp	    = iclog->ic_bp;
	bp->b_blkno = BLOCK_LSN(iclog->ic_header.h_lsn);

	/* Round byte count up to a XLOG_BBSIZE chunk */
	count = BBTOB(BTOBB(iclog->ic_offset)) + XLOG_HEADER_SIZE;
	if (bp->b_blkno + BTOBB(count) > log->l_logBBsize) {
		split = count - (BBTOB(log->l_logBBsize - bp->b_blkno));
		count = BBTOB(log->l_logBBsize - bp->b_blkno);
		iclog->ic_bwritecnt = 2;	/* split into 2 writes */
	} else {
		iclog->ic_bwritecnt = 1;
	}
	bp->b_dmaaddr	= (caddr_t) iclog;
	bp->b_bcount	= bp->b_bufsize	= count;
	bp->b_fsprivate	= iclog;		/* save for later */
	if (flags & XFS_LOG_SYNC)
		bp->b_flags |= (B_BUSY | B_HOLD);
	else
		bp->b_flags |= (B_BUSY | B_ASYNC);

	ASSERT(bp->b_blkno <= log->l_logBBsize-1);
	ASSERT(bp->b_blkno + BTOBB(count) <= log->l_logBBsize);

	if (xlog_debug > 1)
		xlog_verify_iclog(log, iclog, count, B_TRUE);

	/* account for log which don't start at block #0 */
	bp->b_blkno += log->l_logBBstart;

	bwrite(bp);

	if (bp->b_flags & B_ERROR == B_ERROR)
		xlog_panic("xlog_sync: buffer error");

	if (split) {
		bp		= iclog->ic_log->l_xbuf;
		bp->b_blkno	= 0;			/* XXXmiken assumes 0 */
		bp->b_bcount	= bp->b_bufsize = split;
		bp->b_dmaaddr	= (caddr_t)((psint)iclog+(psint)count);
		bp->b_fsprivate = iclog;
		if (flags & XFS_LOG_SYNC)
			bp->b_flags |= (B_BUSY | B_HOLD);
		else
			bp->b_flags |= (B_BUSY | B_ASYNC);
		dptr = bp->b_dmaaddr;
		for (i=0; i<split; i += BBSIZE) {
			*dptr = (*(uint *)dptr + 1);
			dptr += BBSIZE;
		}

		ASSERT(bp->b_blkno <= log->l_logBBsize-1);
		ASSERT(bp->b_blkno + BTOBB(count) <= log->l_logBBsize);

		/* account for log which don't start at block #0 */
		bp->b_blkno += log->l_logBBstart;

		bwrite(bp);

		if (bp->b_flags & B_ERROR == B_ERROR)
			xlog_panic("xlog_sync: buffer error");

	}
}	/* xlog_sync */


/*
 * Unallocate a log
 *
 * What to do... sigh.
 */
void
xlog_unalloc(void)
{
}	/* xlog_unalloc */


/*
 * Write some region out to in-core log
 *
 * This will be called when writing externally provided regions or when
 * writing out a commit record for a given transaction.
 *
 * General algorithm:
 *	1. Find total length of this write.  This may include adding to the
 *		lengths passed in.
 *	2. Check whether we violate the tickets reservation.
 *	3. While writing to this iclog
 *	    A. Reserve as much space in this iclog as can get
 *	    B. If this is first write, save away start lsn
 *	    C. While writing this region:
 *		1. If first write of transaction, write start record
 *		2. Write log operation header (header per region)
 *		3. Find out if we can fit entire region into this iclog
 *		4. Potentially, verify destination bcopy ptr
 *		5. Bcopy (partial) region
 *		6. If partial copy, release iclog; otherwise, continue
 *			copying more regions into current iclog
 *	4. Mark want sync bit (in simulation mode)
 *	5. Release iclog for potential flush to on-disk log.
 *		
 * ERRORS:
 * 1.	Panic if reservation is overrun.  This should never happen since
 *	reservation amounts are generated internal to the filesystem.
 * NOTES:
 * 1. Tickets are single threaded data structures.
 * 2. The XLOG_END_TRANS & XLOG_CONTINUE_TRANS flags are passed down to the
 *	syncing routine.  When a single log_write region needs to span
 *	multiple in-core logs, the XLOG_CONTINUE_TRANS bit should be set
 *	on all log operation writes which don't contain the end of the
 *	region.  The XLOG_END_TRANS bit is used for the in-core log
 *	operation which contains the end of the continued log_write region.
 * 3. When xlog_state_get_iclog_space() grabs the rest of the current iclog,
 *	we don't really know exactly how much space will be used.  As a result,
 *	we don't update ic_offset until the end when we know exactly how many
 *	bytes have been written out.
 */
int
xlog_write(xfs_mount_t *	mp,
	   xfs_log_iovec_t	reg[],
	   int			nentries,
	   xfs_log_ticket_t	tic,
	   xfs_lsn_t		*start_lsn,
	   int			commit)
{
    xlog_t	     *log    = mp->m_log;
    xlog_ticket_t    *ticket = (xlog_ticket_t *)tic;
    xlog_op_header_t *logop_head;    /* ptr to log operation header */
    xlog_in_core_t   *iclog;	     /* ptr to current in-core log */
    psint	     ptr;	     /* copy address into data region */
    int		     len;	     /* # xlog_write() bytes 2 still copy */
    int		     index;	     /* region index currently copying */
    int		     log_offset;     /* offset (from 0) into data region */
    int		     start_rec_copy; /* # bytes to copy for start record */
    int		     partial_copy;   /* did we split a region? */
    int		     partial_copy_len;/* # bytes copied if split region */
    int		     need_copy;      /* # bytes need to bcopy this region */
    int		     copy_len;	     /* # bytes actually bcopy'ing */
    int		     copy_off;	     /* # bytes from entry start */
    int		     continuedwr;    /* continued write of in-core log? */
    int		     firstwr = 0;    /* first write of transaction */
#ifdef XFSDEBUG
    static int			this_log_offset, last_log_offset;
#endif
    
    partial_copy_len = partial_copy = 0;

    /* Calculate potential maximum space.  Each region gets its own
     * xlog_op_header_t and may need to be double word aligned.
     */
    len = 0;
    if (ticket->t_flags & XLOG_TIC_INITED)     /* acct for start rec of xact */
	len += sizeof(xlog_op_header_t);
    
    for (index = 0; index < nentries; index++) {
	len += sizeof(xlog_op_header_t);	    /* each region gets >= 1 */
	len += reg[index].i_len;
    }
    *start_lsn = 0;
    
    if (ticket->t_curr_reserv < len)
       xlog_panic("xfs_log_write: reservation ran out. Need to up reservation")
    else if ((ticket->t_flags & XLOG_TIC_PERM_RESERV) == 0)
	ticket->t_curr_reserv -= len;
    
    for (index = 0; index < nentries; ) {
	log_offset = xlog_state_get_iclog_space(log, len, &iclog, &continuedwr);
#ifdef XFSDEBUG
	last_log_offset = this_log_offset;
	this_log_offset = log_offset;
#endif
	ASSERT(log_offset <= iclog->ic_size - 1);
	ptr = (psint) &iclog->ic_data[log_offset];
	
	/* start_lsn is the first lsn written to. That's all we need. */
	if (! *start_lsn)
	    *start_lsn = iclog->ic_header.h_lsn;
	
	/* This loop writes out as many regions as can fit in the amount
	 * of space which was allocated by xlog_state_get_iclog_space().
	 */
	while (index < nentries) {
	    ASSERT(reg[index].i_len % sizeof(long) == 0);
	    ASSERT((psint)ptr % sizeof(long) == 0);
	    start_rec_copy = 0;
	    
	    /* If first write for transaction, insert start record.
	     * We can't be trying to commit if we are inited.  We can't
	     * have any "partial_copy" if we are inited.
	     */
	    if (ticket->t_flags & XLOG_TIC_INITED) {
		logop_head		= (xlog_op_header_t *)ptr;
		logop_head->oh_tid	= ticket->t_tid;
		logop_head->oh_clientid = ticket->t_clientid;
		logop_head->oh_len	= 0;
		logop_head->oh_flags    = XLOG_START_TRANS;
		logop_head->oh_res2	= 0;
		ticket->t_flags		&= ~XLOG_TIC_INITED;	/* clear bit */
		firstwr++;			  /* increment log ops below */
		
		start_rec_copy = sizeof(xlog_op_header_t);
		xlog_write_adv_cnt(ptr, len, log_offset, start_rec_copy);
	    }
	    
	    /* Copy log operation header directly into data section */
	    logop_head			= (xlog_op_header_t *)ptr;
	    logop_head->oh_tid		= ticket->t_tid;
	    logop_head->oh_clientid	= ticket->t_clientid;
	    logop_head->oh_res2		= 0;
	    
	    /* header copied directly */
	    xlog_write_adv_cnt(ptr, len, log_offset, sizeof(xlog_op_header_t));
	    
	    /* are we copying a commit record? */
	    logop_head->oh_flags = (commit ? XLOG_COMMIT_TRANS : 0);
	    
	    /* Partial write last time? => (partial_copy != 0)
	     * need_copy is the amount we'd like to copy if everything could
	     * fit in the current bcopy.
	     */
	    need_copy =	reg[index].i_len - partial_copy_len;

	    copy_off = partial_copy_len;
	    if (need_copy <= iclog->ic_size - log_offset) { /*complete write */
		logop_head->oh_len = copy_len = need_copy;
		if (partial_copy)
		    logop_head->oh_flags|= (XLOG_END_TRANS|XLOG_WAS_CONT_TRANS);
		partial_copy_len = partial_copy = 0;
	    } else { 					    /* partial write */
		copy_len = logop_head->oh_len =	iclog->ic_size - log_offset;
	        logop_head->oh_flags |= XLOG_CONTINUE_TRANS;
		if (partial_copy)
			logop_head->oh_flags |= XLOG_WAS_CONT_TRANS;
		partial_copy_len += copy_len;
		partial_copy++;
		len += sizeof(xlog_op_header_t); /* from splitting of region */
	    }

	    if (xlog_debug > 1) {
		ASSERT(copy_len >= 0);
		xlog_verify_dest_ptr(log, ptr);
	    }

	    /* copy region */
	    bcopy(reg[index].i_addr + copy_off, (caddr_t)ptr, copy_len);
	    xlog_write_adv_cnt(ptr, len, log_offset, copy_len);

	    /* make copy_len total bytes copied, including headers */
	    copy_len += start_rec_copy + sizeof(xlog_op_header_t);
	    xlog_state_finish_copy(log, iclog, firstwr,
				  (continuedwr ? copy_len : 0));

	    firstwr = 0;
	    if (partial_copy) {			/* copied partial region */
		/* already marked WANT_SYNC */
		xlog_state_release_iclog(log, iclog);
		break;				/* don't increment index */
	    } else {				/* copied entire region */
		index++;
		partial_copy_len = partial_copy = 0;

		if (iclog->ic_size - log_offset <= sizeof(xlog_op_header_t)) {
		    xlog_state_want_sync(log, iclog);
		    xlog_state_release_iclog(log, iclog);
		    if (index == nentries)
			    return 0;		/* we are done */
		    else
			    break;
		}
	    } /* if (partial_copy) */
		if (xlog_debug > 1) {
		    iclog->ic_header.h_len = log_offset;
		    xlog_verify_iclog(log, iclog, log_offset, B_FALSE);
	        }
	} /* while (index < nentries) */
    } /* for (index = 0; index < nentries; ) */
    ASSERT(len == 0);
    
#ifndef _KERNEL
    xlog_state_want_sync(log, iclog);   /* not needed for kernel XXXmiken */
#endif
    
    xlog_state_release_iclog(log, iclog);
    return 0;
}	/* xlog_write */


/*****************************************************************************
 *
 *		State Machine functions
 *
 *****************************************************************************
 */

/* Clean iclogs starting from the head.  This ordering must be
 * maintained, so an iclog doesn't become ACTIVE beyond one that
 * is SYNCING.  This is also required to maintain the notion that we use
 * a counting semaphore to hold off would be writers to the log when every
 * iclog is trying to sync to disk.
 *
 * State Change: DIRTY -> ACTIVE
 */
void
xlog_state_clean_log(xlog_t *log)
{
	xlog_in_core_t	*iclog;

	iclog = log->l_iclog;
	do {
		if (iclog->ic_state == XLOG_STATE_DIRTY) {
			iclog->ic_state	= XLOG_STATE_ACTIVE;
			iclog->ic_offset       = 0;
			iclog->ic_callback	= 0;   /* don't need to free */
			iclog->ic_header.h_num_logops = 0;
			bzero(iclog->ic_header.h_cycle_data,
			      sizeof(iclog->ic_header.h_cycle_data));
			iclog->ic_header.h_lsn = 0;
			vsema(&log->l_flushsema);
		} else if (iclog->ic_state == XLOG_STATE_ACTIVE)
			/* do nothing */;
		else
			break;	/* stop cleaning */
		iclog = iclog->ic_next;
	} while (iclog != log->l_iclog);
}	/* xlog_state_clean_log */


static void
xlog_state_do_callback(xlog_t *log)
{
	xlog_in_core_t	   *iclog, *first_iclog;
	xfs_log_callback_t *cb, *cb_next;
	int		   spl;

	spl = splockspl(log->l_icloglock, splhi);
	first_iclog = iclog = log->l_iclog;

	do {
		if (iclog->ic_state != XLOG_STATE_DONE_SYNC) {
			/* XXXmiken: should I skip the first and continue? */
			goto clean;
		}
		iclog->ic_state = XLOG_STATE_CALLBACK;
		spunlockspl(log->l_icloglock, spl);

		for (cb = iclog->ic_callback; cb != 0; cb = cb_next) {
			cb_next = cb->cb_next;
			cb->cb_func(cb->cb_arg);
		}

		spl = splockspl(log->l_icloglock, splhi);
		iclog->ic_state = XLOG_STATE_DIRTY;

		/* wake up threads waiting in xfs_log_force() */
		while (cvsema(&iclog->ic_forcesema));

		iclog = iclog->ic_next;
	} while (first_iclog != iclog);

clean:
	xlog_state_clean_log(log);
	spunlockspl(log->l_icloglock, spl);
}	/* xlog_state_do_callback */


/*
 * Finish transitioning this iclog to the dirty state.
 *
 * Make sure that we completely execute this routine only when this is
 * the last call to the iclog.  There is a good chance that iclog flushes,
 * when we reach the end of the physical log, get turned into 2 separate
 * calls to bwrite.  Hence, one iclog flush could generate two calls to this
 * routine.  By using the reference count bwritecnt, we guarantee that only
 * the second completion goes through.
 *
 * Callbacks could take time, so they are done outside the scope of the
 * global state machine log lock.  Assume that the calls to cvsema won't
 * take a long time.  At least we know it won't sleep.
 */
void
xlog_state_done_syncing(xlog_in_core_t	*iclog)
{
	int		   spl;
	xlog_t		   *log = iclog->ic_log;

	spl = splockspl(log->l_icloglock, splhi);

	ASSERT(iclog->ic_state == XLOG_STATE_SYNCING);
	ASSERT(iclog->ic_refcnt == 0);
	ASSERT(iclog->ic_bwritecnt == 1 || iclog->ic_bwritecnt == 2);

	if (--iclog->ic_bwritecnt == 1) {
		spunlockspl(log->l_icloglock, spl);
		return;
	}

	iclog->ic_state = XLOG_STATE_DONE_SYNC;
	spunlockspl(log->l_icloglock, spl);
	xlog_state_do_callback(log);
}	/* xlog_state_done_syncing */


/*
 * Update counters atomically now that bcopy is done.
 */
void
xlog_state_finish_copy(xlog_t		*log,
		       xlog_in_core_t	*iclog,
		       int		first_write,
		       int		copy_bytes)
{
	int spl;

	spl = splockspl(log->l_icloglock, splhi);

	if (first_write)
		iclog->ic_header.h_num_logops++;
	iclog->ic_header.h_num_logops++;
	iclog->ic_offset += copy_bytes;

	spunlockspl(log->l_icloglock, spl);
}	/* xlog_state_finish_copy */



/*
 * If the head of the in-core log ring is not (ACTIVE or DIRTY), then we must
 * sleep.  The flush semaphore is set to the number of in-core buffers and
 * decremented around disk syncing.  Therefore, if all buffers are syncing,
 * this semaphore will cause new writes to sleep until a sync completes.
 * Otherwise, this code just does p() followed by v().  This approximates
 * a sleep/wakeup except we can't race.
 *
 * The in-core logs are used in a circular fashion. They are not used
 * out-of-order even when an iclog past the head is free.
 *
 * return:
 *	* log_offset where xlog_write() can start writing into the in-core
 *		log's data space.
 *	* in-core log pointer to which xlog_write() should write.
 *	* boolean indicating this is a continued write to an in-core log.
 *		If this is the last write, then the in-core log's offset field
 *		needs to be incremented, depending on the amount of data which
 *		is copied.
 */
int
xlog_state_get_iclog_space(xlog_t	 *log,
			  int		 len,
			  xlog_in_core_t **iclogp,
			  int		 *continued_write)
{
	int		  spl;
	int		  log_offset;
	xlog_rec_header_t *head;
	xlog_in_core_t	  *iclog;

	xlog_state_do_callback(log);

restart:
	spl = splockspl(log->l_icloglock, splhi);

	iclog = log->l_iclog;
	if (! (iclog->ic_state == XLOG_STATE_ACTIVE ||
	       iclog->ic_state == XLOG_STATE_DIRTY) ) {
		spunlockspl(log->l_icloglock, spl);
		psema(&log->l_flushsema, PINOD);
		vsema(&log->l_flushsema);
		goto restart;
	}

#if XXXmiken
	xlog_state_clean_log(log);
#endif
	head = &iclog->ic_header;

	iclog->ic_refcnt++;			/* prevents sync */
	log_offset = iclog->ic_offset;

	/* On the 1st write to an iclog, figure out lsn.  This works
	 * if iclogs marked XLOG_STATE_WANT_SYNC always write out what they are
	 * committing to.  If the offset is set, that's how many blocks
	 * must be written.
	 */
	if (log_offset == 0) {
		head->h_cycle = log->l_curr_cycle;
		ASSIGN_LSN(head->h_lsn, log);
		ASSERT(log->l_curr_block >= 0);
	}

	/* If there is enough room to write everything, then do it.  Otherwise,
	 * claim the rest of the region and make sure the XLOG_STATE_WANT_SYNC
	 * bit is on, so this will get flushed out.  Don't update ic_offset
	 * until you know exactly how many bytes get copied.  Therefore, wait
	 * until later to update ic_offset.
	 *
	 * xlog_write() algorithm assumes that at least 2 xlog_op_header_t's
	 * can fit into remaining data section.
	 */
	if (iclog->ic_size - iclog->ic_offset < 2*sizeof(xlog_op_header_t)) {
		if (iclog->ic_state == XLOG_STATE_ACTIVE)
			xlog_state_switch_iclogs(log, iclog);

		if (iclog->ic_refcnt == 1) {		/* I'm the only one */
			spunlockspl(log->l_icloglock, spl);
			xlog_state_release_iclog(log, iclog); /* decrs refcnt */
		} else {
			iclog->ic_refcnt--;
			spunlockspl(log->l_icloglock, spl);
		}
		goto restart;
	}
	if (len <= iclog->ic_size - iclog->ic_offset) {	/* write full amount */
		iclog->ic_offset += len;
		*continued_write = 0;
	} else {	/* take as much as possible and write rest in next LR */
		*continued_write = 1;
		if (iclog->ic_state == XLOG_STATE_ACTIVE)
			xlog_state_switch_iclogs(log, iclog);
		/* this iclog releases in xlog_write() */
	}
	*iclogp = iclog;

	ASSERT(iclog->ic_offset <= iclog->ic_size);
	spunlockspl(log->l_icloglock, spl);
	return log_offset;
}	/* xlog_state_get_iclog_space */


/*
 * Atomically get ticket.  Usually, don't return until one is available.
 */
xfs_log_ticket_t
xlog_state_get_ticket(xlog_t	*log,
		      int	len,
		      char	log_client,
		      uint	flags)
{
	int		 spl;
	xfs_log_ticket_t tic;

	spl = splockspl(log->l_icloglock, splhi);

	/* Eventually force out buffers */
	if (log->l_logreserved + len > log->l_logsize) {
		if (flags & XFS_LOG_NOSLEEP)
			return (xfs_log_ticket_t)-1;
		else
			xlog_panic("xlog_state_get_ticket: over reserved");
	}
	log->l_logreserved += len;
	tic = xlog_maketicket(log, len, log_client);
	spunlockspl(log->l_icloglock, spl);

	return tic;

}	/* xlog_state_get_ticket */


/*
 * If the lsn is not found or the iclog with the lsn is in the callback
 * state, we need to call the function directly.  This is done outside
 * this function's scope.  Otherwise, we insert the callback at the end
 * of the iclog's callback list.
 */
int
xlog_state_lsn_is_synced(xlog_t		    *log,
			 xfs_lsn_t	    lsn,
			 xfs_log_callback_t *cb)
{
	xlog_in_core_t *iclog;
	int	      spl;
	int	      lsn_is_synced = 1;

	spl = splockspl(log->l_icloglock, splhi);

	iclog = log->l_iclog;
	do {
		if (iclog->ic_header.h_lsn != lsn) {
			iclog = iclog->ic_next;
			continue;
		} else {
			if ((iclog->ic_state == XLOG_STATE_CALLBACK) ||
			    (iclog->ic_state == XLOG_STATE_DIRTY)) /* call it*/
				break;

			/* insert callback into list */
#if 0
			cb->cb_next = iclog->ic_callback;
			iclog->ic_callback = cb;
#endif
			cb->cb_next = 0;
			*(iclog->ic_callback_tail) = cb;
			iclog->ic_callback_tail = &(cb->cb_next);
			lsn_is_synced = 0;
			break;
		}
	} while (iclog != log->l_iclog);

	spunlockspl(log->l_icloglock, spl);
	return lsn_is_synced;
}	/* xlog_state_lsn_is_synced */


/*
 * Atomically put back used ticket.
 */
void
xlog_state_put_ticket(xlog_t	    *log,
		      xlog_ticket_t *tic)
{
	int	spl;

	spl = splockspl(log->l_icloglock, splhi);

#ifdef DEBUG
	bytes_of_ticket_used = tic->t_orig_reserv - tic->t_curr_reserv;
#endif

	log->l_logreserved -= tic->t_orig_reserv;
	xlog_putticket(log, tic);

	spunlockspl(log->l_icloglock, spl);
}	/* xlog_state_reserve_space */


/*
 * Flush iclog to disk if this is the last reference to the given iclog and
 * the WANT_SYNC bit is set.
 *
 * When this function is entered, the iclog is not necessarily in the
 * WANT_SYNC state.  It may be sitting around waiting to get filled.
 *
 * 
 */
void
xlog_state_release_iclog(xlog_t		*log,
			 xlog_in_core_t	*iclog)
{
    int		spl;
    int		sync = 0;	/* do we sync? */
    xfs_lsn_t	tail_lsn;
    int		blocks;
    
    spl = splockspl(log->l_icloglock, splhi);
    
    ASSERT(iclog->ic_refcnt > 0);
    
    if (--iclog->ic_refcnt == 0 && iclog->ic_state == XLOG_STATE_WANT_SYNC) {
	sync++;
	iclog->ic_state = XLOG_STATE_SYNCING;
	
	if ((tail_lsn = xfs_trans_tail_ail(log->l_mp)) == 0)
	    tail_lsn = iclog->ic_header.h_tail_lsn = log->l_tail_lsn;
	else
	    iclog->ic_header.h_tail_lsn = log->l_tail_lsn = tail_lsn;

	/* check if it will fit */
	if (xlog_debug > 1) {
	    if (CYCLE_LSN(tail_lsn) == log->l_prev_cycle) {
		blocks =
		    log->l_logBBsize - (log->l_prev_block -BLOCK_LSN(tail_lsn));
		if (blocks < BTOBB(iclog->ic_offset)+1)
		    xlog_panic("ran out of log space");
	    } else {
		ASSERT(CYCLE_LSN(tail_lsn)+1 == log->l_prev_cycle);
		if (BLOCK_LSN(tail_lsn) == log->l_prev_block)
		    xlog_panic("ran out of log space");
		
		blocks = BLOCK_LSN(tail_lsn) - log->l_prev_block;
		if (blocks < BTOBB(iclog->ic_offset) + 1)
		    xlog_panic("ran out of log space");
	    }
	}

	/* cycle incremented when incrementing curr_block */
    }
    
    spunlockspl(log->l_icloglock, spl);
    
    if (sync)
	xlog_sync(log, iclog, 0);
    
}	/* xlog_state_release_iclog */


static void
xlog_state_switch_iclogs(xlog_t		*log,
			 xlog_in_core_t *iclog)
{
	iclog->ic_state = XLOG_STATE_WANT_SYNC;
	iclog->ic_header.h_prev_block = log->l_prev_block;
	log->l_prev_block = log->l_curr_block;
	log->l_prev_cycle = log->l_curr_cycle;
	
	/* roll log?: ic_offset changed later */
	log->l_curr_block += BTOBB(iclog->ic_offset)+1;
	if (log->l_curr_block >= log->l_logBBsize) {
		log->l_curr_cycle++;
		log->l_curr_block -= log->l_logBBsize;
		ASSERT(log->l_curr_block >= 0);
	}
	log->l_iclog = iclog->ic_next;
	psema(&log->l_flushsema, PINOD);
}	/* xlog_state_switch_iclogs */


int
xlog_state_sync_all(xlog_t *log, uint flags)
{
	xlog_in_core_t *iclog;
	int spl;

	spl = splockspl(log->l_icloglock, splhi);

	iclog = log->l_iclog;
	do {
			iclog = iclog->ic_next;
		    if (iclog->ic_state == XLOG_STATE_ACTIVE) {
			    xlog_state_switch_iclogs(log, iclog);
		    } else if (iclog->ic_state == XLOG_STATE_DIRTY) {
			    spunlockspl(log->l_icloglock, spl);
			    return 0;
		    }
		    if (flags & XFS_LOG_SYNC)		/* sleep */
			    spunlockspl_psema(log->l_icloglock, spl,
					      &iclog->ic_forcesema, 0);
		    else				/* just return */
			    spunlockspl(log->l_icloglock, spl);
		    return 0;
	} while (iclog != log->l_iclog);

	spunlockspl(log->l_icloglock, spl);
	return XFS_ENOTFOUND;
}	/* xlog_state_sync_all */


/*
 * Used by code which implements synchronous log forces.
 *
 * Find in-core log with lsn.
 *	If it is in the DIRTY state, just return.
 *	If it is in the ACTIVE state, move the in-core log into the WANT_SYNC
 *		state and go to sleep or return.
 *	If it is in any other state, go to sleep or return.
 *
 * XXXmiken: if filesystem activity goes to zero, the iclog may never get
 *	pushed out.
 */
int
xlog_state_sync(xlog_t	  *log,
		xfs_lsn_t lsn,
		uint	  flags)
{
	xlog_in_core_t	*iclog;
	int		spl;

	if (lsn == 0)
		return xlog_state_sync_all(log, flags);

	spl = splockspl(log->l_icloglock, splhi);

	iclog = log->l_iclog;
	do {
		if (iclog->ic_header.h_lsn != lsn) {
			iclog = iclog->ic_next;
		} else {
		    if (iclog->ic_state == XLOG_STATE_ACTIVE) {
			    xlog_state_switch_iclogs(log, iclog);
		    } else if (iclog->ic_state == XLOG_STATE_DIRTY) {
			    spunlockspl(log->l_icloglock, spl);
			    return 0;
		    }
		    if (flags & XFS_LOG_SYNC)		/* sleep */
			    spunlockspl_psema(log->l_icloglock, spl,
					      &iclog->ic_forcesema, 0);
		    else				/* just return */
			    spunlockspl(log->l_icloglock, spl);
		    return 0;
		}
	} while (iclog != log->l_iclog);

	spunlockspl(log->l_icloglock, spl);
	return XFS_ENOTFOUND;
}	/* xlog_state_sync */


/*
 * Called when we want to mark the current iclog as being ready to sync to
 * disk.
 */
void
xlog_state_want_sync(xlog_t *log, xlog_in_core_t *iclog)
{
	int spl;
	
	spl = splockspl(log->l_icloglock, splhi);
	
	if (iclog->ic_state == XLOG_STATE_ACTIVE)
		xlog_state_switch_iclogs(log, iclog);
	else if (iclog->ic_state != XLOG_STATE_WANT_SYNC)
		xlog_panic("xlog_state_want_sync: bad state");
	
	spunlockspl(log->l_icloglock, spl);
}	/* xlog_state_want_sync */



/*****************************************************************************
 *
 *		TICKET functions
 *
 *****************************************************************************
 */

/*
 *	Algorithm doesn't take into account page size. ;-(
 */
static void
xlog_alloc_tickets(xlog_t *log)
{
	caddr_t buf;
	xlog_ticket_t *t_list;
	uint i = (4096 / sizeof(xlog_ticket_t))-1;	/* XXXmiken */

	/*
	 * XXXmiken: may want to account for differing sizes of pointers
	 * or allocate one page at a time.
	 */
	buf = (caddr_t) kmem_zalloc(4096, 0);

	t_list = log->l_freelist = (xlog_ticket_t *)buf;
	for ( ; i > 0; i--) {
		t_list->t_next = t_list+1;
		t_list = t_list->t_next;
	}
	t_list->t_next = 0;
	log->l_tail = t_list;

}	/* xlog_alloc_tickets */


/*
 * Put ticket into free list
 */
static void
xlog_putticket(xlog_t		*log,
	       xlog_ticket_t	*ticket)
{
	xlog_ticket_t *t_list;

#ifndef DEBUG
	/* real code will want to use LIFO for caching */
	ticket->t_next = log->l_freelist;
	log->l_freelist = ticket;
	/* no need to clear fields */
#else
	/* When we debug, it is easier if tickets are cycled */
	ticket->t_next = 0;
	log->l_tail->t_next = ticket;
	log->l_tail = ticket;
#endif
}	/* xlog_putticket */


/*
 * Grab ticket off freelist or allocation some more
 */
xfs_log_ticket_t *
xlog_maketicket(xlog_t		*log,
		int		len,
		char		log_clientid)
{
	xlog_ticket_t *tic;

	if (log->l_freelist == NULL)
		xlog_alloc_tickets(log);

	tic			= log->l_freelist;
	log->l_freelist		= tic->t_next;
	tic->t_orig_reserv	= tic->t_curr_reserv = len;
	tic->t_tid		= (xlog_tid_t)tic;
	tic->t_clientid		= log_clientid;
	tic->t_flags		= XLOG_TIC_INITED;

	return (xfs_log_ticket_t)tic;
}	/* xlog_maketicket */


/******************************************************************************
 *
 *		Log recover routines
 *
 ******************************************************************************
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
	uint		    r_state;		/* not needed */
	xlog_recover_item_t *r_transq;
} xlog_recover_t;


#define XLOG_RHASH_BITS  4
#define XLOG_RHASH_SIZE	16
#define XLOG_RHASH_SHIFT 2
#define XLOG_RHASH(tid)	\
	((((uint)tid)>>XLOG_RHASH_SHIFT) & (XLOG_RHASH_SIZE-1<<XLOG_RHASH_SHIFT))

static xlog_recover_t *
xlog_recover_find_tid(xlog_recover_t *q,
		      xlog_tid_t     tid)
{
	xlog_recover_t *p = q;

	while (p != NULL) {
		if (p->r_tid != tid)
			p = p->r_next;
	}
	return p;
}	/* xlog_recover_find_tid */


static void
xlog_recover_put_hashq(xlog_recover_t **q,
		       xlog_recover_t *trans)
{
	trans->r_next = *q;
	*q = trans;
}	/* xlog_recover_put_hashq */


static void
xlog_recover_add_item(xlog_recover_item_t **ihead)
{
	xlog_recover_item_t	*item;

	item = kmem_zalloc(sizeof(xlog_recover_item_t),0);
	if (*ihead == 0) {
		item->ri_prev = item->ri_next = item;
		*ihead = item;
		
	} else {
		item->ri_next		= *ihead;
		item->ri_prev		= (*ihead)->ri_prev;
		(*ihead)->ri_prev		= item;
		item->ri_prev->ri_next	= item;
	}
}	/* xlog_recover_add_item */


static void
xlog_recover_add_to_cont_trans(xlog_recover_t	*trans,
			       caddr_t		dp,
			       int		len)
{
	xlog_recover_item_t	*item;
	caddr_t			ptr, old_ptr;
	int			old_len;
	
	item = trans->r_transq;
	item = item->ri_prev;

	if (item->ri_buf2_len != 0) {
		old_ptr = item->ri_buf2;
		old_len = item->ri_buf2_len;
	} else if (item->ri_buf1_len != 0) {
		old_ptr = item->ri_buf1;
		old_len = item->ri_buf1_len;
	} else {
		old_ptr = item->ri_desc;
		old_len = item->ri_desc_len;
	}

	ptr = kmem_realloc(old_ptr, len, 0);
	bcopy(&ptr[old_len], dp, len);
}	/* xlog_recover_add_to_cont_trans */


static void
xlog_recover_add_to_trans(xlog_recover_t	*trans,
			  caddr_t		dp,
			  int			len)
{
	xfs_inode_log_format_t	 *in_f;			/* any will do */
	xlog_recover_item_t	 *item;
	caddr_t			 ptr;
	int			 total;

	ptr = kmem_alloc(len, 0);
	bcopy(ptr, dp, len);
	
	in_f = (xfs_inode_log_format_t *)ptr;
	item = trans->r_transq;
	if (item == 0 ||
	    (item->ri_prev->ri_total != 0 &&
	     item->ri_prev->ri_total == item->ri_prev->ri_cnt)) {
		xlog_recover_add_item(&trans->r_transq);
	}
	item = item->ri_prev;

	if (item->ri_total == 0) {
		item->ri_total	= in_f->ilf_size;
		item->ri_cnt	= 1;
		item->ri_desc	= ptr;
		item->ri_desc_len = len;
	} else {
		ASSERT(item->ri_total > item->ri_cnt);
		if (item->ri_cnt == 1) {
			item->ri_buf1	  = ptr;
			item->ri_buf1_len = len;
		} else {
			item->ri_buf2	  = ptr;
			item->ri_buf2_len = len;
		}
		item->ri_cnt++;
	}
}	/* xlog_recover_add_to_trans */


static void
xlog_recover_new_tid(xlog_recover_t	**q,
		     xlog_tid_t	tid)
{
	xlog_recover_t	*trans;

	trans = kmem_alloc(sizeof(xlog_recover_t), 0);
	trans->r_next  = 0;
	trans->r_tid   = tid;
	trans->r_type  = 0;
	trans->r_state = 0;
	trans->r_transq = 0;
	xlog_recover_put_hashq(q, trans);
}	/* xlog_recover_new_tid */


/*
 * There are two valid states of the r_state field.  0 indicates that the
 * transaction structure is in a normal state.  We have either seen the
 * start of the transaction or the last operation we added was not a partial
 * operation.  If the last operation we added to the transaction was a 
 * partial operation, we need to mark r_state with LOG_CONTINUE_TRANS.
 */
static void
xlog_recover_process_data(xlog_recover_t    *rhash[],
			  xlog_rec_header_t *rhead,
			  caddr_t	    dp)
{
    caddr_t		lp = dp+rhead->h_len;
    int			num_logops = rhead->h_num_logops;
    xlog_op_header_t	*ohead;
    xlog_recover_t	*trans;
    xlog_tid_t		tid;
    int			hash;

    while (dp < lp) {
	ASSERT(dp + sizeof(xlog_op_header_t) <= lp);
	ohead = (xlog_op_header_t *)dp;
	dp += sizeof(xlog_op_header_t);
	if (ohead->oh_clientid != XFS_TRANSACTION)
	    xlog_panic("xlog_recover_process_data: bad clientid ");
	tid = ohead->oh_tid;
	hash = XLOG_RHASH(tid);
	trans = xlog_recover_find_tid(rhash[hash], tid);
	if (trans == NULL) {
	    if ((ohead->oh_flags & XLOG_SKIP_TRANS) == 0)
		xlog_recover_new_tid(&rhash[hash], tid);
	} else {
	    ASSERT(dp+ohead->oh_len <= lp);
	    if ((ohead->oh_flags &XLOG_WAS_CONT_TRANS) != XLOG_WAS_CONT_TRANS) {
		xlog_recover_add_to_trans(trans, dp, ohead->oh_len);
	    } else {
		xlog_recover_add_to_cont_trans(trans, dp, ohead->oh_len);
	    }
	}
	dp += ohead->oh_len;
    }
}	/* xlog_recover_process_data */


static void
xlog_unpack_data(xlog_rec_header_t *rhead,
		 caddr_t	   dp)
{
	int i;

	for (i=0; i<BTOBB(rhead->h_len); i++) {
		*(uint *)dp = *(uint *)&rhead->h_cycle_data[i];
		dp += BBSIZE;
	}
}	/* xlog_unpack_data */


int
xlog_do_recover(xlog_t	*log,
		daddr_t head_blk,
		daddr_t tail_blk)
{
	xlog_rec_header_t *rhead;
	daddr_t		  blk_no;
	buf_t		  *hbp, *dbp;
	int		  bblks;
	xlog_recover_t	  *rhash[XLOG_RHASH_SIZE];

	hbp = xlog_get_bp(1);
	dbp = xlog_get_bp(BTOBB(XLOG_RECORD_BSIZE));
	bzero(rhash, sizeof(rhash));
	if (tail_blk <= head_blk) {
		for (blk_no = tail_blk; blk_no < head_blk; ) {
			xlog_bread(log, blk_no, 1, hbp);
			rhead = (xlog_rec_header_t *)hbp->b_dmaaddr;
			ASSERT(rhead->h_magicno == XLOG_HEADER_MAGIC_NUM);
			bblks = BTOBB(rhead->h_len);
			xlog_bread(log, blk_no+1, bblks, dbp);
			xlog_unpack_data(rhead, dbp->b_dmaaddr);
			xlog_recover_process_data(rhash, rhead, dbp->b_dmaaddr);
			blk_no += (bblks+1);
		}
	} else {
	}
	}	/* xlog_do_recover */


int
xlog_recover(xlog_t *log)
{
	daddr_t head_blk, tail_blk;

	tail_blk = xlog_find_tail(log, &head_blk);
	if (tail_blk != head_blk) {
		/* do it */
	}
	return 0;
}


/******************************************************************************
 *
 *		Log debug routines
 *
 ******************************************************************************
 */

/*
 * Make sure that the destination ptr is within the valid data region of
 * one of the iclogs.  This uses backup pointers stored in a different
 * part of the log in case we trash the log structure.
 */
void
xlog_verify_dest_ptr(xlog_t *log,
		     psint  ptr)
{
	int i;
	int good_ptr = 0;

	for (i=0; i < XLOG_NUM_ICLOGS; i++) {
		if (ptr >= (psint)log->l_iclog_bak[i] &&
		    ptr <= (psint)log->l_iclog_bak[i]+log->l_iclog_size)
			good_ptr++;
	}
	if (! good_ptr)
		xlog_panic("xlog_verify_dest_ptr: invalid ptr");
}	/* xlog_verify_dest_ptr */


/*
 * Perform a number of checks on the iclog before writing to disk.
 *
 * 1. Make sure the iclogs are still circular
 * 2. Make sure we have a good magic number
 * 3. Make sure we don't have magic numbers in the data
 * 4. Check fields of each log operation header for:
 *	A. Valid client identifier
 *	B. tid ptr value falls in valid ptr space (user space code)
 *	C. Length in log record header is correct according to the
 *		individual operation headers within record.
 * 5. When a bwrite will occur within 5 blocks of the front of the physical
 *	log, check the preceding blocks of the physical log to make sure all
 *	the cycle numbers agree with the current cycle number.
 */
void
xlog_verify_iclog(xlog_t	 *log,
		  xlog_in_core_t *iclog,
		  int		 count,
		  boolean_t	 syncing)
{
	xlog_op_header_t  *ophead;
	xlog_rec_header_t *rec;
	xlog_in_core_t	 *icptr;
	xlog_tid_t	 tid;
	caddr_t		 ptr;
	char		 clientid;
	char		 buf[XLOG_HEADER_SIZE];
	int		 len;
	int		 fd;
	int		 i;
	int		 op_len;
	int		 cycle_no;

	/* check validity of iclog pointers */
	icptr = log->l_iclog;
	for (i=0; i < XLOG_NUM_ICLOGS; i++) {
		if (icptr == 0)
			xlog_panic("xlog_verify_iclog: illegal ptr");
		icptr = icptr->ic_next;
	}
	if (icptr != log->l_iclog)
		xlog_panic("xlog_verify_iclog: corrupt iclog ring");

	/* check log magic numbers */
	ptr = (caddr_t) iclog;
	if (*(uint *)ptr != XLOG_HEADER_MAGIC_NUM)
		xlog_panic("xlog_verify_iclog: illegal magic num");
	
	for (ptr += BBSIZE; ptr < (caddr_t)iclog+count; ptr += BBSIZE) {
		if (*(uint *)ptr == XLOG_HEADER_MAGIC_NUM)
			xlog_panic("xlog_verify_iclog: unexpected magic num");
	}
	
	/* check fields */
	len = iclog->ic_header.h_len;
	ptr = iclog->ic_data;
	ophead = (xlog_op_header_t *)ptr;
	for (i=0; i<iclog->ic_header.h_num_logops; i++) {
		ophead = (xlog_op_header_t *)ptr;

		/* clientid is only 1 byte */
		if (syncing == B_FALSE ||
		    ((psint)&ophead->oh_clientid & 0x1ff) != 0)
			clientid = ophead->oh_clientid;
		else
			clientid = iclog->ic_header.h_cycle_data[BTOBB(&ophead->oh_clientid - iclog->ic_data)]>>24;
		if (clientid != XFS_TRANSACTION)
			xlog_panic("xlog_verify_iclog: illegal client");

		/* check tids */
		if (syncing == B_FALSE ||
		    ((psint)&ophead->oh_tid & 0x1ff) != 0)
			tid = ophead->oh_tid;
		else
			tid = (xlog_tid_t)iclog->ic_header.h_cycle_data[BTOBB((psint)&ophead->oh_tid - (psint)iclog->ic_data)];

#ifndef _KERNEL
		/* This is a user space check */
		if ((psint)tid < 0x10000000 || (psint)tid > 0x20000000)
			xlog_panic("xlog_verify_iclog: illegal tid");
#endif

		/* check length */
		if (syncing == B_FALSE ||
		    ((psint)&ophead->oh_len & 0x1ff) != 0)
			op_len = ophead->oh_len;
		else
			op_len = iclog->ic_header.h_cycle_data[BTOBB((psint)&ophead->oh_len - (psint)iclog->ic_data)];
		len -= sizeof(xlog_op_header_t) + op_len;
		ptr += sizeof(xlog_op_header_t) + op_len;
	}
	if (len != 0)
		xlog_panic("xlog_verify_iclog: illegal iclog");

#ifndef _KERNEL
	/* check wrapping log */
	if (BLOCK_LSN(iclog->ic_header.h_lsn) < 5) {
		cycle_no = CYCLE_LSN(iclog->ic_header.h_lsn);
		fd = bmajor(log->l_dev);
		if (lseek(fd, 0, SEEK_SET) < 0)
			xlog_panic("xlog_verify_iclog: lseek 0 failed");
		for (i = 0; i < BLOCK_LSN(iclog->ic_header.h_lsn); i++) {
			if (read(fd, buf, XLOG_HEADER_SIZE) == 0)
				xlog_panic("xlog_verify_iclog: bad read");
			rec = (xlog_rec_header_t *)buf;
			if (rec->h_magicno == XLOG_HEADER_MAGIC_NUM &&
			    CYCLE_LSN(rec->h_lsn) < cycle_no)
				xlog_panic("xlog_verify_iclog: bad cycle no");
		}
	}
#endif
}	/* xlog_verify_iclog */
