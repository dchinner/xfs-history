
#ifndef	_XFS_LOG_H
#define _XFS_LOG_H

caddr_t		xfs_log_alloc(size_t, uint, xfs_lsn_t*);
void		xfs_log_free(caddr_t, size_t);
void		xfs_log_notify(void(*)(void*), void*, xfs_lsn_t);
int		xfs_log_reserve(struct xfs_mount *, int, int);

#ifndef SIM
xfs_lsn_t	xfs_log_lsn(struct xfs_mount *);
void		xfs_log_sync(xfs_lsn_t);
void		xfs_log_unreserve(struct xfs_mount *, uint);
void		xfs_log_wait(xfs_lsn_t);
#endif

#endif	/* _XFS_LOG_H */
