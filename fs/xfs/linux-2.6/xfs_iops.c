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

/*
 *  fs/xfs/linux/xfs_iops.c
 *     This file would be called xfs_inode.c, but that is already taken
 *     in the standard XFS code.
 *
 */

#define FSID_T
#include <sys/types.h>
#include <linux/errno.h>

#include <linux/xfs_to_linux.h>

#undef  NODEV
#include <linux/version.h>
#include <linux/fs.h>
#include <linux/sched.h>	/* To get current */
#include <linux/locks.h>
#include <linux/slab.h>

#include <linux/linux_to_xfs.h>

#include <sys/sysmacros.h>
#include <sys/capability.h>
#include <sys/vfs.h>
#include <sys/pvfs.h>
#include <sys/vnode.h>
#include <ksys/behavior.h>
#include <sys/mode.h>
#include <linux/xfs_linux.h>
#include <linux/xfs_cred.h>
#include <linux/xfs_file.h>
#include <linux/page_buf.h>
#include <xfs_buf.h>

#include <asm/uaccess.h> /* For copy_from_user */

/*
 * Common code used to create/instantiate various things in a directory.
 */

int linvfs_common_cr(struct inode *dir, struct dentry *dentry, int mode,
				enum vtype tp, int rdev)
{
	int		error = 0;
	vnode_t		*dvp, *vp;
	struct inode	*ip;
	vattr_t		va;
	ino_t		ino;
	cred_t		cred;		/* Temporary cred workaround */

	dvp = LINVFS_GET_VP(dir);
	ASSERT(dvp);

	vp = NULL;


	bzero(&va, sizeof(va));
	va.va_mask = AT_TYPE|AT_MODE;
	va.va_type = tp;
	va.va_mode = mode;	/* Do we need to pass this through? JIMJIM
				va.va_mode = 0777 & ~current->fs->umask; */
	va.va_size = 0;

	if (tp == VREG) {
		VOP_CREATE(dvp, (char *)dentry->d_name.name, &va, 0, 0, &vp,
							&cred, error);
	} else if ((tp == VBLK) || (tp == VCHR)) {
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
			&cred, error);
	} else if (tp == VDIR) {
		VOP_MKDIR(dvp, (char *)dentry->d_name.name, &va, &vp,
			&cred, error);
	}

	if (!error) {
		ASSERT(vp);
		bzero(&va, sizeof(va));
		va.va_mask = AT_NODEID;
		VOP_GETATTR(vp, &va, 0, &cred, error);
		if (error) {
			VN_RELE(vp);
			return -error;
		}
		ino = va.va_nodeid;
		ASSERT(ino);
		ip = iget(dir->i_sb, ino);
		if (!ip) {
			VN_RELE(vp);
			return -ENOMEM;
		}
		d_instantiate(dentry, ip);
	}
	return 0;
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
	vattr_t		va;
	ino_t		ino;
	cred_t		cred;		/* Temporary cred workaround */

	vp = LINVFS_GET_VP(dir);
	ASSERT(vp);

	/*
	 * Initialize a pathname_t to pass down.
	 */
	bzero(pnp, sizeof(pathname_t));
	pnp->pn_complen = dentry->d_name.len;
	pnp->pn_hash = dentry->d_name.hash;
	pnp->pn_path = (char *)dentry->d_name.name;

	cvp = NULL;

	VOP_LOOKUP(vp, (char *)dentry->d_name.name, &cvp, pnp, 0, NULL,
						&cred, error);
	if (!error) {
		ASSERT(cvp);
		bzero(&va, sizeof(va));
		va.va_mask = AT_NODEID;
		VOP_GETATTR(cvp, &va, 0, &cred, error);
		if (error) {
			VN_RELE(cvp);
			return ERR_PTR(-error);
		}
		ino = va.va_nodeid;
		ASSERT(ino);
		ip = iget(dir->i_sb, ino);
		if (!ip) {
			VN_RELE(cvp);
			return ERR_PTR(-EACCES);
		}
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
	cred_t		cred;	/* Temporary cred workaround */

	tdvp = LINVFS_GET_VP(dir);
	ASSERT(tdvp);

	ip = old_dentry->d_inode;	/* inode being linked to */
	vp = LINVFS_GET_VP(ip);

	error = 0;
	VOP_LINK(tdvp, vp, (char *)dentry->d_name.name, &cred, error);
	if (!error) {
		ip->i_nlink++;
		ip->i_ctime = CURRENT_TIME;
		mark_inode_dirty(ip);
		ip->i_count++;
		VN_HOLD(vp);
		d_instantiate(dentry, ip);
	}
	return -error;
}


int linvfs_unlink(struct inode *dir, struct dentry *dentry)
{
	int		error;
	struct inode	*inode;
	vnode_t		*dvp;	/* directory containing name to remove */
	cred_t		cred;	/* Temporary cred workaround */

	dvp = LINVFS_GET_VP(dir);
	inode = dentry->d_inode;
	ASSERT(dvp);

	/*
	 * Someday we could pass the dentry->d_inode into VOP_REMOVE so
	 * that it can skip the lookup.
	 */

	error = 0;
	VOP_REMOVE(dvp, (char *)dentry->d_name.name, &cred, error);
	if (!error) {
		dir->i_ctime = dir->i_mtime = CURRENT_TIME;
		dir->i_version = ++event;
		inode->i_nlink--;
		inode->i_ctime = dir->i_ctime;
		d_delete(dentry);       /* del and free inode */
	}
	return -error;
}


int linvfs_symlink(struct inode *dir, struct dentry *dentry, const char *symname)
{
	int		error;
	vnode_t		*dvp;	/* directory containing name to remove */
	vnode_t		*cvp;	/* used to lookup symlink to put in dentry */
	vattr_t		va;
	pathname_t	pn;
	pathname_t      *pnp = &pn;
	struct inode	*ip = NULL;
	ino_t		ino;
	cred_t		cred;		/* Temporary cred workaround */

	dvp = LINVFS_GET_VP(dir);
	ASSERT(dvp);

	bzero(&va, sizeof(va));
	va.va_type = VLNK;
	va.va_mode = 0777 & ~current->fs->umask;
	va.va_mask = AT_TYPE|AT_MODE; /* AT_PROJID? */

	error = 0;
	VOP_SYMLINK(dvp, (char *)dentry->d_name.name, &va, (char *)symname,
							&cred, error);
	if (!error) {
		/* Now need to lookup the name since we didn't get the vp back */
		/* Maybe modify VOP_SYMLINK to return *vpp? */
		/* But, lookup should hit DNLC, anyway */

		/*
		 * Initialize a pathname_t to pass down.
		 */
		bzero(pnp, sizeof(pathname_t));
		pnp->pn_complen = dentry->d_name.len;
		pnp->pn_hash = dentry->d_name.hash;
		pnp->pn_path = (char *)dentry->d_name.name;

		cvp = NULL;
		VOP_LOOKUP(dvp, (char *)dentry->d_name.name, &cvp, pnp, 0, NULL,
					&cred, error);
		if (!error) {
			ASSERT(cvp);
			bzero(&va, sizeof(va));
			va.va_mask = AT_NODEID;
			VOP_GETATTR(cvp, &va, 0, &cred, error);
			if (error) {
				VN_RELE(cvp);
				return error;
			}
			ino = va.va_nodeid;
			ASSERT(ino && (cvp->v_type == VLNK));
			ip = iget(dir->i_sb, ino);
			if (!ip) {
				error = -ENOMEM;
				VN_RELE(cvp);
			} else {
				d_instantiate(dentry, ip);
			}
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
	vnode_t		*dvp,	/* directory containing name to remove */
			*pwd_vp; /* current working directory, vnode */
	cred_t		cred;		/* Temporary cred workaround */

	dvp = LINVFS_GET_VP(dir);
	ASSERT(dvp);
	ASSERT(current->fs->pwd->d_inode);

	pwd_vp = LINVFS_GET_VP(current->fs->pwd->d_inode);

	/*
	 * Someday we could pass the dentry->d_inode into VOP_REMOVE so
	 * that it can skip the lookup.
	 */

	error = 0;
	VOP_RMDIR(dvp, (char *)dentry->d_name.name, pwd_vp, &cred, error);
	if (!error) {
		struct inode *inode = dentry->d_inode;
		inode->i_nlink = 0;
		inode->i_size = 0;
		inode->i_version = ++event;
		mark_inode_dirty(inode);
		dir->i_nlink--;
		dir->i_ctime = dir->i_ctime = dir->i_mtime = CURRENT_TIME;
		mark_inode_dirty(dir);
		d_delete(dentry);       /* del and free inode */
	}
	return -error;
}


int linvfs_mknod(struct inode *dir, struct dentry *dentry, int mode, int rdev)
{
	return(linvfs_common_cr(dir, dentry, mode, VBLK, rdev));
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
	cred_t		cred;		/* Temporary cred workaround */

	bzero(pnp, sizeof(pathname_t));
	pnp->pn_complen = ndentry->d_name.len;
	pnp->pn_hash = ndentry->d_name.hash;
	pnp->pn_path = (char *)ndentry->d_name.name;

	fvp = LINVFS_GET_VP(odir);
	tvp = LINVFS_GET_VP(ndir);

	new_inode = ndentry->d_inode;

	VOP_RENAME(fvp, (char *)odentry->d_name.name, tvp,
			   (char *)ndentry->d_name.name, pnp, &cred, error);
	if (error)
		return -error;

	ndir->i_version = ++event;
	odir->i_version = ++event;
	if (new_inode) {
		new_inode->i_nlink--;
		new_inode->i_ctime = CURRENT_TIME;
		mark_inode_dirty(new_inode);
	}

	odir->i_ctime = odir->i_mtime = CURRENT_TIME;
	mark_inode_dirty(odir);
	return 0;
}


int linvfs_readlink(struct dentry *dentry, char *buf, int size)
{
	vnode_t	*vp;
	uio_t	uio;
	iovec_t	iov;
	int	error = 0;
	cred_t	cred;		/* Temporary cred workaround */

	vp = LINVFS_GET_VP(dentry->d_inode);
	iov.iov_base = buf;
	iov.iov_len = size;

	uio.uio_iov = &iov;
	uio.uio_offset = 0;
	uio.uio_segflg = UIO_USERSPACE;
	uio.uio_resid = size;

	UPDATE_ATIME(dentry->d_inode);
	VOP_READLINK(vp, &uio, &cred, error);
	if (error)
		return -error;

	return (size - uio.uio_resid);
}


struct dentry * linvfs_follow_link(struct dentry *dentry,
				   struct dentry *base,
				   unsigned int follow)
{
	vnode_t	*vp;
	uio_t	uio;
	iovec_t	iov;
	int	error = 0;
	char	*link = kmalloc(MAXNAMELEN+1, GFP_KERNEL); 
	cred_t	cred;		/* Temporary cred workaround */

	vp = LINVFS_GET_VP(dentry->d_inode);
	iov.iov_base = link;
	iov.iov_len = MAXNAMELEN;

	uio.uio_iov = &iov;
	uio.uio_offset = 0;
	uio.uio_segflg = UIO_SYSSPACE;
	uio.uio_resid = MAXNAMELEN;

	VOP_READLINK(vp, &uio, &cred, error);
	if (error) {
		kfree_s(link, MAXNAMELEN);
		return NULL;
	}

	link[MAXNAMELEN - uio.uio_resid] = '\0';

	UPDATE_ATIME(dentry->d_inode);
	base = lookup_dentry(link, base, follow);
	kfree_s(link, MAXNAMELEN);
	return base;
}


int linvfs_get_block(struct inode *inode, long block, struct buffer_head *bh_result, int create)
{
	vnode_t		*vp;
	int		block_shift = inode->i_sb->s_blocksize_bits;
	loff_t		offset = block << block_shift;
	ssize_t		count = inode->i_sb->s_blocksize;
	pb_bmap_t	pbmap;
	int		npbmaps = 1;
	int		error;
	long		blockno;

	if (block < 0) {
		printk(__FUNCTION__": called with block of -1\n");
		return -EIO;
	}

	vp = LINVFS_GET_VP(inode);

	VOP_RWLOCK(vp, VRWLOCK_READ);
	VOP_BMAP(vp, offset, count, XFS_B_READ,(struct page_buf_bmap_s *) &pbmap, &npbmaps, error);
	VOP_RWUNLOCK(vp, VRWLOCK_READ);
	if (error)
		return -EIO;
	/*
	 * JIMJIM This interface needs to be fixed to support 64 bit
	 * block numbers.
	 */
	
	blockno = (long)pbmap.pbm_bn;
	if (blockno < 0) return 0;
	if (pbmap.pbm_offset) {
		if ( pbmap.pbm_offset % count) {
			printk("linvfs_bmap: this shouldn't happen %Ld %d!\n",
				pbmap.pbm_offset,
				count);
		}
		blockno += pbmap.pbm_offset >> block_shift;
	}

	if (!create) {
		bh_result->b_blocknr = blockno >> (block_shift - 9);
		bh_result->b_state |= (1UL << BH_Mapped);

		return 0;
	}

	return -EIO;
}

int linvfs_permission(struct inode *ip, int mode)
{
	cred_t	cred;		/* Temporary cred workaround */
        vnode_t *vp;
	int	error;

	mode <<= 6;
        vp = LINVFS_GET_VP(ip);
	VOP_ACCESS(vp, mode, &cred, error);

	return error;
}

/* Brute force approach for now - copy data into linux inode
 * from the results of a getattr. This gets called out of things
 * like stat.
 */
int linvfs_revalidate(struct dentry *dentry)
{
        struct inode *inode = dentry->d_inode;
        vnode_t *vp;
        vattr_t va;
        int     error;
	cred_t	cred;		/* Temporary cred workaround */

        vp = LINVFS_GET_VP(inode);
        va.va_mask = AT_STAT;
        VOP_GETATTR(vp, &va, 0, &cred, error);

        inode->i_mode = VTTOIF(va.va_type) | va.va_mode;
        inode->i_nlink = va.va_nlink;
        inode->i_uid = va.va_uid;
        inode->i_gid = va.va_gid;
        inode->i_rdev = va.va_rdev;
        inode->i_size = va.va_size;
        inode->i_blocks = va.va_nblocks >> (PAGE_SHIFT - 9);
        inode->i_blksize = PAGE_SIZE;
        inode->i_atime = va.va_atime.tv_sec;
        inode->i_mtime = va.va_mtime.tv_sec;
        inode->i_ctime = va.va_ctime.tv_sec;

        return 0;
}

int
linvfs_notify_change(
	struct dentry	*dentry,
	struct iattr	*attr)
{
	vnode_t		*vp = LINVFS_GET_VP(dentry->d_inode);
	vattr_t		vattr;

	unsigned int	ia_valid = attr->ia_valid;
	int		error;
	struct inode	*inode;

	inode = dentry->d_inode;
	error = inode_change_ok(inode, attr);
	if (error){
	  return(error);
	}

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

	if (!error) {
		inode_setattr(inode, attr);
	}

	return(-error);
}

int
linvfs_pb_bmap(struct inode *inode, 
			   loff_t offset,
			   ssize_t count,
			   struct page_buf_bmap_s *pbmapp,
			   int maxpbbm, 
			   int *retpbbm, 
			   int flags/* page_buf_flags_t */)
{
	vnode_t		*vp;
	int		block_shift = inode->i_sb->s_blocksize_bits;
	pb_bmap_t	pbmap;
	int		npbmaps = 1;
	int		error, vop_flags;
	long		blockno;

	/*
	 * First map the flags into vop_flags.
	 */

	switch(flags) {
	case PBF_READ:
		vop_flags = XFS_B_READ;
		break;

	case PBF_WRITE:
		vop_flags = XFS_B_WRITE;
		break;
	
	default:
		printk("linvfs_pb_bmap: flag %x not implemented\n", flags);
		return -EINVAL;
	}

	vp = LINVFS_GET_VP(inode);

	*retpbbm = maxpbbm;

	VOP_BMAP(vp, offset, count, vop_flags,
			(struct page_buf_bmap_s *) pbmapp, retpbbm, error);

	return error;
}

struct address_space_operations linvfs_aops = {
  readpage:		pagebuf_read_full_page,
  writepage:		pagebuf_write_full_page,

	/* prepare_write: ext2_prepare_write,   */
	/* commit_write: generic_commit_write,  */
	/* bmap: ext2_bmap			*/

};

struct inode_operations linvfs_file_inode_operations =
{
  permission:		linvfs_permission,
  revalidate:		linvfs_revalidate,
  setattr:		linvfs_notify_change,
  pagebuf_bmap:		linvfs_pb_bmap,
  pagebuf_fileread:	pagebuf_file_read,
};

struct inode_operations linvfs_dir_inode_operations =
{
  create:		linvfs_create,
  lookup:		linvfs_lookup,
  link:			linvfs_link,	
  unlink:		linvfs_unlink,	
  symlink:		linvfs_symlink,	
  mkdir:		linvfs_mkdir,	
  rmdir:		linvfs_rmdir,	
  mknod:		linvfs_mknod,	
  rename:		linvfs_rename,	
  permission:		linvfs_permission,
  revalidate:		linvfs_revalidate,
  setattr:		linvfs_notify_change
};

struct inode_operations linvfs_symlink_inode_operations =
{
  readlink:		linvfs_readlink,
  follow_link:		linvfs_follow_link,
  permission:		linvfs_permission,
  revalidate:		linvfs_revalidate
};
