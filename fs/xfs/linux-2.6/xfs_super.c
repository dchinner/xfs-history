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
#include <xfs_os_defs.h>

#define FSID_T /* wrapper hack... border files have type problems */
#include <sys/types.h> 

#include <linux/config.h>
#include <linux/module.h>
#include <linux/errno.h>

#undef  NODEV
#include <linux/version.h>
#include <linux/fs.h>
#include <linux/bitops.h>
#include <linux/sched.h>
#include <linux/locks.h>
#include <linux/slab.h>
#include <linux/xfs_iops.h>
#include <linux/blkdev.h>

#include <sys/systm.h>
#include <sys/sysmacros.h>
#include <sys/capability.h>
#include <sys/vfs.h>
#include <sys/pvfs.h>
#include <sys/vnode.h>
#include <sys/mode.h>
#include <ksys/behavior.h>
#include <sys/statvfs.h>
#include <sys/ktrace.h>
#include <sys/kmem.h>
#include <asm/uaccess.h>
#include <linux/init.h>
#include <linux/page_buf.h>

#include <linux/xfs_cred.h>
#include <xfs_grio.h>

#define	MS_DATA		0x04


#include <xfs_clnt.h>
#include <xfs_inum.h>
#include <sys/uuid.h>
#include <sys/pvfs.h>
#include <xfs_iops.h>
#include <xfs_sb.h>

#undef sysinfo

/* xfs_vfs.c */

void vfsinit(void);

/* xfs_vfs.c */
int xfs_init(int fstype);

extern struct super_operations linvfs_sops;



int spectodevs(
	struct super_block *sb,
	struct xfs_args *args,
	dev_t	*ddevp,
	dev_t	*logdevp,
	dev_t	*rtdevp)
{
#if 1
        *ddevp = sb->s_dev;
        if (args->logdev)
                *logdevp = args->logdev;
        else
                *logdevp = *ddevp;
        if (args->rtdev)
                *rtdevp = args->rtdev;
        else
                *rtdevp = 0;
#else
	*ddevp = *logdevp = makedev(MAJOR(sb->s_dev), MINOR(sb->s_dev));
	*rtdevp = 0;
	printk("xfs: ddev=%d, logdev=%d\n", *ddevp, *logdevp);
#endif
	return 0;
}

extern int xfs_bdstrat_cb(page_buf_t *);

static struct inode_operations linvfs_meta_ops = {
	pagebuf_ioinitiate:	xfs_bdstrat_cb,
};

static struct address_space_operations linvfs_meta_aops = {
	sync_page:		block_sync_page,
};

struct inode *
linvfs_make_inode(kdev_t kdev, struct super_block *sb)
{
	struct inode *inode = get_empty_inode();

	inode->i_dev = kdev;
	inode->i_op = &linvfs_meta_ops;
	inode->i_mapping->a_ops = &linvfs_meta_aops;
	inode->i_sb = sb;


	pagebuf_lock_enable(inode);

	return inode;
}

void
linvfs_release_inode(struct inode *inode)
{
	if (inode) {
		pagebuf_delwri_flush(inode, 1);
		pagebuf_lock_disable(inode);
		truncate_inode_pages(&inode->i_data, 0L, TRUNC_NO_TOSS);
		iput(inode);
	}
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
	extern		int mountargs_xfs(char *, struct xfs_args *);

	struct mounta	ap;
	struct mounta	*uap = &ap;
	char		spec[256];
	struct xfs_args	arg, *args = &arg;
	int		error;
	statvfs_t	statvfs;
	u_int		disk, partition;
	struct		inode *ip, *cip;

	MOD_INC_USE_COUNT;

	/* first mount pagebuf delayed write daemon not running yet */
	if (pagebuf_daemon_start() < 0) {
		goto fail_vnrele;
	}

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

	/*  Kludge in XFS until we have other VFS/VNODE FSs  */

	vfsops = &xfs_vfsops;

	/*  Set up the vfs_t structure  */

	vfsp = vfs_allocate();

	if (sb->s_flags & MS_RDONLY)
                vfsp->vfs_flag |= VFS_RDONLY;

	/*  Setup up the cvp structure  */

	cip = (struct inode *)kern_malloc(sizeof(struct inode));
	bzero(cip, sizeof(*cip));

	cip->i_count = 1;

	cvp = LINVFS_GET_VN_ADDRESS(cip);

	cvp->v_type   = VDIR;
	cvp->v_number = 1;		/* Place holder */
	cvp->v_inode  = cip;

#ifdef  CONFIG_XFS_VNODE_TRACING
	cvp->v_trace = ktrace_alloc(VNODE_TRACE_SIZE, KM_SLEEP);
#endif  /* CONFIG_XFS_VNODE_TRACING */

	vn_trace_entry(cvp, "linvfs_read_super", (inst_t *)__return_address);

        cvp->v_flag |= VMOUNTING;

	/*  When we support DMI, we need to set up the behavior
		chain for this vnode  */

	LINVFS_SET_CVP(sb, cvp);
	vfsp->vfs_super = sb;


	sb->s_blocksize = 512;
	sb->s_blocksize_bits = ffs(sb->s_blocksize) - 1;
	set_blocksize(sb->s_dev, 512);

	sb->s_op = &linvfs_sops;

	LINVFS_SET_VFS(sb, vfsp);

	VFSOPS_MOUNT(vfsops, vfsp, cvp, uap, NULL, sys_cred, error);
	if (error)
		goto fail_vfsop;

	VFS_STATVFS(vfsp, &statvfs, NULL, error);
	if (error)
		goto fail_unmount;

	sb->s_magic = XFS_SB_MAGIC;
	sb->s_dirt = 1;  /*  Make sure we get regular syncs  */

        VFS_ROOT(vfsp, &rootvp, error);
        if (error)
                goto fail_unmount;

	ip = LINVFS_GET_IP(rootvp);

	linvfs_set_inode_ops(ip);

	sb->s_root = d_alloc_root(ip);

	if (!sb->s_root)
		goto fail_vnrele;

	if (is_bad_inode((struct inode *) sb->s_root))
		goto fail_vnrele;

	vn_trace_exit(rootvp, "linvfs_read_super", (inst_t *)__return_address);

	return(sb);

fail_vnrele:
	VN_RELE(rootvp);

fail_unmount:
	VFS_UNMOUNT(vfsp, 0, sys_cred, error);
	/*  We need to do something here to shut down the 
		VNODE/VFS layer.  */

fail_vfsop:
	vfs_deallocate(vfsp);

	MOD_DEC_USE_COUNT;

	return(NULL);
}

void
linvfs_set_inode_ops(
	struct inode	*inode)
{
	vnode_t	*vp;

	vp = LINVFS_GET_VN_ADDRESS(inode);

	inode->i_mode = VTTOIF(vp->v_type);

	if (vp->v_type == VNON || S_ISREG(inode->i_mode)) {
		inode->i_op = &linvfs_file_inode_operations;
		inode->i_fop = &linvfs_file_operations;
		inode->i_mapping->a_ops = &linvfs_aops;
	} else if (S_ISDIR(inode->i_mode)) {
		inode->i_op = &linvfs_dir_inode_operations;
		inode->i_fop = &linvfs_dir_operations;
	} else if (S_ISLNK(inode->i_mode)) {
		inode->i_op = &linvfs_symlink_inode_operations;
		if (inode->i_blocks)
			inode->i_mapping->a_ops = &linvfs_aops;
	} else
		init_special_inode(inode, inode->i_mode, inode->i_rdev);
}

void
linvfs_read_inode(
	struct inode	*inode)
{
	vfs_t		*vfsp = LINVFS_GET_VFS(inode->i_sb);

	if (vfsp) {
		vn_initialize(vfsp, inode, 1);
	} else {
		make_bad_inode(inode);
		return;
	}

	inode->i_version = ++event;
}


#ifdef	CONFIG_XFS_VNODE_TRACING

/*
 * We only 'use' the "put" & "write" methods to
 * trace interesting events in the life of a vnode.
 */
void
linvfs_put_inode(
	struct inode	*inode)
{
	vnode_t		*vp = LINVFS_GET_VP(inode);

	if (vp) {
		vn_trace_entry(vp, "linvfs_put_inode",
					(inst_t *)__return_address);
	}
}


void
linvfs_write_inode(
	struct inode	*inode)
{
	vnode_t	*vp = LINVFS_GET_VP(inode);

	if (vp) {
		vn_trace_entry(vp, "linvfs_write_inode",
					(inst_t *)__return_address);
	}
}
#endif	/* CONFIG_XFS_VNODE_TRACING */


void
linvfs_delete_inode(
	struct inode	*inode)
{
	vnode_t	*vp = LINVFS_GET_VP(inode);

	if (vp) {

		vn_trace_entry(vp, "linvfs_delete_inode",
					(inst_t *)__return_address);
		/*
		 * Remove the vnode, the nlink count
		 * is zero & the unlink will complete.
		 */
		vp->v_flag |= VPURGE;
		vn_remove(vp);

	} else {
printk("linvfs_delete_inode: NOVP!: inode/0x%p ino/%ld icnt/%d\n",
inode, inode->i_ino, inode->i_count);
	BUG();
	}

	clear_inode(inode);
}


void
linvfs_clear_inode(
	struct inode	*inode)
{
	vnode_t		*vp = LINVFS_GET_VP(inode);

	if (vp) {

		vn_trace_entry(vp, "linvfs_clear_inode",
					(inst_t *)__return_address);
		/*
		 * Do all our cleanup, and remove
		 * this vnode.
		 */
		vp->v_flag |= VPURGE;
		vn_remove(vp);
	}
}


void
linvfs_put_super(
	struct super_block *sb)
{
	int		error;
	int		sector_size = 512;
	struct inode	*rootip;
	kdev_t		dev = sb->s_dev;
	vfs_t 		*vfsp = LINVFS_GET_VFS(sb);
	vnode_t		*rootvp, *cvp;

	/*
	 * Find the root vnode/inode, we can't
	 * use sb->s_root->d_inode 'cause it
	 * appears to be gone already?
	 */
	VFS_ROOT(vfsp, &rootvp, error);

	if (error) {
		printk("XFS unmount got error %d\n", error);
		printk("linvfs_put_super: vfsp/0x%p left dangling!\n", vfsp);
		return;
	}

	VN_RELE(rootvp);	/* Release the hold taken by VFS_ROOT */

	rootip = LINVFS_GET_IP(rootvp);

	VFS_DOUNMOUNT(vfsp, 0, NULL, sys_cred, error); 

	if (error) {
		printk("XFS unmount got error %d\n", error);
		printk("linvfs_put_super: vfsp/0x%p left dangling!\n", vfsp);
		return;
	}

	vfs_deallocate(vfsp);

	cvp = LINVFS_GET_CVP(sb);

#ifdef  CONFIG_XFS_VNODE_TRACING
	ktrace_free(cvp->v_trace);

	cvp->v_trace = NULL;
#endif  /* CONFIG_XFS_VNODE_TRACING */

	kfree(cvp->v_inode);

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

	VFS_SYNC(vfsp, SYNC_FSDATA|SYNC_BDFLUSH|SYNC_NOWAIT|SYNC_ATTR,
		sys_cred, error);

	sb->s_dirt = 1;  /*  Keep the syncs coming.  */
}


int
linvfs_statfs(
	struct super_block *sb,
	struct statfs	*buf)
{
	vfs_t		*vfsp = LINVFS_GET_VFS(sb);
	vnode_t		*rootvp;
	statvfs_t	stat;

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


	buf->f_type = XFS_SB_MAGIC;
	buf->f_bsize = stat.f_bsize;
	buf->f_blocks = stat.f_blocks;
	buf->f_bfree = stat.f_bfree;
	buf->f_bavail = stat.f_bavail;
	buf->f_files = stat.f_files;
	buf->f_ffree = stat.f_ffree;
	/* sfs.f_fsid = stat.f_fsid; JIMJIMJIM Fix this??? */
	buf->f_namelen = stat.f_namemax;

	return 0;
}


int
linvfs_remount(
	struct super_block *sb,
	int		*flags,
	char		*options)
{
	struct xfs_args args;
	extern vfsops_t xfs_vfsops;
	vfs_t *vfsp;
	vnode_t *cvp;
	extern int mountargs_xfs(char *options, struct xfs_args *args);

	vfsp = LINVFS_GET_VFS(sb);
	cvp = LINVFS_GET_CVP(sb);

	if ((*flags & MS_RDONLY) == (sb->s_flags & MS_RDONLY))
		return 0;

	if (mountargs_xfs(options, &args) != 0)
                return -ENOSPC;

	if (*flags & MS_RDONLY || args.flags & MS_RDONLY) {
		int error;

		printk("XFS: Remounting read-only\n");
		vfsp->vfs_flag |= VFS_RDONLY;
		sb->s_flags |= MS_RDONLY;
		PVFS_SYNC(vfsp->vfs_fbhv, SYNC_ATTR|SYNC_DELWRI|SYNC_NOWAIT,
			  sys_cred, error);
		if (error)
			printk("XFS: PVFS_SYNC failed!\n");
	} else {
		printk("XFS: Remounting read-write\n");
		vfsp->vfs_flag &= ~VFS_RDONLY;
		sb->s_flags &= ~MS_RDONLY;
	}

	return 0;
}



static struct super_operations linvfs_sops = {
	read_inode:		linvfs_read_inode,
#ifdef	CONFIG_XFS_VNODE_TRACING
	put_inode:		linvfs_put_inode,
	write_inode:		linvfs_write_inode,
#endif
	delete_inode:		linvfs_delete_inode,
	clear_inode:		linvfs_clear_inode,
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
	extern void uuid_init(void);
	struct sysinfo	si;

	si_meminfo(&si);

	physmem = si.totalram;

	cred_init();

	vfsinit();
	uuid_init();
	xfs_init(0);

#ifdef CONFIG_XFS_GRIO
        xfs_grio_init();
#endif
        
	return register_filesystem(&xfs_fs_type);
}


#ifdef MODULE

EXPORT_NO_SYMBOLS;

int init_module(void)
{
	return init_xfs_fs();
}

void cleanup_module(void)
{
	extern void xfs_cleanup(void);

#ifdef CONFIG_XFS_GRIO
        xfs_grio_uninit();
#endif
	xfs_cleanup();
        unregister_filesystem(&xfs_fs_type);
}

#endif
