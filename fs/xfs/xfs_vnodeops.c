
#include <sys/types.h>
#ifdef SIM
#define _KERNEL 1
#endif
#include <sys/buf.h>
#include <sys/uio.h>
#include <sys/user.h>
#include <sys/vfs.h>
#include <sys/vnode.h>
#include <sys/systm.h>
#include <sys/dnlc.h>
#include <sys/sysmacros.h>
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
/*
#include <stdio.h>
*/
#else
#include <sys/kmem.h>
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
#include "xfs_types.h"
#include "xfs_inum.h"
#include "xfs_trans.h"
#include "xfs_sb.h"
#include "xfs_mount.h"
#include "xfs_alloc.h"
#include "xfs_ialloc.h"
#include "xfs_ag.h"
#include "xfs_bmap.h"
#include "xfs_btree.h"
#include "xfs_dinode.h"
#include "xfs_inode_item.h"
#include "xfs_inode.h"

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

STATIC int	xfs_read(vnode_t	*vp,
			 uio_t		*uiop,
			 int		ioflag,
			 cred_t		*credp);

STATIC int	xfs_write(vnode_t	*vp,
			  uio_t		*uiop,
			  int		ioflag,
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

STATIC int	xfs_bmap(vnode_t	*vp,
			 off_t		offset,
			 ssize_t	count,
			 int		rw,
			 cred_t		*credp,
			 struct bmapval	*bmapp,
			 int		*nmaps);

STATIC void	xfs_strategy(vnode_t	*vp,
			     buf_t	*bp);

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
			  void		*argp,
			  int		flags,
			  off_t		offset,
			  cred_t	*credp,
			  rval_t	*rvalp);

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
 * xfs_read
 *
 * This is a stub.
 */
STATIC int
xfs_read(vnode_t	*vp,
	 uio_t		*uiop,
	 int		ioflag,
	 cred_t		*credp)
{
	return 0;
}


/*
 * xfs_write
 *
 * This is a stub.
 */
STATIC int
xfs_write(vnode_t	*vp,
	  uio_t		*uiop,
	  int		ioflag,
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
 *
 * This is a stub.
 */
STATIC int
xfs_getattr(vnode_t	*vp,
	    vattr_t	*vap,
	    int		flags,
	    cred_t	*credp)
{
	return 0;
}


/*
 * xfs_setattr
 *
 * This is a stub.
 */
STATIC int
xfs_setattr(vnode_t	*vp,
	    vattr_t	*vap,
	    int		flags,
	    cred_t	*credp)
{
	return 0;
}


/*
 * xfs_access
 *
 * This is a stub.
 */
STATIC int
xfs_access(vnode_t	*vp,
	   int		mode,
	   int		flags,
	   cred_t	*credp)
{
	return 0;
}


/*
 * xfs_readlink
 *
 * This is a stub.
 */
STATIC int
xfs_readlink(vnode_t	*vp,
	     uio_t	*uiop,
	     cred_t	*credp)
{
	return 0;
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
 * The following vnodeops are all involved in directory manipulation.
 */


/*
 * xfs_lookup
 *
 * This is a stub.
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
	return 0;
}


/*
 * xfs_create
 *
 * This is a stub.
 */
STATIC int
xfs_create(vnode_t	*dir_vp,
	   char		*name,
	   vattr_t	*vap,
	   enum vcexcl	excl,
	   int		mode,
	   vnode_t	**vpp,
	   cred_t	*credp)
{
	return 0;
}



/*
 * xfs_remove
 *
 * This is a stub.
 */
STATIC int
xfs_remove(vnode_t	*dir_vp,
	   char		*name,
	   cred_t	*credp)
{
	return 0;
}


/*
 * xfs_link
 *
 * This is a stub.
 */
STATIC int
xfs_link(vnode_t	*target_dir_vp,
	 vnode_t	*src_vp,
	 char		*target_name,
	 cred_t		*credp)
{
	return 0;
}


/*
 * xfs_rename
 *
 * This is a stub.
 */
STATIC int
xfs_rename(vnode_t	*src_dir_vp,
	  char		*src_name,
	  vnode_t	*target_dir_vp,
	  char		*target_name,
	  pathname_t	*target_pnp,
	  cred_t	*credp)
{
	return 0;
}


/*
 * xfs_mkdir
 *
 * This is a stub.
 */
STATIC int
xfs_mkdir(vnode_t	*dir_vp,
	  char		*dir_name,
	  vattr_t	*vap,
	  vnode_t	**vpp,
	  cred_t	*credp)
{
	return 0;
}


/*
 * xfs_rmdir
 *
 * This is a stub.
 */
STATIC int
xfs_rmdir(vnode_t	*dir_vp,
	  char		*name,
	  vnode_t	*current_dir_vp,
	  cred_t	*credp)
{
	return 0;
}


/*
 * xfs_readdir
 *
 * This is a stub.
 */
STATIC int
xfs_readdir(vnode_t	*dir_vp,
	    uio_t	*uiop,
	    cred_t	*credp,
	    int		*eofp)
{
	return 0;
}

/*
 * xfs_symlink
 *
 * This is a stub.
 */
STATIC int
xfs_symlink(vnode_t	*dir_vp,
	    char	*link_name,
	    vattr_t	*vap,
	    char	*target_path,
	    cred_t	*credp)
{
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
 * This is a stub.
 */
STATIC void
xfs_rwlock(vnode_t	*vp)
{
	return;
}


/*
 * xfs_rwunlock
 *
 * This is a stub.
 */
STATIC void
xfs_rwunlock(vnode_t	*vp)
{
	return;
}


/*
 * xfs_seek
 *
 * This is a stub.
 */
STATIC int
xfs_seek(vnode_t	*vp,
	 off_t		old_offset,
	 off_t		*new_offsetp)
{
	return 0;
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
	return 0;
}


/*
 * xfs_bmap
 *
 * This is a stub.
 */
STATIC int
xfs_bmap(vnode_t	*vp,
	 off_t		offset,
	 ssize_t	count,
	 int		rw,
	 cred_t		*credp,
	 struct bmapval	*bmapp,
	 int		*nmaps)
{
	return 0;
}


/*
 * xfs_strategy
 *
 * This is a stub.
 */
STATIC void
xfs_strategy(vnode_t	*vp,
	     buf_t	*bp)
{
	bdstrat(bmajor(bp->b_edev), bp); 
	return;
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
	ASSERT(ip->i_item.ili_fields == 0);
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
 *
 * This is a stub.
 */
STATIC int
xfs_fcntl(vnode_t	*vp,
	  int		cmd,
	  void		*argp,
	  int		flags,
	  off_t		offset,
	  cred_t	*credp,
	  rval_t	*rvalp)
{
	return 0;
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
	xfs_cmp,
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
























