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

#define STATIC static 
#include <sys/types.h>

#ifndef SIM
#include <linux/string.h>   /* to get memcpy, and friends */
#endif

/* #include <sys/syslog.h> */

#include <asm/page.h>
#define _PAGESZ PAGE_SIZE
// #include <asm/semaphore.h> */
//#include <asm/spinlock.h>

#include <sys/ksa.h>
//#include <sys/uio.h>
//#include <stdarg.h>
//#include <time.h>


/* sys/errno.h */
#define EFSCORRUPTED    1010    /* Filesystem is corrupted */
#define ENOATTR         1009    /* Attribute not found */
#define	EWRONGFS	1011	/* Mount with wrong filesystem type */




#define ENTER(x) printk("Entering %s\n",x);
#define EXIT(x)  printk("Exiting  %s\n",x);

/* This is major wrong.... fix me FIX ME  RMC */
#ifndef __ARCH_I386_POSIX_TYPES_H
typedef long            __kernel_clock_t; 
#endif


#define stat64 stat

extern time_t          lbolt;                  /* time in HZ since last boot */


#define __return_address __builtin_return_address(0)

/* ksys/dvh.h */
#define	NPARTAB		16		/* 16 unix partitions */

#define LONGLONG_MAX        9223372036854775807LL /* max "long long int" */

/* stropts.h */
/*
 * Stream buffer structure for putmsg and getmsg system calls
 */
struct strbuf {
	int	maxlen;			/* no. of bytes in buffer */
	int	len;			/* no. of bytes returned */
	char	*buf;			/* pointer to data */
};

#define DIOCGETVOLDEV	_DIOC_(36)	/* NEW: retrieve subvolume devices */

#define NBPP _PAGESZ 

/* sys/kt
   ypes.h */
typedef long int            irix5_off_t;



#ifndef NBBY
#define NBBY    8       /* number of bits per byte */
#endif  /* NBBY */


#if defined(_KERNEL)
extern ksa_t ksa;
extern ksa_t *ksaptr;
#endif



/* XXX should be fairly private - see sys/ddmap.h */
struct __vhandl_s {
	struct pregion	*v_preg;	/* Pointer to the pregion.	*/
	uvaddr_t	v_addr;		/* Virtual address of region.	*/
};


#define findrawpath(x) x
#define findblockpath(x) x

#define dev_is_vertex(dev)	(emajor((dev_t)(dev)) == 0)

#define	get_bdevsw(dev)	((struct bdevsw *)(__psint_t)(dev))


#define bzero(p,s) memset((p), 0, (s))
#define bcopy(s,d,n) memcpy((d),(s),(n))

#define COPYIN_XLATE(from,to,size,func,abi,count) \
	copyin(from, to, size)

// extern void kmem_free(void *, size_t);
extern void *kern_malloc(size_t);
#define bcmp(s1,s2,l) memcmp(s1,s2,l)    

/* sys/ktime.h */
#if 0
struct timespec;
extern void nanotime_syscall(timespec_t *);
#endif

#endif /* __XFS_LINUX_ */
