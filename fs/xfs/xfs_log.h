
#ifndef	_XFS_LOG_H
#define _XFS_LOG_H

#define	XFS_LSN_CMP(x,y)	((x) - (y))
#define	XFS_LSN_DIFF(x,y)	((x) - (y))

/*
 * Macros, structures, prototypes for interface to the log manager.
 */

/*
 * Flags to xfs_log_mount
 */
#define XFS_LOG_RECOVER		0x1

/*
 * Flags to xfs_log_reserve()
 *
 *	XFS_LOG_SLEEP:	 If space is not available, sleep
 *	XFS_LOG_NOSLEEP: If space is not available, return error
 *	XFS_LOG_PERM_RESERV: Permanent reservation.  When writes are
 *		performed against this type of reservation, the reservation
 *		is not decreased.  Long running transactions should use this.
 */
#define XFS_LOG_SLEEP		0x1
#define XFS_LOG_NOSLEEP		0x2
#define XFS_LOG_PERM_RESERV	0x4


/*
 * Flags to xfs_log_force()
 *
 *	XFS_LOG_SYNC:	Synchronous force in-core log to disk.
 *	default:	Force out asynchronously
 *	XFS_LOG_FORCE:	Start in-core log write now.
 *	XFS_LOG_URGE:	Start write within some window of time.
 */
#define XFS_LOG_SYNC		0x1
#define XFS_LOG_FORCE		0x2
#define XFS_LOG_URGE		0x4


/* Log Clients */
#define XFS_TRANSACTION_MANAGER	1
#define XFS_VOLUME_MANAGER	2


typedef struct xfs_log_iovec {
	caddr_t		i_addr;		/* beginning address of region */
	uint		i_len;		/* length in bytes of region */
	xfs_lsn_t	i_lsn;		/* log sequence number of region */
} xfs_log_iovec_t;

typedef void* xfs_log_ticket_t;

/*
 * Structure used to pass callback function and the function's argument
 * to the log manager.
 */
typedef struct xfs_log_callback {
	struct xfs_log_callback	*cb_next;
	void			(*cb_func)(void *);
	void 			*cb_arg;
} xfs_log_callback_t;


/* Log manager interfaces */
struct xfs_mount;
void xfs_log_done(struct xfs_mount *mp, xfs_log_ticket_t ticket, uint flags);
int  xfs_log_force(struct xfs_mount *mp, xfs_lsn_t lsn, uint flags);
int  xfs_log_init();
int  xfs_log_mount(struct xfs_mount *mp, dev_t log_dev, uint flags);
void xfs_log_notify(struct xfs_mount *mp, xfs_lsn_t lsn,
		    xfs_log_callback_t *callback_entry);
int  xfs_log_reserve(struct xfs_mount *mp, uint length,
		     xfs_log_ticket_t *ticket, char clientid, uint flags);
int  xfs_log_write(struct xfs_mount *mp, xfs_log_iovec_t region[], int nentries,
		   xfs_log_ticket_t ticket);

/* Log manager utility interfaces */
void xfs_log_print(struct xfs_mount *mp, dev_t log_dev);

#define XFS_ERECOVER	1	/* Failure to recover log */
#define XFS_ELOGSTAT	2	/* Failure to stat log in user space */
#define XFS_ENOLOGSPACE	3	/* Reservation too large */
#define XFS_ENOTSUP	4

#endif	/* _XFS_LOG_H */
