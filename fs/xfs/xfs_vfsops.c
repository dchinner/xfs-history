
#include <strings.h>
#include <sys/types.h>
#ifdef SIM
#define _KERNEL
#endif
#include <sys/buf.h>
#include <sys/vfs.h>
#ifdef SIM
#undef _KERNEL
#endif
#include <sys/cmn_err.h>
#include <sys/debug.h>
#include <sys/errno.h>
#include <sys/fs_subr.h>
#ifdef SIM
#include <bstring.h>
#include <stdio.h>
#else
#include <sys/kmem.h>
#include <sys/sysmacros.h>
#include <sys/systm.h>
#endif
#include <sys/mount.h>
#include <sys/param.h>
#include <sys/pathname.h>
#include <sys/pfdat.h>
#include <sys/proc.h>
#include <sys/sema.h>
#include <sys/statvfs.h>
#include <sys/uio.h>
#include <sys/user.h>
#include <sys/vnode.h>
#include <sys/uuid.h>
#include "xfs_types.h"
#include "xfs_inum.h"
#include "xfs_log.h"
#include "xfs_trans.h"
#include "xfs_sb.h"
#include "xfs_mount.h"


#ifdef SIM
#include "sim.h"
#endif

/*
 * Static function prototypes.
 */
STATIC int	xfs_vfsmount(vfs_t		*vfsp,
			     vnode_t	*mvp,
			     struct mounta	*map,
			     cred_t	*credp);

STATIC int	xfs_mountroot(vfs_t		*vfsp,
			      enum whymountroot	why);

STATIC int	xfs_unmount(vfs_t	*vfsp,
			    int		flags,
			    cred_t	*credp);

STATIC int	xfs_root(vfs_t		*vfsp,
			 vnode_t	**vpp);

STATIC int	xfs_statvfs(vfs_t	*vfsp,
			    statvfs_t	*statp,
			    vnode_t	*vp);

STATIC int	xfs_sync(vfs_t		*vp,
			 short		flags,
			 cred_t		*credp);

STATIC int	xfs_vget(vfs_t		*vfsp,
			 vnode_t	**vpp,
			 fid_t		*fidp);



/*
 * xfs_type is the number given to xfs to indicate
 * its type among vfs's.  It is initialized in xfs_init().
 */
int	xfs_type;

/*
 * xfs_init
 *
 * This is called through the vfs switch at system initialization
 * to initialize any global state associated with XFS.  All we
 * need to do currently is save the type given to XFS in xfs_type.
 *
 * vswp -- pointer to the XFS entry in the vfs switch table
 * fstype -- index of XFS in the vfs switch table used as the XFS fs type.
 */
int
xfs_init(vfssw_t	*vswp,
	 int		fstype)
{
	xfs_type = fstype;
	return 0;
}


/*
 * xfs_mount
 *
 * This is just a stub.
 */ 
STATIC int
xfs_vfsmount(vfs_t		*vfsp,
	     vnode_t	*mvp,
	     struct mounta	*map,
	     cred_t	*credp)
{
	return 0;
}


/*
 * xfs_mountroot
 *
 * This is just a stub
 */
STATIC int
xfs_mountroot(vfs_t		*vfsp,
	      enum whymountroot	why)
{
	return 0;
}


/*
 * xfs_unmount
 *
 * This is just a stub.
 */
STATIC int
xfs_unmount(vfs_t	*vfsp,
	    int		flags,
	    cred_t	*credp)
{
	return 0;
}


/*
 * xfs_root
 *
 * This is just a stub
 */
STATIC int
xfs_root(vfs_t		*vfsp,
	 vnode_t	**vpp)
{
	return 0;
}


/*
 * xfs_statvfs
 *
 * Fill in the statvfs structure for the given file system.  We use
 * the superblock lock in the mount structure to ensure a consistent
 * snapshot of the counters returned.
 */
STATIC int
xfs_statvfs(vfs_t	*vfsp,
	    statvfs_t	*statp,
	    vnode_t	*vp)
{
	xfs_mount_t	*mp;
	xfs_sb_t	*sbp;
	int		s;

	mp = XFS_VFSTOM(vfsp);
	sbp = &(mp->m_sb);

	s = XFS_SB_LOCK(mp);
	statp->f_bsize = sbp->sb_blocksize;
	statp->f_frsize = sbp->sb_blocksize;
	statp->f_blocks = sbp->sb_dblocks;
	statp->f_bfree = sbp->sb_fdblocks;
	statp->f_files = sbp->sb_icount;
	statp->f_ffree = sbp->sb_ifree;
	statp->f_flag = vf_to_stf(vfsp->vfs_flag);
	XFS_SB_UNLOCK(mp, s);

	statp->f_fsid = mp->m_dev;
	(void) strcpy(statp->f_basetype, vfssw[xfs_type].vsw_name);
	statp->f_namemax = MAXNAMELEN;
	bzero(statp->f_fstr, sizeof(statp->f_fstr));

	return 0;
}


/*
 * xfs_sync
 *
 * This is just a stub
 */
STATIC int
xfs_sync(vfs_t		*vp,
	 short		flags,
	 cred_t		*credp)
{
	return 0;
}


/*
 * xfs_vget
 *
 * This is just a stub
 */
STATIC int
xfs_vget(vfs_t		*vfsp,
	 vnode_t	**vpp,
	 fid_t		*fidp)
{
	return 0;
}



struct vfsops xfs_vfsops = {
	xfs_vfsmount,
	xfs_unmount,
	xfs_root,
	xfs_statvfs,
	xfs_sync,
	xfs_vget,
	xfs_mountroot,
	fs_nosys,	/* swapvp */
};



























