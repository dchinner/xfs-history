/*
 * Copyright (c) 2000-2002 Silicon Graphics, Inc.  All Rights Reserved.
 * Portions Copyright (c) 2002 Christoph Hellwig.  All Rights Reserved.
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
 * or the like.	 Any license provided herein, whether implied or
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
 *	the page cache.	 Cached metadata blocks for a file system are hashed
 *	to the inode for the mounted device.  The page_buf module assembles
 *	buffer (page_buf_t) objects on demand to aggregate such cached pages
 *	for I/O.  The page_buf_locking module adds support for locking such
 *	page buffers.
 *
 *	Written by Steve Lord at SGI
 *
 *
 */

#include <linux/stddef.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/bitops.h>
#include <linux/string.h>
#include <linux/pagemap.h>
#include <linux/init.h>
#include <linux/major.h>

#include "page_buf_internal.h"

#ifndef EVMS_MAJOR
#define EVMS_MAJOR      117
#endif

pb_hash_t	pbhash[NHASH];

/*
 *	Initialization and Termination
 */

/*
 * Hash calculation
 */

static int
_bhash(kdev_t d, loff_t b)
{
	int bit, hval;

	b >>= 9;
	/*
	 * dev_t is 32 bits, daddr_t is always 64 bits
	 */
	b ^= kdev_val(d);
	for (bit = hval = 0; b != 0 && bit < sizeof(b) * 8; bit += NBITS) {
		hval ^= (int)b & (NHASH-1);
		b >>= NBITS;
	}
	return hval;
}


/*
 *	Buffer Locking Control
 */

/*
 *	_pagebuf_get_lockable_buffer
 *
 *	Looks up, and creates if absent, a lockable buffer for
 *	a given range of an inode.  The buffer is returned
 *	locked.	 If other overlapping buffers exist, they are
 *	released before the new buffer is created and locked,
 *	which may imply that this call will block until those buffers
 *	are unlocked.  No I/O is implied by this call.
 */

int
_pagebuf_find_lockable_buffer(pb_target_t *target,
			     loff_t range_base,
			     size_t range_length,
			     page_buf_flags_t flags,
			     page_buf_t **pb_p,
			     page_buf_t *new_pb)
{
	int			hval = _bhash(target->pbr_device, range_base);
	pb_hash_t		*h = &pbhash[hval];
	struct list_head	*p;
	page_buf_t		*pb;
	int			not_locked;

	spin_lock(&h->pb_hash_lock);
	list_for_each(p, &h->pb_hash) {
		pb = list_entry(p, page_buf_t, pb_hash_list);

		if ((target == pb->pb_target) &&
		    (pb->pb_file_offset == range_base) &&
		    (pb->pb_buffer_length == range_length)) {
			if (pb->pb_flags & PBF_FREED)
				break;
			/* If we look at something bring it to the
			 * front of the list for next time
			 */
			list_del(&pb->pb_hash_list);
			list_add(&pb->pb_hash_list, &h->pb_hash);
			goto found;
		}
	}

	/* No match found */
	if (new_pb) {
		_pagebuf_initialize(new_pb, target, range_base,
				range_length, flags | _PBF_LOCKABLE);
		new_pb->pb_hash_index = hval;
		h->pb_count++;
		list_add(&new_pb->pb_hash_list, &h->pb_hash);
	} else {
		PB_STATS_INC(pbstats.pb_miss_locked);
	}

	spin_unlock(&h->pb_hash_lock);
	*pb_p = new_pb;
	return 0;

found:
	atomic_inc(&pb->pb_hold);
	spin_unlock(&h->pb_hash_lock);

	/* Attempt to get the semaphore without sleeping,
	 * if this does not work then we need to drop the
	 * spinlock and do a hard attempt on the semaphore.
	 */
	not_locked = down_trylock(&PBP(pb)->pb_sema);
	if (not_locked) {
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

			pagebuf_rele(pb);
			PB_STATS_INC(pbstats.pb_busy_locked);
			return -EBUSY;
		}
	} else {
		/* trylock worked */
		PB_SET_OWNER(pb);
	}

	if (pb->pb_flags & PBF_STALE)
		pb->pb_flags &= PBF_MAPPABLE | \
				PBF_MAPPED | \
				_PBF_LOCKABLE | \
				_PBF_ALL_PAGES_MAPPED | \
				_PBF_SOME_INVALID_PAGES | \
				_PBF_ADDR_ALLOCATED | \
				_PBF_MEM_ALLOCATED;
	PB_TRACE(pb, PB_TRACE_REC(got_lk), 0);
	*pb_p = pb;
	PB_STATS_INC(pbstats.pb_get_locked);
	return(0);
}

int
_pagebuf_get_lockable_buffer(pb_target_t *target,
			     loff_t range_base,
			     size_t range_length,
			     page_buf_flags_t flags,
			     page_buf_t **pb_p)
{
	int	status;
	page_buf_t *pb = __pagebuf_allocate(flags);

	status = _pagebuf_find_lockable_buffer(target, range_base,
				range_length, flags, pb_p, pb);

	if (status) {
		kmem_cache_free(pagebuf_cache, pb);
		return status;
	}

	if (*pb_p != pb) {
		kmem_cache_free(pagebuf_cache, pb);
	}

	return 0;
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
pagebuf_cond_lock(			/* lock buffer, if not locked	*/
					/* returns -EBUSY if locked)	*/
		  page_buf_t *pb)	/* buffer to lock		*/
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
		  page_buf_t *pb)	/* buffer to test		*/
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
pagebuf_lock(				/* lock buffer			*/
	     page_buf_t *pb)		/* buffer to lock		*/
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
 */

int
pagebuf_lock_disable(			/* disable buffer locking	*/
		     pb_target_t *target)  /* inode for buffers		*/
{
	bdput(target->pbr_bdev);
	kfree(target);

	return(0);
}
/*
 *	pagebuf_lock_enable
 */

pb_target_t *
pagebuf_lock_enable(
	kdev_t kdev,
	struct super_block *sb)
{
	pb_target_t	*target;

	target = kmalloc(sizeof(pb_target_t), GFP_KERNEL);
	if (target) {
		target->pbr_bdev = bdget(kdev_t_to_nr(kdev));
		if (!target->pbr_bdev)
			goto fail;
		target->pbr_device = kdev;
		pagebuf_target_blocksize(target, PAGE_CACHE_SIZE);
		target->pbr_mapping = target->pbr_bdev->bd_inode->i_mapping;
		if ((major(kdev) == MD_MAJOR) || (major(kdev) == EVMS_MAJOR)) 
			target->pbr_flags = PBR_ALIGNED_ONLY;
		else if (major(kdev) == LVM_BLK_MAJOR)
			target->pbr_flags = PBR_SECTOR_ONLY;
		else
			target->pbr_flags = 0;
	}

	return target;

fail:
	kfree(target);
	return NULL;
}

void
pagebuf_target_blocksize(
	pb_target_t	*target,
	unsigned int	blocksize)
{
	target->pbr_blocksize = blocksize;
	target->pbr_blocksize_bits = ffs(blocksize) - 1;
}

int
pagebuf_target_clear(
	pb_target_t	*target)
{
	destroy_buffers(target->pbr_device);
	truncate_inode_pages(PBT_ADDR_SPACE(target), 0LL);
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
pagebuf_unlock(				/* unlock buffer		*/
	       page_buf_t *pb)		/* buffer to unlock		*/
{
	assert(pb->pb_flags & _PBF_LOCKABLE);

	PB_CLEAR_OWNER(pb);
	up(&PBP(pb)->pb_sema);
	PB_TRACE(pb, PB_TRACE_REC(unlock), 0);
}

/*
 *	pagebuf_locking_init
 */

int __init pagebuf_locking_init(void)
{
	int i;

	for (i = 0; i < NHASH; i++) {
		spin_lock_init(&pbhash[i].pb_hash_lock);
		INIT_LIST_HEAD(&pbhash[i].pb_hash);
	}

	return 0;
}

