#ifndef _XFS_DQUOT__H_
#define _XFS_DQUOT__H_

#ident "$Revision: 1.2 $"

/* 
 * Dquots are structures that hold quota information about a user or a project,
 * much like inodes are for files. In fact, dquots share many characteristics
 * with inodes. However, dquots can also be a centralized resource, relative
 * to a collection of inodes. In this respect, dquots share some characteristics
 * of the superblock.
 * XFS dquots exploit both those in its algorithms. They make every attempt
 * to not be a bottleneck when quotas are on, and have minimal impact, if at all,
 * when quotas are off.
 */
#define XFS_DQUOT_MAGIC	 	0x4451	 	/* 'DQ' */
#define XFS_DQUOT_VERSION	(__int8_t)0x01	/* latest version number */

/* 
 * This is the main portion of the on-disk representation of quota 
 * information for a user. This is the q_core of the xfs_dquot_t that
 * is kept in kernel memory. We pad this with some more expansion room
 * to construct the on disk structure.
 */
typedef struct	xfs_disk_dquot {
/*16*/	__uint16_t	d_magic;	/* dquot magic = XFS_DQUOT_MAGIC */
/*8 */	__int8_t	d_version;	/* dquot version */
/*8 */	__int8_t	d_flags;	/* misc flags: type(USR/PRJ) for now */
/*32*/	xfs_dqid_t	d_id;		/* user id or proj id */
/*64*/	xfs_qcnt_t	d_blk_hardlimit;/* absolute limit on disk blks */
/*64*/	xfs_qcnt_t	d_blk_softlimit;/* preferred limit on disk blks */
/*64*/	xfs_qcnt_t	d_ino_hardlimit;/* maximum # allocated inodes */
/*64*/	xfs_qcnt_t	d_ino_softlimit;/* preferred inode limit */
/*64*/	xfs_qcnt_t	d_bcount;	/* disk blocks owned by the user */
/*64*/	xfs_qcnt_t	d_icount;	/* inodes owned by the user */
/*32*/	__int32_t	d_itimer;	/* zero if within inode limits if not, 
					   this is when we refuse service */
/*32*/	__int32_t	d_btimer;	/* similar to above; for disk blocks */
/*16*/	xfs_qwarncnt_t  d_iwarns;       /* warnings issued wrt num inodes */
/*16*/	xfs_qwarncnt_t  d_bwarns;       /* warnings issued wrt disk blocks */
/*64*/	xfs_qcnt_t	d_rtb_hardlimit;/* absolute limit on realtime blks */
/*64*/	xfs_qcnt_t	d_rtb_softlimit;/* preferred limit on RT disk blks */
/*64*/	xfs_qcnt_t	d_rtbcount;	/* realtime blocks owned */
/*32*/	__int32_t	d_rtbtimer;	/* similar to above; for RT disk blocks */
/*16*/	xfs_qwarncnt_t  d_rtbwarns;     /* warnings issued wrt RT disk blocks */
/*16*/	__uint16_t	d_pad;
} xfs_disk_dquot_t;

/*
 * This is what goes on disk. This is separated from the xfs_disk_dquot because
 * carrying the unnecessary padding would be a waste of memory.
 */
typedef struct xfs_dqblk {
	xfs_disk_dquot_t  dd_diskdq;
	char              dd_fill[32];	/* filling for posterity */
} xfs_dqblk_t;

/* 
 * The hash chain headers (hash buckets)
 */
typedef struct xfs_dqhash {
	struct xfs_dquot *qh_next;
	mutex_t		  qh_lock;
	uint		  qh_version;	/* ever increasing version */
#ifdef QUOTADEBUG
	uint		  qh_flags;	/* XXXtmp: for debugging */
#endif
	uint		  qh_nelems;	/* number of dquots on the list */
} xfs_dqhash_t;

typedef struct xfs_dqlink {
	struct xfs_dquot  *ql_next; 	/* forward link */
	struct xfs_dquot **ql_prevp;	/* pointer to prev ql_next */
} xfs_dqlink_t;


/*
 * log format struct for dquots.
 * The first two fields must be the type and size fitting into
 * 32 bits : log_recovery code assumes that.
 */
typedef struct xfs_dq_logformat {
	__uint16_t		qlf_type;      /* dquot log item type */
	__uint16_t		qlf_size;      /* size of this item */
	xfs_dqid_t		qlf_id;	       /* usr/proj id number : 32 bits */
	__int64_t		qlf_blkno;     /* blkno of dquot buffer */
	__int16_t		qlf_len;       /* len of dquot buffer */
	__int16_t		qlf_boffset;   /* off of dquot in buffer */
	char			qlf_padding[4];/* for future use */
} xfs_dq_logformat_t;

typedef struct xfs_dq_logitem {
	xfs_log_item_t		 qli_item;	   /* common portion */
	struct xfs_dquot	*qli_dquot;	   /* dquot ptr */
	xfs_lsn_t		 qli_flush_lsn;	   /* lsn at last flush */
	unsigned short		 qli_flags;	   /* XXX misc flags NEEDED ???*/
	struct buf		*qli_bp;	   /* locked dquot buffer */
	struct proc		*qli_bp_owner;	   /* bp owner proc */
	xfs_dq_logformat_t	 qli_format;	   /* logged structure */
} xfs_dq_logitem_t;

struct xfs_mount;
struct xfs_trans;

/*
 * This is the marker which is designed to occupy the first few
 * bytes of the xfs_dquot_t structure. Even inside this, the freelist pointers
 * must come first.
 * This serves as the marker ("sentinel") when we have to restart list
 * iterations because of locking considerations.
 */
typedef struct xfs_dqmarker {
	struct xfs_dquot*dqm_flnext;	/* link to the freelist: must be first */
	struct xfs_dquot*dqm_flprev;
	xfs_dqlink_t	 dqm_mplist;	/* link to mount's list of dquots */
	xfs_dqlink_t	 dqm_hashlist;	/* link to the hash chain */
	uint             dqm_flags;     /* various flags (XFS_DQ_*) */
} xfs_dqmarker_t;

/*
 * The incore dquot structure
 */
typedef struct xfs_dquot {
	xfs_dqmarker_t	 q_lists;	/* list ptrs, q_flags (marker) */
	xfs_dqhash_t	*q_hash;        /* the hashchain header */
	struct xfs_mount*q_mount;	/* filesystem this relates to */
	struct xfs_trans*q_transp;	/* trans this belongs to currently */
	uint		 q_nrefs;       /* # active refs from inodes */
	daddr_t		 q_blkno;	/* blkno of dquot buffer */
	dev_t		 q_dev;		/* dev for this dquot */
	int		 q_bufoffset;	/* off of dq in buffer (# dquots) */
	xfs_fileoff_t    q_fileoffset;	/* offset in quotas file */

	struct xfs_dquot*q_pdquot; 	/* proj dquot, hint only */
	xfs_disk_dquot_t q_core;	/* actual usage & quotas */
	xfs_dq_logitem_t q_logitem;	/* dquot log item */
	xfs_qcnt_t	 q_res_bcount;	/* total nblks used + nblks reserved */
	xfs_qcnt_t	 q_res_rtbcount;/* total inodes used + reserved */
	mutex_t 	 q_qlock;       /* quota lock */
	sema_t           q_flock;       /* flush lock */
	uint             q_pincount;    /* pin count for this dquot */
	sv_t             q_pinwait;     /* sync var for pinning */
#ifdef DQUOT_TRACING
	struct ktrace   *q_trace;       /* trace header structure */
#endif
} xfs_dquot_t;

/*
 * flags for q_flags field in the dquot.
 */
#define XFS_DQ_USER	 	0x0001		/* a user quota */
#define XFS_DQ_PROJ	 	0x0002		/* a project quota */
#define XFS_DQ_LOCKED		0x0004		/* dquot is locked */
#define XFS_DQ_FLOCKED		0x0008		/* flush lock taken */
#define XFS_DQ_DIRTY		0x0010		/* dquot is dirty */
#define XFS_DQ_WANT		0x0020		/* for lookup/reclaim race */
#define XFS_DQ_INACTIVE		0x0040		/* dq off mplist & hashlist */
#define XFS_DQ_MARKER		0x0080		/* sentinel */


#define dq_flnext	q_lists.dqm_flnext
#define dq_flprev	q_lists.dqm_flprev
#define dq_mplist	q_lists.dqm_mplist
#define dq_hashlist	q_lists.dqm_hashlist
#define dq_flags	q_lists.dqm_flags

#define XFS_DQHOLD(dqp)		((dqp)->q_nrefs++)

/*
 * Quota Accounting flags
 */
#define XFS_ALL_QUOTA_ACCT	(XFS_MOUNT_UDQ_ACCT | XFS_MOUNT_PDQ_ACCT)
#define XFS_ALL_QUOTA_ENFD	(XFS_MOUNT_UDQ_ENFD | XFS_MOUNT_PDQ_ENFD)
#define XFS_ALL_QUOTA_CHKD	(XFS_MOUNT_UDQ_CHKD | XFS_MOUNT_PDQ_CHKD)
#define XFS_ALL_QUOTA_ACTV	(XFS_MOUNT_UDQ_ACTIVE | XFS_MOUNT_PDQ_ACTIVE)
#define XFS_ALL_QUOTA_ACCT_ENFD	(XFS_MOUNT_UDQ_ACCT|XFS_MOUNT_UDQ_ENFD|\
				 XFS_MOUNT_PDQ_ACCT|XFS_MOUNT_PDQ_ENFD)	

#define	XFS_IS_QUOTA_RUNNING(mp)  ((mp)->m_flags & XFS_ALL_QUOTA_ACCT)
#define XFS_IS_UQUOTA_RUNNING(mp) ((mp)->m_flags & XFS_MOUNT_UDQ_ACCT)
#define XFS_IS_PQUOTA_RUNNING(mp) ((mp)->m_flags & XFS_MOUNT_PDQ_ACCT)

/* 
 * Quota Limit Enforcement flags
 */
#define	XFS_IS_QUOTA_ENFORCED(mp) 	((mp)->m_flags & XFS_ALL_QUOTA_ENFD)
#define XFS_IS_UQUOTA_ENFORCED(mp)	((mp)->m_flags & XFS_MOUNT_UDQ_ENFD)
#define XFS_IS_PQUOTA_ENFORCED(mp)	((mp)->m_flags & XFS_MOUNT_PDQ_ENFD)

#define XFS_DQ_IS_LOCKED(dqp)	(mutex_mine(&(dqp)->q_qlock))

/*
 * The following three routines simply manage the q_flock
 * semaphore embedded in the dquot.  This semaphore synchronizes
 * processes attempting to flush the in-core dquot back to disk.
 */
#define xfs_dqflock(dqp)	 { psema(&((dqp)->q_flock), PINOD | PRECALC);\
				   (dqp)->dq_flags |= XFS_DQ_FLOCKED; }  
#define xfs_dqfunlock(dqp)	 { ASSERT(valusema(&((dqp)->q_flock)) <= 0); \
                                   vsema(&((dqp)->q_flock)); \
			           (dqp)->dq_flags &= ~(XFS_DQ_FLOCKED); }

#define XFS_DQ_PINLOCK(dqp)	   mutex_spinlock( \
				     &(XFS_DQ_TO_QINF(dqp)->qi_pinlock))
#define XFS_DQ_PINUNLOCK(dqp, s)   mutex_spinunlock( \
				     &(XFS_DQ_TO_QINF(dqp)->qi_pinlock), s)

#define XFS_DQ_IS_FLUSH_LOCKED(dqp) (valusema(&((dqp)->q_flock)) <= 0)
#define XFS_DQ_IS_ON_FREELIST(dqp)  ((dqp)->dq_flnext != (dqp))
#define XFS_DQ_IS_DIRTY(dqp)	((dqp)->dq_flags & XFS_DQ_DIRTY)
#define XFS_QM_ISUDQ(dqp)	((dqp)->dq_flags & XFS_DQ_USER)
#define XFS_DQ_TO_QINF(dqp)	((dqp)->q_mount->m_quotainfo)
#define XFS_DQ_TO_QIP(dqp)  	(XFS_QM_ISUDQ(dqp) ? \
				 XFS_DQ_TO_QINF(dqp)->qi_uquotaip : \
				 XFS_DQ_TO_QINF(dqp)->qi_pquotaip)

#define XFS_IS_THIS_QUOTA_OFF(d) (! (XFS_QM_ISUDQ(d) ? \
				     (XFS_IS_UQUOTA_ON((d)->q_mount)) : \
				     (XFS_IS_PQUOTA_ON((d)->q_mount))))
#ifdef DQUOT_TRACING
/*
 * Dquot Tracing stuff.
 */
#define DQUOT_TRACE_SIZE	64
#define DQUOT_KTRACE_ENTRY	1
extern void		xfs_dqtrace_entry(xfs_dquot_t *dqp, char *func);
#else
#define xfs_dqtrace_entry(a,b)	
#endif
#ifdef QUOTADEBUG
extern void 		xfs_qm_dqprint(xfs_dquot_t *);
#else
#define xfs_qm_dqprint(a)
#endif


extern xfs_dquot_t 	*xfs_qm_dqinit(xfs_mount_t *, xfs_dqid_t, uint);
extern void		xfs_qm_dqdestroy(xfs_dquot_t *);
extern int		xfs_qm_dqflush(xfs_dquot_t *, uint);
extern xfs_dquot_t *	xfs_qm_dqpurge(xfs_dquot_t *);
extern void		xfs_qm_dqpin(xfs_dquot_t *);
extern void		xfs_qm_dqunpin(xfs_dquot_t *);
extern void		xfs_qm_dqunpin_wait(xfs_dquot_t *);
extern int		xfs_qm_dqlock_nowait(xfs_dquot_t *);
extern int		xfs_qm_dqflock_nowait(xfs_dquot_t *);
extern void 		xfs_qm_dqflock_pushbuf_wait(xfs_dquot_t *dqp);
extern void		xfs_qm_adjust_dqtimers(xfs_mount_t *,
					       xfs_disk_dquot_t	*);
extern int		xfs_qm_dqwarn(xfs_disk_dquot_t *, uint);


#endif /* _XFS_DQUOT__H_ */
