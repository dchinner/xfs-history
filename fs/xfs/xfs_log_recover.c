#ident	"$Revision: 1.55 $"

#ifdef SIM
#define _KERNEL 1
#endif

#include <sys/types.h>
#include <sys/param.h>

#include <sys/sysmacros.h>
#include <sys/buf.h>
#include <sys/sema.h>
#include <sys/vnode.h>

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

#include <sys/errno.h>
#include <sys/kmem.h>
#include <sys/debug.h>
#include <sys/ktrace.h>
#include <sys/user.h>
#include <sys/vfs.h>
#include <stddef.h>

#include <sys/fs/xfs_types.h>
#include <sys/fs/xfs_inum.h>
#include <sys/fs/xfs_log.h>
#include <sys/fs/xfs_ag.h>		/* needed by xfs_sb.h */
#include <sys/fs/xfs_sb.h>		/* depends on xfs_types.h, xfs_inum.h */
#include <sys/fs/xfs_trans.h>
#include <sys/fs/xfs_mount.h>		/* depends on xfs_trans.h & xfs_sb.h */
#include <sys/fs/xfs_bmap_btree.h>
#include <sys/fs/xfs_alloc.h>
#include <sys/fs/xfs_dir.h>
#include <sys/fs/xfs_dinode.h>
#include <sys/fs/xfs_imap.h>
#include <sys/fs/xfs_inode_item.h>
#include <sys/fs/xfs_inode.h>
#include <sys/fs/xfs_ialloc.h>
#include <sys/fs/xfs_error.h>
#include <sys/fs/xfs_log_priv.h>	/* depends on all above */
#include <sys/fs/xfs_buf_item.h>
#include <sys/fs/xfs_alloc_btree.h>
#include <sys/fs/xfs_log_recover.h>
#include <sys/fs/xfs_extfree_item.h>
#include <sys/fs/xfs_trans_priv.h>


#ifdef SIM
#include "sim.h"		/* must be last include file */
#endif

STATIC int	xlog_find_zeroed(xlog_t *log, daddr_t *blk_no);
STATIC void	xlog_recover_insert_item_backq(xlog_recover_item_t **q,
					       xlog_recover_item_t *item);
STATIC void	xlog_recover_insert_item_frontq(xlog_recover_item_t **q,
						xlog_recover_item_t *item);

#ifdef DEBUG
STATIC void	xlog_recover_check_summary(xlog_t *log);
STATIC void	xlog_recover_check_ail(xfs_mount_t *mp, xfs_log_item_t *lip,
				       int gen);
#else
#define	xlog_recover_check_summary(log)
#define	xlog_recover_check_ail(mp, lip, gen)
#endif

buf_t *
xlog_get_bp(int num_bblks)
{
	buf_t   *bp;

	ASSERT(num_bblks > 0);
	bp = ngetrbuf(BBTOB(num_bblks));
	return bp;
}	/* xlog_get_bp */


void
xlog_put_bp(buf_t *bp)
{
	nfreerbuf(bp);
}	/* xlog_get_bp */


/*
 * nbblks should be uint, but oh well.  Just want to catch that 32-bit length.
 */
int
xlog_bread(xlog_t	*log,
	   daddr_t	blk_no,
	   int		nbblks,
	   buf_t	*bp)
{
	ASSERT(nbblks > 0);
	ASSERT(BBTOB(nbblks) <= bp->b_bufsize);

	bp->b_blkno	= blk_no + log->l_logBBstart;
	bp->b_flags	= B_READ|B_BUSY;
	bp->b_bcount	= BBTOB(nbblks);
	bp->b_edev	= log->l_dev;

#ifndef SIM
	bp_dcache_wbinval(bp);
#endif
	bdstrat(bmajor(bp->b_edev), bp);
	iowait(bp);

	if (bp->b_flags & B_ERROR) {
		cmn_err(CE_WARN, "xFS: error reading log block #%d",
			bp->b_blkno);
		ASSERT(0);
		return bp->b_error;
	}
	return 0;
}	/* xlog_bread */


/*
 * This routine finds (to an approximation) the first block in the physical
 * log which contains the given cycle.  It uses a binary search algorithm.
 * Note that the algorithm can not be perfect because the disk will not
 * necessarily be perfect.
 */
STATIC int
xlog_find_cycle_start(xlog_t	*log,
		      buf_t	*bp,
		      daddr_t	first_blk,
		      daddr_t	*last_blk,
		      uint	cycle)
{
	daddr_t mid_blk;
	uint	mid_cycle;
	int	error;

	mid_blk = BLK_AVG(first_blk, *last_blk);
	while (mid_blk != first_blk && mid_blk != *last_blk) {
		if (error = xlog_bread(log, mid_blk, 1, bp))
			return error;
		mid_cycle = GET_CYCLE(bp->b_dmaaddr);
		if (mid_cycle == cycle) {
			*last_blk = mid_blk;
			/* last_half_cycle == mid_cycle */
		} else {
			first_blk = mid_blk;
			/* first_half_cycle == mid_cycle */
		}
		mid_blk = BLK_AVG(first_blk, *last_blk);
	}
	ASSERT((mid_blk == first_blk && mid_blk+1 == *last_blk) ||
	       (mid_blk == *last_blk && mid_blk-1 == first_blk));

	return 0;
}	/* xlog_find_cycle_start */


/*
 * Check that the range of blocks given all start with the cycle number
 * given.  The scan needs to occur from front to back and the ptr into the
 * region must be updated since a later routine will need to perform another
 * test.  If the region is completely good, we end up returning the same
 * last block number.
 *
 * Return -1 if we encounter no errors.  This is an invalid block number
 * since we don't ever expect logs to get this large.
 */
STATIC daddr_t
xlog_find_verify_cycle(caddr_t	*bap,		/* update ptr as we go */
		       daddr_t	start_blk,
		       int	nbblks,
		       uint	verify_cycle_no,
		       int	equals)
{
	int	i;
	uint	cycle;

	if (equals == 1) {
		for (i=0; i<nbblks; i++) {
			cycle = GET_CYCLE(*bap);
			if (cycle == verify_cycle_no) {
				(*bap) += BBSIZE;
			} else {
				return (start_blk+i);
			}
		}
	} else {
		for (i=0; i<nbblks; i++) {
			cycle = GET_CYCLE(*bap);
			if (cycle != verify_cycle_no) {
				(*bap) += BBSIZE;
			} else {
				return (start_blk+i);
			}
		}
	}
	return -1;
}	/* xlog_find_verify_cycle */


/*
 * Potentially backup over partial log record write.
 *
 * In the typical case, last_blk is the number of the block directly after
 * a good log record.  Therefore, we subtract one to get the block number
 * of the last block in the given buffer.  extra_bblks contains the number
 * of blocks we would have read on a previous read.  This happens when the
 * last log record is split over the end of the physical log.
 *
 * extra_bblks is the number of blocks potentially verified on a previous
 * call to this routine.
 */
STATIC int
xlog_find_verify_log_record(caddr_t	ba,	     /* update ptr as we go */
			    daddr_t	start_blk,
			    daddr_t	*last_blk,
			    int		extra_bblks)
{
    xlog_rec_header_t *rhead;
    int		      i;

    ASSERT(start_blk != 0 || *last_blk != start_blk);

    ba -= BBSIZE;
    for (i=(*last_blk)-1; i>=0; i--) {
	if (*(uint *)ba == XLOG_HEADER_MAGIC_NUM) {
	    break;
	} else {
	    if (i < start_blk) {
		/* legal log record not found */
		xlog_warn("xFS: xlog_find_verify_log_record: need to backup");
		ASSERT(0);
		return XFS_ERROR(EIO);
	    }
	    ba -= BBSIZE;
	}
    }

    /*
     * We hit the beginning of the physical log & still no header.  Return
     * to caller.  If caller can handle a return of -1, then this routine
     * will be called again for the end of the physical log.
     */
    if (i == -1)
	    return -1;

    /*
     * We may have found a log record header before we expected one.
     * last_blk will be the 1st block # with a given cycle #.  We may end
     * up reading an entire log record.  In this case, we don't want to
     * reset last_blk.  Only when last_blk points in the middle of a log
     * record do we update last_blk.
     */
    rhead = (xlog_rec_header_t *)ba;
    if (*last_blk - i + extra_bblks != BTOBB(rhead->h_len)+1)
	    *last_blk = i;
    return 0;
}	/* xlog_find_verify_log_record */


/*
 * Head is defined to be the point of the log where the next log write
 * write could go.  This means that incomplete LR writes at the end are
 * eliminated when calculating the head.  We aren't guaranteed that previous
 * LR have complete transactions.  We only know that a cycle number of 
 * current cycle number -1 won't be present in the log if we start writing
 * from our current block number.
 *
 * last_blk contains the block number of the first block with a given
 * cycle number.
 *
 * Also called from xfs_log_print.c
 *
 * Return: zero if normal, non-zero if error.
 */
int
xlog_find_head(xlog_t  *log,
	       daddr_t *return_head_blk)
{
    buf_t   *bp, *big_bp;
    daddr_t new_blk, first_blk, start_blk, mid_blk, last_blk, head_blk;
    daddr_t num_scan_bblks;
    uint    first_half_cycle, mid_cycle, last_half_cycle, cycle;
    caddr_t ba;
    int     equals, error, i, log_bbnum = log->l_logBBsize;

    /* special case freshly mkfs'ed filesystem; return immediately */
    if ((error = xlog_find_zeroed(log, &first_blk)) == -1) {
	*return_head_blk = first_blk;
	return 0;
    } else if (error)
	return error;

    first_blk = 0;				/* get cycle # of 1st block */
    bp = xlog_get_bp(1);
    if (error = xlog_bread(log, 0, 1, bp))
	goto bp_err;
    first_half_cycle = GET_CYCLE(bp->b_dmaaddr);

    last_blk = head_blk = log_bbnum-1;		/* get cycle # of last block */
    if (error = xlog_bread(log, last_blk, 1, bp))
	goto bp_err;
    last_half_cycle = GET_CYCLE(bp->b_dmaaddr);
    ASSERT(last_half_cycle != 0);

    /*
     * If the 1st half cycle number is equal to the last half cycle number,
     * then the entire log is stamped with the same cycle number.  In this
     * case, head_blk can't be set to zero (which makes sense).  The below
     * math doesn't work out properly with head_blk equal to zero.  Instead,
     * we set it to log_bbnum which is an illegal block number, but this
     * value makes the math correct.  If head_blk doesn't changed through
     * all the tests below, *head_blk is set to zero at the very end rather
     * than log_bbnum.  In a sense, log_bbnum and zero are the same block
     * in a circular file.
     */
    if (first_half_cycle == last_half_cycle) {
	head_blk = log_bbnum;
	equals = 1;
    } else {
	/* Find 1st block # with cycle # matching last_half_cycle */
	equals = 0;
	if (error = xlog_find_cycle_start(log, bp, first_blk,
					  &head_blk, last_half_cycle))
	    goto bp_err;
    }

    /*
     * Now validate the answer.  Scan back some number of maximum possible
     * blocks and make sure each one has the expected cycle number.  The
     * maximum is determined by the total possible amount of buffering
     * in the in-core log.  The following number can be made tighter if
     * we actually look at the block size of the filesystem.
     */
    num_scan_bblks = BTOBB(XLOG_MAX_ICLOGS<<XLOG_MAX_RECORD_BSHIFT);
    big_bp = xlog_get_bp(num_scan_bblks);
    if (head_blk >= num_scan_bblks) {
	/*
	 * We are guaranteed that the entire check can be performed
	 * in one buffer.
	 */
	start_blk = head_blk - num_scan_bblks;
	if (error = xlog_bread(log, start_blk, num_scan_bblks, big_bp))
	    goto big_bp_err;
	ba = big_bp->b_dmaaddr;
	new_blk = xlog_find_verify_cycle(&ba, start_blk, num_scan_bblks,
					 last_half_cycle, equals);
	if (new_blk != -1)
	    head_blk = new_blk;
    } else {			/* need to read 2 parts of log */
	/*
	 * We need to read the log in 2 parts.  Scan the physical end of log
	 * first.  If all the cycle numbers are good, we can then move to the
	 * beginning of the log.  head_blk should be a # close to the beginning
	 * of the log.  In the case of 3 cycle #s, the verification of the
	 * last part of the log is still good.
	 */
	start_blk = log_bbnum - num_scan_bblks + head_blk;
	if (error = xlog_bread(log, start_blk, num_scan_bblks-head_blk, big_bp))
	    goto big_bp_err;
	ba = big_bp->b_dmaaddr;
	new_blk= xlog_find_verify_cycle(&ba, start_blk, num_scan_bblks-head_blk,
					last_half_cycle, 1);
	if (new_blk != -1) {
	    head_blk = new_blk;
	    goto bad_blk;
	}

	/*
	 * Scan beginning of log now.  The last part of the physical log
	 * is good.  This scan needs to verify that it doesn't find the
	 * last_half_cycle.
	 */
	start_blk = 0;
	if (error = xlog_bread(log, start_blk, head_blk, big_bp))
	    goto big_bp_err;
	ba = big_bp->b_dmaaddr;
	new_blk = xlog_find_verify_cycle(&ba, start_blk, head_blk,
					 last_half_cycle, 0);
	if (new_blk != -1)
	    head_blk = new_blk;
    }

bad_blk:
    /*
     * Now we need to make sure head_blk is not pointing to a block in
     * the middle of a log record.
     */
    num_scan_bblks = BTOBB(XLOG_MAX_RECORD_BSIZE);
    if (head_blk >= num_scan_bblks) {
	start_blk = head_blk - num_scan_bblks;  /* don't read head_blk */
	if (error = xlog_bread(log, start_blk, num_scan_bblks, big_bp))
	    goto big_bp_err;

	/* start ptr at last block ptr before head_blk */
	ba = big_bp->b_dmaaddr + XLOG_MAX_RECORD_BSIZE;
	if ((error = xlog_find_verify_log_record(ba,
						 start_blk,
						 &head_blk,
						 0)) == -1) {
	    error = XFS_ERROR(EIO);
	    goto big_bp_err;
	} else if (error)
	    goto big_bp_err;
    } else {
	start_blk = 0;
	if (error = xlog_bread(log, start_blk, head_blk, big_bp))
	    goto big_bp_err;
	ba = big_bp->b_dmaaddr + BBTOB(head_blk);
	if ((error = xlog_find_verify_log_record(ba,
						 start_blk,
						 &head_blk,
						 0)) == -1) {
	    /* We hit the beginning of the log during our search */
	    start_blk = log_bbnum - num_scan_bblks + head_blk;
	    new_blk = log_bbnum;
	    if (error = xlog_bread(log, start_blk, log_bbnum - start_blk,
				   big_bp))
		goto big_bp_err;
	    ba = big_bp->b_dmaaddr + BBTOB(log_bbnum - start_blk);
	    if ((error = xlog_find_verify_log_record(ba,
						     start_blk,
						     &new_blk,
						     head_blk)) == -1) {
		error = XFS_ERROR(EIO);
		goto big_bp_err;
	    } else if (error)
		goto big_bp_err;
	    if (new_blk != log_bbnum)
		head_blk = new_blk;
	} else if (error)
	    goto big_bp_err;
    }

    xlog_put_bp(big_bp);
    xlog_put_bp(bp);
    if (head_blk == log_bbnum)
	    *return_head_blk = 0;
    else
	    *return_head_blk = head_blk;
    /*
     * When returning here, we have a good block number.  Bad block
     * means that during a previous crash, we didn't have a clean break
     * from cycle number N to cycle number N-1.  In this case, we need
     * to find the first block with cycle number N-1.
     */
    return 0;

big_bp_err:
	xlog_put_bp(big_bp);
bp_err:
	xlog_put_bp(bp);
	return error;
}	/* xlog_find_head */


#ifndef _KERNEL
/*
 * Start is defined to be the block pointing to the oldest valid log record.
 * Used by log print code.  Don't put in xfs_log_print.c since most of the
 * bread routines live in this module only.
 */
int
xlog_print_find_oldest(xlog_t  *log,
		       daddr_t *last_blk)
{
	buf_t	*bp;
	daddr_t	first_blk;
	uint	first_half_cycle, last_half_cycle;
	int	error;
	
	if (xlog_find_zeroed(log, &first_blk))
		return 0;

	first_blk = 0;					/* read first block */
	bp = xlog_get_bp(1);
	xlog_bread(log, 0, 1, bp);
	first_half_cycle = GET_CYCLE(bp->b_dmaaddr);

	*last_blk = log->l_logBBsize-1;			/* read last block */
	xlog_bread(log, *last_blk, 1, bp);
	last_half_cycle = GET_CYCLE(bp->b_dmaaddr);
	ASSERT(last_half_cycle != 0);

	if (first_half_cycle == last_half_cycle) {/* all cycle nos are same */
		*last_blk = 0;
	} else {		/* have 1st and last; look for middle cycle */
		error = xlog_find_cycle_start(log, bp, first_blk,
					      last_blk, last_half_cycle);
		if (error)
			return error;
	}

	xlog_put_bp(bp);
	return 0;
}	/* xlog_print_find_oldest */
#endif /* _KERNEL */


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
 * We could speed up search by using current head_blk buffer, but it is not
 * available.
 */
int
xlog_find_tail(xlog_t  *log,
	       daddr_t *head_blk,
	       daddr_t *tail_blk)
{
	xlog_rec_header_t *rhead;
	xlog_op_header_t  *op_head;
	buf_t		  *bp;
	int		  error, i, found;
	
	found = error = 0;
	/*
	 * Find previous log record 
	 */
	if (error = xlog_find_head(log, head_blk))
		return error;

	bp = xlog_get_bp(1);
	if (*head_blk == 0) {				/* special case */
		if (error = xlog_bread(log, 0, 1, bp))
			goto bread_err;
		if (GET_CYCLE(bp->b_dmaaddr) == 0) {
			*tail_blk = 0;
			/* leave all other log inited values alone */
			goto exit;
		}
	}

	/*
	 * Search backwards looking for log record header block
	 */
	for (i=(*head_blk)-1; i>=0; i--) {
		if (error = xlog_bread(log, i, 1, bp))
			goto bread_err;
		if (*(uint *)(bp->b_dmaaddr) == XLOG_HEADER_MAGIC_NUM) {
			found = 1;
			break;
		}
	}
	/*
	 * If we haven't found the log record header block, start looking
	 * again from the end of the physical log.  XXXmiken: There should be
	 * a check here to make sure we didn't search more than N blocks in
	 * the previous code.
	 */
	if (!found) {
		for (i=log->l_logBBsize-1; i>=(*head_blk); i--) {
			if (error = xlog_bread(log, i, 1, bp))
				goto bread_err;
			if (*(uint*)(bp->b_dmaaddr) == XLOG_HEADER_MAGIC_NUM) {
				found = 2;
				break;
			}
		}
	}
	if (!found) {
		xlog_warn("xFS: xlog_find_tail: couldn't find sync record");
		ASSERT(0);
		return XFS_ERROR(EIO);
	}

	/* find blk_no of tail of log */
	rhead = (xlog_rec_header_t *)bp->b_dmaaddr;
	*tail_blk = BLOCK_LSN(rhead->h_tail_lsn);

	/*
	 * Reset log values according to the state of the log when we
	 * crashed.  In the case where head_blk == 0, we bump curr_cycle
	 * one because the next write starts a new cycle rather than
	 * continuing the cycle of the last good log record.  At this
	 * point we have guaranteed that all partial log records have been
	 * accounted for.  Therefore, we know that the last good log record
	 * written was complete and ended exactly on the end boundary
	 * of the physical log.
	 */
	log->l_prev_block = i;
	log->l_curr_block = *head_blk;
	log->l_curr_cycle = rhead->h_cycle;
	if (found == 2)
		log->l_curr_cycle++;
	log->l_tail_lsn = rhead->h_tail_lsn;
	log->l_last_sync_lsn = rhead->h_lsn;
	log->l_grant_reserve_cycle = log->l_curr_cycle;
	log->l_grant_reserve_bytes = BBTOB(log->l_curr_block);
	log->l_grant_write_cycle = log->l_curr_cycle;
	log->l_grant_write_bytes = BBTOB(log->l_curr_block);

	/*
	 * Look for unmount record.  If we find it, then we know there
	 * was a clean unmount.
	 */
	if (*head_blk == i+2 && rhead->h_num_logops == 1) {
		if (error = xlog_bread(log, i+1, 1, bp))
			goto bread_err;
		op_head = (xlog_op_header_t *)bp->b_dmaaddr;
		if (op_head->oh_flags & XLOG_UNMOUNT_TRANS) {
			log->l_tail_lsn =
			     ((long long)log->l_curr_cycle<< 32)|((uint)(i+2));
			*tail_blk = i+2;
		}
	}
bread_err:
exit:
	xlog_put_bp(bp);

	return error;
}	/* xlog_find_tail */


/*
 * Is the log zeroed at all?
 *
 * The last binary search should be changed to perform an X block read
 * once X becomes small enough.  You can then search linearly through
 * the X blocks.  This will cut down on the number of reads we need to do.
 *
 * If the log is partially zeroed, this routine will pass back the blkno
 * of the first block with cycle number 0.  It won't have a complete LR
 * preceding it.
 *
 * Return:
 *	0  => the log is completely written to
 *	-1 => use *blk_no as the first block of the log
 *	>0 => error has occurred
 */
STATIC int
xlog_find_zeroed(xlog_t	 *log,
		 daddr_t *blk_no)
{
	buf_t	*bp, *big_bp;
	uint	first_cycle, last_cycle;
	daddr_t	new_blk, last_blk, start_blk;
	daddr_t num_scan_bblks;
	caddr_t	ba;
	int	error, log_bbnum = log->l_logBBsize;

	error = 0;
	/* check totally zeroed log */
	bp = xlog_get_bp(1);
	if (error = xlog_bread(log, 0, 1, bp))
		goto bp_err;
	first_cycle = GET_CYCLE(bp->b_dmaaddr);
	if (first_cycle == 0) {		/* completely zeroed log */
		*blk_no = 0;
		xlog_put_bp(bp);
		return -1;
	}

	/* check partially zeroed log */
	if (error = xlog_bread(log, log_bbnum-1, 1, bp))
		goto bp_err;
	last_cycle = GET_CYCLE(bp->b_dmaaddr);
	if (last_cycle != 0) {		/* log completely written to */
		xlog_put_bp(bp);
		return 0;
	} else if (first_cycle != 1) {
		/*
		 * Hopefully, this will catch the case where someone mkfs's
		 * over a log partition.
		 */
	xlog_warn("xFS: (xlog_find_zeroed): last cycle = 0; first cycle != 1");
		ASSERT(first_cycle == 1);
		return EINVAL;
	}

	/* we have a partially zeroed log */
	last_blk = log_bbnum-1;
	if (error = xlog_find_cycle_start(log, bp, 0, &last_blk, 0))
		goto bp_err;

	/*
	 * Validate the answer.  Because there is no way to guarantee that
	 * the entire log is made up of log records which are the same size,
	 * we scan over the defined maximum blocks.  At this point, the maximum
	 * is not chosen to mean anything special.   XXXmiken
	 */
	num_scan_bblks = BTOBB(XLOG_MAX_ICLOGS<<XLOG_MAX_RECORD_BSHIFT);
	big_bp = xlog_get_bp(num_scan_bblks);
	if (last_blk < num_scan_bblks)
		num_scan_bblks = last_blk;
	start_blk = last_blk - num_scan_bblks;
	if (error = xlog_bread(log, start_blk, num_scan_bblks, big_bp))
		goto big_bp_err;
	ba = big_bp->b_dmaaddr;
	new_blk = xlog_find_verify_cycle(&ba, start_blk, num_scan_bblks, 1, 1);
	if (new_blk != -1)
		last_blk = new_blk;

	/*
	 * Potentially backup over partial log record write.  We don't need
	 * to search the end of the log because we know it is zero.
	 */
	if (error = xlog_find_verify_log_record(ba, start_blk, &last_blk, 0))
	    goto big_bp_err;

	*blk_no = last_blk;
big_bp_err:
	xlog_put_bp(big_bp);
bp_err:
	xlog_put_bp(bp);
	if (error)
		return error;
	return -1;
}	/* xlog_find_zeroed */


/******************************************************************************
 *
 *		Log recover routines
 *
 ******************************************************************************
 */

STATIC xlog_recover_t *
xlog_recover_find_tid(xlog_recover_t *q,
		      xlog_tid_t     tid)
{
	xlog_recover_t *p = q;

	while (p != NULL) {
		if (p->r_log_tid == tid)
		    break;
		p = p->r_next;
	}
	return p;
}	/* xlog_recover_find_tid */


STATIC void
xlog_recover_put_hashq(xlog_recover_t **q,
		       xlog_recover_t *trans)
{
	trans->r_next = *q;
	*q = trans;
}	/* xlog_recover_put_hashq */


STATIC void
xlog_recover_add_item(xlog_recover_item_t **itemq)
{
	xlog_recover_item_t *item;

	item = kmem_zalloc(sizeof(xlog_recover_item_t), 0);
	xlog_recover_insert_item_backq(itemq, item);
}	/* xlog_recover_add_item */


STATIC int
xlog_recover_add_to_cont_trans(xlog_recover_t	*trans,
			       caddr_t		dp,
			       int		len)
{
	xlog_recover_item_t	*item;
	caddr_t			ptr, old_ptr;
	int			old_len;
	
	item = trans->r_itemq;
	if (item == 0) {
		/* finish copying rest of trans header */
		xlog_recover_add_item(&trans->r_itemq);
		ptr = (caddr_t)&trans->r_theader+sizeof(xfs_trans_header_t)-len;
		bcopy(dp, ptr, len); /* s, d, l */
		return 0;
	}
	item = item->ri_prev;

	old_ptr = item->ri_buf[item->ri_cnt-1].i_addr;
	old_len = item->ri_buf[item->ri_cnt-1].i_len;

	ptr = kmem_realloc(old_ptr, len+old_len, 0);
	bcopy(dp , &ptr[old_len], len);			/* s, d, l */
	item->ri_buf[item->ri_cnt-1].i_len += len;
	item->ri_buf[item->ri_cnt-1].i_addr = ptr;
	return 0;
}	/* xlog_recover_add_to_cont_trans */


/* The next region to add is the start of a new region.  It could be
 * a whole region or it could be the first part of a new region.  Because
 * of this, the assumption here is that the type and size fields of all
 * format structures fit into the first 32 bits of the structure.
 *
 * This works because all regions must be 32 bit aligned.  Therefore, we
 * either have both fields or we have neither field.  In the case we have
 * neither field, the data part of the region is zero length.  We only have
 * a log_op_header and can throw away the header since a new one will appear
 * later.  If we have at least 4 bytes, then we can determine how many regions
 * will appear in the current log item.
 */
STATIC int
xlog_recover_add_to_trans(xlog_recover_t	*trans,
			  caddr_t		dp,
			  int			len)
{
	xfs_inode_log_format_t	 *in_f;			/* any will do */
	xlog_recover_item_t	 *item;
	caddr_t			 ptr;

	if (!len)
		return 0;
	ptr = kmem_zalloc(len, 0);
	bcopy(dp, ptr, len);
	
	in_f = (xfs_inode_log_format_t *)ptr;
	item = trans->r_itemq;
	if (item == 0) {
		ASSERT(*(uint *)dp == XFS_TRANS_HEADER_MAGIC);
		if (len == sizeof(xfs_trans_header_t))
			xlog_recover_add_item(&trans->r_itemq);
		bcopy(dp, &trans->r_theader, len); /* s, d, l */
		return 0;
	}
	if (item->ri_prev->ri_total != 0 &&
	     item->ri_prev->ri_total == item->ri_prev->ri_cnt) {
		xlog_recover_add_item(&trans->r_itemq);
	}
	item = trans->r_itemq;
	item = item->ri_prev;

	if (item->ri_total == 0) {		/* first region to be added */
		item->ri_total	= in_f->ilf_size;
	}
	ASSERT(item->ri_total > item->ri_cnt);
	/* Description region is ri_buf[0] */
	item->ri_buf[item->ri_cnt].i_addr = ptr;
	item->ri_buf[item->ri_cnt].i_len  = len;
	item->ri_cnt++;
	return 0;
}	/* xlog_recover_add_to_trans */


STATIC void
xlog_recover_new_tid(xlog_recover_t	**q,
		     xlog_tid_t		tid,
		     xfs_lsn_t		lsn)
{
	xlog_recover_t	*trans;

	trans = kmem_zalloc(sizeof(xlog_recover_t), 0);
	trans->r_log_tid   = tid;
	trans->r_lsn	   = lsn;
	xlog_recover_put_hashq(q, trans);
}	/* xlog_recover_new_tid */


STATIC int
xlog_recover_unlink_tid(xlog_recover_t	**q,
			xlog_recover_t	*trans)
{
	xlog_recover_t	*tp;
	int		found = 0;

	ASSERT(trans != 0);
	if (trans == *q) {
		*q = (*q)->r_next;
	} else {
		tp = *q;
		while (tp != 0) {
			if (tp->r_next == trans) {
				found = 1;
				break;
			}
			tp = tp->r_next;
		}
		if (!found) {
			xlog_warn(
			     "xFS: xlog_recover_unlink_tid: trans not found");
			ASSERT(0);
			return XFS_ERROR(EIO);
		}
		tp->r_next = tp->r_next->r_next;
	}
	return 0;
}	/* xlog_recover_unlink_tid */

#ifdef SIM
STATIC void
xlog_recover_print_trans_head(xlog_recover_t *tr)
{
	static char *trans_type[] = {
		"",
		"SETATTR",
		"SETATTR_SIZE",
		"INACTIVE",
		"CREATE",
		"CREATE_TRUNC",
		"TRUNCATE_FILE",
		"REMOVE",
		"LINK",
		"RENAME",
		"MKDIR",
		"RMDIR",
		"SYMLINK",
		"SET_DMATTRS",
		"GROWFS",
		"STRAT_WRITE",
		"DIOSTRAT"
	};

	printf("TRANS: tid:0x%x  type:%s  #items:%d  trans:0x%x  q:0x%x\n",
	       tr->r_log_tid, trans_type[tr->r_theader.th_type],
	       tr->r_theader.th_num_items,
	       tr->r_theader.th_tid, tr->r_itemq);
}	/* xlog_recover_print_trans_head */


void
xlog_recover_print_data(caddr_t p, int len)
{
    extern int print_data;

    if (print_data) {
	uint *dp  = (uint *)p;
	int  nums = len >> 2;
	int  j = 0;

	while (j < nums) {
	    if ((j % 8) == 0)
		printf("%2x ", j);
	    printf("%8x ", *dp);
	    dp++;
	    j++;
	    if ((j % 8) == 0)
		printf("\n");
	}
	printf("\n");
    }
}	/* xlog_recover_print_data */


STATIC void
xlog_recover_print_buffer(xlog_recover_item_t *item)
{
    xfs_agi_t			*agi;
    xfs_agf_t			*agf;
    xfs_buf_log_format_t	*f;
    caddr_t			p;
    int				len, num, i;
    extern int			print_buffer;

    f = (xfs_buf_log_format_t *)item->ri_buf[0].i_addr;
    len = item->ri_buf[0].i_len;
    printf("	");
    printf("BUF:  #regs:%d   start blkno:0x%x   len:%d   bmap size:%d\n",
	   f->blf_size, f->blf_blkno, f->blf_len, f->blf_map_size);
    num = f->blf_size-1;
    i = 1;
    while (num-- > 0) {
	p = item->ri_buf[i].i_addr;
	len = item->ri_buf[i].i_len;
	i++;
	if (f->blf_blkno == 0) { /* super block */
	    printf("	SUPER Block Buffer:\n");
	    if (!print_buffer) continue;
	    printf("		icount:%lld  ifree:%lld  ",
		   *(long long *)(p), *(long long *)(p+8));
	    printf("fdblks:%lld  frext:%lld\n",
		   *(long long *)(p+16),
		   *(long long *)(p+24));
	} else if (*(uint *)p == XFS_AGI_MAGIC) {
	    agi = (xfs_agi_t *)p;
	    printf("	AGI Buffer: (XAGI)\n");
	    if (!print_buffer) continue;
	    printf("		ver:%d  ", agi->agi_versionnum);
	    printf("seq#:%d  len:%d  cnt:%d  root:%d\n",
		   agi->agi_seqno, agi->agi_length,
		   agi->agi_count, agi->agi_root);
	    printf("		level:%d  free#:0x%x  newino:0x%x\n",
		   agi->agi_level, agi->agi_freecount,
		   agi->agi_newino);
	} else if (*(uint *)p == XFS_AGF_MAGIC) {
	    agf = (xfs_agf_t *)p;
	    printf("	AGF Buffer: (XAGF)\n");
	    if (!print_buffer) continue;
	    printf("		ver:%d  seq#:%d  len:%d  \n",
		   agf->agf_versionnum, agf->agf_seqno,
		   agf->agf_length);
	    printf("		root BNO:%d  CNT:%d\n",
		   agf->agf_roots[XFS_BTNUM_BNOi],
		   agf->agf_roots[XFS_BTNUM_CNTi]);
	    printf("		level BNO:%d  CNT:%d\n",
		   agf->agf_roots[XFS_BTNUM_BNOi],
		   agf->agf_roots[XFS_BTNUM_CNTi]);
	    printf("		1st:%d  last:%d  cnt:%d  freeblks:%d  longest:%d\n",
		   agf->agf_flfirst, agf->agf_fllast, agf->agf_flcount,
		   agf->agf_freeblks, agf->agf_longest);
	} else {
	    printf("	BUF DATA\n");
	    if (!print_buffer) continue;
	    xlog_recover_print_data(p, len);
	}
    }
}	/* xlog_recover_print_buffer */


STATIC void
xlog_recover_print_inode_core(xfs_dinode_core_t *di)
{
    extern int print_inode;

    printf("	CORE inode:\n");
    if (!print_inode)
	return;
    printf("		magic:%c%c  mode:0x%x  ver:%d  format:%d  nlink:%d\n",
	   ((char *)&di->di_magic)[0], ((char *)&di->di_magic)[1], di->di_mode,
	   di->di_version, di->di_format, di->di_nlink);
    printf("		uid:%d  gid:%d  uuid:0x%llx\n",
	   di->di_uid, di->di_gid, di->di_uuid);
    printf("		atime:%d  mtime:%d  ctime:%d\n",
	   di->di_atime.t_sec, di->di_mtime.t_sec, di->di_ctime.t_sec);
    printf("		size:0x%llx  nblks:0x%llx  exsize:%d  nextents:%d  nattrext:%d\n",
	   di->di_size, di->di_nblocks, di->di_extsize, di->di_nextents,
	   (int)di->di_nattrextents);
    printf("		forkoff:%d  dmevmask:0x%x  dmstate:%d  flags:0x%x  gen:%d\n",
	   (int)di->di_forkoff, di->di_dmevmask, (int)di->di_dmstate, (int)di->di_flags,
	   di->di_gen);
}	/* xlog_recover_print_inode_core */


STATIC void
xlog_recover_print_inode(xlog_recover_item_t *item)
{
    xfs_inode_log_format_t *f;
    xfs_dinode_core_t	   *dino;
    caddr_t		   p;
    int			   len;
    extern int		   print_inode, print_data;

    f = (xfs_inode_log_format_t *)item->ri_buf[0].i_addr;
    ASSERT(item->ri_buf[0].i_len == sizeof(xfs_inode_log_format_t));
    printf("	INODE: #regs:%d   ino:0x%llx  flags:0x%x   dsize:%d\n",
	   f->ilf_size, f->ilf_ino, f->ilf_fields, f->ilf_dsize);

    /* core inode comes 2nd */
    ASSERT(item->ri_buf[1].i_len == sizeof(xfs_dinode_core_t));
    xlog_recover_print_inode_core((xfs_dinode_core_t *)item->ri_buf[1].i_addr);

    /* does anything come next */
    switch (f->ilf_fields & XFS_ILOG_NONCORE) {
	case XFS_ILOG_EXT: {
	    ASSERT(f->ilf_size == 3);
	    printf("		EXTENTS inode data:\n");
	    if (print_inode && print_data) {
		xlog_recover_print_data(item->ri_buf[2].i_addr,
					item->ri_buf[2].i_len);
	    }
	    break;
	}
	case XFS_ILOG_BROOT: {
	    ASSERT(f->ilf_size == 3);
	    printf("		BTREE inode data:\n");
	    if (print_inode && print_data) {
		xlog_recover_print_data(item->ri_buf[2].i_addr,
					item->ri_buf[2].i_len);
	    }
	    break;
	}
	case XFS_ILOG_DATA: {
	    ASSERT(f->ilf_size == 3);
	    printf("		LOCAL inode data:\n");
	    if (print_inode && print_data) {
		xlog_recover_print_data(item->ri_buf[2].i_addr,
					item->ri_buf[2].i_len);
	    }
	    break;
	}
	case XFS_ILOG_DEV: {
	    ASSERT(f->ilf_size == 2);
	    printf("		DEV inode: no extra region\n");
	    break;
	}
	case XFS_ILOG_UUID: {
	    ASSERT(f->ilf_size == 2);
	    printf("		UUID inode: no extra region\n");
	    break;
	}
	case 0: {
	    ASSERT(f->ilf_size == 2);
	    break;
	}
	default: {
	    xlog_panic("xlog_print_trans_inode: illegal inode type");
	}
    }
    
}	/* xlog_recover_print_inode */


STATIC void
xlog_recover_print_efd(xlog_recover_item_t *item)
{
    xfs_efd_log_format_t *f;
    xfs_extent_t	 *ex;
    int			 i;

    f = (xfs_efd_log_format_t *)item->ri_buf[0].i_addr;
    /*
     * An xfs_efd_log_format structure contains a variable length array
     * as the last field.  Each element is of size xfs_extent_t.
     */
    ASSERT(item->ri_buf[0].i_len == sizeof(xfs_efd_log_format_t)+sizeof(xfs_extent_t)*(f->efd_nextents-1));
    printf("	EFD:  #regs: %d    num_extents: %d  id: 0x%llx\n",
	   f->efd_size, f->efd_nextents, f->efd_efi_id);
    ex = f->efd_extents;
    printf("	");
    for (i=0; i< f->efd_size; i++) {
	printf("(s: 0x%llx, l: %d) ", ex->ext_start, ex->ext_len);
	if (i % 4 == 3) printf("\n");
	ex++;
    }
    if (i % 4 != 0) printf("\n");
    return;
}	/* xlog_recover_print_efd */


STATIC void
xlog_recover_print_efi(xlog_recover_item_t *item)
{
    xfs_efi_log_format_t *f;
    xfs_extent_t	 *ex;
    int			 i;
    
    f = (xfs_efi_log_format_t *)item->ri_buf[0].i_addr;
    /*
     * An xfs_efi_log_format structure contains a variable length array
     * as the last field.  Each element is of size xfs_extent_t.
     */
    ASSERT(item->ri_buf[0].i_len == sizeof(xfs_efi_log_format_t)+sizeof(xfs_extent_t)*(f->efi_nextents-1));

    printf("	EFI:  #regs:%d    num_extents:%d  id:0x%llx\n",
	   f->efi_size, f->efi_nextents, f->efi_id);
    ex = f->efi_extents;
    printf("	");
    for (i=0; i< f->efi_size; i++) {
	printf("(s: 0x%llx, l: %d) ", ex->ext_start, ex->ext_len);
	if (i % 4 == 3) printf("\n");
	ex++;
    }
    if (i % 4 != 0) printf("\n");
    return;
}	/* xlog_recover_print_efi */
#endif


STATIC void
xlog_recover_print_item(xlog_recover_item_t *item)
{
	int i;

	switch (ITEM_TYPE(item)) {
	    case XFS_LI_BUF:
	    case XFS_LI_OBUF: {
		printf("BUF");
		break;
	    }
	    case XFS_LI_OINODE: {
		printf("OINO");
		break;
	    }
	    case XFS_LI_INODE: {
		printf("INO");
		break;
	    }
	    case XFS_LI_EFD: {
		printf("EFD");
		break;
	    }
	    case XFS_LI_EFI: {
		printf("EFI");
		break;
	    }
	    default: {
		cmn_err(CE_PANIC, "xlog_recover_print_item: illegal type\n");
		break;
	    }
	}
/*	type isn't filled in yet
	printf("ITEM: type: %d cnt: %d total: %d ",
	       item->ri_type, item->ri_cnt, item->ri_total);
*/
	printf(": cnt:%d total:%d ", item->ri_cnt, item->ri_total);
	for (i=0; i<item->ri_cnt; i++) {
		printf("a:0x%x len:%d ",
		       item->ri_buf[i].i_addr, item->ri_buf[i].i_len);
	}
	printf("\n");

#ifdef SIM
	switch (ITEM_TYPE(item)) {
	    case XFS_LI_BUF:
	    case XFS_LI_OBUF: {
		xlog_recover_print_buffer(item);
		break;
	    }
	    case XFS_LI_INODE:
	    case XFS_LI_OINODE: {
		xlog_recover_print_inode(item);
		break;
	    }
	    case XFS_LI_EFD: {
		xlog_recover_print_efd(item);
		break;
	    }
	    case XFS_LI_EFI: {
		xlog_recover_print_efi(item);
		break;
	    }
	    default: {
		printf("xlog_recover_print_item: illegal type\n");
		break;
	    }
	}
#endif
}	/* xlog_recover_print_item */


/*ARGSUSED*/
STATIC void
xlog_recover_print_trans(xlog_recover_t	     *trans,
			 xlog_recover_item_t *itemq,
			 int		     print)
{
	xlog_recover_item_t *first_item, *item;

	if (print < 3)
		return;

	printf("======================================\n");
#ifdef SIM
	xlog_recover_print_trans_head(trans);
#endif
	item = first_item = itemq;
	do {
		xlog_recover_print_item(item);
		item = item->ri_next;
	} while (first_item != item);
}	/* xlog_recover_print_trans */


STATIC void
xlog_recover_insert_item_backq(xlog_recover_item_t **q,
			       xlog_recover_item_t *item)
{
	if (*q == 0) {
		item->ri_prev = item->ri_next = item;
		*q = item;
	} else {
		item->ri_next		= *q;
		item->ri_prev		= (*q)->ri_prev;
		(*q)->ri_prev		= item;
		item->ri_prev->ri_next	= item;
	}
}	/* xlog_recover_insert_item_backq */


STATIC void
xlog_recover_insert_item_frontq(xlog_recover_item_t **q,
				xlog_recover_item_t *item)
{
	xlog_recover_insert_item_backq(q, item);
	*q = item;
}	/* xlog_recover_insert_item_frontq */


/*ARGSUSED*/
STATIC int
xlog_recover_reorder_trans(xlog_t	  *log,
			   xlog_recover_t *trans)
{
    xlog_recover_item_t *first_item, *itemq, *itemq_next;

    first_item = itemq = trans->r_itemq;
    trans->r_itemq = NULL;
    do {
	itemq_next = itemq->ri_next;
	switch (ITEM_TYPE(itemq)) {
	    case XFS_LI_BUF:
	    case XFS_LI_OBUF: {
		xlog_recover_insert_item_frontq(&trans->r_itemq, itemq);
		break;
	    }
	    case XFS_LI_INODE:
	    case XFS_LI_OINODE:
	    case XFS_LI_EFD:
	    case XFS_LI_EFI: {
		xlog_recover_insert_item_backq(&trans->r_itemq, itemq);
		break;
	    }
	    default: {
		xlog_warn(
	"xFS: xlog_recover_reorder_trans: unrecognized type of log operation");
		ASSERT(0);
		return XFS_ERROR(EIO);
	    }
	}
	itemq = itemq_next;
    } while (first_item != itemq);
    return 0;
}	/* xlog_recover_reorder_trans */


/*
 * Build up the table of buf cancel records so that we don't replay
 * cancelled data in the second pass.  For buffer records that are
 * not cancel records, there is nothing to do here so we just return.
 *
 * If we get a cancel record which is already in the table, this indicates
 * that the buffer was cancelled multiple times.  In order to ensure
 * that during pass 2 we keep the record in the table until we reach its
 * last occurrence in the log, we keep a reference count in the cancel
 * record in the table to tell us how many times we expect to see this
 * record during the second pass.
 */
STATIC void
xlog_recover_do_buffer_pass1(xlog_t			*log,
			     xfs_buf_log_format_t	*buf_f)
{
	xfs_buf_cancel_t	*bcp;
	xfs_buf_cancel_t	*nextp;
	xfs_buf_cancel_t	*prevp;
	xfs_buf_cancel_t	**bucket;
	daddr_t			blkno;
	uint			len;

	/*
	 * If this isn't a cancel buffer item, then just return.
	 */
	if (!(buf_f->blf_flags & XFS_BLI_CANCEL)) {
		return;
	}

	/*
	 * Insert an xfs_buf_cancel record into the hash table of
	 * them.  If there is already an identical record, bump
	 * its reference count.
	 */
	blkno = buf_f->blf_blkno;
	len = buf_f->blf_len;
	bucket = &(log->l_buf_cancel_table[blkno % XLOG_BC_TABLE_SIZE]);
	/*
	 * If the hash bucket is empty then just insert a new record into
	 * the bucket.
	 */
	if (*bucket == NULL) {
		bcp = (xfs_buf_cancel_t*)kmem_alloc(sizeof(xfs_buf_cancel_t),
						    KM_SLEEP);
		bcp->bc_blkno = blkno;
		bcp->bc_len = len;
		bcp->bc_refcount = 1;
		bcp->bc_next = NULL;
		*bucket = bcp;
		return;
	}

	/*
	 * The hash bucket is not empty, so search for duplicates of our
	 * record.  If we find one them just bump its refcount.  If not
	 * then add us at the end of the list.
	 */
	prevp = NULL;
	nextp = *bucket;
	while (nextp != NULL) {
		if (nextp->bc_blkno == blkno) {
			ASSERT(nextp->bc_len == len);
			nextp->bc_refcount++;
			return;
		}
		prevp = nextp;
		nextp = nextp->bc_next;
	}
	ASSERT(prevp != NULL);
	bcp = (xfs_buf_cancel_t*)kmem_alloc(sizeof(xfs_buf_cancel_t),
					    KM_SLEEP);
	bcp->bc_blkno = blkno;
	bcp->bc_len = len;
	bcp->bc_refcount = 1;
	bcp->bc_next = NULL;
	prevp->bc_next = bcp;

	return;
}

/*
 * Check to see whether the buffer being recovered has a corresponding
 * entry in the buffer cancel record table.  If it does then return 1
 * so that it will be cancelled, otherwise return 0.  If the buffer is
 * actually a buffer cancel item (XFS_BLI_CANCEL is set), then decrement
 * the refcount on the entry in the table and remove it from the table
 * if this is the last reference.
 *
 * We remove the cancel record from the table when we encounter its
 * last occurrence in the log so that if the same buffer is re-used
 * again after its last cancellation we actually replay the changes
 * made at that point.
 */
STATIC int
xlog_recover_do_buffer_pass2(xlog_t			*log,
			     xfs_buf_log_format_t	*buf_f)
{
	xfs_buf_cancel_t	*bcp;
	xfs_buf_cancel_t	*prevp;
	xfs_buf_cancel_t	**bucket;
	daddr_t			blkno;

	if (log->l_buf_cancel_table == NULL) {
		/*
		 * There is nothing in the table built in pass one,
		 * so this buffer must not be cancelled.
		 */
		ASSERT(!(buf_f->blf_flags & XFS_BLI_CANCEL));
		return 0;
	}

	bucket = &(log->l_buf_cancel_table[buf_f->blf_blkno %
					   XLOG_BC_TABLE_SIZE]);
	bcp = *bucket;
	if (bcp == NULL) {
		/*
		 * There is no corresponding entry in the table built
		 * in pass one, so this buffer has not been cancelled.
		 */
		ASSERT(!(buf_f->blf_flags & XFS_BLI_CANCEL));
		return 0;
	}

	/*
	 * Search for an entry in the buffer cancel table that
	 * matches our buffer.
	 */
	blkno = buf_f->blf_blkno;
	prevp = NULL;
	while (bcp != NULL) {
		if (bcp->bc_blkno == blkno) {
			/*
			 * We've go a match, so return 1 so that the
			 * recovery of this buffer is cancelled.
			 * If this buffer is actually a buffer cancel
			 * log item, then decrement the refcount on the
			 * one in the table and remove it if this is the
			 * last reference.
			 */
			ASSERT(bcp->bc_len == buf_f->blf_len);
			if (buf_f->blf_flags & XFS_BLI_CANCEL) {
				bcp->bc_refcount--;
				if (bcp->bc_refcount == 0) {
					if (prevp == NULL) {
						*bucket = bcp->bc_next;
					} else {
						prevp->bc_next = bcp->bc_next;
					}
					kmem_free(bcp,
						  sizeof(xfs_buf_cancel_t));
				}
			}
			return 1;
		}
		prevp = bcp;
		bcp = bcp->bc_next;
	}
	/*
	 * We didn't find a corresponding entry in the table, so
	 * return 0 so that the buffer is NOT cancelled.
	 */
	ASSERT(!(buf_f->blf_flags & XFS_BLI_CANCEL));
	return 0;
}


/*
 * Perform recovery for a buffer full of inodes.  In these buffers,
 * the only data which should be recovered is that which corresponds
 * to the di_next_unlinked pointers in the on disk inode structures.
 * The rest of the data for the inodes is always logged through the
 * inodes themselves rather than the inode buffer and is recovered
 * in xlog_recover_do_inode_trans().
 *
 * The only time when buffers full of inodes are fully recovered is
 * when the buffer is full of newly allocated inodes.  In this case
 * the buffer will not be marked as an inode buffer and so will be
 * sent to xlog_recover_do_reg_buffer() below during recovery.
 */
STATIC void
xlog_recover_do_inode_buffer(xfs_mount_t		*mp,
			     xlog_recover_item_t	*item,
			     buf_t			*bp,
			     xfs_buf_log_format_t	*buf_f)
{
	int		i;
	int		item_index;
	int		bit;
	int		nbits;
	int		reg_buf_offset;
	int		reg_buf_bytes;
	int		next_unlinked_offset;
	int		inodes_per_buf;
	xfs_agino_t	*logged_nextp;
	xfs_agino_t	*buffer_nextp;

	/*
	 * Set the variables corresponding to the current region to
	 * 0 so that we'll initialize them on the first pass through
	 * the loop.
	 */
	reg_buf_offset = 0;
	reg_buf_bytes = 0;
	bit = 0;
	nbits = 0;
	item_index = 0;
	inodes_per_buf = bp->b_bcount >> mp->m_sb.sb_inodelog;
	for (i = 0; i < inodes_per_buf; i++) {
		next_unlinked_offset = (i * mp->m_sb.sb_inodesize) +
			offsetof(xfs_dinode_t, di_next_unlinked);

		while (next_unlinked_offset >=
		       (reg_buf_offset + reg_buf_bytes)) {
			/*
			 * The next di_next_unlinked field is beyond
			 * the current logged region.  Find the next
			 * logged region that contains or is beyond
			 * the current di_next_unlinked field.
			 */
			bit += nbits;
			bit = xfs_buf_item_next_bit(buf_f->blf_data_map,
						    buf_f->blf_map_size,
						    bit);

			/*
			 * If there are no more logged regions in the
			 * buffer, then we're done.
			 */
			if (bit == -1) {
				return;
			}

			nbits = xfs_buf_item_contig_bits(buf_f->blf_data_map,
							 buf_f->blf_map_size,
							 bit);
			reg_buf_offset = bit << XFS_BLI_SHIFT;
			reg_buf_bytes = nbits << XFS_BLI_SHIFT;
			item_index++;
		}

		/*
		 * If the current logged region starts after the current
		 * di_next_unlinked field, then move on to the next
		 * di_next_unlinked field.
		 */
		if (next_unlinked_offset < reg_buf_offset) {
			continue;
		}

		ASSERT(item->ri_buf[item_index].i_addr != NULL);
		ASSERT((item->ri_buf[item_index].i_len % XFS_BLI_CHUNK) == 0);
		ASSERT((reg_buf_offset + reg_buf_bytes) <= bp->b_bcount);

		/*
		 * The current logged region contains a copy of the
		 * current di_next_unlinked field.  Extract its value
		 * and copy it to the buffer copy.
		 */
		logged_nextp = (xfs_agino_t *)
			       ((char *)(item->ri_buf[item_index].i_addr) +
				(next_unlinked_offset - reg_buf_offset));
		buffer_nextp = (xfs_agino_t *)((char *)(bp->b_un.b_addr) +
					      next_unlinked_offset);
		*buffer_nextp = *logged_nextp;
	}
}	/* xlog_recover_do_inode_buffer */

/*
 * Perform a 'normal' buffer recovery.  Each logged region of the
 * buffer should be copied over the corresponding region in the
 * given buffer.  The bitmap in the buf log format structure indicates
 * where to place the logged data.
 */
/*ARGSUSED*/
STATIC void
xlog_recover_do_reg_buffer(xfs_mount_t		*mp,
			   xlog_recover_item_t	*item,
			   buf_t		*bp,
			   xfs_buf_log_format_t	*buf_f)
{
	int	i;
	int	bit;
	int	nbits;

	bit = 0;
	i = 1;  /* 0 is the buf format structure */
	while (1) {
		bit = xfs_buf_item_next_bit(buf_f->blf_data_map,
					    buf_f->blf_map_size,
					    bit);
		if (bit == -1)
			break;
		nbits = xfs_buf_item_contig_bits(buf_f->blf_data_map,
						 buf_f->blf_map_size,
						 bit);
		ASSERT(item->ri_buf[i].i_addr != 0);
		ASSERT(item->ri_buf[i].i_len % XFS_BLI_CHUNK == 0);
		ASSERT(bp->b_bcount >=
		       ((uint)bit << XFS_BLI_SHIFT)+(nbits<<XFS_BLI_SHIFT));
		bcopy(item->ri_buf[i].i_addr,		           /* source */
		      bp->b_un.b_addr+((uint)bit << XFS_BLI_SHIFT),  /* dest */
		      nbits<<XFS_BLI_SHIFT);			   /* length */
		i++;
		bit += nbits;
	}

	/* Shouldn't be any more regions */
	if (i < XLOG_MAX_REGIONS_IN_ITEM) {
		ASSERT(item->ri_buf[i].i_addr == 0);
		ASSERT(item->ri_buf[i].i_len == 0);
	}
}	/* xlog_recover_do_reg_buffer */

/*
 * This routine replays a modification made to a buffer at runtime.
 * There are actually two types of buffer, regular and inode, which
 * are handled differently.  Inode buffers are handled differently
 * in that we only recover a specific set of data from them, namely
 * the inode di_next_unlinked fields.  This is because all other inode
 * data is actually logged via inode records and any data we replay
 * here which overlaps that may be stale.
 *
 * When meta-data buffers are freed at run time we log a buffer item
 * with the XFS_BLI_CANCEL bit set to indicate that previous copies
 * of the buffer in the log should not be replayed at recovery time.
 * This is so that if the blocks covered by the buffer are reused for
 * file data before we crash we don't end up replaying old, freed
 * meta-data into a user's file.
 *
 * To handle the cancellation of buffer log items, we make two passes
 * over the log during recovery.  During the first we build a table of
 * those buffers which have been cancelled, and during the second we
 * only replay those buffers which do not have corresponding cancel
 * records in the table.  See xlog_recover_do_buffer_pass[1,2] above
 * for more details on the implementation of the table of cancel records.
 */
STATIC int
xlog_recover_do_buffer_trans(xlog_t		 *log,
			     xlog_recover_item_t *item,
			     int		 pass)
{
	xfs_buf_log_format_t *buf_f;
	xfs_mount_t	     *mp;
	buf_t		     *bp;
	int		     error;
	int		     cancel;

	buf_f = (xfs_buf_log_format_t *)item->ri_buf[0].i_addr;

	if (pass == XLOG_RECOVER_PASS1) {
		/*
		 * In this pass we're only looking for buf items
		 * with the XFS_BLI_CANCEL bit set.
		 */
		xlog_recover_do_buffer_pass1(log, buf_f);
		return 0;
	} else {
		/*
		 * In this pass we want to recover all the buffers
		 * which have not been cancelled and are not
		 * cancellation buffers themselves.  The routine
		 * we call here will tell us whether or not to
		 * continue with the replay of this buffer.
		 */
		cancel = xlog_recover_do_buffer_pass2(log, buf_f);
		if (cancel) {
			return 0;
		}
	}
	mp = log->l_mp;
	bp = bread(mp->m_dev, buf_f->blf_blkno, buf_f->blf_len);
	if (bp->b_flags & B_ERROR) {
		cmn_err(CE_WARN,
			"xFS: xlog_recover_do_buffer_trans: bread error (%d)",
			buf_f->blf_blkno);
		ASSERT(0);
		error = bp->b_error;
		brelse(bp);
		return error;
	}

	if (buf_f->blf_flags & XFS_BLI_INODE_BUF) {
		xlog_recover_do_inode_buffer(mp, item, bp, buf_f);
	} else {
		xlog_recover_do_reg_buffer(mp, item, bp, buf_f);
	}

	/*
	 * Perform delayed write on the buffer.  Asynchronous writes will be
	 * slower when taking into account all the buffers to be flushed.
	 * If it is an old, unclustered inode buffer, then make sure to
	 * keep it out of the buffer cache so that it doesn't overlap with
	 * future reads of those inodes.
	 */
	if ((log->l_mp->m_sb.sb_blocksize < XFS_INODE_CLUSTER_SIZE) &&
	    (bp->b_bcount < XFS_INODE_CLUSTER_SIZE) &&
	    (*((__uint16_t *)(bp->b_un.b_addr)) == XFS_DINODE_MAGIC)) {
		bp->b_flags |= B_STALE;
		bwrite(bp);
	} else {
		bdwrite(bp);
	}

	/*
	 * Once bdwrite() is called, we lose track of the buffer.  Therefore,
	 * if we want to keep track of buffer errors, we need to add a
	 * release function which sets some variable which gets looked at
	 * after calling bflush() on the device.  XXXmiken
	 */
	return 0;
}	/* xlog_recover_do_buffer_trans */

STATIC int
xlog_recover_do_inode_trans(xlog_t		*log,
			    xlog_recover_item_t *item,
			    int			pass)
{
	xfs_inode_log_format_t	*in_f;
	xfs_mount_t		*mp;
	buf_t			*bp;
	xfs_imap_t		imap;
	xfs_dinode_t		*dip;
	int			len;
	caddr_t			src;
	int			error;

	if (pass == XLOG_RECOVER_PASS1) {
		return 0;
	}

	in_f = (xfs_inode_log_format_t *)item->ri_buf[0].i_addr;
	mp = log->l_mp;
	if (ITEM_TYPE(item) == XFS_LI_INODE) {
		imap.im_blkno = (daddr_t)in_f->ilf_blkno;
		imap.im_len = in_f->ilf_len;
		imap.im_boffset = in_f->ilf_boffset;
	} else {
		/*
		 * It's an old inode format record.  We don't know where
		 * its cluster is located on disk, and we can't allow
		 * xfs_imap() to figure it out because the inode btrees
		 * are not ready to be used.  Therefore do not pass the
		 * XFS_IMAP_LOOKUP flag to xfs_imap().  This will give
		 * us only the single block in which the inode lives
		 * rather than its cluster, so we must make sure to
		 * invalidate the buffer when we write it out below.
		 */
		xfs_imap(log->l_mp, 0, in_f->ilf_ino, &imap, 0);
	}
	bp = bread(mp->m_dev, imap.im_blkno, imap.im_len);
	if (bp->b_flags & B_ERROR) {
		cmn_err(CE_WARN,
			"xFS: xlog_recover_do_inode_trans: bread error (%d)",
			bp->b_blkno);
		ASSERT(0);
		error = bp->b_error;
		brelse(bp);
		return error;
	}
	xfs_inobp_check(mp, bp);
	ASSERT(in_f->ilf_fields & XFS_ILOG_CORE);
	dip = (xfs_dinode_t *)(bp->b_un.b_addr+imap.im_boffset);


	ASSERT((caddr_t)dip+item->ri_buf[1].i_len <= bp->b_dmaaddr+bp->b_bcount);
	bcopy(item->ri_buf[1].i_addr, dip, item->ri_buf[1].i_len);

	if (in_f->ilf_size == 2)
		goto write_inode_buffer;
	len = item->ri_buf[2].i_len;
	src = item->ri_buf[2].i_addr;
	ASSERT(in_f->ilf_size == 3);
	switch (in_f->ilf_fields & XFS_ILOG_NONCORE) {
	    case XFS_ILOG_DATA:
	    case XFS_ILOG_EXT: {
		    ASSERT((caddr_t)&dip->di_u+len <= bp->b_dmaaddr+bp->b_bcount);
		    bcopy(src, &dip->di_u, len);
		    break;
	    }
	    case XFS_ILOG_BROOT: {
		xfs_bmbt_to_bmdr((xfs_bmbt_block_t *)src, len,
				 &(dip->di_u.di_bmbt),
				 XFS_BMAP_BROOT_SIZE(mp->m_sb.sb_inodesize));
		break;
	    }
	    case XFS_ILOG_DEV: {
		    dip->di_u.di_dev = in_f->ilf_u.ilfu_rdev;
		    break;
	    }
	    case XFS_ILOG_UUID: {
		    dip->di_u.di_muuid = in_f->ilf_u.ilfu_uuid;
		    break;
	    }
	    default: {
		    xlog_warn("xFS: xlog_recover_do_inode_trans: Illegal flag");
		    ASSERT(0);
		    brelse(bp);
		    return XFS_ERROR(EIO);
	    }
	}

write_inode_buffer:
	xfs_inobp_check(mp, bp);
	if (ITEM_TYPE(item) == XFS_LI_INODE) {
		bdwrite(bp);
	} else {
		bp->b_flags |= B_STALE;
		bwrite(bp);
	}

	/*
	 * Once bdwrite() is called, we lose track of the buffer.  Therefore,
	 * if we want to keep track of buffer errors, we need to add a
	 * release function which sets some variable which gets looked at
	 * after calling bflush() on the device.  XXXmiken
	 */
	return 0;
}	/* xlog_recover_do_inode_trans */


/*
 * This routine is called to create an in-core extent free intent
 * item from the efi format structure which was logged on disk.
 * It allocates an in-core efi, copies the extents from the format
 * structure into it, and adds the efi to the AIL with the given
 * LSN.
 */
STATIC void
xlog_recover_do_efi_trans(xlog_t		*log,
			  xlog_recover_item_t	*item,
			  xfs_lsn_t		lsn,
			  int			pass)	  
{
	xfs_mount_t		*mp;
	xfs_efi_log_item_t	*efip;
	xfs_efi_log_format_t	*efi_formatp;
	int			spl;

	if (pass == XLOG_RECOVER_PASS1) {
		return;
	}

	efi_formatp = (xfs_efi_log_format_t *)item->ri_buf[0].i_addr;
	ASSERT(item->ri_buf[0].i_len ==
	       (sizeof(xfs_efi_log_format_t) +
		((efi_formatp->efi_nextents - 1) * sizeof(xfs_extent_t))));

	mp = log->l_mp;
	efip = xfs_efi_init(mp, efi_formatp->efi_nextents);
	bcopy((char *)efi_formatp, (char *)&(efip->efi_format),
	      sizeof(xfs_efi_log_format_t) +
	      ((efi_formatp->efi_nextents - 1) * sizeof(xfs_extent_t)));
	efip->efi_next_extent = efi_formatp->efi_nextents;

	spl = AIL_LOCK(mp);
	/*
	 * xfs_trans_update_ail() drops the AIL lock.
	 */
 	xfs_trans_update_ail(mp, (xfs_log_item_t *)efip, lsn, spl);
}	/* xlog_recover_do_efi_trans */


/*
 * This routine is called when an efd format structure is found in
 * a committed transaction in the log.  It's purpose is to cancel
 * the corresponding efi if it was still in the log.  To do this
 * it searches the AIL for the efi with an id equal to that in the
 * efd format structure.  If we find it, we remove the efi from the
 * AIL and free it.
 */
STATIC void
xlog_recover_do_efd_trans(xlog_t		*log,
			  xlog_recover_item_t	*item,
			  int			pass)
{
	xfs_mount_t		*mp;
	xfs_efd_log_format_t	*efd_formatp;
	xfs_efi_log_item_t	*efip;
	xfs_log_item_t		*lip;
	int			spl;
	int			gen;
	int			nexts;
	__uint64_t		efi_id;

	if (pass == XLOG_RECOVER_PASS1) {
		return;
	}

	efd_formatp = (xfs_efd_log_format_t *)item->ri_buf[0].i_addr;
	ASSERT(item->ri_buf[0].i_len ==
	       (sizeof(xfs_efd_log_format_t) +
		((efd_formatp->efd_nextents - 1) * sizeof(xfs_extent_t))));
	efi_id = efd_formatp->efd_efi_id;

	/*
	 * Search for the efi with the id in the efd format structure
	 * in the AIL.
	 */
	mp = log->l_mp;
	spl = AIL_LOCK(mp);
	lip = xfs_trans_first_ail(mp, &gen);
	while (lip != NULL) {
		if (lip->li_type == XFS_LI_EFI) {
			efip = (xfs_efi_log_item_t *)lip;
			if (efip->efi_format.efi_id == efi_id) {
				/*
				 * xfs_trans_delete_ail() drops the
				 * AIL lock.
				 */
				xfs_trans_delete_ail(mp, lip, spl);
				break;
			}
		}
		lip = xfs_trans_next_ail(mp, lip, &gen, NULL);
	}
	if (lip == NULL) {
		AIL_UNLOCK(mp, spl);
	}

	/*
	 * If we found it, then free it up.  If it wasn't there, it
	 * must have been overwritten in the log.  Oh well.
	 */
	if (lip != NULL) {
		nexts = efip->efi_format.efi_nextents;
		if (nexts > XFS_EFI_MAX_FAST_EXTENTS) {
			kmem_free(lip, sizeof(xfs_efi_log_item_t) +
				  ((nexts - 1) * sizeof(xfs_extent_t)));
		} else {
			kmem_zone_free(xfs_efi_zone, efip);
	}
	}
}	/* xlog_recover_do_efd_trans */


/*
 * Perform the transaction
 *
 * If the transaction modifies a buffer or inode, do it now.  Otherwise,
 * EFIs and EFDs get queued up by adding entries into the AIL for them.
 */
STATIC int
xlog_recover_do_trans(xlog_t	     *log,
		      xlog_recover_t *trans,
		      int	     pass)
{
	xlog_recover_item_t *item, *first_item;
	int		    error = 0;

	if (pass == XLOG_RECOVER_PASS1)
		xlog_recover_print_trans(trans, trans->r_itemq, xlog_debug);
#ifdef _KERNEL
	if (error = xlog_recover_reorder_trans(log, trans))
		return error;
	if (pass == XLOG_RECOVER_PASS1)
		xlog_recover_print_trans(trans, trans->r_itemq, xlog_debug+1);

	first_item = item = trans->r_itemq;
	do {
		if ((ITEM_TYPE(item) == XFS_LI_BUF) ||
		    (ITEM_TYPE(item) == XFS_LI_OBUF)) {
			if (error = xlog_recover_do_buffer_trans(log, item,
								 pass))
				break;
		} else if ((ITEM_TYPE(item) == XFS_LI_INODE) ||
			   (ITEM_TYPE(item) == XFS_LI_OINODE)) {
			if (error = xlog_recover_do_inode_trans(log, item,
								pass))
				break;
		} else if (ITEM_TYPE(item) == XFS_LI_EFI) {
			xlog_recover_do_efi_trans(log, item, trans->r_lsn,
						  pass);
		} else if (ITEM_TYPE(item) == XFS_LI_EFD) {
			xlog_recover_do_efd_trans(log, item, pass);
		} else {
			xlog_warn("xFS: xlog_recover_do_trans");
			ASSERT(0);
			error = XFS_ERROR(EIO);
			break;
		}
		item = item->ri_next;
	} while (first_item != item);
#endif /* _KERNEL */

	return error;
}	/* xlog_recover_do_trans */


/*
 * Free up any resources allocated by the transaction
 *
 * Remember that EFIs, EFDs, and IUNLINKs are handled later.
 */
STATIC void
xlog_recover_free_trans(xlog_recover_t      *trans)
{
	xlog_recover_item_t *first_item, *item, *free_item;
	int i;

	item = first_item = trans->r_itemq;
	do {
		free_item = item;
		item = item->ri_next;
		 /* Free the regions in the item. */
		for (i = 0; i < free_item->ri_cnt; i++) {
			kmem_free(free_item->ri_buf[i].i_addr,
				  free_item->ri_buf[i].i_len);
		}
		/* Free the item itself */
		kmem_free(free_item, sizeof(xlog_recover_item_t));
	} while (first_item != item);
	/* Free the transaction recover structure */
	kmem_free(trans, sizeof(xlog_recover_t));
}	/* xlog_recover_free_trans */


STATIC int
xlog_recover_commit_trans(xlog_t	 *log,
			  xlog_recover_t **q,
			  xlog_recover_t *trans,
			  int		 pass)
{
	int error;

	if (error = xlog_recover_unlink_tid(q, trans))
		return error;
	if (error = xlog_recover_do_trans(log, trans, pass))
		return error;
	xlog_recover_free_trans(trans);			/* no error */
	return 0;
}	/* xlog_recover_commit_trans */


/*ARGSUSED*/
STATIC int
xlog_recover_unmount_trans(xlog_recover_t *trans)
{
	/* Do nothing now */
	xlog_warn("xFS: xlog_recover_unmount_trans: Unmount LR");
	return( 0 );
}	/* xlog_recover_unmount_trans */


/*
 * There are two valid states of the r_state field.  0 indicates that the
 * transaction structure is in a normal state.  We have either seen the
 * start of the transaction or the last operation we added was not a partial
 * operation.  If the last operation we added to the transaction was a 
 * partial operation, we need to mark r_state with XLOG_WAS_CONT_TRANS.
 *
 * NOTE: skip LRs with 0 data length.
 */
STATIC int
xlog_recover_process_data(xlog_t	    *log,
			  xlog_recover_t    *rhash[],
			  xlog_rec_header_t *rhead,
			  caddr_t	    dp,
			  int		    pass)
{
    caddr_t		lp	   = dp+rhead->h_len;
    int			num_logops = rhead->h_num_logops;
    xlog_op_header_t	*ohead;
    xlog_recover_t	*trans;
    xlog_tid_t		tid;
    int			error;
    unsigned long	hash;
    uint		flags;

    while (dp < lp) {
	ASSERT(dp + sizeof(xlog_op_header_t) <= lp);
	ohead = (xlog_op_header_t *)dp;
	dp += sizeof(xlog_op_header_t);
	if (ohead->oh_clientid != XFS_TRANSACTION &&
	    ohead->oh_clientid != XFS_LOG) {
	    xlog_warn("xFS: xlog_recover_process_data: bad clientid ");
	    ASSERT(0);
	    return (XFS_ERROR(EIO));
        }
	tid = ohead->oh_tid;
	hash = XLOG_RHASH(tid);
	trans = xlog_recover_find_tid(rhash[hash], tid);
	if (trans == NULL) {			   /* not found; add new tid */
	    if (ohead->oh_flags & XLOG_START_TRANS)
		xlog_recover_new_tid(&rhash[hash], tid, rhead->h_lsn);
	} else {
	    ASSERT(dp+ohead->oh_len <= lp);
	    flags = ohead->oh_flags & ~XLOG_END_TRANS;
	    if (flags & XLOG_WAS_CONT_TRANS)
		flags &= ~XLOG_CONTINUE_TRANS;
	    switch (flags) {
		case XLOG_COMMIT_TRANS: {
		    error = xlog_recover_commit_trans(log, &rhash[hash],
						      trans, pass);
		    break;
		}
		case XLOG_UNMOUNT_TRANS: {
		    error = xlog_recover_unmount_trans(trans);
		    break;
		}
		case XLOG_WAS_CONT_TRANS: {
		    error = xlog_recover_add_to_cont_trans(trans, dp,
							   ohead->oh_len);
		    break;
		}
		case XLOG_START_TRANS : {
		    xlog_warn("xFS: xlog_recover_process_data: bad transaction");
		    ASSERT(0);
		    error = XFS_ERROR(EIO);
		    break;
		}
		case 0:
		case XLOG_CONTINUE_TRANS: {
		    error = xlog_recover_add_to_trans(trans, dp,
						      ohead->oh_len);
		    break;
		}
		default: {
		    xlog_warn("xFS: xlog_recover_process_data: bad flag");
		    ASSERT(0);
		    error = XFS_ERROR(EIO);
		    break;
		}
	    } /* switch */
	    if (error)
		return error;
	} /* if */
	dp += ohead->oh_len;
	num_logops--;
    }
    return( 0 );
}	/* xlog_recover_process_data */


/*
 * Process an extent free intent item that was recovered from
 * the log.  We need to free the extents that it describes.
 */
STATIC void
xlog_recover_process_efi(xfs_mount_t		*mp,
			 xfs_efi_log_item_t	*efip)
{
	xfs_efd_log_item_t	*efdp;
	xfs_trans_t		*tp;
	int			i;
	xfs_extent_t		*extp;

#ifdef SIM
	ASSERT(0);
#else
	ASSERT(!(efip->efi_flags & XFS_EFI_RECOVERED));

	tp = xfs_trans_alloc(mp, 0);
	xfs_trans_reserve(tp, 0, XFS_ITRUNCATE_LOG_RES(mp), 0, 0, 0);
	efdp = xfs_trans_get_efd(tp, efip, efip->efi_format.efi_nextents);

	for (i = 0; i < efip->efi_format.efi_nextents; i++) {
		extp = &(efip->efi_format.efi_extents[i]);
		xfs_free_extent(tp, extp->ext_start, extp->ext_len);
		xfs_trans_log_efd_extent(tp, efdp, extp->ext_start,
					 extp->ext_len);
	}

	efip->efi_flags |= XFS_EFI_RECOVERED;
	xfs_trans_commit(tp, 0);
#endif	/* SIM */
}	/* xlog_recover_process_efi */


/*
 * Verify that once we've encountered something other than an EFI
 * in the AIL that there are no more EFIs in the AIL.
 */
#ifdef DEBUG
STATIC void
xlog_recover_check_ail(xfs_mount_t	*mp,
		       xfs_log_item_t	*lip,
		       int		gen)
{
	int	orig_gen;

	orig_gen = gen;
	do {
		ASSERT(lip->li_type != XFS_LI_EFI);
		lip = xfs_trans_next_ail(mp, lip, &gen, NULL);
		/*
		 * The check will be bogus if we restart from the
		 * beginning of the AIL, so ASSERT that we don't.
		 * We never should since we're holding the AIL lock
		 * the entire time.
		 */
		ASSERT(gen == orig_gen);
	} while (lip != NULL);
}
#endif	/* DEBUG */


/*
 * When this is called, all of the EFIs which did not have
 * corresponding EFDs should be in the AIL.  What we do now
 * is free the extents associated with each one.
 *
 * Since we process the EFIs in normal transactions, they
 * will be removed at some point after the commit.  This prevents
 * us from just walking down the list processing each one.
 * We'll use a flag in the EFI to skip those that we've already
 * processed and use the AIL iteration mechanism's generation
 * count to try to speed this up at least a bit.
 *
 * When we start, we know that the EFIs are the only things in
 * the AIL.  As we process them, however, other items are added
 * to the AIL.  Since everything added to the AIL must come after
 * everything already in the AIL, we stop processing as soon as
 * we see something other than an EFI in the AIL.
 */
STATIC void
xlog_recover_process_efis(xlog_t	*log)
{
	xfs_log_item_t		*lip;
	xfs_efi_log_item_t	*efip;
	int			gen;
	xfs_mount_t		*mp;
	int			spl;

	mp = log->l_mp;
	spl = AIL_LOCK(mp);

	lip = xfs_trans_first_ail(mp, &gen);
	while (lip != NULL) {
		/*
		 * We're done when we see something other than an EFI.
		 */
		if (lip->li_type != XFS_LI_EFI) {
			xlog_recover_check_ail(mp, lip, gen);
			break;
		}

		/*
		 * Skip EFIs that we've already processed.
		 */
		efip = (xfs_efi_log_item_t *)lip;
		if (efip->efi_flags & XFS_EFI_RECOVERED) {
			lip = xfs_trans_next_ail(mp, lip, &gen, NULL);
			continue;
		}

		AIL_UNLOCK(mp, spl);
		xlog_recover_process_efi(mp, efip);
		spl = AIL_LOCK(mp);
		lip = xfs_trans_next_ail(mp, lip, &gen, NULL);
	}
	AIL_UNLOCK(mp, spl);
}	/* xlog_recover_process_efis */


/*
 * xlog_iunlink_recover
 *
 * This is called during recovery to process any inodes which
 * we unlinked but not freed when the system crashed.  These
 * inodes will be on the lists in the AGI blocks.  What we do
 * here is scan all the AGIs and fully truncate and free any
 * inodes found on the lists.  Each inode is removed from the
 * lists when it has been fully truncated and is freed.  The
 * freeing of the inode and its removal from the list must be
 * atomic.
 */
STATIC void
xlog_recover_process_iunlinks(xlog_t	*log)
{
	xfs_mount_t	*mp;
	xfs_agnumber_t	agno;
	xfs_agi_t	*agi;
	daddr_t		agidaddr;
	buf_t		*agibp;
	xfs_inode_t	*ip;
	xfs_agino_t	agino;
	xfs_ino_t	ino;
	int		bucket;

	mp = log->l_mp;
	for (agno = 0; agno < mp->m_sb.sb_agcount; agno++) {
		/*
		 * Find the agi for this ag.
		 */
		agidaddr = XFS_AG_DADDR(mp, agno, XFS_AGI_DADDR);
		agibp = read_buf(mp->m_dev, agidaddr, 1, 0);
		agi = XFS_BUF_TO_AGI(agibp);
		ASSERT(agi->agi_magicnum == XFS_AGI_MAGIC);

		bucket = 0;
		while (bucket < XFS_AGI_UNLINKED_BUCKETS) {
			/*
			 * If there is nothing in the current bucket,
			 * then continue on to the next one.
			 */
			agino = agi->agi_unlinked[bucket];
			if (agino == NULLAGINO) {
				bucket++;
				continue;
			}

			/*
			 * Release the agi buffer so that it can
			 * be acquired in the normal course of the
			 * transaction to truncate and free the inode.
			 */
			brelse(agibp);

			ino = XFS_AGINO_TO_INO(mp, agno, agino);
			ip = xfs_iget(mp, NULL, ino, 0);
			ASSERT(ip != NULL);
			ASSERT(ip->i_d.di_nlink == 0);
			ASSERT(ip->i_d.di_mode != 0);
			ASSERT(ip->i_vnode->v_count == 1);

			/*
			 * Drop our reference to the inode.  This
			 * will send the inode to xfs_inactive()
			 * which will truncate the file and free
			 * the inode.
			 */
			VN_RELE(ip->i_vnode);

			/*
			 * Reacquire the agibuffer and continue around
			 * the loop.
			 */
			agidaddr = XFS_AG_DADDR(mp, agno, XFS_AGI_DADDR);
			agibp = read_buf(mp->m_dev, agidaddr, 1, 0);
			agi = XFS_BUF_TO_AGI(agibp);
			ASSERT(agi->agi_magicnum == XFS_AGI_MAGIC);
		}

		/*
		 * Release the buffer for the current agi so we can
		 * go on to the next one.
		 */
		brelse(agibp);
	}
}	/* xlog_recover_process_iunlinks */


/*
 * Stamp cycle number in every block
 *
 * This routine is also called in xfs_log.c
 */
/*ARGSUSED*/
void
xlog_pack_data(xlog_t *log, xlog_in_core_t *iclog)
{
	int	i;
	caddr_t dp;
#ifdef DEBUG
	uint	*up;
	uint	chksum = 0;

	up = (uint *)iclog->ic_data;
	/* divide length by 4 to get # words */
	for (i=0; i<iclog->ic_offset >> 2; i++) {
		chksum ^= *up;
		up++;
	}
	iclog->ic_header.h_chksum = chksum;
#endif /* DEBUG */

	dp = iclog->ic_data;
	for (i = 0; i<BTOBB(iclog->ic_offset); i++) {
		iclog->ic_header.h_cycle_data[i] = *(uint *)dp;
		*(uint *)dp = CYCLE_LSN(iclog->ic_header.h_lsn);
		dp += BBSIZE;
	}
}	/* xlog_pack_data */


STATIC void
xlog_unpack_data(xlog_rec_header_t *rhead,
		 caddr_t	   dp,
		 xlog_t		   *log)
{
	int i;
#ifdef DEBUG
	uint *up = (uint *)dp;
	uint chksum = 0;
#endif

	for (i=0; i<BTOBB(rhead->h_len); i++) {
		*(uint *)dp = *(uint *)&rhead->h_cycle_data[i];
		dp += BBSIZE;
	}
#ifdef DEBUG
	/* divide length by 4 to get # words */
	for (i=0; i < rhead->h_len >> 2; i++) {
		chksum ^= *up;
		up++;
	}
	if (chksum != rhead->h_chksum) {
	    if (rhead->h_chksum != 0 ||
		((log->l_flags & XLOG_CHKSUM_MISMATCH) == 0)) {
		    cmn_err(CE_DEBUG,
		        "xFS: LogR chksum mismatch: was (0x%x) is (0x%x)\n",
			    rhead->h_chksum, chksum);
		    cmn_err(CE_DEBUG,
"xFS: Disregard message if filesystem was created with non-DEBUG kernel\n");
		    log->l_flags |= XLOG_CHKSUM_MISMATCH;
	    }
        }
#endif /* DEBUG */
}	/* xlog_unpack_data */


/*
 * Read the log from tail to head and process the log records found.
 * Handle the two cases where the tail and head are in the same cycle
 * and where the active portion of the log wraps around the end of
 * the physical log separately.  The pass parameter is passed through
 * to the routines called to process the data and is not looked at
 * here.
 */
STATIC int
xlog_do_recovery_pass(xlog_t	*log,
		      daddr_t	head_blk,
		      daddr_t	tail_blk,
		      int	pass)
{
    xlog_rec_header_t	*rhead;
    daddr_t		blk_no;
    caddr_t		bufaddr;
    buf_t		*hbp, *dbp;
    int			error;
    int		  	bblks, split_bblks;
    xlog_recover_t	*rhash[XLOG_RHASH_SIZE];

    error = 0;
    hbp = xlog_get_bp(1);
    dbp = xlog_get_bp(BTOBB(XLOG_MAX_RECORD_BSIZE));
    bzero(rhash, sizeof(rhash));
    if (tail_blk <= head_blk) {
	for (blk_no = tail_blk; blk_no < head_blk; ) {
	    if (error = xlog_bread(log, blk_no, 1, hbp))
		goto bread_err;
	    rhead = (xlog_rec_header_t *)hbp->b_dmaaddr;
	    ASSERT(rhead->h_magicno == XLOG_HEADER_MAGIC_NUM);
	    bblks = BTOBB(rhead->h_len);	/* blocks in data section */
	    if (bblks > 0) {
		if (error = xlog_bread(log, blk_no+1, bblks, dbp))
		    goto bread_err;
		xlog_unpack_data(rhead, dbp->b_dmaaddr, log);
		if (error = xlog_recover_process_data(log, rhash,
						      rhead, dbp->b_dmaaddr,
						      pass))
		    return error;
	    }
	    blk_no += (bblks+1);
	}
    } else {
	/*
	 * Perform recovery around the end of the physical log.  When the head
	 * is not on the same cycle number as the tail, we can't do a sequential
	 * recovery as above.
	 */
	blk_no = tail_blk;
	while (blk_no < log->l_logBBsize) {

	    /* Read header of one block */
	    if (error = xlog_bread(log, blk_no, 1, hbp))
		goto bread_err;
	    rhead = (xlog_rec_header_t *)hbp->b_dmaaddr;
	    ASSERT(rhead->h_magicno == XLOG_HEADER_MAGIC_NUM);
	    bblks = BTOBB(rhead->h_len);

	    /* LR body must have data or it wouldn't have been written */
	    ASSERT(bblks > 0);
	    blk_no++;			/* successfully read header */
	    ASSERT(blk_no <= log->l_logBBsize);

	    /* Read in data for log record */
	    if (blk_no+bblks <= log->l_logBBsize) {
		if (error = xlog_bread(log, blk_no, bblks, dbp))
		    goto bread_err;
	    } else {
		/* This log record is split across physical end of log */
		split_bblks = 0;
		if (blk_no != log->l_logBBsize) {

		    /* some data is before physical end of log */
		    split_bblks = log->l_logBBsize - blk_no;
		    ASSERT(split_bblks > 0);
		    if (error = xlog_bread(log, blk_no, split_bblks, dbp))
			goto bread_err;
		}
		bufaddr = dbp->b_dmaaddr;
		dbp->b_dmaaddr += BBTOB(split_bblks);
		if (error = xlog_bread(log, 0, bblks - split_bblks, dbp))
		    goto bread_err;
		dbp->b_dmaaddr = bufaddr;
	    }
	    xlog_unpack_data(rhead, dbp->b_dmaaddr, log);
	    if (error = xlog_recover_process_data(log, rhash,
						  rhead, dbp->b_dmaaddr,
						  pass))
		goto bread_err;
	    blk_no += bblks;
	}

	ASSERT(blk_no >= log->l_logBBsize);
	blk_no -= log->l_logBBsize;

	/* read first part of physical log */
	while (blk_no < head_blk) {
	    if (error = xlog_bread(log, blk_no, 1, hbp))
		goto bread_err;
	    rhead = (xlog_rec_header_t *)hbp->b_dmaaddr;
	    ASSERT(rhead->h_magicno == XLOG_HEADER_MAGIC_NUM);
	    bblks = BTOBB(rhead->h_len);
	    ASSERT(bblks > 0);
	    if (error = xlog_bread(log, blk_no+1, bblks, dbp))
		goto bread_err;
	    xlog_unpack_data(rhead, dbp->b_dmaaddr, log);
	    if (error = xlog_recover_process_data(log, rhash,
						  rhead, dbp->b_dmaaddr,
						  pass))
		goto bread_err;
	    blk_no += (bblks+1);
        }
    }

bread_err:
    xlog_put_bp(dbp);
    xlog_put_bp(hbp);

    return error;
}

/*
 * Do the recovery of the log.  We actually do this in two phases.
 * The two passes are necessary in order to implement the function
 * of cancelling a record written into the log.  The first pass
 * determines those things which have been cancelled, and the
 * second pass replays log items normally except for those which
 * have been cancelled.  The handling of the replay and cancellations
 * takes place in the log item type specific routines.
 *
 * The table of items which have cancel records in the log is allocated
 * and freed at this level, since only here do we know when all of
 * the log recovery has been completed.
 */
STATIC int
xlog_do_log_recovery(xlog_t	*log,
		     daddr_t	head_blk,
		     daddr_t	tail_blk)
{
	int		error;
	int		i;

	/*
	 * First do a pass to find all of the cancelled buf log items.
	 * Store them in the buf_cancel_table for use in the second pass.
	 */
	log->l_buf_cancel_table =
		(xfs_buf_cancel_t **)kmem_zalloc(XLOG_BC_TABLE_SIZE *
						 sizeof(xfs_buf_cancel_t*),
						 KM_SLEEP);
	error = xlog_do_recovery_pass(log, head_blk, tail_blk,
				      XLOG_RECOVER_PASS1);
	if (error != 0) {
		kmem_free(log->l_buf_cancel_table,
			  XLOG_BC_TABLE_SIZE * sizeof(xfs_buf_cancel_t*));
		log->l_buf_cancel_table = NULL;
		return error;
	}
	/*
	 * Then do a second pass to actually recover the items in the log.
	 * When it is complete free the table of buf cancel items.
	 */
	error = xlog_do_recovery_pass(log, head_blk, tail_blk,
				      XLOG_RECOVER_PASS2);
#ifdef	DEBUG
	for (i = 0; i < XLOG_BC_TABLE_SIZE; i++) {
		ASSERT(log->l_buf_cancel_table[i] == NULL);
	}
#endif	/* DEBUG */
	kmem_free(log->l_buf_cancel_table,
		  XLOG_BC_TABLE_SIZE * sizeof(xfs_buf_cancel_t*));
	log->l_buf_cancel_table = NULL;

	return error;
}

/*
 * Do the actual recovery
 */
STATIC int
xlog_do_recover(xlog_t	*log,
		daddr_t head_blk,
		daddr_t tail_blk)
{
	buf_t		*bp;
	xfs_sb_t	*sbp;
	int		error;

	/*
	 * First replay the images in the log.
	 */
	error = xlog_do_log_recovery(log, head_blk, tail_blk);
	if (error) {
		return error;
	}

#ifdef _KERNEL
	bflush(log->l_mp->m_dev);    /* Flush out the delayed write buffers */

	/*
	 * We now update the tail_lsn since much of the recovery has completed
	 * and there may be space available to use.  If there were no extent
	 * or iunlinks, we can free up the entire log and set the tail_lsn to
	 * be the last_sync_lsn.  This was set in xlog_find_tail to be the
	 * lsn of the last known good LR on disk.  If there are extent frees
	 * or iunlinks they will have some entries in the AIL; so we look at
	 * the AIL to determine how to set the tail_lsn.
	 */
	xlog_assign_tail_lsn(log->l_mp, 0);

	/*
	 * Now that we've finished replaying all buffer and inode
	 * updates, re-read in the superblock.
	 */
	bp = xfs_getsb(log->l_mp, 0);
	bp->b_flags &= ~B_DONE;
	bp->b_flags |= B_READ;
#ifndef SIM
	bp_dcache_wbinval(bp);
#endif
	bdstrat(bmajor(bp->b_edev), bp);
	if (error = iowait(bp)) {
		ASSERT(0);
		brelse(bp);
		return error;
	}
	sbp = XFS_BUF_TO_SBP(bp);
	ASSERT(sbp->sb_magicnum == XFS_SB_MAGIC);
	ASSERT(sbp->sb_versionnum == XFS_SB_VERSION);
	log->l_mp->m_sb = *sbp;
	brelse(bp);

	xlog_recover_check_summary(log);

	/* Normal transactions can now occur */
	log->l_flags &= ~XLOG_ACTIVE_RECOVERY;
#endif /* _KERNEL */
	return 0;
}	/* xlog_do_recover */


/*
 * Perform recovery and re-initialize some log variables in xlog_find_tail.
 *
 * Return error or zero.
 */
int
xlog_recover(xlog_t *log)
{
	daddr_t head_blk, tail_blk;
	int	error;

	if (error = xlog_find_tail(log, &head_blk, &tail_blk))
		return error;
	if (tail_blk != head_blk) {
#ifdef SIM
		extern daddr_t HEAD_BLK, TAIL_BLK;
		head_blk = HEAD_BLK;
		tail_blk = TAIL_BLK;
#endif
#if defined(DEBUG) && defined(_KERNEL)
		cmn_err(CE_NOTE,
			"Starting xFS recovery on filesystem: %s (dev: %d/%d)",
			log->l_mp->m_fsname, emajor(log->l_dev),
			eminor(log->l_dev));
#endif
		error = xlog_do_recover(log, head_blk, tail_blk);
		log->l_flags |= XLOG_RECOVERY_NEEDED;
	}
	return error;
}	/* xlog_recover */


/*
 * In the first part of recovery we replay inodes and buffers and build
 * up the list of extent free items which need to be processed.  Here
 * we process the extent free items and clean up the on disk unlinked
 * inode lists.  This is separated from the first part of recovery so
 * that the root and real-time bitmap inodes can be read in from disk in
 * between the two stages.  This is necessary so that we can free space
 * in the real-time portion of the file system.
 */
int
xlog_recover_finish(xlog_t *log)
{
	/*
	 * Now we're ready to do the transactions needed for the
	 * rest of recovery.  Start with completing all the extent
	 * free intent records and then process the unlinked inode
	 * lists.  At this point, we essentially run in normal mode
	 * except that we're still performing recovery actions
	 * rather than accepting new requests.
	 */
	if (log->l_flags & XLOG_RECOVERY_NEEDED) {
#ifdef _KERNEL
		xlog_recover_process_efis(log);
		xlog_recover_process_iunlinks(log);
		xlog_recover_check_summary(log);
#endif /* _KERNEL */
		cmn_err(CE_NOTE,
			"Ending xFS recovery for filesystem: %s (dev: %d/%d)",
			log->l_mp->m_fsname, emajor(log->l_dev),
			eminor(log->l_dev));
		log->l_flags &= ~XLOG_RECOVERY_NEEDED;
	} else {
		cmn_err(CE_NOTE,
			"Ending clean xFS mount for filesystem: %s",
			log->l_mp->m_fsname);
	}
	return 0;
}	/* xlog_recover_finish */


#ifdef DEBUG
/*
 * Read all of the agf and agi counters and check that they
 * are consistent with the superblock counters.
 */
void
xlog_recover_check_summary(xlog_t	*log)
{
	xfs_mount_t	*mp;
	xfs_agf_t	*agfp;
	xfs_agi_t	*agip;
	buf_t		*agfbp;
	buf_t		*agibp;
	daddr_t		agfdaddr;
	daddr_t		agidaddr;
	buf_t		*sbbp;
	xfs_sb_t	*sbp;
	xfs_agnumber_t	agno;
	__uint64_t	freeblks;
	__uint64_t	itotal;
	__uint64_t	ifree;

	mp = log->l_mp;
	freeblks = 0LL;
	itotal = 0LL;
	ifree = 0LL;
	for (agno = 0; agno < mp->m_sb.sb_agcount; agno++) {
		agfdaddr = XFS_AG_DADDR(mp, agno, XFS_AGF_DADDR);
		agfbp = read_buf(mp->m_dev, agfdaddr, 1, 0);
		agfp = XFS_BUF_TO_AGF(agfbp);
		ASSERT(agfp->agf_magicnum == XFS_AGF_MAGIC);
		ASSERT(agfp->agf_versionnum == XFS_AGF_VERSION);
		ASSERT(agfp->agf_seqno == agno);

		freeblks += agfp->agf_freeblks + agfp->agf_flcount;
		brelse(agfbp);

		agidaddr = XFS_AG_DADDR(mp, agno, XFS_AGI_DADDR);
		agibp = read_buf(mp->m_dev, agidaddr, 1, 0);
		agip = XFS_BUF_TO_AGI(agibp);
		ASSERT(agip->agi_magicnum == XFS_AGI_MAGIC);
		ASSERT(agip->agi_versionnum == XFS_AGI_VERSION);
		ASSERT(agip->agi_seqno == agno);

		itotal += agip->agi_count;
		ifree += agip->agi_freecount;
		brelse(agibp);
	}

	sbbp = xfs_getsb(mp, 0);
	sbp = XFS_BUF_TO_SBP(sbbp);
	cmn_err(CE_WARN, "xlog_recover_check_summary: sb_icount %lld itotal %lld\n", sbp->sb_icount, itotal);
	cmn_err(CE_WARN, "xlog_recover_check_summary: sb_ifree %lld itotal %lld\n", sbp->sb_ifree, ifree);
	cmn_err(CE_WARN, "xlog_recover_check_summary: sb_fdblocks %lld freeblks %lld\n", sbp->sb_fdblocks, freeblks);
#if 0
	/*
	 * This is turned off until I account for the allocation
	 * btree blocks which live in free space.
	 */
	ASSERT(sbp->sb_icount == itotal);
	ASSERT(sbp->sb_ifree == ifree);
	ASSERT(sbp->sb_fdblocks == freeblks);
#endif
	brelse(sbbp);
}
#endif
