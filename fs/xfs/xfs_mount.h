#ifndef _FS_XFS_MOUNT_H
#define	_FS_XFS_MOUNT_H

#ident	"$Revision: 1.67 $"

struct buf;
struct cred;
struct vfs;
struct vnode;
struct xfs_ihash;
struct xfs_inode;
struct xfs_perag;

typedef struct xfs_trans_reservations {
	uint	tr_write;	/* extent alloc trans */
	uint	tr_itruncate;	/* truncate trans */
	uint	tr_rename;	/* rename trans */
	uint	tr_link;	/* link trans */
	uint	tr_remove;	/* unlink trans */
	uint	tr_symlink;	/* symlink trans */
	uint	tr_create;	/* create trans */
	uint	tr_mkdir;	/* mkdir trans */
	uint	tr_ifree;	/* inode free trans */
	uint	tr_ichange;	/* inode update trans */
	uint	tr_growdata;	/* fs data section grow trans */
	uint	tr_swrite;	/* sync write inode trans */
	uint	tr_addafork;	/* cvt inode to attributed trans */
	uint	tr_writeid;	/* write setuid/setgid file */
	uint	tr_attrinval;	/* attr fork buffer invalidation */
	uint	tr_attrset;	/* set/create an attribute */
	uint	tr_attrrm;	/* remove an attribute */
	uint	tr_clearagi;	/* clear bad agi unlinked ino bucket */
} xfs_trans_reservations_t;

typedef struct xfs_mount {
	bhv_desc_t		m_bhv;		/* vfs xfs behavior */
	xfs_tid_t		m_tid;		/* next unused tid for fs */
	lock_t			m_ail_lock;	/* fs AIL mutex */
	xfs_ail_entry_t		m_ail;		/* fs active log item list */
	uint			m_ail_gen;	/* fs AIL generation count */
	xfs_sb_t		m_sb;		/* copy of fs superblock */
	lock_t			m_sb_lock;	/* sb counter mutex */
	struct buf		*m_sb_bp;	/* buffer for superblock */
	char			*m_fsname; 	/* filesystem name */
	int			m_fsname_len;	/* strlen of fs name */
	dev_t			m_dev;		/* dev of fs meta-data */
	dev_t			m_logdev;	/* dev of fs log data */
	dev_t			m_rtdev;	/* dev of fs realtime data */
	int			m_bsize;	/* fs logical block size */
	xfs_agnumber_t		m_agfrotor;	/* last ag where space found */
	xfs_agnumber_t		m_agirotor;	/* last ag dir inode alloced */
	lock_t			m_ipinlock;	/* inode pinning mutex */
	struct xfs_ihash	*m_ihash;	/* fs private inode hash table*/
	uint			m_ihashmask;	/* fs inode hash size - 1 */
	struct xfs_inode	*m_inodes;	/* active inode list */
	mutex_t			m_ilock;	/* inode list mutex */
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
	__uint8_t		m_agino_log;	/* #bits for agino in inum */
	__uint8_t		m_nreadaheads;	/* #readahead buffers */
	__uint16_t		m_inode_cluster_size;/* min inode buf size */
	uint			m_blockmask;	/* sb_blocksize-1 */
	uint			m_blockwsize;	/* sb_blocksize in words */
	uint			m_blockwmask;	/* blockwsize-1 */
	uint			m_alloc_mxr[2];	/* XFS_ALLOC_BLOCK_MAXRECS */
	uint			m_alloc_mnr[2];	/* XFS_ALLOC_BLOCK_MINRECS */
	uint			m_bmap_dmxr[2];	/* XFS_BMAP_BLOCK_DMAXRECS */
	uint			m_bmap_dmnr[2];	/* XFS_BMAP_BLOCK_DMINRECS */
	uint			m_inobt_mxr[2];	/* XFS_INOBT_BLOCK_MAXRECS */
	uint			m_inobt_mnr[2];	/* XFS_INOBT_BLOCK_MINRECS */
	uint			m_ag_maxlevels;	/* XFS_AG_MAXLEVELS */
	uint			m_bm_maxlevels[2]; /* XFS_BM_MAXLEVELS */
	uint			m_in_maxlevels;	/* XFS_IN_MAXLEVELS */
	struct xfs_perag	*m_perag;	/* per-ag accounting info */
	mrlock_t		m_peraglock;	/* lock for m_perag (pointer) */
	sema_t			m_growlock;	/* growfs mutex */
	xfs_extlen_t		m_rbmrotor;	/* rt bitmap allocation rotor */
	int			m_fixedfsid[2];	/* unchanged for life of FS */
	uint			m_dmevmask;	/* DMI events for this FS */
	uint			m_flags;	/* global mount flags */
	uint			m_attroffset;	/* inode attribute offset */
	int			m_da_node_ents;	/* how many entries in danode */
	int			m_ialloc_inos;	/* inodes in inode allocation */
	int			m_ialloc_blks;	/* blocks in inode allocation */
	int			m_litino;	/* size of inode union area */
	xfs_trans_reservations_t m_reservations; /* precomputed res values */
	__uint64_t		m_maxicount;	/* maximum inode count */
#if XFS_BIG_FILESYSTEMS
	xfs_ino_t		m_inoadd;	/* add value for ino64_offset */
#endif
} xfs_mount_t;

/*
 * Flags for m_flags.
 */
#define	XFS_MOUNT_WSYNC	0x00000001
#if XFS_BIG_FILESYSTEMS
#define	XFS_MOUNT_INO64	0x00000002
#endif

/*
 * Default minimum read and write sizes.
 */
#define	XFS_READIO_LOG_SMALL	15	/* <= 32MB memory */
#define	XFS_WRITEIO_LOG_SMALL	15
#define	XFS_READIO_LOG_LARGE	16	/* > 32MB memory */
#define	XFS_WRITEIO_LOG_LARGE	16

/*
 * Synchronous read and write sizes.  This should be
 * better for NFS.
 */
#define	XFS_WSYNC_READIO_LOG	15
#define	XFS_WSYNC_WRITEIO_LOG	13

/*
 * Macros for getting from mount to vfs and back.
 */
#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_MTOVFS)
struct vfs *xfs_mtovfs(xfs_mount_t *mp);
#define	XFS_MTOVFS(mp)		xfs_mtovfs(mp)
#else
#define	XFS_MTOVFS(mp)		(bhvtovfs(&(mp)->m_bhv))
#endif
#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_BHVTOM)
xfs_mount_t *xfs_bhvtom(bhv_desc_t *bdp);
#define	XFS_BHVTOM(bdp)	xfs_bhvtom(bdp)
#else
#define	XFS_BHVTOM(vfsp)	((xfs_mount_t *)BHV_PDATA(bdp))
#endif
 
/*
 * This structure is for use by the xfs_mod_incore_sb_batch() routine.
 */
typedef struct xfs_mod_sb {
	xfs_sb_field_t	msb_field;	/* Field to modify, see below */
	int		msb_delta;	/* change to make to the specified field */
} xfs_mod_sb_t;

#define	XFS_MOUNT_ILOCK(mp)	mutex_lock(&((mp)->m_ilock), PINOD)
#define	XFS_MOUNT_IUNLOCK(mp)	mutex_unlock(&((mp)->m_ilock))
#define	XFS_SB_LOCK(mp)		mutex_spinlock(&(mp)->m_sb_lock)
#define	XFS_SB_UNLOCK(mp,s)	mutex_spinunlock(&(mp)->m_sb_lock,(s))

#define	AIL_LOCK(mp)		mutex_spinlock(&(mp)->m_ail_lock)
#define	AIL_UNLOCK(mp,s)	mutex_spinunlock(&(mp)->m_ail_lock, s)

#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_HAS_ATTRIBUTES)
int xfs_has_attributes(xfs_mount_t *mp);
#define	XFS_HAS_ATTRIBUTES(mp)	xfs_has_attributes(mp)
#else
#define	XFS_HAS_ATTRIBUTES(mp)	\
	((mp)->m_sb.sb_versionnum >= XFS_SB_VERSION_HASATTR)
#endif

#ifdef SIM
xfs_mount_t	*xfs_mount(dev_t, dev_t, dev_t);
xfs_mount_t	*xfs_mount_partial(dev_t, dev_t, dev_t);
void		xfs_umount(xfs_mount_t *);
#endif

void		xfs_mod_sb(xfs_trans_t *, __int64_t);
xfs_mount_t	*xfs_mount_init(void);
void		xfs_mount_free(xfs_mount_t *mp);
int		xfs_mountfs(struct vfs *, xfs_mount_t *mp, dev_t);
int		xfs_unmountfs(xfs_mount_t *, int, struct cred *);
int		xfs_mod_incore_sb(xfs_mount_t *, xfs_sb_field_t, int);
int		xfs_mod_incore_sb_batch(xfs_mount_t *, xfs_mod_sb_t *, uint);
struct buf	*xfs_getsb(xfs_mount_t *, int);
extern	struct vfsops xfs_vfsops;
#endif	/* !_FS_XFS_MOUNT_H */
