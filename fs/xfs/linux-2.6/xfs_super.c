/*
 *  fs/xfs/xfs_linux_ops_super.c
 *
 */

/* Don't want anything from linux/capability.h. We need the stuff from IRIX */

#include <linux/types.h>
#include <linux/module.h>

#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/sched.h>

/* Use the IRIX capabilities for now */
#undef CAP_FOWNER
#undef CAP_DAC_READ_SEARCH

typedef struct __timespec {
        time_t  tv_sec;         /* seconds */
	long    tv_nsec;
} timespec_t;

#include <sys/types.h>
#include <sys/capability.h>
#include <sys/cred.h>
#include <sys/vfs.h>
#include <sys/vnode.h>
#include <sys/behavior.h>
#include <sys/statvfs.h>

#undef MS_RDONLY
#undef MS_REMOUNT
#include <sys/mount.h>

#include "xfs_linux_ops_inode.h"

#include "xfs_clnt.h"
#include "xfs_inum.h"
#include <sys/uuid.h>
#include "xfs_sb.h"

/*
 * Global system credential structure.
 */
cred_t	sys_cred_val, *sys_cred = &sys_cred_val;

/*
 * Initialize credentials data structures.
 */
static void
cred_init(void)
{
        memset(sys_cred, 0, sizeof(cred_t));
        sys_cred->cr_ref = 1;

        sys_cred->cr_cap.cap_effective = CAP_ALL_ON;
        sys_cred->cr_cap.cap_inheritable = CAP_ALL_ON;
        sys_cred->cr_cap.cap_permitted = CAP_ALL_ON;
        /*_MAC_INIT_CRED();*/
}


int
fs_dounmount(
        bhv_desc_t      *bdp,
        int             flags,
        vnode_t         *rootvp,
        cred_t          *cr)
{
        struct vfs      *vfsp = bhvtovfs(bdp);
        bhv_desc_t      *fbdp = vfsp->vfs_fbhv;
        vnode_t         *coveredvp;
        int             error;

        /*
         * Wait for sync to finish and lock vfsp.  This also sets the
         * VFS_OFFLINE flag.  Once we do this we can give up reference
         * the root vnode which we hold to avoid the another unmount
         * ripping the vfs out from under us before we get to lock it.
         * The VFS_DOUNMOUNT calling convention is that the reference
         * on the rot vnode is released whether the call succeeds or
         * fails.
         */
        error = vfs_lock_offline(vfsp);
        if (rootvp)
                VN_RELE(rootvp);
        if (error)
                return error;

        /*
         * Get covered vnode after vfs_lock.
         */
        coveredvp = vfsp->vfs_vnodecovered;

        /*
         * Purge all dnlc entries for this vfs.
         */
        (void) dnlc_purge_vfsp(vfsp, 0);

        /*
         * Now invoke SYNC and UNMOUNT ops, using the PVFS versions is
         * OK since we already have a behavior lock as a result of
         * being in VFS_DOUNMOUNT.  It is necessary to do things this
         * way since using the VFS versions would cause us to get the
         * behavior lock twice which can cause deadlock as well as
         * making the coding of vfs relocation unnecessarilty difficult
         * by making relocations invoked by unmount occur in a different
         * environment than those invoked by mount-update.
         */
        PVFS_SYNC(fbdp, SYNC_ATTR|SYNC_DELWRI|SYNC_NOWAIT, cr, error);
        if (error == 0)
                PVFS_UNMOUNT(fbdp, flags, cr, error);

        if (error) {
                vfs_unlock(vfsp);       /* clears VFS_OFFLINE flag, too */
        } else {
                --coveredvp->v_vfsp->vfs_nsubmounts;
                ASSERT(vfsp->vfs_nsubmounts == 0);
                vfs_remove(vfsp);
                VN_RELE(coveredvp);
        }
        return error;
}


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
		inode->i_rdev = attr.va_rdev;
		break;
	case VCHR:
		inode->i_mode |= S_IFCHR;
		inode->i_rdev = attr.va_rdev;
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

	struct mounta	ap;
	struct mounta	*uap = &ap;
	char		spec[256];
	struct xfs_args	arg, *args = &arg;
	char		fsname[256];
	int		error;
	statvfs_t	statvfs;
	vattr_t		attr;


	MOD_INC_USE_COUNT;

	lock_super(sb);

	/*  Kludge in XFS until we have other VFS/VNODE FSs  */

	vfsops = &xfs_vfsops;


	/*  Set up the vfs_t structure  */

	vfsp = vfs_allocate();

	if (sb->s_flags & MS_RDONLY)
                vfsp->vfs_flag |= VFS_RDONLY;

	vfsp->vfs_opsver = 0;
	vfsp->vfs_opsflags = 0;
	if (vfsops->vfs_ops_magic == VFSOPS_MAGIC) {
		vfsp->vfs_opsver = vfsops->vfs_ops_version;
		vfsp->vfs_opsflags = vfsops->vfs_ops_flags;
	}


	/*  Setup up the cvp structure  */

	cvp = (vnode_t *)kern_malloc(sizeof(vnode_t));
	bzero(cvp, sizeof(*cvp));

	cvp->v_type = VDIR;
	cvp->v_count = 1;
        cvp->v_flag |= VMOUNTING;

	/*  When we support DMI, we need to set up the behavior
		chain for this vnode  */

	LINVFS_GET_CVP(sb) = cvp;


	/*  Setup the uap structure  */

	memset(&uap, 0, sizeof(struct mounta));

	switch (MAJOR(sb->s_dev)) {
	case 8: {  /*  SCSI  */
		int disk, partition;

		disk = MINOR(sb->s_dev) / 16;
		partition = MINOR(sb->s_dev) % 16;

		sprintf(spec, "/dev/sd%c%d", 'a' + disk, partition);
		}
    
		break;
	default:
		panic("FixMe!!!  (uap->spec)\n");
	};
	uap->spec = spec;

	/*  uap->dir not needed until DMI is in place  */
	uap->flags = MS_DATA;

	/*  It's possible to teach Linux mount about the XFS args and
		allow the user to fill in the args structure, but for now,
		just fill in defaults here.  */
	memset(args, 0, sizeof(struct xfs_args));
	args->version = 3;
	args->logbufs = -1;
	args->logbufsize = -1;
	strcpy(fsname, "hoopy");  /*  fixMe!!!  */
	args->fsname = fsname;
  
	uap->dataptr = (char *)args;
	uap->datalen = sizeof(*args);


	VFSOPS_MOUNT(vfsops, vfsp, cvp, uap, NULL, sys_cred, error);
	if (error)
		goto fail_vfsop;


	VFS_STATVFS(vfsp, &statvfs, NULL, error);
	if (error)
		goto fail_unmount;

	sb->s_blocksize = statvfs.f_bsize;
	sb->s_blocksize_bits = ffs(sb->s_blocksize) - 1;
	sb->s_magic = XFS_SB_MAGIC;
	sb->s_dirt = 1;  /*  Make sure we get regular syncs  */
	LINVFS_GET_VFS(sb) = vfsp;


        VFS_ROOT(vfsp, &rootvp, error);
        if (error)
                goto fail_unmount;

	memset(&attr, 0, sizeof(vattr_t));
	attr.va_mask = AT_NODEID;

	VOP_GETATTR(rootvp, &attr, 0, sys_cred, error);
	if (error)
		goto fail_vnrele;

	sb->s_root = d_alloc_root(iget(sb, attr.va_nodeid), NULL);
	if (!sb->s_root)
		goto fail_vnrele;


	unlock_super();

	return(NULL);



	fail_vnrele:
		VN_RELE(rootvp);

	fail_unmount:
		VFS_UNMOUNT(vfsp, 0, sys_cred, error);
		/*  We need to do something here to shut down the 
			VNODE/VFS layer.  */

	fail_vfsop:
		kfree(vfsp);

	fail:
		unlock_super();
		MOD_DEC_USE_COUNT;

		return(NULL);
}


void
linvfs_read_inode(
	struct inode	*inode)
{
	vfs_t		*vfsp = LINVFS_GET_VFS(inode->i_sb);
	vnode_t		*vp;
	int		error;


	VOP_GET_VNODE(vfsp, &vp, inode->i_ino, error);
	if (error) {
		make_bad_inode(inode);
		return;
	}


	LINVFS_GET_VP(inode) = vp;

	linvfs_inode_attr_in(inode);


	if (S_ISREG(inode->i_mode))
		inode->i_op = &linvfs_file_inode_operations;
	else if (S_ISDIR(inode->i_mode))
		inode->i_op = &linvfs_dir_inode_operations;
	else if (S_ISLNK(inode->i_mode))
		inode->i_op = &linvfs_symlink_inode_operations;
	else if (S_ISBLK(inode->i_mode))
		inode->i_op = &blkdev_inode_operations;
	else if (S_ISCHR(inode->i_mode))
		inode->i_op = &chrdev_inode_operations;
	else if (S_ISFIFO(inode->i_mode))
		init_fifo(inode);
	else
		panic("XFS:  unknown file type:  %d\n", inode->i_mode);
}


void
linvfs_put_inode(
	struct inode	*inode)
{
/*
   Does XFS do read-ahead stuff that needs to be thrown away every time the
   file is closed?  If so, this function needs to do that.
*/
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
	if (error)
		return(error);

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
	vnode_t		*rootvp;
	int		error;

	VFS_DOUNMOUNT(vfsp, 0, NULL, sys_cred, error); 

	vfs_deallocate(vfsp);

	kfree(LINVFS_GET_CVP(sb));

	/*  Do something to get rid of the VNODE/VFS layer here  */

	MOD_DEC_USE_COUNT; 
}


void
linvfs_write_super(
	struct super_block *sb)
{
	vfs_t		*vfsp = LINVFS_GET_VFS(sb);
	int		error;


	VFS_SYNC(vfsp, SYNC_FSDATA|SYNC_ATTR|SYNC_DELWRI|SYNC_NOWAIT,
		sys_cred, error);


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
	if (error)
		return(-error);

	VFS_STATVFS(vfsp, &stat, rootvp, error);

	VN_RELE(rootvp);

	if (error)
		return(-error);


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


void
linvfs_clear_inode(
	struct inode	*inode)
{
	vnode_t		*vp = LINVFS_GET_VP(inode);

	if (vp) 
		VN_RELE(vp);
}



static struct super_operations linvfs_sops = {
	linvfs_read_inode,
	NULL,			/*  write_inode  */
	linvfs_put_inode,
	NULL,			/*  delete_inode  */
	linvfs_notify_change,
	linvfs_put_super,
	linvfs_write_super,
	linvfs_statfs,
	linvfs_remount,
	linvfs_clear_inode,
	NULL			/*  unmount_begin  */
};


static struct file_system_type linvfs_fs_type = {
	"xfs", 
	FS_REQUIRES_DEV,
	linvfs_read_super, 
	NULL
};


__initfunc(int init_linvfs_fs(void))
{
	cred_init();

	return register_filesystem(&linvfs_fs_type);
}

#ifdef MODULE
EXPORT_NO_SYMBOLS;

int init_module(void)
{
	return init_linvfs_fs();
}

void cleanup_module(void)
{
        unregister_filesystem(&linvfs_fs_type);
}

#endif



