#ident	"$Revision: 1.99 $"

/*
 * High level interface routines for log manager
 */

#include <sys/param.h>

#ifdef SIM
#define _KERNEL 1
#endif

#include <sys/sysmacros.h>
#include <sys/buf.h>
#include <sys/vnode.h>
#include <sys/sysinfo.h>
#include <sys/ksa.h>

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

#include <sys/cmn_err.h>
#include <sys/kmem.h>
#include <sys/ktrace.h>
#include <sys/debug.h>
#include <sys/proc.h>
#include <sys/pda.h>		/* depends on proc.h */
#include <sys/sema.h>
#include <sys/sysmacros.h>
#include <sys/vfs.h>

#include "xfs_inum.h"
#include "xfs_types.h"
#include "xfs_ag.h"		/* needed by xfs_sb.h */
#include "xfs_sb.h"		/* depends on xfs_types.h & xfs_inum.h */
#include "xfs_log.h"
#include "xfs_trans.h"
#include "xfs_mount.h"		/* depends on xfs_trans.h & xfs_sb.h */
#include "xfs_inode_item.h"
#include "xfs_error.h"
#include "xfs_log_priv.h"	/* depends on all above */
#include "xfs_buf_item.h"
#include "xfs_alloc_btree.h"
#include "xfs_log_recover.h"


#ifdef SIM
#include "sim.h"		/* must be last include file */
#endif


#define xlog_write_adv_cnt(ptr, len, off, bytes) \
	{ (ptr) += (bytes); \
	  (len) -= (bytes); \
	  (off) += (bytes);}

/* Local miscellaneous function prototypes */
STATIC xfs_lsn_t xlog_commit_record(xfs_mount_t *mp, xlog_ticket_t *ticket);
STATIC int	 xlog_find_zeroed(xlog_t *log, daddr_t *blk_no);
STATIC xlog_t *  xlog_alloc_log(xfs_mount_t	*mp,
				dev_t		log_dev,
				daddr_t		blk_offset,
				int		num_bblks);
STATIC int	 xlog_space_left(xlog_t *log, int cycle, int bytes);
STATIC void	 xlog_sync(xlog_t *log, xlog_in_core_t *iclog, uint flags);
STATIC void	 xlog_unalloc_log(xlog_t *log);
STATIC int	 xlog_write(xfs_mount_t *mp, xfs_log_iovec_t region[],
			    int nentries, xfs_log_ticket_t tic,
			    xfs_lsn_t *start_lsn, uint flags);

/* local state machine functions */
STATIC void xlog_state_done_syncing(xlog_in_core_t *iclog);
STATIC void xlog_state_finish_copy(xlog_t	   *log,
				   xlog_in_core_t  *iclog,
				   int		   first_write,
				   int		   bytes);
STATIC int  xlog_state_get_iclog_space(xlog_t		*log,
				       int		len,
				       xlog_in_core_t	**iclog,
				       xlog_ticket_t	*ticket,
				       int		*continued_write);
STATIC int  xlog_state_lsn_is_synced(xlog_t	 	*log,
				     xfs_lsn_t		lsn,
				     xfs_log_callback_t *cb);
STATIC void xlog_state_put_ticket(xlog_t	*log,
				  xlog_ticket_t *tic);
STATIC void xlog_state_release_iclog(xlog_t		*log,
				     xlog_in_core_t	*iclog);
STATIC void xlog_state_switch_iclogs(xlog_t		*log,
				     xlog_in_core_t *iclog,
				     int		eventual_size);
STATIC int  xlog_state_sync(xlog_t *log, xfs_lsn_t lsn, uint flags);
STATIC void xlog_state_want_sync(xlog_t	*log, xlog_in_core_t *iclog);


/* local functions to manipulate grant head */
STATIC void xlog_grant_log_space(xlog_t		*log,
				 xlog_ticket_t	*xtic);
STATIC void xlog_grant_push_ail(xfs_mount_t *mp);
STATIC void xlog_regrant_reserve_log_space(xlog_t	 *log,
					   xlog_ticket_t *ticket);
STATIC void xlog_regrant_write_log_space(xlog_t		*log,
					 xlog_ticket_t  *ticket);
STATIC void xlog_ungrant_log_space(xlog_t	 *log,
				   xlog_ticket_t *ticket);


/* local ticket functions */
STATIC void		xlog_state_ticket_alloc(xlog_t *log);
STATIC xfs_log_ticket_t *xlog_ticket_get(xlog_t *log,
					 int	unit_bytes,
					 int	count,
					 char	clientid,
					 uint	flags);
STATIC void		xlog_ticket_put(xlog_t *log, xlog_ticket_t *ticket);

/* local debug functions */
STATIC void	xlog_verify_dest_ptr(xlog_t *log, __psint_t ptr);
STATIC void	xlog_verify_disk_cycle_no(xlog_t *log, xlog_in_core_t *iclog);
STATIC void	xlog_verify_grant_head(xlog_t *log, int equals);
STATIC void	xlog_verify_iclog(xlog_t *log, xlog_in_core_t *iclog,
				  int count, boolean_t syncing);
STATIC void	xlog_verify_tail_lsn(xlog_t *log, xlog_in_core_t *iclog,
				     xfs_lsn_t tail_lsn);


/*
 * 0 => disable log manager
 * 1 => enable log manager
 * 2 => enable log manager and log debugging
 */
int   xlog_debug = 1;
dev_t xlog_devt  = 0;


#if defined(DEBUG) && !defined(SIM)
void
xlog_trace_loggrant(xlog_t *log, xlog_ticket_t *tic, caddr_t string)
{
	if (! log->l_grant_trace)
		log->l_grant_trace = ktrace_alloc(1024, 0);

	ktrace_enter(log->l_grant_trace,
		     (void *)tic,
		     (void *)log->l_reserve_headq,
		     (void *)log->l_write_headq,
		     (void *)log->l_grant_reserve_cycle,     
		     (void *)log->l_grant_reserve_bytes,
		     (void *)log->l_grant_write_cycle,
		     (void *)log->l_grant_write_bytes,
		     (void *)log->l_curr_cycle,
		     (void *)log->l_curr_block,
		     (void *)CYCLE_LSN(log->l_tail_lsn),
		     (void *)BLOCK_LSN(log->l_tail_lsn),
		     (void *)string,
		     (void *)13,
		     (void *)14,
		     (void *)15,
		     (void *)16);
}

void
xlog_trace_tic(xlog_t *log, xlog_ticket_t *tic)
{
	if (! log->l_trace)
		log->l_trace = ktrace_alloc(256, 0);

	ktrace_enter(log->l_trace,
		     (void *)tic,
		     (void *)tic->t_curr_res,
		     (void *)tic->t_unit_res,
		     (void *)tic->t_ocnt,
		     (void *)tic->t_cnt,
		     (void *)tic->t_flags,
		     (void *)7,
		     (void *)8,
		     (void *)9,
		     (void *)10,
		     (void *)11,
		     (void *)12,
		     (void *)13,
		     (void *)14,
		     (void *)15,
		     (void *)16);
}

void
xlog_trace_iclog(xlog_in_core_t *iclog, uint state)
{
	pid_t pid;

	if (private.p_curproc)
		pid = private.p_curproc->p_pid;

	if (!iclog->ic_trace)
		iclog->ic_trace = ktrace_alloc(256, 0);
	ktrace_enter(iclog->ic_trace,
		     (void *)state,
		     (void *)pid,
		     (void *)0,
		     (void *)0,
		     (void *)0,
		     (void *)0,
		     (void *)0,
		     (void *)0,
		     (void *)0,
		     (void *)0,
		     (void *)0,
		     (void *)0,
		     (void *)0,
		     (void *)0,
		     (void *)0,
		     (void *)0);
}

#else
#define	xlog_trace_loggrant(log,tic,string)
#define	xlog_trace_iclog(iclog,state)
#endif /* DEBUG && !SIM */

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
	     xfs_log_ticket_t	xtic,
	     uint		flags)
{
	xlog_t		*log    = mp->m_log;
	xlog_ticket_t	*ticket = (xfs_log_ticket_t) xtic;
	xfs_lsn_t	lsn	= 0;
	
	if (! xlog_debug && xlog_devt == log->l_dev)
		return 0;

	/* If nothing was ever written, don't write out commit record */
	if ((ticket->t_flags & XLOG_TIC_INITED) == 0)
		lsn = xlog_commit_record(mp, ticket);

	if ((ticket->t_flags & XLOG_TIC_PERM_RESERV) == 0 ||
	    (flags & XFS_LOG_REL_PERM_RESERV)) {
		/* Release ticket if not permanent reservation or a specifc
		 * request has been made to release a permanent reservation.
		 */
		xlog_ungrant_log_space(log, ticket);
		xlog_state_put_ticket(log, ticket);
	} else {
		xlog_regrant_reserve_log_space(log, ticket);
	}

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

	if (! xlog_debug && xlog_devt == log->l_dev)
		return 0;
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

	if (! xlog_debug && xlog_devt == log->l_dev)
		return;
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
		int		 unit_bytes,
		int		 cnt,
		xfs_log_ticket_t *ticket,
		char		 client,
		uint		 flags)
{
	xlog_t	  *log = mp->m_log;
	
	if (! xlog_debug && xlog_devt == log->l_dev)
		return 0;

	if (client != XFS_TRANSACTION && client != XFS_LOG)
		return -1;
	
	if (flags & XFS_LOG_NOSLEEP) {
		xlog_panic("xfs_log_reserve: not implemented");
		return XFS_ENOTSUP;
	}
	
	if (*ticket != NULL) {
		ASSERT(flags & XFS_LOG_PERM_RESERV);
		xlog_grant_push_ail(mp);
		xlog_regrant_write_log_space(log, (xlog_ticket_t *)*ticket);
	} else {
		/* may sleep if need to allocate more tickets */
		*ticket = xlog_ticket_get(log, unit_bytes, cnt, client, flags);
		xlog_grant_push_ail(mp);
		xlog_grant_log_space(log, (xlog_ticket_t *)*ticket);
	}

	return 0;
}	/* xfs_log_reserve */


int
xfs_log_stat(caddr_t mnt_pt, int *log_BBstart, int *log_BBsize)
{
	xfs_mount_t *xmp;
	vnode_t *vp;
	int error, start, size;
	extern int xfs_fstype;

	if (error = lookupname(mnt_pt, UIO_USERSPACE, NO_FOLLOW, NULLVPP, &vp))
		return error;
	if (vp->v_vfsp->vfs_fstype != xfs_fstype) {
		error = XFS_ENOTXFS;
	} else {
		xmp = (xfs_mount_t *)vp->v_vfsp->vfs_data;
		start = XFS_FSB_TO_DADDR(xmp, xmp->m_sb.sb_logstart);
		size  = XFS_FSB_TO_BB(xmp, xmp->m_sb.sb_logblocks);
		if ((error = copyout(&start, log_BBstart, sizeof(int))) == 0)
			error = copyout(&size, log_BBsize, sizeof(int));
	}
	VN_RELE(vp);
	return error;
}	/* xfs_log_stat */


/*
 * Mount a log filesystem
 *
 * mp		- ubiquitous xfs mount point structure
 * log_dev	- device number of on-disk log device
 * blk_offset	- Start block # where block size is 512 bytes (BBSIZE)
 * num_bblocks	- Number of BBSIZE blocks in on-disk log
 *
 * Return error or zero.
 */
int
xfs_log_mount(xfs_mount_t	*mp,
	      dev_t		log_dev,
	      daddr_t		blk_offset,
	      int		num_bblks)
{
	xlog_t *log;
	int    error;
	

	log = xlog_alloc_log(mp, log_dev, blk_offset, num_bblks);

	if (! xlog_debug) {
		cmn_err(CE_NOTE, "log dev: 0x%x", log_dev);
		return 0;
	}

	if ((error = xlog_recover(log)) != NULL) {
		xlog_unalloc_log(log);
	}

	/* Normal transactions can now occur */
	log->l_flags &= ~XLOG_ACTIVE_RECOVERY;

	return error;
}	/* xfs_log_mount */


/*
 * Unmount a log filesystem.
 *
 * Mark the filesystem clean as unmount happens.
 */
int
xfs_log_unmount(xfs_mount_t *mp)
{
	xlog_t		 *log = mp->m_log;
	xlog_in_core_t	 *iclog, *first_iclog;
	xfs_log_iovec_t  reg[1];
	xfs_log_ticket_t tic = 0;
	xfs_lsn_t	 lsn;
	int		 error;
	int		 spl;

	if (! xlog_debug && xlog_devt == log->l_dev)
		return 0;

	if (xfs_log_force(mp, 0, XFS_LOG_FORCE|XFS_LOG_SYNC))
		xlog_panic("xfs_log_unmount: log force failed");
#ifdef DEBUG
	first_iclog = iclog = log->l_iclog;
	do {
		ASSERT(iclog->ic_state == XLOG_STATE_ACTIVE);
		ASSERT(iclog->ic_offset == 0);
		iclog = iclog->ic_next;
	} while (iclog != first_iclog);
#endif
	reg[0].i_addr = "Unmount filesystem--";
	reg[0].i_len  = 20;
	if (xfs_log_reserve(mp, 600, 1, &tic, XFS_LOG, 0) == -1)
		xlog_panic("xfs_log_unmount: xfs_log_reserve failed");
	((xlog_ticket_t *)tic)->t_flags = 0;	/* remove inited flag */
	error = xlog_write(mp, reg, 1, tic, &lsn, XLOG_UNMOUNT_TRANS);
	if (error)
		xlog_panic("xfs_log_unmount: unmount record failed");
	spl = LOG_LOCK(log);
	iclog = log->l_iclog;
	iclog->ic_refcnt++;
	LOG_UNLOCK(log, spl);
	xlog_state_want_sync(log, iclog);
	xlog_state_release_iclog(log, iclog);
	spl = LOG_LOCK(log);
	if (!(iclog->ic_state == XLOG_STATE_ACTIVE ||
	      iclog->ic_state == XLOG_STATE_DIRTY))
		spunlockspl_psema(log->l_icloglock, spl,	/* sleep */
				  &iclog->ic_forcesema, 0);
	else
		LOG_UNLOCK(log, spl);
	xlog_state_put_ticket(log, tic);
	xlog_unalloc_log(log);

	return 0;
}	/* xfs_log_unmount */


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
	xlog_t *log = mp->m_log;
	
	if (! xlog_debug && xlog_devt == log->l_dev) {
		*start_lsn = 0;
		return 0;
	}

	return xlog_write(mp, reg, nentries, tic, start_lsn, 0);
}	/* xfs_log_write */


void
xfs_log_move_tail(xfs_mount_t	*mp,
		  xfs_lsn_t	tail_lsn)
{
	xlog_ticket_t	*tic, *last_tic;
	xfs_lsn_t	sync_lsn;
	xlog_t		*log = mp->m_log; 
	int		need_bytes, free_bytes, cycle, bytes, spl;

	if (!xlog_debug && xlog_devt == log->l_dev)
		return;
	if (tail_lsn == 0) {
		/* needed since sync_lsn is 64 bits */
		spl = LOG_LOCK(log);
		tail_lsn = log->l_last_sync_lsn;
		LOG_UNLOCK(log, spl);
	}

	spl = GRANT_LOCK(log);

	/* Also an illegal lsn.  1 implies that we aren't passing in a legal
	 * tail_lsn.
	 */
	if (tail_lsn != 1)
		log->l_tail_lsn = tail_lsn;

	if (tic = log->l_write_headq) {
#ifdef DEBUG
		if (log->l_flags & XLOG_ACTIVE_RECOVERY)
			panic("Recovery problem");
#endif
		cycle = log->l_grant_write_cycle;
		bytes = log->l_grant_write_bytes;
		free_bytes = xlog_space_left(log, cycle, bytes);
		do {
			ASSERT(tic->t_flags & XLOG_TIC_PERM_RESERV);

			if (free_bytes < tic->t_unit_res)
				break;
			free_bytes -= tic->t_unit_res;
			cvsema(&tic->t_sema);
			tic = tic->t_next;
		} while (tic != log->l_write_headq);
	}
	if (tic = log->l_reserve_headq) {
#ifdef DEBUG
		if (log->l_flags & XLOG_ACTIVE_RECOVERY)
			panic("Recovery problem");
#endif
		cycle = log->l_grant_reserve_cycle;
		bytes = log->l_grant_reserve_bytes;
		free_bytes = xlog_space_left(log, cycle, bytes);
		do {
			if (tic->t_flags & XLOG_TIC_PERM_RESERV)
				need_bytes = tic->t_unit_res*tic->t_cnt;
			else
				need_bytes = tic->t_unit_res;
			if (free_bytes < need_bytes)
				break;
			free_bytes -= need_bytes;
			cvsema(&tic->t_sema);
			tic = tic->t_next;
		} while (tic != log->l_reserve_headq);
	}
	GRANT_UNLOCK(log, spl);
}	/* xfs_log_move_tail */


/******************************************************************************
 *
 *	local routines
 *
 ******************************************************************************
 */

/* xfs_trans_tail_ail returns 0 when there is nothing in the list.
 * The log manager must keep track of the last LR which was committed
 * to disk.  The lsn of this LR will become the new tail_lsn whenever
 * xfs_trans_tail_ail returns 0.  If we don't do this, we run into
 * the situation where stuff could be written into the log but nothing
 * was ever in the AIL when asked.  Eventually, we panic since the
 * tail hits the head.
 *
 * We may be holding the log iclog lock upon entering this routine.
 */
xfs_lsn_t
xlog_assign_tail_lsn(xfs_mount_t *mp, xlog_in_core_t *iclog)
{
	xfs_lsn_t tail_lsn;
	int	  spl;
	xlog_t	  *log = mp->m_log;

	tail_lsn = xfs_trans_tail_ail(mp);
	spl = GRANT_LOCK(log);
	if (tail_lsn != 0)
		log->l_tail_lsn = tail_lsn;
	else
		tail_lsn = log->l_tail_lsn = log->l_last_sync_lsn;
	if (iclog)
		iclog->ic_header.h_tail_lsn = tail_lsn;
	GRANT_UNLOCK(log, spl);

	return tail_lsn;
}	/* xlog_assign_tail_lsn */


int
xlog_space_left(xlog_t *log, int cycle, int bytes)
{
	int free_bytes;

	if (CYCLE_LSN(log->l_tail_lsn) == cycle) {
		free_bytes =
			log->l_logsize -
			(bytes - BBTOB(BLOCK_LSN(log->l_tail_lsn)));
	} else {
		free_bytes =
			BBTOB(BLOCK_LSN(log->l_tail_lsn)) - bytes;
	}
	return free_bytes;
}	/* xlog_space_left */


/*
 * Log function which is called when an io completes.
 *
 * The log manager needs its own routine, in order to control what
 * happens with the buffer after the write completes.
 */
void
xlog_iodone(buf_t *bp)
{
	ASSERT(bp->b_fsprivate2 == (void *)2);
	bp->b_fsprivate2 = (void *)1;

	xlog_state_done_syncing((xlog_in_core_t *)(bp->b_fsprivate));
	if ( !(bp->b_flags & B_ASYNC) ) {
		/* Corresponding psema() will be done in bwrite().  If we don't
		 * vsema() here, panic.
		 */
		vsema(&bp->b_iodonesema);
	}
}	/* xlog_iodone */



/*
 * Return size of each in-core log record buffer.
 *
 * Low memory machines only get 4 8KB buffers.  We don't want to waste
 * memory here.  However, all other machines get at least 4 16KB buffers.
 * The number is hard coded because we don't care about the minimum
 * memory size, just 16MB systems.
 *
 * If the filesystem blocksize is too large, we may need to choose a
 * larger size since the directory code currently logs entire blocks.
 * XXXmiken XXXcurtis
 */
int xlogs = 0;
int xlogsize = 0;
int xloglog = 0;

STATIC void
xlog_get_iclog_buffer_size(xfs_mount_t	*mp,
			   xlog_t	*log)
{
	uint buf_size;

	log->l_iclog_bufs = XLOG_NUM_ICLOGS;
	if (physmem <= btoc(16*1024*1024)) {
		log->l_iclog_size = XLOG_RECORD_BSIZE;		/* 16k */
		log->l_iclog_size_log = XLOG_RECORD_BSHIFT;
	} else {
		log->l_iclog_size = XLOG_MAX_RECORD_BSIZE;	/* 32k */
		log->l_iclog_size_log = XLOG_MAX_RECORD_BSHIFT;
	}

	/*
	 * We can't allow 64k log record sizes because there isn't enough
	 * room in the log record header for all the cycle numbers.
	 */
	ASSERT(XLOG_MAX_RECORD_BSIZE == 32*1024);

	if (mp->m_sb.sb_blocksize == XLOG_MAX_RECORD_BSIZE) {		/* 32k */
		log->l_iclog_size = XLOG_MAX_RECORD_BSIZE;
		log->l_iclog_size_log = XLOG_MAX_RECORD_BSHIFT;
		log->l_iclog_bufs = XLOG_NUM_ICLOGS*2;			/* 4 */
	} else if (mp->m_sb.sb_blocksize > XLOG_MAX_RECORD_BSIZE) {	/* 64k */
		log->l_iclog_size = XLOG_MAX_RECORD_BSIZE;
		log->l_iclog_size_log = XLOG_MAX_RECORD_BSHIFT;
		log->l_iclog_bufs = XLOG_MAX_ICLOGS;			/* 8 */
	}
	if (xlogs != 0) {
		log->l_iclog_bufs = xlogs;
		log->l_iclog_size = xlogsize;
		log->l_iclog_size_log = xloglog;
	}
}	/* xlog_get_iclog_buffer_size */


/*
 * This routine initializes some of the log structure for a given mount point.
 * Its primary purpose is to fill in enough, so recovery can occur.  However,
 * some other stuff may be filled in too.
 */
STATIC xlog_t *
xlog_alloc_log(xfs_mount_t	*mp,
	       dev_t		log_dev,
	       daddr_t		blk_offset,
	       int		num_bblks)
{
	xlog_t			*log;
	xlog_rec_header_t	*head;
	xlog_in_core_t		**iclogp;
	xlog_in_core_t		*iclog, *prev_iclog;
	buf_t			*bp;
	uint			buf_size;
	int			i;

	log = mp->m_log = (void *)kmem_zalloc(sizeof(xlog_t), 0);
	
	log->l_mp	   = mp;
	log->l_dev	   = log_dev;
	log->l_logsize     = BBTOB(num_bblks);
	log->l_logBBstart  = blk_offset;
	log->l_logBBsize   = num_bblks;
	log->l_roundoff	   = 0;
	log->l_flags	   |= XLOG_ACTIVE_RECOVERY;

	log->l_prev_block  = -1;
	log->l_tail_lsn    = 0x100000000LL; /* cycle = 1; current block = 0 */
	log->l_last_sync_lsn = log->l_tail_lsn;
	log->l_curr_cycle  = 1;	    /* 0 is bad since this is initial value */
	log->l_curr_block  = 0;		/* filled in by xlog_recover */
	log->l_grant_reserve_bytes = 0;
	log->l_grant_reserve_cycle = 1;
	log->l_grant_write_bytes = 0;
	log->l_grant_write_cycle = 1;

	xlog_get_iclog_buffer_size(mp, log);
	bp = log->l_xbuf   = getrbuf(0);	/* get my locked buffer */
	bp->b_edev	   = log_dev;
	bp->b_bufsize	   = log->l_iclog_size;
	bp->b_iodone	   = xlog_iodone;
	bp->b_fsprivate2   = (void *)1;
	ASSERT(log->l_xbuf->b_flags & B_BUSY);
	ASSERT(valusema(&log->l_xbuf->b_lock) <= 0);
	initnlock(&log->l_icloglock, "iclog");
	initnlock(&log->l_grant_lock, "grhead_iclog");
	initnsema(&log->l_flushsema, 0, "ic-flush");
/*
	initnsema(&log->l_flushsema, log->l_iclog_bufs, "ic-flush");
*/
	xlog_state_ticket_alloc(log);  /* wait until after icloglock inited */
	
	/* log record size must be multiple of BBSIZE; see xlog_rec_header_t */
	ASSERT((bp->b_bufsize & BBMASK) == 0);

	iclogp = &log->l_iclog;
	for (i=0; i < log->l_iclog_bufs; i++) {
		*iclogp = (xlog_in_core_t *)
			kmem_zalloc(sizeof(xlog_in_core_t), VM_CACHEALIGN);

		ASSERT(sizeof(xlog_in_core_t) >= 4096);
		ASSERT(((__psint_t)*iclogp & (__psint_t)0xfff) == 0);

		iclog = *iclogp;
		iclog->ic_prev = prev_iclog;
		prev_iclog = iclog;
		log->l_iclog_bak[i] = iclog;

		head = &iclog->ic_header;
		head->h_magicno = XLOG_HEADER_MAGIC_NUM;
		head->h_version = 1;
		head->h_lsn = 0;
		head->h_tail_lsn = 0;

		bp = iclog->ic_bp = getrbuf(0);		/* my locked buffer */
		bp->b_edev = log_dev;
		bp->b_bufsize = log->l_iclog_size;
		bp->b_iodone = xlog_iodone;
		bp->b_fsprivate2 = (void *)1;

		iclog->ic_size = bp->b_bufsize - XLOG_HEADER_SIZE;
		iclog->ic_state = XLOG_STATE_ACTIVE;
		iclog->ic_log = log;
		iclog->ic_refcnt = 0;
		iclog->ic_roundoff = 0;
		iclog->ic_bwritecnt = 0;
		iclog->ic_callback = 0;
		iclog->ic_callback_tail = &(iclog->ic_callback);

		ASSERT(iclog->ic_bp->b_flags & B_BUSY);
		ASSERT(valusema(&iclog->ic_bp->b_lock) <= 0);
		initnsema(&iclog->ic_forcesema, 0, "iclog-force");

		iclogp = &iclog->ic_next;
	}
	log->l_iclog_bak[i] = 0;
	*iclogp = log->l_iclog;			/* complete ring */
	log->l_iclog->ic_prev = prev_iclog;	/* re-write 1st prev ptr */
	
	return log;
}	/* xlog_alloc_log */


/*
 * Write out the commit record of a transaction associated with the given
 * ticket.  Return the lsn of the commit record.
 */
STATIC xfs_lsn_t
xlog_commit_record(xfs_mount_t  *mp,
		   xlog_ticket_t *ticket)
{
	int		error;
	xfs_log_iovec_t	reg[1];
	xfs_lsn_t	commit_lsn;
	
	reg[0].i_addr = 0;
	reg[0].i_len = 0;

	error = xlog_write(mp, reg, 1, ticket, &commit_lsn, XLOG_COMMIT_TRANS);
	if (error)
		xlog_panic("xlog_commit_record: Can't commit transaction");

	return commit_lsn;
}	/* xlog_commit_record */


/*
 * Push on the buffer cache code if we ever use more than 75% of the on-disk
 * log space.  This code pushes on the lsn which would supposedly free up
 * the 25% which we want to leave free.  We may need to adopt a policy which
 * pushes on an lsn which is further along in the log once we reach the high
 * water mark.  In this manner, we would be creating a low water mark.
 */
void
xlog_grant_push_ail(xfs_mount_t *mp)
{
    xlog_t	*log = mp->m_log;	/* pointer to the log */
    xfs_lsn_t	tail_lsn;		/* lsn of the log tail */
    xfs_lsn_t	threshhold_lsn = 0;	/* lsn we'd like to be at */
    int		free_blocks;		/* free blocks left to write to */
    int		free_bytes;		/* free bytes left to write to */
    int		threshhold_block;	/* block in lsn we'd like to be at */
    int		spl;			/* last spl level */

    spl = GRANT_LOCK(log);
    free_bytes = xlog_space_left(log,
				 log->l_grant_reserve_cycle,
				 log->l_grant_reserve_bytes);
    tail_lsn = log->l_tail_lsn;
    free_blocks = BTOBBT(free_bytes);

    if (free_blocks < (log->l_logBBsize >> 2)) {
	threshhold_block = BLOCK_LSN(tail_lsn) + (log->l_logBBsize >> 2);
	if (threshhold_block >= log->l_logBBsize) {
	    threshhold_block -= log->l_logBBsize;
	    ((uint *)&threshhold_lsn)[0] = CYCLE_LSN(tail_lsn) +1;
	} else {
	    ((uint *)&threshhold_lsn)[0] = CYCLE_LSN(tail_lsn);
	}
	((uint *)&threshhold_lsn)[1] = threshhold_block;

	/* Don't pass in an lsn greater than the lsn of the last
	 * log record known to be on disk.
	 */
	if (threshhold_lsn > log->l_last_sync_lsn)
	    threshhold_lsn = log->l_last_sync_lsn;
    }
    GRANT_UNLOCK(log, spl);
    if (threshhold_lsn)
	    xfs_trans_push_ail(mp, threshhold_lsn);
}	/* xlog_grant_push_ail */


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
	
	XFSSTATS.xs_log_writes++;
	ASSERT(iclog->ic_refcnt == 0);

	if (flags != 0 && (flags & XFS_LOG_SYNC) )
		xlog_panic("xlog_sync: illegal flag");
	
	xlog_pack_data(log, iclog);       /* put cycle number in every block */
	iclog->ic_header.h_len = iclog->ic_offset;	/* real byte length */

	bp	    = iclog->ic_bp;
	ASSERT(bp->b_fsprivate2 == (void *)1);
	bp->b_fsprivate2 = (void *)2;
	bp->b_blkno = BLOCK_LSN(iclog->ic_header.h_lsn);

	/* Round byte count up to a BBSIZE chunk */
	count = BBTOB(BTOBB(iclog->ic_offset));
	if (iclog->ic_offset != count) {
		/* count of 0 is already accounted for up in
		 * xlog_state_sync_all().  Once in this rountine, operations
		 * on the iclog are single threaded.
		 *
		 * Difference between rounded up size and size
		 */
		iclog->ic_roundoff = count - iclog->ic_offset;
		log->l_roundoff += iclog->ic_roundoff;
	}

	/* Add for LR header */
	count += XLOG_HEADER_SIZE;
	XFSSTATS.xs_log_blocks += BTOBB(count);

	/* Do we need to split this write into 2 parts? */
	if (bp->b_blkno + BTOBB(count) > log->l_logBBsize) {
		split = count - (BBTOB(log->l_logBBsize - bp->b_blkno));
		count = BBTOB(log->l_logBBsize - bp->b_blkno);
		iclog->ic_bwritecnt = 2;	/* split into 2 writes */
	} else {
		iclog->ic_bwritecnt = 1;
	}
	bp->b_dmaaddr	= (caddr_t) iclog;
	bp->b_bcount	= count;
	bp->b_fsprivate	= iclog;		/* save for later */
	if (flags & XFS_LOG_SYNC)
		bp->b_flags |= (B_BUSY | B_HOLD);
	else
		bp->b_flags |= (B_BUSY | B_ASYNC);

	ASSERT(bp->b_blkno <= log->l_logBBsize-1);
	ASSERT(bp->b_blkno + BTOBB(count) <= log->l_logBBsize);
	xlog_verify_iclog(log, iclog, count, B_TRUE);

	/* account for log which don't start at block #0 */
	bp->b_blkno += log->l_logBBstart;

	bwrite(bp);

	if (bp->b_flags & B_ERROR)
		xlog_panic("xlog_sync: buffer error");

	if (split) {
		bp		= iclog->ic_log->l_xbuf;
		ASSERT(bp->b_fsprivate2 == (void *)1);
		bp->b_fsprivate2 = (void *)2;
		bp->b_blkno	= 0;		     /* logical 0 */
		bp->b_bcount	= split;
		bp->b_dmaaddr	= (caddr_t)((__psint_t)iclog+(__psint_t)count);
		bp->b_fsprivate = iclog;
		if (flags & XFS_LOG_SYNC)
			bp->b_flags |= (B_BUSY | B_HOLD);
		else
			bp->b_flags |= (B_BUSY | B_ASYNC);
		dptr = bp->b_dmaaddr;
		for (i=0; i<split; i += BBSIZE) {
			*(uint *)dptr += 1;
			if (*(uint *)dptr == XLOG_HEADER_MAGIC_NUM)
				*(uint *)dptr += 1;
			dptr += BBSIZE;
		}

		ASSERT(bp->b_blkno <= log->l_logBBsize-1);
		ASSERT(bp->b_blkno + BTOBB(count) <= log->l_logBBsize);

		/* account for internal log which does't start at block #0 */
		bp->b_blkno += log->l_logBBstart;

		bwrite(bp);

		if (bp->b_flags & B_ERROR)
			xlog_panic("xlog_sync: buffer error");
	}
}	/* xlog_sync */


/*
 * Unallocate a log structure
 */
void
xlog_unalloc_log(xlog_t *log)
{
	xlog_in_core_t	*iclog, *next_iclog;
	xlog_ticket_t	*tic, *next_tic;
	int		i;

	iclog = log->l_iclog;
	for (i=0; i<log->l_iclog_bufs; i++) {
		freesema(&iclog->ic_forcesema);
		freerbuf(iclog->ic_bp);
		next_iclog = iclog->ic_next;
		kmem_free(iclog, sizeof(xlog_in_core_t));
		iclog = next_iclog;
	}
	freesema(&log->l_flushsema);
	freesplock(log->l_icloglock);
	freesplock(log->l_grant_lock);
	if (log->l_ticket_cnt != log->l_ticket_tcnt) {
		cmn_err(CE_WARN,
			"xlog_unalloc_log: (cnt: %d, total: %d)",
			log->l_ticket_cnt, log->l_ticket_tcnt);
		ASSERT(log->l_ticket_cnt == log->l_ticket_tcnt);
	} else {
		tic = log->l_unmount_free;
		while (tic) {
			next_tic = tic->t_next;
			kmem_free(tic, NBPP);
			tic = next_tic;
		}
	}
}	/* xlog_unalloc_log */


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
	   uint			flags)
{
    xlog_t	     *log    = mp->m_log;
    xlog_ticket_t    *ticket = (xlog_ticket_t *)tic;
    xlog_op_header_t *logop_head;    /* ptr to log operation header */
    xlog_in_core_t   *iclog;	     /* ptr to current in-core log */
    __psint_t	     ptr;	     /* copy address into data region */
    int		     len;	     /* # xlog_write() bytes 2 still copy */
    int		     index;	     /* region index currently copying */
    int		     log_offset;     /* offset (from 0) into data region */
    int		     start_rec_copy; /* # bytes to copy for start record */
    int		     partial_copy;   /* did we split a region? */
    int		     partial_copy_len;/* # bytes copied if split region */
    int		     need_copy;      /* # bytes need to bcopy this region */
    int		     copy_len;	     /* # bytes actually bcopy'ing */
    int		     copy_off;	     /* # bytes from entry start */
    int		     contwr;	     /* continued write of in-core log? */
    int		     firstwr = 0;    /* first write of transaction */
    
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
    contwr = *start_lsn = 0;
    
    if (ticket->t_curr_res < len)
       xlog_panic("xfs_log_write: reservation ran out. Need to up reservation")
    else
	ticket->t_curr_res -= len;
    
    for (index = 0; index < nentries; ) {
	log_offset =
		xlog_state_get_iclog_space(log, len, &iclog, ticket, &contwr);
	ASSERT(log_offset <= iclog->ic_size - 1);
	ptr = (__psint_t) &iclog->ic_data[log_offset];
	
	/* start_lsn is the first lsn written to. That's all we need. */
	if (! *start_lsn)
	    *start_lsn = iclog->ic_header.h_lsn;
	
	/* This loop writes out as many regions as can fit in the amount
	 * of space which was allocated by xlog_state_get_iclog_space().
	 */
	while (index < nentries) {
	    ASSERT(reg[index].i_len % sizeof(long) == 0);
	    ASSERT((__psint_t)ptr % sizeof(long) == 0);
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
	    
	    /* are we copying a commit or unmount record? */
	    logop_head->oh_flags = flags;
	    
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
		/* account for new log op header */
		ticket->t_curr_res -= sizeof(xlog_op_header_t);
	    }
	    xlog_verify_dest_ptr(log, ptr);

	    /* copy region */
	    ASSERT(copy_len >= 0);
	    bcopy(reg[index].i_addr + copy_off, (caddr_t)ptr, copy_len);
	    xlog_write_adv_cnt(ptr, len, log_offset, copy_len);

	    /* make copy_len total bytes copied, including headers */
	    copy_len += start_rec_copy + sizeof(xlog_op_header_t);
	    xlog_state_finish_copy(log, iclog, firstwr, (contwr? copy_len : 0));
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
	} /* while (index < nentries) */
    } /* for (index = 0; index < nentries; ) */
    ASSERT(len == 0);
    
#ifndef _KERNEL
    xlog_state_want_sync(log, iclog);
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
		} else if (iclog->ic_state == XLOG_STATE_ACTIVE)
			/* do nothing */;
		else
			break;	/* stop cleaning */
		iclog = iclog->ic_next;
	} while (iclog != log->l_iclog);
}	/* xlog_state_clean_log */


STATIC void
xlog_state_do_callback(xlog_t *log)
{
	xlog_in_core_t	   *iclog, *first_iclog;
	xfs_log_callback_t *cb, *cb_next;
	int		   spl;
	int		   flushcnt = 0;

	spl = LOG_LOCK(log);
	first_iclog = iclog = log->l_iclog;

	do {
		/* skip all iclogs in the ACTIVE & DIRTY states */
		if (iclog->ic_state == XLOG_STATE_ACTIVE ||
		    iclog->ic_state == XLOG_STATE_DIRTY) {
			iclog = iclog->ic_next;
			continue;
		}

		/* Can only perform callbacks in order.  Since
		 * this iclog is not in the DONE_SYNC state, we skip
		 * the rest and just try to clean up.
		 */
		if (iclog->ic_state != XLOG_STATE_DONE_SYNC)
			goto clean;

		iclog->ic_state = XLOG_STATE_CALLBACK;
		LOG_UNLOCK(log, spl);

		/* perform callbacks in the order given */
		for (cb = iclog->ic_callback; cb != 0; cb = cb_next) {
			cb_next = cb->cb_next;
			cb->cb_func(cb->cb_arg);
		}
		iclog->ic_callback_tail = &(iclog->ic_callback);
		iclog->ic_callback = 0;

		/* l_last_sync_lsn field protected by GRANT_LOCK.
		 * Don't worry about iclog's lsn.  No one else can
		 * be here except us.
		 */
		spl = GRANT_LOCK(log);
		log->l_last_sync_lsn = iclog->ic_header.h_lsn;
		GRANT_UNLOCK(log, spl);

		spl = LOG_LOCK(log);
		iclog->ic_state = XLOG_STATE_DIRTY;

		/* wake up threads waiting in xfs_log_force() */
		while (cvsema(&iclog->ic_forcesema));

		iclog = iclog->ic_next;
	} while (first_iclog != iclog);

clean:
	xlog_state_clean_log(log);
	if (log->l_iclog->ic_state == XLOG_STATE_ACTIVE) {
		flushcnt = log->l_flushcnt;
		log->l_flushcnt = 0;
	}
	LOG_UNLOCK(log, spl);
	while (flushcnt--)
		vsema(&log->l_flushsema);
#if 0
	while (cvsema(&log->l_flushsema));
#endif
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

	spl = LOG_LOCK(log);

	ASSERT(iclog->ic_state == XLOG_STATE_SYNCING);
	ASSERT(iclog->ic_refcnt == 0);
	ASSERT(iclog->ic_bwritecnt == 1 || iclog->ic_bwritecnt == 2);

	if (--iclog->ic_bwritecnt == 1) {
		LOG_UNLOCK(log, spl);
		return;
	}

	iclog->ic_state = XLOG_STATE_DONE_SYNC;
	LOG_UNLOCK(log, spl);
	xlog_state_do_callback(log);	/* also cleans log */
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

	spl = LOG_LOCK(log);

	if (first_write)
		iclog->ic_header.h_num_logops++;
	iclog->ic_header.h_num_logops++;
	iclog->ic_offset += copy_bytes;

	LOG_UNLOCK(log, spl);
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
xlog_state_get_iclog_space(xlog_t	  *log,
			   int		  len,
			   xlog_in_core_t **iclogp,
			   xlog_ticket_t  *ticket,
			   int		  *continued_write)
{
	int		  spl;
	int		  log_offset;
	xlog_rec_header_t *head;
	xlog_in_core_t	  *iclog;

	xlog_state_do_callback(log);	/* also cleans log */

restart:
	spl = LOG_LOCK(log);

	iclog = log->l_iclog;
	if (! (iclog->ic_state == XLOG_STATE_ACTIVE)) {
		log->l_flushcnt++;
		LOG_UNLOCK(log, spl);
		xlog_trace_iclog(iclog, XLOG_TRACE_SLEEP_FLUSH);
		XFSSTATS.xs_log_noiclogs++;
		psema(&log->l_flushsema, PINOD);
		goto restart;
	}
	ASSERT(iclog->ic_state == XLOG_STATE_ACTIVE);
	head = &iclog->ic_header;

	iclog->ic_refcnt++;			/* prevents sync */
	log_offset = iclog->ic_offset;

	/* On the 1st write to an iclog, figure out lsn.  This works
	 * if iclogs marked XLOG_STATE_WANT_SYNC always write out what they are
	 * committing to.  If the offset is set, that's how many blocks
	 * must be written.
	 */
	if (log_offset == 0) {
		ticket->t_curr_res -= XLOG_HEADER_SIZE;
		head->h_cycle = log->l_curr_cycle;
		ASSIGN_LSN(head->h_lsn, log);
		ASSERT(log->l_curr_block >= 0);

		/* round off error from last write with this iclog */
		ticket->t_curr_res -= iclog->ic_roundoff;
		log->l_roundoff -= iclog->ic_roundoff;
		iclog->ic_roundoff = 0;
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
		xlog_state_switch_iclogs(log, iclog, iclog->ic_size);

		/* If I'm the only one writing to this iclog, sync it to disk */
		if (iclog->ic_refcnt == 1) {
			LOG_UNLOCK(log, spl);
			xlog_state_release_iclog(log, iclog);
		} else {
			iclog->ic_refcnt--;
			LOG_UNLOCK(log, spl);
		}
		goto restart;
	}

	/* Do we have enough room to write the full amount in the remainder
	 * of this iclog?  Or must we continue a write on the next iclog and
	 * mark this iclog as completely taken?  In the case where we switch
	 * iclogs (to mark it taken), this particular iclog will release/sync
	 * to disk in xlog_write().
	 */
	if (len <= iclog->ic_size - iclog->ic_offset) {
		*continued_write = 0;
		iclog->ic_offset += len;
	} else {
		*continued_write = 1;
		xlog_state_switch_iclogs(log, iclog, iclog->ic_size);
	}
	*iclogp = iclog;

	ASSERT(iclog->ic_offset <= iclog->ic_size);
	LOG_UNLOCK(log, spl);
	return log_offset;
}	/* xlog_state_get_iclog_space */


/*
 * Atomically get the log space required for a log ticket.
 *
 * Once a ticket gets put onto the reserveq, it will only return after
 * the needed reservation is satisfied.
 */
STATIC void
xlog_grant_log_space(xlog_t	   *log,
		     xlog_ticket_t *tic)
{
	xlog_ticket_t	 *head;
	int		 free_bytes;
	int		 need_bytes;
	int		 spl;
	

#ifdef DEBUG
	if (log->l_flags & XLOG_ACTIVE_RECOVERY)
		panic("regrant Recovery problem");
#endif

	/* Is there space or do we need to sleep? */
	spl = GRANT_LOCK(log);
	xlog_trace_loggrant(log, tic, "xlog_grant_log_space: enter");

	/* something is already sleeping; insert new transaction at end */
	if (head = log->l_reserve_headq) {
		XLOG_INS_TICKETQ(log->l_reserve_headq, tic);
		xlog_trace_loggrant(log, tic,
				    "xlog_grant_log_space: sleep 1");
		spunlockspl_psema(log->l_grant_lock, spl, &tic->t_sema, PINOD);
		xlog_trace_loggrant(log, tic,
				    "xlog_grant_log_space: wake 1");
		spl = GRANT_LOCK(log);
	}
	if (tic->t_flags & XFS_LOG_PERM_RESERV)
		need_bytes = tic->t_unit_res*tic->t_ocnt;
	else
		need_bytes = tic->t_unit_res;

redo:
	free_bytes = xlog_space_left(log, log->l_grant_reserve_cycle,
				     log->l_grant_reserve_bytes);
	if (free_bytes < need_bytes) {
		if ((tic->t_flags & XLOG_TIC_IN_Q) == 0)
			XLOG_INS_TICKETQ(log->l_reserve_headq, tic);
		xlog_trace_loggrant(log, tic,
				    "xlog_grant_log_space: sleep 2");
		spunlockspl_psema(log->l_grant_lock, spl, &tic->t_sema, PINOD);
		xlog_trace_loggrant(log, tic,
				    "xlog_grant_log_space: wake 2");
		xlog_grant_push_ail(log->l_mp);
		spl = GRANT_LOCK(log);
		goto redo;
	} else if (tic->t_flags & XLOG_TIC_IN_Q)
		XLOG_DEL_TICKETQ(log->l_reserve_headq, tic);

	/* we've got enough space */
	XLOG_GRANT_ADD_SPACE(log, need_bytes, 'w');
	XLOG_GRANT_ADD_SPACE(log, need_bytes, 'r');
#ifdef DEBUG
	if (CYCLE_LSN(log->l_tail_lsn) != log->l_grant_write_cycle) {
		ASSERT(log->l_grant_write_cycle-1 == CYCLE_LSN(log->l_tail_lsn));
		ASSERT(log->l_grant_write_bytes < BBTOB(BLOCK_LSN(log->l_tail_lsn)));
	}
#endif
	xlog_trace_loggrant(log, tic, "xlog_grant_log_space: exit");
	xlog_verify_grant_head(log, 1);
	GRANT_UNLOCK(log, spl);
	return;
}	/* xlog_grant_log_space */


/*
 * Replenish the byte reservation required by moving the grant write head.
 *
 * 
 */
STATIC void
xlog_regrant_write_log_space(xlog_t	   *log,
			     xlog_ticket_t *tic)
{
	xlog_ticket_t	*head;
	int		spl;
	int		free_bytes, need_bytes;

	tic->t_curr_res = tic->t_unit_res;

	if (tic->t_cnt > 0)
		return;

#ifdef DEBUG
	if (log->l_flags & XLOG_ACTIVE_RECOVERY)
		panic("regrant Recovery problem");
#endif

	spl = GRANT_LOCK(log);
	xlog_trace_loggrant(log, tic, "xlog_regrant_write_log_space: enter");

	need_bytes = tic->t_unit_res;
redo:
	free_bytes = xlog_space_left(log, log->l_grant_write_cycle,
				     log->l_grant_write_bytes);
	if (free_bytes < need_bytes) {
		if ((tic->t_flags & XLOG_TIC_IN_Q) == 0)
			XLOG_INS_TICKETQ(log->l_write_headq, tic);
		spunlockspl_psema(log->l_grant_lock, spl, &tic->t_sema, PINOD);
		xlog_trace_loggrant(log, tic,
				    "xlog_regrant_write_log_space: wake 1");
		xlog_grant_push_ail(log->l_mp);
		spl = GRANT_LOCK(log);
		goto redo;
	} else if (tic->t_flags & XLOG_TIC_IN_Q)
		XLOG_DEL_TICKETQ(log->l_write_headq, tic);

	XLOG_GRANT_ADD_SPACE(log, need_bytes, 'w'); /* we've got enough space */
#ifdef DEBUG
	if (CYCLE_LSN(log->l_tail_lsn) != log->l_grant_write_cycle) {
		ASSERT(log->l_grant_write_cycle-1 == CYCLE_LSN(log->l_tail_lsn));
		ASSERT(log->l_grant_write_bytes < BBTOB(BLOCK_LSN(log->l_tail_lsn)));
	}
#endif

	xlog_trace_loggrant(log, tic, "xlog_regrant_write_log_space: exit");
	xlog_verify_grant_head(log, 1);
	GRANT_UNLOCK(log, spl);
}	/* xlog_regrant_write_log_space */


/* The first cnt-1 times through here we don't need to
 * move the grant write head because the permanent
 * reservation has reserved cnt times the unit amount.
 * Release part of current permanent unit reservation and
 * reset current reservation to be one units worth.  Also
 * move grant reservation head forward.
 */
STATIC void
xlog_regrant_reserve_log_space(xlog_t	     *log,
			       xlog_ticket_t *ticket)
{
	int spl;

	xlog_trace_loggrant(log, ticket,
			    "xlog_regrant_reserve_log_space: enter");
	if (ticket->t_cnt > 0)
		ticket->t_cnt--;

	spl = GRANT_LOCK(log);
	XLOG_GRANT_SUB_SPACE(log, ticket->t_curr_res, 'w');
	XLOG_GRANT_SUB_SPACE(log, ticket->t_curr_res, 'r');
	ticket->t_curr_res = ticket->t_unit_res;
	xlog_trace_loggrant(log, ticket,
			    "xlog_regrant_reserve_log_space: sub current res");
	xlog_verify_grant_head(log, 1);

	/* just return if we still have some of the pre-reserved space */
	if (ticket->t_cnt > 0) {
		GRANT_UNLOCK(log, spl);
		return;
	}

	XLOG_GRANT_ADD_SPACE(log, ticket->t_unit_res, 'r');
	xlog_trace_loggrant(log, ticket,
			    "xlog_regrant_reserve_log_space: exit");
	xlog_verify_grant_head(log, 0);
	GRANT_UNLOCK(log, spl);
	ticket->t_curr_res = ticket->t_unit_res;
}	/* xlog_regrant_reserve_log_space */


/*
 * Give back the space left from a reservation.
 *
 * All the information we need to make a correct determination of space left
 * is present.  For non-permanent reservations, things are quite easy.  The
 * count should have been decremented to zero.  We only need to deal with the
 * space remaining in the current reservation part of the ticket.  If the
 * ticket contains a permanent reservation, there may be left over space which
 * needs to be released.  A count of N means that N-1 refills of the current
 * reservation can be done before we need to ask for more space.  The first
 * one goes to fill up the first current reservation.  Once we run out of
 * space, the count will stay at zero and the only space remaining will be
 * in the current reservation field.
 */
STATIC void
xlog_ungrant_log_space(xlog_t	     *log,
		       xlog_ticket_t *ticket)
{
	int spl;
	int unused_bytes;

	if (ticket->t_cnt > 0)
		ticket->t_cnt--;

	spl = GRANT_LOCK(log);
	xlog_trace_loggrant(log, ticket, "xlog_ungrant_log_space: enter");

	XLOG_GRANT_SUB_SPACE(log, ticket->t_curr_res, 'w');
	XLOG_GRANT_SUB_SPACE(log, ticket->t_curr_res, 'r');

	xlog_trace_loggrant(log, ticket, "xlog_ungrant_log_space: sub current");

	/* If this is a permanent reservation ticket, we may be able to free
	 * up more space based on the remaining count.
	 */
	if (ticket->t_cnt > 0) {
		ASSERT(ticket->t_flags & XLOG_TIC_PERM_RESERV);
		XLOG_GRANT_SUB_SPACE(log, ticket->t_unit_res*ticket->t_cnt,'w');
		XLOG_GRANT_SUB_SPACE(log, ticket->t_unit_res*ticket->t_cnt,'r');
	}

	xlog_trace_loggrant(log, ticket, "xlog_ungrant_log_space: exit");
	xlog_verify_grant_head(log, 1);
	GRANT_UNLOCK(log, spl);
	xfs_log_move_tail(log->l_mp, 1);
}	/* xlog_ungrant_log_space */


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

	spl = LOG_LOCK(log);

	iclog = log->l_iclog;
	do {
		if (iclog->ic_header.h_lsn != lsn) {
			iclog = iclog->ic_next;
			continue;
		} else {
			if ((iclog->ic_state == XLOG_STATE_CALLBACK) ||
			    (iclog->ic_state == XLOG_STATE_DIRTY)) /* call it*/
				break;

			/* insert callback onto end of list */
			cb->cb_next = 0;
			*(iclog->ic_callback_tail) = cb;
			iclog->ic_callback_tail = &(cb->cb_next);
			lsn_is_synced = 0;
			break;
		}
	} while (iclog != log->l_iclog);

	LOG_UNLOCK(log, spl);
	return lsn_is_synced;
}	/* xlog_state_lsn_is_synced */


/*
 * Atomically put back used ticket.
 */
void
xlog_state_put_ticket(xlog_t	    *log,
		      xlog_ticket_t *tic)
{
	int spl;

	spl = LOG_LOCK(log);
	xlog_ticket_put(log, tic);
	LOG_UNLOCK(log, spl);
}	/* xlog_state_put_ticket */


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
	int		blocks;
    
	xlog_assign_tail_lsn(log->l_mp, 0);

	spl = LOG_LOCK(log);
	ASSERT(iclog->ic_refcnt > 0);
	ASSERT(iclog->ic_state == XLOG_STATE_ACTIVE || iclog->ic_state == XLOG_STATE_WANT_SYNC);
	
	if (--iclog->ic_refcnt == 0 &&
	    iclog->ic_state == XLOG_STATE_WANT_SYNC) {
		sync++;
		iclog->ic_state = XLOG_STATE_SYNCING;
		iclog->ic_header.h_tail_lsn = log->l_tail_lsn;
		xlog_verify_tail_lsn(log, iclog, log->l_tail_lsn);
		/* cycle incremented when incrementing curr_block */
	}
	
	LOG_UNLOCK(log, spl);
	
	if (sync)
		xlog_sync(log, iclog, 0);

}	/* xlog_state_release_iclog */


/*
 * This routine will mark the current iclog in the ring as WANT_SYNC
 * and move the current iclog pointer to the next iclog in the ring.
 * When this routine is called from xlog_state_get_iclog_space(), the
 * exact size of the iclog has not yet been determined.  All we know is
 * that every data block.  We have run out of space in this log record.
 */
STATIC void
xlog_state_switch_iclogs(xlog_t		*log,
			 xlog_in_core_t *iclog,
			 int		eventual_size)
{
	ASSERT(iclog->ic_state == XLOG_STATE_ACTIVE);
	if (!eventual_size)
		eventual_size = iclog->ic_offset;
	iclog->ic_state = XLOG_STATE_WANT_SYNC;
	iclog->ic_header.h_prev_block = log->l_prev_block;
	log->l_prev_block = log->l_curr_block;
	log->l_prev_cycle = log->l_curr_cycle;
	
	/* roll log?: ic_offset changed later */
	log->l_curr_block += BTOBB(eventual_size)+1;
	if (log->l_curr_block >= log->l_logBBsize) {
		log->l_curr_cycle++;
		if (log->l_curr_cycle == XLOG_HEADER_MAGIC_NUM)
			log->l_curr_cycle++;
		log->l_curr_block -= log->l_logBBsize;
		ASSERT(log->l_curr_block >= 0);
	}
	ASSERT(iclog == log->l_iclog);
	log->l_iclog = iclog->ic_next;
}	/* xlog_state_switch_iclogs */


/*
 * Write out all data in the in-core log as of this exact moment in time.
 *
 * Data may be written to the in-core log during this call.  However,
 * we don't guarantee this data will be written out.  A change from past
 * implementation means this routine will *not* write out zero length LRs.
 *
 * Basically, we try and perform an intelligent scan of the in-core logs.
 * If we determine there is no flushable data, we just return.  There is no
 * flushable data if:
 *
 *	1. the current iclog is active and has no data; the previous iclog
 *		is in the active or dirty state.
 *	2. the current iclog is drity, and the previous iclog is in the
 *		active or dirty state.
 *
 * We may sleep (call psema) if:
 *
 *	1. the current iclog is not in the active nor dirty state.
 *	2. the current iclog dirty, and the previous iclog is not in the
 *		active nor dirty state.
 *	3. the current iclog is active, and there is another thread writing
 *		to this particular iclog.
 *	4. a) the current iclog is active and has no other writers
 *	   b) when we return from flushing out this iclog, it is still
 *		not in the active nor dirty state.
 */
STATIC int
xlog_state_sync_all(xlog_t *log, uint flags)
{
	xlog_in_core_t	*iclog;
	xfs_lsn_t	lsn;
	int		spl;

	spl = LOG_LOCK(log);

	iclog = log->l_iclog;

	/* If the head iclog is not active nor dirty, we just attach
	 * ourselves to the head and go to sleep.
	 */
	if (iclog->ic_state == XLOG_STATE_ACTIVE ||
	    iclog->ic_state == XLOG_STATE_DIRTY) {
		/*
		 * If the head is dirty or (active and empty), then
		 * we need to look at the previous iclog.  If the previous
		 * iclog is active or dirty we are done.  There is nothing
		 * to sync out.  Otherwise, we attach ourselves to the
		 * previous iclog and go to sleep.
		 */
		if (iclog->ic_state == XLOG_STATE_DIRTY ||
		    (iclog->ic_refcnt == 0 && iclog->ic_offset == 0)) {
			iclog = iclog->ic_prev;
			if (iclog->ic_state == XLOG_STATE_ACTIVE ||
			    iclog->ic_state == XLOG_STATE_DIRTY)
				goto no_sleep;
			else
				goto maybe_sleep;
		} else {
			if (iclog->ic_refcnt == 0) {
				/* We are the only one with access to this
				 * iclog.  Flush it out now.  There should
				 * be a roundoff of zero to show that someone
				 * has already taken care of the roundoff from
				 * the previous sync.
				 */
				ASSERT(iclog->ic_roundoff == 0);
				iclog->ic_refcnt++;
				lsn = iclog->ic_header.h_lsn;
				xlog_state_switch_iclogs(log, iclog, 0);
				LOG_UNLOCK(log, spl);
				xlog_state_release_iclog(log, iclog);
				spl = LOG_LOCK(log);
				if (iclog->ic_header.h_lsn == lsn &&
				    iclog->ic_state != XLOG_STATE_DIRTY)
					goto maybe_sleep;
				else
					goto no_sleep;
			} else {
				/* Someone else is writing to this iclog.
				 * Use its call to flush out the data.  However,
				 * the other thread may not force out this LR,
				 * so we mark it WANT_SYNC.
				 */
				xlog_state_switch_iclogs(log, iclog, 0);
				goto maybe_sleep;
			}
		}
	}

	/* By the time we come around again, the iclog could been filled
	 * which would give it another lsn.  If we have a new lsn, just
	 * return because the relevant data has been flushed.
	 */
maybe_sleep:		
	if (flags & XFS_LOG_SYNC) {
		spunlockspl_psema(log->l_icloglock, spl,	/* sleep */
				  &iclog->ic_forcesema, PINOD);
	} else {

no_sleep:
		LOG_UNLOCK(log, spl);
	}
	return 0;
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
 * If filesystem activity goes to zero, the iclog will get flushed only by
 * bdflush().
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

	spl = LOG_LOCK(log);
	iclog = log->l_iclog;
	do {
		if (iclog->ic_header.h_lsn != lsn) {
			iclog = iclog->ic_next;
		} else {
		    if (iclog->ic_state == XLOG_STATE_ACTIVE) {
			    iclog->ic_refcnt++;
			    xlog_state_switch_iclogs(log, iclog, 0);
			    spunlockspl(log->l_icloglock, spl);
			    xlog_state_release_iclog(log, iclog);
			    spl = splockspl(log->l_icloglock, splhi);
		    } else if (iclog->ic_state == XLOG_STATE_DIRTY) {
			    LOG_UNLOCK(log, spl);
			    return 0;
		    }
		    if ((flags & XFS_LOG_SYNC) &&		    /* sleep */
			!(iclog->ic_state == XLOG_STATE_ACTIVE ||
			  iclog->ic_state == XLOG_STATE_DIRTY)) {
			    spunlockspl_psema(log->l_icloglock, spl,
					      &iclog->ic_forcesema, 0);
		    } else {				/* just return */
			    LOG_UNLOCK(log, spl);
		    }
		    return 0;
		}
	} while (iclog != log->l_iclog);

	LOG_UNLOCK(log, spl);
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
	
	spl = LOG_LOCK(log);
	
	if (iclog->ic_state == XLOG_STATE_ACTIVE)
		xlog_state_switch_iclogs(log, iclog, 0);
	else if (iclog->ic_state != XLOG_STATE_WANT_SYNC)
		xlog_panic("xlog_state_want_sync: bad state");
	
	LOG_UNLOCK(log, spl);
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
STATIC void
xlog_state_ticket_alloc(xlog_t *log)
{
	xlog_ticket_t	*t_list;
	xlog_ticket_t	*next;
	caddr_t		buf;
	int		spl;
	uint		i = (NBPP / sizeof(xlog_ticket_t)) - 2;

	/*
	 * The kmem_zalloc may sleep, so we shouldn't be holding the
	 * global lock.  XXXmiken: may want to use zone allocator.
	 */
	buf = (caddr_t) kmem_zalloc(NBPP, 0);

	spl = LOG_LOCK(log);

	/* Attach 1st ticket to Q, so we can keep track of allocated memory */
	t_list = (xlog_ticket_t *)buf;
	t_list->t_next = log->l_unmount_free;
	log->l_unmount_free = t_list++;
	log->l_ticket_cnt++;
	log->l_ticket_tcnt++;

	/* Next ticket becomes first ticket attached to ticket free list */
	if (log->l_freelist != NULL) {
		ASSERT(log->l_tail != NULL);
		log->l_tail->t_next = t_list;
	} else {
		log->l_freelist = t_list;
	}
	log->l_ticket_cnt++;
	log->l_ticket_tcnt++;

	/* Cycle through rest of alloc'ed memory, building up free Q */
	for ( ; i > 0; i--) {
		next = t_list + 1;
		t_list->t_next = next;
		t_list = next;
		log->l_ticket_cnt++;
		log->l_ticket_tcnt++;
	}
	t_list->t_next = 0;
	log->l_tail = t_list;
	LOG_UNLOCK(log, spl);
}	/* xlog_state_ticket_alloc */


/*
 * Put ticket into free list
 *
 * Assumption: log lock is held around this call.
 */
STATIC void
xlog_ticket_put(xlog_t		*log,
		xlog_ticket_t	*ticket)
{
	xlog_ticket_t *t_list;

	freesema(&ticket->t_sema);
#ifndef DEBUG
	/* real code will want to use LIFO for caching */
	ticket->t_next = log->l_freelist;
	log->l_freelist = ticket;
	/* no need to clear fields */
#else
	/* When we debug, it is easier if tickets are cycled */
	ticket->t_next     = 0;
	if (log->l_tail != 0) {
		log->l_tail->t_next = ticket;
	} else {
		ASSERT(log->l_freelist == 0);
		log->l_freelist = ticket;
	}
	log->l_tail	    = ticket;
#endif /* DEBUG */
	log->l_ticket_cnt++;
}	/* xlog_ticket_put */


/*
 * Grab ticket off freelist or allocation some more
 */
xfs_log_ticket_t *
xlog_ticket_get(xlog_t		*log,
		int		unit_bytes,
		int		cnt,
		char		client,
		uint		xflags)
{
	xlog_ticket_t	*tic;
	int		spl;

 alloc:
	if (log->l_freelist == NULL)
		xlog_state_ticket_alloc(log);		/* potentially sleep */

	spl = LOG_LOCK(log);
	if (log->l_freelist == NULL) {
		LOG_UNLOCK(log, spl);
		goto alloc;
	}
	tic		= log->l_freelist;
	log->l_freelist	= tic->t_next;
	if (log->l_freelist == NULL)
		log->l_tail = NULL;
	log->l_ticket_cnt--;
	LOG_UNLOCK(log, spl);

	/*
	 * Permanent reservations have up to 'cnt'-1 active log operations
	 * in the log.  A unit in this case is the amount of space for one
	 * of these log operations.  Normal reservations have a cnt of 1
	 * and their unit amount is the total amount of space required.
	 * The following line of code adds one log record header length
	 * for each part of an operation which may fall on a different
	 * log record.
	 *
	 * One more XLOG_HEADER_SIZE is added to account for possible
	 * round off errors when syncing a LR to disk.  The bytes are
	 * subtracted if the thread using this ticket is the first writer
	 * to a new LR.
	 */
	unit_bytes += XLOG_HEADER_SIZE * (XLOG_BTOLRBB(unit_bytes) + 1);

	tic->t_unit_res		= unit_bytes;
	tic->t_curr_res		= unit_bytes;
	tic->t_cnt		= cnt;
	tic->t_ocnt		= cnt;
	tic->t_tid		= (xlog_tid_t)tic;
	tic->t_clientid		= client;
	tic->t_flags		= XLOG_TIC_INITED;
	if (xflags & XFS_LOG_PERM_RESERV)
		tic->t_flags |= XLOG_TIC_PERM_RESERV;
	initnsema(&(tic->t_sema), 0, "logtick");

	return (xfs_log_ticket_t)tic;
}	/* xlog_ticket_get */


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
xlog_verify_dest_ptr(xlog_t     *log,
		     __psint_t  ptr)
{
#ifdef DEBUG
	int i;
	int good_ptr = 0;

	for (i=0; i < log->l_iclog_bufs; i++) {
		if (ptr >= (__psint_t)log->l_iclog_bak[i] &&
		    ptr <= (__psint_t)log->l_iclog_bak[i]+log->l_iclog_size)
			good_ptr++;
	}
	if (! good_ptr)
		xlog_panic("xlog_verify_dest_ptr: invalid ptr");
#endif /* DEBUG */
}	/* xlog_verify_dest_ptr */


/* check split LR write */
STATIC void
xlog_verify_disk_cycle_no(xlog_t	 *log,
			  xlog_in_core_t *iclog)
{
    buf_t	*bp;
    uint	cycle_no;
    daddr_t	i;

    if (BLOCK_LSN(iclog->ic_header.h_lsn) < 10) {
	cycle_no = CYCLE_LSN(iclog->ic_header.h_lsn);
	bp = xlog_get_bp(1);
	for (i = 0; i < BLOCK_LSN(iclog->ic_header.h_lsn); i++) {
	    xlog_bread(log, i, 1, bp);
	    if (GET_CYCLE(bp->b_dmaaddr) != cycle_no)
		xlog_warn("xFS: xlog_verify_disk_cycle_no: bad cycle no");
	}
	xlog_put_bp(bp);
    }
}	/* xlog_verify_disk_cycle_no */


STATIC void
xlog_verify_grant_head(xlog_t *log, int equals)
{
    if (log->l_grant_reserve_cycle == log->l_grant_write_cycle) {
	if (equals)
	    ASSERT(log->l_grant_reserve_bytes >= log->l_grant_write_bytes);
	else
	    ASSERT(log->l_grant_reserve_bytes > log->l_grant_write_bytes);
    } else {
	ASSERT(log->l_grant_reserve_cycle-1 == log->l_grant_write_cycle);
	ASSERT(log->l_grant_write_bytes >= log->l_grant_reserve_bytes);
    }
}	/* xlog_verify_grant_head */


/* check if it will fit */
STATIC void
xlog_verify_tail_lsn(xlog_t	    *log,
		     xlog_in_core_t *iclog,
		     xfs_lsn_t	    tail_lsn)
{
#ifdef DEBUG
    int blocks;

    if (CYCLE_LSN(tail_lsn) == log->l_prev_cycle) {
	blocks =
	    log->l_logBBsize - (log->l_prev_block - BLOCK_LSN(tail_lsn));
	if (blocks < BTOBB(iclog->ic_offset)+1)
	    xlog_panic("xlog_verify_tail_lsn: ran out of log space");
    } else {
	ASSERT(CYCLE_LSN(tail_lsn)+1 == log->l_prev_cycle);

	if (BLOCK_LSN(tail_lsn) == log->l_prev_block)
	    xlog_panic("xlog_verify_tail_lsn: tail wrapped");
		
	blocks = BLOCK_LSN(tail_lsn) - log->l_prev_block;
	if (blocks < BTOBB(iclog->ic_offset) + 1)
	    xlog_panic("xlog_verify_tail_lsn: ran out of log space");
    }
#endif /* DEBUG */
}	/* xlog_verify_tail_lsn */


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
STATIC void
xlog_verify_iclog(xlog_t	 *log,
		  xlog_in_core_t *iclog,
		  int		 count,
		  boolean_t	 syncing)
{
#ifdef DEBUG
	xlog_op_header_t  *ophead;
	xlog_rec_header_t *rec;
	xlog_in_core_t	 *icptr;
	xlog_tid_t	 tid;
	caddr_t		 ptr;
	char		 clientid;
	int		 len, fd, i, op_len, cycle_no, spl;
	buf_t		 *bp;

	/* check validity of iclog pointers */
	spl = LOG_LOCK(log);
	icptr = log->l_iclog;
	for (i=0; i < log->l_iclog_bufs; i++) {
		if (icptr == 0)
			xlog_panic("xlog_verify_iclog: illegal ptr");
		icptr = icptr->ic_next;
	}
	if (icptr != log->l_iclog)
		xlog_panic("xlog_verify_iclog: corrupt iclog ring");
	LOG_UNLOCK(log, spl);

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
		    ((__psint_t)&ophead->oh_clientid & 0x1ff))
			clientid = ophead->oh_clientid;
		else
			clientid = iclog->ic_header.h_cycle_data[BTOBB(&ophead->oh_clientid - iclog->ic_data)]>>24;
		if (clientid != XFS_TRANSACTION && clientid != XFS_LOG)
			xlog_panic("xlog_verify_iclog: illegal client");

		/* check tids */
		if (syncing == B_FALSE ||
		    ((__psint_t)&ophead->oh_tid & 0x1ff))
			tid = ophead->oh_tid;
		else
			tid = (xlog_tid_t)iclog->ic_header.h_cycle_data[BTOBB((__psint_t)&ophead->oh_tid - (__psint_t)iclog->ic_data)];

#ifndef _KERNEL
		/* This is a user space check */
		if ((__psint_t)tid < 0x10000000 || (__psint_t)tid > 0x20000000)
			xlog_panic("xlog_verify_iclog: illegal tid");
#endif

		/* check length */
		if (syncing == B_FALSE ||
		    ((__psint_t)&ophead->oh_len & 0x1ff))
			op_len = ophead->oh_len;
		else
			op_len = iclog->ic_header.h_cycle_data[BTOBB((__psint_t)&ophead->oh_len - (__psint_t)iclog->ic_data)];
		len -= sizeof(xlog_op_header_t) + op_len;
		ptr += sizeof(xlog_op_header_t) + op_len;
	}
	if (len != 0)
		xlog_panic("xlog_verify_iclog: illegal iclog");

#endif /* DEBUG */
}	/* xlog_verify_iclog */
