
/*
 *  fs/xfs/linux/xfs_iops.c
 *     This file would be called xfs_inode.c, but that is already taken
 *     in the standard XFS code.
 *
 */

#define FSID_T
#include <sys/types.h>
#include <linux/errno.h>
#include "xfs_coda_oops.h"

#include <linux/xfs_to_linux.h>

#undef  NODEV
#include <linux/fs.h>
#include <linux/sched.h>	/* To get current */
#include <linux/locks.h>
#include <linux/slab.h>

#include <linux/linux_to_xfs.h>

#include <sys/sysmacros.h>
#include <sys/capability.h>
#include <sys/cred.h>
#include <sys/buf.h>
#include <sys/vfs.h>
#include <sys/pvfs.h>
#include <sys/vnode.h>
#include <ksys/behavior.h>
#include <sys/mode.h>
#include <xfs_linux.h>

#include <linux/xfs_file.h>


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
				sys_cred, error);
	} else if ((tp == VBLK) || (tp == VCHR)) {
		/*
		 * Get the real type from the mode
		 */
		va.va_rdev = makedev(MAJOR(rdev), MINOR(rdev));
		va.va_mask |= AT_RDEV;

		va.va_type = IFTOVT(mode);
		if (va.va_type == VNON) {
			return -EINVAL;
		}
		VOP_CREATE(dvp, (char *)dentry->d_name.name, &va, 0, 0, &vp,
			sys_cred, error);
	} else if (tp == VDIR) {
		VOP_MKDIR(dvp, (char *)dentry->d_name.name, &va, &vp,
			sys_cred, error);
	}

	if (!error) {
		ASSERT(vp);
		bzero(&va, sizeof(va));
		va.va_mask = AT_NODEID;
		VOP_GETATTR(vp, &va, 0, sys_cred, error);
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
			sys_cred, error);
	if (!error) {
		ASSERT(cvp);
		bzero(&va, sizeof(va));
		va.va_mask = AT_NODEID;
		VOP_GETATTR(cvp, &va, 0, sys_cred, error);
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

	tdvp = LINVFS_GET_VP(dir);
	ASSERT(tdvp);

	ip = old_dentry->d_inode;	/* inode being linked to */
	vp = LINVFS_GET_VP(ip);

	error = 0;
	VOP_LINK(tdvp, vp, (char *)dentry->d_name.name, sys_cred, error);
	if (!error) {
		d_instantiate(dentry, ip);
	}
	return -error;
}


int linvfs_unlink(struct inode *dir, struct dentry *dentry)
{
	int		error;
	vnode_t		*dvp;	/* directory containing name to remove */

	dvp = LINVFS_GET_VP(dir);
	ASSERT(dvp);

	/*
	 * Someday we could pass the dentry->d_inode into VOP_REMOVE so
	 * that it can skip the lookup.
	 */

	error = 0;
	VOP_REMOVE(dvp, (char *)dentry->d_name.name, sys_cred, error);
	if (!error) {
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

	dvp = LINVFS_GET_VP(dir);
	ASSERT(dvp);

	bzero(&va, sizeof(va));
	va.va_type = VLNK;
	va.va_mode = 0777 & ~current->fs->umask;
	va.va_mask = AT_TYPE|AT_MODE; /* AT_PROJID? */
	error = 0;
	VOP_SYMLINK(dvp, (char *)dentry->d_name.name, &va, (char *)symname,
		sys_cred, error);
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
					sys_cred, error);
		if (!error) {
			ASSERT(cvp);
			bzero(&va, sizeof(va));
			va.va_mask = AT_NODEID;
			VOP_GETATTR(cvp, &va, 0, sys_cred, error);
			if (error) {
				VN_RELE(cvp);
				return error;
			}
			ino = va.va_nodeid;
			ASSERT(ino && (cvp->v_type == VLNK));
			ip = iget(dir->i_sb, ino);
			if (!ip) {
				error = -ENOMEM;
			} else {
				d_instantiate(dentry, ip);
			}
			VN_RELE(cvp);
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

	dvp = LINVFS_GET_VP(dir);
	ASSERT(dvp);
	ASSERT(current->fs->pwd->d_inode);

	pwd_vp = LINVFS_GET_VP(current->fs->pwd->d_inode);

	/*
	 * Someday we could pass the dentry->d_inode into VOP_REMOVE so
	 * that it can skip the lookup.
	 */

	error = 0;
	VOP_RMDIR(dvp, (char *)dentry->d_name.name, pwd_vp, sys_cred, error);
	if (!error) {
		ASSERT(dir->i_nlink == 2);
		dir->i_nlink = 0;
		dir->i_size = 0;
		dir->i_version = ++event;
		mark_inode_dirty(dir);
		dir->i_nlink--;
		dir->i_version = ++event;
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
  return(-ENOSYS);
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

	VOP_READLINK(vp, &uio, sys_cred, error);
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
	char	*link = kmalloc(MAXNAMELEN, GFP_KERNEL); 

	vp = LINVFS_GET_VP(dentry->d_inode);
	iov.iov_base = link;
	iov.iov_len = MAXNAMELEN;

	uio.uio_iov = &iov;
	uio.uio_offset = 0;
	uio.uio_segflg = UIO_SYSSPACE;
	uio.uio_resid = MAXNAMELEN;

	VOP_READLINK(vp, &uio, sys_cred, error);
	if (error) {
		kfree_s(link, MAXNAMELEN);
		return NULL;
	}

	base = lookup_dentry(link, base, follow);
	kfree_s(link, MAXNAMELEN);
	return base;
}

int linvfs_readpage(struct file *filp, struct page *page)
{
	int rval;
	rval = generic_readpage(filp, page);
	return(rval);
}

int linvfs_writepage(struct file *filp, struct page *page)
{
	int rval;
	rval = -ENOSYS;
	printk("linvfs_writepage: NOT IMPLEMENTED\n", rval);
	return(rval);
}

int linvfs_bmap(struct inode *inode, int block)
{
	vnode_t		*vp;
	int		block_shift = inode->i_sb->s_blocksize_bits;
	off_t		offset = block << block_shift;
	ssize_t		count = inode->i_sb->s_blocksize;
	struct	bmapval	bmap;
	int		nbmaps = 1;
	int		error;
	int		blockno;

	if (block < 0) {
		printk("linvfs_bmap: called with block of -1\n");
		return 0;
	}

	vp = LINVFS_GET_VP(inode);

	VOP_RWLOCK(vp, VRWLOCK_READ);
	VOP_BMAP(vp, offset, count, B_READ, sys_cred, &bmap, &nbmaps, error);
	VOP_RWUNLOCK(vp, VRWLOCK_READ);

	if (error)
		return 0;
	/*
	 * JIMJIM This interface needs to be fixed to support 64 bit
	 * block numbers.
	 */

	blockno = (int)bmap.bn;
	if (blockno < 0) return 0;

	if(bmap.pboff) {
		if (bmap.pboff % count) {
			printk("this shouldn't happen %d %d %d!\n",
				bmap.pboff % count, bmap.pboff, count);
		}
		printk("blockno %d moving to %d\n",
				blockno, blockno+bmap.pboff/count);
		blockno += bmap.pboff/count;
	}

	return(blockno);
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

        vp = LINVFS_GET_VP(inode);
        va.va_mask = AT_STAT;
        VOP_GETATTR(vp, &va, 0, sys_cred, error);

        inode->i_mode = VTTOIF(va.va_type) | va.va_mode;
        inode->i_nlink = va.va_nlink;
        inode->i_uid = va.va_uid;
        inode->i_gid = va.va_gid;
        inode->i_rdev = MKDEV(emajor(va.va_rdev), eminor(va.va_rdev));
        inode->i_size = va.va_size;
        inode->i_blocks = va.va_nblocks;
        inode->i_blksize = va.va_blksize;
        inode->i_atime = va.va_atime.tv_sec;
        inode->i_mtime = va.va_mtime.tv_sec;
        inode->i_ctime = va.va_ctime.tv_sec;

        return 0;
}


struct inode_operations linvfs_file_inode_operations =
{
  &linvfs_file_operations,
  NULL,	 /*  create  */
  NULL,	 /*  lookup  */
  NULL,	 /*  link  */
  NULL,	 /*  unlink  */
  NULL,	 /*  symlink  */
  NULL,	 /*  mkdir  */
  NULL,	 /*  rmdir  */
  NULL,	 /*  mknod  */
  NULL,	 /*  rename  */
  NULL,	 /*  readlink  */
  NULL,	 /*  follow_link  */
  linvfs_readpage,
  linvfs_writepage,
  linvfs_bmap,
  NULL,	 /*  truncate  */
  NULL,  /*  permission  */
  NULL,	 /*  smap  */
  NULL,  /*  updatepage  */
  linvfs_revalidate
};

struct inode_operations linvfs_dir_inode_operations =
{
  &linvfs_dir_operations,
  linvfs_create,
  linvfs_lookup,
  linvfs_link,	
  linvfs_unlink,	
  linvfs_symlink,	
  linvfs_mkdir,	
  linvfs_rmdir,	
  linvfs_mknod,	
  linvfs_rename,	
  NULL,	 /*  readlink  */
  NULL,	 /*  follow_link  */
  NULL,	 /*  readpage  */
  NULL,	 /*  writepage  */
  NULL,	 /*  bmap  */
  NULL,	 /*  truncate  */
  NULL,  /*  permission  */
  NULL,  /*  smap  */
  NULL,  /*  updatepage  */
  linvfs_revalidate
};

struct inode_operations linvfs_symlink_inode_operations =
{
  NULL,  /*  struct file_operations  */
  NULL,	 /*  create  */
  NULL,	 /*  lookup  */
  NULL,	 /*  link  */
  NULL,	 /*  unlink  */
  NULL,	 /*  symlink  */
  NULL,	 /*  mkdir  */
  NULL,	 /*  rmdir  */
  NULL,	 /*  mknod  */
  NULL,	 /*  rename  */
  linvfs_readlink,
  linvfs_follow_link,
  NULL,	 /*  readpage  */
  NULL,	 /*  writepage  */
  NULL,	 /*  bmap  */
  NULL,	 /*  truncate  */
  NULL,	 /*  permission  */
  NULL,	 /*  smap  */
  NULL,  /*  updatepage  */
  linvfs_revalidate
};
