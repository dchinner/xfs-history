#ifndef	_XFS_ERROR_H
#define	_XFS_ERROR_H

#ident "$Revision: 1.6 $"

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


/*
 * error injection tags - the labels can be anything you want
 * but each tag should have its own unique number
 */

#define XFS_ERRTAG_NOERROR				0
#define XFS_ERRTAG_IFLUSH_1				1
#define XFS_ERRTAG_IFLUSH_2				2
#define XFS_ERRTAG_IFLUSH_3				3
#define XFS_ERRTAG_IFLUSH_4				4
#define XFS_ERRTAG_IFLUSH_5				5
#define XFS_ERRTAG_IFLUSH_6				6
#define XFS_ERRTAG_MAX					7

#if !defined(SIM) && (defined(DEBUG) || defined(INDUCE_IO_ERROR))
extern int	xfs_error_test(int, int *, char *, int, char *);

#define	XFS_NUM_INJECT_ERROR				10

#ifdef __ANSI_CPP__
#define XFS_TEST_ERROR(expr, mp, tag)			\
	((expr) || \
	 xfs_error_test((tag), (mp)->m_fixedfsid, #expr, __LINE__, __FILE__))
#else
#define XFS_TEST_ERROR(expr, mp, tag)			\
	((expr) || \
	 xfs_error_test((tag), (mp)->m_fixedfsid, "expr", __LINE__, __FILE__))
#endif /* __ANSI_CPP__ */

int		xfs_errortag_add(int error_tag, int fd);
int		xfs_errortag_clear(int error_tag, int fd);

int		xfs_errortag_clearall(int fd);
int		xfs_errortag_clearall_umount(int64_t fsid, char *fsname,
						int loud);
#else
#define XFS_TEST_ERROR(expr, mp, tag)			expr
#endif /* !SIM && (DEBUG || INDUCE_IO_ERROR) */

/*
 * XFS panic tags -- allow a call to xfs_cmn_err() be turned into
 *			a panic by setting xfs_panic_mask in the
 *			stune file.
 */
#define		XFS_NO_PTAG			0LL
#define		XFS_PTAG_IFLUSH			0x0000000000000001LL
#define 	XFS_PTAG_LOGRES			0x0000000000000002LL

struct xfs_mount;
extern uint64_t	xfs_panic_mask;
/* PRINTFLIKE4 */
void		xfs_cmn_err(uint64_t panic_tag, int level, struct xfs_mount *mp,
			    char *fmt, ...);
/* PRINTFLIKE3 */
void		xfs_fs_cmn_err(int level, struct xfs_mount *mp, char *fmt, ...);
