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
 
#ifndef __XFS_LINUX_
#define __XFS_LINUX_


#ifndef STATIC
#define STATIC
#endif

#ifdef	__KERNEL__
# include <asm/types.h>		/* Need BITS_PER_LONG */
#else	/* __KERNEL__ */
# define __KERNEL__
# include <asm/types.h>		/* Need BITS_PER_LONG */
# undef  __KERNEL__
#endif	/* __KERNEL__ */

#if (BITS_PER_LONG == 32)
#define XFS_64	0
#elif (BITS_PER_LONG == 64)
#define XFS_64	1
#else
#error BITS_PER_LONG must be 32 or 64
#endif

#include <sys/types.h>

#ifndef SIM
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/string.h>   /* to get memcpy, and friends */
#else
#include <time.h>
#include <stdio.h>
#endif

#include <asm/page.h>
#include <asm/param.h>
#include <asm/byteorder.h>
#define _PAGESZ		PAGE_SIZE
#define NBPP		PAGE_SIZE 
#define DPPSHFT		(PAGE_SHIFT - 9)
#define NDPP		(1 << (PAGE_SHIFT - 9))
#define dtop(DD)	(((DD) + NDPP - 1) >> DPPSHFT)
#define dtopt(DD)	((DD) >> DPPSHFT)
#define dpoff(DD)	((DD) & (NDPP-1))

#ifndef SIM
#define ENOTSUP		1008	/* Not supported (POSIX 1003.1b) */
#endif

/*
 * XXX these need real values in errno.h. asm-i386/errno.h won't 
 * return errnos out of its known range in errno.
 */

#define ENOATTR         ENODATA /* Attribute not found */
#define EFSCORRUPTED    1010    /* Filesystem is corrupted */
#define	EWRONGFS	1011	/* Mount with wrong filesystem type */


#define ENTER(x) printk("Entering %s\n",x);
#define EXIT(x)  printk("Exiting  %s\n",x);

#ifdef __KERNEL__
extern int get_thread_id(void);
#endif

#ifndef _LINUX_SCHED_H
extern unsigned long volatile jiffies;
#endif
#define lbolt		jiffies


#define __return_address __builtin_return_address(0)

#define LONGLONG_MAX        9223372036854775807LL /* max "long long int" */


typedef struct timespec	timespec_t;

#ifndef NBBY
#define NBBY    8       /* number of bits per byte */
#endif  /* NBBY */


#endif /* __XFS_LINUX_ */
