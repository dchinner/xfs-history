
#ifndef	_XFS_INODE_H
#define	_XFS_INODE_H

#include "xfs_dinode.h"

struct xfs_inode;

typedef struct xfs_inode_log_item {
	xfs_log_item_t		ili_item;
	struct xfs_inode	*ili_inode;
	unsigned int		ili_recur;
	unsigned int		ili_flags;
	unsigned int		ili_field_mask;
} xfs_inode_log_item_t;

#define	XFS_ILI_HOLD	0x1
	

typedef struct xfs_inode {
	xfs_inode_log_item_t	i_item;
	xfs_trans_t		*i_transp;
	unsigned int		i_flags;
	xfs_dinode_t		i_d;
} xfs_inode_t;

#define	I_LOCKED	0x1


xfs_inode_t	*xfs_inode_incore(struct xfs_mount *, xfs_ino_t, void *);
xfs_inode_t	*xfs_iget(struct xfs_mount *, xfs_ino_t);


#endif	/* _XFS_INODE_H */
