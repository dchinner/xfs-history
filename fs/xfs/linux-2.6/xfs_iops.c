/*
 * Copyright (c) 2000-2002 Silicon Graphics, Inc.  All Rights Reserved.
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
 * or the like.	 Any license provided herein, whether implied or
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
#include <linux/mm.h>
#include <linux/pagemap.h>
#include <linux/mpage.h>
#include <linux/iobuf.h>
#include <linux/xfs_iops.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,5,9)
#define page_buffers(page)	((page)->buffers)
#define page_has_buffers(page)	((page)->buffers)
#endif


/*
 * Pull the link count and size up from the xfs inode to the linux inode
 */
STATIC void
validate_fields(
	struct inode	*ip)
{
	vnode_t		*vp = LINVFS_GET_VP(ip);
	vattr_t		va;
	int		error;

	va.va_mask = AT_NLINK|AT_SIZE;
	VOP_GETATTR(vp, &va, ATTR_LAZY, NULL, error);
	ip->i_nlink = va.va_nlink;
	ip->i_size = va.va_size;
	ip->i_blocks = va.va_nblocks;
}

#ifdef CONFIG_FS_POSIX_ACL
/*
 * Determine whether a process has a valid fs_struct (kernel daemons
 * like knfsd don't have an fs_struct).
 */
STATIC int inline
has_fs_struct(struct task_struct *task)
{
	return (task->fs != init_task.fs);
}
#endif

STATIC int
linvfs_mknod(
	struct inode	*dir,
	struct dentry	*dentry,
	int		mode,
	int		rdev)
{
	struct inode	*ip;
	vattr_t		va;
	vnode_t		*vp = NULL, *dvp = LINVFS_GET_VP(dir);
	xattr_exists_t	test_default_acl = _ACL_DEFAULT_EXISTS;
	int		have_default_acl = 0;
	int		error = EINVAL;

	if (test_default_acl)
		have_default_acl = test_default_acl(dvp);

#ifdef CONFIG_FS_POSIX_ACL
	/*
	 * Conditionally compiled so that the ACL base kernel changes can be
	 * split out into separate patches - remove this once the S_POSIXACL
	 * flag is accepted, or some other way to implement this exists.
	 */
	if (IS_POSIXACL(dir) && !have_default_acl && has_fs_struct(current))
		mode &= ~current->fs->umask;
#endif

	bzero(&va, sizeof(va));
	va.va_mask = AT_TYPE|AT_MODE;
	va.va_type = IFTOVT(mode);
	va.va_mode = mode;

	switch (mode & S_IFMT) {
	case S_IFCHR: case S_IFBLK: case S_IFIFO: case S_IFSOCK:
		va.va_rdev = rdev;
		va.va_mask |= AT_RDEV;
		/*FALLTHROUGH*/
	case S_IFREG:
		VOP_CREATE(dvp, dentry, &va, &vp, NULL, error);
		break;
	case S_IFDIR:
		VOP_MKDIR(dvp, dentry, &va, &vp, NULL, error);
		break;
	default:
		error = EINVAL;
		break;
	}

	if (!error) {
		ASSERT(vp);
		ip = LINVFS_GET_IP(vp);
		if (!ip) {
			VN_RELE(vp);
			return -ENOMEM;
		}

		if (S_ISCHR(mode) || S_ISBLK(mode))
			ip->i_rdev = to_kdev_t(rdev);
		/* linvfs_revalidate_core returns (-) errors */
		error = -linvfs_revalidate_core(ip, ATTR_COMM);
		validate_fields(dir);
		mark_inode_dirty(ip);
		d_instantiate(dentry, ip);
		mark_inode_dirty_sync(ip);
		mark_inode_dirty_sync(dir);
	}

	if (!error && have_default_acl) {
		_ACL_DECL	(pdacl);

		if (!_ACL_ALLOC(pdacl)) {
			error = -ENOMEM;
		} else {
			if (_ACL_GET_DEFAULT(dvp, pdacl))
				error = _ACL_INHERIT(vp, &va, pdacl);
			VMODIFY(vp);
			_ACL_FREE(pdacl);
		}
	}
	return -error;
}

STATIC int
linvfs_create(
	struct inode	*dir,
	struct dentry	*dentry,
	int		mode)
{
	return linvfs_mknod(dir, dentry, mode, 0);
}

STATIC int
linvfs_mkdir(
	struct inode	*dir,
	struct dentry	*dentry,
	int		mode)
{
	return linvfs_mknod(dir, dentry, mode|S_IFDIR, 0);
}


STATIC struct dentry *
linvfs_lookup(
	struct inode	*dir,
	struct dentry	*dentry)
{
	int		error;
	vnode_t		*vp, *cvp;
	struct inode	*ip = NULL;

	if (dentry->d_name.len >= MAXNAMELEN)
		return ERR_PTR(-ENAMETOOLONG);

	cvp = NULL;
	vp = LINVFS_GET_VP(dir);
	VOP_LOOKUP(vp, dentry, &cvp, 0, NULL, NULL, error);
	if (!error) {
		ASSERT(cvp);
		ip = LINVFS_GET_IP(cvp);
		if (!ip) {
			VN_RELE(cvp);
			return ERR_PTR(-EACCES);
		}
		error = -linvfs_revalidate_core(ip, ATTR_COMM);
	}
	if (error && (error != ENOENT))
		return ERR_PTR(-error);
	return d_splice_alias(ip, dentry);
}

STATIC int
linvfs_link(
	struct dentry	*old_dentry,
	struct inode	*dir,
	struct dentry	*dentry)
{
	int		error;
	vnode_t		*tdvp;	/* Target directory for new name/link */
	vnode_t		*vp;	/* vp of name being linked */
	struct inode	*ip;	/* inode of guy being linked to */

	ip = old_dentry->d_inode;	/* inode being linked to */
	if (S_ISDIR(ip->i_mode))
		return -EPERM;

	tdvp = LINVFS_GET_VP(dir);
	vp = LINVFS_GET_VP(ip);

	error = 0;
	VOP_LINK(tdvp, vp, dentry, NULL, error);
	if (!error) {
		VMODIFY(tdvp);
		VN_HOLD(vp);
		validate_fields(ip);
		d_instantiate(dentry, ip);
		mark_inode_dirty_sync(ip);
	}
	return -error;
}

STATIC int
linvfs_unlink(
	struct inode	*dir,
	struct dentry	*dentry)
{
	int		error = 0;
	struct inode	*inode;
	vnode_t		*dvp;	/* directory containing name to remove */

	inode = dentry->d_inode;

	dvp = LINVFS_GET_VP(dir);

	VOP_REMOVE(dvp, dentry, NULL, error);

	if (!error) {
		validate_fields(dir);	/* For size only */
		validate_fields(inode);
		mark_inode_dirty_sync(inode);
		mark_inode_dirty_sync(dir);
	}

	return -error;
}

STATIC int
linvfs_symlink(
	struct inode	*dir,
	struct dentry	*dentry,
	const char	*symname)
{
	int		error;
	vnode_t		*dvp;	/* directory containing name to remove */
	vnode_t		*cvp;	/* used to lookup symlink to put in dentry */
	vattr_t		va;
	struct inode	*ip = NULL;

	dvp = LINVFS_GET_VP(dir);

	bzero(&va, sizeof(va));
	va.va_type = VLNK;
	va.va_mode = 0777 & ~current->fs->umask;
	va.va_mask = AT_TYPE|AT_MODE; /* AT_PROJID? */

	error = 0;
	VOP_SYMLINK(dvp, dentry, &va, (char *)symname,
							&cvp, NULL, error);
	if (!error) {
		ASSERT(cvp);
		ASSERT(cvp->v_type == VLNK);
		ip = LINVFS_GET_IP(cvp);
		if (!ip) {
			error = ENOMEM;
			VN_RELE(cvp);
		} else {
			/* linvfs_revalidate_core returns (-) errors */
			error = -linvfs_revalidate_core(ip, ATTR_COMM);
			d_instantiate(dentry, ip);
			validate_fields(dir);
			mark_inode_dirty_sync(ip);
			mark_inode_dirty_sync(dir);
		}
	}
	return -error;
}

STATIC int
linvfs_rmdir(
	struct inode	*dir,
	struct dentry	*dentry)
{
	struct inode	*inode = dentry->d_inode;
	vnode_t		*dvp = LINVFS_GET_VP(dir);
	int		error;

	VOP_RMDIR(dvp, dentry, NULL, error);
	if (!error) {
		validate_fields(inode);
		validate_fields(dir);
		mark_inode_dirty_sync(inode);
		mark_inode_dirty_sync(dir);
	}
	return -error;
}

STATIC int
linvfs_rename(
	struct inode	*odir,
	struct dentry	*odentry,
	struct inode	*ndir,
	struct dentry	*ndentry)
{
	int		error;
	vnode_t		*fvp;	/* from directory */
	vnode_t		*tvp;	/* target directory */
	struct inode	*new_inode = NULL;

	fvp = LINVFS_GET_VP(odir);
	tvp = LINVFS_GET_VP(ndir);

	new_inode = ndentry->d_inode;

	VOP_RENAME(fvp, odentry, tvp, ndentry, NULL, error);
	if (error)
		return -error;

	if (new_inode) {
		validate_fields(new_inode);
	}

	validate_fields(odir);
	if (ndir != odir)
		validate_fields(ndir);
	mark_inode_dirty(ndir);
	return 0;
}

STATIC int
linvfs_readlink(
	struct dentry	*dentry,
	char		*buf,
	int		size)
{
	vnode_t		*vp;
	uio_t		uio;
	iovec_t		iov;
	int		error;

	vp = LINVFS_GET_VP(dentry->d_inode);

	iov.iov_base = buf;
	iov.iov_len = size;

	uio.uio_iov = &iov;
	uio.uio_offset = 0;
	uio.uio_segflg = UIO_USERSPACE;
	uio.uio_resid = size;

	VOP_READLINK(vp, &uio, NULL, error);
	if (error)
		return -error;

	return (size - uio.uio_resid);
}

/*
 * careful here - this function can get called recusively up
 * to 32 times, hence we need to be very careful about how much
 * stack we use. uio is kmalloced for this reason...
 *
 * TODO - nathans: this may no longer be true nowadays (limit is
 * 5 currently, see if we can revert the unnecessary kmalloc now)
*/
STATIC int
linvfs_follow_link(
	struct dentry		*dentry,
	struct nameidata	*nd)
{
	vnode_t			*vp;
	uio_t			*uio;
	iovec_t			iov;
	int			error;
	char			*link;

	ASSERT(dentry);
	ASSERT(nd);

	link = (char *)kmalloc(MAXNAMELEN+1, GFP_KERNEL);
	if (!link)
		return -ENOMEM;

	uio = (uio_t *)kmalloc(sizeof(uio_t), GFP_KERNEL);
	if (!uio) {
		kfree(link);
		return -ENOMEM;
	}

	vp = LINVFS_GET_VP(dentry->d_inode);

	iov.iov_base = link;
	iov.iov_len = MAXNAMELEN;

	uio->uio_iov = &iov;
	uio->uio_offset = 0;
	uio->uio_segflg = UIO_SYSSPACE;
	uio->uio_resid = MAXNAMELEN;
	uio->uio_fmode = 0;

	VOP_READLINK(vp, uio, NULL, error);
	if (error) {
		kfree(uio);
		kfree(link);
		return -error;
	}

	link[MAXNAMELEN - uio->uio_resid] = '\0';
	kfree(uio);

	/* vfs_follow_link returns (-) errors */
	error = vfs_follow_link(nd, link);
	kfree(link);
	return error;
}

STATIC int
linvfs_permission(
	struct inode	*inode,
	int		mode)
{
	vnode_t		*vp = LINVFS_GET_VP(inode);
	int		error;

	mode <<= 6;		/* convert from linux to vnode access bits */
	VOP_ACCESS(vp, mode, NULL, error);
	return -error;
}

/* Brute force approach for now - copy data into linux inode
 * from the results of a getattr. This gets called out of things
 * like stat.
 */
int
linvfs_revalidate_core(
	struct inode	*inode,
	int		flags)
{
	vnode_t		*vp = LINVFS_GET_VP(inode);

	/* vn_revalidate returns (-) error so this is ok */
	return vn_revalidate(vp, flags);
}

STATIC int
linvfs_getattr(
	struct vfsmount *mnt,
	struct dentry	*dentry,
	struct kstat	*stat)
{
	struct inode *inode = dentry->d_inode;
	vnode_t		*vp = LINVFS_GET_VP(inode);
	int		error = 0;

	if (unlikely(vp->v_flag & VMODIFIED)) {
		error = linvfs_revalidate_core(inode, 0);
	}
	if (!error)
		generic_fillattr(inode, stat);
	return 0;
}

STATIC int
linvfs_setattr(
	struct dentry	*dentry,
	struct iattr	*attr)
{
	struct inode	*inode = dentry->d_inode;
	vnode_t		*vp = LINVFS_GET_VP(inode);
	vattr_t		vattr;
	unsigned int	ia_valid = attr->ia_valid;
	int		error;
	int		flags = 0;

	memset(&vattr, 0, sizeof(vattr_t));
	if (ia_valid & ATTR_UID) {
		vattr.va_mask |= AT_UID;
		vattr.va_uid = attr->ia_uid;
	}
	if (ia_valid & ATTR_GID) {
		vattr.va_mask |= AT_GID;
		vattr.va_gid = attr->ia_gid;
	}
	if (ia_valid & ATTR_SIZE) {
		vattr.va_mask |= AT_SIZE;
		vattr.va_size = attr->ia_size;
	}
	if (ia_valid & ATTR_ATIME) {
		vattr.va_mask |= AT_ATIME;
		vattr.va_atime.tv_sec = attr->ia_atime;
		vattr.va_atime.tv_nsec = 0;
	}
	if (ia_valid & ATTR_MTIME) {
		vattr.va_mask |= AT_MTIME;
		vattr.va_mtime.tv_sec = attr->ia_mtime;
		vattr.va_mtime.tv_nsec = 0;
	}
	if (ia_valid & ATTR_CTIME) {
		vattr.va_mask |= AT_CTIME;
		vattr.va_ctime.tv_sec = attr->ia_ctime;
		vattr.va_ctime.tv_nsec = 0;
	}
	if (ia_valid & ATTR_MODE) {
		vattr.va_mask |= AT_MODE;
		vattr.va_mode = attr->ia_mode;
		if (!in_group_p(inode->i_gid) && !capable(CAP_FSETID))
			inode->i_mode &= ~S_ISGID;
	}

	if (ia_valid & (ATTR_MTIME_SET | ATTR_ATIME_SET))
		flags = ATTR_UTIME;

	VOP_SETATTR(vp, &vattr, flags, NULL, error);
	if (error)
		return(-error); /* Positive error up from XFS */
	if (ia_valid & ATTR_SIZE) {
		error = vmtruncate(inode, attr->ia_size);
	}

	if (!error) {
		vn_revalidate(vp, 0);
		mark_inode_dirty_sync(inode);
	}
	return error;
}

STATIC int
linvfs_get_block_core(
	struct inode		*inode,
	sector_t		iblock,
	int			blocks,
	struct buffer_head	*bh_result,
	int			create,
	int			direct,
	page_buf_flags_t	flags)
{
	vnode_t			*vp = LINVFS_GET_VP(inode);
	page_buf_bmap_t		pbmap;
	int			retpbbm = 1;
	int			error;
	ssize_t			size;
	loff_t			offset = (loff_t)iblock << inode->i_blkbits;

	if (blocks) {
		size = blocks << inode->i_blkbits;
	} else {
		/* If we are doing writes at the end of the file,
		 * allocate in chunks
		 */
		if (create && (offset >= inode->i_size) && !(flags & PBF_SYNC))
			size = 1 << XFS_WRITE_IO_LOG;
		else
			size = 1 << inode->i_blkbits;
	}

	VOP_BMAP(vp, offset, size,
		create ? flags : PBF_READ, NULL,
		(struct page_buf_bmap_s *)&pbmap, &retpbbm, error);
	if (error)
		return -error;

	if (retpbbm == 0)
		return 0;

	if (pbmap.pbm_bn != PAGE_BUF_DADDR_NULL) {
		page_buf_daddr_t	bn;
		loff_t			delta;

		delta = offset - pbmap.pbm_offset;
		delta >>= inode->i_blkbits;

		bn = pbmap.pbm_bn >> (inode->i_blkbits - 9);
		bn += delta;

		bh_result->b_blocknr = bn;
		bh_result->b_bdev = pbmap.pbm_target->pbr_bdev;
		set_buffer_mapped(bh_result);
	}

	if (create && !buffer_mapped(bh_result))
		set_buffer_new(bh_result);

	if (pbmap.pbm_flags & PBMF_DELAY) {
		if (unlikely(direct))
			BUG();
		if (create) {
			set_buffer_mapped(bh_result);
			set_buffer_uptodate(bh_result);
		}
		bh_result->b_bdev = pbmap.pbm_target->pbr_bdev;
		set_buffer_delay(bh_result);
	}

	if (blocks) {
		size = (pbmap.pbm_bsize - pbmap.pbm_delta); 
		bh_result->b_size = min(size, blocks << inode->i_blkbits);
	}

	return 0;
}

int
linvfs_get_block(
	struct inode		*inode,
	sector_t		iblock,
	struct buffer_head	*bh_result,
	int			create)
{
	return linvfs_get_block_core(inode, iblock, 0, bh_result,
					create, 0, PBF_WRITE);
}

int
linvfs_get_block_sync(
	struct inode		*inode,
	sector_t		iblock,
	struct buffer_head	*bh_result,
	int			create)
{
	return linvfs_get_block_core(inode, iblock, 0, bh_result,
					create, 0, PBF_SYNC|PBF_WRITE);
}



int
linvfs_get_blocks_direct(
	struct inode		*inode,
	sector_t		iblock,
	unsigned long		max_blocks,
	struct buffer_head	*bh_result,
	int			create)
{
	return linvfs_get_block_core(inode, iblock, max_blocks, bh_result,
					create, 1, PBF_WRITE|PBF_DIRECT);
}

static int
linvfs_direct_IO(int rw, struct inode *inode, char *buf,
                        loff_t offset, size_t count)
{
        return generic_direct_IO(rw, inode, buf, offset, count, 
					linvfs_get_blocks_direct);
}

int
linvfs_pb_bmap(
	struct inode	*inode,
	loff_t		offset,
	ssize_t		count,
	page_buf_bmap_t *pbmapp,
	int		flags)
{
	vnode_t		*vp = LINVFS_GET_VP(inode);
	int		error, nmaps = 1;

retry:
	if (flags & PBF_FILE_ALLOCATE) {
		VOP_STRATEGY(vp, offset, count, flags, NULL,
				pbmapp, &nmaps, error);
	} else {
		VOP_BMAP(vp, offset, count, flags, NULL,
				pbmapp, &nmaps, error);
	}
	if (flags & PBF_WRITE) {
		if (unlikely((flags & PBF_DIRECT) && nmaps &&
		    (pbmapp->pbm_flags & PBMF_DELAY))) {
			flags = PBF_WRITE | PBF_FILE_ALLOCATE;
			goto retry;
		}
		VMODIFY(vp);
	}
	return -error;
}

STATIC int
linvfs_bmap(
	struct address_space	*mapping,
	long			block)
{
	struct inode		*inode = (struct inode *)mapping->host;
	vnode_t			*vp = LINVFS_GET_VP(inode);
	xfs_inode_t		*ip = XFS_BHVTOI(vp->v_fbhv);
	int			error;

	/* block	     - Linux disk blocks    512b */
	/* bmap input offset - bytes		      1b */
	/* bmap output bn    - XFS BBs		    512b */
	/* bmap output delta - bytes		      1b */

	vn_trace_entry(vp, "linvfs_bmap", (inst_t *)__return_address);

	VOP_RWLOCK(vp, VRWLOCK_READ);
	if (ip->i_delayed_blks) {
		VOP_FLUSH_PAGES(vp, (xfs_off_t)0, -1, 0, FI_REMAPF, error);
	}

	VOP_RWUNLOCK(vp, VRWLOCK_READ);
	return generic_block_bmap(mapping, block, linvfs_get_block);
}

STATIC int
linvfs_read_full_page(
	struct file	*unused,
	struct page	*page)
{
	return block_read_full_page(page, linvfs_get_block);
}

STATIC int
linvfs_readpages(
	struct address_space	*mapping,
	struct list_head	*pages,
	unsigned		nr_pages)
{
	return mpage_readpages(mapping, pages, nr_pages, linvfs_get_block);
}


STATIC int
count_page_state(
	struct page	*page,
	int		*nr_delalloc,
	int		*nr_unmapped)
{
	*nr_delalloc = *nr_unmapped = 0;

	if (page_has_buffers(page)) {
		struct buffer_head	*bh, *head;

		bh = head = page_buffers(page);
		do {
			if (buffer_uptodate(bh) && !buffer_mapped(bh)) {
				(*nr_unmapped)++;
				continue;
			}
			if (!buffer_delay(bh))
				continue;
			(*nr_delalloc)++;
		} while ((bh = bh->b_this_page) != head);
		return 1;
	}

	return 0;
}

STATIC int
linvfs_write_full_page(
	struct page	*page)
{
	int		error;
	int		need_trans;
	int		nr_delalloc, nr_unmapped;

	if (count_page_state(page, &nr_delalloc, &nr_unmapped)) {
		need_trans = nr_delalloc + nr_unmapped;
	} else {
		need_trans = 1;
	}

	if ((current->flags & (PF_FSTRANS)) && need_trans)
		goto out_fail;

	error = pagebuf_write_full_page(page, nr_delalloc);

	return error;

out_fail:
	set_page_dirty(page);
	unlock_page(page);
	return 0;
}

STATIC int
linvfs_prepare_write(
	struct file	*file,
	struct page	*page,
	unsigned int	from,
	unsigned int	to)
{
	if (file && (file->f_flags & O_SYNC)) {
		return block_prepare_write(page, from, to,
						linvfs_get_block_sync);
	} else {
		return block_prepare_write(page, from, to,
						linvfs_get_block);
	}
}

STATIC void
linvfs_truncate(
	struct inode	*inode)
{
	block_truncate_page(inode->i_mapping, inode->i_size,
						linvfs_get_block);
}

#if 0
/* Keeping this for now as an example of a better way of
 * doing O_DIRECT for XFS - the generic path has more
 * overhead than we want.
 */

/* Initiate I/O on a kiobuf of user memory */
STATIC int
linvfs_direct_IO(
	int		rw,
	struct inode	*inode,
	struct kiobuf	*iobuf,
	unsigned long	blocknr,
	int		blocksize)
{
	struct page	**maplist;
	size_t		page_offset;
	page_buf_t	*pb;
	page_buf_bmap_t map;
	int		error = 0;
	int		pb_flags, map_flags, pg_index = 0;
	size_t		length, total;
	loff_t		offset;
	size_t		map_size, size;

	total = length = iobuf->length;
	offset = blocknr;
	offset <<= inode->i_blkbits;

	maplist = iobuf->maplist;
	page_offset = iobuf->offset;

	map_flags = (rw ? PBF_WRITE : PBF_READ) | PBF_DIRECT;
	pb_flags = (rw ? PBF_WRITE : PBF_READ) | PBF_FORCEIO | _PBF_LOCKABLE;
	while (length) {
		error = linvfs_pb_bmap(inode, offset, length, &map, map_flags);
		if (error)
			break;

		map_size = map.pbm_bsize - map.pbm_delta;
		size = min(map_size, length);
		if (map.pbm_flags & PBMF_HOLE) {
			size_t	zero_len = size;

			if (rw == WRITE)
				break;

			/* Need to zero it all */
			while (zero_len) {
				struct page *page;
				size_t	pg_len;

				pg_len = min((size_t)(PAGE_CACHE_SIZE - page_offset),
								zero_len);

				page = maplist[pg_index];

				memset(kmap(page) + page_offset, 0, pg_len);
				flush_dcache_page(page);
				kunmap(page);

				zero_len -= pg_len;
				if ((pg_len + page_offset) == PAGE_CACHE_SIZE) {
					pg_index++;
					page_offset = 0;
				} else {
					page_offset = (page_offset + pg_len) &
							~PAGE_CACHE_MASK;
				}
			}
		} else {
			int	pg_count;

			pg_count = (size + page_offset + PAGE_CACHE_SIZE - 1)
					>> PAGE_CACHE_SHIFT;
			pb = pagebuf_lookup(map.pbm_target, inode, offset,
							size, pb_flags);
			/* Need to hook up pagebuf to kiobuf pages */
			pb->pb_pages = &maplist[pg_index];
			pb->pb_offset = page_offset;
			pb->pb_page_count = pg_count;

			pb->pb_bn = map.pbm_bn + (map.pbm_delta >> 9);
			pagebuf_iostart(pb, pb_flags);
			pb->pb_flags &= ~_PBF_LOCKABLE;
			pagebuf_rele(pb);

			page_offset = (page_offset + size) & ~PAGE_CACHE_MASK;
			if (page_offset)
				pg_count--;
			pg_index += pg_count;
		}

		offset += size;
		length -= size;
	}

	return error ? error : total - length;
}
#endif

/*
 * This gets a page into cleanable state - page locked on entry
 * kept locked on exit. If the page is marked dirty we should
 * not come this way.
 */
STATIC int
linvfs_release_page(
	struct page	*page,
	int		gfp_mask)
{
	int		need_trans;
	int		nr_delalloc, nr_unmapped;

	if (count_page_state(page, &nr_delalloc, &nr_unmapped)) {
		need_trans = nr_delalloc;
	} else {
		need_trans = 0;
	}

	if (need_trans == 0) {
		return try_to_free_buffers(page);
	}

	if (gfp_mask & __GFP_FS) {
		pagebuf_release_page(page);
		return try_to_free_buffers(page);
	}
	return 0;
}


struct address_space_operations linvfs_aops = {
	.readpage		= linvfs_read_full_page,
	.readpages		= linvfs_readpages,
	.writepage		= linvfs_write_full_page,
	.sync_page		= block_sync_page,
	.releasepage		= linvfs_release_page,
	.prepare_write		= linvfs_prepare_write,
	.commit_write		= generic_commit_write,
	.bmap			= linvfs_bmap,
	.direct_IO		= linvfs_direct_IO,
};

struct inode_operations linvfs_file_inode_operations =
{
	.permission		= linvfs_permission,
	.getattr		= linvfs_getattr,
	.truncate		= linvfs_truncate,
	.setattr		= linvfs_setattr,
	.setxattr		= linvfs_setxattr,
	.getxattr		= linvfs_getxattr,
	.listxattr		= linvfs_listxattr,
	.removexattr		= linvfs_removexattr,
};

struct inode_operations linvfs_dir_inode_operations =
{
	.create			= linvfs_create,
	.lookup			= linvfs_lookup,
	.link			= linvfs_link,
	.unlink			= linvfs_unlink,
	.symlink		= linvfs_symlink,
	.mkdir			= linvfs_mkdir,
	.rmdir			= linvfs_rmdir,
	.mknod			= linvfs_mknod,
	.rename			= linvfs_rename,
	.permission		= linvfs_permission,
	.getattr		= linvfs_getattr,
	.setattr		= linvfs_setattr,
	.setxattr		= linvfs_setxattr,
	.getxattr		= linvfs_getxattr,
	.listxattr		= linvfs_listxattr,
	.removexattr		= linvfs_removexattr,
};

struct inode_operations linvfs_symlink_inode_operations =
{
	.readlink		= linvfs_readlink,
	.follow_link		= linvfs_follow_link,
	.permission		= linvfs_permission,
	.getattr		= linvfs_getattr,
	.setattr		= linvfs_setattr,
	.setxattr		= linvfs_setxattr,
	.getxattr		= linvfs_getxattr,
	.listxattr		= linvfs_listxattr,
	.removexattr		= linvfs_removexattr,
};
