#ident	"$Revision: 1.3 $"
#ifdef SIM
#define _KERNEL 1
#endif
#include <sys/param.h>
#include <sys/buf.h>
#include <sys/vnode.h>
#include <sys/uuid.h>
#ifdef SIM
#undef _KERNEL
#endif

#include <sys/errno.h>
#include <sys/kmem.h>
#include <sys/debug.h>
#include <sys/proc.h>
#include <sys/cmn_err.h>	
#include <sys/atomic_ops.h>
#ifdef SIM
#include <bstring.h>
#else
#include <sys/systm.h>
#endif

#include "xfs_macros.h"
#include "xfs_types.h"
#include "xfs_inum.h"
#include "xfs_log.h"
#include "xfs_trans.h"
#include "xfs_sb.h"
#include "xfs_mount.h"
#include "xfs_alloc_btree.h"
#include "xfs_bmap_btree.h"
#include "xfs_ialloc_btree.h"
#include "xfs_alloc.h"
#include "xfs_bmap.h"
#include "xfs_btree.h"
#include "xfs_bmap.h"
#include "xfs_attr_sf.h"
#include "xfs_dir_sf.h"
#include "xfs_dinode.h"
#include "xfs_inode_item.h"
#include "xfs_inode.h"
#include "xfs_trans_priv.h"
#include "xfs_buf_item.h"
#include "xfs_quota.h"
#include "xfs_dquot.h"
#include "xfs_qm.h"
#include "xfs_quota_priv.h"

#ifdef SIM
#include "sim.h"
#include <stdio.h>
#include <stdlib.h>
#endif

STATIC
int		xfs_trans_dqresv( xfs_trans_t	*tp,
				 xfs_dquot_t 	*dqp,
				 long  		nblks,
				 xfs_qcnt_t	hardlimit,
				 xfs_qcnt_t	softlimit,
				 time_t		btimer,
				 xfs_qcnt_t	*resbcount,
				 uint		flags);

STATIC 
void		xfs_trans_dqlockedjoin(xfs_trans_t *tp, 
				       xfs_dqtrx_t *q);


#define XFS_QM_INIT_DQARGS(flags, dqp, hlim, slim, timer, resbcntp) { \
		if ((flags) & XFS_TRANS_DQ_RES_BLKS) { \
			(hlim) = (dqp)->q_core.d_blk_hardlimit;\
			(slim) = (dqp)->q_core.d_blk_softlimit;\
			(timer) = (dqp)->q_core.d_btimer;\
			(resbcntp) = &(dqp)->q_res_bcount;\
		} else if ((flags) & XFS_TRANS_DQ_RES_RTBLKS) {\
			(hlim) = (dqp)->q_core.d_rtb_hardlimit;\
			(slim) = (dqp)->q_core.d_rtb_softlimit;\
			(timer) = (dqp)->q_core.d_rtbtimer;\
			(resbcntp) = &(dqp)->q_res_rtbcount;\
		} else { \
			 ASSERT(0);\
		}\
}
#define XFS_QM_INIT_DQRESARGS(flags, dqp, resbcntp) { \
		if ((flags) & XFS_TRANS_DQ_RES_BLKS) { \
			(resbcntp) = &(dqp)->q_res_bcount;\
		} else if ((flags) & XFS_TRANS_DQ_RES_RTBLKS) {\
			(resbcntp) = &(dqp)->q_res_rtbcount;\
		} else { \
			 ASSERT(0);\
		}\
}       		      
/*
 * Add the locked dquot to the transaction.
 * The dquot must be locked, and it cannot be associated with any
 * transaction.  
 */
void
xfs_trans_dqjoin(
        xfs_trans_t     *tp,
	xfs_dquot_t     *dqp)
{
        xfs_dq_logitem_t    *lp;
	
        ASSERT(! XFS_DQ_IS_ADDEDTO_TRX(tp, dqp));
        ASSERT(XFS_DQ_IS_LOCKED(dqp));
        ASSERT(XFS_DQ_IS_LOGITEM_INITD(dqp));
	lp = &dqp->q_logitem;
        ASSERT(lp->qli_flags == 0);
	
        /*
         * Get a log_item_desc to point at the new item.
         */
        (void) xfs_trans_add_item(tp, (xfs_log_item_t*)(lp));

        /*
         * Initialize i_transp so we can later determine if this dquot is
	 * associated with this transaction.
         */
        dqp->q_transp = tp;
}


/*
 * This is called to mark the dquot as needing
 * to be logged when the transaction is committed.  The dquot must
 * already be associated with the given transaction.
 *
 */
void
xfs_trans_log_dquot(
        xfs_trans_t     *tp,
        xfs_dquot_t     *dqp)
{
        xfs_log_item_desc_t     *lidp;

	ASSERT(XFS_DQ_IS_ADDEDTO_TRX(tp, dqp));
        ASSERT(XFS_DQ_IS_LOCKED(dqp));

        lidp = xfs_trans_find_item(tp, (xfs_log_item_t*)(&dqp->q_logitem));
        ASSERT(lidp != NULL);

        tp->t_flags |= XFS_TRANS_DIRTY;
        lidp->lid_flags |= XFS_LID_DIRTY;
}

/*
 * Carry forward whatever is left of the quota blk reservation to
 * the spanky new transaction
 */
void
xfs_trans_dup_dqinfo(
	xfs_trans_t	*otp, 
	xfs_trans_t	*ntp)
{
	xfs_dqtrx_t 	*oq, *nq;
	int		i,j;
	xfs_dqtrx_t 	*oqa, *nqa;

	xfs_trans_alloc_dqinfo(ntp);
	oqa = otp->t_dqinfo->dqa_usrdquots;
	nqa = ntp->t_dqinfo->dqa_usrdquots;
	for (j = 0; j < 2; j++) {
		for (i = 0; i < XFS_QM_TRANS_MAXDQS; i++) {
			if (oqa[i].qt_dquot == NULL)
				break;
			oq = &oqa[i];
			nq = &nqa[i];
		
			nq->qt_dquot = oq->qt_dquot;
			nq->qt_bcount_delta = nq->qt_icount_delta = 0;
			nq->qt_blk_res = oq->qt_blk_res - oq->qt_blk_res_used;
			oq->qt_blk_res = oq->qt_blk_res_used;
		}
		oqa = otp->t_dqinfo->dqa_prjdquots;
		nqa = ntp->t_dqinfo->dqa_prjdquots;
	}
}

/*
 * Delayed allocation purposes only XXX why???
 */
int
xfs_trans_mod_dquot_byino(
	xfs_trans_t	*tp,
	xfs_inode_t	*ip,
	uint		field,
	long		delta)
{
	ASSERT(tp);

	if (tp->t_dqinfo == NULL)
		xfs_trans_alloc_dqinfo(tp);

	if (XFS_IS_UQUOTA_ON(tp->t_mountp) && ip->i_udquot) {
		(void) xfs_trans_mod_dquot(tp, ip->i_udquot, field, delta);
	}
	if (XFS_IS_PQUOTA_ON(tp->t_mountp) && ip->i_pdquot) {
		(void) xfs_trans_mod_dquot(tp, ip->i_pdquot, field, delta);
	}
	return (0);
}	

STATIC xfs_dqtrx_t *
xfs_trans_get_dqtrx(
	xfs_trans_t	*tp,
	xfs_dquot_t	*dqp)
{
	int 		i;
	xfs_dqtrx_t 	*qa;

	for (i = 0; i < XFS_QM_TRANS_MAXDQS; i++) {
		qa = XFS_QM_DQP_TO_DQACCT(tp, dqp);

		if (qa[i].qt_dquot == NULL || 
		    qa[i].qt_dquot == dqp) {
			return (&qa[i]);
		}
	}

	return (NULL);
}

#if 0
/*
 * This can be called to find out the quota reservations of a inode in a 
 * transaction.
 * Used in debugging once upon a time, just didnt want to delete it.
 */
ulong
xfs_trans_get_quota_res(
	xfs_trans_t	*tp,
	xfs_inode_t	*ip)
{
	xfs_dqtrx_t 	*qtrx;
	
	ASSERT(tp->t_dqinfo);

	/*
	 * Both udquot and pdquot have identical reservations.
	 * We've kept them separately here; once debugging is done,
	 * oneday, I'll just make it a single field.
	 */
	if (ip->i_udquot)
		qtrx = xfs_trans_get_dqtrx(tp, ip->i_udquot);
	else if (ip->i_pdquot)
		qtrx = xfs_trans_get_dqtrx(tp, ip->i_pdquot);

	if (qtrx->qt_dquot)
		return (qtrx->qt_blk_res);
	else 
		return (0);
}
#endif

/*
 * Make the changes in the transaction structure.
 * The moral equivalent to xfs_trans_mod_sb().
 * We don't touch any fields in the dquot, so we don't care
 * if it's locked or not (most of the time it won't be).
 */
void
xfs_trans_mod_dquot(
	xfs_trans_t 	*tp,
	xfs_dquot_t 	*dqp,
	uint		field,
	long	  	delta)
{
	xfs_dqtrx_t 	*qtrx;

	ASSERT(tp);
	qtrx = NULL;

	if (tp->t_dqinfo == NULL)
		xfs_trans_alloc_dqinfo(tp);
	/*
	 * Find either the first free slot or the slot that belongs
	 * to this dquot.
	 */
	qtrx = xfs_trans_get_dqtrx(tp, dqp);
	ASSERT(qtrx);
	if (qtrx->qt_dquot == NULL)
		qtrx->qt_dquot = dqp;

	switch (field) {

		/*
		 * regular disk blk reservation 
		 */
	      case XFS_TRANS_DQ_RES_BLKS:
		qtrx->qt_blk_res += (ulong)delta;
		break;

		/* 
		 * disk blocks used.
		 */
	      case XFS_TRANS_DQ_BCOUNT:
		if (qtrx->qt_blk_res && delta > 0) {
			qtrx->qt_blk_res_used += (ulong)delta;
			ASSERT(qtrx->qt_blk_res >= qtrx->qt_blk_res_used);
		}
		qtrx->qt_bcount_delta += delta;
		break;
	      
	      case XFS_TRANS_DQ_DELBCOUNT:
		qtrx->qt_delbcnt_delta += delta;
		break;

		/* 
		 * Inode Count
		 */
	      case XFS_TRANS_DQ_ICOUNT:
		qtrx->qt_icount_delta += delta;
		break;

		/*
		 * rtblk reservation 
		 */
	      case XFS_TRANS_DQ_RES_RTBLKS:
		qtrx->qt_rtblk_res += (ulong)delta;
		/* ASSERT(qtrx->qt_rtblk_res >= 0); */
		break;

		/* 
		 * rtblk count 
		 */	
	      case XFS_TRANS_DQ_RTBCOUNT:
		if (qtrx->qt_rtblk_res && delta > 0) {
			qtrx->qt_rtblk_res_used += (ulong)delta;
			ASSERT(qtrx->qt_rtblk_res >= qtrx->qt_rtblk_res_used);
		}
		qtrx->qt_rtbcount_delta += delta;
		break;
		  
	      case XFS_TRANS_DQ_DELRTBCOUNT:
		qtrx->qt_delrtb_delta += delta;
		break;

	      default:
		ASSERT(0);
	}
	tp->t_flags |= (XFS_TRANS_DIRTY | XFS_TRANS_DQ_DIRTY);
}


/*
 * Given an array of dqtrx structures, lock all the dquots associated
 * and join them to the transaction, provided they have been modified.
 * We know that the highest number of dquots (of one type - usr OR prj),
 * involved in a transaction is 2 and that both usr and prj combined - 3.
 * So, we don't attempt to make this very generic.
 */
STATIC void
xfs_trans_dqlockedjoin(
	xfs_trans_t  	*tp,
	xfs_dqtrx_t  	*q)
{
	ASSERT(q[0].qt_dquot != NULL);
	if (q[1].qt_dquot == NULL) {
		xfs_dqlock(q[0].qt_dquot);
		xfs_trans_dqjoin(tp, q[0].qt_dquot);
	} else {
		ASSERT(XFS_QM_TRANS_MAXDQS == 2);
		xfs_dqlock2(q[0].qt_dquot, q[1].qt_dquot);
		xfs_trans_dqjoin(tp, q[0].qt_dquot);
		xfs_trans_dqjoin(tp, q[1].qt_dquot);
	}
}



/*
 * Called by xfs_trans_commit() and similar in spirit to 
 * xfs_trans_apply_sb_deltas().
 * Go thru all the dquots belonging to this transaction and modify the
 * INCORE dquot to reflect the actual usages.
 * Unreserve just the reservations done by this transaction
 * dquot is still left locked at exit.
 */
void
xfs_trans_apply_dquot_deltas(
	xfs_trans_t 		*tp)
{
	int 			i, j;
	xfs_dquot_t 		*dqp;
	xfs_dqtrx_t 		*qtrx, *qa;
	xfs_disk_dquot_t	*d;
	long			totalbdelta;

	ASSERT(tp->t_dqinfo);

	qa = tp->t_dqinfo->dqa_usrdquots;
	for (j = 0; j < 2; j++) {
		if (qa[0].qt_dquot == NULL) {
			qa = tp->t_dqinfo->dqa_prjdquots;
			continue;
		}

		/*
		 * Lock all of the dquots and join them to the transaction.
		 */
		xfs_trans_dqlockedjoin(tp, qa);

		for (i = 0; i < XFS_QM_TRANS_MAXDQS; i++) {
			qtrx = &qa[i];
			/*
			 * The array of dquots is filled
			 * sequentially, not sparsely.
			 */
			if ((dqp = qtrx->qt_dquot) == NULL)
				break;

			ASSERT(XFS_DQ_IS_LOCKED(dqp));
			ASSERT(XFS_DQ_IS_ADDEDTO_TRX(tp, dqp));
			
			/* 
			 * adjust the actual number of blocks used 
			 */
			d = &dqp->q_core;
			
			/*
			 * The issue here is - sometimes we don't make a blkquota
			 * reservation intentionally to be fair to users 
			 * (when the amount is small). On the other hand,
			 * delayed allocs do make reservations, but that's
			 * outside of a transaction, so we have no 
			 * idea how much was really reserved.
			 * So, here we've accumulated delayed allocation blks and
			 * non-delay blks. The assumption is that the 
			 * delayed ones are always reserved (outside of a 
			 * transaction), and the others may or may not have
			 * quota reservations.
			 */
			totalbdelta = qtrx->qt_bcount_delta + 	
				qtrx->qt_delbcnt_delta;
			
#ifdef QUOTADEBUG
			if (totalbdelta < 0)
				ASSERT(d->d_bcount >= 
				       (xfs_qcnt_t) -totalbdelta);

			if (qtrx->qt_icount_delta < 0)
				ASSERT(d->d_icount >= 
				       (xfs_qcnt_t) -qtrx->qt_icount_delta);
#endif
			d->d_bcount += (xfs_qcnt_t)totalbdelta;
			d->d_icount += (xfs_qcnt_t)qtrx->qt_icount_delta;
			if (qtrx->qt_rtbcount_delta)
				d->d_bcount += 
					(xfs_qcnt_t)qtrx->qt_rtbcount_delta;	
	
			/*
			 * Start/reset the timer(s) if needed.
			 */
			/* if (XFS_IS_QUOTA_ENFORCED(tp->t_mountp)) */
			xfs_qm_adjust_dqtimers(tp->t_mountp, d);

			dqp->dq_flags |= XFS_DQ_DIRTY;
			/* 
			 * add this to the list of items to get logged 
			 */
			xfs_trans_log_dquot(tp, dqp);
		
			/* 
			 * Take off what's left of the original reservation.
			 * In case of delayed allocations, there's no 
			 * reservation that a transaction structure knows of.
			 */
			if (qtrx->qt_blk_res != 0) {
				if (qtrx->qt_blk_res != qtrx->qt_blk_res_used) {
					if (qtrx->qt_blk_res > 
					    qtrx->qt_blk_res_used)
						dqp->q_res_bcount -= (xfs_qcnt_t) 
							(qtrx->qt_blk_res - 
							 qtrx->qt_blk_res_used);
					else
						dqp->q_res_bcount -= (xfs_qcnt_t) 
							(qtrx->qt_blk_res_used - 
							 qtrx->qt_blk_res);
				}
			} else {
				/*
				 * These blks were never reserved, either inside
				 * a transaction or outside one (in a delayed
				 * allocation). Also, this isn't always a 
				 * negative number since we sometimes
				 * deliberately skip quota reservations.
				 */
				if (qtrx->qt_bcount_delta) {
					dqp->q_res_bcount += 
					      (xfs_qcnt_t)qtrx->qt_bcount_delta;
				}
				
				if (qtrx->qt_rtbcount_delta)
					dqp->q_res_rtbcount += 
					     (xfs_qcnt_t)qtrx->qt_rtbcount_delta;
			}

			if (qtrx->qt_rtblk_res && 
			    qtrx->qt_blk_res != qtrx->qt_blk_res_used) {
				if (qtrx->qt_rtblk_res > qtrx->qt_rtblk_res_used)
					dqp->q_res_rtbcount -= (xfs_qcnt_t) 
						(qtrx->qt_rtblk_res - 
						 qtrx->qt_rtblk_res_used);
				else
					dqp->q_res_rtbcount -= (xfs_qcnt_t) 
						(qtrx->qt_rtblk_res_used - 
						 qtrx->qt_rtblk_res);
	
			}
			
			
#ifdef QUOTADEBUG

			if (qtrx->qt_rtblk_res != 0)
				printf("RT res %d for 0x%x\n",
				      (int) qtrx->qt_rtblk_res,
				      dqp);
#endif

			ASSERT(dqp->q_res_bcount >= dqp->q_core.d_bcount);
			ASSERT(dqp->q_res_rtbcount >= dqp->q_core.d_rtbcount);
		}

		/*
		 * Do the project quotas next
		 */
		qa = tp->t_dqinfo->dqa_prjdquots;
	}
}

/*
 * Release the reservations, and adjust the dquots accordingly. 
 * This is called only when the transaction is being aborted. If by
 * any chance we have done dquot modifications incore (ie. deltas) already,
 * we simply throw those away, since that's the expected behavior
 * when a transaction is curtailed without a commit.
 */
void
xfs_trans_unreserve_and_mod_dquots(
	xfs_trans_t	*tp)
{
	int 			i, j;
	xfs_dquot_t 		*dqp;
	xfs_dqtrx_t 		*qtrx, *qa;

	ASSERT(tp->t_dqinfo);
	qa = tp->t_dqinfo->dqa_usrdquots;
	for (j = 0; j < 2; j++) {
		for (i = 0; i < XFS_QM_TRANS_MAXDQS; i++) {
			qtrx = &qa[i];
			/*
			 * We assume that the array of dquots is filled
			 * sequentially, not sparsely.
			 */
			if ((dqp = qtrx->qt_dquot) == NULL)
				break;
			/*
			 * Unreserve the original reservation. We don't care
			 * about the number of blocks used field, or deltas.
			 *
			 * We didn't reserve any inodes so, nothing to do there.
			 * Also we don't bother to zero the fields.
			 */
			if (qtrx->qt_blk_res) {
#ifdef QUOTADEBUG
				printf("############# TRANS_CANCEL: unresv: "
				     "dqp 0x%x (\'%d\'), nblks = %d ##########\n", 
				       dqp, dqp->q_core.d_id, 
				       qtrx->qt_blk_res);
#endif
				xfs_dqlock(dqp);
				dqp->q_res_bcount -= (xfs_qcnt_t)qtrx->qt_blk_res;
				if (qtrx->qt_blk_res)
					dqp->q_res_bcount -= 
						(xfs_qcnt_t)qtrx->qt_blk_res; 
				xfs_dqunlock(dqp);
			}
			if (qtrx->qt_rtblk_res) {
#ifdef QUOTADEBUG
				printf("############# TRANS_CANCEL: unresv: "
				  "dqp 0x%x (\'%d\'), RT nblks = %d ##########\n", 
				       dqp, dqp->q_core.d_id, 
				       qtrx->qt_rtblk_res);
#endif
				xfs_dqlock(dqp);
				dqp->q_res_rtbcount -= 
					(xfs_qcnt_t)qtrx->qt_rtblk_res;
				if (qtrx->qt_rtblk_res)
					dqp->q_res_rtbcount -= 
						(xfs_qcnt_t)qtrx->qt_rtblk_res; 
				xfs_dqunlock(dqp);
			}
		}
		qa = tp->t_dqinfo->dqa_prjdquots;
	}
}


STATIC int
xfs_trans_dqresv(
	xfs_trans_t	*tp,
	xfs_dquot_t 	*dqp,
	long		nblks,
	xfs_qcnt_t	hardlimit,
	xfs_qcnt_t	softlimit,
	time_t		btimer,
	xfs_qcnt_t	*resbcount,
	uint		flags)
{
	int 		error;
	
	if (! (flags & XFS_QMOPT_DQLOCK)) {
		xfs_dqlock(dqp);
	} 
	
	ASSERT(XFS_DQ_IS_LOCKED(dqp));
	error = 0;

	if (nblks > 0 && 
	    (flags & XFS_QMOPT_FORCE_RES) == 0 &&
	    dqp->q_core.d_id != 0 && 
	    XFS_IS_QUOTA_ENFORCED(dqp->q_mount)) {
		/*
		 * dquot is locked already. See if we'd go over the hardlimit or
		 * exceed the timelimit if we allocate nblks
		 */
		if ((hardlimit > 0ULL &&
		     (hardlimit < nblks + *resbcount)) ||
		    (softlimit > 0ULL &&
		     (softlimit < nblks + *resbcount))) {

			/*
			 * If timer or warnings has expired, return EDQUOT
			 */
			if ((btimer != 0 && time > btimer) ||
			    (dqp->q_core.d_bwarns != 0 && 
			     dqp->q_core.d_bwarns >= 
			     dqp->q_mount->m_quotainfo->qi_iwarnlimit)) {
				/* can't do it */
				error = EDQUOT;
				goto error_return;
			}
		}
	}
 
        /*
	 * Change the reservation, but not the actual usage. 
	 * Note that q_res_bcount = q_core.d_bcount + resv
	 */
	(*resbcount) += (xfs_qcnt_t)nblks;

	/*
	 * note the reservation amt in the trans struct too,
	 * so that the transaction knows how much was reserved by
	 * it against this particular dquot.
	 * We don't do this when we are reserving for a delayed allocation,
	 * because we don't have the luxury of a transaction envelope then.
	 */
	if (tp) {
		ASSERT(tp->t_dqinfo);
		ASSERT(flags & XFS_QMOPT_RESBLK_MASK);
		xfs_trans_mod_dquot(tp, dqp, 
				    flags & XFS_QMOPT_RESBLK_MASK, 
				    nblks);
	} 
	ASSERT(dqp->q_res_bcount >= dqp->q_core.d_bcount);
	ASSERT(dqp->q_res_rtbcount >= dqp->q_core.d_rtbcount);

error_return:
	if (! (flags & XFS_QMOPT_DQLOCK)) {
		xfs_dqunlock(dqp);
	}
	return (error);
}


/*
 * Given a dquot(s), make disk block reservations against them.
 * The fact that this does the reservation against both the usr and
 * prj quotas is important, because this follows a both-or-nothing
 * approach.
 * 
 * flags = XFS_QMOPT_DQLOCK indicate if dquot(s) need to be locked.
 *         XFS_QMOPT_FORCE_RES evades limit enforcement. Used by chown.
 * 	   XFS_TRANS_DQ_RES_BLKS reserves regular disk blocks
 * 	   XFS_TRANS_DQ_RES_RTBLKS reserves realtime disk blocks
 * dquots are unlocked on return, if they were not locked by caller.
 */

int
xfs_trans_reserve_quota_bydquots(
	xfs_trans_t 	*tp,
	xfs_dquot_t 	*udqp,
	xfs_dquot_t 	*pdqp,	
	long		nblks,
	uint		flags)
{
	int 		resvd;
	xfs_qcnt_t	hlim, slim;
	timer_t		timer;
	xfs_qcnt_t	*resbcntp;

	if (tp && tp->t_dqinfo == NULL)
		xfs_trans_alloc_dqinfo(tp);

	ASSERT(flags & XFS_QMOPT_RESBLK_MASK);
	resvd = 0;
	if (udqp) {
		XFS_QM_INIT_DQARGS(flags, udqp, hlim, slim, timer, resbcntp);
		if (xfs_trans_dqresv(tp, udqp, nblks, hlim, slim,
				     timer, resbcntp, flags))
			return (EDQUOT);
			
		resvd = 1;
	}
	
	if (pdqp) {
		XFS_QM_INIT_DQARGS(flags, pdqp, hlim, slim, timer, resbcntp);	
		if (xfs_trans_dqresv(tp, pdqp, nblks, hlim, slim, timer,
				     resbcntp, flags)) {
			/* 
			 * can't do it, so backout previous reservation
			 */
			if (resvd) {
				XFS_QM_INIT_DQRESARGS(flags, udqp, resbcntp);
				xfs_trans_dqresv(tp, udqp,  -nblks, hlim, slim,
						 timer, resbcntp, flags);
			}
			return (EDQUOT);
		}
	}

	/* 
	 * Didnt change anything critical, so, no need to log
	 */
	return (0);
}


/*
 * Lock the dquot and change the reservation if we can.
 * This doesnt change the actual usage, just the reservation.
 * 
 * IN: inode, locked EXCL
 *
 * Returns 0 on success, EDQUOT or other errors otherwise
 */
int
xfs_trans_reserve_quota(
	xfs_trans_t 	*tp,
	xfs_inode_t 	*ip,
	long		nblks,
	uint		type)
{
	int error;

#ifdef QUOTADEBUG
	if (ip->i_udquot)
		ASSERT(! XFS_DQ_IS_LOCKED(ip->i_udquot));
	if (ip->i_pdquot)
		ASSERT(! XFS_DQ_IS_LOCKED(ip->i_pdquot));
#endif

	ASSERT(XFS_ISLOCKED_INODE_EXCL(ip));
	ASSERT(XFS_IS_QUOTA_RUNNING(ip->i_mount));
	ASSERT(type == XFS_TRANS_DQ_RES_RTBLKS ||
	       type == XFS_TRANS_DQ_RES_BLKS);
	/*
	 * Find the dquots concerned, lock them and attach them to inode.
	 * Typically, the dquots are already attached at this point, and
	 * we just lock them here. That is because we don't want to go
	 * galvanting while holding the inode lock.
	 */
	/*
	if (error = xfs_qm_dqattach(ip, XFS_QMOPT_DQLOCK|XFS_QMOPT_ILOCKED))
		return (error);
	*/
	/*if (XFS_IS_UQUOTA_ON(ip->i_mount)) {
		ASSERT(ip->i_udquot);
		xfs_dqlock(ip->i_udquot);
	}
	if (XFS_IS_PQUOTA_ON(ip->i_mount)) {
		ASSERT(ip->i_pdquot);
		xfs_dqlock(ip->i_pdquot);
	}*/

	/*
	 * Reserve nblks against these dquots, with trans as the mediator.
	 */
	error = xfs_trans_reserve_quota_bydquots(tp, 
						 ip->i_udquot, ip->i_pdquot,
						 nblks,
						 type);
	/* if (ip->i_pdquot)
		xfs_dqunlock(ip->i_pdquot);
	if (ip->i_udquot)
		xfs_dqunlock(ip->i_udquot); */

	return (error);
	
}

/*
 * Just a wrapper around xfs_qm_check_inoquota()
 */
int
xfs_qm_check_inoquota2(
	xfs_mount_t 	*mp,
	xfs_dquot_t 	*udqp,
	xfs_dquot_t 	*pdqp)
{
	int	error;

	error = 0;
	if (XFS_IS_QUOTA_ENFORCED(mp)) {
		if (udqp) {
			xfs_dqlock(udqp);
			error = xfs_qm_check_inoquota(udqp);
			xfs_dqunlock(udqp);
			if (error)
				return (error);
		}
		if (pdqp) {
			xfs_dqlock(pdqp);
			error = xfs_qm_check_inoquota(pdqp);
			xfs_dqunlock(pdqp);
		}
	}
	return (error);
}

/*
 * Returns EDQUOT if creating one more inode will exceed the hardlimit
 * or if the inode quota timer has already expired. It does NOT change
 * the dquot or associated data structures.
 */
int
xfs_qm_check_inoquota(
	xfs_dquot_t 	*dqp)
{
	ASSERT(dqp);
	ASSERT(XFS_DQ_IS_LOCKED(dqp));

	/* XXX check for warnings too */
	if (XFS_IS_QUOTA_ENFORCED(dqp->q_mount)) {
		/*
		 * dquot is locked already. See if we'd go over the hardlimit or
		 * if the timer has already expired.
		 */
		if (dqp->q_core.d_icount >= dqp->q_core.d_ino_hardlimit &&
		    dqp->q_core.d_ino_hardlimit > 0ULL) {
			return (EDQUOT);
		} else if (dqp->q_core.d_icount >= 
			   dqp->q_core.d_ino_softlimit &&
			   dqp->q_core.d_ino_softlimit > 0ULL) {
			
			/*
			 * If timer or warnings has expired, return EDQUOT
			 */
			if ((dqp->q_core.d_itimer != 0 &&
			    time > dqp->q_core.d_itimer) || 
			    (dqp->q_core.d_iwarns != 0 &&
			     dqp->q_core.d_iwarns >= 
			     dqp->q_mount->m_quotainfo->qi_iwarnlimit)) {
				return (EDQUOT);
			}
			
		}
	}
	return (0);
}

/*
 * This routine is called to allocate a quotaoff log item. 
 */
xfs_qoff_logitem_t *
xfs_trans_get_qoff_item(
	xfs_trans_t		*tp,
	xfs_qoff_logitem_t 	*startqoff,
	uint			flags)
{
	xfs_qoff_logitem_t	*q;

	ASSERT(tp != NULL);

	q = xfs_qm_qoff_logitem_init(tp->t_mountp, startqoff, flags);
	ASSERT(q != NULL);

	/*
	 * Get a log_item_desc to point at the new item.
	 */
	(void) xfs_trans_add_item(tp, (xfs_log_item_t*)q);

	return (q);
}


/*
 * This is called to mark the quotaoff logitem as needing
 * to be logged when the transaction is committed.  The logitem must
 * already be associated with the given transaction.
 */
void
xfs_trans_log_quotaoff_item(
        xfs_trans_t     	*tp,
	xfs_qoff_logitem_t	*qlp)
{
        xfs_log_item_desc_t     *lidp;

        lidp = xfs_trans_find_item(tp, (xfs_log_item_t *)qlp);
        ASSERT(lidp != NULL);

        tp->t_flags |= XFS_TRANS_DIRTY;
        lidp->lid_flags |= XFS_LID_DIRTY;
}

void
xfs_trans_alloc_dqinfo(
	xfs_trans_t	*tp)
{
	(tp)->t_dqinfo = kmem_zone_zalloc(G_xqm->qm_dqtrxzone, KM_SLEEP);
}

void
xfs_trans_free_dqinfo(
	xfs_trans_t	*tp)
{
	kmem_zone_free(G_xqm->qm_dqtrxzone, (tp)->t_dqinfo);
	(tp)->t_dqinfo = NULL;
}
