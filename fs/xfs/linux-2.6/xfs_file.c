/*
 *  fs/xfs/xfs_linux_ops_file.c
 *
 */
#define FSID_T
#include <sys/types.h>
#include <sys/cred.h>
#include <linux/errno.h>

#include "xfs_coda_oops.h"

#include <linux/xfs_to_linux.h>

#undef NODEV
#include <linux/fs.h>
#include <linux/dcache.h>
#include <linux/sched.h>	/* To get current */

#include <linux/linux_to_xfs.h>

#include "xfs_file.h"
#include <sys/vnode.h>
#include <sys/mode.h>
#include <sys/uuid.h>
#include <xfs_linux.h>

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

	return offset;
}

static ssize_t linvfs_read(
	struct file *filp,
	char *buf,
	size_t size,
	loff_t *offset)
{
	int rval;
	rval = generic_file_read(filp, buf, size, offset);
	return(rval);
}


static ssize_t linvfs_write(
	struct file *filp,
	const char *buf,
	size_t size,
	loff_t *offset)
{
  return(-ENOSYS);
}


static int linvfs_ioctl(
	struct inode *inode,
	struct file *filp,
	unsigned int cmd,
	unsigned long arg)
{
  return(-ENOSYS);
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

	VOP_READDIR(vp, &uio, sys_cred, &eof, error);
	filp->f_pos = uio.uio_offset;
	return -error;
}


struct file_operations linvfs_file_operations =
{
	linvfs_file_lseek,
	linvfs_read,  
	linvfs_write,
	NULL,  /*  readdir  */
	NULL,  /*  poll  */
	linvfs_ioctl,
	generic_file_mmap,
	linvfs_open,
	NULL,	 		/*  flush - called from close */
	linvfs_release,		/* called on last close */
	linvfs_fsync,
	NULL,	 /*  fasync  */
	NULL,	 /*  check_media_change  */
	NULL,	/* revalidate */
	NULL	/* lock */
};

struct file_operations linvfs_dir_operations = {
	linvfs_file_lseek,
	linvfs_dir_read,
	NULL,	 /*  write  */
	linvfs_readdir,
	NULL,	 /*  poll  */
	linvfs_ioctl,
	NULL,  /*  mmap  */
	NULL,
	NULL,	 /*  flush  */
	linvfs_release,
	linvfs_fsync,
	NULL,	 /*  fasync  */
	NULL,	 /*  check_media_change  */
	NULL,	/* revalidate */
	NULL	/* lock */
};


