
#ifndef	_XFS_LOG_H
#define _XFS_LOG_H

caddr_t		xfs_log_alloc(size_t, uint, xfs_lsn_t*);
void		xfs_log_free(caddr_t, size_t);
void		xfs_log_notify(void(*)(void*), void*, xfs_lsn_t);
int		xfs_log_reserve(struct xfs_mount *, int, int);



#endif	/* _XFS_LOG_H */
