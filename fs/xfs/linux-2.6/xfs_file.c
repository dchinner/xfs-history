/*
 * Copyright (C) 1999 Silicon Graphics, Inc.  All Rights Reserved.
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston MA 02111-1307, USA.
 */

#define FSID_T
#include <linux/xfs_to_linux.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/dcache.h>
#include <linux/linux_to_xfs.h>
#include <linux/xfs_linux.h>
#include <linux/xfs_cred.h>
#include <sys/vnode.h>

static long long linvfs_file_lseek(
	struct file *file,
	long long offset,
	int origin)
{
	struct inode *inode = file->f_dentry->d_inode;
	vnode_t *vp;
	struct vattr vattr;
	long long old_off = offset;
	int error;

	vp = LINVFS_GET_VP(inode);

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

static ssize_t linvfs_read(
	struct file *filp,
	char *buf,
	size_t size,
	loff_t *offset)
{
	struct inode *inode;
	vnode_t *vp;
	int rv;
	
	if (!filp || !filp->f_dentry ||
			!(inode = filp->f_dentry->d_inode)) {
		printk("EXIT linvfs_read -EBADF\n");
		return -EBADF;
	}

	inode = filp->f_dentry->d_inode;
	vp = LINVFS_GET_VP(inode);

	VOP_READ(vp, filp, buf, size, offset, rv);
	return(rv);
}


static ssize_t linvfs_write(
	struct file *filp,
	const char *buf,
	size_t size,
	loff_t *offset)
{
	struct inode *inode;
	vnode_t *vp;
	int rv;
	
	if (!filp || !filp->f_dentry ||
			!(inode = filp->f_dentry->d_inode)) {
		printk("EXIT linvfs_write -EBADF\n");
		return -EBADF;
	}

	inode = filp->f_dentry->d_inode;
	vp = LINVFS_GET_VP(inode);

	VOP_WRITE(vp, (void *)filp, buf, size, offset, rv);

	return(rv);
}


static int linvfs_open(
	struct inode *inode,
	struct file *filp)
{
	vnode_t *vp = LINVFS_GET_VP(inode);
	vnode_t *newvp;
	int	error;

	VOP_OPEN(vp, &newvp, 0, get_current_cred(), error);

	if (error)
		return -error;

	return 0;
}


static int linvfs_release(
	struct inode *inode,
	struct file *filp)
{
  return(-ENOSYS);
}


static int linvfs_fsync(
	struct file *filp,
	struct dentry *dentry)
{
	struct inode *inode = filp->f_dentry->d_inode;
	vnode_t *vp = LINVFS_GET_VP(inode);
	int	error;

	VOP_FSYNC(vp, FSYNC_WAIT, get_current_cred(),
		(off_t)0, (off_t)-1, error);

	if (error)
		return -error;

	return 0;
}

static ssize_t linvfs_dir_read (struct file * filp, char * buf,
				size_t count, loff_t *ppos)
{
	return -EISDIR;
}

/*
 * linvfs_readdir maps to VOP_READDIR().
 * We need to build a uio, cred, ...
 */

static int linvfs_readdir(
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
	uio.uio_limit = PAGE_SIZE;	/* JIMJIM OK for now? */

	VOP_READDIR(vp, &uio, &cred, &eof, error);
	filp->f_pos = uio.uio_offset;
	return -error;
}


extern int linvfs_ioctl(
	struct inode *inode,
	struct file *filp,
	unsigned int cmd,
	unsigned long arg);


struct file_operations linvfs_file_operations =
{
	llseek:		linvfs_file_lseek,
	read:		linvfs_read,  
	write:		linvfs_write,
	ioctl:		linvfs_ioctl,
	mmap:		generic_file_mmap,
	open:		linvfs_open,
	release:	linvfs_release,
	fsync:		linvfs_fsync,
};

struct file_operations linvfs_dir_operations = {
	llseek:		linvfs_file_lseek,
	read:		linvfs_dir_read,
	readdir:	linvfs_readdir,
	ioctl:		linvfs_ioctl,
	release:	linvfs_release,
	fsync:		linvfs_fsync,
};


