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
#ifndef __XFS_LINUX__
#define __XFS_LINUX__

#include <asm/types.h>
#include <asm/div64.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <asm/page.h>
#include <asm/param.h>
#include <asm/byteorder.h>

#ifndef STATIC
#define STATIC
#endif


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
typedef	__uint32_t	inst_t;		/* an instruction */

typedef __uint32_t	app32_ulong_t;
typedef __uint32_t	app32_ptr_t;

#if (BITS_PER_LONG == 32)
#define XFS_64	0
typedef __int64_t	sysarg_t;
#elif (BITS_PER_LONG == 64)
#define XFS_64	1
typedef int		sysarg_t;
#else
#error BITS_PER_LONG must be 32 or 64
#endif

typedef struct timespec	timespec_t;

typedef struct pathname {
	char	*pn_path;	/* remaining pathname */
	u_long	pn_hash;	/* last component's hash */
	u_short	pn_complen;	/* last component length */
} pathname_t;

typedef __u64	xfs_off_t;
typedef __s32	xfs32_off_t;
typedef __u64	xfs_ino_t;	/* <inode> type */
typedef __s64	xfs_daddr_t;	/* <disk address> type */
typedef char *	xfs_caddr_t;	/* <core address> type */
typedef off_t	linux_off_t;
typedef __kernel_ino_t	linux_ino_t;
typedef __uint32_t	xfs_dev_t;


#define _PAGESZ		PAGE_SIZE
#define NBPP		PAGE_SIZE 
#define DPPSHFT		(PAGE_SHIFT - 9)
#define NDPP		(1 << (PAGE_SHIFT - 9))
#define dtop(DD)	(((DD) + NDPP - 1) >> DPPSHFT)
#define dtopt(DD)	((DD) >> DPPSHFT)
#define dpoff(DD)	((DD) & (NDPP-1))
#define NBBY    8       /* number of bits per byte */

/*
 * Size of block device i/o is parameterized here.
 * Currently the system supports page-sized i/o.
 */
#define	BLKDEV_IOSHIFT		BPCSHIFT
#define	BLKDEV_IOSIZE		(1<<BLKDEV_IOSHIFT)
/* number of BB's per block device block */
#define	BLKDEV_BB		BTOBB(BLKDEV_IOSIZE)

#define	NBPC		_PAGESZ	/* Number of bytes per click */

#if	NBPC == 4096
#define	BPCSHIFT	12	/* LOG2(NBPC) if exact */
#define CPSSHIFT	10	/* LOG2(NCPS) if exact */
#endif
#if	NBPC == 8192
#define	BPCSHIFT	13	/* LOG2(NBPC) if exact */
#define CPSSHIFT	11	/* LOG2(NCPS) if exact */
#endif
#if	NBPC == 16384
#define	BPCSHIFT	14	/* LOG2(NBPC) if exact */
#ifndef	PTE_64BIT
#define CPSSHIFT	12	/* LOG2(NCPS) if exact */
#else	/* PTE_64BIT */
#define CPSSHIFT	11	/* LOG2(NCPS) if exact */
#endif	/* PTE_64BIT */
#endif


#define bzero(p,s)	memset((p), 0, (s))
#define bcopy(s,d,n)	memcpy((d),(s),(n))
#define bcmp(s1,s2,l)	memcmp(s1,s2,l)    
#define ovbcopy(from,to,count)	memmove(to,from,count)


/*
 * XXX these need real values in errno.h. asm-i386/errno.h won't 
 * return errnos out of its known range in errno.
 */
#define ENOTSUP		1008	/* Not supported (POSIX 1003.1b) */
#define ENOATTR         ENODATA /* Attribute not found */
#define EFSCORRUPTED    1010    /* Filesystem is corrupted */
#define	EWRONGFS	1011	/* Mount with wrong filesystem type */

#define SYNCHRONIZE()	((void)0)
#define ENTER(x)	printk("Entering %s\n",x);
#define EXIT(x)		printk("Exiting  %s\n",x);
#define lbolt		jiffies
#define __return_address __builtin_return_address(0)
#define LONGLONG_MAX	9223372036854775807LL	/* max "long long int" */
#define XFS_kmem_realloc(ptr,new,old,flag) kmem_realloc(ptr,new,old,flag)
#define nopkg()		( ENOPKG )
#define getf(fd,fpp)	( printk("getf not implemented\n"), ASSERT(0), 0 )

#define MAXNAMELEN      256
#define	MAXPATHLEN	1024

#define	PSWP	0
#define PMEM	0
#define PINOD   10
#define PRIBIO  20

#define	PLTWAIT 0x288 /* O'01000' */
#define	PVFS	27

#define MIN(a,b)	(((a)<(b))?(a):(b))
#define MAX(a,b)	(((a)>(b))?(a):(b))
#define howmany(x, y)   (((x)+((y)-1))/(y))
#define roundup(x, y)   ((((x)+((y)-1))/(y))*(y))


struct xfs_args;
extern int  mountargs_xfs (char *, struct xfs_args *);


static inline void delay(long ticks)
{
        current->state = TASK_UNINTERRUPTIBLE;
        schedule_timeout(ticks);
}

static inline void nanotime(timespec_t *tvp)
{
        tvp->tv_sec = xtime.tv_sec;
        tvp->tv_nsec = xtime.tv_usec * 1000;
}

/* Move the kernel do_div definition off to one side */
static inline __u32 xfs_do_div(void *a, __u32 b, int n)
{
	__u32	mod;

	switch (n) {
		case 4:
			mod = *(__u32 *)a % b;
			*(__u32 *)a = *(__u32 *)a / b;
			return mod;
		case 8:
			mod = do_div(*(__u64 *)a, b);
			return mod;
	}

	/* NOTREACHED */
	return 0;
}

/* Side effect free 64 bit mod operation */
static inline __u32 xfs_do_mod(void *a, __u32 b, int n)
{
	switch (n) {
		case 4:
			return *(__u32 *)a % b;
		case 8:
			{
			__u64	c = *(__u64 *)a;
			return do_div(c, b);
			}
	}

	/* NOTREACHED */
	return 0;
}

#undef do_div
#define do_div(a, b)	xfs_do_div(&(a), (b), sizeof(a))
#define do_mod(a, b)	xfs_do_mod(&(a), (b), sizeof(a))

extern inline __uint64_t roundup_64(__uint64_t x, __uint32_t y)
{
	x += y - 1;
	do_div(x, y);
	return(x * y);
}

#endif /* __XFS_LINUX__ */
