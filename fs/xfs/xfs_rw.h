

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




#endif /* _XFS_RW_H */
