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

#include <support/kmem.h>
#include <support/debug.h>

#include <linux/locks.h>
#include <linux/smp_lock.h>
#include <linux/time.h>
#include <linux/vmalloc.h>
#include <linux/slab.h>
#include <linux/module.h> /* for EXPORT_SYMBOL */

static __inline__ unsigned int flag_convert(int flags)
{
	return (flags & KM_NOSLEEP) ? GFP_ATOMIC : GFP_KERNEL;
}


#define	SHAKE_COUNT 16

static __inline__ void kmem_shake(int count, int line)
{
	int	shaker = 0;			/* Extreme prune */

	if (count > 1)
		shaker = (SHAKE_COUNT * 256) / count;

	/*
	 * Try pruning the i/dcache to free up some space ...
	 */
	printk("kmem_shake(%4d): pruning i/dcache @ line #%d[%d]\n",
						shaker, line, count);

	lock_kernel();

	prune_dcache(shaker);
	prune_icache(shaker);

	unlock_kernel();
}

static __inline__ void kmem_shake_zone(xfs_zone_t *zone, int count, int line)
{
	int	shaker = 0;			/* Extreme prune */

	if (count > 1)
		shaker = (SHAKE_COUNT * 256) / count;

	/*
	 * Try pruning the i/dcache to free up some space ...
	 */
	printk("kmem_shake_zone(%4d): pruning zone 0x%p @ line #%d[%d]\n",
						shaker, zone, line, count);
	lock_kernel();

	prune_dcache(shaker);
	prune_icache(shaker);

	unlock_kernel();
}


#define MAX_SLAB_SIZE 0x20000

/* ARGSUSED */
void *
kmem_alloc(size_t size, int flags)
{
	int	shrink = SHAKE_COUNT;	/* # times to try to shrink cache */
	void	*rval;

repeat:
	if (MAX_SLAB_SIZE < size) {
				/* not fully debug... leave for the moment */
		printk("kmem_alloc doing a vmalloc %d size & PAGE_SIZE %ld",
						size, size & PAGE_SIZE);
		rval = vmalloc(size);
		printk(" rval=0x%p\n",rval);
	} else {
		rval = kmalloc(size, flag_convert(flags));
	}

	if (rval || (flags & KM_NOSLEEP))
		return rval;

	/*
	 * KM_SLEEP callers don't expect a failure
	 */
	if (shrink) {
		kmem_shake(shrink, __LINE__);

		shrink--;
		goto repeat;
	}

	if (flags & KM_SLEEP)
		panic("kmem_alloc: NULL memory on KM_SLEEP request!");

	return NULL;
}

/* ARGSUSED */
void *
kmem_zalloc(size_t size, int flags)
{
	void	*ptr;

	ptr = kmem_alloc(size, flags); 

	if (ptr)
		memset((char *)ptr, 0, (int)size);

	return (ptr);
}

/* ARGSUSED */
void
kmem_free(void *ptr, size_t size)
{
	if (MAX_SLAB_SIZE < size){
			/* this isn't fully debug... leave in for now */
		printk("Size %d doing a vfree 0x%p\n",size,ptr);
		vfree(ptr);
	} else {
		kfree(ptr);
	}
}

/* ARGSUSED */
void *
kmem_realloc(void *ptr, size_t newsize, size_t oldsize, int flags)
{
	void *new;

	new = kmem_alloc(newsize, flags);
	if (ptr) {
		memcpy(new, ptr, ((oldsize < newsize) ? oldsize : newsize));
		kmem_free(ptr, oldsize);
	}

	return new;
}

#ifdef	XFSDEBUG
void
kmem_check(void)
{
	void *ptr;

	ptr = kmem_alloc(sizeof(ptr), KM_SLEEP);

	ASSERT(ptr);

	kmem_free(ptr, sizeof(ptr));
}
#endif	/* XFSDEBUG */

xfs_zone_t *
kmem_zone_init(int size, char *zone_name)
{
	xfs_zone_t *rval = NULL;

	rval = kmem_cache_create(zone_name, size, 0, 0, NULL, NULL);
	return rval;
}

void *
kmem_zone_alloc(xfs_zone_t *zone, int flags)
{
	int	shrink = SHAKE_COUNT;	/* # times to try to shrink cache */
	void	*ptr = NULL;

repeat:
	ptr = kmem_cache_alloc(zone, flag_convert(flags));

	if (ptr || (flags & KM_NOSLEEP))
		return ptr;

	/*
	 * KM_SLEEP callers don't expect a failure
	 */
	if (shrink) {
		kmem_shake_zone(zone, shrink, __LINE__);

		shrink--;
		goto repeat;
	}

	if (flags & KM_SLEEP)
		panic("kmem_zone_alloc: NULL memory on KM_SLEEP request!");

	return NULL;
}

void *
kmem_zone_zalloc(xfs_zone_t *zone, int flags)
{
	int	shrink = SHAKE_COUNT;	/* # times to try to shrink cache */
	void	*ptr = NULL;

repeat:
	ptr = kmem_cache_zalloc(zone, flag_convert(flags));

	if (ptr || (flags & KM_NOSLEEP))
		return ptr;

	/*
	 * KM_SLEEP callers don't expect a failure
	 */
	if (shrink) {
		kmem_shake_zone(zone, shrink, __LINE__);

		shrink--;
		goto repeat;
	}

	if (flags & KM_SLEEP)
		panic("kmem_zone_zalloc: NULL memory on KM_SLEEP request!");

	return NULL;
}

void
kmem_zone_free(xfs_zone_t *zone, void *ptr)
{
	kmem_cache_free(zone, ptr);
}

EXPORT_SYMBOL(kmem_zone_init);
EXPORT_SYMBOL(kmem_zone_zalloc);
EXPORT_SYMBOL(kmem_zone_alloc);
EXPORT_SYMBOL(kmem_zone_free);
EXPORT_SYMBOL(kmem_alloc);
EXPORT_SYMBOL(kmem_realloc);
EXPORT_SYMBOL(kmem_zalloc);
EXPORT_SYMBOL(kmem_free);
