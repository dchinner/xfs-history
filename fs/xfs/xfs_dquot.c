#ident "$Revision: 1.6 $"
#include <sys/param.h>
#include <sys/sysinfo.h>
#include <sys/buf.h>
#include <sys/ksa.h>
#include <sys/vnode.h>
#include <sys/uuid.h>
#include <sys/atomic_ops.h>
#include <sys/kmem.h>
#include <sys/debug.h>
#include <sys/proc.h>
#include <sys/cmn_err.h>
#include <sys/atomic_ops.h>
#include <sys/idbg.h>
#include <sys/idbgentry.h>
#include <sys/errno.h>
#include <sys/systm.h>
#include <sys/ktrace.h>

#include "xfs_macros.h"
#include "xfs_types.h"
#include "xfs_inum.h"
#include "xfs_log.h"
#include "xfs_trans.h"
#include "xfs_sb.h"
#include "xfs_ag.h"
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
#include "xfs_error.h"
#include "xfs_trans_priv.h"
#include "xfs_buf_item.h"
#include "xfs_itable.h"
#include "xfs_bit.h"
#include "xfs_quota.h"
#include "xfs_dqblk.h"
#include "xfs_dquot.h"
#include "xfs_qm.h"
#include "xfs_quota_priv.h"


/*
   LOCK ORDER
   
   inode lock		    (ilock)
   dquot hash-chain lock    (hashlock)
   xqm dquot freelist lock  (freelistlock
   mount's dquot list lock  (mplistlock)
   user dquot lock - lock ordering among dquots is based on the uid or prid
   proj dquot lock - similar to udquots. Between the two dquots, the udquot 
                     has to be locked first.
   pin lock - the dquot lock must be held to take this lock. 
   flush lock - ditto. 
*/

STATIC int		xfs_qm_dqlookup(xfs_mount_t *, xfs_dqid_t,
					xfs_dqhash_t *,	xfs_dquot_t **);
STATIC int		xfs_qm_idtodq(xfs_mount_t *, xfs_dqid_t,
				      uint, uint, xfs_dquot_t **);
STATIC void 		xfs_qm_dqflush_done(buf_t *, xfs_dq_logitem_t *);
STATIC int		xfs_qm_dqtobp(xfs_trans_t *, xfs_dquot_t *,
				      xfs_disk_dquot_t **, buf_t **, uint);
STATIC void		xfs_qm_init_dquot_blk(xfs_trans_t *, xfs_mount_t *,
					      xfs_dqid_t, uint, buf_t *);
STATIC void		xfs_qm_dqinit_core(xfs_dqid_t, uint, xfs_dqblk_t *);
STATIC int		xfs_qm_dqalloc(xfs_trans_t*, xfs_mount_t *,
				       xfs_dquot_t*, xfs_inode_t *,
				       xfs_fileoff_t, buf_t **);
STATIC int		xfs_qm_dqread(xfs_trans_t*, xfs_dqid_t, xfs_dquot_t *,
				      uint);


/*
 * Allocate and initialize a dquot. We don't always allocate fresh memory;
 * we try to reclaim a free dquot if the number of incore dquots are above
 * a threshold.
 * The only field inside the core that gets initialized at this point
 * is the d_id field. The idea is to fill in the entire q_core
 * when we read in the on disk dquot.
 */
xfs_dquot_t *
xfs_qm_dqinit(
	xfs_mount_t  *mp,
	xfs_dqid_t   id,
	uint	     type)
{
	xfs_dquot_t 	*dqp;
	boolean_t	brandnewdquot;

	brandnewdquot = xfs_qm_dqalloc_incore(&dqp);
	dqp->dq_flags = type;
	dqp->q_core.d_id = id;
	dqp->q_mount = mp;
	dqp->q_dev = mp->m_dev;
	
	/*
	 * No need to re-initialize these if this is a reclaimed dquot.
	 */
	if (brandnewdquot) {
		dqp->dq_flnext = dqp->dq_flprev = dqp;
		mutex_init(&dqp->q_qlock, MUTEX_DEFAULT, "xdq"); 
		initnsema(&dqp->q_flock, 1, "fdq");
		sv_init(&dqp->q_pinwait, SV_DEFAULT, "pdq");

#ifdef DQUOT_TRACING
		dqp->q_trace = ktrace_alloc(DQUOT_TRACE_SIZE, 0);
		xfs_dqtrace_entry(dqp, "DQINIT");
#endif	
	} else {
		/*
		 * Only the q_core portion was bzeroed in dqreclaim_one().
		 * So, we need to reset others.
		 */
		 dqp->q_nrefs = 0;
		 dqp->q_blkno = 0;
		 dqp->MPL_NEXT = dqp->HL_NEXT = NULL;
		 dqp->HL_PREVP = dqp->MPL_PREVP = NULL;
		 dqp->q_bufoffset = 0;
		 dqp->q_fileoffset = 0;
		 dqp->q_transp = NULL;
		 dqp->q_pdquot = NULL;
		 dqp->q_res_bcount = 0;
		 dqp->q_res_icount = 0;
		 dqp->q_res_rtbcount = 0;
		 dqp->q_pincount = 0;
		 dqp->q_hash = 0;
		 ASSERT(dqp->dq_flnext == dqp->dq_flprev);
		 
#ifdef DQUOT_TRACING
		 ASSERT(dqp->q_trace);
		 xfs_dqtrace_entry(dqp, "DQRECLAIMED_INIT");
#endif	
	 }

	/*
	 * log item gets initialized later
	 */
	return (dqp);
}

/* 
 * This is called to free all the memory associated with a dquot
 */
void
xfs_qm_dqdestroy(
	xfs_dquot_t 	*dqp)
{
	ASSERT(! XFS_DQ_IS_ON_FREELIST(dqp));

	mutex_destroy(&dqp->q_qlock);
	freesema(&dqp->q_flock);
	sv_destroy(&dqp->q_pinwait);

#ifdef DQUOT_TRACING
	if (dqp->q_trace)
	     ktrace_free(dqp->q_trace);
	dqp->q_trace = NULL;
#endif
	kmem_zone_free(G_xqm->qm_dqzone, dqp);
	atomicAddUint(&G_xqm->qm_totaldquots, -1);
}

/*
 * This is what a 'fresh' dquot inside a dquot chunk looks like on disk.
 */
STATIC void
xfs_qm_dqinit_core(
	xfs_dqid_t	 id,
	uint		 type,
	xfs_dqblk_t 	 *d)
{
	/*
	 * Caller has bzero'd the entire dquot 'chunk' already.
	 */
	d->dd_diskdq.d_magic = XFS_DQUOT_MAGIC;
	d->dd_diskdq.d_version = XFS_DQUOT_VERSION;
	d->dd_diskdq.d_id = id;
	d->dd_diskdq.d_flags = type;
}


#ifdef DQUOT_TRACING
/*
 * Dquot tracing for debugging.
 */
/* ARGSUSED */
void
xfs_dqtrace_entry(
	xfs_dquot_t *dqp, 
	char *func)
{
	extern time_t	time;

	ASSERT(dqp->q_trace);
        ktrace_enter(dqp->q_trace, 
		     (void *)(__psint_t)DQUOT_KTRACE_ENTRY, 
		     (void *)func,
		     (void *)(__psint_t)dqp->q_nrefs,
		     (void *)(__psint_t)dqp->dq_flags, 
		     (void *)(__psint_t)dqp->q_res_bcount,
		     (void *)(__psint_t)dqp->q_core.d_bcount,
		     (void *)(__psint_t)dqp->q_core.d_icount,
		     (void *)(__psint_t)dqp->q_core.d_blk_hardlimit,
		     (void *)(__psint_t)dqp->q_core.d_blk_softlimit, 
		     (void *)(__psint_t)dqp->q_core.d_ino_hardlimit,
		     (void *)(__psint_t)dqp->q_core.d_ino_softlimit,
		     (void *)(__psint_t)dqp->q_core.d_id, /* 11 */
		     (void *)(__psint_t)curprocp->p_pid,
		     (void *)(__psint_t)time,
		     0, 0);
	return;
}
#endif


/*
 * Check the limits and timers of a dquot and start or reset timers
 * if necessary.
 * This gets called even when quota enforcement is OFF, which makes our
 * life a little less complicated. (We just don't reject any quota
 * reservations in that case, when enforcement is off). 
 * We also return 0 as the values of the timers in Q_GETQUOTA calls, when
 * enforcement's off.
 * In contrast, warnings are a little different in that they don't 
 * 'automatically' get started when limits get exceeded. 
 */
void
xfs_qm_adjust_dqtimers(
	xfs_mount_t		*mp,
	xfs_disk_dquot_t	*d)
{
	/*
	 * The dquot had better be locked. We are modifying it here.
	 */

	/* 
	 * root's limits are not real limits.
	 */
	if (d->d_id == 0)
		return;

#ifdef QUOTADEBUG
	if (d->d_blk_hardlimit)
		ASSERT(d->d_blk_softlimit <= d->d_blk_hardlimit);
	if (d->d_ino_hardlimit)
		ASSERT(d->d_ino_softlimit <= d->d_ino_hardlimit);
#endif
	if (d->d_btimer == 0) {
		if ((d->d_blk_softlimit &&
		    (d->d_bcount >= d->d_blk_softlimit)) ||
		    (d->d_blk_hardlimit &&
		    (d->d_bcount >= d->d_blk_hardlimit))) {
#ifdef QUOTADEBUG
		printf("----@@ starting btimer: %llu >= %llu \n",
			       d->d_bcount, d->d_blk_softlimit);
#endif
			d->d_btimer = time + mp->QI_BTIMELIMIT;
		}
	} else {
		if ((d->d_blk_softlimit == 0 ||	
		    (d->d_bcount < d->d_blk_softlimit)) &&
		    (d->d_blk_hardlimit == 0 ||	
		    (d->d_bcount < d->d_blk_hardlimit))) {
#ifdef QUOTADEBUG
			printf("----@@ stopping btimer: %llu < %llu\n",
			        d->d_bcount, d->d_blk_softlimit);
#endif
			d->d_btimer = 0;
		}
	}

	if (d->d_itimer == 0) {
		if ((d->d_ino_softlimit &&
		    (d->d_icount >= d->d_ino_softlimit)) ||
		    (d->d_ino_hardlimit &&
		    (d->d_icount >= d->d_ino_hardlimit))) {
#ifdef QUOTADEBUG
			printf("----@@ starting itimer: %llu >= %llu\n",
			       d->d_icount, d->d_ino_softlimit);
#endif
			d->d_itimer = time + mp->QI_ITIMELIMIT;
		}
	} else {
		if ((d->d_ino_softlimit == 0 ||
		    (d->d_icount < d->d_ino_softlimit))  &&
		    (d->d_ino_hardlimit == 0 ||	
		    (d->d_icount < d->d_ino_hardlimit))) {
#ifdef QUOTADEBUG
			printf("----@@ stopping itimer: %llu < %llu\n",
			       d->d_icount, d->d_ino_softlimit);
#endif
			d->d_itimer = 0;
		}
	}
#ifdef QUOTADEBUG
	if (d->d_itimer)
		printf("--------@@Inode Timer running : %llu >= %llu\n", 
		       d->d_icount, d->d_ino_softlimit);
	if (d->d_btimer)
		printf("--------@@Blks Timer running : %llu >= %llu\n",
		       d->d_bcount, d->d_blk_softlimit);
#endif
	


}

/*
 * Increment or reset warnings of a given dquot.
 */
int
xfs_qm_dqwarn(
	xfs_disk_dquot_t 	*d,
	uint			flags)
{
	int	warned;
	
	/* 
	 * root's limits are not real limits.
	 */
	if (d->d_id == 0)
		return (0);

	warned = 0;
	if (d->d_blk_softlimit &&
	    (d->d_bcount >= d->d_blk_softlimit)) {
		if (flags & XFS_QMOPT_DOWARN) {
			d->d_bwarns++; 
			warned++;
		}
	} else {
		if (d->d_blk_softlimit == 0 ||	
		    (d->d_bcount < d->d_blk_softlimit)) {
			d->d_bwarns = 0;
		}
	}

	if (d->d_ino_softlimit > 0 &&
	    (d->d_icount >= d->d_ino_softlimit)) {
		if (flags & XFS_QMOPT_DOWARN) {
			d->d_iwarns++;
			warned++;
		}
	} else {
		if ((d->d_ino_softlimit == 0) ||
		    (d->d_icount < d->d_ino_softlimit)) {
			d->d_iwarns = 0;
		}
	}
#ifdef QUOTADEBUG
	if (d->d_iwarns)
		printf("--------@@Inode warnings running : %llu >= %llu\n", 
		       d->d_icount, d->d_ino_softlimit);
	if (d->d_bwarns)
		printf("--------@@Blks warnings running : %llu >= %llu\n",
		       d->d_bcount, d->d_blk_softlimit);
#endif
	return (warned);
}


/*
 * initialize a buffer full of dquots and log the whole thing
 */
STATIC void
xfs_qm_init_dquot_blk(
	xfs_trans_t	*tp,
	xfs_mount_t	*mp,
	xfs_dqid_t	id,
	uint		type,
	buf_t		*bp)
{
	xfs_dqblk_t	*d;
	int		curid, i;

	ASSERT(tp);
	ASSERT(bp->b_flags & B_BUSY);
	ASSERT(valusema(&bp->b_lock) <= 0);

	d = (xfs_dqblk_t *)bp->b_un.b_addr;

	/* 
	 * ID of the first dquot in the block - id's are zero based.
	 */
	curid = id - (id % XFS_QM_DQPERBLK(mp));
	ASSERT(curid >= 0);
	bzero(d, BBTOB(mp->QI_DQCHUNKLEN));
	for (i = 0; i < XFS_QM_DQPERBLK(mp); i++, d++, curid++)
		xfs_qm_dqinit_core(curid, type, d);
	xfs_trans_dquot_buf(tp, bp, 
			    type & XFS_DQ_USER ?
			    XFS_BLI_UDQUOT_BUF :
			    XFS_BLI_PDQUOT_BUF);
	xfs_trans_log_buf(tp, bp, 0, BBTOB(mp->QI_DQCHUNKLEN) - 1);

}



/*
 * Allocate a block and fill it with dquots.
 * This is called when the bmapi finds a hole.
 */
STATIC int
xfs_qm_dqalloc(
	xfs_trans_t	*tp, 
	xfs_mount_t	*mp,
	xfs_dquot_t	*dqp,
	xfs_inode_t 	*quotip,
	xfs_fileoff_t   offset_fsb,
	buf_t		**O_bpp)
{
	xfs_fsblock_t   firstblock;
	xfs_bmap_free_t flist;
	xfs_bmbt_irec_t map;
	int		nmaps, error, committed;
	buf_t		*bp;

	ASSERT(tp != NULL);
	xfs_dqtrace_entry(dqp, "DQALLOC");

	/*
         * Initialize the bmap freelist prior to calling bmapi code.
         */
	XFS_BMAP_INIT(&flist, &firstblock);
	xfs_ilock(quotip, XFS_ILOCK_EXCL);
	/*
	 * Return if this type of quotas is turned off while we didn't
	 * have an inode lock
	 */
	if (XFS_IS_THIS_QUOTA_OFF(dqp)) {
		xfs_iunlock(quotip, XFS_ILOCK_EXCL);
		return (ESRCH);
	}

	/*
         * xfs_trans_commit normally decrements the vnode ref count
         * when it unlocks the inode. Since we want to keep the quota
         * inode around, we bump the vnode ref count now.
         */
        VN_HOLD(XFS_ITOV(quotip));

	xfs_trans_ijoin(tp, quotip, XFS_ILOCK_EXCL);
	nmaps = 1;
	if (error = xfs_bmapi(tp, quotip, 
			      offset_fsb, XFS_DQUOT_CLUSTER_SIZE_FSB,
			      XFS_BMAPI_METADATA | XFS_BMAPI_WRITE,
			      &firstblock, 
			      1, /* only one block */
			      &map, &nmaps, &flist)) {
		goto error0;
	}
	ASSERT(map.br_blockcount == XFS_DQUOT_CLUSTER_SIZE_FSB);
	ASSERT(nmaps == 1);
	ASSERT((map.br_startblock != DELAYSTARTBLOCK) &&
	       (map.br_startblock != HOLESTARTBLOCK));

	/*
	 * Keep track of the blkno to save a lookup later
	 */
	dqp->q_blkno = XFS_FSB_TO_DADDR(mp, map.br_startblock);

	/* now we can just get the buffer (there's nothing to read yet) */
	bp = xfs_trans_get_buf(tp, mp->m_dev,
			       dqp->q_blkno,
			       mp->QI_DQCHUNKLEN,
			       0);
	if (!bp || (error = geterror(bp)))
		goto error1;
	/*
	 * Make a chunk of dquots out of this buffer and log
	 * the entire thing.
	 */
	xfs_qm_init_dquot_blk(tp, mp, dqp->q_core.d_id, 
			      dqp->dq_flags & (XFS_DQ_USER|XFS_DQ_PROJ),
			      bp);

	if (error = xfs_bmap_finish(&tp, &flist, firstblock, &committed)) {
		goto error1;
	}

	*O_bpp = bp;
	return 0;	

      error1:
	xfs_bmap_cancel(&flist);
      error0:
	xfs_iunlock(quotip, XFS_ILOCK_EXCL);

	return (error);
}

/*
 * Maps a dquot to the buffer containing its on-disk version.
 * This returns a ptr to the buffer containing the on-disk dquot 
 * in the bpp param, and a ptr to the on-disk dquot within that buffer
 */
STATIC int
xfs_qm_dqtobp(
	xfs_trans_t		*tp,
	xfs_dquot_t    		*dqp,
	xfs_disk_dquot_t	**O_ddpp,
	buf_t          		**O_bpp,
	uint			doalloc)
{
	xfs_fsblock_t   firstblock;
	xfs_bmbt_irec_t map;
	int             nmaps, error;
	buf_t           *bp;
	xfs_inode_t     *quotip;
	xfs_mount_t	*mp;
	xfs_disk_dquot_t *ddq;
	xfs_dqid_t	id;
	boolean_t	newdquot;

	firstblock = NULLFSBLOCK;
	mp = dqp->q_mount;
	id = dqp->q_core.d_id;
	nmaps = 1;
	newdquot = B_FALSE;	   

	/* 
	 * If we don't know where the dquot lives, find out.
	 */
	if (dqp->q_blkno == (daddr_t) 0) {
		/* We use the id as an index */
		dqp->q_fileoffset = (xfs_fileoff_t) ((uint) id / 
						     XFS_QM_DQPERBLK(mp));
		nmaps = 1;
		quotip = XFS_DQ_TO_QIP(dqp);
		xfs_ilock(quotip, XFS_ILOCK_SHARED);
		/*
		 * Return if this type of quotas is turned off while we didn't
		 * have an inode lock
		 */
		if (XFS_IS_THIS_QUOTA_OFF(dqp)) {
			xfs_iunlock(quotip, XFS_ILOCK_SHARED);
			return (ESRCH);
		}
		/*
		 * Find the block map; no allocations yet
		 */
		error = xfs_bmapi(NULL, quotip, dqp->q_fileoffset,
				  XFS_DQUOT_CLUSTER_SIZE_FSB,
				  XFS_BMAPI_METADATA,
				  &firstblock, 
				  0,
				  &map, &nmaps, NULL);

		xfs_iunlock(quotip, XFS_ILOCK_SHARED);
		if (error) 
			return (error);
		ASSERT(nmaps == 1);
		ASSERT(map.br_blockcount == 1);

		/*
		 * offset of dquot in the (fixed sized) dquot chunk.
		 */
		dqp->q_bufoffset = (id % XFS_QM_DQPERBLK(mp)) * 
			sizeof(xfs_dqblk_t);
		if (map.br_startblock == HOLESTARTBLOCK) {
			/*
			 * We don't allocate unless we're asked to 
			 */
			if (!doalloc)
				return (ENOENT);

			ASSERT(tp);
			if (error = xfs_qm_dqalloc(tp, mp, dqp, quotip,
						dqp->q_fileoffset, &bp)) 
				return (error);	
			newdquot = B_TRUE;
		} else {
			/* 
			 * store the blkno etc so that we don't have to do the
			 * mapping all the time
			 */
			dqp->q_blkno = XFS_FSB_TO_DADDR(mp, map.br_startblock);
		}
	} 
	ASSERT(dqp->q_blkno != DELAYSTARTBLOCK);
	ASSERT(dqp->q_blkno != HOLESTARTBLOCK);
	
	/*
	 * Read in the buffer, unless we've just done the allocation
	 * (in which case we already have the buf).
	 */
	if (! newdquot) {
		xfs_dqtrace_entry(dqp, "DQTOBP READBUF");
		if (error = xfs_trans_read_buf(tp, mp->m_dev,
					       dqp->q_blkno,
					       mp->QI_DQCHUNKLEN,
					       0, &bp)) {
			return (error);
		}
		if (!bp || geterror(bp))
			return XFS_ERROR(EIO);
	}
	ASSERT(bp->b_flags & B_BUSY);
	ASSERT(valusema(&bp->b_lock) <= 0);

	/* 
	 * calculate the location of the dquot inside the buffer.
	 */
	ddq = (xfs_disk_dquot_t *)((char *)bp->b_un.b_addr + dqp->q_bufoffset);

	/*
	 * A simple sanity check in case we got a corrupted dquot...
	 */
	if (xfs_qm_dqcheck(ddq, id, "dqtobp")) {
		xfs_trans_brelse(tp, bp);
		return XFS_ERROR(EIO);
	}

	*O_bpp = bp;
	*O_ddpp = ddq;

	return (0);
}	   


/*
 * Read in the ondisk dquot using dqtobp() then copy it to an incore version,
 * and release the buffer immediately.
 * 
 */
STATIC int
xfs_qm_dqread(
	xfs_trans_t	*tp,
	xfs_dqid_t	id,
	xfs_dquot_t 	*dqp, 	/* dquot to get filled in */
	uint        	newdquot)
{
	xfs_disk_dquot_t *ddqp;
	buf_t		 *bp;
	int		 error;

	/* 
	 * get a pointer to the on-disk dquot and the buffer containing it
	 * dqp already knows its own type (PROJ/USER).
	 */
	xfs_dqtrace_entry(dqp, "DQREAD");
	if (error = xfs_qm_dqtobp(tp, dqp, &ddqp, &bp, newdquot)) {
		return (error);
	}

	/* copy everything from disk dquot to the incore dquot */
	bcopy(ddqp, &dqp->q_core, sizeof(xfs_disk_dquot_t));
	ASSERT(dqp->q_core.d_id == id);
	xfs_qm_dquot_logitem_init(dqp);

	/*
	 * Reservation counters are defined as reservation plus current usage
	 * to avoid having to add everytime.
	 */
	dqp->q_res_bcount = ddqp->d_bcount;
	dqp->q_res_icount = ddqp->d_icount;
	dqp->q_res_rtbcount = ddqp->d_rtbcount;

	/* Mark the buf so that this will stay incore a little longer */
	bp->b_ref = XFS_DQUOT_REF;

	/* 
	 * We got the buffer with a xfs_trans_read_buf() (in dqtobp())
	 * So we need to release with xfs_trans_brelse().
	 * The strategy here is identical to that of inodes; we lock
	 * the dquot in xfs_qm_dqget() before making it accessible to
	 * others. This is because dquots, like inodes, need a good level of
	 * concurrency, and we don't want to take locks on the entire buffers
	 * for dquot accesses.
	 */
	ASSERT(bp->b_flags & B_BUSY);
	ASSERT(valusema(&bp->b_lock) <= 0);
	xfs_trans_brelse(tp, bp);

	return (error);
}



/*
 * allocate an incore dquot from the kernel heap,
 * and fill its core with quota information kept on disk.
 * If 'doalloc' is TRUE, it'll allocate a dquot on disk
 * if it wasn't already allocated.
 */
STATIC int
xfs_qm_idtodq(
	xfs_mount_t  	*mp,
	xfs_dqid_t   	id,	 /* prid or uid, depending on type */
	uint	     	type,	 /* UDQUOT or PDQUOT */
	uint	     	doalloc, /* if dquot's not on-disk, alloc it or not */
	xfs_dquot_t  	**O_dqpp)/* OUT : incore dquot, not locked */
{
	xfs_dquot_t 	*dqp;
	int 	    	error;
	xfs_trans_t	*tp;
	int		cancelflags;

	dqp = xfs_qm_dqinit(mp, id, type);
	tp = NULL;
	if (doalloc) {
		tp = xfs_trans_alloc(mp, XFS_TRANS_QM_DQALLOC);

		/* XXXsup */
		if (error = xfs_trans_reserve(tp,
				       mp->m_bm_maxlevels[XFS_DATA_FORK] + 
					      XFS_DQUOT_CLUSTER_SIZE_FSB,
				       XFS_WRITE_LOG_RES(mp) + 
					      BBTOB(mp->QI_DQCHUNKLEN) - 1 +
					      128,
				       0,
				       XFS_TRANS_PERM_LOG_RES,
				       XFS_WRITE_LOG_COUNT)) {
			cancelflags = 0;
			goto error_return;
		}
		cancelflags = XFS_TRANS_RELEASE_LOG_RES | XFS_TRANS_ABORT;
	}
		
	/*
	 * Read it from disk; xfs_dqread() takes care of
	 * all the necessary initialization of dquot's fields (locks, etc) 
	 */
	if (error = xfs_qm_dqread(tp, id, dqp, doalloc)) {
		/*
		 * This can happen if quotas got turned off (ESRCH),
		 * or if the dquot didn't exist on disk and we ask to 
		 * allocate (ENOENT).
		 */
		xfs_dqtrace_entry(dqp, "DQREAD FAIL");
		goto error_return;
	}
	if (tp) {
		if (error = xfs_trans_commit(tp, XFS_TRANS_RELEASE_LOG_RES)) {
			xfs_qm_dqdestroy(dqp);
			*O_dqpp = NULL;
			return (error);
		}
	}
	
	*O_dqpp = dqp;
	ASSERT(! XFS_DQ_IS_LOCKED(dqp));
	return (0);

 error_return:
	ASSERT(error);
	if (tp)
		xfs_trans_cancel(tp, cancelflags);
	xfs_qm_dqdestroy(dqp);
	*O_dqpp = NULL;
	return (error);
}

/*
 * Lookup a dquot in the incore dquot hashtable. We keep two separate
 * hashtables for user and project dquots; and, these are global tables
 * inside the XQM, not per-filesystem tables.
 * The hash chain must be locked by caller, and it is left locked
 * on return. Returning dquot is locked.
 */
STATIC int
xfs_qm_dqlookup(
	xfs_mount_t		*mp,
	xfs_dqid_t		id,
	xfs_dqhash_t    	*qh,
	xfs_dquot_t		**O_dqpp)
{
	xfs_dquot_t		*dqp;
	uint			flist_locked;
	xfs_dquot_t		*d;

	ASSERT(XFS_DQ_IS_HASH_LOCKED(qh)); 

	flist_locked = B_FALSE;

	/* 
	 * Traverse the hashchain looking for a match 
	 */
	for (dqp = qh->qh_next; dqp != NULL; dqp = dqp->HL_NEXT) {
		/*
		 * We already have the hashlock. We don't need the
		 * dqlock to look at the id field of the dquot, since the
		 * id can't be modified without the hashlock anyway.
		 */ 
		if (dqp->q_core.d_id == id && dqp->q_mount == mp) {
			xfs_dqtrace_entry(dqp, "DQFOUND BY LOOKUP");
			/*
			 * All in core dquots must be on the dqlist of mp
			 */
			ASSERT(dqp->MPL_PREVP != NULL);

			xfs_dqlock(dqp);
			if (dqp->q_nrefs == 0) {
				ASSERT (XFS_DQ_IS_ON_FREELIST(dqp));
				if (! xfs_qm_freelist_lock_nowait(G_xqm)) {
					xfs_dqtrace_entry(dqp, "DQLOOKUP: WANT");
					
					/*
					 * We may have raced with dqreclaim_one()
					 * (and lost). So, flag that we don't
					 * want the dquot to be reclaimed.
					 */
					dqp->dq_flags |= XFS_DQ_WANT;
					xfs_dqunlock(dqp);
					xfs_qm_freelist_lock(G_xqm);
					xfs_dqlock(dqp);
					dqp->dq_flags &= ~(XFS_DQ_WANT);
				} 
				flist_locked = B_TRUE;
			}

			/* 
			 * id couldn't have changed; we had the hashlock all
			 * along
			 */
			ASSERT(dqp->q_core.d_id == id); 

			if (flist_locked) {
				if (dqp->q_nrefs != 0) {
					xfs_qm_freelist_unlock(G_xqm);
					flist_locked = B_FALSE;
				} else {
					/*
					 * take it off the freelist
					 */
					xfs_dqtrace_entry(dqp, 
							"DQLOOKUP: TAKEOFF FL");
					XQM_FREELIST_REMOVE(dqp);
				        /* xfs_qm_freelist_print(&(G_xqm->
							qm_dqfreelist),
							"after removal"); */
				}
			}

			/*
			 * grab a reference 
			 */
			XFS_DQHOLD(dqp);

			if (flist_locked) 
				xfs_qm_freelist_unlock(G_xqm);
			/* 
			 * move the dquot to the front of the hashchain
			 */
			ASSERT(XFS_DQ_IS_HASH_LOCKED(qh)); 
			if (dqp->HL_PREVP != &qh->qh_next) {
				xfs_dqtrace_entry(dqp,
						  "DQLOOKUP: HASH MOVETOFRONT");
				if (d = dqp->HL_NEXT)
					d->HL_PREVP = dqp->HL_PREVP;
				*(dqp->HL_PREVP) = d;
				d = qh->qh_next;
				d->HL_PREVP = &dqp->HL_NEXT;
				dqp->HL_NEXT = d;
				dqp->HL_PREVP = &qh->qh_next;
				qh->qh_next = dqp;
			}
			xfs_dqtrace_entry(dqp, "LOOKUP END");
			*O_dqpp = dqp;
			ASSERT(XFS_DQ_IS_HASH_LOCKED(qh)); 
			return (0);
		}
	}

	*O_dqpp = NULL;
	ASSERT(XFS_DQ_IS_HASH_LOCKED(qh)); 
	return (1);
}

/*
 * Given the file system, inode OR id, and type (UDQUOT/PDQUOT), return a 
 * a locked dquot, doing an allocation (if requested) as needed.
 * When both an inode and an id are given, the inode's id takes precedence.
 * That is, if the id changes while we dont hold the ilock inside this
 * function, the new dquot is returned, not necessarily the one requested
 * in the id argument.
 */
int
xfs_qm_dqget(
	xfs_mount_t  	*mp,
	xfs_inode_t	*ip,   	  /* locked inode (optional) */
	xfs_dqid_t   	id,       /* prid or uid, depending on type */
	uint	     	type,  	  /* UDQUOT or PDQUOT */
	uint	     	flags,    /* DQALLOC, DQSUSER */
	xfs_dquot_t  	**O_dqpp) /* OUT : locked incore dquot */
{
	xfs_dquot_t 	*dqp;
	xfs_dqhash_t 	*h;
	uint	      	version;
	int		error;

	ASSERT(XFS_IS_QUOTA_RUNNING(mp));
	if ((! XFS_IS_UQUOTA_ON(mp) &&
	    type == XFS_DQ_USER) ||
	    (! XFS_IS_PQUOTA_ON(mp) &&
	    type == XFS_DQ_PROJ)) {
		return (ESRCH);
	}
	h = XFS_DQ_HASH(mp, id, type);
	
 again:

#ifdef QUOTADEBUG
	ASSERT(type == XFS_DQ_USER ||
	       type == XFS_DQ_PROJ);
	if (ip) {
		ASSERT(XFS_ISLOCKED_INODE_EXCL(ip));
		if (type == XFS_DQ_USER)
			ASSERT(ip->i_udquot == NULL);
		else
			ASSERT(ip->i_pdquot == NULL);
	}
#endif
	XFS_DQ_HASH_LOCK(h);
	
	/*
	 * Look in the cache (hashtable).
	 * The chain is kept locked during lookup.
	 */
	if (xfs_qm_dqlookup(mp, id, h, O_dqpp) == 0) {
#ifndef _BANYAN_XFS
		XFSSTATS.xs_qm_dqcachehits++;
#endif
		/* 
		 * The dquot was found, moved to the front of the chain, 
		 * taken off the freelist if it was on it, and locked
		 * at this point. Just unlock the hashchain and return.
		 */
		ASSERT(*O_dqpp);
		ASSERT(XFS_DQ_IS_LOCKED(*O_dqpp)); 
		XFS_DQ_HASH_UNLOCK(h);
		xfs_dqtrace_entry(*O_dqpp, "DQGET DONE (FROM CACHE)"); 
		return (0);	/* success */
	}
#ifndef _BANYAN_XFS
	 XFSSTATS.xs_qm_dqcachemisses++;
#endif

	/* 
	 * Dquot cache miss. We don't want to keep the inode lock across 
	 * a (potential) disk read. However, dropping it here means dealing
	 * with a chown that can happen before we re-acquire the lock.
	 */
	if (ip)
		xfs_iunlock(ip, XFS_ILOCK_EXCL);
	/* 
	 * Save the hashchain version stamp, and unlock the chain, so that
	 * we don't keep the lock across a disk read
	 */
	version = h->qh_version;
	XFS_DQ_HASH_UNLOCK(h);
	
	/*
	 * allocate the dquot on the kernel heap, and read the ondisk
	 * portion off the disk. Also, do all the necessary initialization
	 * This can return ENOENT if dquot didn't exist on disk and we didn't
	 * ask it to allocate; ESRCH if quotas got turned off suddenly.
	 */
	if (error = xfs_qm_idtodq(mp, id, type, flags & XFS_QMOPT_DQALLOC,
				  &dqp)) {
		if (ip)
			xfs_ilock(ip, XFS_ILOCK_EXCL);
		return (error);
	}

	/*
	 * If this is mount code calling to look at the overall quota limits
	 * which are stored in the id == 0 user or project's dquot.
	 * Since we may not have done a quotacheck by this point, just return
	 * the dquot without attaching it to any hashtables, lists, etc, or even
	 * taking a reference.
	 * The caller must dqdestroy this once done.
	 */
	if (flags & XFS_QMOPT_DQSUSER) {
		ASSERT(id == 0);
		ASSERT(! ip);
		goto dqret;
	}
		
	/* 
	 * Dquot lock comes after hashlock in the lock ordering 
	 */
	ASSERT(! XFS_DQ_IS_LOCKED(dqp));
	if (ip) {
		xfs_ilock(ip, XFS_ILOCK_EXCL);
		if (! XFS_IS_DQTYPE_ON(mp, type)) {
			/* inode stays locked on return */
			xfs_qm_dqdestroy(dqp);
			return (ESRCH);
		}
		/*
		 * A dquot could be attached to this inode by now, since
		 * we had dropped the ilock.
		 */
		if (type == XFS_DQ_USER) {
			if (ip->i_udquot) {
				xfs_qm_dqdestroy(dqp);
				dqp = ip->i_udquot;
				xfs_dqlock(dqp);
				goto dqret;
			}
		} else {
			if (ip->i_pdquot) {
				xfs_qm_dqdestroy(dqp);
				dqp = ip->i_pdquot;
				xfs_dqlock(dqp);
				goto dqret;
			}
		}
	}
	
	
	/*
	 * Hashlock comes after ilock in lock order
	 */
	XFS_DQ_HASH_LOCK(h);
	if (version != h->qh_version) {
		xfs_dquot_t *tmpdqp;
		/*
		 * Now, see if somebody else put the dquot in the
		 * hashtable before us. This can happen because we didn't
		 * keep the hashchain lock. We don't have to worry about
		 * lock order between the two dquots here since dqp isn't
		 * on any findable lists yet.
		 */
		if (xfs_qm_dqlookup(mp, id, h, &tmpdqp) == 0) {
			/* 
			 * Duplicate found. Just throw away the new dquot
			 * and start over.
			 */
			xfs_qm_dqput(tmpdqp);
			XFS_DQ_HASH_UNLOCK(h);
			xfs_qm_dqdestroy(dqp);
#ifndef _BANYAN_XFS
			XFSSTATS.xs_qm_dquot_dups++; 
#endif
			goto again;
		}
	}
	
	/* 
	 * Put the dquot at the beginning of the hash-chain and mp's list
	 * LOCK ORDER: hashlock, freelistlock, mplistlock, udqlock, pdqlock ..
	 */
	ASSERT(XFS_DQ_IS_HASH_LOCKED(h)); 
	dqp->q_hash = h;
	XQM_HASHLIST_INSERT(h, dqp);

	/*
	 * Attach this dquot to this filesystem's list of all dquots,
	 * kept inside the mount structure in m_quotainfo field
	 */
	xfs_qm_mplist_lock(mp);
	
	/*
	 * We return a locked dquot to the caller, with a reference taken
	 */
	xfs_dqlock(dqp);
	dqp->q_nrefs = 1;
	
	XQM_MPLIST_INSERT(&(mp->QI_MPL_LIST), dqp);

	xfs_qm_mplist_unlock(mp);
	XFS_DQ_HASH_UNLOCK(h);
 dqret:		
	xfs_dqtrace_entry(dqp, "DQGET DONE");
	*O_dqpp = dqp;
	return (0);
}


/* 
 * Release a reference to the dquot (decrement ref-count)
 * and unlock it. If there is a project quota attached to this 
 * dquot, carefully release that too without tripping over 
 * deadlocks'n'stuff.
 */
void
xfs_qm_dqput(
	xfs_dquot_t	*dqp)
{
	xfs_dquot_t	*pdqp;

	ASSERT(dqp->q_nrefs > 0);
	ASSERT(XFS_DQ_IS_LOCKED(dqp));
	xfs_dqtrace_entry(dqp, "DQPUT");

	if (dqp->q_nrefs != 1) {
		dqp->q_nrefs--;
		xfs_dqunlock(dqp);			
		return;
	}

	/* 
	 * drop the dqlock and acquire the freelist and dqlock
	 * in the right order; but try to get it out-of-order first
	 */
	if (! xfs_qm_freelist_lock_nowait(G_xqm)) {
		xfs_dqtrace_entry(dqp, "DQPUT: FLLOCK-WAIT");
		xfs_dqunlock(dqp);
		xfs_qm_freelist_lock(G_xqm);
		xfs_dqlock(dqp);
	}

	while (1) {
		pdqp = NULL;
		
		/* We can't depend on nrefs being == 1 here */	
		if (--dqp->q_nrefs == 0) {
			xfs_dqtrace_entry(dqp, "DQPUT: ON FREELIST");
			/*
			 * insert at end of the freelist.
			 */
			XQM_FREELIST_INSERT(&(G_xqm->qm_dqfreelist), dqp);

			/* 
			 * If we just added a udquot to the freelist, then
			 * we want to release the pdquot reference that
			 * it (probably) has. Otherwise it'll keep the 
			 * pdquot from getting reclaimed.
			 */
			if (pdqp = dqp->q_pdquot) {
				/* 
				 * Avoid a recursive dqput call 
				 */
				xfs_dqlock(pdqp);
				dqp->q_pdquot = NULL;
			}
		
			/* xfs_qm_freelist_print(&(G_xqm->qm_dqfreelist),
			   "@@@@@++ Free list (after append) @@@@@+");
			   */
		}
		xfs_dqunlock(dqp);	
		
		/*
		 * If we had a proj quota inside the user quota as a hint, 
		 * release it now.
		 */
		if (! pdqp)
			break;
		
		dqp = pdqp;
		
	}
	xfs_qm_freelist_unlock(G_xqm);
}

/*
 * Release a dquot. Flush it if dirty, then dqput() it.
 * dquot must not be locked.
 */
void
xfs_qm_dqrele(
	xfs_dquot_t   	*dqp)
{
	ASSERT(dqp);
	ASSERT(! XFS_DQ_IS_LOCKED(dqp));
	xfs_dqtrace_entry(dqp, "DQRELE");

	xfs_dqlock(dqp);
	/* 
	 * We don't care to flush it if the dquot is dirty here.
	 * That will create stutters that we want to avoid.
	 * Instead we do a delayed write when we try to reclaim 
	 * a dirty dquot. Also xfs_sync will take part of the burden...
	 */
	xfs_qm_dqput(dqp);
}


/* 
 * write a modified dquot to disk.
 * The dquot must be locked and the flush lock too taken by caller.
 * The flush lock will not be unlocked until the dquot reaches the disk,
 * but the dquot is free to be unlocked and modified by the caller
 * in the interim. Dquot is still locked on return. This behavior is
 * identical to that of inodes.
 */

int
xfs_qm_dqflush(
	xfs_dquot_t 		*dqp,
	uint			flags)
{
	xfs_mount_t		*mp;
	buf_t			*bp;
	xfs_disk_dquot_t 	*ddqp;
	int			s, error;
	
	ASSERT(XFS_DQ_IS_LOCKED(dqp));
	ASSERT(XFS_DQ_IS_FLUSH_LOCKED(dqp));
	xfs_dqtrace_entry(dqp, "DQFLUSH");

	/*
	 * If not dirty, nada.
	 */
	if (!XFS_DQ_IS_DIRTY(dqp)) {
		xfs_dqfunlock(dqp);
		return (0);
	}

	/*
	 * Cant flush a pinned dquot. Wait for it.
	 */
	xfs_qm_dqunpin_wait(dqp);

	/*
	 * Get the buffer containing the on-disk dquot 
	 * We don't need a transaction envelope because we know that the
	 * the ondisk-dquot has already been allocated for.
	 */
	if (error = xfs_qm_dqtobp(NULL, dqp, &ddqp, &bp, 0)) {
		xfs_dqtrace_entry(dqp, "DQTOBP FAIL");
		ASSERT(error != ENOENT);
		/* 
		 * Quotas could have gotten turned off (ESRCH)
		 */
		xfs_dqfunlock(dqp);
		return (error);
	}
	/*
         * Make sure the location we're flushing out to really looks
         * like a dquot. Ditto for the dquot being flushed to disk.
         */
	if (xfs_qm_dqcheck(ddqp, dqp->q_core.d_id, "dqflush (disk buf)")) {
		xfs_dqtrace_entry(dqp, "DQTOBP FAIL DQCHECK1");
		brelse(bp);
		xfs_dqfunlock(dqp);
		return XFS_ERROR(EIO);
	}
	if (xfs_qm_dqcheck(&dqp->q_core, ddqp->d_id, "dqflush (disk copy)")) {
		xfs_dqtrace_entry(dqp, "DQTOBP FAIL DQCHECK2");
		brelse(bp);
		xfs_dqfunlock(dqp);
		return XFS_ERROR(EIO);
	}

	/* This is the only portion of data that needs to persist */
	bcopy(&(dqp->q_core), ddqp, sizeof(xfs_disk_dquot_t));
	
	/*
	 * Clear the dirty field and remember the flush lsn for later use.
	 */
	dqp->dq_flags &= ~(XFS_DQ_DIRTY);
	mp = dqp->q_mount;

	/* lsn is 64 bits */
	s = AIL_LOCK(mp);
	dqp->q_logitem.qli_flush_lsn = dqp->q_logitem.qli_item.li_lsn;
	AIL_UNLOCK(mp, s);

	/*
	 * Attach an iodone routine so that we can remove this dquot from the
	 * AIL and release the flush lock once the dquot is synced to disk.
	 */
	xfs_buf_attach_iodone(bp, (void(*)(buf_t*,xfs_log_item_t*))
			      xfs_qm_dqflush_done, &(dqp->q_logitem.qli_item));
	/*
	 * If the buffer is pinned then push on the log so we won't
	 * get stuck waiting in the write for too long.
	 */
	if (bp->b_pincount > 0) {
		xfs_dqtrace_entry(dqp, "DQFLUSH LOG FORCE");
		xfs_log_force(mp, (xfs_lsn_t)0, XFS_LOG_FORCE);
	}
	
        if (flags & XFS_QMOPT_DELWRI) {
                bdwrite(bp);
        } else if (flags & XFS_QMOPT_ASYNC) {
                bawrite(bp);
        } else {
                error = bwrite(bp);
        }
	xfs_dqtrace_entry(dqp, "DQFLUSH END");
	/* 
	 * dqp is still locked, but caller is free to unlock it now.
	 */
	return (0);
	
}

/*
 * This is the dquot flushing I/O completion routine.  It is called
 * from interrupt level when the buffer containing the dquot is
 * flushed to disk.  It is responsible for removing the dquot logitem
 * from the AIL if it has not been re-logged, and unlocking the dquot's
 * flush lock. This behavior is very similar to that of inodes..
 */
/*ARGSUSED*/
STATIC void
xfs_qm_dqflush_done(
        buf_t         		*bp,
        xfs_dq_logitem_t	*qip)
{
        xfs_dquot_t     	*dqp;
        int             	s;

        dqp = qip->qli_dquot;

        /*
         * We only want to pull the item from the AIL if its
	 * location in the log has not changed since we started the flush.
	 * Thus, we only bother if the dquot's lsn has
	 * not changed. First we check the lsn outside the lock
         * since it's cheaper, and then we recheck while
         * holding the lock before removing the dquot from the AIL.
         */
        if ((qip->qli_item.li_flags & XFS_LI_IN_AIL) &&
	    qip->qli_item.li_lsn == qip->qli_flush_lsn) {
                s = AIL_LOCK(dqp->q_mount);
		/*
		 * xfs_trans_delete_ail() drops the AIL lock.
		 */
                if (qip->qli_item.li_lsn == qip->qli_flush_lsn) 
                        xfs_trans_delete_ail(dqp->q_mount,
                                             (xfs_log_item_t*)qip, s);
                else 
                        AIL_UNLOCK(dqp->q_mount, s);
                
        }

	/*
         * Release the dq's flush lock since we're done with it.
         */
        xfs_dqfunlock(dqp);
}

	
int
xfs_qm_dqflock_nowait(
	xfs_dquot_t *dqp)
{
	int locked;
	
	locked = cpsema(&((dqp)->q_flock));

	/* XXX ifdef these out */
	if (locked)
		(dqp)->dq_flags |= XFS_DQ_FLOCKED;
	return (locked);
}


int
xfs_qm_dqlock_nowait(
	xfs_dquot_t *dqp)
{
	int locked;
	
	locked = mutex_trylock(&((dqp)->q_qlock)); 
	if (locked) 
		(dqp)->dq_flags |= XFS_DQ_LOCKED;

	return (locked);
}

void
xfs_dqlock(
	xfs_dquot_t *dqp)
{
	mutex_lock(&(dqp->q_qlock), PINOD); 
	(dqp)->dq_flags |= XFS_DQ_LOCKED;
}

void
xfs_dqunlock(
	xfs_dquot_t *dqp)
{
	mutex_unlock(&(dqp->q_qlock)); 
	dqp->dq_flags &= ~(XFS_DQ_LOCKED);
	if (dqp->q_logitem.qli_dquot == dqp) { 
		xfs_trans_unlocked_item(dqp->q_mount,
					(xfs_log_item_t*)&(dqp->q_logitem));	
	}
}


void
xfs_dqunlock_nonotify(
	xfs_dquot_t *dqp)
{
	mutex_unlock(&(dqp->q_qlock)); 
	dqp->dq_flags &= ~(XFS_DQ_LOCKED);
}

void
xfs_dqlock2(
	xfs_dquot_t	*d1,
	xfs_dquot_t	*d2)
{
	if (d1 && d2) {
		ASSERT(d1 != d2);
		if (d1->q_core.d_id > d2->q_core.d_id) {
			xfs_dqlock(d2);
			xfs_dqlock(d1);
		} else {
			xfs_dqlock(d1);
			xfs_dqlock(d2);
		}
	} else {
		if (d1) {
			xfs_dqlock(d1);
		} else if (d2) {
			xfs_dqlock(d2);
		}
	}
}


/*
 * A rarely used accessor. This exists because we don't really want 
 * to expose the internals of a dquot to the outside world.
 */
xfs_dqid_t
xfs_qm_dqid(
	xfs_dquot_t	*dqp)
{
	return (dqp->q_core.d_id);
}


/*
 * Take a dquot out of the mount's dqlist as well as the hashlist.
 * This is called via unmount as well as quotaoff, and the purge
 * will always succeed.
 * This also returns a pointer to the next dquot in the mount's dquot list.
 */
xfs_dquot_t *
xfs_qm_dqpurge(
	xfs_dquot_t	*dqp)
{
	xfs_dquot_t 	*nextdqp;
	xfs_dqhash_t	*thishash;
	xfs_mount_t	*mp;

	mp = dqp->q_mount;

	ASSERT(XFS_QM_IS_MPLIST_LOCKED(mp));
	ASSERT(XFS_DQ_IS_HASH_LOCKED(dqp->q_hash));

	xfs_dqlock(dqp);	
	/*
	 * We really can't afford to purge a dquot that is
	 * referenced, because these are hard refs.
	 * It shouldn't happen because we went thru _all_ inodes in 
	 * dqrele_all_inodes before calling this from
	 * quotaoff code, and didn't let the mountlock go.
	 */
	ASSERT(dqp->q_nrefs == 0);
	ASSERT(XFS_DQ_IS_ON_FREELIST(dqp));

	/*
	 * If we're turning off quotas, we have to make sure that, for
	 * example, we don't delete quota disk blocks while dquots are
	 * in the process of getting written to those disk blocks.
	 * This dquot might well be on AIL, and we can't leave it there
	 * if we're turning off quotas. Basically, we need this flush
	 * lock, and are willing to block on it.
	 */
	if (! xfs_qm_dqflock_nowait(dqp)) {
		/*
		 * Block on the flush lock after nudging dquot buffer,
		 * if it is incore.	
		 */
		xfs_qm_dqflock_pushbuf_wait(dqp);
	}

	/*
	 * XXXIf we're turning this type of quotas off, we don't care
	 * about the dirty metadata sitting in this dquot. OTOH, if 
	 * we're unmounting, we do care, so we flush it and wait.
	 */
	if (XFS_DQ_IS_DIRTY(dqp)) {
		xfs_dqtrace_entry(dqp, "DQPURGE ->DQFLUSH: DQDIRTY");
		/* dqflush unlocks dqflock */	
		/*
		 * Given that dqpurge is a very rare occurence, it is OK
		 * that we're holding the hashlist and mplist locks
		 * across the disk write. But, ... XXXsup
		 */
		(void) xfs_qm_dqflush(dqp, XFS_QMOPT_SYNC);
		xfs_dqflock(dqp); 
	}
	ASSERT(dqp->q_pincount == 0);
	ASSERT(! (dqp->q_logitem.qli_item.li_flags & XFS_LI_IN_AIL));

	nextdqp = dqp->MPL_NEXT;	
	thishash = dqp->q_hash;
	XQM_HASHLIST_REMOVE(thishash, dqp);
	XQM_MPLIST_REMOVE(&(mp->QI_MPL_LIST), dqp);
	/*
	 * XXX Move this to the front of the freelist, if we can get the
	 * freelist lock.
	 */
	ASSERT(XFS_DQ_IS_ON_FREELIST(dqp));

	dqp->q_mount = NULL;;
	dqp->q_hash = NULL;
	dqp->dq_flags = XFS_DQ_INACTIVE;
	bzero(&dqp->q_core, sizeof(dqp->q_core));

	xfs_dqfunlock(dqp);
	xfs_dqunlock(dqp);
	XFS_DQ_HASH_UNLOCK(thishash);
	return (nextdqp);
}


/*
 * Do some primitive error checking on ondisk dquot 
 * data structures. Not just for debugging, actually;
 * this can be useful for detecting data corruption mainly due to
 * disk failures.
 */
int
xfs_qm_dqcheck(
	xfs_disk_dquot_t *ddq,
	xfs_dqid_t	 id,
	char		 *str)
{
	int errs;

	errs = 0;
	if (ddq->d_magic != XFS_DQUOT_MAGIC) {
		cmn_err(CE_ALERT, 
			"%s : ondisk-dquot 0x%x, magic 0x%x != 0x%x\n",
			str, ddq, ddq->d_magic, XFS_DQUOT_MAGIC);
		errs++;
	}
	if (ddq->d_version != XFS_DQUOT_VERSION) {
		cmn_err(CE_ALERT, 
			"%s : ondisk-dquot 0x%x, version 0x%x != 0x%x\n",
			str, ddq, ddq->d_magic, XFS_DQUOT_VERSION);
		errs++;
	}

	if (ddq->d_flags != XFS_DQ_USER &&
	    ddq->d_flags != XFS_DQ_PROJ) {
		cmn_err(CE_ALERT, 
			"%s : ondisk-dquot 0x%x, unknown flags 0x%x\n",
			str, ddq, ddq->d_flags);
		errs++;
	}
	
	if (id != -1) {
		if (id != ddq->d_id) {
			cmn_err(CE_ALERT, 
				"%s : ondisk-dquot 0x%x, ID mismatch: "
				"0x%x expected, found id 0x%x\n",
				str, ddq, id, ddq->d_id);
			errs++;
		}
	}
	return (errs);
}

#ifdef QUOTADEBUG
void
xfs_qm_dqprint(xfs_dquot_t *dqp)
{
	printf( "-----------KERNEL DQUOT----------------\n");
	printf( "---- dquot ID	=  %d\n", (int) dqp->q_core.d_id);
	printf( "---- type      =  %s\n", XFS_QM_ISUDQ(dqp) ? "USR" :
	       "PRJ");
	printf( "---- fs        =  0x%x\n", dqp->q_mount);
	printf( "---- blkno     =  0x%x\n", (int) dqp->q_blkno);
	printf( "---- boffset	=  0x%x\n", (int) dqp->q_bufoffset);
	printf( "---- blkhlimit	=  %llu (%d)\n", 
	       dqp->q_core.d_blk_hardlimit,
	       (int) dqp->q_core.d_blk_hardlimit);
	printf( "---- blkslimit	=  %llu (0x%x)\n", 
	       dqp->q_core.d_blk_softlimit,
	       (int)dqp->q_core.d_blk_softlimit);
	printf( "---- inohlimit	=  %llu (0x%x)\n", 
	       dqp->q_core.d_ino_hardlimit,
	       (int)dqp->q_core.d_ino_hardlimit);
	printf( "---- inoslimit	=  %llu (0x%x)\n", 
	       dqp->q_core.d_ino_softlimit,
	       (int)dqp->q_core.d_ino_softlimit);
	printf( "---- bcount	=  %llu (0x%x)\n", 
	       dqp->q_core.d_bcount,
	       (int)dqp->q_core.d_bcount);
	printf( "---- icount	=  %llu (0x%x)\n", 
	       dqp->q_core.d_icount,
	       (int)dqp->q_core.d_icount);
	printf( "---- btimer	=  %d\n", (int)dqp->q_core.d_btimer);
	printf( "---- itimer	=  %d\n", (int)dqp->q_core.d_itimer);

	printf( "---------------------------\n");
}
#endif

/*
 * Give the buffer a little push if it is incore and 
 * wait on the flush lock.
 */
void
xfs_qm_dqflock_pushbuf_wait(
	xfs_dquot_t *dqp)
{
	buf_t *bp;
	
	/*
	 * Check to see if the dquot has been flushed delayed
	 * write.  If so, grab its buffer and send it
	 * out immediately.  We'll be able to acquire
	 * the flush lock when the I/O completes.
	 */
	bp = incore(dqp->q_dev, dqp->q_blkno, 
		    dqp->q_mount->QI_DQCHUNKLEN,
		    INCORE_TRYLOCK);
	if (bp != NULL) {
		if (bp->b_flags & B_DELWRI) {
			if (bp->b_pincount > 0) {
				xfs_log_force(dqp->q_mount,
					      (xfs_lsn_t)0,
					      XFS_LOG_FORCE);
			}
			bawrite(bp);
		} else {
			brelse(bp);
		}
	}
	xfs_dqflock(dqp);
}


