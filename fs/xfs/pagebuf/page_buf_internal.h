/*
 * Copyright (c) 2002 Silicon Graphics, Inc.  All Rights Reserved.
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
 * Written by Steve Lord at SGI
 */

#ifndef __PAGE_BUF_PRIVATE_H__
#define __PAGE_BUF_PRIVATE_H__

#include "page_buf.h"

#define _PAGE_BUF_INTERNAL_
#define PB_DEFINE_TRACES
#include "page_buf_trace.h"

#define PAGE_CACHE_OFF_LL	((long long)(PAGE_CACHE_SIZE-1))
#define PAGE_CACHE_MASK_LL	(~((long long)(PAGE_CACHE_SIZE-1)))
#define PAGE_CACHE_ALIGN_LL(addr) \
				(((addr)+PAGE_CACHE_SIZE-1)&PAGE_CACHE_MASK_LL)

typedef struct page_buf_private_s {
	page_buf_t		pb_common;	/* public part of structure */
	struct semaphore	pb_sema;	/* semaphore for lockables  */
	unsigned long		pb_flushtime;	/* time to flush pagebuf    */
	atomic_t		pb_io_remaining;/* #outstanding I/O requests */
	atomic_t		pb_pin_count;	/* pin count                */
	wait_queue_head_t	pb_waiters;	/* unpin waiters            */
#ifdef PAGEBUF_LOCK_TRACKING
	int			pb_last_holder;
#endif
} page_buf_private_t;

#define PBC(pb) (&((pb)->pb_common))
#define PBP(pb) ((page_buf_private_t *) (pb))

#ifdef PAGEBUF_LOCK_TRACKING
#define PB_SET_OWNER(pb)	(PBP(pb)->pb_last_holder = current->pid)
#define PB_CLEAR_OWNER(pb)	(PBP(pb)->pb_last_holder = -1)
#define PB_GET_OWNER(pb)	(PBP(pb)->pb_last_holder)
#else
#define PB_SET_OWNER(pb)
#define PB_CLEAR_OWNER(pb)
#define PB_GET_OWNER(pb)
#endif /* PAGEBUF_LOCK_TRACKING */

/* Tracing utilities for pagebuf */
typedef struct {
	int			event;
	unsigned long		pb;
	page_buf_flags_t	flags;
	unsigned short		hold;
	unsigned short		lock_value;
	void			*task;
	void			*misc;
	void			*ra;
	loff_t			offset;
	size_t			size;
} pagebuf_trace_t;

struct pagebuf_trace_buf {
        pagebuf_trace_t		*buf;
        volatile int		start;
        volatile int		end;
};

#define PB_TRACE_BUFSIZE	1024
#define CIRC_INC(i)     (((i) + 1) & (PB_TRACE_BUFSIZE - 1))

typedef struct pagebuf_daemon {
	int			active;
	spinlock_t		pb_delwrite_lock;
	struct list_head	pb_delwrite_l;
	int			pb_delwri_cnt;
} pagebuf_daemon_t;

#define NBITS   5
#define NHASH   (1<<NBITS)

typedef struct {
	struct list_head        pb_hash;
	int                     pb_count;
	spinlock_t              pb_hash_lock;
} pb_hash_t;

extern pb_hash_t	pbhash[];

#define pb_hash(pb)	&pbhash[pb->pb_hash_index]

/*
 * Tunable pagebuf parameters
 */

#define P_PARAM	4

typedef union pagebuf_param {
	struct {
		ulong	flush_interval;	/* interval between runs of the
					 * delwri flush daemon.  */
		ulong	age_buffer;	/* time for buffer to age before
					 * we flush it.  */
		ulong	max_dio;	/* maximum pages locked in a dio call */
		ulong	debug;		/* debug tracing on or off */
	} p_un;
	ulong data[P_PARAM];
} pagebuf_param_t;

enum {
        PB_FLUSH_INT = 1,
        PB_FLUSH_AGE = 2,
	PB_DIO_MAX = 3,
        PB_DEBUG = 4
};

extern pagebuf_param_t	pb_params;

/*
 * Pagebuf statistics
 */

struct pbstats {
	u_int32_t	pb_get;
	u_int32_t	pb_create;
	u_int32_t	pb_get_locked;
	u_int32_t	pb_get_locked_waited;
	u_int32_t	pb_busy_locked;
	u_int32_t	pb_miss_locked;
	u_int32_t	pb_page_alloc;
	u_int32_t	pb_page_found;
	u_int32_t	pb_get_read;
};

extern struct pbstats pbstats;
extern kmem_cache_t	*pagebuf_cache;

#define PB_STATS_INC(count)	( count ++ )

static __inline__ page_buf_t *__pagebuf_allocate(int flags)
{
	return kmem_cache_alloc(pagebuf_cache,
			(flags & PBF_DONT_BLOCK) ? SLAB_NOFS : SLAB_KERNEL);
}

/*
 * Internal routines
 */

extern int _pagebuf_get_lockable_buffer(
			pb_target_t *, loff_t, size_t, page_buf_flags_t,
			page_buf_t **);
extern int _pagebuf_find_lockable_buffer(
			pb_target_t *, loff_t, size_t, page_buf_flags_t,
			page_buf_t **, page_buf_t *);

extern int _pagebuf_initialize(
		page_buf_t *,
		struct pb_target *,
		loff_t,
		size_t,
		page_buf_flags_t);

extern page_buf_bmap_t *
__pb_match_offset_to_mapping(
		struct page *, page_buf_bmap_t *, int, unsigned long, int *);
extern void
__pb_map_buffer_at_offset(
		pb_target_t *, struct page *, struct buffer_head *,
		unsigned long, int, page_buf_bmap_t *);

extern int pagebuf_locking_init(void);
extern void pagebuf_locking_terminate(void);

extern int pagebuf_init(void);
extern void pagebuf_terminate(void);

#undef assert
#ifdef PAGEBUF_DEBUG
# define assert(expr) \
	if (!(expr)) {                                          \
		printk("Assertion failed: %s\n%s::%s line %d\n",\
		#expr,__FILE__,__FUNCTION__,__LINE__);          \
		BUG();                                          \
	}
#else
# define assert(x)	do { } while (0)
#endif

#ifndef STATIC
# define STATIC	static
#endif

#endif /* __PAGE_BUF_PRIVATE_H__ */
