#ident	"$Revision: 1.2 $"

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

#include <sys/kmem.h>
#include <sys/debug.h>
#include <sys/sema.h>
#include <sys/uuid.h>
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
#include "xfs_log_recover.h"


#ifdef SIM
#include "sim.h"		/* must be last include file */
#endif

STATIC int	xlog_find_zeroed(xlog_t *log, daddr_t *blk_no);

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
void
xlog_bread(xlog_t	*log,
	   daddr_t	blk_no,
	   int		nbblks,
	   buf_t	*bp)
{
	ASSERT(nbblks > 0);

	bp->b_blkno	= blk_no + log->l_logBBstart;
	bp->b_flags	= B_READ|B_BUSY;
	bp->b_bcount	= BBTOB(nbblks);
	bp->b_bufsize	= bp->b_bcount;
	bp->b_edev	= log->l_dev;

	bdstrat(bmajor(bp->b_edev), bp);
	iowait(bp);

	if (bp->b_flags & B_ERROR) {
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
 * write could go.  This means that incomplete LR writes at the end are
 * eliminated when calculating the head.  We aren't guaranteed that previous
 * LR have complete transactions.  We only know that a cycle number of 
 * current cycle number -1 won't be present in the log if we start writing
 * from our current block number.
 */
daddr_t
xlog_find_head(xlog_t *log)
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
	
	xlog_put_bp(big_bp);
	xlog_put_bp(bp);
	return last_blk;
}	/* xlog_find_head */


/*
 * Start is defined to be the block pointing to the oldest valid log record.
 */
daddr_t
xlog_print_find_oldest(xlog_t *log)
{
	buf_t	*bp;
	daddr_t	first_blk, last_blk;
	uint	first_half_cycle, last_half_cycle;
	
	if (xlog_find_zeroed(log, &first_blk))
		return 0;

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

	xlog_put_bp(bp);
	return last_blk;
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
 * We could speed up search by using current head_blk buffer, but it is not
 * available.
 */
static daddr_t
xlog_find_tail(xlog_t  *log,
	       daddr_t *head_blk)
{
	xlog_rec_header_t *rhead;
	xlog_op_header_t  *op_head;
	daddr_t		  tail_blk;
	buf_t		  *bp;
	int		  i, found = 0;
	int		  zeroed_log = 0;
	
	/* find previous log record */
	*head_blk = xlog_find_head(log);

	bp = xlog_get_bp(1);
	if (*head_blk == 0) {
		xlog_bread(log, 0, 1, bp);
		if (GET_CYCLE(bp->b_dmaaddr) == 0) {
			tail_blk = 0;
			/* leave all other log inited values alone */
			goto exit;
		}
	}
	for (i=(*head_blk)-1; i>=0; i--) {
		xlog_bread(log, i, 1, bp);
		if (*(uint *)(bp->b_dmaaddr) == XLOG_HEADER_MAGIC_NUM) {
			found = 1;
			break;
		}
#if 0
 else if (*(uint *)(bp->b_dmaaddr == 0)) {
			if (zeroed_log)
				goto exit;
			zeroed_log = 1;
			tail_blk = i;
		}
#endif
	}
	if (!found) {		/* search from end of log */
		for (i=log->l_logBBsize-1; i>=(*head_blk); i--) {
			xlog_bread(log, i, 1, bp);
			if (*(uint *)(bp->b_dmaaddr) == XLOG_HEADER_MAGIC_NUM) {
				found = 1;
				break;
			}
#if 0
 else if (*(uint *)(bp->b_dmaaddr == 0)) {
				if (zeroed_log)
					goto exit;
				zeroed_log = 1;
				tail_blk = i;
			}
#endif
		}
	}
	if (!found)
		xlog_panic("xlog_find_tail: counldn't find sync record");

	/* find blk_no of tail of log */
	rhead = (xlog_rec_header_t *)bp->b_dmaaddr;
	tail_blk = BLOCK_LSN(rhead->h_tail_lsn);

	log->l_prev_block = i;
	log->l_curr_block = *head_blk;
	log->l_curr_cycle = rhead->h_cycle;
	log->l_tail_lsn = rhead->h_tail_lsn;
	log->l_last_sync_lsn = log->l_tail_lsn;
	if (*head_blk == i+2 && rhead->h_num_logops == 1) {
		xlog_bread(log, i+1, 1, bp);
		op_head = (xlog_op_header_t *)bp->b_dmaaddr;
		if ((op_head->oh_flags & XLOG_UNMOUNT_TRANS) != 0) {
			log->l_tail_lsn =
			     ((long long)log->l_curr_cycle << 32)|((uint)(i+2));
		}
	}
	if (BLOCK_LSN(log->l_tail_lsn) <= *head_blk) {
		log->l_logreserved = BBTOB(*head_blk-BLOCK_LSN(log->l_tail_lsn));
	} else {
		log->l_logreserved =
		    log->l_logsize - BBTOB(BLOCK_LSN(log->l_tail_lsn)-*head_blk);
	}
exit:
	xlog_put_bp(bp);

	return tail_blk;
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
		xlog_put_bp(bp);
		return 1;
	}

	/* check partially zeroed log */
	xlog_bread(log, log_bbnum-1, 1, bp);
	last_cycle = GET_CYCLE(bp->b_dmaaddr);
	if (last_cycle != 0) {		/* log completely written to */
		xlog_put_bp(bp);
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
	xlog_put_bp(big_bp);
	xlog_put_bp(bp);
	return 1;
}	/* xlog_find_zeroed */


/******************************************************************************
 *
 *		Log recover routines
 *
 ******************************************************************************
 */

static xlog_recover_t *
xlog_recover_find_tid(xlog_recover_t *q,
		      xlog_tid_t     tid)
{
	xlog_recover_t *p = q;

	while (p != NULL) {
		if (p->r_tid == tid)
		    break;
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
		(*ihead)->ri_prev	= item;
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

	old_ptr = item->ri_buf[item->ri_cnt-1].i_addr;
	old_len = item->ri_buf[item->ri_cnt-1].i_len;

	ptr = kmem_realloc(old_ptr, len, 0);
	bcopy(&ptr[old_len], dp, len);
	item->ri_buf[item->ri_cnt-1].i_len += old_len;
}	/* xlog_recover_add_to_cont_trans */


static void
xlog_recover_add_to_trans(xlog_recover_t	*trans,
			  caddr_t		dp,
			  int			len)
{
	xfs_inode_log_format_t	 *in_f;			/* any will do */
	xlog_recover_item_t	 *item;
	xfs_trans_header_t	 *thead;
	caddr_t			 ptr;
	int			 total;

	ptr = kmem_alloc(len, 0);
	bcopy(dp, ptr, len);
	
	in_f = (xfs_inode_log_format_t *)ptr;
	item = trans->r_transq;
	if (item == 0) {
		xlog_recover_add_item(&trans->r_transq);
		ASSERT(*(uint *)dp == XFS_TRANS_HEADER_MAGIC);
		thead			= (xfs_trans_header_t *)dp;
		trans->r_type		= thead->th_type;
		trans->r_items		= thead->th_num_items;
		trans->r_trans_tid	= thead->th_tid;
		return;
	}
	if (item->ri_prev->ri_total != 0 &&
	     item->ri_prev->ri_total == item->ri_prev->ri_cnt) {
		xlog_recover_add_item(&trans->r_transq);
	}
	item = trans->r_transq;
	item = item->ri_prev;

	if (item->ri_total == 0) {		/* first region to be added */
		item->ri_total	= in_f->ilf_size;
	}
	ASSERT(item->ri_total > item->ri_cnt);
	/* Description region is ri_buf[0] */
	item->ri_buf[item->ri_cnt].i_addr = ptr;
	item->ri_buf[item->ri_cnt].i_len  = len;
	item->ri_cnt++;
}	/* xlog_recover_add_to_trans */


static void
xlog_recover_new_tid(xlog_recover_t	**q,
		     xlog_tid_t		tid)
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


static void
xlog_recover_delete_tid(xlog_recover_t	**q,
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
		if (!found)
			xlog_panic("xlog_recover_delete_tid: trans not found");
		tp->r_next = tp->r_next->r_next;
	}
}	/* xlog_recover_new_tid */


static void
xlog_recover_print_trans(xlog_recover_t *tr)
{
    cmn_err(CE_CONT,
	    "TRANS: tid: 0x%x type: %d #: %d trans: 0x%x q: 0x%x\n",
	    tr->r_tid, tr->r_type, tr->r_items, tr->r_trans_tid,
	    tr->r_transq);
}	/* xlog_recover_print_trans */

static void
xlog_recover_print_item(xlog_recover_item_t *item)
{
	int i;

	cmn_err(CE_CONT,
		"ITEM: type: %d cnt: %d ttl: %d ",
		item->ri_type, item->ri_cnt, item->ri_total);
	for (i=0; i<item->ri_cnt; i++) {
		cmn_err(CE_CONT, "a: 0x%x len: %d ",
			item->ri_buf[i].i_addr, item->ri_buf[i].i_len);
	}
	cmn_err(CE_CONT, "\n");
}	/* xlog_recover_print_item */

static void
xlog_recover_do_trans(xlog_recover_t *trans)
{
	xlog_recover_item_t *first_item, *item;

	if (xlog_debug < 2)
		return;
	xlog_recover_print_trans(trans);
	item = first_item = trans->r_transq;
	do {
		xlog_recover_print_item(item);
		item = item->ri_next;
	} while (first_item != item);
}	/* xlog_recover_do_trans */


static void
xlog_recover_free_trans(xlog_recover_t *trans)
{
	xlog_recover_item_t *first_item, *item, *free_item;

	item = first_item = trans->r_transq;
	do {
		free_item = item;
		item = item->ri_next;
		kmem_free(free_item, sizeof(xlog_recover_item_t));
	} while (first_item != item);
	kmem_free(trans, sizeof(xlog_recover_t));
}	/* xlog_recover_free_trans */


static void
xlog_recover_commit_trans(xlog_recover_t **q,
			  xlog_recover_t *trans)
{
	xlog_recover_delete_tid(q, trans);
	xlog_recover_do_trans(trans);
	xlog_recover_free_trans(trans);
}	/* xlog_recover_commit_trans */


static void
xlog_recover_unmount_trans(xlog_recover_t *trans)
{
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
static void
xlog_recover_process_data(xlog_recover_t    *rhash[],
			  xlog_rec_header_t *rhead,
			  caddr_t	    dp)
{
    caddr_t		lp	   = dp+rhead->h_len;
    int			num_logops = rhead->h_num_logops;
    xlog_op_header_t	*ohead;
    xlog_recover_t	*trans;
    xlog_tid_t		tid;
    int			hash;

    while (dp < lp) {
	ASSERT(dp + sizeof(xlog_op_header_t) <= lp);
	ohead = (xlog_op_header_t *)dp;
	dp += sizeof(xlog_op_header_t);
	if (ohead->oh_clientid != XFS_TRANSACTION &&
	    ohead->oh_clientid != XFS_LOG)
	    xlog_panic("xlog_recover_process_data: bad clientid ");
	tid = ohead->oh_tid;
	hash = XLOG_RHASH(tid);
	trans = xlog_recover_find_tid(rhash[hash], tid);
	if (trans == NULL) {			    /* not found; add new tid */
	    if ((ohead->oh_flags & XLOG_START_TRANS) != 0)
		xlog_recover_new_tid(&rhash[hash], tid);
	} else {
	    ASSERT(dp+ohead->oh_len <= lp);
	    switch (ohead->oh_flags & ~XLOG_END_TRANS) {
		case XLOG_COMMIT_TRANS: {
		    xlog_recover_commit_trans(&rhash[hash], trans);
		    break;
		}
		case XLOG_UNMOUNT_TRANS: {
		    xlog_recover_unmount_trans(trans);
		    break;
		}
		case XLOG_WAS_CONT_TRANS: {
		    xlog_recover_add_to_cont_trans(trans, dp, ohead->oh_len);
		    break;
		}
		case XLOG_START_TRANS : {
		    xlog_panic("xlog_recover_process_data: bad transaction");
		    break;
		}
		case 0:
		case XLOG_CONTINUE_TRANS: {
		    xlog_recover_add_to_trans(trans, dp, ohead->oh_len);
		    break;
		}
		default: {
		    xlog_panic("xlog_recover_process_data: bad flag");
		    break;
		}
	    }
	}
	dp += ohead->oh_len;
	num_logops--;
    }
}	/* xlog_recover_process_data */


/*
 * Stamp cycle number in every block.
 */
void
xlog_pack_data(xlog_t *log, xlog_in_core_t *iclog)
{
	int	i;
	caddr_t dp;

	dp = iclog->ic_data;
	for (i = 0; i<BTOBB(iclog->ic_offset); i++) {
		iclog->ic_header.h_cycle_data[i] = *(uint *)dp;
		*(uint *)dp = CYCLE_LSN(iclog->ic_header.h_lsn);
		dp += BBSIZE;
	}
}	/* xlog_pack_data */


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


/*
 * Do the actual recovery.
 */
static int
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
			if (bblks > 0) {
				xlog_bread(log, blk_no+1, bblks, dbp);
				xlog_unpack_data(rhead, dbp->b_dmaaddr);
				xlog_recover_process_data(rhash, rhead,
							  dbp->b_dmaaddr);
			}
			blk_no += (bblks+1);
		}
	} else {
	}
}	/* xlog_do_recover */


/*
 * Perform recovery and re-initialize some log variables in xlog_find_tail.
 */
int
xlog_recover(xlog_t *log)
{
	daddr_t head_blk, tail_blk;

	tail_blk = xlog_find_tail(log, &head_blk);
	if (tail_blk != head_blk) {
		xlog_do_recover(log, head_blk, tail_blk);
	}
	return 0;
}


