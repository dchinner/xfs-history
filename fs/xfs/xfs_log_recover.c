#ident	"$Revision: 1.27 $"

#include <sys/types.h>
#include <sys/param.h>

#ifdef SIM
#define _KERNEL
#endif

#include <sys/sysmacros.h>
#include <sys/buf.h>
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
#include <sys/sema.h>
#include <sys/vfs.h>

#include "xfs_inum.h"
#include "xfs_types.h"
#include "xfs_log.h"
#include "xfs_ag.h"		/* needed by xfs_sb.h */
#include "xfs_sb.h"		/* depends on xfs_types.h & xfs_inum.h */
#include "xfs_trans.h"
#include "xfs_mount.h"		/* depends on xfs_trans.h & xfs_sb.h */
#include "xfs_bmap_btree.h"
#include "xfs_alloc.h"
#include "xfs_dinode.h"
#include "xfs_imap.h"
#include "xfs_inode_item.h"
#include "xfs_inode.h"
#include "xfs_error.h"
#include "xfs_log_priv.h"	/* depends on all above */
#include "xfs_buf_item.h"
#include "xfs_alloc_btree.h"
#include "xfs_log_recover.h"
#include "xfs_extfree_item.h"
#include "xfs_trans_priv.h"


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
STATIC int
xlog_find_verify_cycle(caddr_t	*bap,		/* update ptr as we go */
		       daddr_t	start_blk,
		       int	nbblks,
		       uint	verify_cycle_no)
{
	int	i;
	uint	cycle;

	for (i=0; i<nbblks; i++) {
		cycle = GET_CYCLE(*bap);
		if (cycle == verify_cycle_no) {
			(*bap) += BBSIZE;
		} else {
			return (start_blk+i);
		}
	}
	return -1;
}	/* xlog_find_verify_cycle */


/*
 * Potentially backup over partial log record write
 */
STATIC int
xlog_find_verify_log_record(caddr_t	ba,	     /* update ptr as we go */
			    daddr_t	start_blk,
			    daddr_t	*last_blk)
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
		xlog_warn("xFS: xlog_find_verify_log_record: need to backup");
		ASSERT(0);
		return XFS_ERROR(EIO);
	}
	    ba -= BBSIZE;
	}
    }
    rhead = (xlog_rec_header_t *)ba;
    if (*last_blk - i != BTOBB(rhead->h_len)+1)
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
 * Also called from xfs_log_print.c
 *
 * Return: zero if normal, non-zero if error.
 */
int
xlog_find_head(xlog_t  *log,
	       daddr_t *head_blk)
{
	buf_t	*bp, *big_bp;
	daddr_t	new_blk, first_blk, start_blk, mid_blk, last_blk;
	daddr_t num_scan_bblks;
	uint	first_half_cycle, mid_cycle, last_half_cycle, cycle;
	caddr_t	ba;
	int	error, i, log_bbnum = log->l_logBBsize;

	/* special case freshly mkfs'ed filesystem */
	if ((error = xlog_find_zeroed(log, &first_blk)) == -1) {
		*head_blk = first_blk;
		return 0;
	} else if (error)
		return error;

	first_blk = 0;					/* read first block */
	bp = xlog_get_bp(1);
	if (error = xlog_bread(log, 0, 1, bp))
		goto bp_err;
	first_half_cycle = GET_CYCLE(bp->b_dmaaddr);

	last_blk = log->l_logBBsize-1;			/* read last block */
	if (error = xlog_bread(log, last_blk, 1, bp))
		goto bp_err;
	last_half_cycle = GET_CYCLE(bp->b_dmaaddr);
	ASSERT(last_half_cycle != 0);

	if (first_half_cycle == last_half_cycle) {/* all cycle nos are same */
		last_blk = 0;
	} else {		/* have 1st and last; look for middle cycle */
		if (error = xlog_find_cycle_start(log, bp, first_blk,
						  &last_blk, last_half_cycle))
			goto bp_err;
	}

	/* Now validate the answer */
	num_scan_bblks = BTOBB(XLOG_MAX_ICLOGS<<XLOG_MAX_RECORD_BSHIFT);
	big_bp = xlog_get_bp(num_scan_bblks);
	if (last_blk >= num_scan_bblks) {
		start_blk = last_blk - num_scan_bblks;
		if (error = xlog_bread(log, start_blk, num_scan_bblks, big_bp))
			goto big_bp_err;
		ba = big_bp->b_dmaaddr;
		new_blk = xlog_find_verify_cycle(&ba, start_blk,
						 num_scan_bblks,
						 first_half_cycle);
		if (new_blk != -1)
			last_blk = new_blk;
	} else {	/* need to read 2 parts of log */
		/* scan end of physical log */
		start_blk = log_bbnum - num_scan_bblks + last_blk;
		if (error = xlog_bread(log, start_blk,
				       num_scan_bblks - last_blk, big_bp))
			goto big_bp_err;
		ba = big_bp->b_dmaaddr;
		new_blk = xlog_find_verify_cycle(&ba, start_blk,
						 num_scan_bblks - last_blk,
						 last_half_cycle);
		if (new_blk != -1) {
			last_blk = new_blk;
			goto bad_blk;
		}

		/* scan beginning of physical log */
		start_blk = 0;
		if (error = xlog_bread(log, start_blk, last_blk, big_bp))
			goto big_bp_err;
		ba = big_bp->b_dmaaddr;
		new_blk = xlog_find_verify_cycle(&ba, start_blk, last_blk,
						 first_half_cycle);
		if (new_blk != -1)
			last_blk = new_blk;
	}

bad_blk:
	/* Potentially backup over partial log record write */
	if (error = xlog_find_verify_log_record(ba, start_blk, &last_blk))
		goto big_bp_err;
	
	xlog_put_bp(big_bp);
	xlog_put_bp(bp);
	*head_blk = last_blk;
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
STATIC int
xlog_find_tail(xlog_t  *log,
	       daddr_t *head_blk,
	       daddr_t *tail_blk)
{
	xlog_rec_header_t *rhead;
	xlog_op_header_t  *op_head;
	buf_t		  *bp;
	int		  error, i, found, zeroed_log;
	
	found = error = zeroed_log = 0;
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
				found = 1;
				break;
			}
		}
	}
	if (!found) {
		xlog_warn("xFS: xlog_find_tail: counldn't find sync record");
		ASSERT(0);
		return XFS_ERROR(EIO);
	}

	/* find blk_no of tail of log */
	rhead = (xlog_rec_header_t *)bp->b_dmaaddr;
	*tail_blk = BLOCK_LSN(rhead->h_tail_lsn);

	/*
	 * Reset log values according to the state of the log when we crashed.
	 */
	log->l_prev_block = i;
	log->l_curr_block = *head_blk;
	log->l_curr_cycle = rhead->h_cycle;
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
	uint	first_cycle, mid_cycle, last_cycle, cycle;
	daddr_t	new_blk, first_blk, mid_blk, last_blk, start_blk;
	daddr_t num_scan_bblks;
	caddr_t	ba;
	int	error, i, log_bbnum = log->l_logBBsize;

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
	}
	ASSERT(first_cycle == 1);

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
	new_blk = xlog_find_verify_cycle(&ba, start_blk, num_scan_bblks, 1);
	if (new_blk != -1)
		last_blk = new_blk;

	/* Potentially backup over partial log record write */
	if (error = xlog_find_verify_log_record(ba, start_blk, &last_blk))
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
	xfs_trans_header_t	 *thead;
	caddr_t			 ptr;
	int			 total;

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


STATIC void
xlog_recover_print_trans_head(xlog_recover_t *tr)
{
    cmn_err(CE_CONT,
	    "TRANS: tid: 0x%x type: %d #: %d trans: 0x%x q: 0x%x\n",
	    tr->r_log_tid, tr->r_theader.th_type, tr->r_theader.th_num_items,
	    tr->r_theader.th_tid, tr->r_itemq);
}	/* xlog_recover_print_trans_head */


STATIC void
xlog_recover_print_item(xlog_recover_item_t *item)
{
	int i;

	switch (ITEM_TYPE(item)) {
	    case XFS_LI_BUF: {
		cmn_err(CE_CONT, "BUF ");
		break;
	    }
	    case XFS_LI_INODE: {
		cmn_err(CE_CONT, "INO ");
		break;
	    }
	    case XFS_LI_EFD: {
		cmn_err(CE_CONT, "EFD ");
		break;
	    }
	    case XFS_LI_EFI: {
		cmn_err(CE_CONT, "EFI ");
		break;
	    }
	    default: {
		cmn_err(CE_PANIC, "xlog_recover_print_item: illegal type\n");
		break;
	    }
	}
	cmn_err(CE_CONT,
		"ITEM: type: %d cnt: %d ttl: %d ",
		item->ri_type, item->ri_cnt, item->ri_total);
	for (i=0; i<item->ri_cnt; i++) {
		cmn_err(CE_CONT, "a: 0x%x len: %d ",
			item->ri_buf[i].i_addr, item->ri_buf[i].i_len);
	}
	cmn_err(CE_CONT, "\n");
}	/* xlog_recover_print_item */


STATIC void
xlog_recover_print_trans(xlog_recover_t	     *trans,
			 xlog_recover_item_t *itemq,
			 int		     print)
{
	xlog_recover_item_t *first_item, *item;

	if (print < 3)
		return;

	cmn_err(CE_CONT, "======================================\n");
	xlog_recover_print_trans_head(trans);
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
	    case XFS_LI_BUF: {
		xlog_recover_insert_item_frontq(&trans->r_itemq, itemq);
		break;
	    }
	    case XFS_LI_INODE:
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


STATIC int
xlog_recover_do_buffer_trans(xlog_t		 *log,
			     xlog_recover_item_t *item)
{
	xfs_buf_log_format_t *buf_f;
	xfs_mount_t	     *mp;
	buf_t		     *bp;
	int		     error, nbits, bit = 0;
	int		     i = 1;	/* 0 is format structure */

	buf_f = (xfs_buf_log_format_t *)item->ri_buf[0].i_addr;
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
		ASSERT(bp->b_bcount >= ((uint)bit << XFS_BLI_SHIFT)+(nbits<<XFS_BLI_SHIFT));
		bcopy(item->ri_buf[i].i_addr,		           /* source */
		      bp->b_un.b_addr+((uint)bit << XFS_BLI_SHIFT),  /* dest */
		      nbits<<XFS_BLI_SHIFT);			   /* length */
		i++;
		bit += nbits;
	}

	/*
	 * Perform delayed write on the buffer.  Asynchronous writes will be
	 * slower when taking into account all the buffers to be flushed.
	 */
	bp->b_flags = (B_BUSY | B_HOLD);	/* synchronous */
	bdwrite(bp);
	/*
	 * Once bdwrite() is called, we lose track of the buffer.  Therefore,
	 * if we want to keep track of buffer errors, we need to add a
	 * release function which sets some variable which gets looked at
	 * after calling bflush() on the device.  XXXmiken
	 */

	/* Shouldn't be any more regions */
	if (i < XLOG_MAX_REGIONS_IN_ITEM) {
		ASSERT(item->ri_buf[i].i_addr == 0);
		ASSERT(item->ri_buf[i].i_len == 0);
	}
	return 0;
}	/* xlog_recover_do_buffer_trans */


STATIC int
xlog_recover_do_inode_trans(xlog_t		*log,
			    xlog_recover_item_t *item)
{
	xfs_inode_log_format_t	*in_f;
	xfs_mount_t		*mp;
	buf_t			*bp;
	xfs_imap_t		imap;
	xfs_dinode_t		*dip;
	int			len;
	caddr_t			src;
	int			error;

	in_f = (xfs_inode_log_format_t *)item->ri_buf[0].i_addr;
	mp = log->l_mp;
	xfs_imap(log->l_mp, 0, in_f->ilf_ino, &imap);
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
	bp->b_flags = (B_BUSY | B_HOLD);
	xfs_inobp_check(mp, bp);
	bdwrite(bp);
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
			  xfs_lsn_t		lsn)	  
{
	xfs_mount_t		*mp;
	xfs_efi_log_item_t	*efip;
	xfs_efi_log_format_t	*efi_formatp;
	int			spl;

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
 	xfs_trans_update_ail(mp, (xfs_log_item_t *)efip, lsn);
	AIL_UNLOCK(mp, spl);
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
			  xlog_recover_item_t	*item)
{
	xfs_mount_t		*mp;
	xfs_efd_log_format_t	*efd_formatp;
	xfs_efi_log_item_t	*efip;
	xfs_log_item_t		*lip;
	int			spl;
	int			gen;
	__uint64_t		efi_id;

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
				xfs_trans_delete_ail(mp, lip);
				break;
			}
		}
		lip = xfs_trans_next_ail(mp, lip, &gen, NULL);
	}
	AIL_UNLOCK(mp, spl);

	/*
	 * If we found it, then free it up.  If it wasn't there, it
	 * must have been overwritten in the log.  Oh well.
	 */
	if (lip != NULL) {
		kmem_free(lip, sizeof(xfs_efi_log_item_t) +
			  ((efip->efi_format.efi_nextents - 1) *
			   sizeof(xfs_extent_t)));
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
		      xlog_recover_t *trans)
{
	xlog_recover_item_t *item, *first_item;
	int		    error = 0;

	xlog_recover_print_trans(trans, trans->r_itemq, xlog_debug);
	if (error = xlog_recover_reorder_trans(log, trans))
		return error;
	xlog_recover_print_trans(trans, trans->r_itemq, xlog_debug+1);

	first_item = item = trans->r_itemq;
	do {
		if (ITEM_TYPE(item) == XFS_LI_BUF) {
			if (error = xlog_recover_do_buffer_trans(log, item))
				break;
		} else if (ITEM_TYPE(item) == XFS_LI_INODE) {
			if (error = xlog_recover_do_inode_trans(log, item))
				break;
		} else if (ITEM_TYPE(item) == XFS_LI_EFI) {
			xlog_recover_do_efi_trans(log, item, trans->r_lsn);
		} else if (ITEM_TYPE(item) == XFS_LI_EFD) {
			xlog_recover_do_efd_trans(log, item);
		} else {
			xlog_warn("xFS: xlog_recover_do_buffer_inode_trans");
			ASSERT(0);
			error = XFS_ERROR(EIO);
			break;
		}
		item = item->ri_next;
	} while (first_item != item);

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
			  xlog_recover_t *trans)
{
	int error;

	if (error = xlog_recover_unlink_tid(q, trans))
		return error;
	if (error = xlog_recover_do_trans(log, trans))
		return error;
	xlog_recover_free_trans(trans);			/* no error */
	return 0;
}	/* xlog_recover_commit_trans */


STATIC int
xlog_recover_unmount_trans(xlog_recover_t *trans)
{
	/* Do nothing now */
	xlog_warn("xFS: xlog_recover_unmount_trans: Unmount LR");
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
			  caddr_t	    dp)
{
    caddr_t		lp	   = dp+rhead->h_len;
    int			num_logops = rhead->h_num_logops;
    xlog_op_header_t	*ohead;
    xlog_recover_t	*trans;
    xlog_tid_t		tid;
    int			hash, error;
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
		    error = xlog_recover_commit_trans(log, &rhash[hash], trans);
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
		    error = xlog_recover_add_to_trans(trans, dp, ohead->oh_len);
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

	ASSERT(!(efip->efi_flags & XFS_EFI_RECOVERED));

	tp = xfs_trans_alloc(mp, 0);
	xfs_trans_reserve(tp, 0, XFS_DEFAULT_LOG_RES(mp), 0, 0,
			  XFS_DEFAULT_LOG_COUNT);
	efdp = xfs_trans_get_efd(tp, efip, efip->efi_format.efi_nextents);

	for (i = 0; i < efip->efi_format.efi_nextents; i++) {
		extp = &(efip->efi_format.efi_extents[i]);
		xfs_free_extent(tp, extp->ext_start, extp->ext_len);
		xfs_trans_log_efd_extent(tp, efdp, extp->ext_start,
					 extp->ext_len);
	}

	efip->efi_flags |= XFS_EFI_RECOVERED;
	xfs_trans_commit(tp, 0);
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


#ifndef SIM
/*
 * Do the actual recovery
 */
STATIC int
xlog_do_recover(xlog_t	*log,
		daddr_t head_blk,
		daddr_t tail_blk)
{
    xlog_rec_header_t	*rhead;
    daddr_t		blk_no;
    caddr_t		bufaddr;
    buf_t		*hbp, *dbp;
    buf_t		*bp;
    xfs_sb_t		*sbp;
    int			error;
    int		  	bblks, split_bblks;
    xlog_recover_t	*rhash[XLOG_RHASH_SIZE];

    error = 0;
    /*
     * This first part of the routine will read in the part of the log
     * containing data to recover.  It will replay all committed buffer and
     * inode transactions.  Extent and iunlink transactions are processed
     * at the end.  Buffers are reused by each read and are not used by
     * the delayed write calls.  The write calls get their own buffers.
     */
    hbp = xlog_get_bp(1);
    dbp = xlog_get_bp(BTOBB(XLOG_RECORD_BSIZE));
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
						      rhead, dbp->b_dmaaddr))
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
						  rhead, dbp->b_dmaaddr))
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
						  rhead, dbp->b_dmaaddr))
		goto bread_err;
	    blk_no += (bblks+1);
        }
    }

bread_err:
    xlog_put_bp(dbp);
    xlog_put_bp(hbp);
    if (error)
	    return error;

    bflush(log->l_mp->m_dev);    /* Flush out all the delayed write buffers */

    /*
     * We now update the tail_lsn since much of the recovery has completed
     * and there may be space available to use.  If there were no extent
     * or iunlinks, we can free up the entire log and set the tail_lsn to be
     * the last_sync_lsn.  This was set in xlog_find_tail to be the lsn of the
     * last known good LR on disk.  If there are extent frees or iunlinks,
     * they will have some entries in the AIL; so we look at the AIL to
     * determine how to set the tail_lsn.
     */
    xlog_assign_tail_lsn(log->l_mp, 0);

    /*
     * Now that we've finished replaying all buffer and inode
     * updates, re-read in the superblock.
     */
    bp = xfs_getsb(log->l_mp, 0);
    bp->b_flags &= ~B_DONE;
    bp->b_flags |= B_READ;
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

    /*
     * Now we're ready to do the transactions needed for the
     * rest of recovery.  Start with completing all the extent
     * free intent records and then process the unlinked inode
     * lists.  At this point, we essentially run in normal mode
     * except that we're still performing recovery actions
     * rather than accepting new requests.
     */
    xlog_recover_process_efis(log);
    xlog_recover_process_iunlinks(log);
    xlog_recover_check_summary(log);
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
	int error;

	if (error = xlog_find_tail(log, &head_blk, &tail_blk))
		return error;
	if (tail_blk != head_blk) {
		error = xlog_do_recover(log, head_blk, tail_blk);
	}
	return error;
}	/* xlog_recover */
#endif	/* !SIM */


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
