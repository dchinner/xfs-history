#ifndef _XFS_QUOTA_PRIV_H_
#define _XFS_QUOTA_PRIV_H_

extern time_t	time;
struct xfs_dquotblk;

/*
 * Number of bmaps that we ask from bmapi when doing a quotacheck.
 * We make this restriction to keep the memory usage to a minimum.
 */
#define XFS_DQITER_MAP_SIZE	10
	
/* Number of dquots that fit in to a dquot block */
#define XFS_QM_DQPERBLK(mp)	((mp)->m_quotainfo->qi_dqperchunk)

#define XFS_ISLOCKED_INODE(ip)		(ismrlocked(&(ip)->i_lock, \
					    MR_UPDATE | MR_ACCESS) != 0)
#define XFS_ISLOCKED_INODE_EXCL(ip)	(ismrlocked(&(ip)->i_lock, \
					    MR_UPDATE) != 0)

#define XFS_DQ_IS_ADDEDTO_TRX(t, d)	((d)->q_transp == (t))

#define QM_FLLOCK	qm_dqfreelist.qh_lock
#define QM_FLNEXT	qm_dqfreelist.qh_next

#define QI_MPL_LIST	m_quotainfo->qi_dqlist
#define QI_MPLLOCK	m_quotainfo->qi_dqlist.qh_lock
#define QI_MPLNEXT	m_quotainfo->qi_dqlist.qh_next
#define QI_MPLNDQUOTS	m_quotainfo->qi_dqlist.qh_nelems

#define QI_UQIP		m_quotainfo->qi_uquotaip
#define QI_PQIP		m_quotainfo->qi_pquotaip
#define QI_DQCHUNKLEN	m_quotainfo->qi_dqchunklen
#define QI_BTIMELIMIT	m_quotainfo->qi_btimelimit
#define QI_RTBTIMELIMIT	m_quotainfo->qi_rtbtimelimit
#define QI_ITIMELIMIT	m_quotainfo->qi_itimelimit
#define QI_BWARNLIMIT	m_quotainfo->qi_bwarnlimit
#define QI_IWARNLIMIT	m_quotainfo->qi_iwarnlimit
#define QI_QOFFLOCK	m_quotainfo->qi_quotaofflock
#define QI_MPLRECLAIMS	m_quotainfo->qi_dqreclaims


#define XQMLCK(h)			mutex_lock(&((h)->qh_lock), PINOD)
#define XQMUNLCK(h)			mutex_unlock(&((h)->qh_lock))
#define XQMISLCKD(h)			(mutex_mine(&((h)->qh_lock)))

#define XFS_DQ_HASH_LOCK(h)		XQMLCK(h)
#define XFS_DQ_HASH_UNLOCK(h)		XQMUNLCK(h)
#define XFS_DQ_IS_HASH_LOCKED(h)	XQMISLCKD(h)

#define xfs_qm_mplist_lock(mp)		XQMLCK(&((mp)->QI_MPL_LIST))
#define xfs_qm_mplist_unlock(mp)	XQMUNLCK(&((mp)->QI_MPL_LIST))
#define XFS_QM_IS_MPLIST_LOCKED(mp)	XQMISLCKD(&((mp)->QI_MPL_LIST))

#define xfs_qm_freelist_lock(qm) 	XQMLCK(&((qm)->qm_dqfreelist))
#define xfs_qm_freelist_unlock(qm) 	XQMUNLCK(&((qm)->qm_dqfreelist))
#define XFS_QM_IS_FREELIST_LOCKED(qm) 	XQMISLCKD(&((qm)->qm_dqfreelist))

/*
 * Hash into a bucket in the dquot hash table, based on <mp, id>.
 */
#define XFS_DQ_HASHVAL(mp, id) (((__psunsigned_t)(mp) + \
				 (__psunsigned_t)(id)) & \
				(G_xqm->qm_dqhashmask - 1))
#define XFS_DQ_HASH(mp, id, type)   (type == XFS_DQ_USER ? \
				     (G_xqm->qm_usr_dqhtable + \
				      XFS_DQ_HASHVAL(mp, id)) : \
				     (G_xqm->qm_prj_dqhtable + \
				      XFS_DQ_HASHVAL(mp, id)))
#define XFS_IS_DQTYPE_ON(mp, type)   (type == XFS_DQ_USER ? \
				      XFS_IS_UQUOTA_ON(mp):XFS_IS_PQUOTA_ON(mp))
#define XFS_IS_DQUOT_UNINITIALIZED(dqp) \
                   (dqp->q_core.d_blk_hardlimit == 0ULL && \
		    dqp->q_core.d_blk_softlimit == 0ULL && \
		    dqp->q_core.d_rtb_hardlimit == 0ULL && \
		    dqp->q_core.d_rtb_softlimit == 0ULL && \
		    dqp->q_core.d_ino_hardlimit == 0ULL && \
		    dqp->q_core.d_ino_softlimit == 0ULL && \
		    dqp->q_core.d_bcount == 0ULL &&	   \
		    dqp->q_core.d_rtbcount == 0ULL &&	   \
		    dqp->q_core.d_icount == 0ULL)
			     
#define HL_PREVP	dq_hashlist.ql_prevp
#define HL_NEXT		dq_hashlist.ql_next
#define MPL_PREVP	dq_mplist.ql_prevp
#define MPL_NEXT	dq_mplist.ql_next


#define _LIST_REMOVE(h, dqp, PVP, NXT)				\
        {							\
                 xfs_dquot_t *d;				\
                 if ((d) = (dqp)->NXT)				\
                         (d)->PVP = (dqp)->PVP;			\
                 *((dqp)->PVP) = d;				\
                 (dqp)->NXT = NULL;				\
                 (dqp)->PVP = NULL;				\
                 (h)->qh_version++;				\
                 (h)->qh_nelems--;				\
	}

#define _LIST_INSERT(h, dqp, PVP, NXT)				\
	{							\
		 xfs_dquot_t *d;				\
		 if ((d) = (h)->qh_next)			\
			 (d)->PVP = &((dqp)->NXT);		\
		 (dqp)->NXT = d;				\
		 (dqp)->PVP = &((h)->qh_next);			\
		 (h)->qh_next = dqp;				\
		 (h)->qh_version++;				\
                 (h)->qh_nelems++;				\
	 }

#define FOREACH_DQUOT_IN_MP(dqp, mp) \
	for ((dqp) = (mp)->QI_MPLNEXT; (dqp) != NULL; (dqp) = (dqp)->MPL_NEXT) 

#define FOREACH_DQUOT_IN_FREELIST(dqp, qlist) 	\
for ((dqp) = (qlist)->qh_next; (dqp) != (xfs_dquot_t *)(qlist); \
     (dqp) = (dqp)->dq_flnext)		
		
#define XQM_HASHLIST_INSERT(h, dqp)	\
         _LIST_INSERT(h, dqp, HL_PREVP, HL_NEXT)

#define XQM_FREELIST_INSERT(h, dqp)	\
         xfs_qm_freelist_append(h, dqp)

#define XQM_MPLIST_INSERT(h, dqp)	\
         _LIST_INSERT(h, dqp, MPL_PREVP, MPL_NEXT)
		
#define XQM_HASHLIST_REMOVE(h, dqp)	\
         _LIST_REMOVE(h, dqp, HL_PREVP, HL_NEXT)
#define XQM_FREELIST_REMOVE(dqp)	\
         xfs_qm_freelist_unlink(dqp)
#define XQM_MPLIST_REMOVE(h, dqp)	\
        { _LIST_REMOVE(h, dqp, MPL_PREVP, MPL_NEXT); \
	  (dqp)->q_mount->QI_MPLRECLAIMS++; }

#define XFS_DQ_IS_LOGITEM_INITD(dqp)	((dqp)->q_logitem.qli_dquot == (dqp))

#define XFS_QM_DQP_TO_DQACCT(tp, dqp)	(XFS_QM_ISUDQ(dqp) ? \
					 (tp)->t_dqinfo->dqa_usrdquots : \
					 (tp)->t_dqinfo->dqa_prjdquots)
#define XFS_IS_SUSER_DQUOT(dqp)		((dqp)->q_core.d_id == 0)

#define XFS_PURGE_INODE(vp) 		\
	{ 				\
	  vmap_t dqvmap;		\
	  VMAP((vp), dqvmap); 		\
	  VN_RELE(vp);			\
	  vn_purge((vp), &dqvmap);	\
        }

#define DQFLAGTO_TYPESTR(d) 	(((d)->dq_flags & XFS_DQ_USER) ? "USR" : \
				 (((d)->dq_flags & XFS_DQ_PROJ) ? "PRJ" : "???"))
#define DQFLAGTO_DIRTYSTR(d)	(XFS_DQ_IS_DIRTY(d) ? "DIRTY" : "NOTDIRTY")

#endif
