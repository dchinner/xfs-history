
/*	Stub routines needed for X/open DMAPI implementation.
 *	This is duplicated from kern/stubs/xfsdmapistubs.c
 */

extern int nopkg(void);

int	xfs_dm_fcntl() { return nopkg(); }

int	xfs_dm_send_create_event() { return 0; }

int	xfs_dm_send_data_event() { return nopkg(); }
