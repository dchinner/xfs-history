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

void
pagebuf_lock_disable(			/* disable buffer locking	*/
		     pb_target_t *target)  /* inode for buffers		*/
{
	pagebuf_delwri_flush(target, PBDF_WAIT, NULL);
	blkdev_put(target->pbr_bdev, BDEV_FS);
	kfree(target);
}

/*
 *	pagebuf_lock_enable
 */
pb_target_t *
pagebuf_lock_enable(
	dev_t			dev,
	struct super_block	*sb)
{
	struct block_device *bdev;
	pb_target_t	*target;
	int		error = -ENOMEM;

	target = kmalloc(sizeof(pb_target_t), GFP_KERNEL);
	if (unlikely(!target))
		return ERR_PTR(error);

	bdev = bdget(dev);
	if (unlikely(!bdev))
		goto fail;

	error = blkdev_get(bdev, FMODE_READ|FMODE_WRITE, 0, BDEV_FS);
	if (unlikely(error))
		goto fail;

	target->pbr_dev = dev;
	target->pbr_kdev = to_kdev_t(dev);
	target->pbr_bdev = bdev;
	target->pbr_mapping = bdev->bd_inode->i_mapping;

	pagebuf_target_blocksize(target, PAGE_CACHE_SIZE);
	
	if ((MAJOR(dev) == MD_MAJOR) || (MAJOR(dev) == EVMS_MAJOR))
		target->pbr_flags = PBR_ALIGNED_ONLY;
	else if (MAJOR(dev) == LVM_BLK_MAJOR)
		target->pbr_flags = PBR_SECTOR_ONLY;
	else
		target->pbr_flags = 0;

	return target;

fail:
	kfree(target);
	return ERR_PTR(error);
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
	destroy_buffers(target->pbr_kdev);
	truncate_inode_pages(target->pbr_mapping, 0LL);
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
