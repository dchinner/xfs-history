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
 *	page_buf_locking.c
 *
 *	The page_buf module provides an abstract buffer cache model on top of
 *	the Linux page cache.  Cached blocks for a file are hashed to the
 *	inode for that file, and can be held dirty in delayed write mode in
 *	the page cache.  Cached metadata blocks for a file system are hashed
 *	to the inode for the mounted device.  The page_buf module assembles
 *	buffer (page_buf_t) objects on demand to aggregate such cached pages
 *	for I/O.  The page_buf_locking module adds support for locking such
 *      page buffers.
 *
 *      Written by William J. Earl and Steve Lord at SGI 
 *
 *
 */

#include <linux/module.h>
#include <linux/stddef.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/bitops.h>
#include <linux/string.h>
#include <linux/pagemap.h>
#include <linux/init.h>

#include "avl.h"
#include "page_buf.h"
#define _PAGE_BUF_INTERNAL_
#define PB_DEFINE_TRACES
#include "page_buf_trace.h"

/*
 *	Locking model:
 *
 *	Buffers associated with inodes for which buffer locking
 *	is not enabled are not protected by semaphores, and are
 *	assumed to be exclusively owned by the caller.  There is
 *	spinlock in the buffer, for use by the caller when concurrent
 *	access is possible.
 *
 *	Buffers asociated with inodes for which buffer locking is
 *	enabled are protected by semaphores in the page_buf_lockable_t
 *	structure, but only between different callers.  For a given
 *	caller, the buffer is exclusively owned by the caller, but
 *	the caller must still use the spinlock when concurrent access
 *	is possible.
 *
 *	Internally, when implementing buffer locking, page_buf uses
 *	a rwlock_t to protect the pagebuf_registered_inodes tree,
 *	a spinlock_t to protect the buffer tree associated with an inode,
 *	as well as to protect the hold count in the page_buf_lockable_t.
 *	The locking order is the pagebuf_registered_inodes tree lock
 *	first, then the page_buf_registration_t lock.  The semaphore
 *	in the page_buf_lockable_t should be acquired only after acquiring
 *	a hold on the page_buf_lockable_t (and of course releasing the
 *	page_buf_registration_t spinlock_t).
 */

static kmem_cache_t *pagebuf_registration_cache = NULL;


/*
 *	Initialization and Termination
 */

/*
 *	pagebuf_locking_init
 */

int __init pagebuf_locking_init(void)
{
	if (pagebuf_registration_cache == NULL) {
		pagebuf_registration_cache = kmem_cache_create("page_buf_reg_t",
						  sizeof(pb_target_t),
						  0,
						  SLAB_HWCACHE_ALIGN,
						  NULL,
						  NULL);
		if (pagebuf_registration_cache == NULL)
			return(-ENOMEM);
	}

	return(0);
}


/*
 *	Buffer Locking Control
 */

/*
 *	_pagebuf_registration_free
 *
 *	Free a page_buf_registration_t object.  The caller must hold
 *	the pagebuf_registered_inodes_lock.
 */

static void
_pagebuf_registration_free(pb_target_t *target)
{
	if (target != NULL) {
		if (target->pbr_buffers != NULL) 
			avl_destroy(target->pbr_buffers);
		kmem_cache_free(pagebuf_registration_cache, target);
	}
}


int
_pagebuf_free_lockable_buffer(page_buf_t *pb, unsigned long flags)
{
	pb_target_t *target = pb->pb_target;
	int	status;

	PB_TRACE(pb, PB_TRACE_REC(free_lk), 0);

	spin_lock(&target->pbr_lock);
	spin_lock(&PBP(pb)->pb_lock);
	status = (avl_delete(target->pbr_buffers,
			  (avl_key_t) pb, (avl_value_t) pb));

	spin_unlock_irqrestore(&target->pbr_lock, flags);

	PB_TRACE(pb, PB_TRACE_REC(freed_l), status);

	return status;
}



/*
 *	_pagebuf_lockable_compare
 */

static int
_pagebuf_lockable_compare_key(avl_key_t key_a,
			      avl_key_t key_b)
{
	page_buf_t *pb_a = (page_buf_t *) key_a;
	page_buf_t *pb_b = (page_buf_t *) key_b;
	int	ret;

	if (pb_b == NULL) {
		if (pb_a == NULL)
			return(0);
		else
			return(-1);
	}

	//assert(pb_a->pb_target == pb_b->pb_target);
	if (pb_a->pb_file_offset == pb_b->pb_file_offset)
		ret = 0;
	else if (pb_a->pb_file_offset < pb_b->pb_file_offset)
		ret = -1;
	else
		ret = 1;

	return ret;
}

/*
 *	_pagebuf_lockable_increment_key
 */

static void
_pagebuf_lockable_increment_key(avl_key_t *next_key,avl_key_t key)
{
	page_buf_t *next_pb = (page_buf_t *) (*next_key);
	page_buf_t *pb = (page_buf_t *) key;

	assert((next_pb != NULL) && \
	       (next_pb->pb_flags & _PBF_NEXT_KEY) && \
	       (pb != NULL));
	
	next_pb->pb_file_offset = pb->pb_file_offset + pb->pb_buffer_length;
}


/*
 *	_pagebuf_get_lockable_buffer
 *
 *	Looks up, and creates if absent, a lockable buffer for
 * 	a given range of an inode.  The buffer is returned
 *	locked.  If other overlapping buffers exist, they are
 *	released before the new buffer is created and locked,
 *	which may imply that this call will block until those buffers
 *	are unlocked.  No I/O is implied by this call.
 *
 *	The caller must have previously called _pagebuf_check_lockable()
 *	successfully, and must pass in the page_buf_registration_t pointer
 *	obtained via that call, with the pbr_lock spinlock held and interrupts
 *	disabled.
 */

int
_pagebuf_find_lockable_buffer(pb_target_t *target,
			     loff_t range_base,
			     size_t range_length,
			     page_buf_flags_t flags,
			     page_buf_t **pb_p)
{
	page_buf_t next_key_buf;
	page_buf_t *pb;
	avl_key_t next_key;
	avl_key_t key;
	avl_value_t value;
	int	not_locked;

	next_key_buf.pb_flags = _PBF_NEXT_KEY;
	next_key_buf.pb_file_offset = range_base;
	next_key_buf.pb_buffer_length = range_length;
	next_key = (avl_key_t) &next_key_buf;
	while (avl_lookup_next(target->pbr_buffers,
			       &next_key,
			       &key,
			       &value) == 0) {
		pb = (page_buf_t *)value;
		assert(pb != NULL);

		if (pb->pb_file_offset >= (range_base + range_length))
			break;	/* no overlap found - allocate buffer */

		if (pb->pb_flags & PBF_FREED)
			continue;

		spin_lock(&PBP(pb)->pb_lock);
		if (pb->pb_flags & PBF_FREED) {
			spin_unlock(&PBP(pb)->pb_lock);
			continue;
		}
		pb->pb_hold++;

		PB_TRACE(pb, PB_TRACE_REC(avl_ret), 0);

		/* Attempt to get the semaphore without sleeping,
		 * if this does not work then we need to drop the
		 * spinlocks and do a hard attempt on the semaphore.
		 */
		not_locked = down_trylock(&PBP(pb)->pb_sema);
		if (not_locked) {
			spin_unlock(&PBP(pb)->pb_lock);
			spin_unlock_irq(&(target->pbr_lock));

			if (!(flags & PBF_TRYLOCK)) {
				/* wait for buffer ownership */
				PB_TRACE(pb, PB_TRACE_REC(get_lk), 0);
				pagebuf_lock(pb);
				PB_STATS_INC(pbstats.pb_get_locked_waited);
			} else {
				/* We asked for a trylock and failed, no need
				 * to look at file offset and length here, we
				 * know that this pagebuf at least overlaps our
				 * pagebuf and is locked, therefore our buffer
				 * either does not exist, or is this buffer
				 */

				if (down_trylock(&PBP(pb)->pb_sema)) {
					pagebuf_rele(pb);

					PB_STATS_INC(pbstats.pb_busy_locked);
					return -EBUSY;
				}
				PB_SET_OWNER(pb);
			}
		} else {
			/* trylock worked */
			PB_SET_OWNER(pb);
			spin_unlock(&target->pbr_lock);
		}


		if (pb->pb_file_offset == range_base &&
		    pb->pb_buffer_length == range_length) {
			if (not_locked)
				spin_lock_irq(&PBP(pb)->pb_lock);
			if (!(pb->pb_flags & PBF_FREED)) {
				if (pb->pb_flags & PBF_STALE)
					pb->pb_flags &=	PBF_MAPPABLE | \
						PBF_MAPPED | \
						_PBF_LOCKABLE | \
						_PBF_ALL_PAGES_MAPPED | \
						_PBF_SOME_INVALID_PAGES | \
						_PBF_ADDR_ALLOCATED | \
						_PBF_MEM_ALLOCATED;
				spin_unlock_irq(&PBP(pb)->pb_lock);
				PB_TRACE(pb, PB_TRACE_REC(got_lk), 0);
				*pb_p = pb;
				PB_STATS_INC(pbstats.pb_get_locked);
				return(0);
			}
			spin_unlock_irq(&PBP(pb)->pb_lock);
		} else if (!not_locked) {
			spin_unlock_irq(&PBP(pb)->pb_lock);
		}

		/* Let go of the buffer - if the count goes to zero
		 * this will remove it from the tree. The problem here
		 * is that if there is a hold on the pagebuf without a
		 * lock then we just threw away the contents....
		 * Which means that if someone else comes along and
		 * locks the pagebuf which they have a hold on they
		 * can discover that the memory has gone away on them.
		 */
		PB_CLEAR_OWNER(pb);
		PB_TRACE(pb, PB_TRACE_REC(skip), 0);
		up(&PBP(pb)->pb_sema);
		if (pb->pb_flags & PBF_STALE) {
			pagebuf_rele(pb);
			continue;
		}

		pagebuf_rele(pb);

		return -EBUSY;
	}

	spin_unlock_irq(&target->pbr_lock);

	/* No match found */
	PB_STATS_INC(pbstats.pb_miss_locked);
	*pb_p = NULL;
	return 0;
}

int
_pagebuf_get_lockable_buffer(pb_target_t *target,
			     loff_t range_base,
			     size_t range_length,
			     page_buf_flags_t flags,
			     page_buf_t **pb_p)
{
	int	status;
	page_buf_t *pb;

retry_scan:
	spin_lock_irq(&target->pbr_lock);
	status = _pagebuf_find_lockable_buffer(target, range_base,
				range_length, flags, pb_p);

	if (status)
		return status;

	if (*pb_p)
		return 0;


	status = _pagebuf_get_object(target, range_base, range_length,
				     flags | _PBF_LOCKABLE, &pb);
	if (status != 0) {
		return(status);
	}


	/* Tree manipulation requires the registration spinlock */
	spin_lock_irq(&target->pbr_lock);
	status = avl_insert(target->pbr_buffers,
			    (avl_key_t) pb,
			    (avl_value_t) pb);
	spin_unlock_irq(&target->pbr_lock);
	PB_TRACE(pb, PB_TRACE_REC(avl_ins), status);
	if (status != 0) {
		unsigned long flags;

		spin_lock_irqsave(&PBP(pb)->pb_lock, flags);
		pb->pb_flags &= ~_PBF_LOCKABLE;	/* we are not in the avl */
		_pagebuf_free_object(pb, flags);
		if (status == -EEXIST) {
			/* Race condition with another thread - try again,
			 * set up locking state first.
			 */
			*pb_p = NULL;

			goto retry_scan;
		}
		return(status);
	}

	*pb_p = pb;
	return(0);
}

/*
 *	Locking and Unlocking Buffers 
 */

/*
 *	pagebuf_cond_lock
 *
 *	pagebuf_cond_lock locks a buffer object, if it is not already locked.
 *	Note that this in no way
 *	locks the underlying pages, so it is only useful for synchronizing
 *	concurrent use of page buffer objects, not for synchronizing independent
 *	access to the underlying pages.
 */

int
pagebuf_cond_lock(  		        /* lock buffer, if not locked   */
					/* returns -EBUSY if locked)    */
		  page_buf_t *pb) 	/* buffer to lock               */
{
	int	locked;

	assert(pb->pb_flags & _PBF_LOCKABLE);

	locked = down_trylock(&PBP(pb)->pb_sema) == 0;
	if (locked) {
		PB_SET_OWNER(pb);
	}

	PB_TRACE(pb, PB_TRACE_REC(condlck), locked);

	return(locked ? 0 : -EBUSY);
}


/*
 *	pagebuf_is_locked
 *
 *	pagebuf_is_locked tests if the buffer is locked, return 1 if locked
 *	and 0 if not.  This routine is useful only for assertions that
 *	the buffer is locked, since the state could change at any time
 *	if the buffer is not locked.
 */

int
pagebuf_is_locked(			/* test if buffer is locked	*/
		  page_buf_t *pb) 	/* buffer to test               */
{
	assert(pb->pb_flags & _PBF_LOCKABLE);

	return(atomic_read(&PBP(pb)->pb_sema.count) <= 0 );
}

/*
 *	pagebuf_lock_value
 *
 *	Return lock value for a pagebuf
 */

int
pagebuf_lock_value(page_buf_t *pb)
{
	assert(pb->pb_flags & _PBF_LOCKABLE);

	return(atomic_read(&PBP(pb)->pb_sema.count));
}



/*
 *	pagebuf_lock
 *
 *	pagebuf_lock locks a buffer object.  Note that this in no way
 *	locks the underlying pages, so it is only useful for synchronizing
 *	concurrent use of page buffer objects, not for synchronizing independent
 *	access to the underlying pages.
 */

int
pagebuf_lock(                        	/* lock buffer                  */
	     page_buf_t *pb)            /* buffer to lock               */
{
	assert(pb->pb_flags & _PBF_LOCKABLE);

	PB_TRACE(pb, PB_TRACE_REC(lock), 0);
	if (atomic_read(&PBP(pb)->pb_io_remaining))
		run_task_queue(&tq_disk);
	down(&PBP(pb)->pb_sema);
	PB_SET_OWNER(pb);
	PB_TRACE(pb, PB_TRACE_REC(locked), 0);
	return(0);
}


/*
 *	pagebuf_lock_disable
 *
 *	pagebuf_lock_disable disables buffer object locking for an inode.
 *	This call fails with -EBUSY if buffers are still in use and locked for
 *	this inode.
 */

int
pagebuf_lock_disable(			/* disable buffer locking	*/
		     pb_target_t *target)  /* inode for buffers	        */
{
	avl_key_t next_key;
	avl_key_t key;
	avl_value_t value;

	spin_lock_irq(&target->pbr_lock);
	if (target->pbr_buffers != NULL) {
		next_key = 0;
		if (avl_lookup_next(target->pbr_buffers,
				    &next_key,
				    &key,
				    &value) == 0) {
			spin_unlock_irq(&target->pbr_lock);
			return(-EBUSY);
		}
	}
	destroy_buffers(target->pbr_device);
	truncate_inode_pages(target->pbr_inode->i_mapping, 0LL);
	_pagebuf_registration_free(target);
	local_irq_enable();
	MOD_DEC_USE_COUNT;

	return(0);
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,4,13)
static struct address_space_operations pagebuf_aops = {
	sync_page:	block_sync_page,
};
#endif

/*
 *	pagebuf_lock_enable
 *
 *	pagebuf_lock_enable enables buffer object locking for an inode.
 *	This call fails with -EBUSY if buffers are in use for this inode.
 */

pb_target_t *
pagebuf_lock_enable(
	kdev_t kdev,
	struct super_block *sb)
{
	int	status = 0;
	pb_target_t	*target;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,4,13)
	struct block_device *bdev = bdget(kdev);

	if (!bdev)
		return NULL;
#endif

	target = kmem_cache_zalloc(pagebuf_registration_cache,
						SLAB_KERNEL);
	if (target == NULL) {
		return(NULL);
	}
	spin_lock_init(&target->pbr_lock);
	target->pbr_device = kdev;
	target->pbr_sector = sb->s_blocksize;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,4,13)
	target->pbr_inode = bdev->bd_inode;
	bdput(bdev);
#else
	INIT_LIST_HEAD(&target->pbr_addrspace.clean_pages);
	INIT_LIST_HEAD(&target->pbr_addrspace.dirty_pages);
	INIT_LIST_HEAD(&target->pbr_addrspace.locked_pages);
	spin_lock_init(&target->pbr_addrspace.i_shared_lock);
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,4,4)
	/* Also needed for AC kernels */
	spin_lock_init(&target->pbr_addrspace.page_lock);
#endif
	target->pbr_addrspace.a_ops = &pagebuf_aops;
#endif

	status = avl_create(&target->pbr_buffers,
			    avl_opt_nolock,
			    _pagebuf_lockable_compare_key,
			    _pagebuf_lockable_increment_key);
	if (status) {
		_pagebuf_registration_free(target);
		return(NULL);
	}
	MOD_INC_USE_COUNT;

	return(target);
}

void
pagebuf_target_blocksize(
	pb_target_t     *target,
	unsigned int    blocksize)
{
	target->pbr_blocksize = blocksize;
	target->pbr_blocksize_bits = (unsigned short) ffs(blocksize) - 1;
}

int
pagebuf_target_clear(
	pb_target_t	*target)
{
	destroy_buffers(target->pbr_device);
	truncate_inode_pages(target->pbr_inode->i_mapping, 0LL);
	return 0;
}


/*
 *	pagebuf_unlock
 *
 *	pagebuf_unlock releases the lock on the buffer object created by
 *	pagebuf_lock or pagebuf_cond_lock (not any
 *	pinning of underlying pages created by pagebuf_pin).
 */

void
pagebuf_unlock(                     	/* unlock buffer                */
	       page_buf_t *pb)          /* buffer to unlock             */
{
	assert(pb->pb_flags & _PBF_LOCKABLE);

	PB_CLEAR_OWNER(pb);
	up(&PBP(pb)->pb_sema);
	PB_TRACE(pb, PB_TRACE_REC(unlock), 0);
}


/*
 *	pagebuf_terminate_locking.  Do not define as __exit, it is called from
 *	pagebuf_terminate.
 */

void pagebuf_locking_terminate(void)
{
	if (pagebuf_registration_cache != NULL)
		kmem_cache_destroy(pagebuf_registration_cache);
}


/*
 *	Module management
 */

EXPORT_SYMBOL(pagebuf_cond_lock);
EXPORT_SYMBOL(pagebuf_lock);
EXPORT_SYMBOL(pagebuf_is_locked);
EXPORT_SYMBOL(pagebuf_lock_value);
EXPORT_SYMBOL(pagebuf_lock_disable);
EXPORT_SYMBOL(pagebuf_lock_enable);
EXPORT_SYMBOL(pagebuf_unlock);
EXPORT_SYMBOL(pagebuf_target_blocksize);
EXPORT_SYMBOL(pagebuf_target_clear);
