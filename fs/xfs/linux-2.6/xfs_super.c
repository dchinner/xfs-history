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
#include <linux/bitops.h>
#include <linux/locks.h>
#include <linux/smp_lock.h>
#include <linux/xfs_iops.h>
#include <linux/blkdev.h>
#include <linux/init.h>
#include <linux/page_buf.h>

/* xfs_vfs[ops].c */
extern void vfsinit(void);
extern int  xfs_init(int fstype);
extern void xfs_cleanup(void);

#ifdef CONFIG_XFS_DMAPI
extern void dmapi_init(void);
extern void dmapi_uninit(void);
#else
# define dmapi_init()		do { } while (0)
# define dmapi_uninit()		do { } while (0)
#endif

#ifdef CONFIG_XFS_GRIO
extern void xfs_grio_init(void);
extern void xfs_grio_uninit(void);
#else
# define xfs_grio_init()	do { } while (0)
# define xfs_grio_uninit()	do { } while (0)
#endif

#ifdef CELL_CAPABLE
extern int cxfs_parseargs(char *, int, struct xfs_args *);
#else
# define cxfs_parseargs(opt,flag,xargs)	(0) /* success */
#endif

#ifdef CONFIG_FS_POSIX_ACL
# define set_posix_acl(sb)	((sb)->s_posix_acl_flag = 1)
#else
# define set_posix_acl(sb)	do { } while (0)
#endif

/* For kernels which have the s_maxbytes field - set it */
#ifdef MAX_NON_LFS
# define set_max_bytes(sb)	((sb)->s_maxbytes = XFS_MAX_FILE_OFFSET)
#else
# define set_max_bytes(sb)	do { } while (0)
#endif

#ifdef CONFIG_XFS_QUOTA
static struct quota_operations linvfs_qops = {
	getxstate:		linvfs_getxstate,
	setxstate:		linvfs_setxstate,
	getxquota:		linvfs_getxquota,
	setxquota:		linvfs_setxquota,
};
# define set_quota_ops(sb)	((sb)->s_qop = &linvfs_qops)
#else
# define set_quota_ops(sb)	((sb)->s_qop = NULL)
#endif

static struct super_operations linvfs_sops;

#define MNTOPT_LOGBUFS  "logbufs"       /* number of XFS log buffers */
#define MNTOPT_LOGBSIZE "logbsize"      /* size of XFS log buffers */
#define MNTOPT_LOGDEV	"logdev"	/* log device */
#define MNTOPT_RTDEV	"rtdev"		/* realtime I/O device */
#define MNTOPT_DMAPI    "dmapi"         /* DMI enabled (DMAPI / XDSM) */
#define MNTOPT_XDSM     "xdsm"          /* DMI enabled (DMAPI / XDSM) */
#define MNTOPT_BIOSIZE  "biosize"       /* log2 of preferred buffered io size */
#define MNTOPT_WSYNC    "wsync"         /* safe-mode nfs compatible mount */
#define MNTOPT_NOATIME  "noatime"       /* don't modify access times on reads */
#define MNTOPT_INO64    "ino64"         /* force inodes into 64-bit range */
#define MNTOPT_NOALIGN  "noalign"       /* turn off stripe alignment */
#define MNTOPT_SUNIT    "sunit"         /* data volume stripe unit */
#define MNTOPT_SWIDTH   "swidth"        /* data volume stripe width */
#define MNTOPT_NORECOVERY "norecovery"  /* don't run XFS recovery */
#define MNTOPT_OSYNCISDSYNC "osyncisdsync" /* o_sync == o_dsync on this fs */
#define MNTOPT_QUOTA    "quota"         /* disk quotas */
#define MNTOPT_MRQUOTA  "mrquota"       /* don't turnoff if SB has quotas on */
#define MNTOPT_NOSUID   "nosuid"        /* disallow setuid program execution */
#define MNTOPT_NOQUOTA  "noquota"       /* no quotas */
#define MNTOPT_UQUOTA   "usrquota"      /* user quota enabled */
#define MNTOPT_GQUOTA   "grpquota"      /* group quota enabled */
#define MNTOPT_UQUOTANOENF "uqnoenforce"/* user quota limit enforcement */
#define MNTOPT_GQUOTANOENF "gqnoenforce"/* group quota limit enforcement */
#define MNTOPT_QUOTANOENF  "qnoenforce" /* same as uqnoenforce */
#define MNTOPT_RO       "ro"            /* read only */
#define MNTOPT_RW       "rw"            /* read/write */
#define MNTOPT_NOUUID   "nouuid"	/* Ignore FS uuid */

STATIC int
xfs_parseargs(
	char		*options,
	int		flags,
	struct xfs_args	*args)
{
	char		*this_char, *value, *eov;
	int		logbufs = -1;
	int		logbufsize = -1;
	int		dsunit, dswidth, vol_dsunit, vol_dswidth;
	int		iosize;
	int		rval = 1;	/* failure is default */

	iosize = dsunit = dswidth = vol_dsunit = vol_dswidth = 0;
	memset(args, 0, sizeof(struct xfs_args));
	args->slcount = args->stimeout = args->ctimeout = -1;

	/* Copy the already-parsed mount(2) flags we're interested in */
	args->flags = flags & MS_RDONLY;
	if (flags & MS_NOATIME)
		args->flags |= XFSMNT_NOATIME;

	for (this_char = strtok (options, ",");
	     this_char != NULL;
	     this_char = strtok (NULL, ",")) {

		if ((value = strchr (this_char, '=')) != NULL)
			*value++ = 0;

		if (!strcmp(this_char, MNTOPT_LOGBUFS)) {
			if (!strcmp(value, "none")) {
				logbufs = 0;
				printk(
				"XFS: this FS is trash after writing to it\n");
			} else {
				logbufs = simple_strtoul(value, &eov, 10);
				if (logbufs < 2 || logbufs > 8) {
					printk(
					"XFS: Illegal logbufs: %d [not 2-8]\n",
						logbufs);
					return rval;
				}
			}
		} else if (!strcmp(this_char, MNTOPT_LOGBSIZE)) {
			logbufsize = simple_strtoul(value, &eov, 10);
			if (logbufsize != 16*1024 && logbufsize != 32*1024) {
				printk(
			"XFS: Illegal logbufsize: %d [not 16k or 32k]\n",
					logbufsize);
				return rval;
			}
		} else if (!strcmp(this_char, MNTOPT_LOGDEV)) {
			strncpy(args->logname, value, MAXNAMELEN);
		} else if (!strcmp(this_char, MNTOPT_DMAPI)) {
			args->flags |= XFSMNT_DMAPI;
		} else if (!strcmp(this_char, MNTOPT_XDSM)) {
			args->flags |= XFSMNT_DMAPI;
                } else if (!strcmp(this_char, MNTOPT_RTDEV)) {
			strncpy(args->rtname, value, MAXNAMELEN);
		} else if (!strcmp(this_char, MNTOPT_BIOSIZE)) {
			iosize = simple_strtoul(value, &eov, 10);
			if (iosize > 255 || iosize <= 0) {
				printk(
			"XFS: biosize value %d is out of bounds [0-255]\n",
					iosize);
				return rval;
			}
			args->flags |= XFSMNT_IOSIZE;
			args->iosizelog = (uint8_t) iosize;
		} else if (!strcmp(this_char, MNTOPT_WSYNC)) {
			args->flags |= XFSMNT_WSYNC;
		} else if (!strcmp(this_char, MNTOPT_NOATIME)) {
			args->flags |= XFSMNT_NOATIME;
		} else if (!strcmp(this_char, MNTOPT_OSYNCISDSYNC)) {
			args->flags |= XFSMNT_OSYNCISDSYNC;
		} else if (!strcmp(this_char, MNTOPT_NORECOVERY)) {
			args->flags |= XFSMNT_NORECOVERY;
		} else if (!strcmp(this_char, MNTOPT_INO64)) {
#ifdef XFS_BIG_FILESYSTEMS
			args->flags |= XFSMNT_INO64;
#else
			printk("XFS: ino64 option not allowed on this system\n");
			return rval;
#endif
		} else if (!strcmp(this_char, MNTOPT_UQUOTA)) {
			args->flags |= XFSMNT_UQUOTA | XFSMNT_UQUOTAENF;
		} else if (!strcmp(this_char, MNTOPT_QUOTA)) {
			args->flags |= XFSMNT_UQUOTA | XFSMNT_UQUOTAENF;
		} else if (!strcmp(this_char, MNTOPT_UQUOTANOENF)) {
			args->flags |= XFSMNT_UQUOTA;
			args->flags &= ~XFSMNT_UQUOTAENF;
		} else if (!strcmp(this_char, MNTOPT_QUOTANOENF)) {
			args->flags |= XFSMNT_UQUOTA;
			args->flags &= ~XFSMNT_UQUOTAENF;
		} else if (!strcmp(this_char, MNTOPT_MRQUOTA)) {
			args->flags |= XFSMNT_QUOTAMAYBE;
		} else if (!strcmp(this_char, MNTOPT_GQUOTA)) {
			args->flags |= XFSMNT_GQUOTA | XFSMNT_GQUOTAENF;
		} else if (!strcmp(this_char, MNTOPT_GQUOTANOENF)) {
			args->flags |= XFSMNT_GQUOTA;
			args->flags &= ~XFSMNT_GQUOTAENF;
		} else if (!strcmp(this_char, MNTOPT_NOALIGN)) {
			args->flags |= XFSMNT_NOALIGN;
		} else if (!strcmp(this_char, MNTOPT_SUNIT)) {
			dsunit = simple_strtoul(value, &eov, 10);
		} else if (!strcmp(this_char, MNTOPT_SWIDTH)) {
			dswidth = simple_strtoul(value, &eov, 10);
		} else if (!strcmp(this_char, MNTOPT_RO)) {
			args->flags |= MS_RDONLY;
		} else if (!strcmp(this_char, MNTOPT_NOSUID)) {
			args->flags |= MS_NOSUID;
		} else if (!strcmp(this_char, MNTOPT_NOUUID)) {
			args->flags |= XFSMNT_NOUUID; 
		} else {
			printk("XFS: unknown mount option [%s].\n", this_char);
			return rval;
                }
	}

	if (args->flags & XFSMNT_NORECOVERY) {
		if ((args->flags & MS_RDONLY) == 0) {
			printk("XFS: no-recovery mounts must be read-only.\n");
			return rval;
		}
	}

	if ((args->flags & XFSMNT_NOALIGN) && (dsunit || dswidth)) {
		printk(
	"XFS: sunit and swidth options incompatible with the noalign option\n");
		return rval;
	}

	if ((dsunit && !dswidth) || (!dsunit && dswidth)) {
		printk("XFS: sunit and swidth must be specified together\n");
		return rval;
	}

	if (dsunit && (dswidth % dsunit != 0)) {
		printk(
	"XFS: stripe width (%d) must be a multiple of the stripe unit (%d)\n",
			dswidth, dsunit);
		return rval;
	}

	if ((args->flags & XFSMNT_NOALIGN) != XFSMNT_NOALIGN) {
		if (dsunit) { 
			args->sunit = dsunit;
			args->flags |= XFSMNT_RETERR;
		} else
			args->sunit = vol_dsunit;	
		dswidth ? (args->swidth = dswidth) : 
			  (args->swidth = vol_dswidth);
	} else 
		args->sunit = args->swidth = 0;

	args->logbufs = logbufs;
	args->logbufsize = logbufsize;

	rval = cxfs_parseargs(options, flags, args);
	return rval;
}

/*
 * Convert one device special file to a dev_t.
 * Helper routine, used only by spectodevs below.
 */
STATIC int
spectodev(
	const char	*name,
	const char	*id,
	dev_t		*dev)
{
	struct nameidata nd;
	int		rval = 0;

	if (path_init(name, LOOKUP_FOLLOW, &nd))
		rval = path_walk(name, &nd);
	if (rval)
		printk("XFS: Invalid %s device [%s], err=%d\n", id, name, rval);
	else
		*dev = nd.dentry->d_inode->i_rdev;
	path_release(&nd);
	return rval;
}

/*
 * Convert device special files to dev_t for data, log, realtime.
 */
int
spectodevs(
	struct super_block *sb,
	struct xfs_args	*args,
	dev_t		*ddevp,
	dev_t		*logdevp,
	dev_t		*rtdevp)
{
	int		rval = 0;

	*ddevp = *logdevp = sb->s_dev;
	if (args->logname[0])
		rval = spectodev(args->logname, "log", logdevp);
	if (args->rtname[0] && !rval)
		rval = spectodev(args->rtname, "realtime", rtdevp);
	else
		*rtdevp = 0;
	return rval;
}

/*
 * Initialise a device forming part of a filessystem.
 * The "data" flag indicates if this is part of the data
 * volume - if so, we don't need to initialize a block
 * device reference as the VFS has already done this.
 *
 * In the other cases (external log, rt volume) we must
 * initialise the device because:
 *  - its device driver may not yet have been loaded
 *  - without opening the block device, the kernel has no
 *    knowledge that we are actually using the device, so
 *    the driver may choose to unload at any time, etc.
 */
int
linvfs_fill_buftarg(
	struct buftarg		*btp,
	dev_t			dev,
	struct super_block	*sb,
	int			data)
{
	int			rval;
	struct block_device	*bdev = NULL;

	if (!data) {
		bdev = bdget(dev);
		if (!bdev)
			return -ENOMEM;
		rval = blkdev_get(bdev, FMODE_READ|FMODE_WRITE, 0, BDEV_FS);
		if (rval) {
			printk("XFS: blkdev_get failed on device %d\n", dev);
			bdput(bdev);
			return rval;
		}
	}

	btp->bd_targ = bdev;
	btp->pb_targ = pagebuf_lock_enable(dev, sb);
	btp->dev = dev;
	return 0;
}

void
linvfs_release_buftarg(
	struct buftarg		*btp)
{
	struct pb_target	*target = btp->pb_targ;
	struct block_device	*bdev = btp->bd_targ;

	if (target) {
		pagebuf_delwri_flush(target, PBDF_WAIT, NULL);
		pagebuf_lock_disable(target);
	}
	if (bdev) {
		blkdev_put(bdev, BDEV_FS);
		bdput(bdev);
	}
}

struct super_block *
linvfs_read_super(
	struct super_block *sb,
	void		*data,
	int		silent)
{
	vfs_t		*vfsp;
	vfsops_t	*vfsops;
	vnode_t		*cvp, *rootvp;
	struct inode	*ip, *cip;
	struct mounta	ap;
	struct xfs_args	args;
	statvfs_t	statvfs;
	int		error;

	if (xfs_parseargs((char *)data, sb->s_flags, &args))
		return NULL;
	strncpy(args.fsname, bdevname(sb->s_dev), MAXNAMELEN);
	/* args.rtdev and args.logdev done in xfs_parseargs */

	/*  Setup the generic "mounta" structure  */
	memset(&ap, 0, sizeof(struct mounta));
	ap.dataptr = (char *)&args;
	ap.datalen = sizeof(struct xfs_args);
	ap.spec = args.fsname;

	/*  Kludge in XFS until we have other VFS/VNODE FSs  */
	vfsops = &xfs_vfsops;

	/*  Set up the vfs_t structure  */
	vfsp = vfs_allocate();
	if (!vfsp) 
		return NULL; 

	if (sb->s_flags & MS_RDONLY)
		vfsp->vfs_flag |= VFS_RDONLY;

	/*  Setup up the cvp structure  */

	cip = (struct inode *)kmem_alloc(sizeof(struct inode),0);
	if (!cip) { 
		vfs_deallocate(vfsp);
		return NULL;
	} 

	bzero(cip, sizeof(*cip));
	atomic_set(&cip->i_count, 1);

	cvp = LINVFS_GET_VN_ADDRESS(cip);
	cvp->v_type   = VDIR;
	cvp->v_number = 1;		/* Place holder */
	cvp->v_inode  = cip;

#ifdef CONFIG_XFS_VNODE_TRACING
	cvp->v_trace = ktrace_alloc(VNODE_TRACE_SIZE, KM_SLEEP);
	vn_trace_entry(cvp, "linvfs_read_super", (inst_t *)__return_address);
#endif /* CONFIG_XFS_VNODE_TRACING */

        cvp->v_flag |= VMOUNTING;
	vn_bhv_head_init(VN_BHV_HEAD(cvp), "vnode");	/* for DMAPI */
	LINVFS_SET_CVP(sb, cvp);
	vfsp->vfs_super = sb;

	sb->s_blocksize = 512;
	sb->s_blocksize_bits = ffs(sb->s_blocksize) - 1;
	set_blocksize(sb->s_dev, 512);
	set_posix_acl(sb);
	set_max_bytes(sb);
	set_quota_ops(sb);
	sb->s_op = &linvfs_sops;

	LINVFS_SET_VFS(sb, vfsp);

	VFSOPS_MOUNT(vfsops, vfsp, cvp, &ap, NULL, sys_cred, error);
	if (error)
		goto fail_vfsop;

	VFS_STATVFS(vfsp, &statvfs, NULL, error);
	if (error)
		goto fail_unmount;

	sb->s_magic = XFS_SB_MAGIC;
	sb->s_dirt = 1;

        VFS_ROOT(vfsp, &rootvp, error);
        if (error)
                goto fail_unmount;

	ip = LINVFS_GET_IP(rootvp);
	linvfs_set_inode_ops(ip);
	linvfs_revalidate_core(ip, ATTR_COMM);

	sb->s_root = d_alloc_root(ip);
	if (!sb->s_root)
		goto fail_vnrele;
	if (is_bad_inode((struct inode *)sb->s_root))
		goto fail_vnrele;

	/* Don't set the VFS_DMI flag until here because we don't want
	 * to send events while replaying the log.
	 */
	if (args.flags & XFSMNT_DMAPI)
		vfsp->vfs_flag |= VFS_DMI;

	vn_trace_exit(rootvp, "linvfs_read_super", (inst_t *)__return_address);

	return(sb);

fail_vnrele:
	if (sb->s_root) {
		dput(sb->s_root);
		sb->s_root = NULL;
	} else {
		VN_RELE(rootvp);
	}

fail_unmount:
	VFS_UNMOUNT(vfsp, 0, sys_cred, error);
	/*  We need to do something here to shut down the VNODE/VFS layer.  */

fail_vfsop:
	vfs_deallocate(vfsp);

#ifdef  CONFIG_XFS_VNODE_TRACING
	ktrace_free(cvp->v_trace);
	cvp->v_trace = NULL;
#endif  /* CONFIG_XFS_VNODE_TRACING */

	kfree(cvp->v_inode);
	return(NULL);
}

void
linvfs_set_inode_ops(
	struct inode	*inode)
{
	vnode_t		*vp;

	vp = LINVFS_GET_VN_ADDRESS(inode);

	inode->i_mode = VTTOIF(vp->v_type);

	if (vp->v_type == VNON) {
		make_bad_inode(inode);
	} else if (S_ISREG(inode->i_mode)) {
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
	} else {
		inode->i_op = &linvfs_file_inode_operations;
		init_special_inode(inode, inode->i_mode, inode->i_rdev);
	}
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

/*
 * We do not actually write the inode here, just mark the
 * super block dirty so that sync_supers calls us and
 * forces the flush.
 */
void
linvfs_write_inode(
	struct inode	*inode,
        int             sync)
{
	vnode_t	*vp = LINVFS_GET_VP(inode);

	if (vp) {
		vn_trace_entry(vp, "linvfs_write_inode",
					(inst_t *)__return_address);
	}
	inode->i_sb->s_dirt = 1;
}

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
		 * Do all our cleanup, and remove this vnode.
		 */
		vp->v_flag |= VPURGE;
		vn_remove(vp);
	}
}

void 
linvfs_put_inode(
	struct inode	*inode)
{
	vnode_t		*vp = LINVFS_GET_VP(inode);

    	if (vp) vn_put(vp);
}

void
linvfs_put_super(
	struct super_block *sb)
{
	int		error;
	int		sector_size = 512;
	kdev_t		dev = sb->s_dev;
	vfs_t 		*vfsp = LINVFS_GET_VFS(sb);
	vnode_t		*cvp;

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
}

void
linvfs_write_super(
	struct super_block *sb)
{
 	vfs_t		*vfsp = LINVFS_GET_VFS(sb); 
 	int		error; 

	sb->s_dirt = 0;
	if (sb->s_flags & MS_RDONLY)
		return;
	VFS_SYNC(vfsp, SYNC_FSDATA|SYNC_BDFLUSH|SYNC_NOWAIT|SYNC_ATTR,
		sys_cred, error);
}

int
linvfs_statfs(
	struct super_block *sb,
	struct statfs	*buf)
{
	vfs_t		*vfsp = LINVFS_GET_VFS(sb);
	statvfs_t	stat;
	int		error;

	VFS_STATVFS(vfsp, &stat, NULL, error);
	if (error)
		return(-error);

	buf->f_type = XFS_SB_MAGIC;
	buf->f_bsize = stat.f_bsize;
	buf->f_blocks = stat.f_blocks;
	buf->f_bfree = stat.f_bfree;
	buf->f_bavail = stat.f_bavail;
	buf->f_files = stat.f_files;
	buf->f_ffree = stat.f_ffree;
	buf->f_fsid.val[0] = stat.f_fsid;
	buf->f_fsid.val[1] = 0;
	buf->f_namelen = stat.f_namemax;

	return 0;
}

int
linvfs_remount(
	struct super_block *sb,
	int		*flags,
	char		*options)
{
	struct xfs_args	args;
	vfs_t		*vfsp;
	vnode_t		*cvp;

	vfsp = LINVFS_GET_VFS(sb);
	cvp = LINVFS_GET_CVP(sb);

	set_posix_acl(sb);

	if ((*flags & MS_RDONLY) == (sb->s_flags & MS_RDONLY))
		return 0;

	if (xfs_parseargs(options, *flags, &args))
                return -ENOSPC;

	if (*flags & MS_RDONLY || args.flags & MS_RDONLY) {
		sb->s_flags |= MS_RDONLY;
		XFS_log_write_unmount_ro(vfsp->vfs_fbhv);
		vfsp->vfs_flag |= VFS_RDONLY;
	} else {
		vfsp->vfs_flag &= ~VFS_RDONLY;
		sb->s_flags &= ~MS_RDONLY;
	}

	return 0;
}

void
linvfs_freeze_fs(
	struct super_block *sb)
{
	vfs_t		*vfsp;
	vnode_t		*vp;
	int		error;

	vfsp = LINVFS_GET_VFS(sb);
        if (sb->s_flags & MS_RDONLY)
                return;
	VFS_ROOT(vfsp, &vp, error);
	VOP_IOCTL(vp, LINVFS_GET_IP(vp), NULL, XFS_IOC_FREEZE, 0, error);
	VN_RELE(vp);
}

void
linvfs_unfreeze_fs(
	struct super_block *sb)
{
	vfs_t		*vfsp;
	vnode_t		*vp;
	int		error;

	vfsp = LINVFS_GET_VFS(sb);
	VFS_ROOT(vfsp, &vp, error);
	VOP_IOCTL(vp, LINVFS_GET_IP(vp), NULL, XFS_IOC_THAW, 0, error);
	VN_RELE(vp);
}

int
linvfs_dmapi_mount(
	struct super_block *sb,
	char		*dir_name)
{
	vfsops_t	*vfsops;
	char		fsname[MAXNAMELEN];
	vnode_t		*cvp;	/* covered vnode */
	vfs_t		*vfsp; /* mounted vfs */
	int		error;

	vfsp = LINVFS_GET_VFS(sb);
	if ( ! (vfsp->vfs_flag & VFS_DMI) )
		return 0;
	cvp = LINVFS_GET_CVP(sb);
	strncpy(fsname, bdevname(sb->s_dev), MAXNAMELEN);

	/*  Kludge in XFS until we have other VFS/VNODE FSs  */
	vfsops = &xfs_vfsops;

	VFSOPS_DMAPI_MOUNT(vfsops, vfsp, cvp, dir_name, fsname, error);
	if (error) {
		if (atomic_read(&sb->s_active) == 1)
			vfsp->vfs_flag &= ~VFS_DMI;
		return -error;
	}
	return 0;
}


static struct super_operations linvfs_sops = {
	read_inode:		linvfs_read_inode,
	write_inode:		linvfs_write_inode,
#ifdef CONFIG_XFS_DMAPI
	dmapi_mount_event:	linvfs_dmapi_mount,
#endif
	put_inode:		linvfs_put_inode,
	delete_inode:		linvfs_delete_inode,
	clear_inode:		linvfs_clear_inode,
	put_super:		linvfs_put_super,
	write_super:		linvfs_write_super,
	write_super_lockfs:	linvfs_freeze_fs,
	unlockfs:		linvfs_unfreeze_fs,
	statfs:			linvfs_statfs,
	remount_fs:		linvfs_remount
};

DECLARE_FSTYPE_DEV(xfs_fs_type, XFS_NAME, linvfs_read_super);

static int __init init_xfs_fs(void)
{
	struct sysinfo	si;
	static char message[] __initdata =
		KERN_INFO "SGI XFS with " XFS_BUILD_OPTIONS " enabled\n";

	si_meminfo(&si);
	xfs_physmem = si.totalram;

	printk(message);

	cred_init();
	vfsinit();
	xfs_init(0);
	xfs_grio_init();
	dmapi_init();

	return register_filesystem(&xfs_fs_type);
}


static void __exit exit_xfs_fs(void)
{
        dmapi_uninit();
        xfs_grio_uninit();
	xfs_cleanup();
        unregister_filesystem(&xfs_fs_type);
}

EXPORT_NO_SYMBOLS;

module_init(init_xfs_fs);
module_exit(exit_xfs_fs);

MODULE_AUTHOR("SGI <sgi.com>");
MODULE_DESCRIPTION("SGI XFS with " XFS_BUILD_OPTIONS " enabled");
MODULE_LICENSE("GPL");
