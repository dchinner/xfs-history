

#ifndef	_XFS_RW_H
#define	_XFS_RW_H


/*
 * Prototypes for functions in xfs_rw.c.
 */
int
xfs_read(vnode_t	*vp,
	 uio_t		*uiop,
	 int		ioflag,
	 cred_t		*credp);

int
xfs_write(vnode_t	*vp,
	  uio_t		*uiop,
	  int		ioflag,
	  cred_t	*credp);

void
xfs_strategy(vnode_t	*vp,
	     buf_t	*bp);

int
xfs_bmap(vnode_t	*vp,
	 off_t		offset,
	 ssize_t	count,
	 int		flags,
	 cred_t		*credp,
	 struct bmapval	*bmapp,
	 int		*nbmaps);

void
xfs_zero_eof(xfs_inode_t	*ip,
	     off_t		offset,
	     __int64_t		isize,
	     cred_t		*credp);



#endif /* _XFS_RW_H */
