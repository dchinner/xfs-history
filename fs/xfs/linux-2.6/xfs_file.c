/*
 *  fs/xfs/xfs_linux_ops_file.c
 *
 */
#include <sys/types.h>
#include <sys/cred.h>
#include <linux/errno.h>

#include "xfs_coda_oops.h"

#include <linux/fs.h>
#include <linux/dcache.h>
#include <linux/sched.h>	/* To get current */

#include "xfs_file.h"
#include <sys/vnode.h>
#include <sys/mode.h>
#include <xfs_linux.h>


ssize_t linvfs_read(struct file *filp, const char *buf, size_t size, loff_t *offset)
{
  return(-ENOSYS);
}


ssize_t linvfs_write(struct file *filp, const char *buf, size_t size, loff_t *offset)
{
  return(-ENOSYS);
}


int linvfs_ioctl(struct inode *inode, struct file *filp, unsigned int cmd, unsigned long arg)
{
  return(-ENOSYS);
}


int linvfs_open(struct inode *inode, struct file *filp)
{
  return(-ENOSYS);
}


int linvfs_release(struct inode *inode, struct file *filp)
{
  return(-ENOSYS);
}


int linvfs_fsync(struct file *filp, struct dentry *dentry)
{
  return(-ENOSYS);
}

/*
 * linvfs_readdir maps to VOP_READDIR().
 * We need to build a uio, cred, ...
 */

int linvfs_readdir(struct file *filp, void *dirent, filldir_t filldir)
{
	struct inode		*inode;
	int			error = 0;
	vnode_t			*vp;
	uio_t			uio;
	iovec_t			iov;
	int			eof;

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
	uio.uio_copy = filldir;
	uio.uio_fmode = filp->f_mode;
	uio.uio_offset = filp->f_pos;
	uio.uio_segflg = UIO_USERSPACE;
	uio.uio_resid = 0;
	uio.uio_limit = PAGE_SIZE;	/* JIMJIM OK for now? */

	VOP_READDIR(vp, &uio, sys_cred, &eof, error);
	return error;
}


static struct file_operations linvfs_file_operations =
{
  NULL,  /*  lseek  */
  linvfs_read,  
  linvfs_write,
  NULL,  /*  readdir  */
  NULL,  /*  poll  */
  linvfs_ioctl,
  generic_file_mmap,
  linvfs_open,
  NULL,	 /*  flush  */
  linvfs_release,
  linvfs_fsync,
  NULL,	 /*  fasync  */
  NULL,	 /*  check_media_change  */
  NULL	 /*  revalidate  */
};

static struct file_operations linvfs_dir_operations = {
  NULL,  /*  lseek  */
  NULL,	 /*  read  */
  NULL,	 /*  write  */
  linvfs_readdir,
  NULL,	 /*  poll  */
  linvfs_ioctl,
  NULL,  /*  mmap  */
  linvfs_open,
  NULL,	 /*  flush  */
  linvfs_release,
  linvfs_fsync,
  NULL,	 /*  fasync  */
  NULL,	 /*  check_media_change  */
  NULL	/*  revalidate  */
};


