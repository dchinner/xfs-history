#ifndef _FS_XFS_MOUNT_H
#define	_FS_XFS_MOUNT_H

#ident	"$Revision: 1.43 $"

struct cred;
struct xfs_ihash;

typedef struct xfs_mount {
	struct vfs		*m_vfsp;	/* ptr to vfs */
	xfs_tid_t		m_tid;		/* next unused tid for fs */
	lock_t			m_ail_lock;	/* fs AIL mutex */
	xfs_ail_entry_t		m_ail;		/* fs active log item list */
	uint			m_ail_gen;	/* fs AIL generation count */
	xfs_ail_ticket_t	*m_ail_wait;	/* list of AIL push waiters*/
	xfs_trans_t		*m_async_trans;	/* list of async transactions */
	lock_t			m_async_lock;	/* async trans list mutex */
	xfs_sb_t		m_sb;		/* copy of fs superblock */
	lock_t			m_sb_lock;	/* sb counter mutex */
	buf_t			*m_sb_bp;	/* buffer for superblock */
	char			*m_fsname; /* filesystem name */
	dev_t			m_dev;		/* dev of fs meta-data */
	dev_t			m_logdev;	/* dev of fs log data */
	dev_t			m_rtdev;	/* dev of fs realtime data */
	int			m_bsize;	/* fs logical block size */
	xfs_agnumber_t		m_agfrotor;	/* last ag where space found */
	xfs_agnumber_t		m_agirotor;	/* last ag dir inode alloced */
	lock_t			m_ipinlock;	/* inode pinning mutex */
	struct xfs_ihash	*m_ihash;	/* fs private inode hash table*/
	ulong			m_ihashmask;	/* fs inode hash size - 1 */
	struct xfs_inode	*m_inodes;	/* active inode list */
	sema_t			m_ilock;	/* inode list mutex */
	uint			m_ireclaims;	/* count of calls to reclaim*/
	uint			m_readio_log;	/* min read size log bytes */
	uint			m_readio_blocks; /* min read size blocks */
	uint			m_writeio_log;	/* min write size log bytes */
	uint			m_writeio_blocks; /* min write size blocks */
	void			*m_log;		/* log specific stuff */
	int			m_logbufs;	/* number of log buffers */
	int			m_logbsize;	/* size of each log buffer */
	uint			m_rsumlevels;	/* rt summary levels */
	uint			m_rsumsize;	/* size of rt summary, bytes */
	struct xfs_inode	*m_rbmip;	/* pointer to bitmap inode */
	struct xfs_inode	*m_rsumip;	/* pointer to summary inode */
	struct xfs_inode	*m_rootip;	/* pointer to root directory */
	struct vnode 		*m_ddevp;	/* ptr to data dev vnode */
	struct vnode 		*m_logdevp;	/* ptr to log dev vnode */
	struct vnode 		*m_rtdevp;	/* prt to rt dev vnode   */
	__uint8_t		m_dircook_elog;	/* log d-cookie entry bits */
	__uint8_t		m_blkbit_log;	/* blocklog + NBBY */
	__uint8_t		m_blkbb_log;	/* blocklog - BBSHIFT */
	__uint8_t		m_agno_log;	/* log #ag's */
	uint			m_blockmask;	/* sb_blocksize-1 */
	uint			m_blockwsize;	/* sb_blocksize in words */
	uint			m_blockwmask;	/* blockwsize-1 */
	uint			m_alloc_mxr[2];	/* XFS_ALLOC_BLOCK_MAXRECS */
	uint			m_alloc_mnr[2];	/* XFS_ALLOC_BLOCK_MINRECS */
	uint			m_bmap_dmxr[4];	/* XFS_BMAP_BLOCK_DMAXRECS */
	uint			m_bmap_dmnr[4];	/* XFS_BMAP_BLOCK_DMINRECS */
	uint			m_bmap_ext_mxr;	/* XFS_BMAP_EXT_MAXRECS */
	uint			m_inobt_mxr[2];	/* XFS_INOBT_BLOCK_MAXRECS */
	uint			m_inobt_mnr[2];	/* XFS_INOBT_BLOCK_MINRECS */
	uint			m_ag_maxlevels;	/* XFS_AG_MAXLEVELS */
	uint			m_bm_maxlevels;	/* XFS_BM_MAXLEVELS */
	uint			m_in_maxlevels;	/* XFS_IN_MAXLEVELS */
	struct xfs_perag	*m_perag;	/* per-ag accounting info */
	mrlock_t		m_peraglock;	/* lock for m_perag (pointer) */
	sema_t			m_growlock;	/* growfs mutex */
	xfs_extlen_t		m_rbmrotor;	/* rt bitmap allocation rotor */
	int			m_fixedfsid[2];	/* unchanged for life of FS */
	uint			m_dmevmask;	/* DMI events for this FS */
	uint			m_flags;	/* global mount flags */
} xfs_mount_t;

/*
 * Flags for m_flags.
 */
#define	XFS_MOUNT_WSYNC	0x00000001

/*
 * Default minimum read and write sizes.
 */
#define	XFS_READIO_LOG	16
#define	XFS_WRITEIO_LOG	16

/*
 * Macros for getting from mount to vfs and back.
 */
#define	XFS_MTOVFS(mp)		((mp)->m_vfsp)
#define	XFS_VFSTOM(vfsp)	((xfs_mount_t *) (vfsp)->vfs_data)

 
/*
 * This structure is for use by the xfs_mod_incore_sb_batch() routine.
 */
typedef struct xfs_mod_sb {
	uint	msb_field;	/* Field to modify, see below */
	int	msb_delta;	/* change to make to the specified field */
} xfs_mod_sb_t;

#define	XFS_MOUNT_ILOCK(mp)	psema(&((mp)->m_ilock), PINOD)
#define	XFS_MOUNT_IUNLOCK(mp)	vsema(&((mp)->m_ilock))
#define	XFS_SB_LOCK(mp)		splockspl((mp)->m_sb_lock, splhi)
#define	XFS_SB_UNLOCK(mp,s)	spunlockspl((mp)->m_sb_lock,(s))

#define	AIL_LOCK(mp)		(splockspl((mp)->m_ail_lock, splhi))
#define	AIL_TRYLOCK(mp)		(_trylock((mp)->m_ail_lock, splhi))
#define	AIL_UNLOCK(mp,s)	(spunlockspl((mp)->m_ail_lock, s))


#ifdef SIM
xfs_mount_t	*xfs_mount(dev_t, dev_t, dev_t);
void		xfs_umount(xfs_mount_t *);
#endif

void		xfs_mod_sb(xfs_trans_t *, int);
xfs_mount_t	*xfs_mount_init(void);
void		xfs_mount_free(xfs_mount_t *mp);
int		xfs_mountfs(struct vfs *, dev_t);
int		xfs_unmountfs(xfs_mount_t *, int, struct cred *);
int		xfs_mod_incore_sb(xfs_mount_t *, uint, int);
int		xfs_mod_incore_sb_batch(xfs_mount_t *, xfs_mod_sb_t *, uint);
buf_t		*xfs_getsb(xfs_mount_t *, int);

#endif	/* !_FS_XFS_MOUNT_H */
