#ifndef _FS_XFS_MOUNT_H
#define	_FS_XFS_MOUNT_H

#ident	"$Revision$"

#include "xfs.h"
#include "xfs_types.h"

typedef struct xfs_mount {
	xfs_tid_t		m_tid;
	struct xfs_log_item	*m_ail;
	uint			m_ail_gen;
	lock_t			m_ail_lock;
	xfs_lsn_t		m_ail_lsn;
	uint			m_log_thresh;
	struct xfs_trans	*m_async_trans;
	lock_t			m_async_lock;
	struct xfs_sb		*m_sb;
	dev_t			m_dev;
	int			m_bsize;
	xfs_agnumber_t		m_agrotor;
} xfs_mount_t;

void xfs_mod_sb(xfs_trans_t *, int, int);
void xfs_mount(xfs_mount_t *, dev_t);
void xfs_umount(xfs_mount_t *);

#endif	/* !_FS_XFS_MOUNT_H */
