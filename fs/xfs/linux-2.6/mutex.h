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
#ifndef __XFS_SUPPORT_MUTEX_H__
#define __XFS_SUPPORT_MUTEX_H__

/* semaphore.h pulls in kernel.h which has a linux version of sysinfo 
 * in our sysinfo we define sysinfo to be xfs_sysinfo 
 * we must now undef it so we can actually avoid the conflict we are 
 * trying to solve 
 */
#undef sysinfo 
#include <linux/version.h>
#include <linux/time.h>
#include <linux/wait.h>
#include <asm/atomic.h>
#include <asm/semaphore.h>

/*
 * Map the mutex'es from IRIX to Linux semaphores.
 *
 * Destroy just simply initializes to -99 which should block all other
 * callers.
 */

typedef struct mutex_s {
	struct semaphore sema;
	int state;
} mutex_t;


#define init_mutex(ptr, type, name, sequence) mutex_init(ptr, type, name)

void _mutex_init( mutex_t *mutex);
void _mutex_lock( mutex_t *mutex);
void _mutex_unlock( mutex_t *mutex);
int  _mutex_trylock( mutex_t *mutex);
void _mutex_destroy( mutex_t *mutex);

#define mutex_init(lock, type, name)	_mutex_init(lock)
#define mutex_lock(lock, num)		_mutex_lock(lock)
#define mutex_trylock(lock)		_mutex_trylock(lock)
#define mutex_unlock(lock)		_mutex_unlock(lock)
#define mutex_destroy(lock)		_mutex_destroy(lock)

/*
 * mp_mutex_spinlock is used in xfs_rw.c during an interrupt thread so it
 * must be irq safe. This happens when a write finishes and we queue
 * a request to xfsc to update the unwritten extent flag.
 * There may be other places.
 *
 * mutex_spinlock is also called extensively by interrupt threads.
 *
 * spin_lock_irqsave needs a long.
 * mutex_spinlock and mp_mutex_spinlock use ints. On i386 this should be OK.
 * since an int and long are both 4 bytes. Other architectures may be a problem.
 * We don't want to change all the code that interfaces with mutex_spinlock and
 * mp_mutex_spinlock to pass longs, but ....
 */

static __inline__ int mutex_spinlock(spinlock_t *l)
{
	long flags;
	spin_lock_irqsave(l, flags);
	return(flags);
}

#define mutex_spinunlock(lockp,flags)		spin_unlock_irqrestore(lockp, flags)

#define mp_mutex_spinlock(lockp)		mutex_spinlock(lockp)
#define mp_mutex_spinunlock(lockp,flags)	spin_unlock_irqrestore(lockp, flags)


/*
 * Types for mutex_init(), mutex_alloc()
 */
#define MUTEX_DEFAULT		0x0

/*
 * void mutex_init(mutex_t *mp, int type, char *name);
 * void	init_mutex(mutex_t *mp, int type, char *name, long sequence);
 *
 * Name may be null -- it is only used when metering mutexes are installed,
 * tag the metering structure with an ascii name.
 * Only METER_NAMSZ-1 characters are recorded.
 *
 * If init_mutex interface is used to initialize, metering name is
 * constructed from 'name' prefix and and ascii suffix generated from
 * the 'sequence' argument:  [ "MyLock", 12 ] becomes "MyLock00012"
 */


#endif /* __XFS_SUPPORT_MUTEX_H__ */
