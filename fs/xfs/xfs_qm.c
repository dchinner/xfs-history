#ident "$Revision: 1.8 $"


#include <sys/param.h>
#include <sys/sysinfo.h>
#include <sys/buf.h>
#include <sys/ksa.h>
#include <sys/vnode.h>
#include <sys/pfdat.h>
#include <sys/uuid.h>
#include <sys/capability.h>
#include <sys/errno.h>
#include <sys/kmem.h>
#include <sys/debug.h>
#include <sys/proc.h>
#include <sys/cmn_err.h>
#include <sys/cred.h>
#include <sys/vfs.h>
#include <sys/atomic_ops.h>
#include <sys/systm.h>
#include <sys/ktrace.h>
#include <sys/quota.h>
#include <limits.h>

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
#include "xfs_ialloc.h"
#include "xfs_alloc.h"
#include "xfs_bmap.h"
#include "xfs_btree.h"
#include "xfs_bmap.h"
#include "xfs_attr_sf.h"
#include "xfs_dir_sf.h"
#include "xfs_dinode.h"
#include "xfs_inode_item.h"
#include "xfs_buf_item.h"
#include "xfs_da_btree.h"
#include "xfs_inode.h"
#include "xfs_error.h"
#include "xfs_trans_priv.h"
#include "xfs_bit.h"
#include "xfs_clnt.h"
#include "xfs_quota.h"
#include "xfs_dqblk.h"
#include "xfs_dquot.h"
#include "xfs_qm.h"
#include "xfs_quota_priv.h"
#include "xfs_itable.h"


extern int      ncsize;
struct xfs_qm	*G_xqm = NULL;
extern int 	xfs_dir_ialloc(xfs_trans_t **tpp, xfs_inode_t *dp, mode_t mode, 
			       nlink_t nlink, dev_t rdev, cred_t *credp,
			       prid_t prid, xfs_inode_t	**ipp,
			       int *committed);
extern time_t	time;

typedef int 	(*xfs_dqbuf_iter) (xfs_mount_t *mp, buf_t *);

STATIC void	xfs_qm_list_init(xfs_dqlist_t *, char *, int);
STATIC void	xfs_qm_list_destroy(xfs_dqlist_t *);
STATIC int 	xfs_qm_dqget_noattach(xfs_inode_t *, xfs_dquot_t **, 
				      xfs_dquot_t **);
STATIC int	xfs_qm_qino_alloc(xfs_mount_t *, xfs_inode_t **, __int64_t,
				  uint);
STATIC int	xfs_qm_reset_dqcounts(xfs_mount_t *, buf_t *);
STATIC int	xfs_qm_dqiter_bufs(xfs_mount_t *, xfs_fsblock_t,
				   xfs_filblks_t, xfs_dqbuf_iter, uint);
STATIC int	xfs_qm_dqiterate(xfs_mount_t *, xfs_inode_t *, uint, 
				 xfs_dqbuf_iter);
STATIC void 	xfs_qm_quotacheck_dqadjust(xfs_dquot_t *, xfs_qcnt_t, xfs_qcnt_t);
STATIC int	xfs_qm_dqusage_adjust(xfs_mount_t *, xfs_trans_t *, xfs_ino_t, 
				      void *, daddr_t);
STATIC int	xfs_qm_quotacheck(xfs_mount_t *);

STATIC int	xfs_qm_init_quotainos(xfs_mount_t *);
STATIC int	xfs_qm_shake_freelist(int);
STATIC int	xfs_qm_shake(int);
STATIC xfs_dquot_t *xfs_qm_dqreclaim_one(void);
STATIC int	xfs_qm_dqattach_one(xfs_inode_t	*, xfs_dqid_t, uint, uint, uint,
				    xfs_dquot_t	*, xfs_dquot_t	**);
STATIC void	xfs_qm_dqattach_projhint(xfs_dquot_t *, xfs_dquot_t *, uint);
STATIC void 	xfs_qm_hold_quotafs_ref(struct xfs_mount *);
STATIC void 	xfs_qm_rele_quotafs_ref(struct xfs_mount *);
STATIC void	xfs_qm_dettach_pdquots(xfs_mount_t *);
STATIC int	xfs_qm_get_rtblks(xfs_inode_t *, xfs_qcnt_t *);

#ifdef DEBUG
extern mutex_t	qcheck_lock;
#endif

#ifdef QUOTADEBUG
#define XQM_LIST_PRINT(l, NXT, title) \
{ \
	  xfs_dquot_t	*dqp; int i = 0;\
	  printf("%s (#%d)\n", title, (int) (l)->qh_nelems); \
	  for (dqp = (l)->qh_next; dqp != NULL; dqp = dqp->NXT) { \
	    printf("\t%d.\t\"%d (%s)\"\t bcnt = %d, icnt = %d refs = %d\n", \
			 ++i, (int) dqp->q_core.d_id, \
		         DQFLAGTO_TYPESTR(dqp),      \
			 (int) dqp->q_core.d_bcount, \
			 (int) dqp->q_core.d_icount, \
                         (int) dqp->q_nrefs);  } \
}
#endif

/*
 * Initialize the XQM structure.
 * Note that there is only one quota manager for the entire kernel,
 * not one each per file system.
 */
struct xfs_qm *
xfs_qm_init()
{
	xfs_qm_t 	*xqm;
	int		hsize, i;
	
	xqm = kmem_zalloc(sizeof(xfs_qm_t), KM_SLEEP);
	ASSERT(xqm);

	/*
	 * initialize the dquot hash tables .
	 */
	hsize = (ncsize < XFS_QM_NCSIZE_THRESHOLD) ? 
		XFS_QM_HASHSIZE_LOW : XFS_QM_HASHSIZE_HIGH;

	xqm->qm_dqhashmask = hsize - 1;
	/*
	 * XXXsup We can keep reference counts on user and proj quotas
	 * inside XQM separately, and avoid having two hashtables even
	 * when only one 'type' is active in the system.
	 */
	xqm->qm_usr_dqhtable = (xfs_dqhash_t *)kmem_zalloc(hsize *
						      sizeof(xfs_dqhash_t),
						      KM_SLEEP);
	xqm->qm_prj_dqhtable = (xfs_dqhash_t *)kmem_zalloc(hsize *
						      sizeof(xfs_dqhash_t),
						      KM_SLEEP);
        ASSERT(xqm->qm_usr_dqhtable != NULL);
        ASSERT(xqm->qm_prj_dqhtable != NULL);

        for (i = 0; i < hsize; i++) {
		xfs_qm_list_init(&(xqm->qm_usr_dqhtable[i]), "uxdqh", i);
		xfs_qm_list_init(&(xqm->qm_prj_dqhtable[i]), "pxdqh", i);
        }

	/* 
	 * freelist of all dquots of all file systems
	 */
	xfs_qm_freelist_init(&(xqm->qm_dqfreelist));

	/*
	 * dquot zone. we register our own shaked callback.
	 */
	xqm->qm_dqzone = kmem_zone_init(sizeof(xfs_dquot_t), "dquots");
	shake_register(SHAKEMGR_MEMORY, xfs_qm_shake);

	/*
	 * The t_dqinfo portion of transactions.
	 */
	xqm->qm_dqtrxzone = kmem_zone_init(sizeof(xfs_dquot_acct_t), "dqtrx");

	xqm->qm_totaldquots = 0;
	xqm->qm_dqfree_ratio = XFS_QM_DQFREE_RATIO;
	xqm->qm_nrefs = 0;
#ifdef DEBUG
	mutex_init(&qcheck_lock, MUTEX_DEFAULT, "qchk");
#endif
	return (xqm);
}

/*
 * Destroy the global quota manager when its reference count goes to zero.
 */
void
xfs_qm_destroy(
	struct xfs_qm *xqm)
{
	int     hsize, i;

	ASSERT(xqm != NULL);
	ASSERT(xqm->qm_nrefs == 0);
        hsize = xqm->qm_dqhashmask + 1;
	for (i = 0; i < hsize; i++) {
		xfs_qm_list_destroy(&(xqm->qm_usr_dqhtable[i]));
		xfs_qm_list_destroy(&(xqm->qm_prj_dqhtable[i]));
	}
	kmem_free(xqm->qm_usr_dqhtable, hsize * sizeof(xfs_dqhash_t));
	kmem_free(xqm->qm_prj_dqhtable, hsize * sizeof(xfs_dqhash_t));
	xqm->qm_usr_dqhtable = NULL;
	xqm->qm_prj_dqhtable = NULL;
	xqm->qm_dqhashmask = 0;
	xfs_qm_freelist_destroy(&(xqm->qm_dqfreelist));
#ifdef DEBUG
	mutex_destroy(&qcheck_lock);
#endif
	kmem_free(xqm, sizeof(xfs_qm_t));
}

/*
 * Called at mount time to let XQM know that another file system is
 * starting quotas. This isn't crucial information as the individual mount
 * structures are pretty independent, but it helps the XQM keep a
 * global view of what's going on.
 */
/* ARGSUSED */
STATIC void
xfs_qm_hold_quotafs_ref(
	struct xfs_mount *mp)
{	
	ASSERT(G_xqm);
	/*
	 * We can keep a list of all filesystems with quotas mounted for
	 * debugging and statistical purposes, but ...
	 * Just take a reference and get out.
	 */
	XFS_QM_HOLD(G_xqm);
}


/*
 * Release the reference that a filesystem took at mount time,
 * so that we know when we need to destroy the entire quota manager.
 */
/* ARGSUSED */
STATIC void
xfs_qm_rele_quotafs_ref(
	struct xfs_mount *mp)
{
	xfs_dquot_t	*dqp, *nextdqp;

	ASSERT(G_xqm);
	ASSERT(G_xqm->qm_nrefs > 0);
	XFS_QM_RELE(G_xqm);

	/*
	 * Go thru the freelist and destroy all inactive dquots.
	 */
	xfs_qm_freelist_lock(G_xqm);

	for (dqp = G_xqm->qm_dqfreelist.qh_next; 
	     dqp != (xfs_dquot_t *)&(G_xqm->qm_dqfreelist); ) {
		xfs_dqlock(dqp);	
		nextdqp = dqp->dq_flnext;
		if (dqp->dq_flags & XFS_DQ_INACTIVE) {
			ASSERT(dqp->q_mount == NULL);
			ASSERT(! XFS_DQ_IS_DIRTY(dqp));
			ASSERT(dqp->HL_PREVP == NULL);
			ASSERT(dqp->MPL_PREVP == NULL);
			XQM_FREELIST_REMOVE(dqp);
			xfs_dqunlock(dqp);
			xfs_qm_dqdestroy(dqp);
		} else {
			xfs_dqunlock(dqp);
		}
		dqp = nextdqp;
	}
	xfs_qm_freelist_unlock(G_xqm);

	/*
	 * Destroy the entire XQM. If somebody mounts with quotaon, this'll
	 * be restarted. XXXsup this is racey.. Somebody can be mounting 
	 * at the same time... 
	 */
	if (G_xqm->qm_nrefs == 0) {
		xfs_qm_destroy(G_xqm);
		G_xqm = NULL;
	}
}

/*
 * This is called at mount time from xfs_cmountfs in vfsops.c to initialize the
 * quotainfo structure and start the global quotamanager (G_xqm) if it hasn't
 * already.
 * Note that the superblock has not been read in yet. This is before
 * xfs_mount_int() gets called.
 */
void
xfs_qm_mount_quotainit(
	xfs_mount_t	*mp,
	uint		flags)
{
	/*
	 * User or project quotas has to be on, or we shouldn't be here
	 */
	ASSERT(flags & (XFSMNT_UQUOTA | XFSMNT_PQUOTA));

	if (G_xqm == NULL) {
		if ((G_xqm = xfs_qm_init()) == NULL)
			return; 
	}

	/*
	 * Initialize the flags in the mount structure. From this point
	 * onwards we look at m_qflags to figure out if quotas's ON/OFF, etc.
	 * Note that we enforce nothing if accounting is off.
	 * ie.  XFSMNT_*QUOTA must be ON for XFSMNT_*QUOTAENF.
	 * It isn't necessary to take the quotaoff lock to do this; this is
	 * called from mount.
	 */
	if (flags & XFSMNT_UQUOTA) {
		mp->m_qflags |= (XFS_UQUOTA_ACCT | XFS_UQUOTA_ACTIVE);
		if (flags & XFSMNT_UQUOTAENF) 
			mp->m_qflags |= XFS_UQUOTA_ENFD;
	}
	if (flags & XFSMNT_PQUOTA) {
		mp->m_qflags |= (XFS_PQUOTA_ACCT | XFS_PQUOTA_ACTIVE);
		if (flags & XFSMNT_PQUOTAENF) 
			mp->m_qflags |= XFS_PQUOTA_ENFD;
	}
}

/*
 * Just destroy the quotainfo structure.
 */
void
xfs_qm_unmount_quotadestroy(
	xfs_mount_t	*mp)
{
	xfs_qm_destroy_quotainfo(mp);
}

/*
 * This is called from xfs_mount_int to start quotas and initialize all
 * necessary data structures like quotainfo, and in the rootfs's case
 * G_xqm. This is also responsible for running a quotacheck as necessary.
 * We are guaranteed that the superblock is consistently read in at this point.
 */
int
xfs_qm_mount_quotas(
	xfs_mount_t	*mp)
{
	int		s;
	int		error;
	uint		sbf;
	extern dev_t	rootdev;

#ifdef DEBUG
	cmn_err(CE_NOTE, "Attempting to turn on disk quotas.");
#endif
	error = 0;
	/*
	 * If a non-root file system had quotas running earlier, but decided
	 * to mount without -o quota/pquota options, we revoke the quotachecked
	 * license, and bail out.
	 */
	if (! XFS_IS_QUOTA_ON(mp) &&
	    (mp->m_dev != rootdev) &&
	    (mp->m_sb.sb_qflags & (XFS_UQUOTA_ACCT|XFS_PQUOTA_ACCT))) {
		mp->m_qflags = 0;
		goto write_changes;
	}
	    
#ifdef _IRIX62_XFS_ONLY
	/*
	 * If quotas on realtime volumes is not supported, we disable
	 * quotas immediately.
	 */
	if (mp->m_sb.sb_rextents) {
		cmn_err(CE_NOTE,
			"Cannot turn quotas on a realtime filesystem :%s",
			mp->m_fsname);
		mp->m_qflags = 0;
		goto write_changes;
	}
#endif
	/*
	 * If this is the root file system, mark flags in mount struct first.
	 * We couldn't do this earlier because we didn't have the superblock
	 * read in.
	 */
	if (mp->m_dev == rootdev) {
		ASSERT(XFS_SB_VERSION_HASQUOTA(&mp->m_sb));
		ASSERT(mp->m_sb.sb_qflags & 
		       (XFS_UQUOTA_ACCT|XFS_PQUOTA_ACCT));
		if (G_xqm == NULL) {
			if ((G_xqm = xfs_qm_init()) == NULL) {	
				mp->m_qflags = 0;
				error = EINVAL;
				goto write_changes;
			}
		}
		mp->m_qflags = mp->m_sb.sb_qflags;
		if (mp->m_qflags & XFS_UQUOTA_ACCT)
			mp->m_qflags |= XFS_UQUOTA_ACTIVE;
		if (mp->m_qflags & XFS_PQUOTA_ACCT)
			mp->m_qflags |= XFS_PQUOTA_ACTIVE;
		/*
		 * The quotainode of the root file system may or may not
		 * exist at this point.
		 */
	}

	ASSERT(XFS_IS_QUOTA_RUNNING(mp));
	/*
	 * Allocate the quotainfo structure inside the mount struct, and
	 * and create quotainode(s), and change/rev superblock if necessary.
	 */
	if (error = xfs_qm_init_quotainfo(mp)) {
		/* 
		 * We must turn off quotas.
		 */
		ASSERT(mp->m_quotainfo == NULL);
		ASSERT(G_xqm != NULL);
		mp->m_qflags = 0;
		goto write_changes;
	}
	/*
	 * If any of the quotas isn't consistent,
	 * do a quotacheck.
	 */
	if (XFS_QM_NEED_QUOTACHECK(mp)) {
#ifdef DEBUG
		cmn_err(CE_NOTE, "Doing a quotacheck. Please wait.");
#endif
		if (error = xfs_qm_quotacheck(mp)) {
			cmn_err(CE_WARN, "Quotacheck unsuccessful (Error %d): "
				"Disabling quotas.",
				error);
			/* 
			 * We must turn off quotas.
			 */
			ASSERT(mp->m_quotainfo != NULL);
			ASSERT(G_xqm != NULL);
			xfs_qm_destroy_quotainfo(mp);
			mp->m_qflags = 0;
			goto write_changes;
		}
#ifdef DEBUG
		cmn_err(CE_NOTE, "Done quotacheck.");
#endif		
	}
 write_changes:	
	/*
	 * We actually don't have to acquire the SB_LOCK at all.
	 * This can only be called from mount, and that's single threaded. XXX
	 */
	s = XFS_SB_LOCK(mp);
	sbf = mp->m_sb.sb_qflags;
	mp->m_sb.sb_qflags = mp->m_qflags & XFS_MOUNT_QUOTA_ALL;
	XFS_SB_UNLOCK(mp, s);
	
	if (sbf != (mp->m_qflags & XFS_MOUNT_QUOTA_ALL)) {
		if (xfs_qm_write_sb_changes(mp, XFS_SB_QFLAGS)) {
			/*
			 * We could only have been turning quotas off.
			 * We aren't in very good shape actually because
			 * the incore structures are convinced that quotas are
			 * off, but the on disk superblock doesn't know that !
			 */
			ASSERT(!(XFS_IS_QUOTA_RUNNING(mp)));
			cmn_err(CE_ALERT,
				"XFS mount_quotas: Superblock update failed!"
				);
		}
	}

	if (error) {
		cmn_err(CE_WARN, "Failed to initialize disk quotas.");
	}
	return XFS_ERROR(error);
}

/*
 * Called from the vfsops layer.
 */
void
xfs_qm_unmount_quotas(
	xfs_mount_t	*mp)
{
	xfs_inode_t	*uqp, *pqp;

	/*
	 * Release the dquots that root inode, et al might be holding,
	 * before we flush quotas and blow away the quotainfo structure.
	 */
	if (mp->m_rootip->i_udquot || mp->m_rootip->i_pdquot)
		xfs_qm_dqdettach_inode(mp->m_rootip);
	if (mp->m_rbmip->i_udquot || mp->m_rbmip->i_pdquot)
		xfs_qm_dqdettach_inode(mp->m_rbmip);
	if (mp->m_rsumip->i_udquot || mp->m_rsumip->i_pdquot)
		xfs_qm_dqdettach_inode(mp->m_rsumip);
	
	/* 
	 * Flush out the quota inodes.
	 */
	uqp = pqp = NULL;
	if (mp->m_quotainfo) {
		if ((uqp = mp->m_quotainfo->qi_uquotaip) != NULL) {
			xfs_ilock(uqp, XFS_ILOCK_EXCL);
			xfs_iflock(uqp);
			xfs_iflush(uqp, XFS_IFLUSH_SYNC);
			xfs_iunlock(uqp, XFS_ILOCK_EXCL);
		}
		if ((pqp = mp->m_quotainfo->qi_pquotaip) != NULL) {
			xfs_ilock(pqp, XFS_ILOCK_EXCL);
			xfs_iflock(pqp);
			xfs_iflush(pqp, XFS_IFLUSH_SYNC);
			xfs_iunlock(pqp, XFS_ILOCK_EXCL);
		}
	}
	if (uqp) {
		 XFS_PURGE_INODE(XFS_ITOV(uqp));
		 mp->m_quotainfo->qi_uquotaip = NULL;
	}
	if (pqp) {
		XFS_PURGE_INODE(XFS_ITOV(pqp));
		mp->m_quotainfo->qi_pquotaip = NULL;
	}
}

/*
 * Flush all dquots of the given file system to disk. The dquots are
 * _not_ purged from memory here.
 */
int
xfs_qm_dqflush_all(
	xfs_mount_t	*mp,
	int		flags)
{
	int		recl;
	xfs_dquot_t	*dqp;

	if (mp->m_quotainfo == NULL)	
		return (0);
again:
	xfs_qm_mplist_lock(mp);
	FOREACH_DQUOT_IN_MP(dqp, mp) {
		xfs_dqlock(dqp);
		if (! XFS_DQ_IS_DIRTY(dqp)) {
			xfs_dqunlock(dqp);
			continue;
		}
		xfs_dqtrace_entry(dqp, "FLUSHALL: DQDIRTY");
		/* XXX a sentinel would be better */
		recl = mp->QI_MPLRECLAIMS;
		if (! xfs_qm_dqflock_nowait(dqp)) {
			/*
			 * If we can't grab the flush lock then check
                         * to see if the dquot has been flushed delayed
                         * write.  If so, grab its buffer and send it
                         * out immediately.  We'll be able to acquire
                         * the flush lock when the I/O completes.
                         */
			xfs_qm_dqflock_pushbuf_wait(dqp);
		}
		/*
		 * Let go of the mplist lock. We don't want to hold it
		 * across a disk write
		 */
		xfs_qm_mplist_unlock(mp);
		xfs_qm_dqflush(dqp, flags); 
		xfs_dqunlock(dqp);
		xfs_qm_mplist_lock(mp);
		if (recl != mp->QI_MPLRECLAIMS) {
			xfs_qm_mplist_unlock(mp);
			/* XXX restart limit */
			goto again;
		}
	}

	xfs_qm_mplist_unlock(mp);
	/* return ! busy */
	return (0);
}
/*
 * Release the proj dquot pointers the user dquots may be 
 * carrying around as a hint. mplist is locked on entry and exit.
 */
STATIC void
xfs_qm_dettach_pdquots(
	xfs_mount_t	*mp)
{
	xfs_dquot_t 	*dqp, *pdqp;
	int		nrecl;
	
 again:
	ASSERT(XFS_QM_IS_MPLIST_LOCKED(mp));
	dqp = mp->QI_MPLNEXT;
	while (dqp) {
		xfs_dqlock(dqp);	
		if (pdqp = dqp->q_pdquot) {
			xfs_dqlock(pdqp);
			dqp->q_pdquot = NULL;
		}
		xfs_dqunlock(dqp);
		
		if (pdqp) {

			/*
			 * Can't hold the mplist lock across a dqput.
			 * XXXmust convert to marker based iterations here.
			 */
			nrecl = mp->QI_MPLRECLAIMS;
			xfs_qm_mplist_unlock(mp);
			xfs_qm_dqput(pdqp);

			xfs_qm_mplist_lock(mp);
			if (nrecl != mp->QI_MPLRECLAIMS) 
				goto again;
		}
		dqp = dqp->MPL_NEXT;
	}
	
}

/*
 * Go through all the incore dquots of this file system and take them
 * of the mplist and hashlist, if the dquot type matches the dqtype
 * parameter. This is used when turning off quota accounting for 
 * users and/or projects, as well as when the filesystem is unmounting.
 */
int
xfs_qm_dqpurge_all(
	xfs_mount_t	*mp,
	uint		flags)  /* QUOTAOFF/UMOUNTING/UQUOTA/PQUOTA */
{
	xfs_dquot_t 	*dqp;
	uint		dqtype;
	int		nrecl;

	if (mp->m_quotainfo == NULL)
		return (0);

	dqtype = (flags & XFS_QMOPT_UQUOTA) ? XFS_DQ_USER : 0;
	dqtype |= (flags & XFS_QMOPT_PQUOTA) ? XFS_DQ_PROJ : 0;

	xfs_qm_mplist_lock(mp);
	
	/*
	 * In the first pass through all incore dquots of this filesystem,
	 * we release the proj dquot pointers the user dquots may be 
	 * carrying around as a hint. We need to do this irrespective of 
	 * what's being turned off.
	 */
	xfs_qm_dettach_pdquots(mp);

 again:
	ASSERT(XFS_QM_IS_MPLIST_LOCKED(mp));
	/*
	 * Try to get rid of all of the unwanted dquots. The idea is to
	 * get them off mplist and hashlist, but leave them on freelist.
	 */
	dqp = mp->QI_MPLNEXT;
	while (dqp) {
		/*
		 * It's OK to look at the type without taking dqlock here.
		 * We're holding the mplist lock here, and that's needed for
		 * a dqreclaim. 
		 */
		if ((dqp->dq_flags & dqtype) == 0) {
			dqp = dqp->MPL_NEXT;
			continue;
		}
		
		if (! xfs_qm_dqhashlock_nowait(dqp)) {
			nrecl = mp->QI_MPLRECLAIMS;
			xfs_qm_mplist_unlock(mp);
			XFS_DQ_HASH_LOCK(dqp->q_hash);
			xfs_qm_mplist_lock(mp);

			/*
			 * XXXTheoretically, we can get into a very long
			 * ping pong game here. 
			 * No one can be adding dquots to the mplist at
			 * this point, but somebody might be taking things off.
			 */
			if (nrecl != mp->QI_MPLRECLAIMS) {
				XFS_DQ_HASH_UNLOCK(dqp->q_hash);
				goto again;
			}
		}
		
		/*
		 * Take the dquot off the mplist and hashlist. It may remain on
		 * freelist. This also returns a ptr to the next dquot on mplist.
		 */
		dqp = xfs_qm_dqpurge(dqp);
	}
	xfs_qm_mplist_unlock(mp);
#if 0
	xfs_qm_freelist_print(&(G_xqm->qm_dqfreelist),
			      "@@@@@++ Free list (After dqpurge_all) @@@@@+");
#endif
	return (0);
}

STATIC int
xfs_qm_dqattach_one(
	xfs_inode_t	*ip,
	xfs_dqid_t	id,
	uint		type,
	uint		doalloc,
	uint		dolock,
	xfs_dquot_t	*udqhint, /* hint */
	xfs_dquot_t	**IO_idqpp)
{
	xfs_dquot_t	*dqp;
	int		error;

	ASSERT(XFS_ISLOCKED_INODE_EXCL(ip));
	error = 0;
	/* 
	 * See if we already have it in the inode itself. IO_idqpp is
	 * &i_udquot or &i_pdquot. This made the code look weird, but
	 * made the logic a lot simpler.
	 */
	if (dqp = *IO_idqpp) {
		if (dolock)
			xfs_dqlock(dqp);
		xfs_dqtrace_entry(dqp, "DQATTACH: found in ip");
		goto done;
	} 

	/*
	 * udqhint is the i_udquot field in inode, and is non-NULL only
	 * when the type arg is XFS_DQ_PROJ. Its purpose is to save a 
	 * lookup by dqid (xfs_qm_dqget) by caching a project dquot inside
	 * the user dquot.
	 */
	if (udqhint && !dolock)
		xfs_dqlock(udqhint);

	/*
	 * No need to take dqlock to look at the id.
	 * The ID can't change until it gets reclaimed, and it won't
	 * be reclaimed as long as we have a ref from inode and we hold
	 * the ilock.
	 */
	if (udqhint &&  
	    (dqp = udqhint->q_pdquot) &&
	    (dqp->q_core.d_id == id)) {
		ASSERT(XFS_DQ_IS_LOCKED(udqhint));
		xfs_dqlock(dqp);
		XFS_DQHOLD(dqp);
		ASSERT(*IO_idqpp == NULL);
		*IO_idqpp = dqp;
		if (!dolock) {
			xfs_dqunlock(dqp);
			xfs_dqunlock(udqhint);
		}
		/* XXX XFSSTATS */
		goto done;
	}
	/*
	 * We can't hold a dquot lock when we call the dqget code. 
	 * We'll deadlock in no time, because of (not conforming to) 
	 * lock ordering - the inodelock comes before any dquot lock, 
	 * and we may drop and reacquire the ilock in xfs_qm_dqget().
	 */
	if (udqhint)
		xfs_dqunlock(udqhint);
	/* 
	 * Find the dquot from somewhere. This bumps the
	 * reference count of dquot and returns it locked.
	 * This can return ENOENT if dquot didn't exist on
	 * disk and we didn't ask it to allocate; 
	 * ESRCH if quotas got turned off suddenly.
	 */
	if (error = xfs_qm_dqget(ip->i_mount, ip, id, type, doalloc, &dqp)) {
		if (error != ESRCH && error != ENOENT)
			xfs_qm_force_quotaoff(ip->i_mount);
		if (udqhint && dolock)
			xfs_dqlock(udqhint);
		goto done;
	}

	xfs_dqtrace_entry(dqp, "DQATTACH: found by dqget");
	*IO_idqpp = dqp;
	ASSERT(dqp);
	ASSERT(XFS_DQ_IS_LOCKED(dqp));
	if (! dolock) {
		xfs_dqunlock(dqp);
		if (udqhint)
			ASSERT(! XFS_DQ_IS_LOCKED(udqhint));
		goto done;
	}
	if (! udqhint)
		goto done;

	ASSERT(udqhint);
	ASSERT(dolock);
	ASSERT(! XFS_DQ_IS_LOCKED(udqhint));
	ASSERT(XFS_DQ_IS_LOCKED(dqp));
	if (! xfs_qm_dqlock_nowait(udqhint)) {
		xfs_dqunlock(dqp);
		xfs_dqlock(udqhint);	
		xfs_dqlock(dqp);
	}	
done:	
#ifdef QUOTADEBUG
	if (udqhint) {
		if (dolock)
			ASSERT(XFS_DQ_IS_LOCKED(udqhint));
		else
			ASSERT(! XFS_DQ_IS_LOCKED(udqhint));
	}
	if (! error) {
		if (dolock)
			ASSERT(XFS_DQ_IS_LOCKED(dqp));
		else
			ASSERT(! XFS_DQ_IS_LOCKED(dqp));
	}
#endif
	return (error);
}


/*
 * Given a udquot and proj dquot, attach a ptr to the proj dquot in the
 * udquot as a hint for future lookups. The idea sounds simple, but the
 * execution isn't, because the udquot might have a proj dquot attached
 * already and getting rid of that gets us into lock ordering contraints.
 * The process is complicated more by the fact that the dquots may or may not
 * be locked on entry.
 */
STATIC void
xfs_qm_dqattach_projhint(
	xfs_dquot_t	*udq,
	xfs_dquot_t	*pdq,
	uint		locked)
{
	xfs_dquot_t	*tmp;

#ifdef QUOTADEBUG
	if (locked) {
		ASSERT(XFS_DQ_IS_LOCKED(udq));
		ASSERT(XFS_DQ_IS_LOCKED(pdq));
	} else {
		ASSERT(! XFS_DQ_IS_LOCKED(udq));
		ASSERT(! XFS_DQ_IS_LOCKED(pdq));
	}
#endif
	if (! locked) 
		xfs_dqlock(udq);
	
	if (tmp = udq->q_pdquot) {
		if (tmp == pdq) {
			if (! locked) 
				xfs_dqunlock(udq);
			return;
		}

		udq->q_pdquot = NULL;
		/*
		 * We can't keep any dqlocks when calling dqrele,
		 * because the freelist lock comes before dqlocks.
		 */
		xfs_dqunlock(udq);
		if (locked) 
			xfs_dqunlock(pdq);
		/*
		 * we took a hard reference once upon a time in dqget, 
		 * so give it back when the udquot no longer points at it
		 * dqput() does the unlocking of the dquot.
		 */
		xfs_qm_dqrele(tmp);
			
		ASSERT(! XFS_DQ_IS_LOCKED(udq));
		ASSERT(! XFS_DQ_IS_LOCKED(pdq));
		xfs_dqlock(udq);
		xfs_dqlock(pdq);

	} else {
		ASSERT(XFS_DQ_IS_LOCKED(udq));
		if (! locked) {
			ASSERT(! XFS_DQ_IS_LOCKED(pdq));
			xfs_dqlock(pdq);
		}
	}

	ASSERT(XFS_DQ_IS_LOCKED(udq));
	ASSERT(XFS_DQ_IS_LOCKED(pdq));
	/*
	 * Somebody could have attached a pdquot here,
	 * when we dropped the uqlock. If so, just do nothing.
	 */
	if (udq->q_pdquot == NULL) {
		XFS_DQHOLD(pdq);
		udq->q_pdquot = pdq;
	}
	if (! locked) {
		xfs_dqunlock(pdq);
		xfs_dqunlock(udq);
	}
}

			 
/*
 * Given a locked inode, attach dquot(s) to it, taking UQUOTAON / PQUOTAON
 * in to account.
 * If XFS_QMOPT_DQALLOC, the dquot(s) will be allocated if needed.
 * If XFS_QMOPT_DQLOCK, the dquot(s) will be returned locked. This option pretty
 * much made this code a complete mess, but it has been pretty useful.
 * If XFS_QMOPT_ILOCKED, then inode sent is already locked EXCL.
 * Inode may get unlocked and relocked in here, and the caller must deal with
 * the consequences.
 */
int		
xfs_qm_dqattach(
	xfs_inode_t	*ip,
	uint		flags)
{
	int		error;
	xfs_mount_t 	*mp;
	uint		nquotas;

	mp = ip->i_mount;
	ASSERT(ip->i_ino != mp->m_sb.sb_uquotino &&
	       ip->i_ino != mp->m_sb.sb_pquotino);


#ifdef QUOTADEBUG
	if (flags & XFS_QMOPT_ILOCKED)
		ASSERT(XFS_ISLOCKED_INODE_EXCL(ip));
#endif
	nquotas = 0;
	error = 0;
	if (! (flags & XFS_QMOPT_ILOCKED))
		xfs_ilock(ip, XFS_ILOCK_EXCL);
	
	if (XFS_IS_UQUOTA_ON(mp)) {
		if (error = xfs_qm_dqattach_one(ip, ip->i_d.di_uid, XFS_DQ_USER,
						flags & XFS_QMOPT_DQALLOC,
						flags & XFS_QMOPT_DQLOCK,
						NULL, &ip->i_udquot))
			goto done;
		nquotas++;
	}
	ASSERT(XFS_ISLOCKED_INODE_EXCL(ip));
	if (XFS_IS_PQUOTA_ON(mp)) {
		if (error = xfs_qm_dqattach_one(ip, ip->i_d.di_projid, 
						XFS_DQ_PROJ,
						flags & XFS_QMOPT_DQALLOC,
						flags & XFS_QMOPT_DQLOCK,
						ip->i_udquot, 
						&ip->i_pdquot))
			/* 
			 * Don't worry about the udquot that we may have
			 * attached above. It'll get dettached, if not already.
			 */
			goto done;
		nquotas++;
	}

	/*
	 * Attach this project quota to the user quota as a hint.
	 * This WON'T, in general, result in a thrash.
	 */
	if (nquotas == 2) {
		ASSERT(XFS_ISLOCKED_INODE_EXCL(ip));
		ASSERT(ip->i_udquot);
		ASSERT(ip->i_pdquot);

		/*
		 * We may or may not have the i_udquot locked at this point,
		 * but this check is OK since we don't depend on the i_pdquot to
		 * be accurate 100% all the time. It is just a hint, and this
		 * will succeed in general.
		 */
		if (ip->i_udquot->q_pdquot == ip->i_pdquot)
			goto done;
		/*
		 * Attach i_pdquot to the pdquot hint inside the i_udquot.
		 */
		xfs_qm_dqattach_projhint(ip->i_udquot, ip->i_pdquot,
					 flags & XFS_QMOPT_DQLOCK);
	}

 done:

#ifdef QUOTADEBUG
	if (! error) {
		if (ip->i_udquot) {
			if (flags & XFS_QMOPT_DQLOCK)
				ASSERT(XFS_DQ_IS_LOCKED(ip->i_udquot));
			else
				ASSERT(! XFS_DQ_IS_LOCKED(ip->i_udquot));
		}
		if (ip->i_pdquot) {
			if (flags & XFS_QMOPT_DQLOCK)
				ASSERT(XFS_DQ_IS_LOCKED(ip->i_pdquot));
			else
				ASSERT(! XFS_DQ_IS_LOCKED(ip->i_pdquot));
		}
		if (XFS_IS_UQUOTA_ON(mp))
			ASSERT(ip->i_udquot);
		if (XFS_IS_PQUOTA_ON(mp))
			ASSERT(ip->i_pdquot);
	}
#endif

	if (! (flags & XFS_QMOPT_ILOCKED))
		xfs_iunlock(ip, XFS_ILOCK_EXCL);

#ifdef QUOTADEBUG
	else
		ASSERT(XFS_ISLOCKED_INODE_EXCL(ip));
#endif
	return (error);						
}
     
/*
 * Release dquots (and their references) if any.
 * The inode should be locked EXCL except when this's called by
 * xfs_ireclaim.
 */
void
xfs_qm_dqdettach_inode(
	xfs_inode_t	*ip)
{
	ASSERT(ip->i_ino != ip->i_mount->m_sb.sb_uquotino);
	ASSERT(ip->i_ino != ip->i_mount->m_sb.sb_pquotino);
        if (ip->i_udquot) {
                xfs_qm_dqrele(ip->i_udquot);
                ip->i_udquot = NULL;
        }
        if (ip->i_pdquot) {
                xfs_qm_dqrele(ip->i_pdquot);
                ip->i_pdquot = NULL;
        }
	
}

int
xfs_qm_unmount(
	xfs_mount_t	*mp)
{
	vnode_t		*vp;

	if (XFS_IS_UQUOTA_ON(mp)) {
		vp = XFS_ITOV(mp->QI_UQIP);
		VN_RELE(vp);
		if (vp->v_count > 1)
			cmn_err(CE_WARN, "UQUOTA busy vp=0x%x count=%d\n", 
				vp, vp->v_count);
	} 
	if (XFS_IS_PQUOTA_ON(mp)) {
		vp = XFS_ITOV(mp->QI_PQIP);
		VN_RELE(vp);
		if (vp->v_count > 1)
			cmn_err(CE_WARN, "PQUOTA busy vp=0x%x count=%d\n", 
				vp, vp->v_count);
	} 

	return (0);
}


/*
   vfs_sync: SYNC_FSDATA|SYNC_ATTR|SYNC_BDFLUSH|SYNC_NOWAIT 0x31
   syscall sync: SYNC_FSDATA|SYNC_ATTR|SYNC_DELWRI 0x25
   umountroot : SYNC_WAIT | SYNC_CLOSE | SYNC_ATTR | SYNC_FSDATA
*/
#if 0
/*
 * VFS_SYNC flags: left here for reference.
 */
#define SYNC_NOWAIT     0               /* start delayed writes */
#define SYNC_ATTR       0x01            /* sync attributes */
#define SYNC_CLOSE      0x02            /* close file system down */
#define SYNC_DELWRI     0x04            /* look at delayed writes */
#define SYNC_WAIT       0x08            /* wait for i/o to complete */
#define SYNC_BDFLUSH    0x10            /* BDFLUSH is calling -- don't block */
#define SYNC_FSDATA     0x20            /* flush fs data (e.g. superblocks) */
#define SYNC_PDFLUSH    0x40            /* push v_dpages */

#endif

void
xfs_qm_sync(
	xfs_mount_t	*mp,
	short		flags)
{
	int		recl, restarts;
	xfs_dquot_t	*dqp;
	uint		flush_flags;
	boolean_t	nowait;

	restarts = 0;
	/*
	 * We won't block unless we are asked to.
	 */
	nowait = (boolean_t)(flags & SYNC_BDFLUSH || (flags & SYNC_WAIT) == 0);

 again:
	xfs_qm_mplist_lock(mp);
	/*
	 * dqpurge_all takes the mplist and iterate thru all dquots in
	 * quotaoff also. However, if the QUOTA_ACTIVE bits are not cleared
	 * when we have the mplist lock, we know that dquots will be consistent
	 * as long as we have it locked.
	 */
	if (! XFS_IS_QUOTA_ON(mp)) { 
		xfs_qm_mplist_unlock(mp);
		return;
	}
	FOREACH_DQUOT_IN_MP(dqp, mp) {
		/*
		 * If this is vfs_sync calling, then skip the dquots that
		 * don't 'seem' to be dirty. ie. don't acquire dqlock.
		 * This is very similar to what xfs_sync does with inodes.
		 */
		if (flags & SYNC_BDFLUSH) {
			if (! XFS_DQ_IS_DIRTY(dqp))
				continue;
		}

		if (nowait) {
			/*
			 * Try to acquire the dquot lock. We are NOT out of
			 * lock order, but we just don't want to wait for this
			 * lock, unless somebody wanted us to.
			 */
			if (! xfs_qm_dqlock_nowait(dqp))
				continue;
		} else {
			xfs_dqlock(dqp);
		}
		
		/*
		 * Now, find out for sure if this dquot is dirty or not.
		 */
		if (! XFS_DQ_IS_DIRTY(dqp)) {
			xfs_dqunlock(dqp);
			continue;
		}
		
		/* XXX a sentinel would be better */
		recl = mp->QI_MPLRECLAIMS;
		if (! xfs_qm_dqflock_nowait(dqp)) {
			if (nowait) {
				xfs_dqunlock(dqp);
				continue;
			}
			/*
			 * If we can't grab the flush lock then if the caller
			 * really wanted us to give this our best shot,
			 * see if we can give a push to the buffer before we wait
			 * on the flush lock. At this point, we know that 
			 * eventhough the dquot is being flushed, 
			 * it has (new) dirty data.
			 */ 
			xfs_qm_dqflock_pushbuf_wait(dqp);
		}
		/*
		 * Let go of the mplist lock. We don't want to hold it
		 * across a disk write
		 */
		flush_flags = (nowait) ? XFS_QMOPT_DELWRI : XFS_QMOPT_SYNC;
		xfs_qm_mplist_unlock(mp);
		xfs_dqtrace_entry(dqp, "XQM_SYNC: DQFLUSH");
		xfs_qm_dqflush(dqp, flush_flags);
		xfs_dqunlock(dqp);

		xfs_qm_mplist_lock(mp);
		if (recl != mp->QI_MPLRECLAIMS) {
			if (++restarts >= XFS_QM_SYNC_MAX_RESTARTS) 
				break;

			xfs_qm_mplist_unlock(mp);
			goto again;
		}
	}

	xfs_qm_mplist_unlock(mp);
}



/*
 * This initializes all the quota information that's kept in the
 * mount structure
 */
int
xfs_qm_init_quotainfo(
	xfs_mount_t	*mp)
{
	xfs_quotainfo_t	*qinf;
	int		error;
	xfs_dquot_t	*dqp;

	ASSERT(XFS_IS_QUOTA_RUNNING(mp));
	ASSERT(G_xqm);

	qinf = mp->m_quotainfo = kmem_zalloc(sizeof(xfs_quotainfo_t), KM_SLEEP);

	/*
	 * See if quotainodes are setup, and if not, allocate them,
	 * and change the superblock accordingly.
	 */
	if (error = xfs_qm_init_quotainos(mp)) {
		kmem_free(qinf, sizeof(xfs_quotainfo_t));
		mp->m_quotainfo = NULL;
		return (error);
	}

	spinlock_init(&qinf->qi_pinlock, "xfs_qinf_pin");
	xfs_qm_list_init(&qinf->qi_dqlist, "mpdqlist", 0);
	qinf->qi_dqreclaims = 0;

	/* mutex used to serialize quotaoffs */
	mutex_init(&qinf->qi_quotaofflock, MUTEX_DEFAULT, "qoff");

	/* Precalc some constants */
	qinf->qi_dqchunklen = XFS_FSB_TO_BB(mp, XFS_DQUOT_CLUSTER_SIZE_FSB);
	ASSERT(qinf->qi_dqchunklen);
	qinf->qi_dqperchunk = BBTOB(qinf->qi_dqchunklen) /
		sizeof(xfs_dqblk_t);
	/*
	 * Tell XQM that we exist.
	 */
	xfs_qm_hold_quotafs_ref(mp);
	mp->m_qflags |= (mp->m_sb.sb_qflags & XFS_ALL_QUOTA_CHKD);
	
	/*
	 * We try to get the limits from the superuser's limits fields.
	 * This is quite hacky, but it is standard quota practice.
	 * We look at the USR dquot with id == 0 first, but if user quotas
	 * are not enabled we goto the PROJ dquot with id == 0.
	 * We don't really care to keep separate default limits for user
	 * and project quotas, at least not at this point.
	 */
	error = xfs_qm_dqget(mp, NULL, (xfs_dqid_t)0, 
			     (XFS_IS_UQUOTA_RUNNING(mp)) ?
			     XFS_DQ_USER : XFS_DQ_PROJ,
			     XFS_QMOPT_DQSUSER, &dqp);
	if (! error) {
		/*
		 * The warnings and timers set the grace period given to
		 * a user or project before he or she can not perform 
		 * any more writing. If it is zero, a default is used.
		 */
		qinf->qi_btimelimit = dqp->q_core.d_btimer ?
			dqp->q_core.d_btimer : XFS_QM_BTIMELIMIT;
		qinf->qi_itimelimit = dqp->q_core.d_itimer ? 
			dqp->q_core.d_itimer : XFS_QM_ITIMELIMIT;
		qinf->qi_rtbtimelimit = dqp->q_core.d_rtbtimer ?
			dqp->q_core.d_rtbtimer : XFS_QM_RTBTIMELIMIT;
		qinf->qi_bwarnlimit = dqp->q_core.d_bwarns ? 
			dqp->q_core.d_bwarns : XFS_QM_BWARNLIMIT;
		qinf->qi_iwarnlimit = dqp->q_core.d_iwarns ?
			dqp->q_core.d_iwarns : XFS_QM_IWARNLIMIT;
		
		/*
		 * We sent the XFS_QMOPT_DQSUSER flag to dqget because
		 * we don't want this dquot cached. We haven't done a 
		 * quotacheck yet, and quotacheck doesn't like incore dquots.
		 */
		xfs_qm_dqdestroy(dqp);
	} else {
		qinf->qi_btimelimit = XFS_QM_BTIMELIMIT;
		qinf->qi_itimelimit = XFS_QM_ITIMELIMIT;
		qinf->qi_rtbtimelimit = XFS_QM_RTBTIMELIMIT;
		qinf->qi_bwarnlimit = XFS_QM_BWARNLIMIT;
		qinf->qi_iwarnlimit = XFS_QM_IWARNLIMIT;
	}
	
	return (0);
}


/* 
 * Gets called when unmounting a filesystem or when all quotas get
 * turned off.
 * This purges the quota inodes, destroys locks and frees itself.
 */
void
xfs_qm_destroy_quotainfo(
	xfs_mount_t 	*mp)
{
	xfs_quotainfo_t	*qi;
	
	qi = mp->m_quotainfo;
	ASSERT(qi != NULL);
	ASSERT(G_xqm != NULL);

	/*
	 * Release the reference that XQM kept, so that we know
	 * when the XQM structure should be freed. We cannot assume
	 * that G_xqm is non-null after this point.
	 */
	xfs_qm_rele_quotafs_ref(mp);

	spinlock_destroy(&qi->qi_pinlock);

	if (qi->qi_uquotaip) {
		XFS_PURGE_INODE(XFS_ITOV(qi->qi_uquotaip));
		qi->qi_uquotaip = NULL; /* paranoia */
	}
	if (qi->qi_pquotaip) {
		XFS_PURGE_INODE(XFS_ITOV(qi->qi_pquotaip));
		qi->qi_pquotaip = NULL; 
	}
	mutex_destroy(&qi->qi_quotaofflock);
	kmem_free(qi, sizeof(xfs_quotainfo_t));
	mp->m_quotainfo = NULL;
	
}



/* ------------------- PRIVATE STATIC FUNCTIONS ----------------------- */

/* ARGSUSED */
STATIC void
xfs_qm_list_init(
	xfs_dqlist_t 	*list,
	char 		*str,
	int 		n)
{
	mutex_init(&list->qh_lock, MUTEX_DEFAULT, str); 
	list->qh_next = NULL;
	list->qh_version = 0;
	list->qh_nelems = 0;
}	

STATIC void
xfs_qm_list_destroy(
	xfs_dqlist_t 	*list)
{
	mutex_destroy(&(list->qh_lock));
}


/*
 * Stripped down version of dqattach. This doesn't attach, or even look at the
 * dquots attached to the inode. The rationale is that there won't be any 
 * attached at the time this is called from quotacheck.
 */
STATIC int
xfs_qm_dqget_noattach(
	xfs_inode_t	*ip,
	xfs_dquot_t	**O_udqpp,
	xfs_dquot_t	**O_pdqpp)
{
	int		error;
	xfs_mount_t 	*mp;
	xfs_dquot_t	*udqp, *pdqp;

	ASSERT(XFS_ISLOCKED_INODE_EXCL(ip));
	mp = ip->i_mount;
	udqp = NULL;
	pdqp = NULL;

	if (XFS_IS_UQUOTA_ON(mp)) { 
		if (error = xfs_qm_dqget(mp, ip, ip->i_d.di_uid,
					 XFS_DQ_USER, 
					 XFS_QMOPT_DQALLOC, &udqp)) {
			/*
			 * Shouldn't be able to turn off quotas here.
			 */
			ASSERT(error != ESRCH);
			ASSERT(error != ENOENT);
			xfs_qm_force_quotaoff(mp);
			return XFS_ERROR(error);
		}
		ASSERT(udqp);
	}
	
	if (XFS_IS_PQUOTA_ON(mp)) {
		if (udqp)
			xfs_dqunlock(udqp);
		if (error = xfs_qm_dqget(mp, ip, ip->i_d.di_projid,
					 XFS_DQ_PROJ, 
					 XFS_QMOPT_DQALLOC, &pdqp)) {
			if (udqp)
				xfs_qm_dqrele(udqp);
			ASSERT(error != ESRCH);
			ASSERT(error != ENOENT);
			xfs_qm_force_quotaoff(mp);
			return XFS_ERROR(error);
		}
		ASSERT(pdqp);

		/* Reacquire the locks in the right order */
		if (udqp) {
			if (! xfs_qm_dqlock_nowait(udqp)) {
				xfs_dqunlock(pdqp);
				xfs_dqlock(udqp);	
				xfs_dqlock(pdqp);
			}
		}
	}

	*O_udqpp = udqp;
	*O_pdqpp = pdqp;

#ifdef QUOTADEBUG
	if (udqp) ASSERT(XFS_DQ_IS_LOCKED(udqp));
	if (pdqp) ASSERT(XFS_DQ_IS_LOCKED(pdqp));
#endif
	return (0);
	
}

/*
 * Create an inode and return with a reference already taken, but unlocked
 * This is how we create quota inodes
 */
STATIC int
xfs_qm_qino_alloc(
	xfs_mount_t 	*mp, 
	xfs_inode_t	**ip,
	__int64_t	sbfields,
	uint		flags)
{
	xfs_trans_t 	*tp;
	int		error, s;
	cred_t		zerocr;
	int		committed;
	/* extern 		prid_t	dfltprid; */

	tp = xfs_trans_alloc(mp,XFS_TRANS_QM_QINOCREATE);
	if (error = xfs_trans_reserve(tp,
				      XFS_IALLOC_BLOCKS(mp) +
				      XFS_IN_MAXLEVELS(mp) +
				      XFS_BM_MAXLEVELS(mp, XFS_DATA_FORK) +
				      mp->m_sb.sb_sectsize + 128,
				      XFS_CREATE_LOG_RES(mp), 0,
				      XFS_TRANS_PERM_LOG_RES,
				      XFS_CREATE_LOG_COUNT)) {
		xfs_trans_cancel(tp, 0);
		return (error);
	}
	bzero(&zerocr, sizeof(zerocr));

	/* XXX should the projid be 0 or dfltprid here ? */
	if (error = xfs_dir_ialloc(&tp, mp->m_rootip, IFREG, 1, mp->m_dev,
				   &zerocr, (xfs_prid_t) 0, ip, 
				   &committed)) {
		xfs_trans_cancel(tp, XFS_TRANS_RELEASE_LOG_RES | 
				 XFS_TRANS_ABORT);
		return (error);
	}
	
	/*
	 * Keep an extra reference to this quota inode. This inode is
	 * locked exclusively and joined to the transaction already.
	 */
	ASSERT(XFS_ISLOCKED_INODE_EXCL(*ip));
	VN_HOLD(XFS_ITOV((*ip)));
	
	/*
	 * Make the changes in the superblock, and log those too.
	 * sbfields arg may contain fields other than *QUOTINO;
	 * VERSIONNUM for example.
	 */
	s = XFS_SB_LOCK(mp);
	if (flags & XFS_QMOPT_SBVERSION) {
#ifdef DEBUG
		unsigned oldv = mp->m_sb.sb_versionnum;
#endif
		ASSERT(!XFS_SB_VERSION_HASQUOTA(&mp->m_sb));
		ASSERT((sbfields & (XFS_SB_VERSIONNUM | XFS_SB_UQUOTINO |
				   XFS_SB_PQUOTINO | XFS_SB_QFLAGS)) ==
		       (XFS_SB_VERSIONNUM | XFS_SB_UQUOTINO |
			XFS_SB_PQUOTINO | XFS_SB_QFLAGS));

		XFS_SB_VERSION_ADDQUOTA(&mp->m_sb);
		mp->m_sb.sb_uquotino = NULLFSINO;
		mp->m_sb.sb_pquotino = NULLFSINO;

		/* qflags will get updated _after_ quotacheck */
		mp->m_sb.sb_qflags = 0;
#ifdef DEBUG
		cmn_err(CE_NOTE, 
			"Old superblock version %x, converting to %x.",
			oldv, mp->m_sb.sb_versionnum);
#endif
	}
	if (flags & XFS_QMOPT_UQUOTA)
		mp->m_sb.sb_uquotino = (*ip)->i_ino;
	else
		mp->m_sb.sb_pquotino = (*ip)->i_ino;
	XFS_SB_UNLOCK(mp, s);
	xfs_mod_sb(tp, sbfields);

	if (error = xfs_trans_commit(tp, XFS_TRANS_RELEASE_LOG_RES)) {
		cmn_err(CE_ALERT,
			"XFS qino_alloc failed!"
			);
		return (error);
	}
	return (0);
}


STATIC int
xfs_qm_reset_dqcounts(
	xfs_mount_t	*mp,
	buf_t		*bp)
{
	xfs_disk_dquot_t	*ddq;
	int			j;

	/* 
	 * Reset all counters and timers. They'll be
	 * started afresh by xfs_qm_quotacheck. 
	 */
	ASSERT(XFS_QM_DQPERBLK(mp) == 
	       XFS_FSB_TO_B(mp, XFS_DQUOT_CLUSTER_SIZE_FSB) /
				      sizeof(xfs_dqblk_t));
	ddq = (xfs_disk_dquot_t *)bp->b_un.b_addr;
	for (j = 0; j < XFS_QM_DQPERBLK(mp); j++) {
		if (xfs_qm_dqcheck(ddq, -1, "xfs_qm_reset_dqcounts")) {
			return XFS_ERROR(EIO);
		}
		ddq->d_bcount = 0ULL;
		ddq->d_icount = 0ULL;
		ddq->d_rtbcount = 0ULL;
		ddq->d_btimer = (time_t)0;
		ddq->d_itimer = (time_t)0;
		ddq->d_bwarns = 0UL;
		ddq->d_iwarns = 0UL;
		ddq = (xfs_disk_dquot_t *) ((xfs_dqblk_t *)ddq + 1);
	} 
	return (0);
}
			
STATIC int
xfs_qm_dqiter_bufs(
	xfs_mount_t	*mp,
	xfs_fsblock_t	bno,   
	xfs_filblks_t	blkcnt,
	xfs_dqbuf_iter	iterfunc,	   
	uint		flags)
{
	buf_t		*bp;	
	int		error;
	int		notcommitted;
	int		incr;
	xfs_trans_t	*tp;
	
	ASSERT(blkcnt > 0);
	notcommitted = 0;
	incr = (blkcnt > XFS_QM_MAX_DQCLUSTER_LOGSZ) ? 
		XFS_QM_MAX_DQCLUSTER_LOGSZ : blkcnt;
	tp = NULL;

	/*
	 * Blkcnt arg can be a very big number, and might even be
	 * larger than the log itself. So, we have to break it up into
	 * manageable-sized transactions.
	 * Note that we don't start a permanent transaction here; we might
	 * not be able to get a log reservation for the whole thing up front,
	 * and we don't really care to either, because we just discard 
	 * everything if we were to crash in the middle of this loop.
	 */
	while (blkcnt--) {
		bp = NULL;
		if ((flags & XFS_QMOPT_DOLOG) && 
		    tp == NULL) {
			tp = xfs_trans_alloc(mp, XFS_TRANS_QM_DQCLUSTER);
			/* Cannot fail here - no block reservation */
			xfs_trans_reserve(tp, 0, 
					  XFS_FSB_TO_B(mp, incr) + 128,
					  0, 0,	
					  XFS_DEFAULT_LOG_COUNT);
		}

		error = xfs_trans_read_buf(tp, mp->m_dev,
					   XFS_FSB_TO_DADDR(mp, bno),
					   (int)mp->QI_DQCHUNKLEN,
					   0, &bp);
		
		if (error || !bp) 
			break;
		
		/*
		 * Send a buffer full of dquots to the supplied
		 * routine.
		 */
		if (iterfunc) {
			error = (*iterfunc)(mp, bp);
			if (error) {
				xfs_trans_brelse(tp, bp);
				break;
			}
		}
		if (tp) {
			/*
			 * Tag this as a cluster of dquots. 
			 * (We don't have to distinguish between
			 * newly allocated and old dquot clusters
			 * as was done in inode clusters).
			 * Then log it.
			 */
			xfs_trans_dquot_buf(tp, bp, 
					    flags & XFS_QMOPT_UQUOTA ?
					    XFS_BLI_UDQUOT_BUF :
					    XFS_BLI_PDQUOT_BUF);
			xfs_trans_log_buf(tp, bp, 0, 
					  BBTOB(mp->QI_DQCHUNKLEN) - 1);

			if (++notcommitted == incr) {
				notcommitted = 0;
				xfs_trans_commit(tp, 0);
				tp = NULL;
			}
					
		} else {
			/*
			 * When we are doing the first phase of
			 * the quotacheck, we don't care about
			 * logging this buffer because if we 
			 * crash, we'll have to do this again 
			 * anyway. A delayed write is great 
			 * for this because hopefully it'll 
			 * accumulate all our (quotacheck) dquot
			 * updates and write them together.
			 */
			bdwrite(bp);
		}
		/*
		 * goto the next block.
		 */
		bno++;
		
	}
	if (tp) {
		if (!error)
			xfs_trans_commit(tp, 0);
		else 
			xfs_trans_cancel(tp, XFS_TRANS_RELEASE_LOG_RES);
	}

	return (0);
}

/*
 * Iterate over all allocated USR/PRJ dquots in the system, calling a 
 * caller supplied function for every chunk of dquots that we find.
 */
STATIC int
xfs_qm_dqiterate(
	xfs_mount_t	*mp, 
	xfs_inode_t	*qip,
	uint		flags,
	xfs_dqbuf_iter	iterfunc)
{
        xfs_bmbt_irec_t         map[XFS_DQITER_MAP_SIZE];          
        int                     i, nmap;    	/* number of map entries */
	xfs_fsblock_t           firstblock;     /* start block for bmapi */
	int                     error;          /* return value */
	__int64_t               len, end, off, newoff, outlen;
	xfs_dfiloff_t		lblkno;
	xfs_filblks_t		lblkcnt, nblks;
	
	error = 0;

	/*
	 * This looks racey, but we can't keep an inode lock across a 
	 * trans_reserve. But, this gets called during quotacheck, and that
	 * happens only at mount time which is single threaded. We take the
	 * ilock here because di_nblocks is a 64-bit number, but even that
	 * isn't necessary.
	 */
	xfs_ilock(qip, XFS_ILOCK_SHARED);
	nblks = (xfs_filblks_t)qip->i_d.di_nblocks;
	xfs_iunlock(qip, XFS_ILOCK_SHARED);
	if (nblks == 0)
		return (0);

	lblkno = 0;
	lblkcnt = XFS_B_TO_FSB(mp, (xfs_ufsize_t)XFS_MAX_FILE_OFFSET);
	len = XFS_FSB_TO_BB(mp, lblkcnt);
	if (nblks > 500) {
		cmn_err(CE_NOTE, 
			"XFS quotacheck: Iterating over quota entries in 0x%x BBs",
			XFS_FSB_TO_BB(mp, nblks));
	}
	do {
		firstblock = NULLFSBLOCK;
		nmap = XFS_DQITER_MAP_SIZE;
		/*
		 * We aren't changing the inode itself. Just changing
		 * some of its data. No new blocks are added here, and
		 * the inode is never added to the transaction.
		 */
		xfs_ilock(qip, XFS_IOLOCK_SHARED);
		xfs_ilock(qip, XFS_ILOCK_SHARED);
		error = xfs_bmapi(NULL, qip, (xfs_fileoff_t) lblkno,
				  lblkcnt, 
				  XFS_BMAPI_ENTIRE|XFS_BMAPI_METADATA,
				  &firstblock,
				  0, map, &nmap, NULL);
		xfs_iunlock(qip, XFS_ILOCK_SHARED);
		xfs_iunlock(qip, XFS_IOLOCK_SHARED);
		if (error)
                       break;
		if (nmap == 0)
			continue;

		ASSERT(nmap <= XFS_DQITER_MAP_SIZE);
		
		end = len;
		/* printf("nmaps %d\n", nmap); */
		for (error = i = 0; i < nmap && len; i++) {
			ASSERT(map[i].br_startblock != DELAYSTARTBLOCK);
			if (map[i].br_blockcount == 0)
				continue;

			lblkno += map[i].br_blockcount;
			lblkcnt -= map[i].br_blockcount;
			if (map[i].br_startblock == HOLESTARTBLOCK)
				continue;
			
			off = XFS_FSB_TO_BB(mp, map[i].br_startoff);
			outlen = XFS_FSB_TO_BB(mp, map[i].br_blockcount);
			if (error = xfs_qm_dqiter_bufs(mp, 
						       map[i].br_startblock, 
						       map[i].br_blockcount,
						       iterfunc,
						       flags))
				break;
				
			newoff = off + outlen;
			len = MAX(0, end - newoff);
			nblks -= map[i].br_blockcount;
		}

		if (error) 
			break;
	} while (len && nmap && nblks > 0); 

        return (error);
}

/*
 * Called by dqusage_adjust in doing a quotacheck.
 * Given the inode, and a dquot (either USR or PRJ, doesn't matter),
 * this updates its incore copy as well as the buffer copy. This is
 * so that once the quotacheck is done, we can just log all the buffers, 
 * as opposed to logging numerous updates to individual dquots.
 */
STATIC void
xfs_qm_quotacheck_dqadjust(
	xfs_dquot_t		*dqp,
	xfs_qcnt_t		nblks,
	xfs_qcnt_t		rtblks)
{
	ASSERT(XFS_DQ_IS_LOCKED(dqp));	
	xfs_dqtrace_entry(dqp, "QCHECK DQADJUST");
	/*
	 * Adjust the inode count and the block count to reflect this inode's
	 * resource usage.
	 */
	dqp->q_core.d_icount++;
	dqp->q_res_icount++;
	if (nblks) {
		dqp->q_core.d_bcount += nblks;
		dqp->q_res_bcount += nblks;
	}
	if (rtblks) {
		dqp->q_core.d_rtbcount += rtblks;
		dqp->q_res_rtbcount += rtblks;
	}

	/*
	 * Adjust the timers since we just changed usages
	 */
	if (! XFS_IS_SUSER_DQUOT(dqp))
		xfs_qm_adjust_dqtimers(dqp->q_mount, &dqp->q_core);

	dqp->dq_flags |= XFS_DQ_DIRTY; 
}

STATIC int
xfs_qm_get_rtblks(
	xfs_inode_t	*ip,
	xfs_qcnt_t	*O_rtblks)
{
	xfs_filblks_t	rtblks;			/* total rt blks */
	xfs_ifork_t	*ifp;			/* inode fork pointer */
	xfs_extnum_t	nextents;		/* number of extent entries */
	xfs_bmbt_rec_t	*base;			/* base of extent array */
	xfs_bmbt_rec_t	*ep;			/* pointer to an extent entry */
	int		error;

	ASSERT(XFS_IS_REALTIME_INODE(ip));
	ifp = XFS_IFORK_PTR(ip, XFS_DATA_FORK);
	if (!(ifp->if_flags & XFS_IFEXTENTS)) {
		if (error = xfs_iread_extents(NULL, ip, XFS_DATA_FORK))
			return (error);
	}
	rtblks = 0;
	nextents = ifp->if_bytes / sizeof(xfs_bmbt_rec_t);
	base = &ifp->if_u1.if_extents[0];
	for (ep = base; ep < &base[nextents]; ep++)
		rtblks += xfs_bmbt_get_blockcount(ep);
	*O_rtblks = (xfs_qcnt_t)rtblks;
	return (0);
}

/*
 * callback routine supplied to bulkstat(). Given an inumber, find its
 * dquots and update them to account for resources taken by that inode.
 */
/* ARGSUSED */
STATIC int
xfs_qm_dqusage_adjust(
	xfs_mount_t     *mp,            /* mount point for filesystem */
        xfs_trans_t     *tp,            /* transaction pointer - NULL */
        xfs_ino_t       ino,            /* inode number to get data for */
        void            *buffer,        /* not used */
        daddr_t         bno)            /* starting block of inode cluster */
{
	xfs_inode_t     *ip;
	xfs_dquot_t	*udqp, *pdqp;
	xfs_qcnt_t	nblks, rtblks;

	ASSERT(XFS_IS_QUOTA_RUNNING(mp));

	/* 
	 * rootino must have its resources accounted for, not so with the quota
	 * inodes. How about the rt inodes ??? XXX
	 */
	if (ino == mp->m_sb.sb_rbmino || ino == mp->m_sb.sb_rsumino ||
	    ino == mp->m_sb.sb_uquotino || ino == mp->m_sb.sb_pquotino)
                return (0);

	/*
	 * We don't _need_ to take the ilock EXCL. However, the xfs_qm_dqget
	 * interface expects the inode to be exclusively locked because that's 
	 * the case in all other instances. It's OK that we do this because
	 * quotacheck is done only at mount time.
	 */
        if (xfs_iget(mp, tp, ino, XFS_ILOCK_EXCL, &ip, bno))
                return (0);

        if (ip->i_d.di_mode == 0) {
                xfs_iput(ip, XFS_ILOCK_EXCL);
                return (0);
        }

	/*
	 * Obtain the locked dquots.
	 */
	if (xfs_qm_dqget_noattach(ip, &udqp, &pdqp)) {
		xfs_iput(ip, XFS_ILOCK_EXCL);
		return (0);
	}

#ifdef  _IRIX62_XFS_ONLY
	ASSERT(! XFS_IS_REALTIME_INODE(ip));
#endif
	rtblks = 0;
	if (! XFS_IS_REALTIME_INODE(ip)) {
		nblks = (xfs_qcnt_t)ip->i_d.di_nblocks;
	} else {
		/*
		 * Walk thru the extent list and count the realtime blocks.
		 */
		if (xfs_qm_get_rtblks(ip, &rtblks)) {
			xfs_iput(ip, XFS_ILOCK_EXCL);
			if (udqp)
				xfs_qm_dqput(udqp);
			if (pdqp)
				xfs_qm_dqput(pdqp);
			return (0);
		}
		nblks = (xfs_qcnt_t)ip->i_d.di_nblocks - rtblks;
	}
	ASSERT(ip->i_delayed_blks == 0);

	/*
	 * We can't release the inode while holding its dquot locks.
	 * The inode can go into inactive and might try to acquire the dquotlocks.
	 * So, just unlock here and do a vn_rele at the end.
	 */
	xfs_iunlock(ip, XFS_ILOCK_EXCL);

	/* 
	 * Add the (disk blocks and inode) resources occupied by this
	 * inode to its dquots. We do this adjustment in the incore dquot,
	 * and also copy the changes to its buffer. 
	 * We don't care about putting these changes in a transaction
	 * envelope because if we crash in the middle of a 'quotacheck'
	 * we have to start from the beginning anyway. 
	 * Once we're done, we'll log all the dquot bufs.
	 * 
	 * The *QUOTA_ON checks below may look pretty racey, but quotachecks
	 * and quotaoffs don't race. (Quotachecks happen at mount time only).
	 */
	if (XFS_IS_UQUOTA_ON(mp)) {
		ASSERT(udqp);
		xfs_qm_quotacheck_dqadjust(udqp, nblks, rtblks);
		xfs_qm_dqput(udqp);
	}
	if (XFS_IS_PQUOTA_ON(mp)) {
		ASSERT(pdqp);
		xfs_qm_quotacheck_dqadjust(pdqp, nblks, rtblks);
		xfs_qm_dqput(pdqp);
	}
	/*
	 * Now release the inode. This will send it to 'inactive', and
	 * possibly even free blocks.
	 */
	VN_RELE(XFS_ITOV(ip));

	/*
	 * No point in returning error values to bulkstat(), the caller,
	 * in our case. There isn't a way to propagate them.
	 */
	return (1);
}


STATIC int
xfs_qm_quotacheck(
	xfs_mount_t	*mp)
{
	int 		done, count, error;
	ino64_t 	lastino;
	size_t		structsz;
	xfs_inode_t	*uip, *pip;
	uint		flags;

	count = INT_MAX;
	structsz = 1;
	lastino = 0;
	flags = 0;

	ASSERT(mp->QI_UQIP || mp->QI_PQIP);
	ASSERT(XFS_IS_QUOTA_RUNNING(mp));

	/*
	 * There should be no cached dquots. The (simplistic) quotacheck
	 * algorithm doesn't like that.
	 */
	ASSERT(mp->QI_MPLNDQUOTS == 0);

	/*
	 * First we go thru all the dquots on disk, USR and PRJ, and reset
	 * their counters to zero. We need a clean slate.
	 * We don't log our changes till later.
	 */
	if (uip = mp->QI_UQIP) {
		if (error = xfs_qm_dqiterate(mp, uip, XFS_QMOPT_UQUOTA,
					     xfs_qm_reset_dqcounts))
			return (error);
	}
	
	if (pip = mp->QI_PQIP) {
		if (error = xfs_qm_dqiterate(mp, pip, XFS_QMOPT_PQUOTA,
					     xfs_qm_reset_dqcounts))
			return (error);
	}
		
	do {
		/*
		 * Iterate thru all the inodes in the file system,
		 * adjusting the corresponding dquot counters in core.
		 */
		if (error = xfs_bulkstat(mp, NULL, &lastino, &count, 
				     xfs_qm_dqusage_adjust,
				     structsz, NULL, &done))
			break;
	} while (! done);

	/*
	 * We've made all the changes that we need to make incore. Now flush_them
	 * down to disk buffers.
	 */
	xfs_qm_dqflush_all(mp, XFS_QMOPT_DELWRI);

	/*
	 * We didn't log anything, because if we crashed, we'll have to
	 * start the quotacheck from scratch anyway. However, we must make
	 * sure that our dquot changes are secure before we put the
	 * quotacheck'd stamp on the superblock.
	 * So, here we iterate thru all the dquot clusters and log them.
	 * It isn't sufficient to just log the individual incore dquots because
	 * at the beginning we went ahead and zeroed all the counter values of all
	 * dquots right on the disk buffers, so we need to make sure that those 
	 * somehow are permanent before we stamp the file system q'checked.
	 */
	if (uip) {
		if (error = xfs_qm_dqiterate(mp, uip, 
					     XFS_QMOPT_UQUOTA | XFS_QMOPT_DOLOG,
					     NULL))
			return (error);
		flags |= XFS_UQUOTA_CHKD;
	}
	
	if (pip) {
		if (error = xfs_qm_dqiterate(mp, pip, 
					     XFS_QMOPT_PQUOTA | XFS_QMOPT_DOLOG,
					     NULL))
			return (error);
		flags |= XFS_PQUOTA_CHKD;
	}
	
	/*
	 * If one type of quotas is off, then it will lose its
	 * quotachecked status, since we won't be doing accounting for
	 * that type anymore.
	 */	
	mp->m_qflags &= ~(XFS_PQUOTA_CHKD | XFS_UQUOTA_CHKD);
	mp->m_qflags |= flags;

#ifdef QUOTADEBUG
	XQM_LIST_PRINT(&(mp->QI_MPL_LIST), MPL_NEXT, "++++ Mp list +++"); 
#endif
	return (error);
}

/*
 * This is called after the superblock has been read in and we're ready to
 * iget the quota inodes.
 */
STATIC int
xfs_qm_init_quotainos(
	xfs_mount_t	*mp)
{
	xfs_inode_t	*uip, *pip;
	int		error;
	__int64_t	sbflags;
	uint		flags;

	ASSERT(mp->m_quotainfo);
	uip = pip = NULL;
	sbflags = 0;
	flags = 0;

	/*
	 * Get the uquota and pquota inodes
	 */
	if (XFS_SB_VERSION_HASQUOTA(&mp->m_sb)) {
		if (XFS_IS_UQUOTA_ON(mp) &&
		    mp->m_sb.sb_uquotino != NULLFSINO) {
			ASSERT(mp->m_sb.sb_uquotino > 0);
			if (error = xfs_iget(mp, NULL, mp->m_sb.sb_uquotino, 
					     0, &uip, 0))
				return XFS_ERROR(error);
		}
		if (XFS_IS_PQUOTA_ON(mp) && 
		    mp->m_sb.sb_pquotino != NULLFSINO) {
			ASSERT(mp->m_sb.sb_pquotino > 0);
			if (error = xfs_iget(mp, NULL, mp->m_sb.sb_pquotino,
					     0, &pip, 0)) {
				if (uip)
					VN_RELE(XFS_ITOV(uip));
				return XFS_ERROR(error);
			}
		}
	} else {
		flags |= XFS_QMOPT_SBVERSION;
		sbflags |= (XFS_SB_VERSIONNUM | XFS_SB_UQUOTINO |
			    XFS_SB_PQUOTINO | XFS_SB_QFLAGS);
	}
	
	/*
	 * Create the two inodes, if they don't exist already. The changes
	 * made above will get added to a transaction and logged in one of
	 * the qino_alloc calls below.
	 */
	if (XFS_IS_UQUOTA_ON(mp) && uip == NULL) {
		if (error = xfs_qm_qino_alloc(mp, &uip,
					      sbflags | XFS_SB_UQUOTINO,
					      flags | XFS_QMOPT_UQUOTA))
			return XFS_ERROR(error);
	}
	if (XFS_IS_PQUOTA_ON(mp) && pip == NULL) {
		if (error = xfs_qm_qino_alloc(mp, &pip,
					      sbflags | XFS_SB_PQUOTINO,
					      flags | XFS_QMOPT_PQUOTA)) {
			if (uip)
				VN_RELE(XFS_ITOV(uip));
			
			return XFS_ERROR(error);
		}
	}
	
	mp->QI_UQIP = uip;
	mp->QI_PQIP = pip;

	return (0);
}


/*
 * Traverse the freelist of dquots and attempt to reclaim a maximum of
 * 'howmany' dquots. This operation races with dqlookup(), and attempts to
 * favor the lookup function ...
 * XXXsup merge this with qm_reclaim_one().
 */
STATIC int
xfs_qm_shake_freelist(
	int howmany)
{
	int		nreclaimed;
	xfs_dqhash_t	*hash;
	xfs_dquot_t	*dqp, *nextdqp;
	int 		restarts;

	if (howmany <= 0)
		return (0);

	nreclaimed = 0; 
	restarts = 0;

	/* lock order is : hashchainlock, freelistlock, mplistlock */
 tryagain:
	xfs_qm_freelist_lock(G_xqm);

	for (dqp = G_xqm->QM_FLNEXT;
	     ((dqp != (xfs_dquot_t *) &G_xqm->qm_dqfreelist) &&
	      nreclaimed < howmany); ) {
		xfs_dqlock(dqp); 

		/*
		 * We are racing with dqlookup here. Naturally we don't
		 * want to reclaim a dquot that lookup wants.
		 */
		if (dqp->dq_flags & XFS_DQ_WANT) {
			xfs_dqunlock(dqp);
			xfs_qm_freelist_unlock(G_xqm);
			if (++restarts >= XFS_QM_RECLAIM_MAX_RESTARTS) 
				return (nreclaimed != howmany);
#ifndef _BANYAN_XFS
			XFSSTATS.xs_qm_dqwants++;
#endif
			goto tryagain;
		}
		
		/*
		 * If the dquot is inactive, we are assured that it is
		 * not on the mplist or the hashlist, and that makes our
		 * life easier.
		 */
		if (dqp->dq_flags & XFS_DQ_INACTIVE) {
			ASSERT(dqp->q_mount == NULL);
			ASSERT(! XFS_DQ_IS_DIRTY(dqp));
			ASSERT(dqp->HL_PREVP == NULL);
			ASSERT(dqp->MPL_PREVP == NULL);
			ASSERT(dqp->dq_flags == 0);
#ifndef _BANYAN_XFS
			XFSSTATS.xs_qm_dqinact_reclaims++;
#endif
			goto off_freelist;
		}

		ASSERT(dqp->MPL_PREVP);
		/* 
		 * Try to grab the flush lock. If this dquot is in the process of
		 * getting flushed to disk, we don't want to reclaim it.
		 */
		if (! xfs_qm_dqflock_nowait(dqp)) {
			xfs_dqunlock(dqp);	
			dqp = dqp->dq_flnext;
			continue;
		}
			
		/*
		 * We have the flush lock so we know that this is not in the
		 * process of being flushed. So, if this is dirty, flush it
		 * DELWRI so that we don't get a freelist infested with
		 * dirty dquots.
		 */
		if (XFS_DQ_IS_DIRTY(dqp)) {
			xfs_dqtrace_entry(dqp, "DQSHAKE: DQDIRTY");
			/*
			 * We flush it delayed write, so don't bother
			 * releasing the mplock.
			 */
			(void) xfs_qm_dqflush(dqp, XFS_QMOPT_DELWRI);
			xfs_dqunlock(dqp); /* dqflush unlocks dqflock */
			dqp = dqp->dq_flnext;
			continue;
		}
		/* 
		 * We're trying to get the hashlock out of order. This races 
		 * with dqlookup; so, we giveup and goto the next dquot if
		 * we couldn't get the hashlock. This way, we won't starve
		 * a dqlookup process that holds the hashlock that is
		 * waiting for the freelist lock.
		 */
		if (! xfs_qm_dqhashlock_nowait(dqp)) {
			xfs_dqfunlock(dqp);
			xfs_dqunlock(dqp);
			dqp = dqp->dq_flnext;
			continue;
		}
		/* 
		 * This races with dquot allocation code as well as dqflush_all 
		 * and reclaim code. So, if we failed to grab the mplist lock, 
		 * giveup everything and start over.
		 */
		hash = dqp->q_hash;
		ASSERT(hash);
		if (! xfs_qm_mplist_nowait(dqp->q_mount)) {
			/* XXX put a sentinel so that we can come back here */
			xfs_dqfunlock(dqp);
			xfs_dqunlock(dqp);
			XFS_DQ_HASH_UNLOCK(hash);
			xfs_qm_freelist_unlock(G_xqm);
			if (++restarts >= XFS_QM_RECLAIM_MAX_RESTARTS) 
				return (nreclaimed != howmany);
			goto tryagain;
		}
		xfs_dqtrace_entry(dqp, "DQSHAKE: UNLINKING");
		ASSERT(dqp->q_nrefs == 0);
		nextdqp = dqp->dq_flnext;
		XQM_MPLIST_REMOVE(&(dqp->q_mount->QI_MPL_LIST), dqp);
		XQM_HASHLIST_REMOVE(hash, dqp);
		xfs_dqfunlock(dqp);
		xfs_qm_mplist_unlock(dqp->q_mount);
		XFS_DQ_HASH_UNLOCK(hash);

 off_freelist:		
		XQM_FREELIST_REMOVE(dqp);
		xfs_dqunlock(dqp);
		nreclaimed++;
#ifndef _BANYAN_XFS
		XFSSTATS.xs_qm_dqshake_reclaims++;
#endif
		xfs_qm_dqdestroy(dqp);
		dqp = nextdqp;
	}
	xfs_qm_freelist_unlock(G_xqm);
	return (nreclaimed != howmany);

}


/*
 * The shake manager routine called by shaked() when memory is
 * running low. The heuristics here are copied from vn_shake().
 * We are a lot more aggressive than vn_shake though.
 */
/* ARGSUSED */
STATIC int
xfs_qm_shake(int level)
{
	int ndqused, nfree, n;
	
	if (G_xqm == NULL)
		return (0);

	nfree = G_xqm->qm_dqfreelist.qh_nelems; /* free dquots */
	ndqused = G_xqm->qm_totaldquots - nfree;/* incore dquots in all f/s's */
	
	ASSERT(ndqused >= 0);
	
	if (nfree <= ndqused && nfree < XFS_QM_NDQUOT_HIWAT)
		return 0;

	ndqused *= G_xqm->qm_dqfree_ratio;	/* target # of free dquots */
	n = nfree - ndqused - XFS_QM_NDQUOT_HIWAT;	/* # over target */
	
	return xfs_qm_shake_freelist(MAX(nfree, n));
}


/*
 * Just pop the least recently used dquot off the freelist and
 * recycle it. The returned dquot is locked.
 */
STATIC xfs_dquot_t *
xfs_qm_dqreclaim_one(void)
{
	xfs_dquot_t 	*dqpout;
	xfs_dquot_t	*dqp;
	int		restarts;
	
	restarts = 0;
	dqpout = NULL;

	/* lockorder: hashchainlock, freelistlock, mplistlock, dqlock, dqflock */
 startagain:
	xfs_qm_freelist_lock(G_xqm);

	FOREACH_DQUOT_IN_FREELIST(dqp, &(G_xqm->qm_dqfreelist)) {
		xfs_dqlock(dqp); 

		/*
		 * We are racing with dqlookup here. Naturally we don't
		 * want to reclaim a dquot that lookup wants. We release the
		 * freelist lock and start over, so that lookup will grab 
		 * both the dquot and the freelistlock.
		 */
		if (dqp->dq_flags & XFS_DQ_WANT) {
			ASSERT(! (dqp->dq_flags & XFS_DQ_INACTIVE));
			xfs_dqtrace_entry(dqp, "DQRECLAIM: DQWANT");
			xfs_dqunlock(dqp);
			xfs_qm_freelist_unlock(G_xqm);
			if (++restarts >= XFS_QM_RECLAIM_MAX_RESTARTS) 
				return (NULL);
#ifndef _BANYAN_XFS
			XFSSTATS.xs_qm_dqwants++;
#endif
			goto startagain;
		}

		/*
		 * If the dquot is inactive, we are assured that it is
		 * not on the mplist or the hashlist, and that makes our
		 * life easier.
		 */
		if (dqp->dq_flags & XFS_DQ_INACTIVE) {
			ASSERT(dqp->q_mount == NULL);
			ASSERT(! XFS_DQ_IS_DIRTY(dqp));
			ASSERT(dqp->HL_PREVP == NULL);
			ASSERT(dqp->MPL_PREVP == NULL);
			XQM_FREELIST_REMOVE(dqp);
			xfs_dqunlock(dqp);
			dqpout = dqp;
#ifndef _BANYAN_XFS
			XFSSTATS.xs_qm_dqinact_reclaims++;
#endif
			break;
		}

		ASSERT(dqp->q_hash);
		ASSERT(dqp->MPL_PREVP);

		/* 
		 * Try to grab the flush lock. If this dquot is in the process of
		 * getting flushed to disk, we don't want to reclaim it.
		 */
		if (! xfs_qm_dqflock_nowait(dqp)) {
			xfs_dqunlock(dqp);	
			continue;
		}

		/*
		 * We have the flush lock so we know that this is not in the
		 * process of being flushed. So, if this is dirty, flush it
		 * DELWRI so that we don't get a freelist infested with
		 * dirty dquots.
		 */
		if (XFS_DQ_IS_DIRTY(dqp)) {
			xfs_dqtrace_entry(dqp, "DQRECLAIM: DQDIRTY");
			/*
			 * We flush it delayed write, so don't bother
			 * releasing the mplock.
			 */
			(void) xfs_qm_dqflush(dqp, XFS_QMOPT_DELWRI);
			xfs_dqunlock(dqp); /* dqflush unlocks dqflock */
			continue;
		}
		
		if (! xfs_qm_mplist_nowait(dqp->q_mount)) {
			xfs_dqfunlock(dqp);
			xfs_dqunlock(dqp);	
			continue;
		}

		if (! xfs_qm_dqhashlock_nowait(dqp))
			goto mplistunlock;

		ASSERT(dqp->q_nrefs == 0);
		xfs_dqtrace_entry(dqp, "DQRECLAIM: UNLINKING");
		XQM_MPLIST_REMOVE(&(dqp->q_mount->QI_MPL_LIST), dqp);
		XQM_HASHLIST_REMOVE(dqp->q_hash, dqp);
		XQM_FREELIST_REMOVE(dqp);
		dqpout = dqp;
		XFS_DQ_HASH_UNLOCK(dqp->q_hash);
 mplistunlock:
		xfs_qm_mplist_unlock(dqp->q_mount);
		xfs_dqfunlock(dqp);
		xfs_dqunlock(dqp);		
		if (dqpout)
			break;
	}
	
	xfs_qm_freelist_unlock(G_xqm);
	return (dqpout);
}

/*
 * In case of errors like disk failures, we disable quotas without
 * the user asking us to do so, and try to continue. The attempt is to
 * not halt the entire system because of quota problems.
 */
void
xfs_qm_force_quotaoff(
	xfs_mount_t	*mp)
{
	cmn_err(CE_WARN, 
		"Quota Error: disabling quotas :%s\n",
		mp->m_fsname);
	/*
	 * Caller must assure us that we're not in the middle of a
	 * transaction. We just call the regular quotaoff routine
	 * with the FORCE option.
	 */
	(void) xfs_qm_scall_quotaoff(mp, mp->m_qflags, B_TRUE);
}


/*------------------------------------------------------------------*/

/*
 * Return a new incore dquot. Depending on the number of
 * dquots in the system, we either allocate a new one on the kernel heap,
 * or reclaim a free one.
 * Return value is B_TRUE if we allocated a new dquot, B_FALSE if we managed
 * to reclaim an existing one from the freelist.
 */
boolean_t
xfs_qm_dqalloc_incore(
	xfs_dquot_t **O_dqpp)
{
	xfs_dquot_t 	*dqp;

	/* 
	 * Check against high water mark to see if we want to pop
	 * a nincompoop dquot off the freelist.
	 */
	if (G_xqm->qm_totaldquots >= XFS_QM_NDQUOT_HIWAT) {
		/*
		 * Try to recycle a dquot from the freelist.
		 */
		if (dqp = xfs_qm_dqreclaim_one()) {
#ifndef _BANYAN_XFS
			XFSSTATS.xs_qm_dqreclaims++;
#endif
			/*
			 * Just bzero the core here. The rest will get
			 * reinitialized by caller. XXX we shouldn't even
			 * do this bzero ...
			 */
			bzero(&dqp->q_core, sizeof(dqp->q_core));
			*O_dqpp = dqp;
			return (B_FALSE);
		}
#ifndef _BANYAN_XFS
		XFSSTATS.xs_qm_dqreclaim_misses++;
#endif
	}
	/*
	 * Allocate a brand new dquot on the kernel heap and return it
	 * to the caller to initialize.
	 */
	ASSERT(G_xqm->qm_dqzone != NULL);
	*O_dqpp = kmem_zone_zalloc(G_xqm->qm_dqzone, KM_SLEEP);
	atomicAddUint(&G_xqm->qm_totaldquots, 1);

	return (B_TRUE);
}


/*
 * Start a transaction and write the incore superblock changes to
 * disk. flags parameter indicates which fields have changed.
 */
int
xfs_qm_write_sb_changes(
	xfs_mount_t	*mp,
	__int64_t	flags)
{
	xfs_trans_t	*tp;
	int 		error;
	
#ifdef QUOTADEBUG	
	cmn_err(CE_NOTE, 	
		"Writing superblock quota changes :%s\n",
		mp->m_fsname);
#endif
	tp = xfs_trans_alloc(mp, XFS_TRANS_QM_SBCHANGE);
	if (error = xfs_trans_reserve(tp, 0, 
				      mp->m_sb.sb_sectsize + 128, 0,
				      0, 
				      XFS_DEFAULT_LOG_COUNT))
		goto error0;
	xfs_mod_sb(tp, flags);
	error = xfs_trans_commit(tp, 0);
 
error0:
	if (error)
		xfs_trans_cancel(tp, XFS_TRANS_RELEASE_LOG_RES);
	return (error);
}


/* --------------- utility functions for vnodeops ---------------- */


/*
 * Given an inode, a uid and prid (from cred_t) make sure that we have 
 * allocated relevant dquot(s) on disk, and that we won't exceed inode
 * quotas by creating this file.
 * This also attaches dquot(s) to the given inode after locking it,
 * and returns the dquots corresponding to the uid and/or prid.
 *
 * in   : inode (unlocked) 
 * out	: udquot, pdquot with references taken and unlocked
 */
int
xfs_qm_vop_dqalloc(
	xfs_mount_t	*mp,
	xfs_inode_t	*ip,
	uid_t		uid,
	xfs_prid_t	prid,
	uint		flags,
	xfs_dquot_t	**O_udqpp,
	xfs_dquot_t	**O_pdqpp)
{
	int		error;
	xfs_dquot_t	*uq, *pq;

	xfs_ilock(ip, XFS_ILOCK_EXCL);
	/*
	 * Attach the dquot(s) to this inode, doing a dquot allocation
	 * if necessary. The dquot(s) will not be locked.
	 */
	if (XFS_NOT_DQATTACHED(mp, ip)) {
		if (error = xfs_qm_dqattach(ip, XFS_QMOPT_DQALLOC |
					    XFS_QMOPT_ILOCKED)) {
			xfs_iunlock(ip, XFS_ILOCK_EXCL);
			return (error);
		}
	}

	uq = pq = NULL;
	if ((flags & XFS_QMOPT_UQUOTA) &&
	    XFS_IS_UQUOTA_ON(mp)) {
		if (ip->i_d.di_uid != uid) {
			if (error = xfs_qm_dqget(mp, NULL, (xfs_dqid_t) uid,
						 XFS_DQ_USER, 
						 XFS_QMOPT_DQALLOC,
						 &uq)) {
				xfs_iunlock(ip, XFS_ILOCK_EXCL);
				ASSERT(error != ENOENT);
				if (error != ESRCH)
					xfs_qm_force_quotaoff(mp);
				return (error);
			}
		} else {
			/*
			 * Take an extra reference, because we'll return 
			 * this to caller
			 */
			ASSERT(ip->i_udquot);
			uq = ip->i_udquot;
			xfs_dqlock(uq);
			XFS_DQHOLD(uq);
		}
		xfs_dqunlock(uq);
	} 
	if ((flags & XFS_QMOPT_PQUOTA) &&
	    XFS_IS_PQUOTA_ON(mp)) {
		if (ip->i_d.di_projid != prid) {
			if (error = xfs_qm_dqget(mp, NULL, (xfs_dqid_t)prid,
						 XFS_DQ_PROJ, 
						 XFS_QMOPT_DQALLOC,
						 &pq)) {
				xfs_iunlock(ip, XFS_ILOCK_EXCL);
				if (uq)
					xfs_qm_dqrele(uq);
				ASSERT(error != ENOENT);
				if (error != ESRCH)
					xfs_qm_force_quotaoff(mp);
				return (error);
			}
		} else {
			ASSERT(ip->i_pdquot);
			pq = ip->i_pdquot;
			xfs_dqlock(pq);
			XFS_DQHOLD(pq);
		}
		xfs_dqunlock(pq);
	}

	xfs_iunlock(ip, XFS_ILOCK_EXCL);
	if (O_udqpp)
		*O_udqpp = uq;
	if (O_pdqpp)
		*O_pdqpp = pq;
	return (0);
	
}

/*
 * Actually transfer ownership, and do dquot modifications.
 * These were already reserved.
 */
xfs_dquot_t *
xfs_qm_vop_chown(
	xfs_trans_t	*tp,
	xfs_inode_t 	*ip,
	xfs_dquot_t	**IO_olddq,
	xfs_dquot_t	*newdq)
{
	xfs_dquot_t	*prevdq;
	ASSERT(XFS_ISLOCKED_INODE_EXCL(ip));
	ASSERT(XFS_IS_QUOTA_RUNNING(ip->i_mount));

	/* old dquot */
	prevdq = *IO_olddq;
	ASSERT(prevdq);
	ASSERT(prevdq != newdq);
      
	xfs_trans_mod_dquot(tp, prevdq, 
			    XFS_TRANS_DQ_BCOUNT, 
			    -(ip->i_d.di_nblocks));
	xfs_trans_mod_dquot(tp, prevdq, 
			    XFS_TRANS_DQ_ICOUNT,
			    -1);

	/* the sparkling new dquot */
	xfs_trans_mod_dquot(tp, newdq,
			    XFS_TRANS_DQ_BCOUNT, 
			    ip->i_d.di_nblocks);
	xfs_trans_mod_dquot(tp, newdq, 
			    XFS_TRANS_DQ_ICOUNT,
			    1);
	
	/*
	 * Take an extra reference, because the inode
	 * is going to keep this dquot pointer even
	 * after the trans_commit.
	 */
	xfs_dqlock(newdq);
	XFS_DQHOLD(newdq);
	xfs_dqunlock(newdq);
	*IO_olddq = newdq;

	return (prevdq);
}

/*
 * Quota reservations for setattr(AT_UID|AT_PROJID).
 */
int
xfs_qm_vop_chown_reserve(
	xfs_trans_t	*tp,
	xfs_inode_t	*ip,
	xfs_dquot_t	*udqp,
	xfs_dquot_t	*pdqp,
	uint		privileged)
{
	int 		error;
	xfs_mount_t	*mp;
	uint		delblks;
	xfs_dquot_t	*unresudq, *unrespdq, *delblksudq, *delblkspdq;

	ASSERT(XFS_ISLOCKED_INODE(ip));
	mp = ip->i_mount;
	ASSERT(XFS_IS_QUOTA_RUNNING(mp));

	delblks = ip->i_delayed_blks;
	delblksudq = delblkspdq = unresudq = unrespdq = NULL;

	if (XFS_IS_UQUOTA_ON(mp) && udqp && 
	    ip->i_d.di_uid != (uid_t)udqp->q_core.d_id) {
		delblksudq = udqp;
		/*
		 * If there are delayed allocation blocks, then we have to
		 * unreserve those from the old dquot, and add them to the
		 * new dquot.
		 */
		if (delblks) {
			ASSERT(ip->i_udquot);
			unresudq = ip->i_udquot;
		}
	}
	if (XFS_IS_PQUOTA_ON(ip->i_mount) && pdqp &&
	    ip->i_d.di_projid != (xfs_prid_t)pdqp->q_core.d_id) {
		delblkspdq = pdqp;
		if (delblks) {
			ASSERT(ip->i_pdquot);
			unrespdq = ip->i_pdquot;
		}
	}
	
	if (error = xfs_trans_reserve_quota(tp, delblksudq,
					    delblkspdq,
					    ip->i_d.di_nblocks, 1,
					    privileged)) 
		return (error);

	
	/*
	 * Do the delayed blks reservations/unreservations now. Since, these
	 * are done without the help of a transaction, if a reservation fails
	 * its previous reservations won't be automatically undone by trans
	 * code. So, we have to do it manually here.
	 */
	if (delblks) {
		/*
		 * Do the reservations first. Unreservation can't fail.
		 */
		ASSERT(delblksudq || delblkspdq);
		ASSERT(unresudq || unrespdq);
		if (error = xfs_trans_reserve_quota(NULL, 
						    delblksudq, delblkspdq,
						    (xfs_qcnt_t)delblks, 0,
						    privileged))
			return (error);
		(void) xfs_trans_unreserve_quota(NULL, 
						 unresudq, unrespdq,
						 (xfs_qcnt_t)delblks, 0,
						 0);
	}	

	return (0);
}

int
xfs_qm_vop_rename_dqattach(
	xfs_inode_t	**i_tab)
{
	xfs_inode_t	*ip;
	int		i;
	int		error;

	ip = i_tab[0];
	
	if (XFS_NOT_DQATTACHED(ip->i_mount, ip)) {
		error = xfs_qm_dqattach(ip, 0);
		if (error) 
			return (error);
	}
	for (i = 1; (i < 4 && i_tab[i]); i++) {
		/*
		 * Watch out for duplicate entries in the table.
		 */
		if ((ip = i_tab[i]) != i_tab[i-1]) {
			if (XFS_NOT_DQATTACHED(ip->i_mount, ip)) {
				error = xfs_qm_dqattach(ip, 0);
				if (error) 	
					return (error);
			}
		}
	}
	return (0);
}

void
xfs_qm_vop_dqattach_and_dqmod_newinode(
	xfs_trans_t	*tp,
	xfs_inode_t	*ip,
	xfs_dquot_t	*udqp,
	xfs_dquot_t	*pdqp)
{
	ASSERT(XFS_ISLOCKED_INODE_EXCL(ip));
	ASSERT(XFS_IS_QUOTA_RUNNING(tp->t_mountp));

	if (udqp) {
		xfs_dqlock(udqp);
		XFS_DQHOLD(udqp);
		xfs_dqunlock(udqp);
		ASSERT(ip->i_udquot == NULL);
		ip->i_udquot = udqp;
		ASSERT(ip->i_d.di_uid == udqp->q_core.d_id);
		xfs_trans_mod_dquot(tp, udqp, 
				    XFS_TRANS_DQ_ICOUNT,
				    1);
	}
	if (pdqp) {
		xfs_dqlock(pdqp);
		XFS_DQHOLD(pdqp);
		xfs_dqunlock(pdqp);
		ASSERT(ip->i_pdquot == NULL);
		ip->i_pdquot = pdqp;
		ASSERT(ip->i_d.di_projid == pdqp->q_core.d_id);
		xfs_trans_mod_dquot(tp, pdqp, 
				    XFS_TRANS_DQ_ICOUNT,
				    1);
	}

}

/* ------------- list stuff -----------------*/
void
xfs_qm_freelist_init(xfs_frlist_t *ql)
{
	ql->qh_next = ql->qh_prev = (xfs_dquot_t *) ql;
	mutex_init(&ql->qh_lock, MUTEX_DEFAULT, "dqf");
	ql->qh_version = 0;
	ql->qh_nelems = 0;
}

void
xfs_qm_freelist_destroy(xfs_frlist_t *ql)
{
	xfs_dquot_t	*dqp, *nextdqp;

	mutex_lock(&ql->qh_lock, PINOD);
	for (dqp = ql->qh_next; 
	     dqp != (xfs_dquot_t *)ql; ) {
		xfs_dqlock(dqp);	
		nextdqp = dqp->dq_flnext;
#ifdef QUOTADEBUG
		printf("FREELIST destroy 0x%x\n", dqp);
#endif
		XQM_FREELIST_REMOVE(dqp);
		xfs_dqunlock(dqp);
		xfs_qm_dqdestroy(dqp);
		dqp = nextdqp;
	}
	/*
	 * Don't bother about unlocking.
	 */
	mutex_destroy(&ql->qh_lock);
	
	ASSERT(ql->qh_nelems == 0);
}

void
xfs_qm_freelist_insert(xfs_frlist_t *ql, xfs_dquot_t *dq)
{
        dq->dq_flnext = ql->qh_next;
        dq->dq_flprev = (xfs_dquot_t *)ql;
        ql->qh_next = dq;
        dq->dq_flnext->dq_flprev = dq;
	G_xqm->qm_dqfreelist.qh_nelems++;
	G_xqm->qm_dqfreelist.qh_version++;
}

void
xfs_qm_freelist_unlink(xfs_dquot_t *dq)
{
        xfs_dquot_t *next = dq->dq_flnext;
        xfs_dquot_t *prev = dq->dq_flprev;

        next->dq_flprev = prev;
        prev->dq_flnext = next;
        dq->dq_flnext = dq->dq_flprev = dq;
	G_xqm->qm_dqfreelist.qh_nelems--;
	G_xqm->qm_dqfreelist.qh_version++;
}

#ifdef QUOTADEBUG
void
xfs_qm_freelist_print(xfs_frlist_t *qlist, char *title) 
{
	xfs_dquot_t *dq;
	int i = 0;
	printf("%s (#%d)\n", title, (int) qlist->qh_nelems);	
	FOREACH_DQUOT_IN_FREELIST(dq, qlist) {
		printf("\t%d.\t\"%d (%s:0x%x)\"\t bcnt = %d, icnt = %d "
		       "refs = %d\n",  
		       ++i, (int) dq->q_core.d_id,
		       DQFLAGTO_TYPESTR(dq), dq,     
		       (int) dq->q_core.d_bcount, 
		       (int) dq->q_core.d_icount, 
		       (int) dq->q_nrefs);
	}
	return;
}
#endif

void
xfs_qm_freelist_append(xfs_frlist_t *ql, xfs_dquot_t *dq)
{
	xfs_qm_freelist_insert((xfs_frlist_t *)ql->qh_prev, dq);
}
 
int
xfs_qm_dqhashlock_nowait(
	xfs_dquot_t *dqp)
{
	int locked;
	
	locked = mutex_trylock(&((dqp)->q_hash->qh_lock));
	return (locked);
}

int
xfs_qm_freelist_lock_nowait(
	xfs_qm_t *xqm)
{
	int locked;
	
	locked = mutex_trylock(&(xqm->qm_dqfreelist.qh_lock));
	return (locked);
}

int
xfs_qm_mplist_nowait(
	xfs_mount_t	*mp)
{
	int locked;
	
	ASSERT(mp->m_quotainfo);
	locked = mutex_trylock(&(mp->QI_MPL_LIST.qh_lock));
	return (locked);
}
