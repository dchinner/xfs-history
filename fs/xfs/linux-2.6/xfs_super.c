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
#include <linux/smp_lock.h>
#include <linux/xfs_iops.h>
#include <linux/blkdev.h>
#include <linux/namei.h>
#include <linux/init.h>

/* xfs_vfs[ops].c */
extern int  xfs_init(int fstype);
extern void xfs_cleanup(void);

#ifdef CELL_CAPABLE
extern int cxfs_parseargs(char *, int, struct xfs_args *);
#else
# define cxfs_parseargs(opt,flag,xargs)	(0) /* success */
#endif

#ifdef CONFIG_FS_POSIX_ACL
# define set_posix_acl(sb)	((sb)->s_xattr_flags|= XATTR_MNT_FLAG_POSIX_ACL)
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
static struct quotactl_ops linvfs_qops = {
	get_xstate:		linvfs_getxstate,
	set_xstate:		linvfs_setxstate,
	get_xquota:		linvfs_getxquota,
	set_xquota:		linvfs_setxquota,
};
# define set_quota_ops(sb)	((sb)->s_qcop = &linvfs_qops)
#else
# define set_quota_ops(sb)	do { } while (0)
#endif

static struct super_operations linvfs_sops;
static struct export_operations linvfs_export_ops;

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
					   /* (this is now the default!) */
#define MNTOPT_OSYNCISOSYNC "osyncisosync" /* o_sync is REALLY o_sync */
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
#define MNTOPT_IRIXSGID "irixsgid"	/* Irix-style sgid inheritance */
#define MNTOPT_NOLOGFLUSH	"nologflush"	/* Don't use hard flushes in log writing */

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

	args->flags |= XFSMNT_32BITINODES;

	if (!options) {
		args->logbufs = logbufs;
		args->logbufsize = logbufsize;
		return 0;
	}
	
	while ((this_char = strsep (&options, ",")) != NULL) {
		if (!*this_char)
			continue;
		if ((value = strchr (this_char, '=')) != NULL)
			*value++ = 0;

		if (!strcmp(this_char, MNTOPT_LOGBUFS)) {
			if (!strcmp(value, "none")) {
				logbufs = 0;
				printk(
				"XFS: this FS is trash after writing to it\n");
			} else {
				logbufs = simple_strtoul(value, &eov, 10);
				if (logbufs < XLOG_NUM_ICLOGS ||
				    logbufs > XLOG_MAX_ICLOGS) {
					printk(
					"XFS: Illegal logbufs: %d [not %d-%d]\n",
						logbufs, XLOG_NUM_ICLOGS,
						XLOG_MAX_ICLOGS);
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
			/* no-op, this is now the default */
		} else if (!strcmp(this_char, MNTOPT_OSYNCISOSYNC)) {
			args->flags |= XFSMNT_OSYNCISOSYNC;
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
		} else if (!strcmp(this_char, MNTOPT_IRIXSGID)) {
			args->flags |= XFSMNT_IRIXSGID;
		} else if (!strcmp(this_char, MNTOPT_NOLOGFLUSH)) {
			args->flags |= XFSMNT_NOLOGFLUSH;
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
	kdev_t		*dev)
{
	struct nameidata nd;
	int	rval = path_lookup(name, LOOKUP_FOLLOW, &nd);

	if (!rval) {
		*dev = nd.dentry->d_inode->i_rdev;
		path_release(&nd);
	}
	return rval;
}

/*
 * Convert device special files to dev_t for data, log, realtime.
 */
int
spectodevs(
	struct super_block *sb,
	struct xfs_args	*args,
	kdev_t		*ddevp,
	kdev_t		*logdevp,
	kdev_t		*rtdevp)
{
	int		rval = 0;

	*ddevp = *logdevp = sb->s_dev;
	if (args->logname[0])
		rval = spectodev(args->logname, "log", logdevp);
	if (args->rtname[0] && !rval)
		rval = spectodev(args->rtname, "realtime", rtdevp);
	else
		*rtdevp = NODEV;
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
	kdev_t			dev,
	struct super_block	*sb,
	int			data)
{
	int			rval;
	struct block_device	*bdev = NULL;

	if (!data) {
		bdev = bdget(kdev_t_to_nr(dev));
		if (!bdev)
			return -ENOMEM;
		rval = blkdev_get(bdev, FMODE_READ|FMODE_WRITE, 0, BDEV_FS);
		if (rval) {
			printk("XFS: blkdev_get failed on device %d\n",
				kdev_t_to_nr(dev));
			bdput(bdev);
			return rval;
		}
	}

	btp->pb_targ = pagebuf_lock_enable(dev, sb);
	btp->bd_targ = bdev;
	btp->dev = dev;
	return 0;
}

void
linvfs_bsize_buftarg(
	struct buftarg		*btp,
	unsigned int		blocksize)
{
	pagebuf_target_blocksize(btp->pb_targ, blocksize);
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
	}
}

static kmem_cache_t * linvfs_inode_cachep;

static __inline__ unsigned int gfp_mask(void)
{
        /* If we're not in a transaction, FS activity is ok */
        if (current->flags & PF_FSTRANS) return GFP_NOFS;
	return GFP_KERNEL;
}


static struct inode *linvfs_alloc_inode(struct super_block *sb)
{
	vnode_t	*vp;

	vp = (vnode_t *)kmem_cache_alloc(linvfs_inode_cachep, gfp_mask());
	if (!vp)
		return NULL;
	return LINVFS_GET_IP(vp);
}

static void linvfs_destroy_inode(struct inode *inode)
{
	kmem_cache_free(linvfs_inode_cachep, LINVFS_GET_VP(inode));
}

static void init_once(void * foo, kmem_cache_t * cachep, unsigned long flags)
{
	vnode_t *vp = (vnode_t *)foo;
	if ((flags & (SLAB_CTOR_VERIFY|SLAB_CTOR_CONSTRUCTOR)) ==
	    SLAB_CTOR_CONSTRUCTOR)
		inode_init_once(LINVFS_GET_IP(vp));
}

static int init_inodecache(void)
{
	linvfs_inode_cachep = kmem_cache_create("linvfs_icache",
				sizeof(vnode_t), 0, SLAB_HWCACHE_ALIGN,
				init_once, NULL);

	if (linvfs_inode_cachep == NULL)
		return -ENOMEM;
	return 0;
}

static void destroy_inodecache(void)
{
	if (kmem_cache_destroy(linvfs_inode_cachep))
		printk(KERN_INFO "linvfs_inode_cache: not all structures were freed\n");
}

static int
linvfs_fill_super(
	struct super_block *sb,
	void		*data,
	int		silent)
{
	vfs_t		*vfsp;
	vfsops_t	*vfsops;
	vnode_t		*rootvp;
	struct inode	*ip;
	struct mounta	ap;
	struct xfs_args	args;
	struct statfs	statvfs;
	int		error;

	if (xfs_parseargs((char *)data, sb->s_flags, &args))
		return  -EINVAL;
	strncpy(args.fsname, sb->s_id, MAXNAMELEN);
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
		return  -EINVAL; 

	if (sb->s_flags & MS_RDONLY)
		vfsp->vfs_flag |= VFS_RDONLY;

	vfsp->vfs_super = sb;
	set_blocksize(sb->s_bdev, BBSIZE);
	set_posix_acl(sb);
	sb->s_xattr_flags |= XATTR_MNT_FLAG_USER;
	set_max_bytes(sb);
	set_quota_ops(sb);
	sb->s_op = &linvfs_sops;
	sb->s_export_op = &linvfs_export_ops;

	LINVFS_SET_VFS(sb, vfsp);

	VFSOPS_MOUNT(vfsops, vfsp, NULL, &ap, NULL, sys_cred, error);
	if (error)
		goto fail_vfsop;

	VFS_STATVFS(vfsp, &statvfs, NULL, error);
	if (error)
		goto fail_unmount;

	sb->s_magic = XFS_SB_MAGIC;
	sb->s_dirt = 1;
	sb->s_blocksize = statvfs.f_bsize;
	sb->s_blocksize_bits = ffs(statvfs.f_bsize) - 1;

        VFS_ROOT(vfsp, &rootvp, error);
        if (error)
                goto fail_unmount;

	ip = LINVFS_GET_IP(rootvp);
	linvfs_revalidate_core(ip, ATTR_COMM);

	sb->s_root = d_alloc_root(ip);
	if (!sb->s_root)
		goto fail_vnrele;
	if (is_bad_inode(sb->s_root->d_inode))
		goto fail_vnrele;

	/* Don't set the VFS_DMI flag until here because we don't want
	 * to send events while replaying the log.
	 */
	if (args.flags & XFSMNT_DMAPI)
		vfsp->vfs_flag |= VFS_DMI;

	vn_trace_exit(rootvp, "linvfs_read_super", (inst_t *)__return_address);

	return(0);

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
}

void
linvfs_set_inode_ops(
	struct inode	*inode)
{
	vnode_t		*vp;

	vp = LINVFS_GET_VPTR(inode);

	inode->i_mode = VTTOIF(vp->v_type);

	/* If this isn't a new inode, nothing to do */
	if (!(inode->i_state & I_NEW))
		return;

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
		init_special_inode(inode, inode->i_mode,
					kdev_t_to_nr(inode->i_rdev));
	}

	unlock_new_inode(inode);
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
linvfs_clear_inode(
	struct inode	*inode)
{
	vnode_t		*vp = LINVFS_GET_VP(inode);

	if (vp) {
		vn_rele(vp);
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
	struct inode	*ip)
{
	vnode_t		*vp = LINVFS_GET_VP(ip);
	int		error;

    	if (vp && vp->v_fbhv && (atomic_read(&ip->i_count) == 1))
		VOP_RELEASE(vp, error);
}

void
linvfs_put_super(
	struct super_block *sb)
{
	int		error;
	int		sector_size;
	vfs_t 		*vfsp = LINVFS_GET_VFS(sb);

	VFS_DOUNMOUNT(vfsp, 0, NULL, sys_cred, error); 
	if (error) {
		printk("XFS unmount got error %d\n", error);
		printk("linvfs_put_super: vfsp/0x%p left dangling!\n", vfsp);
		return;
	}

	vfs_deallocate(vfsp);

	/* Reset device block size */
	sector_size = bdev_hardsect_size(sb->s_bdev);
	set_blocksize(sb->s_bdev, sector_size);
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
	struct statfs	*statp)
{
	vfs_t		*vfsp = LINVFS_GET_VFS(sb);
	int		error;

	VFS_STATVFS(vfsp, statp, NULL, error);

	return error;
}

int
linvfs_remount(
	struct super_block *sb,
	int		*flags,
	char		*options)
{
	struct xfs_args	args;
	vfs_t		*vfsp;

	vfsp = LINVFS_GET_VFS(sb);

	set_posix_acl(sb);
	sb->s_xattr_flags |= XATTR_MNT_FLAG_USER;

	if ((*flags & MS_RDONLY) == (sb->s_flags & MS_RDONLY))
		return 0;

	if (xfs_parseargs(options, *flags, &args))
                return -EINVAL;

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
	struct vfsmount *mnt,
	char		*dir_name)
{
	struct super_block *sb = mnt->mnt_sb;
	vfsops_t	*vfsops;
	vfs_t		*vfsp; /* mounted vfs */
	int		error;

	vfsp = LINVFS_GET_VFS(sb);
	if ( ! (vfsp->vfs_flag & VFS_DMI) )
		return 0;

	/*  Kludge in XFS until we have other VFS/VNODE FSs  */
	vfsops = &xfs_vfsops;

	VFSOPS_DMAPI_MOUNT(vfsops, vfsp, NULL, dir_name, sb->s_id, mnt, error);
	if (error) {
		if (atomic_read(&sb->s_active) == 1)
			vfsp->vfs_flag &= ~VFS_DMI;
		return -error;
	}
	return 0;
}

struct dentry *linvfs_get_parent(struct dentry *child)
{
	int		error;
	vnode_t		*vp, *cvp;
	struct dentry	*parent;
	struct inode	*ip = NULL;
	struct dentry dotdot;

	dotdot.d_name.name = "..";
	dotdot.d_name.len = 2;
	dotdot.d_inode = 0;

	cvp = NULL;
	vp = LINVFS_GET_VP(child->d_inode);
	VOP_LOOKUP(vp, &dotdot, &cvp, 0, NULL, NULL, error);

	if (!error) {
		ASSERT(cvp);
		ip = LINVFS_GET_IP(cvp);
		if (!ip) {
			VN_RELE(cvp);
			return ERR_PTR(-EACCES);
		}
		error = -linvfs_revalidate_core(ip, ATTR_COMM);
	}
	if (error)
		return ERR_PTR(-error);
	parent = d_alloc_anon(ip);
	if (!parent) {
		VN_RELE(cvp);
		parent = ERR_PTR(-ENOMEM);
	}
	return parent;
}

static struct export_operations linvfs_export_ops = {
	get_parent: linvfs_get_parent,
};

static struct super_operations linvfs_sops = {
	alloc_inode:		linvfs_alloc_inode,
	destroy_inode:		linvfs_destroy_inode,
	write_inode:		linvfs_write_inode,
#ifdef CONFIG_HAVE_XFS_DMAPI
	dmapi_mount_event:	linvfs_dmapi_mount,
#endif
	put_inode:		linvfs_put_inode,
	clear_inode:		linvfs_clear_inode,
	put_super:		linvfs_put_super,
	write_super:		linvfs_write_super,
	write_super_lockfs:	linvfs_freeze_fs,
	unlockfs:		linvfs_unfreeze_fs,
	statfs:			linvfs_statfs,
	remount_fs:		linvfs_remount,
};

static struct super_block *linvfs_get_sb(struct file_system_type *fs_type,
	int flags, char *dev_name, void *data)
{
	return get_sb_bdev(fs_type, flags, dev_name, data, linvfs_fill_super);
}

static struct file_system_type xfs_fs_type = {
	owner:		THIS_MODULE,
	name:		"xfs",
	get_sb:		linvfs_get_sb,
	kill_sb:	kill_block_super,
	fs_flags:	FS_REQUIRES_DEV,
};

static int __init init_xfs_fs(void)
{
	int error;
	struct sysinfo	si;
	static char message[] __initdata =
		KERN_INFO "SGI XFS with " XFS_BUILD_OPTIONS " enabled\n";

	error = init_inodecache();
	if (error < 0)
		return error;

	error = pagebuf_init();
	if (error < 0)
		goto out;

	si_meminfo(&si);
	xfs_physmem = si.totalram;

	printk(message);

	cred_init();
	vn_init();
	xfs_init(0);

	error = register_filesystem(&xfs_fs_type);
	if (error)
		goto out;
	return 0;

out:
	destroy_inodecache();
	return error;
}


static void __exit exit_xfs_fs(void)
{
	xfs_cleanup();
        unregister_filesystem(&xfs_fs_type);
	pagebuf_terminate();
	destroy_inodecache();
}

EXPORT_NO_SYMBOLS;

module_init(init_xfs_fs);
module_exit(exit_xfs_fs);

MODULE_AUTHOR("SGI <sgi.com>");
MODULE_DESCRIPTION("SGI XFS with " XFS_BUILD_OPTIONS " enabled");
MODULE_LICENSE("GPL");
