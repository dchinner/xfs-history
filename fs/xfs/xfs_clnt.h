#ifndef __SYS_XFS_CLNT_H__
#define __SYS_XFS_CLNT_H__

/**************************************************************************
 *									  *
 * 		 Copyright (C) 1993, Silicon Graphics, Inc.		  *
 *									  *
 *  These coded instructions, statements, and computer programs  contain  *
 *  unpublished  proprietary  information of Silicon Graphics, Inc., and  *
 *  are protected by Federal copyright law.  They  may  not be disclosed  *
 *  to  third  parties  or copied or duplicated in any form, in whole or  *
 *  in part, without the prior written consent of Silicon Graphics, Inc.  *
 *									  *
 **************************************************************************/
#ident "$Revision: 1.3 $"

/*
 * XFS arguments to the mount system call.
 */
struct xfs_args {
	int	version;	/* version of this */
	int	flags;		/* flags */
	int	logbufs;	/* Number of log buffers */
	int	logbufsize;	/* Size of log buffers */
	char	*fsname;	/* filesystem name */
};

/*
 * XFS mount option flags
 */
#define	XFSMNT_CHKLOG	0x0001	/* check log */
#define	XFSMNT_WSYNC	0x0002	/* safe mode nfs mount compatible */

#endif /* !__SYS_XFS_CLNT_H__ */
