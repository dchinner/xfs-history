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
 * Written by Steve Lord, Jim Mostek, Russell Cattelan at SGI
 */

#ifndef __PAGE_BUF_H__
#define __PAGE_BUF_H__

#include <linux/version.h>
#include <linux/list.h>
#include <linux/types.h>
#include <linux/spinlock.h>
#include <asm/system.h>
#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/iobuf.h>
#include <linux/smp_lock.h>
#include <linux/uio.h>


/*
 * Turn this on to get pagebuf lock ownership
#define PAGEBUF_LOCK_TRACKING
*/

/*
 *	Base types 
 */

/* daddr must be signed since -1 is used for bmaps that are not yet allocated */
typedef loff_t page_buf_daddr_t;

#define PAGE_BUF_DADDR_NULL ((page_buf_daddr_t) (-1LL))

typedef size_t page_buf_dsize_t;		/* size of buffer in blocks */

#define page_buf_ctob(pp)	((pp) * PAGE_CACHE_SIZE)
#define page_buf_btoc(dd)	(((dd) + PAGE_CACHE_SIZE - 1) >> PAGE_CACHE_SHIFT)
#define page_buf_btoct(dd)	((dd) >> PAGE_CACHE_SHIFT)
#define page_buf_poff(aa)	((aa) & ~PAGE_CACHE_MASK)

typedef enum page_buf_rw_e {
	PBRW_READ = 1,			/* transfer into target memory */
	PBRW_WRITE = 2,			/* transfer from target memory */
	PBRW_ZERO = 3			/* Zero target memory */
} page_buf_rw_t;

/*
 *	page_buf_bmap_t:  File system I/O map 
 *
 * The pbm_bn, pbm_offset and pbm_length fields are expressed in disk blocks.
 * The pbm_length field specifies the size of the underlying backing store
 * for the particular mapping.
 *
 * The pbm_bsize, pbm_size and pbm_delta fields are expressed in bytes and indicate
 * the size of the mapping, the number of bytes that are valid to access
 * (read or write), and the offset into the mapping, given the offset
 * supplied to the file I/O map routine.  pbm_delta is the offset of the
 * desired data from the beginning of the mapping.
 *
 * When a request is made to read beyond the logical end of the object,
 * pbm_size may be set to 0, but pbm_offset and pbm_length should be set to reflect
 * the actual amount of underlying storage that has been allocated, if any.
 */

/* pbm_flags values */
typedef enum {
	PBMF_EOF = 		0x01,	/* mapping contains EOF 	*/
	PBMF_HOLE = 		0x02,	/* mapping covers a hole 	*/
	PBMF_DELAY = 		0x04,	/* mapping covers delalloc region  */
	PBMF_UNWRITTEN = 	0x20,	/* mapping covers allocated 	*/
					/* but uninitialized XFS data 	*/
} bmap_flags_t;

typedef struct page_buf_bmap_s {
	page_buf_daddr_t pbm_bn;	/* block number in file system 	    */
	kdev_t		pbm_dev;	/* device for I/O		    */
	loff_t 		pbm_offset;	/* byte offset of mapping in file   */
	size_t		pbm_delta;	/* offset of request into bmap	    */
	size_t		pbm_bsize;	/* size of this mapping in bytes    */
	bmap_flags_t	pbm_flags;	/* options flags for mapping	    */
} page_buf_bmap_t;

typedef page_buf_bmap_t pb_bmap_t;

/*
 *	page_buf_t:  Buffer structure for page cache-based buffers
 *
 * This buffer structure is used by the page cache buffer management routines
 * to refer to an assembly of pages forming a logical buffer.  The actual
 * I/O is performed with kiobuf or buffer_head structures, as required by
 * drivers, for drivers which do not understand this structure.
 * The buffer structure is used on temporary basis only, and
 * discarded when released.  
 *
 * The real data storage is recorded in the page cache.  Metadata is
 * hashed to the inode for the block device on which the file system resides.
 * File data is hashed to the inode for the file.  Pages which are only
 * partially filled with data have bits set in their block_map entry
 * to indicate which disk blocks in the page are not valid.
 */

typedef enum page_buf_flags_e {
	PBF_READ = (1 << 0),	/* buffer intended for reading from device */
	PBF_WRITE = (1 << 1),	/* buffer intended for writing to device   */
	PBF_MAPPED = (1 << 2),  /* buffer mapped (pb_addr valid)           */
	PBF_PARTIAL = (1 << 3), /* buffer partially read                   */
	PBF_ASYNC = (1 << 4),   /* initiator will not wait for completion  */
	PBF_NONE = (1 << 5),    /* buffer not read at all                  */
	PBF_DELWRI = (1 << 6),  /* buffer has dirty pages                  */
	PBF_FREED = (1 << 7),   /* buffer has been freed and is invalid    */
	PBF_SYNC = (1 << 8),    /* force updates to disk                   */
	PBF_MAPPABLE = (1 << 9),/* use directly-addressable pages          */
	PBF_STALE = (1 << 10),	/* buffer has been staled, do not find it  */	
	PBF_FS_MANAGED = (1 << 11), /* filesystem controls freeing memory  */
	PBF_RELEASE = (1 << 12),/* buffer to be released after I/O is done */

	/* flags used only as arguments to access routines */
	PBF_LOCK = (1 << 13),   /* lock requested                          */
	PBF_TRYLOCK = (1 << 14), /* lock requested, but do not wait        */
	PBF_ALLOCATE = (1 << 15), /* allocate all pages           (UNUSED) */
	PBF_FILE_ALLOCATE = (1 << 16), /* allocate all file space          */
	PBF_DONT_BLOCK = (1 << 17), /* do not block in current thread	   */
	PBF_DIRECT = (1 << 18),   /* direct I/O desired                    */
	PBF_ENTER_PAGES = (1 << 21), /* create invalid pages for all       */
				/* pages in the range of the buffer 	   */
				/* not already associated with buffer	   */

	/* flags used only internally */
	_PBF_LOCKABLE = (1 << 19), /* page_buf_t may be locked		   */
	_PBF_NEXT_KEY = (1 << 20), /* page_buf_t is a dummy used as a key  */
	_PBF_ALL_PAGES_MAPPED = (1 << 22),
				/* all pages in rage are mapped		   */
	_PBF_SOME_INVALID_PAGES = (1 << 23),
				/* some mapped pages are not valid	   */
	_PBF_ADDR_ALLOCATED = (1 << 24),
				/* pb_addr space was allocated		   */
	_PBF_MEM_ALLOCATED = (1 << 25),
				/* pb_mem and underlying pages allocated   */

	PBF_GRIO = (1 << 26),
	PBF_FORCEIO = (1 << 27),
	PBF_FS_RESERVED_3 = (1 << 31)	/* reserved (XFS use: XFS_B_STALE) */

} page_buf_flags_t;

#define PBF_UPDATE (PBF_READ | PBF_WRITE)
#define PBF_NOT_DONE(pb) (((pb)->pb_flags & (PBF_PARTIAL|PBF_NONE)) != 0)
#define PBF_DONE(pb) (((pb)->pb_flags & (PBF_PARTIAL|PBF_NONE)) == 0)


#if LINUX_VERSION_CODE < KERNEL_VERSION(2,4,13)

typedef struct pb_target {
	kdev_t			pbr_device;
	unsigned short		pbr_sector;
	unsigned short		pbr_blocksize;
	unsigned int 		pbr_blocksize_bits;
	struct address_space 	*pbr_addrspace;
	avl_handle_t		pbr_buffers;
	spinlock_t		pbr_lock;
} pb_target_t;

#define PB_ADDR_SPACE(pb)  (&(pb)->pb_target->pbr_addrspace)

#else

typedef struct pb_target {
	kdev_t			pbr_device;
	unsigned short		pbr_sector;
	unsigned short		pbr_blocksize;
	unsigned int 		pbr_blocksize_bits;
	struct inode	 	*pbr_inode;
	avl_handle_t		pbr_buffers;
	spinlock_t		pbr_lock;
} pb_target_t;

#define PB_ADDR_SPACE(pb)  ((pb)->pb_target->pbr_inode->i_mapping)

#endif

struct page_buf_s;

typedef void (*page_buf_iodone_t)(struct page_buf_s *);
			/* call-back function on I/O completion */
typedef void (*page_buf_relse_t)(struct page_buf_s *);
			/* call-back function on I/O completion */
typedef int (*page_buf_bdstrat_t)(struct page_buf_s *);
typedef int (pagebuf_bmap_fn_t) (struct inode *, loff_t, ssize_t,
                             page_buf_bmap_t *, int, int *, int);

#define PB_SECTOR_BITS	9
#define PB_PAGES	4

typedef struct page_buf_s {
	struct list_head	pb_list;
	page_buf_flags_t	pb_flags;	/* status flags */
	struct pb_target	*pb_target;	/* logical object */
	unsigned int		pb_hold;	/* reference count */
	page_buf_daddr_t	pb_bn;		/* block number for I/O */
	kdev_t			pb_dev;
	loff_t			pb_file_offset;	/* offset in file */
	size_t			pb_buffer_length; /* size of buffer in bytes */
	size_t			pb_count_desired; /* desired transfer size */
	void			*pb_addr;	/* virtual address of buffer */
	page_buf_iodone_t	pb_iodone;	/* I/O completion function */
	page_buf_relse_t	pb_relse;	/* releasing function */
	page_buf_bdstrat_t	pb_strat;	/* pre-write function */
	int			pb_error;	/* error code on I/O */
	void			*pb_fspriv;
	void			*pb_fspriv2;
	void			*pb_fspriv3;
	int			pb_page_count;	/* size of page array */
	int			pb_locked;	/* page array is locked */
	int			pb_offset;	/* page offset in first page */
	struct page		**pb_pages;	/* array of page pointers */
	struct page		*pb_page_array[PB_PAGES]; /* inline pages */
} page_buf_t;

typedef struct page_buf_private_s {
	page_buf_t		pb_common;	/* public part of structure */
	spinlock_t		pb_lock;
	struct semaphore	pb_sema;	/* semaphore for lockables  */
	unsigned long		pb_flushtime;	/* time to flush pagebuf    */
	atomic_t		pb_io_remaining;/* #outstanding I/O requests */
	struct semaphore	pb_iodonesema;	/* Semaphore for I/O waiters */
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
	
typedef struct pagebuf_marker_s {
	struct list_head	pb_list;
	page_buf_flags_t	pb_flags;	
} pagebuf_marker_t;

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

#define PB_STATS_INC(count)	( count ++ )


/* 
 *	Page_buf_apply_t
 *
 *	Type of function pointer supplied to pagebuf_segment_apply.
 */

typedef int (*page_buf_apply_t)(	/* function to apply to segment	*/
		page_buf_t *,		/* buffer to examine		*/
		loff_t,			/* offset in file of segment	*/
		struct page *,		/* page (NULL if not in mem_map[]) */
		size_t,			/* offset in page		*/
		size_t);		/* length of segment		*/

/*
 * page_buf module entry points
 */

/* Finding and Reading Buffers */

extern page_buf_t *pagebuf_find(	/* find buffer for block if 	*/
					/* the block is in memory	*/
		struct pb_target *,	/* inode for block		*/
		loff_t,			/* starting offset of range     */
		size_t,			/* length of range              */
		page_buf_flags_t);      /* PBF_LOCK, PBF_ALWAYS_ALLOC   */

extern page_buf_t *pagebuf_get(		/* allocate a buffer		*/
		struct pb_target *,	/* inode for buffer		*/
		loff_t,			/* starting offset of range     */
		size_t,			/* length of range              */
		page_buf_flags_t);	/* PBF_LOCK, PBF_READ, PBF_ALLOCATE, */
					/* PBF_ASYNC, PBF_SEQUENTIAL    */

extern page_buf_t *pagebuf_lookup(
		struct inode *,
		loff_t,			/* starting offset of range     */
		size_t,			/* length of range              */
		int);			/* PBF_ENTER_PAGES 		*/

extern page_buf_t *pagebuf_get_empty(	/* allocate pagebuf struct with	*/
					/*  no memory or disk address	*/
		struct pb_target *);	/* mount point "fake" inode	*/

extern page_buf_t *pagebuf_get_no_daddr(/* allocate pagebuf struct	*/
					/* without disk address		*/
		size_t len,
		struct pb_target *);	/* mount point "fake" inode	*/

extern int	pagebuf_associate_memory(
		page_buf_t *,
		void *,
		size_t);


extern void pagebuf_hold(		/* increment reference count 	*/
		page_buf_t *);		/* buffer to hold		*/

extern void pagebuf_readahead(		/* read ahead into cache	*/
		struct pb_target  *,	/* target for buffer (or NULL)	*/
                loff_t,		       	/* starting offset of range     */
                size_t,		       	/* length of range              */
		int);			/* additional read flags	*/

/* Writing and Releasing Buffers */

extern void pagebuf_free(		/* deallocate a buffer		*/
		page_buf_t *);		/* buffer to deallocate		*/

extern void pagebuf_rele(		/* release hold on a buffer 	*/
		page_buf_t *);		/* buffer to release		*/

/* Locking and Unlocking Buffers */

extern int pagebuf_cond_lock(           /* lock buffer, if not locked   */
                        		/* (returns -EBUSY if locked)   */
		page_buf_t *);          /* buffer to lock               */

extern int pagebuf_is_locked(		/* test if buffer is locked	*/
			    		/* (0 if unlocked, 1 if locked) */
                page_buf_t *);          /* buffer to test               */

extern int pagebuf_lock_value(		/* return count on lock		*/
                page_buf_t *);          /* buffer to check              */

extern int pagebuf_lock(		/* lock buffer                  */
		page_buf_t *);          /* buffer to lock               */

extern int pagebuf_lock_disable(	/* disable buffer locking	*/
	        struct pb_target *);    /* inode for buffers		*/

extern struct pb_target *pagebuf_lock_enable(
	        kdev_t,
		struct super_block *);

extern void pagebuf_target_blocksize(
		pb_target_t *,
		unsigned int);		/* block size			*/

extern int pagebuf_target_clear(struct pb_target *);

extern void pagebuf_unlock(             /* unlock buffer                */
		page_buf_t *);          /* buffer to unlock             */

/* Buffer Management for Inodes */

extern void pagebuf_flush(		/* write buffered storage	*/
		struct inode *,		/* inode for range		*/
		loff_t,	       		/* first location in range	*/
		page_buf_flags_t);	/* PBF_ASYNC                    */

extern void pagebuf_inval(		/* invalidate buffered data for	*/
			  		/* inode 			*/
		struct inode *,		/* inode for range		*/
		loff_t,	       		/* first location in range	*/
		page_buf_flags_t);	/* PBF_ASYNC                    */

extern void pagebuf_flushinval(		/* write and invalidate */
					/* buffered data for inode */
		struct inode *,		/* inode for range		*/
		loff_t,	       		/* first location in range	*/
		page_buf_flags_t);	/* PBF_ASYNC                    */

/* Buffer Utility Routines */

extern int pagebuf_geterror(		/* return buffer error		*/
		page_buf_t *);		/* buffer 			*/

extern void pagebuf_iodone(		/* mark buffer I/O complete	*/
		page_buf_t *);		/* buffer to mark		*/

extern void pagebuf_ioerror(		/* mark buffer in error	(or not) */
		page_buf_t *,		/* buffer to mark		*/
		int);			/* error to store (0 if none)	*/

extern int pagebuf_iostart(		/* start I/O on a buffer	*/
		page_buf_t *,		/* buffer to start		*/
                page_buf_flags_t);      /* PBF_LOCK, PBF_ASYNC, PBF_READ, */
					/* PBF_WRITE, PBF_ALLOCATE,	*/
					/* PBF_DELWRI, PBF_SEQUENTIAL,  */
					/* PBF_SYNC			*/

extern int pagebuf_iorequest(		/* start real I/O		*/
                page_buf_t *);		/* buffer to convey to device	*/

	/*
	 * pagebuf_iorequest is the core I/O request routine.
	 * It assumes that the buffer is well-formed and
	 * mapped and ready for physical I/O, unlike
	 * pagebuf_iostart() and pagebuf_iophysio().  Those
	 * routines call the inode pagebuf_ioinitiate routine to start I/O,
	 * if it is present, or else call pagebuf_iorequest()
	 * directly if the inode pagebuf_ioinitiate routine is not present.  
	 */

extern int pagebuf_iowait(		/* wait for buffer I/O done	*/
		page_buf_t *);		/* buffer to wait on		*/

extern int pagebuf_iozero(		/* zero contents of buffer	*/
		struct inode *,		/* inode owning pages		*/
		page_buf_t *,		/* buffer to zero		*/
		off_t,			/* offset in buffer		*/
		size_t, 		/* size of data to zero		*/
		loff_t,			/* last permissible isize value */
		pb_bmap_t *);		/* pointer to pagebuf bmap	*/

extern caddr_t	pagebuf_offset(page_buf_t *, off_t);

extern int pagebuf_segment(		/* return next segment of buffer */
		page_buf_t *,		/* buffer to examine		*/
		loff_t *,		/* offset in buffer of next	*/
					/* segment (updated)		*/
		struct page **,		/* page (updated)		*/
					/* (NULL if not in mem_map[])	*/
		size_t *,		/* offset in page (updated)	*/
		size_t *,		/* length of segment (updated)	*/
                page_buf_flags_t);      /* PBF_ALWAYS_ALLOC             */

extern int pagebuf_iomove(		/* move data in/out of pagebuf  */
		page_buf_t *,		/* buffer to manipulate		*/
		off_t,			/* starting buffer offset	*/
		size_t,			/* length in buffer		*/
		caddr_t,		/* data pointer			*/
		page_buf_rw_t);		/* direction			*/

extern int pagebuf_segment_apply(	/* apply function to segments	*/
		page_buf_apply_t,	/* function to call		*/
		page_buf_t *);		/* buffer to examine		*/

/* Pinning Buffer Storage in Memory */

extern void pagebuf_pin(		/* pin buffer in memory		*/
		page_buf_t *);		/* buffer to pin		*/

extern void pagebuf_unpin(		/* unpin buffered data		*/
		page_buf_t *);		/* buffer to unpin		*/

extern int pagebuf_ispin( page_buf_t *); /* check if pagebuf is pinned	*/

/* Reading and writing pages */

extern ssize_t pagebuf_direct_file_read(
		struct file *,		/* file to read			*/
		char *,			/* buffer address		*/
		size_t,			/* size of buffer		*/
		loff_t *,		/* file offset to use and update */
		pagebuf_bmap_fn_t);	/* bmap function		*/

	/*
	 * pagebuf_direct_file_read reads data from the specified file
	 * at the loff_t referenced, updating the loff_t to point after the
	 * data read and returning the count of bytes read.
	 * The data is read into the supplied buffer, up to a maximum of the
	 * specified size.
	 */


extern ssize_t pagebuf_generic_file_write(
		struct file *,		/* file to write		*/
		char *,			/* buffer address		*/
		size_t,			/* size of buffer		*/
		loff_t *,		/* file offset to use and update */
		pagebuf_bmap_fn_t);	/* bmap function		*/

	/*
	 * pagebuf_generic_file_write writes data from the specified file
	 * at the loff_t referenced, updating the loff_t to point after the
	 * data written and returning the count of bytes written.
	 * The data is written from the supplied buffer, up to a maximum of the
	 * specified size.  Normally all of the data is written, unless there
	 * is an error.
	 */

extern int pagebuf_read_full_page(	/* read a page via pagebuf	*/
		struct file *, 		/* file to read			*/
		struct page *, 		/* page to read			*/
		pagebuf_bmap_fn_t);	/* bmap function		*/

extern int pagebuf_write_full_page(	/* write a page via pagebuf	*/
		struct page *,		/* page to write		*/
		pagebuf_bmap_fn_t);	/* bmap function		*/

extern void pagebuf_release_page(	/* Attempt to convert a delalloc page */
		struct page *,		/* page to write		*/
		pagebuf_bmap_fn_t);	/* bmap function		*/

extern int pagebuf_commit_write(
		struct file *,		/* file to write		*/
		struct page *,		/* page to write		*/
		unsigned,		/* from offset			*/
		unsigned);		/* to offset			*/

extern int pagebuf_prepare_write(
		struct file *,		/* file to write		*/
		struct page *,		/* page to write		*/
		unsigned,		/* from offset			*/
		unsigned,		/* to offset			*/
		pagebuf_bmap_fn_t);	/* bmap function		*/

extern void pagebuf_delwri_queue(page_buf_t *, int);
extern void pagebuf_delwri_dequeue(page_buf_t *);

#define PBDF_WAIT    0x01
#define PBDF_TRYLOCK 0x02
extern void pagebuf_delwri_flush(
		struct pb_target *,
		unsigned long,
		int *);

extern int _pagebuf_get_object(
		struct pb_target *,
		loff_t,
		size_t,
		page_buf_flags_t,
		page_buf_t **);

extern void _pagebuf_free_object(	/* deallocate a buffer		*/
		page_buf_t *,		/* buffer to deallocate		*/
		unsigned long);		/* interrupt flags		*/

extern int _pagebuf_lookup_pages(
		page_buf_t *,
		struct address_space *,
		page_buf_flags_t);

extern int pagebuf_locking_init(void);
extern void pagebuf_locking_terminate(void);

extern int pagebuf_init(void);
extern void pagebuf_terminate(void);

static __inline__ int __pagebuf_iorequest(page_buf_t *pb)
{
	if (pb->pb_strat)
		return pb->pb_strat(pb);
	return pagebuf_iorequest(pb);
}

extern size_t pagebuf_max_direct(void);


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

#endif /* __PAGE_BUF_H__ */
