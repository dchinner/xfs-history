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

int    		xfs_refcache_percent = 100;
int		mac_enabled = 0;
int		acl_enabled = 0;
int		ncsize = 792;
int		nclrus = 0;


int imon_enabled;


int
cap_able(long long int cid)
{
	/*
	 * This function requires user context. If you hit this
	 * ASSERT, there's a call to CAP_ABLE() which needs to be
	 * changed to use CAP_CRABLE() (or cap_able_cred())
	 */
  printf("cap_able not implemented\n");
#if 0
	ASSERT(get_current_cred() != NULL);

	return cap_able_cred(get_current_cred(), cid);
#endif
}
