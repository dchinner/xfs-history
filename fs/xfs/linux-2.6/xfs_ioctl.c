
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
#include <xfs_log.h>
#include <xfs_trans.h>
#include <xfs_sb.h>
#include <xfs_mount.h>
#include <xfs_fsops.h>
#include <xfs_error.h>

#include <asm/uaccess.h> /* For copy_from_user */


int
xfs_getattr(
	bhv_desc_t	*bdp,
	vattr_t		*vap,
	int		flags,
	cred_t		*credp);

int
xfs_setattr(
	bhv_desc_t	*bdp,
	vattr_t		*vap,
	int		flags,
	cred_t		*credp);

int
xfs_set_dmattrs (
	bhv_desc_t	*bdp,
	u_int		evmask,
	u_int16_t	state,
	cred_t		*credp);

int
xfs_get_uiosize(
	xfs_mount_t	*mp,
	xfs_inode_t	*ip,
	struct biosize	*bs,
	cred_t		*credp);

int
xfs_set_uiosize(
	xfs_mount_t	*mp,
	xfs_inode_t	*ip,
	uint		flags,
	int		read_iosizelog,
	int		write_iosizelog,
	cred_t	*credp);

int
xfs_change_file_space(
	bhv_desc_t	*bdp,
	int		cmd,
	xfs_flock64_t	*bf,
	off_t           offset,
	cred_t		*credp,
	int		attr_flags);



int linvfs_ioctl (
	struct inode	*inode,
	struct file	*filp,
	unsigned int	cmd,
	unsigned long	arg)
{
	int		error;
	struct biosize	bs;
	struct dioattr	da;
	xfs_flock64_t	bf;
	struct fsdmidata dmi;
	struct fsxattr	fa;
	bhv_desc_t	*bdp;
	cred_t		cred;		/* Temporary cred workaround */
	vattr_t		va;
	vnode_t		*vp;
	xfs_fsop_geom_t	fsgeo;
	xfs_inode_t	*ip;
	xfs_mount_t	*mp;

	vp = LINVFS_GET_VP(inode);

	ASSERT(vp);

	vn_trace_entry(vp, "linvfs_ioctl", (inst_t *)__return_address);

	bdp = VNODE_TO_FIRST_BHV(vp);

	ASSERT(bdp);

	ip = XFS_BHVTOI(bdp);
	mp = ip->i_mount;

	if (XFS_FORCED_SHUTDOWN(mp))
		return (EIO);

printk("linvfs_ioctl: cmd/0x%x: inode/%p, filp/%p, arg/%lu, vp/%p, xip/%p\n",
cmd, inode, filp, arg, vp, ip);
	switch (cmd) {
	case XFS_IOC_ALLOCSP:
	case XFS_IOC_FREESP:

	case XFS_IOC_RESVSP:
	case XFS_IOC_UNRESVSP:

	case XFS_IOC_ALLOCSP64:
	case XFS_IOC_FREESP64:

	case XFS_IOC_RESVSP64:
	case XFS_IOC_UNRESVSP64: {
		int	attr_flags;

		if (filp->f_flags & O_RDONLY)
			return -XFS_ERROR(EBADF);

		if (vp->v_type != VREG)
			return -XFS_ERROR(EINVAL);

		if (copy_from_user(&bf, (xfs_flock64_t *)arg,
						sizeof(xfs_flock64_t)))
			return -XFS_ERROR(EFAULT);

		attr_flags = (filp->f_flags & (O_NDELAY|O_NONBLOCK))
						? ATTR_NONBLOCK : 0;

		/* if (filp->f_flags & FINVIS)	XXXjtk - need O_INVISIBLE? */
			attr_flags |= ATTR_DMI;

		error = xfs_change_file_space(bdp, cmd, &bf, filp->f_pos,
						      &cred, attr_flags);
		if (error)
			return -error;

		return 0;
	}

	case XFS_IOC_DIOINFO:
		da.d_miniosz = mp->m_sb.sb_blocksize;

		da.d_mem = 4096;

		/*
		 * this only really needs to be BBSIZE.
		 * it is set to the file system block size to
		 * avoid having to do block zeroing on short writes.
		 */
		da.d_maxiosz = XFS_FSB_TO_B(mp, XFS_B_TO_FSBT(mp, 1));

		return copy_to_user((struct dioattr *)arg, &da,
						sizeof(struct dioattr));

	case XFS_IOC_FSGEOMETRY:
		error = xfs_fs_geometry(mp, &fsgeo, 3);

		if (error)
			return -error;

		return copy_to_user((xfs_fsop_geom_t *)arg, &fsgeo,
						sizeof(xfs_fsop_geom_t));

	case XFS_IOC_FSGETXATTR:
		va.va_mask = AT_XFLAGS|AT_EXTSIZE|AT_NEXTENTS;

		error = xfs_getattr(bdp, &va, 0, &cred);

		if (error)
			return -error;

		fa.fsx_xflags   = va.va_xflags;
		fa.fsx_extsize  = va.va_extsize;
		fa.fsx_nextents = va.va_nextents;

		return copy_to_user((struct fsxattr *)arg, &fa,
						sizeof(struct fsxattr));

	case XFS_IOC_FSSETXATTR: {
		int	attr_flags;

		if (copy_from_user(&fa, (struct fsxattr *)arg,
						sizeof(struct fsxattr)))
			return -XFS_ERROR(EFAULT);

		va.va_mask = AT_XFLAGS | AT_EXTSIZE;

		va.va_xflags  = fa.fsx_xflags;
		va.va_extsize = fa.fsx_extsize;

		attr_flags = (filp->f_flags & (O_NDELAY|O_NONBLOCK) )
							? ATTR_NONBLOCK : 0;

		error = xfs_setattr(bdp, &va, attr_flags, &cred);

		if (error)
			return -error;

		return 0;
	}

	case XFS_IOC_FSGETXATTRA:
		va.va_mask = AT_XFLAGS|AT_EXTSIZE|AT_ANEXTENTS;

		error = xfs_getattr(bdp, &va, 0, &cred);

		if (error)
			return -error;

		fa.fsx_xflags   = va.va_xflags;
		fa.fsx_extsize  = va.va_extsize;
		fa.fsx_nextents = va.va_anextents;

		return copy_to_user((struct fsxattr *)arg, &fa,
						sizeof(struct fsxattr));

	case XFS_IOC_GETBIOSIZE:
		error = xfs_get_uiosize(mp, ip, &bs, &cred);

		if (error)
			return -error;

		return copy_to_user((struct biosize *)arg, &bs,
						sizeof(struct biosize));

	case XFS_IOC_SETBIOSIZE:
		if (copy_from_user(&bs, (struct biosize *)arg,
						sizeof(struct biosize)))
			return -XFS_ERROR(EFAULT);

		error = xfs_set_uiosize(mp, ip, bs.biosz_flags, bs.biosz_read,
						bs.biosz_write, &cred);
		if (error)
			return -error;

		return 0;

	case XFS_IOC_FSSETDM:
		if (copy_from_user(&dmi, (struct fsdmidata *)arg,
						sizeof(struct fsdmidata)))
			return -XFS_ERROR(EFAULT);

		error = xfs_set_dmattrs(bdp, dmi.fsd_dmevmask, dmi.fsd_dmstate,
							&cred);
		if (error)
			return -error;

		return 0;

	case XFS_IOC_GETBMAP:
	case XFS_IOC_GETBMAPA: {
		struct	getbmap	bm;
		int		iflags;

		if (copy_from_user(&bm, (struct getbmap *)arg,
						sizeof(struct getbmap)))
			return -XFS_ERROR(EFAULT);

		if (bm.bmv_count < 2)
			return -XFS_ERROR(EINVAL);

		iflags = (cmd == XFS_IOC_GETBMAPA ? BMV_IF_ATTRFORK : 0);

		/* if (filp->f_flags & FINVIS)	XXXjtk - need O_INVISIBLE? */
			iflags |= BMV_IF_NO_DMAPI_READ;

		error = xfs_getbmap(bdp, &bm,
					(struct getbmap *)arg + 1, iflags);

		if (error)
			return -error;

		return copy_to_user((struct getbmap *)arg, &bm, sizeof(bm));
	}

	case XFS_IOC_GETBMAPX: {
		struct	getbmapx	bmx;
		struct	getbmap		bm;
		int			iflags;


		if (copy_from_user(&bmx, (struct getbmapx *)arg,
						sizeof(struct getbmapx)))
			return -XFS_ERROR(EFAULT);

		if (bmx.bmv_count < 2)
			return -XFS_ERROR(EINVAL);

		/*
		 * Map input getbmapx structure to a getbmap
		 * structure for xfs_getbmap.
		 */
		GETBMAP_CONVERT(bmx, bm);

		iflags = bmx.bmv_iflags;

		if (iflags & (~BMV_IF_VALID))
			return -XFS_ERROR(EINVAL);

		iflags |= BMV_IF_EXTENDED;

		error = xfs_getbmap(bdp, &bm,
				(struct getbmapx *)arg + 1, iflags);
		if (error)
			return -error;

		GETBMAP_CONVERT(bm, bmx);

		return copy_to_user((struct getbmapx *)arg, &bmx, sizeof(bmx));
	}

#if 0	/* XXXjtk do these?? */
	case EXT2_IOC_GETVERSION:
		return put_user(inode->i_generation, (int *) arg);
	case EXT2_IOC_SETVERSION:
		if ((current->fsuid != inode->i_uid) && !capable(CAP_FOWNER))
			return -EPERM;
		if (IS_RDONLY(inode))
			return -EROFS;
		if (get_user(inode->i_generation, (int *) arg))
			return -EFAULT;	
		inode->i_ctime = CURRENT_TIME;
		mark_inode_dirty(inode);
		return 0;
#endif
	default:
		return -EINVAL;
	}
}
