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
#ident "$Revision: 1.6 $"

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

#ifdef _KERNEL
struct irix5_xfs_args {
	__int32_t	version;
	__int32_t	flags;
	__int32_t	logbufs;
	__int32_t	logbufsize;
	app32_ptr_t	fsname;
};
#endif /* _KERNEL */

/*
 * XFS mount option flags
 */
#define	XFSMNT_CHKLOG	0x0001	/* check log */
#define	XFSMNT_WSYNC	0x0002	/* safe mode nfs mount compatible */
#define	XFSMNT_INO64	0x0004	/* move inode numbers up past 2^32 */
#define XFSMNT_UQUOTA		0x0008	/* user quota accounting */
#define XFSMNT_PQUOTA		0x0010	/* project quota accounting */
#define XFSMNT_UQUOTAENF	0x0020	/* user quota limit enforcement */
#define XFSMNT_PQUOTAENF	0x0040	/* project quota limit enforcement */

#endif /* !__SYS_XFS_CLNT_H__ */
