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

#include <linux/vnode.h>
#include <linux/mm.h>
#include <linux/xfs_fs.h>
#include <linux/xfs_vfs.h>
#include <linux/xfs_vnode.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <pagebuf/page_buf.h>
#include <linux/file.h>
#include <linux/swap.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/bitops.h>
#include <asm/page.h>
#include <asm/div64.h>
#include <asm/param.h>
#include <asm/uaccess.h>
#include <asm/byteorder.h>
#include <linux/xfs_cred.h>
#include <linux/xfs_stats.h>
#include <linux/xfs_sysctl.h>
#include <linux/xfs_super.h>
#include <linux/xfs_globals.h>
#include <linux/xfs_behavior.h>
#include <linux/xfs_fs_subr.h>
#include <linux/xfs_xattr.h>
#include <linux/dmapi.h>
#include <linux/dmapi_kern.h>

#ifndef STATIC
#define STATIC static
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,5,2)
#define kdev_val(dev)	(unsigned)(dev)
#endif

typedef struct statvfs {
	ulong_t	f_bsize;	/* fundamental file system block size */
	ulong_t	f_frsize;	/* fragment size */
	__uint64_t f_blocks;	/* total # of blocks of f_frsize on fs */
	__uint64_t f_bfree;	/* total # of free blocks of f_frsize */
	__uint64_t f_bavail;	/* # of free blocks avail to non-superuser */
	__uint64_t f_files;	/* total # of file nodes (inodes) */
	__uint64_t f_ffree;	/* total # of free file nodes */
	__uint64_t f_favail;	/* # of free nodes avail to non-superuser */
	ulong_t	f_namemax;	/* maximum file name length */
	ulong_t	f_fsid;		/* file system id (dev for now) */
	char	f_basetype[16];	/* target fs type name, null-terminated */
	char	f_fstr[32];	/* filesystem-specific string */
} statvfs_t;

typedef struct xfs_dirent {		/* data from readdir() */
	xfs_ino_t d_ino;	/* inode number of entry */
	xfs_off_t d_off;	/* offset of disk directory entry */
	unsigned short d_reclen;/* length of this record */
	char d_name[1];		/* name of file */
} xfs_dirent_t;

typedef struct xfs_dirent32 {	/* Irix5 view of dirent structure */
	app32_ulong_t d_ino;	/* inode number of entry */
	xfs32_off_t d_off;	/* offset of disk directory entry */
	unsigned short d_reclen;/* length of this record */
	char d_name[1];		/* name of file */
} xfs_dirent32_t;

#define GETDENTS_ABI(abi, uiop)	1
#define DIRENTBASESIZE		(((xfs_dirent_t *)0)->d_name - (char *)0)
#define DIRENTSIZE(namelen)	\
	((DIRENTBASESIZE + (namelen) + \
		sizeof(xfs_off_t)) & ~(sizeof(xfs_off_t) - 1))
#define DIRENT32BASESIZE	(((xfs_dirent32_t *)0)->d_name - (char *)0)
#define DIRENT32SIZE(namelen) \
	((DIRENT32BASESIZE + (namelen) + \
		sizeof(xfs32_off_t)) & ~(sizeof(xfs32_off_t) - 1))

#define ABI_IRIX5	0x02	/* an IRIX5/SVR4 ABI binary */
#define ABI_IRIX5_64	0x04	/* an IRIX5-64 bit binary */
#define ABI_IRIX5_N32	0x08	/* an IRIX5-32 bit binary (new abi) */

#define ABI_IS(set,abi)		(((set) & (abi)) != 0)
#define ABI_IS_IRIX5(abi)	(ABI_IS(ABI_IRIX5, abi))
#define ABI_IS_IRIX5_N32(abi)	(ABI_IS(ABI_IRIX5_N32, abi))
#define ABI_IS_IRIX5_64(abi)	(ABI_IS(ABI_IRIX5_64, abi))
#define ABI_IS_IRIX5_N32(abi)	(ABI_IS(ABI_IRIX5_N32, abi))
/* try 64 bit first */
#define get_current_abi()	ABI_IRIX5_64

#define NBPP		PAGE_SIZE 
#define DPPSHFT		(PAGE_SHIFT - 9)
#define NDPP		(1 << (PAGE_SHIFT - 9))
#define dtop(DD)	(((DD) + NDPP - 1) >> DPPSHFT)
#define dtopt(DD)	((DD) >> DPPSHFT)
#define dpoff(DD)	((DD) & (NDPP-1))

#define NBBY		8		/* number of bits per byte */
#define	NBPC		PAGE_SIZE	/* Number of bytes per click */
#define	BPCSHIFT	PAGE_SHIFT	/* LOG2(NBPC) if exact */

/*
 * Size of block device i/o is parameterized here.
 * Currently the system supports page-sized i/o.
 */
#define	BLKDEV_IOSHIFT		BPCSHIFT
#define	BLKDEV_IOSIZE		(1<<BLKDEV_IOSHIFT)
/* number of BB's per block device block */
#define	BLKDEV_BB		BTOBB(BLKDEV_IOSIZE)

/* bytes to clicks */
#define	btoc(x)		(((__psunsigned_t)(x)+(NBPC-1))>>BPCSHIFT)
#define	btoct(x)	((__psunsigned_t)(x)>>BPCSHIFT)
#define	btoc64(x)	(((__uint64_t)(x)+(NBPC-1))>>BPCSHIFT)
#define	btoct64(x)	((__uint64_t)(x)>>BPCSHIFT)
#define	io_btoc(x)	(((__psunsigned_t)(x)+(IO_NBPC-1))>>IO_BPCSHIFT)
#define	io_btoct(x)	((__psunsigned_t)(x)>>IO_BPCSHIFT)

/* off_t bytes to clicks */
#define offtoc(x)       (((__uint64_t)(x)+(NBPC-1))>>BPCSHIFT)
#define offtoct(x)      ((xfs_off_t)(x)>>BPCSHIFT)

/* clicks to off_t bytes */
#define	ctooff(x)	((xfs_off_t)(x)<<BPCSHIFT)

/* clicks to bytes */
#define	ctob(x)		((__psunsigned_t)(x)<<BPCSHIFT)
#define btoct(x)        ((__psunsigned_t)(x)>>BPCSHIFT)
#define	ctob64(x)	((__uint64_t)(x)<<BPCSHIFT)
#define	io_ctob(x)	((__psunsigned_t)(x)<<IO_BPCSHIFT)

/* bytes to clicks */
#define btoc(x)         (((__psunsigned_t)(x)+(NBPC-1))>>BPCSHIFT)

#ifndef CELL_CAPABLE
#define CELL_ONLY(x)
#define CELL_NOT(x)	(x)
#define CELL_IF(a, b)	(b)
#define CELL_MUST(a)   	ASSERT(0)
#define CELL_ASSERT(x)
#define FSC_NOTIFY_NAME_CHANGED(vp)
#endif

#ifndef ENOTSUP
#define ENOTSUP		EOPNOTSUPP	/* Not supported (POSIX 1003.1b) */
#endif

#ifndef ENOATTR
#define ENOATTR		ENODATA		/* Attribute not found */
#endif

/* Note: EWRONGFS never visible outside the kernel */
#define	EWRONGFS	EINVAL		/* Mount with wrong filesystem type */

/*
 * XXX EFSCORRUPTED needs a real value in errno.h. asm-i386/errno.h won't
 *     return codes out of its known range in errno.
 * XXX Also note: needs to be < 1000 and fairly unique on Linux (mustn't
 *     conflict with any code we use already or any code a driver may use)
 * XXX Some options (currently we do #2):
 *	1/ New error code ["Filesystem is corrupted", _after_ glibc updated]
 *	2/ 990 ["Unknown error 990"]
 *	3/ EUCLEAN ["Structure needs cleaning"]
 *	4/ Convert EFSCORRUPTED to EIO [just prior to return into userspace]
 */
#define EFSCORRUPTED    990		/* Filesystem is corrupted */

#define SYNCHRONIZE()	barrier()
#define lbolt		jiffies
#define rootdev		ROOT_DEV
#define __return_address __builtin_return_address(0)
#define LONGLONG_MAX	9223372036854775807LL	/* max "long long int" */
#define nopkg()		( ENOSYS )
#define getf(fd,fpp)	( printk("getf not implemented\n"), ASSERT(0), 0 )

/* IRIX uses a dynamic sizing algorithm (ndquot = 200 + numprocs*2) */
/* we may well need to fine-tune this if it ever becomes an issue.  */
#define DQUOT_MAX_HEURISTIC	1024	/* NR_DQUOTS */
#define ndquot			DQUOT_MAX_HEURISTIC

/* IRIX uses the current size of the name cache to guess a good value */
/* - this isn't the same but is a good enough starting point for now. */
#define DQUOT_HASH_HEURISTIC	files_stat.nr_files

/* IRIX inodes maintain the project ID also, zero this field on Linux */
#define DEFAULT_PROJID	0
#define dfltprid	DEFAULT_PROJID

#define MAXNAMELEN      256
#define	MAXPATHLEN	1024

#define	PSWP	0
#define PMEM	0
#define PINOD   10
#define PRIBIO  20

#define	PLTWAIT 0x288 /* O'01000' */
#define	PVFS	27

#define FREAD		0x01
#define FWRITE		0x02
#define FINVIS		0x0100	/* don't update timestamps - XFS */
#define FSOCKET		0x0200	/* open file refers to a vsocket */

#define MIN(a,b)	(min(a,b))
#define MAX(a,b)	(max(a,b))
#define howmany(x, y)   (((x)+((y)-1))/(y))
#define roundup(x, y)   ((((x)+((y)-1))/(y))*(y))

/* Move the kernel do_div definition off to one side */

#if defined __i386__
/* For ia32 we need to pull some tricks to get past various versions
 * of the compiler which do not like us using do_div in the middle
 * of large functions.
 */
static inline __u32 xfs_do_div(void *a, __u32 b, int n)
{
	__u32	mod;

	switch (n) {
		case 4:
			mod = *(__u32 *)a % b;
			*(__u32 *)a = *(__u32 *)a / b;
			return mod;
		case 8:
			{
			unsigned long __upper, __low, __high, __mod;
			__u64	c = *(__u64 *)a;
			__upper = __high = c >> 32;
			__low = c;
			if (__high) {
				__upper = __high % (b);
				__high = __high / (b);
			}
			asm("divl %2":"=a" (__low), "=d" (__mod):"rm" (b), "0" (__low), "1" (__upper));
			asm("":"=A" (c):"a" (__low),"d" (__high));
			*(__u64 *)a = c;
			return __mod;
			}
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
			unsigned long __upper, __low, __high, __mod;
			__u64	c = *(__u64 *)a;
			__upper = __high = c >> 32;
			__low = c;
			if (__high) {
				__upper = __high % (b);
				__high = __high / (b);
			}
			asm("divl %2":"=a" (__low), "=d" (__mod):"rm" (b), "0" (__low), "1" (__upper));
			asm("":"=A" (c):"a" (__low),"d" (__high));
			return __mod;
			}
	}

	/* NOTREACHED */
	return 0;
}
#else
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
#endif

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
