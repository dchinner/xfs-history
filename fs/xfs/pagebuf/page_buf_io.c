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
 *	page_buf_io.c
 *
 * 	See generic comments about page_bufs in page_buf.c. This file deals with
 * 	file I/O (reads & writes) including delayed allocation & direct IO.
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
#include <linux/locks.h>
#include <linux/swap.h>

#include "avl.h"
#include "page_buf.h"

#define PAGE_CACHE_MASK_LL	(~((long long)(PAGE_CACHE_SIZE-1)))
#define PAGE_CACHE_ALIGN_LL(addr) \
			(((addr)+PAGE_CACHE_SIZE-1)&PAGE_CACHE_MASK_LL)
#define MAX_BUF_PER_PAGE 	1 /* (PAGE_CACHE_SIZE / 512) */
#define PBF_IO_CHUNKSIZE 	65536
#define PBF_MAX_MAPS		1

/*
 * Forward declarations.
 */
STATIC void __pb_block_commit_write_async(struct inode *,
		struct page *, int);
STATIC int __pb_block_prepare_write_async(struct inode *, struct page *,
		unsigned, unsigned, int, pagebuf_bmap_fn_t,
		page_buf_bmap_t *, int);
STATIC int pagebuf_delalloc_convert(struct inode *, struct page *,
		unsigned long, pagebuf_bmap_fn_t);
STATIC void hook_buffers_to_page(struct inode *, struct page *,
		page_buf_bmap_t *);

/*
 * The following structure is used to communicate between various levels
 * of pagebuf code. It is used by iomove, through segment_apply, into
 * the actual copyfrom or copyto routines.
 */

/* The start of this deliberately looks like a read_descriptor_t in layout */
typedef struct {
	read_descriptor_t io_rdesc;

	/* 0x10 */
	loff_t io_offset;	/* Starting offset of I/O */
	int io_iovec_nr;	/* Number of entries in iovec */

	/* 0x20 */
	struct iovec **io_iovec;	/* iovec list indexed by iovec_index */
	loff_t io_iovec_offset;	/* offset into current iovec. */
	int io_iovec_index;	/* current iovec being processed */
	loff_t io_i_size;	/* size of the file */
} pb_io_desc_t;


#define io_written	io_rdesc.written
#define io_total_count	io_rdesc.count
#define io_error	io_rdesc.error


static inline int
pagebuf_commit_write_core(
	struct inode *inode,
	struct page *page,
	unsigned from,
	unsigned to)
{
	int partial = (from - to) != PAGE_CACHE_SIZE;

	__pb_block_commit_write_async(inode, page, partial);

	kunmap(page);

	return 0;
}

int
pagebuf_commit_write(
	struct file *file,
	struct page *page,
	unsigned from,
	unsigned to)
{
	struct inode *inode = (struct inode*)page->mapping->host;
	loff_t pos = ((loff_t)page->index << PAGE_CACHE_SHIFT) + to;

	pagebuf_commit_write_core(inode, page, from, to);

	if (pos > inode->i_size)
		inode->i_size = pos;

	return 0;
}

/*
 *	pagebuf_flush: write back cached pages to disk.
 *
 */
void pagebuf_flush(
    struct inode *ip,		/* inode for range              */
    loff_t ioff,		/* first location in range      */
    page_buf_flags_t bflags)	/* buffer flags, usually        */
				/* PBF_ASYNC                    */

{
	filemap_fdatasync(ip->i_mapping);
	fsync_inode_data_buffers(ip);
	filemap_fdatawait(ip->i_mapping);
}


/*
 *	pagebuf_inval: invalidate from page-cache (no implied write back).
 */
void pagebuf_inval( /* invalidate buffered data for */
    struct inode *ip,		/* inode for range              */
    loff_t ioff,		/* first location in range      */
    page_buf_flags_t bflags)	/* buffer flags, usually PBF_ASYNC	*/
{
	truncate_inode_pages(ip->i_mapping, ioff);
}

/*
 *	pagebuf_flushinval
 */
void pagebuf_flushinval(	/* write & invalidate buffered storage	*/
    struct inode *ip,		/* inode for range              	*/
    loff_t ioff,		/* first location in range      	*/
    page_buf_flags_t bflags)	/* buffer flags, usually PBF_ASYNC	*/
{
	pagebuf_flush(ip, ioff, bflags);
	truncate_inode_pages(ip->i_mapping, ioff);
}

/*
 *	_pagebuf_handle_iovecs
 *
 *	_pagebuf_handle_iovecs uses an I/O descriptor structure containing
 *	iovec(s) and copies in/out or zeros the memory associated with it.
 *	This routine is passed (for copies in/out) a kern address with contiguous
 *	memory which is to be copied to/from the iovec(s). This routine updates
 *	the I/O descriptor to show what happened including setting io_error if
 *	something goes awry.
 */

int _pagebuf_handle_iovecs(
    pb_io_desc_t * iodp,	/* I/O descriptor */
    unsigned long offset,	/* start offset into page data */
    loff_t pb_off, size_t len,	/* Total length to copy in this call */
    page_buf_rw_t op)		/* read/write/zero */
{
	int start_off;		/* offset past kern_addr to start */
	struct iovec *iovecp;
	void *user_addr;	/* user addr computed from iovec(s) */
	size_t copy_len,	/* Amount per copy to user */
	 left,			/* not copied by __copy_to_user */
	 iov_left_len,		/* amount to do in one iovec entry */
	 copied;		/* amount successfully copied */

	/*
	 * If the offsets don't match, move kern_addr up
	 * and length down.
	 */
	if (pb_off != iodp->io_offset) {
		start_off = iodp->io_offset - pb_off;
		offset += start_off;
		len -= start_off;
	} else {
		start_off = 0;
	}

	copied = start_off;	/* Tell caller we used this + what we copy
				   use below. */

	while (len && iodp->io_error == 0 &&
	    iodp->io_iovec_index < iodp->io_iovec_nr &&
	    (iodp->io_offset < iodp->io_i_size)) {
		iovecp = iodp->io_iovec[iodp->io_iovec_index];
		user_addr = iovecp->iov_base + iodp->io_iovec_offset;
		iov_left_len = iovecp->iov_len - iodp->io_iovec_offset;
		copy_len = min(iov_left_len, len);

		/*
		 * Restrict read/zero size to what is left
		 * in the file.
		 */
		copy_len = min_t(size_t, copy_len,
				iodp->io_i_size - iodp->io_offset);

		/* Use __clear_user since we don't need
		 * to check VERIFY_WRITE, again.
		 */
		left = __clear_user(user_addr, copy_len);

		if (left) {
			copy_len -= left;
			iodp->io_error = -EFAULT;
		}

		/* Move to the next iov or update the offset in the current */
		if (copy_len == iov_left_len) {
			iodp->io_iovec_index++;
			iodp->io_iovec_offset = 0;
		} else {
			iodp->io_iovec_offset += copy_len;
		}

		/* Move along the total offset, length, writen, ... */
		iodp->io_written += copy_len;
		iodp->io_total_count -= copy_len;
		iodp->io_offset += copy_len;
		len = len - copy_len;
		copied += copy_len;
		offset += copy_len;
	}

	if (iodp->io_error)
		return iodp->io_error;
	else
		return copied;
}

/*
 *	pagebuf_iozero
 *
 *	pagebuf_iozero clears the specified range of buffer supplied,
 *	and marks all the affected blocks as valid and modified.  If
 *	an affected block is not allocated, it will be allocated.  If
 *	an affected block is not completely overwritten, and is not
 *	valid before the operation, it will be read from disk before
 *	being partially zeroed. 
 */

int pagebuf_iozero(		/* zero contents of buffer      */
    struct inode *ip,		/* inode owning buffer		*/
    page_buf_t * pb,		/* buffer to zero               */
    off_t boff,			/* offset in buffer             */
    size_t bsize,		/* size of data to zero         */
    loff_t end_size,		/* maximum file size to set	*/
    pb_bmap_t *pbmapp)		/* pointer to pagebuf bmap	*/
{
	loff_t cboff;
	size_t cpoff;
	size_t csize;
	struct page *page;
	loff_t pos;
	int at_eof;

	cboff = boff;
	boff += bsize; /* last */

	/* check range */
	if (boff > pb->pb_buffer_length)
		return (-ENOENT);

	while (cboff < boff) {
		if (pagebuf_segment(pb, &cboff, &page, &cpoff, &csize, 0)) {
			return (-ENOMEM);
		}
		assert(((csize + cpoff) <= PAGE_CACHE_SIZE));
		lock_page(page);
		SetPageUptodate(page);

		at_eof = page->index >= (ip->i_size >> PAGE_CACHE_SHIFT);

		__pb_block_prepare_write_async(ip, page,
			cpoff, cpoff+csize, at_eof, NULL,
			pbmapp, PBF_WRITE);
		memset((void *) (kmap(page) + cpoff), 0, csize);
		pagebuf_commit_write_core(ip, page, cpoff, cpoff + csize);
		pos = ((loff_t)page->index << PAGE_CACHE_SHIFT) +
			cpoff + csize;
		if (pos > ip->i_size)
			ip->i_size = pos < end_size ? pos : end_size;

		UnlockPage(page);
	}

	pb->pb_flags &= ~(PBF_READ | PBF_WRITE);
	pb->pb_flags &= ~(PBF_PARTIAL | PBF_NONE);

	return (0);
}


/* 
 *	Reading and writing files
 */


size_t
_pb_direct_io(
	struct inode	*inode,
	loff_t		rounded_offset,
	size_t		pb_size,
	page_buf_bmap_t	*mp,
	void		*user_addr,
	pb_io_desc_t	*rdp,
	int		rdwr)
{
	page_buf_t	*pb;
	struct kiobuf	*kp;
	int		rval;
	struct iovec	*iovecp;
	int		pb_flags;
	off_t		offset;
	size_t		max_io = pb_params.p_un.max_dio << PAGE_SHIFT;

	pb_size = min(pb_size, max_io);

	pb_flags = (rdwr ? PBF_WRITE : PBF_READ) | PBF_FORCEIO | _PBF_LOCKABLE;

	pb = pagebuf_lookup(inode, rounded_offset, pb_size, pb_flags);
	if (!pb) {
		if (rdp)
			rdp->io_error = -ENOMEM;
		return 0;
	}

	offset = mp->pbm_delta >> inode->i_sb->s_blocksize_bits;
	pb->pb_dev = mp->pbm_dev;
	pb->pb_bn = mp->pbm_bn + offset;

	/* Do our own allocation to avoid the buffer_head overhead */
	kp = kmalloc(sizeof(struct kiobuf), SLAB_KERNEL);
	if (!kp) {
		pb->pb_flags &= ~_PBF_LOCKABLE;
		pagebuf_rele(pb);
		if (rdp)
			rdp->io_error = -ENOMEM;
		return 0;
	}
	memset(kp, 0, sizeof(struct kiobuf));
	kp->array_len = KIO_STATIC_PAGES;
	kp->maplist = kp->map_array;
	if (rdp) {
		iovecp = rdp->io_iovec[rdp->io_iovec_index];
		user_addr = iovecp->iov_base + rdp->io_iovec_offset;
	}
	rval = map_user_kiobuf(rdwr ? WRITE : READ,
			kp, (unsigned long) user_addr, pb_size);
	if (rval == 0) {
		pb->pb_pages = kp->maplist;
		pb->pb_page_count = kp->nr_pages;
		pb->pb_offset = kp->offset;
		rval = pagebuf_iostart(pb, pb_flags);
		unmap_kiobuf(kp);
	}
	kfree(kp);

	if (rdp) {
		if (rval == 0) {
			rdp->io_written += pb_size;
			rdp->io_total_count -= pb_size;
			rdp->io_offset += pb_size;
			rdp->io_iovec_offset += pb_size;
		} else {
			rdp->io_error = rval;
		}
	}

	pb->pb_flags &= ~_PBF_LOCKABLE;
	pagebuf_rele(pb);

	return rval ? 0 : pb_size;
}

/*
 * Return size in bytes of maximum direct I/O done in one call
 */
size_t pagebuf_max_direct()
{
	return pb_params.p_un.max_dio << PAGE_SHIFT;
}

/*
 *	pagebuf_direct_file_read
 *
 *	pagebuf_file_read reads data from the specified file
 *	at the loff_t referenced, updating the loff_t to point after the
 *	data read and updating "rdp" to contain any errors and the bytes
 *	read.
 */

ssize_t
pagebuf_direct_file_read(
    struct file * filp,		/* file to read                 */
    char *buf,			/* buffer address               */
    size_t len,			/* size of buffer               */
    loff_t * lp,		/* file offset to use and update */
    pagebuf_bmap_fn_t bmap)	/* bmap function		*/
{
	pb_io_desc_t iodp, *rdp=&iodp;
	struct iovec iovec, *iovp = &iovec;
	struct inode *inode = filp->f_dentry->d_inode;
	page_buf_bmap_t maps[PBF_MAX_MAPS], *mp;
	int maps_returned, map_entry;
	unsigned long chunksize, map_size, size;
	unsigned long	rounding;
	loff_t 		mask, rounded_isize;

	if (!access_ok(VERIFY_WRITE, buf, len)) {
		return -EFAULT;
	}

	rdp->io_offset = *lp;
	rdp->io_written = 0;
	rdp->io_total_count = len;
	rdp->io_iovec_nr = 1;
	rdp->io_iovec = &iovp;
	iovp->iov_base = buf;
	iovp->iov_len = len;
	rdp->io_iovec_offset = 0;
	rdp->io_iovec_index = 0;
	rdp->io_error = 0;
	rdp->io_i_size = inode->i_size;

	 
	rounding = inode->i_sb->s_blocksize;
	mask = ~(loff_t)(rounding - 1);

	/*
	 * while we have data to do, get a bunch of mapping for this
	 * file to blocks on disk (or in delalloc or holes or ...).
	 *  For each map entry,
	 *      get a pagebuf. Note that pagebuf's are limited in size
	 *              so we need to loop again for each chunksize
	 *              within the mapping the file system returned.
	 */

	rounded_isize = (inode->i_size + rounding - 1) & mask;

	while (rdp->io_total_count && !rdp->io_error &&
	    rdp->io_offset < inode->i_size) {
		loff_t rounded_offset;
		unsigned long rounded_size;

		/*
		 * Let's start by calling bmap.
		 * Back up the offset to start at a page and increase to size
		 * to make it cover an entire page (plus any backing up done).
		 * This will return the on disk representation
		 * (or dealalloc/holes).
		 * There isn't a page for the first part of the I/O
		 * (at least there wasn't before we were called).
		 *
		 * Once we know the on disk and/or in memory representation,
		 * we can better do the I/Os or zero out or ...
		 */

		/*
		 * Make the I/O which will fill pages,
		 * page aligned and complete pages.
		 */

		rounded_offset = rdp->io_offset & mask;
		rounded_size = rdp->io_total_count
		    + (rdp->io_offset - rounded_offset);
		rounded_size =
			(rounded_size + rounding - 1) & mask;

		/*
		 * Truncate the read at the page/block where EOF resides.
		 */
		if (rounded_offset + rounded_size > rounded_isize)
			rounded_size = rounded_isize - rounded_offset;

		rdp->io_error = bmap(inode, rounded_offset, rounded_size,
					&maps[0], PBF_MAX_MAPS,
					&maps_returned, PBF_READ);

		map_entry = 0;

		while (rdp->io_total_count && map_entry < maps_returned &&
		    !rdp->io_error && rdp->io_offset < inode->i_size) {

			/*
			 * Let's look at each maps entry and decide how
			 * to handle things.
			 */

			mp = &maps[map_entry];

			/*
			 * First, get the size from this map entry that
			 * applies to this user's I/O. The offset of
			 * the mapping indicates how far from pbm_bn
			 * we need to go (in bytes) to find the block
			 * containing the offset we requested.
			 *
			 * Get the size of this mapping from the offset we
			 * care about to the end of the mapping. Then,
			 * reduce the size to lesser of the user's or the
			 * piece in the map.
			 */

			map_size = mp->pbm_bsize - mp->pbm_delta;
			size = min(map_size, rounded_size);

			if (mp->pbm_flags & (PBMF_HOLE|PBMF_UNWRITTEN)) {

				/*
				 * Zero the user's area for the size of
				 * this mapping.
				 */

				_pagebuf_handle_iovecs(rdp, 0,
				    rounded_offset, size, PBRW_ZERO);
				rounded_offset += size;
			} else {
				size_t pb_size, pb_done;

				pb_size = chunksize = size;
				pb_done = 0;

				while (pb_done < size) {
					pb_size = _pb_direct_io(inode,
							rounded_offset,
							pb_size, mp,
							NULL, rdp, 0);
					if (rdp->io_error)
						break;
					pb_done += pb_size;
					rounded_offset += pb_size;
					mp->pbm_delta += pb_size;

					/*
					 * Next size is either chunksize or what
					 * is left between pb_done and pb_size.
					 */
					pb_size = min(chunksize, size - pb_done);
				}	/* End of for chunksizes to get/read/copy pbs */
			}	/* End of else we need to do I/O */
			map_entry++;
		}		/* end for all the bmaps */
	}	/* end while we have data to do, bmap */

	if ((rdp->io_written > 0) && (rdp->io_offset > inode->i_size)) {
		rdp->io_written -= rdp->io_offset - inode->i_size;
		rdp->io_offset  = inode->i_size;
	}

	*lp = rdp->io_offset;

	if (!rdp->io_error) {
		return (rdp->io_written);
	}
	return (rdp->io_error);
}

STATIC void end_pb_buffer_io_sync(struct buffer_head *bh, int uptodate)
{
	struct page *page = bh->b_page;

	mark_buffer_uptodate(bh, uptodate);
	SetPageUptodate(page);
	unlock_buffer(bh);
}


/*
 * Generic "read page" function for block devices that have the normal
 * get_block functionality. This is most of the block device filesystems.
 * Reads the page asynchronously --- the unlock_buffer() and
 * mark_buffer_uptodate() functions propagate buffer state into the
 * page struct once IO has completed.
 *
 *	pagebuf_read_full_page
 */

int pagebuf_read_full_page(
	struct file		*filp,
	struct page		*page,
	pagebuf_bmap_fn_t	 bmap)
{
	struct inode	*inode = (struct inode*)page->mapping->host;
	/* arr is sized for worst case */
	struct buffer_head *bh, *head, *arr[MAX_BUF_PER_PAGE];
	int blocksize, bbits = inode->i_sb->s_blocksize_bits;
	long nr, i, status = 0;

	if (!PageLocked(page))
		PAGE_BUG(page);

	if (DelallocPage(page)) {
		SetPageUptodate(page);
		UnlockPage(page);
		return status;
	}

	bh = head = page->buffers;
	if (!bh || !buffer_mapped(bh)) {
		page_buf_bmap_t	map;
		int		nmaps;
		
		status = bmap(inode, ((loff_t)page->index << PAGE_CACHE_SHIFT),
				PAGE_CACHE_SIZE, &map, 1, &nmaps, PBF_READ);

		if (map.pbm_bn >= 0) {
			hook_buffers_to_page(inode, page, &map);
			bh = head = page->buffers;
		} else if (map.pbm_flags & (PBMF_HOLE|PBMF_DELAY)) {
			memset(kmap(page), 0, PAGE_CACHE_SIZE);
			flush_dcache_page(page);
			kunmap(page);
			goto page_done;
		}
	}

	nr = 0;
	i = 0;

	do {
		blocksize = bh->b_size;

		if (buffer_uptodate(bh))
			continue;

		arr[nr] = bh;
		nr++;
	}  while (i += blocksize >> bbits, (bh = bh->b_this_page) != head);

	if (!nr) {
page_done:
		/*
		 * all buffers are uptodate - we can set the page
		 * uptodate as well.
		 */
		SetPageUptodate(page);
		UnlockPage(page);
		return 0;
	}

	/* Stage two: lock the buffers */
	for (i = 0; i < nr; i++) {
		struct buffer_head * bh = arr[i];
		lock_buffer(bh);
		set_buffer_async_io(bh);
	}

	/* Stage 3: start the IO */
	for(i=0; i < nr; i++){
		submit_bh(READ, arr[i]);
	}
	return 0;
}


/*
 *	pagebuf_write_full_page
 */

int pagebuf_write_full_page(struct page *page, pagebuf_bmap_fn_t bmap)
{
	struct inode *inode = (struct inode*)page->mapping->host;
	unsigned long end_index = inode->i_size >> PAGE_CACHE_SHIFT, pb_flags;
	loff_t offset;
	int ret;

	if (DelallocPage(page))
		pb_flags = PBF_WRITE|PBF_FILE_ALLOCATE;
	else
		pb_flags = PBF_WRITE|PBF_DIRECT;
	/* easy case */
	if (page->index < end_index) {
		ret = pagebuf_delalloc_convert(inode, page, pb_flags, bmap);
		goto out;
	}

	/* things got complicated... */
	offset = inode->i_size & (PAGE_CACHE_SIZE-1);
	/* OK, are we completely out? */
	if ((page->index >= end_index+1) || !offset) {
		ret =  -EIO;
		goto out;
	}
	ret = __pb_block_prepare_write_async(inode, page,
			0, PAGE_CACHE_SIZE, 1, bmap, NULL, pb_flags);
	if (!ret) {
		struct buffer_head *bh, *head;

		__pb_block_commit_write_async(inode, page, 0);
		
		/*
		 * Pages over holes may have just been allocated
		 * buffers, so bh needs to be looked up now.
		 */
		bh = page->buffers;

		/*
		 * Kick-start that last write...
		 */
		head = bh;

		do {
			lock_buffer(bh);
			set_buffer_async_io(bh);
			set_bit(BH_Uptodate, &bh->b_state);
			clear_bit(BH_Dirty, &bh->b_state);
			bh = bh->b_this_page;
		} while (bh != head);

		do {
			submit_bh(WRITE, bh);
			bh = bh->b_this_page;
		} while (bh != head);
	}

	kunmap(page);
out:
	if (ret < 0) {
		/*
		 * If it's delalloc and we have nowhere to put it,
		 * throw it away.
		 */
		if (DelallocPage(page))
			block_flushpage(page, 0);
		ClearPageUptodate(page);
		UnlockPage(page);
	}

	return ret;
}

STATIC void
hook_buffers_to_page_delay(struct inode *inode, struct page *page)
{
	struct buffer_head 	*bh;

	if (page->buffers)
		BUG();
	create_empty_buffers(page, inode->i_dev, PAGE_CACHE_SIZE);
	bh = page->buffers;
	bh->b_state = (1 << BH_Delay) | (1 << BH_Mapped);
	__mark_buffer_dirty(bh);
	buffer_insert_inode_data_queue(bh, inode);
	balance_dirty();
}

STATIC void
hook_buffers_to_page(struct inode *inode,
	struct page *page, page_buf_bmap_t *mp)
{
	struct buffer_head 	*bh;
	page_buf_daddr_t	bn;
	loff_t			delta;

	if (!page->buffers)
		create_empty_buffers(page, mp->pbm_dev, PAGE_CACHE_SIZE);
	bh = page->buffers;
	/*
	 * pbm_offset:pbm_bn :: (page's offset):???
	 * 
	 * delta = offset of _this_ page in extent.
	 * pbm_offset = offset in file corresponding to pbm_bn.
	 */
	delta = page->index;	    /* do computations in 64 bit ...   */
	delta <<= PAGE_CACHE_SHIFT; /* delta now offset from 0 of page */
	delta -= mp->pbm_offset;    /* delta now offset in extent of page */

	bn = mp->pbm_bn >>
		(PAGE_CACHE_SHIFT - inode->i_sb->s_blocksize_bits);
	bn += (delta >> PAGE_CACHE_SHIFT);
	bh->b_blocknr = bn; 
	bh->b_dev = mp->pbm_dev;
	if (buffer_locked(bh))
		BUG();
	set_bit(BH_Mapped, &bh->b_state);
}

STATIC void
set_buffer_dirty_uptodate(
	struct inode *inode,
	struct buffer_head *bh,
	int partial)
{
	int need_balance_dirty = 0;

#ifdef PAGEBUF_DEBUG
	/* negative blocknr is always bad; if it's zero, and the
	 * inode & bh are on different devices, it could be an
	 * xfs realtime file, which would be OK.  Either that
	 * or something is seriously wrong!
	 */
	if ((bh->b_blocknr < 0) || 
	    (bh->b_blocknr == 0) && (inode->i_dev == bh->b_dev)) {
		printk("Warning: buffer 0x%p with weird blockno (%ld)\n",
			bh, bh->b_blocknr);
	}
#endif /* PAGEBUF_DEBUG */
	set_bit(BH_Uptodate, &bh->b_state);
	if (!buffer_dirty(bh)) {
		need_balance_dirty = 1;
	}
	__mark_buffer_dirty(bh);

	if (need_balance_dirty) {
		buffer_insert_inode_data_queue(bh, inode);
		if (!partial)
			balance_dirty();
	}
}

STATIC int
__pb_block_prepare_write_async(struct inode *inode, struct page *page,
		unsigned from, unsigned to, int at_eof,
		pagebuf_bmap_fn_t  bmap, page_buf_bmap_t *mp, int flags)
{
	struct buffer_head 	*bh;
	int 			err = 0;
	int			nmaps, dp = DelallocPage(page);
	char			*kaddr = kmap(page);
	page_buf_bmap_t		map;


	/*
	 * Create & map buffer.
	 *
	 * If we are not already mapped via buffers to a disk address,
	 * or the page is not already marked delalloc, then we need to
	 * go get some space.
	 */
	bh = page->buffers;
	if ((!bh || buffer_delay(bh)) && (!dp || (flags & PBF_FILE_ALLOCATE)))
	{
		if (!mp) {
			mp = &map;
			err = bmap(inode, ((loff_t)page->index << PAGE_CACHE_SHIFT),
				PAGE_CACHE_SIZE, mp, 1, &nmaps, flags);
			if (err < 0) {
				goto out;
			}
		}
		if (mp->pbm_bn >= 0) {
			hook_buffers_to_page(inode, page, mp);
			bh = page->buffers;
			if (dp) {
				lock_buffer(bh);
				clear_bit(BH_Delay, &bh->b_state);
				unlock_buffer(bh);
			}
		}
	}

	/* Is the write over the entire page?  */
	if (from == 0 && to == PAGE_CACHE_SIZE)
		goto out;

	/*  Partial write. Is the page valid anyway?  */
	if (Page_Uptodate(page) || dp)
		goto out;

	/*
	 * If writing at eof and the i_size at beginning of page
	 * then we can zero the page (or parts of it).
	 */
	if ((at_eof && (!bh || buffer_delay(bh) ||
		(inode->i_size & ~PAGE_CACHE_MASK_LL) == 0)) ||
		(mp && (mp->pbm_flags & (PBMF_DELAY|PBMF_UNWRITTEN)))) {

		/*
		 * Zero the parts of page not coverd by this I/O
		 */
		if (PAGE_CACHE_SIZE > to) {
			memset(kaddr+to, 0, PAGE_CACHE_SIZE-to);
		}
		if (0 < from) {
			memset(kaddr, 0, from);
		}
		if ((0 < from) || (PAGE_CACHE_SIZE > to))
			flush_dcache_page(page);
		goto out;
	}
	/*
	 * Ensure only one block allocated.
	 */
	if (bh != bh->b_this_page) {
		printk("bh 0x%p != bh->b_this_page 0x%p\n",bh,bh->b_this_page);
		err = -EIO;
		goto out;
	}
	lock_buffer(bh);
	bh->b_end_io = end_pb_buffer_io_sync;

	submit_bh(READ,bh);
	wait_on_buffer(bh);
	if (!buffer_uptodate(bh))
		err = -EIO;
out:
	return err;
}

int
pagebuf_prepare_write(
	struct file *file,
	struct page *page,
	unsigned from,
	unsigned to,
	pagebuf_bmap_fn_t  bmap)
{
	struct inode *inode = (struct inode*)page->mapping->host;
	int at_eof = page->index >= (inode->i_size >> PAGE_CACHE_SHIFT);
	int err;

	err = __pb_block_prepare_write_async(inode, page, from, to,
					at_eof, bmap, NULL, PBF_WRITE);
	if (err) {
		ClearPageUptodate(page);
		kunmap(page);
	}

	return err;
}


STATIC void
__pb_block_commit_write_async(struct inode 	*inode,
				struct page 	*page,
				int		partial)
{
	struct buffer_head	*bh;

	/*
	 * Prepare write took care of reading/zero-out
	 * parts of page not covered by from/to. Page is now fully valid.
	 */
	SetPageUptodate(page);
	if ((bh = page->buffers) && !buffer_delay(bh)) {
		set_buffer_dirty_uptodate(inode, page->buffers, partial);
	} else if (!DelallocPage(page)) {
		hook_buffers_to_page_delay(inode, page);
	}
}

int
__pagebuf_do_delwri(
	struct file	*filp,		/* file to write                */
	loff_t 		rounded_offset, /* offset in file, page aligned	     */
	unsigned long 	size,		/* size to write, constrained by wdp */
	char		*buf,
	size_t		len,
	loff_t		*lp,
	page_buf_bmap_t *mp)		/* bmap for page		     */
{
	struct inode *inode = filp->f_dentry->d_inode;
	struct page *page;
	unsigned long done;
	int err = 0, written = 0;
	loff_t foff = *lp;
	char *kaddr;

	for (done = 0; (done < size) && len;
		done += PAGE_CACHE_SIZE, rounded_offset += PAGE_CACHE_SIZE)
	{
		unsigned long bytes;
		int at_eof;
		unsigned long offset;

		offset = foff & (PAGE_CACHE_SIZE - 1);
		bytes = PAGE_CACHE_SIZE - offset;
		if (bytes > len)
			bytes = len;
		bytes = min(bytes, size);

		/*
		 * Bring in the user page that we will copy from _first_.
		 * Otherwise there's a nasty deadlock on copying from the
		 * same page as we're writing to, without it being marked
		 * up-to-date.
		 */
		{ volatile unsigned char dummy;
			__get_user(dummy, buf);
			__get_user(dummy, buf+bytes-1);
		}

		page = grab_cache_page(inode->i_mapping,
				rounded_offset >> PAGE_CACHE_SHIFT);

		if (!page) {
			err = -ENOMEM;
			break;
		}

		at_eof = foff == inode->i_size;

		err = __pb_block_prepare_write_async(inode, page,
			offset, offset + bytes,
			at_eof, NULL, mp, PBF_WRITE);
		
		if (err)
			goto unlock;
		kaddr = page_address(page);

		err = __copy_from_user(kaddr + offset, buf, bytes);
		if (err) {
			err = -EFAULT;
			kunmap(page);
			ClearPageUptodate(page);
			goto unlock;
		}

		pagebuf_commit_write(filp, page, offset, offset + bytes);

		foff += bytes;
		len -= bytes;
		written += bytes;
		buf += bytes; 

unlock:
		UnlockPage(page);
		page_cache_release(page);
		if (err < 0)
			break;
	}
	*lp = foff;
	return err ? err : written;
}

int
_pagebuf_file_write(
	struct file * filp,	/* file to write                */
	char *buf,		/* buffer address               */
	size_t len,		/* size of buffer               */
	loff_t * lp,		/* file offset to use and update */
	pagebuf_bmap_fn_t  bmap,	/* bmap function */
	int pb_flags)		/* flags to pass to bmap calls	*/
{
	struct inode *inode = filp->f_dentry->d_inode;
	page_buf_bmap_t map;
	int maps_returned;
	unsigned long map_size, size;
	int status = 0, written = 0;
	loff_t foff = *lp;
	int sync;

	sync = filp->f_flags & (O_SYNC|O_DIRECT);

	/*
	 * while we have data to do, get a bunch of mapping for this
	 * file to blocks on disk (or in delalloc or holes or ...).
	 *  For each map entry,
	 *      get a pagebuf. Note that pagebuf's are limited in size
	 *              so we need to loop again for each chunksize
	 *              within the mapping the file system returned.
	 */

	while (len) {

		loff_t rounded_offset;
		size_t rounded_size;

		/*
		 * Let's start by calling bmap for the offset/len we have.
		 * This will return the on disk representation
		 * (or dealalloc/holes).
		 *
		 * Once we know the on disk and/or in memory representation,
		 * we can better do the I/Os in chunks or ...
		 */

		/*
		 * Make the I/O which will fill pages,
		 * page aligned and complete pages.
		 */

		if (!(filp->f_flags & O_DIRECT)) {
			rounded_offset = foff & PAGE_CACHE_MASK_LL;
		} else {
			rounded_offset = foff;
		}
		rounded_size = len + (foff - rounded_offset);
		rounded_size =
			(rounded_size + PAGE_CACHE_SIZE - 1) &
			PAGE_CACHE_MASK;
		size = rounded_size;

		/*
		 * round the size up to some minimum value
		 * since this is allocation. 64K for now.
		 *
		 * Don't round up if we are writing within the file
		 * since this probably means seek/write/... and
		 * it isn't sequential. Leave holes.
		 */

		if ((rounded_offset >= inode->i_size) && !sync)
			size = PBF_IO_CHUNKSIZE;

		status = bmap(inode, rounded_offset, size,
				&map, 1, &maps_returned, pb_flags);

		if (status) {
			break;
		}

		/*
		 * Get the size of this mapping from the offset we
		 * care about to the end of the mapping. Then,
		 * reduce the size to lesser of the user's or the
		 * piece in the map.
		 */

		map_size = map.pbm_bsize - map.pbm_delta;
		size = min((unsigned long)map_size, size);

		/*
		 * Holes mean we came to the end of the space returned
		 * from the file system. We need to go back and ask for
		 * more space.
		 */
		if (map.pbm_flags & PBMF_HOLE) {
			printk("pbfwa: HOLE ro 0x%Lx size 0x%lx mp 0x%p\n",
				rounded_offset, size, &map);
			break;
		}
		/*
		 * Handle delwri or direct I/O
		 */
		if ((filp->f_flags & O_DIRECT) == 0) {
			status = __pagebuf_do_delwri(filp,
					rounded_offset, size, buf,
					len, &foff, &map);
		} else {
			int	io_size = (int)min(size, (unsigned long) len);

			status = _pb_direct_io(inode, rounded_offset,
					io_size, &map, buf, NULL, 1);
			if (status > 0)
				foff += status;
			if (foff > inode->i_size)
				inode->i_size = foff;
		}
		if (status <= 0)
			break;
		written += status;
		buf += status;
		len -= status;
	}
	*lp = foff;
	return written ? written : status;
}

ssize_t
pagebuf_generic_file_write(
    struct file * filp,		/* file to write                */
    char *buf,			/* buffer address               */
    size_t len,			/* size of buffer               */
    loff_t * lp,		/* file offset to use and update */
    pagebuf_bmap_fn_t  bmap)	/* bmap function */
{
	struct inode *inode = filp->f_dentry->d_inode;
	unsigned long index;
	int status = 0, written = 0;
	char *kaddr;
	loff_t foff;
	struct page *page;
	int pb_flags;
	int direct = filp->f_flags & O_DIRECT;

	pb_flags = PBF_WRITE;
	if (filp->f_flags & O_SYNC)
		pb_flags |= PBF_SYNC;
	if (direct)
		pb_flags |= PBF_DIRECT;

	if ((foff = *lp) < 0)
		goto out;

	if ((status = filp->f_error) != 0) {
		filp->f_error = 0;
		goto out;
	}

	while (len) {
		unsigned long bytes, offset;
		int	at_eof;
		/*
		 * Try to find the page in the cache. If it isn't there,
		 * allocate a free page. If the write is sufficiently big,
		 * use pagebufs with possible delayed allocation.
		 */
		offset = foff & (PAGE_CACHE_SIZE - 1);
		index = foff >> PAGE_CACHE_SHIFT;
		bytes = PAGE_CACHE_SIZE - offset;
		if (bytes > len) {
			bytes = len;
		}

		if (&inode->i_data != inode->i_mapping)
			BUG();

		if (!direct) {
			/*
			 * Bring in the user page that we will copy from
			 * _first_. Otherwise there's a nasty deadlock on
			 * copying from the same page as we're writing to,
			 * without it being marked up-to-date.
			 */
			{ volatile unsigned char dummy;
				__get_user(dummy, buf);
				__get_user(dummy, buf+bytes-1);
			}

			page = find_lock_page(&inode->i_data, index);
		} else {
			page = NULL;
		}

		if (!page) {
			status = _pagebuf_file_write(filp,
					buf, len, &foff, bmap, pb_flags);
			if (status > 0)
				written += status;
			break;
		}

		if (!PageLocked(page)) {
			PAGE_BUG(page);
		}

		at_eof = foff == inode->i_size;

		status = __pb_block_prepare_write_async(inode, page,
			offset, offset + bytes, at_eof, bmap,
			NULL, pb_flags);
			
		if (status)
			goto unlock;

		kaddr = page_address(page);
		status = __copy_from_user(kaddr+offset, buf, bytes);

		if (status) {
			status = -EFAULT;
			kunmap(page);
			ClearPageUptodate(page);
			goto unlock;
		}

		pagebuf_commit_write(filp, page, offset, offset + bytes);

		len -= bytes;
		buf += bytes;
		foff += bytes;
		written += bytes;

unlock:
		SetPageReferenced(page);
		UnlockPage(page);
		page_cache_release(page);

		if (status < 0)
			break;
	}
	*lp = foff;

out:
	return written ? written : status;
}

/*
 * Probe for a given page (index) in the inode & test if it is delayed.
 * Returns page locked and with an extra reference count.
 */
STATIC struct page *
probe_page(struct inode *inode, unsigned long index)
{
	struct page *page, **hash;

	hash = page_hash(inode->i_mapping, index);
	page = __find_get_page(inode->i_mapping, index, hash);
	if (!page)
		return NULL;
	if (TryLockPage(page)) {
		page_cache_release(page);
		return NULL;
	}
	if (!page->mapping || !DelallocPage(page)) {
		UnlockPage(page);
		page_cache_release(page);
		return NULL;
	}
	return page;
}


/*
 * Allocate & map buffers for page given the extent map. Write it out.
 */

STATIC void
convert_page(struct inode *inode, struct page *page, page_buf_bmap_t *mp)
{
	struct buffer_head *head, *bh = page->buffers;
	struct buffer_head *array[PAGE_CACHE_SIZE >> 9];
	int i, count = 0;

	if (!bh || DelallocPage(page)) {
		hook_buffers_to_page(inode, page, mp);
		bh = page->buffers;
	}

	head = bh;
	do {
		lock_buffer(bh);
		if (!test_and_clear_bit(BH_Delay, &bh->b_state) ||
		    atomic_set_buffer_clean(bh)) {
			get_bh(bh);
			bh->b_end_io = end_buffer_io_sync;
			refile_buffer(bh);
			array[count++] = bh;
		} else {
			unlock_buffer(bh);
			refile_buffer(bh);
		}
		bh = bh->b_this_page;
	} while (bh != head);

	for (i = 0; i < count; i++) {
		submit_bh(WRITE, array[i]);
	}

	SetPageUptodate(page);
	UnlockPage(page);
	page_cache_release(page);
}


/*
 * Convert & write out a cluster of pages in the same extent as defined
 * by mp and surrounding the start page.
 */
STATIC int
cluster_write(struct inode *inode,
	      struct page *startpage,
	      page_buf_bmap_t *mp)
{
	unsigned long tindex, tlast;
	struct page *page;

	if (startpage->index != 0) {
		tlast = mp->pbm_offset >> PAGE_CACHE_SHIFT;
		for (tindex = startpage->index-1; tindex >= tlast; tindex--) {
			if (!(page = probe_page(inode, tindex)))
				break;
			convert_page(inode, page, mp);
		}
	}
	convert_page(inode, startpage, mp);
	tlast = PAGE_CACHE_ALIGN_LL(mp->pbm_offset + mp->pbm_bsize) >>
							PAGE_CACHE_SHIFT;
	for (tindex = startpage->index + 1; tindex < tlast; tindex++) {
		if (!(page = probe_page(inode, tindex)))
			break;
		convert_page(inode, page, mp);
	}
	return 0;
}

STATIC int
pagebuf_delalloc_convert(
	struct inode *inode,
	struct page *page,	/* delalloc page to convert - locked */
	unsigned long flags,	/* allocation mode to use */
	pagebuf_bmap_fn_t  bmap) /* bmap function */
{
	page_buf_bmap_t maps[PBF_MAX_MAPS];
	int maps_returned, error;
	loff_t rounded_offset;

	rounded_offset = ((unsigned long long) page->index) << PAGE_CACHE_SHIFT;
	error = bmap(inode, rounded_offset, PAGE_CACHE_SIZE,
			&maps[0], PBF_MAX_MAPS, &maps_returned, flags);
	if (error)
		return error;

	if ((maps[0].pbm_flags & PBMF_HOLE)) {
		printk("delalloc page 0x%p with no extent index 0x%lx\n",
			page, page->index);
		BUG();
	}

	if (maps[0].pbm_delta % PAGE_CACHE_SIZE) {
		printk("PCD: pbm_delta not page aligned mp 0x%p\n", &maps[0]);
		return -EIO;
	}

	/*
	 * page needs to be setup as though find_page(...) returned it,
	 * which is a locked page with an extra reference.
	 */
	page_cache_get(page);
	return cluster_write(inode, page, &maps[0]);
}

