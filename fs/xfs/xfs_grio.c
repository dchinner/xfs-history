#ident "$Header: /home/cattelan/xfs_cvs/xfs-for-git/fs/xfs/Attic/xfs_grio.c,v 1.40 1994/12/28 23:05:43 rcc Exp $"

#include <sys/types.h>
#include <sys/param.h>
#include <sys/errno.h>
#ifndef SIM
#include <sys/systm.h>
#endif
#include <sys/debug.h>
#include <sys/buf.h>
#include <sys/sema.h>
#include <sys/lock.h>
#ifdef SIM
#define _KERNEL
#endif
#include <sys/vnode.h>
#include <sys/grio.h>
#ifdef SIM
#undef _KERNEL
#endif
#include <sys/vfs.h>
#include <sys/kmem.h>
#include <sys/cred.h>
#include <sys/proc.h>
#include <sys/user.h>
#include <sys/ktime.h>
#include <sys/fs/xfs_types.h>
#include <sys/fs/xfs_log.h>
#include <sys/fs/xfs_inum.h>
#include <sys/fs/xfs_trans.h>
#include <sys/fs/xfs_sb.h>
#include <sys/fs/xfs_mount.h>
#include <sys/fs/xfs_alloc_btree.h>
#include <sys/fs/xfs_bmap_btree.h>
#include <sys/fs/xfs_ialloc_btree.h>
#include <sys/fs/xfs_btree.h>
#include <sys/fs/xfs_dinode.h>
#include <sys/fs/xfs_inode_item.h>
#include <sys/fs/xfs_inode.h>
#include <sys/fs/xfs_itable.h>

#ifdef SIM
#include "sim.h"
#include "stdio.h"
#endif

/*
 * These routines determine if a griven file read or write request 
 * has been guaranteed rate I/O and marks the buffer accordingly.
 *
 *
 *
 */

#define TIME_IS_LESS(tv, nexttv)	\
      	(( tv.tv_sec < nexttv.tv_sec) ||	\
	       ((tv.tv_sec == nexttv.tv_sec) && (tv.tv_nsec < nexttv.tv_nsec)))

#if defined(DEBUG) && !defined(SIM)
extern int	grio_debug;
#define GRIO_DBPRNT( level, str)        \
        if (grio_debug > level) {       \
                printf(str);            \
        }
#else
#define GRIO_DBPRNT(s1, s2 )
#endif

#define IRELE(ip)	VN_RELE(XFS_ITOV(ip))

int xfs_grio_add_ticket( file_id_t *, int, char *);
int xfs_grio_remove_ticket( file_id_t *, char *);
int xfs_add_ticket_to_inode( xfs_inode_t *, int, struct reservation_id *);
void xfs_remove_ticket_from_inode( xfs_inode_t *, struct reservation_id *);
int ticket_lock( xfs_inode_t *);
void ticket_unlock( xfs_inode_t *, int);
extern int xfs_read_file(vnode_t *, uio_t *, int, cred_t *);
extern int xfs_write_file(vnode_t *, uio_t *, int, cred_t *);
extern int xfs_diordwr(vnode_t *,uio_t *, int, cred_t *,int);
extern int strncmp(char *, char *, int);
extern struct vfs *vfs_devsearch( dev_t );
STATIC int xfs_grio_issue_io( vnode_t *, uio_t *,int, cred_t *,int);
STATIC int xfs_crack_file_id(file_id_t *, dev_t *, xfs_ino_t *);
xfs_inode_t *xfs_get_inode ( dev_t, xfs_ino_t);
grio_ticket_t *xfs_io_is_guaranteed( xfs_inode_t *, struct reservation_id *, int *);

#ifndef SIM
extern void fasthz_delay( struct timeval *);
extern void timestruc_sub( timestruc_t *, timestruc_t *);
extern void timestruc_fix( timestruc_t *);
#endif


/*
 * xfs_get_inode_from_inum()
 *
 *	This routine takes the dev_t of a file system and an
 *	inode number on that file system, and returns a pointer
 *	to the corresponding incore xfs inode structure.
 *
 * RETURNS:
 *	xfs_inode_t pointer on success
 *	NULL on failure
 *
 */
xfs_inode_t *
xfs_get_inode(  dev_t fs_dev, xfs_ino_t ino)
{
	int			ret;
        struct vfs		*vfsp;
        xfs_inode_t		*ip = NULL ;
	extern struct vfs	*vfs_devsearch( dev_t );



	/*
	 * Lookup the vfs structure and mark it busy.
	 * This prevents race conditions with unmount.
	 *
	 * If this returns NULL, the file system may be in the process
	 * of being unmounted. The unmount may succeed or fail.  If the 
	 * umount fails, the grio ticket will remain attached to the
	 * inode structure. It will be cleanup when the inode structure is
	 * freed.
	 */
        vfsp = vfs_busydev( fs_dev );

        if (vfsp) {
		
		/*
		 * Verify that this is an xfs file system.
		 */
#ifndef SIM
		if (strncmp(vfssw[vfsp->vfs_fstype].vsw_name, "xfs", 3) == 0) 
#endif
		{
                	ip = xfs_iget( XFS_VFSTOM( vfsp ), 
					NULL, ino, XFS_ILOCK_EXCL);


			if ( (ip == NULL) || (ip->i_d.di_mode == 0) ) {
				if (ip) {
					xfs_iunlock( ip, XFS_ILOCK_EXCL );
				}
				ip = NULL;
			}
#ifdef DEBUG
			if (!ip) 
				printf("xfs_get failed on %d \n",ino);
#endif

		}

		/*
		 * Decrement the vfs busy count.
		 */
		vfs_unbusy( vfsp );
        } 
#ifdef GRIO_DEBUG
	else {
		printf("grio vfs_busydev failed \n");
	}
#endif
        return( ip );
}


/*
 * xfs_add_ticket_to_inode()
 *
 *	Add another ticket to the inode ticket list. The inode must
 *	be locked to perform this.
 *
 *  RETURNS:
 *	EAGAIN if a ticket with the given id already exists
 *	0 on success
 */
int
xfs_add_ticket_to_inode( xfs_inode_t *ip, int sz, struct reservation_id *id )
{
	int		ret = 0, s;
	grio_ticket_t	*ticket;

	/*
 	 * Check if ticket with this id is already on the list.
 	 */
	if (xfs_io_is_guaranteed( ip, id, &s ) ) {
		ret = EAGAIN;
	} else {
		if (ticket = kmem_zalloc( sizeof( grio_ticket_t), KM_SLEEP)) {
			ticket->sz     = sz;
			ticket->type   = NON_ROTATE_TYPE;
			ticket->id.ino = id->ino;
			ticket->id.pid = id->pid;
			if (id->vod_rotate_slot != NULL_VOD_SLOT) {
				ticket->rotator_slot = id->vod_rotate_slot;
				ticket->rotator_group_size = id->vod_group_size;
				ticket->type = ROTATE_TYPE;
			}

			if (ip->i_ticket) 
				ticket->nextticket = ip->i_ticket;
			/*
			 * This write must be atomic, otherwise
			 * the exclusive ticket lock must be held.
			 */
			ip->i_ticket = ticket;
		}
	}
	ticket_unlock(ip, s);
	return(ret);
}

/*
 * xfs_grio_add_ticket()
 *
 *	Add the rate guarantee reservation of numios I/Os per
 *	second to the given xfs inode.
 *
 *  RETURNS:
 *	0 on success
 *	-1 on failure
 */
int
xfs_grio_add_ticket( file_id_t *fileidp, int sz, char *idptr)
{
	int			ret = -1;
	dev_t			fs_dev;
	xfs_ino_t		ino;
	xfs_inode_t		*ip;
	struct reservation_id	id;


	xfs_crack_file_id( fileidp, &fs_dev, &ino);

#if defined(DEBUG) && !defined(SIM)
	if (grio_debug)
		printf("add ticket: ino = %llx, dev = %x \n",
					(__int64_t)ino, fs_dev);
#endif

	if (copyin(idptr, (caddr_t)&id, sizeof(id))) {
#ifdef DEBUG
		printf("COULD NOT GET ID \n");
#endif
		ret = EFAULT;
		return( ret );
	}

        /*
         * Lock the inode IOLOCK_EXCL so that the i_ticket
         * list in the inode can be safely updated.
         */
        if (ip = xfs_get_inode( fs_dev, ino )) {
		ret = xfs_add_ticket_to_inode( ip, sz, &id );
		xfs_iunlock( ip, XFS_ILOCK_EXCL );
		IRELE( ip );
        }
#ifdef DEBUG
        else {
                printf("could not get ip to add ticket to !\n");
        }
#endif


	return(ret);
}

/*
 * xfs_remove_ticket_from_inode()
 *
 *	This routine removes the ticket with the matching id from the
 *      inode ticket list. The inode ticket list is protected by per
 *      inode ticket list lock. This lock is held shared when processes
 *      are scanning the ticket list and updating tickets. It is held
 *      exclusive when a ticket is being removed from the list.
 *
 * RETURNS:
 *      none
 */
void
xfs_remove_ticket_from_inode( xfs_inode_t *ip, struct reservation_id *id)
{
	grio_ticket_t *ticket, *previousticket;
	int s;

	s = ticket_lock( ip );
	if (ticket = ip->i_ticket) {
		if (MATCH_ID((&(ticket->id)), id)) {
			ip->i_ticket = ticket->nextticket;
		} else {
			while (ticket && (!(MATCH_ID((&(ticket->id)), id)))) {
				previousticket = ticket;
				ticket = ticket->nextticket;
			}
			if (ticket)
				previousticket->nextticket = ticket->nextticket;
		}
		if (ticket) {
			kmem_free( ticket, sizeof( grio_ticket_t ) );
		}
	}
	ticket_unlock( ip, s );
}

/*
 * xfs_grio_remove_ticket()
 *
 *	Remove the rate guarantee reservation with the given id
 *	from the given xfs inode.
 *
 *  RETURNS:
 *	0 on success
 *	-1 on failure
 */
int
xfs_grio_remove_ticket( file_id_t *fileidp, char *idptr)
{
	dev_t			fs_dev;
	xfs_ino_t		ino;
	xfs_inode_t 		*ip;
	struct reservation_id	id;

	xfs_crack_file_id( fileidp, &fs_dev, &ino);

#if defined(DEBUG) && !defined(SIM)
	if (grio_debug) {
		printf("remove ticket: ino = %llx, dev = %x \n",
				(__int64_t)ino, fs_dev);
	}
#endif

	if (copyin(idptr, (caddr_t)&id, sizeof(id))) {
#ifdef DEBUG
		printf("COULD NOT GET ID \n");
#endif
		return( EFAULT );
	}

	if (ip = xfs_get_inode( fs_dev, ino)) {
		xfs_remove_ticket_from_inode (ip, &id);
		xfs_iunlock( ip, XFS_ILOCK_EXCL );
		IRELE( ip );
	}
	return(0);
}

/*
 * xfs_free_remaining_tickets()
 *
 * 	This routine is called from xfs_idestroy(). It frees the
 *	memory associated with any tickets still attached to the inode.
 *	There may be tickets still attached to the inode if a REMOVE_TICKET
 *	call failed due to the file system being in the process of being 
 *	unmounted.
 *
 *
 *
 * RETURNS:
 *	none
 */
void
xfs_free_remaining_tickets( xfs_inode_t *ip )
{
	int		s;
	grio_ticket_t	*ticket;

	s = ticket_lock( ip );
	while ( ticket = ip->i_ticket ) {
		ip->i_ticket = ticket->nextticket;
		kmem_free( ticket, sizeof( grio_ticket_t ) );
	}
	ticket_unlock( ip , s );
}

/*
 * xfs_io_is_guaranteed()
 *
 *	Check if a ticket with the given id is already on the list.
 *
 * RETURNS:
 *	0 if there is no guarantee
 *	non-zero if there is a guarantee
 */
grio_ticket_t *
xfs_io_is_guaranteed( xfs_inode_t *ip, struct reservation_id *id, int *s)
{
	grio_ticket_t *ticket;

	*s = ticket_lock( ip );
	ticket = ip->i_ticket;

	while (ticket && (!MATCH_ID((&(ticket->id)), id))) {
		ticket = ticket->nextticket;
	} 
	return(ticket);
}


/*
 * xfs_grio_req()
 *
 *	This routine issues user guaranteed rate I/O requests. If the
 *	size of the request is larger than the amount guaranteed for the
 *	user, this routine breaks up the request into chucks of the size 
 *	of the guaranteed amount. It issues one of the guaranteed amount
 * 	size requests each second.
 *
 *
 * RETURNS:
 *	0 on success
 *	non-0 on failure.
 */
int
xfs_grio_req( xfs_inode_t *ip, 
	struct reservation_id *id, 
	uio_t *uiop, 
	int ioflag,
	cred_t *credp,
	off_t	offset,
	int rw)
{
	int 		sz, ret = 0, remainingio, thisioreq, set_size, s, eof = 0; 
	int		which_disk, current_disk, num_sec, sec, delay_ticks;
	time_t		snap_lbolt;
	vnode_t 	*vp;
	timestruc_t	tv, nexttv;
	grio_ticket_t 	*ticket;
	struct	timeval	tvl;

retry_request:

        /*
         * Determine if this request has a guaranteed rate of I/O.
         */
        if (ticket = xfs_io_is_guaranteed( ip, id, &s)) {
		/*
 		 * Mark this request as being rate guaranteed. 
		 */
		ioflag |= IO_GRIO;

		if ( ROTATE_TICKET(ticket) ) {
			ASSERT(uiop->uio_resid == ticket->sz);

			snap_lbolt 	= lbolt;
			set_size 	= ticket->rotator_group_size;
			sec 		= (snap_lbolt / HZ) % set_size;

			which_disk 	= (offset / ticket->sz) % set_size;
			current_disk 	= (sec + ticket->rotator_slot) % 
						set_size;

			if (which_disk != current_disk) {
				/*
				 * put the process to sleep until the
				 * next second, then check again.
				 */
				delay_ticks = HZ - (snap_lbolt % HZ);
                        	ticket_unlock(ip, s );
		                delay( delay_ticks );
				goto retry_request;
			}

			/*
			 * Issue the request.
			 * This is the correct disk at this time
			 * for a request in this slot.
			 */
        		ticket->lastreq.tv_sec  = 0;
        		ticket->lastreq.tv_nsec = 0;
		}

		/*
	 	 * 1) Determine the time of the last request.
	 	 *    *) if it has been more than 1 second, zero out
	 	 *	the iothissecond field and set the new time
		 *    *) if if has been less than 1 second since last req
		 *	continue.
	 	 * 2) Check the size of the request,
		 *	*) if sz >= (reqsize + iothissecond)
		 *		issue the whole request and update iothissecond.
 		 *	*) if sz < (reqsize + iothissecond)
	 	 *		issue a request of the size 
		 *			reqsize1 = sz - iothissecond.
		 *		and update iothissecond
		 */

		ret = 0;
		remainingio = uiop->uio_resid;
		uiop->uio_resid = 0;
		while ((remainingio) && (!ret) && (!eof)) {

	       		/*
         		 * Check if the request is within the time limits
         		 * of the rate guarantee.
         		 */
        		nanotime(&tv);

        		/*
         	 	 * Determine the time that the next 
			 * request can be issued.
         	 	 */
        		nexttv.tv_sec  = ticket->lastreq.tv_sec + 1;
        		nexttv.tv_nsec = ticket->lastreq.tv_nsec;
			
			/* 
			 * Check if it has been less than 1 second
			 * since the last I/O request.
			 */
			if ( !(TIME_IS_LESS( tv, nexttv )) ) {
				/*
			 	 * It has been more than 1 second	
			 	 * since the last request on this ticket.
			 	 */
				ticket->iothissecond    = 0;

				nexttv.tv_sec += 1;
				/*
				 * Check if it has been more than 2 seconds
				 * since the last I/O request.
				 *
				 * NB: this tries to correct for
				 *     outward time skew, for closely
				 *     timed requests
				 */
				if ( TIME_IS_LESS(tv, nexttv) )  {
					ticket->lastreq.tv_sec  += 1;
				} else {
					ticket->lastreq.tv_sec  = tv.tv_sec;
					ticket->lastreq.tv_nsec = tv.tv_nsec;
				}
			}

			/*
			 * Only allow a single request per process each second.
			 */
                	if (ticket->iothissecond == 0) {

                        	/*
                         	 * Issue the largest I/O possible. 
                         	 */
				thisioreq = ticket->sz - ticket->iothissecond;
				if (thisioreq > remainingio)
					thisioreq = remainingio;

				uiop->uio_resid 	 = thisioreq;
				uiop->uio_iov[0].iov_len = thisioreq; 
				remainingio		-= thisioreq;

				ticket->iothissecond    += thisioreq; 

                        	ticket_unlock(ip, s);
				vp = XFS_ITOV(ip);

				/*
				 * Issue the request.
				 */
				ret = xfs_grio_issue_io( vp, 
					uiop, ioflag, credp, rw);

				/*
			 	 * Check for end of file.
			 	 */
				if ((uiop->uio_resid != 0)) {
					eof = 1;
				}
			} else {
                		/*
                 		 * The user issued the next request too soon, 
				 * or the I/O request was too large. The
                 		 * process will be put to sleep until the next 
				 * time slice.
                 		 * The granularity of this delay is 1/fasthz 
				 * (1 millisecond).
                 		 */
                        	ticket_unlock(ip, s );
                		GRIO_DBPRNT(2,"request issued too soon \n");

#ifdef SIM
				delay(1);
#else
		                timestruc_sub( &nexttv, &tv );
               			timestruc_fix( &nexttv );

                		TIMESTRUC_TO_TIMEVAL(&nexttv, &tvl);

		                fasthz_delay( &tvl );
#endif
			}

			/*
		 	 * Check if rate guarantee was removed while
		 	 * I/O was taking place.
		 	 */
			if (( ticket = xfs_io_is_guaranteed(ip,id,&s)) == NULL){
       				ticket_unlock(ip, s);
		        	uiop->uio_resid += remainingio;
				uiop->uio_iov[0].iov_len = uiop->uio_resid;
				ioflag &= ~IO_GRIO;
				if (uiop->uio_resid) {
					vp = XFS_ITOV(ip);
					ret = xfs_grio_issue_io(vp, uiop, 
						ioflag, credp, rw);
				}
				return(ret);
			}
                }

		/*
		 * release inode ticket lock
		 */
       		ticket_unlock(ip, s);

		/*
		 * Add unperformed I/O back into resid.
		 */
		uiop->uio_resid += remainingio;
		uiop->uio_iov[0].iov_len = uiop->uio_resid;

	} else {

		/*
		 * release inode ticket lock
		 */
       		ticket_unlock(ip, s);

		vp = XFS_ITOV(ip);
		ret = xfs_grio_issue_io(vp, uiop, ioflag, credp, rw);
	}

        return (ret) ;
}




/*
 * xfs_grio_issue_io()
 *	This routine issues the actual I/O request using the file 
 *	type and I/O operation.
 *
 * RETURNS:
 *	0 on success
 *	-1 on failure
 */
STATIC int
xfs_grio_issue_io( vnode_t *vp, 
	uio_t   *uiop, 
	int     ioflag,
	cred_t  *credp,
	int     rw)
{
	int ret;

	/*
  	 * Currently rate guaranteed I/O can only be issued IO_DIRECT.
	 */
	if ((ioflag & IO_GRIO) && (!(ioflag & IO_DIRECT))) {
		ret = -1;
#ifdef DEBUG
		printf("File is rate guaranteed - cannot user buffer cache.\n");
#endif
		return( ret );
	}

	if (rw == UIO_READ) {
		if (ioflag & IO_DIRECT)
			ret = xfs_diordwr( vp, uiop, ioflag, credp, B_READ);
		else 
			ret = xfs_read_file(vp, uiop, ioflag, credp);
	} else {
		if (ioflag & IO_DIRECT)
			ret = xfs_diordwr( vp, uiop, ioflag, credp, B_WRITE);
		else
			ret = xfs_write_file(vp, uiop, ioflag, credp);
	}
	return( ret );
}


/*
 * ticket_lock()
 *      Locks the per inode ticket lock in exclusive mode.
 *
 * RETURNS:
 *	The current spl level.
 */
int
ticket_lock(xfs_inode_t *ip)
{
	int s;

#ifndef SIM
	s = splockspl(ip->i_ticketlock, splhi);
#endif
	return( s );
}

/*
 * ticket_unlock()
 *      Unlocks the per inode ticket lock.
 *
 * RETURNS:
 *	none
 */
void
ticket_unlock(xfs_inode_t *ip, int s)
{
#ifndef SIM
	spunlockspl(ip->i_ticketlock, s);
#endif
}


/*
 * xfs_get_file_extents()
 *	This routine creates the cononical forms of all the extents
 *	for the given file and returns them to the user.
 *
 *
 *
 *  RETURNS:
 *	0 on success
 *	~0 on failure
 */
int
xfs_get_file_extents(file_id_t *fileidp, xfs_bmbt_irec_t extents[], int *count)
{
	int			i, recsize, num_extents = 0, ret = 0;
	dev_t			fs_dev;
	xfs_ino_t		ino;
	xfs_inode_t 		*ip;
	xfs_bmbt_rec_t 		*ep;
	xfs_bmbt_irec_t 	thisrec;
	grio_bmbt_irec_t	*grec;

	xfs_crack_file_id( fileidp, &fs_dev, &ino);

#if defined(DEBUG) && !defined(SIM)
	if (grio_debug) {
		printf("get file extents: ino = %llx, dev = %x \n",
				(__int64_t)ino, fs_dev);
	}
#endif
	/*
 	 * Get the inode.
	 */
	if (!(ip = xfs_get_inode( fs_dev, ino ))) {
		ret = ENOENT;
		if (copyout( &num_extents, count, sizeof( num_extents))) {
			ret = EFAULT;
		}
		return( ret );
	}

	/*
	 * Get the number of extents in the file.
	 */
	num_extents = ip->i_d.di_nextents;

	if (num_extents) {

		/*
		 * Copy the extents if they exist.
		 */
		ASSERT(num_extents <  XFS_MAX_INCORE_EXTENTS);

		/*
		 * Read in the file extents from disk if necessary.
		 */
		if (!(ip->i_flags & XFS_IEXTENTS))
			xfs_iread_extents(NULL, ip);

		recsize = sizeof(grio_bmbt_irec_t) * num_extents;
		grec = kmem_alloc(recsize, KM_SLEEP );

		ep = ip->i_u1.iu_extents;

		ASSERT( ep );

		for (i = 0; i < num_extents; i++, ep++) {
			/*
 			 * copy extent numbers;
 			 */
			xfs_bmbt_get_all(ep, &thisrec);
			grec[i].br_startoff 	= thisrec.br_startoff;
			grec[i].br_startblock 	= thisrec.br_startblock;
			grec[i].br_blockcount 	= thisrec.br_blockcount;
		}

		if (copyout(grec, extents, recsize )) {
			ret = EFAULT;
		}
		kmem_free(grec, recsize);
	}

	/* 
	 * copyout to user space along with count.
 	 */
	if (copyout( &num_extents, count, sizeof( num_extents))) {
		ret = EFAULT;
	}

	xfs_iunlock( ip, XFS_ILOCK_EXCL );
	IRELE( ip );
	return( ret );
}

/*
 * xfs_get_file_rt()
 *	This routine determines if the given file has real time
 *	extents. If so a 1 is written to the user memory pointed at
 *	by rt, if not a 0 is written.
 *
 *
 * 
 */
xfs_get_file_rt( file_id_t *fileidp, int *rt)
{
	int 		inodert = 0, ret = 0;
	dev_t		fs_dev;
	xfs_ino_t	ino;
	xfs_inode_t 	*ip;

	xfs_crack_file_id( fileidp, &fs_dev, &ino);

#if defined(DEBUG) && !defined(SIM)
	if (grio_debug) {
		printf("get file rt: ino = %llx, dev = %x \n",
				(__int64_t)ino, fs_dev);
	}
#endif

	/*
 	 * Get the inode.
	 */
	if (!(ip = xfs_get_inode( fs_dev, ino ))) {
		return( ENOENT );
	}

	/*
	 * Check if the inode is marked as real time.
	 */
	if (ip->i_d.di_flags & XFS_DIFLAG_REALTIME) {
		inodert = 1;
	}

	/*
 	 * Copy the results to user space.
 	 */
	if (copyout( &inodert, rt, sizeof( rt))) {
		ret = EFAULT;
	}

	xfs_iunlock( ip, XFS_ILOCK_EXCL );
	IRELE( ip );
	return( ret );

}


/*
 * xfs_get_block_size()
 *	This routine determines the block size of the given
 *	file system copies it to the user memory pointed at by fs_size.
 *
 * RETURNS:
 *	0 on success
 *	-1 on failure
 */
xfs_get_block_size(dev_t fsdev, int *fs_size)
{
	int 		ret = 0;
	struct vfs	*vfsp;

	if (vfsp = vfs_devsearch( fsdev )) {
		if (copyout(&(vfsp->vfs_bsize), fs_size, sizeof(*fs_size))) {
			ret = EFAULT;
		}
	} else {
		ret = -1;
	}
	return( ret );
}


/*
 * xfs_remove_all_tickets()
 *	This routine is called by the xfs_remove_all_tickets() routine
 *	to remove the rate guarantee tickets from the inodes on a 
 *	specific file system. This routine removes the guarantee tickets
 *	and frees the memory space.
 *
 * RETURNS:
 *	none
 */
STATIC void
xfs_remove_tickets_from_fs(vfs_t *vfsp)
{
	xfs_mount_t 	*mp;
	xfs_inode_t 	*ip;
	grio_ticket_t	*ticket, *nextticket;


	mp = XFS_VFSTOM(vfsp);
	XFS_MOUNT_ILOCK(mp);
	if (mp->m_inodes == NULL) {
		XFS_MOUNT_IUNLOCK(mp);
		return;
	}

	ip = mp->m_inodes;
	do {
		for (ticket = ip->i_ticket; ticket; ticket = nextticket) {
			nextticket = ticket->nextticket;
			kmem_free( ticket, sizeof(grio_ticket_t));
		}
		ip->i_ticket = NULL;
		ip = ip->i_mnext;
	} while (ip != mp->m_inodes);
	XFS_MOUNT_IUNLOCK(mp);
}
	

/*
 * xfs_remove_all_tickets()
 *	This routine is called when the guarantee granting daemon
 *	is started to remove and previous guarantees that it does
 *	not know about. This only has meaning when the daemon crashes
 *	and needs to be restarted.
 *
 * RETURNS:
 * 	always returns 0
 */
int
xfs_remove_all_tickets()
{
	int 		s;
	vfs_t		*vfsp;
	extern int	xfs_type;
	extern vfs_t	*rootvfs;
	extern lock_t	vfslock;
	extern int	xfs_fstype;

	/*
 	 * For each file system on the machine.
	 */
loop:
#ifndef SIM
	s = splock(vfslock);
#endif
	for (vfsp = rootvfs; vfsp != NULL; vfsp = vfsp->vfs_next) {
		/*
 		 * If this is an xfs file system ...
		 */
		if (vfsp->vfs_fstype == xfs_fstype) {
			/*
			 * check if the file system is in the process
			 * of being mounted or unmounted.
			 */
#ifdef REDWOOD
			/*
			 * redwood uses a different vfs
			 * locking protocol
			 */
			if (vfsp->vfs_flag & VFS_MPBUSY) {
				vfsp->vfs_flag |= VFS_MPWAIT;
				if (spunlock_psema(vfslock,
						s, &vfsp->vfs_mbusy, PVFS|PCATCH))
					return EINTR;
				s = splock(vfslock);
                        	goto loop;
			} else {
#else
  			if (vfsp->vfs_flag & (VFS_MLOCK|VFS_MWANT)) {
                        	ASSERT(vfsp->vfs_flag & VFS_MWANT ||
                               		vfsp->vfs_busycnt == 0);
                        	vfsp->vfs_flag |= VFS_MWAIT;
                        	if (spunlock_psema(vfslock, s, 
					&vfsp->vfs_wait, PZERO)) {
                                	return EINTR;
                        	}
                        	goto loop;
			} else {
#endif /* REDWOOD */

#ifndef SIM
#ifdef REDWOOD
				/* for redwood vfs locking protocol */
				vfsp->vfs_flag |= VFS_MPBUSY;
#else
				vfsp->vfs_busycnt++;
#endif /* REDWOOD */
				spunlock(vfslock, s);
#endif

				/*
 	 			 * Remove the tickets from the inodes.
 	 			 */
				xfs_remove_tickets_from_fs(vfsp);


#ifndef SIM
				s = splock(vfslock);
#ifdef REDWOOD
				/* for redwood vfs locking protocol */

				ASSERT(vfsp->vfs_flag & VFS_MPBUSY);
				vfsp->vfs_flag &= ~VFS_MPBUSY;

				if (vfsp->vfs_flag & VFS_MPWAIT) {
					vfsp->vfs_flag &= ~VFS_MPWAIT;
					while (cvsema(&vfsp->vfs_mbusy))
						;
				}
#else
			        ASSERT(!(vfsp->vfs_flag & (VFS_MLOCK|VFS_OFFLINE)));
        			ASSERT(vfsp->vfs_busycnt > 0);
        			if (--vfsp->vfs_busycnt == 0) {
					/*
					 * If there's an updater (mount/unmount)
					 *  waiting for the vfs lock, wake up 
					 * only it.  Updater should be the first
					 *  on the sema queue.
					 *
					 * Otherwise, wake all accessors 
					 * (traverse() or vfs_syncall())
					 * waiting for the lock to clear.
					 */
       			 		if (vfsp->vfs_flag & VFS_MWANT) {
  		              			vsema(&vfsp->vfs_wait);
        				} else if (vfsp->vfs_flag & VFS_MWAIT) {
                				vfsp->vfs_flag &= ~VFS_MWAIT;
                				while (cvsema(&vfsp->vfs_wait))
                        				;
        				}

				}
#endif /* REDWOOD */
				
#endif
			}
		}
	}
#ifndef SIM
	spunlock(vfslock, s);
#endif
	return(0);
}

/*
 * xfs_remove_grio_guarantee()
 *	This routine constructs a grio msg structure to send to 
 *	the guarantee granting daemon which instructs it to remove 
 *	the rate guarantee for the given pid and inode. The message is
 * 	sent asynchronously.
 *
 * RETURNS:
 *	0 on success
 *	non zero on failure
 */
int
xfs_remove_grio_guarantee(xfs_inode_t *ip, pid_t pid)
{
	int		ret;
	grio_blk_t 	grioblk;
	extern int	grio_issue_async_grio_req(grio_blk_t *);
	extern void	bzero( void *, int);

	bzero(&grioblk, sizeof(grio_blk_t));

	grioblk.resv_type 	= GRIO_UNRESV_FILE_ASYNC;
	grioblk.procid 		= pid;
	grioblk.ino		= ip->i_ino;
	grioblk.fs_dev		= ip->i_dev;

	/*
 	 * Issue the message, do not wait for completion.
 	 */
	ret = grio_issue_async_grio_req(&grioblk);
	return( ret );
}

/*
 * xfs_mark_inode_grio()
 *	This routine sets the XFS_IGRIO flag from the incore
 *	inode structure of the file described by fileidp.
 *
 * RETURNS:
 *	0 on success
 *	1 on failure
 */
xfs_mark_inode_grio( file_id_t *fileidp)
{
	dev_t		fs_dev;
	xfs_ino_t	ino;
	xfs_inode_t	*ip;

	xfs_crack_file_id(fileidp, &fs_dev, &ino);

#if defined(DEBUG) && !defined(SIM)
	if (grio_debug) {
		printf("mark inode: ino = %llx, dev = %x \n",
			(__int64_t)ino, fs_dev);
	}
#endif
	/*
 	 * get inode
	 */
	if (!(ip = xfs_get_inode( fs_dev, ino ))) {
		return( 1 );
	}

	ip->i_flags |= XFS_IGRIO;

	xfs_iunlock( ip, XFS_ILOCK_EXCL );
	IRELE( ip );
	return( 0 );

}

/*
 * xfs_clear_inode_grio()
 *	This routine clears the XFS_IGRIO flag from the incore
 *	inode structure of the file described by fileidp.
 *
 * RETURNS:
 *	0 on success
 *	1 on failure
 */
int
xfs_clear_inode_grio( file_id_t *fileidp)
{
	dev_t		fs_dev;
	xfs_ino_t	ino;
	xfs_inode_t	*ip;

	xfs_crack_file_id(fileidp, &fs_dev, &ino);

#if defined(DEBUG) && !defined(SIM)
	if (grio_debug) {
		printf("clear inode: ino = %llx, dev = %x \n",
			(__int64_t)ino, fs_dev);
	}
#endif

	/*
 	 * Get the inode.
	 */
	if (!(ip = xfs_get_inode( fs_dev, ino ))) {
		return( 1 );
	}

	ip->i_flags &= ~XFS_IGRIO;

	xfs_iunlock( ip, XFS_ILOCK_EXCL );
	IRELE( ip );
	return( 0 );
}

/*
 * xfs_crack_file_id()
 *	This routine takes the guarantee granting daemon's version of
 *	the file inode number and the file system dev_t, and coverts
 *	them to the kernel's version. The ggd handles all inode numbers
 *	as 64 bit quantities, the kernel may use 32 or 64 bits to define
 *	inode numbers.
 *	The 
 *
 * RETURNS:
 *	0 on success
 *	non 0 no failure
 */
STATIC int
xfs_crack_file_id(file_id_t *ufile_idp, dev_t *fs_devp, xfs_ino_t *inop)
{
	file_id_t	file_id;
	
	if (copyin((char *)ufile_idp, (caddr_t)&file_id, sizeof(file_id))) {
#ifdef DEBUG
		printf("Could not access user memory.\n");
		ASSERT(0);
#endif
		return( EFAULT );
	}

	*fs_devp = file_id.fs_dev;
	*inop    = file_id.ino;
	return(0);
}
