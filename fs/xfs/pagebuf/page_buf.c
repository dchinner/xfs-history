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
 *	page_buf.c
 *
 *	The page_buf module provides an abstract buffer cache model on top of
 *	the Linux page cache.  Cached blocks for a file are hashed to the
 *	inode for that file, and can be held dirty in delayed write mode in
 *	the page cache.	 Cached metadata blocks for a file system are hashed
 *	to the inode for the mounted device.  The page_buf module assembles
 *	buffer (page_buf_t) objects on demand to aggregate such cached pages
 *	for I/O.
 *
 *
 *	Written by Steve Lord, Jim Mostek, Russell Cattelan
 *		    and Rajagopal Ananthanarayanan ("ananth") at SGI.
 *
 */

#include <linux/module.h>
#include <linux/stddef.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/pagemap.h>
#include <linux/init.h>
#include <linux/vmalloc.h>
#include <linux/blkdev.h>
#include <linux/locks.h>
#include <asm/softirq.h>
#include <linux/sysctl.h>
#include <linux/proc_fs.h>

#include <support/kmem.h>
#include "page_buf_internal.h"

#define SECTOR_SHIFT	9
#define SECTOR_SIZE	(1<<SECTOR_SHIFT)
#define SECTOR_MASK	(SECTOR_SIZE - 1)
#define BN_ALIGN_MASK	((1 << (PAGE_CACHE_SHIFT - SECTOR_SHIFT)) - 1)

#ifndef GFP_READAHEAD
#define GFP_READAHEAD	0
#endif

static inline void set_cpus_allowed(struct task_struct *p, unsigned long mask)
{

}

/*
 * Debug code
 */

#ifdef PAGEBUF_TRACE
static	spinlock_t		pb_trace_lock = SPIN_LOCK_UNLOCKED;
struct pagebuf_trace_buf	pb_trace;
EXPORT_SYMBOL(pb_trace);
EXPORT_SYMBOL(pb_trace_func);
#define CIRC_INC(i)	(((i) + 1) & (PB_TRACE_BUFSIZE - 1))

void
pb_trace_func(
	page_buf_t	*pb,
	int		event,
	void		*misc,
	void		*ra)
{
	int		j;
	unsigned long	flags;

	if (!pb_params.p_un.debug) return;

	if (ra == NULL) ra = (void *)__builtin_return_address(0);

	spin_lock_irqsave(&pb_trace_lock, flags);
	j = pb_trace.start;
	pb_trace.start = CIRC_INC(j);
	spin_unlock_irqrestore(&pb_trace_lock, flags);

	pb_trace.buf[j].pb = (unsigned long) pb;
	pb_trace.buf[j].event = event;
	pb_trace.buf[j].flags = pb->pb_flags;
	pb_trace.buf[j].hold = pb->pb_hold.counter;
	pb_trace.buf[j].lock_value = PBP(pb)->pb_sema.count.counter;
	pb_trace.buf[j].task = (void *)current;
	pb_trace.buf[j].misc = misc;
	pb_trace.buf[j].ra = ra;
	pb_trace.buf[j].offset = pb->pb_file_offset;
	pb_trace.buf[j].size = pb->pb_buffer_length;
}
#endif	/* PAGEBUF_TRACE */

#ifdef PAGEBUF_TRACKING
#define MAX_PB	10000
page_buf_t	*pb_array[MAX_PB];
EXPORT_SYMBOL(pb_array);

void
pb_tracking_get(
	page_buf_t	*pb)
{
	int		i;

	for (i = 0; (pb_array[i] != 0) && (i < MAX_PB); i++) { }
	if (i == MAX_PB)
		printk("pb 0x%p not recorded in pb_array\n", pb);
	else {
		//printk("pb_get 0x%p in pb_array[%d]\n", pb, i);
		pb_array[i] = pb;
	}
}

void
pb_tracking_free(
	page_buf_t	*pb)
{
	int		i;

	for (i = 0; (pb_array[i] != pb) && (i < MAX_PB); i++) { }
	if (i < MAX_PB) {
		//printk("pb_free 0x%p from pb_array[%d]\n", pb, i);
		pb_array[i] = NULL;
	}
	else
		printk("Freed unmonitored pagebuf 0x%p\n", pb);
}
#else
#define pb_tracking_get(pb)	do { } while (0)
#define pb_tracking_free(pb)	do { } while (0)
#endif	/* PAGEBUF_TRACKING */

/*
 *	File wide globals
 */

STATIC kmem_cache_t *pagebuf_cache;
STATIC pagebuf_daemon_t *pb_daemon;
STATIC struct list_head pagebuf_iodone_tq[NR_CPUS];
STATIC wait_queue_head_t pagebuf_iodone_wait[NR_CPUS];

/*
 *	For pre-allocated buffer head pool
 */

#define NR_RESERVED_BH	64
static wait_queue_head_t	pb_resv_bh_wait;
static spinlock_t		pb_resv_bh_lock = SPIN_LOCK_UNLOCKED;
struct buffer_head		*pb_resv_bh = NULL;	/* list of bh */
int				pb_resv_bh_cnt = 0;	/* # of bh available */

STATIC void pagebuf_daemon_wakeup(int);
STATIC int _pagebuf_segment_apply(page_buf_t *);

/*
 * Pagebuf module configuration parameters, exported via
 * /proc/sys/vm/pagebuf
 */

unsigned long pagebuf_min[P_PARAM] = {	HZ/2,	1*HZ, 0, 0 };
unsigned long pagebuf_max[P_PARAM] = { HZ*30, HZ*300, 1, 1 };

pagebuf_param_t pb_params = {{ HZ, 15 * HZ, 0, 0 }};

/*
 * Pagebuf statistics variables
 */

struct pbstats pbstats;

/*
 * Pagebuf allocation / freeing.
 */

#define pb_to_gfp(flags) \
	(((flags) & PBF_READ_AHEAD) ? GFP_READAHEAD : \
	 ((flags) & PBF_DONT_BLOCK) ? GFP_NOFS : GFP_KERNEL)

#define pagebuf_allocate(flags) \
	kmem_cache_alloc(pagebuf_cache, pb_to_gfp(flags))
#define pagebuf_deallocate(pb) \
	kmem_cache_free(pagebuf_cache, (pb));

/*
 * Pagebuf hashing
 */

#define NBITS	5
#define NHASH	(1<<NBITS)

typedef struct {
	struct list_head	pb_hash;
	int			pb_count;
	spinlock_t		pb_hash_lock;
} pb_hash_t;

STATIC pb_hash_t	pbhash[NHASH];
#define pb_hash(pb)	&pbhash[pb->pb_hash_index]

STATIC int
_bhash(
	dev_t		dev,
	loff_t		base)
{
	int		bit, hval;

	base >>= 9;
	/*
	 * dev_t is 16 bits, loff_t is always 64 bits
	 */
	base ^= dev;
	for (bit = hval = 0; base != 0 && bit < sizeof(base) * 8; bit += NBITS) {
		hval ^= (int)base & (NHASH-1);
		base >>= NBITS;
	}
	return hval;
}

/*
 * Mapping of multi-page buffers into contingous virtual space
 */

STATIC void *pagebuf_mapout_locked(page_buf_t *);

STATIC	spinlock_t		as_lock = SPIN_LOCK_UNLOCKED;
typedef struct a_list {
	void	*vm_addr;
	struct a_list	*next;
} a_list_t;
STATIC	a_list_t	*as_free_head;
STATIC	int		as_list_len;

STATIC void
free_address(
	void		*addr)
{
	a_list_t	*aentry;

	spin_lock(&as_lock);
	aentry = kmalloc(sizeof(a_list_t), GFP_ATOMIC);
	aentry->next = as_free_head;
	aentry->vm_addr = addr;
	as_free_head = aentry;
	as_list_len++;
	spin_unlock(&as_lock);
}

STATIC void
purge_addresses(void)
{
	a_list_t	*aentry, *old;

	if (as_free_head == NULL) return;

	spin_lock(&as_lock);
	aentry = as_free_head;
	as_free_head = NULL;
	as_list_len = 0;
	spin_unlock(&as_lock);

	while ((old = aentry) != NULL) {
		vunmap(aentry->vm_addr);
		aentry = aentry->next;
		kfree(old);
	}
}

/*
 *	Locking model:
 *
 *	Buffers associated with inodes for which buffer locking
 *	is not enabled are not protected by semaphores, and are
 *	assumed to be exclusively owned by the caller.	There is
 *	spinlock in the buffer, for use by the caller when concurrent
 *	access is possible.
 */

/*
 *	Internal pagebuf object manipulation
 */

STATIC void
_pagebuf_initialize(
	page_buf_t		*pb,
	pb_target_t		*target,
	loff_t			range_base,
	size_t			range_length,
	page_buf_flags_t	flags)
{
	/*
	 * We don't want certain flags to appear in pb->pb_flags.
	 */
	flags &= ~(PBF_LOCK|PBF_ENTER_PAGES|PBF_MAPPED);
	flags &= ~(PBF_DONT_BLOCK|PBF_READ_AHEAD);

	pb_tracking_get(pb);

	memset(pb, 0, sizeof(page_buf_private_t));
	atomic_set(&pb->pb_hold, 1);
	init_MUTEX_LOCKED(&pb->pb_iodonesema);
	INIT_LIST_HEAD(&pb->pb_list);
	INIT_LIST_HEAD(&pb->pb_hash_list);
	init_MUTEX_LOCKED(&PBP(pb)->pb_sema); /* held, no waiters */
	PB_SET_OWNER(pb);
	pb->pb_target = target;
	pb->pb_file_offset = range_base;
	/*
	 * Set buffer_length and count_desired to the same value initially.
	 * IO routines should use count_desired, which will be the same in
	 * most cases but may be reset (e.g. XFS recovery).
	 */
	pb->pb_buffer_length = pb->pb_count_desired = range_length;
	pb->pb_flags = flags | PBF_NONE;
	pb->pb_bn = PAGE_BUF_DADDR_NULL;
	atomic_set(&PBP(pb)->pb_pin_count, 0);
	init_waitqueue_head(&PBP(pb)->pb_waiters);

	PB_STATS_INC(pbstats.pb_create);
	PB_TRACE(pb, PB_TRACE_REC(get), target);
}

/*
 * Allocate a page array capable of holding a specified number
 * of pages, and point the page buf at it.
 */
STATIC int
_pagebuf_get_pages(
	page_buf_t		*pb,
	int			page_count,
	int			flags)
{
	int			gpf_mask = pb_to_gfp(flags);

	/* Make sure that we have a page list */
	if (pb->pb_pages == NULL) {
		pb->pb_offset = page_buf_poff(pb->pb_file_offset);
		pb->pb_page_count = page_count;
		if (page_count <= PB_PAGES) {
			pb->pb_pages = pb->pb_page_array;
		} else {
			pb->pb_pages = kmalloc(sizeof(struct page *) *
					page_count, gpf_mask);
			if (pb->pb_pages == NULL)
				return -ENOMEM;
		}
		memset(pb->pb_pages, 0, sizeof(struct page *) * page_count);
	}
	return 0;
}

/*
 * Walk a pagebuf releasing all the pages contained within it.
 */
STATIC inline void
_pagebuf_freepages(
	page_buf_t		*pb)
{
	int			buf_index;

	for (buf_index = 0; buf_index < pb->pb_page_count; buf_index++) {
		struct page	*page = pb->pb_pages[buf_index];

		if (page) {
			pb->pb_pages[buf_index] = NULL;
			page_cache_release(page);
		}
	}

	if (pb->pb_pages != pb->pb_page_array)
		kfree(pb->pb_pages);
}

/*
 *	_pagebuf_free_object
 *
 *	_pagebuf_free_object releases the contents specified buffer.
 *	The modification state of any associated pages is left unchanged.
 */
void
_pagebuf_free_object(
	pb_hash_t	*hash,	/* hash bucket for buffer	*/
	page_buf_t	*pb)	/* buffer to deallocate		*/
{
	int		pb_flags = pb->pb_flags;

	PB_TRACE(pb, PB_TRACE_REC(free_obj), 0);
	pb->pb_flags |= PBF_FREED;

	if (hash) {
		if (!list_empty(&pb->pb_hash_list)) {
			hash->pb_count--;
			list_del_init(&pb->pb_hash_list);
		}
		spin_unlock(&hash->pb_hash_lock);
	}

	if (!(pb_flags & PBF_FREED)) {
		/* release any virtual mapping */ ;
		if (pb->pb_flags & _PBF_ADDR_ALLOCATED) {
			void *vaddr = pagebuf_mapout_locked(pb);
			if (vaddr) {
				free_address(vaddr);
			}
		}

		if (pb->pb_flags & _PBF_MEM_ALLOCATED) {
			if (pb->pb_pages) {
				/* release the pages in the address list */
				if (pb->pb_pages[0] &&
				    PageSlab(pb->pb_pages[0])) {
					/*
					 * This came from the slab
					 * allocator free it as such
					 */
					kfree(pb->pb_addr);
				} else {
					_pagebuf_freepages(pb);
				}

				pb->pb_pages = NULL;
			}
			pb->pb_flags &= ~_PBF_MEM_ALLOCATED;
		}
	}

	pb_tracking_free(pb);
	pagebuf_deallocate(pb);
}

/*
 *	_pagebuf_lookup_pages
 *
 *	_pagebuf_lookup_pages finds all pages which match the buffer
 *	in question and the range of file offsets supplied,
 *	and builds the page list for the buffer, if the
 *	page list is not already formed or if not all of the pages are
 *	already in the list. Invalid pages (pages which have not yet been
 *	read in from disk) are assigned for any pages which are not found.
 */
STATIC int
_pagebuf_lookup_pages(
	page_buf_t		*pb,
	struct address_space	*aspace,
	page_buf_flags_t	flags)
{
	loff_t			next_buffer_offset;
	unsigned long		page_count, pi, index;
	struct page		*page;
	int			gfp_mask, retry_count = 5, rval = 0;
	int			all_mapped, good_pages;
	size_t			blocksize;

	/* For pagebufs where we want to map an address, do not use
	 * highmem pages - so that we do not need to use kmap resources
	 * to access the data.
	 *
	 * For pages where the caller has indicated there may be resource
	 * contention (e.g. called from a transaction) do not flush
	 * delalloc pages to obtain memory.
	 */

	if (flags & PBF_READ_AHEAD) {
		gfp_mask = GFP_READAHEAD;
		retry_count = 0;
	} else if (flags & PBF_DONT_BLOCK) {
		gfp_mask = GFP_NOFS;
	} else if (flags & PBF_MAPPABLE) {
		gfp_mask = GFP_KERNEL;
	} else {
		gfp_mask = GFP_HIGHUSER;
	}

	next_buffer_offset = pb->pb_file_offset + pb->pb_buffer_length;

	good_pages = page_count = (page_buf_btoc(next_buffer_offset) -
				   page_buf_btoct(pb->pb_file_offset));

	if (pb->pb_flags & _PBF_ALL_PAGES_MAPPED) {
		/* Bring pages forward in cache */
		for (pi = 0; pi < page_count; pi++) {
			mark_page_accessed(pb->pb_pages[pi]);
		}
		if ((flags & PBF_MAPPED) && !(pb->pb_flags & PBF_MAPPED)) {
			all_mapped = 1;
			goto mapit;
		}
		return 0;
	}

	/* Ensure pb_pages field has been initialised */
	rval = _pagebuf_get_pages(pb, page_count, flags);
	if (rval)
		return rval;

	rval = pi = 0;
	blocksize = pb->pb_target->pbr_blocksize;

	/* Enter the pages in the page list */
	index = (pb->pb_file_offset - pb->pb_offset) >> PAGE_CACHE_SHIFT;
	for (all_mapped = 1; pi < page_count; pi++, index++) {
		if (pb->pb_pages[pi] == 0) {
		      retry:
			page = find_or_create_page(aspace, index, gfp_mask);
			if (!page) {
				if (--retry_count > 0) {
					PB_STATS_INC(pbstats.pb_page_retries);
					pagebuf_daemon_wakeup(1);
					current->state = TASK_UNINTERRUPTIBLE;
					schedule_timeout(10);
					goto retry;
				}
				rval = -ENOMEM;
				all_mapped = 0;
				continue;
			}
			PB_STATS_INC(pbstats.pb_page_found);
			mark_page_accessed(page);
			pb->pb_pages[pi] = page;
		} else {
			page = pb->pb_pages[pi];
			lock_page(page);
		}

		/* If we need to do I/O on a page record the fact */
		if (!Page_Uptodate(page)) {
			good_pages--;
			if ((blocksize == PAGE_CACHE_SIZE) &&
			    (flags & PBF_READ))
				pb->pb_locked = 1;
		}
	}

	if (!pb->pb_locked) {
		for (pi = 0; pi < page_count; pi++) {
			unlock_page(pb->pb_pages[pi]);
		}
	}

mapit:
	pb->pb_flags |= _PBF_MEM_ALLOCATED;
	if (all_mapped) {
		pb->pb_flags |= _PBF_ALL_PAGES_MAPPED;

		/* A single page buffer is always mappable */
		if (page_count == 1) {
			pb->pb_addr = (caddr_t)
			    	page_address(pb->pb_pages[0]) + pb->pb_offset;
			pb->pb_flags |= PBF_MAPPED;
		} else if (flags & PBF_MAPPED) {
			if (as_list_len > 64)
				purge_addresses();
			pb->pb_addr = vmap(pb->pb_pages, page_count);
			if (!pb->pb_addr)
				BUG();
			pb->pb_addr += pb->pb_offset;
			pb->pb_flags |= PBF_MAPPED | _PBF_ADDR_ALLOCATED;
		}
	}
	/* If some pages were found with data in them
	 * we are not in PBF_NONE state.
	 */
	if (good_pages != 0) {
		pb->pb_flags &= ~(PBF_NONE);
		if (good_pages != page_count) {
			pb->pb_flags |= PBF_PARTIAL;
		}
	}

	PB_TRACE(pb, PB_TRACE_REC(look_pg), good_pages);

	return rval;
}


/*
 *	Pre-allocation of a pool of buffer heads for use in
 *	low-memory situations.
 */

/*
 *	_pagebuf_prealloc_bh
 *
 *	Pre-allocate a pool of "count" buffer heads at startup.
 *	Puts them on a list at "pb_resv_bh"
 *	Returns number of bh actually allocated to pool.
 */
STATIC int
_pagebuf_prealloc_bh(
	int			count)
{
	struct buffer_head	*bh;
	int			i;

	for (i = 0; i < count; i++) {
		bh = kmem_cache_alloc(bh_cachep, SLAB_KERNEL);
		if (!bh)
			break;
		bh->b_pprev = &pb_resv_bh;
		bh->b_next = pb_resv_bh;
		pb_resv_bh = bh;
		pb_resv_bh_cnt++;
	}
	return i;
}

/*
 *	_pagebuf_get_prealloc_bh
 *
 *	Get one buffer head from our pre-allocated pool.
 *	If pool is empty, sleep 'til one comes back in.
 *	Returns aforementioned buffer head.
 */
STATIC struct buffer_head *
_pagebuf_get_prealloc_bh(void)
{
	unsigned long		flags;
	struct buffer_head	*bh = NULL;
	struct task_struct	*tsk = current;
	DECLARE_WAITQUEUE	(wait, tsk);

	spin_lock_irqsave(&pb_resv_bh_lock, flags);

	if (pb_resv_bh_cnt < 1) {

		add_wait_queue(&pb_resv_bh_wait, &wait);
		do {
			run_task_queue(&tq_disk);
			set_task_state(tsk, TASK_UNINTERRUPTIBLE);
			spin_unlock_irqrestore(&pb_resv_bh_lock, flags);
			schedule();
			spin_lock_irqsave(&pb_resv_bh_lock, flags);
		} while (pb_resv_bh_cnt < 1);
		tsk->state = TASK_RUNNING;
		remove_wait_queue(&pb_resv_bh_wait, &wait);
	}

	if (pb_resv_bh_cnt < 1)
		BUG();

	bh = pb_resv_bh;

	if (!bh)
		BUG();

	pb_resv_bh = bh->b_next;
	bh->b_state = 0;
	pb_resv_bh_cnt--;

	spin_unlock_irqrestore(&pb_resv_bh_lock, flags);

	return bh;
}

/*
 *	_pagebuf_free_bh
 *
 *	Take care of buffer heads that we're finished with.
 *	Call this instead of just kmem_cache_free(bh_cachep, bh)
 *	when you're done with a bh.
 *
 *	If our pre-allocated pool is full, just free the buffer head.
 *	Otherwise, put it back in the pool, and wake up anybody
 *	waiting for one.
 */
STATIC inline void
_pagebuf_free_bh(
	struct buffer_head	*bh)
{
	unsigned long		flags;

	if (pb_resv_bh_cnt == NR_RESERVED_BH){
		kmem_cache_free(bh_cachep, bh);
	} else {
		spin_lock_irqsave(&pb_resv_bh_lock, flags);

		bh->b_pprev = &pb_resv_bh;
		bh->b_next = pb_resv_bh;
		pb_resv_bh = bh;
		pb_resv_bh_cnt++;

		if (waitqueue_active(&pb_resv_bh_wait)) {
			wake_up(&pb_resv_bh_wait);
		}

		spin_unlock_irqrestore(&pb_resv_bh_lock, flags);
	}
}

/*
 *	Finding and Reading Buffers
 */

/*
 *	_pagebuf_find
 *
 *	Looks up, and creates if absent, a lockable buffer for
 *	a given range of an inode.  The buffer is returned
 *	locked.	 If other overlapping buffers exist, they are
 *	released before the new buffer is created and locked,
 *	which may imply that this call will block until those buffers
 *	are unlocked.  No I/O is implied by this call.
 */
STATIC page_buf_t *
_pagebuf_find(				/* find buffer for block	*/
	pb_target_t		*target,/* target for block		*/
	loff_t			ioff,	/* starting offset of range	*/
	size_t			isize,	/* length of range		*/
	page_buf_flags_t	flags,	/* PBF_TRYLOCK			*/
	page_buf_t		*new_pb)/* newly allocated buffer	*/
{
	loff_t			range_base;
	size_t			range_length;
	int			hval;
	pb_hash_t		*h;
	struct list_head	*p;
	page_buf_t		*pb;
	int			not_locked;

	range_base = (ioff << SECTOR_SHIFT);
	range_length = (isize << SECTOR_SHIFT);

	hval = _bhash(target->pbr_bdev->bd_dev, range_base);
	h = &pbhash[hval];

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
	return (new_pb);

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
			return (NULL);
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
	PB_STATS_INC(pbstats.pb_get_locked);
	return (pb);
}


/*
 *	pagebuf_find
 *
 *	pagebuf_find returns a buffer matching the specified range of
 *	data for the specified target, if any of the relevant blocks
 *	are in memory.	The buffer may have unallocated holes, if
 *	some, but not all, of the blocks are in memory.	 Even where
 *	pages are present in the buffer, not all of every page may be
 *	valid.	The file system may use pagebuf_segment to visit the
 *	various segments of the buffer.
 */
page_buf_t *
pagebuf_find(				/* find buffer for block	*/
					/* if the block is in memory	*/
	pb_target_t		*target,/* target for block		*/
	loff_t			ioff,	/* starting offset of range	*/
	size_t			isize,	/* length of range		*/
	page_buf_flags_t	flags)	/* PBF_TRYLOCK			*/
{
	return _pagebuf_find(target, ioff, isize, flags, NULL);
}

/*
 *	pagebuf_get
 *
 *	pagebuf_get assembles a buffer covering the specified range.
 *	Some or all of the blocks in the range may be valid.  The file
 *	system may use pagebuf_segment to visit the various segments
 *	of the buffer.	Storage in memory for all portions of the
 *	buffer will be allocated, although backing storage may not be.
 *	If PBF_READ is set in flags, pagebuf_read
 */
page_buf_t *
pagebuf_get(				/* allocate a buffer		*/
	pb_target_t		*target,/* target for buffer 		*/
	loff_t			ioff,	/* starting offset of range	*/
	size_t			isize,	/* length of range		*/
	page_buf_flags_t	flags)	/* PBF_TRYLOCK			*/
{
	page_buf_t		*pb, *new_pb;
	int			error;

	new_pb = pagebuf_allocate(flags);
	if (unlikely(!new_pb))
		return (NULL);

	pb = _pagebuf_find(target, ioff, isize, flags, new_pb);
	if (pb != new_pb) {
		pagebuf_deallocate(new_pb);
		if (unlikely(!pb))
			return (NULL);
	}

	PB_STATS_INC(pbstats.pb_get);

	/* fill in any missing pages */
	error = _pagebuf_lookup_pages(pb, pb->pb_target->pbr_mapping, flags);
	if (unlikely(error)) {
		pagebuf_free(pb);
		return (NULL);
	}

	/*
	 * Always fill in the block number now, the mapped cases can do
	 * their own overlay of this later.
	 */
	pb->pb_bn = ioff;
	pb->pb_count_desired = pb->pb_buffer_length;

	if (flags & PBF_READ) {
		if (PBF_NOT_DONE(pb)) {
			PB_TRACE(pb, PB_TRACE_REC(get_read), flags);
			PB_STATS_INC(pbstats.pb_get_read);
			pagebuf_iostart(pb, flags);
		} else if (flags & PBF_ASYNC) {
			/*
			 * Read ahead call which is already satisfied,
			 * drop the buffer
			 */
			if (flags & (PBF_LOCK | PBF_TRYLOCK))
				pagebuf_unlock(pb);
			pagebuf_rele(pb);
			return NULL;
		} else {
			/* We do not want read in the flags */
			pb->pb_flags &= ~PBF_READ;
		}
	}

	PB_TRACE(pb, PB_TRACE_REC(get_obj), flags);
	return (pb);
}

/*
 * Create a pagebuf and populate it with pages from the address
 * space of the passed in inode.
 */
page_buf_t *
pagebuf_lookup(
	struct pb_target	*target,
	struct inode		*inode,
	loff_t			ioff,
	size_t			isize,
	int			flags)
{
	page_buf_t		*pb = NULL;
	int			status;

	flags |= _PBF_PRIVATE_BH;
	pb = pagebuf_allocate(flags);
	if (pb) {
		_pagebuf_initialize(pb, target, ioff, isize, flags);
		if (flags & PBF_ENTER_PAGES) {
			status = _pagebuf_lookup_pages(pb, &inode->i_data, 0);
			if (status != 0) {
				pagebuf_free(pb);
				return (NULL);
			}
		}
	}
	return pb;
}

/*
 * If we are not low on memory then do the readahead in a deadlock
 * safe manner.
 */
void
pagebuf_readahead(
	pb_target_t		*target,
	loff_t			ioff,
	size_t			isize,
	int			flags)
{
	flags |= (PBF_TRYLOCK|PBF_READ|PBF_ASYNC|PBF_MAPPABLE|PBF_READ_AHEAD);
	pagebuf_get(target, ioff, isize, flags);
}

page_buf_t *
pagebuf_get_empty(
	pb_target_t		*target)
{
	page_buf_t		*pb;

	pb = pagebuf_allocate(_PBF_LOCKABLE);
	if (pb)
		_pagebuf_initialize(pb, target, 0, 0, _PBF_LOCKABLE);
	return pb;
}

static inline struct page *
mem_to_page(
	void			*addr)
{
	if (((unsigned long)addr < VMALLOC_START) ||
            ((unsigned long)addr >= VMALLOC_END)) {
		return virt_to_page(addr);
	} else {
		return vmalloc_to_page(addr);
	}
}

int
pagebuf_associate_memory(
	page_buf_t		*pb,
	void			*mem,
	size_t			len)
{
	int			rval;
	int			i = 0;
	size_t			ptr;
	size_t			end, end_cur;
	off_t			offset;
	int			page_count;

	page_count = PAGE_CACHE_ALIGN(len) >> PAGE_CACHE_SHIFT;
	offset = (off_t) mem - ((off_t)mem & PAGE_CACHE_MASK);
	if (offset && (len > PAGE_CACHE_SIZE))
		page_count++;

	/* Free any previous set of page pointers */
	if (pb->pb_pages && (pb->pb_pages != pb->pb_page_array)) {
		kfree(pb->pb_pages);
	}
	pb->pb_pages = NULL;
	pb->pb_addr = mem;

	rval = _pagebuf_get_pages(pb, page_count, 0);
	if (rval)
		return rval;

	pb->pb_offset = offset;
	ptr = (size_t) mem & PAGE_CACHE_MASK;
	end = PAGE_CACHE_ALIGN((size_t) mem + len);
	end_cur = end;
	/* set up first page */
	pb->pb_pages[0] = mem_to_page(mem);

	ptr += PAGE_CACHE_SIZE;
	pb->pb_page_count = ++i;
	while (ptr < end) {
		pb->pb_pages[i] = mem_to_page((void *)ptr);
		pb->pb_page_count = ++i;
		ptr += PAGE_CACHE_SIZE;
	}
	pb->pb_locked = 0;

	pb->pb_count_desired = pb->pb_buffer_length = len;
	pb->pb_flags |= PBF_MAPPED | _PBF_PRIVATE_BH;

	return 0;
}

page_buf_t *
pagebuf_get_no_daddr(
	size_t			len,
	pb_target_t		*target)
{
	int			rval;
	void			*rmem = NULL;
	int			flags = _PBF_LOCKABLE | PBF_FORCEIO;
	page_buf_t		*pb;
	size_t			tlen = 0;

	if (len > 0x20000)
		return(NULL);

	pb = pagebuf_allocate(flags);
	if (!pb)
		return NULL;

	_pagebuf_initialize(pb, target, 0, len, flags);

	do {
		if (tlen == 0) {
			tlen = len; /* first time */
		} else {
			kfree(rmem); /* free the mem from the previous try */
			tlen <<= 1; /* double the size and try again */
			/*
			printk(
			"pb_get_no_daddr NOT block 0x%p mask 0x%p len %d\n",
				rmem, ((size_t)rmem & (size_t)~SECTOR_MASK),
				len);
			*/
		}
		if ((rmem = kmalloc(tlen, GFP_KERNEL)) == 0) {
			pagebuf_free(pb);
			return NULL;
		}
	} while ((size_t)rmem != ((size_t)rmem & (size_t)~SECTOR_MASK));

	if ((rval = pagebuf_associate_memory(pb, rmem, len)) != 0) {
		kfree(rmem);
		pagebuf_free(pb);
		return NULL;
	}
	/* otherwise pagebuf_free just ignores it */
	pb->pb_flags |= _PBF_MEM_ALLOCATED;
	up(&PBP(pb)->pb_sema);	/* Return unlocked pagebuf */

	PB_TRACE(pb, PB_TRACE_REC(no_daddr), rmem);

	return pb;
}


/*
 *	pagebuf_hold
 *
 *	Increment reference count on buffer, to hold the buffer concurrently
 *	with another thread which may release (free) the buffer asynchronously.
 *
 *	Must hold the buffer already to call this function.
 */
void
pagebuf_hold(
	page_buf_t		*pb)
{
	atomic_inc(&pb->pb_hold);
	PB_TRACE(pb, PB_TRACE_REC(hold), 0);
}

/*
 *	pagebuf_free
 *
 *	pagebuf_free releases the specified buffer.  The modification
 *	state of any associated pages is left unchanged.
 */
void
pagebuf_free(
	page_buf_t		*pb)
{
	if (pb->pb_flags & _PBF_LOCKABLE) {
		pb_hash_t	*h = pb_hash(pb);

		spin_lock(&h->pb_hash_lock);
		_pagebuf_free_object(h, pb);
	} else {
		_pagebuf_free_object(NULL, pb);
	}
}

/*
 *	pagebuf_rele
 *
 *	pagebuf_rele releases a hold on the specified buffer.  If the
 *	the hold count is 1, pagebuf_rele calls pagebuf_free.
 */
void
pagebuf_rele(
	page_buf_t		*pb)
{
	pb_hash_t		*h;

	PB_TRACE(pb, PB_TRACE_REC(rele), pb->pb_relse);
	if (pb->pb_flags & _PBF_LOCKABLE) {
		h = pb_hash(pb);
		spin_lock(&h->pb_hash_lock);
	} else {
		h = NULL;
	}

	if (atomic_dec_and_test(&pb->pb_hold)) {
		int		do_free = 1;

		if (pb->pb_relse) {
			atomic_inc(&pb->pb_hold);
			if (h)
				spin_unlock(&h->pb_hash_lock);
			(*(pb->pb_relse)) (pb);
			do_free = 0;
		}
		if (pb->pb_flags & PBF_DELWRI) {
			pb->pb_flags |= PBF_ASYNC;
			atomic_inc(&pb->pb_hold);
			if (h && do_free)
				spin_unlock(&h->pb_hash_lock);
			pagebuf_delwri_queue(pb, 0);
			do_free = 0;
		} else if (pb->pb_flags & PBF_FS_MANAGED) {
			if (h)
				spin_unlock(&h->pb_hash_lock);
			do_free = 0;
		}

		if (do_free) {
			_pagebuf_free_object(h, pb);
		}
	} else if (h) {
		spin_unlock(&h->pb_hash_lock);
	}
}


/*
 *	Pinning Buffer Storage in Memory
 */

/*
 *	pagebuf_pin
 *
 *	pagebuf_pin locks all of the memory represented by a buffer in
 *	memory.	 Multiple calls to pagebuf_pin and pagebuf_unpin, for
 *	the same or different buffers affecting a given page, will
 *	properly count the number of outstanding "pin" requests.  The
 *	buffer may be released after the pagebuf_pin and a different
 *	buffer used when calling pagebuf_unpin, if desired.
 *	pagebuf_pin should be used by the file system when it wants be
 *	assured that no attempt will be made to force the affected
 *	memory to disk.	 It does not assure that a given logical page
 *	will not be moved to a different physical page.
 */
void
pagebuf_pin(
	page_buf_t		*pb)
{
	atomic_inc(&PBP(pb)->pb_pin_count);
	PB_TRACE(pb, PB_TRACE_REC(pin), PBP(pb)->pb_pin_count.counter);
}

/*
 *	pagebuf_unpin
 *
 *	pagebuf_unpin reverses the locking of memory performed by
 *	pagebuf_pin.  Note that both functions affected the logical
 *	pages associated with the buffer, not the buffer itself.
 */
void
pagebuf_unpin(
	page_buf_t		*pb)
{
	if (atomic_dec_and_test(&PBP(pb)->pb_pin_count)) {
		wake_up_all(&PBP(pb)->pb_waiters);
	}
	PB_TRACE(pb, PB_TRACE_REC(unpin), PBP(pb)->pb_pin_count.counter);
}

int
pagebuf_ispin(
	page_buf_t		*pb)
{
	return atomic_read(&PBP(pb)->pb_pin_count);
}

/*
 *	pagebuf_wait_unpin
 *
 *	pagebuf_wait_unpin waits until all of the memory associated
 *	with the buffer is not longer locked in memory.	 It returns
 *	immediately if none of the affected pages are locked.
 */
static inline void
_pagebuf_wait_unpin(
	page_buf_t		*pb)
{
	DECLARE_WAITQUEUE	(wait, current);

	if (atomic_read(&PBP(pb)->pb_pin_count) == 0)
		return;

	add_wait_queue(&PBP(pb)->pb_waiters, &wait);
	for (;;) {
		current->state = TASK_UNINTERRUPTIBLE;
		if (atomic_read(&PBP(pb)->pb_pin_count) == 0) {
			break;
		}
		run_task_queue(&tq_disk);
		schedule();
	}
	remove_wait_queue(&PBP(pb)->pb_waiters, &wait);
	current->state = TASK_RUNNING;
}

void
pagebuf_queue_task(
	struct tq_struct	*task)
{
	queue_task(task, &pagebuf_iodone_tq[smp_processor_id()]);
	wake_up(&pagebuf_iodone_wait[smp_processor_id()]);
}


/*
 *	Buffer Utility Routines
 */

/*
 *	pagebuf_iodone
 *
 *	pagebuf_iodone marks a buffer for which I/O is in progress
 *	done with respect to that I/O.	The pb_done routine, if
 *	present, will be called as a side-effect.
 */
void
pagebuf_iodone_sched(
	void			*v)
{
	page_buf_t		*pb = (page_buf_t *)v;

	if (pb->pb_iodone) {
		(*(pb->pb_iodone)) (pb);
		return;
	}

	if (pb->pb_flags & PBF_ASYNC) {
		if ((pb->pb_flags & _PBF_LOCKABLE) && !pb->pb_relse)
			pagebuf_unlock(pb);
		pagebuf_rele(pb);
	}
}

void
pagebuf_iodone(
	page_buf_t		*pb)
{
	pb->pb_flags &= ~(PBF_READ | PBF_WRITE);
	if (pb->pb_error == 0) {
		pb->pb_flags &= ~(PBF_PARTIAL | PBF_NONE);
	}

	PB_TRACE(pb, PB_TRACE_REC(done), pb->pb_iodone);

	if ((pb->pb_iodone) || (pb->pb_flags & PBF_ASYNC)) {
		INIT_TQUEUE(&pb->pb_iodone_sched,
			pagebuf_iodone_sched, (void *)pb);

		queue_task(&pb->pb_iodone_sched,
				&pagebuf_iodone_tq[smp_processor_id()]);
		wake_up(&pagebuf_iodone_wait[smp_processor_id()]);
	} else {
		up(&pb->pb_iodonesema);
	}
}

/*
 *	pagebuf_ioerror
 *
 *	pagebuf_ioerror sets the error code for a buffer.
 */
void
pagebuf_ioerror(			/* mark/clear buffer error flag */
	page_buf_t		*pb,	/* buffer to mark		*/
	unsigned int		error)	/* error to store (0 if none)	*/
{
	pb->pb_error = error;
	PB_TRACE(pb, PB_TRACE_REC(ioerror), error);
}

/*
 *	pagebuf_iostart
 *
 *	pagebuf_iostart initiates I/O on a buffer, based on the flags supplied.
 *	If necessary, it will arrange for any disk space allocation required,
 *	and it will break up the request if the block mappings require it.
 *	An pb_iodone routine in the buffer supplied will only be called
 *	when all of the subsidiary I/O requests, if any, have been completed.
 *	pagebuf_iostart calls the pagebuf_ioinitiate routine or
 *	pagebuf_iorequest, if the former routine is not defined, to start
 *	the I/O on a given low-level request.
 */
int
pagebuf_iostart(			/* start I/O on a buffer	  */
	page_buf_t		*pb,	/* buffer to start		  */
	page_buf_flags_t	flags)	/* PBF_LOCK, PBF_ASYNC, PBF_READ, */
					/* PBF_WRITE, PBF_ALLOCATE,	  */
					/* PBF_DELWRI, 			  */
					/* PBF_SYNC, PBF_DONT_BLOCK	  */
					/* PBF_RELEASE			  */
{
	int			status = 0;

	PB_TRACE(pb, PB_TRACE_REC(iostart), flags);

	if (flags & PBF_DELWRI) {
		pb->pb_flags &= ~(PBF_READ | PBF_WRITE | PBF_ASYNC);
		pb->pb_flags |= flags &
				(PBF_DELWRI | PBF_ASYNC | PBF_SYNC);
		pagebuf_delwri_queue(pb, 1);
		return status;
	}

	pb->pb_flags &= ~(PBF_READ|PBF_WRITE|PBF_ASYNC|PBF_DELWRI|PBF_READ_AHEAD);
	pb->pb_flags |= flags & (PBF_READ|PBF_WRITE|PBF_ASYNC|PBF_SYNC|PBF_READ_AHEAD);

	if (pb->pb_bn == PAGE_BUF_DADDR_NULL) {
		BUG();
	}

	/* For writes call internal function which checks for
	 * filesystem specific callout function and execute it.
	 */
	if (flags & PBF_WRITE) {
		status = __pagebuf_iorequest(pb);
	} else {
		status = pagebuf_iorequest(pb);
	}

	/* Wait for I/O if we are not an async request */
	if ((status == 0) && (flags & PBF_ASYNC) == 0) {
		status = pagebuf_iowait(pb);
	}

	return status;
}


/*
 * Helper routines for pagebuf_iorequest (pagebuf I/O completion)
 * 
 * (different routines for locked/unlocked, and single/multi-bh pagebufs)
 */

STATIC inline void
_pb_io_done(
	page_buf_t		*pb)
{
	if (atomic_dec_and_test(&PBP(pb)->pb_io_remaining) == 1) {
		pb->pb_locked = 0;
		pagebuf_iodone(pb);
	}
}

STATIC void
_end_pagebuf_page_io(
	struct buffer_head	*bh,
	int			uptodate,
	int			locked)
{
	struct page		*page;
	page_buf_t		*pb = (page_buf_t *) bh->b_private;

	mark_buffer_uptodate(bh, uptodate);
	atomic_dec(&bh->b_count);

	page = bh->b_page;
	if (!test_bit(BH_Uptodate, &bh->b_state)) {
		set_bit(PG_error, &page->flags);
		pb->pb_error = EIO;
	}

	unlock_buffer(bh);
	_pagebuf_free_bh(bh);

	SetPageUptodate(page);
	if (locked)
		unlock_page(page);
	_pb_io_done(pb);
}

STATIC void
_end_io_locked(
	struct buffer_head	*bh,
	int			uptodate)
{
	_end_pagebuf_page_io(bh, uptodate, 1);
}

STATIC void
_end_io_nolock(
	struct buffer_head	*bh,
	int			uptodate)
{
	_end_pagebuf_page_io(bh, uptodate, 0);
}

typedef struct {
	page_buf_t	*pb;		/* pointer to pagebuf page is within */
	int		locking;	/* are pages locked? */
	atomic_t	remain;		/* count of remaining I/O requests */
} pagesync_t;

STATIC void
_end_pagebuf_page_io_multi(
	struct buffer_head	*bh,
	int			uptodate,
	int			fullpage)
{
	pagesync_t		*psync = (pagesync_t *) bh->b_private;
	page_buf_t		*pb = psync->pb;
	struct page		*page;

	mark_buffer_uptodate(bh, uptodate);
	put_bh(bh);

	page = bh->b_page;
	if (!test_bit(BH_Uptodate, &bh->b_state)) {
		set_bit(PG_error, &page->flags);
		pb->pb_error = EIO;
	}

	unlock_buffer(bh);
	if (fullpage)
		_pagebuf_free_bh(bh);

	if (atomic_dec_and_test(&psync->remain) == 1) {
		if (fullpage)
			SetPageUptodate(page);
		if (psync->locking)
			unlock_page(page);
		kfree(psync);
		_pb_io_done(pb);
	}
}

STATIC void
_end_io_multi_full(
	struct buffer_head	*bh,
	int			uptodate)
{
	_end_pagebuf_page_io_multi(bh, uptodate, 1);
}

STATIC void
_end_io_multi_part(
	struct buffer_head	*bh,
	int			uptodate)
{
	_end_pagebuf_page_io_multi(bh, uptodate, 0);
}


/*
 * Initiate I/O on part of a page we are interested in
 */
STATIC int
_pagebuf_page_io(
	struct page		*page,	/* Page structure we are dealing with */
	page_buf_t		*pb,	/* pagebuf holding it, can be NULL */
	page_buf_daddr_t	bn,	/* starting block number */
	kdev_t			dev,	/* device for I/O */
	size_t			blocksize,	/* filesystem block size */
	off_t			pg_offset,	/* starting offset in page */
	size_t			pg_length,	/* count of data to process */
	int			locking,	/* page locking in use */
	int			rw,	/* read/write operation */
	int			flush)
{
	size_t			sector;
	size_t			blk_length = 0;
	struct buffer_head	*bh, *head, *bufferlist[MAX_BUF_PER_PAGE];
	int			multi_ok;
	int			i = 0, cnt = 0, err = 0;
	int			public_bh = 0;

	if ((blocksize < PAGE_CACHE_SIZE) &&
	    !(pb->pb_flags & _PBF_PRIVATE_BH)) {
		int		cache_ok;

		cache_ok = !((pb->pb_flags & PBF_FORCEIO) || (rw == WRITE));
		public_bh = multi_ok = 1;

		if (!page_has_buffers(page)) {
			if (!locking) {
				lock_page(page);
				if (!page_has_buffers(page)) {
					create_empty_buffers(page, dev,
							SECTOR_SIZE);
				}
				unlock_page(page);
			} else {
				create_empty_buffers(page, dev, SECTOR_SIZE);
			}
		}

		/* Find buffer_heads belonging to just this pagebuf */
		bh = head = page_buffers(page);
		do {
			if (buffer_uptodate(bh) && cache_ok)
				continue;
			blk_length = i << SECTOR_SHIFT;
			if (blk_length < pg_offset)
				continue;
			if (blk_length >= pg_offset + pg_length)
				break;

			lock_buffer(bh);
			get_bh(bh);
			assert(!waitqueue_active(&bh->b_wait));

			bh->b_size = SECTOR_SIZE;
			bh->b_blocknr = bn + (i - (pg_offset >> SECTOR_SHIFT));
			bufferlist[cnt++] = bh;
		} while (i++, (bh = bh->b_this_page) != head);

		goto request;
	}

	/* Calculate the block offsets and length we will be using */
	if (pg_offset) {
		size_t		block_offset;

		block_offset = pg_offset >> SECTOR_SHIFT;
		block_offset = pg_offset - (block_offset << SECTOR_SHIFT);
		blk_length = (pg_length + block_offset + SECTOR_MASK) >>
								SECTOR_SHIFT;
	} else {
		blk_length = (pg_length + SECTOR_MASK) >> SECTOR_SHIFT;
	}

	/* This will attempt to make a request bigger than the sector
	 * size if we are well aligned.
	 */
	switch (pb->pb_target->pbr_flags) {
	case 0:
		sector = blk_length << SECTOR_SHIFT;
		blk_length = 1;
		break;
	case PBR_ALIGNED_ONLY:
		if ((pg_offset == 0) && (pg_length == PAGE_CACHE_SIZE) &&
		    (((unsigned int) bn) & BN_ALIGN_MASK) == 0) {
			sector = blk_length << SECTOR_SHIFT;
			blk_length = 1;
			break;
		}
	case PBR_SECTOR_ONLY:
		/* Fallthrough, same as default */
	default:
		sector = SECTOR_SIZE;
	}

	/* The b_size field of struct buffer_head is an unsigned short
	 * ... we may need to split this request up.  [64K is too big]
	 */
	assert(sizeof(bh->b_size) == 2);
	while (sector > 0xffff) {
		sector >>= 1;
		blk_length++;
	}

	multi_ok = (blk_length != 1);

	for (; blk_length > 0; blk_length--, pg_offset += sector) {
		bh = kmem_cache_alloc(bh_cachep, SLAB_NOFS);
		if (!bh) {
			bh = _pagebuf_get_prealloc_bh();
			if (!bh) {
				/* This should never happen */
				err = -ENOMEM;
				goto error;
			}
		}
		memset(bh, 0, sizeof(*bh));
		bh->b_size = sector;
		bh->b_blocknr = bn++;
		bh->b_dev = dev;
		set_bit(BH_Lock, &bh->b_state);
		set_bh_page(bh, page, pg_offset);
		init_waitqueue_head(&bh->b_wait);
		atomic_set(&bh->b_count, 1);
		bufferlist[cnt++] = bh;
	}

request:
	if (cnt) {
		pagesync_t	*psync = NULL;
		void		(*callback)(struct buffer_head *, int);

		if (multi_ok) {
			size_t	size = sizeof(pagesync_t);

			psync = (pagesync_t *) kmalloc(size, GFP_NOFS);
			if (!psync)
				BUG();	/* Ugh - out of memory condition here */
			psync->pb = pb;
			psync->locking = locking;
			atomic_set(&psync->remain, 0);

			callback = public_bh ?
				   _end_io_multi_part : _end_io_multi_full;
		} else {
			callback = locking ? _end_io_locked : _end_io_nolock;
		}

		/* Indicate that there is another page in progress */
		atomic_inc(&PBP(pb)->pb_io_remaining);

#ifdef RQ_WRITE_ORDERED
		if (flush)
			set_bit(BH_Ordered_Flush, &bufferlist[cnt-1]->b_state);
#endif

		for (i = 0; i < cnt; i++) {
			bh = bufferlist[i];

			/* Complete the buffer_head, then submit the IO */
			if (psync) {
				init_buffer(bh, callback, psync);
				atomic_inc(&psync->remain);
			} else {
				init_buffer(bh, callback, pb);
			}

			bh->b_rdev = bh->b_dev;
			bh->b_rsector = bh->b_blocknr;
			set_bit(BH_Mapped, &bh->b_state);
			set_bit(BH_Req, &bh->b_state);

			if (rw == WRITE) {
				set_bit(BH_Uptodate, &bh->b_state);
			}
			generic_make_request(rw, bh);
		}
	} else {
		if (locking)
			unlock_page(page);
	}

	return err;
error:
	/* If we ever do get here then clean up what we already did */
	for (i = 0; i < cnt; i++) {
		atomic_set_buffer_clean(bufferlist[i]);
		bufferlist[i]->b_end_io(bufferlist[i], 0);
	}
	return err;
}

STATIC int
_page_buf_page_apply(
	page_buf_t		*pb,
	loff_t			offset,
	struct page		*page,
	size_t			pg_offset,
	size_t			pg_length,
	int			last)
{
	page_buf_daddr_t	bn = pb->pb_bn;
	kdev_t			dev = pb->pb_target->pbr_kdev;
	size_t			blocksize = pb->pb_target->pbr_blocksize;
	loff_t			pb_offset;
	size_t			ret_len = pg_length;

	assert(page);

	if ((blocksize == PAGE_CACHE_SIZE) &&
	    (pb->pb_buffer_length < PAGE_CACHE_SIZE) &&
	    (pb->pb_flags & PBF_READ) && pb->pb_locked) {
		bn -= (pb->pb_offset >> SECTOR_SHIFT);
		pg_offset = 0;
		pg_length = PAGE_CACHE_SIZE;
	} else {
		pb_offset = offset - pb->pb_file_offset;
		if (pb_offset) {
			bn += (pb_offset + SECTOR_MASK) >> SECTOR_SHIFT;
		}
	}

	if (pb->pb_flags & PBF_READ) {
		_pagebuf_page_io(page, pb, bn, dev, blocksize,
			(off_t)pg_offset, pg_length, pb->pb_locked, READ, 0);
	} else if (pb->pb_flags & PBF_WRITE) {
		int locking = (pb->pb_flags & _PBF_LOCKABLE) == 0;

		/* Check we need to lock pages */
		if (locking && (pb->pb_locked == 0))
			lock_page(page);
		_pagebuf_page_io(page, pb, bn, dev, blocksize,
			(off_t)pg_offset, pg_length, locking, WRITE,
			last && (pb->pb_flags & PBF_FLUSH));
	}

	return ret_len;
}

/*
 *	pagebuf_iorequest
 *
 *	pagebuf_iorequest is the core I/O request routine.
 *	It assumes that the buffer is well-formed and
 *	mapped and ready for physical I/O, unlike
 *	pagebuf_iostart() and pagebuf_iophysio().  Those
 *	routines call the pagebuf_ioinitiate routine to start I/O,
 *	if it is present, or else call pagebuf_iorequest()
 *	directly if the pagebuf_ioinitiate routine is not present.
 *
 *	This function will be responsible for ensuring access to the
 *	pages is restricted whilst I/O is in progress - for locking
 *	pagebufs the pagebuf lock is the mediator, for non-locking
 *	pagebufs the pages will be locked. In the locking case we
 *	need to use the pagebuf lock as multiple meta-data buffers
 *	will reference the same page.
 */
int
pagebuf_iorequest(			/* start real I/O		*/
	page_buf_t		*pb)	/* buffer to convey to device	*/
{
	int			status = 0;

	PB_TRACE(pb, PB_TRACE_REC(ioreq), 0);

	if (pb->pb_flags & PBF_DELWRI) {
		pagebuf_delwri_queue(pb, 1);
		return status;
	}

	if (pb->pb_flags & PBF_WRITE) {
		_pagebuf_wait_unpin(pb);
	}

	/* Set the count to 1 initially, this will stop an I/O
	 * completion callout which happens before we have started
	 * all the I/O from calling iodone too early
	 */
	atomic_set(&PBP(pb)->pb_io_remaining, 1);
	status = _pagebuf_segment_apply(pb);

	/* Drop our count and if everything worked we are done */
	if (atomic_dec_and_test(&PBP(pb)->pb_io_remaining) == 1) {
		pagebuf_iodone(pb);
	} else if ((pb->pb_flags & (PBF_SYNC|PBF_ASYNC)) == PBF_SYNC)  {
		run_task_queue(&tq_disk);
	}

	return status < 0 ? status : 0;
}

/*
 *	pagebuf_iowait
 *
 *	pagebuf_iowait waits for I/O to complete on the buffer supplied.
 *	It returns immediately if no I/O is pending.  In any case, it returns
 *	the error code, if any, or 0 if there is no error.
 */
int
pagebuf_iowait(
	page_buf_t		*pb)
{
	PB_TRACE(pb, PB_TRACE_REC(iowait), 0);
	run_task_queue(&tq_disk);
	down(&pb->pb_iodonesema);
	PB_TRACE(pb, PB_TRACE_REC(iowaited), (int)pb->pb_error);
	return pb->pb_error;
}

STATIC void *
pagebuf_mapout_locked(
	page_buf_t		*pb)
{
	void			*old_addr = NULL;

	if (pb->pb_flags & PBF_MAPPED) {
		if (pb->pb_flags & _PBF_ADDR_ALLOCATED)
			old_addr = pb->pb_addr - pb->pb_offset;
		pb->pb_addr = NULL;
		pb->pb_flags &= ~(PBF_MAPPED | _PBF_ADDR_ALLOCATED);
	}

	return old_addr;	/* Caller must free the address space,
				 * we are under a spin lock, probably
				 * not safe to do vfree here
				 */
}

caddr_t
pagebuf_offset(
	page_buf_t		*pb,
	off_t			offset)
{
	struct page		*page;

	offset += pb->pb_offset;

	page = pb->pb_pages[offset >> PAGE_CACHE_SHIFT];
	return (caddr_t) page_address(page) + (offset & (PAGE_CACHE_SIZE - 1));
}

/*
 *	pagebuf_segment
 *
 *	pagebuf_segment is used to retrieve the various contiguous
 *	segments of a buffer.  The variable addressed by the
 *	loff_t * should be initialized to 0, and successive
 *	calls will update to point to the segment following the one
 *	returned.
 */
STATIC void
pagebuf_segment(
	page_buf_t		*pb,	/* buffer to examine		*/
	loff_t			*boff_p,/* offset in buffer of next	*/
					/* next segment (updated)	*/
	struct page		**spage_p, /* page (updated)		*/
					/* (NULL if not in page array)	*/
	size_t			*soff_p,/* offset in page (updated)	*/
	size_t			*ssize_p) /* segment length (updated)	*/
{
	loff_t			kpboff;	/* offset in pagebuf		*/
	int			kpi;	/* page index in pagebuf	*/
	size_t			slen;	/* segment length		*/

	kpboff = *boff_p;

	kpi = page_buf_btoct(kpboff + pb->pb_offset);

	*spage_p = pb->pb_pages[kpi];

	*soff_p = page_buf_poff(kpboff + pb->pb_offset);
	slen = PAGE_CACHE_SIZE - *soff_p;
	if (slen > (pb->pb_count_desired - kpboff))
		slen = (pb->pb_count_desired - kpboff);
	*ssize_p = slen;

	*boff_p = *boff_p + slen;
}

/*
 *	pagebuf_iomove
 *
 *	Move data into or out of a buffer.
 */
void
pagebuf_iomove(
	page_buf_t		*pb,	/* buffer to process		*/
	off_t			boff,	/* starting buffer offset	*/
	size_t			bsize,	/* length to copy		*/
	caddr_t			data,	/* data address			*/
	page_buf_rw_t		mode)	/* read/write flag		*/
{
	loff_t			cboff;
	size_t			cpoff;
	size_t			csize;
	struct page		*page;

	cboff = boff;
	boff += bsize; /* last */

	while (cboff < boff) {
		pagebuf_segment(pb, &cboff, &page, &cpoff, &csize);
		assert(((csize + cpoff) <= PAGE_CACHE_SIZE));

		switch (mode) {
		case PBRW_ZERO:
			memset(page_address(page) + cpoff, 0, csize);
			break;
		case PBRW_READ:
			memcpy(data, page_address(page) + cpoff, csize);
			break;
		case PBRW_WRITE:
			memcpy(page_address(page) + cpoff, data, csize);
		}

		data += csize;
	}
}

/*
 *	_pagebuf_segment_apply
 *
 *	Applies _page_buf_page_apply to each segment of the page_buf_t.
 */
STATIC int
_pagebuf_segment_apply(			/* apply function to segments	*/
	page_buf_t		*pb)	/* buffer to examine		*/
{
	int			buf_index, sval, status = 0;
	loff_t			buffer_offset = pb->pb_file_offset;
	size_t			buffer_len = pb->pb_count_desired;
	size_t			page_offset, len, total = 0;
	size_t			cur_offset, cur_len;

	pagebuf_hold(pb);

	cur_offset = pb->pb_offset;
	cur_len = buffer_len;

	for (buf_index = 0; buf_index < pb->pb_page_count; buf_index++) {
		if (cur_len == 0)
			break;
		if (cur_offset >= PAGE_CACHE_SIZE) {
			cur_offset -= PAGE_CACHE_SIZE;
			continue;
		}

		page_offset = cur_offset;
		cur_offset = 0;

		len = PAGE_CACHE_SIZE - page_offset;
		if (len > cur_len)
			len = cur_len;
		cur_len -= len;

		sval = _page_buf_page_apply(pb, buffer_offset,
				pb->pb_pages[buf_index], page_offset, len,
				buf_index+1 == pb->pb_page_count);
		if (sval <= 0) {
			status = sval;
			break;
		} else {
			len = sval;
			total += len;
		}

		buffer_offset += len;
		buffer_len -= len;
	}

	pagebuf_rele(pb);

	if (!status)
		status = total;

	return (status);
}


/*
 * Pagebuf delayed write buffer handling
 */

void
pagebuf_delwri_queue(
	page_buf_t		*pb,
	int			unlock)
{
	PB_TRACE(pb, PB_TRACE_REC(delwri_q), unlock);
	spin_lock(&pb_daemon->pb_delwrite_lock);
	/* If already in the queue, dequeue and place at tail */
	if (!list_empty(&pb->pb_list)) {
		if (unlock) {
			atomic_dec(&pb->pb_hold);
		}
		list_del(&pb->pb_list);
	} else {
		pb_daemon->pb_delwri_cnt++;
	}
	list_add_tail(&pb->pb_list, &pb_daemon->pb_delwrite_l);
	PBP(pb)->pb_flushtime = jiffies + pb_params.p_un.age_buffer;
	spin_unlock(&pb_daemon->pb_delwrite_lock);

	if (unlock && (pb->pb_flags & _PBF_LOCKABLE)) {
		pagebuf_unlock(pb);
	}
}

void
pagebuf_delwri_dequeue(
	page_buf_t		*pb)
{
	PB_TRACE(pb, PB_TRACE_REC(delwri_uq), 0);
	spin_lock(&pb_daemon->pb_delwrite_lock);
	list_del_init(&pb->pb_list);
	pb->pb_flags &= ~PBF_DELWRI;
	pb_daemon->pb_delwri_cnt--;
	spin_unlock(&pb_daemon->pb_delwrite_lock);
}


/*
 * The pagebuf iodone daemon
 */

STATIC int pb_daemons[NR_CPUS];

STATIC int
pagebuf_iodone_daemon(
	void			*__bind_cpu)
{
	int			bind_cpu = (int) (long) __bind_cpu;
	int			cpu = cpu_logical_map(bind_cpu);
	DECLARE_WAITQUEUE	(wait, current);

	/*  Set up the thread  */
	daemonize();

	/* Avoid signals */
	spin_lock_irq(&current->sigmask_lock);
	sigfillset(&current->blocked);
	recalc_sigpending(current);
	spin_unlock_irq(&current->sigmask_lock);

	/* Migrate to the right CPU */
#ifdef SCHED_YIELD			/* XXX: actualy testing for O(1) sched */
	while (smp_processor_id() != cpu)
		schedule();
#else
	set_cpus_allowed(current, 1UL << cpu);
#endif

	sprintf(current->comm, "pagebuf_io_CPU%d", bind_cpu);
	INIT_LIST_HEAD(&pagebuf_iodone_tq[cpu]);
	init_waitqueue_head(&pagebuf_iodone_wait[cpu]);
	__set_current_state(TASK_INTERRUPTIBLE);
	mb();

	pb_daemons[cpu] = 1;

	for (;;) {
		add_wait_queue(&pagebuf_iodone_wait[cpu],
				&wait);

		if (TQ_ACTIVE(pagebuf_iodone_tq[cpu]))
			__set_task_state(current, TASK_RUNNING);
		schedule();
		remove_wait_queue(&pagebuf_iodone_wait[cpu],
				&wait);
		run_task_queue(&pagebuf_iodone_tq[cpu]);
		if (pb_daemons[cpu] == 0)
			break;
		__set_current_state(TASK_INTERRUPTIBLE);
	}

	pb_daemons[cpu] = -1;
	wake_up_interruptible(&pagebuf_iodone_wait[cpu]);
	return 0;
}

/* Defines for pagebuf daemon */
DECLARE_WAIT_QUEUE_HEAD(pbd_waitq);
STATIC int force_flush;

STATIC void
pagebuf_daemon_wakeup(
	int			flag)
{
	force_flush = flag;
	if (waitqueue_active(&pbd_waitq)) {
		wake_up_interruptible(&pbd_waitq);
	}
}

typedef void (*timeout_fn)(unsigned long);

STATIC int
pagebuf_daemon(
	void			*data)
{
	int			count;
	page_buf_t		*pb;
	struct list_head	*curr, *next, tmp;
	struct timer_list	pb_daemon_timer =
		{ {NULL, NULL}, 0, 0, (timeout_fn)pagebuf_daemon_wakeup };

	/*  Set up the thread  */
	daemonize();

	/* Avoid signals */
	spin_lock_irq(&current->sigmask_lock);
	sigfillset(&current->blocked);
	recalc_sigpending(current);
	spin_unlock_irq(&current->sigmask_lock);

	strcpy(current->comm, "pagebufd");
	current->flags |= PF_MEMALLOC;

	INIT_LIST_HEAD(&tmp);
	do {
		if (pb_daemon->active == 1) {
			del_timer(&pb_daemon_timer);
			pb_daemon_timer.expires = jiffies +
					pb_params.p_un.flush_interval;
			add_timer(&pb_daemon_timer);
			interruptible_sleep_on(&pbd_waitq);
		}

		if (pb_daemon->active == 0) {
			del_timer(&pb_daemon_timer);
		}

		spin_lock(&pb_daemon->pb_delwrite_lock);

		count = 0;
		list_for_each_safe(curr, next, &pb_daemon->pb_delwrite_l) {
			pb = list_entry(curr, page_buf_t, pb_list);

			PB_TRACE(pb, PB_TRACE_REC(walkq1), pagebuf_ispin(pb));

			if ((pb->pb_flags & PBF_DELWRI) && !pagebuf_ispin(pb) &&
			    (((pb->pb_flags & _PBF_LOCKABLE) == 0) ||
			     !pagebuf_cond_lock(pb))) {

				if (!force_flush && time_before(jiffies,
						PBP(pb)->pb_flushtime)) {
					pagebuf_unlock(pb);
					break;
				}

				list_del(&pb->pb_list);
				list_add(&pb->pb_list, &tmp);

				count++;
			}
		}

		spin_unlock(&pb_daemon->pb_delwrite_lock);
		while (!list_empty(&tmp)) {
			pb = list_entry(tmp.next,
							page_buf_t, pb_list);
			list_del_init(&pb->pb_list);
			pb->pb_flags &= ~PBF_DELWRI;
			pb->pb_flags |= PBF_WRITE;

			__pagebuf_iorequest(pb);
		}

		if (count)
			run_task_queue(&tq_disk);
		if (as_list_len > 0)
			purge_addresses();

		force_flush = 0;
	} while (pb_daemon->active == 1);

	pb_daemon->active = -1;
	wake_up_interruptible(&pbd_waitq);

	return 0;
}

void
pagebuf_delwri_flush(
	pb_target_t		*target,
	u_long			flags,
	int			*pinptr)
{
	page_buf_t		*pb;
	struct list_head	*curr, *next, tmp;
	int			pincount = 0;

	spin_lock(&pb_daemon->pb_delwrite_lock);
	INIT_LIST_HEAD(&tmp);

	list_for_each_safe(curr, next, &pb_daemon->pb_delwrite_l) {
		pb = list_entry(curr, page_buf_t, pb_list);

		/*
		 * Skip other targets, markers and in progress buffers
		 */

		if ((pb->pb_flags == 0) || (pb->pb_target != target) ||
		    !(pb->pb_flags & PBF_DELWRI)) {
			continue;
		}

		PB_TRACE(pb, PB_TRACE_REC(walkq2), pagebuf_ispin(pb));
		if (pagebuf_ispin(pb)) {
			pincount++;
			continue;
		}

		if (flags & PBDF_TRYLOCK) {
			if (!pagebuf_cond_lock(pb)) {
				pincount++;
				continue;
			}
		}

		list_del_init(&pb->pb_list);
		if (flags & PBDF_WAIT) {
			list_add(&pb->pb_list, &tmp);
			pb->pb_flags &= ~PBF_ASYNC;
		}

		spin_unlock(&pb_daemon->pb_delwrite_lock);

		if ((flags & PBDF_TRYLOCK) == 0) {
			pagebuf_lock(pb);
		}

		pb->pb_flags &= ~PBF_DELWRI;
		pb->pb_flags |= PBF_WRITE;

		__pagebuf_iorequest(pb);

		spin_lock(&pb_daemon->pb_delwrite_lock);
	}

	spin_unlock(&pb_daemon->pb_delwrite_lock);

	run_task_queue(&tq_disk);

	if (pinptr)
		*pinptr = pincount;

	if ((flags & PBDF_WAIT) == 0)
		return;

	while (!list_empty(&tmp)) {
		pb = list_entry(tmp.next, page_buf_t, pb_list);

		list_del_init(&pb->pb_list);
		pagebuf_iowait(pb);
		if (!pb->pb_relse)
			pagebuf_unlock(pb);
		pagebuf_rele(pb);
	}
}

STATIC int
pagebuf_daemon_start(void)
{
	if (!pb_daemon) {
		int		cpu;

		pb_daemon = (pagebuf_daemon_t *)
				kmalloc(sizeof(pagebuf_daemon_t), GFP_KERNEL);
		if (!pb_daemon) {
			return -1; /* error */
		}

		pb_daemon->active = 1;
		pb_daemon->io_active = 1;
		pb_daemon->pb_delwri_cnt = 0;
		pb_daemon->pb_delwrite_lock = SPIN_LOCK_UNLOCKED;

		INIT_LIST_HEAD(&pb_daemon->pb_delwrite_l);

		kernel_thread(pagebuf_daemon, (void *)pb_daemon,
				CLONE_FS|CLONE_FILES|CLONE_VM);
		for (cpu = 0; cpu < smp_num_cpus; cpu++) {
			if (kernel_thread(pagebuf_iodone_daemon,
					(void *)(long) cpu,
					CLONE_FS|CLONE_FILES|CLONE_VM) < 0) {
				printk("pagebuf_daemon_start failed\n");
			} else {
				while (!pb_daemons[cpu_logical_map(cpu)]) {
					/*
					 * Must be that ugly because yield()
					 * is broken in 2.4.19 (my fault, btw)
					 *
					 * XXX: Rip this out once we've merged
					 * up to 2.4.20.
					 *			--hch
					 */
#ifdef SCHED_YIELD
					current->policy |= SCHED_YIELD;
					schedule();
#else
					yield();
#endif
				}
			}
		}
	}
	return 0;
}

/*
 * pagebuf_daemon_stop
 * 
 * Note: do not mark as __exit, it is called from pagebuf_terminate.
 */
STATIC void
pagebuf_daemon_stop(void)
{
	if (pb_daemon) {
		int		cpu;

		pb_daemon->active = 0;
		pb_daemon->io_active = 0;

		wake_up_interruptible(&pbd_waitq);
		while (pb_daemon->active == 0) {
			interruptible_sleep_on(&pbd_waitq);
		}
		for (cpu = 0; cpu < smp_num_cpus; cpu++) {
			pb_daemons[cpu_logical_map(cpu)] = 0;
			wake_up(&pagebuf_iodone_wait[cpu_logical_map(cpu)]);
			while (pb_daemons[cpu_logical_map(cpu)] != -1) {
				interruptible_sleep_on(
				    &pagebuf_iodone_wait[cpu_logical_map(cpu)]);
			}
		}

		kfree(pb_daemon);
		pb_daemon = NULL;
	}
}


/*
 * Pagebuf sysctl interface
 */

STATIC int
pb_stats_clear_handler(
	ctl_table		*ctl,
	int			write,
	struct file		*filp,
	void			*buffer,
	size_t			*lenp)
{
	int			ret;
	int			*valp = ctl->data;

	ret = proc_doulongvec_minmax(ctl, write, filp, buffer, lenp);

	if (!ret && write && *valp) {
		printk("XFS Clearing pbstats\n");
		memset(&pbstats, 0, sizeof(pbstats));
		pb_params.p_un.stats_clear = 0;
	}

	return ret;
}

STATIC struct ctl_table_header *pagebuf_table_header;

STATIC ctl_table pagebuf_table[] = {
	{PB_FLUSH_INT, "flush_int", &pb_params.data[0],
	sizeof(ulong), 0644, NULL, &proc_doulongvec_ms_jiffies_minmax,
	&sysctl_intvec, NULL, &pagebuf_min[0], &pagebuf_max[0]},

	{PB_FLUSH_AGE, "flush_age", &pb_params.data[1],
	sizeof(ulong), 0644, NULL, &proc_doulongvec_ms_jiffies_minmax,
	&sysctl_intvec, NULL, &pagebuf_min[1], &pagebuf_max[1]},

	{PB_STATS_CLEAR, "stats_clear", &pb_params.data[3],
	sizeof(ulong), 0644, NULL, &pb_stats_clear_handler,
	&sysctl_intvec, NULL, &pagebuf_min[3], &pagebuf_max[3]},

#ifdef PAGEBUF_TRACE
	{PB_DEBUG, "debug", &pb_params.data[4],
	sizeof(ulong), 0644, NULL, &proc_doulongvec_minmax,
	&sysctl_intvec, NULL, &pagebuf_min[4], &pagebuf_max[4]},
#endif
	{0}
};

STATIC ctl_table pagebuf_dir_table[] = {
	{VM_PAGEBUF, "pagebuf", NULL, 0, 0555, pagebuf_table},
	{0}
};

STATIC ctl_table pagebuf_root_table[] = {
	{CTL_VM, "vm",	NULL, 0, 0555, pagebuf_dir_table},
	{0}
};

#ifdef CONFIG_PROC_FS
STATIC int
pagebuf_readstats(
	char			*buffer,
	char			**start,
	off_t			offset,
	int			count,
	int			*eof,
	void			*data)
{
	int			i, len;

	len = 0;
	len += sprintf(buffer + len, "pagebuf");
	for (i = 0; i < sizeof(pbstats) / sizeof(u_int32_t); i++) {
		len += sprintf(buffer + len, " %u",
			*(((u_int32_t*)&pbstats) + i));
	}
	buffer[len++] = '\n';

	if (offset >= len) {
		*start = buffer;
		*eof = 1;
		return 0;
	}
	*start = buffer + offset;
	if ((len -= offset) > count)
		return count;
	*eof = 1;

	return len;
}
#endif	/* CONFIG_PROC_FS */

STATIC void
pagebuf_shaker(void)
{
	pagebuf_daemon_wakeup(1);
}


/*
 *	Initialization and Termination
 */

int __init
pagebuf_init(void)
{
	int			i;

	pagebuf_table_header = register_sysctl_table(pagebuf_root_table, 1);

#ifdef CONFIG_PROC_FS
	if (proc_mkdir("fs/pagebuf", 0))
		create_proc_read_entry(
			"fs/pagebuf/stat", 0, 0, pagebuf_readstats, NULL);
#endif

	pagebuf_cache = kmem_cache_create("page_buf_t",
			sizeof(page_buf_private_t), 0,
			SLAB_HWCACHE_ALIGN, NULL, NULL);
	if (pagebuf_cache == NULL) {
		printk("pagebuf: couldn't init pagebuf cache\n");
		pagebuf_terminate();
		return -ENOMEM;
	}

	if (_pagebuf_prealloc_bh(NR_RESERVED_BH) < NR_RESERVED_BH) {
		printk("pagebuf: couldn't pre-allocate %d buffer heads\n",
			NR_RESERVED_BH);
		pagebuf_terminate();
		return -ENOMEM;
	}

	init_waitqueue_head(&pb_resv_bh_wait);

	for (i = 0; i < NHASH; i++) {
		spin_lock_init(&pbhash[i].pb_hash_lock);
		INIT_LIST_HEAD(&pbhash[i].pb_hash);
	}

#ifdef PAGEBUF_TRACE
# if 1
	pb_trace.buf = (pagebuf_trace_t *)kmalloc(
			PB_TRACE_BUFSIZE * sizeof(pagebuf_trace_t), GFP_KERNEL);
# else
	/* Alternatively, for really really long trace bufs */
	pb_trace.buf = (pagebuf_trace_t *)vmalloc(
			PB_TRACE_BUFSIZE * sizeof(pagebuf_trace_t));
# endif
	memset(pb_trace.buf, 0, PB_TRACE_BUFSIZE * sizeof(pagebuf_trace_t));
	pb_trace.start = 0;
	pb_trace.end = PB_TRACE_BUFSIZE - 1;
#endif

	pagebuf_daemon_start();
	kmem_shake_register(pagebuf_shaker);
	return 0;
}

/*
 *	pagebuf_terminate. 
 * 
 *	Note: do not mark as __exit, this is also called from the __init code.
 */
void
pagebuf_terminate(void)
{
	pagebuf_daemon_stop();

	kmem_cache_destroy(pagebuf_cache);
	kmem_shake_deregister(pagebuf_shaker);

	unregister_sysctl_table(pagebuf_table_header);
#ifdef	CONFIG_PROC_FS
	remove_proc_entry("fs/pagebuf/stat", NULL);
	remove_proc_entry("fs/pagebuf", NULL);
#endif
}


/*
 *	Module management (for kernel debugger module)
 */
EXPORT_SYMBOL(pagebuf_offset);
