#ifndef __XFS_QUOTA_H__
#define __XFS_QUOTA_H__
#ident "$Revision: 7.0 $"
/*
 * External Interface to the XFS disk quota subsystem.
 */
struct  xfs_dqhash;
struct  xfs_inode;
struct  xfs_disk_dquot;
struct  xfs_dquot;
struct  xfs_mount;
struct  xfs_trans;
struct  vfs;

/* 
 * We use only 16-bit prid's in the inode, not the 64-bit version in the proc.
 * uid_t is hard-coded to 32 bits in the inode. Hence, an 'id' in a dquot is
 * 32 bits..
 */
typedef __int32_t	xfs_dqid_t;
/*
 * Eventhough users may not have quota limits occupying all 64-bits, 
 * they may need 64-bit accounting. Hence, 64-bit quota-counters,
 * and quota-limits. This is a waste in the common case, but heh ...
 */
typedef __uint64_t	xfs_qcnt_t;
typedef __uint16_t      xfs_qwarncnt_t;

/*
 * Checking XFS_IS_*QUOTA_ON() while holding any inode lock guarantees
 * quota will be not be switched off as long as that inode lock is held.
 */
#define XFS_IS_QUOTA_ON(mp)  	((mp)->m_flags & (XFS_MOUNT_UDQ_ACTIVE | \
						  XFS_MOUNT_PDQ_ACTIVE))
#define XFS_IS_UQUOTA_ON(mp)	((mp)->m_flags & XFS_MOUNT_UDQ_ACTIVE)
#define XFS_IS_PQUOTA_ON(mp) 	((mp)->m_flags & XFS_MOUNT_PDQ_ACTIVE)

/*
 * Flags to tell various functions what to do. Not all of these are meaningful
 * to a single function. None of these XFS_QMOPT_* flags are meant to have
 * persistent values (ie. their values can and will change between versions)
 */
#define XFS_QMOPT_DQLOCK	0x000001 /* dqlock */
#define XFS_QMOPT_DQALLOC	0x000002 /* alloc dquot ondisk if needed */
#define XFS_QMOPT_UQUOTA	0x000004 /* user dquot requested */
#define XFS_QMOPT_PQUOTA	0x000008 /* proj dquot requested */
#define XFS_QMOPT_FORCE_RES	0x000010 /* ignore quota limits */
#define XFS_QMOPT_DQSUSER	0x000020 /* don't cache super users dquot */
#define XFS_QMOPT_INOQCHK	0x000040 /* do a inoquotacheck */
#define XFS_QMOPT_QUOTAOFF	0x000080 /* quotas are being turned off */
#define XFS_QMOPT_UMOUNTING	0x000100 /* filesys is being unmounted */
#define XFS_QMOPT_DOLOG		0x000200 /* log buf changes (in quotacheck) */
#define XFS_QMOPT_DOWARN        0x000400 /* increase warning cnt if necessary */
#define XFS_QMOPT_ILOCKED	0x000800 /* Inode is already locked (excl) */
/* 
 * flags to xfs_trans_mod_dquot to indicate which field needs to be
 * modified.
 */
#define XFS_QMOPT_RES_REGBLKS 	0x001000
#define XFS_QMOPT_RES_RTBLKS	0x002000
#define XFS_QMOPT_BCOUNT	0x004000
#define XFS_QMOPT_ICOUNT	0x008000
#define XFS_QMOPT_RTBCOUNT	0x010000
#define XFS_QMOPT_DELBCOUNT	0x020000
#define XFS_QMOPT_DELRTBCOUNT	0x040000

/*
 * flags for dqflush and dqflush_all.
 */
#define XFS_QMOPT_SYNC		0x100000
#define XFS_QMOPT_ASYNC		0x200000
#define XFS_QMOPT_DELWRI	0x400000

/* 
 * flags to xfs_trans_mod_dquot.
 */
#define XFS_TRANS_DQ_RES_BLKS	XFS_QMOPT_RES_REGBLKS
#define XFS_TRANS_DQ_RES_RTBLKS	XFS_QMOPT_RES_RTBLKS
#define XFS_TRANS_DQ_BCOUNT	XFS_QMOPT_BCOUNT
#define XFS_TRANS_DQ_DELBCOUNT	XFS_QMOPT_DELBCOUNT
#define XFS_TRANS_DQ_ICOUNT	XFS_QMOPT_ICOUNT
#define XFS_TRANS_DQ_RTBCOUNT	XFS_QMOPT_RTBCOUNT
#define XFS_TRANS_DQ_DELRTBCOUNT XFS_QMOPT_DELRTBCOUNT


#define XFS_QMOPT_QUOTALL	(XFS_QMOPT_UQUOTA|XFS_QMOPT_PQUOTA)
#define XFS_QMOPT_RESBLK_MASK	(XFS_QMOPT_RES_REGBLKS | XFS_QMOPT_RES_RTBLKS)

/*
 * This check is done typically without holding the inode lock;
 * that may seem racey, but it is harmless in the context that it is used.
 * The inode cannot go inactive as long a reference is kept, and 
 * therefore if dquot(s) were attached, they'll stay consistent.
 * If, for example, the ownership of the inode changes while
 * we didnt have the inode locked, the appropriate dquot(s) will be
 * attached atomically.
 */
#define XFS_NOT_DQATTACHED(mp, ip) ((XFS_IS_UQUOTA_ON(mp) &&\
				     (ip)->i_udquot == NULL) || \
				    (XFS_IS_PQUOTA_ON(mp) && \
				     (ip)->i_pdquot == NULL))


#define XFS_MOUNT_QUOTA_ALL	(XFS_MOUNT_UDQ_ACCT|XFS_MOUNT_UDQ_ENFD|\
				 XFS_MOUNT_UDQ_CHKD|XFS_MOUNT_PDQ_ACCT|\
				 XFS_MOUNT_PDQ_ENFD|XFS_MOUNT_PDQ_CHKD)
#define XFS_MOUNT_QUOTA_MASK	(XFS_MOUNT_QUOTA_ALL | XFS_MOUNT_UDQ_ACTIVE | \
				 XFS_MOUNT_PDQ_ACTIVE)

#define XFS_PROC_PROJID(c)	  ((c)->p_arsess->as_prid)
#define XFS_IS_REALTIME_INODE(ip) ((ip)->i_d.di_flags & XFS_DIFLAG_REALTIME)


extern struct xfs_qm   *xfs_qm_init(void);
extern void 		xfs_qm_destroy(struct xfs_qm *xqm);
extern int		xfs_qm_dqflush_all(struct xfs_mount *, int);
extern int		xfs_qm_dqattach(struct xfs_inode *, uint);
extern int		xfs_qm_sysent(struct vfs *, int, int, caddr_t);
extern int		xfs_qm_dqpurge_all(struct xfs_mount *, uint);
extern void		xfs_qm_mount_quotainit(struct xfs_mount *, uint);
extern void		xfs_qm_unmount_quotadestroy(struct xfs_mount *);
extern int		xfs_qm_mount_quotas(struct xfs_mount *);
extern void 		xfs_qm_unmount_quotas(struct xfs_mount *);
extern void		xfs_qm_dqdettach_inode(struct xfs_inode *ip);
extern void 		xfs_qm_sync(struct xfs_mount *mp, short flags);
extern int		xfs_qm_check_inoquota(struct xfs_dquot *dqp);
extern int		xfs_qm_check_inoquota2(struct xfs_mount *mp,
					       struct xfs_dquot *udqp,
					       struct xfs_dquot *pdqp);

/*
 * dquot interface
 */
extern void		xfs_dqlock(struct xfs_dquot *dqp);
extern void		xfs_dqunlock(struct xfs_dquot *dqp);
extern void		xfs_dqunlock_nonotify(struct xfs_dquot *dqp);
extern void		xfs_dqlock2(struct xfs_dquot *d1,
				    struct xfs_dquot *d2);
extern void 		xfs_qm_dqput(struct xfs_dquot *dqp);
extern void 		xfs_qm_dqrele(struct xfs_dquot *dqp);
extern xfs_dqid_t	xfs_qm_dqid(struct xfs_dquot *dqp);
extern int		xfs_qm_dqget(struct xfs_mount *mp, 
				     struct xfs_inode *ip,
				      xfs_dqid_t id,
				      uint type, uint doalloc, 
				      struct xfs_dquot **O_dqpp);
extern int 		xfs_qm_dqcheck(struct xfs_disk_dquot *, 
				       xfs_dqid_t, char *);

/*
 * vnodeops stuff
 */
extern struct xfs_dquot *	xfs_qm_vop_chown(struct xfs_trans *tp, 
						 struct xfs_inode *ip, 
						 struct xfs_dquot **olddq,
						 struct xfs_dquot *newdq);
extern int		xfs_qm_vop_dqalloc(struct xfs_mount	*mp,
					   struct xfs_inode	*dp,
					   uid_t	uid,
					   xfs_prid_t	prid,
					   uint		flags,
					   struct xfs_dquot	**udqpp,
					   struct xfs_dquot	**pdqpp);

extern int		xfs_qm_vop_chown_dqalloc(struct xfs_mount	*mp,
						 struct xfs_inode	*ip,
						 int			mask,
						 uid_t			uid,
						 xfs_prid_t		prid,
						 struct xfs_dquot	**udqpp,
						 struct xfs_dquot	**pdqpp);

extern int		xfs_qm_vop_chown_reserve(struct xfs_trans	*tp,
						 struct xfs_inode	*ip,
						 struct xfs_dquot	*udqp,
						 struct xfs_dquot	*pdqp,
						 uint		privileged);

extern int		xfs_qm_vop_rename_dqattach(struct xfs_inode	**i_tab);
extern void		xfs_qm_vop_dqattach_and_dqmod_newinode(
						struct xfs_trans *tp,
						struct xfs_inode *ip,
						struct xfs_dquot *udqp,	
						struct xfs_dquot *pdqp);


/*
 * Dquot Transaction interface
 */
extern void 		xfs_trans_alloc_dqinfo(struct xfs_trans *tp);
extern void 		xfs_trans_free_dqinfo(struct xfs_trans *tp);
extern void		xfs_trans_dup_dqinfo(struct xfs_trans *otp, 
					     struct xfs_trans *ntp);
extern void		xfs_trans_mod_dquot(struct xfs_trans *tp, 
					    struct xfs_dquot *dqp,
					    uint field, long delta);
extern int		xfs_trans_mod_dquot_byino(struct xfs_trans *tp, 
						  struct xfs_inode *ip,
						  uint field, long delta);
extern void		xfs_trans_apply_dquot_deltas(struct xfs_trans *tp);
extern void		xfs_trans_unreserve_and_mod_dquots(struct xfs_trans *tp);

extern int		xfs_trans_reserve_quota(struct xfs_trans *tp,
						struct xfs_inode *ip,
						long nblks,
						uint type);


extern int		xfs_trans_reserve_quota_bydquots(struct xfs_trans *tp,
							 struct xfs_dquot *udqp,
							 struct xfs_dquot *pdqp,
							 long nblks,
							 uint flags);
extern void		xfs_trans_log_dquot(struct xfs_trans *tp, 
					    struct xfs_dquot *dqp);
extern void		xfs_trans_dqjoin(struct xfs_trans *tp, 
					 struct xfs_dquot *dqp);



/* 
 * Regular disk block quota reservations 
 */
#define 	xfs_trans_reserve_blkquota(tp, ip, nblks) \
xfs_trans_reserve_quota(tp, ip, nblks, XFS_QMOPT_RES_REGBLKS)
						  
#define 	xfs_trans_unreserve_blkquota(tp, ip, nblks) \
xfs_trans_reserve_quota(tp, ip, -nblks, XFS_QMOPT_RES_REGBLKS)

#define 	xfs_trans_reserve_blkquota_bydquots(tp, udq, pdq, nblks, f) \
xfs_trans_reserve_quota_bydquots(tp, udq, pdq, nblks, f|XFS_QMOPT_RES_REGBLKS) 

#define 	xfs_trans_unreserve_blkquota_bydquots(tp, udq, pdq, nblks, f) \
xfs_trans_reserve_quota_bydquots(tp, udq, pdq, -nblks, f|XFS_QMOPT_RES_REGBLKS)

/*
 * Realtime disk block quota reservations 
 */
#define 	xfs_trans_reserve_rtblkquota(mp, tp, ip, nblks) \
xfs_trans_reserve_quota(tp, ip, nblks, XFS_QMOPT_RES_RTBLKS)
						  
#define 	xfs_trans_unreserve_rtblkquota(tp, ip, nblks) \
xfs_trans_reserve_quota(tp, ip, -nblks, XFS_QMOPT_RES_RTBLKS)

#define 	xfs_trans_reserve_rtblkquota_bydquots(mp, tp, udq, pdq, nblks, f) \
xfs_trans_reserve_quota_bydquots(mp, tp, udq, pdq, nblks, f|XFS_QMOPT_RES_RTBLKS) 

#define 	xfs_trans_unreserve_rtblkquota_bydquots(tp, udq, pdq, nblks) \
xfs_trans_reserve_quota_bydquots(tp, udq, pdq, -nblks, XFS_QMOPT_RES_RTBLKS)

#endif
