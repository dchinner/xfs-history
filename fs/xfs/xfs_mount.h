#ifndef _FS_XFS_MOUNT_H
#define	_FS_XFS_MOUNT_H

#ident	"$Revision: 1.12 $"

struct xfs_ihash;

typedef struct xfs_mount {
	struct vfs		*m_vfsp;	/* ptr to vfs */
	xfs_tid_t		m_tid;		/* next unused tid for fs */
	lock_t			m_ail_lock;	/* fs AIL mutex */
	xfs_ail_entry_t		m_ail;		/* fs active log item list */
	uint			m_ail_gen;	/* fs AIL generation count */
	xfs_lsn_t		m_ail_lsn;	/* lsn of 1st elmt in AIL */
	uint			m_log_thresh;	/* log head/tail separation */
	xfs_trans_t		*m_async_trans;	/* list of async transactions */
	lock_t			m_async_lock;	/* async trans list mutex */
	xfs_sb_t		m_sb;		/* ptr to fs superblock */
	lock_t			m_sb_lock;	/* sb counter mutex */
	buf_t			*m_sb_bp;	/* buffer for superblock */
	dev_t			m_dev;		/* dev of fs meta-data */
	int			m_bsize;	/* fs logical block size */
	xfs_agnumber_t		m_agrotor;	/* last ag where space found */
	lock_t			m_ipinlock;	/* inode pinning mutex */
	struct xfs_ihash	*m_ihash;	/* fs private inode hash table*/
	ulong			m_ihashmask;	/* fs inode hash size - 1 */
	struct xfs_inode	*m_inodes;	/* active inode list */
	lock_t			m_ilock;	/* inode list mutex */
	void			*m_log;		/* log specific stuff */
} xfs_mount_t;
 
/*
 * This structure is for use by the xfs_mod_incore_sb_batch() routine.
 *
 */
typedef struct xfs_mod_sb {
	uint	msb_field;	/* Field to modify, see below */
	int	msb_delta;	/* change to make to the specified field */
} xfs_mod_sb_t;

#define	XFS_MOUNT_ILOCK(mp)	splockspl((mp)->m_ilock, splhi)
#define	XFS_MOUNT_IUNLOCK(mp,s)	spunlockspl((mp)->m_ilock,(s))
#define	XFS_SB_LOCK(mp)		splockspl((mp)->m_sb_lock, splhi)
#define	XFS_SB_UNLOCK(mp,s)	spunlockspl((mp)->m_sb_lock,(s))

#define	AIL_LOCK(mp)		(splockspl((mp)->m_ail_lock, splhi))
#define	AIL_TRYLOCK(mp)		(_trylock((mp)->m_ail_lock, splhi))
#define	AIL_UNLOCK(mp,s)	(spunlockspl((mp)->m_ail_lock, s))

void		xfs_mod_sb(xfs_trans_t *, int);
xfs_mount_t	*xfs_mount(dev_t);
void		xfs_umount(xfs_mount_t *);
int		xfs_mod_incore_sb(xfs_mount_t *, uint, int);
int		xfs_mod_incore_sb_batch(xfs_mount_t *, xfs_mod_sb_t *, uint);
buf_t		*xfs_getsb(xfs_mount_t *);

#endif	/* !_FS_XFS_MOUNT_H */
