/*
 * Copyright (c) 2000 Silicon Graphics, Inc.  All Rights Reserved.
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
 * or the like.  Any license provided herein, whether implied or
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
#include <linux/xfs_linux.h>
#include <xfs_arch.h>

#ifdef NEED_FS_H
# include <linux/fs.h>
#endif

#ifndef SIM
#include <asm/div64.h>

typedef __u64    xfs_off_t;
typedef	__u64    xfs_ino_t;		/* <inode> type */
typedef	__s64    xfs_daddr_t;	/* <disk address> type */
typedef	char *	 xfs_caddr_t;	/* ?<core address> type */

typedef off_t linux_off_t;
typedef __kernel_ino_t linux_ino_t;

/* Side effect free 64 bit mod operation */
extern inline __u32 do_mod(__u64 a, __u32 b) {
	return do_div(a, b);
}

#else
typedef loff_t  xfs_off_t;
typedef	__u64	xfs_ino_t;		/* <inode> type */
typedef __s64   xfs_daddr_t;
typedef	char *	xfs_caddr_t;	/* ?<core address> type */
typedef ino_t   linux_ino_t;
typedef off_t   linux_off_t;

#define do_mod(a, b)	((a) % (b))
#define do_div(n,base) ({ \
        int __res; \
        __res = ((unsigned long) n) % (unsigned) base; \
        n = ((unsigned long) n) / (unsigned) base; \
        __res; })

#endif

#define XFS_kmem_realloc(ptr,new,old,flag) kmem_realloc(ptr,new,old,flag)
