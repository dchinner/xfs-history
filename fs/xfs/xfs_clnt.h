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
#ident "$Revision: 1.11 $"

/*
 * XFS arguments to the mount system call.
 */
struct xfs_args {
	int	version;	/* version of this */
				/* 1, see xfs_args_ver_1 */
				/* 2, see xfs_args_ver_2 */
	int	flags;		/* flags, see XFSMNT_... below */
	int	logbufs;	/* Number of log buffers, -1 to default */
	int	logbufsize;	/* Size of log buffers, -1 to default */
	char	*fsname;	/* filesystem name (mount point) */
	/*
	 * Next two are for stripe aligment.
	 * Set 0 for no alignment handling (see XFSMNT_NOALIGN flag)
	 */
	int	sunit;		/* stripe unit (bbs) */
	int	swidth;		/* stripe width (bbs), multiple of sunit */
};

#ifdef _KERNEL
struct xfs_args_ver_1 {
	__int32_t	version;
	__int32_t	flags;
	__int32_t	logbufs;
	__int32_t	logbufsize;
	app32_ptr_t	fsname;
};

struct xfs_args_ver_2 {
	__int32_t	version;
	__int32_t	flags;
	__int32_t	logbufs;
	__int32_t	logbufsize;
	app32_ptr_t	fsname;
	__int32_t	sunit;
	__int32_t	swidth;
};
#endif /* _KERNEL */

/*
 * XFS mount option flags
 */
#define	XFSMNT_CHKLOG		0x0001	/* check log */
#define	XFSMNT_WSYNC		0x0002	/* safe mode nfs mount compatible */
#define	XFSMNT_INO64		0x0004	/* move inode numbers up past 2^32 */
#define XFSMNT_UQUOTA		0x0008	/* user quota accounting */
#define XFSMNT_PQUOTA		0x0010	/* project quota accounting */
#define XFSMNT_UQUOTAENF	0x0020	/* user quota limit enforcement */
#define XFSMNT_PQUOTAENF	0x0040	/* project quota limit enforcement */
#define XFSMNT_QUOTAMAYBE	0x0080  /* don't turn off if SB has quotas on */
#define XFSMNT_NOATIME		0x0100  /* don't modify access times on reads */
#define XFSMNT_NOALIGN		0x0200	/* don't allocate at stripe boundaries*/
#define XFSMNT_RETERR		0x0400	/* return error to user */

#endif /* !__SYS_XFS_CLNT_H__ */
