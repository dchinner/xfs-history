/*
 *
 * $Header: /isms/pingu/slinx-xfs/kern/fs/xfs/linux/RCS/xfs_globals.c,v 1.3 1999/09/02 22:24:14 cattelan Exp $
 * $Author: cattelan $
 * $Id: xfs_globals.c,v 1.3 1999/09/02 22:24:14 cattelan Exp $
 *
 * $Log: xfs_globals.c,v $
 * Revision 1.3  1999/09/02 22:24:14  cattelan
 * No Message Supplied
 *
 * Revision 1.1  1999/09/01 00:35:47  mostek
 * Add some globals that came from systune. Systune doesn't exist
 * on Linux.
 *
 */

/*
 * This file contains globals needed by XFS that were normally defined
 * somewhere else in IRIX.
 */

#include <sys/types.h>
#include <sys/var.h>
#include <sys/vfs.h>
#include <sys/sysinfo.h>
#include <sys/cred.h>

int    		xfs_refcache_percent = 100;
int		mac_enabled = 0;
int		acl_enabled = 0;
int		ncsize = 792;
int		nclrus = 0;
int		xpg4_sticky_dir = 1; /* see xfs_stickytest */
int		imon_enabled;
int		xfsd_pri = 0x5c;	/* priority of xfsd thread(s) */
uint64_t	xfs_panic_mask;		/* set to cause more panics */
int		xfs_nfs_io_units = 10;	/* Ignore for now. Affects NFS performance. */
vfssw_t		vfssw[2] = { { "xfs" } };	/* vfssw for XFS only */
struct var	v = {
			500 /* JIMJIM set these up if needed */
		  /* int     v_buf * Nbr of I/O buffers.                  */
		  /* int     v_hbuf * Nbr of hash buffers to allocate.     */
		  /* int	v_maxdmaszi * Max dma unbroken dma transfer size. */
		};

struct syswait	syswait;
dev_t           rootdev = NODEV;
int             restricted_chown = 0;
int     	scache_linemask = 0x1f;       /* second level cache line size mask */
int		imon_enabled;
prid_t		dfltprid;
