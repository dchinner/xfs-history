#ident "$Header: /home/cattelan/xfs_cvs/xfs-for-git/fs/xfs/Attic/xfs_grio.c,v 1.3 1994/03/10 18:51:29 tap Exp $"

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
#include <sys/uuid.h>
#include <sys/vnode.h>
#include <sys/vfs.h>
#include <sys/kmem.h>
#include <sys/cred.h>
#include <sys/proc.h>
#include <sys/user.h>
#include <sys/fs/xfs_types.h>
#include <sys/fs/xfs_log.h>
#include <sys/fs/xfs_inum.h>
#ifdef SIM
#define _KERNEL
#endif
#include <sys/grio.h>
#ifdef SIM
#undef _KERNEL
#endif
#include <sys/fs/xfs_trans.h>
#include <sys/fs/xfs_sb.h>
#include <sys/fs/xfs_mount.h>
#include <sys/fs/xfs_alloc_btree.h>
#include <sys/fs/xfs_bmap_btree.h>
#include <sys/fs/xfs_btree.h>
#include <sys/fs/xfs_dinode.h>
#include <sys/fs/xfs_inode_item.h>
#include <sys/fs/xfs_inode.h>

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

#if defined(DEBUG) && !defined(SIM)
extern int	grio_debug;
#define GRIO_DBPRNT( level, str)        \
        if (grio_debug > level) {       \
                printf(str);            \
        }
#else
#define GRIO_DBPRNT(s1, s2 )
#endif

int xfs_grio_add_ticket( int, dev_t, int, char *);
int xfs_grio_remove_ticket( int, dev_t, char *);
xfs_inode_t *xfs_get_inode ( dev_t, int);
int xfs_add_ticket_to_inode( xfs_inode_t *, int, struct reservation_id *);
void xfs_remove_ticket_from_inode( xfs_inode_t *, struct reservation_id *);
grio_ticket_t *xfs_io_is_guaranteed( xfs_inode_t *, struct reservation_id *);
STATIC int xfs_grio_issue_io( vnode_t *, uio_t *,int, cred_t *,int);

void ticket_lock_shared( xfs_inode_t *);
void ticket_lock_exclusive( xfs_inode_t *);
void ticket_unlock( xfs_inode_t *);


extern int xfs_read_file(vnode_t *, uio_t *, int, cred_t *);
extern int xfs_write_file(vnode_t *, uio_t *, int, cred_t *);
extern int xfs_diordwr(vnode_t *,uio_t *, int, cred_t *,int);
extern struct vfs *vfs_devsearch( dev_t );


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
xfs_get_inode(  dev_t fs_dev, int ino)
{
        struct vfs *vfsp;
        xfs_inode_t *ip = NULL ;
	extern struct vfs *vfs_devsearch( dev_t );

        vfsp = vfs_devsearch( fs_dev );
        if (vfsp) {
                ip = xfs_iget( XFS_VFSTOM( vfsp ), NULL, ino, 0);
#ifdef DEBUG
		if (!ip) 
			printf("xfs_get failed on %d \n",ino);
#endif
        } 
#ifdef DEBUG
	else {
		printf("vfs_devsearch failed \n");
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
 */
int
xfs_add_ticket_to_inode( xfs_inode_t *ip, int sz, struct reservation_id *id )
{
	grio_ticket_t *ticket;
	int 	ret = 0;

	/*
 	 * Check if ticket with this id is already on the list.
 	 */
	if (xfs_io_is_guaranteed( ip, id ) ) {
		ret = -1;
	} else {
		if (ticket = kmem_zalloc( sizeof( grio_ticket_t), KM_SLEEP)) {
			ticket->sz = sz;
			ticket->id.ino = id->ino;
			ticket->id.pid = id->pid;
			ticket->lastreq = 0;
			if (ip->i_ticket) 
				ticket->nextticket = ip->i_ticket;
			/*
			 * This write must be atomic, otherwise
			 * the exclusive ticket lock must be held.
			 */
			ip->i_ticket = ticket;
		}
	}
	ticket_unlock(ip);
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
xfs_grio_add_ticket( int ino, dev_t fs_dev, int sz, char *idptr)
{
	int ret = -1;
	xfs_inode_t *ip;
	struct reservation_id id;


	if (copyin(idptr, (caddr_t)&id, sizeof(id))) {
#ifdef DEBUG
		printf("COULD NOT GET ID \n");
#endif
		ret = EFAULT;
		return( ret );
	}

#ifdef DEBUG
	{
		int t1,t2;
       		char    str[80];

		t1 = id.ino;
		t2 = id.pid;

        	sprintf(str,"grio add tkt: dev %x, ino %x, sz %x, id (%x,%x)\n",
                     fs_dev, ino, sz, t1, t2);
        	GRIO_DBPRNT(0, str);
	}
#endif
        /*
         * Lock the inode IOLOCK_EXCL so that the i_ticket
         * list in the inode can be safely updated.
         */
        if (ip = xfs_get_inode( fs_dev, ino )) {
		ret = xfs_add_ticket_to_inode( ip, sz, &id );
		xfs_iunlock( ip, XFS_ILOCK_EXCL );
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

	ticket_lock_exclusive( ip );
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
	ticket_unlock( ip );
	
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
xfs_grio_remove_ticket( int ino, dev_t fs_dev, char *idptr)
{
	xfs_inode_t *ip;
	struct reservation_id id;

	if (copyin(idptr, (caddr_t)&id, sizeof(id))) {
#ifdef DEBUG
		printf("COULD NOT GET ID \n");
#endif
		return( EFAULT );
	}

#ifdef DEBUG
	{
        	char str[80];
		int t1, t2;
	
		t1  = id.ino;
		t2  = id.pid;

       		sprintf(str,"grio rm tkt: dev %x, ino %x, id (%x,%x)\n",
                        fs_dev, ino, t1, t2);
        	GRIO_DBPRNT(0, str);
	}
#endif

	if (ip = xfs_get_inode( fs_dev, ino)) {
		xfs_remove_ticket_from_inode (ip, &id);
		xfs_iunlock( ip, XFS_ILOCK_EXCL );
	}
	return(0);
}

/*
 * xfs_io_is_guaranteed()
 *
 *	Check if a process using the given pid and fd has
 *	a ticket for guaranteed rate I/O on the given xfs
 *	file. If it does, check if the I/O size requested is
 *	within the guaranteed amount.
 *
 *	If the requested I/O is larger than the guaranteed 
 *	I/O, issue part of the request and put the requestor to
 *	sleep.
 *
 * RETURNS:
 *	0 if there is no guarantee
 *	non-zero if there is a guarantee
 */
grio_ticket_t *
xfs_io_is_guaranteed( xfs_inode_t *ip, struct reservation_id *id)
{
	grio_ticket_t *ticket;

	ticket_lock_shared( ip );
	ticket = ip->i_ticket;

	while (ticket && (!MATCH_ID((&(ticket->id)), id))) {
		ticket = ticket->nextticket;
	}
	return(ticket);
}
/*
 * xfs_request_larger_than_guarantee()
 *      This routine takes the given user request (uiop) and issues
 *      multiple requests of the guaranteed rate size until the user request
 *      size is less than or equal to the guaranteed rate size.
 *      The routine returns immediately if the rate guarantee is removed
 *
 *
 * RETURNS:
 *      0 on success
 *      non-zero on error
 */
int
xfs_request_larger_than_guarantee(xfs_inode_t *ip, 	
		struct reservation_id *id, 
		uio_t *uiop, 
		int ioflag, 
		cred_t *credp, 
		int rw)
{
        int     remainingio, ret = 0;
        grio_ticket_t *ticket;
	vnode_t *vp;

        /*
         * Check that rate guarantee ticket still exists.
         */
        if (ticket = xfs_io_is_guaranteed( ip, id)) {
                remainingio = uiop->uio_resid;
                uiop->uio_resid = 0;

                /*
                 * Keep issuing requests until the user request size
                 * is within the guaranteed amount.
                 */
                while ((remainingio > ticket->sz) && (!ret))  {
                        /*
                         * The size of the request is more than the guaranteed
                         * amount. Issue the requests in pieces, each piece
                         * the size of the guaranteed rate.
                         */
                        GRIO_DBPRNT(2,"request larger than guarantee.\n");

                        /*
                         * Set the I/O to be equal to the guaranteed rate size.
                         */
                        uiop->uio_resid = ticket->sz;
                        remainingio    -= ticket->sz;

                        /*
                         * Check if the request is within the time limits
                         * of the rate guarantee.
                         */
                        if (lbolt < (ticket->lastreq + HZ)) {
                                /*
                                 * This request is being issued too soon
                                 * after the last request for this guarantee.
                                 * It cannot be issued until the next second.
                                 */
                                GRIO_DBPRNT(2,"request issued too soon \n");
                                delay(ticket->lastreq + HZ - lbolt);
                        }
                        ticket->lastreq = lbolt;

                        /*
                         * Drop ticket lock before calling read/write.
                         */
                        ticket_unlock(ip);
                        ticket = NULL;

			vp = XFS_ITOV(ip);
                        /*
                         * Issue the request.
                         */
			ret = xfs_grio_issue_io(vp, uiop, ioflag, credp, rw);

                        /*
                         * Check for errors.
                         */
                        if (ret || (uiop->uio_resid != 0)) {
                                /*
                                 * Error occured.
                                 */
                                GRIO_DBPRNT(0, "GRIO RETURNING A -1 \n");
                                ret = -1;
                        }
                        /*
                         * Get the ticket again.
                         */
                        if ((ticket = xfs_io_is_guaranteed( ip, id)) == NULL) {
                                /*
                                 * Guarantee has been removed.
                                 */
                                GRIO_DBPRNT(0,"TICKET WAS REMOVED !! \n");
                                break;
                        }
                }
                /*
                 * Add unperformed I/O count to the resid.
                 */
                uiop->uio_resid += remainingio;
        }
        /*
         * Drop lock obtained by xfs_io_is_guaranteed().
         */
        ticket_unlock(ip);
        return(ret);
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
	int rw)
{
	grio_ticket_t *ticket;
	int sz, ret = 0, remainingio;
	vnode_t *vp;

        /*
         * Determine if this request has a guaranteed rate of I/O.
         */
        if (ticket = xfs_io_is_guaranteed( ip, id)) {
                /*
                 * Determine if the size of the request is
                 * within the limits of the guaranteed rate.
                 */
                if (uiop->uio_resid > ticket->sz) {
                        ticket_unlock(ip);
                        /*
                         * If the request is too large,
                         * break it into smaller pieces.
                         */
                        ret = xfs_request_larger_than_guarantee(
                                              ip, id, uiop, ioflag, credp,rw);
                        ticket = xfs_io_is_guaranteed( ip, id);
                }
                /*
                 * The request is the correct size, Check if
                 * it is being issued too soon after the last
                 * request for this guarantee.
                 */
                if ((!ret) && ticket) {
                        if (lbolt < (ticket->lastreq  + HZ)) {
                                /*
                                * This request cannot be
                                 * issued until the next second.
                                 */
                                GRIO_DBPRNT(2, "request issued too soon \n");
                                delay(ticket->lastreq + HZ - lbolt);
                        }
                        ticket->lastreq = lbolt;
                }
        }
        /*
         * Drop lock obtained by xfs_io_is_guaranteed().
         */
        ticket_unlock(ip);
        if (!ret) {
		vp = XFS_ITOV(ip);
		ret = xfs_grio_issue_io( vp, uiop, ioflag, credp, rw);
        }
        return (ret) ;
}

/*
 * xfs_grio_issue_io()
 *
 *
 */
STATIC int
xfs_grio_issue_io( vnode_t *vp,
		   uio_t   *uiop,
		   int     ioflag,
		   cred_t  *credp,
		   int     rw)
{
	int ret;

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
 * ticket_lock_shared()
 *      Locks the per inode ticket lock in shared mode.
 *
 */
void
ticket_lock_shared(xfs_inode_t *ip)
{
#ifndef SIM
        mrlock(&ip->i_ticketlock, MR_ACCESS, PINOD);
#endif
}

/*
 * ticket_lock_exclusive()
 *      Locks the per inode ticket lock in exclusive mode.
 *
 */
void
ticket_lock_exclusive(xfs_inode_t *ip)
{
#ifndef SIM
        mrlock(&ip->i_ticketlock, MR_UPDATE, PINOD);
#endif
}

/*
 * ticket_unlock()
 *      Unlocks the per inode ticket lock.
 *
 */
void
ticket_unlock(xfs_inode_t *ip)
{
#ifndef SIM
        mrunlock(&ip->i_ticketlock);
#endif
}


/*
 * xfs_get_file_extents()
 *
 *
 *
 *  RETURNS:
 *	0 on success
 *	~0 on failure
 */
int
xfs_get_file_extents(dev_t fsdev, 
		int ino, 
		xfs_bmbt_irec_t extents[], 
		int *count)
{
	xfs_inode_t *ip;
	xfs_bmbt_rec_t *ep;
	xfs_bmbt_irec_t *rec;
	int	i, recsize, num_extents, ret = 0;

	/*
 	 * get inode
	 */
	if (!(ip = xfs_get_inode( fsdev, ino ))) {
		return( 1 );

	}

	recsize = sizeof(xfs_bmbt_irec_t) * XFS_MAX_INCORE_EXTENTS;
	rec = kmem_alloc(recsize, KM_SLEEP );
	ASSERT(rec);
	num_extents = ip->i_bytes / sizeof(*ep);
	ep = ip->i_u1.iu_extents;


	for (i = 0; i < num_extents; i++, ep++) {
		/*
 		 * copy extent numbers;
 		 */
		xfs_bmbt_get_all( ep, rec[i]);
	}

	/* 
	 * copyout to user space along with count.
 	 */
	if (copyout( &num_extents, count, sizeof( num_extents))) {
		ret = EFAULT;
	}
	if (copyout(rec, extents,recsize )) {
		ret = EFAULT;
	}

	kmem_free(rec, 0 );
	xfs_iunlock( ip, XFS_ILOCK_EXCL );
	return( ret );
}

xfs_get_block_size(dev_t fsdev, int *fs_size)
{
	int ret = 0;
	struct vfs *vfsp;

	vfsp = vfs_devsearch( fsdev );
	if ( vfsp ) {
		if (copyout(&(vfsp->vfs_bsize), fs_size, sizeof(*fs_size))) {
			ret = EFAULT;
		}
	} else {
		ret = -1;
	}
	return( ret );
}
