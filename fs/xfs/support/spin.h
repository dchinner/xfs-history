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
#ifndef __XFS_SUPPORT_SPIN_H__
#define __XFS_SUPPORT_SPIN_H__

#include <linux/version.h>
#include <linux/time.h>
#include <linux/wait.h>
#include <asm/atomic.h>
#include <asm/semaphore.h>
#include <linux/spinlock.h>

/*
 * Map the spinlocks from IRIX to Linux.
 * For now, make them all as if they were used in interrupt thread (irq safe).
 */
#define spinlock_init(lock, name)	spin_lock_init(lock)
#define init_spinlock(lock, name, ll)	spin_lock_init(lock)
#define	spinlock_destroy(lock)

/*
 * Map lock_t from IRIX to Linux spinlocks.
 *
 * Note that linux turns on/off spinlocks depending on CONFIG_SMP.
 * We don't need to worry about SMP or not here.
 *
 * The irq safe calls get mapped from spinlocks and IRQ safe calls
 * to just spls.
 */

typedef spinlock_t lock_t;

#define nested_spinunlock(a)	spin_unlock(a)
#define nested_spinlock(a)	spin_lock(a)
#define nested_spintrylock(a)	spin_trylock(a)

#endif /* __XFS_SUPPORT_SPIN_H__ */
