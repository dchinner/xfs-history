
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
/*
 *  fs/xfs/xfs_super.c
 *
 */


/* Don't want anything from linux/capability.h. We need the stuff from IRIX */
#define FSID_T /* wrapper hack... border files have type problems */
#include <sys/types.h> 

#include <linux/module.h>
#include <linux/errno.h>

#include "xfs_coda_oops.h"

#include <linux/xfs_to_linux.h>
#include <config/page/buf/meta.h>	/* CONFIG_PAGE_BUF_META */

#undef  NODEV
#include <linux/version.h>
#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/locks.h>
#include <linux/slab.h>
#include <linux/xfs_iops.h>
#include <linux/blkdev.h>

#include <linux/linux_to_xfs.h>

#include <sys/systm.h>
#include <sys/sysmacros.h>
#include <sys/capability.h>
#include <sys/cred.h>
#include <sys/vfs.h>
#include <sys/pvfs.h>
#include <sys/vnode.h>
#include <ksys/behavior.h>
#include <sys/statvfs.h>
#include <asm/uaccess.h>
#include <linux/init.h>
#include <linux/page_buf.h>

#define	MS_DATA		0x04


#include <xfs_clnt.h>
#include <xfs_inum.h>
#include <sys/uuid.h>
#include <sys/pvfs.h>
#include <xfs_sb.h>

#undef sysinfo


#if CONFIG_PAGE_BUF_META
extern pagebuf_daemon_t *pb_daemon;
#else
/* xfs_fs_bio.c */
void binit(void);
#endif

/* xfs_vfs.c */

void vfsinit(void);

/* xfs_vfs.c */
int xfs_init(int fstype);

extern struct super_operations linvfs_sops;


void
linvfs_inode_attr_in(
	struct inode	*inode)
{
	vnode_t		*vp = LINVFS_GET_VP(inode);
	vattr_t		attr;
	int		error;

	memset(&attr, 0, sizeof(vattr_t));
	attr.va_mask = AT_STAT;

	VOP_GETATTR(vp, &attr, 0, sys_cred, error);

	if (error) {
		printk("linvfs:  Whoah!  Bad VOP_GETATTR()\n");
		return;
	}

	inode->i_mode = attr.va_mode & S_IALLUGO;

	switch (attr.va_type) {
	case VREG:
		inode->i_mode |= S_IFREG;
		break;
	case VDIR:
		inode->i_mode |= S_IFDIR;
		break;
	case VLNK:
		inode->i_mode |= S_IFLNK;
		break;
	case VBLK:
		inode->i_mode |= S_IFBLK;
		inode->i_rdev = MKDEV(emajor(attr.va_rdev),
				      eminor(attr.va_rdev));
		break;
	case VCHR:
		inode->i_mode |= S_IFCHR;
		inode->i_rdev = MKDEV(emajor(attr.va_rdev),
				      eminor(attr.va_rdev));
		break;
	case VFIFO:
		inode->i_mode |= S_IFIFO;
		break;
	default:
		printk(KERN_ERR "XFS:  D'oh!  dinode %lu has bad type %d\n",
			inode->i_ino, attr.va_type);
		break;
	};

	inode->i_nlink = attr.va_nlink;
	inode->i_uid = attr.va_uid;
	inode->i_gid = attr.va_gid;
	inode->i_size = attr.va_size;
	inode->i_atime = attr.va_atime.tv_sec;
	inode->i_mtime = attr.va_mtime.tv_sec;
	inode->i_ctime = attr.va_ctime.tv_sec;
	inode->i_blksize = attr.va_blksize;
	inode->i_blocks = attr.va_nblocks;
	inode->i_version = ++event;
}

int spectodevs(
	struct super_block *sb,
	dev_t	*ddevp,
	dev_t	*logdevp,
	dev_t	*rtdevp)
{
	*ddevp = *logdevp = makedev(MAJOR(sb->s_dev), MINOR(sb->s_dev));
	*rtdevp = 0;

	return 0;
}

extern int xfs_bdstrat_cb(page_buf_t *);

static struct inode_operations linvfs_meta_ops = {
	pagebuf_ioinitiate:	xfs_bdstrat_cb,
};

struct inode *
linvfs_make_inode(kdev_t kdev, struct super_block *sb)
{
	struct inode *inode = get_empty_inode();

	inode->i_dev = kdev;
	inode->i_op = &linvfs_meta_ops;
	inode->i_sb = sb;

	pagebuf_lock_enable(inode);

	return inode;
}

void
linvfs_release_inode(struct inode *inode)
{
	pagebuf_lock_disable(inode);
	truncate_inode_pages(inode, 0L);
	iput(inode);
}


struct super_block *
linvfs_read_super(
	struct super_block *sb,
	void		*data,
	int		silent)
{
	vfsops_t	*vfsops;
	extern vfsops_t xfs_vfsops;
	vfs_t		*vfsp;
	vnode_t		*cvp, *rootvp;
	unsigned long	ino;
	extern		int mountargs_xfs(char *, struct xfs_args *);

	struct mounta	ap;
	struct mounta	*uap = &ap;
	char		spec[256];
	struct xfs_args	arg, *args = &arg;
	int		error, locked = 1;
	statvfs_t	statvfs;
	vattr_t		attr;
	u_int		disk, partition;

	MOD_INC_USE_COUNT;

#if CONFIG_PAGE_BUF_META
	if (!pb_daemon){
		/* first mount pagebuf delayed write daemon not running yet */
		if (pagebuf_daemon_start() < 0) {
			goto fail_vnrele;
		}
	}
#endif

	/*  Setup the uap structure  */

	memset(uap, 0, sizeof(struct mounta));

	sprintf(spec, bdevname(sb->s_dev));
	uap->spec = spec;

	/*  uap->dir not needed until DMI is in place  */
	uap->flags = MS_DATA;

	memset(args, 0, sizeof(struct xfs_args));
	if (mountargs_xfs((char *)data, args) != 0) {
		MOD_DEC_USE_COUNT;
		return NULL;
	}

	args->fsname = uap->spec;
  
	uap->dataptr = (char *)args;
	uap->datalen = sizeof(*args);

	lock_super(sb);
	/*  Kludge in XFS until we have other VFS/VNODE FSs  */

	vfsops = &xfs_vfsops;

	/*  Set up the vfs_t structure  */

	vfsp = vfs_allocate();

	if (sb->s_flags & MS_RDONLY)
                vfsp->vfs_flag |= VFS_RDONLY;

	/*  Setup up the cvp structure  */

	cvp = (vnode_t *)kern_malloc(sizeof(vnode_t));
	bzero(cvp, sizeof(*cvp));

	cvp->v_type = VDIR;
	cvp->v_count = 1;
        cvp->v_flag |= VMOUNTING;

	/*  When we support DMI, we need to set up the behavior
		chain for this vnode  */

	LINVFS_SET_CVP(sb, cvp);
	vfsp->vfs_super = sb;


	sb->s_blocksize = 512;
	sb->s_blocksize_bits = ffs(sb->s_blocksize) - 1;
	set_blocksize(sb->s_dev, 512);

	VFSOPS_MOUNT(vfsops, vfsp, cvp, uap, NULL, sys_cred, error);
	if (error)
		goto fail_vfsop;

	VFS_STATVFS(vfsp, &statvfs, NULL, error);
	if (error)
		goto fail_unmount;

	sb->s_magic = XFS_SB_MAGIC;
	sb->s_dirt = 1;  /*  Make sure we get regular syncs  */
	LINVFS_SET_VFS(sb, vfsp);

        VFS_ROOT(vfsp, &rootvp, error);
        if (error)
                goto fail_unmount;

	attr.va_mask = AT_NODEID;

	VOP_GETATTR(rootvp, &attr, 0, sys_cred, error);
	if (error)
		goto fail_vnrele;

	ino = (unsigned long) attr.va_nodeid;
	sb->s_op = &linvfs_sops;
	unlock_super(sb);
	locked = 0;
	sb->s_root = d_alloc_root(iget(sb, ino));
	if (!sb->s_root)
		goto fail_vnrele;

	if (is_bad_inode((struct inode *) sb->s_root))
		goto fail_vnrele;

	return(sb);

fail_vnrele:
	VN_RELE(rootvp);

fail_unmount:
	VFS_UNMOUNT(vfsp, 0, sys_cred, error);
	/*  We need to do something here to shut down the 
		VNODE/VFS layer.  */

fail_vfsop:
	vfs_deallocate(vfsp);
	sb->s_dev = 0;
	if (locked)
		unlock_super(sb);
	MOD_DEC_USE_COUNT;

	return(NULL);
}


void
linvfs_read_inode(
	struct inode	*inode)
{
	vfs_t		*vfsp = LINVFS_GET_VFS(inode->i_sb);
	vnode_t		*vp;
	int		error = ENOENT;

	if (vfsp) {
		VFS_GET_VNODE(vfsp, &vp, inode->i_ino, error);
	}
	if (error) {
		make_bad_inode(inode);
		return;
	}

	LINVFS_GET_VP(inode) = vp;
	vp->v_inode = inode;

	linvfs_inode_attr_in(inode);

	if (S_ISREG(inode->i_mode))
		inode->i_op = &linvfs_file_inode_operations;
	else if (S_ISDIR(inode->i_mode))
		inode->i_op = &linvfs_dir_inode_operations;
	else if (S_ISLNK(inode->i_mode))
		inode->i_op = &linvfs_symlink_inode_operations;
	else if (S_ISBLK(inode->i_mode))
		init_special_inode(inode, inode->i_mode, inode->i_rdev);
}


void
linvfs_put_inode(
	struct inode	*inode)
{
	vnode_t		*vp = LINVFS_GET_VP(inode);

	if (vp) {
		VN_RELE(vp);
	}
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

	linvfs_inode_attr_in(inode);

	return(-error);
}


void
linvfs_put_super(
	struct super_block *sb)
{
	vfs_t 		*vfsp = LINVFS_GET_VFS(sb);
	int		error;
	int		sector_size = 512;
	kdev_t		dev = sb->s_dev;

	VFS_DOUNMOUNT(vfsp, 0, NULL, sys_cred, error); 
	if (error)
		printk("XFS unmount got error %d\n", error);

	vfs_deallocate(vfsp);

	kfree(LINVFS_GET_CVP(sb));

	/*  Do something to get rid of the VNODE/VFS layer here  */

	/* Reset device block size */
	if (hardsect_size[MAJOR(dev)])
		sector_size = hardsect_size[MAJOR(dev)][MINOR(dev)];

	set_blocksize(dev, sector_size);

	MOD_DEC_USE_COUNT; 
}


void
linvfs_write_super(
	struct super_block *sb)
{
 	vfs_t		*vfsp = LINVFS_GET_VFS(sb); 
 	int		error; 
	irix_dev_t	dev;
#if !CONFIG_PAGE_BUF_META
	extern void bflush_bufs(dev_t);
#endif


	VFS_SYNC(vfsp, SYNC_FSDATA|SYNC_BDFLUSH|SYNC_NOWAIT|SYNC_ATTR,
		sys_cred, error);

#if !CONFIG_PAGE_BUF_META
	bflush_bufs(vfsp->vfs_dev);/* Pretend a bdflush is going off for XFS
					specific buffers from xfs_fs_bio.c */
#endif

	sb->s_dirt = 1;  /*  Keep the syncs coming.  */
}


int
linvfs_statfs(
	struct super_block *sb,
	struct statfs	*statfsbuf,
	int		size)
{
	vfs_t		*vfsp = LINVFS_GET_VFS(sb);
	vnode_t		*rootvp;
	statvfs_t	stat;

	struct statfs	sfs;
	int		error;
	
	VFS_ROOT(vfsp, &rootvp, error);
	if (error){
	  return(-error);
	}

	VFS_STATVFS(vfsp, &stat, rootvp, error);

	VN_RELE(rootvp);

	if (error){
	  return(-error);
	}


	memset(&sfs, 0, sizeof(struct statfs));

	sfs.f_type = XFS_SB_MAGIC;
	sfs.f_bsize = stat.f_bsize;
	sfs.f_blocks = stat.f_blocks;
	sfs.f_bfree = stat.f_bfree;
	sfs.f_bavail = stat.f_bavail;
	sfs.f_files = stat.f_files;
	sfs.f_ffree = stat.f_ffree;
	/* sfs.f_fsid = stat.f_fsid; JIMJIMJIM Fix this??? */
	sfs.f_namelen = stat.f_namemax;

	error = copy_to_user(statfsbuf, &sfs,
		(size < sizeof(struct statfs)) ? size : sizeof(struct statfs));

	return(error);
}


int
linvfs_remount(
	struct super_block *sb,
	int		*flags,
	char		*options)
{
	printk(KERN_ERR "XFS:  Hoser!!!  Somebody called remountfs()\n");
	return(-ENOSYS);
}



static struct super_operations linvfs_sops = {
	read_inode:		linvfs_read_inode,
	put_inode:		linvfs_put_inode,
	notify_change:		linvfs_notify_change,
	put_super:		linvfs_put_super,
	write_super:		linvfs_write_super,
	statfs:			linvfs_statfs,
	remount_fs:		linvfs_remount
};

static struct file_system_type xfs_fs_type = {
	"xfs", 
	FS_REQUIRES_DEV,
	linvfs_read_super, 
	NULL
};

int __init init_xfs_fs(void)
{
  struct sysinfo	si;

  ENTER("init_xfs_fs"); 
  si_meminfo(&si);

  physmem = si.totalram;

  cred_init();
#if !CONFIG_PAGE_BUF_META
  binit();
#endif
  vfsinit();
  uuid_init();
  xfs_init(0);
  
  EXIT("init_xfs_fs"); 
  return register_filesystem(&xfs_fs_type);
}


#ifdef MODULE

int init_module(void)
{
	return init_xfs_fs();
}

void cleanup_module(void)
{
	extern void xfs_cleanup(void);
	extern void vn_cleanup();

	xfs_cleanup();
	vn_cleanup();
        unregister_filesystem(&xfs_fs_type);
}

#endif
