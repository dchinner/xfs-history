
/*
 * Copyright (C) 1999 Silicon Graphics, Inc.  All Rights Reserved.
 * 
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as published
 * by the Free Software Fondation.
 * 
 * This program is distributed in the hope that it would be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  Further, any license provided herein,
 * whether implied or otherwise, is limited to this program in accordance with
 * the express provisions of the GNU General Public License.  Patent licenses,
 * if any, provided herein do not apply to combinations of this program with
 * other product or programs, or any other product whatsoever.  This program is
 * distributed without any warranty that the program is delivered free of the
 * rightful claim of any third person by way of infringement or the like.  See
 * the GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write the Free Software Foundation, Inc., 59 Temple
 * Place - Suite 330, Boston MA 02111-1307, USA.
 */
#ifndef	_XFS_LOG_H
#define _XFS_LOG_H

#ident	"$Revision: 1.39 $"

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
 * Flags to xfs_log_done()
 */
#define XFS_LOG_REL_PERM_RESERV	0x1


/*
 * Flags to xfs_log_reserve()
 *
 *	XFS_LOG_SLEEP:	 If space is not available, sleep (default)
 *	XFS_LOG_NOSLEEP: If space is not available, return error
 *	XFS_LOG_PERM_RESERV: Permanent reservation.  When writes are
 *		performed against this type of reservation, the reservation
 *		is not decreased.  Long running transactions should use this.
 */
#define XFS_LOG_SLEEP		0x0
#define XFS_LOG_NOSLEEP		0x1
#define XFS_LOG_PERM_RESERV	0x2
#define XFS_LOG_RESV_ALL	(XFS_LOG_NOSLEEP|XFS_LOG_PERM_RESERV)


/*
 * Flags to xfs_log_force()
 *
 *	XFS_LOG_SYNC:	Synchronous force in-core log to disk
 *	XFS_LOG_FORCE:	Start in-core log write now.
 *	XFS_LOG_URGE:	Start write within some window of time.
 *
 * Note: Either XFS_LOG_FORCE or XFS_LOG_URGE must be set.
 */
#define XFS_LOG_SYNC		0x1
#define XFS_LOG_FORCE		0x2
#define XFS_LOG_URGE		0x4


/*
 * Flags to xfs_log_print()
 *
 */
#define XFS_LOG_PRINT_EXIT	0x1
#define XFS_LOG_PRINT_NO_DATA	0x2
#define XFS_LOG_PRINT_NO_PRINT	0x4


/* Log Clients */
#define XFS_TRANSACTION		0x69
#define XFS_VOLUME		0x2
#define XFS_LOG			0xaa

typedef struct xfs_log_iovec {
	caddr_t		i_addr;		/* beginning address of region */
	int		i_len;		/* length in bytes of region */
} xfs_log_iovec_t;

typedef void* xfs_log_ticket_t;

/*
 * Structure used to pass callback function and the function's argument
 * to the log manager.
 */
typedef struct xfs_log_callback {
	struct xfs_log_callback	*cb_next;
	void			(*cb_func)(void *, int);
	void 			*cb_arg;
} xfs_log_callback_t;


/* Log manager interfaces */
struct xfs_mount;
xfs_lsn_t xfs_log_done(struct xfs_mount *mp,
		       xfs_log_ticket_t ticket,
		       uint		flags);
int	  xfs_log_force(struct xfs_mount *mp,
			xfs_lsn_t	 lsn,
			uint		 flags);
int	  xfs_log_init(void);
int	  xfs_log_mount(struct xfs_mount *mp,
			dev_t		 log_dev,
			daddr_t		 start_block,
			int		 num_bblocks);
int	  xfs_log_mount_finish(struct xfs_mount *mp, int);
void	  xfs_log_move_tail(struct xfs_mount	*mp,
			    xfs_lsn_t		tail_lsn);
void	  xfs_log_notify(struct xfs_mount	*mp,
			 xfs_lsn_t		lsn,
			 xfs_log_callback_t	*callback_entry);
int	  xfs_log_reserve(struct xfs_mount *mp,
			  int		   length,
			  int		   count,
			  xfs_log_ticket_t *ticket,
			  char		   clientid,
			  uint		   flags);
int	  xfs_log_write(struct xfs_mount *mp,
			xfs_log_iovec_t  region[],
			int		 nentries,
			xfs_log_ticket_t ticket,
			xfs_lsn_t	 *start_lsn);
int	  xfs_log_unmount(struct xfs_mount *mp);
int	  xfs_log_unmount_write(struct xfs_mount *mp);
void      xfs_log_unmount_dealloc(struct xfs_mount *mp);
int	  xfs_log_force_umount(struct xfs_mount *mp, int logerror);

/* Log manager utility interfaces */
void xfs_log_print(struct xfs_mount *mp,
		   dev_t	    log_dev,
		   daddr_t	    start_block,
		   int		    num_bblocks,
		   int		    start_print_block,
		   uint		    flags);


extern int xlog_debug;		/* set to 1 to enable real log */

extern int xfs_log_need_covered(struct xfs_mount *mp);

#endif	/* _XFS_LOG_H */
