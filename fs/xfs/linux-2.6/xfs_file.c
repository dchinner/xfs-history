/*
 * Copyright (c) 2000-2005 Silicon Graphics, Inc.
 * All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write the Free Software Foundation,
 * Inc.,  51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */
#include "xfs.h"
#include "xfs_bit.h"
#include "xfs_log.h"
#include "xfs_inum.h"
#include "xfs_sb.h"
#include "xfs_ag.h"
#include "xfs_dir2.h"
#include "xfs_trans.h"
#include "xfs_dmapi.h"
#include "xfs_mount.h"
#include "xfs_bmap_btree.h"
#include "xfs_alloc_btree.h"
#include "xfs_ialloc_btree.h"
#include "xfs_alloc.h"
#include "xfs_btree.h"
#include "xfs_attr_sf.h"
#include "xfs_dir2_sf.h"
#include "xfs_dinode.h"
#include "xfs_inode.h"
#include "xfs_error.h"
#include "xfs_rw.h"
#include "xfs_ioctl32.h"
#include "xfs_vnodeops.h"

#include <linux/dcache.h>
#include <linux/smp_lock.h>

static struct vm_operations_struct xfs_file_vm_ops;
#ifdef HAVE_DMAPI
static struct vm_operations_struct xfs_dmapi_file_vm_ops;
#endif

STATIC_INLINE ssize_t
__xfs_file_read(
	struct kiocb		*iocb,
	const struct iovec	*iov,
	unsigned long		nr_segs,
	int			ioflags,
	loff_t			pos)
{
	struct file		*file = iocb->ki_filp;

	BUG_ON(iocb->ki_pos != pos);
	if (unlikely(file->f_flags & O_DIRECT))
		ioflags |= IO_ISDIRECT;
	return xfs_read(XFS_I(file->f_path.dentry->d_inode), iocb, iov,
				nr_segs, &iocb->ki_pos, ioflags);
}

STATIC ssize_t
xfs_file_aio_read(
	struct kiocb		*iocb,
	const struct iovec	*iov,
	unsigned long		nr_segs,
	loff_t			pos)
{
	return __xfs_file_read(iocb, iov, nr_segs, IO_ISAIO, pos);
}

STATIC ssize_t
xfs_file_aio_read_invis(
	struct kiocb		*iocb,
	const struct iovec	*iov,
	unsigned long		nr_segs,
	loff_t			pos)
{
	return __xfs_file_read(iocb, iov, nr_segs, IO_ISAIO|IO_INVIS, pos);
}

STATIC_INLINE ssize_t
__xfs_file_write(
	struct kiocb		*iocb,
	const struct iovec	*iov,
	unsigned long		nr_segs,
	int			ioflags,
	loff_t			pos)
{
	struct file	*file = iocb->ki_filp;

	BUG_ON(iocb->ki_pos != pos);
	if (unlikely(file->f_flags & O_DIRECT))
		ioflags |= IO_ISDIRECT;
	return xfs_write(XFS_I(file->f_mapping->host), iocb, iov, nr_segs,
				&iocb->ki_pos, ioflags);
}

STATIC ssize_t
xfs_file_aio_write(
	struct kiocb		*iocb,
	const struct iovec	*iov,
	unsigned long		nr_segs,
	loff_t			pos)
{
	return __xfs_file_write(iocb, iov, nr_segs, IO_ISAIO, pos);
}

STATIC ssize_t
xfs_file_aio_write_invis(
	struct kiocb		*iocb,
	const struct iovec	*iov,
	unsigned long		nr_segs,
	loff_t			pos)
{
	return __xfs_file_write(iocb, iov, nr_segs, IO_ISAIO|IO_INVIS, pos);
}

STATIC ssize_t
xfs_file_sendfile(
	struct file		*filp,
	loff_t			*pos,
	size_t			count,
	read_actor_t		actor,
	void			*target)
{
	return xfs_sendfile(XFS_I(filp->f_path.dentry->d_inode),
				filp, pos, 0, count, actor, target);
}

STATIC ssize_t
xfs_file_sendfile_invis(
	struct file		*filp,
	loff_t			*pos,
	size_t			count,
	read_actor_t		actor,
	void			*target)
{
	return xfs_sendfile(XFS_I(filp->f_path.dentry->d_inode),
				filp, pos, IO_INVIS, count, actor, target);
}

STATIC ssize_t
xfs_file_splice_read(
	struct file		*infilp,
	loff_t			*ppos,
	struct pipe_inode_info	*pipe,
	size_t			len,
	unsigned int		flags)
{
	return xfs_splice_read(XFS_I(infilp->f_path.dentry->d_inode),
				   infilp, ppos, pipe, len, flags, 0);
}

STATIC ssize_t
xfs_file_splice_read_invis(
	struct file		*infilp,
	loff_t			*ppos,
	struct pipe_inode_info	*pipe,
	size_t			len,
	unsigned int		flags)
{
	return xfs_splice_read(XFS_I(infilp->f_path.dentry->d_inode),
				   infilp, ppos, pipe, len, flags, IO_INVIS);
}

STATIC ssize_t
xfs_file_splice_write(
	struct pipe_inode_info	*pipe,
	struct file		*outfilp,
	loff_t			*ppos,
	size_t			len,
	unsigned int		flags)
{
	return xfs_splice_write(XFS_I(outfilp->f_path.dentry->d_inode),
				    pipe, outfilp, ppos, len, flags, 0);
}

STATIC ssize_t
xfs_file_splice_write_invis(
	struct pipe_inode_info	*pipe,
	struct file		*outfilp,
	loff_t			*ppos,
	size_t			len,
	unsigned int		flags)
{
	return xfs_splice_write(XFS_I(outfilp->f_path.dentry->d_inode),
				    pipe, outfilp, ppos, len, flags, IO_INVIS);
}

STATIC int
xfs_file_open(
	struct inode	*inode,
	struct file	*filp)
{
	if (!(filp->f_flags & O_LARGEFILE) && i_size_read(inode) > MAX_NON_LFS)
		return -EFBIG;
	return -xfs_open(XFS_I(inode));
}

STATIC int
xfs_file_release(
	struct inode	*inode,
	struct file	*filp)
{
	return -xfs_release(XFS_I(inode));
}

STATIC int
xfs_file_fsync(
	struct file	*filp,
	struct dentry	*dentry,
	int		datasync)
{
	bhv_vnode_t	*vp = vn_from_inode(dentry->d_inode);
	int		flags = FSYNC_WAIT;

	if (datasync)
		flags |= FSYNC_DATA;
	if (VN_TRUNC(vp))
		VUNTRUNCATE(vp);
	return -xfs_fsync(XFS_I(dentry->d_inode), flags,
			(xfs_off_t)0, (xfs_off_t)-1);
}

#ifdef HAVE_DMAPI
STATIC struct page *
xfs_vm_nopage(
	struct vm_area_struct	*area,
	unsigned long		address,
	int			*type)
{
	struct inode	*inode = area->vm_file->f_path.dentry->d_inode;
	bhv_vnode_t	*vp = vn_from_inode(inode);

	ASSERT_ALWAYS(vp->v_vfsp->vfs_flag & VFS_DMI);
	if (XFS_SEND_MMAP(XFS_VFSTOM(vp->v_vfsp), area, 0))
		return NULL;
	return filemap_nopage(area, address, type);
}
#endif /* HAVE_DMAPI */

STATIC int
xfs_file_readdir(
	struct file	*filp,
	void		*dirent,
	filldir_t	filldir)
{
	struct inode	*inode = filp->f_path.dentry->d_inode;
	xfs_inode_t	*ip = XFS_I(inode);
	int		error;
	size_t		bufsize;

	/*
	 * The Linux API doesn't pass down the total size of the buffer
	 * we read into down to the filesystem.  With the filldir concept
	 * it's not needed for correct information, but the XFS dir2 leaf
	 * code wants an estimate of the buffer size to calculate it's
	 * readahead window and size the buffers used for mapping to
	 * physical blocks.
	 *
	 * Try to give it an estimate that's good enough, maybe at some
	 * point we can change the ->readdir prototype to include the
	 * buffer size.
	 */
	bufsize = (size_t)min_t(loff_t, PAGE_SIZE, inode->i_size);

	error = xfs_readdir(ip, dirent, bufsize,
				(xfs_off_t *)&filp->f_pos, filldir);
	if (error)
		return -error;
	return 0;
}

STATIC int
xfs_file_mmap(
	struct file	*filp,
	struct vm_area_struct *vma)
{
	vma->vm_ops = &xfs_file_vm_ops;

#ifdef HAVE_DMAPI
	if (vn_from_inode(filp->f_path.dentry->d_inode)->v_vfsp->vfs_flag & VFS_DMI)
		vma->vm_ops = &xfs_dmapi_file_vm_ops;
#endif /* HAVE_DMAPI */

	file_accessed(filp);
	return 0;
}

STATIC long
xfs_file_ioctl(
	struct file	*filp,
	unsigned int	cmd,
	unsigned long	p)
{
	int		error;
	struct inode	*inode = filp->f_path.dentry->d_inode;
	bhv_vnode_t	*vp = vn_from_inode(inode);

	error = xfs_ioctl(XFS_I(inode), filp, 0, cmd, (void __user *)p);
	VMODIFY(vp);

	/* NOTE:  some of the ioctl's return positive #'s as a
	 *	  byte count indicating success, such as
	 *	  readlink_by_handle.  So we don't "sign flip"
	 *	  like most other routines.  This means true
	 *	  errors need to be returned as a negative value.
	 */
	return error;
}

STATIC long
xfs_file_ioctl_invis(
	struct file	*filp,
	unsigned int	cmd,
	unsigned long	p)
{
	int		error;
	struct inode	*inode = filp->f_path.dentry->d_inode;
	bhv_vnode_t	*vp = vn_from_inode(inode);

	error = xfs_ioctl(XFS_I(inode), filp, IO_INVIS, cmd, (void __user *)p);
	VMODIFY(vp);

	/* NOTE:  some of the ioctl's return positive #'s as a
	 *	  byte count indicating success, such as
	 *	  readlink_by_handle.  So we don't "sign flip"
	 *	  like most other routines.  This means true
	 *	  errors need to be returned as a negative value.
	 */
	return error;
}

#ifdef HAVE_DMAPI
#ifdef HAVE_VMOP_MPROTECT
STATIC int
xfs_vm_mprotect(
	struct vm_area_struct *vma,
	unsigned int	newflags)
{
	bhv_vnode_t	*vp = vn_from_inode(vma->vm_file->f_path.dentry->d_inode);
	int		error = 0;

	if (vp->v_vfsp->vfs_flag & VFS_DMI) {
		if ((vma->vm_flags & VM_MAYSHARE) &&
		    (newflags & VM_WRITE) && !(vma->vm_flags & VM_WRITE)) {
			xfs_mount_t	*mp = XFS_VFSTOM(vp->v_vfsp);

			error = XFS_SEND_MMAP(mp, vma, VM_WRITE);
		    }
	}
	return error;
}
#endif /* HAVE_VMOP_MPROTECT */
#endif /* HAVE_DMAPI */

#ifdef HAVE_FOP_OPEN_EXEC
/* If the user is attempting to execute a file that is offline then
 * we have to trigger a DMAPI READ event before the file is marked as busy
 * otherwise the invisible I/O will not be able to write to the file to bring
 * it back online.
 */
STATIC int
xfs_file_open_exec(
	struct inode	*inode)
{
	bhv_vnode_t	*vp = vn_from_inode(inode);

	if (unlikely(vp->v_vfsp->vfs_flag & VFS_DMI)) {
		xfs_mount_t	*mp = XFS_VFSTOM(vp->v_vfsp);
		xfs_inode_t	*ip = xfs_vtoi(vp);

		if (!ip)
			return -EINVAL;
		if (DM_EVENT_ENABLED(ip, DM_EVENT_READ))
			return -XFS_SEND_DATA(mp, DM_EVENT_READ, vp,
					       0, 0, 0, NULL);
	}
	return 0;
}
#endif /* HAVE_FOP_OPEN_EXEC */

/*
 * mmap()d file has taken write protection fault and is being made
 * writable. We can set the page state up correctly for a writable
 * page, which means we can do correct delalloc accounting (ENOSPC
 * checking!) and unwritten extent mapping.
 */
STATIC int
xfs_vm_page_mkwrite(
	struct vm_area_struct	*vma,
	struct page		*page)
{
	return block_page_mkwrite(vma, page, xfs_get_blocks);
}

const struct file_operations xfs_file_operations = {
	.llseek		= generic_file_llseek,
	.read		= do_sync_read,
	.write		= do_sync_write,
	.aio_read	= xfs_file_aio_read,
	.aio_write	= xfs_file_aio_write,
	.sendfile	= xfs_file_sendfile,
	.splice_read	= xfs_file_splice_read,
	.splice_write	= xfs_file_splice_write,
	.unlocked_ioctl	= xfs_file_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl	= xfs_file_compat_ioctl,
#endif
	.mmap		= xfs_file_mmap,
	.open		= xfs_file_open,
	.release	= xfs_file_release,
	.fsync		= xfs_file_fsync,
#ifdef HAVE_FOP_OPEN_EXEC
	.open_exec	= xfs_file_open_exec,
#endif
};

const struct file_operations xfs_invis_file_operations = {
	.llseek		= generic_file_llseek,
	.read		= do_sync_read,
	.write		= do_sync_write,
	.aio_read	= xfs_file_aio_read_invis,
	.aio_write	= xfs_file_aio_write_invis,
	.sendfile	= xfs_file_sendfile_invis,
	.splice_read	= xfs_file_splice_read_invis,
	.splice_write	= xfs_file_splice_write_invis,
	.unlocked_ioctl	= xfs_file_ioctl_invis,
#ifdef CONFIG_COMPAT
	.compat_ioctl	= xfs_file_compat_invis_ioctl,
#endif
	.mmap		= xfs_file_mmap,
	.open		= xfs_file_open,
	.release	= xfs_file_release,
	.fsync		= xfs_file_fsync,
};


const struct file_operations xfs_dir_file_operations = {
	.read		= generic_read_dir,
	.readdir	= xfs_file_readdir,
	.unlocked_ioctl	= xfs_file_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl	= xfs_file_compat_ioctl,
#endif
	.fsync		= xfs_file_fsync,
};

static struct vm_operations_struct xfs_file_vm_ops = {
	.nopage		= filemap_nopage,
	.populate	= filemap_populate,
	.page_mkwrite	= xfs_vm_page_mkwrite,
};

#ifdef HAVE_DMAPI
static struct vm_operations_struct xfs_dmapi_file_vm_ops = {
	.nopage		= xfs_vm_nopage,
	.populate	= filemap_populate,
	.page_mkwrite	= xfs_vm_page_mkwrite,
#ifdef HAVE_VMOP_MPROTECT
	.mprotect	= xfs_vm_mprotect,
#endif
};
#endif /* HAVE_DMAPI */
