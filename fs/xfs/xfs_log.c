#ident	"$Revision: 1.37 $"

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
#include "xfs_log_priv.h"	/* depends on all above */

#ifdef SIM
#include "sim.h"		/* must be last include file */
#endif


#define log_write_adv_cnt(ptr, len, off, bytes) \
	{ (ptr) += (bytes); \
	  (len) -= (bytes); \
	  (off) += (bytes);}


/* Local miscellaneous function prototypes */
STATIC void	 log_alloc(xfs_mount_t *mp, dev_t log_dev, int start_block,
			   int num_bblocks);
STATIC xfs_lsn_t log_commit_record(xfs_mount_t *mp, log_ticket_t *ticket);
STATIC int	 log_find_end(dev_t log_dev, int log_bbnum);
int	 log_find_start(dev_t log_dev, int log_bbnum);
STATIC void	 log_push_buffers_to_disk(xfs_mount_t *mp, log_t *log);
STATIC void	 log_sync(log_t *log, log_in_core_t *iclog, uint flags);
STATIC void	 log_unalloc(void);
STATIC int	 log_write(xfs_mount_t *mp, xfs_log_iovec_t region[],
			   int nentries, xfs_log_ticket_t tic,
			   xfs_lsn_t *start_lsn, int commit);

/* local state machine functions */
STATIC void		log_state_done_syncing(log_in_core_t *iclog);
STATIC void		log_state_finish_copy(log_t *log, log_in_core_t *iclog,
					      int first_write, int bytes);
STATIC int		log_state_get_iclog_space(log_t *log, int len,
						  log_in_core_t **iclog,
						  int *continued_write);
STATIC xfs_log_ticket_t log_state_get_ticket(log_t *log, int len,
					     char log_client, uint flags);
STATIC int		log_state_lsn_is_synced(log_t *log, xfs_lsn_t lsn,
						xfs_log_callback_t *cb);
STATIC void		log_state_put_ticket(log_t *log, log_ticket_t *tic);
STATIC void		log_state_release_iclog(log_t *log,
						log_in_core_t *iclog);
STATIC int		log_state_sync(log_t *log, xfs_lsn_t lsn, uint flags);
STATIC void		log_state_want_sync(log_t *log, log_in_core_t *iclog);

/* local ticket functions */
STATIC xfs_log_ticket_t *log_maketicket(log_t *log, int len, char clientid);
STATIC void		log_alloc_tickets(log_t *log);
STATIC void		log_putticket(log_t *log, log_ticket_t *ticket);
STATIC void		log_relticket(log_ticket_t *ticket);

STATIC int	log_recover(struct xfs_mount *mp, dev_t log_dev);

STATIC void	log_verify_dest_ptr(log_t *log, psint ptr);
STATIC void	log_verify_iclog(log_t *log, log_in_core_t *iclog, int count);


/*
 * 0 => disable log manager
 * 1 => enable log manager
 * 2 => enable log manager and log debugging
 */
int log_debug = 0;


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
 * no data.  The flag LOG_TIC_INITED is set when the first write occurs with
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
	log_t		*log    = mp->m_log;
	log_ticket_t	*ticket = (xfs_log_ticket_t) tic;
	xfs_lsn_t	lsn;
	
	if (! log_debug)
		return 0;

	/* If nothing was ever written, don't write out commit record */
	if ((ticket->t_flags & LOG_TIC_INITED) == 0)
		lsn = log_commit_record(mp, ticket);

	/* Release ticket if not permanent reservation or a specifc
	 * request has been made to release a permanent reservation.
	 */
	if ((ticket->t_flags & LOG_TIC_PERM_RESERV) == 0 ||
	    (flags & XFS_LOG_REL_PERM_RESERV))
		log_state_put_ticket(log, ticket);

	/* If this ticket was a permanent reservation and we aren't
	 * trying to release it, reset the inited flags; so next time
	 * we write, a start record will be written out.
	 */
	if ((ticket->t_flags & LOG_TIC_PERM_RESERV) &&
	    (flags & XFS_LOG_REL_PERM_RESERV) == 0)
		ticket->t_flags |= LOG_TIC_INITED;

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
	log_t		*log = mp->m_log;
	
	if (flags & XFS_LOG_FORCE) {
		return(log_state_sync(log, lsn, flags));
	} else if (flags & XFS_LOG_URGE) {
		log_panic("xfs_log_force: not yet implemented");
		return -1;
	} else
		log_panic("xfs_log_force: illegal flags");
	
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
	log_t *log = mp->m_log;
	
	cb->cb_next = 0;
	if (log_state_lsn_is_synced(log, lsn, cb))
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
	log_t *log = mp->m_log;
	
	if (! log_debug)
		return 0;

	if (client != XFS_TRANSACTION)
		return -1;
	
	if (flags & XFS_LOG_SLEEP) {
		log_panic("xfs_log_reserve: not implemented");
		return XFS_ENOTSUP;
	}
	
	/*
	 * Permanent reservations always have at least two active log
	 * operations in the log.  Other reservations may need one log
	 * record header for each part of an operation which falls in
	 * a different log record.  This is a gross over estimate.
	 */
	if (flags & XFS_LOG_PERM_RESERV)
		len += 2*LOG_HEADER_SIZE;
	else {
		len += LOG_HEADER_SIZE *
			((len+LOG_RECORD_BSIZE-1) >> LOG_RECORD_BSHIFT);
	}

	log->l_sync_lsn = xfs_trans_tail_ail(mp);
	log_push_buffers_to_disk(mp, log);

	if ((int)(*ticket =log_state_get_ticket(log, len, client, flags)) == -1)
		return XFS_ENOLOGSPACE;

	if (flags & XFS_LOG_PERM_RESERV)
		((log_ticket_t *)ticket)->t_flags |= LOG_TIC_PERM_RESERV;

	return 0;
}	/* xfs_log_reserve */


/*
 * Mount a log filesystem.
 *
 * mp		- ubiquitous xfs mount point structure
 * log_dev	- device number of on-disk log device
 * start_block	- Start block # where block size is 512 bytes (BBSIZE)
 * num_bblocks	- Number of BBSIZE blocks in on-disk log
 * flags	-
 *
 */
int
xfs_log_mount(xfs_mount_t	*mp,
	      dev_t		log_dev,
	      int		start_block,
	      int		num_bblocks,
	      uint		flags)
{
	log_t *log;
	
	if (! log_debug)
		return 0;

	if (log_recover(mp, log_dev) != 0) {
		return XFS_ERECOVER;
	}
	log_alloc(mp, log_dev, start_block, num_bblocks);
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
	log_unalloc();

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
	if (! log_debug) {
		*start_lsn = 0;
		return 0;
	}

	log_write(mp, reg, nentries, tic, start_lsn, 0);
}	/* xfs_log_write */


/******************************************************************************
 *
 *	local routines
 *
 ******************************************************************************
 */


/*
 * Allocate a log
 *
 * Perform all the specific initialization required during a log mount.
 */
void
log_alloc(xfs_mount_t	*mp,
	  dev_t		log_dev,
	  int		start_block,
	  int		num_bblocks)
{
	log_t			*log;
	log_rec_header_t	*head;
	log_in_core_t		**iclogp;
	log_in_core_t		*iclog;
	int i;
	caddr_t			unaligned;
	
	/* LOG_RECORD_BSIZE must be multiple of BBSIZE; see log_rec_header_t */
	ASSERT((LOG_RECORD_BSIZE & BBMASK) == 0);

	log = mp->m_log = (void *)kmem_zalloc(sizeof(log_t), 0);
	log_alloc_tickets(log);
	
	log->l_mp	   = mp;
	log->l_dev	   = log_dev;
/*	log->l_logreserved = 0; done with kmem_zalloc()*/
	log->l_prev_block  = -1;
	log->l_sync_lsn    = 0x100000000LL;  /* cycle = 1; current block = 0 */
	log->l_curr_cycle  = 1;	      /* 0 is bad since this is initial value */
	log->l_xbuf	   = getrbuf(0);	/* get my locked buffer */
	log->l_curr_block  = log_find_end(log_dev, num_bblocks);
	ASSERT(log->l_xbuf->b_flags & B_BUSY);
	ASSERT(valusema(&log->l_xbuf->b_lock) <= 0);
	initnlock(&log->l_icloglock, "iclog");
	initnsema(&log->l_flushsema, LOG_NUM_ICLOGS, "iclog-flush");

	log->l_logsize     = BBTOB(num_bblocks);
	log->l_logBBstart  = start_block;
	log->l_logBBsize   = BTOBB(log->l_logsize);
	iclogp = &log->l_iclog;
	for (i=0; i < LOG_NUM_ICLOGS; i++) {
		unaligned = kmem_zalloc(sizeof(log_in_core_t)+4096, 0);
		*iclogp =(log_in_core_t *)(((psint)unaligned+4095) & ~0xfff);
		iclog = *iclogp;
		log->l_iclog_bak[i] = iclog;

		head = &iclog->ic_header;
		head->h_magicno = LOG_HEADER_MAGIC_NUM;
		head->h_version = 1;
/*		head->h_lsn = 0;*/
/*		head->h_sync_lsn = 0;*/

/* XXXmiken: Need to make the size of an iclog at least 2x the size of
 *		a filesystem block.  This means some code will not be
 *		compilable.  Additional fields may be needed to precompute
 *		values.
 */
		iclog->ic_size = LOG_RECORD_BSIZE-LOG_HEADER_SIZE;
		iclog->ic_state = LOG_STATE_ACTIVE;
		iclog->ic_log = log;
/*		iclog->ic_refcnt = 0;	*/
/*		iclog->ic_callback = 0;	*/
		iclog->ic_bp = getrbuf(0);		/* my locked buffer */
		ASSERT(iclog->ic_bp->b_flags & B_BUSY);
		ASSERT(valusema(&iclog->ic_bp->b_lock) <= 0);
		initnsema(&iclog->ic_forcesema, 0, "iclog-force");

		iclogp = &iclog->ic_next;
	}
	log->l_iclog_bak[i] = 0;
	log->l_iclog_size = LOG_RECORD_BSIZE;
	*iclogp = log->l_iclog;		/* complete ring */
}	/* log_alloc */


/*
 * Write out the commit record of a transaction associated with the given
 * ticket.  Return the lsn of the commit record.
 */
xfs_lsn_t
log_commit_record(xfs_mount_t  *mp,
		  log_ticket_t *ticket)
{
	int		error;
	xfs_log_iovec_t	reg[1];
	xfs_lsn_t	commit_lsn;
	
	reg[0].i_addr = 0;
	reg[0].i_len = 0;

	error = log_write(mp, reg, 1, ticket, &commit_lsn, 1);
	if (error)
		log_panic("log_commit_record: Can't commit transaction");

	return commit_lsn;
}	/* log_commit_record */


/*
 * Code needs to look at cycle # at start of block  XXXmiken
 */
int
log_find_start(dev_t log_dev, int log_bbnum)
{
    log_rec_header_t	*head;
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
	    log_panic("log_find_start");
	}
	head = (log_rec_header_t *)bp->b_dmaaddr;
	if (head->h_magicno != LOG_HEADER_MAGIC_NUM) {
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

    return block_start;
}	/* log_find_start */


/*
 *
 */
int
log_find_end(dev_t log_dev, int log_bbnum)
{
	log_rec_header_t  *head;		/* ptr to log record header */
	int		  log_rec_block_no = 0;	/* last start of log record */
	int		  block_no = 0;		/* current block number */
	int		  cycle_no = 0;		/* current cycle number */
	int		  start_block = 0;	/* block to start writing at */
	int		  log_rec_bblocks;	/* num of bblocks in log rec */
	int		  skip_first_part_of_log;
	buf_t		  *bp;
	int		  i;
	int		  *int_ptr;
    
	skip_first_part_of_log = 1;
	while (block_no < log_bbnum) {
		bp = bread(log_dev, block_no, 1);
		if (bp->b_flags & B_ERROR) {
			brelse(bp);
			log_panic("log_find_end");
		}
		head = (log_rec_header_t *)bp->b_dmaaddr;
		if (head->h_magicno != LOG_HEADER_MAGIC_NUM) {
			if (skip_first_part_of_log) {
				block_no++;
				brelse(bp);
				continue;
			} else {
				goto end;
			}
		}
		skip_first_part_of_log = 0;
		
		if (cycle_no == 0) {
			cycle_no	 = CYCLE_LSN(head->h_lsn);
			start_block	 = log_rec_block_no = block_no;
			log_rec_bblocks  = BTOBB(head->h_len);
		} else if (CYCLE_LSN(head->h_lsn) < cycle_no) {
			cycle_no	 = CYCLE_LSN(head->h_lsn);
			log_rec_block_no = block_no;
			goto end;
		} else {	/* cycle num equal */
			ASSERT(CYCLE_LSN(head->h_lsn) == cycle_no);
			
			log_rec_block_no = block_no;
			log_rec_bblocks  = BTOBB(head->h_len);
		}
		brelse(bp);
		block_no++;
		
		/* verify log record data cycle numbers */
		for (i=0; i<log_rec_bblocks; i++) {
			bp = bread(log_dev, block_no, 1);
			if (bp->b_flags & B_ERROR) {
				brelse(bp);
				log_panic("log_find_end");
			}
			int_ptr = (int *)bp->b_dmaaddr;
			if (*int_ptr != cycle_no)	/* invalid data blk */
				goto end;
			brelse(bp);
		}
		block_no += log_rec_bblocks;
		log_rec_block_no = block_no;	/* OK to this point */
	}
	
end:
	start_block = log_rec_block_no;
	brelse(bp);
	return start_block;
}	/* log_find_end */


/*
 * Log function which is called when an io completes.
 *
 * The log manager needs its own routine, in order to control what
 * happens with the buffer after the write completes.
 */
void
log_iodone(buf_t *bp)
{
	log_state_done_syncing((log_in_core_t *)(bp->b_fsprivate));
	if ( !(bp->b_flags & B_ASYNC) ) {
		/* Corresponding psema() will be done in bwrite().  If we don't
		 * vsema() here, panic.
		 */
		vsema(&bp->b_iodonesema);
	}
}	/* log_iodone */


/*
 * Push on the buffer cache code if we ever use more than 75% of the on-disk
 * log space.  This code pushes on the lsn which would supposedly free up
 * the 25% which we want to leave free.  We may need to adopt a policy which
 * pushes on an lsn which is further along in the log once we reach the high
 * water mark.  In this manner, we would be creating a low water mark.
 */
void
log_push_buffers_to_disk(xfs_mount_t *mp, log_t *log)
{
    int		blocks;		/* valid blocks left to write to */
    xfs_lsn_t	sync_lsn;
    xfs_lsn_t	threshhold_lsn;
    int		threshhold_block;

    sync_lsn = log->l_sync_lsn;
    if (CYCLE_LSN(sync_lsn) == log->l_curr_cycle) {
	blocks = log->l_logBBsize - (log->l_curr_block - BLOCK_LSN(sync_lsn));
    } else {
	ASSERT(CYCLE_LSN(sync_lsn) + 1 == log->l_curr_cycle);
	blocks = BLOCK_LSN(sync_lsn) - log->l_curr_block;
    }

    if (blocks < (log->l_logBBsize >> 2)) {
	threshhold_block = BLOCK_LSN(sync_lsn) + (log->l_logBBsize >> 2);
	if (threshhold_block >= log->l_logBBsize) {
	    threshhold_block -= log->l_logBBsize;
	    ((uint *)&(threshhold_lsn))[0] = CYCLE_LSN(sync_lsn) +1;
	} else {
	    ((uint *)&(threshhold_lsn))[0] = CYCLE_LSN(sync_lsn);
	}
	((uint *)&(threshhold_lsn))[1] = threshhold_block;
	xfs_trans_push_ail(mp, threshhold_lsn);
    }
}	/* log_push_buffers_to_disk */


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
log_sync(log_t		*log,
	 log_in_core_t	*iclog,
	 uint		flags)
{
	caddr_t		dptr;		/* pointer to byte sized element */
	buf_t		*bp;
	int		i;
	uint		count;		/* byte count of bwrite */
	int		split = 0;	/* split write into two regions */
	
	ASSERT(iclog->ic_refcnt == 0);

	if (flags != 0 && ((flags & XFS_LOG_SYNC) != XFS_LOG_SYNC))
		log_panic("log_sync: illegal flag");
	
	/* put cycle number in every block */
	for (i = 0,
	     dptr = (caddr_t)iclog->ic_data;
	     dptr < (caddr_t)iclog->ic_data + iclog->ic_offset;
	     dptr += BBSIZE, i++) {
		iclog->ic_header.h_cycle_data[i] = *(uint *)dptr;
		*(uint *)dptr = log->l_curr_cycle;
	}
	iclog->ic_header.h_len = iclog->ic_offset;	/* real byte length */

	bp	    = iclog->ic_bp;
	bp->b_blkno = BLOCK_LSN(iclog->ic_header.h_lsn);

	/* Round byte count up to a LOG_BBSIZE chunk */
	count = BBTOB(BTOBB(iclog->ic_offset)) + LOG_HEADER_SIZE;
	if (bp->b_blkno + BTOBB(count) > log->l_logBBsize) {
		split = count - (BBTOB(log->l_logBBsize - bp->b_blkno));
		count = BBTOB(log->l_logBBsize - bp->b_blkno);
		iclog->ic_bwritecnt = 2;	/* split into 2 writes */
	} else {
		iclog->ic_bwritecnt = 1;
	}
	bp->b_dmaaddr	= (caddr_t) iclog;
	bp->b_bcount	= bp->b_bufsize	= count;
	bp->b_iodone	= log_iodone;
	bp->b_edev	= log->l_dev;
	bp->b_fsprivate	= iclog;		/* save for later */
	if (flags & XFS_LOG_SYNC)
		bp->b_flags |= (B_BUSY | B_HOLD);
	else
		bp->b_flags |= (B_BUSY | B_ASYNC);

	ASSERT(bp->b_blkno <= log->l_logBBsize-1);
	ASSERT(bp->b_blkno + BTOBB(count) <= log->l_logBBsize);

	if (log_debug > 1)
		log_verify_iclog(log, iclog, count);

	/* account for log which don't start at block #0 */
	bp->b_blkno += log->l_logBBstart;

	bwrite(bp);

	if (bp->b_flags & B_ERROR == B_ERROR)
		log_panic("log_sync: buffer error");

	if (split) {
		bp		= iclog->ic_log->l_xbuf;
		bp->b_blkno	= 0;			/* XXXmiken assumes 0 */
		bp->b_bcount	= bp->b_bufsize = split;
		bp->b_dmaaddr	= (caddr_t)((psint)iclog+(psint)count);
		bp->b_iodone	= log_iodone;
		bp->b_edev	= log->l_dev;
		bp->b_fsprivate = iclog;
		if (flags & XFS_LOG_SYNC)
			bp->b_flags |= (B_BUSY | B_HOLD);
		else
			bp->b_flags |= (B_BUSY | B_ASYNC);

		ASSERT(bp->b_blkno <= log->l_logBBsize-1);
		ASSERT(bp->b_blkno + BTOBB(count) <= log->l_logBBsize);

		/* account for log which don't start at block #0 */
		bp->b_blkno += log->l_logBBstart;

		bwrite(bp);

		if (bp->b_flags & B_ERROR == B_ERROR)
			log_panic("log_sync: buffer error");

	}
}	/* log_sync */


/*
 * Unallocate a log
 *
 * What to do... sigh.
 */
void
log_unalloc(void)
{
}	/* log_unalloc */


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
 * 2. The LOG_END_TRANS & LOG_CONTINUE_TRANS flags are passed down to the
 *	syncing routine.  When a single log_write region needs to span
 *	multiple in-core logs, the LOG_CONTINUE_TRANS bit should be set
 *	on all log operation writes which don't contain the end of the
 *	region.  The LOG_END_TRANS bit is used for the in-core log
 *	operation which contains the end of the continued log_write region.
 * 3. When log_state_get_iclog_space() grabs the rest of the current iclog,
 *	we don't really know exactly how much space will be used.  As a result,
 *	we don't update ic_offset until the end when we know exactly how many
 *	bytes have been written out.
 */
int
log_write(xfs_mount_t *		mp,
	  xfs_log_iovec_t	reg[],
	  int			nentries,
	  xfs_log_ticket_t	tic,
	  xfs_lsn_t		*start_lsn,
	  int			commit)
{
    log_t		*log	= mp->m_log;
    log_ticket_t	*ticket = (log_ticket_t *)tic;
    log_op_header_t	*logop_head;	/* ptr to log operation header */
    log_in_core_t	*iclog;		/* ptr to current in-core log */
    psint		ptr;		/* copy address into data region */
    int			len;		/* # log_write() bytes to still copy */
    int			index;		/* region index currently copying */
    int			log_offset;	/* offset (from 0) into data region */
    int			start_rec_copy;	/* # bytes to copy for start record */
    int			partial_copy=0; /* # bytes copied if split region */
    int			need_copy;	/* # bytes need to bcopy this region */
    int			copy_len;	/* # bytes actually bcopy'ing */
    int			copy_off;	/* # bytes from entry start */
    int			continuedwr;	/* continued write of in-core log? */
    int			firstwr = 0;	/* first write of transaction */
    
    /* Calculate potential maximum space.  Each region gets its own
     * log_op_header_t and may need to be double word aligned.
     */
    len = 0;
    if (ticket->t_flags & LOG_TIC_INITED)	/* acct for start rec of xact */
	len += sizeof(log_op_header_t);
    
    for (index = 0; index < nentries; index++) {
	len += sizeof(log_op_header_t);		/* each region gets >= 1 */
	len += reg[index].i_len;
    }
    *start_lsn = 0;
    
    if (ticket->t_curr_reserv < len)
	log_panic("xfs_log_write: reservation ran out.  Need to up reservation")
    else if ((ticket->t_flags & LOG_TIC_PERM_RESERV) == 0)
	ticket->t_curr_reserv -= len;
    
    for (index = 0; index < nentries; ) {
	log_offset = log_state_get_iclog_space(log, len, &iclog, &continuedwr);
	
	ptr = (psint) &iclog->ic_data[log_offset];
	
	/* start_lsn is the first lsn written to. That's all we need. */
	if (! *start_lsn)
	    *start_lsn = iclog->ic_header.h_lsn;
	
	/* This loop writes out as many regions as can fit in the amount
	 * of space which was allocated by log_state_get_iclog_space().
	 */
	while (index < nentries) {
	    ASSERT(reg[index].i_len % sizeof(long) == 0);
	    ASSERT((psint)ptr % sizeof(long) == 0);
	    start_rec_copy = 0;
	    
	    /*
	     * If first write for transaction, insert start record.
	     * We can't be trying to commit if we are inited.  We can't
	     * have any "partial_copy" if we are inited.
	     */
	    if (ticket->t_flags & LOG_TIC_INITED) {
		logop_head		= (log_op_header_t *)ptr;
		logop_head->oh_tid	= ticket->t_tid;
		logop_head->oh_clientid = ticket->t_clientid;
		logop_head->oh_len	= 0;
		logop_head->oh_flags    = LOG_START_TRANS;
		ticket->t_flags		&= ~LOG_TIC_INITED;	/* clear bit */
		firstwr++;			  /* increment log ops below */
		
		start_rec_copy = sizeof(log_op_header_t);
		log_write_adv_cnt(ptr, len, log_offset, start_rec_copy);
	    }
	    
	    /* Copy log operation header directly into data section */
	    logop_head			= (log_op_header_t *)ptr;
	    logop_head->oh_tid		= ticket->t_tid;
	    logop_head->oh_clientid	= ticket->t_clientid;
	    
	    /* header copied directly */
	    log_write_adv_cnt(ptr, len, log_offset, sizeof(log_op_header_t));
	    
	    /* commit record? */
	    logop_head->oh_flags = (commit ? LOG_COMMIT_TRANS : 0);
	    
	    /* Partial write last time? => (partial_copy != 0)
	     * need_copy is the amount we'd like to copy if everything could
	     * fit in the current bcopy.
	     */
	    need_copy =	reg[index].i_len - partial_copy;
	    
	    copy_off = partial_copy;
	    if (need_copy <= iclog->ic_size - log_offset) {  /*complete write */
		logop_head->oh_len = copy_len = need_copy;
		if (partial_copy)
		    logop_head->oh_flags |= LOG_END_TRANS;
		partial_copy = 0;
	    } else { 					     /* partial write */
		copy_len = logop_head->oh_len =	iclog->ic_size - log_offset;
		partial_copy += copy_len;
	        logop_head->oh_flags |= LOG_CONTINUE_TRANS;
		len += sizeof(log_op_header_t);   /* from splitting of region */
	    }

	    if (log_debug > 1) {
		ASSERT(copy_len >= 0);
		log_verify_dest_ptr(log, ptr);
	    }

	    /* copy region */
	    bcopy(reg[index].i_addr + copy_off, (caddr_t)ptr, copy_len);
	    log_write_adv_cnt(ptr, len, log_offset, copy_len);

	    /* make copy_len total bytes copied, including headers */
	    copy_len += start_rec_copy + sizeof(log_op_header_t);
	    log_state_finish_copy(log, iclog, firstwr,
				  (continuedwr ? copy_len : 0));

	    firstwr = 0;
	    if (partial_copy) {			/* copied partial region */
		/* already marked WANT_SYNC */
		log_state_release_iclog(log, iclog);
		break;				     /* don't increment index */
	    } else {				/* copied entire region */
		index++;
		partial_copy = 0;
		if (iclog->ic_size - log_offset <= sizeof(log_op_header_t)) {
		    log_state_want_sync(log, iclog);
		    log_state_release_iclog(log, iclog);
		    break;
		}
	    }
	}
    }
    
    
    log_state_want_sync(log, iclog);   /* not needed for kernel XXXmiken */
    
    log_state_release_iclog(log, iclog);
}	/* log_write */


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
 */
void
log_state_clean_log(log_t *log)
{
	log_in_core_t	*iclog;

	iclog = log->l_iclog;
	do {
		if (iclog->ic_state == LOG_STATE_DIRTY) {
			iclog->ic_state	= LOG_STATE_ACTIVE;
			iclog->ic_offset       = 0;
			iclog->ic_callback	= 0;   /* don't need to free */
			iclog->ic_header.h_num_logops = 0;
			bzero(iclog->ic_header.h_cycle_data,
			      sizeof(iclog->ic_header.h_cycle_data));
			iclog->ic_header.h_lsn = 0;
			vsema(&log->l_flushsema);
		} else if (iclog->ic_state == LOG_STATE_ACTIVE)
			/* do nothing */;
		else
			break;	/* stop cleaning */
		iclog = iclog->ic_next;
	} while (iclog != log->l_iclog);
}	/* log_state_clean_log */


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
log_state_done_syncing(log_in_core_t	*iclog)
{
	int		   spl;
	log_t		   *log = iclog->ic_log;
	log_in_core_t	   *iclogp;
	xfs_log_callback_t *cb, *cb_next;

	spl = splockspl(log->l_icloglock, splhi);

	ASSERT(iclog->ic_state == LOG_STATE_SYNCING);
	ASSERT(iclog->ic_refcnt == 0);
	ASSERT(iclog->ic_bwritecnt == 1 || iclog->ic_bwritecnt == 2);

	if (--iclog->ic_bwritecnt == 1) {
		spunlockspl(log->l_icloglock, spl);
		return;
	}

	iclog->ic_state = LOG_STATE_CALLBACK;
	spunlockspl(log->l_icloglock, spl);

	/* perform callbacks XXXmiken */
	for (cb = iclog->ic_callback; cb != 0; cb = cb_next) {
		cb_next = cb->cb_next;
		cb->cb_func(cb->cb_arg);
	}

	spl = splockspl(log->l_icloglock, splhi);

	ASSERT(iclog->ic_state == LOG_STATE_CALLBACK);

	iclog->ic_state		= LOG_STATE_DIRTY;

	/* wake up threads waiting in xfs_log_force() */
	while (cvsema(&iclog->ic_forcesema));

	log_state_clean_log(log);

	spunlockspl(log->l_icloglock, spl);

}	/* log_state_done_syncing */


/*
 * Update counters atomically now that bcopy is done.
 */
void
log_state_finish_copy(log_t		*log,
		      log_in_core_t	*iclog,
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
}	/* log_state_finish_copy */



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
 *	* log_offset where log_write() can start writing into the in-core
 *		log's data space.
 *	* in-core log pointer to which log_write() should write.
 *	* boolean indicating this is a continued write to an in-core log.
 *		If this is the last write, then the in-core log's offset field
 *		needs to be incremented, depending on the amount of data which
 *		is copied.
 */
int
log_state_get_iclog_space(log_t		*log,
			  int		len,
			  log_in_core_t **iclogp,
			  int		*continued_write)
{
	int		 spl;
	int		 log_offset;
	log_rec_header_t *head;
	log_in_core_t	 *iclog;

restart:
	spl = splockspl(log->l_icloglock, splhi);

	iclog = log->l_iclog;
	if (! (iclog->ic_state == LOG_STATE_ACTIVE ||
	       iclog->ic_state == LOG_STATE_DIRTY) ) {
		spunlockspl(log->l_icloglock, spl);
		psema(&log->l_flushsema, PINOD);
		vsema(&log->l_flushsema);
		goto restart;
	}

	log_state_clean_log(log);
	head = &iclog->ic_header;

	iclog->ic_refcnt++;			/* prevents sync */
	log_offset = iclog->ic_offset;

	/* On the 1st write to an iclog, figure out lsn.  This works
	 * if iclogs marked LOG_STATE_WANT_SYNC always write out what they are
	 * committing to.  If the offset is set, that's how many blocks
	 * must be written.
	 */
	if (log_offset == 0) {
		head->h_cycle = log->l_curr_cycle;
		ASSIGN_LSN(head->h_lsn, log);
		ASSERT(log->l_curr_block >= 0);
	}

	/* If there is enough room to write everything, then do it.  Otherwise,
	 * claim the rest of the region and make sure the LOG_STATE_WANT_SYNC
	 * bit is on, so this will get flushed out.  Don't update ic_offset
	 * until you know exactly how many bytes get copied.  Therefore, wait
	 * until later to update ic_offset.
	 */
	if (len <= iclog->ic_size - iclog->ic_offset) {
	    iclog->ic_offset += len;
	    *continued_write = 0;
	} else {
	    *continued_write = 1;
	    if (iclog->ic_state != LOG_STATE_WANT_SYNC) {
		iclog->ic_state = LOG_STATE_WANT_SYNC;
		iclog->ic_header.h_prev_offset = log->l_prev_block;
		log->l_prev_block = log->l_curr_block;
		log->l_prev_cycle = log->l_curr_cycle;

		/* roll log?: ic_offset changed later */
		log->l_curr_block += BTOBB(iclog->ic_size)+1;
		if (log->l_curr_block >= log->l_logBBsize) {
		    log->l_curr_cycle++;
		    log->l_curr_block -= log->l_logBBsize;
		    ASSERT(log->l_curr_block >= 0);
		}
		log->l_iclog = iclog->ic_next;
		psema(&log->l_flushsema, PINOD);
	    }

	    /* log_write() algorithm assumes that at least 2
	     * log_op_header_t's can fit into remaining data section.
	     */
	    if (iclog->ic_size - iclog->ic_offset < 2*sizeof(log_op_header_t)) {
		iclog->ic_refcnt--;
		spunlockspl(log->l_icloglock, spl);
		goto restart;
	    }
	}
	*iclogp = iclog;

	spunlockspl(log->l_icloglock, spl);
	return log_offset;
}	/* log_state_get_iclog_space */


/*
 * Atomically get ticket.  Usually, don't return until one is available.
 */
xfs_log_ticket_t
log_state_get_ticket(log_t	*log,
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
			log_panic("log_state_get_ticket: over reserved");
	}
	log->l_logreserved += len;
	tic = log_maketicket(log, len, log_client);
	spunlockspl(log->l_icloglock, spl);

	return tic;

}	/* log_state_get_ticket */


/*
 * If the lsn is not found or the iclog with the lsn is in the callback
 * state, we need to call the function directly.  This is done outside
 * this function's scope.  Otherwise, we insert the callback at the front
 * of the iclog's callback list.
 */
int
log_state_lsn_is_synced(log_t		   *log,
			xfs_lsn_t	   lsn,
			xfs_log_callback_t *cb)
{
	log_in_core_t *iclog;
	int	      spl;
	int	      lsn_is_synced = 1;

	spl = splockspl(log->l_icloglock, splhi);

	iclog = log->l_iclog;
	do {
		if (iclog->ic_header.h_lsn != lsn) {
			iclog = iclog->ic_next;
			continue;
		} else {
			if ((iclog->ic_state == LOG_STATE_CALLBACK) ||
			    (iclog->ic_state == LOG_STATE_DIRTY))   /*call it*/
				break;

			/* insert callback into list */
			cb->cb_next = iclog->ic_callback;
			iclog->ic_callback = cb;
			lsn_is_synced = 0;
			break;
		}
	} while (iclog != log->l_iclog);

	spunlockspl(log->l_icloglock, spl);
	return lsn_is_synced;
}	/* log_state_lsn_is_synced */


/*
 * Atomically put back used ticket.
 */
void
log_state_put_ticket(log_t	  *log,
		     log_ticket_t *tic)
{
	int	spl;

	spl = splockspl(log->l_icloglock, splhi);

#ifdef DEBUG
	bytes_of_ticket_used = tic->t_orig_reserv - tic->t_curr_reserv;
#endif

	log->l_logreserved -= tic->t_orig_reserv;
	log_putticket(log, tic);

	spunlockspl(log->l_icloglock, spl);
}	/* log_state_reserve_space */


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
log_state_release_iclog(log_t		*log,
			log_in_core_t	*iclog)
{
    int		spl;
    int		sync = 0;	/* do we sync? */
    xfs_lsn_t	sync_lsn;
    int		blocks;
    
    spl = splockspl(log->l_icloglock, splhi);
    
    ASSERT(iclog->ic_refcnt > 0);
    
    if (--iclog->ic_refcnt == 0 && iclog->ic_state == LOG_STATE_WANT_SYNC) {
	ASSERT(valusema(&log->l_flushsema) > 0);
	sync++;
	iclog->ic_state = LOG_STATE_SYNCING;
	
	if ((sync_lsn = xfs_trans_tail_ail(log->l_mp)) == 0)
	    sync_lsn = iclog->ic_header.h_sync_lsn = log->l_sync_lsn;
	else
	    iclog->ic_header.h_sync_lsn = log->l_sync_lsn = sync_lsn;

	/* check if it will fit */
	if (log_debug > 1) {
	    if (CYCLE_LSN(sync_lsn) == log->l_prev_cycle) {
		blocks =
		    log->l_logBBsize - (log->l_prev_block -BLOCK_LSN(sync_lsn));
		if (blocks < BTOBB(iclog->ic_offset)+1)
		    log_panic("ran out of log space");
	    } else {
		ASSERT(CYCLE_LSN(sync_lsn)+1 == log->l_prev_cycle);
		if (BLOCK_LSN(sync_lsn) == log->l_prev_block)
		    log_panic("ran out of log space");
		
		blocks = BLOCK_LSN(sync_lsn) - log->l_prev_block;
		if (blocks < BTOBB(iclog->ic_offset) + 1)
		    log_panic("ran out of log space");
	    }
	}

	/* cycle incremented when incrementing curr_block */
    }
    
    spunlockspl(log->l_icloglock, spl);
    
    if (sync)
	log_sync(log, iclog, 0);
    
}	/* log_state_release_iclog */


/*
 * Used by code which implements synchronous log forces.
 *
 * Find in-core log with lsn.
 *	If it is in the DIRTY state, just return.
 *	If it is in the ACTIVE state, move the in-core log into the WANT_SYNC
 *		state and go to sleep or return.
 *	If it is in any other state, go to sleep or return.
 */
int
log_state_sync(log_t *log, xfs_lsn_t lsn, uint flags)
{
	log_in_core_t *iclog;
	int spl;

	spl = splockspl(log->l_icloglock, splhi);

	iclog = log->l_iclog;
	do {
		if (iclog->ic_header.h_lsn != lsn) {
		    iclog = iclog->ic_next;
		} else {
		    if (iclog->ic_state == LOG_STATE_ACTIVE) {
			iclog->ic_state = LOG_STATE_WANT_SYNC;
			iclog->ic_header.h_prev_offset = log->l_prev_block;
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
		    } else if (iclog->ic_state == LOG_STATE_DIRTY) {
			spunlockspl(log->l_icloglock, spl);
			return 0;
		    }
		    if (flags & XFS_LOG_SYNC)		/* sleep */
			spunlockspl_psema(log->l_icloglock, spl,
					  &iclog->ic_forcesema, 0);
		    else					/* just return*/
			spunlockspl(log->l_icloglock, spl);
		    return 0;
		}
	} while (iclog != log->l_iclog);

	spunlockspl(log->l_icloglock, spl);
	return XFS_ENOTFOUND;
}	/* log_state_sync */


/*
 * Called when we want to mark the current iclog as being ready to sync to
 * disk.
 */
void
log_state_want_sync(log_t *log, log_in_core_t *iclog)
{
	int spl;
	
	spl = splockspl(log->l_icloglock, splhi);
	
	if (iclog->ic_state == LOG_STATE_ACTIVE) {
		iclog->ic_state = LOG_STATE_WANT_SYNC;
		iclog->ic_header.h_prev_offset = log->l_prev_block;
		log->l_prev_block = log->l_curr_block;
		log->l_prev_cycle = log->l_curr_cycle;
		
		/* roll log?: ic_offset changed later */
		log->l_curr_block += BTOBB(iclog->ic_offset)+1;
		if (log->l_curr_block >= log->l_logBBsize){
			log->l_curr_cycle++;
			log->l_curr_block -= log->l_logBBsize;
			ASSERT(log->l_curr_block >= 0);
		}
		
		log->l_iclog = log->l_iclog->ic_next;
		psema(&log->l_flushsema, PINOD);
	} else if (iclog->ic_state != LOG_STATE_WANT_SYNC)
		log_panic("log_state_want_sync: bad state");
	
	spunlockspl(log->l_icloglock, spl);
}	/* log_state_want_sync */



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
	t_list->t_next = 0;
	log->l_tail = t_list;

}	/* log_alloc_tickets */


/*
 * Put ticket into free list
 */
void log_putticket(log_t	*log,
		   log_ticket_t *ticket)
{
	log_ticket_t *t_list;

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
}	/* log_putticket */


/*
 * Grab ticket off freelist or allocation some more
 */
xfs_log_ticket_t *
log_maketicket(log_t		*log,
	       int		len,
	       char		log_clientid)
{
	log_ticket_t *tic;

	if (log->l_freelist == NULL) {
		log_panic("xfs_log_ticket: ran out of tickets"); /* XXXmiken */
		/* just call log_alloc_tickets */
	}

	tic			= log->l_freelist;
	log->l_freelist		= tic->t_next;
	tic->t_orig_reserv	= tic->t_curr_reserv = len;
	tic->t_tid		= (log_tid_t)tic;
	tic->t_clientid		= log_clientid;
	tic->t_flags		= LOG_TIC_INITED;

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
log_verify_dest_ptr(log_t *log,
		    psint ptr)
{
	int i;
	int good_ptr = 0;

	for (i=0; i < LOG_NUM_ICLOGS; i++) {
		if (ptr >= (psint)log->l_iclog_bak[i] &&
		    ptr <= (psint)log->l_iclog_bak[i]+log->l_iclog_size)
			good_ptr++;
	}
	if (! good_ptr)
		log_panic("log_verify_dest_ptr: invalid ptr");
}	/* log_verify_dest_ptr */


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
log_verify_iclog(log_t		*log,
		 log_in_core_t	*iclog,
		 int		count)
{
	log_op_header_t  *ophead;
	log_rec_header_t *rec;
	log_in_core_t	 *icptr;
	log_tid_t	 tid;
	caddr_t		 ptr;
	char		 clientid;
	char		 buf[LOG_HEADER_SIZE];
	int		 len;
	int		 fd;
	int		 i;
	int		 op_len;
	int		 cycle_no;

	/* check validity of iclog pointers */
	icptr = log->l_iclog;
	for (i=0; i < LOG_NUM_ICLOGS; i++) {
		if (icptr == 0)
			log_panic("log_verify_iclog: illegal ptr");
		icptr = icptr->ic_next;
	}
	if (icptr != log->l_iclog)
		log_panic("log_verify_iclog: corrupt iclog ring");

	/* check log magic numbers */
	ptr = (caddr_t) iclog;
	if (*(uint *)ptr != LOG_HEADER_MAGIC_NUM)
		log_panic("log_verify_iclog: illegal magic num");
	
	for (ptr += BBSIZE; ptr < (caddr_t)iclog+count; ptr += BBSIZE) {
		if (*(uint *)ptr == LOG_HEADER_MAGIC_NUM)
			log_panic("log_verify_iclog: unexpected magic num");
	}
	
	/* check fields */
	len = iclog->ic_header.h_len;
	ptr = iclog->ic_data;
	ophead = (log_op_header_t *)ptr;
	for (i=0; i<iclog->ic_header.h_num_logops; i++) {
		ophead = (log_op_header_t *)ptr;

		/* clientid is only 1 byte */
		if (((psint)&ophead->oh_clientid & 0x1ff) != 0)
			clientid = ophead->oh_clientid;
		else
			clientid = iclog->ic_header.h_cycle_data[BTOBB(&ophead->oh_clientid - iclog->ic_data)]>>24;
		if (clientid != XFS_TRANSACTION)
			log_panic("log_verify_iclog: illegal client");

		/* check tids */
		if (((psint)&ophead->oh_tid & 0x1ff) != 0)
			tid = ophead->oh_tid;
		else
			tid = (log_tid_t)iclog->ic_header.h_cycle_data[BTOBB((psint)&ophead->oh_tid - (psint)iclog->ic_data)];

#ifndef _KERNEL
		/* This is a user space check */
		if ((psint)tid < 0x10000000 || (psint)tid > 0x20000000)
			log_panic("log_verify_iclog: illegal tid");
#endif

		/* check length */
		if (((psint)&ophead->oh_len & 0x1ff) != 0)
			op_len = ophead->oh_len;
		else
			op_len = iclog->ic_header.h_cycle_data[BTOBB((psint)&ophead->oh_len - (psint)iclog->ic_data)];
		len -= sizeof(log_op_header_t) + op_len;
		ptr += sizeof(log_op_header_t) + op_len;
	}
	if (len != 0)
		log_panic("log_verify_iclog: illegal iclog");

#ifndef _KERNEL
	/* check wrapping log */
	if (BLOCK_LSN(iclog->ic_header.h_lsn) < 5) {
		cycle_no = CYCLE_LSN(iclog->ic_header.h_lsn);
		fd = bmajor(log->l_dev);
		if (lseek(fd, 0, SEEK_SET) < 0)
			log_panic("log_verify_iclog: lseek 0 failed");
		for (i = 0; i < BLOCK_LSN(iclog->ic_header.h_lsn); i++) {
			if (read(fd, buf, LOG_HEADER_SIZE) == 0)
				log_panic("log_verify_iclog: bad read");
			rec = (log_rec_header_t *)buf;
			if (rec->h_magicno == LOG_HEADER_MAGIC_NUM &&
			    CYCLE_LSN(rec->h_lsn) < cycle_no)
				log_panic("log_verify_iclog: bad cycle no");
		}
	}
#endif
}	/* log_verify_iclog */
