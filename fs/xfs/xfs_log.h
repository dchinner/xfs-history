
#ifndef	_XFS_LOG_H
#define _XFS_LOG_H

#ifndef LOG_DEBUG
caddr_t         xfs_log_alloc(size_t, uint, xfs_lsn_t*);
void            xfs_log_free(caddr_t, size_t);
void            xfs_log_notify(void(*)(void*), void*, xfs_lsn_t);
int             xfs_log_reserve(struct xfs_mount *, int, int);

#ifndef SIM
xfs_lsn_t       xfs_log_lsn(struct xfs_mount *);
void            xfs_log_sync(xfs_lsn_t);
void            xfs_log_unreserve(struct xfs_mount *, uint);
void            xfs_log_wait(xfs_lsn_t);
#endif
#else
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
    caddr_t	i_addr;		/* beginning address of region */
    uint	i_len;		/* length in bytes of region */
    xfs_lsn_t	i_lsn;		/* log sequence number of region */
} xfs_log_iovec_t;

typedef void* xfs_log_ticket_t;

/* Log manager interfaces */
#ifdef TICKET_INT
void xfs_log_done(xfs_mount_t *mp, int ticket);
int  xfs_log_force(xfs_mount_t *mp, int ticket, uint flags);
int  xfs_log_init();
int  xfs_log_mount(xfs_mount_t *mp, dev_t log_dev, uint flags);
int  xfs_log_new_transaction(xfs_mount_t *mp, int ticket, xfs_tid_t old_tid,
			     xfs_tid_t new_tid);
void xfs_log_notify(xfs_mount_t *mp, xfs_lsn_t lsn,
		    void (*callback_func)(void*), void* callback_arg);
int  xfs_log_reserve(xfs_mount_t *mp, xfs_tid_t tid, uint len, int *ticket,
		     char clientid, uint flags);
int  xfs_log_write(xfs_mount_t *mp, xfs_log_iovec_t reg[], int num, int ticket);
#else
void xfs_log_done(xfs_mount_t *mp, xfs_log_ticket_t ticket);
int  xfs_log_force(xfs_mount_t *mp, xfs_log_ticket_t ticket, uint flags);
int  xfs_log_init();
int  xfs_log_mount(xfs_mount_t *mp, dev_t log_dev, uint flags);
int  xfs_log_new_transaction(xfs_mount_t *mp, xfs_log_ticket_t ticket,
			     xfs_tid_t old_tid, xfs_tid_t new_tid);
void xfs_log_notify(xfs_mount_t *mp, xfs_lsn_t lsn,
		    void (*callback_func)(void*), void* callback_arg);
int  xfs_log_reserve(xfs_mount_t *mp, xfs_tid_t tid, uint len,
		     xfs_log_ticket_t *ticket, char clientid, uint flags);
int  xfs_log_write(xfs_mount_t *mp, xfs_log_iovec_t reg[], int num,
		   xfs_log_ticket_t ticket);
#endif /* TICKET_INT */

/* Log manager utility interfaces */
void xfs_log_print(xfs_mount_t *mp, dev_t log_dev);

#endif /* LOG_DEBUG */
#endif	/* _XFS_LOG_H */
