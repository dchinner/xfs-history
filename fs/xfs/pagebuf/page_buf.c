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
 *	page_buf.c
 *
 *	The page_buf module provides an abstract buffer cache model on top of
 *	the Linux page cache.  Cached blocks for a file are hashed to the
 *	inode for that file, and can be held dirty in delayed write mode in
 *	the page cache.  Cached metadata blocks for a file system are hashed
 *	to the inode for the mounted device.  The page_buf module assembles
 *	buffer (page_buf_t) objects on demand to aggregate such cached pages
 *	for I/O.
 *
 *
 *      Written by Steve Lord, Jim Mostek, Russell Cattelan
 *		    and Rajagopal Ananthanarayanan ("ananth") at SGI.
 *
 */

#include <linux/module.h>
#include <linux/stddef.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <asm/uaccess.h>
#include <linux/string.h>
#include <linux/pagemap.h>
#include <linux/init.h>
#include <linux/vmalloc.h>
#include <linux/blkdev.h>
#include <linux/locks.h>
#include <linux/swap.h>
#include <asm/softirq.h>
#include <linux/sysctl.h>
#include <linux/proc_fs.h>
#include <linux/xfs_support/support.h>

#include "avl.h"
#include "page_buf.h"

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,4,6)
#define SLAB_NOFS	SLAB_PAGE_IO
#define GFP_NOFS	GFP_PAGE_IO
#endif

/*
 * Debug code
 */

#define PB_DEFINE_TRACES
#include "page_buf_trace.h"

#ifdef PAGEBUF_TRACE
static	spinlock_t		pb_trace_lock = SPIN_LOCK_UNLOCKED;
struct pagebuf_trace_buf	pb_trace;
EXPORT_SYMBOL(pb_trace);
EXPORT_SYMBOL(pb_trace_func);
#define CIRC_INC(i)     (((i) + 1) & (PB_TRACE_BUFSIZE - 1))

void	pb_trace_func(page_buf_t *pb, int event, void *misc, void *ra)
{
	int	j;
	unsigned long flags;

	if (!pb_params.p_un.debug) return;

	if (ra == NULL) ra = (void *)__builtin_return_address(0);

	spin_lock_irqsave(&pb_trace_lock, flags);
	j = pb_trace.start;
	pb_trace.start = CIRC_INC(j);
	spin_unlock_irqrestore(&pb_trace_lock, flags);

	pb_trace.buf[j].pb = (unsigned long) pb;
	pb_trace.buf[j].event = event;
	pb_trace.buf[j].flags = pb->pb_flags;
	pb_trace.buf[j].hold = pb->pb_hold;
	pb_trace.buf[j].lock_value = PBP(pb)->pb_sema.count.counter;
	pb_trace.buf[j].task = (void *)current;
	pb_trace.buf[j].misc = misc;
	pb_trace.buf[j].ra = ra;
	pb_trace.buf[j].offset = pb->pb_file_offset;
	pb_trace.buf[j].size = pb->pb_buffer_length;
}
#define ENTER(x)	printk("Entering " #x "\n")
#define EXIT(x)		printk("Exiting  " #x "\n")
#else
#define ENTER(x)	do { } while (0)
#define EXIT(x)		do { } while (0)
#endif	/* PAGEBUF_TRACE */

#ifdef PAGEBUF_TRACKING
#define MAX_PB	10000
page_buf_t	*pb_array[MAX_PB];
EXPORT_SYMBOL(pb_array);

void	pb_tracking_get(page_buf_t *pb)
{
	int	i;

	for (i = 0; (pb_array[i] != 0) && (i < MAX_PB); i++) { }
	if (i == MAX_PB)
		printk("pb 0x%p not recorded in pb_array\n", pb);
	else {
		//printk("pb_get 0x%p in pb_array[%d]\n", pb, i);
		pb_array[i] = pb;
	}
}

void	pb_tracking_free(page_buf_t *pb)
{
	int	i;

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

void pagebuf_terminate(void);

/*
 *	External locking functions
 */

extern int _pagebuf_get_lockable_buffer(
			pb_target_t *, loff_t, size_t, page_buf_flags_t,
			page_buf_t **);
extern int _pagebuf_find_lockable_buffer(
			pb_target_t *, loff_t, size_t, page_buf_flags_t,
			page_buf_t **);
extern int _pagebuf_free_lockable_buffer(
			page_buf_t *, unsigned long);


/*
 *	File wide globals
 */

STATIC kmem_cache_t *pagebuf_cache = NULL;
STATIC pagebuf_daemon_t *pb_daemon = NULL;

/*
 *	For pre-allocated buffer head pool
 */

#define NR_RESERVED_BH	64
static wait_queue_head_t	pb_resv_bh_wait;
static spinlock_t		pb_resv_bh_lock = SPIN_LOCK_UNLOCKED;
struct buffer_head		*pb_resv_bh = NULL;	/* list of bh */
int				pb_resv_bh_cnt = 0;	/* # of bh available */

STATIC void pagebuf_daemon_wakeup(int);

/*
 * Pagebuf module configuration parameters, exported via
 * /proc/sys/vm/pagebuf
 */

unsigned long pagebuf_min[P_PARAM] = { HZ/2, 1*HZ,  1, 0 };
unsigned long pagebuf_max[P_PARAM] = { HZ*30, HZ*300, 4096, 1 };

pagebuf_param_t pb_params = {{ HZ, 15 * HZ, 256, 0 }};

/*
 * Pagebuf statistics variables
 */

struct pbstats pbstats;

#define REMAPPING_SUPPORT

#ifdef REMAPPING_SUPPORT
STATIC void *pagebuf_mapout_locked(page_buf_t *);

STATIC  spinlock_t              as_lock = SPIN_LOCK_UNLOCKED;
typedef struct a_list {
	void	*vm_addr;
	struct a_list	*next;
} a_list_t;
STATIC  a_list_t	*as_free_head;
STATIC  int		as_list_len;

STATIC void
free_address(void *addr)
{
	unsigned long flags;
	a_list_t	*aentry;

	spin_lock_irqsave(&as_lock, flags);
	aentry = kmalloc(sizeof(a_list_t), GFP_ATOMIC);
	aentry->next = as_free_head;
	aentry->vm_addr = addr;
	as_free_head = aentry;
	as_list_len++;
	spin_unlock_irqrestore(&as_lock, flags);
}

STATIC void
purge_addresses(void)
{
	unsigned long flags;
	a_list_t	*aentry, *old;

	if (as_free_head == NULL) return;

	spin_lock_irqsave(&as_lock, flags); 
	aentry = as_free_head;
	as_free_head = NULL;
	as_list_len = 0;
	spin_unlock_irqrestore(&as_lock, flags);

	while ((old = aentry) != NULL) {
		vfree(aentry->vm_addr);
		aentry = aentry->next;
		kfree(old);
	}
}
#endif

/*
 *	Locking model:
 *
 *	Buffers associated with inodes for which buffer locking
 *	is not enabled are not protected by semaphores, and are
 *	assumed to be exclusively owned by the caller.  There is
 *	spinlock in the buffer, for use by the caller when concurrent
 *	access is possible.
 */

/*
 *	Internal pagebuf object manipulation
 */

/*
 *	_pagebuf_get_object
 *
 *	This routine allocates a page_buf_t object and initializes it,
 *	with no other operations implied.
 */

int
_pagebuf_get_object(
    pb_target_t *target,
    loff_t range_base,
    size_t range_length,
    page_buf_flags_t flags,
    page_buf_t ** pb_p)
{
	page_buf_t *pb;
	
	pb = kmem_cache_alloc(pagebuf_cache,
		(flags & PBF_DONT_BLOCK) ? SLAB_NOFS : SLAB_KERNEL);
	if (pb == NULL)
		return (-ENOMEM);
	pb_tracking_get(pb);

	memset(pb, 0, sizeof(page_buf_private_t));
	pb->pb_hold = 1;
	spin_lock_init(&PBP(pb)->pb_lock);
	init_MUTEX_LOCKED(&PBP(pb)->pb_iodonesema);
	INIT_LIST_HEAD(&pb->pb_list);
	init_MUTEX_LOCKED(&PBP(pb)->pb_sema); /* held, no waiters */
	PB_SET_OWNER(pb);
	pb->pb_target = target;
	if (target) {
		pb->pb_dev = target->pbr_device;
	}
	pb->pb_file_offset = range_base;
	pb->pb_buffer_length = pb->pb_count_desired = range_length; 
	/* set buffer_length and count_desired to the same value initially 
	 * io routines should use count_desired, which will the same in
	 * most cases but may be reset (e.g. XFS recovery)
	 */
	pb->pb_flags = (flags & ~(PBF_ENTER_PAGES|PBF_MAPPED|PBF_DONT_BLOCK)) |
			PBF_NONE;
	pb->pb_bn = PAGE_BUF_DADDR_NULL;
	atomic_set(&PBP(pb)->pb_pin_count, 0);
	init_waitqueue_head(&PBP(pb)->pb_waiters);

	*pb_p = pb;

	PB_STATS_INC(pbstats.pb_create);
	PB_TRACE(pb, PB_TRACE_REC(get), target);
	return (0);
}

/*
 * Allocate a page array capable of holding a specified number
 * of pages, and point the page buf at it.
 */
STATIC int
_pagebuf_get_pages(page_buf_t * pb, int page_count, int flags)
{

	int	gpf_mask = (flags & PBF_DONT_BLOCK) ?
				SLAB_NOFS : SLAB_KERNEL;

	/* assure that we have a page list */
	if (pb->pb_pages == NULL) {
		pb->pb_offset = page_buf_poff(pb->pb_file_offset);
		pb->pb_page_count = page_count;
		if (page_count <= PB_PAGES) {
			pb->pb_pages = pb->pb_page_array;
		} else {
			pb->pb_pages = kmalloc(sizeof(struct page *) *
					page_count, gpf_mask);
			if (pb->pb_pages == NULL)
				return (-ENOMEM);
		}
		memset(pb->pb_pages, 0, sizeof(struct page *) * page_count);
	}
	return (0);
}

/*
 * Walk a pagebuf releasing all the pages contained
 * within it.
 */
STATIC inline void _pagebuf_freepages(page_buf_t *pb)
{
	int buf_index;
	struct page *page;

	for (buf_index = 0; buf_index < pb->pb_page_count; buf_index++) {
		page = pb->pb_pages[buf_index];
		if (page != NULL) {
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
 *	Caller must call with pb_lock held.  If pb_hold is non-zero after this
 *	routine decrements it, the page_buf_t is not freed, although it
 *	is marked as having been freed.
 */

void _pagebuf_free_object(
	page_buf_t * pb,	/* buffer to deallocate         */
	unsigned long flags)	/* interrupt state to restore	*/
{
#ifdef REMAPPING_SUPPORT
	void *vaddr = NULL;
#endif

	PB_TRACE(pb, PB_TRACE_REC(free_obj), 0);
	if (!(pb->pb_flags & PBF_FREED)) {
#ifdef REMAPPING_SUPPORT
		/* release any virtual mapping */ ;
		if (pb->pb_flags & _PBF_ADDR_ALLOCATED)
			vaddr = pagebuf_mapout_locked(pb);
#endif

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

		pb->pb_flags |= PBF_FREED;
	}
	pb->pb_hold--;
	if (pb->pb_hold == 0) {
		/* Drop the spinlock before calling free lockable,
		 * as it needs to get the pbr_lock. We have set
		 * PBF_FREED, so anyone doing a lookup should
		 * skip this pagebuf.
		 */
		spin_unlock(&PBP(pb)->pb_lock);
		if (pb->pb_flags & _PBF_LOCKABLE)
			_pagebuf_free_lockable_buffer(pb, flags);
		pb_tracking_free(pb);
		kmem_cache_free(pagebuf_cache, pb);
	} else {
		spin_unlock_irqrestore(&PBP(pb)->pb_lock, flags);
	}

#ifdef REMAPPING_SUPPORT
	if (vaddr) {
		free_address(vaddr);
	}
#endif
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

int
_pagebuf_lookup_pages(
    page_buf_t * pb,
    struct address_space *aspace,
    page_buf_flags_t flags)
{
	loff_t next_buffer_offset;
	unsigned long page_count;
	int rval = 0;
	unsigned long pi;
	unsigned long index;
	int all_mapped, good_pages;
	struct page *cp, **hash, *cached_page;
	int gfp_mask;
	int	retry_count = 0;

	/* For pagebufs where we want to map an address, do not use
	 * highmem pages - so that we do not need to use kmap resources
	 * to access the data.
	 *
	 * For pages were the caller has indicated there may be resource
	 * contention (e.g. called from a transaction) do not flush
	 * delalloc pages to obtain memory.
	 */

	if (flags & PBF_DONT_BLOCK) {
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
		if ((flags & PBF_MAPPED) && !(pb->pb_flags & PBF_MAPPED)) {
			all_mapped = 1;
			goto mapit;
		}
		return (0);
	}

	/* assure that we have a page list */
	rval = _pagebuf_get_pages(pb, page_count, flags);
	if (rval != 0)
		return (rval);

	rval = pi = 0;
	cached_page = NULL;
	/* enter the pages in the page list */
	index = (pb->pb_file_offset - pb->pb_offset) >> PAGE_CACHE_SHIFT;
	for (all_mapped = 1; pi < page_count; pi++, index++) {
		if (pb->pb_pages[pi] == 0) {
			hash = page_hash(aspace, index);
		      retry:
			cp = __find_lock_page(aspace, index, hash);
			if (!cp) {
				PB_STATS_INC(pbstats.pb_page_alloc);
				if (!cached_page) {
					/* allocate a new page */
					cached_page = alloc_pages(gfp_mask, 0);

					if (!cached_page) {
						if (++retry_count < 6) {
							pagebuf_daemon_wakeup(1);
							current->state = TASK_UNINTERRUPTIBLE;
							schedule_timeout(10);
							goto retry;
						}

						rval = -ENOMEM;
						all_mapped = 0;
						continue;
					}
				}
				cp = cached_page;
				if (add_to_page_cache_unique(cp,
					aspace, index, hash))
					goto retry;
				cached_page = NULL;
			} else {
				PB_STATS_INC(pbstats.pb_page_found);
			}

			pb->pb_pages[pi] = cp;
		} else {
			cp = pb->pb_pages[pi];
			while (TryLockPage(cp)) {
				___wait_on_page(cp);
			}
		}

		/* Test for the page being valid. There is a special case
		 * in here for the case where we are reading a pagebuf
		 * smaller than a page. We want to populate the whole page
		 * here rather than just the part the caller wanted. That
		 * way we do not need to deal with partially valid pages.
		 * We keep the page locked, and in the read path fake out
		 * the lower layers to issue an I/O for the whole page.
		 */
		if (!Page_Uptodate(cp)) {
			good_pages--;
			if ((pb->pb_buffer_length < PAGE_CACHE_SIZE) &&
			    (flags & PBF_READ) && !PageSlab(cp)) {
				pb->pb_locked = 1;
			}
		}
		if (!pb->pb_locked)
			UnlockPage(cp);
	}
	if (cached_page)
		page_cache_release(cached_page);

mapit:
	pb->pb_flags |= _PBF_MEM_ALLOCATED;
	if (all_mapped) {
		pb->pb_flags |= _PBF_ALL_PAGES_MAPPED;
		/* A single page buffer is always mappable */
		if (page_count == 1) {
			pb->pb_addr =
			    (caddr_t) page_address(pb->pb_pages[0]) + 
					pb->pb_offset;
			pb->pb_flags |= PBF_MAPPED;
		}
#ifdef REMAPPING_SUPPORT
		else if (flags & PBF_MAPPED) {
			if (as_list_len > 64)
				purge_addresses();
			pb->pb_addr = remap_page_array(pb->pb_pages,
							page_count, gfp_mask);
			if (!pb->pb_addr)
				BUG();
			pb->pb_addr += pb->pb_offset;
			pb->pb_flags |= PBF_MAPPED | _PBF_ADDR_ALLOCATED;
		}
#else
		else if (flags & PBF_MAPPED) {
			printk("request for a mapped pagebuf > page size\n");
			BUG();
		}
#endif
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

	return (rval);
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
_pagebuf_prealloc_bh(int count)
{
	int i = 0;

	while ( i < count) {
		struct buffer_head *bh;
		bh = kmem_cache_alloc(bh_cachep, SLAB_KERNEL);
		if (!bh) 
			break;
		bh->b_pprev = &pb_resv_bh;
		bh->b_next = pb_resv_bh;
		pb_resv_bh = bh;
		pb_resv_bh_cnt++;

		i++;
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

STATIC struct buffer_head *_pagebuf_get_prealloc_bh(void)
{
	struct buffer_head *bh = NULL;
	unsigned long flags;
	struct task_struct *tsk = current;
	DECLARE_WAITQUEUE(wait, tsk);

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

STATIC inline void _pagebuf_free_bh(struct buffer_head *bh)
{
	unsigned long flags;

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
 *	pagebuf_find
 *
 *	pagebuf_find returns a buffer matching the specified range of
 *	data for the specified target, if any of the relevant blocks
 *	are in memory.  The buffer may have unallocated holes, if
 *	some, but not all, of the blocks are in memory.  Even where
 *	pages are present in the buffer, not all of every page may be
 *	valid.  The file system may use pagebuf_segment to visit the
 *	various segments of the buffer.  pagebuf_find will return an
 *	empty buffer (with no storage allocated) if the fifth argument
 *	is TRUE. 
 */

page_buf_t *pagebuf_find(	/* find buffer for block if     */
				/* the block is in memory       */
    pb_target_t *target,	/* target for block              */
    loff_t ioff,		/* starting offset of range     */
    size_t isize,		/* length of range              */
    page_buf_flags_t flags)	/* PBF_LOCK, PBF_ALWAYS_ALLOC   */
{
	page_buf_t *pb = NULL;

	ioff <<= 9;
	isize <<= 9;

	spin_lock_irq(&target->pbr_lock);
	_pagebuf_find_lockable_buffer(target, ioff, isize, flags, &pb);

	return (pb);
}


/*
 *	pagebuf_get
 *
 *	pagebuf_get assembles a buffer covering the specified range.
 *	Some or all of the blocks in the range may be valid.  The file
 *	system may use pagebuf_segment to visit the various segments
 *	of the buffer.  Storage in memory for all portions of the
 *	buffer will be allocated, although backing storage may not be. 
 *	If PBF_READ is set in flags, pagebuf_read
 */

page_buf_t *pagebuf_get(	/* allocate a buffer            */
    pb_target_t *target,	/* target for buffer (or NULL)   */
    loff_t ioff,		/* starting offset of range     */
    size_t isize,		/* length of range              */
    page_buf_flags_t flags) 	/* PBF_LOCK, PBF_TRYLOCK, PBF_READ, */
				/* PBF_LONG_TERM, PBF_SEQUENTIAL, */
				/* PBF_MAPPED */
{
	int rval;
	page_buf_t *pb;

	isize <<= 9;

	rval = _pagebuf_get_lockable_buffer(target, ioff << 9,
						isize, flags, &pb);

	if (rval != 0)
		return (NULL);

	PB_STATS_INC(pbstats.pb_get);

	/* fill in any missing pages */
	rval = _pagebuf_lookup_pages(pb, PB_ADDR_SPACE(pb), flags);
	if (rval != 0) {
		pagebuf_free(pb);
		return (NULL);
	}

	/* Always fill in the block number now, the mapped cases can do
	 * their own overlay of this later.
	 */

	pb->pb_bn = ioff;
	pb->pb_count_desired = pb->pb_buffer_length;

	if (flags & PBF_READ) {
		if (PBF_NOT_DONE(pb)) {
			PB_TRACE(pb, PB_TRACE_REC(get_read), flags);
			pagebuf_iostart(pb, flags);
		} else if (flags & PBF_ASYNC) {
			/* Read ahead call which is already satisfied,
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

page_buf_t *pagebuf_lookup(
    struct inode *inode,
    loff_t ioff,
    size_t isize,
    int flags)
{
	page_buf_t	*pb = NULL;
	int		status;

	_pagebuf_get_object(NULL, ioff, isize, flags, &pb);
	if (pb) {
		pb->pb_dev = inode->i_sb->s_dev;
		if (flags & PBF_ENTER_PAGES) {
			status = _pagebuf_lookup_pages(pb, &inode->i_data, 0);
			if (status != 0) {
				pagebuf_free(pb);
				return (NULL);
			}
		}
	}
	return (pb);
}

/*
 * If we are not low on memory then do the readahead in a deadlock
 * safe manner.
 */
void
pagebuf_readahead(
    pb_target_t * target,	/* target for buffer (or NULL)   */
    loff_t ioff,		/* starting offset of range     */
    size_t isize,		/* length of range              */
    int	   flags)		/* extra flags for the read	*/
{
	if (start_aggressive_readahead(GFP_KERNEL)) {
		(void)pagebuf_get(target, ioff, isize,
			flags | PBF_TRYLOCK | PBF_READ |
			PBF_ASYNC | PBF_MAPPABLE);
	}
}

page_buf_t *
pagebuf_get_empty(pb_target_t *target)
{
	int rval;
	int flags = _PBF_LOCKABLE;
	page_buf_t *pb;

	rval = _pagebuf_get_object(target, 0, 0, flags, &pb);
	return ((rval != 0) ? NULL : pb);
}

int
pagebuf_associate_memory(
	page_buf_t *pb,
	void *mem,
	size_t len)
{
	int	rval;
	int i = 0;
	size_t ptr; 
	size_t end, end_cur;
	off_t	offset;
	int page_count = PAGE_CACHE_ALIGN(len) >> PAGE_CACHE_SHIFT;

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
	if (0 != rval) {
		return (rval);
	}
	pb->pb_offset = offset;
	ptr = (size_t) mem & PAGE_CACHE_MASK;
	end = PAGE_CACHE_ALIGN((size_t) mem + len);
	end_cur = end;
	/* set up first page */
	pb->pb_pages[0] = virt_to_page(mem);

	ptr += PAGE_CACHE_SIZE;
	pb->pb_page_count = ++i;
	while (ptr < end) {
		pb->pb_pages[i] = virt_to_page(ptr);
		pb->pb_page_count = ++i;
		ptr += PAGE_CACHE_SIZE;
	}
	pb->pb_locked = 0;

	pb->pb_count_desired = pb->pb_buffer_length = len;
	pb->pb_flags |= PBF_MAPPED;

	return 0;
}

page_buf_t *
pagebuf_get_no_daddr(size_t len, pb_target_t *target)
{
	int rval;
	void *rmem = NULL;
	int flags = _PBF_LOCKABLE | PBF_FORCEIO;
	page_buf_t *pb;
	size_t tlen  = 0;

	if (len > 0x20000)
		return(NULL);

	rval = _pagebuf_get_object(target, 0, len, flags, &pb);

	if (0 != rval)
		return (NULL);

	do {
		if (tlen == 0) {
			tlen = len; /* first time */
		} else {
			kfree(rmem); /* free the mem from the previous try */
			tlen <<= 1; /* double the size and try again */
			/*
			printk(
			"pb_get_no_daddr NOT block 0x%p mask 0x%p len %d\n",
				rmem, ((size_t)rmem & (size_t)~BBMASK), len);
			*/
		}
		if ((rmem = kmalloc(tlen, GFP_KERNEL)) == 0) {
			pagebuf_free(pb);
			return (NULL);
		}
	} while ((size_t)rmem != ((size_t)rmem & (size_t)~BBMASK));

	if ((rval = pagebuf_associate_memory(pb, rmem, len)) != 0) {
		kfree(rmem);
		pagebuf_free(pb);
		return (NULL);
	}
	/* otherwise pagebuf_free just ignores it */
	pb->pb_flags |= _PBF_MEM_ALLOCATED;

	PB_TRACE(pb, PB_TRACE_REC(no_daddr), rmem);

	return (pb);
}


/*
 *	pagebuf_hold
 *
 *	Increment reference count on buffer, to hold the buffer concurrently
 *	with another thread which may release (free) the buffer asynchronously.
 *
 *	Must hold the buffer already to call this function.
 */

void pagebuf_hold(page_buf_t * pb)
{
	unsigned long flags;

	if (pb != NULL) {
		spin_lock_irqsave(&PBP(pb)->pb_lock, flags);
		assert(pb->pb_hold > 0);
		pb->pb_hold++;
		spin_unlock_irqrestore(&PBP(pb)->pb_lock, flags);

		PB_TRACE(pb, PB_TRACE_REC(hold), 0);
	}
}


/*
 *	pagebuf_free
 *
 *	pagebuf_free releases the specified buffer.  The modification
 *	state of any associated pages is left unchanged.
 */

void pagebuf_free(	/* deallocate a buffer          */
    page_buf_t * pb)	/* buffer to deallocate           */
{
	unsigned long flags;

	spin_lock_irqsave(&PBP(pb)->pb_lock, flags);
	_pagebuf_free_object(pb, flags);
}


/*
 *	pagebuf_rele
 * 
 *	pagebuf_rele releases a hold on the specified buffer.  If the
 *	the hold count is 1, pagebuf_rele calls pagebuf_free.
 */

void pagebuf_rele(page_buf_t * pb)
{
	int	do_free;
	unsigned long flags;

	PB_TRACE(pb, PB_TRACE_REC(rele), pb->pb_relse);
	spin_lock_irqsave(&PBP(pb)->pb_lock, flags);

	if (pb->pb_hold == 1) {
		do_free = 1;
		if (pb->pb_relse) {
			spin_unlock_irqrestore(&PBP(pb)->pb_lock, flags);
			(*(pb->pb_relse)) (pb);
			do_free = 0;
		}
		if (pb->pb_flags & PBF_DELWRI) {
			pb->pb_flags |= PBF_ASYNC;
			if (do_free)
				spin_unlock_irqrestore(&PBP(pb)->pb_lock,flags);
			pagebuf_delwri_queue(pb, 0);
			do_free = 0;
		}

		if (do_free) {
			_pagebuf_free_object(pb, flags);
		}
	} else {
		pb->pb_hold--;
		spin_unlock_irqrestore(&PBP(pb)->pb_lock, flags);
	}
}


/*
 *	Pinning Buffer Storage in Memory
 */

/*
 *	pagebuf_pin
 *
 *	pagebuf_pin locks all of the memory represented by a buffer in
 *	memory.  Multiple calls to pagebuf_pin and pagebuf_unpin, for
 *	the same or different buffers affecting a given page, will
 *	properly count the number of outstanding "pin" requests.  The
 *	buffer may be released after the pagebuf_pin and a different
 *	buffer used when calling pagebuf_unpin, if desired.
 *	pagebuf_pin should be used by the file system when it wants be
 *	assured that no attempt will be made to force the affected
 *	memory to disk.  It does not assure that a given logical page
 *	will not be moved to a different physical page.  Only the
 *	raw_count field of mem_map_t can in general assure that a
 *	logical page will not be moved to a different physical page. 
 */

void pagebuf_pin(	/* pin buffer in memory         */
     page_buf_t * pb)	/* buffer to pin          */
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

void pagebuf_unpin(		/* unpin buffered data          */
    page_buf_t * pb)		/* buffer to unpin                */
{
	if (atomic_dec_and_test(&PBP(pb)->pb_pin_count)) {
		wake_up(&PBP(pb)->pb_waiters);
	}
	PB_TRACE(pb, PB_TRACE_REC(unpin), PBP(pb)->pb_pin_count.counter);
}

int
pagebuf_ispin(page_buf_t *pb) {
	return atomic_read(&PBP(pb)->pb_pin_count);
}
/*
 *	pagebuf_wait_unpin
 *
 *	pagebuf_wait_unpin waits until all of the memory associated
 *	with the buffer is not longer locked in memory.  It returns
 *	immediately if none of the affected pages are locked. 
 */

static inline void	_pagebuf_wait_unpin(page_buf_t * pb)
{
	DECLARE_WAITQUEUE(wait, current);

	if (atomic_read(&PBP(pb)->pb_pin_count) == 0) {
		return;
	}

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

void pagebuf_wait_unpin(	/* wait for buffer to be unpinned */
    page_buf_t * pb)		/* buffer for which to wait       */
{
	_pagebuf_wait_unpin(pb);
}

/*
 * 	Buffer Utility Routines 
 */

/*
 *	pagebuf_geterror
 *
 *	pagebuf_geterror returns the error stored in the buffer, or 0 if
 *	there is no error.
 */

int pagebuf_geterror(		/* return buffer error          */
    page_buf_t * pb)		/* buffer                       */
{
	if (!pb) 
		return ENOMEM;
	return (pb->pb_error);
}


/*
 *	pagebuf_iodone
 *
 *	pagebuf_iodone marks a buffer for which I/O is in progress
 *	done with respect to that I/O.  The pb_done routine, if
 *	present, will be called as a side-effect. 
 */

void pagebuf_iodone(		/* mark buffer I/O complete     */
    page_buf_t * pb)		/* buffer to mark 	        */
{
	pb->pb_flags &= ~(PBF_READ | PBF_WRITE);
	if (pb->pb_error == 0) {
		pb->pb_flags &=
		    ~(PBF_PARTIAL | PBF_NONE);
	}

	PB_TRACE(pb, PB_TRACE_REC(done), pb->pb_iodone);

	/* If we were on the delwri list, dequeue if there is no one waiting */
	if ((pb->pb_flags & PBF_ASYNC) &&
	    (pb->pb_list.next != &pb->pb_list))
		pagebuf_delwri_dequeue(pb);

	if (pb->pb_iodone) {
		(*(pb->pb_iodone)) (pb);
		return;
	}

	if (pb->pb_flags & PBF_ASYNC) {
		if ((pb->pb_flags & _PBF_LOCKABLE) && !pb->pb_relse)
			pagebuf_unlock(pb);
		pagebuf_rele(pb);
	} else {
		up(&PBP(pb)->pb_iodonesema);
	}
}

/*
 *	pagebuf_ioerror
 *
 *	pagebuf_ioerror sets the error code for a buffer.
 */

void pagebuf_ioerror(	/* mark buffer in error (or not) */
    page_buf_t * pb,	/* buffer to mark               */
    int serror)		/* error to store (0 if none)     */
{
	pb->pb_error = serror;
	PB_TRACE(pb, PB_TRACE_REC(ioerror), serror);
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

int pagebuf_iostart(		/* start I/O on a buffer          */
    page_buf_t * pb,		/* buffer to start                */
    page_buf_flags_t flags)	/* PBF_LOCK, PBF_ASYNC, PBF_READ, */
				/* PBF_WRITE, PBF_ALLOCATE,       */
				/* PBF_DELWRI, PBF_SEQUENTIAL,	  */
				/* PBF_SYNC, PBF_DONT_BLOCK       */
				/* PBF_RELEASE			  */
{
	int status = 0;

	PB_TRACE(pb, PB_TRACE_REC(iostart), flags);

	if (flags & PBF_DELWRI) {
		pb->pb_flags &= ~(PBF_READ | PBF_WRITE | PBF_ASYNC);
		pb->pb_flags |= flags &
				(PBF_DELWRI | PBF_ASYNC | PBF_SYNC);
		pagebuf_delwri_queue(pb, 1);
		return status;
	}
 
	pb->pb_flags &= ~(PBF_READ | PBF_WRITE | PBF_ASYNC | PBF_DELWRI);
	pb->pb_flags |= flags & (PBF_READ | PBF_WRITE | PBF_ASYNC | PBF_SYNC);

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
		if (flags & (PBF_WRITE| PBF_DELWRI))
			pagebuf_rele(pb);
	}

	return status;
}

/* Helper routines for pagebuf_iorequest */

typedef struct {
	page_buf_t *pb;		/* pointer to pagebuf page is within */
	int locking;		/* are pages locked */
	atomic_t remain;	/* count of remaining I/O requests */
} pagesync_t;

static inline void _pb_io_done(page_buf_t *pb)
{
	if (atomic_dec_and_test(&PBP(pb)->pb_io_remaining) == 1) {
		pb->pb_locked = 0;
		pagebuf_iodone(pb);
	}
}


/* I/O completion routine for pagebuf I/O on a page, can be used for a
 * page without a pagebuf - the pb field in pagesync_t is not set.
 */
STATIC void _end_pagebuf_page_io(struct buffer_head *bh, int uptodate)
{
	struct page *page;
	page_buf_t *pb = (page_buf_t *) bh->b_private;

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
	_pb_io_done(pb);
}

STATIC void _end_pagebuf_page_io_locked(struct buffer_head *bh, int uptodate)
{
	struct page *page;
	page_buf_t *pb = (page_buf_t *) bh->b_private;

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
	UnlockPage(page);
	_pb_io_done(pb);
}

STATIC void _end_pagebuf_page_io_multi(struct buffer_head *bh, int uptodate)
{
	pagesync_t *psync = (pagesync_t *) bh->b_private;
	page_buf_t *pb = psync->pb;
	struct page *page;

	mark_buffer_uptodate(bh, uptodate);
	atomic_dec(&bh->b_count);

	page = bh->b_page;
	if (!test_bit(BH_Uptodate, &bh->b_state)) {
		set_bit(PG_error, &page->flags);
		pb->pb_error = EIO;
	}

	unlock_buffer(bh);
	_pagebuf_free_bh(bh);

	if (atomic_dec_and_test(&psync->remain) == 1) {
		SetPageUptodate(page);
		if (psync->locking)
			UnlockPage(page);
		kfree(psync);
		_pb_io_done(pb);
	}
}

/*
 * Initiate I/O on part of a page we are interested in
 *
 * This will attempt to make a request bigger than the sector
 * size if we are not running on the MD device - LVM need to be
 * added to this logic as well.
 *
 * If you think this change is causing problems initializing the
 * concat_ok variable will turn it off again.
 */
STATIC int
_pagebuf_page_io(
    struct page *page,		/* Page structure we are dealing with */
    page_buf_t * pb,		/* pagebuf holding it, can be NULL */
    page_buf_daddr_t bn,	/* starting block number */
    kdev_t dev,			/* device for I/O */
    size_t sector,		/* device block size */
    off_t pg_offset,		/* starting offset in page */
    size_t pg_length,		/* count of data to process */
    int locking,		/* page locking in use */
    int rw)			/* read/write operation */
{
	int cnt,itr;
	pagesync_t *psync = NULL;
	struct buffer_head *bh, *bufferlist[8];
	size_t blk_length;
	int err=0;
	int concat_ok;

	if ((MAJOR(dev) != LVM_BLK_MAJOR) && (MAJOR(dev) != MD_MAJOR)) {
		concat_ok = 1;
	} else if ((MAJOR(dev) == MD_MAJOR) && (pg_offset == 0) &&
		   (pg_length == PAGE_CACHE_SIZE) &&
		   ((bn & ((page_buf_daddr_t)(PAGE_CACHE_SIZE - 1) >> 9)) == 0)) {
		concat_ok = 1;
	} else {
		concat_ok = 0;
	}

	/* Calculate the block offsets and length we will be using */
	if (pg_offset) {
		size_t block_offset;

		block_offset = pg_offset >> PB_SECTOR_BITS;
		block_offset = pg_offset - (block_offset << PB_SECTOR_BITS);
		blk_length = (pg_length + block_offset + sector - 1) >> PB_SECTOR_BITS;
	} else {
		blk_length = (pg_length + sector - 1) >> PB_SECTOR_BITS;
	}

	if (concat_ok) {
		/* This should just create one buffer head */
		sector *= blk_length;
		blk_length = 1;
	}

	/* Allocate pagesync_t and buffer heads for portions of the
	 * page which need I/O.
	 * Call generic_make_request
	 */

	if (blk_length != 1) {
		psync = (pagesync_t *) kmalloc(sizeof(pagesync_t), GFP_NOFS);

		/* Ugh - out of memory condition here */
		if (psync == NULL)
			BUG();

		psync->pb = pb;
		psync->locking = locking;
		atomic_set(&psync->remain, 0);
	}

	for (cnt = 0; blk_length > 0;
	     blk_length--, pg_offset += sector) {
		bh = kmem_cache_alloc(bh_cachep, SLAB_NOFS);
		if (bh == NULL){
			bh = _pagebuf_get_prealloc_bh();
			if (bh == NULL) {
				/* This should never happen */
				err = -ENOMEM;
				goto error;
			}
		}

		memset(bh, 0, sizeof(*bh));
		init_waitqueue_head(&bh->b_wait);

		if (psync) {
			init_buffer(bh, _end_pagebuf_page_io_multi, psync);
			atomic_inc(&psync->remain);
		} else if (locking) {
			init_buffer(bh, _end_pagebuf_page_io_locked, pb);
		} else {
			init_buffer(bh, _end_pagebuf_page_io, pb);
		}

		bh->b_size = sector;
		set_bh_page(bh, page, pg_offset);
		atomic_set(&bh->b_count, 1);
		bh->b_dev = dev;
		bh->b_blocknr = bn++;

		bh->b_rdev = bh->b_dev;
		bh->b_rsector = bh->b_blocknr;
		set_bit(BH_Lock, &bh->b_state);
		set_bit(BH_Mapped, &bh->b_state);

		if (rw == WRITE ) {
			set_bit(BH_Uptodate, &bh->b_state);
			set_bit(BH_Dirty, &bh->b_state);
		}
		bufferlist[cnt++] = bh;
	}

	if (cnt) {
		/* Indicate that there is another page in progress */
		atomic_inc(&PBP(pb)->pb_io_remaining);

		for (itr=0; itr < cnt; itr++){
			generic_make_request(rw, bufferlist[itr]);
		}		  
	} else {
		if (psync)
			kfree(psync);
		if (locking)
			UnlockPage(page);
	}

	return err;
error:
	/* If we ever do get here then clean up what we already did */
	for (itr=0; itr < cnt; itr++) {
		atomic_set_buffer_clean (bufferlist[itr]);
		bufferlist[itr]->b_end_io(bufferlist[itr], 0);
	}
	return err;
}

/* Apply function for pagebuf_segment_apply */
STATIC int _page_buf_page_apply(
    page_buf_t * pb,
    loff_t offset,
    struct page *page,
    size_t pg_offset,
    size_t pg_length)
{
	page_buf_daddr_t bn = pb->pb_bn;
	kdev_t dev = pb->pb_dev;
	size_t sector = 1 << PB_SECTOR_BITS;
	loff_t pb_offset;
	size_t	ret_len = pg_length;
	assert(page);

	if ((pb->pb_buffer_length < PAGE_CACHE_SIZE) &&
	    (pb->pb_flags & PBF_READ) && pb->pb_locked) {
		bn -= (pb->pb_offset >> PB_SECTOR_BITS);
		pg_offset = 0;
		pg_length = PAGE_CACHE_SIZE;
	} else {
		pb_offset = offset - pb->pb_file_offset;
		if (pb_offset) {
			bn += (pb_offset + sector - 1) >> PB_SECTOR_BITS;
		}
	}

	if (pb->pb_flags & PBF_READ) {
		/* We only need to do I/O on pages which are not upto date */
		_pagebuf_page_io(page, pb, bn, dev,
		    sector, (off_t) pg_offset, pg_length, pb->pb_locked, READ);
	} else if (pb->pb_flags & PBF_WRITE) {
		int locking = (pb->pb_flags & _PBF_LOCKABLE) == 0;

		/* Check we need to lock pages */
		if (locking && (pb->pb_locked == 0))
			lock_page(page);
		_pagebuf_page_io(page, pb, bn, dev, sector,
			    (off_t) pg_offset, pg_length, locking, WRITE);
	}

	return (ret_len);
}

/*
 *	pagebuf_iorequest
 *
 * 	pagebuf_iorequest is the core I/O request routine.
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

int pagebuf_iorequest(		/* start real I/O               */
	page_buf_t * pb)	/* buffer to convey to device   */
{
	int status = 0;

	//assert(pb->pb_flags & _PBF_ALL_PAGES_MAPPED);

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
	status = pagebuf_segment_apply(_page_buf_page_apply, pb);

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

int pagebuf_iowait(page_buf_t * pb) /* buffer to wait on              */
{
	PB_TRACE(pb, PB_TRACE_REC(iowait), 0);
	run_task_queue(&tq_disk);
	down(&PBP(pb)->pb_iodonesema);
	PB_TRACE(pb, PB_TRACE_REC(iowaited), pb->pb_error);
	return (pb->pb_error);
}


/* reverse pagebuf_mapin()      */ 
STATIC void *
pagebuf_mapout_locked(
    page_buf_t * pb)	/* buffer to unmap                */
{				
	void *old_addr = NULL;

	if (pb->pb_flags & PBF_MAPPED) {
		if (pb->pb_flags & _PBF_ADDR_ALLOCATED)
			old_addr = pb->pb_addr - pb->pb_offset;
		pb->pb_addr = NULL;
		pb->pb_flags &= ~(PBF_MAPPED | _PBF_ADDR_ALLOCATED);
	}

	return (old_addr);	/* Caller must free the address space,
				 * we are under a spin lock, probably
				 * not safe to do vfree here
				 */
}

caddr_t
pagebuf_offset(page_buf_t *pb, off_t offset)
{
        struct page *page;

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
 *	returned.  pagebuf_segment returns 0 on a successful
 *	retrieval, and a negative error code on any error (including
 *	-ENOENT when the loff_t is out of range). 
 *
 *	The mem_map_t * return value may be set to NULL if the
 *	page is outside of main memory (as in the case of memory on a controller
 *	card).  The page_buf_pgno_t may be set to PAGE_BUF_PGNO_NULL
 *	as well, if the page is not actually allocated, unless the
 *	PBF_ALWAYS_ALLOC flag is set in the page_buf_flags_t,
 *	in which allocation of storage will be forced.
 */

int pagebuf_segment(		/* return next segment of buffer */
    page_buf_t * pb,		/* buffer to examine            */
    loff_t * boff_p,		/* offset in buffer of next     */
				/* segment (updated)            */
    mem_map_t ** spage_p,	/* page (updated)               */
				/* (NULL if not in mem_map[])   */
    size_t * soff_p,		/* offset in page (updated)     */
    size_t * ssize_p,		/* length of segment (updated)  */
    page_buf_flags_t flags)	/* PBF_ALWAYS_ALLOC             */
{
	loff_t kpboff;		/* offset in pagebuf		*/
	int kpi;		/* page index in pagebuf	*/
	size_t slen;		/* segment length               */

	kpboff = *boff_p;

	kpi = page_buf_btoct(kpboff + pb->pb_offset);

	*spage_p = pb->pb_pages[kpi];

	*soff_p = page_buf_poff(kpboff + pb->pb_offset);
	slen = PAGE_CACHE_SIZE - *soff_p;
	if (slen > (pb->pb_count_desired - kpboff))
		slen = (pb->pb_count_desired - kpboff);
	*ssize_p = slen;

	*boff_p = *boff_p + slen;

	return (0);
}


int pagebuf_iomove(			/* move data in/out of buffer	*/
    page_buf_t		*pb,		/* buffer to process		*/
    off_t		boff,		/* starting buffer offset	*/
    size_t		bsize,		/* length to copy		*/
    caddr_t		data,		/* data address			*/
    page_buf_rw_t	mode)		/* read/write flag		*/
{
	loff_t cboff;
	size_t cpoff;
	size_t csize;
	struct page *page;

	cboff = boff;
	boff += bsize; /* last */

	while (cboff < boff) {
		if (pagebuf_segment(pb, &cboff, &page, &cpoff, &csize, 0)) {
			/* XXX allocate missing page */
			return (-ENOMEM);
		}
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
	return 0;
}

/*
 *	pagebuf_segment_apply
 *
 *	pagebuf_segment_apply applies the page_buf_apply_t function
 *	to each segment of the page_buf_t.  It may be used to walk
 *	the segments of a buffer, as when building 
 *	a driver scatter-gather list.
 */

int pagebuf_segment_apply(	/* apply function to segments   */
    page_buf_apply_t func,	/* function to call             */
    page_buf_t * pb)		/* buffer to examine            */
{
	int buf_index;
	int status = 0;
	int sval;
	loff_t buffer_offset = pb->pb_file_offset;
	size_t buffer_len = pb->pb_count_desired;
	size_t page_offset;
	size_t len;
	size_t total = 0;
	size_t cur_offset;
	size_t cur_len;

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
		/* func probably = _page_buf_page_apply */
		sval = func(pb, buffer_offset,
			    pb->pb_pages[buf_index], page_offset, len);

		if (sval <= 0) {
			status = sval;
			goto out;
		} else {
			len = sval;
			total += len;
		}

		buffer_offset += len;
		buffer_len -= len;
	}

out:
	pagebuf_rele(pb);

	if (!status)
		status = total;

	return (status);
}

/*
 * Pagebuf delayed write buffer handling
 */


void
pagebuf_delwri_queue(page_buf_t *pb, int unlock)
{
	unsigned long flags;

	PB_TRACE(pb, PB_TRACE_REC(delwri_q), unlock);
	spin_lock_irqsave(&pb_daemon->pb_delwrite_lock, flags);
	/* If already in the queue, dequeue and place at tail */
	if (pb->pb_list.next != &pb->pb_list) {
		if (unlock) {
			spin_lock(&PBP(pb)->pb_lock);
			if (pb->pb_hold > 1) pb->pb_hold--;
			spin_unlock(&PBP(pb)->pb_lock);
		}
		list_del(&pb->pb_list);
	} else {
		pb_daemon->pb_delwri_cnt++;
	}
	list_add_tail(&pb->pb_list, &pb_daemon->pb_delwrite_l);
	PBP(pb)->pb_flushtime = jiffies + pb_params.p_un.age_buffer;
	spin_unlock_irqrestore(&pb_daemon->pb_delwrite_lock, flags);

	if (unlock && (pb->pb_flags & _PBF_LOCKABLE)) {
		pagebuf_unlock(pb);
	}
}

void
pagebuf_delwri_dequeue(page_buf_t *pb)
{
	unsigned long	flags;

	PB_TRACE(pb, PB_TRACE_REC(delwri_uq), 0);
	spin_lock_irqsave(&pb_daemon->pb_delwrite_lock, flags);
	list_del(&pb->pb_list);
	INIT_LIST_HEAD(&pb->pb_list);
	pb->pb_flags &= ~PBF_DELWRI;
	pb_daemon->pb_delwri_cnt--;
	spin_unlock_irqrestore(&pb_daemon->pb_delwrite_lock, flags);
}

/* Defines for page buf daemon */
DECLARE_WAIT_QUEUE_HEAD(pbd_waitq);

STATIC int force_flush;
STATIC void
pagebuf_daemon_wakeup(int flag)
{
	force_flush = flag;
	if (waitqueue_active(&pbd_waitq)) {
		wake_up_interruptible(&pbd_waitq);
	}
}

typedef void (*timeout_fn)(unsigned long);


STATIC int
pagebuf_daemon(void *data)
{
	u_long		flags;
	int		count;
	page_buf_t	*pb = NULL;
	struct list_head *head, *curr;
	pagebuf_marker_t *pb_marker_ptr;
	struct timer_list pb_daemon_timer = 
		{ {NULL, NULL}, 0, 0, (timeout_fn)pagebuf_daemon_wakeup };


	pb_marker_ptr = kmalloc(sizeof(pagebuf_marker_t), GFP_KERNEL);
	
	pb_marker_ptr->pb_flags = 0; 

	/*  Set up the thread  */
	exit_files(current);
	daemonize();

	spin_lock_irqsave(&current->sigmask_lock, flags);	
	flush_signals(current);
	sigfillset(&current->blocked);
	recalc_sigpending(current);
	spin_unlock_irqrestore(&current->sigmask_lock, flags);

	strcpy(current->comm, "pagebuf_daemon");
	current->flags |= PF_MEMALLOC;

	do {
		if (pb_daemon->active == 1) {
			del_timer(&pb_daemon_timer);
			pb_daemon_timer.expires = jiffies + pb_params.p_un.flush_interval;
			add_timer(&pb_daemon_timer);
			interruptible_sleep_on(&pbd_waitq);
		}

		if (pb_daemon->active == 0) {
			del_timer(&pb_daemon_timer);
		}

		spin_lock_irqsave(&pb_daemon->pb_delwrite_lock, flags);

		head = curr = &pb_daemon->pb_delwrite_l;
		curr = curr->next; /* need to walk off the list head,
				    * since it just a global place holder */

		count = 0;
		while (curr != head) {
			pb = list_entry(curr, page_buf_t, pb_list);

			/*
			 * Skip other markers.
			 */
			if (pb->pb_flags == 0 ) { 
				curr = curr->next;
				continue;
			}

			PB_TRACE(pb, PB_TRACE_REC(walkq1), pagebuf_ispin(pb));

			if ((pb->pb_flags & PBF_DELWRI) && !pagebuf_ispin(pb) &&
			    (((pb->pb_flags & _PBF_LOCKABLE) == 0) ||
			     !pagebuf_cond_lock(pb))) {

				if (!force_flush && time_before(jiffies,
						PBP(pb)->pb_flushtime)) {
					pagebuf_unlock(pb);
					break;
				}

				pb->pb_flags &= ~PBF_DELWRI;
				pb->pb_flags |= PBF_WRITE;

				/* insert a place holder */
				list_add(&pb_marker_ptr->pb_list, curr);

				spin_unlock_irqrestore(
						&pb_daemon->pb_delwrite_lock,
						flags);

				__pagebuf_iorequest(pb);
				count++;

				spin_lock_irqsave(
						&pb_daemon->pb_delwrite_lock,
						flags);
				/*
				 * ok got the lock back; pick up the place
				 * holder and continue on
				 */
				curr = pb_marker_ptr->pb_list.next;
				list_del(&pb_marker_ptr->pb_list);

			} else {
				/* not doing anything with current...
				 * move on to the next one */
				curr = curr->next;
			}
		}

		spin_unlock_irqrestore(&pb_daemon->pb_delwrite_lock, flags);

#ifdef REMAPPING_SUPPORT
		if (as_list_len > 0)
			purge_addresses();
#endif

		if (count)
			run_task_queue(&tq_disk);
		force_flush = 0;
	} while (pb_daemon->active == 1);

	pb_daemon->active = -1;
	wake_up_interruptible(&pbd_waitq);
	kfree(pb_marker_ptr);

	return(0);
}


void
pagebuf_delwri_flush(pb_target_t *target, u_long flags, int *pinptr)
{
	page_buf_t *pb = NULL;
	struct list_head *head, *curr;
	unsigned long save;
	int locked;
	int pincount = 0;
	pagebuf_marker_t *pb_marker_ptr;


	pb_marker_ptr = kmalloc(sizeof(pagebuf_marker_t), GFP_KERNEL);

	pb_marker_ptr->pb_flags = 0;

	spin_lock_irqsave(&pb_daemon->pb_delwrite_lock, save);
	locked = 1;

	head = curr = &pb_daemon->pb_delwrite_l;
	curr = curr->next; /* need to walk off the list head,
			    * since it just a global place holder */

	while (curr != head) {
		pb = list_entry(curr, page_buf_t, pb_list);

		/*
		 * Skip other targets, markers and in progress buffers 
		 */

		if ((pb->pb_flags == 0) || (pb->pb_target != target) ||
		    !(pb->pb_flags & PBF_DELWRI)) {
			curr = curr->next;
			continue;
		}

		PB_TRACE(pb, PB_TRACE_REC(walkq2), pagebuf_ispin(pb));

		if (flags & PBDF_TRYLOCK) {
			if (!pagebuf_cond_lock(pb)) {
				pincount++;
				curr = curr->next;
				continue;
			}
		} else {
			list_add(&pb_marker_ptr->pb_list, curr);
			spin_unlock_irqrestore(&pb_daemon->pb_delwrite_lock,
						save);
			locked = 0;
			pagebuf_lock(pb);
		}

		if (pagebuf_ispin(pb)) {
			pincount++;
			pagebuf_unlock(pb);
			if (!locked)
				goto relock;
			curr = curr->next;
			continue;
		}

		pb->pb_flags &= ~PBF_DELWRI;
		pb->pb_flags |= PBF_WRITE;
		if (flags & PBDF_WAIT)
			pb->pb_flags &= ~PBF_ASYNC;

		if (locked) {
			list_add(&pb_marker_ptr->pb_list, curr);
			spin_unlock_irqrestore(&pb_daemon->pb_delwrite_lock,
						save);
		}

		__pagebuf_iorequest(pb);

relock:
		spin_lock_irqsave( &pb_daemon->pb_delwrite_lock,
					save);
		/*
		 * ok got the lock back; pick up the place
		 * holder and continue on
		 */
		curr = pb_marker_ptr->pb_list.next;
		list_del(&pb_marker_ptr->pb_list);
		locked = 1;
	}

	spin_unlock_irqrestore(&pb_daemon->pb_delwrite_lock, save);

	run_task_queue(&tq_disk);

	if (pinptr)
		*pinptr = pincount;

	if ((flags & PBDF_WAIT) == 0 ){
		kfree(pb_marker_ptr);
		return;
	}

	/*
	 * The problem to solve here:  if you find a buffer on the
	 * delwri queue, under protection of "pb_delwrite_lock",
	 * and it's had I/O initiated via the above loop, as soon
	 * as you drop "pb_delwrite_lock" it can turn into somebody
	 * else's buffer before you can try to lock/unlock it in
	 * order to synchronize with it.
	 */


	/* Now do that again, just waiting for the lock */
	spin_lock_irqsave(&pb_daemon->pb_delwrite_lock, flags);

	head = curr = &pb_daemon->pb_delwrite_l;
	curr = curr->next;


	while (curr != head) {

		pb = list_entry(curr, page_buf_t, pb_list);

		/*
		 * Skip stuff we do not care about
		 */
		if ((pb->pb_flags == 0) || (pb->pb_flags & PBF_DELWRI) ||
		    (pb->pb_target != target)) { 
			curr = curr->next;
			continue;
		}

		PB_TRACE(pb, PB_TRACE_REC(walkq3), pagebuf_ispin(pb));

		if (pb->pb_flags & PBF_ASYNC) {
			curr = curr->next;
			continue;
		}

		list_add(&pb_marker_ptr->pb_list, curr);

		spin_unlock_irqrestore( &pb_daemon->pb_delwrite_lock, flags);
		pagebuf_iowait(pb);
		pagebuf_delwri_dequeue(pb);
		if (!pb->pb_relse)
			pagebuf_unlock(pb);
		pagebuf_rele(pb);

		spin_lock_irqsave(&pb_daemon->pb_delwrite_lock, flags);

		curr = pb_marker_ptr->pb_list.next;
		list_del(&pb_marker_ptr->pb_list);
	}

	spin_unlock_irqrestore(&pb_daemon->pb_delwrite_lock, flags);
	
	kfree(pb_marker_ptr);
}

static int pagebuf_daemon_start(void)
{
  
	if (!pb_daemon){
		pb_daemon = (pagebuf_daemon_t *)
				kmalloc(sizeof(pagebuf_daemon_t), GFP_KERNEL);
		if (!pb_daemon){
			return -1; /* error */
		}

		pb_daemon->active = 1;
		pb_daemon->pb_delwri_cnt = 0;
		pb_daemon->pb_delwrite_lock = SPIN_LOCK_UNLOCKED;

		INIT_LIST_HEAD(&pb_daemon->pb_delwrite_l);

		if (0 > kernel_thread(pagebuf_daemon, (void *)pb_daemon,
				CLONE_FS|CLONE_FILES|CLONE_SIGHAND)) {
			printk("Can't start pagebuf daemon\n");
			kfree(pb_daemon);
			return -1; /* error */
		}
	}
	return 0;
}	

/* Do not mark as __exit, it is called from pagebuf_terminate.  */

static int pagebuf_daemon_stop(void)
{
	if (pb_daemon) {
		pb_daemon->active = 0;

		wake_up_interruptible(&pbd_waitq);

		while (pb_daemon->active == 0) {
			interruptible_sleep_on(&pbd_waitq);
		}

		kfree(pb_daemon);
		pb_daemon = NULL;
	}

	return 0;
}

/*
 * Pagebuf sysctl interface
 */

static struct ctl_table_header *pagebuf_table_header;


static ctl_table pagebuf_table[] = {
	{PB_FLUSH_INT, "flush_int", &pb_params.data[0],
	sizeof(int), 0644, NULL, &proc_doulongvec_ms_jiffies_minmax,
	&sysctl_intvec, NULL, &pagebuf_min[0], &pagebuf_max[0]},
	{PB_FLUSH_AGE, "flush_age", &pb_params.data[1],
	sizeof(int), 0644, NULL, &proc_doulongvec_ms_jiffies_minmax,
	&sysctl_intvec, NULL, &pagebuf_min[1], &pagebuf_max[1]},
	{PB_DIO_MAX, "max_dio_pages", &pb_params.data[2],
	sizeof(int), 0644, NULL, &proc_doulongvec_minmax, &sysctl_intvec, NULL,
	&pagebuf_min[2], &pagebuf_max[2]},
#ifdef PAGEBUF_TRACE
	{PB_DEBUG, "debug", &pb_params.data[3],
	sizeof(int), 0644, NULL, &proc_doulongvec_minmax, &sysctl_intvec, NULL,
	&pagebuf_min[3], &pagebuf_max[3]},
#endif
	{0}
};

static ctl_table pagebuf_dir_table[] = {
	{VM_PAGEBUF, "pagebuf", NULL, 0, 0555, pagebuf_table},
	{0}
};

static ctl_table pagebuf_root_table[] = {
	{CTL_VM, "vm",  NULL, 0, 0555, pagebuf_dir_table},
	{0}
};

#ifdef CONFIG_PROC_FS
static int
pagebuf_readstats(char *buffer, char **start, off_t offset,
			int count, int *eof, void *data)
{
	int     i, len;

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
#endif  /* CONFIG_PROC_FS */

static void	pagebuf_shaker(void)
{
	pagebuf_daemon_wakeup(1);
}

/*
 *	Initialization and Termination
 */

/*
 *	pagebuf_init
 */

int __init pagebuf_init(void)
{
	pagebuf_table_header = register_sysctl_table(pagebuf_root_table, 1);

#ifdef  CONFIG_PROC_FS
	if (proc_mkdir("fs/pagebuf", 0))
		create_proc_read_entry("fs/pagebuf/stat", 0, 0, pagebuf_readstats, NULL);
#endif

	avl_init();
	pagebuf_locking_init();
	if (pagebuf_cache == NULL) {
		pagebuf_cache = kmem_cache_create("page_buf_t",
		    sizeof(page_buf_private_t),
		    0, SLAB_HWCACHE_ALIGN, NULL, NULL);
		if (pagebuf_cache == NULL) {
			printk("pagebuf: couldn't init pagebuf cache\n");
			pagebuf_terminate();
			return (-ENOMEM);
		}
	}

	if ( _pagebuf_prealloc_bh(NR_RESERVED_BH) < NR_RESERVED_BH) {
		printk("pagebuf: couldn't pre-allocate %d buffer heads\n", NR_RESERVED_BH);
		pagebuf_terminate();
		return (-ENOMEM);
	}

	init_waitqueue_head(&pb_resv_bh_wait);

#ifdef PAGEBUF_TRACE
	pb_trace.buf = (pagebuf_trace_t *)kmalloc(PB_TRACE_BUFSIZE *
				sizeof(pagebuf_trace_t), GFP_KERNEL);
/* For really really long trace bufs */
/*	pb_trace.buf = (pagebuf_trace_t *)vmalloc(PB_TRACE_BUFSIZE * sizeof(pagebuf_trace_t)); */
	memset(pb_trace.buf, 0, PB_TRACE_BUFSIZE * sizeof(pagebuf_trace_t));
	pb_trace.start = 0;
	pb_trace.end = PB_TRACE_BUFSIZE - 1;
#endif

	pagebuf_daemon_start();

	kmem_shake_register(pagebuf_shaker);

	return (0);
}


/*
 *	pagebuf_terminate.  Do not mark as __exit, this is also called from the
 *	__init code.
 */

void pagebuf_terminate(void)
{
	if (pagebuf_cache != NULL)
		kmem_cache_destroy(pagebuf_cache);
	pagebuf_daemon_stop();

	kmem_shake_deregister(pagebuf_shaker);

	pagebuf_locking_terminate();
	avl_terminate();
	unregister_sysctl_table(pagebuf_table_header);
#ifdef  CONFIG_PROC_FS
	remove_proc_entry("fs/pagebuf/stat", NULL);
	remove_proc_entry("fs/pagebuf", NULL);
#endif
}


/*
 *	Module management
 */

EXPORT_SYMBOL(pagebuf_offset);
