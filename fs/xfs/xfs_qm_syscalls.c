#ident "$Revision: 1.5 $"

#include <sys/param.h>
#include <sys/sysinfo.h>
#include <sys/buf.h>
#include <sys/ksa.h>
#include <sys/vnode.h>
#include <sys/pfdat.h>
#include <sys/uuid.h>
#include <sys/capability.h>
#include <sys/param.h>
#include <sys/cred.h>
#include <sys/errno.h>
#include <sys/buf.h>
#include <sys/kmem.h>
#include <sys/debug.h>
#include <sys/proc.h>
#include <sys/vnode.h>
#include <sys/cmn_err.h>
#include <sys/vfs.h>
#include <sys/uuid.h>
#include <sys/atomic_ops.h>
#include <sys/systm.h>
#include <sys/ktrace.h>
#include <limits.h>

#include "xfs_macros.h"
#include "xfs_types.h"
#include <sys/quota.h>
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
#include "xfs_itable.h"
#include "xfs_trans_priv.h"
#include "xfs_bit.h"
#include "xfs_clnt.h"
#include "xfs_quota.h"
#include "xfs_dquot.h"
#include "xfs_qm.h"
#include "xfs_quota_priv.h"

extern int 	xfs_fstype;

STATIC int	xfs_qm_scall_trunc_qfiles(xfs_mount_t *, uint);
STATIC int	xfs_qm_scall_getquota(xfs_mount_t *, xfs_dqid_t, uint, caddr_t);
STATIC int	xfs_qm_scall_getqstat(xfs_mount_t *, uint *);
STATIC int	xfs_qm_scall_setqlim(xfs_mount_t *, xfs_dqid_t, uint, caddr_t);
STATIC int	xfs_qm_scall_quotaoff(xfs_mount_t *, uint);
STATIC int	xfs_qm_scall_quotaon(xfs_mount_t *, uint);
STATIC int	xfs_qm_log_quotaoff(xfs_mount_t *, xfs_qoff_logitem_t **, uint);
STATIC int	xfs_qm_log_quotaoff_end(xfs_mount_t *, xfs_qoff_logitem_t *,
					uint);
STATIC uint	xfs_qm_import_flags(uint *);
STATIC uint	xfs_qm_export_flags(uint);
STATIC uint	xfs_qm_import_qtype_flags(uint *);
STATIC uint	xfs_qm_export_qtype_flags(uint);
STATIC void	xfs_qm_export_dquot(xfs_mount_t *, xfs_disk_dquot_t *,
				    struct fs_disk_quota *);
STATIC void	xfs_qm_dqrele_all_inodes(xfs_mount_t *, uint);


int
xfs_qm_sysent(
	struct	vfs	*vfsp,
	int		cmd,
	int		id,
	caddr_t		addr)
{
	xfs_mount_t	*mp;
	int 		error;
#ifndef _BANYAN_XFS
	/* 
	 * TEMP: XXX to keep various trees from getting out of sync. 
	 * In the 6.2 code, project quotas and vfs behaviors
	 * are not supported.
	 */
	bhv_desc_t 	*bdp;

	ASSERT(vfsp);
	bdp = bhv_lookup_unlocked(VFS_BHVHEAD(vfsp), &xfs_vfsops);
        mp = XFS_BHVTOM(bdp);
#else
	ASSERT(vfsp);
	mp = (xfs_mount_t *) vfsp->vfs_data;
	ASSERT(mp->m_vfsp->vfs_fstype == xfs_fstype);
#endif

#ifdef DEBUG		
	/* Internal quota accounting check for debugging */
	if (cmd == 50) 
		return (xfs_qm_internalqcheck(mp));
#endif
	if (addr == NULL)
		return XFS_ERROR(EINVAL);
	
	/*
	 * The following commands are valid even when quotaoff.
	 */
	switch (cmd) {
	      	/* 
		 * truncate quota files. quota must be off.
		 */
	      case Q_QUOTARM:
		if (XFS_IS_QUOTA_ON(mp) || addr == NULL)
			return XFS_ERROR(EINVAL);
		return (xfs_qm_scall_trunc_qfiles(mp, 
				       xfs_qm_import_qtype_flags((uint *)addr)));
		/*
		 * Get quota status information.
		 */
	      case Q_GETQSTAT:

		return (xfs_qm_scall_getqstat(mp, (uint *)addr));

		/*
		 * QUOTAON for root f/s and quota enforcement on others..
		 * Quota accounting for non-root f/s's must be turned on
		 * at mount time.
		 */
	      case Q_QUOTAON:
		if (addr == NULL)
			return XFS_ERROR(EINVAL);
		return (xfs_qm_scall_quotaon(mp,
					     xfs_qm_import_flags((uint *)addr)));
	}

	if (! XFS_IS_QUOTA_ON(mp))
		return (ESRCH);

	switch (cmd) {
	      case Q_QUOTAOFF:
		error = xfs_qm_scall_quotaoff(mp,
					      xfs_qm_import_flags((uint *)addr));
		break;

		/* 
		 * Defaults to XFS_GETUQUOTA. 
		 */
	      case Q_XGETQUOTA:
		error = xfs_qm_scall_getquota(mp, (xfs_dqid_t)id, XFS_DQ_USER, 
					addr);
		break;
		/*
		 * Set limits, both hard and soft. Defaults to Q_SETUQLIM.
		 */
	      case Q_XSETQLIM:
		error = xfs_qm_scall_setqlim(mp, (xfs_dqid_t)id, XFS_DQ_USER,
					     addr);
		break;
#ifndef _BANYAN_XFS
	       case Q_XSETPQLIM:
		error = xfs_qm_scall_setqlim(mp, (xfs_dqid_t)id, XFS_DQ_PROJ,
					     addr);
		break;

	      		
	      case Q_XGETPQUOTA:
		error = xfs_qm_scall_getquota(mp, (xfs_dqid_t)id, XFS_DQ_PROJ, 
					addr);
		break;
		/*
		 * Set limits, both hard and soft. Defaults to Q_SETUQLIM.
		 */
#else	
	      case Q_XSETPQLIM:
	      case Q_XGETPQUOTA:
		error = ENOTSUP;
		break;
#endif
		/*
		 * Quotas are entirely undefined after quotaoff in XFS quotas.
		 * For instance, there's no way to set limits when quotaoff.
		 */
	      case Q_ACTIVATE:
	      case Q_SETQUOTA:
	      case Q_GETQUOTA:
	      case Q_GETPQUOTA:
	      case Q_SETQLIM:
	      case Q_SETPQLIM:
	      case Q_SYNC:
		error = ENOTSUP;
		break;

	      default:
		error = XFS_ERROR(EINVAL);
		break;
	}
	return (error);

}


STATIC int
xfs_qm_scall_trunc_qfiles(
	xfs_mount_t	*mp,
	uint		flags)
{
	int 		error;
	xfs_inode_t	*qip;
	/*
	 * Function stolen from xfs_vnodeops.c 
	 */
	extern int  xfs_truncate_file(xfs_mount_t *mp, xfs_inode_t *ip);

	if (!_CAP_ABLE(CAP_QUOTA_MGT))
		return XFS_ERROR(EPERM);
	error = 0;
	if (mp->m_sb.sb_versionnum < XFS_SB_VERSION_HASQUOTA)
		return XFS_ERROR(EINVAL);
	if (flags == 0)
		return XFS_ERROR(EINVAL);

	if ((flags & XFS_DQ_USER) &&
	    mp->m_sb.sb_uquotino != NULLFSINO) {
		error = xfs_iget(mp, NULL, 
				 mp->m_sb.sb_uquotino,
				 NULL,
				 &qip, 0);
		if (! error) {
#ifdef QUOTADEBUG		
			printf("UQINO %d, nrefs %d\n",
			       (int) mp->m_sb.sb_uquotino,	
			       (int) (XFS_ITOV(qip))->v_count);
#endif
			(void) xfs_truncate_file(mp, qip);
			VN_RELE(XFS_ITOV(qip));
		}
	}
	
	if ((flags & XFS_DQ_PROJ) &&
	    mp->m_sb.sb_pquotino != NULLFSINO) {
		error = xfs_iget(mp, NULL, 
				 mp->m_sb.sb_pquotino,
				 NULL,
				 &qip, 0);
		if (! error) {
#ifdef QUOTADEBUG		
			printf("PQINO %d, nrefs %d\n",
			       (int) mp->m_sb.sb_pquotino,	
			       (int) (XFS_ITOV(qip))->v_count);
#endif
			(void) xfs_truncate_file(mp, qip);
			VN_RELE(XFS_ITOV(qip));
		}
	}

	return (error);
}


/* 
 * This does two separate functions:
 * Switch on quotas for the root file system. This will take effect only
 * on reboot.
 * Switch on (a given) quota enforcement for both root and non-root filesystems.
 * This takes effect immediately.
 */
STATIC int
xfs_qm_scall_quotaon(
	xfs_mount_t	*mp,
	uint		flags)
{
	extern dev_t	rootdev;
	int		error, s;
	uint		qf;
	__int64_t	sbflags;
	boolean_t	rootfs;
	boolean_t	delay;

	if (!_CAP_ABLE(CAP_QUOTA_MGT))
		return (EPERM);

	rootfs = (boolean_t) (mp->m_dev == rootdev);
	delay = (boolean_t) ((flags & XFS_ALL_QUOTA_ACCT) != 0);
	flags &= (XFS_ALL_QUOTA_ACCT | XFS_ALL_QUOTA_ENFD);
	sbflags = 0;

	if (flags == 0)
		return (EINVAL);
	/*
	 * Switching on quota accounting for non-root filesystems
	 * must be done at mount time. OTOH, root filesystem doesn't
	 * take a mount time option to enable quotas.
	 */
	if (!rootfs && delay) {
#ifdef QUOTADEBUG
		printf("NON-root fs. Must enable acctg @ mount time\n");
#endif
		return (EINVAL);
	}
	/*
	 * Change superblock version (if needed) for the root filesystem
	 */
	if (rootfs && 
	    mp->m_sb.sb_versionnum < XFS_SB_VERSION_HASQUOTA) {
#ifdef DEBUG
		cmn_err(CE_NOTE, 
			"Old superblock version %d.0, converting to %d.0\n",
			(int) mp->m_sb.sb_versionnum,
			(int) XFS_SB_VERSION);
#endif
		s = XFS_SB_LOCK(mp);
		mp->m_sb.sb_versionnum = XFS_SB_VERSION;    
		mp->m_sb.sb_uquotino = NULLFSINO;
		mp->m_sb.sb_pquotino = NULLFSINO;
		mp->m_sb.sb_qflags = 0;
		XFS_SB_UNLOCK(mp, s);
		sbflags |= (XFS_SB_VERSIONNUM | XFS_SB_UQUOTINO |
			    XFS_SB_PQUOTINO | XFS_SB_QFLAGS);
	}

	/*
	 * Change sb_qflags on disk but not incore, if this is the root f/s.
	 */
	s = XFS_SB_LOCK(mp);
	qf = mp->m_sb.sb_qflags;
	mp->m_sb.sb_qflags = qf | flags;
	XFS_SB_UNLOCK(mp, s);
	
	if (qf == flags && sbflags == 0)  /* TMP - use ASSERT */
		return (EEXIST);
	sbflags |= XFS_SB_QFLAGS;

	if (error = xfs_qm_write_sb_changes(mp, sbflags))
		return XFS_ERROR(error);
	/*
	 * If we had just turned on quotas (ondisk) for rootfs, or if we aren't
	 * trying to switch on quota enforcement, we are done.
	 */
	if (delay ||
	    (flags & XFS_ALL_QUOTA_ENFD) == 0)
		return (0);
	
	if (! XFS_IS_QUOTA_RUNNING(mp))
		return XFS_ERROR(ESRCH);

	/*
	 * Switch on quota enforcement in core. This applies to both root
	 * and non-root file systems.
	 */
	mutex_lock(&mp->QI_QOFFLOCK, PINOD);
	mp->m_flags |= (flags & XFS_ALL_QUOTA_ENFD);
	mutex_unlock(&mp->QI_QOFFLOCK);
	
	return (0);
}



/*
 * Return quota status information, such as uquota-off, enforcements, etc.
 */
STATIC int
xfs_qm_scall_getqstat(
	xfs_mount_t 	*mp,
	uint		*flags)
{
	uint		fl;

	fl = mp->m_flags & (XFS_ALL_QUOTA_ACCT|XFS_ALL_QUOTA_ENFD);
	fl = xfs_qm_export_flags(fl);
	if (suword(flags, fl))
		return XFS_ERROR(EFAULT);
	return (0);
}

/*
 * Adjust quota limits, and start/stop timers accordingly.
 */
STATIC int
xfs_qm_scall_setqlim(
	xfs_mount_t		*mp,
	xfs_dqid_t		id,
	uint			type,
	caddr_t			addr)
{
	xfs_disk_dquot_t	*ddq;
	fs_disk_quota_t		newlim;
	xfs_dquot_t		*dqp;
	xfs_trans_t		*tp;
	int			error;
	xfs_qcnt_t		hard, soft;

	if (!_CAP_ABLE(CAP_QUOTA_MGT))
		return XFS_ERROR(EPERM);
	if (copyin(addr, &newlim, sizeof newlim))
		return XFS_ERROR(EFAULT);

	tp = xfs_trans_alloc(mp, XFS_TRANS_QM_SETQLIM); 
	if (error = xfs_trans_reserve(tp, 0, sizeof(xfs_disk_dquot_t) + 128,
				      0, 0, XFS_DEFAULT_LOG_COUNT)) {
		xfs_trans_cancel(tp, 0);
		mutex_unlock(&mp->QI_QOFFLOCK);
		return (error);
	}

	/*
	 * We don't want to race with a quotaoff so take the quotaoff lock.
	 * (We don't hold an inode lock, so there's nothing else to stop
	 * a quotaoff from happening). (XXXThis doesn't currently doesn't happen
	 * because we take the vfslock before calling xfs_qm_sysent).
	 */
	mutex_lock(&mp->QI_QOFFLOCK, PINOD);

	/*
	 * Get the dquot (locked), and join it to the transaction.
	 * Allocate the dquot if this doesn't exist.
	 */
	if (error = xfs_qm_dqget(mp, NULL, id, type, XFS_QMOPT_DQALLOC, &dqp)) {
		xfs_trans_cancel(tp, XFS_TRANS_RELEASE_LOG_RES);
		mutex_unlock(&mp->QI_QOFFLOCK);
		ASSERT(error != ENOENT);
		if (error != ESRCH)
			xfs_qm_force_quotaoff(mp);
		return (error);	    
	}
	xfs_dqtrace_entry(dqp, "Q_SETQLIM: AFT DQGET");
	xfs_trans_dqjoin(tp, dqp);
	ddq = &dqp->q_core;

	/*
	 * Make sure that hardlimits are >= soft limits before changing.
	 * XXX Should we expand the interface to set individual limits ?
	 */
	hard = (newlim.d_blk_hardlimit != (xfs_qcnt_t) -1) ?
		(xfs_qcnt_t) XFS_BB_TO_FSB(mp, newlim.d_blk_hardlimit) :
			ddq->d_blk_hardlimit;
	soft = (newlim.d_blk_softlimit != (xfs_qcnt_t) -1) ?
		(xfs_qcnt_t) XFS_BB_TO_FSB(mp, newlim.d_blk_softlimit) :
			ddq->d_blk_softlimit;
	if (!hard || hard >= soft) {
		ddq->d_blk_hardlimit = hard;
		ddq->d_blk_softlimit = soft;
	}
#ifdef QUOTADEBUG
	else 
		printf("blkhard 0x%x < blksoft 0x%x\n", hard, soft);
#endif			
	hard = (newlim.d_rtb_hardlimit != (xfs_qcnt_t) -1) ?
		(xfs_qcnt_t) XFS_BB_TO_FSB(mp, newlim.d_rtb_hardlimit) :
			ddq->d_rtb_hardlimit;
	soft = (newlim.d_rtb_softlimit != (xfs_qcnt_t) -1) ?
		(xfs_qcnt_t) XFS_BB_TO_FSB(mp, newlim.d_rtb_softlimit) :
			ddq->d_rtb_softlimit;
	if (!hard || hard >= soft) {
		ddq->d_rtb_hardlimit = hard;
		ddq->d_rtb_softlimit = soft;
	}
#ifdef QUOTADEBUG
	else 
		printf("rtbhard 0x%x < rtbsoft 0x%x\n", hard, soft);
#endif	
	
	hard = (newlim.d_ino_hardlimit != (xfs_qcnt_t) -1) ?
		(xfs_qcnt_t) newlim.d_ino_hardlimit : ddq->d_ino_hardlimit;
	soft = (newlim.d_ino_softlimit != (xfs_qcnt_t) -1) ?
		(xfs_qcnt_t) newlim.d_ino_softlimit : ddq->d_ino_softlimit;
	if (!hard || hard >= soft) {
		ddq->d_ino_hardlimit = hard;
		ddq->d_ino_softlimit = soft;
	}
#ifdef QUOTADEBUG
	else 
		printf("ihard 0x%x < isoft 0x%x\n", hard, soft);
#endif		
		
#ifdef QUOTADEBUG	
	printf("@@@ ID %d: bsoft sent %llu, now %llu : isoft %llu, now %llu\n",
	       (int) id,
	       newlim.d_blk_softlimit, 
	       ddq->d_blk_softlimit,
	       newlim.d_ino_softlimit, 
	       ddq->d_ino_softlimit);
#endif
	if (id == 0) {
		/*
		 * Timelimits for the super user set the relative time
		 * the other users can be over quota for this file system.
		 * If it is zero a default is used.
		 */
		mp->m_quotainfo->qi_btimelimit = newlim.d_btimer ?
			newlim.d_btimer : XFS_QM_BTIMELIMIT;
		mp->m_quotainfo->qi_itimelimit = newlim.d_itimer ?
			newlim.d_itimer : XFS_QM_ITIMELIMIT;
		mp->m_quotainfo->qi_rtbtimelimit = newlim.d_rtbtimer ?
			newlim.d_rtbtimer : XFS_QM_RTBTIMELIMIT;
		
		/*
		 * ditto, for warning limits.
		 * (XXXare we actually doing these here?)
		 */
		mp->m_quotainfo->qi_bwarnlimit = newlim.d_bwarns ?
			newlim.d_bwarns : XFS_QM_BWARNLIMIT;
		mp->m_quotainfo->qi_iwarnlimit = newlim.d_iwarns ?
			newlim.d_iwarns : XFS_QM_IWARNLIMIT;

	} else /* if (XFS_IS_QUOTA_ENFORCED(mp)) */ {
#ifdef QUOTADEBUG	
		printf("@@@ (before changing) ID %d: bsoft %d, bcount %d : "
		       "ihard %d\n",
		       (int) id,
		       (int) ddq->d_blk_softlimit,
		       (int) ddq->d_bcount,
		       (int) ddq->d_ino_hardlimit);
#endif
		/*
		 * If the user is now over quota, start the timelimit.
		 * The user will not be 'warned'. Warnings increase only
		 * by request via Q_WARN.
		 */
		xfs_qm_adjust_dqtimers(mp, ddq);
	}
	
	xfs_trans_log_dquot(tp, dqp);
	xfs_dqtrace_entry(dqp, "Q_SETQLIM: COMMIT");
	xfs_trans_commit(tp, 0);
	xfs_qm_dqprint(dqp);
	xfs_qm_dqrele(dqp);
	mutex_unlock(&mp->QI_QOFFLOCK);
	return (0);
}


STATIC int
xfs_qm_scall_getquota(
	xfs_mount_t 	*mp,
	xfs_dqid_t 	id,
	uint		type,
	caddr_t 	addr)
{
	xfs_dquot_t	*dqp;
	fs_disk_quota_t	out;
	int 		error;

#ifndef _BANYAN_XFS
	if (id != get_current_cred()->cr_ruid && !_CAP_ABLE(CAP_QUOTA_MGT))
		return XFS_ERROR(EPERM);
#else
	if (id != curprocp->p_cred->cr_ruid && !_CAP_ABLE(CAP_QUOTA_MGT))
		return XFS_ERROR(EPERM);
#endif
	/*
	 * Try to get the dquot. We don't want it allocated on disk, so
	 * we aren't passing the XFS_QMOPT_DOALLOC flag. If it doesn't
	 * exist, we'll get ENOENT back.
	 */
#ifdef QUOTADEBUG
	printf("Checking ID %d, type 0x%x in mp = 0x%x\n",
	       id, type, mp);
#endif
	if (error = xfs_qm_dqget(mp, NULL, id, type, 0, &dqp)) {
		if (error != ESRCH && error != ENOENT)
			xfs_qm_force_quotaoff(mp);
#ifdef QUOTADEBUG
		else
			printf("!ID %d, type 0x%x not in mp = 0x%x\n",
			       id, type, mp);
#endif
		return (error);
	}

	xfs_dqtrace_entry(dqp, "Q_GETQUOTA SUCCESS");
	/*
	 * If everything's NULL, this dquot doesn't quite exist as far as
	 * our utility programs are concerned.
	 */
	if (XFS_IS_DQUOT_UNINITIALIZED(dqp)) {
		xfs_qm_dqput(dqp);
		return (ENOENT);
	}
	xfs_qm_dqprint(dqp);
	/*
	 * Convert the disk dquot to the exportable format
	 */
	xfs_qm_export_dquot(mp, &dqp->q_core, &out);
	error = copyout(&out, addr, sizeof(out));
	xfs_qm_dqput(dqp);
	return (error ? EFAULT : 0);
}

/*
 * Turn off quota accounting and/or enforcement for all udquots and/or
 * pdquots. Called only at mount time.
 *
 * This assumes that there are no dquots of this file system cached 
 * incore, and modifies the ondisk dquot directly. Therefore, for example,
 * it is an error to call this twice, without purging the cache.
 */
STATIC int
xfs_qm_scall_quotaoff(
	xfs_mount_t	*mp,
	uint		flags)
{
	uint			dqtype;
	int			s;
	int			error;
	uint			inactivate_flags;
	xfs_qoff_logitem_t 	*qoffstart;

	if (!_CAP_ABLE(CAP_QUOTA_MGT))
		return (EPERM);
	
	/*
	 * quota utilities like quotaoff expect errno == EEXIST.
	 */
	if ((mp->m_flags & flags) == 0)
		return (EEXIST);
	
	ASSERT(mp->m_quotainfo);
	
	/* 
	 * We don't want to deal with two quotaoffs messing up each other,
	 * so we're going to serialize it. quotaoff isn't exactly a performance
	 * critical thing.
	 */
	mutex_lock(&mp->QI_QOFFLOCK, PINOD);
	
	/*
	 * if we're just turning off quota enforcement, change mp and go.
	 */
	if ((flags & XFS_ALL_QUOTA_ACCT) == 0) {
		mp->m_flags &= ~(flags);
		
		s = XFS_SB_LOCK(mp);
		mp->m_sb.sb_qflags = mp->m_flags;
		XFS_SB_UNLOCK(mp, s);
		mutex_unlock(&mp->QI_QOFFLOCK);
		
		/* XXX what to do if error ? Revert back to old vals incore ? */
		error = xfs_qm_write_sb_changes(mp, XFS_SB_QFLAGS);
		return (error);
	}

	dqtype = 0;
	inactivate_flags = 0;
	/* 
	 * If accounting is off, we must turn enforcement off, clear the
	 * quota 'CHKD' certificate to make it known that we have to
	 * do a quotacheck the next time this quota is turned on.
	 */
	if (flags & XFS_MOUNT_UDQ_ACCT) {
		dqtype |= XFS_QMOPT_UQUOTA;
		flags |= (XFS_MOUNT_UDQ_CHKD | XFS_MOUNT_UDQ_ENFD);
		inactivate_flags |= XFS_MOUNT_UDQ_ACTIVE;
	}
	if (flags & XFS_MOUNT_PDQ_ACCT) {
		dqtype |= XFS_QMOPT_PQUOTA;
		flags |= (XFS_MOUNT_PDQ_CHKD | XFS_MOUNT_PDQ_ENFD);
		inactivate_flags |= XFS_MOUNT_PDQ_ACTIVE;
	}
	
	/*
	 * Nothing to do? Don't complain.
	 * This happens when we're just turning off quota enforcement.
	 */
	if ((mp->m_flags & flags) == 0) {
		mutex_unlock(&mp->QI_QOFFLOCK);
		return (0);
	}
	
	/*
	 * Write the LI_QUOTAOFF log record, and do SB changes atomically,
	 * and synchronously.
	 */
	xfs_qm_log_quotaoff(mp, &qoffstart, flags);

	/*
	 * Next we clear the XFS_MOUNT_*DQ_ACTIVE bit(s) in the mount struct
	 * to take care of the race between dqget and quotaoff. We don't take
	 * any special locks to reset these bits. All processes need to check
	 * these bits *after* taking inode lock(s) to see if the particular
	 * quota type is in the process of being turned off. If *ACTIVE, it is
	 * guaranteed that all dquot structures and all quotainode ptrs will all
	 * stay valid as long as that inode is kept locked. 
	 *
	 * There is no turning back after this.
	 */
	mp->m_flags &= ~inactivate_flags;

	/*
	 * Give back all the dquot reference(s) held by inodes.
	 * Here we go thru every single incore inode in this file system, and
	 * do a dqrele on the i_udquot/i_pdquot that it may have.
	 * Essentially, as long as somebody has an inode locked, this guarantees
	 * that quotas will not be turned off. This is handy because in a
	 * transaction once we lock the inode(s) and check for quotaon, we can
	 * depend on the quota inodes (and other things) being valid as long as
	 * we keep the lock(s).
	 */
	xfs_qm_dqrele_all_inodes(mp, flags);
	
	/*
	 * Next we make the changes in the quota flag in the mount struct. 
	 * This isn't protected by a particular lock directly, because we
	 * don't want to take a mrlock everytime we depend on quotas being on.
	 */
	mp->m_flags &= ~(flags);	

	/*
	 * Go through all the dquots of this file system and purge them,
	 * according to what was turned off.
	 * At this point if everybody played according to the rules and didnt
	 * slip thru the cracks, we should be able to free every single dquot
	 * that is affected by this quotaoff all the time.
	 */
	(void) xfs_qm_dqpurge_all(mp, dqtype|XFS_QMOPT_QUOTAOFF);

	/*
	 * Transactions that had started before ACTIVE state bit was cleared 
	 * could have logged many dquots, so they'd have higher LSNs than 
	 * the first QUOTAOFF log record does. If we happen to crash when
	 * the tail of the log has gone past the QUOTAOFF record, but 
	 * before the last dquot modification, those dquots __will__
	 * recover, and that's not good.
	 *
	 * So, we have QUOTAOFF start and end logitems; the start
	 * logitem won't get overwritten until the end logitem appears...
	 */
	xfs_qm_log_quotaoff_end(mp, qoffstart, flags);

#ifdef QUOTADEBUG
	if (mp->QI_UQIP)
		printf("QINF uqino %d SB-UQINO %d, nrefs %d\n", 
		       (int) mp->QI_UQIP->i_ino, 
		       (int) mp->m_sb.sb_uquotino,
		       (XFS_ITOV(mp->QI_UQIP))->v_count);
	if (mp->QI_PQIP)
		printf("QINF pqino %d SB-PQINO %d, nrefs %d\n", 
		       (int) mp->QI_PQIP->i_ino, 
		       (int) mp->m_sb.sb_pquotino,
		       (XFS_ITOV(mp->QI_PQIP))->v_count);
#endif

	/*	
	 * If quotas is completely disabled, close shop.
	 */
	if ((flags & XFS_MOUNT_QUOTA_ALL) == XFS_MOUNT_QUOTA_ALL) {
		mutex_unlock(&mp->QI_QOFFLOCK);
		xfs_qm_destroy_quotainfo(mp);	
		return (0);
	}

	/*
	 * Release our quotainode references, and vn_purge them,
	 * if we don't need them anymore.
	 */
	if ((dqtype & XFS_QMOPT_UQUOTA) &&
	    mp->QI_UQIP) {
		XFS_PURGE_INODE(XFS_ITOV(mp->QI_UQIP));
		mp->QI_UQIP = NULL;
	}
	if ((dqtype & XFS_QMOPT_PQUOTA) && 
	    mp->QI_PQIP) {
		XFS_PURGE_INODE(XFS_ITOV(mp->QI_PQIP));
		mp->QI_PQIP = NULL;
	}
	mutex_unlock(&mp->QI_QOFFLOCK);

	return (error);
}
	


STATIC int
xfs_qm_log_quotaoff_end(
	xfs_mount_t 		*mp,
	xfs_qoff_logitem_t 	*startqoff,
	uint			flags)
{
	xfs_trans_t	       *tp;
	int 			error;
	xfs_qoff_logitem_t     *qoffi;

#ifdef QUOTADEBUG	
	printf("Writing QuotaOFF END record 0x%x, sb-qflags 0x%x\n",
	       flags & XFS_ALL_QUOTA_ACCT,
	       (mp->m_flags & ~(flags)) & XFS_MOUNT_QUOTA_ALL);
#endif

	tp = xfs_trans_alloc(mp, XFS_TRANS_QM_QUOTAOFF_END);

	/* XXX can this ever fail ??? */
	(void) xfs_trans_reserve(tp, 0, sizeof(xfs_qoff_logitem_t) * 2,
			  0,
			  0, 
			  XFS_DEFAULT_LOG_COUNT);
	

	qoffi = xfs_trans_get_qoff_item(tp, startqoff,
					flags & XFS_ALL_QUOTA_ACCT);
	xfs_trans_log_quotaoff_item(tp, qoffi);

	/*
	 * We have to make sure that the transaction is secure on disk before
	 * we return and actually stop quota accounting. So, make it synchronous.
	 * We don't care about quotoff's performance.
	 */
	xfs_trans_set_sync(tp);
	error = xfs_trans_commit(tp, 0);
	return (error);

}


STATIC int
xfs_qm_log_quotaoff(
	xfs_mount_t 	       *mp,
	xfs_qoff_logitem_t     **qoffstartp,
	uint		       flags)
{
	xfs_trans_t	       *tp;
	int 			error, s;
	xfs_qoff_logitem_t     *qoffi;
	uint			oldsbqflag;

#ifdef QUOTADEBUG	
	printf("Writing QuotaOFF record 0x%x, sb-qflags 0x%x\n",
	       flags & XFS_ALL_QUOTA_ACCT,
	       (mp->m_flags & ~(flags)) & XFS_MOUNT_QUOTA_ALL);
#endif
	tp = xfs_trans_alloc(mp, XFS_TRANS_QM_QUOTAOFF);
	if (error = xfs_trans_reserve(tp, 0, 
				      sizeof(xfs_qoff_logitem_t) * 2 +
				      mp->m_sb.sb_sectsize + 128,
				      0,
				      0, 
				      XFS_DEFAULT_LOG_COUNT)) {
		goto error0;
	}

	qoffi = xfs_trans_get_qoff_item(tp, NULL, flags & XFS_ALL_QUOTA_ACCT);
	xfs_trans_log_quotaoff_item(tp, qoffi);

	s = XFS_SB_LOCK(mp);
	oldsbqflag = mp->m_sb.sb_qflags;
	mp->m_sb.sb_qflags = (mp->m_flags & ~(flags)) & XFS_MOUNT_QUOTA_ALL;
	XFS_SB_UNLOCK(mp, s);

	xfs_mod_sb(tp, XFS_SB_QFLAGS);

	/*
	 * We have to make sure that the transaction is secure on disk before
	 * we return and actually stop quota accounting. So, make it synchronous.
	 * We don't care about quotoff's performance.
	 */
	xfs_trans_set_sync(tp);
	error = xfs_trans_commit(tp, 0);
 
error0:
	if (error) {
		xfs_trans_cancel(tp, XFS_TRANS_RELEASE_LOG_RES);
		/*
		 * No one else is modifying sb_qflags, so this is OK.
		 * We still hold the quotaofflock.
		 */
		s = XFS_SB_LOCK(mp);
		mp->m_sb.sb_qflags = oldsbqflag;
		XFS_SB_UNLOCK(mp, s);
	} 
	*qoffstartp = qoffi;
	return (error);

}



/*
 * Translate an internal style on-disk-dquot to the exportable format.
 * Currently, the main difference is that the counters/limits are all in
 * Basic Blocks (BBs) instead of the internal FSBs.
 * This is used by qm_sysent to service syscalls like quotactl(Q_XGETQUOTA)
 */
STATIC void
xfs_qm_export_dquot(
	xfs_mount_t		*mp,
	xfs_disk_dquot_t	*src,
	struct fs_disk_quota	*dst)
{
	bzero(dst, sizeof(*dst));
	dst->d_version = src->d_version;
	dst->d_flags = xfs_qm_export_qtype_flags(src->d_flags);
	dst->d_id = src->d_id;
	dst->d_blk_hardlimit = (__uint64_t) 
		XFS_FSB_TO_BB(mp, src->d_blk_hardlimit);
	dst->d_blk_softlimit = (__uint64_t) 
		XFS_FSB_TO_BB(mp, src->d_blk_softlimit);
	dst->d_ino_hardlimit = (__uint64_t) src->d_ino_hardlimit;
	dst->d_ino_softlimit = (__uint64_t) src->d_ino_softlimit;
		
	dst->d_bcount = (__uint64_t) XFS_FSB_TO_BB(mp, src->d_bcount);
	dst->d_icount = (__uint64_t) src->d_icount;
	dst->d_btimer = (__uint32_t) src->d_btimer;
	dst->d_itimer = (__uint32_t) src->d_itimer;
	dst->d_iwarns = src->d_iwarns;
	dst->d_bwarns = src->d_bwarns;

	dst->d_rtb_hardlimit = (__uint64_t) 
		XFS_FSB_TO_BB(mp, src->d_rtb_hardlimit);
	dst->d_rtb_softlimit = (__uint64_t) 
		XFS_FSB_TO_BB(mp, src->d_rtb_softlimit);
	dst->d_rtbcount = (__uint64_t) XFS_FSB_TO_BB(mp, src->d_rtbcount);
	dst->d_rtbtimer = (__uint32_t) src->d_rtbtimer;
	dst->d_rtbwarns = src->d_rtbwarns;
	
	/*
	 * Internally, we don't reset all the timers when quota enforcement
	 * gets turned off. No need to confuse the userlevel code, 
	 * so return zeroes in that case.
	 */
	if (! XFS_IS_QUOTA_ENFORCED(mp)) {
		dst->d_btimer = 0;
		dst->d_itimer = 0;
		dst->d_rtbtimer = 0;
	}
	
#ifdef QUOTADEBUG	
	if (XFS_IS_QUOTA_ENFORCED(mp) && dst->d_id != 0) {
		if (((int) dst->d_bcount >= (int) dst->d_blk_softlimit) &&
		    (dst->d_blk_softlimit > 0)) {
			ASSERT(dst->d_btimer != 0);
		}
		if (((int) dst->d_icount >= (int) dst->d_ino_softlimit) &&
		    (dst->d_ino_softlimit > 0)) {
			ASSERT(dst->d_itimer != 0);
		}
	}
#endif
}	

STATIC uint
xfs_qm_import_qtype_flags(
	uint *uflagsp)
{
	uint uflags;
	
	if (copyin(uflagsp, &uflags, sizeof(uint)) < 0) {
#ifdef QUOTADEBUG
		printf("copyin failed\n");
#endif
		return (0);
	}

	/*
	 * Can't be both at the same time.
	 */
	if (((uflags & (XFS_PROJ_QUOTA | XFS_USER_QUOTA)) ==
	     (XFS_PROJ_QUOTA | XFS_USER_QUOTA)) ||
	    ((uflags & (XFS_PROJ_QUOTA | XFS_USER_QUOTA)) == 0))
		return (0);

	return (uflags & XFS_USER_QUOTA) ? 
		XFS_DQ_USER : XFS_DQ_PROJ;
}

STATIC uint
xfs_qm_export_qtype_flags(
	uint flags)
{
	/*
	 * Can't be both at the same time.
	 */
	ASSERT((flags & (XFS_PROJ_QUOTA | XFS_USER_QUOTA)) !=
		(XFS_PROJ_QUOTA | XFS_USER_QUOTA));
	ASSERT((flags & (XFS_PROJ_QUOTA | XFS_USER_QUOTA)) != 0);

	return (flags & XFS_DQ_USER) ? 
		XFS_USER_QUOTA : XFS_PROJ_QUOTA;
}

STATIC uint
xfs_qm_import_flags(
	uint *uflagsp)
{
	uint flags, uflags;
	
	if (copyin(uflagsp, &uflags, sizeof(uint)) < 0) 
		return (0);

	flags = 0;
	if (uflags & XFS_QUOTA_UDQ_ACCT)
		flags |= XFS_MOUNT_UDQ_ACCT;
	if (uflags & XFS_QUOTA_PDQ_ACCT)
		flags |= XFS_MOUNT_PDQ_ACCT;
	if (uflags & XFS_QUOTA_UDQ_ENFD)
		flags |= XFS_MOUNT_UDQ_ENFD;
	if (uflags & XFS_QUOTA_PDQ_ENFD)
		flags |= XFS_MOUNT_PDQ_ENFD;
	return (flags);
}


STATIC uint
xfs_qm_export_flags(
	uint flags)
{
	uint uflags;
	
	uflags = 0;
	if (flags & XFS_MOUNT_UDQ_ACCT)
		uflags |= XFS_QUOTA_UDQ_ACCT;
	if (flags & XFS_MOUNT_PDQ_ACCT)
		uflags |= XFS_QUOTA_PDQ_ACCT;
	if (flags & XFS_MOUNT_UDQ_ENFD)
		uflags |= XFS_QUOTA_UDQ_ENFD;
	if (flags & XFS_MOUNT_PDQ_ENFD)
		uflags |= XFS_QUOTA_PDQ_ENFD;
	return (uflags);
}


/* 
 * Go thru all the inodes in the file system, releasing their dquots.
 * This is only called as a result of turning off user or project (or both)
 * quota accounting.
 * Note that the mount structure gets modified to indicate that quotas is off
 * AFTER this.
 */
STATIC void
xfs_qm_dqrele_all_inodes(
	xfs_mount_t	*mp,
	uint		flags)
{
	vmap_t 		vmap;
	xfs_inode_t	*ip;
	uint		ireclaims;
	vnode_t		*vp;
	int		temp = 0;
	ASSERT(mp->m_quotainfo);

again:
	XFS_MOUNT_ILOCK(mp);
	ip = mp->m_inodes;
	if (ip == NULL)
		goto out;
        do {		
		/*
		 * Rootinode has blocks associated with it. How about
		 * rbmip and rsumip ??? XXX
		 */
		if (ip == mp->QI_UQIP || ip == mp->QI_PQIP) {
			ASSERT(ip->i_udquot == NULL);
			ASSERT(ip->i_pdquot == NULL);
			ip = ip->i_mnext;
			continue;
		}
		vp = XFS_ITOV(ip);

		if (xfs_ilock_nowait(ip, XFS_ILOCK_EXCL) == 0) {
			/*
			 * Sample vp mapping while holding the mplock, lest
			 * we come across a non-existent vnode.
			 */
			VMAP(vp, vmap);
			ireclaims = mp->m_ireclaims;
			XFS_MOUNT_IUNLOCK(mp);
#ifndef _BANYAN_XFS
			if (!(vp = vn_get(vp, &vmap, 0)))
				goto again;
#else
			if (!(vp = vn_get(vp, &vmap)))
				goto again;
			ASSERT(ip == XFS_VTOI(vp));
#endif
			xfs_ilock(ip, XFS_ILOCK_EXCL);
			VN_RELE(vp);
		} else {
			ireclaims = mp->m_ireclaims;
			XFS_MOUNT_IUNLOCK(mp);
		}
#ifdef QUOTADEBUG
		if (ip->i_udquot || ip->i_pdquot)
			printf(
		"+dqrele(inode = 0x%x, ino %d), udq 0x%x, pdq 0x%x\n", 
		       ip, (int) ip->i_ino, ip->i_udquot, ip->i_pdquot);
#endif
		/* 
		 * We don't keep the mountlock across the dqrele() call,
		 * since it can take a while..
		 */
		if ((flags & XFS_MOUNT_UDQ_ACCT) && ip->i_udquot) {
			xfs_qm_dqrele(ip->i_udquot);
			ip->i_udquot = NULL;
		}
		if ((flags & XFS_MOUNT_PDQ_ACCT) && ip->i_pdquot) {
			xfs_qm_dqrele(ip->i_pdquot);
			ip->i_pdquot = NULL;
		}

#ifdef QUOTADEBUG
		if (ip->i_udquot || ip->i_pdquot)
			printf(
	        " -dqrele(inode = 0x%x, ino %d), udq 0x%x, pdq 0x%x\n", 
		       ip, (int) ip->i_ino, ip->i_udquot, ip->i_pdquot);
#endif
		xfs_iunlock(ip, XFS_ILOCK_EXCL);

		temp++;
		XFS_MOUNT_ILOCK(mp);
		if (mp->m_ireclaims != ireclaims) {
			/* XXX use a sentinel */
			XFS_MOUNT_IUNLOCK(mp);
			goto again;
		}
		ip = ip->i_mnext;
	} while (ip != mp->m_inodes);
 out:
	XFS_MOUNT_IUNLOCK(mp);
	return;
}

/*------------------------------------------------------------------------*/
#ifdef DEBUG
/*
 * This contains all the test functions for XFS disk quotas.
 * Currently it does an quota accounting check. ie. it walks through
 * all inodes in the file system, calculating the dquot accounting fields,	
 * and prints out any inconsistencies. 
 */
xfs_dqhash_t *qmtest_udqtab;
xfs_dqhash_t *qmtest_pdqtab;
int	      qmtest_hashmask;
int	      qmtest_nfails;
mutex_t	      qcheck_lock;

#define DQTEST_HASHVAL(mp, id) (((__psunsigned_t)(mp) + \
				 (__psunsigned_t)(id)) & \
				(qmtest_hashmask - 1))

#define DQTEST_HASH(mp, id, type)   ((type & XFS_DQ_USER) ? \
				     (qmtest_udqtab + \
				      DQTEST_HASHVAL(mp, id)) : \
				     (qmtest_pdqtab + \
				      DQTEST_HASHVAL(mp, id)))

#define DQTEST_LIST_PRINT(l, NXT, title) \
{ \
	  xfs_dqtest_t	*dqp; int i = 0;\
	  printf("%s (#%d)\n", title, (int) (l)->qh_nelems); \
	  for (dqp = (xfs_dqtest_t *)(l)->qh_next; dqp != NULL; \
	       dqp = (xfs_dqtest_t *)dqp->NXT) { \
	    printf("\t%d\.\t\"%d (%s)\"\t bcnt = %d, icnt = %d\n", \
			 ++i, (int) dqp->d_id, \
		         DQFLAGTO_TYPESTR(dqp),      \
			 (int) dqp->d_bcount, \
			 (int) dqp->d_icount); } \
}
	
typedef struct dqtest {
	xfs_dqmarker_t	q_lists;
	xfs_dqhash_t	*q_hash;        /* the hashchain header */
	struct xfs_mount *q_mount;	/* filesystem this relates to */
	xfs_dqid_t	d_id;		/* user id or proj id */
	xfs_qcnt_t	d_bcount;	/* # disk blocks owned by the user */
	xfs_qcnt_t	d_icount;	/* # inodes owned by the user */
} xfs_dqtest_t;

STATIC void
xfs_qm_hashinsert(xfs_dqhash_t *h, xfs_dqtest_t *dqp)
{							
	xfs_dquot_t *d;			
	if ((d) = (h)->qh_next)		
		(d)->HL_PREVP = &((dqp)->HL_NEXT);	
	(dqp)->HL_NEXT = d;			
	(dqp)->HL_PREVP = &((h)->qh_next);		
	(h)->qh_next = (xfs_dquot_t *)dqp;			
	(h)->qh_version++;			
	(h)->qh_nelems++;			
}
STATIC void
xfs_qm_dqtest_print(
	xfs_dqtest_t	*d)
{
	printf( "-----------DQTEST DQUOT----------------\n");
	printf( "---- dquot ID	=  %d\n", (int) d->d_id);
	printf( "---- type      =  %s\n", XFS_QM_ISUDQ(d) ? "USR" :
	       "PRJ");
	printf( "---- fs        =  0x%x\n", d->q_mount);
	printf( "---- bcount	=  %llu (0x%x)\n", 
	       d->d_bcount,
	       (int)d->d_bcount);
	printf( "---- icount	=  %llu (0x%x)\n", 
	       d->d_icount,
	       (int)d->d_icount);
	printf( "---------------------------\n");
}

STATIC void
xfs_qm_dqtest_failed(
	xfs_dqtest_t	*d,
	xfs_dquot_t	*dqp,
	char 		*reason,
	xfs_qcnt_t	a,
	xfs_qcnt_t	b,
	int		error)
{	
	qmtest_nfails++;
	if (error)
		printf("quotacheck failed for %d, error = %d\nreason = %s\n",
		       d->d_id, error, reason);
	else
		printf("quotacheck failed for %d (%s) [%d != %d]\n",
		       d->d_id, reason, (int)a, (int)b);
	xfs_qm_dqtest_print(d);
	if (dqp)
		xfs_qm_dqprint(dqp);
}

STATIC int
xfs_dqtest_cmp2(
	xfs_dqtest_t	*d,
	xfs_dquot_t	*dqp)
{
	int err = 0;
	if (dqp->q_core.d_icount != d->d_icount) {
		xfs_qm_dqtest_failed(d, dqp, "icount mismatch", 
				     dqp->q_core.d_icount, d->d_icount, 0);
		err++;
	}
	if (dqp->q_core.d_bcount != d->d_bcount) {
		xfs_qm_dqtest_failed(d, dqp, "bcount mismatch", 
				     dqp->q_core.d_bcount, d->d_bcount, 0);
		err++;
	}
	if (dqp->q_core.d_blk_softlimit &&
	    dqp->q_core.d_bcount >= dqp->q_core.d_blk_softlimit) {
		if (dqp->q_core.d_btimer == 0 &&
		    dqp->q_core.d_id != 0) {
			printf("%d [%s] [0x%x] BLK TIMER NOT STARTED\n", 
			       (int) d->d_id, XFS_QM_ISUDQ(d) ? "USR" : "PRJ", 
			       d->q_mount);
			err++;
		}
	}
	if (dqp->q_core.d_ino_softlimit &&
	    dqp->q_core.d_icount >= dqp->q_core.d_ino_softlimit) {
		if (dqp->q_core.d_itimer == 0 &&
		    dqp->q_core.d_id != 0) {
			printf("%d [%s] [0x%x] INO TIMER NOT STARTED\n", 
			       (int) d->d_id, XFS_QM_ISUDQ(d) ? "USR" : "PRJ", 
			       d->q_mount);
			err++;
		}
	}
	if (!err) {
		printf("%d [%s] [0x%x] qchecked\n", 
		       (int) d->d_id, XFS_QM_ISUDQ(d) ? "USR" : "PRJ", d->q_mount);
	}
	return (err);
}

STATIC void
xfs_dqtest_cmp(
	xfs_dqtest_t	*d)
{
	xfs_dquot_t	*dqp;
	int		error;

	xfs_qm_dqtest_print(d);
	if (error = xfs_qm_dqget(d->q_mount, NULL, d->d_id, d->dq_flags, 0,
				 &dqp)) {
		xfs_qm_dqtest_failed(d, NULL, "dqget failed", 0, 0, error);
		return;
	}
	xfs_dqtest_cmp2(d, dqp);
	xfs_qm_dqput(dqp);
}

STATIC int
xfs_qm_internalqcheck_dqget(
	xfs_mount_t	*mp,
	xfs_dqid_t	id,
	uint		type,
	xfs_dqtest_t 	**O_dq)
{
	xfs_dqtest_t 	*d;
	xfs_dqhash_t	*h;

	h = DQTEST_HASH(mp, id, type);
	for (d = (xfs_dqtest_t *) h->qh_next; d != NULL; 
	     d = (xfs_dqtest_t *) d->HL_NEXT) {
		/* DQTEST_LIST_PRINT(h, HL_NEXT, "@@@@@ dqtestlist @@@@@"); */
		if (d->d_id == id && mp == d->q_mount) {
			*O_dq = d;
			return (0);
		}
	}
	d = kmem_zalloc(sizeof(xfs_dqtest_t), KM_SLEEP);
	d->dq_flags = type;
	d->d_id = id;
	d->q_mount = mp;
	printf("allocing 0x%x, %d hash %d, [%s]\n", 
	       d, d->d_id,
	       (int) DQTEST_HASHVAL(mp, id),
	       XFS_QM_ISUDQ(d) ? "USR" : "PRJ");
	d->q_hash = h;
	xfs_qm_hashinsert(h, d);
	*O_dq = d;
	return (0);
}	

STATIC int
xfs_qm_internalqcheck_get_dquots(
	xfs_mount_t	*mp,
	xfs_dqid_t	uid,
	xfs_dqid_t	prid,
	xfs_dqtest_t 	**ud,
	xfs_dqtest_t 	**pd)
{
	if (XFS_IS_UQUOTA_ON(mp)) 
		xfs_qm_internalqcheck_dqget(mp, uid, XFS_DQ_USER, ud);
	if (XFS_IS_PQUOTA_ON(mp)) 
		xfs_qm_internalqcheck_dqget(mp, prid, XFS_DQ_PROJ, pd);
	return (0);
}


STATIC int
xfs_qm_internalqcheck_dqadjust(
	xfs_inode_t		*ip,
	xfs_dqtest_t        	*d)
{
	d->d_icount++;
	d->d_bcount += (xfs_qcnt_t)ip->i_d.di_nblocks;
	return (0);
}

/* ARGSUSED */
STATIC int
xfs_qm_internalqcheck_adjust(
	xfs_mount_t     *mp,            /* mount point for filesystem */
        xfs_trans_t     *tp,            /* transaction pointer */
        xfs_ino_t       ino,            /* inode number to get data for */
        void            *buffer,        /* not used */
        daddr_t         bno)            /* starting block of inode cluster */	
{
	xfs_inode_t     	*ip;
	xfs_dqtest_t		*ud, *pd;
	uint			lock_flags;
	extern	dev_t		rootdev;

	ASSERT(XFS_IS_QUOTA_ON(mp));

	/* XXX */
	if (ino == mp->m_sb.sb_rbmino || ino == mp->m_sb.sb_rsumino ||
	    ino == mp->m_sb.sb_uquotino || ino == mp->m_sb.sb_pquotino)
                return (0);
	lock_flags = XFS_ILOCK_SHARED;
        if (xfs_iget(mp, tp, ino, lock_flags, &ip, bno))
                return (0);

        if (ip->i_d.di_mode == 0) {
                xfs_iput(ip, lock_flags);
                return (0);
        }
	
	if (xfs_qm_internalqcheck_get_dquots(mp, 
					     (xfs_dqid_t) ip->i_d.di_uid,
					     (xfs_dqid_t) ip->i_d.di_projid,
					     &ud, &pd)) {
		xfs_iput(ip, lock_flags);
		return (0);
	}
	if (mp->m_dev != rootdev) {
		if (ip->i_d.di_uid == 0)
			printf("uid 0 ino %d, bcount 0x%x\n",
			       (int) ip->i_ino, (int) ip->i_d.di_nblocks);
		if (ip->i_d.di_projid == 0)
			printf("projid 0 ino %d, bcount 0x%x\n",
			       (int) ip->i_ino, (int) ip->i_d.di_nblocks);
	}
	if (XFS_IS_UQUOTA_ON(mp)) {
		ASSERT(ud);
		(void) xfs_qm_internalqcheck_dqadjust(ip, ud);
	}
	if (XFS_IS_PQUOTA_ON(mp)) {
		ASSERT(pd);
		(void) xfs_qm_internalqcheck_dqadjust(ip, pd);
	}
	xfs_iput(ip, lock_flags);
	return (1);
}


/* PRIVATE, debugging */
int
xfs_qm_internalqcheck(
	xfs_mount_t	*mp)
{
	ino64_t 	lastino;
	int 		done, count;
	int		i;
	xfs_dqtest_t	*d, *e;
	xfs_dqhash_t	*h1;


	lastino = 0;
	qmtest_hashmask = 32;
	count = 5;
	done = 0;
	qmtest_nfails = 0;
	
	mutex_lock(&qcheck_lock, PINOD);
	/* There should be absolutely no quota activity while this
	   is going on. */
	qmtest_udqtab = kmem_zalloc(qmtest_hashmask *
				    sizeof(xfs_dqhash_t), KM_SLEEP);
	qmtest_pdqtab = kmem_zalloc(qmtest_hashmask *
				    sizeof(xfs_dqhash_t), KM_SLEEP);
	do {
		/*
		 * Iterate thru all the inodes in the file system,
		 * adjusting the corresponding dquot counters
		 */
		if (xfs_bulkstat(mp, NULL, &lastino, &count, 
					 xfs_qm_internalqcheck_adjust,
					 0, 
					 NULL,
					 &done))
			break;
	} while (! done);

	printf("Checking results against system dquots\n");
	for (i = 0; i < qmtest_hashmask; i++) {
		h1 = &qmtest_udqtab[i];
		for (d = (xfs_dqtest_t *) h1->qh_next; d != NULL; ) {
			xfs_dqtest_cmp(d);
			e = (xfs_dqtest_t *) d->HL_NEXT;
			kmem_free(d, sizeof(xfs_dqtest_t)); 
			d = e;
		}
		h1 = &qmtest_pdqtab[i];
		for (d = (xfs_dqtest_t *) h1->qh_next; d != NULL; ) {
			xfs_dqtest_cmp(d);
			e = (xfs_dqtest_t *) d->HL_NEXT;
			kmem_free(d, sizeof(xfs_dqtest_t)); 
			d = e;
		}	
	}

	if (qmtest_nfails) {
		printf("**************  quotacheck failed  **************\n");
		printf("failures = %d\n", qmtest_nfails);
	} else {
		printf("**************  quotacheck successful! **************\n");
	}
	kmem_free(qmtest_udqtab, qmtest_hashmask * sizeof(xfs_dqhash_t));
	kmem_free(qmtest_pdqtab, qmtest_hashmask * sizeof(xfs_dqhash_t));
	mutex_unlock(&qcheck_lock);
	return (qmtest_nfails);
}

#endif /* DEBUG */
