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
/*
 *
 * /ptools/plroot/pingu/slinx-xfs/kern/fs/xfs/linux/RCS/xfs_linux.h,v 1.1 1999/10/01 18:28:33 cattelan Exp
 * cattelan
 * xfs_linux.h,v 1.1 1999/10/01 18:28:33 cattelan Exp
 *
 * xfs_linux.h,v
 * Revision 1.1  1999/10/01 18:28:33  cattelan
 * kern/fs/xfs/xfs_linux.h 1.19 Renamed to kern/fs/xfs/linux/xfs_linux.h
 *
 * Revision 1.19  1999/09/29 17:55:19  lord
 * remove zone definition
 *
 * Revision 1.18  1999/09/10 18:00:00  cattelan
 * Added ENTER and EXIT macros. 
 *
 * Revision 1.16  1999/09/03 00:40:44  mostek
 * Add include of linux/string.h to get memcpy and friends from linux.
 *
 * Revision 1.14  1999/09/02 22:24:14  cattelan
 * No Message Supplied
 *
 * Revision 1.12  1999/09/02 18:49:02  mostek
 * Move _PAGESZ define up so that it is defined when used (see immu.h).
 *
 * Revision 1.11  1999/08/31 16:17:54  cattelan
 * Removed more stuff;
 *
 * Revision 1.9  1999/08/26 19:43:29  mostek
 * Remove stuff moved to other files and add protos for routines that
 * we will define in sim.random.c.
 *
 * Revision 1.8  1999/08/25 16:13:43  mostek
 * Don't typedef __kernel_clock_t if posix types has been included.
 * define bzero and bcopy (memset's)
 * extern for sys_cred that lots of folks will need.
 *
 * Revision 1.7  1999/08/25 02:22:32  cattelan
 * Many more header file fix ups.
 * Many more to come.
 *
 * Revision 1.5  1999/08/20 17:05:17  mostek
 * comment1
 *
 * Revision 1.4  1999/08/20 15:55:00  cattelan
 * Added a few more function decs
 *
 */
#ifndef __XFS_LINUX_
#define __XFS_LINUX_

#define _MIPS_SIM _ABIN32

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

/* ksys/dvh.h */
#define	NPARTAB		16		/* 16 unix partitions */

#define LONGLONG_MAX        9223372036854775807LL /* max "long long int" */

#define _DIOC_(x) (('d'<<8) | x)
#define DIOCGETVOLDEV	_DIOC_(36)	/* NEW: retrieve subvolume devices */

typedef long int	irix5_off_t;

typedef struct timespec	timespec_t;

#ifndef NBBY
#define NBBY    8       /* number of bits per byte */
#endif  /* NBBY */

#define findrawpath(x) x
#define findblockpath(x) x

#undef bzero
#define bzero(p,s) memset((p), 0, (s))

#undef bcopy
#define bcopy(s,d,n) memcpy((d),(s),(n))

extern void *kern_malloc(size_t);
#define bcmp(s1,s2,l) memcmp(s1,s2,l)    

#endif /* __XFS_LINUX_ */
