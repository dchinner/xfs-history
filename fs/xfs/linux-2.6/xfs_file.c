/*
 * Copyright (c) 2000 Silicon Graphics, Inc.  All Rights Reserved.
 * 
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 * 
 * This program is distributed in the hope that it would be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * 
 * Further, this software is distributed without any warranty that it is
 * free of the rightful claim of any third person regarding infringement
 * or the like.  Any license provided herein, whether implied or
 * otherwise, applies only to this software file.  Patent licenses, if
 * any, provided herein do not apply to combinations of this program with
 * other software, or any other product whatsoever.
 * 
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write the Free Software Foundation, Inc., 59
 * Temple Place - Suite 330, Boston MA 02111-1307, USA.
 * 
 * Contact information: Silicon Graphics, Inc., 1600 Amphitheatre Pkwy,
 * Mountain View, CA  94043, or:
 * 
 * http://www.sgi.com 
 * 
 * For further information regarding this notice, see: 
 * 
 * http://oss.sgi.com/projects/GenInfo/SGIGPLNoticeExplan/
 */

#include <xfs.h>
#include <linux/dcache.h>
#include <linux/pagemap.h>
#include <linux/slab.h>


STATIC ssize_t
linvfs_read(
	struct file	*filp,
	char		*buf,
	size_t		size,
	loff_t		*offset)
{
	vnode_t		*vp;
	int		err;
	uio_t		uio;
	iovec_t		iov;

	vp = LINVFS_GET_VP(filp->f_dentry->d_inode);
	ASSERT(vp);

	uio.uio_iov = &iov;
	uio.uio_offset = *offset;
	uio.uio_fp = filp;
	uio.uio_iovcnt = 1;
	uio.uio_iov->iov_base = buf;
	uio.uio_iov->iov_len = uio.uio_resid = size;

	XFS_STATS_INC(xfsstats.xs_read_calls);
	XFS_STATS_ADD(xfsstats.xs_read_bytes, size);
        
	VOP_READ(vp, &uio, 0, NULL, NULL, err);
        *offset = uio.uio_offset;
        
	/*
	 * If we got a return value, it was an error
	 * Flip to negative & return that
	 * Otherwise, return bytes actually read
	 */
	return(err ? -err : size-uio.uio_resid);
}


STATIC ssize_t
linvfs_write(
	struct file	*file,
	const char	*buf,
	size_t		count,
	loff_t		*ppos)
{
	struct inode	*inode = file->f_dentry->d_inode;
	unsigned long	limit = current->rlim[RLIMIT_FSIZE].rlim_cur;
	loff_t		pos;
	vnode_t		*vp;
	int		err;	/* Use negative errors in this f'n */
	uio_t		uio;
	iovec_t		iov;

	if ((ssize_t) count < 0)
		return -EINVAL;

	if (!access_ok(VERIFY_READ, buf, count))
		return -EFAULT;

	down(&inode->i_sem);

	pos = *ppos;
	err = -EINVAL;
	if (pos < 0)
		goto out;

	err = file->f_error;
	if (err) {
		file->f_error = 0;
		goto out;
	}

	if (!S_ISBLK(inode->i_mode) && file->f_flags & O_APPEND)
		pos = inode->i_size;

	/*
	 * Check whether we've reached the file size limit.
	 */
	err = -EFBIG;
	
	if (limit != RLIM_INFINITY) {
		if (pos >= limit) {
			send_sig(SIGXFSZ, current, 0);
			goto out;
		}
		if (pos > 0xFFFFFFFFULL || count > limit - (u32)pos) {
			/* send_sig(SIGXFSZ, current, 0); */
			count = limit - (u32)pos;
		}
	}

	/*
	 *	LFS rule 
	 */
	if ( pos + count > MAX_NON_LFS && !(file->f_flags&O_LARGEFILE)) {
		if (pos >= MAX_NON_LFS) {
			send_sig(SIGXFSZ, current, 0);
			goto out;
		}
		if (count > MAX_NON_LFS - (u32)pos) {
			/* send_sig(SIGXFSZ, current, 0); */
			count = MAX_NON_LFS - (u32)pos;
		}
	}

	/*
	 *	Are we about to exceed the fs block limit ?
	 *
	 *	If we have written data it becomes a short write
	 *	If we have exceeded without writing data we send
	 *	a signal and give them an EFBIG.
	 *
	 *	Linus frestrict idea will clean these up nicely..
	 */
	 
	if (!S_ISBLK(inode->i_mode)) {
		if (pos >= inode->i_sb->s_maxbytes)
		{
			if (count || pos > inode->i_sb->s_maxbytes) {
				send_sig(SIGXFSZ, current, 0);
				err = -EFBIG;
				goto out;
			}
			/* zero-length writes at ->s_maxbytes are OK */
		}

		if (pos + count > inode->i_sb->s_maxbytes)
			count = inode->i_sb->s_maxbytes - pos;
	} else {
		if (is_read_only(inode->i_rdev)) {
			err = -EPERM;
			goto out;
		}
		if (pos >= inode->i_size) {
			if (count || pos > inode->i_size) {
				err = -ENOSPC;
				goto out;
			}
		}

		if (pos + count > inode->i_size)
			count = inode->i_size - pos;
	}

	err = 0;
	if (count == 0)
		goto out;

	XFS_STATS_INC(xfsstats.xs_write_calls);
	XFS_STATS_ADD(xfsstats.xs_write_bytes, count);

	vp = LINVFS_GET_VP(inode);
	ASSERT(vp);
        
	uio.uio_iov = &iov;
	uio.uio_offset = pos;
	uio.uio_fp = file;
	uio.uio_iovcnt = 1;
	uio.uio_iov->iov_base = (void *)buf;
	uio.uio_iov->iov_len = uio.uio_resid = count;
        
	VOP_WRITE(vp, &uio, file->f_flags, NULL, NULL, err);
	/* xfs_write returns positive errors */
	err = -err;	/* if it's 0 this is harmless */
	*ppos = pos = uio.uio_offset;
	count -= uio.uio_resid;
out:
	up(&inode->i_sem);

	/*
	 * If we got an error return that.
	 * Otherwise, return bytes actually written
	 */

	return(err ? err : count);
}


STATIC int
linvfs_open(
	struct inode	*inode,
	struct file	*filp)
{
	vnode_t		*vp = LINVFS_GET_VP(inode);
	vnode_t		*newvp;
	int		error;

	if (!(filp->f_flags & O_LARGEFILE) && inode->i_size > MAX_NON_LFS)
		return -EFBIG;

	ASSERT(vp);
	VOP_OPEN(vp, &newvp, 0, get_current_cred(), error);
	return -error;
}


STATIC int
linvfs_release(
	struct inode	*inode,
	struct file	*filp)
{
	vnode_t		*vp = LINVFS_GET_VP(inode);
	int		error = 0;

	if (vp)
		VOP_RELEASE(vp, error);
	return -error;
}


STATIC int
linvfs_fsync(
	struct file	*filp,
	struct dentry	*dentry,
	int		datasync)
{
	struct inode	*inode = dentry->d_inode;
	vnode_t		*vp = LINVFS_GET_VP(inode);
	int		error;
	int		flags = FSYNC_WAIT;

	if (datasync)
		flags |= FSYNC_DATA;

	ASSERT(vp);

	VOP_FSYNC(vp, flags, get_current_cred(),
		(off_t)0, (off_t)-1, error);

	return -error;
}

/*
 * linvfs_readdir maps to VOP_READDIR().
 * We need to build a uio, cred, ...
 */

#define nextdp(dp)      ((struct xfs_dirent *)((char *)(dp) + (dp)->d_reclen))

STATIC int
linvfs_readdir(
	struct file	*filp,
	void		*dirent,
	filldir_t	filldir)
{
	int		error = 0;
	vnode_t		*vp;
	uio_t		uio;
	iovec_t		iov;
	int		eof = 0;
	cred_t		cred;		/* Temporary cred workaround */
	caddr_t		read_buf;
	int		namelen, size = 0;
	size_t		rlen = PAGE_CACHE_SIZE << 2;
	off_t		start_offset;
	xfs_dirent_t	*dbp = NULL;

        vp = LINVFS_GET_VP(filp->f_dentry->d_inode);
	ASSERT(vp);

	/* Try fairly hard to get memory */
	do {
		if ((read_buf = (caddr_t)kmalloc(rlen, GFP_KERNEL)))
			break;
		rlen >>= 1;
	} while (rlen >= 1024);

	if (read_buf == NULL)
		return -ENOMEM;

	uio.uio_iov = &iov;
	uio.uio_fmode = filp->f_mode;
	uio.uio_segflg = UIO_SYSSPACE;
	uio.uio_offset = filp->f_pos;

	while (!eof) {
		uio.uio_resid = iov.iov_len = rlen;
		iov.iov_base = read_buf;
		uio.uio_iovcnt = 1;

		start_offset = uio.uio_offset;
		
		VOP_READDIR(vp, &uio, &cred, &eof, error);
		if ((uio.uio_offset == start_offset) || error) {
			size = 0;
			break;
		}

		size = rlen - uio.uio_resid;
		dbp = (xfs_dirent_t *)read_buf;
		while (size > 0) {
			namelen = strlen(dbp->d_name);

			if (filldir(dirent, dbp->d_name, namelen,
					(off_t) dbp->d_off,
					(linux_ino_t) dbp->d_ino,
					DT_UNKNOWN)) {
				goto done;
			}
			size -= dbp->d_reclen;
			dbp = nextdp(dbp);
		}
	}
done:
	if (!error) {
		if (size == 0)
			filp->f_pos = uio.uio_offset;
		else if (dbp)
			filp->f_pos = dbp->d_off;
	}

	kfree(read_buf);
	return -error;
}



#ifdef CONFIG_HAVE_XFS_DMAPI
STATIC int
linvfs_dmapi_map_event(
	struct file	*filp,
	struct vm_area_struct *vma,
	unsigned int	wantflag)
{
	vnode_t		*vp;
	xfs_inode_t	*ip;
	bhv_desc_t	*bdp;
	int		ret = 0;
	dm_fcntl_mapevent_t maprq;
	dm_eventtype_t	max_event = DM_EVENT_READ;

	vp = LINVFS_GET_VP(filp->f_dentry->d_inode);
	ASSERT(vp);

	if ((vp->v_type != VREG) || !(vp->v_vfsp->vfs_flag & VFS_DMI))
		return 0;

	/* If they specifically asked for 'read', then give it to them.
	 * Otherwise, see if it's possible to give them 'write'.
	 */
	if( wantflag & VM_READ ){
		max_event = DM_EVENT_READ;
	}
	else if( ! (vma->vm_flags & VM_DENYWRITE) ) {
		if((wantflag & VM_WRITE) || (vma->vm_flags & VM_WRITE))
			max_event = DM_EVENT_WRITE;
	}

	if( (wantflag & VM_WRITE) && (max_event != DM_EVENT_WRITE) ){
		return -EACCES;
	}

	maprq.max_event = max_event;

	/* Figure out how much of the file is being requested by the user. */
	maprq.length = 0; /* whole file, for now */

	bdp = bhv_base_unlocked(VN_BHV_HEAD(vp));
	ip = XFS_BHVTOI(bdp);

	if(DM_EVENT_ENABLED(vp->v_vfsp, ip, max_event)){
		xfs_dm_mapevent(bdp, 0, 0, &maprq);
		ret = maprq.error;
	}

	return -ret;
}
#endif


STATIC int
linvfs_generic_file_mmap(
	struct file	*filp,
	struct vm_area_struct *vma)
{
	vnode_t		*vp;
	int		ret;

	/* this will return a (-) error so flip */
	ret = -generic_file_mmap(filp, vma);
	if (!ret) {
		vattr_t va, *vap;

		vap = &va;
		vap->va_mask = AT_UPDATIME;

		vp = LINVFS_GET_VP(filp->f_dentry->d_inode);
		ASSERT(vp);

		VOP_SETATTR(vp, vap, AT_UPDATIME, NULL, ret);
		if(ret)
			goto out;

#ifdef	CONFIG_HAVE_XFS_DMAPI	/* Temporary until dmapi is in main kernel */
		if( filp->f_op->dmapi_map_event )
			ret = -filp->f_op->dmapi_map_event( filp, vma, 0 );
#endif
	}
out:
	return(-ret);
}


STATIC int
linvfs_ioctl(
	struct inode	*inode,
	struct file	*filp,
	unsigned int	cmd,
	unsigned long	arg)
{
	int		error;
	vnode_t		*vp = LINVFS_GET_VP(inode);

	ASSERT(vp);
	VOP_IOCTL(vp, inode, filp, cmd, arg, error);
	VMODIFY(vp);

	/* NOTE:  some of the ioctl's return positive #'s as a
	 *	  byte count indicating success, such as
	 *	  readlink_by_handle.  So we don't "sign flip"
	 *	  like most other routines.  This means true
	 *	  errors need to be returned as a negative value.
	 */
	return error;
}


struct file_operations linvfs_file_operations =
{
	llseek:		generic_file_llseek,
	read:		linvfs_read,  
	write:		linvfs_write,
	ioctl:		linvfs_ioctl,
	mmap:		linvfs_generic_file_mmap,
	open:		linvfs_open,
	release:	linvfs_release,
	fsync:		linvfs_fsync,
#ifdef	CONFIG_HAVE_XFS_DMAPI	/* Temporary until dmapi is in main kernel */
	dmapi_map_event:	linvfs_dmapi_map_event,
#endif
};

struct file_operations linvfs_dir_operations = {
	read:		generic_read_dir,
	readdir:	linvfs_readdir,
	ioctl:		linvfs_ioctl,
	fsync:		linvfs_fsync,
};
