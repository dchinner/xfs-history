
/*
 * Copyright (C) 1999 Silicon Graphics, Inc.  All Rights Reserved.
 * 
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as published
 * by the Free Software Fondation.
 * 
 * This program is distributed in the hope that it would be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  Further, any license provided herein,
 * whether implied or otherwise, is limited to this program in accordance with
 * the express provisions of the GNU General Public License.  Patent licenses,
 * if any, provided herein do not apply to combinations of this program with
 * other product or programs, or any other product whatsoever.  This program is
 * distributed without any warranty that the program is delivered free of the
 * rightful claim of any third person by way of infringement or the like.  See
 * the GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write the Free Software Foundation, Inc., 59 Temple
 * Place - Suite 330, Boston MA 02111-1307, USA.
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
#include "xfs_coda_oops.h"

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
#include <sys/cred.h>
#include <sys/buf.h>
#include <sys/vfs.h>
#include <sys/pvfs.h>
#include <sys/vnode.h>
#include <ksys/behavior.h>
#include <sys/mode.h>
#include <xfs_linux.h>

#include <linux/xfs_file.h>

#include <linux/page_buf.h>

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
	int		error;
	vnode_t		*fvp;	/* from directory */
	vnode_t		*tvp;	/* target directory */
	pathname_t	pn;
	pathname_t      *pnp = &pn;
	struct inode	*ip = NULL;

	bzero(pnp, sizeof(pathname_t));
	pnp->pn_complen = ndentry->d_name.len;
	pnp->pn_hash = ndentry->d_name.hash;
	pnp->pn_path = (char *)ndentry->d_name.name;

	fvp = LINVFS_GET_VP(odir);
	tvp = LINVFS_GET_VP(ndir);

	VOP_RENAME(fvp, (char *)odentry->d_name.name, tvp,
		   (char *)ndentry->d_name.name, pnp, sys_cred, error);
	if (error)
		return -error;

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

/*
 * The following is used to get a list of blocks of the given
 * size from a pagebuf_bmap_t returned by VOP_BMAP.
 */

static void
_linvfs_set_blocks(
	int		*blocks,	/* Array of blocknos to fill in */
	int		nblocks,	/* number of elms in blocks array */
	pb_bmap_t	*pbmap,		/* mappings of blocks (big for XFS) */
	int		npbmaps,	/* number of pbmap entries */
	int		block_bits,	/* bits (max size) of blocks e.g. 9 for 512 */
	ssize_t		blocksize)	/* size of block */
{
	int i, *p, bmaps, blockno;

	i = nblocks;
        p = blocks;

	/* Zero out all the blocks, first */
	while(i) {
		*p++ = 0;
		i--;
	}

        i = nblocks;
	p = blocks;

	/*
	 * We need to walk the bmaps to cover the case where this page
	 * spans extents.
	 */

	for(bmaps = 0; bmaps < npbmaps && i; bmaps++, pbmap++) {

		/* While we have room in this bmap, bump the block number */
		while(pbmap->pbm_bsize && i) {
			blockno = (long)pbmap->pbm_bn;
			if (blockno < 0) {
				printk("_linvfs_set_blocks have negative block %d\n",
						blockno);
				return;
			} else if (pbmap->pbm_offset) {
				if ( pbmap->pbm_offset % blocksize) {
					printk("_linvfs_set_blocks: this shouldn't happen %lld %d!\n",
					   pbmap->pbm_offset, blocksize);
				}
				blockno += pbmap->pbm_offset >> block_bits;
				pbmap->pbm_offset = 0;
				pbmap->pbm_bn = blockno;
			}

			*p = blockno;
			p++;
			i--;
			pbmap->pbm_bn++;
			pbmap->pbm_bsize -= blocksize;
		}
        }
	return;
}


int linvfs_readpage(struct file *filp, struct page *page)
{
	int rval;
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,3,1)
	struct dentry	*dentry = filp->f_dentry;
	struct inode	*inode = dentry->d_inode;
	vnode_t		*vp;
	int		nr[PAGE_SIZE/512];
#define LINBMAP_MAX 4
	pb_bmap_t	pbmap[LINBMAP_MAX];
	int		npbmaps = LINBMAP_MAX;
	int		error;

	atomic_inc(&page->count);
	set_bit(PG_locked, &page->flags);
	set_bit(PG_free_after, &page->flags);
	
	vp = LINVFS_GET_VP(inode);

	VOP_RWLOCK(vp, VRWLOCK_READ);
	VOP_BMAP(vp, page->offset, PAGE_SIZE, B_READ,(struct page_buf_bmap_s *) &pbmap, &npbmaps, error);
	VOP_RWUNLOCK(vp, VRWLOCK_READ);

	if (error)
		return -EIO;

	_linvfs_set_blocks(nr, PAGE_SIZE/512, &pbmap[0], npbmaps,
		inode->i_sb->s_blocksize_bits, inode->i_sb->s_blocksize);

	/* IO start */
	rval = brw_page(READ, page, inode->i_dev, nr, inode->i_sb->s_blocksize, 1);
#else
	rval = block_read_full_page(filp, page);
#endif
	return(rval);
}

int linvfs_writepage(struct file *filp, struct page *page)
{
	int rval;
	rval = -ENOSYS;
	printk("linvfs_writepage: NOT IMPLEMENTED\n");
	return(rval);
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,3,1)
int linvfs_bmap(struct inode *inode, int block)
#else
int linvfs_get_block(struct inode *inode, long block, struct buffer_head *bh_result, int create)
#endif
{
	vnode_t		*vp;
	int		block_shift = inode->i_sb->s_blocksize_bits;
	off_t		offset = block << block_shift;
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
	VOP_BMAP(vp, offset, count, B_READ,(struct page_buf_bmap_s *) &pbmap, &npbmaps, error);
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
			printk("linvfs_bmap: this shouldn't happen %lld %d!\n",
				pbmap.pbm_offset,
				count);
		}
		blockno += pbmap.pbm_offset >> block_shift;
	}

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,3,1)
	return(blockno >> (block_shift - 9));
#else
	if (!create) {
		bh_result->b_dev = inode->i_dev;
		bh_result->b_blocknr = blockno >> (block_shift - 9);
		bh_result->b_state |= (1UL << BH_Mapped);

		return 0;
	}

	return -EIO;
#endif
}

/*
 * JIMJIM have linvfs_updatepage use a new VOP if we are going to keep it
 * around. We might just want to use _pagebuf_file_write and the bmap, ...
 * instead of having this interface.
 */

int
linvfs_updatepage(struct file *filp, struct page *page, const char *buf,
	unsigned long offset, unsigned int bytes, int sync)
{
	vnode_t		*vp;
	struct dentry	*dentry = filp->f_dentry;
	struct inode	*inode = dentry->d_inode;
	unsigned	long block;
	int		*p, nr[PAGE_SIZE/512], error;
	int		wrote = 0, status, i, bmaps, block_bits;
	pb_bmap_t	pbmap[2];
	int		npbmaps = 2;
	long		blockno;

	/* printk("linvfs_updatepage(%s/%s %d@%ld, sync=%d)\n",
		dentry->d_parent->d_name.name, dentry->d_name.name,
		bytes, page->offset+offset, sync); */
	
	vp = LINVFS_GET_VP(inode);

	VOP_RWLOCK(vp, VRWLOCK_READ);
	VOP_BMAP(vp, page->offset, offset+bytes, B_WRITE,
		(struct page_buf_bmap_s *) &pbmap, &npbmaps, error);
	VOP_RWUNLOCK(vp, VRWLOCK_READ);
	if (error)
		return -EIO;

	atomic_inc(&page->count);

        set_bit(PG_locked, &page->flags);
        set_bit(PG_free_after, &page->flags);

	_linvfs_set_blocks(nr, PAGE_SIZE/512, &pbmap[0], npbmaps,
		inode->i_sb->s_blocksize_bits, inode->i_sb->s_blocksize);

	/* bytes can't be more than one page. */
	ASSERT(bytes <= PAGE_CACHE_SIZE);

	/* Copy down the user's data */
	bytes -= copy_from_user((unsigned long *)(page_address(page) + offset),
						buf, bytes);

	if (!bytes) {
		wrote = -EFAULT;
		clear_bit(PG_locked, &page->flags);
		clear_bit(PG_uptodate, &page->flags);
		goto out;
	}

	/* set_bit(PG_uptodate, &page->flags); who set's this? */

	/* Now, write out the page to the right spot */
	status = brw_page(WRITE, page, inode->i_dev, nr, inode->i_sb->s_blocksize, 1);

	if (status < 0) /* Return error below if this failed */
		wrote = status;
	else
		wrote = bytes;

out:
	wake_up(&page->wait);
	return(wrote); /* The amount written */
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

int
linvfs_pb_bmap(struct inode *inode, 
			   loff_t offset,
			   size_t count,
			   pb_bmap_t *pbmapp,
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
		vop_flags = B_READ;
		break;

	case PBF_WRITE:
		vop_flags = B_WRITE;
		break;
	
	default:
		printk("linvfs_pb_bmap: flag %x not implemented\n", flags);
		return -EINVAL;
	}

	vp = LINVFS_GET_VP(inode);

	*retpbbm = maxpbbm;

	VOP_RWLOCK(vp, VRWLOCK_READ);
	VOP_BMAP(vp, offset, count, vop_flags,
			(struct page_buf_bmap_s *) pbmapp, retpbbm, error);
	VOP_RWUNLOCK(vp, VRWLOCK_READ);

	return error;
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
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,3,1)
  linvfs_readpage,
  linvfs_writepage,
  linvfs_bmap,
#else
  linvfs_get_block,
  linvfs_readpage,
  linvfs_writepage,
  block_flushpage,
#endif
  NULL,	 /*  truncate  */
  NULL,  /*  permission  */
  NULL,	 /*  smap  */
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,3,1)
  linvfs_updatepage,  /*  updatepage  */
#endif
  linvfs_revalidate
#if defined(_USING_BUF_T)
  , linvfs_pb_bmap
#endif
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
