#include <sys/types.h>
#ifdef SIM
#define _KERNEL 1
#endif
#include <sys/buf.h>
#include <sys/uio.h>
#include <sys/user.h>
#include <sys/vfs.h>
#include <sys/vnode.h>
#include <fs/specfs/snode.h>
#include <sys/systm.h>
#include <sys/dnlc.h>
#include <sys/sysmacros.h>
#include <sys/cred.h>
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
#include <sys/kmem.h>
#include <sys/conf.h>
#endif
#include <sys/mount.h>
#include <sys/param.h>
#include <sys/pathname.h>
#include <sys/pfdat.h>
#include <sys/proc.h>
#include <sys/sema.h>
#include <sys/statvfs.h>
#include <sys/uuid.h>
#include <sys/region.h>
#include <fs/specfs/snode.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <sys/mode.h>
#include <sys/sysinfo.h>
#include <sys/ksa.h>
#include <sys/grio.h>
#include <sys/var.h>
#include <string.h>
#include "xfs_types.h"
#include "xfs_inum.h"
#include "xfs_log.h"
#include "xfs_trans.h"
#include "xfs_sb.h"
#include "xfs_mount.h"
#include "xfs_alloc_btree.h"
#include "xfs_ialloc.h"
#include "xfs_ag.h"
#include "xfs_bio.h"
#include "xfs_bmap_btree.h"
#include "xfs_bmap.h"
#include "xfs_btree.h"
#include "xfs_dinode.h"
#include "xfs_inode_item.h"
#include "xfs_inode.h"
#include "xfs_dir.h"
#include "xfs_rw.h"

#ifdef SIM
#include "sim.h"
#endif


STATIC int	xfs_open(vnode_t	**vpp,
			 mode_t		mode,
			 cred_t		*credp);

STATIC int	xfs_close(vnode_t	*vp,
			  int		flag,
			  lastclose_t	lastclose,
			  off_t		offset,
			  cred_t	*credp);

STATIC int	xfs_ioctl(vnode_t	*vp,
			  int		cmd,
			  void		*argp,
			  int		flag,
			  cred_t	*credp,
			  int		*rvalp);

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

STATIC void	xfs_rwlock(vnode_t	*vp);

STATIC void	xfs_rwunlock(vnode_t	*vp);

STATIC int	xfs_seek(vnode_t	*vp,
			 off_t		old_offset,
			 off_t		*new_offsetp);

STATIC int	xfs_cmp(vnode_t	*vp1,
			vnode_t	*vp2);

STATIC int	xfs_frlock(vnode_t	*vp,
			   int		cmd,
			   flock_t	*flockp,
			   int		flag,
			   off_t	offset,
			   cred_t	*credp);

STATIC int	xfs_realvp(vnode_t	*vp,
			   vnode_t	**vpp);

STATIC int	xfs_map(vnode_t	*vp,
			uint	offset,
			preg_t	*pregp,
			addr_t	*addrp,
			uint	len,
			uint	prot,
			uint	max_prot,
			uint	flags,
			cred_t	*credp);

STATIC int	xfs_addmap(vnode_t	*vp,
			   uint		offset,
			   preg_t	*pregp,
			   addr_t	addr,
			   uint		len,
			   uint		prot,
			   uint		max_prot,
			   uint		flags,
			   cred_t	*credp);

STATIC int	xfs_delmap(vnode_t	*vp,
			   uint		offset,
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

STATIC int	xfs_setfl(vnode_t	*vp,
			  int		old_flags,
			  int		new_flags,
			  cred_t	*credp);

STATIC int	xfs_fcntl(vnode_t	*vp,
			  int		cmd,
			  void		*arg,
			  int		flags,
			  off_t		offset,
			  cred_t	*credp,
			  rval_t	*rvalp);


extern struct igetstats XFS_IGETINFO;

#define IRELE(ip)	VN_RELE(XFS_ITOV(ip))
#define IHOLD(ip)	VN_HOLD(XFS_ITOV(ip))


/*
 * xfs_open
 *
 * This is a stub.
 */
STATIC int
xfs_open(vnode_t	**vpp,
	 mode_t		mode,
	 cred_t		*credp)
{
	return 0;
}


/*
 * xfs_close
 *
 * This is a stub.
 */
STATIC int
xfs_close(vnode_t	*vp,
	  int		flag,
	  lastclose_t	lastclose,
	  off_t		offset,
	  cred_t	*credp)
{
	return 0;
}


/*
 * xfs_ioctl
 *
 * This is a stub.
 */
STATIC int
xfs_ioctl(vnode_t	*vp,
	  int		cmd,
	  void		*argp,
	  int		flag,
	  cred_t	*credp,
	  int		*rvalp)
{
	return 0;
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

	ip = XFS_VTOI(vp);
	xfs_ilock (ip, XFS_ILOCK_SHARED);

	vap->va_size = (ulong) ip->i_d.di_size;
        if (vap->va_mask == AT_SIZE)
                return 0;
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

        switch (ip->i_d.di_mode & IFMT) {
          case IFBLK:
          case IFCHR:
                vap->va_blksize = BLKDEV_IOSIZE;
                break;
          default:
		mp = XFS_VFSTOM(vp->v_vfsp);
		vap->va_blksize = mp->m_sb.sb_blocksize;	/* in bytes */
		break;
        }

	/*
	 * XXX : truncate to 32 bites for now.
	 */
        vap->va_nblocks = BTOBB((u_long) (ip->i_d.di_size));

	/*
	 * XFS-added attributes
	 */
	if (flags & (AT_XFLAGS|AT_EXTSIZE|AT_NEXTENTS|AT_UUID)) {
		vap->va_xflags = ip->i_d.di_flags;
		vap->va_extsize = ip->i_d.di_extsize;
		vap->va_nextents = ip->i_d.di_nextents;
		vap->va_uuid = ip->i_d.di_uuid;
	}

	xfs_iunlock (ip, XFS_ILOCK_EXCL);

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
	int		mask;
	int		code;
	timestruc_t 	tv;

	/*
	 * Cannot set certain attributes.
         */
        mask = vap->va_mask;
        if (mask & AT_NOSET)
                return EINVAL;

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
        }


	/*
	 * For the other attributes, we acquire the inode lock and
	 * first do an error checking pass.
	 */
        tp = xfs_trans_alloc (XFS_VFSTOM((XFS_ITOV(ip))->v_vfsp),
                        XFS_TRANS_WAIT);
        if (code = xfs_trans_reserve (tp, 10, 10, 0, 0)) {
                xfs_trans_cancel (tp, 0);
                return code;
        }

        xfs_ilock (ip, XFS_ILOCK_EXCL);
        xfs_trans_ijoin (tp, ip, XFS_ILOCK_EXCL);


        /*
         * Change file access modes.  Must be owner or privileged.
	 * Also check here for xflags, extsize.
         */
        if (mask & (AT_MODE|AT_XFLAGS|AT_EXTSIZE)) {
                if (credp->cr_uid != ip->i_d.di_uid && !crsuser(credp)) {
                        code = EPERM;
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
                uid_t uid = (mask & AT_UID) ? vap->va_uid : ip->i_d.di_uid;
                gid_t gid = (mask & AT_GID) ? vap->va_gid : ip->i_d.di_gid;

                if (!crsuser(credp)) {
                        if (credp->cr_uid != ip->i_d.di_uid ||
                                (restricted_chown &&
                                (ip->i_d.di_uid != uid ||
                                 !groupmember(gid, credp)))) {
                                code = EPERM;
                                goto error_return;
                        }
                }
        }

        /*
         * Truncate file.  Must have write permission and not be a directory.
         */
        if (mask & AT_SIZE) {
                if (vp->v_type == VDIR) {
                        code = EISDIR;
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
                                code = EPERM;
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
			code = EINVAL;	/* EFBIG? */
			goto error_return;
		}
		/*
		 * Can't change realtime flag if any extents are allocated.
		 */
		if (ip->i_d.di_nextents && (mask & AT_XFLAGS) &&
		    (ip->i_d.di_flags & XFS_DIFLAG_REALTIME) !=
		    (vap->va_xflags & XFS_DIFLAG_REALTIME)) {
			code = EINVAL;	/* EFBIG? */
			goto error_return;
		}
	}

	/*
	 * Now we can make the changes.
	 */


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
                uid_t uid = (mask & AT_UID) ? vap->va_uid : ip->i_d.di_uid;
                gid_t gid = (mask & AT_GID) ? vap->va_gid : ip->i_d.di_gid;

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
         * Truncate file.  Must have write permission and not be a directory.
         */
        if (mask & AT_SIZE) {

		/* Call xfs_itrunc to vap->va_size */
		ASSERT (0);
 
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
		if (mask & AT_EXTSIZE)
			ip->i_d.di_extsize = vap->va_extsize;
		if (mask & AT_XFLAGS)
			ip->i_d.di_flags = vap->va_xflags & XFS_DIFLAG_ALL;
		xfs_trans_log_inode(tp, ip, XFS_ILOG_CORE);
	}

	XFS_IGETINFO.ig_attrchg++;

	IHOLD (ip);
	xfs_trans_commit (tp, 0);

	return 0;




error_return:

	/*
	 * Retain our vnode reference.
	 */
	IHOLD (ip);
	xfs_trans_cancel (tp, 0);

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
        xfs_mount_t     *mp;
        xfs_inode_t     *ip;
	int		count;
	off_t		offset;
	int		pathlen;
	timestruc_t     tv;
        int             error = 0;

	if (vp->v_type != VLNK)
                return EINVAL;

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
                error = EINVAL;
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
		xfs_trans_t	*tp = NULL;
		xfs_mount_t	*mp;
                xfs_fsblock_t   first_fsb;
                xfs_extlen_t    fs_blocks;
                int             nmaps;
                xfs_bmbt_irec_t mval [SYMLINK_MAPS];
                daddr_t         d;
                char            *cur_chunk;
                int             byte_cnt, n;
                struct          buf *bp;
		xfs_sb_t        *sbp;
		xfs_bmap_free_t free_list;

		mp = XFS_VFSTOM(vp->v_vfsp);
                first_fsb = 0;
                fs_blocks = xfs_b_to_fsb(sbp, pathlen);
                nmaps = SYMLINK_MAPS;

                (void) xfs_bmapi (tp, ip, first_fsb, fs_blocks,
                         0, NULLFSBLOCK, 0, mval, &nmaps, &free_list);

                for (n = 0; n < nmaps; n++) {
                        d = xfs_fsb_to_daddr(sbp, mval[n].br_startblock);
                        byte_cnt = xfs_fsb_to_b(sbp, mval[n].br_blockcount);
                        bp = xfs_read_buf (mp->m_dev, d, BTOBB(byte_cnt), 0);
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
 * This is a stub.
 */
STATIC int
xfs_fsync(vnode_t	*vp,
	  int		flag,
	  cred_t	*credp)
{
	return 0;
}


/*
 * xfs_inactive
 *
 * This is a stub.
 */
STATIC void
xfs_inactive(vnode_t	*vp,
	     cred_t	*credp)
{
	return;
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

	do_iget = flag & DLF_IGET;

	/*
	 * Handle degenerate pathname component.
	 */
	if (*name == '\0') {
		vn_hold(dir_vp);
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
		if (do_iget) 
			*ip = XFS_VTOI(vp);
		else
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
	struct xfs_mount	*mp;
	int			code = 0;

	dp = XFS_VTOI(dir_vp);
	xfs_ilock(dp, XFS_ILOCK_SHARED);

	/*
	 * XXX Check access.
	 */

	code = xfs_dir_lookup_int (NULL, dir_vp, DLF_IGET, name, pnp, 
				   &e_inum, &ip);
	if (code) {
		xfs_iunlock(dp, XFS_ILOCK_SHARED);
		return code;
	}

#if 0
	/*
	 * If vnode is a device return special vnode instead.
         */
        if (ISVDEV(vp->v_type)) {
                newvp = specvp(vp, vp->v_rdev, vp->v_type, credp);
                VN_RELE(vp);
                if (newvp == NULL)
                        return ENOSYS;
                *vpp = newvp;
        }
#endif

	xfs_iunlock(dp, XFS_ILOCK_SHARED);
	*vpp = XFS_ITOV(ip);

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
	xfs_inode_t	**ipp)		/* pointer to inode; it will be
					   locked. */

{
	xfs_trans_t	*tp;
	xfs_inode_t	*ip;
	struct buf	*ialloc_context = NULL;
	boolean_t	call_again = B_FALSE;
	int		code;

	tp = *tpp;

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
		xfs_trans_commit (tp, 0);

		tp = xfs_trans_alloc (XFS_VFSTOM((XFS_ITOV(dp))->v_vfsp), 
			XFS_TRANS_WAIT);
		if (code = xfs_trans_reserve (tp, 10, 10, 0, 0)) {
			xfs_trans_cancel (tp, 0);
			*tpp = NULL;
			brelse (ialloc_context);
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
                 * We've got the last reference to this inode.
                 * XXX Log intent_to_trunc record (stick pointer to
                 * the xfs_log_item_t in the inode.)
                 */

                /*
                 * At some point, the vnode will become inactive
                 * and our VOP_INACTIVE routine will be called.
                 * It will perform the incore operations to
                 * truncate the file & set xfs_trans_callback
                 * so that we get called when trunc gets written
                 * to disk. At that point, we will remove the
                 * intent_to_trunc log item from the ail.
                 */
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
        struct vnode            *vp, *newvp;
	xfs_trans_t      	*tp = NULL;
        xfs_ino_t               e_inum;
        struct xfs_mount        *mp;
	dev_t			rdev;
	unsigned long   	dir_generation;
        int                     error;
        xfs_bmap_free_t 	free_list;
        xfs_fsblock_t   	first_block;


        dp = XFS_VTOI(dir_vp);

	xfs_ilock (dp, XFS_ILOCK_EXCL);

try_again:

	error = xfs_dir_lookup_int (NULL, dir_vp, DLF_IGET, name, NULL, 
		&e_inum, &ip);
	if ((error != 0) && (error != ENOENT)) {
		ip = NULL;
		goto error_return;
	}

	first_block = NULLFSBLOCK;
	bzero (&free_list, sizeof(free_list));

	if (error == ENOENT) {

		/* check access */

		mp = XFS_VFSTOM(dir_vp->v_vfsp);
		tp = xfs_trans_alloc (mp, XFS_TRANS_WAIT);
		if (error = xfs_trans_reserve (tp, XFS_IALLOC_MAX_EVER_BLOCKS,
						10, 0, 0))
			goto error_return;

		rdev = (vap->va_mask & AT_RDEV) ? vap->va_rdev : NODEV;

		error = xfs_dir_ialloc(&tp, dp, 
			MAKEIMODE(vap->va_type,vap->va_mode), 
			1, rdev, credp, &ip);
		if (error)
			goto error_return;

		/*
		 * At this point, we've gotten a newly allocated inode.
		 * It is locked.
		 */

		ASSERT (ismrlocked (&ip->i_lock, MR_UPDATE));

		xfs_trans_ijoin (tp, dp, XFS_ILOCK_EXCL);

		/*
		 * XXX Need to sanity check namelen.
		 */

		if (error = xfs_dir_createname (tp, dp, name, ip->i_ino,
						&first_block, &free_list,
						MAX_EXT_NEEDED)) {
			xfs_droplink (tp, ip);
			goto error_return;
		}

		dnlc_enter(dir_vp, name, XFS_ITOV(ip), NOCRED);

	}
	else {
		ASSERT (ip != NULL);

		vp = XFS_ITOV(ip);
		if (excl == EXCL)
                        error = EEXIST;
		else if (vp->v_type == VDIR && (I_mode & IWRITE))
                        error = EISDIR;
		else if (I_mode) {
			/* XXX Do access checks */
			error = 0;
		}
		if (!error && vp->v_type == VREG && (vap->va_mask & AT_SIZE)) {

			/*
			 * Truncation case, not implemented yet.
			 */
			ASSERT (0);

			/* Truncate case, not implemented yet.
				GET the inode, droplink. */
			ASSERT (0);

			ASSERT (ip != NULL);

			/* Now lock the inode also, in the right order. */
			if (dp->i_ino < ip->i_ino) {
				xfs_ilock (ip, XFS_ILOCK_EXCL);
				IRELE (ip);	/* ref from lookup */
			}
			else {
				dir_generation = dp->i_gen;
				xfs_iunlock (dp, XFS_ILOCK_EXCL);
				xfs_ilock (ip, XFS_ILOCK_EXCL);
				xfs_ilock (dp, XFS_ILOCK_EXCL);
				IRELE (ip);	/* ref from lookup */

				/*
				 * If things have changed while the dp was
				 * unlocked, drop all except the lock on the
				 * directory inode & try again.
				 */
				if (dp->i_gen != dir_generation) {
					xfs_iunlock (ip, XFS_ILOCK_EXCL);
					goto try_again;
				}
			}
		}

	}


	/*
	 * xfs_trans_commit normally decrements the vnode ref count
	 * when it unlocks the inode. In this case, we want to just
	 * unlock the inode but retain the vnode reference. So we
	 * hold the inode and then explicitly unlock only the inode 
	 * after the commit.
	 */
	xfs_trans_ihold (tp, dp);
	xfs_trans_ihold (tp, ip);

	xfs_bmap_finish (&tp, &free_list, first_block);
	xfs_trans_commit (tp, 0);

	xfs_iunlock (dp, XFS_ILOCK_EXCL);
	xfs_iunlock (ip, XFS_ILOCK_EXCL);

	
	vp = XFS_ITOV (ip);

#if 0
        /*
         * If vnode is a device, return special vnode instead.
         */
        if (ISVDEV(vp->v_type)) {
                newvp = specvp(vp, vp->v_rdev, vp->v_type, credp);
                VN_RELE(vp);
                if (newvp == NULL)
                        return ENOSYS;
                vp = newvp;
        }
#endif

#if 0
	if (truncated && vp->v_type == VREG && VN_MAPPED(vp))
                remapf(vp, newsize, 0);
#endif

        *vpp = vp;


	return 0;

error_return:

	/*
	 * Note: this code assumes that we don't join ip & dp
	 * to the transaction until we are certain we can commit.
	 *
	 * Otherwise, will have to do iunlock only in the cases
	 * where tp == NULL.
	 */
	if (tp) {
		xfs_trans_cancel (tp, 0);
		tp = NULL;
	}
	xfs_iunlock (dp, XFS_ILOCK_EXCL);
	if (ip != NULL) {
		IRELE (ip);
		if (ip != dp) 
			xfs_iunlock (ip, XFS_ILOCK_EXCL);
	}
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

	ip2 = NULL;

	/*
	 * Frst, find out the current inums of the entries so that we
	 * can determine the initial locking order.  We'll have to 
	 * sanity check stuff after all the locks have been acquired
	 * to see if we still have the right inodes, directories, etc.
	 */
        xfs_ilock (dp1, XFS_ILOCK_SHARED);
        error = xfs_dir_lookup_int(NULL, XFS_ITOV(dp1), DLF_IGET,
				   name1, NULL, &inum1, &ip1);

	/*
	 * Save the current generation so that we can detect if it's
	 * modified between when we drop the lock & reacquire it down
	 * below.  We only need to do this for the src directory since
	 * the target entry does not need to exist yet. 
	 */
	dir_gen1 = dp1->i_gen;
	xfs_iunlock (dp1, XFS_ILOCK_SHARED);
        if (error)
                return error;
	ASSERT (ip1);

        xfs_ilock (dp2, XFS_ILOCK_SHARED);
        error = xfs_dir_lookup_int(NULL, XFS_ITOV(dp2), DLF_IGET,
				   name2, NULL, &inum2, &ip2);
	dir_gen2 = dp2->i_gen;
        xfs_iunlock (dp2, XFS_ILOCK_SHARED);
	if (error == ENOENT) {		/* target does not need to exist. */
		inum2 = 0;
	}
        else if (error) {
		IRELE (ip1);
                return error;
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
	if (dp1->i_gen != dir_gen1) {
		/*
		 * This is an unimplemented case.
		 */
		ASSERT (0);
	}
        if (dp2->i_gen != dir_gen2) {
                /*
                 * This is an unimplemented case.
                 */
                ASSERT (0);
		/*
		 * Someone else may have linked in a new inode
		 * with the same name.  If so, we'll need to
		 * release our locks & go through the whole
		 * thing again.
		 */
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
        int                     error = 0;
	unsigned long		dir_generation;
        xfs_bmap_free_t         free_list;
        xfs_fsblock_t           first_block;

        dp = XFS_VTOI(dir_vp);
	ip = NULL;

	error = xfs_lock_dir_and_entry (dp, name, &ip);
	if (error) {
		return error;
	}

	/*
	 * At this point, we've gotten both the directory and the entry
	 * inodes locked.
	 */

	/* Check MAC access */

	if (XFS_ITOV(ip)->v_vfsmountedhere) {
                        error = EBUSY;
			goto error_return;
	}
	if ((ip->i_d.di_mode & IFMT) == IFDIR && !crsuser(credp)) {
                        error = EPERM;
			goto error_return;
	}

	/*
	 * Return error when removing . and ..
	 */
	if (name[0] == '.') {
		if (name[1] == '\0') {
			error = EINVAL;
			goto error_return;
		}
		else if (name[1] == '.' && name[2] == '\0') {
			error = EEXIST;
			goto error_return;
		}
	} 

	tp = xfs_trans_alloc (XFS_VFSTOM(dir_vp->v_vfsp), XFS_TRANS_WAIT);
	if (error = xfs_trans_reserve (tp, 10, 10, 0, 0)) {
		xfs_trans_cancel (tp, 0);
		goto error_return;
	}

	xfs_trans_ijoin (tp, dp, XFS_ILOCK_EXCL);
	xfs_trans_ijoin (tp, ip, XFS_ILOCK_EXCL);

	/*
	 * Entry must exist since we did a lookup in xfs_lock_dir_and_entry.
	 */
	first_block = NULLFSBLOCK;
	bzero (&free_list, sizeof(free_list));
	error = xfs_dir_removename (tp, dp, name, &first_block, &free_list, 0);
	ASSERT (error == 0);

	dnlc_remove (dir_vp, name);

	dp->i_gen++;
	xfs_trans_log_inode (tp, dp, XFS_ILOG_CORE);

	xfs_droplink (tp, ip);

	xfs_trans_ihold (tp, dp);
	xfs_bmap_finish (&tp, &free_list, first_block);
	xfs_trans_commit (tp, 0);

	xfs_iunlock (dp, XFS_ILOCK_EXCL);
	IRELE (ip);

	/*
	 * We won't drop any locks here since xfs_trans_commit will
	 * do so when the transaction commits.
	 */

	return 0;

error_return:
	if (ip)
		IRELE (ip);

	xfs_iunlock (dp, XFS_ILOCK_EXCL);               
	if (dp != ip)
		xfs_iunlock (ip, XFS_ILOCK_EXCL);               

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
	int			error;
        xfs_bmap_free_t         free_list;
        xfs_fsblock_t           first_block;


	/*
	 * Get the real vnode.
	 */
	if (VOP_REALVP(src_vp, &realvp) == 0)
                src_vp = realvp;
        if (src_vp->v_type == VDIR && !crsuser(credp))
                return EPERM;

	sip = XFS_VTOI(src_vp);
	tdp = XFS_VTOI(target_dir_vp);

	xfs_lock_2_inodes (sip, tdp);

	/* XXX perform access checks */

	error = xfs_dir_lookup_int(NULL, target_dir_vp, 0, target_name, 
				   NULL, &e_inum, NULL);
	if (error != ENOENT) {
		if (error == 0) 
			error = EEXIST;
		goto error_return;
	}

        tp = xfs_trans_alloc (XFS_VFSTOM(target_dir_vp->v_vfsp),
			      XFS_TRANS_WAIT);
        if (error = xfs_trans_reserve (tp, 10, 10, 0, 0))
                goto error_return;

	xfs_trans_ijoin (tp, sip, XFS_ILOCK_EXCL);
        xfs_trans_ijoin (tp, tdp, XFS_ILOCK_EXCL);

	first_block = NULLFSBLOCK;
	bzero (&free_list, sizeof(free_list));

	if (error = xfs_dir_createname (tp, tdp, target_name, sip->i_ino,
					&first_block, &free_list,
					MAX_EXT_NEEDED)) {
		xfs_trans_cancel (tp, 0);
		return error;
	}

	tdp->i_gen++;
	xfs_trans_log_inode (tp, tdp, XFS_ILOG_CORE);


	xfs_bumplink(tp, sip);

	xfs_trans_ihold (tp, sip);
	xfs_trans_ihold (tp, tdp);

	xfs_bmap_finish (&tp, &free_list, first_block);
	xfs_trans_commit (tp, 0);

	dnlc_enter (target_dir_vp, target_name, XFS_ITOV(sip), credp);

	xfs_iunlock (sip, XFS_ILOCK_EXCL);
	xfs_iunlock (tdp, XFS_ILOCK_EXCL);

	return 0;

error_return:
	xfs_iunlock (tdp, XFS_ILOCK_EXCL);
	xfs_iunlock (sip, XFS_ILOCK_EXCL);
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
	xfs_trans_t	*tp = NULL;
	xfs_inode_t	*src_dp, *target_dp, *src_ip, *target_ip;
	boolean_t	new_parent;		/* moving to a new dir */
	boolean_t	src_is_directory;	/* src_name is a directory */
	int		error;		
        xfs_bmap_free_t free_list;
        xfs_fsblock_t   first_block;


	src_dp = XFS_VTOI(src_dir_vp);
	target_dp = XFS_VTOI(target_dir_vp);

	/*
	 * Lock all the participating inodes. Depending upon whether
	 * the target_name exists in the target directory, and
	 * whether the target directory is the same as the source
	 * directory, we can lock from 2 to 4 inodes.
	 * xfs_lock_for_rename() will return ENOENT if src_name
	 * does not exist in the source directory.
	 */
	if (error = xfs_lock_for_rename(src_dp, target_dp, src_name,
				        target_name, &src_ip, &target_ip))
		return error;

	ASSERT (src_ip != NULL);

	ASSERT (! ((src_name[0] == '.') && (src_name[1] == '\0')));
	ASSERT (! ((target_name[0] == '.') && (target_name[1] == '\0')));

	/*
	 * XXX Permission checks:
	 *   1) remove permission on src dir.
	 *   2) unlink and enter permission on target dir.
	 *      (need unlink in case the target exists.)
	 */



	if ((src_ip == src_dp) || (target_ip == target_dp)) {
		error = EINVAL;
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
	new_parent = (src_dp != target_dp);

	if (src_is_directory) {

		/*
		 * Cannot rename ".."
		 */
		if ((src_name[0] == '.') && (src_name[1] == '.') &&
		    (src_name[3] == '\0')) {
			error = EINVAL;
			goto error_return;
		}
                if ((target_name[0] == '.') && (target_name[1] == '.') &&
                    (target_name[3] == '\0')) {
                        error = EINVAL;
                        goto error_return;
                }


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

		/* XXX Unimplemented code */
	}


	/*
	 * We should be in the same file system.
	 */
	ASSERT (XFS_VFSTOM(src_dir_vp->v_vfsp) ==
		XFS_VFSTOM(target_dir_vp->v_vfsp));

        tp = xfs_trans_alloc (XFS_VFSTOM(src_dir_vp->v_vfsp),
                              XFS_TRANS_WAIT);
        if (error = xfs_trans_reserve (tp, 10, 10, 0, 0))
                goto error_return;

	xfs_trans_ijoin (tp, src_dp, XFS_ILOCK_EXCL);
	if (new_parent)
		xfs_trans_ijoin (tp, target_dp, XFS_ILOCK_EXCL);
        xfs_trans_ijoin (tp, src_ip, XFS_ILOCK_EXCL);
	if (target_ip != NULL)
		xfs_trans_ijoin (tp, target_ip, XFS_ILOCK_EXCL);

	first_block = NULLFSBLOCK;
	bzero (&free_list, sizeof(free_list));

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
			goto trans_error_return;

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
				error = EEXIST;
                                goto trans_error_return;
                        }

			/*
			 * Make sure src is a directory.
			 */
			if (! src_is_directory) {
				error = EISDIR;
				goto trans_error_return;
			}

			if (ABI_IS_SVR4(u.u_procp->p_abi) &&
                            XFS_ITOV(target_ip)->v_vfsmountedhere) {
                                error = EBUSY;
                                goto trans_error_return;
                        }


		}
		else {
			if (src_is_directory) {
				error = ENOTDIR;
				goto trans_error_return;
			}
		}

		/*
		 * Link the source inode under the target inode.
		 * If the source inode is a directory and we are moving
		 * it across directories, its ".." entry will be 
		 * inconsistent until xfs_dir_init.
		 *
		 * In case there is already an entry with the same
		 * name at the destination directory, remove it first.
		 */
		error = xfs_dir_removename (tp, target_dp, target_name,
					    &first_block, &free_list,
					    MAX_EXT_NEEDED);
		ASSERT ((! error) || (error == ENOENT));

		error = xfs_dir_createname (tp, target_dp, target_name,
					    target_ip->i_ino, &first_block,
					    &free_list, MAX_EXT_NEEDED);
		ASSERT (! error);	

		dnlc_enter (src_dir_vp, target_name, XFS_ITOV(src_ip), credp);

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
		 * Add the "." and ".." entries.
		 */
		error = xfs_dir_init(tp, src_ip, target_dp);
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


	xfs_trans_ihold (tp, src_dp);
	if (new_parent)
		xfs_trans_ihold (tp, target_dp);

	xfs_bmap_finish (&tp, &free_list, first_block);

	/*
	 * trans_commit will unlock src_ip, target_ip & decrement
	 * the vnode references.
	 */
	xfs_trans_commit (tp, 0);

	xfs_iunlock (src_dp, XFS_ILOCK_EXCL);
	if (new_parent)
		xfs_iunlock (target_dp, XFS_ILOCK_EXCL);


	return 0;

error_return:

	if (tp) {
		/*
		 * We make sure to join the inodes to the transaction
	 	 * only after sure we can commit. This means that we
		 * always have control over how the inodes are unlocked
		 * and their ref counts adjusted.
		 */
		xfs_trans_cancel (tp, 0);
	}

	/* release locks */

	xfs_iunlock (src_dp, XFS_ILOCK_EXCL);
	if (src_dp != target_dp)
		xfs_iunlock (target_dp, XFS_ILOCK_EXCL);
	xfs_iunlock (src_ip, XFS_ILOCK_EXCL);
	if ((target_ip != NULL) && (target_ip != src_ip))
		xfs_iunlock (target_ip, XFS_ILOCK_EXCL);

	IRELE (src_ip);
	if (target_ip)
		IRELE (target_ip);

	return error;

trans_error_return:

	/*
	 * We come here after we've ijoined the inodes to the 
	 * transaction.
	 *
	 * We want to control how the inodes are unlocked and
	 * their reference counts decremented. So we explicitly
	 * do iholds on them across the cancel.
	 */
	IHOLD (src_dp);
	if (src_dp != target_dp)
		IHOLD (src_dp);
	xfs_trans_cancel (tp, 0);
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
        struct xfs_mount        *mp;
        int                     code;
        xfs_bmap_free_t         free_list;
        xfs_fsblock_t           first_block;


        dp = XFS_VTOI(dir_vp);

        xfs_ilock (dp, XFS_ILOCK_EXCL);

	/*
	 * Since dp was not locked between VOP_LOOKUP and VOP_MKDIR,
	 * the directory could have been removed.
	 */
        if (dp->i_d.di_nlink <= 0) {
		code = ENOENT;
                goto error_return;
	}

        code = xfs_dir_lookup_int(NULL, dir_vp, 0, dir_name, NULL,
				  &e_inum, NULL);
        if (code != ENOENT) {
		if (code == 0)
			code = EEXIST;
                goto error_return;
	}


	/* check access */

	mp = XFS_VFSTOM(dir_vp->v_vfsp);
	tp = xfs_trans_alloc (mp, XFS_TRANS_WAIT);
	if (code = xfs_trans_reserve (tp, XFS_IALLOC_MAX_EVER_BLOCKS, 10, 0, 0))
		goto error_return;

	/*
	 * create the directory inode.
	 *
	 * XXX why does efs_mkdir pass 0 for rdev?
	 */
	rdev = (vap->va_mask & AT_RDEV) ? vap->va_rdev : NODEV;
        mode = IFDIR | (vap->va_mode & ~IFMT);
	code = xfs_dir_ialloc(&tp, dp, mode, 2, rdev, credp, &cdp);
	if (code)
		goto error_return;

	/*
	 * At this point, we've gotten a newly allocated inode.
	 * It is locked.
	 */
	xfs_trans_ijoin (tp, dp, XFS_ILOCK_EXCL);

	first_block = NULLFSBLOCK;
	bzero (&free_list, sizeof(free_list));

	if (code = xfs_dir_createname (tp, dp, dir_name, cdp->i_ino,
				       &first_block, &free_list,
				       MAX_EXT_NEEDED)) 
 		ASSERT (0);

	dnlc_enter (dir_vp, dir_name, XFS_ITOV(cdp), NOCRED);
	
	if (code = xfs_dir_init(tp, cdp, dp))
		ASSERT (0);

	cdp->i_gen = 1;

	dnlc_remove (XFS_ITOV(cdp), "..");


	*vpp = XFS_ITOV(cdp);

	xfs_trans_ihold (tp, dp);
	xfs_trans_ihold (tp, cdp);

	xfs_bmap_finish (&tp, &free_list, first_block);
	xfs_trans_commit (tp, 0);

	xfs_iunlock (dp, XFS_ILOCK_EXCL);
	xfs_iunlock (cdp, XFS_ILOCK_EXCL);

	return 0;

error_return:
	if (tp) {
		xfs_trans_cancel (tp, 0);
	}
	else 
		xfs_iunlock(dp, XFS_ILOCK_EXCL);

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
        xfs_inode_t             *cdp;   /* inode of created dir */
        xfs_trans_t             *tp = NULL;
        xfs_ino_t               e_inum;
        dev_t                   rdev;
        mode_t                  mode;
        struct xfs_mount        *mp;
        int                     error;
        xfs_bmap_free_t         free_list;
        xfs_fsblock_t           first_block;

        dp = XFS_VTOI(dir_vp);
	cdp = NULL;

	/* check access */

        xfs_ilock (dp, XFS_ILOCK_EXCL);

        if (error = xfs_dir_lookup_int(NULL, dir_vp, DLF_IGET, name, NULL,
				       &e_inum, &cdp))
		goto error_return;

	xfs_ilock (cdp, XFS_ILOCK_EXCL);

	if ((cdp == dp) || (cdp == XFS_VTOI(current_dir_vp))) {
		error = EINVAL;
		goto error_return;
	}
	if ((cdp->i_d.di_mode & IFMT) != IFDIR) {
	        error = ENOTDIR;
		goto error_return;
	}
	if (XFS_ITOV(cdp)->v_vfsmountedhere) {
		error = EBUSY;
		goto error_return;
	}
	if (cdp->i_d.di_nlink != 2) {
		error = EEXIST;
		goto error_return;
        }
	if ((! xfs_dir_isempty(cdp)) || (cdp->i_d.di_nlink > 2)) {
		error = EEXIST;
		goto error_return;
	}

	/*
	 * Now we can make the actual changes.
	 */
        tp = xfs_trans_alloc (XFS_VFSTOM(dir_vp->v_vfsp), XFS_TRANS_WAIT);
        if (error = xfs_trans_reserve (tp, 10, 10, 0, 0))
                goto error_return;

	xfs_trans_ijoin (tp, dp, XFS_ILOCK_EXCL);
	xfs_trans_ijoin (tp, cdp, XFS_ILOCK_EXCL);

	first_block = NULLFSBLOCK;
	bzero (&free_list, sizeof(free_list));
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
	 * When we remove a directory, we should decrement the vnode
	 * reference count. xfs_trans_commit will do that.
	 * We also need to drop the vnode reference obtained from
	 * xfs_dir_lookup_int. 
	 */
	IRELE (cdp);

	xfs_trans_ihold (tp, dp);

	xfs_bmap_finish (&tp, &free_list, first_block);
	xfs_trans_commit (tp, 0);

	xfs_iunlock (dp, XFS_ILOCK_EXCL);

	return 0;

error_return:
	xfs_iunlock (dp, XFS_ILOCK_EXCL);
	if (cdp) {
		IRELE (cdp);
		xfs_iunlock (cdp, XFS_ILOCK_EXCL);
	}
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

        dp = XFS_VTOI(dir_vp);
        xfs_ilock (dp, XFS_ILOCK_SHARED);

        if ((dp->i_d.di_mode & IFMT) != IFDIR) {
		xfs_iunlock(dp, XFS_ILOCK_SHARED);
                return ENOTDIR;
        }


	/* If the directory has been removed after it was opened. */
        if (dp->i_d.di_nlink <= 0) {
                xfs_iunlock(dp, XFS_ILOCK_SHARED);
                return 0;
        }

	status = xfs_dir_getdents (tp, dp, uiop, eofp);

	xfs_iunlock(dp, XFS_ILOCK_SHARED);
	
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
	xfs_sb_t        *sbp;
	xfs_bmap_free_t free_list;
	xfs_fsblock_t   first_block;

	/*
	 * Check component lengths of the target path name.
         */
        pathlen = strlen(target_path);
        if (pathlen >= MAXPATHLEN)      /* total string too long */
                return ENAMETOOLONG;
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
	xfs_ilock (dp, XFS_ILOCK_EXCL);

        error = xfs_dir_lookup_int(NULL, dir_vp, 0, link_name, NULL,
                                       &e_inum, NULL);
	if (error != ENOENT) {
		error = EEXIST;
                goto error_return;
	}

	mp = XFS_VFSTOM(dir_vp->v_vfsp);
	tp = xfs_trans_alloc (mp, XFS_TRANS_WAIT);
	if (error = xfs_trans_reserve (tp, XFS_IALLOC_MAX_EVER_BLOCKS, 10, 0, 0))
		goto error_return;

	/*
	 * Initialize the bmap freelist prior to calling either
	 * bmapi or the directory create code.
	 */
	bzero (&free_list, sizeof(free_list));
	first_block = NULLFSBLOCK;

	/*
	 * Allocate an inode for the symlink.
	 */
	rdev = (vap->va_mask & AT_RDEV) ? vap->va_rdev : NODEV;

	error = xfs_dir_ialloc(&tp, dp, IFLNK | (vap->va_mode&~IFMT),
		1, rdev, credp, &ip);
	if (error)
		goto error_return;

	sbp = &mp->m_sb;

	/*
	 * If the symlink will fit into the inode, write it inline.
	 */
	if (pathlen <= XFS_LITINO(sbp)) {
		xfs_idata_realloc (ip, pathlen);
		bcopy(target_path, ip->i_u1.iu_data, pathlen);

		/*
		 * The inode was initially created in extent format.
		 */
		ip->i_flags &= ~(XFS_IEXTENTS | XFS_IBROOT);
		ip->i_flags |= XFS_IINLINE;

		/*
		 * XXX This is temporary, should be removed later.
		 */
		ip->i_d.di_mode = (ip->i_d.di_mode & ~IFMT) | IFLNK;

		xfs_trans_ijoin (tp, ip, XFS_ILOCK_EXCL);
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
		fs_blocks = xfs_b_to_fsb(sbp, pathlen);
		nmaps = SYMLINK_MAPS;

		first_block = xfs_bmapi (tp, ip, first_fsb, fs_blocks,
                         XFS_BMAPI_WRITE, first_block, fs_blocks+dir_needs, 
			 mval, &nmaps, &free_list);

		cur_chunk = target_path;
		for (n = 0; n < nmaps; n++) {
			d = xfs_fsb_to_daddr(sbp, mval[n].br_startblock);
			byte_cnt = xfs_fsb_to_b(sbp, mval[n].br_blockcount);
			bp = xfs_trans_get_buf (tp, mp->m_dev, d, 
				BTOBB(byte_cnt), 0);
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

	xfs_trans_ijoin (tp, dp, XFS_ILOCK_EXCL);
	xfs_trans_ijoin (tp, ip, XFS_ILOCK_EXCL);

	/* Defer this to after directory code. */
	xfs_bmap_finish (&tp, &free_list, first_block);

	xfs_trans_commit (tp, 0);

	return 0;

error_return:
	if (tp)
		xfs_trans_cancel (tp, 0);

	xfs_iunlock (dp, XFS_ILOCK_EXCL);
	return error;

	return 0;
}


/*
 * xfs_fid
 *
 * This is a stub.
 */
STATIC int
xfs_fid(vnode_t	*vp,
	fid_t	**fidpp)
{
	return 0;
}


/*
 * xfs_rwlock
 *
 */
STATIC void
xfs_rwlock(vnode_t	*vp)
{
	xfs_inode_t	*ip;

	ip = XFS_VTOI(vp);
	xfs_ilock (ip, XFS_IOLOCK_EXCL);
	return;
}


/*
 * xfs_rwunlock
 *
 */
STATIC void
xfs_rwunlock(vnode_t	*vp)
{
        xfs_inode_t     *ip;

        ip = XFS_VTOI(vp);
        xfs_iunlock (ip, XFS_IOLOCK_EXCL);
        return;
}


/*
 * xfs_seek
 *
 */
STATIC int
xfs_seek(vnode_t	*vp,
	 off_t		old_offset,
	 off_t		*new_offsetp)
{
	return *new_offsetp < 0 ? EINVAL : 0;
}


/*
 * xfs_cmp
 *
 * This is a stub.
 */
STATIC int
xfs_cmp(vnode_t	*vp1,
	vnode_t	*vp2)
{
	return 0;
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
	return 0;
}


/*
 * xfs_realvp
 *
 * This is a stub.
 */
STATIC int
xfs_realvp(vnode_t	*vp,
	   vnode_t	**vpp)
{
	return -1;
}


/*
 * xfs_map
 *
 * This is a stub.
 */
STATIC int
xfs_map(vnode_t	*vp,
	uint	offset,
	preg_t	*pregp,
	addr_t	*addrp,
	uint	len,
	uint	prot,
	uint	max_prot,
	uint	flags,
	cred_t	*credp)
{
	return 0;
}


/*
 * xfs_addmap
 *
 * This is a stub.
 */
STATIC int
xfs_addmap(vnode_t	*vp,
	   uint		offset,
	   preg_t	*pregp,
	   addr_t	addr,
	   uint		len,
	   uint		prot,
	   uint		max_prot,
	   uint		flags,
	   cred_t	*credp)
{
	return 0;
}


/*
 * xfs_delmap
 *
 * This is a stub.
 */
STATIC int
xfs_delmap(vnode_t	*vp,
	   uint		offset,
	   preg_t	*pregp,
	   addr_t	addr,
	   uint		len,
	   uint		prot,
	   uint		max_prot,
	   uint		flags,
	   cred_t	*credp)
{
	return 0;
}


/*
 * xfs_allocstore
 *
 * This is a stub.
 */
STATIC int
xfs_allocstore(vnode_t	*vp,
	       uint	offset,
	       uint	len,
	       cred_t	*credp)
{
	return 0;
}


/*
 * xfs_reclaim
 *
 * This is a stub.
 */
STATIC int
xfs_reclaim(vnode_t	*vp,
	    int		flag)
{
	xfs_inode_t	*ip;

	ASSERT(!VN_MAPPED(vp));
	ip = XFS_VTOI(vp);

/*
	This part of the EFS code needs to be ported once
	we have a page cache and file I/O.

	if (ip->i_flags & IINCORE && ip->i_numextents > 0) {
		struct extent *ex = &ip->i_extents[ip->i_numextents - 1];
		pflushinvalvp(vp, 0, BBTOB(ex->ex_offset+ex->ex_length));
	}
*/
	dnlc_purge_vp(vp);
	ASSERT(ip->i_update_core == 0);
	ASSERT(ip->i_item.ili_format.ilf_fields == 0);
	xfs_ireclaim(ip);
	return 0;
}


/*
 * xfs_setfl
 *
 * This is a stub.
 */
STATIC int
xfs_setfl(vnode_t	*vp,
	  int		old_flags,
	  int		new_flags,
	  cred_t	*credp)
{
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
	xfs_inode_t	*ip;

	switch (cmd) {
	case F_DIOINFO: {
		struct dioattr	da;
		xfs_sb_t	*sbp;

		/* only works on files opened for direct I/O */
		if (!(flags & FDIRECT)) {
			error = EINVAL;
			break;
		}
		ip = XFS_VTOI(vp);
		sbp = &ip->i_mount->m_sb;
		da.d_mem = sbp->sb_blocksize;	/* memory alignment?? */
		da.d_miniosz =
			(ip->i_d.di_extsize ? ip->i_d.di_extsize :
			 (ip->i_d.di_flags & XFS_DIFLAG_REALTIME ?
			  sbp->sb_rextsize : 1)) << sbp->sb_blocklog;
		da.d_maxiosz = ctob(v.v_maxdmasz);	/* round? */
		if (copyout(&da, arg, sizeof(da)))
			error = EFAULT;
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
			error = EFAULT;
		break;
	    }

	case F_FSSETXATTR: {
		struct fsxattr fa;
		vattr_t va;

		if (copyin(arg, &fa, sizeof(fa))) {
			error = EFAULT;
			break;
		}
		va.va_xflags = fa.fsx_xflags;
		va.va_extsize = fa.fsx_extsize;
		error = xfs_setattr(vp, &va, AT_XFLAGS|AT_EXTSIZE, credp);
		break;
	    }

	default:
		error = EINVAL;
		break;
	}
	return error;
}


struct vnodeops xfs_vnodeops = {
	xfs_open,
	xfs_close,
	xfs_read,
	xfs_write,
	xfs_ioctl,
	xfs_setfl,
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
	xfs_realvp,
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
