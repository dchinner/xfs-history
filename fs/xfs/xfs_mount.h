#ifndef _FS_XFS_MOUNT_H
#define	_FS_XFS_MOUNT_H

#ident	"$Revision: 1.2 $"

/*
 * xfs_mount struct is in xfs.h for now, move it here later.
 */

void xfs_mod_sb(xfs_trans_t *, int, int);
void xfs_mount(xfs_mount_t *, dev_t);
void xfs_umount(xfs_mount_t *);

#endif	/* !_FS_XFS_MOUNT_H */
