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
#include <xfs_os_defs.h>
#define FSID_T
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/dcache.h>
#include <linux/xfs_cred.h>
#include <sys/vnode.h>

STATIC long long linvfs_file_lseek(
	struct file *file,
	loff_t offset,
	int origin)
{
	struct inode *inode = file->f_dentry->d_inode;
	vnode_t *vp;
	struct vattr vattr;
	loff_t old_off = offset;
	int error;

	vp = LINVFS_GET_VP(inode);

	ASSERT(vp);

	switch (origin) {
		case 2:
			vattr.va_mask = AT_SIZE;
			VOP_GETATTR(vp, &vattr, 0, get_current_cred(), error);
			if (error)
				return -error;

			offset += vattr.va_size;
			break;
		case 1:
			offset += file->f_pos;
	}

	/* All for the sake of seeing if we are too big */
	VOP_SEEK(vp, old_off, &offset, error);

	if (error)
		return -error;

	if (offset != file->f_pos) {
		file->f_pos = offset;
		file->f_version = ++event;
		file->f_reada = 0;
	}

	return offset;
}

STATIC ssize_t linvfs_read(
	struct file *filp,
	char *buf,
	size_t size,
	loff_t *offset)
{
	struct inode *inode;
	vnode_t *vp;
	int rv;
	
	XFS_STATS_INC(xs_read_calls);
	XFS_STATS64_ADD(xs_read_bytes, size);

	if (!filp || !filp->f_dentry ||
			!(inode = filp->f_dentry->d_inode)) {
		printk("EXIT linvfs_read -EBADF\n");
		return -EBADF;
	}

	inode = filp->f_dentry->d_inode;
	vp = LINVFS_GET_VP(inode);

	ASSERT(vp);

	VOP_READ(vp, filp, buf, size, offset, rv);
	return(rv);
}


STATIC ssize_t linvfs_write(
	struct file *filp,
	const char *buf,
	size_t size,
	loff_t *offset)
{
	struct inode *inode;
	loff_t	pos;
	vnode_t *vp;
	int	err;
	
	if (!filp || !filp->f_dentry ||
			!(inode = filp->f_dentry->d_inode)) {
		printk("EXIT linvfs_write -EBADF\n");
		return -EBADF;
	}

	inode = filp->f_dentry->d_inode;

	down(&inode->i_sem);

	err = -EINVAL;

	pos = *offset;
	if (pos < 0)
		goto out;

	err = filp->f_error;
	if (err) {
		filp->f_error = 0;
		goto out;
	}

	if (filp->f_flags & O_APPEND)
		pos = inode->i_size;

	XFS_STATS_INC(xs_write_calls);
	XFS_STATS64_ADD(xs_write_bytes, size);

	vp = LINVFS_GET_VP(inode);

	ASSERT(vp);

	VOP_WRITE(vp, (void *)filp, buf, size, &pos, err);

	*offset = pos;
out:
	up(&inode->i_sem);
	return(err);
}


STATIC int linvfs_open(
	struct inode *inode,
	struct file *filp)
{
	vnode_t *vp = LINVFS_GET_VP(inode);
	vnode_t *newvp;
	int	error;

	ASSERT(vp);

	VOP_OPEN(vp, &newvp, 0, get_current_cred(), error);

	return -error;
}


STATIC int linvfs_release(
	struct inode *inode,
	struct file *filp)
{
	vnode_t *vp = LINVFS_GET_VP(inode);
	int	error = 0;

	if (vp) {
		VOP_RELEASE(vp, error);
	}

	return -error;
}


STATIC int linvfs_fsync(
	struct file *filp,
	struct dentry *dentry,
	int datasync)
{
	struct inode *inode = filp->f_dentry->d_inode;
	vnode_t *vp = LINVFS_GET_VP(inode);
	int	error;
	int	flags = FSYNC_WAIT;

	if (datasync)
		flags |= FSYNC_DATA;

	ASSERT(vp);

	VOP_FSYNC(vp, flags, get_current_cred(),
		(off_t)0, (off_t)-1, error);

	if (error)
		return -error;

	return 0;
}

STATIC ssize_t linvfs_dir_read (struct file * filp, char * buf,
				size_t count, loff_t *ppos)
{
	return -EISDIR;
}

/*
 * linvfs_readdir maps to VOP_READDIR().
 * We need to build a uio, cred, ...
 */

STATIC int linvfs_readdir(
	struct file *filp,
	void *dirent,
	filldir_t filldir)
{
	struct inode		*inode;
	int			error = 0;
	vnode_t			*vp;
	uio_t			uio;
	iovec_t			iov;
	int			eof;
	cred_t			cred;		/* Temporary cred workaround */

	if (!filp || !filp->f_dentry ||
			!(inode = filp->f_dentry->d_inode) ||
					!S_ISDIR(inode->i_mode))
		return -EBADF;

        vp = LINVFS_GET_VP(filp->f_dentry->d_inode);

	ASSERT(vp);

	iov.iov_base = dirent;
	iov.iov_len = sizeof(dirent); /* Arbitrary. The real size is held in
					the abstract structure pointed to by dirent */
				      /* filldir will return an error if we go
				      	beyond the buffer. */
	uio.uio_iov = &iov;
	uio.uio_copy = (uio_copy_t)filldir;
	uio.uio_fmode = filp->f_mode;
	uio.uio_offset = filp->f_pos;
	uio.uio_segflg = UIO_USERSPACE;
	uio.uio_resid = 0;

	VOP_READDIR(vp, &uio, &cred, &eof, error);
	filp->f_pos = uio.uio_offset;
	return -error;
}


int linvfs_generic_file_mmap(struct file *filp, struct vm_area_struct *vma)
{
	vnode_t	*vp;
	int	ret;

	ret = generic_file_mmap(filp, vma);
	if (!ret) {
		vattr_t va, *vap;

		vap = &va;
		vap->va_mask = AT_UPDATIME;

		vp = LINVFS_GET_VP(filp->f_dentry->d_inode);

		ASSERT(vp);

		VOP_SETATTR(vp, vap, AT_UPDATIME, NULL, ret);
	}
	return(ret);
}


STATIC int linvfs_ioctl(
	struct inode	*inode,
	struct file	*filp,
	unsigned int	cmd,
	unsigned long	arg)
{
	int	error;
	vnode_t	*vp = LINVFS_GET_VP(inode);


	ASSERT(vp);

	VOP_IOCTL(vp, inode, filp, cmd, arg, error);

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
	llseek:		linvfs_file_lseek,
	read:		linvfs_read,  
	write:		linvfs_write,
	ioctl:		linvfs_ioctl,
	mmap:		linvfs_generic_file_mmap,
	open:		linvfs_open,
	release:	linvfs_release,
	fsync:		linvfs_fsync,
};

struct file_operations linvfs_dir_operations = {
	llseek:		linvfs_file_lseek,
	read:		linvfs_dir_read,
	readdir:	linvfs_readdir,
	ioctl:		linvfs_ioctl,
	open:		linvfs_open,
	fsync:		linvfs_fsync,
};


