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
#include <linux/mm.h>
#include <linux/locks.h>
#include <linux/xfs_iops.h>


/*
 * Pull the link count and size up from the xfs inode to the linux inode
 */

static void validate_fields(struct inode *ip)
{
	vnode_t	*vp = LINVFS_GET_VP(ip);
	vattr_t va;
	int	error;

	va.va_mask = AT_NLINK|AT_SIZE;
	VOP_GETATTR(vp, &va, ATTR_LAZY, NULL, error);
	ip->i_nlink = va.va_nlink;
	ip->i_size = va.va_size;
	ip->i_blocks = va.va_nblocks;
}


/*
 * Common code used to create/instantiate various things in a directory.
 */

int linvfs_common_cr(struct inode *dir, struct dentry *dentry, int mode,
				enum vtype tp, int rdev)
{
	int		error = 0;
	vnode_t		*dvp, *vp = NULL;
	struct inode	*ip;
	vattr_t		va;
	xfs_acl_t	pdacl; /* parent default ACL */
	int		have_default_acl;

	dvp = LINVFS_GET_VP(dir);
	ASSERT(dvp);

	bzero(&va, sizeof(va));
	va.va_mask = AT_TYPE|AT_MODE;
	va.va_type = tp;
	have_default_acl = _ACL_GET_DEFAULT(dvp, &pdacl);
/* FIXME: This is a temporary fix for the XFS/NFS ACL/umask problem.  Review it
 * when we agree with Andreas G. how ACLs are to be handled.
 * It is wrapped in CONFIG_FS_POSIX_ACL so this code can compile without
 * applying the split-acl-extattr patch.  KAO
 */
#ifdef	CONFIG_FS_POSIX_ACL
	if (IS_POSIX_ACL(dir) && !have_default_acl)
		mode &= ~current->fs->umask;
#endif
	va.va_mode = mode;	
	va.va_size = 0;

	if (tp == VREG) {
		VOP_CREATE(dvp, (char *)dentry->d_name.name, &va, 0, 0, &vp,
							NULL, error);
	} else if (ISVDEV(tp)) {
		/*
		 * Get the real type from the mode
		 */
		va.va_rdev = rdev;
		va.va_mask |= AT_RDEV;

		va.va_type = IFTOVT(mode);
		if (va.va_type == VNON) {
			return -EINVAL;
		}
		VOP_CREATE(dvp, (char *)dentry->d_name.name, &va, 0, 0, &vp,
			NULL, error);
	} else if (tp == VDIR) {
		VOP_MKDIR(dvp, (char *)dentry->d_name.name, &va, &vp,
			NULL, error);
	} else {
		error = EINVAL;
	}

	if (!error) {
		ASSERT(vp);
		ip = LINVFS_GET_IP(vp);
		if (!ip) {
			VN_RELE(vp);
			return -ENOMEM;
		}
		if (ISVDEV(tp))
			ip->i_rdev = to_kdev_t(rdev);
		linvfs_set_inode_ops(ip);
		/* linvfs_revalidate_core returns (-) errors */
		error = -linvfs_revalidate_core(ip, ATTR_COMM);
		validate_fields(dir);
		mark_inode_dirty(ip);
		d_instantiate(dentry, ip);
	}

	if (!error && have_default_acl) {
		error = _ACL_INHERIT(vp, &va, &pdacl);
		VMODIFY(vp);
	}

	return -error;
}

/*
 * Create a new file in dir using mode.
 */
int linvfs_create(struct inode *dir, struct dentry *dentry, int mode)
{
	return(linvfs_common_cr(dir, dentry, mode, VREG, 0));
}

struct dentry * linvfs_lookup(struct inode *dir, struct dentry *dentry)
{
	int		error;
	vnode_t		*vp, *cvp;
	pathname_t	pn;
	pathname_t      *pnp = &pn;
	struct inode	*ip = NULL;

	vp = LINVFS_GET_VP(dir);

	/*
	 * Initialize a pathname_t to pass down.
	 */
	bzero(pnp, sizeof(pathname_t));
	pnp->pn_complen = dentry->d_name.len;
	pnp->pn_hash = dentry->d_name.hash;
	pnp->pn_path = (char *)dentry->d_name.name;

	cvp = NULL;

	VOP_LOOKUP(vp, (char *)dentry->d_name.name, &cvp, pnp, 0, NULL,
						NULL, error);
	if (!error) {
		ASSERT(cvp);
		ip = LINVFS_GET_IP(cvp);
		if (!ip) {
			VN_RELE(cvp);
			return ERR_PTR(-EACCES);
		}
		linvfs_set_inode_ops(ip);
		error = linvfs_revalidate_core(ip, ATTR_COMM);
	}
	d_add(dentry, ip);	/* Negative entry goes in if ip is NULL */
	return NULL;
}


int linvfs_link(struct dentry *old_dentry, struct inode *dir, struct dentry *dentry)
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
	VOP_LINK(tdvp, vp, (char *)dentry->d_name.name, NULL, error);
	if (!error) {
		VMODIFY(tdvp);
		ip->i_ctime = CURRENT_TIME;
		VN_HOLD(vp);
		validate_fields(ip);
		d_instantiate(dentry, ip);
		mark_inode_dirty(ip);
		mark_inode_dirty(LINVFS_GET_IP(tdvp));
	}
	return -error;
}


int linvfs_unlink(struct inode *dir, struct dentry *dentry)
{
	int		error = 0;
	struct inode	*inode;
	vnode_t		*dvp;	/* directory containing name to remove */

	inode = dentry->d_inode;

	dvp = LINVFS_GET_VP(dir);

	VOP_REMOVE(dvp, (char *)dentry->d_name.name, NULL, error);

	if (!error) {
		dir->i_ctime = dir->i_mtime = CURRENT_TIME;
		dir->i_version = ++event;

		inode->i_ctime = dir->i_ctime;
		validate_fields(dir);	/* For size only */
		validate_fields(inode);
		mark_inode_dirty(inode);
		mark_inode_dirty(LINVFS_GET_IP(dvp));
	}

	return -error;
}


int linvfs_symlink(struct inode *dir, struct dentry *dentry, const char *symname)
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
	VOP_SYMLINK(dvp, (char *)dentry->d_name.name, &va, (char *)symname,
							&cvp, NULL, error);
	if (!error) {
		ASSERT(cvp);
		ASSERT(cvp->v_type == VLNK);
		ip = LINVFS_GET_IP(cvp);
		if (!ip) {
			error = ENOMEM;
			VN_RELE(cvp);
		} else {
			linvfs_set_inode_ops(ip);
			/* linvfs_revalidate_core returns (-) errors */
			error = -linvfs_revalidate_core(ip, ATTR_COMM);
			d_instantiate(dentry, ip);
			validate_fields(dir);
			mark_inode_dirty(ip);
			mark_inode_dirty(dir);
		}
	}
	return -error;
}


int linvfs_mkdir(struct inode *dir, struct dentry *dentry, int mode)
{
	return(linvfs_common_cr(dir, dentry, mode, VDIR, 0));
}


int linvfs_rmdir(struct inode *dir, struct dentry *dentry)
{
	int		error;
	vnode_t		*dvp,		/* directory with name to remove */
			*pwd_vp;	/* current working directory, vnode */
	struct inode *inode = dentry->d_inode;

	dvp = LINVFS_GET_VP(dir);

	pwd_vp = NULL;			/* Used for an unnecessary test */

	/*
	 * Someday we could pass the dentry->d_inode into VOP_REMOVE so
	 * that it can skip the lookup.
	 */

	error = 0;
	VOP_RMDIR(dvp, (char *)dentry->d_name.name, pwd_vp, NULL, error);
	if (!error) {
		validate_fields(inode);
		validate_fields(dir);
		inode->i_version = ++event;
		inode->i_ctime = dir->i_ctime = dir->i_mtime = CURRENT_TIME;
		mark_inode_dirty(inode);
		mark_inode_dirty(dir);
	}
	return -error;
}


int linvfs_mknod(struct inode *dir, struct dentry *dentry, int mode, int rdev)
{
	int error = 0;
	enum vtype tp;

	if (S_ISCHR(mode)) {
		tp = VCHR;
	} else if (S_ISBLK(mode)) {
		tp = VBLK;
	} else if (S_ISFIFO(mode)) {
		tp = VFIFO;
	} else if (S_ISSOCK(mode)) {
		tp = VSOCK;
	} else {
		return -EINVAL;
	}

	/* linvfs_common_cr will return (-) errors */
	error = linvfs_common_cr(dir, dentry, mode, tp, rdev);

	return(error);
}


int linvfs_rename(struct inode *odir, struct dentry *odentry,
	       struct inode *ndir, struct dentry *ndentry)
{
	int		error;
	vnode_t		*fvp;	/* from directory */
	vnode_t		*tvp;	/* target directory */
	pathname_t	pn;
	pathname_t      *pnp = &pn;
	struct inode	*new_inode = NULL;

	bzero(pnp, sizeof(pathname_t));
	pnp->pn_complen = ndentry->d_name.len;
	pnp->pn_hash = ndentry->d_name.hash;
	pnp->pn_path = (char *)ndentry->d_name.name;

	fvp = LINVFS_GET_VP(odir);

	tvp = LINVFS_GET_VP(ndir);

	new_inode = ndentry->d_inode;

	VOP_RENAME(fvp, (char *)odentry->d_name.name, tvp,
			   (char *)ndentry->d_name.name, pnp, NULL, error);
	if (error)
		return -error;

	ndir->i_version = ++event;
	odir->i_version = ++event;
	if (new_inode) {
		new_inode->i_ctime = CURRENT_TIME;
		validate_fields(new_inode);
	}

	odir->i_ctime = odir->i_mtime = CURRENT_TIME;

	validate_fields(odir);
	if (ndir != odir)
		validate_fields(ndir);
	mark_inode_dirty(ndir);
	return 0;
}


int linvfs_readlink(struct dentry *dentry, char *buf, int size)
{
	vnode_t	*vp;
	uio_t	uio;
	iovec_t	iov;
	int	error = 0;

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
 */

int linvfs_follow_link(struct dentry *dentry,
				   struct nameidata *nd)
{
	vnode_t	*vp;
	uio_t	*uio;
	iovec_t	iov;
	int	error = 0;
	char	*link;

	ASSERT(dentry);
	ASSERT(nd);

	link = (char *)kmalloc(MAXNAMELEN+1, GFP_KERNEL);
	if (!link)
		return -ENOMEM;

	uio = (uio_t*)kmalloc(sizeof(uio_t), GFP_KERNEL);
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

int linvfs_permission(struct inode *ip, int mode)
{
	vnode_t	*vp;
	int	error;

	mode <<= 6;		/* convert from linux to vnode access bits */
	vp = LINVFS_GET_VP(ip);
	VOP_ACCESS(vp, mode, NULL, error);
	return -error;
}

/* Brute force approach for now - copy data into linux inode
 * from the results of a getattr. This gets called out of things
 * like stat.
 */
int linvfs_revalidate_core(struct inode *inode, int flags)
{
	vnode_t *vp;

	vp = LINVFS_GET_VP(inode);
	ASSERT(vp);
	/* vn_revalidate returns (-) error so this is ok */
	return vn_revalidate(vp, flags);
}

STATIC int linvfs_revalidate(struct dentry *dentry)
{
	vnode_t *vp;

	vp = LINVFS_GET_VP(dentry->d_inode);
	if (vp->v_flag & VMODIFIED) {
		return linvfs_revalidate_core(dentry->d_inode, 0);
	}
	return 0;
}


int
linvfs_setattr(
	struct dentry	*dentry,
	struct iattr	*attr)
{
	vnode_t		*vp = LINVFS_GET_VP(dentry->d_inode);
	vattr_t		vattr;

	unsigned int	ia_valid = attr->ia_valid;
	int		error;
	struct inode	*inode;

	inode = dentry->d_inode;

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

	VOP_SETATTR(vp, &vattr, 0, sys_cred, error);
	if (error)
		return(-error);	/* Positive error up from XFS */

	error = inode_setattr(inode, attr);
	
	return(error);
}

int
linvfs_pb_bmap(struct inode *inode, 
	loff_t offset,
	ssize_t count,
	page_buf_bmap_t *pbmapp,
	int maxpbbm, 
	int *retpbbm, 
	int flags)
{
	vnode_t		*vp;
	int		error;

	vp = LINVFS_GET_VP(inode);

	*retpbbm = maxpbbm;

retry:
	if (flags & PBF_FILE_ALLOCATE) {
		VOP_STRATEGY(vp, offset, count, flags, NULL,
			(struct page_buf_bmap_s *) pbmapp, retpbbm, error);
	} else {
		VOP_BMAP(vp, offset, count, flags, NULL,
			(struct page_buf_bmap_s *) pbmapp, retpbbm, error);
	}
	if (flags & PBF_WRITE) {
		if ((flags & PBF_DIRECT) && *retpbbm &&
		    (pbmapp->pbm_flags & PBMF_DELAY)) {
			flags = PBF_WRITE | PBF_FILE_ALLOCATE;
			goto retry;
		}
		VMODIFY(vp);
	}
	return -error;
}

STATIC
int linvfs_bmap(struct address_space *mapping, long block)
{
	struct inode        *inode          = (struct inode *)mapping->host;
	vnode_t             *vp             = LINVFS_GET_VP(inode);
        pb_bmap_t           bmap            = {0};
        int                 nbm             = 1;
	int                 error;

	/* block             - linux disk blocks    512b */
	/* bmap input offset - bytes                  1b */
	/* bmap outut bn     - xfs BBs              512b */
	/* bmap outut delta  - bytes                  1b */

	vn_trace_entry(vp, "linvfs_bmap", (inst_t *)__return_address);

	VOP_RWLOCK(vp, VRWLOCK_READ);
	if (inode->i_mapping->nrpages) {
		VOP_FLUSH_PAGES(vp, (xfs_off_t)0, -1, 0, FI_REMAPF, error);
		if (error) {
			VOP_RWUNLOCK(vp, VRWLOCK_READ);
			/* VOP_FLUSH_PAGES currently returns nothing but 0... */
			return -error;
		}
	}

	VOP_BMAP(vp, block << 9, 1, PBF_READ, NULL, &bmap, &nbm, error);
	VOP_RWUNLOCK(vp, VRWLOCK_READ);
	if (error)
		return -error;
	if (bmap.pbm_bn < 0)
		return 0;
	return (int)(bmap.pbm_bn + (bmap.pbm_delta >> 9));
}
 
STATIC int linvfs_read_full_page(struct file *filp, struct page *page)
{
	return pagebuf_read_full_page(filp, page, linvfs_pb_bmap);
}

STATIC int linvfs_write_full_page(struct page *page)
{
	int	ret;
	int	flagset = 0;

	if ((current->flags & (PF_FSTRANS|PF_NOIO)) &&
	    (!page->buffers || buffer_delay(page->buffers)))
		goto out_fail;

	if (!page->buffers || buffer_delay(page->buffers)) {
		current->flags |= PF_NOIO;
		flagset = 1;
	}

	ret = pagebuf_write_full_page(page, linvfs_pb_bmap);

	if (flagset)
		current->flags &= ~PF_NOIO;

	return ret;
out_fail:
	SetPageDirty(page);
	UnlockPage(page);
	return 0;
}

STATIC int linvfs_prepare_write(
	struct file *file,
	struct page *page,
	unsigned from,
	unsigned to)
{
	return pagebuf_prepare_write(file, page, from, to, linvfs_pb_bmap);
}

/* This gets a page into cleanable state - page locked on entry
 * kept locked on exit.
 */
STATIC int linvfs_release_page(
	struct page *page,
	int gfp_mask)
{
	if (page->buffers && !buffer_delay(page->buffers)) {
		return 1;
	}

	if (gfp_mask & __GFP_FS) {
		pagebuf_release_page(page, linvfs_pb_bmap);
		return 1;
	}
	return 0;
}


struct address_space_operations linvfs_aops = {
	readpage:		linvfs_read_full_page,
	writepage:		linvfs_write_full_page,
	sync_page:		block_sync_page,
	releasepage:		linvfs_release_page,
	bmap:			linvfs_bmap,
	prepare_write:		linvfs_prepare_write,
	commit_write:		pagebuf_commit_write,
};

struct inode_operations linvfs_file_inode_operations =
{
	permission:		linvfs_permission,
	revalidate:		linvfs_revalidate,
	setattr:		linvfs_setattr,
	setxattr:		linvfs_setxattr,
	getxattr:		linvfs_getxattr,
	listxattr:		linvfs_listxattr,
	removexattr:		linvfs_removexattr,
};

struct inode_operations linvfs_dir_inode_operations =
{
	create:			linvfs_create,
	lookup:			linvfs_lookup,
	link:			linvfs_link,	
	unlink:			linvfs_unlink,	
	symlink:		linvfs_symlink,	
	mkdir:			linvfs_mkdir,	
	rmdir:			linvfs_rmdir,	
	mknod:			linvfs_mknod,	
	rename:			linvfs_rename,	
	permission:		linvfs_permission,
	revalidate:		linvfs_revalidate,
	setattr:		linvfs_setattr,
	setxattr:		linvfs_setxattr,
	getxattr:		linvfs_getxattr,
	listxattr:		linvfs_listxattr,
	removexattr:		linvfs_removexattr,
};

struct inode_operations linvfs_symlink_inode_operations =
{
	readlink:		linvfs_readlink,
	follow_link:		linvfs_follow_link,
	permission:		linvfs_permission,
	revalidate:		linvfs_revalidate,
	setattr:		linvfs_setattr,
	setxattr:		linvfs_setxattr,
	getxattr:		linvfs_getxattr,
	listxattr:		linvfs_listxattr,
	removexattr:		linvfs_removexattr,
};
