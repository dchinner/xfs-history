#ident "$Revision: 1.197 $"

#ifdef SIM
#define _KERNEL 1
#endif
#include <sys/types.h>
#include <sys/time.h>
#include <sys/timers.h>
#include <sys/buf.h>
#include <sys/uio.h>
#include <sys/user.h>
#include <sys/vfs.h>
#include <sys/vnode.h>
#include <specfs/snode.h>
#include <sys/systm.h>
#include <sys/dnlc.h>
#include <sys/sysmacros.h>
#include <sys/prctl.h>
#include <sys/cred.h>
#include <sys/grio.h>
#include <sys/pfdat.h>
#include <sys/sysinfo.h>
#include <sys/ksa.h>
#include <sys/dmi.h>
#include <sys/dmi_kern.h>
#include <sys/pda.h>
#ifdef SIM
#undef _KERNEL
#endif
#include <sys/cmn_err.h>
#include <sys/debug.h>
#include <sys/errno.h>
#include <sys/fs_subr.h>
#include <sys/fcntl.h>
#include <sys/ktrace.h>
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
#include <sys/stat.h>
#include <sys/file.h>
#include <sys/mode.h>
#include <sys/var.h>
#include <sys/capability.h>
#include <sys/flock.h>
#include <sys/kfcntl.h>
#include <string.h>
#include <sys/dirent.h>
#include <sys/attributes.h>
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
#include "xfs_itable.h"
#include "xfs_btree.h"
#include "xfs_ialloc.h"
#include "xfs_alloc.h"
#include "xfs_bmap.h"
#include "xfs_attr_sf.h"
#include "xfs_dir_sf.h"
#include "xfs_dinode.h"
#include "xfs_inode_item.h"
#include "xfs_inode.h"
#include "xfs_da_btree.h"
#include "xfs_attr.h"
#include "xfs_dir.h"
#include "xfs_rw.h"
#include "xfs_error.h"

#ifdef SIM
#include "sim.h"
#endif

#ifdef _K64U64
int irix5_to_flock(enum xlate_mode, void *, int, xlate_info_t *);
int flock_to_irix5(void *, int, xlate_info_t *);
int irix5_n32_to_flock(enum xlate_mode, void *, int, xlate_info_t *);
int flock_to_irix5_n32(void *, int, xlate_info_t *);
#endif


#ifndef SIM
STATIC int	xfs_truncate_file(xfs_mount_t	*mp,
				  xfs_inode_t	*ip);

STATIC int	xfs_droplink(xfs_trans_t *tp,
			     xfs_inode_t *ip);

STATIC void	xfs_bumplink(xfs_trans_t *tp,
			     xfs_inode_t *ip);

STATIC int	xfs_open(vnode_t	**vpp,
			 mode_t		flag,
			 cred_t		*credp);

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

STATIC int	xfs_fsync(vnode_t	*vp,
			  int		flag,
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
			   vrwlock_t	write_lock);

STATIC void	xfs_rwunlock(vnode_t	*vp,
			     vrwlock_t	write_lock);

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
			size_t	len,
			uint	prot,
			uint	max_prot,
			uint	flags,
			cred_t	*credp);

STATIC int	xfs_addmap(vnode_t	*vp,
			   off_t	offset,
			   preg_t	*pregp,
			   addr_t	addr,
			   size_t	len,
			   uint		prot,
			   uint		max_prot,
			   uint		flags,
			   cred_t	*credp);

STATIC int	xfs_delmap(vnode_t	*vp,
			   off_t	offset,
			   preg_t	*pregp,
			   addr_t	addr,
			   size_t	len,
			   uint		prot,
			   uint		max_prot,
			   uint		flags,
			   cred_t	*credp);

STATIC int	xfs_allocstore(vnode_t	*vp,
			       off_t	offset,
			       size_t	len,
			       cred_t	*credp);

STATIC int	xfs_fcntl(vnode_t	*vp,
			  int		cmd,
			  void		*arg,
			  int		flags,
			  off_t		offset,
			  cred_t	*credp,
			  rval_t	*rvalp);

STATIC int	xfs_set_dmattrs (vnode_t	*vp,
			 u_int		evmask,
			 u_int		state);

STATIC int 	xfs_change_file_space( vnode_t *,
			int,
			flock_t *,
			off_t,
			cred_t *);
#endif	/* !SIM */

STATIC void	xfs_itruncate_cleanup(xfs_trans_t	**tpp,
				      xfs_inode_t	*ip,
				      int		commit_flags,
				      int		fork);

STATIC void	xfs_inactive(vnode_t	*vp,
			     cred_t	*credp);

STATIC int	xfs_reclaim(vnode_t	*vp,
			    int		flag);

#ifdef DEBUG
STATIC void
xfs_ctrunc_trace(
	int		tag,
	xfs_inode_t	*ip);
#else
#define	xfs_ctrunc_trace(tag, ip)
#endif /* DEBUG */

#ifndef SIM

mutex_t	xfs_ancestormon;		/* initialized in xfs_init */
int	xfs_do_fast_fid = 1;

#define IRELE(ip)	VN_RELE(XFS_ITOV(ip))
#define IHOLD(ip)	VN_HOLD(XFS_ITOV(ip))
#define	ITRACE(ip)	vn_trace_ref(XFS_ITOV(ip), __FILE__, __LINE__)


/*
 * For xfs, we check that the file isn't too big to be opened by this kernel.
 * No other open action is required for regular files.  Devices are handled
 * through the specfs file system, pipes through fifofs.  Device and
 * fifo vnodes are "wrapped" by specfs and fifofs vnodes, respectively,
 * when a new vnode is first looked up or created.
 */
/*ARGSUSED*/
STATIC int
xfs_open(
	vnode_t		**vpp,
	mode_t		flag,
	cred_t		*credp)
{
	int		rval = 0;
	vnode_t		*vp = *vpp;
	xfs_inode_t	*ip = XFS_VTOI(vp);

	xfs_ilock(ip, XFS_ILOCK_SHARED);
#if XFS_BIG_FILES == 0
	if (ip->i_d.di_size > XFS_MAX_FILE_OFFSET)
		rval = XFS_ERROR(EFBIG);
#endif
	/*
	 * If it's a directory with any blocks, read-ahead block 0
	 * as we're almost certain to have the next operation be a read there.
	 */
	if (vp->v_type == VDIR && ip->i_d.di_nextents)
		(void)xfs_da_reada_buf(NULL, ip, 0, XFS_DATA_FORK);
	xfs_iunlock(ip, XFS_ILOCK_SHARED);
	return rval;
}

/*
 * xfs_close
 *
 */
/*ARGSUSED*/
STATIC int
xfs_close(
	vnode_t		*vp,
	int		flag,
	lastclose_t	lastclose,
	off_t		offset,
	cred_t		*credp)
{

	extern 	int	grio_remove_reservation( pid_t, dev_t, gr_ino_t);
	int		isshd, nofiles, vpcount, i;
	proc_t		*p = u.u_procp;
	shaddr_t	*sa = p->p_shaddr;
        xfs_inode_t	*ip;
	struct file	*fp;
	struct ufchunk	*ufp;

	vn_trace_entry(vp, "xfs_close");
	ip = XFS_VTOI(vp);

	/*
	 * if this is the last close of a file,
	 * remove any outstanding an i/o rate guarantees
	 */
	if ( lastclose && ( ip->i_d.di_flags & XFS_DIFLAG_REALTIME) ) {

		vpcount = 0;

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

		/*
		 * If this process is nolonger accessing
		 * this file, remove any guarantees that
		 * were made by this process on this file.
		 */
		if (!vpcount) {
			grio_remove_reservation(p->p_pid,ip->i_dev,ip->i_ino);
		}
	}


	xfs_ilock(ip, XFS_ILOCK_SHARED);
	cleanlocks(vp, u.u_procp->p_epid, u.u_procp->p_sysid);
	xfs_iunlock(ip, XFS_ILOCK_SHARED);

	return 0;
}


/*
 * xfs_getattr
 */
/*ARGSUSED*/
STATIC int
xfs_getattr(
	vnode_t	*vp,
	vattr_t	*vap,
	int	flags,
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

	/* trunc to 32 bits in 32-bit kernels */

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
        vap->va_vcode = 0L;
	/*
	 * Minor optimization, check the common cases first.
	 */
	if ((vp->v_type == VREG) || (vp->v_type == VDIR)) {
		vap->va_rdev = 0;
        } else if ((vp->v_type == VCHR) ||
		   (vp->v_type == VBLK) ||
		   (vp->v_type == VXNAM)) {
                vap->va_rdev = ip->i_df.if_u2.if_rdev;
        } else {
                vap->va_rdev = 0;       /* not a b/c spec. */
	}

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
	vap->va_nblocks =
		XFS_FSB_TO_BB(mp, ip->i_d.di_nblocks + ip->i_delayed_blks);

	/*
	 * XFS-added attributes
	 */
	if (flags & (AT_XFLAGS|AT_EXTSIZE|AT_NEXTENTS|AT_UUID|AT_ANEXTENTS)) {
		/*
		 * convert di_flags to xflags
		 */
		vap->va_xflags = 0;
		if (ip->i_d.di_flags & XFS_DIFLAG_REALTIME)
			vap->va_xflags |= XFS_XFLAG_REALTIME;
		if (ip->i_d.di_flags & XFS_DIFLAG_PREALLOC)
			vap->va_xflags |= XFS_XFLAG_PREALLOC;
		vap->va_extsize = ip->i_d.di_extsize << mp->m_sb.sb_blocklog;
		vap->va_nextents = (ip->i_df.if_flags & XFS_IFEXTENTS) ?
			ip->i_df.if_bytes / sizeof(xfs_bmbt_rec_t) :
			ip->i_d.di_nextents;
		vap->va_uuid = ip->i_d.di_uuid;
		if (ip->i_afp != NULL) {
			vap->va_anextents =
				(ip->i_afp->if_flags & XFS_IFEXTENTS) ?
				 ip->i_afp->if_bytes / sizeof(xfs_bmbt_rec_t) :
				 ip->i_d.di_anextents;
		} else {
			vap->va_anextents = 0;
		}
	}

	xfs_iunlock (ip, XFS_ILOCK_SHARED);

	return 0;
}


/*
 * xfs_setattr
 */
STATIC int
xfs_setattr(
	vnode_t	*vp,
	vattr_t	*vap,
	int	flags,
	cred_t	*credp)
{
        xfs_inode_t     *ip;
	xfs_trans_t	*tp = NULL;
	xfs_mount_t	*mp;
	int		mask;
	int		code;
	uint		lock_flags;
	uint		commit_flags;
	boolean_t	ip_held;
	uid_t		uid;
	gid_t		gid;
	int		timeflags = 0;

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
                ASSERT((mask & ~AT_UPDTIMES) == 0);
		timeflags = ((mask & AT_UPDATIME) ? XFS_ICHGTIME_ACC : 0) |
			    ((mask & AT_UPDCTIME) ? XFS_ICHGTIME_CHG : 0) |
			    ((mask & AT_UPDMTIME) ? XFS_ICHGTIME_MOD : 0);
		xfs_ichgtime(ip, timeflags);
		return 0;
        }


	/*
	 * For the other attributes, we acquire the inode lock and
	 * first do an error checking pass.
	 */
	mp = ip->i_mount;
	tp = NULL;
	lock_flags = XFS_ILOCK_EXCL;
	if (!(mask & AT_SIZE)) {
		tp = xfs_trans_alloc(mp, XFS_TRANS_SETATTR_NOT_SIZE);
		if (code = xfs_trans_reserve(tp, 0,
					     XFS_ICHANGE_LOG_RES(mp), 0,
					     0, 0)) {
			xfs_trans_cancel(tp, 0);
			return code;
		}
		commit_flags = 0;

	} else {
		if (DM_EVENT_ENABLED (vp->v_vfsp, ip, DM_TRUNCATE) &&
		    !(flags & ATTR_DMI)) {
			code = dm_data_event (vp, DM_TRUNCATE, vap->va_size, 0);
			if (code)
				return code;
		}
		lock_flags |= XFS_IOLOCK_EXCL;
	}

        xfs_ilock (ip, lock_flags);

        /*
         * Change file access modes.  Must be owner or privileged.
	 * Also check here for xflags, extsize.
         */
        if (mask & (AT_MODE|AT_XFLAGS|AT_EXTSIZE)) {
                if (credp->cr_uid != ip->i_d.di_uid &&
		    !_CAP_CRABLE(credp, CAP_FOWNER)) {
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

                if (!_CAP_CRABLE(credp, CAP_FOWNER)) {
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
                } else if (vp->v_type == VBLK || vp->v_type == VCHR) {
			code = 0;
			goto error_return;
		}

                /*
		 * There used to be a call to fs_vcode() here, but
		 * that will only be necessary if we ever support
		 * RFS.  Ha!
		 */
        }

        /*
         * Change file access or modified times.
         */
        if (mask & (AT_ATIME|AT_MTIME)) {
                if (credp->cr_uid != ip->i_d.di_uid &&
		    !_CAP_CRABLE(credp, CAP_FOWNER)) {
                        if (flags & ATTR_UTIME) {
                                code = XFS_ERROR(EPERM);
                                goto error_return;
                        }
			if (code = xfs_iaccess(ip, IWRITE, credp))
				goto error_return;
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
		    ((ip->i_d.di_extsize << mp->m_sb.sb_blocklog) != 
				vap->va_extsize) ) {
			code = XFS_ERROR(EINVAL);	/* EFBIG? */
			goto error_return;
		}

		/*
		 * Can't set extent size unless the file is marked, or
		 * about to be marked as a realtime file.	
		 *
		 * This check will be removed when fixed size extents
		 * with buffered data writes is implemented.
		 *
		 */
		if ( (mask & AT_EXTSIZE) 			&&
		     ( ( ip->i_d.di_extsize << mp->m_sb.sb_blocklog) != 
			vap->va_extsize)	&&
		     ( !( (ip->i_d.di_flags & XFS_DIFLAG_REALTIME)  ||
		          ((mask & AT_XFLAGS) &&
			   (vap->va_xflags & XFS_XFLAG_REALTIME)) ) ) ) {
			code = XFS_ERROR(EINVAL);
			goto error_return;
		}

		/*
		 * Can't change realtime flag if any extents are allocated.
		 */
		if (ip->i_d.di_nextents && (mask & AT_XFLAGS) &&
		    (ip->i_d.di_flags & XFS_DIFLAG_REALTIME) !=
		    (vap->va_xflags & XFS_XFLAG_REALTIME)) {
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
			    (vap->va_xflags & XFS_XFLAG_REALTIME)))
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
		    (vap->va_xflags & XFS_XFLAG_REALTIME)) {
			if ( (mp->m_sb.sb_rblocks == 0)  ||
			     (mp->m_sb.sb_rextsize == 0)  ||
			     (ip->i_d.di_extsize % mp->m_sb.sb_rextsize) ) {

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
			code = xfs_igrow_start(ip, vap->va_size, credp);
			xfs_iunlock(ip, XFS_ILOCK_EXCL);
		} else if (vap->va_size < ip->i_d.di_size) {
			xfs_iunlock(ip, XFS_ILOCK_EXCL);
			xfs_itruncate_start(ip, XFS_ITRUNC_DEFINITE,
					    (xfs_fsize_t)vap->va_size);
			code = 0;
		} else {
			xfs_iunlock(ip, XFS_ILOCK_EXCL);
			code = 0;
		}
		if (code) {
			ASSERT(tp == NULL);
			lock_flags &= ~XFS_ILOCK_EXCL;
			ASSERT(lock_flags == XFS_IOLOCK_EXCL);
			goto error_return;
		}
		tp = xfs_trans_alloc(mp, XFS_TRANS_SETATTR_SIZE);
		if (code = xfs_trans_reserve(tp, 0,
					     XFS_ITRUNCATE_LOG_RES(mp), 0,
					     XFS_TRANS_PERM_LOG_RES,
					     XFS_ITRUNCATE_LOG_COUNT)) {
			xfs_trans_cancel(tp, 0);
			xfs_iunlock(ip, XFS_IOLOCK_EXCL);
			return code;
		}
		commit_flags = XFS_TRANS_RELEASE_LOG_RES;
		xfs_ilock(ip, XFS_ILOCK_EXCL);
	}

        xfs_trans_ijoin(tp, ip, lock_flags);
	ip_held = B_FALSE;

	/*
         * Truncate file.  Must have write permission and not be a directory.
         */
        if (mask & AT_SIZE) {
		if (vap->va_size > ip->i_d.di_size) {
			xfs_igrow_finish(tp, ip, vap->va_size);
		} else if (vap->va_size < ip->i_d.di_size) {
			xfs_trans_ihold(tp, ip);
			code = xfs_itruncate_finish(&tp, ip,
					    (xfs_fsize_t)vap->va_size,
					    XFS_DATA_FORK, 0);
			ip_held = B_TRUE;
			if (code) {
				goto abort_return;
			}
		}
		/*
		 * Have to do this even if the file's size doesn't change.
		 */
		timeflags |= XFS_ICHGTIME_MOD | XFS_ICHGTIME_CHG;
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
                if (!_CAP_CRABLE(credp, CAP_DAC_OVERRIDE)) {
                        if (vp->v_type != VDIR && (ip->i_d.di_mode & ISVTX))
                                ip->i_d.di_mode &= ~ISVTX;
                        if (!groupmember(ip->i_d.di_gid, credp) && 
			    (ip->i_d.di_mode & ISGID))
                                ip->i_d.di_mode &= ~ISGID;
                }
		xfs_trans_log_inode (tp, ip, XFS_ILOG_CORE);
		timeflags |= XFS_ICHGTIME_CHG;
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

                if (!_CAP_CRABLE(credp, CAP_FOWNER)) {
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
		timeflags |= XFS_ICHGTIME_CHG;
        }


	/*
         * Change file access or modified times.
         */
        if (mask & (AT_ATIME|AT_MTIME)) {
                if (mask & AT_ATIME) {
                        ip->i_d.di_atime.t_sec = vap->va_atime.tv_sec;
                        ip->i_d.di_atime.t_nsec = vap->va_atime.tv_nsec;
			ip->i_update_core = 1;
			timeflags &= ~XFS_ICHGTIME_ACC;
		}
                if (mask & AT_MTIME) {
			ip->i_d.di_mtime.t_sec = vap->va_mtime.tv_sec;
			ip->i_d.di_mtime.t_nsec = vap->va_mtime.tv_nsec;
			timeflags &= ~XFS_ICHGTIME_MOD;
			timeflags |= XFS_ICHGTIME_CHG;
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
		if (mask & AT_XFLAGS) {
			ip->i_d.di_flags = 0;
			if ( vap->va_xflags & XFS_XFLAG_REALTIME) {
				ip->i_d.di_flags |= XFS_DIFLAG_REALTIME;
			}
			/* can't set PREALLOC this way, just ignore it */
		}
		xfs_trans_log_inode(tp, ip, XFS_ILOG_CORE);
		timeflags |= XFS_ICHGTIME_CHG;
	}

	/*
	 * Send out timestamp changes that need to be set to the
	 * current time.  Not done when called by a DMI function.
	 */
	if (timeflags && !(flags & ATTR_DMI))
		xfs_ichgtime(ip, timeflags);

	XFSSTATS.xs_ig_attrchg++;

	if (!ip_held)
		IHOLD (ip);

	/*
	 * If this is a synchronous mount, make sure that the
	 * transaction goes to disk before returning to the user.
	 */
	if (mp->m_flags & XFS_MOUNT_WSYNC) {
		xfs_trans_set_sync(tp);
	}

	code = xfs_trans_commit (tp, commit_flags);
	if (ip_held) {
		xfs_iunlock (ip, lock_flags);
	}

	if (code) {
		return XFS_ERROR(code);
	}

	if (DM_EVENT_ENABLED(vp->v_vfsp, ip, DM_ATTRIBUTE) &&
	    !(flags & ATTR_DMI)) {
		(void) dm_namesp_event (DM_ATTRIBUTE, vp, NULL, NULL, NULL,
			0, 0);
	}
	return 0;

 abort_return:
	commit_flags |= XFS_TRANS_ABORT;
	/* FALLTHROUGH */
 error_return:

	if (tp) {
		xfs_trans_cancel(tp, commit_flags);
	}
	if (lock_flags != 0) {
		xfs_iunlock (ip, lock_flags);
	}

	return XFS_ERROR(code);
}


/*
 * xfs_access
 * Null conversion from vnode mode bits to inode mode bits, as in efs.
 */
/*ARGSUSED*/
STATIC int
xfs_access(
	vnode_t	*vp,
	int	mode,
	int	flags,
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
/*ARGSUSED*/
STATIC int
xfs_readlink(
	vnode_t	*vp,
	uio_t	*uiop,
	cred_t	*credp)
{
        xfs_inode_t     *ip;
	int		count;
	off_t		offset;
	int		pathlen;
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

	if (!(uiop->uio_fmode & FINVIS)) {
		xfs_ichgtime(ip, XFS_ICHGTIME_ACC);
	}

	/*
	 * See if the symlink is stored inline.
	 */
	pathlen = (int) ip->i_d.di_size;

	if (ip->i_df.if_flags & XFS_IFINLINE) {
		error = uiomove (ip->i_df.if_u1.if_data, pathlen, UIO_READ, uiop);
	}
	else {
		/*
		 * Symlink not inline.  Call bmap to get it in.
		 */
		xfs_mount_t	*mp;
		xfs_fsblock_t	firstblock;
                int             nmaps;
                xfs_bmbt_irec_t mval [SYMLINK_MAPS];
                daddr_t         d;
                int             byte_cnt, n;
                struct          buf *bp;

		mp = XFS_VFSTOM(vp->v_vfsp);
                nmaps = SYMLINK_MAPS;
		firstblock = NULLFSBLOCK;

                error = xfs_bmapi (NULL, ip, 0, XFS_B_TO_FSB(mp, pathlen),
				   0, &firstblock, 0, mval, &nmaps, NULL);

		if (error) {
			goto error_return;
		}

                for (n = 0; n < nmaps; n++) {
                        d = XFS_FSB_TO_DADDR(mp, mval[n].br_startblock);
                        byte_cnt = XFS_FSB_TO_B(mp, mval[n].br_blockcount);
                        bp = read_buf (mp->m_dev, d, BTOBB(byte_cnt), 0);
			error = geterror(bp);
			if (error) {
				brelse(bp);
				goto error_return;
			}
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
/*ARGSUSED*/
STATIC int
xfs_fsync(
	vnode_t	*vp,
	int	flag,
	cred_t	*credp)
{
	xfs_inode_t	*ip;
	xfs_fsize_t	last_byte;
	int		error;
	buf_t		*bp;

	vn_trace_entry(vp, "xfs_fsync");
	ip = XFS_VTOI(vp);
	xfs_ilock(ip, XFS_IOLOCK_EXCL);
	last_byte = xfs_file_last_byte(ip);
	if (flag & FSYNC_INVAL) {
		if (ip->i_df.if_flags & XFS_IFEXTENTS && ip->i_df.if_bytes > 0) {
			if (VN_MAPPED(vp)) {
				remapf(vp, 0, 1);
			}
			pflushinvalvp(vp, 0, last_byte);
		}
		ASSERT((vp->v_pgcnt == 0) && (vp->v_buf == 0));
	} else {
		/*
		 * In the non-invalidating case calls to fsync() do not
		 * flush all the dirty mmap'd pages.  That requires a
		 * call to msync().
		 */
		pflushvp(vp, last_byte, (flag & FSYNC_WAIT) ? 0 : B_ASYNC);
	}

	if (!(flag & FSYNC_DATA)) {
		ASSERT(!(flag & (FSYNC_INVAL | FSYNC_WAIT)) ||
		       (!VN_DIRTY(vp) &&
			(ip->i_delayed_blks == 0) &&
			(ip->i_queued_bufs == 0)));
		xfs_ilock(ip, XFS_ILOCK_SHARED);
		if (!xfs_iflock_nowait(ip)) {
			/*
			 * If we can't grab the flush lock then check
			 * to see if the inode has been flushed delayed
			 * write.  If so, grab its buffer and send it
			 * out immediately.  We'll be able to acquire
			 * the flush lock when the I/O completes.
			 */
			bp = incore(ip->i_dev, ip->i_blkno, ip->i_len,
				    INCORE_TRYLOCK);
			if (bp != NULL) {
				if (bp->b_flags & B_DELWRI) {
#ifndef SIM
					buftrace("XFS_FSYNC", bp);
#endif
					bawrite(bp);
				} else {
					brelse(bp);
				}
			}
			xfs_iflock(ip);
		}
		error = xfs_iflush(ip, (flag & FSYNC_WAIT) ?
				   XFS_IFLUSH_SYNC : XFS_IFLUSH_ASYNC);
		xfs_iunlock(ip, XFS_IOLOCK_EXCL | XFS_ILOCK_SHARED);
	} else {
		xfs_log_force(ip->i_mount, (xfs_lsn_t)0,
			      XFS_LOG_FORCE | XFS_LOG_SYNC );
		error = 0;
	}
	return error;
}

#endif	/* !SIM */


/*
 * This is a utility routine for xfs_inactive.  It is called when a
 * transaction attempting to free up the disk space for a file encounters
 * an error.  It cancels the old transaction and starts up a new one
 * to be used to free up the inode.  It also sets the inode size and extent
 * counts to 0 and frees up any memory being used to store inline data,
 * extents, or btree roots.
 */
STATIC void
xfs_itruncate_cleanup(
	xfs_trans_t	**tpp,
	xfs_inode_t	*ip,
	int		commit_flags,
	int		fork)
{
	xfs_mount_t	*mp;
	int		error;

	mp = ip->i_mount;
	if (*tpp) {
		xfs_trans_cancel(*tpp, commit_flags | XFS_TRANS_ABORT);
	}
	xfs_iunlock(ip, XFS_IOLOCK_EXCL | XFS_ILOCK_EXCL);
	*tpp = xfs_trans_alloc(mp, XFS_TRANS_INACTIVE);
	error = xfs_trans_reserve(*tpp, 0, XFS_IFREE_LOG_RES(mp), 0, 0,
				  XFS_DEFAULT_LOG_COUNT);
	ASSERT(error == 0);
	
	xfs_ilock(ip, XFS_ILOCK_EXCL | XFS_IOLOCK_EXCL);
	xfs_trans_ijoin(*tpp, ip, XFS_IOLOCK_EXCL | XFS_ILOCK_EXCL);
	xfs_trans_ihold(*tpp, ip);

	xfs_idestroy_fork(ip, fork);

	if (fork == XFS_DATA_FORK) {
		ip->i_d.di_nblocks = 0;
		ip->i_d.di_nextents = 0;
		ip->i_d.di_size = 0;
	} else {
		ip->i_d.di_anextents = 0;
	}
	xfs_trans_log_inode(*tpp, ip, XFS_ILOG_CORE);
}

/*
 * xfs_inactive
 *
 * This is called when the vnode reference count for the vnode
 * goes to zero.  If the file has been unlinked, then it must
 * now be truncated.  Also, we clear all of the read-ahead state
 * kept for the inode here since the file is now closed.
 */
/*ARGSUSED*/
STATIC void
xfs_inactive(
	vnode_t	*vp,
	cred_t	*credp)
{
	xfs_inode_t	*ip;
	xfs_trans_t	*tp;
	xfs_trans_t	*ntp;
	xfs_mount_t	*mp;
	int		truncate;
	int		error;
	int		done;
	int		committed;
	int		commit_flags;
	xfs_fsblock_t	first_block;
	xfs_bmap_free_t	free_list;
	xfs_fileoff_t	end_fsb;
	xfs_fileoff_t	last_fsb;
	xfs_filblks_t	map_len;
	int		nimaps;
	xfs_bmbt_irec_t	imap;

	vn_trace_entry(vp, "xfs_inactive");
	ip = XFS_VTOI(vp);

	/*
	 * If the inode is already free, then there can be nothing
	 * to clean up here.
	 */
	if (ip->i_d.di_mode == 0) {
		ASSERT(ip->i_df.if_real_bytes == 0);
		ASSERT(ip->i_df.if_broot_bytes == 0);
		return;
	}

	/*
	 * Only do a truncate if it's a regular file with
	 * some actual space in it.  It's OK to look at the
	 * inode's fields without the lock because we're the
	 * only one with a reference to the inode.
	 */
	truncate = ((ip->i_d.di_nlink == 0) &&
	    ((ip->i_d.di_size != 0) || (ip->i_d.di_nextents > 0)) &&
	    ((ip->i_d.di_mode & IFMT) == IFREG));

	if (ip->i_d.di_nlink == 0 &&
	    DM_EVENT_ENABLED(vp->v_vfsp, ip, DM_DESTROY)) {
		(void) dm_namesp_event(DM_DESTROY, vp, NULL, NULL, NULL, 0, 0);
	}

#ifdef SIM
	ASSERT(ip->i_d.di_nlink >= 0);
#else
	ASSERT(ip->i_d.di_nlink >= 0);
	mp = ip->i_mount;
	if (ip->i_d.di_nlink == 0) {
		tp = xfs_trans_alloc(mp, XFS_TRANS_INACTIVE);

		if (truncate) {
			/*
			 * Do the xfs_itruncate_start() call before
			 * reserving any log space because itruncate_start
			 * will call into the buffer cache and we can't
			 * do that within a transaction.
			 */
			xfs_ilock(ip, XFS_IOLOCK_EXCL);
			xfs_itruncate_start(ip, XFS_ITRUNC_DEFINITE, 0);

			error = xfs_trans_reserve(tp, 0,
					XFS_ITRUNCATE_LOG_RES(mp),
					0, XFS_TRANS_PERM_LOG_RES,
					XFS_ITRUNCATE_LOG_COUNT);
			ASSERT(error == 0);

			xfs_ilock(ip, XFS_ILOCK_EXCL);
			xfs_trans_ijoin(tp, ip,
					XFS_IOLOCK_EXCL | XFS_ILOCK_EXCL);
			xfs_trans_ihold(tp, ip);

			error = xfs_itruncate_finish(&tp, ip, 0,
						     XFS_DATA_FORK, 0);
			commit_flags = XFS_TRANS_RELEASE_LOG_RES;

			if (error) {
				/*
				 * If we get an error while trying to
				 * truncate the file, abort the truncation
				 * transaction and just try to continue
				 * by freeing the inode.
				 */
				commit_flags |= XFS_TRANS_ABORT;
				xfs_itruncate_cleanup(&tp, ip, commit_flags,
						      XFS_DATA_FORK);
				commit_flags = 0;
			}
		} else if ((ip->i_d.di_mode & IFMT) == IFLNK) {

			/*
			 * If we get an error while cleaning up a
			 * symlink we jump to freeing the attribute fork
			 * of the inode.
			 */
			if (ip->i_d.di_size > XFS_IFORK_DSIZE(ip)) {
				/*
				 * We're freeing a symlink that has some
				 * blocks allocated to it.  Free the
				 * blocks here.  We know that we've got
				 * either 1 or 2 extents and that we can
				 * free them all in one bunmapi call.
				 */
				ASSERT((ip->i_d.di_nextents > 0) &&
				       (ip->i_d.di_nextents <= 2));
				error = xfs_trans_reserve(tp, 0,
						XFS_ITRUNCATE_LOG_RES(mp),
						0, XFS_TRANS_PERM_LOG_RES,
						XFS_ITRUNCATE_LOG_COUNT);
				ASSERT(error == 0);

				xfs_ilock(ip, XFS_IOLOCK_EXCL |
					  XFS_ILOCK_EXCL);
				xfs_trans_ijoin(tp, ip,
						XFS_ILOCK_EXCL |
						XFS_IOLOCK_EXCL);
				xfs_trans_ihold(tp, ip);

				done = 0;
				XFS_BMAP_INIT(&free_list, &first_block);
				error = xfs_bunmapi(tp, ip, 0, 2,
						  XFS_BMAPI_METADATA, 2,
						  &first_block, &free_list,
						  &done);
				if (error) {
					xfs_bmap_cancel(&free_list);
					xfs_itruncate_cleanup(&tp, ip,
						 (XFS_TRANS_RELEASE_LOG_RES |
						  XFS_TRANS_ABORT),
						 XFS_DATA_FORK);
					goto free_attrs;
				}
				ASSERT(done);
				error = xfs_bmap_finish(&tp, &free_list,
						 first_block, &committed);
				if (error) {
					xfs_bmap_cancel(&free_list);
					xfs_itruncate_cleanup(&tp, ip,
						(XFS_TRANS_RELEASE_LOG_RES |
						 XFS_TRANS_ABORT),
						XFS_DATA_FORK);
					goto free_attrs;
				}
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
				/*
				 * If we get an error on the commit here,
				 * we'll simply fall down and free the inode
				 * using the already duped transaction.
				 */
				(void) xfs_trans_commit(tp, 0);
				tp = ntp;
				if (ip->i_df.if_bytes) {
					xfs_idata_realloc(ip,
						-(ip->i_df.if_bytes),
						XFS_DATA_FORK);
				}
				ASSERT(ip->i_df.if_bytes == 0);
				error = xfs_trans_reserve(tp, 0,
					       XFS_ITRUNCATE_LOG_RES(mp),
					       0, XFS_TRANS_PERM_LOG_RES,
					       XFS_ITRUNCATE_LOG_COUNT);
				ASSERT(error == 0);

			} else {
				/*
				 * We're freeing a symlink which fit into
				 * the inode.  Just free the memory used
				 * to hold the old symlink.
				 */
				error = xfs_trans_reserve(tp, 0,
					   XFS_ITRUNCATE_LOG_RES(mp),
					   0, XFS_TRANS_PERM_LOG_RES,
					   XFS_ITRUNCATE_LOG_COUNT);
				ASSERT(error == 0);

				xfs_ilock(ip, XFS_ILOCK_EXCL |
					  XFS_IOLOCK_EXCL);
				ASSERT(ip->i_df.if_bytes > 0);
				xfs_idata_realloc(ip, -(ip->i_df.if_bytes),
					XFS_DATA_FORK);
				ASSERT(ip->i_df.if_bytes == 0);
			}

			xfs_trans_ijoin(tp, ip,
					XFS_IOLOCK_EXCL | XFS_ILOCK_EXCL);
			xfs_trans_ihold(tp, ip);
			commit_flags = XFS_TRANS_RELEASE_LOG_RES;

		} else {
			error = xfs_trans_reserve(tp, 0,
						  XFS_IFREE_LOG_RES(mp),
						  0, 0,
						  XFS_DEFAULT_LOG_COUNT);
			ASSERT(error == 0);
			
			xfs_ilock(ip, XFS_ILOCK_EXCL |
				  XFS_IOLOCK_EXCL);
			xfs_trans_ijoin(tp, ip,
					XFS_IOLOCK_EXCL | XFS_ILOCK_EXCL);
			xfs_trans_ihold(tp, ip);
			commit_flags = 0;
		}

		/*
		 * If there are attributes associated with the file
		 * then blow them away now.  The code calls a routine
		 * that recursively deconstructs the attribute fork.
		 * We need to just commit the current transaction
		 * because we can't use it for xfs_attr_inactive().
		 */
 free_attrs:
		if (ip->i_d.di_anextents > 0) {
			ASSERT(ip->i_d.di_forkoff != 0);
			error = xfs_trans_commit(tp, commit_flags);
			if (error) {
				xfs_iunlock(ip,
					    XFS_IOLOCK_EXCL | XFS_ILOCK_EXCL);
				goto out;
			}

			/*
			 * The return value is ignored cause we don't know
			 * what to do about it.
			 */
			(void)xfs_attr_inactive(ip);	

			xfs_iunlock(ip, XFS_ILOCK_EXCL);

			tp = xfs_trans_alloc(mp, XFS_TRANS_INACTIVE);
			error = xfs_trans_reserve(tp, 0,
						  XFS_IFREE_LOG_RES(mp),
						  0, 0,
						  XFS_DEFAULT_LOG_COUNT);
			commit_flags = 0;
			ASSERT(error == 0);

			xfs_ilock(ip, XFS_ILOCK_EXCL);
			xfs_trans_ijoin(tp, ip,
					XFS_IOLOCK_EXCL | XFS_ILOCK_EXCL);
			xfs_trans_ihold(tp, ip);

			xfs_idestroy_fork(ip, XFS_ATTR_FORK);

			ASSERT(error || (ip->i_d.di_anextents == 0));
		} else if (ip->i_afp)
			xfs_idestroy_fork(ip, XFS_ATTR_FORK);
				
		/*
		 * Free the inode.
		 */
		error = xfs_ifree(tp, ip);
		if (error) {
			/*
			 * If we fail to free the inode, just do our best
			 * to keep going.
			 */
			xfs_trans_cancel(tp, commit_flags | XFS_TRANS_ABORT);
		} else {
			/*
			 * Just ignore errors at this point.  There is
			 * nothing we can do except to try to keep going.
			 */
			(void) xfs_trans_commit(tp , commit_flags);
		}
		xfs_iunlock(ip, XFS_IOLOCK_EXCL | XFS_ILOCK_EXCL);
	} else if ((((ip->i_d.di_mode & IFMT) == IFREG) &&
		   (ip->i_d.di_size > 0) &&
		   (ip->i_df.if_flags & XFS_IFEXTENTS))  &&
		   (!(ip->i_d.di_flags & XFS_DIFLAG_PREALLOC)) ) { 
		/*
		 * Figure out if there are any blocks beyond the end
		 * of the file.  If not, then there is nothing to do.
		 */
		end_fsb = XFS_B_TO_FSB(mp, ((xfs_ufsize_t)ip->i_d.di_size));
		last_fsb = XFS_B_TO_FSB(mp, (xfs_ufsize_t)XFS_MAX_FILE_OFFSET);
		map_len = last_fsb - end_fsb;
		if (map_len > 0) {
			nimaps = 1;
			first_block = NULLFSBLOCK;
			xfs_ilock(ip, XFS_ILOCK_SHARED);
			error = xfs_bmapi(NULL, ip, end_fsb, map_len, 0,
					  &first_block, 0, &imap, &nimaps,
					  NULL);
			xfs_iunlock(ip, XFS_ILOCK_SHARED);
			if (!error && (nimaps != 0) &&
			    (imap.br_startblock != HOLESTARTBLOCK)) {
				/*
				 * There are blocks after the end of file.
				 * Free them up now by truncating the file to
				 * its current size.
				 */
				tp = xfs_trans_alloc(mp, XFS_TRANS_INACTIVE);
				
				/*
				 * Do the xfs_itruncate_start() call before
				 * reserving any log space because
				 * itruncate_start will call into the buffer
				 * cache and we can't
				 * do that within a transaction.
				 */
				xfs_ilock(ip, XFS_IOLOCK_EXCL);
				xfs_itruncate_start(ip, XFS_ITRUNC_DEFINITE,
						    ip->i_d.di_size);
				
				error = xfs_trans_reserve(tp, 0,
						   XFS_ITRUNCATE_LOG_RES(mp),
						   0, XFS_TRANS_PERM_LOG_RES,
						   XFS_ITRUNCATE_LOG_COUNT);
				ASSERT(error == 0);
				
				xfs_ilock(ip, XFS_ILOCK_EXCL);
				xfs_trans_ijoin(tp, ip,
						XFS_IOLOCK_EXCL |
						XFS_ILOCK_EXCL);
				xfs_trans_ihold(tp, ip);

				error = xfs_itruncate_finish(&tp, ip,
							     ip->i_d.di_size,
							     XFS_DATA_FORK,
							     0);
				/*
				 * If we get an error at this point we
				 * simply don't bother truncating the file.
				 */
				if (error) {
					xfs_trans_cancel(tp,
						 (XFS_TRANS_RELEASE_LOG_RES |
						  XFS_TRANS_ABORT));
				} else {
					(void) xfs_trans_commit(tp,
						 XFS_TRANS_RELEASE_LOG_RES);
				}
				xfs_iunlock(ip, XFS_IOLOCK_EXCL |
					    XFS_ILOCK_EXCL);
			}
		}
	}
#endif	/* !SIM */

 out:
	/*
	 * Clear all the inode's read-ahead state.  We don't need the
	 * lock for this because the inode is inactive.  Noone can
	 * be looking at this stuff.
	 */
	XFS_INODE_CLEAR_READ_AHEAD(ip);

	return;
}


#ifndef SIM

/*
 * Test the sticky attribute of a directory.  We can unlink from a sticky
 * directory that's writable by us if: we are superuser, we own the file,
 * we own the directory, or the file is writable.
 */
STATIC int
xfs_stickytest(
	xfs_inode_t	*dp,
	xfs_inode_t	*ip,
	cred_t		*cr)
{
	extern int	xpg4_sticky_dir;

        if ((dp->i_d.di_mode & ISVTX) &&
	    cr->cr_uid != 0 &&
	    cr->cr_uid != ip->i_d.di_uid &&
	    cr->cr_uid != dp->i_d.di_uid) {
		if (xpg4_sticky_dir) {
			return EACCES;
		} else {
			return xfs_iaccess(ip, IWRITE, cr);
		}
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

#define DLF_IGET	0x01	/* get entry inode if name lookup succeeds */

/*
 * Wrapper around xfs_dir_lookup. This routine will first look in
 * the dnlc.
 *
 * If DLF_IGET is set, then this routine will also return the inode.
 * Note that the inode will not be locked. Note, however, that the
 * vnode will have an additional reference in this case.
 */
STATIC int
xfs_dir_lookup_int(
	xfs_trans_t  		*tp,
	vnode_t      		*dir_vp,
	int		 	flag,
	char         		*name,
	pathname_t   		*pnp,
	xfs_ino_t    		*inum,
	xfs_inode_t  		**ipp,
	struct ncfastdata	*fd)
{
	vnode_t	   *vp;
	int	   name_len;
	int	   code = 0;
	int	   do_iget;

	vn_trace_entry(dir_vp, "xfs_dir_lookup_int");
	do_iget = flag & DLF_IGET;

	/*
	 * Handle degenerate pathname component.
	 */
	if (*name == '\0') {
		VN_HOLD(dir_vp);
		*inum = XFS_VTOI(dir_vp)->i_ino;
		if (do_iget)
			*ipp = XFS_VTOI(dir_vp);
		return (0);
        }

        /*
         * Try the directory name lookup cache.
         */
        if (vp = dnlc_lookup_fast(dir_vp, name, pnp, fd, NOCRED)) {
                *inum = XFS_VTOI(vp)->i_ino;
		if (do_iget) {
			*ipp = XFS_VTOI(vp);
			ASSERT((*ipp)->i_d.di_mode != 0);
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
	if (!code && do_iget) {
		code = xfs_iget(XFS_VFSTOM(dir_vp->v_vfsp), NULL, *inum,
				0, ipp, 0);
		if (code) {
			*ipp = NULL;
			return code;
		}
		/*
		 * This should never happen since the name exists in the
		 * directory, but if the inode number in the directory is
		 * bogus it is better to return EIO than to crash on
		 * a NULL pointer below.  Don't use ENOENT for this case,
		 * since that would indicate the some callers to go ahead
		 * and try to create an entry with the given name.
		 */
		if (*ipp == NULL) {
			return XFS_ERROR(EIO);
		}
		ASSERT(*ipp != NULL);
		vp = XFS_ITOV(*ipp);
		if ((*ipp)->i_d.di_mode == 0) {
			/*
			 * The inode has been freed.  This
			 * had better be "..".
			 */
			ASSERT((name[0] == '.') &&
			       (name[1] == '.') &&
			       (name[2] == 0));
			*ipp = NULL;
			VN_RELE (vp);
			code = XFS_ERROR(ENOENT);
		} else {
			dnlc_enter_fast(dir_vp, fd, vp, NOCRED);
		}
	}

	return (code);
}


/*

 * xfs_lookup
 *
 */
/*ARGSUSED*/
STATIC int
xfs_lookup(
	vnode_t		*dir_vp,
	char		*name,
	vnode_t		**vpp,
	pathname_t	*pnp,
	int		flags,
	vnode_t		*rdir, 
	cred_t		*credp)
{
	xfs_inode_t		*dp, *ip;
	struct vnode		*vp, *newvp;
	xfs_ino_t		e_inum;
	int			code = 0;
	uint			lock_mode;
	struct ncfastdata	fastdata;

	vn_trace_entry(dir_vp, "xfs_lookup");

	/*
	 * If it's not a directory, fail the request.
	 */
	if (dir_vp->v_type != VDIR)
		return XFS_ERROR(ENOTDIR);

	dp = XFS_VTOI(dir_vp);
	lock_mode = xfs_ilock_map_shared(dp);

	/*
	 * If the directory has been removed, then fail all lookups.
	 */
	if (dp->i_d.di_nlink == 0) {
		xfs_iunlock_map_shared(dp, lock_mode);
		return XFS_ERROR(ENOENT);
	}

	if (code = xfs_iaccess(dp, IEXEC, credp)) {
		xfs_iunlock_map_shared(dp, lock_mode);
		return code;
	}

	code = xfs_dir_lookup_int (NULL, dir_vp, DLF_IGET, name, pnp, 
				   &e_inum, &ip, &fastdata);
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
	buf_t		*ialloc_context = NULL;
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
	code = xfs_ialloc(tp, dp, mode, nlink, rdev, credp, 
			  &ialloc_context, &call_again, &ip);

	/*
	 * Return an error if we were unable to allocate a new inode.
	 * This should only happen if we run out of space on disk or
	 * encounter a disk error.
	 */
	if (code) {
		*ipp = NULL;
		return code;
	}
	if (!call_again && (ip == NULL)) {
		*ipp = NULL;
		return XFS_ERROR(ENOSPC);
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
		xfs_trans_bhold(tp, ialloc_context);
		/*
		 * Save the log reservation so we can use
		 * them in the next transaction.
		 */
		log_res = xfs_trans_get_log_res(tp);
		log_count = xfs_trans_get_log_count(tp);
		ntp = xfs_trans_dup(tp);
		code = xfs_trans_commit(tp, 0);
		tp = ntp;
		if (committed != NULL) {
			*committed = 1;
		}
		/*
		 * If we get an error during the commit processing,
		 * release the buffer that is still held and return
		 * to the caller.
		 */
		if (code) {
			brelse(ialloc_context);
			*tpp = ntp;
			*ipp = NULL;
			return code;
		}
		(void) xfs_trans_reserve(tp, 0, log_res, 0,
					 XFS_TRANS_PERM_LOG_RES, log_count);
		xfs_trans_bjoin (tp, ialloc_context);

		/*
		 * Call ialloc again. Since we've locked out all
		 * other allocations in this allocation group,
		 * this call should always succeed.
		 */
		code = xfs_ialloc(tp, dp, mode, nlink, rdev, credp,
				  &ialloc_context, &call_again, &ip);

		/*
		 * If we get an error at this point, return to the caller
		 * so that the current transaction can be aborted.
		 */
		if (code) {
			*tpp = tp;
			*ipp = NULL;
			return code;
		}
		ASSERT ((!call_again) && (ip != NULL));

	} else {
		if (committed != NULL) {
			*committed = 0;
		}
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
STATIC int				/* error */
xfs_droplink(
	xfs_trans_t *tp,
	xfs_inode_t *ip)
{
	int	error;

	xfs_ichgtime(ip, XFS_ICHGTIME_CHG);

        ASSERT (ip->i_d.di_nlink > 0);
        ip->i_d.di_nlink--;
        xfs_trans_log_inode(tp, ip, XFS_ILOG_CORE);

	error = 0;
        if (ip->i_d.di_nlink == 0) {
                /*
                 * We're dropping the last link to this file.
		 * Move the on-disk inode to the AGI unlinked list.
		 * From xfs_inactive() we will pull the inode from
		 * the list and free it.
                 */
		error = xfs_iunlink(tp, ip);
        }
	return error;
}

/*
 * Increment the link count on an inode & log the change.
 */
STATIC void
xfs_bumplink(
	xfs_trans_t *tp,
	xfs_inode_t *ip)
{
	xfs_ichgtime(ip, XFS_ICHGTIME_CHG);

        ip->i_d.di_nlink++;
        xfs_trans_log_inode(tp, ip, XFS_ILOG_CORE);
}


#ifdef DEBUG
STATIC void
xfs_ctrunc_trace(
	int		tag,
	xfs_inode_t	*ip)
{
	if (ip->i_rwtrace == NULL) {
		return;
	}

	ktrace_enter(ip->i_rwtrace,
		     (void*)((long)tag),
		     (void*)ip, 
		     (void*)((long)private.p_cpuid),
		     (void*)0,
		     (void*)0,
		     (void*)0,
		     (void*)0,
		     (void*)0,
		     (void*)0,
		     (void*)0,
		     (void*)0,
		     (void*)0,
		     (void*)0,
		     (void*)0,
		     (void*)0,
		     (void*)0);
}
#endif /* DEBUG */


/*
 * xfs_create
 *
 */
STATIC int
xfs_create(
	vnode_t		*dir_vp,
	char		*name,
	vattr_t		*vap,
	enum vcexcl	excl,
	int		I_mode,
	vnode_t		**vpp,
	cred_t		*credp)
{
	xfs_inode_t      	*dp, *ip;
        vnode_t		        *vp, *newvp;
	xfs_trans_t      	*tp;
        xfs_ino_t               e_inum;
        xfs_mount_t	        *mp;
	dev_t			rdev;
	unsigned long   	dir_generation;
        int                     error;
        xfs_bmap_free_t 	free_list;
        xfs_fsblock_t   	first_block;
	boolean_t		dp_joined_to_trans;
	boolean_t		truncated;
	uint			cancel_flags;
	int			committed;
	struct ncfastdata	fastdata;

	vn_trace_entry(dir_vp, "xfs_create");

        dp = XFS_VTOI(dir_vp);

	if (DM_EVENT_ENABLED(dir_vp->v_vfsp, dp, DM_CREATE)) {
		error = dm_namesp_event(DM_CREATE, dir_vp, NULL, name, NULL,
			vap->va_mode, 0);
		if (error)
			return error;
	}
 try_again:

	ip = NULL;
	dp_joined_to_trans = B_FALSE;
	truncated = B_FALSE;
	mp = XFS_VFSTOM(dir_vp->v_vfsp);
	tp = xfs_trans_alloc(mp, XFS_TRANS_CREATE);
	cancel_flags = XFS_TRANS_RELEASE_LOG_RES;
	/*
	 * Initially assume that the file does not exist and
	 * reserve the resources for that case.  If that is not
	 * the case we'll drop the one and get a more appropriate
	 * transaction later.
	 */
	if (error = xfs_trans_reserve(tp,
				      XFS_IALLOC_BLOCKS(mp) +
				      XFS_IN_MAXLEVELS(mp) +
				      XFS_BM_MAXLEVELS(mp, XFS_DATA_FORK) + 10,
				      XFS_CREATE_LOG_RES(mp), 0,
				      XFS_TRANS_PERM_LOG_RES,
				      XFS_CREATE_LOG_COUNT)) {
		cancel_flags = 0;
		dp = NULL;
		goto error_return;
	}

	xfs_ilock(dp, XFS_ILOCK_EXCL);

	/*
	 * If the directory has been removed, then fail all creates.
	 */
	if (dp->i_d.di_nlink == 0) {
		error = XFS_ERROR(ENOENT);
		goto error_return;
	}

	if (error = xfs_iaccess(dp, IEXEC, credp))
                goto error_return;

	error = xfs_dir_lookup_int(NULL, dir_vp, DLF_IGET, name, NULL, 
				   &e_inum, &ip, &fastdata);
	if (error && (error != ENOENT)) {
		goto error_return;
	}

	XFS_BMAP_INIT(&free_list, &first_block);

	if (error == ENOENT) {

		ASSERT(ip == NULL);

		if (error = xfs_iaccess(dp, IWRITE, credp)) {
			goto error_return;
		}

		/*
		 * XPG4 says create cannot allocate a file if the
		 * file size limit is set to 0.
		 */
		if (u.u_rlimit[RLIMIT_FSIZE].rlim_cur == 0) {
			error = XFS_ERROR(EFBIG);
			goto error_return;
		}

		rdev = (vap->va_mask & AT_RDEV) ? vap->va_rdev : NODEV;

		error = xfs_dir_ialloc(&tp, dp, 
				       MAKEIMODE(vap->va_type,vap->va_mode), 
				       1, rdev, credp, &ip, &committed);
		if (error) {
			goto abort_return;
		}
		ITRACE(ip);

		/*
		 * At this point, we've gotten a newly allocated inode.
		 * It is locked (and joined to the transaction).
		 */

		ASSERT(ismrlocked (&ip->i_lock, MR_UPDATE));

		/*
		 * Now we join the directory inode to the transaction.
		 * We do not do it earlier because xfs_dir_ialloc
		 * might commit the previous transaction (and release
		 * all the locks).
		 */

		VN_HOLD(dir_vp);
		xfs_trans_ijoin(tp, dp, XFS_ILOCK_EXCL);
		dp_joined_to_trans = B_TRUE;

		/*
		 * XXX Need to sanity check namelen.
		 */

		error = xfs_dir_createname(tp, dp, name, ip->i_ino,
					   &first_block, &free_list,
					   MAX_EXT_NEEDED);
		if (error) {
			ASSERT(error != ENOSPC);
			goto abort_return;
		}
		xfs_ichgtime(dp, XFS_ICHGTIME_MOD | XFS_ICHGTIME_CHG);

		/*
		 * If this is a synchronous mount, make sure that the
		 * create transaction goes to disk before returning to
		 * the user.
		 */
		if (mp->m_flags & XFS_MOUNT_WSYNC) {
			xfs_trans_set_sync(tp);
		}

		dp->i_gen++;
		dnlc_enter_fast(dir_vp, &fastdata, XFS_ITOV(ip), NOCRED);

	} else {
		ASSERT(ip != NULL);
		ITRACE(ip);

		/*
		 * The file already exists, so we're in the wrong
		 * transaction for this operation.  Cancel the old
		 * transaction and start a new one.  We have to drop
		 * our locks in doing this, so check the directory
		 * generation number to make sure nothing changes
		 * while we're unlocked.
		 */
		xfs_trans_cancel(tp, cancel_flags);
		tp = NULL;
		dir_generation = dp->i_gen;
		xfs_iunlock(dp, XFS_ILOCK_EXCL);

		/*
		 * Since we're at a good, clean point, check for any
		 * obvious problems and get out if they occur.
		 */
		vp = XFS_ITOV(ip);
		if (excl == EXCL) {
                        error = XFS_ERROR(EEXIST);
		} else if (vp->v_type == VDIR) {
                        error = XFS_ERROR(EISDIR);
		}

		if (error) {
			dp = NULL;
			IRELE(ip);
			goto error_return;
		}

		/*
		 * We need to do the xfs_itruncate_start call before
		 * reserving any log space in the transaction.
		 */
		xfs_ilock(ip, XFS_IOLOCK_EXCL);
		xfs_ctrunc_trace(XFS_CTRUNC1, ip);
		if ((vp->v_type == VREG) &&
		    (vap->va_mask & AT_SIZE) &&
		    ((ip->i_d.di_size != 0) || (ip->i_d.di_nextents != 0))) {
			xfs_itruncate_start(ip, XFS_ITRUNC_MAYBE, 0);
		}

		tp = xfs_trans_alloc(mp, XFS_TRANS_CREATE_TRUNC);
		if (error = xfs_trans_reserve(tp, 0,
					      XFS_ITRUNCATE_LOG_RES(mp), 0,
					      XFS_TRANS_PERM_LOG_RES,
					      XFS_ITRUNCATE_LOG_COUNT)) {
			xfs_iunlock(ip, XFS_IOLOCK_EXCL);
			IRELE(ip);
			cancel_flags = 0;
			dp = NULL;
			xfs_ctrunc_trace(XFS_CTRUNC2, ip);
			goto error_return;
		}
		
		/*
		 * Now lock inode and directory in the right order.
		 */
		if (dp->i_ino < ip->i_ino) {
			xfs_ilock(dp, XFS_ILOCK_EXCL);
			xfs_ilock(ip, XFS_ILOCK_EXCL);
		} else {
			xfs_ilock(ip, XFS_ILOCK_EXCL);
			xfs_ilock(dp, XFS_ILOCK_EXCL);
		}
			
		/*
		 * If things have changed while the dp was
		 * unlocked, drop all the locks and try
		 * again.
		 */
		if (dp->i_gen != dir_generation) {
			xfs_trans_cancel(tp, cancel_flags);
			xfs_ctrunc_trace(XFS_CTRUNC3, ip);
			xfs_iunlock(dp, XFS_ILOCK_EXCL);
			xfs_iunlock(ip, XFS_ILOCK_EXCL | XFS_IOLOCK_EXCL);
			IRELE(ip);
			goto try_again;
		}

		/*
		 * If the directory has been removed, then fail all creates.
		 * We check this again since we dropped the directory lock.
		 */
		if (dp->i_d.di_nlink == 0) {
			error = XFS_ERROR(ENOENT);
		} else if (I_mode) {
			error = xfs_iaccess(ip, I_mode, credp);
		}
			
		IHOLD(dp);
		xfs_trans_ijoin(tp, dp, XFS_ILOCK_EXCL);
		dp_joined_to_trans = B_TRUE;
		xfs_trans_ijoin(tp, ip, XFS_ILOCK_EXCL | XFS_IOLOCK_EXCL);

		if (error) {
			xfs_ctrunc_trace(XFS_CTRUNC4, ip);
			goto error_return;
		}

		if (vp->v_type == VREG && (vap->va_mask & AT_SIZE)) {
			/*
			 * Truncate the file.  The timestamps must
			 * be updated whether the file is changed
			 * or not.
			 */
			ASSERT(vap->va_size == 0);
			if ((ip->i_d.di_size > 0) || (ip->i_d.di_nextents)) {
				xfs_ctrunc_trace(XFS_CTRUNC5, ip);
				xfs_trans_ihold(tp, ip);
				error = xfs_itruncate_finish(&tp, ip,
							     (xfs_fsize_t)0,
							     XFS_DATA_FORK, 0);
				if (error) {
					ASSERT(ip->i_transp == tp);
					xfs_trans_ihold_release(tp, ip);
					goto abort_return;
				}
				truncated = B_TRUE;
			}
			xfs_ichgtime(ip, XFS_ICHGTIME_MOD | XFS_ICHGTIME_CHG);
			xfs_trans_log_inode(tp, ip, XFS_ILOG_CORE);
		}
		xfs_ctrunc_trace(XFS_CTRUNC6, ip);
	}


	/*
	 * xfs_trans_commit normally decrements the vnode ref count
	 * when it unlocks the inode. Since we want to return the 
	 * vnode to the caller, we bump the vnode ref count now.
	 */

	IHOLD(ip);
	vp = XFS_ITOV(ip);

	error = xfs_bmap_finish(&tp, &free_list, first_block, &committed);
	if (error) {
		xfs_bmap_cancel(&free_list);
		if (truncated) {
			/*
			 * If we truncated the file, then the inode will
			 * have been held within the previous transaction
			 * and must be unlocked now.
			 */
			xfs_iunlock(ip, XFS_ILOCK_EXCL | XFS_IOLOCK_EXCL);
			ASSERT(vp->v_count >= 2);
			IRELE(ip);
		}		
		goto abort_rele;
	}

	error = xfs_trans_commit(tp, XFS_TRANS_RELEASE_LOG_RES);
	if (truncated) {
		/*
		 * If we truncated the file, then the inode will
		 * have been held within the transaction and must
		 * be unlocked now.
		 */
		xfs_iunlock(ip, XFS_ILOCK_EXCL | XFS_IOLOCK_EXCL);
		ASSERT(vp->v_count >= 2);
		IRELE(ip);
	}
	if (error) {
		IRELE(ip);
		tp = NULL;
		goto error_return;
	}
	ASSERT(dp_joined_to_trans);

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

        *vpp = vp;

	if (DM_EVENT_ENABLED(dir_vp->v_vfsp, dp, DM_POSTCREATE)) {
		(void) dm_namesp_event(DM_POSTCREATE, dir_vp, vp, name, NULL,
			ip->i_d.di_mode, 0);
	}
	return 0;

 abort_return:
	cancel_flags |= XFS_TRANS_ABORT;
	/* FALLTHROUGH */
 error_return:

	if (tp != NULL)
		xfs_trans_cancel(tp, cancel_flags);

	if (!dp_joined_to_trans && (dp != NULL))
		xfs_iunlock(dp, XFS_ILOCK_EXCL);
	return error;

 abort_rele:
	/*
	 * Wait until after the current transaction is aborted to
	 * release the inode.  This prevents recursive transactions
	 * and deadlocks from xfs_inactive.
	 */
	cancel_flags |= XFS_TRANS_ABORT;
	xfs_trans_cancel(tp, cancel_flags);
	IRELE(ip);
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
xfs_lock_dir_and_entry(
	xfs_inode_t	*dp,
	char		*name,
	xfs_inode_t	**ipp)	/* inode of entry 'name' */
{
	xfs_inode_t		*ip, *new_ip;
	xfs_ino_t		e_inum;
	unsigned long		dir_generation;
	int			error;
	struct ncfastdata	fastdata;

 again:

        xfs_ilock(dp, XFS_ILOCK_EXCL);

	/*
	 * If the link count on the directory is 0, there are no
	 * entries to lock.
	 */
	if (dp->i_d.di_nlink == 0) {
		xfs_iunlock(dp, XFS_ILOCK_EXCL);
		*ipp = NULL;
		return XFS_ERROR(ENOENT);
	}

	error = xfs_dir_lookup_int(NULL, XFS_ITOV(dp), DLF_IGET, name, 
				   NULL, &e_inum, &ip, &fastdata);
        if (error) {
                xfs_iunlock(dp, XFS_ILOCK_EXCL);
		*ipp = NULL;
                return error;
        }

        ASSERT((e_inum != 0) && ip);
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
		xfs_ilock(ip, XFS_ILOCK_EXCL);

	} else if (e_inum < dp->i_ino) {
                dir_generation = dp->i_gen;
                xfs_iunlock(dp, XFS_ILOCK_EXCL);

		xfs_ilock(ip, XFS_ILOCK_EXCL);
                xfs_ilock(dp, XFS_ILOCK_EXCL);

                /*
                 * Make sure that things are still consistent during
                 * the period we dropped the directory lock.
                 * Do a new lookup if directory was changed.
                 */
                if (dp->i_gen != dir_generation) {
			error = xfs_dir_lookup_int(NULL, XFS_ITOV(dp),
				DLF_IGET, name, NULL, &e_inum, &new_ip,
				&fastdata);

                        if (error) {
				xfs_iunlock(dp, XFS_ILOCK_EXCL);
				xfs_iunlock(ip, XFS_ILOCK_EXCL);
				IRELE(ip);
				*ipp = NULL;
				return error;
			}

			ITRACE(new_ip);

                        if (new_ip != ip) {

				/*
				 * Things have changed too much,
				 * just goto the top & start again.
				 */
				xfs_iunlock(dp, XFS_ILOCK_EXCL);
                                xfs_iunlock(ip, XFS_ILOCK_EXCL);
				IRELE(ip);
				IRELE(new_ip);

				goto again;
                        }
			IRELE(new_ip); /* ref from xfs_dir_lookup_int */
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
	xfs_inode_t    **ipp2)	/* inode of new entry, if it 
		           	   already exists, NULL otherwise. */
{
	xfs_inode_t		*ip1, *ip2, *temp;
	xfs_ino_t		inum1, inum2;
	unsigned long		dir_gen1, dir_gen2;
	int			error;
	xfs_inode_t		*i_tab[4];
	int			num_inodes;
	int			i, j;
	uint			lock_mode;
	struct ncfastdata	fastdata;

	ip2 = NULL;

	/*
	 * First, find out the current inums of the entries so that we
	 * can determine the initial locking order.  We'll have to 
	 * sanity check stuff after all the locks have been acquired
	 * to see if we still have the right inodes, directories, etc.
	 */
        lock_mode = xfs_ilock_map_shared(dp1);

	/*
	 * We don't want to do lookups in unlinked directories.
	 */
	if (dp1->i_d.di_nlink == 0) {
		xfs_iunlock_map_shared(dp1, lock_mode);
		return XFS_ERROR(ENOENT);
	}

        error = xfs_dir_lookup_int(NULL, XFS_ITOV(dp1), DLF_IGET,
				   name1, NULL, &inum1, &ip1, &fastdata);

	/*
	 * Save the current generation so that we can detect if it's
	 * modified between when we drop the lock & reacquire it down
	 * below.  We only need to do this for the src directory since
	 * the target entry does not need to exist yet. 
	 */
	dir_gen1 = dp1->i_gen;
	xfs_iunlock_map_shared(dp1, lock_mode);
        if (error)
                return error;
	ASSERT (ip1);
	ITRACE(ip1);

        lock_mode = xfs_ilock_map_shared(dp2);

	/*
	 * We don't want to do lookups in unlinked directories.
	 */
	if (dp2->i_d.di_nlink == 0) {
		xfs_iunlock_map_shared(dp2, lock_mode);
		return XFS_ERROR(ENOENT);
	}

        error = xfs_dir_lookup_int(NULL, XFS_ITOV(dp2), DLF_IGET,
				   name2, NULL, &inum2, &ip2, &fastdata);
	dir_gen2 = dp2->i_gen;
        xfs_iunlock_map_shared(dp2, lock_mode);
	if (error == ENOENT) {		/* target does not need to exist. */
		inum2 = 0;
	} else if (error) {
		IRELE (ip1);
                return error;
	} else {
		ITRACE(ip2);
	}

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
	} else {
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
			xfs_ilock(i_tab[i], XFS_ILOCK_EXCL);
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
		xfs_iunlock(i_tab[0], XFS_ILOCK_EXCL);
		for (i=1; i < num_inodes; i++) {
			if (i_tab[i] != i_tab[i-1])
				xfs_iunlock(i_tab[i], XFS_ILOCK_EXCL);
		}
		if (num_inodes == 4)
			IRELE (ip2);
		IRELE (ip1);
		return XFS_ERROR(EAGAIN);
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
		xfs_ilock(ip1, XFS_ILOCK_EXCL);
		xfs_ilock(ip2, XFS_ILOCK_EXCL);
	} else {
		xfs_ilock(ip2, XFS_ILOCK_EXCL);
		xfs_ilock(ip1, XFS_ILOCK_EXCL);
	}
}

/*
 * Try to truncate the given file to 0 length.
 */
STATIC int
xfs_truncate_file(
	xfs_mount_t	*mp,
	xfs_inode_t	*ip)
{
	xfs_trans_t	*tp;
	int		error;

	/*
	 * Make the call to xfs_itruncate_start before starting the
	 * transaction, because we cannot make the call while we're
	 * in a transaction.
	 */
	xfs_ilock(ip, XFS_IOLOCK_EXCL);
	xfs_itruncate_start(ip, XFS_ITRUNC_DEFINITE, (xfs_fsize_t)0);

	tp = xfs_trans_alloc(mp, XFS_TRANS_TRUNCATE_FILE);
	if (error = xfs_trans_reserve(tp, 0, XFS_ITRUNCATE_LOG_RES(mp), 0,
				      XFS_TRANS_PERM_LOG_RES,
				      XFS_ITRUNCATE_LOG_COUNT)) {
		xfs_trans_cancel(tp, 0);
		return error;
	}

	/*
	 * Follow the normal truncate locking protocol.  Since we
	 * hold the inode in the transaction, we know that it's number
	 * of references will stay constant.
	 */
	xfs_ilock(ip, XFS_ILOCK_EXCL);
	xfs_trans_ijoin(tp, ip, XFS_ILOCK_EXCL | XFS_IOLOCK_EXCL);
	xfs_trans_ihold(tp, ip);
	error = xfs_itruncate_finish(&tp, ip, (xfs_fsize_t)0,
				     XFS_DATA_FORK, 0);
	if (error) {
		xfs_trans_cancel(tp, XFS_TRANS_RELEASE_LOG_RES |
				 XFS_TRANS_ABORT);
	} else {
		xfs_ichgtime(ip, XFS_ICHGTIME_MOD | XFS_ICHGTIME_CHG);
		error = xfs_trans_commit(tp, XFS_TRANS_RELEASE_LOG_RES);
	}
	xfs_iunlock(ip, XFS_ILOCK_EXCL | XFS_IOLOCK_EXCL);

	return error;
}

int remove_which_error_return = 0;

/*
 * xfs_remove
 *
 */
STATIC int
xfs_remove(
	vnode_t	*dir_vp,
	char	*name,
	cred_t	*credp)
{
        xfs_inode_t             *dp, *ip;
        xfs_trans_t             *tp = NULL;
	xfs_mount_t		*mp;
        int                     error = 0;
        xfs_bmap_free_t         free_list;
        xfs_fsblock_t           first_block;
	int			cancel_flags;
	int			nospace;
	int			committed;

	nospace = 0;
	vn_trace_entry(dir_vp, "xfs_remove");
	mp = XFS_VFSTOM(dir_vp->v_vfsp);

	if (DM_EVENT_ENABLED(dir_vp->v_vfsp, XFS_VTOI (dir_vp), DM_REMOVE)) {
		error = dm_namesp_event(DM_REMOVE, dir_vp, NULL,
					name, NULL, 0, 0);
		if (error)
			return error;
	}
 retry:
	tp = xfs_trans_alloc(mp, XFS_TRANS_REMOVE);
	cancel_flags = XFS_TRANS_RELEASE_LOG_RES;
        if (error = xfs_trans_reserve(tp, 10, XFS_REMOVE_LOG_RES(mp),
				      0, XFS_TRANS_PERM_LOG_RES,
				      XFS_REMOVE_LOG_COUNT)) {
		cancel_flags = 0;
		/*
		 * If we can't reserve the necessary amount of space,
		 * then try to truncate the file below (after doing
		 * the regular error checking) and start over again.
		 * The nospace variable indicates whether this has
		 * happened or not.  We only want to try this once,
		 * so if we've already tried it (nospace > 0) then
		 * just return an error.
		 */
		if ((error != ENOSPC) || (nospace > 0)) {
			remove_which_error_return = 1;
			goto error_return;
		} else {
			nospace = 1;
		}
	}

	dp = XFS_VTOI(dir_vp);
        ip = NULL;

	error = xfs_lock_dir_and_entry(dp, name, &ip);
	if (error) {
		remove_which_error_return = 2;
		goto error_return;
	}

	/*
	 * At this point, we've gotten both the directory and the entry
	 * inodes locked.
	 */
	xfs_trans_ijoin(tp, dp, XFS_ILOCK_EXCL);
	if (dp != ip) {
		/*
		 * Increment vnode ref count only in this case since
		 * there's an extra vnode reference in the case where
		 * dp == ip.
		 */
		IHOLD(dp);
		xfs_trans_ijoin(tp, ip, XFS_ILOCK_EXCL);
	}
 
	if (error = xfs_iaccess(dp, IEXEC | IWRITE, credp)) {
		remove_which_error_return = 3;
		goto error_return;
	}

	if (error = xfs_stickytest(dp, ip, credp)) {
		remove_which_error_return = 4;
		goto error_return;
	}

	if (XFS_ITOV(ip)->v_vfsmountedhere) {
		error = XFS_ERROR(EBUSY);
		remove_which_error_return = 5;
		goto error_return;
	}
	if ((ip->i_d.di_mode & IFMT) == IFDIR) {
		error = XFS_ERROR(EPERM);
		remove_which_error_return = 6;
		goto error_return;
	}

	/*
	 * Return error when removing . and ..
	 */
	if (name[0] == '.') {
		if (name[1] == '\0') {
			error = XFS_ERROR(EINVAL);
			remove_which_error_return = 7;
			goto error_return;
		}
		else if (name[1] == '.' && name[2] == '\0') {
			error = XFS_ERROR(EEXIST);
			remove_which_error_return = 8;
			goto error_return;
		}
	} 

	if (nospace == 1) {
		/*
		 * We can't reserve the space to perform the actual
		 * remove, so try to truncate the file and start over
		 * if we can.  We can only do this to a regular file
		 * that has a link count of 1.  Otherwise we'd break
		 * the regular behavior of the unlink() system call
		 * that gets us here.
		 */
		if (((ip->i_d.di_mode & IFMT) == IFREG) &&
		    ((ip->i_d.di_size > 0) || (ip->i_d.di_nextents != 0)) &&
		    (ip->i_d.di_nlink == 1)) {
			IHOLD(ip);
			xfs_trans_cancel(tp, cancel_flags);
			error = xfs_truncate_file(mp, ip);
			IRELE(ip);
			if (error) {
				remove_which_error_return = 9;
				return error;
			}
			nospace = 2;
			goto retry;
		}
		error = XFS_ERROR(ENOSPC);
		remove_which_error_return = 10;
		goto error_return;
	}

	/*
	 * Entry must exist since we did a lookup in xfs_lock_dir_and_entry.
	 */
	XFS_BMAP_INIT(&free_list, &first_block);
	error = xfs_dir_removename(tp, dp, name, &first_block, &free_list, 0);
	if (error) {
		ASSERT(error != ENOENT);
		remove_which_error_return = 11;
		goto error1;
	}
	xfs_ichgtime(dp, XFS_ICHGTIME_MOD | XFS_ICHGTIME_CHG);

	dnlc_remove(dir_vp, name);

	dp->i_gen++;
	xfs_trans_log_inode(tp, dp, XFS_ILOG_CORE);

	error = xfs_droplink(tp, ip);
	if (error) {
		remove_which_error_return = 12;
		goto error1;
	}

	/*
	 * Take an extra ref on the inode so that it doesn't
	 * go to xfs_inactive() from within the commit.
	 */
	IHOLD(ip);

	/*
	 * If this is a synchronous mount, make sure that the
	 * remove transaction goes to disk before returning to
	 * the user.
	 */
	if (mp->m_flags & XFS_MOUNT_WSYNC) {
		xfs_trans_set_sync(tp);
	}

	error = xfs_bmap_finish(&tp, &free_list, first_block, &committed);
	if (error) {
		remove_which_error_return = 13;
		goto error_rele;
	}

	error = xfs_trans_commit(tp, XFS_TRANS_RELEASE_LOG_RES);
	if (error) {
		IRELE(ip);
		return error;
	}

	if (DM_EVENT_ENABLED(dir_vp->v_vfsp, dp, DM_POSTREMOVE)) {
		(void) dm_namesp_event(DM_POSTREMOVE, dir_vp, NULL,
				       name, NULL, ip->i_d.di_mode, 0);
	}

	/*
	 * Before we drop our extra reference to the inode, purge it
	 * from the refcache if it is there.  By waiting until afterwards
	 * to do the IRELE, we ensure that we won't go inactive in the
	 * xfs_refcache_purge_ip routine (although that would be OK).
	 */
#ifndef SIM
	xfs_refcache_purge_ip(ip);
#endif
	IRELE(ip);

	return 0;

 error1:
	xfs_bmap_cancel(&free_list);
	cancel_flags |= XFS_TRANS_ABORT;

 error_return:
	xfs_trans_cancel(tp, cancel_flags);
	return error;

 error_rele:
	/*
	 * In this case make sure to not release the inode until after
	 * the current transaction is aborted.  Releasing it beforehand
	 * can cause us to go to xfs_inactive and start a recursive
	 * transaction which can easily deadlock with the current one.
	 */
	xfs_bmap_cancel(&free_list);
	cancel_flags |= XFS_TRANS_ABORT;
	xfs_trans_cancel(tp, cancel_flags);
	/*
	 * Before we drop our extra reference to the inode, purge it
	 * from the refcache if it is there.  By waiting until afterwards
	 * to do the IRELE, we ensure that we won't go inactive in the
	 * xfs_refcache_purge_ip routine (although that would be OK).
	 */
#ifndef SIM
	xfs_refcache_purge_ip(ip);
#endif
	IRELE(ip);
	return error;	
}


/*
 * xfs_link
 *
 */
STATIC int
xfs_link(
	vnode_t	*target_dir_vp,
	vnode_t	*src_vp,
	char	*target_name,
	cred_t	*credp)
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
	int			committed;
	struct ncfastdata	fastdata;

	vn_trace_entry(target_dir_vp, "xfs_link");
	/*
	 * Get the real vnode.
	 */
	if (VOP_REALVP(src_vp, &realvp) == 0) {
                src_vp = realvp;
	}
	vn_trace_entry(src_vp, "xfs_link");
        if (src_vp->v_type == VDIR) {
                return XFS_ERROR(EPERM);
	}

	sip = XFS_VTOI(src_vp);
	tdp = XFS_VTOI(target_dir_vp);

	if (DM_EVENT_ENABLED(src_vp->v_vfsp, tdp, DM_LINK)) {
		error = dm_namesp_event(DM_LINK, target_dir_vp, src_vp,
					target_name, NULL, 0, 0);
		if (error)
			return error;
	}

	mp = XFS_VFSTOM(target_dir_vp->v_vfsp);
        tp = xfs_trans_alloc(mp, XFS_TRANS_LINK);
	cancel_flags = XFS_TRANS_RELEASE_LOG_RES;
        if (error = xfs_trans_reserve(tp, 10, XFS_LINK_LOG_RES(mp),
				      0, XFS_TRANS_PERM_LOG_RES,
				      XFS_LINK_LOG_COUNT)) {
		cancel_flags = 0;
                goto error_return;
	}

	xfs_lock_2_inodes(sip, tdp);

	/*
	 * Increment vnode ref counts since xfs_trans_commit &
	 * xfs_trans_cancel will both unlock the inodes and
	 * decrement the associated ref counts.
	 */
	VN_HOLD(src_vp);
	VN_HOLD(target_dir_vp);
	xfs_trans_ijoin(tp, sip, XFS_ILOCK_EXCL);
        xfs_trans_ijoin(tp, tdp, XFS_ILOCK_EXCL);

	/*
	 * If the target directory has been removed, we can't link
	 * any more files in it.
	 */
	if (tdp->i_d.di_nlink == 0) {
		error = XFS_ERROR(ENOENT);
		goto error_return;
	}

	if (error = xfs_iaccess(tdp, IEXEC | IWRITE, credp)) {
                goto error_return;
	}

	error = xfs_dir_lookup_int(NULL, target_dir_vp, 0, target_name, 
				   NULL, &e_inum, NULL, &fastdata);
	if (error != ENOENT) {
		if (error == 0) {
			error = XFS_ERROR(EEXIST);
		}
		goto error_return;
	}

	XFS_BMAP_INIT(&free_list, &first_block);

	error = xfs_dir_createname(tp, tdp, target_name, sip->i_ino,
				   &first_block, &free_list, MAX_EXT_NEEDED);
	if (error) {
		goto abort_return;
	}
	xfs_ichgtime(tdp, XFS_ICHGTIME_MOD | XFS_ICHGTIME_CHG);
	tdp->i_gen++;
	xfs_trans_log_inode(tp, tdp, XFS_ILOG_CORE);


	xfs_bumplink(tp, sip);

	/*
	 * If this is a synchronous mount, make sure that the
	 * link transaction goes to disk before returning to
	 * the user.
	 */
	if (mp->m_flags & XFS_MOUNT_WSYNC) {
		xfs_trans_set_sync(tp);
	}

	error = xfs_bmap_finish (&tp, &free_list, first_block, &committed);
	if (error) {
		xfs_bmap_cancel(&free_list);
		goto abort_return;
	}

	error = xfs_trans_commit(tp, XFS_TRANS_RELEASE_LOG_RES);
	if (error) {
		return error;
	}

	dnlc_enter_fast(target_dir_vp, &fastdata, XFS_ITOV(sip), credp);

	if (DM_EVENT_ENABLED(src_vp->v_vfsp, tdp, DM_POSTLINK)) {
		(void) dm_namesp_event(DM_POSTLINK, target_dir_vp, src_vp,
				       target_name, NULL, 0, 0);
	}
	return 0;

 abort_return:
	cancel_flags |= XFS_TRANS_ABORT;
	/* FALLTHROUGH */
 error_return:
	xfs_trans_cancel(tp, cancel_flags);

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
xfs_ancestor_check(
	xfs_inode_t *src_dp, 
	xfs_inode_t *src_ip,
	xfs_inode_t *target_dp,
	xfs_inode_t *target_ip, 
	boolean_t   *state_has_changed)
{
	struct {
		xfs_inode_t     *ip;
		unsigned long   gen;
	}			i_tab[4], temp;
	int			i, j, num_inodes;
	xfs_mount_t		*mp;
	xfs_inode_t		*ip;
	xfs_ino_t		root_ino;
	int			error = 0;
	struct ncfastdata	fastdata;

	mp = src_dp->i_mount;
	root_ino = mp->m_sb.sb_rootino;

	/*
	 * We know that all the inodes involved are directories at this
	 * point.
	 */
	ASSERT((src_dp->i_d.di_mode & IFMT) == IFDIR);
	ASSERT((target_dp->i_d.di_mode & IFMT) == IFDIR);
	if (src_ip != NULL)
		ASSERT((src_ip->i_d.di_mode & IFMT) == IFDIR);
	if (target_ip != NULL)
		ASSERT((target_ip->i_d.di_mode & IFMT) == IFDIR);

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
	} else {
		num_inodes = 3;
	}

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
	xfs_iunlock(src_dp, XFS_ILOCK_EXCL);
	/*
	 * src_ip can't be same as src_dp
	 */
	xfs_iunlock(src_ip, XFS_ILOCK_EXCL);
	/*
	 * target_dp can't be same as src_dp
	 */
	if (target_dp != src_ip)
		xfs_iunlock(target_dp, XFS_ILOCK_EXCL);
	/*
	 * target_ip can't be same as src_ip or target_dp
	 */
	if (target_ip && target_ip != src_dp)
		xfs_iunlock(target_ip, XFS_ILOCK_EXCL);

	mutex_lock(&xfs_ancestormon, PINOD);

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
	xfs_ilock(ip, XFS_ILOCK_EXCL);

	while (ip->i_ino != root_ino) {
		xfs_ino_t	parent_ino;

		if (ip == src_ip) {
			error = XFS_ERROR(EINVAL);
			break;
		}
		error = xfs_dir_lookup_int(NULL, XFS_ITOV(ip), 0, "..",
					   NULL, &parent_ino, NULL,
					   &fastdata);
		if (error) {
			break;
		}
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
		xfs_iunlock(ip, XFS_ILOCK_EXCL);
		if (ip != target_dp)
			IRELE(ip);

		error = xfs_iget(mp, NULL, parent_ino, XFS_ILOCK_EXCL, &ip, 0);
		if (error) {
			goto relock_and_return;
		}
		ASSERT(ip != NULL);
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
	xfs_iunlock(ip, XFS_ILOCK_EXCL);
	if (ip != target_dp)
		IRELE(ip);

 relock_and_return:
	/*
         * Reacquire all the inode locks in exclusive mode. If an inode 
	 * appears twice in the list, it will only be locked once.
         */
        xfs_ilock(i_tab[0].ip, XFS_ILOCK_EXCL);
        for (i=1; i < num_inodes; i++) {
                if (i_tab[i].ip != i_tab[i-1].ip)
                        xfs_ilock(i_tab[i].ip, XFS_ILOCK_EXCL);
        }

	/*
	 * Unserialize efs_notancestor calls.
         */
        mutex_unlock(&xfs_ancestormon);

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



int rename_which_error_return = 0;

/*
 * xfs_rename
 *
 */
STATIC int
xfs_rename(
	vnode_t		*src_dir_vp,
	char		*src_name,
	vnode_t		*target_dir_vp,
	char		*target_name,
	pathname_t	*target_pnp,
	cred_t		*credp)
{
	xfs_trans_t	*tp;
	xfs_inode_t	*src_dp, *target_dp, *src_ip, *target_ip;
	xfs_mount_t	*mp;
	boolean_t	state_has_changed;
	int		new_parent;		/* moving to a new dir */
	int		src_is_directory;	/* src_name is a directory */
	int		error;		
        xfs_bmap_free_t free_list;
        xfs_fsblock_t   first_block;
	int		cancel_flags;
	int		committed;

	vn_trace_entry(src_dir_vp, "xfs_rename");
	vn_trace_entry(target_dir_vp, "xfs_rename");

	if (DM_EVENT_ENABLED(src_dir_vp->v_vfsp, XFS_VTOI(src_dir_vp), DM_RENAME) ||
	    DM_EVENT_ENABLED(target_dir_vp->v_vfsp, XFS_VTOI(target_dir_vp), DM_RENAME)) {
		error = dm_namesp_event(DM_RENAME, src_dir_vp, target_dir_vp,
					 src_name, target_name, 0, 0);
		if (error)
			return error;
	}

 start_over:
	XFS_BMAP_INIT(&free_list, &first_block);
	mp = XFS_VFSTOM(src_dir_vp->v_vfsp);
	tp = xfs_trans_alloc(mp, XFS_TRANS_RENAME);
	cancel_flags = XFS_TRANS_RELEASE_LOG_RES;
        if (error = xfs_trans_reserve(tp, 10, XFS_RENAME_LOG_RES(mp),
				      0, XFS_TRANS_PERM_LOG_RES,
				      XFS_RENAME_LOG_COUNT)) {
		cancel_flags = 0;
		rename_which_error_return = 1;
                goto error_return;
	}

	/*
	 * Lock all the participating inodes. Depending upon whether
	 * the target_name exists in the target directory, and
	 * whether the target directory is the same as the source
	 * directory, we can lock from 2 to 4 inodes.
	 * xfs_lock_for_rename() will return ENOENT if src_name
	 * does not exist in the source directory.
	 */
	src_dp = XFS_VTOI(src_dir_vp);
        target_dp = XFS_VTOI(target_dir_vp);
	while ((error = xfs_lock_for_rename(src_dp, target_dp, src_name,
				target_name, &src_ip, &target_ip)) == EAGAIN)
		continue;
	if (error) {
		rename_which_error_return = 2;
		goto error_return;
	}

	ASSERT(src_ip != NULL);

	ASSERT(!((src_name[0] == '.') && (src_name[1] == '\0')));
	ASSERT(!((target_name[0] == '.') && (target_name[1] == '\0')));

	new_parent = (src_dp != target_dp);

	/*
	 * Join all the inodes to the transaction. From this point on,
	 * we can rely on either trans_commit or trans_cancel to unlock
	 * them.  Note that we need to add a vnode reference to the
	 * directories since trans_commit & trans_cancel will decrement
	 * them when they unlock the inodes.  Also, we need to be careful
	 * not to add an inode to the transaction more than once.
	 */
        VN_HOLD(src_dir_vp);
        xfs_trans_ijoin(tp, src_dp, XFS_ILOCK_EXCL);
        if (new_parent) {
                VN_HOLD(target_dir_vp);
                xfs_trans_ijoin(tp, target_dp, XFS_ILOCK_EXCL);
        }
	if ((src_ip != src_dp) && (src_ip != target_dp)) {
		xfs_trans_ijoin(tp, src_ip, XFS_ILOCK_EXCL);
	}
        if (target_ip &&
	    (target_ip != src_ip) &&
	    (target_ip != src_dp) &&
	    (target_ip != target_dp)) {
                xfs_trans_ijoin(tp, target_ip, XFS_ILOCK_EXCL);
	}



	/*
	 * Make all the access checks.
	 */

	/*
	 * If the target directory has been removed, we can't create any
	 * more files in it.  We don't need to check the source dir,
	 * because it was checked in xfs_lock_for_rename() while looking
	 * for the source inode.  If it had been removed the source
	 * dir's gen count would have been bumped removing the last entry
	 * and then we'd have noticed that its link count had gone to zero.
	 */
	if (target_dp->i_d.di_nlink == 0) {
		error = XFS_ERROR(ENOENT);
		rename_which_error_return = 3;
		goto error_return;
	}
	if (error = xfs_iaccess(src_dp, IEXEC | IWRITE, credp)) {
		rename_which_error_return = 4;
                goto error_return;
	}
	if (error = xfs_stickytest(src_dp, src_ip, credp)) {
		rename_which_error_return = 5;
		goto error_return;
	}

	if (target_dp && (src_dp != target_dp)) {
		if (error = xfs_iaccess(target_dp, IEXEC | IWRITE, credp)) {
			rename_which_error_return = 6;
			goto error_return;
		}
		if ((target_ip != NULL) &&
		    (error = xfs_stickytest(target_dp, target_ip, credp))) {
			rename_which_error_return = 7;
			goto error_return;
		}
	}
	else {
		if ((target_ip != NULL) &&
		    (error = xfs_stickytest(src_dp, target_ip, credp))) {
			rename_which_error_return = 8;
                        goto error_return;
		}
	}

	if ((src_ip == src_dp) || (target_ip == target_dp)) {
		error = XFS_ERROR(EINVAL);
		rename_which_error_return = 9;
		goto error_return;
	}

	/*
	 * Source and target are identical.
	 */
	if (src_ip == target_ip) {
		error = 0;		/* no-op */
		rename_which_error_return = 10;
		goto error_return;
	}

	/*
	 * Directory renames require special checks.
	 */
	src_is_directory = ((src_ip->i_d.di_mode & IFMT) == IFDIR);

	if (src_is_directory) {

		ASSERT(src_ip->i_d.di_nlink >= 2);

		/*
		 * Cannot rename ".."
		 */
		if ((src_name[0] == '.') && (src_name[1] == '.') &&
		    (src_name[2] == '\0')) {
			error = XFS_ERROR(EINVAL);
			rename_which_error_return = 11;
			goto error_return;
		}
                if ((target_name[0] == '.') && (target_name[1] == '.') &&
                    (target_name[2] == '\0')) {
                        error = XFS_ERROR(EINVAL);
			rename_which_error_return = 12;
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

			error = xfs_ancestor_check(src_dp, src_ip,
						   target_dp, target_ip,
						   &state_has_changed);
			if (error) {
				rename_which_error_return = 13;
				goto error_return;
			}

			if (state_has_changed) {
				xfs_trans_cancel(tp, 0);
				goto start_over;
			}
		}
	}


	/*
	 * We should be in the same file system.
	 */
	ASSERT(XFS_VFSTOM(src_dir_vp->v_vfsp) ==
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
		error = xfs_dir_createname(tp, target_dp, target_name,
					   src_ip->i_ino, &first_block,
					   &free_list, MAX_EXT_NEEDED);
		if (error) {
			rename_which_error_return = 14;
			goto abort_return;
		}
		xfs_ichgtime(target_dp, XFS_ICHGTIME_MOD | XFS_ICHGTIME_CHG);

		if (new_parent && src_is_directory) {
			xfs_bumplink(tp, target_dp);
		}
	} else { /* target_ip != NULL */

		/*
		 * If target exists and it's a directory, check that both
		 * target and source are directories and that target can be
		 * destroyed, or that neither is a directory.
		 */

		if ((target_ip->i_d.di_mode & IFMT) == IFDIR) {

			/*
			 * Make sure src is a directory.
			 */
			if (!src_is_directory) {
				error = XFS_ERROR(EISDIR);
				rename_which_error_return = 15;
				goto error_return;
			}

			/*
			 * Make sure target dir is empty.
			 */
			if (!(xfs_dir_isempty(target_ip)) || 
			    (target_ip->i_d.di_nlink > 2)) {
				error = XFS_ERROR(EEXIST);
				rename_which_error_return = 16;
                                goto error_return;
                        }

			if (ABI_IS_SVR4(u.u_procp->p_abi) &&
                            XFS_ITOV(target_ip)->v_vfsmountedhere) {
                                error = XFS_ERROR(EBUSY);
				rename_which_error_return = 17;
                                goto error_return;
                        }

		} else {
			if (src_is_directory) {
				error = XFS_ERROR(ENOTDIR);
				rename_which_error_return = 18;
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
		error = xfs_dir_replace(tp, target_dp, target_name,
			((target_pnp != NULL) ? target_pnp->pn_complen :
			 strlen(target_name)), src_ip->i_ino);
		if (error) {
			rename_which_error_return = 19;
			goto abort_return;
		}
		xfs_ichgtime(target_dp, XFS_ICHGTIME_MOD | XFS_ICHGTIME_CHG);

		dnlc_enter(target_dir_vp, target_name, XFS_ITOV(src_ip),
			   credp);

		/*
		 * Decrement the link count on the target since the target
		 * dir no longer points to it.
		 */
		error = xfs_droplink(tp, target_ip);
		if (error) {
			rename_which_error_return = 20;
			goto abort_return;;
		}

		if (src_is_directory) {
			/*
			 * Drop the link from the old "." entry.
			 */
			error = xfs_droplink(tp, target_ip);
			if (error) {
				rename_which_error_return = 21;
				goto abort_return;
			}
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
		error = xfs_dir_replace(tp, src_ip, "..", 2,
					target_dp->i_ino);
		if (error) {
			rename_which_error_return = 22;
			goto abort_return;
		}
		xfs_ichgtime(src_ip, XFS_ICHGTIME_MOD | XFS_ICHGTIME_CHG);

		dnlc_remove(XFS_ITOV(src_ip), "..");
		
		/*
		 * Decrement link count on src_directory since the
		 * entry that's moved no longer points to it.
		 */
		error = xfs_droplink(tp, src_dp);
		if (error) {
			rename_which_error_return = 23;
			goto abort_return;
		}
	}


	error = xfs_dir_removename(tp, src_dp, src_name, &first_block,
				   &free_list, MAX_EXT_NEEDED);
	if (error) {
		rename_which_error_return = 24;
		goto abort_return;
	}
	xfs_ichgtime(src_dp, XFS_ICHGTIME_MOD | XFS_ICHGTIME_CHG);

	dnlc_remove(src_dir_vp, src_name);


	/*
	 * Update the generation counts on all the directory inodes
	 * that we're modifying.
	 */
	src_dp->i_gen++;
	xfs_trans_log_inode(tp, src_dp, XFS_ILOG_CORE);

	if (new_parent) {
                target_dp->i_gen++;
                xfs_trans_log_inode(tp, target_dp, XFS_ILOG_CORE);
	}

	/*
	 * If there was a target inode, take an extra reference on
	 * it here so that it doesn't go to xfs_inactive() from
	 * within the commit.
	 */
	if (target_ip != NULL) {
		IHOLD(target_ip);
	}

	/*
	 * If this is a synchronous mount, make sure that the
	 * rename transaction goes to disk before returning to
	 * the user.
	 */
	if (mp->m_flags & XFS_MOUNT_WSYNC) {
		xfs_trans_set_sync(tp);
	}

	error = xfs_bmap_finish(&tp, &free_list, first_block, &committed);
	if (error) {
		xfs_bmap_cancel(&free_list);
		xfs_trans_cancel(tp, (XFS_TRANS_RELEASE_LOG_RES |
				 XFS_TRANS_ABORT));
		if (target_ip != NULL) {
			IRELE(target_ip);
		}
		return error;
	}
	/*
	 * trans_commit will unlock src_ip, target_ip & decrement
	 * the vnode references.
	 */
	error = xfs_trans_commit(tp, XFS_TRANS_RELEASE_LOG_RES);
	if (target_ip != NULL) {
#ifndef SIM
		xfs_refcache_purge_ip(target_ip);
#endif
		IRELE(target_ip);
	}
	if (error) {
		return error;
	}

	if (DM_EVENT_ENABLED(src_dir_vp->v_vfsp, XFS_VTOI(src_dir_vp), DM_POSTRENAME) ||
	    DM_EVENT_ENABLED(target_dir_vp->v_vfsp, XFS_VTOI(target_dir_vp), DM_POSTRENAME)) {
		(void) dm_namesp_event(DM_POSTRENAME, src_dir_vp,
			target_dir_vp, src_name, target_name, 0, 0);
	}

	return 0;

 abort_return:
	cancel_flags |= XFS_TRANS_ABORT;
	/* FALLTHROUGH */
 error_return:
	xfs_bmap_cancel(&free_list);
	xfs_trans_cancel(tp, cancel_flags);
	return error;
}


/*
 * xfs_mkdir
 *
 */
STATIC int
xfs_mkdir(
	vnode_t		*dir_vp,
	char		*dir_name,
	vattr_t		*vap,
	vnode_t		**vpp,
	cred_t		*credp)
{
        xfs_inode_t             *dp;
	xfs_inode_t		*cdp;	/* inode of created dir */
        xfs_trans_t             *tp = NULL;
        xfs_ino_t               e_inum;
	dev_t			rdev;
	mode_t			mode;
        xfs_mount_t		*mp;
	int			cancel_flags;
	int			error;
	int			committed;
        xfs_bmap_free_t         free_list;
        xfs_fsblock_t           first_block;
	boolean_t		dp_joined_to_trans = B_FALSE;
	struct ncfastdata	fastdata;

        dp = XFS_VTOI(dir_vp);

	if (DM_EVENT_ENABLED(dir_vp->v_vfsp, dp, DM_CREATE)) {
		error = dm_namesp_event(DM_CREATE, dir_vp, NULL, dir_name,
				       NULL, vap->va_mode, 0);
		if (error)
			return error;
	}
	vn_trace_entry(dir_vp, "xfs_mkdir");
	mp = XFS_VFSTOM(dir_vp->v_vfsp);
        tp = xfs_trans_alloc(mp, XFS_TRANS_MKDIR);
	cancel_flags = XFS_TRANS_RELEASE_LOG_RES;
        if (error = xfs_trans_reserve(tp,
				      XFS_IALLOC_BLOCKS(mp) +
				      XFS_IN_MAXLEVELS(mp) + 10,
				      XFS_MKDIR_LOG_RES(mp), 0,
				      XFS_TRANS_PERM_LOG_RES,
				      XFS_MKDIR_LOG_COUNT)) {
		cancel_flags = 0;
		dp = NULL;
		goto error_return;
	}

        xfs_ilock(dp, XFS_ILOCK_EXCL);

	/*
	 * Since dp was not locked between VOP_LOOKUP and VOP_MKDIR,
	 * the directory could have been removed.
	 */
        if (dp->i_d.di_nlink <= 0) {
		error = XFS_ERROR(ENOENT);
                goto error_return;
	}

        error = xfs_dir_lookup_int(NULL, dir_vp, 0, dir_name, NULL,
				   &e_inum, NULL, &fastdata);
        if (error != ENOENT) {
		if (error == 0)
			error = XFS_ERROR(EEXIST);
                goto error_return;
	}

	/*
	 * check access.
	 */
	if (error = xfs_iaccess(dp, IEXEC | IWRITE, credp)) {
                goto error_return;
	}


	/*
	 * create the directory inode.
	 */
	rdev = (vap->va_mask & AT_RDEV) ? vap->va_rdev : NODEV;
        mode = IFDIR | (vap->va_mode & ~IFMT);
	error = xfs_dir_ialloc(&tp, dp, mode, 2, rdev, credp, &cdp, NULL);
	if (error) {
		if (error == ENOSPC) {
			goto error_return;
		}
		goto abort_return;
	}
	ITRACE(cdp);

	/*
	 * Now we add the directory inode to the transaction.
	 * We waited until now since xfs_dir_ialloc might start
	 * a new transaction.  Had we joined the transaction
	 * earlier, the locks might have gotten released.
	 */
	VN_HOLD(dir_vp);
        xfs_trans_ijoin(tp, dp, XFS_ILOCK_EXCL);
	dp_joined_to_trans = B_TRUE;

	XFS_BMAP_INIT(&free_list, &first_block);

	error = xfs_dir_createname(tp, dp, dir_name, cdp->i_ino,
				   &first_block, &free_list, MAX_EXT_NEEDED);
	if (error) {
		goto error1;
	}
	xfs_ichgtime(dp, XFS_ICHGTIME_MOD | XFS_ICHGTIME_CHG);

	dnlc_enter_fast(dir_vp, &fastdata, XFS_ITOV(cdp), NOCRED);
	
	error = xfs_dir_init(tp, cdp, dp);
	if (error) {
		goto error2;
	}

	cdp->i_gen = 1;
	xfs_bumplink(tp, dp);

	dnlc_remove(XFS_ITOV(cdp), "..");

	*vpp = XFS_ITOV(cdp);

	IHOLD(cdp);

	/*
	 * If this is a synchronous mount, make sure that the
	 * mkdir transaction goes to disk before returning to
	 * the user.
	 */
	if (mp->m_flags & XFS_MOUNT_WSYNC) {
		xfs_trans_set_sync(tp);
	}

	error = xfs_bmap_finish(&tp, &free_list, first_block, &committed);
	if (error) {
		goto error2;
	}

	error = xfs_trans_commit(tp, XFS_TRANS_RELEASE_LOG_RES);
	if (error) {
		dnlc_remove_fast(dir_vp, &fastdata);
		return error;
	}

	if (DM_EVENT_ENABLED(dir_vp->v_vfsp, dp, DM_POSTCREATE)) {
		(void) dm_namesp_event(DM_POSTCREATE, dir_vp, NULL, dir_name,
				       NULL, cdp->i_d.di_mode, 0);
	}
	return 0;

 error2:
	dnlc_remove_fast(dir_vp, &fastdata);
 error1:
	xfs_bmap_cancel(&free_list);
 abort_return:
	cancel_flags |= XFS_TRANS_ABORT;
 error_return:
	xfs_trans_cancel(tp, cancel_flags);

	if (!dp_joined_to_trans && (dp != NULL)) {
		xfs_iunlock(dp, XFS_ILOCK_EXCL);
	}

	return error;
}


/*
 * xfs_rmdir
 *
 */
STATIC int
xfs_rmdir(
	vnode_t		*dir_vp,
	char		*name,
	vnode_t		*current_dir_vp,
	cred_t		*credp)
{
        xfs_inode_t             *dp;
        xfs_inode_t             *cdp;   /* child directory */
        xfs_trans_t             *tp;
        xfs_ino_t               e_inum;
	xfs_mount_t		*mp;
        int                     error;
        xfs_bmap_free_t         free_list;
        xfs_fsblock_t           first_block;
	int			cancel_flags;
	int			committed;
	struct ncfastdata	fastdata;

	vn_trace_entry(dir_vp, "xfs_rmdir");

	if (DM_EVENT_ENABLED(dir_vp->v_vfsp, XFS_VTOI (dir_vp), DM_REMOVE)) {
		error = dm_namesp_event(DM_REMOVE, dir_vp, NULL,
					name, NULL, 0, 0);
		if (error)
			return error;
	}
	mp = XFS_VFSTOM(dir_vp->v_vfsp);
	tp = xfs_trans_alloc(mp, XFS_TRANS_RMDIR);
	cancel_flags = XFS_TRANS_RELEASE_LOG_RES;
        if (error = xfs_trans_reserve(tp, 10, XFS_REMOVE_LOG_RES(mp),
				      0, XFS_TRANS_PERM_LOG_RES,
				      XFS_DEFAULT_LOG_COUNT)) {
		cancel_flags = 0;
                goto error_return;
	}
	XFS_BMAP_INIT(&free_list, &first_block);

        dp = XFS_VTOI(dir_vp);
	cdp = NULL;

	VN_HOLD(dir_vp);
        xfs_ilock(dp, XFS_ILOCK_EXCL);
	xfs_trans_ijoin(tp, dp, XFS_ILOCK_EXCL);

	/*
	 * If the directory has been removed, we don't want to do any
	 * lookups and we know it has no entries.
	 */
	if (dp->i_d.di_nlink == 0) {
		error = XFS_ERROR(ENOENT);
		goto error_return;
	}

	if (error = xfs_iaccess(dp, IEXEC | IWRITE, credp)) {
                goto error_return;
	}

        error = xfs_dir_lookup_int(NULL, dir_vp, DLF_IGET, name, NULL,
				   &e_inum, &cdp, &fastdata);
	if (error) {
		goto error_return;
	}

	ITRACE(cdp);
	xfs_ilock(cdp, XFS_ILOCK_EXCL);
	xfs_trans_ijoin(tp, cdp, XFS_ILOCK_EXCL);

	if (error = xfs_stickytest(dp, cdp, credp)) {
                goto error_return;
	}


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
	ASSERT(cdp->i_d.di_nlink >= 2);
	if (cdp->i_d.di_nlink != 2) {
		error = XFS_ERROR(EEXIST);
		goto error_return;
        }
	if (!xfs_dir_isempty(cdp)) {
		error = XFS_ERROR(EEXIST);
		goto error_return;
	}

        error = xfs_dir_removename(tp, dp, name, &first_block, &free_list, 0);
	if (error) {
		goto error1;
	}

	xfs_ichgtime(dp, XFS_ICHGTIME_MOD | XFS_ICHGTIME_CHG);

	dnlc_remove_fast(dir_vp, &fastdata);

	/*
	 * Drop the link from cdp's "..".
	 */
	error = xfs_droplink(tp, dp);
	if (error) {
		goto error1;
	}

	/*
	 * Drop the link from dp to cdp.
	 */
	error = xfs_droplink(tp, cdp);
	if (error) {
		goto error1;
	}

	/*
	 * Drop the "." link from cdp to self.
	 */
	error = xfs_droplink(tp, cdp);
	if (error) {
		goto error1;
	}

	/*
	 * Take an extra ref on the child vnode so that it
	 * does not go to xfs_inactive() from within the commit.
	 */
	IHOLD(cdp);

	/*
	 * If this is a synchronous mount, make sure that the
	 * rmdir transaction goes to disk before returning to
	 * the user.
	 */
	if (mp->m_flags & XFS_MOUNT_WSYNC) {
		xfs_trans_set_sync(tp);
	}

	error = xfs_bmap_finish (&tp, &free_list, first_block, &committed);
	if (error) {
		xfs_bmap_cancel(&free_list);
		xfs_trans_cancel(tp, (XFS_TRANS_RELEASE_LOG_RES |
				 XFS_TRANS_ABORT));
		IRELE(cdp);
		return error;
	}

	error = xfs_trans_commit(tp, XFS_TRANS_RELEASE_LOG_RES);
	if (error) {
		IRELE(cdp);
		return error;
	}

	if (DM_EVENT_ENABLED(dir_vp->v_vfsp, dp, DM_POSTREMOVE)) {
		(void) dm_namesp_event(DM_POSTREMOVE, dir_vp, NULL,
				       name, NULL, cdp->i_d.di_mode, 0);
	}
	IRELE(cdp);

	return 0;

 error1:
	xfs_bmap_cancel(&free_list);
	cancel_flags |= XFS_TRANS_ABORT;
 error_return:
	xfs_trans_cancel(tp, cancel_flags);
	return error;
}



/*
 * xfs_readdir
 *
 * Read dp's entries starting at uiop->uio_offset and translate them into
 * bufsize bytes worth of struct dirents starting at bufbase.
 */
STATIC int
xfs_readdir(
	vnode_t		*dir_vp,
	uio_t		*uiop,
	cred_t		*credp,
	int		*eofp)
{
        xfs_inode_t             *dp;
        xfs_trans_t             *tp = NULL;
	int			error;
	uint			lock_mode;
	off_t			start_offset;

	vn_trace_entry(dir_vp, "xfs_readdir");
        dp = XFS_VTOI(dir_vp);
        lock_mode = xfs_ilock_map_shared(dp);

        if ((dp->i_d.di_mode & IFMT) != IFDIR) {
		xfs_iunlock_map_shared(dp, lock_mode);
                return XFS_ERROR(ENOTDIR);
        }

	if (error = xfs_iaccess(dp, IEXEC, credp)) {
		xfs_iunlock_map_shared(dp, lock_mode);
                return error;
	}


	/* If the directory has been removed after it was opened. */
        if (dp->i_d.di_nlink <= 0) {
                xfs_iunlock_map_shared(dp, lock_mode);
                return 0;
        }

	start_offset = uiop->uio_offset;
	error  = xfs_dir_getdents(tp, dp, uiop, eofp);
	if (start_offset != uiop->uio_offset) {
		xfs_ichgtime(dp, XFS_ICHGTIME_ACC);
	}

	xfs_iunlock_map_shared(dp, lock_mode);
	
	return error;
}


/*
 * The number of blocks that the directory code requires for
 * the createname operation.
 */
#define DIR_NEEDS 7

/*
 * xfs_symlink
 *
 */
STATIC int
xfs_symlink(
	vnode_t		*dir_vp,
	char	       	*link_name,
	vattr_t		*vap,
	char		*target_path,
	cred_t		*credp)
{
	xfs_trans_t		*tp = NULL;
	xfs_mount_t		*mp;
	xfs_inode_t		*dp, *ip;
        int 			error = 0, pathlen;
        struct pathname		cpn, ccpn;
	xfs_ino_t		e_inum;
	dev_t			rdev;
	xfs_bmap_free_t		free_list;
	xfs_fsblock_t		first_block;
	boolean_t		dp_joined_to_trans = B_FALSE;
	uint			cancel_flags;
	int			committed;
	struct ncfastdata	fastdata;

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
        dp = XFS_VTOI(dir_vp);

	if (DM_EVENT_ENABLED(dir_vp->v_vfsp, dp, DM_SYMLINK)) {
		error = dm_namesp_event(DM_SYMLINK, dir_vp, NULL,
					link_name, target_path, 0, 0);
		if (error)
			return error;
	}

	mp = XFS_VFSTOM(dir_vp->v_vfsp);
        tp = xfs_trans_alloc(mp, XFS_TRANS_SYMLINK);
	cancel_flags = XFS_TRANS_RELEASE_LOG_RES;
        if (error = xfs_trans_reserve(tp,
				      XFS_IALLOC_BLOCKS(mp) +
				      XFS_IN_MAXLEVELS(mp) + 12,
				      XFS_SYMLINK_LOG_RES(mp), 0,
				      XFS_TRANS_PERM_LOG_RES,
				      XFS_SYMLINK_LOG_COUNT)) {
		cancel_flags = 0;
		dp = NULL;
                goto error_return;
	}

	xfs_ilock(dp, XFS_ILOCK_EXCL);

	/*
	 * If the directory has been removed, then we can't create
	 * anything in it.
	 */
	if (dp->i_d.di_nlink == 0) {
		error = XFS_ERROR(ENOENT);
		goto error_return;
	}

	if (error = xfs_iaccess(dp, IEXEC | IWRITE, credp)) {
                goto error_return;
	}

        error = xfs_dir_lookup_int(NULL, dir_vp, 0, link_name, NULL,
				   &e_inum, NULL, &fastdata);
	if (error != ENOENT) {
		if (!error) {
			error = XFS_ERROR(EEXIST);
		}
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
	if (error) {
		goto error1;
	}
	ITRACE(ip);

	VN_HOLD(dir_vp);
        xfs_trans_ijoin(tp, dp, XFS_ILOCK_EXCL);
        dp_joined_to_trans = B_TRUE;

	/*
	 * If the symlink will fit into the inode, write it inline.
	 */
	if (pathlen <= XFS_IFORK_DSIZE(ip)) {
		xfs_idata_realloc(ip, pathlen, XFS_DATA_FORK);
		bcopy(target_path, ip->i_df.if_u1.if_data, pathlen);
		ip->i_d.di_size = pathlen;

		/*
		 * The inode was initially created in extent format.
		 */
		ip->i_df.if_flags &= ~(XFS_IFEXTENTS | XFS_IFBROOT);
		ip->i_df.if_flags |= XFS_IFINLINE;

		ip->i_d.di_format = XFS_DINODE_FMT_LOCAL;
		xfs_trans_log_inode(tp, ip, XFS_ILOG_DDATA | XFS_ILOG_CORE);

	} else {
                xfs_fileoff_t   first_fsb;
                xfs_filblks_t   fs_blocks;
                int             nmaps;
                xfs_bmbt_irec_t mval[SYMLINK_MAPS];
                daddr_t         d;
                char            *cur_chunk;
                int             byte_cnt, n;
		struct 		buf *bp;

		first_fsb = 0;
		fs_blocks = XFS_B_TO_FSB(mp, pathlen);
		nmaps = SYMLINK_MAPS;

		error = xfs_bmapi(tp, ip, first_fsb, fs_blocks,
				  XFS_BMAPI_WRITE | XFS_BMAPI_METADATA,
				  &first_block, fs_blocks+DIR_NEEDS, mval,
				  &nmaps, &free_list);
		if (error) {
			goto error1;
		}

		ip->i_d.di_size = pathlen;
		xfs_trans_log_inode(tp, ip, XFS_ILOG_CORE);

		cur_chunk = target_path;
		for (n = 0; n < nmaps; n++) {
			d = XFS_FSB_TO_DADDR(mp, mval[n].br_startblock);
			byte_cnt = XFS_FSB_TO_B(mp, mval[n].br_blockcount);
			bp = xfs_trans_get_buf(tp, mp->m_dev, d, 
					       BTOBB(byte_cnt), 0);
			ASSERT(bp && !geterror(bp));
			if (pathlen < byte_cnt) {
				byte_cnt = pathlen;
			}
			pathlen -= byte_cnt;

			bcopy(cur_chunk, bp->b_un.b_addr, byte_cnt);
			cur_chunk += byte_cnt;

			xfs_trans_log_buf(tp, bp, 0, byte_cnt);
		}
	}


	/*
	 * Create the directory entry for the symlink.
	 */
	error = xfs_dir_createname(tp, dp, link_name, ip->i_ino,
				   &first_block, &free_list, MAX_EXT_NEEDED);
	if (error) {
		goto error1;
	}
	xfs_ichgtime(dp, XFS_ICHGTIME_MOD | XFS_ICHGTIME_CHG);

        dnlc_enter_fast(dir_vp, &fastdata, XFS_ITOV(ip), NOCRED);

	/*
	 * If this is a synchronous mount, make sure that the
	 * symlink transaction goes to disk before returning to
	 * the user.
	 */
	if (mp->m_flags & XFS_MOUNT_WSYNC) {
		xfs_trans_set_sync(tp);
	}

	error = xfs_bmap_finish(&tp, &free_list, first_block, &committed);
	if (error) {
		goto error2;
	}
	error = xfs_trans_commit(tp, XFS_TRANS_RELEASE_LOG_RES);
	if (error) {
		dnlc_remove_fast(dir_vp, &fastdata);
		return error;
	}

	if (DM_EVENT_ENABLED(dir_vp->v_vfsp, dp, DM_POSTSYMLINK)) {
		(void) dm_namesp_event(DM_POSTSYMLINK, dir_vp, XFS_ITOV(ip),
				       link_name, target_path, 0, 0);
	}
	return 0;

 error2:
	dnlc_remove_fast(dir_vp, &fastdata);
 error1:
	xfs_bmap_cancel(&free_list);
	cancel_flags |= XFS_TRANS_ABORT;
 error_return:
	xfs_trans_cancel(tp, cancel_flags);

	if (!dp_joined_to_trans && (dp != NULL)) {
		xfs_iunlock(dp, XFS_ILOCK_EXCL);
	}

	return error;
}

/*
 * This is called from the customized NFS server code that
 * keeps the fid structure on the stack rather than having
 * us kmem_alloc one.  It is much more CPU efficient to do
 * it this way.
 */
int
xfs_fast_fid(
	vnode_t		*vp,
	xfs_fid_t	*xfid)
{
	xfs_inode_t	*ip;
	xfs_mount_t	*mp;

	ASSERT(xfs_do_fast_fid != 0);

	mp = XFS_VFSTOM(vp->v_vfsp);
	if (XFS_INO_BITS(mp) > (NBBY * sizeof(xfs_fid_ino_t))) {
		/*
		 * If the ino won't fit into the __uint32_t that's
		 * in our xfs_fid structure, then return an error.
		 */
		return XFS_ERROR(EFBIG);
	}

	ip = XFS_VTOI(vp);
	xfid->fid_len = sizeof(xfs_fid_t) - sizeof(xfid->fid_len);
	xfid->fid_pad = 0;
	xfid->fid_ino = (xfs_fid_ino_t)ip->i_ino;
	xfid->fid_gen = ip->i_d.di_gen;	

	return 0;
}

/*
 * xfs_fid
 *
 */
STATIC int
xfs_fid(
	vnode_t	*vp,
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
		return XFS_ERROR(EFBIG);
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
xfs_rwlock(
	vnode_t		*vp,
	vrwlock_t	locktype)
{
	xfs_inode_t	*ip;

	ip = XFS_VTOI(vp);
	if (locktype == VRWLOCK_WRITE) {
		xfs_ilock(ip, XFS_IOLOCK_EXCL);
	} else {
		ASSERT((locktype == VRWLOCK_READ) ||
		       (locktype == VRWLOCK_WRITE_DIRECT));
		xfs_ilock(ip, XFS_IOLOCK_SHARED);
	}
	return;
}


/*
 * xfs_rwunlock
 *
 */
STATIC void
xfs_rwunlock(
	vnode_t		*vp,
	vrwlock_t	locktype)
{
        xfs_inode_t     *ip;
	xfs_inode_t	*release_ip;

        ip = XFS_VTOI(vp);
	if (locktype == VRWLOCK_WRITE) {
		/*
		 * In the write case, we may have added a new entry to
		 * the reference cache.  This might store a pointer to
		 * an inode to be released in this inode.  If it is there,
		 * clear the pointer and release the inode after unlocking
		 * this one.
		 */
		release_ip = ip->i_release;
		ip->i_release = NULL;
        	xfs_iunlock (ip, XFS_IOLOCK_EXCL);
		
		if (release_ip != NULL) {
			VN_RELE(XFS_ITOV(release_ip));
		}
	} else {
		ASSERT((locktype == VRWLOCK_READ) ||
		       (locktype == VRWLOCK_WRITE_DIRECT));
        	xfs_iunlock(ip, XFS_IOLOCK_SHARED);
	}
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
/*ARGSUSED*/
STATIC int
xfs_seek(
	vnode_t		*vp,
	off_t		old_offset,
	off_t		*new_offsetp)
{
	if ((*new_offsetp > XFS_MAX_FILE_OFFSET) ||
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
xfs_frlock(
	vnode_t		*vp,
	int		cmd,
	flock_t		*flockp,
	int		flag,
	off_t		offset,
	cred_t		*credp)
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
 */
STATIC int
xfs_map(
	vnode_t		*vp,
	off_t		offset,
	preg_t		*pregp,
	addr_t		*addrp,
	size_t		len,
	uint		prot,
	uint		max_prot,
	uint		flags,
	cred_t		*credp)
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
/*ARGSUSED*/
STATIC int
xfs_addmap(
	vnode_t		*vp,
	off_t		offset,
	preg_t		*pregp,
	addr_t		addr,
	size_t		len,
	uint		prot,
	uint		max_prot,
	uint		flags,
	cred_t		*credp)
{
#ifdef DEBUG
	xfs_inode_t	*ip;

	ip = XFS_VTOI(vp);
	xfs_ilock(ip, XFS_ILOCK_EXCL);
	ASSERT(vp->v_mreg);
	ip->i_mapcnt += btoc(len);
	xfs_iunlock(ip, XFS_ILOCK_EXCL);
#endif /* DEBUG */
	return 0;
}


/*
 * xfs_delmap
 *
 * This is called when mappings to the given file are deleted.  All
 * we do is decrement our count of the number of pages mapped in this
 * file.  This count used to be used in xfs_frlock(), but now it is
 * used only for debugging.
 */
/*ARGSUSED*/
STATIC int
xfs_delmap(
	vnode_t		*vp,
	off_t		offset,
	preg_t		*pregp,
	addr_t		addr,
	size_t		len,
	uint		prot,
	uint		max_prot,
	uint		flags,
	cred_t		*credp)
{
#ifdef DEBUG
	xfs_inode_t	*ip;

	ip = XFS_VTOI(vp);
	xfs_ilock(ip, XFS_ILOCK_EXCL);
	ip->i_mapcnt -= btoc(len);
	ASSERT(((long)ip->i_mapcnt) >= 0);
	xfs_iunlock(ip, XFS_ILOCK_EXCL);
#endif /* DEBUG */
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
/*ARGSUSED*/
STATIC int
xfs_allocstore(
	vnode_t		*vp,
	off_t		offset,
	size_t		count,
	cred_t		*credp)
{
	xfs_mount_t	*mp;
	xfs_inode_t	*ip;
	off_t		isize;
	xfs_fileoff_t	offset_fsb;
	xfs_fileoff_t	last_fsb;
        xfs_fileoff_t	curr_off_fsb;
	xfs_fileoff_t	unmap_offset_fsb;
	xfs_filblks_t	count_fsb;
	xfs_filblks_t	unmap_len_fsb;
	xfs_fsblock_t	firstblock;
	xfs_bmbt_irec_t	*imapp;
	xfs_bmbt_irec_t	*last_imapp;
	int		i;
	int		nimaps;
	int		orig_nimaps;
	int		error;
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
	count_fsb = (xfs_filblks_t)(last_fsb - offset_fsb);
	orig_nimaps = NDPP;
	firstblock = NULLFSBLOCK;
	error = xfs_bmapi(NULL, ip, offset_fsb, count_fsb, 0, &firstblock, 0,
			  orig_imap, &orig_nimaps, NULL);
	if (error) {
		xfs_iunlock(ip, XFS_ILOCK_EXCL);
		return XFS_ERROR(error);
	}
	ASSERT(orig_nimaps > 0);

	curr_off_fsb = offset_fsb;
	while (count_fsb > 0) {
		nimaps = XFS_BMAP_MAX_NMAP;
		firstblock = NULLFSBLOCK;
		error = xfs_bmapi(NULL, ip, curr_off_fsb,
				  (xfs_filblks_t)(last_fsb - curr_off_fsb),
				  XFS_BMAPI_DELAY | XFS_BMAPI_WRITE,
				  &firstblock, 1, imap, &nimaps, NULL);
		if (error || (nimaps == 0)) {
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
			ASSERT(((long)count_fsb) >= 0);
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
	 *
	 * We know we can't get errors here, since the extent list has
	 * already been read in and we're only removing delayed allocation
	 * extents.
	 */
	unmap_offset_fsb = offset_fsb;
	imapp = &orig_imap[0];
	last_imapp = &orig_imap[orig_nimaps - 1];
	while (imapp <= last_imapp) {
		if (unmap_offset_fsb != imapp->br_startoff) {
			unmap_len_fsb = imapp->br_startoff - unmap_offset_fsb;
			firstblock = NULLFSBLOCK;
			(void) xfs_bunmapi(NULL, ip, unmap_offset_fsb,
					    unmap_len_fsb, 0, 1, &firstblock,
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
				firstblock = NULLFSBLOCK;
				(void) xfs_bunmapi(NULL, ip,
						   unmap_offset_fsb,
						   unmap_len_fsb, 0, 1,
						   &firstblock, NULL, NULL);
			}
		}
		imapp++;
	}
	xfs_iunlock(ip, XFS_ILOCK_EXCL);
	if (!error) {
		error = XFS_ERROR(ENOSPC);
	}
	return error;
}


/*
 * xfs_fcntl
 */
/*ARGSUSED*/
STATIC int
xfs_fcntl(
	vnode_t		*vp,
	int		cmd,
	void		*arg,
	int		flags,
	off_t		offset,
	cred_t		*credp,
	rval_t		*rvalp)
{
	int			error = 0;
	xfs_mount_t		*mp;
	struct flock		bf;
	struct irix5_flock	i5_bf;
	extern int		scache_linemask;
	
	vn_trace_entry(vp, "xfs_fcntl");
	mp = XFS_VFSTOM( vp->v_vfsp );
	switch (cmd) {
	case F_DIOINFO: {
		struct dioattr	da;

		/* only works on files opened for direct I/O */
		if (!(flags & FDIRECT)) {
			error = XFS_ERROR(EINVAL);
			break;
		}
                /*
		 * We align to the secondary cache line size so that we
		 * don't have to worry about nasty writeback caches on
		 * I/O incoherent machines.  Making this less than a page
		 * requires setting the maximum I/O size to 1 page less
		 * than maxdmasz.  This is for the case of a maximum
		 * size I/O that is not page aligned.  It requires the
		 * maximum size plus 1 pages.
                 */
		ASSERT(scache_linemask != 0);
		da.d_mem = scache_linemask + 1;

		/*
		 * this only really needs to be BBSIZE.
		 * it is set to the file system block size to
		 * avoid having to do block zeroing on short writes.
		 */
		da.d_miniosz = mp->m_sb.sb_blocksize;
		da.d_maxiosz = XFS_FSB_TO_B(mp,
				    XFS_B_TO_FSBT(mp, ctob(v.v_maxdmasz - 1)));

		if (copyout(&da, arg, sizeof(da))) {
			error = XFS_ERROR(EFAULT);
		}
		break;
	    }

	case F_FSGETXATTR:
	case F_FSGETXATTRA: {
		struct fsxattr fa;
		vattr_t va;

		error = xfs_getattr(vp, &va,
			AT_XFLAGS|AT_EXTSIZE|AT_NEXTENTS|AT_UUID|AT_ANEXTENTS,
			credp);
		if (error) {
			break;
		}
		fa.fsx_xflags = va.va_xflags;
		fa.fsx_extsize = va.va_extsize;
		fa.fsx_nextents =
			(cmd == F_FSGETXATTR) ?
				va.va_nextents : va.va_anextents;
		fa.fsx_uuid = va.va_uuid;
		if (copyout(&fa, arg, sizeof(fa))) {
			error = XFS_ERROR(EFAULT);
		}
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

	case F_GETBMAP:
	case F_GETBMAPA: {
		struct getbmap bm;

		if (copyin(arg, &bm, sizeof(bm))) {
			error = XFS_ERROR(EFAULT);
			break;
		}
		if (bm.bmv_count < 2) {
			error = XFS_ERROR(EINVAL);
			break;
		}
		error = xfs_getbmap(vp, &bm, (struct getbmap *)arg + 1,
			cmd == F_GETBMAP ? XFS_DATA_FORK : XFS_ATTR_FORK);
		if (!error && copyout(&bm, arg, sizeof(bm))) {
			error = XFS_ERROR(EFAULT);
		}
		break;
	    }

	case F_FSSETDM: {
		u_int	values [2];

		if (copyin (arg, values, sizeof values)) {
			error = XFS_ERROR (EFAULT);
			break;
		}
		error = xfs_set_dmattrs(vp, values [0], values [1]);
		break;
	    }

	case F_ALLOCSP:
	case F_FREESP:

	case F_RESVSP:
	case F_UNRESVSP:

	case F_ALLOCSP64:
	case F_FREESP64:

	case F_RESVSP64:
	case F_UNRESVSP64:

		cmd = cmd;
		if ((flags & FWRITE) == 0) {
			error = XFS_ERROR(EBADF);
		} else if (vp->v_type != VREG) {
			error = XFS_ERROR(EINVAL);
#ifdef _K64U64
		} else if (ABI_IS_IRIX5_64(u.u_procp->p_abi)) {
			if (copyin((caddr_t)arg, &bf, sizeof bf)) {
				error = XFS_ERROR(EFAULT);
				break;
			}
#endif
		} else if (cmd == F_ALLOCSP64 || cmd == F_FREESP64 ||
			   cmd == F_RESVSP64 ||
			   ABI_IS_IRIX5_N32(u.u_procp->p_abi)) {
			/* 
			 * The n32 flock structure is the same size as the
			 * o32 flock64 structure. So the copyin_xlate
			 * with irix5_n32_to_flock works here.
			 */
			if (COPYIN_XLATE((caddr_t)arg, &bf, sizeof bf,
					 irix5_n32_to_flock,
					 u.u_procp->p_abi, 1)) {
				error = XFS_ERROR(EFAULT);
				break;
			}
		} else {
			if (copyin((caddr_t)arg, &i5_bf, sizeof i5_bf)) {
				error = XFS_ERROR(EFAULT);
				break;
			}
			/* 
			 * Now expand to 64 bit sizes. 
			 */
			bf.l_type = i5_bf.l_type;
			bf.l_whence = i5_bf.l_whence;
			bf.l_start = i5_bf.l_start;
			bf.l_len = i5_bf.l_len;
		}

		if (!error) {
			error = xfs_change_file_space(vp, cmd, &bf, offset,
						      u.u_cred );
		}
		break;

	default:
		error = XFS_ERROR(EINVAL);
		break;
	}
	return error;
}

int
xfs_set_dmattrs (
	vnode_t *vp,
	u_int	evmask,
	u_int	state)
{
        xfs_inode_t     *ip;
	xfs_trans_t	*tp;
	xfs_mount_t	*mp;
	int		error;

	if (!_CAP_ABLE (CAP_DEVICE_MGT))
		return XFS_ERROR(EPERM);

        ip = XFS_VTOI (vp);
	mp = ip->i_mount;
	tp = xfs_trans_alloc(mp, XFS_TRANS_SET_DMATTRS);
	error = xfs_trans_reserve(tp, 0, XFS_ICHANGE_LOG_RES (mp), 0, 0, 0);
	if (error) {
		xfs_trans_cancel(tp, 0);
		return error;
	}
        xfs_ilock(ip, XFS_ILOCK_EXCL);
        xfs_trans_ijoin(tp, ip, XFS_ILOCK_EXCL);

	ip->i_d.di_dmevmask = evmask;
	ip->i_d.di_dmstate  = state;

	xfs_trans_log_inode(tp, ip, XFS_ILOG_CORE);
	IHOLD(ip);
	error = xfs_trans_commit(tp, 0);

	return error;
}

#endif	/* !SIM */

/*
 * xfs_reclaim
 */
STATIC int
xfs_reclaim(
	vnode_t		*vp,
	int		flag)
{
	xfs_inode_t	*ip;
	xfs_fsize_t	last_byte;
	int		locked;
	int		error;

	vn_trace_entry(vp, "xfs_reclaim");
	ASSERT(!VN_MAPPED(vp));
	ip = XFS_VTOI(vp);
	ASSERT(ip->i_queued_bufs >= 0);
	locked = 0;

	/*
	 * If this is not an unmount (flag == 0) and the inode's data
	 * still needs to be flushed, then we do not allow
	 * the inode to be reclaimed.  This is to avoid many different
	 * deadlocks.
	 *
	 * Doing the pflushinvalvp() can cause
	 * us to wait in the buffer cache.  We can be called here via
	 * vn_alloc() from xfs_iget().  We can be holding any number of
	 * locks at that point in the middle of a transaction, so we
	 * can't do anything that might need log space or the locks we
	 * might be holding.  Flushing our buffers can require log space
	 * to allocate the space for delayed allocation extents underlying
	 * them.  If the transaction we're already in has all the log
	 * space, then we won't be able to get any more and we'll hang.
	 *
	 * Not allowing the inode to be reclaimed if it has dirty data
	 * also prevents memory deadlocks where it is vhand calling here
	 * via the vnode shake routine.  Since our dirty data might be
	 * delayed allocation dirty data which will require us to allocate
	 * memory to flush, we can't do this from vhand.
	 *
	 * It is OK to return an error here.  The vnode cache will just
	 * come back later.
	 *
	 * XXXajs Distinguish vhand from vn_alloc and fail vhand case
	 * if the inode is dirty.  This will prevent deadlocks where the
	 * process with the inode buffer locked needs memory.  We can't
	 * always fail when the inode is dirty because then we don't
	 * reclaim enough.  The vnode cache then grows far too large.
	 */
	if (!(flag & FSYNC_INVAL)) {
		if (VN_DIRTY(vp) || (ip->i_queued_bufs > 0)) {
			return XFS_ERROR(EAGAIN);
		}
		if (!xfs_ilock_nowait(ip, XFS_ILOCK_EXCL)) {
			return XFS_ERROR(EAGAIN);
		}
		if (!xfs_iflock_nowait(ip)) {
			xfs_iunlock(ip, XFS_ILOCK_EXCL);
			return XFS_ERROR(EAGAIN);
		}
		if ((ip->i_item.ili_format.ilf_fields != 0) ||
		    (ip->i_item.ili_last_fields != 0)) {
			(void) xfs_iflush(ip, XFS_IFLUSH_DELWRI);
			xfs_iunlock(ip, XFS_ILOCK_EXCL);
			return XFS_ERROR(EAGAIN);
		}
		locked = 1;
	}
	if (((ip->i_d.di_mode & IFMT) == IFREG) &&
	    (ip->i_d.di_size > 0)) {
		/*
		 * Flush and invalidate any data left around that is
		 * a part of this file.
		 *
		 * Get the inode's i/o lock so that buffers are pushed
		 * out while holding the proper lock.  We can't hold
		 * the inode lock here since flushing out buffers may
		 * cause us to try to get the lock in xfs_strategy().
		 *
		 * We don't have to call remapf() here, because there
		 * cannot be any mapped file references to this vnode
		 * since it is being reclaimed.
		 */
		if (locked) {
			xfs_ifunlock(ip);
			xfs_iunlock(ip, XFS_ILOCK_EXCL);
			locked = 0;
		}
	 	xfs_ilock(ip, XFS_IOLOCK_EXCL);
		last_byte = xfs_file_last_byte(ip);
		pflushinvalvp(vp, 0, last_byte);			     
		ASSERT(!VN_DIRTY(vp) &&
		       (ip->i_delayed_blks == 0) &&
		       (ip->i_queued_bufs == 0) &&
		       (vp->v_pgcnt == 0) &&
		       (vp->v_buf == NULL));
		xfs_iunlock(ip, XFS_IOLOCK_EXCL);
	}

#ifndef SIM
	dnlc_purge_vp(vp);
#endif	/* !SIM */
	/*
	 * If the inode is still dirty, then flush it out.  If the inode
	 * is not in the AIL, then it will be OK to flush it delwri as
	 * long as xfs_iflush() does not keep any references to the inode.
	 * We leave that decision up to xfs_iflush() since it has the
	 * knowledge of whether it's OK to simply do a delwri flush of
	 * the inode or whether we need to wait until the inode is
	 * pulled from the AIL.
	 * We get the flush lock regardless, though, just to make sure
	 * we don't free it while it is being flushed.
	 */
	if (!locked) {
		xfs_ilock(ip, XFS_ILOCK_EXCL);
		xfs_iflock(ip);
	}
	if (ip->i_update_core || (ip->i_item.ili_format.ilf_fields != 0)) {
		error = xfs_iflush(ip, XFS_IFLUSH_DELWRI_ELSE_SYNC);
		if (error) {
			xfs_iunlock(ip, XFS_ILOCK_EXCL);
			return error;
		}
	}
	xfs_iunlock(ip, XFS_ILOCK_EXCL);
	ASSERT(ip->i_update_core == 0);
	ASSERT(ip->i_item.ili_format.ilf_fields == 0);
	ASSERT(!VN_DIRTY(vp) && (ip->i_queued_bufs == 0));
	xfs_ireclaim(ip);
	return 0;
}

#ifndef SIM

/*
 * xfs_free_file_space()
 *      This routine frees disk space for the given file.
 *
 * RETURNS:
 *       0 on success
 *      errno on error
 *
 */
STATIC int
xfs_free_file_space( 
	vnode_t 	*vp,
	off_t 		offset,
	off_t		len)
{

        int             error;
	xfs_inode_t	*ip;
        xfs_trans_t     *tp;
	xfs_mount_t	*mp;

	vn_trace_entry(vp, "xfs_free_file_space");

	/*
	 * Currently, can only free to eof
	 */
	if (len) {
		return( XFS_ERROR(EINVAL) );
	}

	ip = XFS_VTOI(vp);
	mp = XFS_VFSTOM(vp->v_vfsp);

        /*
         * Make the call to xfs_itruncate_start before starting the
         * transaction, because we cannot make the call while we're
         * in a transaction.
         */
        xfs_ilock(ip, XFS_IOLOCK_EXCL);
        xfs_itruncate_start(ip, XFS_ITRUNC_DEFINITE, (xfs_fsize_t)offset);

        tp = xfs_trans_alloc(mp, XFS_TRANS_TRUNCATE_FILE);
        if (error = xfs_trans_reserve(tp, 0, XFS_ITRUNCATE_LOG_RES(mp), 0,
                                      XFS_TRANS_PERM_LOG_RES,
                                      XFS_ITRUNCATE_LOG_COUNT)) {
                xfs_trans_cancel(tp, 0);
		xfs_iunlock(ip, XFS_IOLOCK_EXCL);
                return error;
        }

        /*
         * Follow the normal truncate locking protocol.  Since we
         * hold the inode in the transaction, we know that it's number
         * of references will stay constant.
         */
        xfs_ilock(ip, XFS_ILOCK_EXCL);
        xfs_trans_ijoin(tp, ip, XFS_ILOCK_EXCL | XFS_IOLOCK_EXCL);
        xfs_trans_ihold(tp, ip);
	ip->i_d.di_flags &= ~XFS_DIFLAG_PREALLOC;
	xfs_trans_log_inode(tp, ip, XFS_ILOG_CORE);
        error = xfs_itruncate_finish(&tp, ip, (xfs_fsize_t)offset,
				     XFS_DATA_FORK, 0);
	if (error) {
		xfs_trans_cancel(tp, (XFS_TRANS_RELEASE_LOG_RES |
				 XFS_TRANS_ABORT));
	} else {
		xfs_ichgtime(ip, XFS_ICHGTIME_MOD | XFS_ICHGTIME_CHG);
		error = xfs_trans_commit(tp, XFS_TRANS_RELEASE_LOG_RES);
	}
        xfs_iunlock(ip, XFS_ILOCK_EXCL | XFS_IOLOCK_EXCL);

        return error;
}

/*
 * xfs_alloc_file_space()
 *      This routine allocates disk space for the given file.
 *
 * RETURNS:
 *       0 on success
 *      errno on error
 *
 */
STATIC int
xfs_alloc_file_space( 
	vnode_t 	*vp,
	off_t 		offset,
	off_t		len)
{
	int			error, reccount, rt; 
	int			rtextsize, numrtextents;
	int			prealloc_set;
	int			committed;
	off_t			count;
	xfs_trans_t		*tp;
	xfs_inode_t		*ip;
	xfs_mount_t		*mp;
	xfs_fileoff_t		startoffset_fsb;
	xfs_filblks_t		allocatesize_fsb, allocated_fsb, datablocks;
	xfs_fsblock_t		firstfsb;
	xfs_bmbt_irec_t		imaps[1], *imapp;
	xfs_bmap_free_t		free_list;

	vn_trace_entry(vp, "xfs_alloc_file_space");
	
	ip = XFS_VTOI(vp);
	mp = XFS_VFSTOM(vp->v_vfsp);

	/*
	 * determine if this is a realtime file
	 */
        if (ip->i_d.di_flags & XFS_DIFLAG_REALTIME)  {
                rt = 1;
                rtextsize = mp->m_sb.sb_rextsize;
        } else {
                rt = 0;
                rtextsize = 0;
        }

	count  			= len;
	error			= 0;
	imapp 			= &imaps[0];
	reccount 		= 1;
	startoffset_fsb		= XFS_B_TO_FSBT( mp, offset );
	allocatesize_fsb	= XFS_B_TO_FSB( mp, count );
	prealloc_set		= 0;

	/*
	 * allocate file space until done or until there is an error	
 	 */
	while (allocatesize_fsb && (!error)) {
		/*
		 * determine if reserving space on
		 * the data or realtime partition.
		 */
		if (rt) {
			numrtextents = (allocatesize_fsb + rtextsize - 1) /
					rtextsize;
			datablocks = 0;
		} else {
			datablocks = allocatesize_fsb;
		}

		/*
		 * allocate and setup the transaction
		 */
		tp = xfs_trans_alloc(mp, XFS_TRANS_DIOSTRAT);
		error = xfs_trans_reserve(tp,
					  XFS_BM_MAXLEVELS(mp, XFS_DATA_FORK) + datablocks,
					  XFS_WRITE_LOG_RES(mp),
					  numrtextents,
					  XFS_TRANS_PERM_LOG_RES,
					  XFS_WRITE_LOG_COUNT);

		/*
		 * check for running out of space
		 */
		if (error) {
			/*
			 * Free the transaction structure.
			 */
			ASSERT(error == ENOSPC);
			xfs_trans_cancel(tp, 0);
			break;
		} else {
			xfs_ilock(ip, XFS_ILOCK_EXCL);
			xfs_trans_ijoin(tp, ip, XFS_ILOCK_EXCL);
			xfs_trans_ihold(tp, ip);
			if (!prealloc_set) {
				ip->i_d.di_flags |= XFS_DIFLAG_PREALLOC;
				xfs_trans_log_inode(tp, ip, XFS_ILOG_CORE);
			}
		}

		/*
		 * issue the bmapi() call to allocate the blocks
	 	 */
		XFS_BMAP_INIT(&free_list, &firstfsb);
		error = xfs_bmapi(tp, ip, startoffset_fsb, 
				  allocatesize_fsb, 1, &firstfsb, 0, imapp, 
				  &reccount, &free_list);
		if (error) {
			goto error0;
		}

		/*
		 * complete the transaction
		 */
		error = xfs_bmap_finish(&tp, &free_list, firstfsb,
					&committed);
		if (error) {
			goto error0;
		}

		error = xfs_trans_commit(tp, XFS_TRANS_RELEASE_LOG_RES );
		xfs_iunlock(ip, XFS_ILOCK_EXCL);
		if (error) {
			break;
		}

		allocated_fsb = imapp->br_blockcount;

		if (reccount == 0) {
			error = XFS_ERROR(ENOSPC);
			break;
		}

		startoffset_fsb 	+= allocated_fsb;
		allocatesize_fsb  	-= allocated_fsb;
	}

	return error;

 error0:
	xfs_bmap_cancel(&free_list);
	xfs_trans_cancel(tp, XFS_TRANS_RELEASE_LOG_RES | XFS_TRANS_ABORT);
	xfs_iunlock(ip, XFS_ILOCK_EXCL);
	return error;
}


/*
 * xfs_change_file_space()
 *      This routine allocates or frees disk space for the given file.
 *      The user specified parameters are checked for alignment and size
 *      limitations.
 *
 * RETURNS:
 *       0 on success
 *      errno on error
 *
 */
STATIC int
xfs_change_file_space( 
	vnode_t 	*vp,
	int		cmd,
	flock_t 	*bf,
	off_t 		offset,
	cred_t  	*credp)
{
	int			error;
	int			allocspace, resvspace = 0;
	off_t			startoffset, len;
	vattr_t			va;
	xfs_inode_t		*ip;

	vn_trace_entry(vp, "xfs_change_file_space");
	
	len 	= bf->l_len;
	ip 	= XFS_VTOI(vp);

	/*
	 * must be a regular file and have write permission
	 */
	if (vp->v_type != VREG) {
		return(XFS_ERROR(EINVAL));
	}

	xfs_ilock(ip, XFS_ILOCK_SHARED);
	if (error = xfs_iaccess(ip, IWRITE, credp)) {
		xfs_iunlock(ip, XFS_ILOCK_SHARED);
		return error;
	}
	xfs_iunlock(ip, XFS_ILOCK_SHARED);

	switch ( cmd ) {
		case F_RESVSP:
		case F_RESVSP64:
			resvspace = 1;
		case F_ALLOCSP:
		case F_ALLOCSP64:
			allocspace = 1;
			break;

		case F_UNRESVSP:
		case F_UNRESVSP64:
			resvspace = 1;
		case F_FREESP:
		case F_FREESP64:
			allocspace = 0;
			break;
		default:
			ASSERT(0);
			return( XFS_ERROR(EINVAL) );
	}


	/*
	 * determine how much to allocate/free starting from where
	 */
	if (bf->l_whence == 1) {
		/*
		 * relative to current offset
		 */
		startoffset = bf->l_start + offset;
	} else if (bf->l_whence == 2) {
		/*
		 * relative to end of file
		 */
		startoffset = bf->l_start + ip->i_d.di_size;
	} else if (bf->l_whence == 0) {
		/*
		 * relative to start of file. 
		 */
		startoffset = bf->l_start;
	} else {
		return XFS_ERROR(EINVAL);
	}

	/*
	 * F_RESVSP and F_UNRESVSP will reserve or unreserve file space.
	 * these calls do NOT zero the data space allocated
	 * to the file, nor do they change the file size.
 	 *
	 * F_ALLOCSP and F_FREESP will allocate and free file space.
	 * these calls cause the new file data to be zeroed and the file
	 * size to be changed.
	 */
	if ( allocspace ) {
		error = xfs_alloc_file_space( vp, startoffset, len);
		if ((!error) && (!resvspace)) {
			/*
			 * zero pages, and change file size
			 */
			va.va_mask = AT_SIZE;
			va.va_size = startoffset + len;
			error = xfs_setattr( vp, &va, 0, credp );
		}
	} else {
		/*
		 * len is currently assumed to be zero,
		 * which means to free to end-of-file
		 */
		if (startoffset > ip->i_d.di_size) {
			/*
			 * freeing after end-of-file means to extend the file.
			 */
			error = xfs_alloc_file_space( vp, 
					ip->i_d.di_size,
					(startoffset - ip->i_d.di_size));

			if ((!error) && (!resvspace)) {
				/*
				 * zero pages and change file size
				 */
				va.va_mask = AT_SIZE;
				va.va_size = startoffset;
				error = xfs_setattr( vp, &va, 0, credp );
			}

		} else {
			/*
			 * free from startoffset to end-of-file
			 */
			error = xfs_free_file_space( vp, startoffset, 0);
		}
	}


	/*
	 *  update the inode timestamp 
	 */
	if (!error) {
		xfs_ilock(ip, XFS_ILOCK_EXCL);

		if ((ip->i_d.di_mode & (ISUID|ISGID)) && credp->cr_uid != 0) {
			ip->i_d.di_mode &= ~ISUID;
			if (ip->i_d.di_mode & (IEXEC >> 3))
				ip->i_d.di_mode &= ~ISGID;
		}
		xfs_ichgtime(ip, XFS_ICHGTIME_MOD | XFS_ICHGTIME_CHG);

		xfs_iunlock(ip, XFS_ILOCK_EXCL);
	}

	return error;
}

/*
 * print out error describing the problem with the fs
 *
 *
 * type 1 indicates the file system ran out of data space
 * type 2 indicates the file system ran out of realtime space
 *
 */
void
xfs_error(
	xfs_mount_t	*mp,
	int		type)
{
	dev_t	dev;
	
	switch (type) {
	case 1:
		dev = mp->m_dev;
		prdev("Process [%s] ran out of disk space",
		      dev, u.u_comm);
		break;
	case 2:
		dev = mp->m_rtdev;
		prdev("Process [%s] ran out of disk space",
		      dev, u.u_comm);
		break;
	}
}
#endif	/* !SIM */

#ifdef SIM

struct vnodeops xfs_vnodeops = {
	fs_noerr,
	fs_nosys,
	fs_nosys,
	fs_nosys,
	fs_nosys,
	fs_noerr,
	fs_nosys,
	fs_nosys,
	fs_nosys,
	fs_nosys,
	fs_nosys,
	fs_nosys,
	fs_nosys,
	fs_nosys,
	fs_nosys,
	fs_nosys,
	fs_nosys,
	fs_nosys,
	fs_nosys,
	fs_nosys,
	xfs_inactive,
	fs_nosys,
	(void (*)(vnode_t *, vrwlock_t))fs_nosys,
	(void (*)(vnode_t *, vrwlock_t))fs_nosys,
	fs_nosys,
	fs_nosys,
	fs_nosys,
	fs_nosys,	/* realvp */
	fs_nosys,
	(void (*)(vnode_t *, buf_t *))fs_nosys,
	fs_nosys,
	fs_nosys,
	fs_nosys,
	(int (*)(vnode_t *, short, int, short *, struct pollhead **))fs_nosys,
	fs_nosys,	/* dump */
	fs_nosys,
	fs_nosys,
	fs_nosys,
	xfs_reclaim,
	fs_nosys,	/* attr_get */
	fs_nosys,	/* attr_set */
	fs_nosys,	/* attr_remove */
	fs_nosys,	/* attr_list */
};

#else

struct vnodeops xfs_vnodeops = {
	xfs_open,
	xfs_close,
	xfs_read,
	xfs_write,
	fs_nosys,	/* ioctl */
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
	xfs_attr_get,	/* attr_get */
	xfs_attr_set,	/* attr_set */
	xfs_attr_remove,/* attr_remove */
	xfs_attr_list,	/* attr_list */
};

#endif	/* SIM */


