#ifndef XFS_LINUX_OPS_SUPER_DOT_H
#define XFS_LINUX_OPS_SUPER_DOT_H

void
linvfs_inode_attr_in(
	struct inode	*inode);
int
fs_dounmount(
        bhv_desc_t      *bdp,
        int             flags,
        vnode_t         *rootvp,
        cred_t          *cr);

#endif  /*  XFS_LINUX_OPS_SUPER_DOT_H  */


