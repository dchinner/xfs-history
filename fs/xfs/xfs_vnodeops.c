#include <sys/types.h>
#ifdef SIM
#define _KERNEL 1
#endif
#include <sys/time.h>
#include <sys/timers.h>
#include <sys/buf.h>
#include <sys/uio.h>
#include <sys/user.h>
#include <sys/vfs.h>
#include <sys/vnode.h>
#include <fs/specfs/snode.h>
#include <sys/systm.h>
#include <sys/dnlc.h>
#include <sys/sysmacros.h>
#include <sys/prctl.h>
#include <sys/cred.h>
#include <sys/grio.h>
#include <sys/pfdat.h>
#ifdef SIM
#undef _KERNEL
#endif
#include <sys/cmn_err.h>
#include <sys/debug.h>
#include <sys/errno.h>
#include <sys/fs_subr.h>
#include <sys/fcntl.h>
#ifdef SIM
#include <bstring.h>
#else
#include <sys/conf.h>
#endif
#include <sys/kmem.h>
#include <sys/mount.h>
#include <sys/param.h>
#include <sys/pathname.h>
#include <sys/proc.h>
#include <sys/sema.h>
#include <sys/statvfs.h>
#include <sys/region.h>
#include <fs/specfs/snode.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <sys/mode.h>
#include <sys/sysinfo.h>
#include <sys/ksa.h>
#include <sys/var.h>
#include <string.h>
#include "xfs_types.h"
#include "xfs_inum.h"
#include "xfs_log.h"
#include "xfs_trans.h"
#include "xfs_sb.h"
#include "xfs_ag.h"
#include "xfs_mount.h"
#include "xfs_alloc_btree.h"
#include "xfs_bmap_btree.h"
#include "xfs_ialloc_btree.h"
#include "xfs_btree.h"
#include "xfs_ialloc.h"
#include "xfs_alloc.h"
#include "xfs_bmap.h"
#include "xfs_dinode.h"
#include "xfs_inode_item.h"
#include "xfs_inode.h"
#include "xfs_dir.h"
#include "xfs_rw.h"
#include "xfs_error.h"

#ifdef SIM
#include "sim.h"
#endif

STATIC void	xfs_droplink (xfs_trans_t *tp,
			      xfs_inode_t *ip);

STATIC int	xfs_close(vnode_t	*vp,
			  int		flag,
			  lastclose_t	lastclose,
			  off_t		offset,
			  cred_t	*credp);

STATIC int	xfs_getattr(vnode_t	*vp,
			    vattr_t	*vap,
			    int		flags,
			    cred_t	*credp);

STATIC int	xfs_setattr(vnode_t	*vp,
			    vattr_t	*vap,
			    int		flags,
			    cred_t	*credp);

STATIC int	xfs_access(vnode_t	*vp,
			   int		mode,
			   int		flags,
			   cred_t	*credp);

STATIC int	xfs_readlink(vnode_t	*vp,
			     uio_t	*uiop,
			     cred_t	*credp);

STATIC int	xfs_fsync(vnode_t	*vp,
			  int		flag,
			  cred_t	*credp);

STATIC void	xfs_inactive(vnode_t	*vp,
			     cred_t	*credp);

STATIC int	xfs_lookup(vnode_t	*dir_vp,
			   char		*name,
			   vnode_t	**vpp,
			   pathname_t	*pnp,
			   int		flags,
			   vnode_t	*rdir, 
			   cred_t	*credp);

STATIC int	xfs_create(vnode_t	*dir_vp,
			   char		*name,
			   vattr_t	*vap,
			   enum vcexcl	excl,
			   int		mode,
			   vnode_t	**vpp,
			   cred_t	*credp);

STATIC int	xfs_remove(vnode_t	*dir_vp,
			   char		*name,
			   cred_t	*credp);

STATIC int	xfs_link(vnode_t	*target_dir_vp,
			 vnode_t	*src_vp,
			 char		*target_name,
			 cred_t		*credp);

STATIC int	xfs_readlink(vnode_t	*vp,
			     uio_t	*uiop,
			     cred_t	*credp);

STATIC int	xfs_rename(vnode_t	*src_dir_vp,
			   char		*src_name,
			   vnode_t	*target_dir_vp,
			   char		*target_name,
			   pathname_t	*target_pnp,
			   cred_t	*credp);

STATIC int	xfs_mkdir(vnode_t	*dir_vp,
			  char		*dir_name,
			  vattr_t	*vap,
			  vnode_t	**vpp,
			  cred_t	*credp);

STATIC int	xfs_rmdir(vnode_t	*dir_vp,
			  char		*name,
			  vnode_t	*current_dir_vp,
			  cred_t	*credp);

STATIC int	xfs_readdir(vnode_t	*dir_vp,
			    uio_t	*uiop,
			    cred_t	*credp,
			    int		*eofp);

STATIC int	xfs_symlink(vnode_t	*dir_vp,
			    char	*link_name,
			    vattr_t	*vap,
			    char	*target_path,
			    cred_t	*credp);

STATIC int	xfs_fid(vnode_t	*vp,
			fid_t	**fidpp);

STATIC void	xfs_rwlock(vnode_t	*vp,
			   int		write_lock);

STATIC void	xfs_rwunlock(vnode_t	*vp,
			     int	write_lock);

STATIC int	xfs_seek(vnode_t	*vp,
			 off_t		old_offset,
			 off_t		*new_offsetp);

STATIC int	xfs_frlock(vnode_t	*vp,
			   int		cmd,
			   flock_t	*flockp,
			   int		flag,
			   off_t	offset,
			   cred_t	*credp);

STATIC int	xfs_map(vnode_t	*vp,
			off_t	offset,
			preg_t	*pregp,
			addr_t	*addrp,
			uint	len,
			uint	prot,
			uint	max_prot,
			uint	flags,
			cred_t	*credp);

STATIC int	xfs_addmap(vnode_t	*vp,
			   off_t	offset,
			   preg_t	*pregp,
			   addr_t	addr,
			   uint		len,
			   uint		prot,
			   uint		max_prot,
			   uint		flags,
			   cred_t	*credp);

STATIC int	xfs_delmap(vnode_t	*vp,
			   off_t	offset,
			   preg_t	*pregp,
			   addr_t	addr,
			   uint		len,
			   uint		prot,
			   uint		max_prot,
			   uint		flags,
			   cred_t	*credp);

STATIC int	xfs_allocstore(vnode_t	*vp,
			       uint	offset,
			       uint	len,
			       cred_t	*credp);

STATIC int	xfs_reclaim(vnode_t	*vp,
			    int		flag);

STATIC int	xfs_fcntl(vnode_t	*vp,
			  int		cmd,
			  void		*arg,
			  int		flags,
			  off_t		offset,
			  cred_t	*credp,
			  rval_t	*rvalp);


extern struct igetstats XFS_IGETINFO;

sema_t xfs_ancestormon;		/* initialized in xfs_init */

#define IRELE(ip)	VN_RELE(XFS_ITOV(ip))
#define IHOLD(ip)	VN_HOLD(XFS_ITOV(ip))
#define	ITRACE(ip)	vn_trace_ref(XFS_ITOV(ip), __FILE__, __LINE__)


/*
 * No open action is required for regular files.  Devices are handled
 * through the specfs file system, pipes through fifofs.  Device and
 * fifo vnodes are "wrapped" by specfs and fifofs vnodes, respectively,
 * when a new vnode is first looked up or created.
 */


/*
 * xfs_close
 *
 */
STATIC int
xfs_close(vnode_t	*vp,
	  int		flag,
	  lastclose_t	lastclose,
	  off_t		offset,
	  cred_t	*credp)
{
	extern int 	xfs_remove_grio_guarantee( xfs_inode_t *, pid_t);

	struct ufchunk  *ufp;
	struct file	*fp;
        xfs_inode_t	*ip;
	proc_t		*p  = u.u_procp;
	shaddr_t	*sa = p->p_shaddr;
	int		isshd, nofiles;
	int		i, vpcount, ret;

	vn_trace_entry(vp, "xfs_close");
        ip = XFS_VTOI(vp);

	/*
 	 * If this is a last close and the file was marked 
	 * as being used for grio, check for tickets to remove.
	 */
	if (lastclose && (ip->i_flags & XFS_IGRIO)) {

		vpcount = 0;

#ifndef SIM
		if (isshd = ISSHDFD(p, sa)) {
			mrlock(&sa->s_fsync, MR_ACCESS, PZERO);
			ufp = sa->s_flist;
			nofiles  = sa->s_nofiles;
		} else {
			ufp = u.u_flist;
			nofiles = u.u_nofiles;
		}

		for (i = 0 ; i < nofiles; i++ ) {
			if ((fp = ufgetfast( i,nofiles, ufp))) {
				if ((fp->f_vnode == vp) && (fp->f_count > 0)) {
					vpcount++;
				}
			}
		}

		if (isshd) {
			mrunlock(&sa->s_fsync);
		}
#endif

		/*
		 * If this process is nolonger accessing 
 		 * this file, remove any guarantees that
		 * were made by this process on this file.
		 */
		if (!vpcount) {
			xfs_remove_grio_guarantee(ip, u.u_procp->p_pid);
		}
	}

        xfs_ilock(ip, XFS_ILOCK_SHARED);


        cleanlocks(vp, u.u_procp->p_epid, u.u_procp->p_sysid);
        xfs_iunlock(ip, XFS_ILOCK_SHARED);
        return 0;
}


/*
 * xfs_ioctl
 *
 */
STATIC int
xfs_ioctl(vnode_t	*vp,
	  int		cmd,
	  void		*argp,
	  int		flag,
	  cred_t	*credp,
	  int		*rvalp)
{
	return XFS_ERROR(ENOTTY);
}


/*
 * xfs_getattr
 */
STATIC int
xfs_getattr(vnode_t	*vp,
	    vattr_t	*vap,
	    int		flags,
	    cred_t	*credp)
{
	xfs_inode_t	*ip;
	xfs_mount_t	*mp;

	vn_trace_entry(vp, "xfs_getattr");
	ip = XFS_VTOI(vp);
	xfs_ilock (ip, XFS_ILOCK_SHARED);

	vap->va_size = ip->i_d.di_size;
        if (vap->va_mask == AT_SIZE) {
		xfs_iunlock (ip, XFS_ILOCK_SHARED);
                return 0;
	}
        vap->va_fsid = ip->i_dev;

	/* XXX trunc to 32 bits for now. */

        vap->va_nodeid = ip->i_ino;

        if (vap->va_mask == (AT_FSID|AT_NODEID)) {
		xfs_iunlock (ip, XFS_ILOCK_SHARED);
                return 0;
	}

	/*
         * Copy from in-core inode.
         */
        vap->va_type = vp->v_type;
        vap->va_mode = ip->i_d.di_mode & MODEMASK;
        vap->va_uid = ip->i_d.di_uid;
        vap->va_gid = ip->i_d.di_gid;
        vap->va_nlink = ip->i_d.di_nlink;
        vap->va_vcode = ip->i_vcode;
        if (vp->v_type == VCHR || vp->v_type == VBLK || vp->v_type == VXNAM)
                vap->va_rdev = ip->i_u2.iu_rdev;
        else
                vap->va_rdev = 0;       /* not a b/c spec. */

	/*
	 * Note: Although we store 64 bit timestamps, the code 
	 * currently only updates the upper 32 bits.
	 */
        vap->va_atime.tv_sec = ip->i_d.di_atime.t_sec;
        vap->va_atime.tv_nsec = ip->i_d.di_atime.t_nsec;
        vap->va_mtime.tv_sec = ip->i_d.di_mtime.t_sec;
        vap->va_mtime.tv_nsec = ip->i_d.di_mtime.t_nsec;
        vap->va_ctime.tv_sec = ip->i_d.di_ctime.t_sec;
        vap->va_ctime.tv_nsec = ip->i_d.di_ctime.t_nsec;

	mp = XFS_VFSTOM(vp->v_vfsp);

        switch (ip->i_d.di_mode & IFMT) {
          case IFBLK:
          case IFCHR:
                vap->va_blksize = BLKDEV_IOSIZE;
                break;
          default:
		vap->va_blksize = mp->m_sb.sb_blocksize;	/* in bytes */
		break;
        }

	/*
	 * XXX : truncate to 32 bits for now.
	 */
	switch (ip->i_d.di_format) {
	case XFS_DINODE_FMT_LOCAL:
	case XFS_DINODE_FMT_DEV:
	case XFS_DINODE_FMT_UUID:
		vap->va_nblocks = 0;
		break;
	case XFS_DINODE_FMT_EXTENTS:
	case XFS_DINODE_FMT_BTREE:
		vap->va_nblocks = ip->i_d.di_nblocks + ip->i_delayed_blks;
		break;
	default:
		ASSERT(0);
	}

	/*
	 * XFS-added attributes
	 */
	if (flags & (AT_XFLAGS|AT_EXTSIZE|AT_NEXTENTS|AT_UUID)) {
		vap->va_xflags = ip->i_d.di_flags;
		vap->va_extsize = ip->i_d.di_extsize << mp->m_sb.sb_blocklog;
		vap->va_nextents = (ip->i_flags & XFS_IEXTENTS) ?
			ip->i_bytes / sizeof(xfs_bmbt_rec_t) :
			ip->i_d.di_nextents;
		vap->va_uuid = ip->i_d.di_uuid;
	}

	xfs_iunlock (ip, XFS_ILOCK_SHARED);

	return 0;
}


/*
 * xfs_setattr
 */
STATIC int
xfs_setattr(vnode_t	*vp,
	    vattr_t	*vap,
	    int		flags,
	    cred_t	*credp)
{
        xfs_inode_t     *ip;
	xfs_trans_t	*tp = NULL;
	xfs_mount_t	*mp;
	int		mask;
	int		code;
	uint		lock_flags;
	boolean_t	ip_held;
	timestruc_t 	tv;
	uid_t		uid;
	gid_t		gid;

	vn_trace_entry(vp, "xfs_setattr");
	/*
	 * Cannot set certain attributes.
         */
        mask = vap->va_mask;
        if (mask & AT_NOSET)
                return XFS_ERROR(EINVAL);

        ip = XFS_VTOI(vp);

        /*
         * Timestamps do not need to be logged and hence do not
         * need to be done within a transaction.
         */
        if (mask & AT_UPDTIMES) {
                /*
                 * NOTE: Although we set all 64 bits of the timestamp,
                 * the underlying code will ignore the low order 32
                 * bits.  The low order 32 bits will always be zero
                 * on disk.
                 */
                ASSERT((mask & ~AT_UPDTIMES) == 0);
                nanotime(&tv);

                if (mask & AT_UPDATIME) {
                        ip->i_d.di_atime.t_sec = tv.tv_sec;
                        ip->i_d.di_atime.t_nsec = tv.tv_nsec;
                }
                if (mask & AT_UPDCTIME) {
                        ip->i_d.di_ctime.t_sec = tv.tv_sec;
                        ip->i_d.di_ctime.t_nsec = tv.tv_nsec;
                }
                if (mask & AT_UPDMTIME) {
                        ip->i_d.di_mtime.t_sec = tv.tv_sec;
                        ip->i_d.di_mtime.t_nsec = tv.tv_nsec;
                }
		return 0;
        }


	/*
	 * For the other attributes, we acquire the inode lock and
	 * first do an error checking pass.
	 */
	mp = ip->i_mount;
        tp = xfs_trans_alloc (mp, 0);
        if (code = xfs_trans_reserve (tp, 0, XFS_ITRUNCATE_LOG_RES(mp), 0,
				      XFS_TRANS_PERM_LOG_RES,
				      XFS_ITRUNCATE_LOG_COUNT)) {
                xfs_trans_cancel (tp, 0);
                return code;
        }

	lock_flags = XFS_ILOCK_EXCL;
	if (mask & AT_SIZE) {
		lock_flags |= XFS_IOLOCK_EXCL;
	}
        xfs_ilock (ip, lock_flags);


        /*
         * Change file access modes.  Must be owner or privileged.
	 * Also check here for xflags, extsize.
         */
        if (mask & (AT_MODE|AT_XFLAGS|AT_EXTSIZE)) {
                if (credp->cr_uid != ip->i_d.di_uid && !crsuser(credp)) {
                        code = XFS_ERROR(EPERM);
                        goto error_return;
                }
        }

        /*
         * Change file ownership.  Must be the owner or privileged.
         * If the system was configured with the "restricted_chown"
         * option, the owner is not permitted to give away the file,
         * and can change the group id only to a group of which he
         * or she is a member.
         */
        if (mask & (AT_UID|AT_GID)) {
                uid = (mask & AT_UID) ? vap->va_uid : ip->i_d.di_uid;
                gid = (mask & AT_GID) ? vap->va_gid : ip->i_d.di_gid;

                if (!crsuser(credp)) {
                        if (credp->cr_uid != ip->i_d.di_uid ||
                                (restricted_chown &&
                                (ip->i_d.di_uid != uid ||
                                 !groupmember(gid, credp)))) {
                                code = XFS_ERROR(EPERM);
                                goto error_return;
                        }
                }
        }

        /*
         * Truncate file.  Must have write permission and not be a directory.
         */
        if (mask & AT_SIZE) {
                if (vp->v_type == VDIR) {
                        code = XFS_ERROR(EISDIR);
                        goto error_return;
                }

                /* XXX Check for write access to inode */

                if (vp->v_type == VREG && (code = fs_vcode(vp, &ip->i_vcode)))
                        goto error_return;
        }

        /*
         * Change file access or modified times.
         */
        if (mask & (AT_ATIME|AT_MTIME)) {
                if (credp->cr_uid != ip->i_d.di_uid && !crsuser(credp)) {
                        if (flags & ATTR_UTIME) {
                                code = XFS_ERROR(EPERM);
                                goto error_return;
                        }

                        /* Check for WRITE access */
                }
        }

	/*
	 * Change extent size or realtime flag.
	 */
	if (mask & (AT_EXTSIZE|AT_XFLAGS)) {
		/*
		 * Can't change extent size if any extents are allocated.
		 */
		if (ip->i_d.di_nextents && (mask & AT_EXTSIZE) &&
		    ip->i_d.di_extsize != vap->va_extsize) {
			code = XFS_ERROR(EINVAL);	/* EFBIG? */
			goto error_return;
		}
		/*
		 * Can't change realtime flag if any extents are allocated.
		 */
		if (ip->i_d.di_nextents && (mask & AT_XFLAGS) &&
		    (ip->i_d.di_flags & XFS_DIFLAG_REALTIME) !=
		    (vap->va_xflags & XFS_DIFLAG_REALTIME)) {
			code = XFS_ERROR(EINVAL);	/* EFBIG? */
			goto error_return;
		}
		/*
		 * Extent size must be a multiple of the appropriate block
		 * size, if set at all.
		 */
		if ((mask & AT_EXTSIZE) && vap->va_extsize != 0) {
			xfs_extlen_t	size;

			if ((ip->i_d.di_flags & XFS_DIFLAG_REALTIME) ||
			    ((mask & AT_XFLAGS) && 
			    (vap->va_xflags & XFS_DIFLAG_REALTIME)))
				size = mp->m_sb.sb_rextsize <<
					mp->m_sb.sb_blocklog;
			else
				size = mp->m_sb.sb_blocksize;
			if (vap->va_extsize % size) {
				code = XFS_ERROR(EINVAL);
				goto error_return;
			}
		}
		/*
		 * If realtime flag is set then must have realtime data.
		 */
		if ((mask & AT_XFLAGS) &&
		    (vap->va_xflags & XFS_DIFLAG_REALTIME)) {
			if ((mp->m_sb.sb_rextsize == 0)  ||
			    (ip->i_d.di_extsize % mp->m_sb.sb_rextsize)) {

				code = XFS_ERROR(EINVAL);	/* ??? */
				goto error_return;
			}
		}
	}

	/*
	 * Now we can make the changes.  Before we join the inode
	 * to the transaction, if AT_SIZE is set then take care of
	 * the part of the truncation that must be done without the
	 * inode lock.  This needs to be done before joining the inode
	 * to the transaction, because the inode cannot be unlocked
	 * once it is a part of the transaction.
	 */
	if (mask & AT_SIZE) {
		if (vap->va_size > ip->i_d.di_size) {
			xfs_igrow_start (ip, vap->va_size, credp);
		} else if (vap->va_size < ip->i_d.di_size) {
			xfs_iunlock (ip, XFS_ILOCK_EXCL);
			xfs_itruncate_start (ip, XFS_ITRUNC_DEFINITE,
					     (xfs_fsize_t)vap->va_size);
			xfs_ilock (ip, XFS_ILOCK_EXCL);
		}
	}

        xfs_trans_ijoin (tp, ip, lock_flags);
	ip_held = B_FALSE;

	/*
         * Truncate file.  Must have write permission and not be a directory.
         */
        if (mask & AT_SIZE) {
		if (vap->va_size > ip->i_d.di_size) {
			xfs_igrow_finish (tp, ip, vap->va_size);
		} else if (vap->va_size < ip->i_d.di_size) {
			xfs_trans_ihold(tp, ip);
			xfs_itruncate_finish (&tp, ip,
					      (xfs_fsize_t)vap->va_size);
			ip_held = B_TRUE;
		}
        }

        /*
         * Change file access modes.
         */
        if (mask & AT_MODE) {
                ip->i_d.di_mode &= IFMT;
                ip->i_d.di_mode |= vap->va_mode & ~IFMT;
                /*
                 * A non-privileged user can set the sticky and sgid
                 * bits on a directory.
                 */
                if (!crsuser(credp)) {
                        if (vp->v_type != VDIR && (ip->i_d.di_mode & ISVTX))
                                ip->i_d.di_mode &= ~ISVTX;
                        if (!groupmember(ip->i_d.di_gid, credp) && 
			    (ip->i_d.di_mode & ISGID))
                                ip->i_d.di_mode &= ~ISGID;
                }
		xfs_trans_log_inode (tp, ip, XFS_ILOG_CORE);
        }

	/*
	 * Change file ownership.  Must be the owner or privileged.
         * If the system was configured with the "restricted_chown"
         * option, the owner is not permitted to give away the file,
         * and can change the group id only to a group of which he
         * or she is a member.
         */
        if (mask & (AT_UID|AT_GID)) {
                uid = (mask & AT_UID) ? vap->va_uid : ip->i_d.di_uid;
                gid = (mask & AT_GID) ? vap->va_gid : ip->i_d.di_gid;

                if (!crsuser(credp)) {
                        ip->i_d.di_mode &= ~(ISUID|ISGID);
                }
                if (ip->i_d.di_uid == uid) {
                        /*
                         * XXX This won't work once we have group quotas
                         */
                        ip->i_d.di_gid = gid;
                } else {
                        ip->i_d.di_uid = uid;
                        ip->i_d.di_gid = gid;
                }
		xfs_trans_log_inode (tp, ip, XFS_ILOG_CORE);
        }


	/*
         * Change file access or modified times.
         */
        if (mask & (AT_ATIME|AT_MTIME)) {
                /*
                 * since utime() always updates both mtime and atime
                 * ctime will always be set, as it need to be so there
                 * no reason to set ICHG
                 */
                if (mask & AT_ATIME) {
                        ip->i_d.di_atime.t_sec = vap->va_atime.tv_sec;
			ip->i_d.di_atime.t_nsec = vap->va_atime.tv_nsec;
                }
                if (mask & AT_MTIME) {
                        nanotime(&tv);
			ip->i_d.di_mtime.t_sec = vap->va_mtime.tv_sec;
			ip->i_d.di_mtime.t_nsec = vap->va_mtime.tv_nsec;
			ip->i_d.di_ctime.t_sec = tv.tv_sec;
			ip->i_d.di_ctime.t_nsec = tv.tv_nsec;
                }
        }

	/*
	 * Change XFS-added attributes.
	 */
	if (mask & (AT_EXTSIZE|AT_XFLAGS)) {
		if (mask & AT_EXTSIZE) {
			/*
 			 * Converting bytes to fs blocks.
			 */
			ip->i_d.di_extsize = vap->va_extsize >>
				mp->m_sb.sb_blocklog;
		}
		if (mask & AT_XFLAGS)
			ip->i_d.di_flags = vap->va_xflags & XFS_DIFLAG_ALL;
		xfs_trans_log_inode(tp, ip, XFS_ILOG_CORE);
	}

	XFS_IGETINFO.ig_attrchg++;

	if (!ip_held)
		IHOLD (ip);
	xfs_trans_commit (tp, XFS_TRANS_RELEASE_LOG_RES);
	if (ip_held) {
		xfs_iunlock (ip, lock_flags);
	}

	return 0;




error_return:

	xfs_trans_cancel (tp, XFS_TRANS_RELEASE_LOG_RES);
	xfs_iunlock (ip, lock_flags);

	return code;

}


/*
 * xfs_access
 * Null conversion from vnode mode bits to inode mode bits, as in efs.
 */
STATIC int
xfs_access(vnode_t	*vp,
	   int		mode,
	   int		flags,
	   cred_t	*credp)
{
	xfs_inode_t	*ip;
	int		error;

	vn_trace_entry(vp, "xfs_access");
	ip = XFS_VTOI(vp);
	xfs_ilock (ip, XFS_ILOCK_SHARED);
	error = xfs_iaccess(ip, mode, credp);
	xfs_iunlock (ip, XFS_ILOCK_SHARED);
	return error;
}

/*
 * The maximum pathlen is 1024 bytes. Since the minimum file system
 * blocksize is 512 bytes, we can get a max of 2 extents back from
 * bmapi.
 */
#define SYMLINK_MAPS 2


/*
 * xfs_readlink
 *
 */
STATIC int
xfs_readlink(vnode_t	*vp,
	     uio_t	*uiop,
	     cred_t	*credp)
{
        xfs_inode_t     *ip;
	int		count;
	off_t		offset;
	int		pathlen;
	timestruc_t     tv;
        int             error = 0;

	vn_trace_entry(vp, "xfs_readlink");
	if (vp->v_type != VLNK)
                return XFS_ERROR(EINVAL);

	ip = XFS_VTOI(vp);
	xfs_ilock (ip, XFS_ILOCK_SHARED);

	ASSERT ((ip->i_d.di_mode & IFMT) == IFLNK);

	offset = uiop->uio_offset;
        count = uiop->uio_resid;

	if (MANDLOCK(vp, ip->i_d.di_mode)
            && (error = chklock(vp, FREAD, offset, count, uiop->uio_fmode))) {
                goto error_return;
        }
	if (offset < 0) {
                error = XFS_ERROR(EINVAL);
		goto error_return;
	}
        if (count <= 0) {
                error = 0;
		goto error_return;
	}

	nanotime(&tv);
	ip->i_d.di_atime.t_sec = tv.tv_sec;
	ip->i_d.di_atime.t_nsec = tv.tv_nsec;

	/*
	 * See if the symlink is stored inline.
	 */
	pathlen = (int) ip->i_d.di_size;

	if (ip->i_flags & XFS_IINLINE) {
		error = uiomove (ip->i_u1.iu_data, pathlen, UIO_READ, uiop);
	}
	else {
		/*
		 * Symlink not inline.  Call bmap to get it in.
		 */
		xfs_mount_t	*mp;
                int             nmaps;
                xfs_bmbt_irec_t mval [SYMLINK_MAPS];
                daddr_t         d;
                int             byte_cnt, n;
                struct          buf *bp;

		mp = XFS_VFSTOM(vp->v_vfsp);
                nmaps = SYMLINK_MAPS;

                (void) xfs_bmapi (NULL, ip, 0, XFS_B_TO_FSB(mp, pathlen),
                         0, NULLFSBLOCK, 0, mval, &nmaps, NULL);

                for (n = 0; n < nmaps; n++) {
                        d = XFS_FSB_TO_DADDR(mp, mval[n].br_startblock);
                        byte_cnt = XFS_FSB_TO_B(mp, mval[n].br_blockcount);
                        bp = read_buf (mp->m_dev, d, BTOBB(byte_cnt), 0);
                        if (pathlen < byte_cnt)
                                byte_cnt = pathlen;
                        pathlen -= byte_cnt;

                        error = uiomove (bp->b_un.b_addr, byte_cnt,
					 UIO_READ, uiop);
			brelse (bp);
                }

	}


error_return:

	xfs_iunlock (ip, XFS_ILOCK_SHARED);

	return error;
}


/*
 * xfs_fsync
 *
 * This is called to sync the inode and its data out to disk.
 * We need to hold the I/O lock while flushing the data, and
 * the inode lock while flushing the inode.  The inode lock CANNOT
 * be held while flushing the data, so acquire after we're done
 * with that.
 */
STATIC int
xfs_fsync(vnode_t	*vp,
	  int		flag,
	  cred_t	*credp)
{
	xfs_inode_t *ip;

	vn_trace_entry(vp, "xfs_fsync");
	ip = XFS_VTOI(vp);
	xfs_ilock(ip, XFS_IOLOCK_EXCL);
	if (flag & FSYNC_INVAL) {
		if (ip->i_flags & XFS_IEXTENTS && ip->i_bytes > 0) {
			pflushinvalvp(vp, 0, XFS_ISIZE_MAX(ip));
		}
	} else {
		pflushvp(vp, ip->i_d.di_size,
			 (flag & FSYNC_WAIT) ? 0 : B_ASYNC);
	}
	xfs_ilock(ip, XFS_ILOCK_SHARED);
	xfs_iflock(ip);
	xfs_iflush(ip, (flag & FSYNC_WAIT) ? 0 : B_ASYNC);
	xfs_iunlock(ip, XFS_IOLOCK_EXCL | XFS_ILOCK_SHARED);
	return 0;
}


/*
 * xfs_inactive
 *
 * This is called when the vnode reference count for the vnode
 * goes to zero.  If the file has been unlinked, then it must
 * now be truncated.  When we truncate the file we must also
 * call remapf() to invalidate any memory mappings to the file.
 * Also, we clear all of the read-ahead state
 * kept for the inode here since the file is now closed.
 */
STATIC void
xfs_inactive(vnode_t	*vp,
	     cred_t	*credp)
{
	xfs_inode_t	*ip;
	xfs_trans_t	*tp;
	xfs_trans_t	*ntp;
	xfs_mount_t	*mp;
	int		truncate;
	int		status;
	int		done;
	int		committed;
	int		commit_flags;
	xfs_fsblock_t	first_block;
	xfs_bmap_free_t	free_list;

	vn_trace_entry(vp, "xfs_inactive");
	ip = XFS_VTOI(vp);

	/*
	 * If the inode is already free, then there can be nothing
	 * to clean up here.
	 */
	if (ip->i_d.di_mode == 0) {
		ASSERT(ip->i_real_bytes == 0);
		ASSERT(ip->i_broot_bytes == 0);
		return;
	}

	/*
	 * Only do a truncate if it's a regular file with
	 * some actual space in it.  It's OK to look at the
	 * inode's fields without the lock because we're the
	 * only one with a reference to the inode.
	 */
	truncate = ((ip->i_d.di_nlink == 0) &&
		    (ip->i_d.di_size > 0) &&
		    ((ip->i_d.di_mode & IFMT) == IFREG));

	ASSERT(ip->i_d.di_nlink >= 0);
	if (ip->i_d.di_nlink == 0) {
		mp = ip->i_mount;
		tp = xfs_trans_alloc(mp, 0);

		if (truncate) {
			status = xfs_trans_reserve(tp, 0,
					XFS_ITRUNCATE_LOG_RES(mp),
					0, XFS_TRANS_PERM_LOG_RES,
					XFS_ITRUNCATE_LOG_COUNT);
			ASSERT(status == 0);

			xfs_ilock(ip, XFS_IOLOCK_EXCL);
			xfs_itruncate_start(ip, XFS_ITRUNC_DEFINITE, 0);
			xfs_ilock(ip, XFS_ILOCK_EXCL);

			xfs_trans_ijoin(tp, ip,
					XFS_IOLOCK_EXCL | XFS_ILOCK_EXCL);
			xfs_trans_ihold(tp, ip);

			xfs_itruncate_finish(&tp, ip, 0);
			commit_flags = XFS_TRANS_RELEASE_LOG_RES;

		} else if ((ip->i_d.di_mode & IFMT) == IFLNK) {

			if (ip->i_d.di_size > XFS_LITINO(mp)) {
				/*
				 * We're freeing a symlink that has some
				 * blocks allocated to it.  Free the
				 * blocks here.  We know that we've got
				 * either 1 or 2 extents and that we can
				 * free them all in one bunmapi call.
				 */
				ASSERT((ip->i_d.di_nextents > 0) &&
				       (ip->i_d.di_nextents <= 2));
				status = xfs_trans_reserve(tp, 0,
						XFS_ITRUNCATE_LOG_RES(mp),
						0, XFS_TRANS_PERM_LOG_RES,
						XFS_ITRUNCATE_LOG_COUNT);
				ASSERT(status == 0);

				xfs_ilock(ip, XFS_IOLOCK_EXCL |
					  XFS_ILOCK_EXCL);
				xfs_trans_ijoin(tp, ip,
						XFS_ILOCK_EXCL |
						XFS_IOLOCK_EXCL);
				xfs_trans_ihold(tp, ip);

				done = 0;
				XFS_BMAP_INIT(&free_list, &first_block);
				first_block = xfs_bunmapi(tp, ip, 0, 2, 2,
						  first_block, &free_list,
						  &done);
				ASSERT(done);
				committed = xfs_bmap_finish(&tp, &free_list,
							    first_block);
				if (committed) {
					/*
					 * The first xact was committed,
					 * so add the inode to the new one.
					 * Mark it dirty so it will be logged
					 * and moved forward in the log as
					 * part of every commit.
					 */
					xfs_trans_ijoin(tp, ip,
							XFS_ILOCK_EXCL |
							XFS_IOLOCK_EXCL);
					xfs_trans_ihold(tp, ip);
					xfs_trans_log_inode(tp, ip,
							    XFS_ILOG_CORE);
				}
				ntp = xfs_trans_dup(tp);
				xfs_trans_commit(tp, 0);
				tp = ntp;
				ASSERT(ip->i_bytes == 0);
				status = xfs_trans_reserve(tp, 0,
					       XFS_ITRUNCATE_LOG_RES(mp),
					       0, XFS_TRANS_PERM_LOG_RES,
					       XFS_ITRUNCATE_LOG_COUNT);
				ASSERT(status == 0);

			} else {
				/*
				 * We're freeing a symlink which fit into
				 * the inode.  Just free the memory used
				 * to hold the old symlink.
				 */
				status = xfs_trans_reserve(tp, 0,
					   XFS_ITRUNCATE_LOG_RES(mp),
					   0, XFS_TRANS_PERM_LOG_RES,
					   XFS_ITRUNCATE_LOG_COUNT);
				ASSERT(status == 0);

				xfs_ilock(ip, XFS_ILOCK_EXCL |
					  XFS_IOLOCK_EXCL);
				ASSERT(ip->i_bytes > 0);
				xfs_idata_realloc(ip, -(ip->i_bytes));
				ASSERT(ip->i_bytes == 0);
			}

			xfs_trans_ijoin(tp, ip,
					XFS_IOLOCK_EXCL | XFS_ILOCK_EXCL);
			xfs_trans_ihold(tp, ip);
			commit_flags = XFS_TRANS_RELEASE_LOG_RES;

		} else {
			status = xfs_trans_reserve(tp, 0,
						   XFS_IFREE_LOG_RES(mp),
						   0, 0,
						   XFS_DEFAULT_LOG_COUNT);
			ASSERT(status == 0);
			
			xfs_ilock(ip, XFS_ILOCK_EXCL |
				  XFS_IOLOCK_EXCL);
			xfs_trans_ijoin(tp, ip,
					XFS_IOLOCK_EXCL | XFS_ILOCK_EXCL);
			xfs_trans_ihold(tp, ip);
			commit_flags = 0;
		}

		/*
		 * Free the inode.
		 */
		xfs_ifree(tp, ip);

		xfs_trans_commit(tp , commit_flags);
		xfs_iunlock(ip, XFS_IOLOCK_EXCL | XFS_ILOCK_EXCL);
	}

	/*
	 * Clear all the inode's read-ahead state.  We don't need the
	 * lock for this because the inode is inactive.  Noone can
	 * be looking at this stuff.
	 */
	XFS_INODE_CLEAR_READ_AHEAD(ip);

	/*
	 * Blow away any now stale mappings to the file.
	 */
#ifndef SIM
	if (truncate && VN_MAPPED(vp)) {
		remapf(vp, 0, 0);
	}
#endif

	return;
}


/*
 * Test the sticky attribute of a directory.  We can unlink from a sticky
 * directory that's writable by us if: we are superuser, we own the file,
 * we own the directory, or the file is writable.
 */
STATIC int
xfs_stickytest(xfs_inode_t *dp, xfs_inode_t *ip, struct cred *cr)
{
        if ((dp->i_d.di_mode & ISVTX)
            && cr->cr_uid != 0
            && cr->cr_uid != ip->i_d.di_uid
            && cr->cr_uid != dp->i_d.di_uid) {
                return xfs_iaccess(ip, IWRITE, cr);
        }
        return 0;
}


/*
 * Max number of extents needed for directory create + symlink.
 */
#define MAX_EXT_NEEDED 9

/*
 * xfs_dir_lookup_int flags.
 */

#define DLF_IGET	0x01/* get entry inode if name lookup succeeds */

/*
 * Wrapper around xfs_dir_lookup. This routine will first look in
 * the dnlc.
 *
 * If DLF_IGET is set, then this routine will also return the inode.
 * Note that the inode will not be locked. Note, however, that the
 * vnode will have an additional reference in this case.
 */
STATIC int
xfs_dir_lookup_int (xfs_trans_t  *tp,
		    vnode_t      *dir_vp,
		    int		 flag,
                    char         *name,
                    pathname_t   *pnp,
		    xfs_ino_t    *inum,
		    xfs_inode_t  **ip)
{
	vnode_t		   *vp;
	struct ncfastdata  fastdata;
	int		   name_len;
	int		   code = 0;
	boolean_t	   do_iget;

	vn_trace_entry(dir_vp, "xfs_dir_lookup_int");
	do_iget = flag & DLF_IGET;

	/*
	 * Handle degenerate pathname component.
	 */
	if (*name == '\0') {
		VN_HOLD(dir_vp);
		*inum = XFS_VTOI(dir_vp)->i_ino;
		if (do_iget)
			*ip = XFS_VTOI(dir_vp);
		return (0);
        }

        /*
         * Try the directory name lookup cache.
         */
        if (vp = dnlc_lookup(dir_vp, name, NOCRED)) {
                *inum = XFS_VTOI(vp)->i_ino;
		if (do_iget) {
			*ip = XFS_VTOI(vp);
			ASSERT((*ip)->i_d.di_mode != 0);
		} else
			VN_RELE (vp);
		return 0;
        }

	/*
	 * If all else fails, call the directory code.
	 */
	if (pnp != NULL)
		name_len = pnp->pn_complen;
	else
		name_len = strlen(name);

	code = xfs_dir_lookup(tp, XFS_VTOI(dir_vp), name, name_len, inum);
	if (! code && do_iget) {
		*ip = xfs_iget(XFS_VFSTOM(dir_vp->v_vfsp), NULL, *inum, 0);
		vp = XFS_ITOV(*ip);
		if ((*ip)->i_d.di_mode == 0) {
			/*
			 * The inode has been freed.  This
			 * had better be "..".
			 */
			ASSERT((name[0] == '.') &&
			       (name[1] == '.') &&
			       (name[2] == 0));
			*ip = NULL;
			VN_RELE (vp);
			code = ENOENT;
		}
	}

	return (code);
}


/*

 * xfs_lookup
 *
 */
STATIC int
xfs_lookup(vnode_t	*dir_vp,
	   char		*name,
	   vnode_t	**vpp,
	   pathname_t	*pnp,
	   int		flags,
	   vnode_t	*rdir, 
	   cred_t	*credp)
{
	xfs_inode_t		*dp, *ip;
	struct vnode		*vp, *newvp;
	xfs_ino_t		e_inum;
	int			code = 0;
	uint			lock_mode;

	vn_trace_entry(dir_vp, "xfs_lookup");
	dp = XFS_VTOI(dir_vp);
	lock_mode = xfs_ilock_map_shared(dp);

	if (code = xfs_iaccess(dp, IEXEC, credp)) {
		xfs_iunlock_map_shared(dp, lock_mode);
		return code;
	}

	code = xfs_dir_lookup_int (NULL, dir_vp, DLF_IGET, name, pnp, 
				   &e_inum, &ip);
	if (code) {
		xfs_iunlock_map_shared(dp, lock_mode);
		return code;
	}
	ITRACE(ip);

	xfs_iunlock_map_shared(dp, lock_mode);

	vp = XFS_ITOV (ip);

	/*
	 * If vnode is a device return special vnode instead.
         */
        if (ISVDEV(vp->v_type)) {
                newvp = specvp(vp, vp->v_rdev, vp->v_type, credp);
                VN_RELE(vp);
                if (newvp == NULL)
                        return XFS_ERROR(ENOSYS);
                vp = newvp;
        }

	*vpp = vp;

	return 0;
}


/*
 * Allocates a new inode from disk and return a pointer to the
 * incore copy. This routine will internally commit the current
 * transaction and allocate a new one if the Space Manager needed
 * to do an allocation to replenish the inode free-list.
 *
 * This routine is designed to be called from xfs_create and
 * xfs_create_dir.
 *
 */
STATIC int
xfs_dir_ialloc(
	xfs_trans_t	**tpp,		/* input: current transaction;
					   output: may be a new transaction. */
	xfs_inode_t	*dp,		/* directory within whose allocate
					   the inode. */
	mode_t		mode,
	ushort		nlink,
	dev_t		rdev,
	cred_t		*credp,
	xfs_inode_t	**ipp,		/* pointer to inode; it will be
					   locked. */
	int		*committed)

{
	xfs_trans_t	*tp;
	xfs_trans_t	*ntp;
	xfs_inode_t	*ip;
	struct buf	*ialloc_context = NULL;
	boolean_t	call_again = B_FALSE;
	int		code;
	uint		log_res;
	uint		log_count;

	tp = *tpp;
	ASSERT(tp->t_flags & XFS_TRANS_PERM_LOG_RES);

	/*
	 * xfs_ialloc will return a pointer to an incore inode if
	 * the Space Manager has an available inode on the free
	 * list. Otherwise, it will do an allocation and replenish
	 * the freelist.  Since we can only do one allocation per
	 * transaction without deadlocks, we will need to commit the
	 * current transaction and start a new one.  We will then
	 * need to call xfs_ialloc again to get the inode.
	 *
	 * If xfs_ialloc did an allocation to replenish the freelist,
	 * it returns the bp containing the head of the freelist as 
	 * ialloc_context. We will hold a lock on it across the
	 * transaction commit so that no other process can steal 
	 * the inode(s) that we've just allocated.
	 */
	ip = xfs_ialloc (tp, dp, mode, nlink, rdev, credp, 
			 &ialloc_context, &call_again);

	/*
	 * Return an error if we were unable to allocate a new inode.
	 * This should only happen if we run out of space on disk.
	 */
	if (!call_again && (ip == NULL)) {
		*ipp = NULL;
		return ENOSPC;
	}

	/*
	 * If call_again is set, then we were unable to get an
	 * inode in one operation.  We need to commit the current
	 * transaction and call xfs_ialloc() again.  It is guaranteed
	 * to succeed the second time.
	 */
	if (call_again) {

		/*
		 * Normally, xfs_trans_commit releases all the locks.
	         * We call bhold to hang on to the ialloc_context across
		 * the commit.  Holding this buffer prevents any other
		 * processes from doing any allocations in this 
		 * allocation group.
		 */
		xfs_trans_bhold (tp, ialloc_context);
		/*
		 * Save the log reservation so we can use
		 * them in the next transaction.
		 */
		log_res = xfs_trans_get_log_res(tp);
		log_count = xfs_trans_get_log_count(tp);
		ntp = xfs_trans_dup(tp);
		xfs_trans_commit (tp, 0);
		tp = ntp;
		if (committed != NULL)
			*committed = 1;

		if (code = xfs_trans_reserve (tp, 0, log_res, 0,
					      XFS_TRANS_PERM_LOG_RES,
					      log_count)) {
			ASSERT(0);
			*tpp = NULL;
			return code;
		}
		xfs_trans_bjoin (tp, ialloc_context);

		/*
		 * Call ialloc again. Since we've locked out all
		 * other allocations in this allocation group,
		 * this call should always succeed.
		 */
		ip = xfs_ialloc (tp, dp, mode, nlink, rdev, credp,
				 &ialloc_context, &call_again);

		ASSERT ((! call_again) && (ip != NULL));

	} else {
		if (committed != NULL)
			*committed = 0;
	}

	*ipp = ip;
	*tpp = tp;

	return 0;
}



/*
 * Decrement the link count on an inode & log the change.
 * If this causes the link count to go to zero, initiate the
 * logging activity required to truncate a file.
 */
STATIC void
xfs_droplink (xfs_trans_t *tp,
	      xfs_inode_t *ip)
{
        ASSERT (ip->i_d.di_nlink > 0);
        ip->i_d.di_nlink--;
        xfs_trans_log_inode(tp, ip, XFS_ILOG_CORE);

        if (ip->i_d.di_nlink == 0) {
                /*
                 * We're dropping the last link to this file.
		 * Move the on-disk inode to the AGI unlinked list.
		 * From xfs_inactive() we will pull the inode from
		 * the list and free it.
                 */
		xfs_iunlink(tp, ip);
        }
}

/*
 * Increment the link count on an inode & log the change.
 */
STATIC void
xfs_bumplink (xfs_trans_t *tp,
              xfs_inode_t *ip)
{
        ip->i_d.di_nlink++;
        xfs_trans_log_inode(tp, ip, XFS_ILOG_CORE);
}



/*
 * xfs_create
 *
 */
STATIC int
xfs_create(vnode_t	*dir_vp,
	   char		*name,
	   vattr_t	*vap,
	   enum vcexcl	excl,
	   int		I_mode,
	   vnode_t	**vpp,
	   cred_t	*credp)
{
	xfs_inode_t      	*dp, *ip = NULL;
        vnode_t		        *vp, *newvp;
	xfs_trans_t      	*tp = NULL;
        xfs_ino_t               e_inum;
        xfs_mount_t	        *mp;
	dev_t			rdev;
	unsigned long   	dir_generation;
        int                     error;
        xfs_bmap_free_t 	free_list;
        xfs_fsblock_t   	first_block;
	boolean_t		dp_joined_to_trans = B_FALSE;
	boolean_t		truncated = B_FALSE;
	uint			cancel_flags;
	int			committed;
	uint			truncate_flag;
	timestruc_t		tv;

	vn_trace_entry(dir_vp, "xfs_create");

try_again:

	mp = XFS_VFSTOM(dir_vp->v_vfsp);
	tp = xfs_trans_alloc (mp, 0);
	cancel_flags = XFS_TRANS_RELEASE_LOG_RES;
	if (error = xfs_trans_reserve (tp, XFS_IALLOC_BLOCKS(mp) + 12,
				       XFS_ITRUNCATE_LOG_RES(mp), 0,
				       XFS_TRANS_PERM_LOG_RES,
				       XFS_ITRUNCATE_LOG_COUNT)) {
		cancel_flags = 0;
		dp = NULL;
		goto error_return;
	}

        dp = XFS_VTOI(dir_vp);

	xfs_ilock (dp, XFS_ILOCK_EXCL);

	if (error = xfs_iaccess(dp, IEXEC, credp))
                goto error_return;

	error = xfs_dir_lookup_int (NULL, dir_vp, DLF_IGET, name, NULL, 
				    &e_inum, &ip);
	if ((error != 0) && (error != ENOENT)) 
		goto error_return;

	XFS_BMAP_INIT(&free_list, &first_block);

	if (error == ENOENT) {

		ASSERT (ip == NULL);

		if (error = xfs_iaccess(dp, IWRITE, credp)) {
			goto error_return;
		}

		rdev = (vap->va_mask & AT_RDEV) ? vap->va_rdev : NODEV;

		error = xfs_dir_ialloc(&tp, dp, 
				       MAKEIMODE(vap->va_type,vap->va_mode), 
				       1, rdev, credp, &ip, &committed);
		if (error)
			goto error_return;
		ITRACE(ip);

		/*
		 * At this point, we've gotten a newly allocated inode.
		 * It is locked (and joined to the transaction).
		 */

		ASSERT (ismrlocked (&ip->i_lock, MR_UPDATE));

		/*
		 * Now we join the directory inode to the transaction.
		 * We do not do it earlier because xfs_dir_ialloc
		 * might commit the previous transaction (and release
		 * all the locks).
		 */

		VN_HOLD (dir_vp);
		xfs_trans_ijoin (tp, dp, XFS_ILOCK_EXCL);
		dp_joined_to_trans = B_TRUE;

		/*
		 * XXX Need to sanity check namelen.
		 */

		if (error = xfs_dir_createname (tp, dp, name, ip->i_ino,
						&first_block, &free_list,
						MAX_EXT_NEEDED)) 
			ASSERT (0);	/* we've reserved the space. */

		dnlc_enter(dir_vp, name, XFS_ITOV(ip), NOCRED);

	}
	else {
		ASSERT (ip != NULL);
		ITRACE(ip);

		/* Now lock the inode also, in the right order. */
		if (dp->i_ino < ip->i_ino) {
			xfs_ilock (ip, XFS_ILOCK_EXCL | XFS_IOLOCK_EXCL);
		}
		else if (! xfs_ilock_nowait (ip,
					     (XFS_ILOCK_EXCL |
					      XFS_IOLOCK_EXCL))) {
			/*
			 * We're out of order and the trylock failed.
			 */
			dir_generation = dp->i_gen;
			xfs_iunlock (dp, XFS_ILOCK_EXCL);
			xfs_ilock (ip, XFS_ILOCK_EXCL | XFS_IOLOCK_EXCL);
			xfs_ilock (dp, XFS_ILOCK_EXCL);
			
			/*
			 * If things have changed while the dp was
			 * unlocked, drop all the locks and try
			 * again.
			 */
			if (dp->i_gen != dir_generation) {
				xfs_trans_cancel (tp, cancel_flags);
				xfs_iunlock(dp, XFS_ILOCK_EXCL);
				xfs_iunlock(ip,
					    XFS_ILOCK_EXCL | XFS_IOLOCK_EXCL);
				IRELE(ip);
				goto try_again;
			}
		}

		vp = XFS_ITOV(ip);
		if (excl == EXCL)
                        error = XFS_ERROR(EEXIST);
		else if (vp->v_type == VDIR && (I_mode & IWRITE))
                        error = XFS_ERROR(EISDIR);
		else if (I_mode)
			error = xfs_iaccess(ip, I_mode, credp);

		if (!error &&
		    (vp->v_type == VREG) &&
		    (vap->va_mask & AT_SIZE) &&
		    (ip->i_d.di_size != 0)) {
			/*
			 * We need to drop the inode lock in order to call
			 * xfs_itruncate_start().  The relocking when we're
			 * out of order is a bit of a nightmare, but we
			 * need to preserve lock ordering.
			 */
			xfs_iunlock (ip, XFS_ILOCK_EXCL);
			if (dp->i_ino < ip->i_ino) {
				/*
				 * We're in order so we know we won't have
				 * to bail out.
				 */
				xfs_itruncate_start (ip, XFS_ITRUNC_DEFINITE,
						     0);
				xfs_ilock (ip, XFS_ILOCK_EXCL);
			} else {
				/*
				 * We're out of order.
				 */
				xfs_itruncate_start (ip, XFS_ITRUNC_MAYBE, 0);
				if (!xfs_ilock_nowait (ip, XFS_ILOCK_EXCL)) {
					/*
					 * We can't get the inode lock, so
					 * drop the directory lock and do
					 * things in the right order.  We
					 * don't have to drop the I/O lock,
					 * because for these inode numbers
					 * it is in order.  If the directory
					 * generation count changes while
					 * we're doing this, then start all
					 * over again.
					 */
					dir_generation = dp->i_gen;
					xfs_iunlock (dp, XFS_ILOCK_EXCL);
					xfs_ilock (ip, XFS_ILOCK_EXCL);
					xfs_ilock (dp, XFS_ILOCK_EXCL);

					if (dp->i_gen != dir_generation) {
						xfs_trans_cancel(tp,
							cancel_flags);
						xfs_iunlock(dp,
							    XFS_ILOCK_EXCL);
						xfs_iunlock(ip,
							    XFS_ILOCK_EXCL |
							    XFS_IOLOCK_EXCL);
						IRELE(ip);
						goto try_again;
					}
				}
			}
		}
			
		IHOLD(dp);
		xfs_trans_ijoin (tp, dp, XFS_ILOCK_EXCL);
		dp_joined_to_trans = B_TRUE;
		xfs_trans_ijoin (tp, ip, XFS_ILOCK_EXCL | XFS_IOLOCK_EXCL);

		if (error)
			goto error_return;

		if (vp->v_type == VREG && (vap->va_mask & AT_SIZE)) {
			/*
			 * Truncate the file.  The timestamps must
			 * be updated whether the file is changed
			 * or not.
			 */
			ASSERT(vap->va_size == 0);
			if (ip->i_d.di_size > 0) {
				xfs_trans_ihold(tp, ip);
				xfs_itruncate_finish(&tp, ip, (xfs_fsize_t)0);
				truncated = B_TRUE;
			}
			nanotime(&tv);
			ip->i_d.di_mtime.t_sec = tv.tv_sec;
			ip->i_d.di_ctime.t_sec = tv.tv_sec;
			xfs_trans_log_inode(tp, ip, XFS_ILOG_CORE);
		}
	}


	/*
	 * xfs_trans_commit normally decrements the vnode ref count
	 * when it unlocks the inode. Since we want to return the 
	 * vnode to the caller, we bump the vnode ref count now.
	 */

	IHOLD (ip);
	vp = XFS_ITOV (ip);

	(void) xfs_bmap_finish (&tp, &free_list, first_block);

	xfs_trans_commit (tp, XFS_TRANS_RELEASE_LOG_RES);
	if (truncated) {
		/*
		 * If we truncated the file, then the inode will
		 * have been held within the transaction and must
		 * be unlocked now.
		 */
		xfs_iunlock (ip, XFS_ILOCK_EXCL | XFS_IOLOCK_EXCL);
		ASSERT (vp->v_count >= 2);
		IRELE (ip);
	}
	ASSERT (dp_joined_to_trans);

        /*
         * If vnode is a device, return special vnode instead.
         */
        if (ISVDEV(vp->v_type)) {
                newvp = specvp(vp, vp->v_rdev, vp->v_type, credp);
                VN_RELE(vp);
                if (newvp == NULL)
                        return XFS_ERROR(ENOSYS);
                vp = newvp;
        }

#ifndef SIM
	if (truncated && vp->v_type == VREG && VN_MAPPED(vp))
                remapf(vp, 0, 0);
#endif

        *vpp = vp;

	return 0;

error_return:

	if (tp != NULL)
		xfs_trans_cancel (tp, cancel_flags);

	if (! dp_joined_to_trans && (dp != NULL))
		xfs_iunlock (dp, XFS_ILOCK_EXCL);
	ASSERT(!truncated);

	return error;

}



/*
 * The following routine will lock the inodes associated with the
 * directory and the named entry in the directory. The locks are
 * acquired in increasing inode number.
 * 
 * If the entry is "..", then only the directory is locked. The
 * vnode ref count will still include that from the .. entry in
 * this case.
 */

STATIC int
xfs_lock_dir_and_entry (xfs_inode_t	*dp,
			char		*name,
			xfs_inode_t	**ipp)	/* inode of entry 'name' */
{
	xfs_inode_t	*ip, *new_ip;
	xfs_ino_t	e_inum;
	unsigned long	dir_generation;
	int		error;

again:

        xfs_ilock (dp, XFS_ILOCK_EXCL);

	error = xfs_dir_lookup_int (NULL, XFS_ITOV(dp), DLF_IGET, name, 
				    NULL, &e_inum, &ip);
        if (error) {
                xfs_iunlock (dp, XFS_ILOCK_EXCL);
		*ipp = NULL;
                return error;
        }

        ASSERT ((e_inum != 0) && ip);
	ITRACE(ip);

        /*
         * We want to lock in increasing inum. Since we've already
         * acquired the lock on the directory, we may need to release
         * if if the inum of the entry turns out to be less.
         */
	if (e_inum > dp->i_ino) {
		/*
		 * We are already in the right order, so just 
		 * lock on the inode of the entry.
		 */
		xfs_ilock (ip, XFS_ILOCK_EXCL);
	}
        else if (e_inum < dp->i_ino) {
                dir_generation = dp->i_gen;
                xfs_iunlock (dp, XFS_ILOCK_EXCL);

		xfs_ilock (ip, XFS_ILOCK_EXCL);
                xfs_ilock (dp, XFS_ILOCK_EXCL);

                /*
                 * Make sure that things are still consistent during
                 * the period we dropped the directory lock.
                 * Do a new lookup if directory was changed.
                 */
                if (dp->i_gen != dir_generation) {
			error = xfs_dir_lookup_int (NULL, XFS_ITOV(dp),
				DLF_IGET, name, NULL, &e_inum, &new_ip);
			ITRACE(new_ip);
			IRELE (new_ip); /* ref from xfs_dir_lookup_int */
                        if (error) {
				xfs_iunlock (dp, XFS_ILOCK_EXCL);
				xfs_iunlock (ip, XFS_ILOCK_EXCL);
				*ipp = NULL;
				return error;
			}

                        if (new_ip != ip) {

				/*
				 * Things have changed too much,
				 * just goto the top & start again.
				 */
				xfs_iunlock (dp, XFS_ILOCK_EXCL);
                                xfs_iunlock (ip, XFS_ILOCK_EXCL);
				IRELE (ip);
				IRELE (new_ip);

				goto again;
                        }
                }
        }
	/* else  e_inum == dp->i_ino */
		/*     This can happen if we're asked to lock /x/..
		 *     the entry is "..", which is also the parent directory.
		 */

	*ipp = ip;

	return 0;
}


/*
 * The following routine will acquire the locks required for a rename
 * operation. The code understands the semantics of renames and will
 * validate that name1 exists under dp1 & that name2 may or may not
 * exist under dp2.
 *
 * We are renaming dp1/name1 to dp2/name2.
 *
 * Return ENOENT if dp1 does not exist, other lookup errors, or 0 for success.
 * Return EAGAIN if the caller needs to try again.
 */
STATIC int
xfs_lock_for_rename(
		 xfs_inode_t	*dp1,	/* old (source) directory inode */
		 xfs_inode_t	*dp2,	/* new (target) directory inode */
		 char		*name1,	/* old entry name */
		 char		*name2,	/* new entry name */
		 xfs_inode_t	**ipp1,	/* inode of old entry */
		 xfs_inode_t    **ipp2) /* inode of new entry, if it 
				           already exists, NULL otherwise. */
{
	xfs_inode_t	*ip1, *ip2, *temp;
	xfs_ino_t	inum1, inum2;
	unsigned long	dir_gen1, dir_gen2;
	int		error;
	xfs_inode_t	*i_tab[4];
	int		num_inodes;
	int		i, j;
	uint		lock_mode;

	ip2 = NULL;

	/*
	 * First, find out the current inums of the entries so that we
	 * can determine the initial locking order.  We'll have to 
	 * sanity check stuff after all the locks have been acquired
	 * to see if we still have the right inodes, directories, etc.
	 */
        lock_mode = xfs_ilock_map_shared (dp1);
        error = xfs_dir_lookup_int(NULL, XFS_ITOV(dp1), DLF_IGET,
				   name1, NULL, &inum1, &ip1);

	/*
	 * Save the current generation so that we can detect if it's
	 * modified between when we drop the lock & reacquire it down
	 * below.  We only need to do this for the src directory since
	 * the target entry does not need to exist yet. 
	 */
	dir_gen1 = dp1->i_gen;
	xfs_iunlock_map_shared (dp1, lock_mode);
        if (error)
                return error;
	ASSERT (ip1);
	ITRACE(ip1);

        lock_mode = xfs_ilock_map_shared (dp2);
        error = xfs_dir_lookup_int(NULL, XFS_ITOV(dp2), DLF_IGET,
				   name2, NULL, &inum2, &ip2);
	dir_gen2 = dp2->i_gen;
        xfs_iunlock_map_shared (dp2, lock_mode);
	if (error == ENOENT) {		/* target does not need to exist. */
		inum2 = 0;
	}
        else if (error) {
		IRELE (ip1);
                return error;
	}
	else
		ITRACE(ip2);

	/*
	 * i_tab contains a list of pointers to inodes.  We initialize
	 * the table here & we'll sort it.  We will then use it to 
	 * order the acquisition of the inode locks.
	 *
	 * Note that the table may contain duplicates.  e.g., dp1 == dp2.
	 */
        i_tab[0] = dp1;
        i_tab[1] = dp2;
        i_tab[2] = ip1;
	if (inum2 == 0) {
		num_inodes = 3;
		i_tab[3] = NULL;
	}
	else {
		num_inodes = 4;
        	i_tab[3] = ip2;
	}

	/*
	 * Sort the elements via bubble sort.  (Remember, there are at
	 * most 4 elements to sort, so this is adequate.)
	 */
	for (i=0; i < num_inodes; i++) {
		for (j=1; j < num_inodes; j++) {
			if (i_tab[j]->i_ino < i_tab[j-1]->i_ino) {
				temp = i_tab[j];
				i_tab[j] = i_tab[j-1];
				i_tab[j-1] = temp;
			}
		}
	}

	/*
	 * Lock all the inodes in exclusive mode. If an inode appears
	 * twice in the list, it will only be locked once.
	 */
	xfs_ilock (i_tab[0], XFS_ILOCK_EXCL);
	for (i=1; i < num_inodes; i++) {
		if (i_tab[i] != i_tab[i-1])
			xfs_ilock (i_tab[i], XFS_ILOCK_EXCL);
	}

	/*
	 * See if either of the directories was modified during the
	 * interval between when the locks were released and when
	 * they were reacquired.
	 */
	if (dp1->i_gen != dir_gen1 || dp2->i_gen != dir_gen2) {
		/*
		 * Someone else may have linked in a new inode
		 * with the same name.  If so, we'll need to
		 * release our locks & go through the whole
		 * thing again.
		 */
		xfs_iunlock (i_tab[0], XFS_ILOCK_EXCL);
		for (i=1; i < num_inodes; i++) {
			if (i_tab[i] != i_tab[i-1])
				xfs_iunlock (i_tab[i], XFS_ILOCK_EXCL);
		}
		if (num_inodes == 4)
			IRELE (ip2);
		IRELE (ip1);
		return EAGAIN;
        }


	/*
	 * Set the return value.
	 */
	*ipp1 = *ipp2 = NULL;
	for (i=0; i < num_inodes; i++) {
		if (i_tab[i]->i_ino == inum1)
			*ipp1 = i_tab[i];
		if (i_tab[i]->i_ino == inum2)
			*ipp2 = i_tab[i];
	}
	return 0;
}

/*
 * The following routine will lock 2 inodes in exclusive mode.
 * Lock ordering is preserved.
 */
STATIC void
xfs_lock_2_inodes (
	xfs_inode_t	*ip1,
	xfs_inode_t	*ip2)
{
	if (ip1->i_ino < ip2->i_ino) {
		xfs_ilock (ip1, XFS_ILOCK_EXCL);
		xfs_ilock (ip2, XFS_ILOCK_EXCL);
	}
	else {
		xfs_ilock (ip2, XFS_ILOCK_EXCL);
		xfs_ilock (ip1, XFS_ILOCK_EXCL);
	}
}


/*
 * xfs_remove
 *
 */
STATIC int
xfs_remove(vnode_t	*dir_vp,
	   char		*name,
	   cred_t	*credp)
{
        xfs_inode_t             *dp, *ip;
        xfs_trans_t             *tp = NULL;
        xfs_ino_t               e_inum;
	xfs_mount_t		*mp;
        int                     error = 0;
	unsigned long		dir_generation;
        xfs_bmap_free_t         free_list;
        xfs_fsblock_t           first_block;
	int			cancel_flags;

	vn_trace_entry(dir_vp, "xfs_remove");
	mp = XFS_VFSTOM(dir_vp->v_vfsp);
	tp = xfs_trans_alloc (mp, 0);
	cancel_flags = XFS_TRANS_RELEASE_LOG_RES;
        if (error = xfs_trans_reserve (tp, 10, XFS_REMOVE_LOG_RES(mp),
				       0, XFS_TRANS_PERM_LOG_RES,
				       XFS_REMOVE_LOG_COUNT)) {
		cancel_flags = 0;
                goto error_return;
	}

	dp = XFS_VTOI(dir_vp);
        ip = NULL;

	if (error = xfs_iaccess (dp, IEXEC | IWRITE, credp))
		goto error_return;

	error = xfs_lock_dir_and_entry (dp, name, &ip);
	if (error) {
		goto error_return;
	}

	if (error = xfs_stickytest (dp, ip, credp))
                goto error_return;


	/*
	 * At this point, we've gotten both the directory and the entry
	 * inodes locked.
	 */
	xfs_trans_ijoin (tp, dp, XFS_ILOCK_EXCL);
	if (dp != ip) {
		/* Increment vnode ref count only in this case since
		 * there's an extra vnode reference in the case where
		 * dp == ip.
		 */
		IHOLD (dp);
		xfs_trans_ijoin (tp, ip, XFS_ILOCK_EXCL);
	}


	if (XFS_ITOV(ip)->v_vfsmountedhere) {
                        error = XFS_ERROR(EBUSY);
			goto error_return;
	}
	if ((ip->i_d.di_mode & IFMT) == IFDIR) {
                        error = XFS_ERROR(EPERM);
			goto error_return;
	}

	/*
	 * Return error when removing . and ..
	 */
	if (name[0] == '.') {
		if (name[1] == '\0') {
			error = XFS_ERROR(EINVAL);
			goto error_return;
		}
		else if (name[1] == '.' && name[2] == '\0') {
			error = XFS_ERROR(EEXIST);
			goto error_return;
		}
	} 

	/*
	 * Entry must exist since we did a lookup in xfs_lock_dir_and_entry.
	 */
	XFS_BMAP_INIT(&free_list, &first_block);
	error = xfs_dir_removename (tp, dp, name, &first_block, &free_list, 0);
	ASSERT (error == 0);

	dnlc_remove (dir_vp, name);

	dp->i_gen++;
	xfs_trans_log_inode (tp, dp, XFS_ILOG_CORE);

	xfs_droplink (tp, ip);

	/*
	 * Take an extra ref on the inode so that it doesn't
	 * go to xfs_inactive() from within the commit.
	 */
	IHOLD(ip);

	(void) xfs_bmap_finish (&tp, &free_list, first_block);

	xfs_trans_commit (tp, XFS_TRANS_RELEASE_LOG_RES);
	IRELE(ip);

	return 0;

error_return:
	xfs_trans_cancel (tp, cancel_flags);
	return error;

}


/*
 * xfs_link
 *
 */
STATIC int
xfs_link(vnode_t	*target_dir_vp,
	 vnode_t	*src_vp,
	 char		*target_name,
	 cred_t		*credp)
{
	struct vnode		*realvp;
        xfs_inode_t		*tdp, *sip;
	xfs_ino_t		e_inum;
	xfs_trans_t		*tp;
	xfs_mount_t		*mp;
	int			error;
        xfs_bmap_free_t         free_list;
        xfs_fsblock_t           first_block;
	int			cancel_flags;

	vn_trace_entry(target_dir_vp, "xfs_link");
	/*
	 * Get the real vnode.
	 */
	if (VOP_REALVP(src_vp, &realvp) == 0)
                src_vp = realvp;
	vn_trace_entry(src_vp, "xfs_link");
        if (src_vp->v_type == VDIR)
                return XFS_ERROR(EPERM);

	mp = XFS_VFSTOM(target_dir_vp->v_vfsp);
        tp = xfs_trans_alloc (mp, 0);
	cancel_flags = XFS_TRANS_RELEASE_LOG_RES;
        if (error = xfs_trans_reserve (tp, 10, XFS_LINK_LOG_RES(mp),
				       0, XFS_TRANS_PERM_LOG_RES,
				       XFS_LINK_LOG_COUNT)) {
		cancel_flags = 0;
                goto error_return;
	}

	sip = XFS_VTOI(src_vp);
	tdp = XFS_VTOI(target_dir_vp);

	xfs_lock_2_inodes (sip, tdp);

	/*
	 * Increment vnode ref counts since xfs_trans_commit &
	 * xfs_trans_cancel will both unlock the inodes and
	 * decrement the associated ref counts.
	 */
	VN_HOLD(src_vp);
	VN_HOLD(target_dir_vp);
	xfs_trans_ijoin (tp, sip, XFS_ILOCK_EXCL);
        xfs_trans_ijoin (tp, tdp, XFS_ILOCK_EXCL);

	if (error = xfs_iaccess (tdp, IEXEC | IWRITE, credp))
                goto error_return;

	error = xfs_dir_lookup_int(NULL, target_dir_vp, 0, target_name, 
				   NULL, &e_inum, NULL);
	if (error != ENOENT) {
		if (error == 0) 
			error = XFS_ERROR(EEXIST);
		goto error_return;
	}

	XFS_BMAP_INIT(&free_list, &first_block);

	if (error = xfs_dir_createname (tp, tdp, target_name, sip->i_ino,
					&first_block, &free_list,
					MAX_EXT_NEEDED)) 
		goto error_return;

	tdp->i_gen++;
	xfs_trans_log_inode (tp, tdp, XFS_ILOG_CORE);


	xfs_bumplink(tp, sip);

	(void) xfs_bmap_finish (&tp, &free_list, first_block);
	xfs_trans_commit (tp, XFS_TRANS_RELEASE_LOG_RES);

	dnlc_enter (target_dir_vp, target_name, XFS_ITOV(sip), credp);

	return 0;

error_return:
	xfs_trans_cancel (tp, cancel_flags);

	return error;
}


/*
 * xfs_ancestor_check.
 *
 * Routine called by xfs_rename to make sure that we are not moving
 * a directory under one of its children. This would have the effect
 * of orphaning the whole directory subtree.
 *
 * If two calls to xfs_ancestor_check overlapped execution, the 
 * partially completed work of one call could be invalidated by the
 * rename that activated the other.  To avoid this, we serialize
 * using xfs_ancestormon.
 *
 * This routine will internally release all the inode locks and
 * then reacquire them before it returns. If the state of any of
 * the inodes have changed in the interim, state_has_changed will
 * be set to true.  In this case, the caller should take the 
 * appropriate recovery action - e.g., restart the whole rename
 * operation again.
 */
STATIC int
xfs_ancestor_check (xfs_inode_t *src_dp, 
		    xfs_inode_t *src_ip,
		    xfs_inode_t *target_dp,
		    xfs_inode_t *target_ip, 
		    boolean_t	*state_has_changed)
{
	struct {
		xfs_inode_t     *ip;
		unsigned long   gen;
	}		i_tab[4], temp;
	int		i, j, num_inodes;
	xfs_mount_t	*mp;
	xfs_inode_t	*ip;
	xfs_ino_t	root_ino;
	int		error = 0;

	mp = src_dp->i_mount;
	root_ino = mp->m_sb.sb_rootino;

	/*
	 * We know that all the inodes involved are directories at this
	 * point.
	 */
	ASSERT ((src_dp->i_d.di_mode & IFMT) == IFDIR);
	ASSERT ((target_dp->i_d.di_mode & IFMT) == IFDIR);
	if (src_ip != NULL)
		ASSERT ((src_ip->i_d.di_mode & IFMT) == IFDIR);
	if (target_ip != NULL)
		ASSERT ((target_ip->i_d.di_mode & IFMT) == IFDIR);

	/*
	 * Assert that all the relationships that were checked by our
	 * caller are true!
	 */
	ASSERT(src_ip != src_dp);
	ASSERT(target_ip != target_dp);
	ASSERT(src_ip != target_ip);
	ASSERT(src_dp != target_dp);

	/*
	 * Record all the inodes and their current generations.
	 *
	 * There are at least 2 entries in table.  May have duplicate 
	 * entries if src_ip == target_dp, for example.
	 */
	i_tab[0].ip = src_dp;
	i_tab[0].gen = src_dp->i_gen;
        i_tab[1].ip = src_ip;
	i_tab[1].gen = src_ip->i_gen;
        i_tab[2].ip = target_dp;
	i_tab[2].gen = target_dp->i_gen;
	if (target_ip) {
		i_tab[3].ip = target_ip;
		i_tab[3].gen = target_ip->i_gen;
		num_inodes = 4;
	}
	else
		num_inodes = 3;

        /*
         * Sort the elements.
         */
        for (i=0; i < num_inodes; i++) {
                for (j=1; j < num_inodes; j++) {
                        if (i_tab[j].ip->i_ino < i_tab[j-1].ip->i_ino) {
                                temp = i_tab[j];
                                i_tab[j] = i_tab[j-1];
                                i_tab[j-1] = temp;
                        }
                }
        }

	/*
         * Release all the inode locks before acquiring xfs_ancestormon.
         * We do this to avoid deadlocking with another process that
         * already has xfs_ancestormon and is trying to get one of
         * our directories via "..". 
         */
	xfs_iunlock (src_dp, XFS_ILOCK_EXCL);
	/*
	 * src_ip can't be same as src_dp
	 */
	xfs_iunlock (src_ip, XFS_ILOCK_EXCL);
	/*
	 * target_dp can't be same as src_dp
	 */
	if (target_dp != src_ip)
		xfs_iunlock (target_dp, XFS_ILOCK_EXCL);
	/*
	 * target_ip can't be same as src_ip or target_dp
	 */
	if (target_ip && target_ip != src_dp)
		xfs_iunlock (target_ip, XFS_ILOCK_EXCL);

	psema (&xfs_ancestormon, PINOD);

	/*
	 * Ascend the target_dp's ancestor line, stopping if we
	 * either encounter src_ip (failed check), or reached the
	 * root of the filesystem.
	 * If we discover an anomaly, e.g., ".." missing, return
	 * ENOENT.
	 *
	 * In this loop we need to lock the inodes exclusively since
	 * we can't really get at the xfs_ilock_map_shared() interface
	 * through xfs_dir_lookup_int().
	 */
	ip = target_dp;
	xfs_ilock (ip, XFS_ILOCK_EXCL);

	while (ip->i_ino != root_ino) {
		xfs_ino_t	parent_ino;

		if (ip == src_ip) {
			error = XFS_ERROR(EINVAL);
			break;
		}
		if (error = xfs_dir_lookup_int (NULL, XFS_ITOV(ip), 0, "..",
						NULL, &parent_ino, NULL))
			break;
		if (parent_ino == ip->i_ino) {
			prdev("Directory inode %lld has bad parent link",
                              ip->i_dev, ip->i_ino);
                        error = XFS_ERROR(ENOENT);
                        break;
		}

		/*
		 * Now release ip and get its parent inode.
		 * If we're on the first pass through this loop and
		 * ip == target_dp, then we do not want to release
		 * the vnode reference.
		 */
		xfs_iunlock (ip, XFS_ILOCK_EXCL);
		if (ip != target_dp)
			IRELE (ip);

		ip = xfs_iget(mp, NULL, parent_ino, XFS_ILOCK_EXCL);
		if (((ip->i_d.di_mode & IFMT) != IFDIR) ||
		    (ip->i_d.di_nlink <= 0)) {
                        prdev("Ancestor inode %d is not a directory",
                                ip->i_dev, ip->i_ino);
                        error = XFS_ERROR(ENOTDIR);
                        break;
                }
	}

	/*
	 * Release the lock on the inode, taking care to decrement the 
	 * vnode reference count only if ip != target_dp.
	 */
	xfs_iunlock (ip, XFS_ILOCK_EXCL);
	if (ip != target_dp)
		IRELE (ip);

	/*
         * Reacquire all the inode locks in exclusive mode. If an inode 
	 * appears twice in the list, it will only be locked once.
         */
        xfs_ilock (i_tab[0].ip, XFS_ILOCK_EXCL);
        for (i=1; i < num_inodes; i++) {
                if (i_tab[i].ip != i_tab[i-1].ip)
                        xfs_ilock (i_tab[i].ip, XFS_ILOCK_EXCL);
        }

	/*
	 * Unserialize efs_notancestor calls.
         */
        vsema(&xfs_ancestormon);

	/*
	 * See if anything has changed when they were unlocked.
	 */
	*state_has_changed = B_FALSE;
	for (i=1; i < num_inodes; i++) {
		if (i_tab[i].gen != i_tab[i].ip->i_gen) {
			*state_has_changed = B_TRUE;
			break;
		}
	}

	return error;

}




/*
 * xfs_rename
 *
 */
STATIC int
xfs_rename(vnode_t	*src_dir_vp,
	  char		*src_name,
	  vnode_t	*target_dir_vp,
	  char		*target_name,
	  pathname_t	*target_pnp,
	  cred_t	*credp)
{
	xfs_trans_t	*tp;
	xfs_inode_t	*src_dp, *target_dp, *src_ip, *target_ip;
	xfs_mount_t	*mp;
	boolean_t	new_parent;		/* moving to a new dir */
	boolean_t	src_is_directory;	/* src_name is a directory */
	boolean_t	state_has_changed;
	int		error;		
        xfs_bmap_free_t free_list;
        xfs_fsblock_t   first_block;
	int		cancel_flags;

	vn_trace_entry(src_dir_vp, "xfs_rename");
	vn_trace_entry(target_dir_vp, "xfs_rename");

start_over:

	mp = XFS_VFSTOM(src_dir_vp->v_vfsp);
	tp = xfs_trans_alloc (mp, 0);
	cancel_flags = XFS_TRANS_RELEASE_LOG_RES;
        if (error = xfs_trans_reserve (tp, 10, XFS_RENAME_LOG_RES(mp),
				       0, XFS_TRANS_PERM_LOG_RES,
				       XFS_RENAME_LOG_COUNT)) {
		cancel_flags = 0;
                goto error_return;
	}

	XFS_BMAP_INIT(&free_list, &first_block);

	/*
	 * Lock all the participating inodes. Depending upon whether
	 * the target_name exists in the target directory, and
	 * whether the target directory is the same as the source
	 * directory, we can lock from 2 to 4 inodes.
	 * xfs_lock_for_rename() will return ENOENT if src_name
	 * does not exist in the source directory.
	 */
	src_dp = XFS_VTOI(src_dir_vp);
	ASSERT (src_dp->i_d.di_nlink >= 2);
        target_dp = XFS_VTOI(target_dir_vp);
	ASSERT (target_dp->i_d.di_nlink >= 2);
	while ((error = xfs_lock_for_rename(src_dp, target_dp, src_name,
				target_name, &src_ip, &target_ip)) == EAGAIN)
		continue;
	if (error)
		goto error_return;

	ASSERT (src_ip != NULL);

	ASSERT (! ((src_name[0] == '.') && (src_name[1] == '\0')));
	ASSERT (! ((target_name[0] == '.') && (target_name[1] == '\0')));

	new_parent = (src_dp != target_dp);

	/*
	 * Join all the inodes to the transaction. From this point on,
	 * we can rely on either trans_commit or trans_cancel to unlock
	 * them.  Note that we need to add a vnode reference to the
	 * directories since trans_commit & trans_cancel will decrement
	 * them when they unlock the inodes.  Also, we need to be careful
	 * not to add an inode to the transaction more than once.
	 */
        VN_HOLD (src_dir_vp);
        xfs_trans_ijoin (tp, src_dp, XFS_ILOCK_EXCL);
        if (new_parent) {
                VN_HOLD (target_dir_vp);
                xfs_trans_ijoin (tp, target_dp, XFS_ILOCK_EXCL);
        }
	if ((src_ip != src_dp) && (src_ip != target_dp)) {
		xfs_trans_ijoin (tp, src_ip, XFS_ILOCK_EXCL);
	}
        if (target_ip &&
	    (target_ip != src_ip) &&
	    (target_ip != src_dp) &&
	    (target_ip != target_dp)) {
                xfs_trans_ijoin (tp, target_ip, XFS_ILOCK_EXCL);
	}



	/*
	 * Make all the access checks.
	 */
	if (error = xfs_iaccess (src_dp, IEXEC | IWRITE, credp))
                goto error_return;
	if (error = xfs_stickytest (src_dp, src_ip, credp))
		goto error_return;

	if (target_dp && (src_dp != target_dp)) {
		if (error = xfs_iaccess (target_dp, IEXEC | IWRITE, credp))
			goto error_return;
		if ((target_ip != NULL) &&
		    (error = xfs_stickytest (target_dp, target_ip, credp)))
			goto error_return;
	}
	else {
		if ((target_ip != NULL) &&
		    (error = xfs_stickytest (src_dp, target_ip, credp)))
                        goto error_return;
	}

	if ((src_ip == src_dp) || (target_ip == target_dp)) {
		error = XFS_ERROR(EINVAL);
		goto error_return;
	}

	/*
	 * Source and target are identical.
	 */
	if (src_ip == target_ip) {
		error = 0;		/* no-op */
		goto error_return;
	}

	/*
	 * Directory renames require special checks.
	 */
	src_is_directory = ((src_ip->i_d.di_mode & IFMT) == IFDIR);

	if (src_is_directory) {

		ASSERT (src_ip->i_d.di_nlink >= 2);

		/*
		 * Cannot rename ".."
		 */
		if ((src_name[0] == '.') && (src_name[1] == '.') &&
		    (src_name[2] == '\0')) {
			error = XFS_ERROR(EINVAL);
			goto error_return;
		}
                if ((target_name[0] == '.') && (target_name[1] == '.') &&
                    (target_name[2] == '\0')) {
                        error = XFS_ERROR(EINVAL);
                        goto error_return;
                }


		if (src_dp != target_dp) {
		/*
			 * Check whether the rename would orphan the tree
			 * rooted at src_ip by moving it under itself.
			 *
			 * This is very hairy.  We'll have to release all
			 * the locks, do the ancestor check, and then
			 * reacquire all the locks.
			 * After we reacquire the locks, we'll have to run
			 * through all the checks again if any of the
			 * directories have been modified.  To simplify it,
			 * we'll just return to the top and redo all this.
			 * Fortunately, this is a rare case.
			 */

			if (error = xfs_ancestor_check (src_dp, src_ip,
				target_dp, target_ip, &state_has_changed)) 
			goto error_return;

			if (state_has_changed) {
				xfs_trans_cancel (tp, 0);
				goto start_over;
			}
		}

	}


	/*
	 * We should be in the same file system.
	 */
	ASSERT (XFS_VFSTOM(src_dir_vp->v_vfsp) ==
		XFS_VFSTOM(target_dir_vp->v_vfsp));

	/*
	 * Set up the target.
	 */

	if (target_ip == NULL) {

		/*
		 * If target does not exist and the rename crosses directories,
		 * adjust the target directory link count to account for the
		 * ".." reference from the new entry.
		 */
		if (error = xfs_dir_createname (tp, target_dp, target_name,
						src_ip->i_ino, &first_block,
						&free_list, MAX_EXT_NEEDED))
			goto error_return;

		if (new_parent && src_is_directory)
			xfs_bumplink(tp, target_dp);
	}
	else { /* target_ip != NULL */

		/*
		 * If target exists and it's a directory, check that both
		 * target and source are directories and that target can be
		 * destroyed, or that neither is a directory.
		 */

		if ((target_ip->i_d.di_mode & IFMT) == IFDIR) {

			/*
			 * Make sure target dir is empty.
			 */
			if (! (xfs_dir_isempty(target_dp)) || 
			    (target_ip->i_d.di_nlink > 2)) {
				error = XFS_ERROR(EEXIST);
                                goto error_return;
                        }

			/*
			 * Make sure src is a directory.
			 */
			if (! src_is_directory) {
				error = XFS_ERROR(EISDIR);
				goto error_return;
			}

			if (ABI_IS_SVR4(u.u_procp->p_abi) &&
                            XFS_ITOV(target_ip)->v_vfsmountedhere) {
                                error = XFS_ERROR(EBUSY);
                                goto error_return;
                        }


		}
		else {
			if (src_is_directory) {
				error = XFS_ERROR(ENOTDIR);
				goto error_return;
			}
		}
	       
		/*
		 * Purge all dnlc references to the old target.
		 */
		dnlc_purge_vp(XFS_ITOV(target_ip));

		/*
		 * Link the source inode under the target name.
		 * If the source inode is a directory and we are moving
		 * it across directories, its ".." entry will be 
		 * inconsistent until we replace that down below.
		 *
		 * In case there is already an entry with the same
		 * name at the destination directory, remove it first.
		 */
		error = xfs_dir_replace (tp, target_dp, target_name,
			((target_pnp != NULL) ? target_pnp->pn_complen :
			 strlen(target_name)), src_ip->i_ino);
		ASSERT (!error);

		dnlc_enter (target_dir_vp, target_name,
			    XFS_ITOV(src_ip), credp);

		/*
		 * Decrement the link count on the target since the target
		 * dir no longer points to it.
		 */
		xfs_droplink (tp, target_ip);

		if (src_is_directory) {
			/*
			 * Drop the link from the old "." entry.
			 */
			xfs_droplink (tp, target_ip);
		} 


	} /* target_ip != NULL */



	/*
	 * Remove the source.
	 */
	if (new_parent && src_is_directory) {

		/*
		 * Rewrite the ".." entry to point to the new 
	 	 * directory.
		 */
		error = xfs_dir_replace (tp, src_ip, "..",
                        2, target_dp->i_ino);
		
		ASSERT (! error);

		/*
		 * Decrement link count on src_directory since the
		 * entry that's moved no longer points to it.
		 */
		xfs_droplink(tp, src_dp);
	}


	error = xfs_dir_removename (tp, src_dp, src_name, &first_block,
				    &free_list, MAX_EXT_NEEDED);
	ASSERT (! error);

	dnlc_remove (src_dir_vp, src_name);


	/*
	 * Update the generation counts on all the directory inodes
	 * that we're modifying.
	 */
	src_dp->i_gen++;
	xfs_trans_log_inode (tp, src_dp, XFS_ILOG_CORE);

	if (new_parent) {
                target_dp->i_gen++;
                xfs_trans_log_inode (tp, target_dp, XFS_ILOG_CORE);
	}

	/*
	 * If there was a target inode, take an extra reference on
	 * it here so that it doesn't go to xfs_inactive() from
	 * within the commit.
	 */
	if (target_ip != NULL) {
		IHOLD(target_ip);
	}

	(void) xfs_bmap_finish (&tp, &free_list, first_block);

	/*
	 * trans_commit will unlock src_ip, target_ip & decrement
	 * the vnode references.
	 */
	xfs_trans_commit (tp, XFS_TRANS_RELEASE_LOG_RES);
	if (target_ip != NULL) {
		IRELE(target_ip);
	}

	return 0;

error_return:

	xfs_trans_cancel (tp, cancel_flags);
	return error;

}


/*
 * xfs_mkdir
 *
 */
STATIC int
xfs_mkdir(vnode_t	*dir_vp,
	  char		*dir_name,
	  vattr_t	*vap,
	  vnode_t	**vpp,
	  cred_t	*credp)
{
        xfs_inode_t             *dp;
	xfs_inode_t		*cdp;	/* inode of created dir */
        xfs_trans_t             *tp = NULL;
        xfs_ino_t               e_inum;
	dev_t			rdev;
	mode_t			mode;
        xfs_mount_t		*mp;
        int                     code;
	int			cancel_flags;
        xfs_bmap_free_t         free_list;
        xfs_fsblock_t           first_block;
	boolean_t		dp_joined_to_trans = B_FALSE;


	vn_trace_entry(dir_vp, "xfs_mkdir");
	mp = XFS_VFSTOM(dir_vp->v_vfsp);
        tp = xfs_trans_alloc (mp, 0);
	cancel_flags = XFS_TRANS_RELEASE_LOG_RES;
        if (code = xfs_trans_reserve (tp, XFS_IALLOC_BLOCKS(mp) + 10,
				      XFS_MKDIR_LOG_RES(mp), 0,
				      XFS_TRANS_PERM_LOG_RES,
				      XFS_MKDIR_LOG_COUNT)) {
		cancel_flags = 0;
		dp = NULL;
		goto error_return;
	}

        dp = XFS_VTOI(dir_vp);

        xfs_ilock (dp, XFS_ILOCK_EXCL);


	/*
	 * Since dp was not locked between VOP_LOOKUP and VOP_MKDIR,
	 * the directory could have been removed.
	 */
        if (dp->i_d.di_nlink <= 0) {
		code = XFS_ERROR(ENOENT);
                goto error_return;
	}

        code = xfs_dir_lookup_int(NULL, dir_vp, 0, dir_name, NULL,
				  &e_inum, NULL);
        if (code != ENOENT) {
		if (code == 0)
			code = XFS_ERROR(EEXIST);
                goto error_return;
	}

	/*
	 * check access.
	 */
	if (code = xfs_iaccess (dp, IEXEC | IWRITE, credp))
                goto error_return;


	/*
	 * create the directory inode.
	 *
	 * XXX why does efs_mkdir pass 0 for rdev?
	 */
	rdev = (vap->va_mask & AT_RDEV) ? vap->va_rdev : NODEV;
        mode = IFDIR | (vap->va_mode & ~IFMT);
	code = xfs_dir_ialloc(&tp, dp, mode, 2, rdev, credp, &cdp, NULL);
	if (code)
		goto error_return;
	ITRACE(cdp);

	/*
	 * Now we add the directory inode to the transaction.
	 * We waited until now since xfs_dir_ialloc might start
	 * a new transaction.  Had we joined the transaction
	 * earlier, the locks might have gotten released.
	 */
	VN_HOLD (dir_vp);
        xfs_trans_ijoin (tp, dp, XFS_ILOCK_EXCL);
	dp_joined_to_trans = B_TRUE;

	XFS_BMAP_INIT(&free_list, &first_block);

	if (code = xfs_dir_createname (tp, dp, dir_name, cdp->i_ino,
				       &first_block, &free_list,
				       MAX_EXT_NEEDED)) 
 		ASSERT (0);

	dnlc_enter (dir_vp, dir_name, XFS_ITOV(cdp), NOCRED);
	
	if (code = xfs_dir_init(tp, cdp, dp))
		ASSERT (0);

	cdp->i_gen = 1;
	xfs_bumplink(tp, dp);

	dnlc_remove (XFS_ITOV(cdp), "..");


	*vpp = XFS_ITOV(cdp);

	IHOLD (cdp);

	(void) xfs_bmap_finish (&tp, &free_list, first_block);
	xfs_trans_commit (tp, XFS_TRANS_RELEASE_LOG_RES);

	return 0;

error_return:
	xfs_trans_cancel (tp, cancel_flags);

	if (! dp_joined_to_trans && (dp != NULL))
		xfs_iunlock (dp, XFS_ILOCK_EXCL);

	return code;
}


/*
 * xfs_rmdir
 *
 */
STATIC int
xfs_rmdir(vnode_t	*dir_vp,
	  char		*name,
	  vnode_t	*current_dir_vp,
	  cred_t	*credp)
{
        xfs_inode_t             *dp;
        xfs_inode_t             *cdp;   /* child directory */
        xfs_trans_t             *tp;
        xfs_ino_t               e_inum;
	xfs_mount_t		*mp;
        dev_t                   rdev;
        mode_t                  mode;
        int                     error;
        xfs_bmap_free_t         free_list;
        xfs_fsblock_t           first_block;
	int			cancel_flags;

	vn_trace_entry(dir_vp, "xfs_rmdir");
	mp = XFS_VFSTOM(dir_vp->v_vfsp);
	tp = xfs_trans_alloc (mp, 0);
	cancel_flags = XFS_TRANS_RELEASE_LOG_RES;
        if (error = xfs_trans_reserve (tp, 10, XFS_REMOVE_LOG_RES(mp),
				       0, XFS_TRANS_PERM_LOG_RES,
				       XFS_DEFAULT_LOG_COUNT)) {
		cancel_flags = 0;
                goto error_return;
	}
	XFS_BMAP_INIT(&free_list, &first_block);

        dp = XFS_VTOI(dir_vp);
	cdp = NULL;

	VN_HOLD (dir_vp);
        xfs_ilock (dp, XFS_ILOCK_EXCL);
	xfs_trans_ijoin (tp, dp, XFS_ILOCK_EXCL);

	if (error = xfs_iaccess (dp, IEXEC | IWRITE, credp))
                goto error_return;

        if (error = xfs_dir_lookup_int(NULL, dir_vp, DLF_IGET, name, NULL,
				       &e_inum, &cdp))
		goto error_return;

	ITRACE(cdp);
	xfs_ilock (cdp, XFS_ILOCK_EXCL);
	xfs_trans_ijoin (tp, cdp, XFS_ILOCK_EXCL);

	if (error = xfs_stickytest (dp, cdp, credp))
                goto error_return;


	if ((cdp == dp) || (cdp == XFS_VTOI(current_dir_vp))) {
		error = XFS_ERROR(EINVAL);
		goto error_return;
	}
	if ((cdp->i_d.di_mode & IFMT) != IFDIR) {
	        error = XFS_ERROR(ENOTDIR);
		goto error_return;
	}
	if (XFS_ITOV(cdp)->v_vfsmountedhere) {
		error = XFS_ERROR(EBUSY);
		goto error_return;
	}
	ASSERT (cdp->i_d.di_nlink >= 2);
	if (cdp->i_d.di_nlink != 2) {
		error = XFS_ERROR(EEXIST);
		goto error_return;
        }
	if (! xfs_dir_isempty(cdp)) {
		error = XFS_ERROR(EEXIST);
		goto error_return;
	}

        error = xfs_dir_removename (tp, dp, name, &first_block, &free_list, 0);
        ASSERT (! error);

	dnlc_remove (dir_vp, name);

	/*
	 * Drop the link from cdp's "..".
	 */
	xfs_droplink (tp, dp);

	/*
	 * Drop the link from dp to cdp.
	 */
	xfs_droplink (tp, cdp);

	/*
	 * Drop the "." link from cdp to self.
	 */
	xfs_droplink (tp, cdp);

	/*
	 * Take an extra ref on the child vnode so that it
	 * does not go to xfs_inactive() from within the commit.
	 */
	IHOLD(cdp);

	(void) xfs_bmap_finish (&tp, &free_list, first_block);

	xfs_trans_commit (tp, XFS_TRANS_RELEASE_LOG_RES);
	IRELE(cdp);

	return 0;

error_return:

	xfs_trans_cancel (tp, cancel_flags);
	return error;
}



/*
 * xfs_readdir
 *
 * Read dp's entries starting at uiop->uio_offset and translate them into
 * bufsize bytes worth of struct dirents starting at bufbase.
 */
STATIC int
xfs_readdir(vnode_t	*dir_vp,
	    uio_t	*uiop,
	    cred_t	*credp,
	    int		*eofp)
{
        xfs_inode_t             *dp;
        xfs_trans_t             *tp = NULL;
	int			status;
	uint			lock_mode;

	vn_trace_entry(dir_vp, "xfs_readdir");
        dp = XFS_VTOI(dir_vp);
        lock_mode = xfs_ilock_map_shared (dp);

        if ((dp->i_d.di_mode & IFMT) != IFDIR) {
		xfs_iunlock_map_shared(dp, lock_mode);
                return XFS_ERROR(ENOTDIR);
        }

	if (status = xfs_iaccess (dp, IEXEC, credp)) {
		xfs_iunlock_map_shared(dp, lock_mode);
                return status;
	}


	/* If the directory has been removed after it was opened. */
        if (dp->i_d.di_nlink <= 0) {
                xfs_iunlock_map_shared(dp, lock_mode);
                return 0;
        }

	status = xfs_dir_getdents (tp, dp, uiop, eofp);

	xfs_iunlock_map_shared(dp, lock_mode);
	
	return status;
}


/*
 * XXX The number of blocks that the directory code requires for
 * the createname operation.
 */
#define dir_needs 7

/*
 * xfs_symlink
 *
 */
STATIC int
xfs_symlink(vnode_t	*dir_vp,
	    char	*link_name,
	    vattr_t	*vap,
	    char	*target_path,
	    cred_t	*credp)
{
	xfs_trans_t	*tp = NULL;
	xfs_mount_t	*mp;
	xfs_inode_t	*dp, *ip;
        int 		error = 0, pathlen;
        struct pathname cpn, ccpn;
	xfs_ino_t	e_inum;
	dev_t		rdev;
	xfs_bmap_free_t free_list;
	xfs_fsblock_t   first_block;
	boolean_t	dp_joined_to_trans = B_FALSE;
	uint		cancel_flags;

	vn_trace_entry(dir_vp, "xfs_symlink");
	/*
	 * Check component lengths of the target path name.
         */
        pathlen = strlen(target_path);
        if (pathlen >= MAXPATHLEN)      /* total string too long */
                return XFS_ERROR(ENAMETOOLONG);
        if (pathlen >= MAXNAMELEN) {    /* is any component too long? */
                pn_alloc(&cpn);
                pn_alloc(&ccpn);
                bcopy(target_path, cpn.pn_path, pathlen);
                cpn.pn_pathlen = pathlen;
                while (cpn.pn_pathlen > 0 && !error) {
                        if (error = pn_getcomponent(&cpn, ccpn.pn_path, 0)) {
                                pn_free(&cpn);
                                pn_free(&ccpn);
                                if (error == ENAMETOOLONG)
                                        return error;
                        } else if (cpn.pn_pathlen) {    /* advance past slash */                                cpn.pn_path++;
                                cpn.pn_pathlen--;
                        }
                }
                pn_free(&cpn);
                pn_free(&ccpn);
        }

	mp = XFS_VFSTOM(dir_vp->v_vfsp);
        tp = xfs_trans_alloc (mp, 0);
	cancel_flags = XFS_TRANS_RELEASE_LOG_RES;
        if (error = xfs_trans_reserve (tp, XFS_IALLOC_BLOCKS(mp) + 12,
				       XFS_SYMLINK_LOG_RES(mp), 0,
				       XFS_TRANS_PERM_LOG_RES,
				       XFS_SYMLINK_LOG_COUNT)) {
		cancel_flags = 0;
		dp = NULL;
                goto error_return;
	}


        dp = XFS_VTOI(dir_vp);
	xfs_ilock (dp, XFS_ILOCK_EXCL);

	if (error = xfs_iaccess (dp, IEXEC | IWRITE, credp))
                goto error_return;

        error = xfs_dir_lookup_int(NULL, dir_vp, 0, link_name, NULL,
                                       &e_inum, NULL);
	if (error != ENOENT) {
		error = XFS_ERROR(EEXIST);
                goto error_return;
	}


	/*
	 * Initialize the bmap freelist prior to calling either
	 * bmapi or the directory create code.
	 */
	XFS_BMAP_INIT(&free_list, &first_block);

	/*
	 * Allocate an inode for the symlink.
	 */
	rdev = (vap->va_mask & AT_RDEV) ? vap->va_rdev : NODEV;

	error = xfs_dir_ialloc(&tp, dp, IFLNK | (vap->va_mode&~IFMT),
			       1, rdev, credp, &ip, NULL);
	if (error)
		goto error_return;
	ITRACE(ip);

	VN_HOLD (dir_vp);
        xfs_trans_ijoin (tp, dp, XFS_ILOCK_EXCL);
        dp_joined_to_trans = B_TRUE;

	/*
	 * If the symlink will fit into the inode, write it inline.
	 */
	if (pathlen <= XFS_LITINO(mp)) {
		xfs_idata_realloc (ip, pathlen);
		bcopy(target_path, ip->i_u1.iu_data, pathlen);
		ip->i_d.di_size = pathlen;

		/*
		 * The inode was initially created in extent format.
		 */
		ip->i_flags &= ~(XFS_IEXTENTS | XFS_IBROOT);
		ip->i_flags |= XFS_IINLINE;

		/*
		 * XXX This is temporary, should be removed later.
		 */
		ip->i_d.di_mode = (ip->i_d.di_mode & ~IFMT) | IFLNK;
		ip->i_d.di_format = XFS_DINODE_FMT_LOCAL;

		xfs_trans_log_inode (tp, ip, XFS_ILOG_DATA | XFS_ILOG_CORE);
	}
	else {
                xfs_fsblock_t   first_fsb;
                xfs_extlen_t    fs_blocks;
                int             nmaps;
                xfs_bmbt_irec_t mval [SYMLINK_MAPS];
                daddr_t         d;
                char            *cur_chunk;
                int             byte_cnt, n;
		struct 		buf *bp;

		first_fsb = 0;
		fs_blocks = XFS_B_TO_FSB(mp, pathlen);
		nmaps = SYMLINK_MAPS;

		first_block = xfs_bmapi (tp, ip, first_fsb, fs_blocks,
                         XFS_BMAPI_WRITE | XFS_BMAPI_METADATA, first_block,
			 fs_blocks+dir_needs, mval, &nmaps, &free_list);

		ip->i_d.di_size = pathlen;
		xfs_trans_log_inode (tp, ip, XFS_ILOG_CORE);

		cur_chunk = target_path;
		for (n = 0; n < nmaps; n++) {
			d = XFS_FSB_TO_DADDR(mp, mval[n].br_startblock);
			byte_cnt = XFS_FSB_TO_B(mp, mval[n].br_blockcount);
			bp = xfs_trans_get_buf (tp, mp->m_dev, d, 
				BTOBB(byte_cnt), 0);
			ASSERT(bp && !geterror(bp));
			if (pathlen < byte_cnt)
				byte_cnt = pathlen;
			pathlen -= byte_cnt;

			bcopy (cur_chunk, bp->b_un.b_addr, byte_cnt);
			cur_chunk += byte_cnt;

			xfs_trans_log_buf (tp, bp, 0, byte_cnt);
		}
	}


	/*
	 * Create the directory entry for the symlink.
	 */
	if (error = xfs_dir_createname (tp, dp, link_name, ip->i_ino,
					&first_block, &free_list,
					MAX_EXT_NEEDED))
                ASSERT (0);
        dnlc_enter (dir_vp, link_name, XFS_ITOV(ip), NOCRED);


	(void) xfs_bmap_finish (&tp, &free_list, first_block);
	xfs_trans_commit (tp, XFS_TRANS_RELEASE_LOG_RES);

	return 0;

error_return:

	xfs_trans_cancel (tp, cancel_flags);

	if (! dp_joined_to_trans && (dp != NULL))
		xfs_iunlock (dp, XFS_ILOCK_EXCL);

	return error;

}


/*
 * xfs_fid
 *
 */
STATIC int
xfs_fid(vnode_t	*vp,
	fid_t	**fidpp)
{
	xfs_fid_t	*fid;
	xfs_mount_t	*mp;
	xfs_inode_t	*ip;

	vn_trace_entry(vp, "xfs_fid");
	mp = XFS_VFSTOM(vp->v_vfsp);
	if (XFS_INO_BITS(mp) > (NBBY * sizeof(xfs_fid_ino_t))) {
		/*
		 * If the ino won't fit into the __uint32_t that's
		 * in our xfs_fid structure, then return an error.
		 */
		*fidpp = NULL;
		return EFBIG;
	}
	
	fid = (xfs_fid_t *) kmem_alloc (sizeof(xfs_fid_t), KM_SLEEP);
	fid->fid_len = sizeof(xfs_fid_t) - sizeof(fid->fid_len);
	fid->fid_pad = 0;
	ip = XFS_VTOI(vp);
	fid->fid_gen = ip->i_d.di_gen;
	fid->fid_ino = (xfs_fid_ino_t)ip->i_ino;

	*fidpp = (struct fid *)fid;

	return 0;
}


/*
 * xfs_rwlock
 *
 */
STATIC void
xfs_rwlock(vnode_t	*vp,
	   int		write_lock)
{
	xfs_inode_t	*ip;

	ip = XFS_VTOI(vp);
	if (write_lock)
		xfs_ilock (ip, XFS_IOLOCK_EXCL);
	else
		xfs_ilock (ip, XFS_IOLOCK_SHARED);
	return;
}


/*
 * xfs_rwunlock
 *
 */
STATIC void
xfs_rwunlock(vnode_t	*vp,
	     int	write_lock)
{
        xfs_inode_t     *ip;

        ip = XFS_VTOI(vp);
	if (write_lock)
        	xfs_iunlock (ip, XFS_IOLOCK_EXCL);
	else
        	xfs_iunlock (ip, XFS_IOLOCK_SHARED);
        return;
}


/*
 * xfs_seek
 *
 * Return an error if the new offset has overflowed and gone below
 * 0 or is greater than our maximum defined file offset.  Just checking
 * for overflow is not enough since off_t may be an __int64_t but the
 * file size may be limited to some number of bits between 32 and 64.
 */
STATIC int
xfs_seek(vnode_t	*vp,
	 off_t		old_offset,
	 off_t		*new_offsetp)
{
	if ((*new_offsetp >= XFS_MAX_FILE_OFFSET) ||
	    (*new_offsetp < 0)) {
		return XFS_ERROR(EINVAL);
	} else {
		return 0;
	}
}



/*
 * xfs_frlock
 *
 * This is a stub.
 */
STATIC int
xfs_frlock(vnode_t	*vp,
	   int		cmd,
	   flock_t	*flockp,
	   int		flag,
	   off_t	offset,
	   cred_t	*credp)
{
	xfs_inode_t	*ip;
	int		error;

	vn_trace_entry(vp, "xfs_frlock");
	ip = XFS_VTOI(vp);
	xfs_ilock(ip, XFS_IOLOCK_EXCL);
	error = fs_frlock(vp, cmd, flockp, flag, offset, credp);
	xfs_iunlock(ip, XFS_IOLOCK_EXCL);
	return error;
}


/*
 * xfs_map
 *
 * This is called when a file is first mapped by a process by either
 * a call to mmap() or an exec().  All the work is simply done in the
 * fs_map_subr() routine, which sets up the mapping in the process'
 * address space by making lots of VM specific calls.  These calls may
 * in turn call back to the xfs_addmap() routine, so we need to ensure
 * that we don't lock the inode across the call to fs_map_subr().
 *
 * XXXajs
 * This is a ridiculous layering of the code.  Why is all this VM
 * baggage embedded in the file system?
 */
STATIC int
xfs_map(vnode_t	*vp,
	off_t	offset,
	preg_t	*pregp,
	addr_t	*addrp,
	uint	len,
	uint	prot,
	uint	max_prot,
	uint	flags,
	cred_t	*credp)
{
	xfs_inode_t	*ip;
	int		error;

	ip = XFS_VTOI(vp);
	error = fs_map_subr(vp, ip->i_d.di_size, ip->i_d.di_mode,
			    offset, pregp, *addrp, len, prot,
			    max_prot, flags, credp);
	return error;
}


/*
 * xfs_addmap
 *
 * This is called when new mappings are added to the given file.  All
 * we do here is record the number of pages mapped in this file so that
 * we can reject record locking while a file is mapped (see xfs_frlock()).
 */
STATIC int
xfs_addmap(vnode_t	*vp,
	   off_t	offset,
	   preg_t	*pregp,
	   addr_t	addr,
	   uint		len,
	   uint		prot,
	   uint		max_prot,
	   uint		flags,
	   cred_t	*credp)
{
	xfs_inode_t	*ip;

	ip = XFS_VTOI(vp);
	xfs_ilock(ip, XFS_ILOCK_EXCL);
	ASSERT(vp->v_mreg);
	ip->i_mapcnt += btoc(len);
	xfs_iunlock(ip, XFS_ILOCK_EXCL);
	return 0;
}


/*
 * xfs_delmap
 *
 * This is called when mappings to the given file are deleted.  All
 * we do is decrement our count of the number of pages mapped in this
 * file.  This count is only used in xfs_frlock() in deciding whether
 * to accept a call.
 */
STATIC int
xfs_delmap(vnode_t	*vp,
	   off_t	offset,
	   preg_t	*pregp,
	   addr_t	addr,
	   uint		len,
	   uint		prot,
	   uint		max_prot,
	   uint		flags,
	   cred_t	*credp)
{
	xfs_inode_t	*ip;

	ip = XFS_VTOI(vp);
	xfs_ilock(ip, XFS_ILOCK_EXCL);
	ip->i_mapcnt -= btoc(len);
	ASSERT(ip->i_mapcnt >= 0);
	xfs_iunlock(ip, XFS_ILOCK_EXCL);
	return 0;
}


/*
 * xfs_allocstore
 *
 * This is called to reserve or allocate space for the given range.
 * Currently, this only supports reserving the space for a single
 * page.  By using NDPP (number of BBs per page) bmbt_irec structures,
 * we ensure that the entire page can be mapped in a single bmap call.
 * This simplifies the back out code in that all the information we need
 * to back out is in the single bmbt_irec array orig_imap.
 */
STATIC int
xfs_allocstore(vnode_t	*vp,
	       uint	offset,
	       uint	count,
	       cred_t	*credp)
{
	xfs_mount_t	*mp;
	xfs_inode_t	*ip;
	off_t		isize;
	xfs_fileoff_t	offset_fsb;
	xfs_fileoff_t	last_fsb;
        xfs_fileoff_t	curr_off_fsb;
	xfs_fileoff_t	unmap_offset_fsb;
	xfs_extlen_t	count_fsb;
	xfs_extlen_t	unmap_len_fsb;
	xfs_bmbt_irec_t	*imapp;
	xfs_bmbt_irec_t	*last_imapp;
	int		i;
	int		nimaps;
	int		orig_nimaps;
	xfs_bmbt_irec_t	imap[XFS_BMAP_MAX_NMAP];
	xfs_bmbt_irec_t	orig_imap[NDPP];
	
	vn_trace_entry(vp, "xfs_allocstore");
	/*
	 * This code currently only works for a single page.
	 */
	ASSERT(poff(offset) == 0);
	ASSERT(count == NBPP);
	ip = XFS_VTOI(vp);
	mp = ip->i_mount;
	offset_fsb = XFS_B_TO_FSBT(mp, offset);
	xfs_ilock(ip, XFS_ILOCK_EXCL);
	isize = ip->i_d.di_size;
	if (offset >= isize) {
		xfs_iunlock(ip, XFS_ILOCK_EXCL);
		return XFS_ERROR(EINVAL);
	}
	if ((offset + count) > isize) {
		count = isize - offset;
	}
	last_fsb = XFS_B_TO_FSB(mp, offset + count);
	count_fsb = (xfs_extlen_t)(last_fsb - offset_fsb);
	orig_nimaps = NDPP;
	(void) xfs_bmapi(NULL, ip, offset_fsb, count_fsb, 0, NULLFSBLOCK, 0,
			 orig_imap, &orig_nimaps, NULL);
	ASSERT(orig_nimaps > 0);

	curr_off_fsb = offset_fsb;
	while (count_fsb > 0) {
		nimaps = XFS_BMAP_MAX_NMAP;
		(void) xfs_bmapi(NULL, ip, curr_off_fsb,
				 (xfs_extlen_t)(last_fsb - curr_off_fsb),
				 XFS_BMAPI_DELAY | XFS_BMAPI_WRITE,
				 NULLFSBLOCK, 1, imap, &nimaps, NULL);
		if (nimaps == 0) {
			/*
			 * If we didn't get anything back, we must be
			 * out of space.  Break out of the loop and
			 * back out whatever we've done so far.
			 */
			break;
		}

		/*
		 * Count up the amount of space returned.
		 */
		for (i = 0; i < nimaps; i++) {
			ASSERT(imap[i].br_startblock != HOLESTARTBLOCK);
			count_fsb -= imap[i].br_blockcount;
			ASSERT(count_fsb >= 0);
			curr_off_fsb += imap[i].br_blockcount;
			ASSERT(curr_off_fsb <= last_fsb);
		}
	}

	if (count_fsb == 0) {
		/*
		 * We go it all, so get out of here.
		 */
		xfs_iunlock(ip, XFS_ILOCK_EXCL);
		return 0;
	}

	/*
	 * We didn't get it all, so back out anything new that we did
	 * create.  What we do is unmap all of the holes in the original
	 * map.  This will do at least one unnecessary unmap, but it's
	 * much simpler than being exact and it still works fine since
	 * we hold the inode lock all along.
	 */
	unmap_offset_fsb = offset_fsb;
	imapp = &orig_imap[0];
	last_imapp = &orig_imap[orig_nimaps - 1];
	while (imapp <= last_imapp) {
		if (unmap_offset_fsb != imapp->br_startoff) {
			unmap_len_fsb = imapp->br_startoff -
				        unmap_offset_fsb;
			(void) xfs_bunmapi(NULL, ip, unmap_offset_fsb,
					   unmap_len_fsb, 1, NULLFSBLOCK,
					   NULL, NULL);
		}
		unmap_offset_fsb = imapp->br_startoff + imapp->br_blockcount;
		if (imapp == last_imapp) {
			if (unmap_offset_fsb < (offset_fsb + count_fsb)) {
				/*
				 * There is a hole after the last original
				 * imap, so unmap it as well.
				 */
				unmap_len_fsb = (offset_fsb + count_fsb) -
					        unmap_offset_fsb;
				(void) xfs_bunmapi(NULL, ip,
						   unmap_offset_fsb,
						   unmap_len_fsb, 1,
						   NULLFSBLOCK, NULL, NULL);
			}
		}
		imapp++;
	}
	xfs_iunlock(ip, XFS_ILOCK_EXCL);
	return XFS_ERROR(ENOSPC);
}


/*
 * xfs_reclaim
 */
STATIC int
xfs_reclaim(vnode_t	*vp,
	    int		flag)
{
	xfs_inode_t		*ip;
	xfs_mount_t		*mp;

	vn_trace_entry(vp, "xfs_reclaim");
	ASSERT(!VN_MAPPED(vp));
	ip = XFS_VTOI(vp);
	mp = ip->i_mount;

	/*
	 * Flush and invalidate any data left around that is
	 * a part of this file.
	 */
	if (((ip->i_d.di_mode & IFMT) == IFREG) && (ip->i_d.di_size > 0)) {
		/*
		 * Get the inode's i/o lock so that buffers are pushed
		 * out while holding the proper lock.  We can't hold
		 * the inode lock here since flushing out buffers may
		 * cause us to try to get the lock in xfs_strategy().
		 */
	 	xfs_ilock(ip, XFS_IOLOCK_EXCL);
		pflushinvalvp(vp, 0,
			      (ip->i_d.di_size + (1 << mp->m_writeio_log)));
		xfs_iunlock(ip, XFS_IOLOCK_EXCL);
	}

	dnlc_purge_vp(vp);
	ASSERT(ip->i_iui == NULL);
	/*
	 * If the inode is still dirty, then flush it out synchronously.
	 * We get the flush lock regardless, though, just to make sure
	 * we don't free it while it is being flushed.
	 */
	xfs_ilock(ip, XFS_ILOCK_EXCL);
	xfs_iflock(ip);
	if (ip->i_update_core || (ip->i_item.ili_format.ilf_fields != 0)) {
		xfs_iflush(ip, 0);
	}
	xfs_iunlock(ip, XFS_ILOCK_EXCL);
	ASSERT(ip->i_update_core == 0);
	ASSERT(ip->i_item.ili_format.ilf_fields == 0);
	xfs_ireclaim(ip);
	return 0;
}



/*
 * xfs_fcntl
 */
STATIC int
xfs_fcntl(vnode_t	*vp,
	  int		cmd,
	  void		*arg,
	  int		flags,
	  off_t		offset,
	  cred_t	*credp,
	  rval_t	*rvalp)
{
	int		error = 0;

	vn_trace_entry(vp, "xfs_fcntl");
	switch (cmd) {
	case F_DIOINFO: {
		struct dioattr	da;

		/* only works on files opened for direct I/O */
		if (!(flags & FDIRECT)) {
			error = XFS_ERROR(EINVAL);
			break;
		}
		da.d_mem = BBSIZE;
		da.d_miniosz = BBSIZE;
		da.d_maxiosz = ctob(v.v_maxdmasz);
		if (copyout(&da, arg, sizeof(da)))
			error = XFS_ERROR(EFAULT);
		break;
	    }

	case F_FSGETXATTR: {
		struct fsxattr fa;
		vattr_t va;

		error = xfs_getattr(vp, &va, AT_XFLAGS|AT_EXTSIZE|AT_NEXTENTS|AT_UUID, credp);
		if (error)
			break;
		fa.fsx_xflags = va.va_xflags;
		fa.fsx_extsize = va.va_extsize;
		fa.fsx_nextents = va.va_nextents;
		fa.fsx_uuid = va.va_uuid;
		if (copyout(&fa, arg, sizeof(fa)))
			error = XFS_ERROR(EFAULT);
		break;
	    }

	case F_FSSETXATTR: {
		struct fsxattr fa;
		vattr_t va;

		if (copyin(arg, &fa, sizeof(fa))) {
			error = XFS_ERROR(EFAULT);
			break;
		}
		va.va_mask = AT_XFLAGS | AT_EXTSIZE;
		va.va_xflags = fa.fsx_xflags;
		va.va_extsize = fa.fsx_extsize;
		error = xfs_setattr(vp, &va, 0, credp);
		break;
	    }

	case F_GETBMAP: {
		struct getbmap bm;

		if (copyin(arg, &bm, sizeof(bm))) {
			error = XFS_ERROR(EFAULT);
			break;
		}
		if (bm.bmv_count < 2) {
			error = XFS_ERROR(EINVAL);
			break;
		}
		error = xfs_getbmap(vp, &bm, (struct getbmap *)arg + 1);
		if (!error && copyout(&bm, arg, sizeof(bm)))
			error = XFS_ERROR(EFAULT);
		break;
	    }

	default:
		error = XFS_ERROR(EINVAL);
		break;
	}
	return error;
}


struct vnodeops xfs_vnodeops = {
	fs_noerr,
	xfs_close,
	xfs_read,
	xfs_write,
	fs_nosys,
	fs_noerr,
	xfs_getattr,
	xfs_setattr,
	xfs_access,
	xfs_lookup,
	xfs_create,
	xfs_remove,
	xfs_link,
	xfs_rename,
	xfs_mkdir,
	xfs_rmdir,
	xfs_readdir,
	xfs_symlink,
	xfs_readlink,
	xfs_fsync,
	xfs_inactive,
	xfs_fid,
	xfs_rwlock,
	xfs_rwunlock,
	xfs_seek,
	fs_cmp,
	xfs_frlock,
	fs_nosys,	/* realvp */
	xfs_bmap,
	xfs_strategy,
	xfs_map,
	xfs_addmap,
	xfs_delmap,
	fs_poll,
	fs_nosys,	/* dump */
	fs_pathconf,
	xfs_allocstore,
	xfs_fcntl,
	xfs_reclaim,
};
