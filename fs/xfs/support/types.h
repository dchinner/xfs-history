/*
 * Copyright (c) 2000-2002 Silicon Graphics, Inc.  All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * Further, this software is distributed without any warranty that it is
 * free of the rightful claim of any third person regarding infringement
 * or the like.	 Any license provided herein, whether implied or
 * otherwise, applies only to this software file.  Patent licenses, if
 * any, provided herein do not apply to combinations of this program with
 * other software, or any other product whatsoever.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write the Free Software Foundation, Inc., 59
 * Temple Place - Suite 330, Boston MA 02111-1307, USA.
 *
 * Contact information: Silicon Graphics, Inc., 1600 Amphitheatre Pkwy,
 * Mountain View, CA  94043, or:
 *
 * http://www.sgi.com
 *
 * For further information regarding this notice, see:
 *
 * http://oss.sgi.com/projects/GenInfo/SGIGPLNoticeExplan/
 */
#ifndef __XFS_SUPPORT_TYPES_H__
#define __XFS_SUPPORT_TYPES_H__

#include <linux/types.h>

/*
 * Additional type declarations for XFS
 */

typedef signed char	__int8_t;
typedef unsigned char	__uint8_t;
typedef signed short int	__int16_t;
typedef unsigned short int	__uint16_t;
typedef signed int	__int32_t;
typedef unsigned int	__uint32_t;
typedef signed long long int	__int64_t;
typedef unsigned long long int	__uint64_t;

/* POSIX Extensions */
typedef unsigned char	uchar_t;
typedef unsigned short	ushort_t;
typedef unsigned int	uint_t;
typedef unsigned long	ulong_t;

typedef enum { B_FALSE, B_TRUE } boolean_t;

typedef __int64_t	prid_t;		/* project ID */
typedef __uint32_t	inst_t;		/* an instruction */

typedef __uint32_t	app32_ulong_t;
typedef __uint32_t	app32_ptr_t;

#if (BITS_PER_LONG == 32)
#define XFS_64	0
typedef __int64_t	sysarg_t;
#elif (BITS_PER_LONG == 64)
#define XFS_64	1
typedef long		sysarg_t;
#else
#error BITS_PER_LONG must be 32 or 64
#endif

typedef struct timespec timespec_t;

typedef __u64	xfs_off_t;
typedef __s32	xfs32_off_t;
typedef __u64	xfs_ino_t;	/* <inode> type */
typedef __s64	xfs_daddr_t;	/* <disk address> type */
typedef char *	xfs_caddr_t;	/* <core address> type */
typedef __u32	xfs_dev_t;

typedef struct {
	unsigned char	__u_bits[16];
} uuid_t;

/* alias kmem zones for xfs */
#define xfs_zone_t kmem_zone_t
#define xfs_zone   kmem_cache_s

#endif	/* __XFS_SUPPORT_TYPES_H__ */
