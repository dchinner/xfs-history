#ident "$Header: /home/cattelan/xfs_cvs/xfs-for-git/fs/xfs/Attic/xfs_grio.c,v 1.14 1994/04/14 18:27:24 tap Exp $"

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

int xfs_grio_add_ticket( file_id_t *, int, char *);
int xfs_grio_remove_ticket( file_id_t *, char *);
xfs_inode_t *xfs_get_inode ( dev_t, int);
int xfs_add_ticket_to_inode( xfs_inode_t *, int, struct reservation_id *);
void xfs_remove_ticket_from_inode( xfs_inode_t *, struct reservation_id *);
grio_ticket_t *xfs_io_is_guaranteed( xfs_inode_t *, struct reservation_id *);
STATIC int xfs_grio_issue_io( vnode_t *, uio_t *,int, cred_t *,int);
STATIC int xfs_crack_file_id(file_id_t *, dev_t *, xfs_ino_t *);

void ticket_lock_shared( xfs_inode_t *);
void ticket_lock_exclusive( xfs_inode_t *);
void ticket_unlock( xfs_inode_t *);


extern int xfs_read_file(vnode_t *, uio_t *, int, cred_t *);
extern int xfs_write_file(vnode_t *, uio_t *, int, cred_t *);
extern int xfs_diordwr(vnode_t *,uio_t *, int, cred_t *,int);
extern struct vfs *vfs_devsearch( dev_t );
extern int strncmp(char *, char *, int);


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
        struct vfs 	*vfsp;
        xfs_inode_t 	*ip = NULL ;
	extern struct vfs *vfs_devsearch( dev_t );

        vfsp = vfs_devsearch( fs_dev );
        if (vfsp) {
		/*
		 * Verify that this is an xfs file system.
		 */
#ifndef SIM
		if (strncmp(vfssw[vfsp->vfs_fstype].vsw_name, "xfs", 3) == 0) {
#else
		{
#endif
                	ip = xfs_iget( XFS_VFSTOM( vfsp ), NULL, ino, XFS_ILOCK_EXCL);

#ifdef DEBUG
			if (!ip) 
				printf("xfs_get failed on %d \n",ino);
#endif

		}
        } else {
		printf("vfs_devsearch failed \n");
	}
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
xfs_grio_add_ticket( file_id_t *fileidp, int sz, char *idptr)
{
	xfs_ino_t	ino;
	dev_t		fs_dev;
	int ret = -1;
	xfs_inode_t *ip;
	struct reservation_id id;


	xfs_crack_file_id( fileidp, &fs_dev, &ino);

#ifdef DEBUG
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

#ifdef DEBUG
	{
		int t1,t2;
       		char    str[80];

		t1 = id.ino;
		t2 = id.pid;

        	sprintf(str,"grio add tkt: dev %x, ino %x, sz %x, id (%llx,%x)\n",
                     fs_dev, ino, sz, id.ino, id.pid);
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
xfs_grio_remove_ticket( file_id_t *fileidp, char *idptr)
{
	xfs_ino_t	ino;
	dev_t		fs_dev;
	xfs_inode_t *ip;
	struct reservation_id id;

	xfs_crack_file_id( fileidp, &fs_dev, &ino);
#ifdef DEBUG
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

#ifdef DEBUG
	{
        	char str[80];
		int t1, t2;
	
		t1  = id.ino;
		t2  = id.pid;

       		sprintf(str,"grio rm tkt: dev %x, ino %x, id (%llx,%x)\n",
                        fs_dev, ino, id.ino, id.pid);
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
                        uiop->uio_resid      = ticket->sz;
			uiop->uio_iov[0].iov_len = ticket->sz;
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
		if (ret) {
                	uiop->uio_resid += remainingio;
		} else {
                	uiop->uio_resid = remainingio;
			uiop->uio_iov[0].iov_len = remainingio;
		}
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
	int 		sz, ret = 0, remainingio;
	vnode_t 	*vp;
	grio_ticket_t 	*ticket;

        /*
         * Determine if this request has a guaranteed rate of I/O.
         */
        if (ticket = xfs_io_is_guaranteed( ip, id)) {
		/*
 		 * Mark this request as being rate guaranteed. 
		 */
		ioflag |= IO_GRIO;

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
				/*
 				 * Should lastreq be set to 0 here so that a 
				 * requestor will not be out of step forever?
				 * The max we delay is every other time.
 				 */
                        	ticket->lastreq = 0;
			} else {
				ticket->lastreq = lbolt;
			}

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
 * ticket_lock_shared()
 *      Locks the per inode ticket lock in shared mode.
 *
 * RETURNS:
 *	none
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
 * RETURNS:
 *	none
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
 * RETURNS:
 *	none
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
	dev_t			fs_dev;
	xfs_ino_t		ino;
	xfs_inode_t 		*ip;
	xfs_bmbt_rec_t 		*ep;
	xfs_bmbt_irec_t 	thisrec;
	grio_bmbt_irec_t	*grec;
	int			i, recsize, num_extents = 0, ret = 0;

	xfs_crack_file_id( fileidp, &fs_dev, &ino);

#ifdef DEBUG
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

	num_extents = ip->i_bytes / sizeof(*ep);
	if (num_extents) {

		/*
		 * Copy the extents if they exist.
		 */
		ASSERT(num_extents <  XFS_MAX_INCORE_EXTENTS);

		recsize = sizeof(grio_bmbt_irec_t) * num_extents;
		grec = kmem_alloc(recsize, KM_SLEEP );
		ASSERT(grec);

		ep = ip->i_u1.iu_extents;

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
		kmem_free(grec, 0 );
	}

	/* 
	 * copyout to user space along with count.
 	 */
	if (copyout( &num_extents, count, sizeof( num_extents))) {
		ret = EFAULT;
	}

	xfs_iunlock( ip, XFS_ILOCK_EXCL );
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

#ifdef DEBUG
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

	for ( ip = mp->m_inodes; ip; ip = ip->i_mnext ) {
		for (ticket = ip->i_ticket; ticket; ticket = nextticket) {
			nextticket = ticket->nextticket;
			kmem_free( ticket, sizeof(grio_ticket_t));
		}
		ip->i_ticket = NULL;
	}
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
	extern vfs_t	*rootvfs;
	extern lock_t	vfslock;
	extern int	xfs_type;

	/*
 	 * For each file system on the machine.
	 */
#ifndef SIM
	s = splock(vfslock);
#endif
	for (vfsp = rootvfs; vfsp != NULL; vfsp = vfsp->vfs_next) {
		/*
 		 * If is is an xfs file system ?
		 */
		if (vfsp->vfs_fstype == xfs_type) {
			/*
 	 		 * Remove the tickets from the inodes.
 	 		 */
			xfs_remove_tickets_from_fs(vfsp);
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
	grio_msg_t 	griomsg;
	extern int	grio_issue_async_grio_req(grio_msg_t *);
	extern void	bzero( void *, int);

	bzero(&griomsg, sizeof(grio_msg_t));

	griomsg.pid 			= pid;
	griomsg.grioblk.resv_type 	= GRIO_UNRESV_FILE_ASYNC;
	griomsg.grioblk.procid 		= pid;
	griomsg.grioblk.ino		= ip->i_ino;
	griomsg.grioblk.fs_dev		= ip->i_dev;

	/*
 	 * Issue the message, do not wait for completion.
 	 */
	ret = grio_issue_async_grio_req(&griomsg);
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

#ifdef DEBUG
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

	if (!(ip->i_flags & XFS_IGRIO)) {
		ip->i_flags |= XFS_IGRIO;
	} 

	xfs_iunlock( ip, XFS_ILOCK_EXCL );
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
#ifdef DEBUG
	if (grio_debug) {
		printf("clear inode: ino = %llx, dev = %x \n",
			(__int64_t)ino, fs_dev);
	}
#endif

	/*
 	 * get inode
	 */
	if (!(ip = xfs_get_inode( fs_dev, ino ))) {
		return( 1 );
	}

	if (ip->i_flags & XFS_IGRIO) {
		ip->i_flags &= ~XFS_IGRIO;
	} 
	xfs_iunlock( ip, XFS_ILOCK_EXCL );
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
