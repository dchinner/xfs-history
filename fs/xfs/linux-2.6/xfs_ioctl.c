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

#define FSID_T
#include <sys/types.h>
#include <linux/errno.h>

#undef  NODEV
#include <linux/version.h>
#include <linux/fs.h>
#include <linux/sched.h>	/* To get current */
#include <linux/locks.h>
#include <linux/slab.h>

#include <sys/sysmacros.h>
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
#include <xfs_itable.h>
#include <xfs_iops.h>
#include <linux/dmapi_kern.h>
#include <xfs_dmapi.h>
#include <xfs_dfrag.h>

#include <linux/smp_lock.h>

#include <linux/dcache.h>
#include <linux/file.h>
#include <linux/stat.h>
#include <linux/xfs_fs.h>

#include <ksys/vfile.h>

#include <asm/uaccess.h> /* For copy_from_user */

#include <sys/uuid.h>

#define BREAKPOINT() asm("   int $3");


/*
 * The "open_by_handle() & readlink_by_handle() ioctl's
 * require 'root', or 'SysAdmin' capabilies.
 * To change to a simple 'permission' test,
 * uncomment the following #define.
 */
/* #define	PERMISSIONS_BY_USER	*/


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
	xfs_off_t       offset,
	cred_t		*credp,
	int		attr_flags);


/*
 * xfs_find_handle: map from userspace xfs_fsop_handlereq structure to
 * a file or fs handle - used for XFS_IOC_PATH_TO_FSHANDLE,
 * XFS_IOC_PATH_TO_HANDLE and XFS_IOC_FD_TO_HANDLE.
 */

int
xfs_find_handle(
        unsigned int    cmd,
	unsigned long	arg)
{
	int			hsize;
	xfs_handle_t		handle;
	xfs_fsop_handlereq_t	hreq;
        struct inode            *inode;
        struct vnode            *vp;
        int                     only_fsid;

        /* read handle request from user */
	if (copy_from_user(&hreq, (struct xfs_fsop_handlereq *)arg,
					sizeof(struct xfs_fsop_handlereq)))
		return -XFS_ERROR(EFAULT);

        /* zero our handle */
	bzero((char *)&handle, sizeof(handle));
        
        /* now we need the inode in question */
        only_fsid=0;
        switch (cmd) {
	    case XFS_IOC_PATH_TO_FSHANDLE:
                only_fsid=1;
                /* fallthrough */
	    case XFS_IOC_PATH_TO_HANDLE: {
                struct nameidata        nd;
                char                    *path;
                int                     error;
                
                /* we need the path */
                path = getname(hreq.path);
                if (IS_ERR(path))
                        return PTR_ERR(path);
                
                /* traverse the path */
                error = 0;
                if (path_init(path, LOOKUP_POSITIVE, &nd))
                        error = path_walk(path, &nd);
                putname(path);
                if (error)
                        return error;

                /* XXX dxm - is locking here ok? */                
                ASSERT(nd.dentry);
                ASSERT(nd.dentry->d_inode);
                inode = igrab(nd.dentry->d_inode);
                path_release(&nd);
                
                break;
            }
                
	    case XFS_IOC_FD_TO_HANDLE: {
                struct file     * file;
                
                file = fget(hreq.fd);
                if (!file)
                    return -EBADF;
                
                /* XXX dxm - is locking here ok? */
                ASSERT(file->f_dentry);
                ASSERT(file->f_dentry->d_inode);
                inode = igrab(file->f_dentry->d_inode);
                fput(file);
                
                break;
            }
                
            default:
                ASSERT(0);
                return -XFS_ERROR(EINVAL);
        }
        
        /* we need the vnode */
	vp = LINVFS_GET_VP(inode);
        if (!vp || !vp->v_vfsp->vfs_altfsid) {
            /* we're not in XFS anymore, Toto */
            iput(inode);
            return -XFS_ERROR(EINVAL);
        }
        if (vp->v_type != VREG && vp->v_type != VDIR && vp->v_type != VLNK) {
            iput(inode);
            return -XFS_ERROR(EBADF);
        }
        
        /* now we can grab the fsid */
        memcpy(&handle.ha_fsid, vp->v_vfsp->vfs_altfsid, sizeof(xfs_fsid_t));
        hsize = sizeof(xfs_fsid_t);
        
        if (!only_fsid) {
            xfs_inode_t             *ip;
            int                     error;
	    bhv_desc_t	            *bhv;
            int                     lock_mode;
            
            /* we need to get access to the xfs_inode to read the generation */
            
            VN_BHV_READ_LOCK(&(vp)->v_bh);
            bhv = VNODE_TO_FIRST_BHV(vp);
            ASSERT(bhv);
            ip = XFS_BHVTOI(bhv);
            ASSERT(ip);
            lock_mode = xfs_ilock_map_shared(ip);
            
	    /* fill in fid section of handle from inode */

	    handle.ha_fid.xfs_fid_len = sizeof(xfs_fid_t) - 
                                            sizeof(handle.ha_fid.xfs_fid_len);
	    handle.ha_fid.xfs_fid_pad = 0;
	    handle.ha_fid.xfs_fid_gen = ip->i_d.di_gen;
            handle.ha_fid.xfs_fid_ino = ip->i_ino;
            
            xfs_iunlock_map_shared(ip, lock_mode);
            VN_BHV_READ_UNLOCK(&(vp)->v_bh);
            
            hsize = XFS_HSIZE(handle);
        }
        
        /* now copy our handle into the user buffer & write out the size */
	if (copy_to_user((xfs_handle_t *)hreq.ohandle, &handle, hsize) ||
	    copy_to_user(hreq.ohandlen, &hsize, sizeof(__s32))) {
                iput(inode);
		return -XFS_ERROR(EFAULT);
        }
        
        iput(inode);
	return 0;
}


int
xfs_open_by_handle(
	unsigned int	cmd,
	unsigned long	arg,
	struct file	*parfilp,
	struct inode	*parinode,
	vfs_t		*vfsp,
	xfs_mount_t	*mp)
{
	int			i;
	int			error;
	int			klocked = 0;
	int			newfd = -1;
	int			permflag;
	char			sname[(2 * MAXFIDSZ) + 1];
	char			*cpi;
	char			*cpo;
	__u32			igen;
	struct dentry		*dentry = NULL;
	struct dentry		*pardentry;
	struct file		*filp;
	struct inode		*inode = NULL;
	struct qstr		sqstr;
	void			*hanp;
	size_t			hlen;
	vnode_t			*vp = NULL;
	xfs_fid_t		*xfid;
	xfs_handle_t		*handlep;
	xfs_handle_t		handle;
	xfs_ino_t		ino;
	xfs_inode_t		*ip = NULL;
	xfs_fsop_handlereq_t	hreq;


#ifndef	PERMISSIONS_BY_USER
	/*
	 * Only allow Sys Admin capable users.
	 */
	if (! capable(CAP_SYS_ADMIN))
		return -EPERM;

#endif	/* ! PERMISSIONS_BY_USER */

	/*
	 * Only allow handle opens under a directory.
	 */
	if ( !S_ISDIR(parinode->i_mode) )
		return -ENOTDIR;

	/*
	 * Copy the handle down from the user and validate
	 * that it looks to be in the correct format.
	 */
	if (copy_from_user(&hreq, (struct xfs_fsop_handlereq *)arg,
					sizeof(struct xfs_fsop_handlereq)))
		return -XFS_ERROR(EFAULT);

	hanp = hreq.ihandle;
	hlen = hreq.ihandlen;

	handlep = &handle;

	/*
	 * gethandle(hanp, hlen, &handle)
	 */
	if (hlen < sizeof(handlep->ha_fsid) || hlen > sizeof(*handlep))
		return -XFS_ERROR(EINVAL);

	if (copy_from_user(handlep, hanp, hlen))
		return -XFS_ERROR(EFAULT);

	if (hlen < sizeof(*handlep))
		bzero(((char *)handlep) + hlen,
					sizeof(*handlep) - hlen);

	if (hlen > sizeof(handlep->ha_fsid)) {

		if (   handlep->ha_fid.xfs_fid_len !=
				(hlen - sizeof(handlep->ha_fsid)
					- sizeof(handlep->ha_fid.xfs_fid_len))
		    || handlep->ha_fid.xfs_fid_pad) {

			return -XFS_ERROR(EINVAL);
		}
	}


	/*
	 * Crack the handle, obtain the inode # & generation #
	 */

	/*
	 * handle_to_vp(&handle)
	 * VFS_VGET (vfsp, &vp, &handlep->ha_fid, error)
	 *	     bdp, **vp, fidp;
	 */
	xfid = (struct xfs_fid *)&handlep->ha_fid;

	if (xfid->xfs_fid_len == sizeof(*xfid) - sizeof(xfid->xfs_fid_len)) {
		ino  = xfid->xfs_fid_ino;
		igen = xfid->xfs_fid_gen;
	} else {
		/*
		 * Invalid.  Since handles can be created in user
		 * space and passed in, this is not cause for a panic.
		 */
		return -XFS_ERROR(EINVAL);
	}


	/*
	 * Make a unique name for dcache.
	 */
	cpi = (char *)handlep;
	cpo = sname;

	for (i = 0; i < hlen && i < MAXFIDSZ; i++) {
		sprintf(cpo, "%02x", *cpi);

		cpi++;
		cpo += 2;
	}

	*cpo = '\0';

	/*
	 * Obtain/create a 'dentry'.
	 */
	sqstr.name = sname;
	sqstr.len  = 2 * hlen;
	sqstr.hash = full_name_hash(sname, hlen);

	pardentry = parfilp->f_dentry;


	down(&parinode->i_sem);

	dentry = d_lookup(pardentry, &sqstr);	/* There already? */
	if (!dentry) {
		dentry = d_alloc(pardentry, &sqstr);

		if (! dentry) {
			up(&parinode->i_sem);
			return -ENOMEM;
		}
        }

	up(&parinode->i_sem);
	inode = dentry->d_inode;

	/*
	 * Get the XFS inode, building a vnode to go with it 
	 */
	error = xfs_iget(mp, NULL, inode?inode->i_ino:ino, XFS_ILOCK_SHARED, &ip, 0);

	if (error) {
		error = -error;

		goto cleanup_dentry;
	}

	if (ip == NULL) {
		error = -XFS_ERROR(EIO);

		goto cleanup_dentry;
	}

	if (ip->i_d.di_mode == 0 || ip->i_d.di_gen != igen) {

		xfs_iput(ip, XFS_ILOCK_SHARED);

		error = -XFS_ERROR(ENOENT);

		goto cleanup_dentry;
	}

	vp = XFS_ITOV(ip);

	xfs_iunlock(ip, XFS_ILOCK_SHARED);

	if (inode == NULL) {
		inode = igrab(vp->v_inode);

		if (! inode) {
			error = -EACCES;

			VN_RELE(vp);

			goto cleanup_dentry;
		}

		/*
		 * Set xfs inode ops.
		 */
		linvfs_set_inode_ops(inode);
		d_add(dentry, inode);
	}

	/*
	 * Restrict handle operations to directories & regular files.
	 */
	if (! (S_ISREG(inode->i_mode) || S_ISDIR(inode->i_mode)) ) {

		error = -XFS_ERROR(EINVAL);

		goto cleanup_dentry;
	}

	/*
	 * Get a file descriptor # to use.
	 */
	lock_kernel();

	klocked = 1;

	newfd = get_unused_fd();

	if (newfd < 0) {
		error = newfd;

		goto cleanup_dentry;
	}

	/*
	 * Get a file table entry.
	 */
	filp = get_empty_filp();

	if (! filp) {
		error = -ENFILE;

		goto cleanup_fd;
	}

	filp->f_flags = hreq.oflags;

	filp->f_mode = (hreq.oflags + 1) & O_ACCMODE;

#ifdef	PERMISSIONS_BY_USER
	/*
	 * Do permission/write checks
	 */
	permflag = 0;

	if (filp->f_mode & FMODE_READ)
		permflag |= MAY_READ;

	if (filp->f_mode & FMODE_WRITE)
		permflag |= MAY_WRITE;

	if (error = permission(inode, permflag))
		goto cleanup_file;

#endif	/* PERMISSIONS_BY_USER */

	if (filp->f_mode & FMODE_WRITE) {

		if (vp->v_type == VDIR) {	/* Can't write directories */
			error = -EISDIR;

			goto cleanup_file;
		}

		error = get_write_access(inode);

		if (error)
			goto cleanup_file;
	}


	/*
	 * Stitch it all together.
	 */
	filp->f_dentry = dentry;
	filp->f_pos    = 0;
	filp->f_reada  = 0;
	filp->f_op     = inode->i_fop;

	
	
	if (inode->i_sb)
		file_move(filp, &inode->i_sb->s_files);

	if (filp->f_op && filp->f_op->open) {

		error = filp->f_op->open(inode, filp);

		if (error)
			goto cleanup_all;
	}

	filp->f_flags &= ~(O_CREAT | O_EXCL | O_NOCTTY | O_TRUNC);


	if (klocked)
		unlock_kernel();
	klocked = 0;

	fd_install(newfd, filp);
        
        iput(inode);

	return newfd;


cleanup_all:
	if (filp->f_mode & FMODE_WRITE)
		put_write_access(inode);

cleanup_file:
	filp->f_dentry = NULL;
	put_filp(filp);

cleanup_fd:
	if (newfd >= 0)
		put_unused_fd(newfd);

cleanup_dentry:
        if (inode)
            iput(inode);
	dput(dentry);

	if (klocked)
		unlock_kernel();

	return error;
}


int
xfs_readlink_by_handle(
	unsigned int	cmd,
	unsigned long	arg,
	struct file	*parfilp,
	struct inode	*parinode,
	vfs_t		*vfsp,
	xfs_mount_t	*mp)
{
	int			i;
	int			error;
	int			rlsize;
	__u32			igen;
	struct iovec		aiov;
	struct uio		auio;
	void			*hanp;
	size_t			hlen;
	vnode_t			*vp = NULL;
	xfs_fid_t		*xfid;
	xfs_handle_t		*handlep;
	xfs_handle_t		handle;
	xfs_ino_t		ino;
	xfs_inode_t		*ip = NULL;
	xfs_fsop_handlereq_t	hreq;
        __u32                   olen;

#ifndef	PERMISSIONS_BY_USER
	/*
	 * Only allow Sys Admin capable users.
	 */
	if (! capable(CAP_SYS_ADMIN))
		return -EPERM;

#endif	/* ! PERMISSIONS_BY_USER */

	/*
	 * Only allow handle opens under a directory.
	 */
	if ( !S_ISDIR(parinode->i_mode) )
		return -ENOTDIR;

	/*
	 * Copy the handle down from the user and validate
	 * that it looks to be in the correct format.
	 */
	if (copy_from_user(&hreq, (struct xfs_fsop_handlereq *)arg,
					sizeof(struct xfs_fsop_handlereq)))
		return -XFS_ERROR(EFAULT);

	hanp = hreq.ihandle;
	hlen = hreq.ihandlen;

	handlep = &handle;

	/*
	 * gethandle(hanp, hlen, &handle)
	 */
	if (hlen < sizeof(handlep->ha_fsid) || hlen > sizeof(*handlep))
		return -XFS_ERROR(EINVAL);

	if (copy_from_user(handlep, hanp, hlen))
		return -XFS_ERROR(EFAULT);

	if (hlen < sizeof(*handlep))
		bzero(((char *)handlep) + hlen,
					sizeof(*handlep) - hlen);

	if (hlen > sizeof(handlep->ha_fsid)) {

		if (   handlep->ha_fid.xfs_fid_len !=
				(hlen - sizeof(handlep->ha_fsid)
					- sizeof(handlep->ha_fid.xfs_fid_len))
		    || handlep->ha_fid.xfs_fid_pad) {

			return -XFS_ERROR(EINVAL);
		}
	}


	/*
	 * Crack the handle, obtain the inode # & generation #
	 */

	/*
	 * handle_to_vp(&handle)
	 * VFS_VGET (vfsp, &vp, &handlep->ha_fid, error)
	 *	     bdp, **vp, fidp;
	 */
	xfid = (struct xfs_fid *)&handlep->ha_fid;

	if (xfid->xfs_fid_len == sizeof(*xfid) - sizeof(xfid->xfs_fid_len)) {
		ino  = xfid->xfs_fid_ino;
		igen = xfid->xfs_fid_gen;
	} else {
		/*
		 * Invalid.  Since handles can be created in user
		 * space and passed in via gethandle(), this is not
		 * cause for a panic.
		 */
		return -XFS_ERROR(EINVAL);
	}


	/*
	 * Get the XFS inode, building a vnode to go with it.
	 */
	error = xfs_iget(mp, NULL, ino, XFS_ILOCK_SHARED, &ip, 0);

	if (error)
		return -error;

	if (ip == NULL)
		return -XFS_ERROR(EIO);

	if (ip->i_d.di_mode == 0 || ip->i_d.di_gen != igen) {

		xfs_iput(ip, XFS_ILOCK_SHARED);

		return -XFS_ERROR(ENOENT);
	}

	vp = XFS_ITOV(ip);

	xfs_iunlock(ip, XFS_ILOCK_SHARED);


	/*
	 * Restrict handle operations to symlinks.
	 */
	if (vp->v_type != VLNK) {
			VN_RELE(vp);

			return -XFS_ERROR(EINVAL);
	}

	aiov.iov_base	= hreq.ohandle;
        
        
	if (copy_from_user(&olen, hreq.ohandlen, sizeof(__u32)))
            return -XFS_ERROR(EFAULT);
	aiov.iov_len	= olen;

	auio.uio_iov	= &aiov;
	auio.uio_iovcnt	= 1;
	auio.uio_fmode	= FINVIS;
	auio.uio_offset	= 0;
	auio.uio_segflg	= UIO_USERSPACE;
	auio.uio_resid	= olen;

	VOP_READLINK(vp, &auio, get_current_cred(), error);

	VN_RELE(vp);

	rlsize = olen - auio.uio_resid;

	return rlsize;
}


int
xfs_ioctl(
	bhv_desc_t	*bdp,
	struct inode	*inode,
	struct file	*filp,
	unsigned int	cmd,
	unsigned long	arg)
{
	int		error;
	struct biosize	bs;
	struct dioattr	da;
	struct fsdmidata dmi;
	struct fsxattr	fa;
	cred_t		cred;		/* Temporary cred workaround */
	vattr_t		va;
	vnode_t		*vp;
	vfs_t		*vfsp;
	xfs_fsop_geom_t	fsgeo;
	xfs_flock64_t	bf;
	xfs_inode_t	*ip;
	xfs_mount_t	*mp;

	vp = LINVFS_GET_VP(inode);

	ASSERT(vp);

	vn_trace_entry(vp, "xfs_ioctl", (inst_t *)__return_address);

	ip = XFS_BHVTOI(bdp);
	mp = ip->i_mount;

	vfsp = LINVFS_GET_VFS(inode->i_sb);

	ASSERT(vfsp);

	if (XFS_FORCED_SHUTDOWN(mp))
		return (EIO);


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

	case XFS_IOC_FSBULKSTAT_SINGLE:
	case XFS_IOC_FSBULKSTAT: {
		int		count;		/* # of records returned */
		xfs_ino_t	inlast;		/* last inode number */
		int		done;		/* = 1 if there are more
						 * stats to get and if
						 * bulkstat should be called
						 * again.
						 * This is unused in syssgi
						 * but used in dmi */
		xfs_fsop_bulkreq_t	bulkreq;


		if (copy_from_user(&bulkreq, (xfs_fsop_bulkreq_t *)arg,
						sizeof(xfs_fsop_bulkreq_t)))
			return -XFS_ERROR(EFAULT);

		if (copy_from_user(&inlast, (__s64 *)bulkreq.lastip,
							sizeof(__s64)))
			return -XFS_ERROR(EFAULT);

		if ((count = bulkreq.icount) <= 0)
			return -XFS_ERROR(EINVAL);

		if (cmd == XFS_IOC_FSBULKSTAT_SINGLE) {
			error = xfs_bulkstat_single(mp, &inlast,
						bulkreq.ubuffer, &done);
		} else {
			if (count == 1 && inlast != 0) {
				inlast++;

				error = xfs_bulkstat_single(mp, &inlast,
						bulkreq.ubuffer, &done);
			} else {
				error = xfs_bulkstat(mp, NULL, &inlast, &count,
					(bulkstat_one_pf)xfs_bulkstat_one, 
					sizeof(xfs_bstat_t), bulkreq.ubuffer, 
					BULKSTAT_FG_QUICK, &done);
			}
		}

		if (error)
			return -error;

		if (bulkreq.ocount != NULL) {
			if (copy_to_user((xfs_ino_t *)bulkreq.lastip, &inlast,
							sizeof(xfs_ino_t)))
				return -XFS_ERROR(EFAULT);

			if (copy_to_user((__s32 *)bulkreq.ocount, &count,
							sizeof(count)))
				return -XFS_ERROR(EFAULT);
		}

		return 0;
	}

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

	case XFS_IOC_PATH_TO_FSHANDLE:
	case XFS_IOC_FD_TO_HANDLE:
	case XFS_IOC_PATH_TO_HANDLE: {
		/*
		 * XFS_IOC_PATH_TO_FSHANDLE 
                 *    returns fs handle for a mount point or path within
                 *    that mount point
                 * XFS_IOC_FD_TO_HANDLE
                 *    returns full handle for a FD opened in user space
	         * XFS_IOC_PATH_TO_HANDLE
                 *    returns full handle for a path
		 */
		return xfs_find_handle(cmd, arg);
	}

	case XFS_IOC_OPEN_BY_HANDLE: {

		return xfs_open_by_handle(cmd, arg, filp, inode, vfsp, mp);
	}

	case XFS_IOC_READLINK_BY_HANDLE: {

		return xfs_readlink_by_handle(cmd, arg, filp, inode, vfsp, mp);
	}

	case XFS_IOC_SWAPEXT: {

                error = xfs_swapext((struct xfs_swapext *)arg);

		if (error)
			return -error;
		return error;

	}

	case XFS_IOC_GETFSUUID: {

		return copy_to_user((char *)arg, (char *)&mp->m_sb.sb_uuid,
						sizeof(uuid_t));
	}
        
        case XFS_IOC_FSCOUNTS: {
                xfs_fsop_counts_t out;
                
		error = xfs_fs_counts(mp, &out);
                if (error)
                        return -error;
            
                return copy_to_user((char *)arg, &out, sizeof(xfs_fsop_counts_t));
        }
        
        case XFS_IOC_SET_RESBLKS: {
                xfs_fsop_resblks_t inout;
                __uint64_t in; 
                              
		error=copy_from_user(&inout, (char *)arg, sizeof(xfs_fsop_resblks_t));
                if (error)
                        return -error;
                
                /* input parameter is passed in resblks field of structure */
                in=inout.resblks;
		error = xfs_reserve_blocks(mp, &in, &inout);
                
                return copy_to_user((char *)arg, &inout, sizeof(xfs_fsop_resblks_t));
        }
            
        case XFS_IOC_GET_RESBLKS: {
                xfs_fsop_resblks_t out;
                
		error = xfs_reserve_blocks(mp, NULL, &out);
                if (error)
                        return -error;
        
                return copy_to_user((char *)arg, &out, sizeof(xfs_fsop_resblks_t));
        }

	case XFS_IOC_FSGROWFSDATA: {
		xfs_growfs_data_t in;
		error = copy_from_user(&in, (char *)arg, sizeof(xfs_growfs_data_t));
		if (error)
			return -error;
		error = xfs_growfs_data(mp, &in);
		if (error)
			return -error;
		return error;
	}

	case XFS_IOC_FSGROWFSLOG: {
		xfs_growfs_log_t in;
		error = copy_from_user(&in, (char *)arg, sizeof(xfs_growfs_log_t));
		if (error)
			return -error;
		error = xfs_growfs_log(mp, &in);
		if (error)
			return -error;
		return error;
	}

	case XFS_IOC_FSGROWFSRT: {
		xfs_growfs_rt_t in;
		error = copy_from_user(&in, (char *)arg, sizeof(xfs_growfs_rt_t));
		if (error)
			return -error;
		error = xfs_growfs_rt(mp, &in);
		if (error)
			return -error;
		return error;
	}


	default:
		return -EINVAL;
	}
}
