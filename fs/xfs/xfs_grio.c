#ident "$Header: /home/cattelan/xfs_cvs/xfs-for-git/fs/xfs/Attic/xfs_grio.c,v 1.57 1995/06/12 15:40:15 tap Exp $"

#include <sys/types.h>
#include <string.h>
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
#define _KERNEL 1
#endif
#include <sys/vnode.h>
#include <sys/file.h>
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
#include <sys/fs/xfs_attr_sf.h>
#include <sys/fs/xfs_dir_sf.h>
#include <sys/fs/xfs_dinode.h>
#include <sys/fs/xfs_inode_item.h>
#include <sys/fs/xfs_inode.h>
#include <sys/fs/xfs_itable.h>
#include <sys/fs/xfs_error.h>
#include <sys/pda.h>

#ifdef SIM
#include "sim.h"
#include <stdio.h>
#endif

#define IRELE(ip)		VN_RELE(XFS_ITOV(ip))

xfs_inode_t 			*xfs_get_inode ( dev_t, xfs_ino_t);
extern struct vfs 		*vfs_devsearch( dev_t );
extern grio_stream_info_t 	* grio_find_stream_with_proc_dev_inum( 
					pid_t, dev_t, xfs_ino_t);


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
        struct vfs		*vfsp;
        xfs_inode_t		*ip = NULL ;
	int			error;
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
                	error = xfs_iget( XFS_VFSTOM( vfsp ), 
					NULL, ino, XFS_ILOCK_SHARED, &ip, 0);

			if ( error ) {
				ip = NULL;
			}

			if ( (ip == NULL) || (ip->i_d.di_mode == 0) ) {
				if (ip) {
					xfs_iunlock( ip, XFS_ILOCK_SHARED );
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
 * xfs_get_file_extents()
 *	This routine creates the cononical forms of all the extents
 *	for the given file and returns them to the user.
 *
 *  RETURNS:
 *	0 on success
 *	non zero on failure
 */
int
xfs_get_file_extents(
	sysarg_t sysarg_file_id,
	sysarg_t sysarg_extents_addr,
	sysarg_t sysarg_count)
{
	int			i, recsize, num_extents = 0;
	int			error = 0;
	dev_t			fs_dev;
	xfs_ino_t		ino;
	xfs_inode_t 		*ip;
	xfs_bmbt_rec_t 		*ep;
	xfs_bmbt_irec_t 	thisrec;
	grio_bmbt_irec_t	*grec;
	grio_file_id_t		fileid;
	caddr_t			extents, count;

	if ( copyin((caddr_t)sysarg_file_id, &fileid, sizeof(grio_file_id_t))) {
		error = XFS_ERROR(EFAULT);
		return( error );
	}

	fs_dev 		= fileid.fs_dev;
	ino		= fileid.ino;

	/*
	 * Get sysarg arguements
	 */
	extents		= (caddr_t)sysarg_extents_addr;
	count		= (caddr_t)sysarg_count;

	/*
 	 * Get the inode
	 */
	if (!(ip = xfs_get_inode( fs_dev, ino ))) {
		error = XFS_ERROR(ENOENT);
		if (copyout( 	&num_extents, 
				(caddr_t)count, 
				sizeof( num_extents))) {

			error = XFS_ERROR(EFAULT);
		}
		return( error );
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
		if (!(ip->i_df.if_flags & XFS_IFEXTENTS)) {
			error = xfs_iread_extents(NULL, ip, XFS_DATA_FORK);
			if (error) {
				goto out;
			}
		}

		recsize = sizeof(grio_bmbt_irec_t) * num_extents;
		grec = kmem_alloc(recsize, KM_SLEEP );

		ep = ip->i_df.if_u1.if_extents;

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

		if (copyout(grec, (caddr_t)extents, recsize )) {
			error = XFS_ERROR(EFAULT);
		}
		kmem_free(grec, recsize);
	}

	/* 
	 * copyout to user space along with count.
 	 */
	if (copyout( &num_extents, (caddr_t)count, sizeof( num_extents))) {
		error = XFS_ERROR(EFAULT);
	}

 out:
	xfs_iunlock( ip, XFS_ILOCK_SHARED );
	IRELE( ip );
	return( error );
}

/*
 * xfs_get_file_rt()
 *	This routine determines if the given file has real time
 *	extents. If so a 1 is written to the user memory pointed at
 *	by rt, if not a 0 is written.
 *
 *
 * RETURNS:
 *	0 on success
 *	non zero on failure
 */
xfs_get_file_rt( 
	sysarg_t sysarg_file_id,
	sysarg_t sysarg_rt)
{
	int 		inodert = 0, error = 0;
	dev_t		fs_dev;
	xfs_ino_t	ino;
	xfs_inode_t 	*ip;
	caddr_t		rt;
	grio_file_id_t	fileid;


	if ( copyin((caddr_t)sysarg_file_id, &fileid, sizeof(grio_file_id_t))) {
		error = XFS_ERROR(EFAULT);
		return( error );
	}

	rt		= (caddr_t)sysarg_rt;
	fs_dev 		= fileid.fs_dev;
	ino		= fileid.ino;

	/*
 	 * Get the inode.
	 */
	if (!(ip = xfs_get_inode( fs_dev, ino ))) {
		error = XFS_ERROR( ENOENT );
		return( error );
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
	if ( copyout( &inodert, (caddr_t)rt, sizeof(int)) ) {
		error = XFS_ERROR(EFAULT);
	}

	xfs_iunlock( ip, XFS_ILOCK_SHARED );
	IRELE( ip );
	return( error );

}


/*
 * xfs_get_block_size()
 *	This routine determines the block size of the given
 *	file system copies it to the user memory pointed at by fs_size.
 *
 * RETURNS:
 *	0 on success
 *	non zero on failure
 */
xfs_get_block_size(
	sysarg_t sysarg_fs_dev, 
	sysarg_t sysarg_fs_size)
{
	int 		error = 0;
	dev_t		fs_dev;
	caddr_t		fs_size;
	struct vfs	*vfsp;


	fs_dev 		= (dev_t)sysarg_fs_dev;
	fs_size		= (caddr_t)sysarg_fs_size;

	if ( vfsp = vfs_devsearch( fs_dev ) ) {
		if ( copyout(	&(vfsp->vfs_bsize), 
				(caddr_t)fs_size, 
				sizeof(u_int)) ) {

			error = XFS_ERROR(EFAULT);
		}
	} else {
		error = XFS_ERROR( EIO );
	}
	return( error );
}



/*
 * xfs_io_is_guaranteed()
 *
 * RETURNS:
 *	0 if there is no guarantee
 *	non-zero if there is a guarantee
 */
int
xfs_io_is_guaranteed( xfs_inode_t *ip, stream_id_t *stream_id)
{
	pid_t			proc_id;
	dev_t			fs_dev;
	xfs_ino_t		inum;
	grio_stream_info_t	*griosp;

	proc_id = private.p_curproc->p_pid;
	fs_dev 	= ip->i_dev;
	inum 	= ip->i_ino;

	griosp = grio_find_stream_with_proc_dev_inum( proc_id, fs_dev, inum);
	if ( griosp ) {
		COPY_STREAM_ID( griosp->stream_id, (*stream_id) );
		return( 1 );
	} else {
		return( 0 );
	}
}

/*
 * xfs_grio_get_inumber
 *	Convert a users file descriptor to an inode number.
 *
 * RETURNS:
 *	64 bit inode number
 *
 * CAVEAT:
 *	this must be called from context of the user process
 */
xfs_ino_t 
xfs_grio_get_inumber( int fdes )
{
	file_t	*fp;
	vnode_t	*vp;
	xfs_inode_t	*ip;

	if ( getf( fdes, &fp ) != 0 ) {
		return( (xfs_ino_t)0 );
	}

	vp = fp->f_vnode;
	ip = XFS_VTOI( vp );
	return( ip->i_ino );
}


/*
 * xfs_grio_get_fs_dev
 *	Convert a users file descriptor to a file system device.
 *
 * RETURNS:
 *	the dev_t of the file system where the file resides
 *
 * CAVEAT:
 *	this must be called from the context of the user process
 */
dev_t 
xfs_grio_get_fs_dev( int fdes )
{
	file_t	*fp;
	vnode_t	*vp;
	xfs_inode_t	*ip;

	if ( getf( fdes, &fp ) != 0 ) {
		return( 0 );
	}

	vp = fp->f_vnode;
	ip = XFS_VTOI( vp );
	return( ip->i_dev );
}
