/*
 *
 * $Header: /plroot/pingu/slinx-xfs/kern/fs/xfs/linux/RCS/xfs_globals.c,v 1.1 1999/09/01 00:35:47 mostek Exp $
 * $Author: mostek $
 * $Id: xfs_globals.c,v 1.1 1999/09/01 00:35:47 mostek Exp $
 *
 * $Log: xfs_globals.c,v $
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
#include <sys/cred.h>
int    		xfs_refcache_percent = 100;
int		mac_enabled = 0;
int		acl_enabled = 0;
int		ncsize = 792;
int		nclrus = 0;


int imon_enabled;
int	scache_linemask;	/* second level cache line size mask */


prid_t dfltprid;
struct cred		*credp;
