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
#include <linux/mm.h>

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
#include <xfs_types.h>
#include <xfs_inum.h>
#include <xfs_bmap_btree.h>
#include <xfs_bmap.h>
#include <xfs_dir.h>
#include <xfs_dir_sf.h>
#include <xfs_dir2.h>
#include <xfs_dir2_sf.h>
#include <xfs_attr_sf.h>
#include <xfs_dinode.h>
#include <xfs_inode.h>

#include <xfs_bit.h> /* xfs_ag.h depends on xfs_bit.h */
#include <xfs_ag.h> /* xfs_sb.h depends on xfs_ag.h */
#include <xfs_sb.h> /* xfs_rw.h depends on xfs_sb.h */
#include <xfs_rw.h> /* for XFS_FSB_TO_DB */

#include <xfs_log.h> /* xfs_trans.h depends on xfs_log.h */
#include <xfs_trans.h> /* xfs_mount.h depends on xfs_trans.h */
#include <xfs_mount.h> /* for xfs_mount_t */

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
			&cred, error);
	} else if (tp == VDIR) {
		VOP_MKDIR(dvp, (char *)dentry->d_name.name, &va, &vp,
			&cred, error);
	} else {
		error = EINVAL;
	}

	if (!error) {
		ASSERT(vp);
		ino = vp->v_nodeid;
		ASSERT(ino);
		ip = iget(dir->i_sb, ino);
		if (!ip) {
			VN_RELE(vp);
			return -ENOMEM;
		}
		d_instantiate(dentry, ip);
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
		ino = cvp->v_nodeid;
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

#ifdef DELALLOC_PURGE
	/* XXX ---------- DELALLOC --------------- XXX */
	extern atomic_t	pb_delalloc_pages;
	extern wait_queue_head_t pcd_waitq;

	while (atomic_read(&pb_delalloc_pages) > 0) {
		wake_up_interruptible(&pcd_waitq);
		/* Sleep 10 mS - arbitrary */
		schedule_timeout((10*HZ)/1000);
	}
	/* XXX ---------- DELALLOC --------------- XXX */
#endif


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
        vattr_t va;
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
							&cvp, &cred, error);
	if (!error) {
		ASSERT(cvp);
		ino = cvp->v_nodeid;
		ASSERT(ino && (cvp->v_type == VLNK));
		ip = iget(dir->i_sb, ino);
		if (!ip) {
			error = -ENOMEM;
			VN_RELE(cvp);
		} else {
			d_instantiate(dentry, ip);
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
		error = -EINVAL;
	}

	if (!error) {
		error = linvfs_common_cr(dir, dentry, mode, tp, rdev);
	}

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
		dput(base);
		return NULL;
	}

	link[MAXNAMELEN - uio.uio_resid] = '\0';

	UPDATE_ATIME(dentry->d_inode);
	base = lookup_dentry(link, base, follow);
	kfree_s(link, MAXNAMELEN);
	return base;
}

int linvfs_permission(struct inode *ip, int mode)
{
	cred_t	cred;		/* Temporary cred workaround */
        vnode_t *vp;
	int	error;

	mode <<= 6;		/* convert from linux to xfs access bits */
        vp = LINVFS_GET_VP(ip);
	VOP_ACCESS(vp, mode, &cred, error);

	return -error;
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

	if (!error) {
		inode->i_mode = VTTOIF(va.va_type) | va.va_mode;
		inode->i_nlink = va.va_nlink;
		inode->i_uid = va.va_uid;
		inode->i_gid = va.va_gid;
		inode->i_rdev = va.va_rdev;
		inode->i_size = va.va_size;
		inode->i_blocks = va.va_nblocks;
		inode->i_blksize = va.va_blksize;
		inode->i_atime = va.va_atime.tv_sec;
		inode->i_mtime = va.va_mtime.tv_sec;
		inode->i_ctime = va.va_ctime.tv_sec;
	}

        return -error;
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
	int		error;
	long		blockno;
	xfs_inode_t	*ip;

	vp = LINVFS_GET_VP(inode);

	*retpbbm = maxpbbm;

#ifdef XFS_DELALLOC
	if (flags & PBF_BMAP_TRY_ILOCK) {
		bhv_desc_t	*bdp;

		vp = LINVFS_GET_VP(inode);
		ASSERT(vp);
		bdp = VNODE_TO_FIRST_BHV(vp);
		ASSERT(bdp);
		ip = XFS_BHVTOI(bdp);

		if (!xfs_ilock_nowait(ip, XFS_IOLOCK_EXCL))
			return EAGAIN;
	}
#endif

	VOP_BMAP(vp, offset, count, flags,
			(struct page_buf_bmap_s *) pbmapp, retpbbm, error);

#ifdef XFS_DELALLOC
	if (flags & PBF_BMAP_TRY_ILOCK)
		xfs_iunlock(ip, XFS_IOLOCK_EXCL);
#endif


	return error;
}

extern int xfs_ilock_nowait(xfs_inode_t *, uint lock_flags);

int xfs_hit_full_page, xfs_hit_nowait, xfs_hit_nowait_done;

int
linvfs_read_full_page(
	struct dentry *dentry,
	mem_map_t *page)
{
	vnode_t		*vp;
	struct inode	*inode = dentry->d_inode;
	int		error;
	bhv_desc_t	*bdp;
	xfs_inode_t	*ip;

	if (!PageLocked(page))
		BUG();

	xfs_hit_full_page++;
	vp = LINVFS_GET_VP(inode);
	ASSERT(vp);
	bdp = VNODE_TO_FIRST_BHV(vp);
	ASSERT(bdp);
	ip = XFS_BHVTOI(bdp);
	if (!xfs_ilock_nowait(ip, XFS_IOLOCK_SHARED)) {
		xfs_hit_nowait++;
		ASSERT(atomic_read(&page->count));
		UnlockPage(page);
		xfs_ilock(ip, XFS_IOLOCK_SHARED);
		lock_page(page); /* We can wait for the page with inode I/O lock */
		if (Page_Uptodate(page)) {
			UnlockPage(page);
			xfs_hit_nowait_done++;
			xfs_iunlock(ip, XFS_IOLOCK_SHARED);
			return 0;
		}
	}
	error = pagebuf_read_full_page(dentry, page);
	xfs_iunlock(ip, XFS_IOLOCK_SHARED);
	return error;
}

int
linvfs_write_full_page(
	struct dentry *dentry,
	struct page *page)
{
	vnode_t		*vp;
	struct inode	*inode = dentry->d_inode;
	int		error;
	bhv_desc_t	*bdp;
	xfs_inode_t	*ip;

	xfs_hit_full_page++;
	vp = LINVFS_GET_VP(inode);
	ASSERT(vp);
	bdp = VNODE_TO_FIRST_BHV(vp);
	ASSERT(bdp);
	ip = XFS_BHVTOI(bdp);
	if (!xfs_ilock_nowait(ip, XFS_IOLOCK_EXCL)) {
		xfs_hit_nowait++;
		ASSERT(atomic_read(&page->count));
		UnlockPage(page);
		xfs_ilock(ip, XFS_IOLOCK_EXCL);
		lock_page(page); /* We can wait for the page with inode I/O lock */
		if (Page_Uptodate(page)) {
			xfs_hit_nowait_done++;
			xfs_iunlock(ip, XFS_IOLOCK_EXCL);
			return 0;
		}
	}
	error = pagebuf_write_full_page(dentry, page);
	xfs_iunlock(ip, XFS_IOLOCK_EXCL);
	return error;
}

void linvfs_file_read(
	struct file *filp,
	void * desc)
{
	struct dentry *dentry = filp->f_dentry;
	struct inode *inode = dentry->d_inode;
	vnode_t		*vp;
	int error;

	vp = LINVFS_GET_VP(inode);
	VOP_RWLOCK(vp, VRWLOCK_READ);
	pagebuf_file_read(filp, desc);
	VOP_RWUNLOCK(vp, VRWLOCK_READ);
	return;
}

static int linvfs_bmap(struct address_space *mapping, long block)
{
	struct inode *inode = (struct inode *)mapping->host;
	xfs_bmbt_irec_t mval;
	int error, nmaps = 1;
	xfs_fsblock_t output;
	long xfsblock;
	xfs_mount_t *mp;

	vnode_t *vp = LINVFS_GET_VP(inode);
	xfs_inode_t *xfs_inode;

	ASSERT(vp);

	vn_trace_entry(vp, "linvfs_bmap", (inst_t *)__return_address);

	xfs_inode = XFS_BHVTOI(vp->v_fbhv);
	mp = XFS_BHVTOM(vp->v_fbhv);

	/* The blockno passed is in terms of basic blocks */
#if 0
	printk("mp: %p, blocklog: %u, BBSHIFT: %d, blkbblog: %lu\n",
	       mp, (unsigned int)(mp->m_sb.sb_blocklog), BBSHIFT,
	       (unsigned long)mp->m_blkbb_log);
#endif
	xfsblock = XFS_BB_TO_FSBT(mp, block);

	error = xfs_bmapi(NULL, xfs_inode, xfsblock, 1, 0, NULL, 0, &mval,
			  &nmaps, NULL);

#if 0
	printk("bmapi.br_startoff  : %u\n", (int)(mval.br_startoff));
	printk("bmapi.br_startblock: %u\n", (int)(mval.br_startblock));
	printk("bmapi.br_blockcount: %u\n", (int)(mval.br_blockcount));
	printk("extent: %d\n", (int)(mval.br_startblock + (block - mval.br_startoff)));
#endif

	if (error) {
		printk("error (%d)!\n", error);
		return -1;
	}

	if (mval.br_startblock == HOLESTARTBLOCK) {
		return -1;
	} else {
		output = XFS_FSB_TO_DB(xfs_inode, mval.br_startblock) +
			 XFS_BB_FSB_OFFSET(mp, block);
#if 0
		printk("bmapi: %ld\n", output);
#endif
		return (int)output;
	}
}

struct address_space_operations linvfs_aops = {
  readpage:		linvfs_read_full_page,
  writepage:		linvfs_write_full_page,
  bmap:			linvfs_bmap,
	/* prepare_write: ext2_prepare_write,   */
	/* commit_write: generic_commit_write,  */

};

struct inode_operations linvfs_file_inode_operations =
{
  permission:		linvfs_permission,
  revalidate:		linvfs_revalidate,
  setattr:		linvfs_notify_change,
  pagebuf_bmap:		linvfs_pb_bmap,
  pagebuf_fileread:	linvfs_file_read,
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
