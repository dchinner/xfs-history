#ifndef	_XFS_ERROR_H
#define	_XFS_ERROR_H

#ident "$Revision: 1.4 $"

#define XFS_ERECOVER	1	/* Failure to recover log */
#define XFS_ELOGSTAT	2	/* Failure to stat log in user space */
#define XFS_ENOLOGSPACE	3	/* Reservation too large */
#define XFS_ENOTSUP	4	/* Operation not supported */
#define	XFS_ENOLSN	5	/* Can't find the lsn you asked for */
#define XFS_ENOTFOUND	6
#define XFS_ENOTXFS	7	/* Not XFS filesystem */

#ifdef DEBUG
#define	XFS_ERROR_NTRAP	10
extern int	xfs_etrap[XFS_ERROR_NTRAP];
extern int	xfs_error_trap(int);
#define	XFS_ERROR(e)	xfs_error_trap(e)
#else
#define	XFS_ERROR(e)	(e)
#endif

#endif	/* _XFS_ERROR_H */
