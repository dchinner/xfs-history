#ifndef _FS_XFS_MOUNT_H
#define	_FS_XFS_MOUNT_H

#ident	"$Revision: 1.6 $"

struct xfs_ihash;

typedef struct xfs_mount {
	struct vfs		*m_vfsp;	/* ptr to vfs */
	xfs_tid_t		m_tid;		/* next unused tid for fs */
	xfs_log_item_t		*m_ail;		/* fs active log item list */
	uint			m_ail_gen;	/* fs AIL generation count */
	lock_t			m_ail_lock;	/* fs AIL mutex */
	xfs_lsn_t		m_ail_lsn;	/* lsn of 1st elmt in AIL */
	uint			m_log_thresh;	/* log head/tail separation */
	xfs_trans_t		*m_async_trans;	/* list of async transactions */
	lock_t			m_async_lock;	/* async trans list mutex */
	xfs_sb_t		m_sb;		/* ptr to fs superblock */
	dev_t			m_dev;		/* dev of fs meta-data */
	int			m_bsize;	/* fs logical block size */
	xfs_agnumber_t		m_agrotor;	/* last ag where space found */
	struct xfs_ihash	*m_ihash;	/* fs private inode hash table*/
	ulong			m_ihashmask;	/* fs inode hash size - 1 */
	struct xfs_inode	*m_inodes;	/* active inode list */
	lock_t			m_ilock;	/* inode list mutex */
	void			*m_log;		/* log specific stuff */
} xfs_mount_t;

#define	XFS_MOUNT_ILOCK(mp)	splockspl((mp)->m_ilock, splhi)
#define	XFS_MOUNT_IUNLOCK(mp,s)	spunlockspl((mp)->m_ilock,(s))

void xfs_mod_sb(xfs_trans_t *, int);
void xfs_mount(xfs_mount_t *, dev_t);
void xfs_umount(xfs_mount_t *);

#endif	/* !_FS_XFS_MOUNT_H */
