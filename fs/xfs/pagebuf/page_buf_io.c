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

#include "page_buf_internal.h"

#define MAX_BUF_PER_PAGE 	(PAGE_CACHE_SIZE / 512)
#define PBF_IO_CHUNKSIZE 	65536
#define PBF_MAX_MAPS		1 /* TODO: XFS_BMAP_MAX_NMAP? */

/*
 * Forward declarations.
 */
STATIC void __pb_block_commit_write_async(
		pb_target_t *, struct inode *, struct page *, int);
STATIC int  __pb_block_prepare_write_async(
		pb_target_t *, struct inode *, struct page *, unsigned,
		unsigned, int, pagebuf_bmap_fn_t, page_buf_bmap_t *, int, int);
STATIC int  pagebuf_delalloc_convert(
		pb_target_t *, struct inode *, struct page *, unsigned long,
		pagebuf_bmap_fn_t, int);

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
	pb_target_t	*target,
	struct inode	*inode,
	struct page	*page,
	unsigned	from,
	unsigned	to)
{
	int		partial = (to - from) != PAGE_CACHE_SIZE;

	__pb_block_commit_write_async(target, inode, page, partial);
	kunmap(page);

	return 0;
}

int
pagebuf_commit_write(
	pb_target_t	*target,
	struct page	*page,
	unsigned	from,
	unsigned	to)
{
	struct inode *inode = (struct inode*)page->mapping->host;
	loff_t pos = ((loff_t)page->index << PAGE_CACHE_SHIFT) + to;

	pagebuf_commit_write_core(target, inode, page, from, to);
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
int
pagebuf_iozero(
	struct inode	*ip,		/* inode owning buffer		*/
	page_buf_t	*pb,		/* buffer to zero               */
	off_t		boff,		/* offset in buffer             */
	size_t		bsize,		/* size of data to zero         */
	loff_t		end_size,	/* maximum file size to set	*/
	pb_bmap_t	*pbmapp,	/* pointer to pagebuf bmap	*/
	int		nmaps)
{
	loff_t cboff;
	size_t cpoff;
	size_t csize;
	struct page *page;
	loff_t pos;
	int at_eof;

	assert(pb->pb_target);

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

		__pb_block_prepare_write_async(pb->pb_target, ip, page,
			cpoff, cpoff+csize, at_eof, NULL,
			pbmapp, nmaps, PBF_WRITE);
		/* __pb_block_prepare_write already kmap'd the page */
		memset((void *) (page_address(page) + cpoff), 0, csize);
		pagebuf_commit_write_core(pb->pb_target, ip, page,
			cpoff, cpoff + csize);
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
	struct pb_target	*target,
	struct inode		*inode,
	loff_t			rounded_offset,
	size_t			pb_size,
	page_buf_bmap_t		*mp,
	void			*user_addr,
	pb_io_desc_t		*rdp,
	int			rdwr)
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

	pb = pagebuf_lookup(target, inode, rounded_offset, pb_size, pb_flags);
	if (!pb) {
		if (rdp)
			rdp->io_error = -ENOMEM;
		return 0;
	}

	offset = mp->pbm_delta >> inode->i_sb->s_blocksize_bits;
	pb->pb_dev = target->pbr_device;
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
	struct pb_target	*target,
	struct file		*filp,	/* file to read                 */
	char			*buf,	/* buffer address               */
	size_t			len,	/* size of buffer               */
	loff_t			*lp,	/* file offset to use and update*/
	pagebuf_bmap_fn_t	bmap)	/* bmap function		*/
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
	 * while we have data to do, get a bunch of mappings for this
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

		rdp->io_error = bmap(inode,
					rounded_offset, rounded_size,
					maps, PBF_MAX_MAPS, &maps_returned,
					PBF_READ);

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
					pb_size = _pb_direct_io(target, inode,
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
 * __pb_match_offset_to_mapping
 * Finds the corresponding mapping in block @map array of the
 * given @offset within a @page.
 * @index keeps track of where we were in @map after previous
 * calls with earlier @offsets.
 */
page_buf_bmap_t *
__pb_match_offset_to_mapping(
	struct page		*page,
	page_buf_bmap_t		*map,
	int			nmaps,
	unsigned long		offset,
	int			*index)
{
	int			i;
	loff_t			full_offset;	/* offset from start of file */

	assert(*index <= nmaps);
	assert(offset < PAGE_CACHE_SIZE);

	full_offset = page->index << PAGE_CACHE_SHIFT;	/* from file start */
	full_offset += offset;			/* include from page start */

	for (i = *index; i < nmaps; i++) {
		if (map[i].pbm_offset + map[i].pbm_bsize > full_offset)
			break;
	}
	if ((*index = i) == nmaps)
		return NULL;
	return &map[i];
}

void
__pb_map_buffer_at_offset(
	struct pb_target	*target,
	struct page		*page,
	struct buffer_head	*bh,
	unsigned long		offset,
	int			block_bits,
	page_buf_bmap_t		*mp)
{
	page_buf_daddr_t	bn;
	loff_t			delta;
	int			sector_shift;

	assert(!(mp->pbm_flags & PBMF_HOLE));
	assert(!(mp->pbm_flags & PBMF_DELAY));
	assert(!(mp->pbm_flags & PBMF_UNWRITTEN));
	assert(mp->pbm_bn != PAGE_BUF_DADDR_NULL);

	delta = page->index;
	delta <<= PAGE_CACHE_SHIFT;
	delta -= mp->pbm_offset;
	delta += offset;
	delta >>= block_bits;

	sector_shift = block_bits - 9;
	bn = mp->pbm_bn >> sector_shift;
	bn += delta;
	assert((bn << sector_shift) >= mp->pbm_bn);

	lock_buffer(bh);
	bh->b_blocknr = bn;
	bh->b_dev = target->pbr_device;
	set_bit(BH_Mapped, &bh->b_state);
	clear_bit(BH_Delay, &bh->b_state);
	unlock_buffer(bh);
}

STATIC void
zero_buffer_at_offset(
	struct pb_target	*target,
	struct page		*page,
	struct buffer_head	*bh,
	unsigned long		offset)
{
	assert(PageLocked(page));

	memset(kmap(page) + offset, 0, target->pbr_blocksize);
	flush_dcache_page(page);
	kunmap(page);

	lock_buffer(bh);
	bh->b_dev = target->pbr_device;
	set_bit(BH_Uptodate, &bh->b_state);
	clear_bit(BH_Mapped, &bh->b_state);
	clear_bit(BH_Delay, &bh->b_state);
	unlock_buffer(bh);
}

/*
 * pagebuf_read_full_page
 * Originally derived from buffer.c::block_read_full_page
 */
int
pagebuf_read_full_page(
	pb_target_t		*target,
	struct page		*page,
	pagebuf_bmap_fn_t	 bmap)
{
	struct inode		*inode = page->mapping->host;
	unsigned long		offset;
	struct buffer_head	*bh, *head, *arr[MAX_BUF_PER_PAGE];
	page_buf_bmap_t		*tmp, *mp = NULL, maps[PBF_MAX_MAPS];
	int			err, index = 0, nmaps = 0, i = 0, nr = 0;
	int			bbits = target->pbr_blocksize_bits;

	if (!PageLocked(page))
		PAGE_BUG(page);

	if (!page->buffers) {
		/* shortcut, may not need a buffer_head here */
		if (target->pbr_blocksize == PAGE_CACHE_SIZE) {
			mp = maps;
			err = bmap(inode,
				((loff_t)page->index << PAGE_CACHE_SHIFT),
				PAGE_CACHE_SIZE, maps, PBF_MAX_MAPS, &nmaps,
			 	PBF_READ);
			if (err) {
				UnlockPage(page);
				return err;
			}
			assert(nmaps == 1);
			if (maps[0].pbm_flags & (PBMF_HOLE|PBMF_DELAY)) {
				memset(kmap(page), 0, PAGE_CACHE_SIZE);
				flush_dcache_page(page);
				kunmap(page);
				goto page_done;
			}
		}
		create_empty_buffers(page, target->pbr_device,
					   target->pbr_blocksize);
	}
	bh = head = page->buffers;

	/* Stage 1: find buffers in need of IO */
	do {
		if (buffer_delay(bh))
			continue;
		if (buffer_uptodate(bh))
			continue;
		if (buffer_mapped(bh))
			continue;
		offset = i << bbits;
		if (!mp) {
remap:
			index = 0;
			mp = maps;
			err = bmap(inode,
				((loff_t)page->index << PAGE_CACHE_SHIFT) +
					offset,
				PAGE_CACHE_SIZE - offset,
				maps, PBF_MAX_MAPS, &nmaps, PBF_READ);
			if (err) {
				UnlockPage(page);
				return err;
			}
		}
		tmp = __pb_match_offset_to_mapping(page, mp, nmaps,
							offset, &index);
		if (!tmp || tmp->pbm_flags & (PBMF_HOLE|PBMF_DELAY)) {
			if (!tmp) {
				/*
				 * No mapping for this offset.  2 cases:
				 * - last mapping was at eof (zero to end)
				 * - we need more mappings (try, try again)
				 */
				if (!(mp[nmaps - 1].pbm_flags & PBMF_EOF)) {
					goto remap;
				}
			}
			zero_buffer_at_offset(target, page, bh, offset);
			continue;
		}
		__pb_map_buffer_at_offset(target, page, bh, offset, bbits, tmp);
		arr[nr++] = bh;
	} while (i++, (bh = bh->b_this_page) != head);

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
		lock_buffer(arr[i]);
		set_buffer_async_io(arr[i]);
	}

	/* Stage 3: start the IO */
	for (i = 0; i < nr; i++) {
		submit_bh(READ, arr[i]);
	}
	return 0;
}

void
pagebuf_release_page(
	pb_target_t		*target,
	struct page		*page,
	pagebuf_bmap_fn_t	bmap)
{
	struct inode *inode = (struct inode*)page->mapping->host;
	unsigned long pb_flags;

	if (PageDelalloc(page))
		pb_flags = PBF_WRITE|PBF_FILE_ALLOCATE;
	else
		pb_flags = PBF_WRITE|PBF_DIRECT;
	pagebuf_delalloc_convert(target, inode, page, pb_flags, bmap, 0);
}

/*
 * pagebuf_write_full_page
 * Originally derived from buffer.c::block_write_full_page
 */
int
pagebuf_write_full_page(
	pb_target_t		*target,
	struct page		*page,
	pagebuf_bmap_fn_t	bmap)
{
	struct inode *inode = (struct inode*)page->mapping->host;
	unsigned long end_index = inode->i_size >> PAGE_CACHE_SHIFT, pb_flags;
	int ret;

	if (PageDelalloc(page))
		pb_flags = PBF_WRITE|PBF_FILE_ALLOCATE;
	else
		pb_flags = PBF_WRITE|PBF_DIRECT;


	/* Are we off the end of the file ? */
	if (page->index > end_index) {
		loff_t offset = inode->i_size & (PAGE_CACHE_SIZE-1);
		if ((page->index >= end_index+1) || !offset) {
			ret =  -EIO;
			goto out;
		}
	}

	ret = pagebuf_delalloc_convert(target, inode, page, pb_flags, bmap, 1);
out:
	if (ret < 0) {
		/*
		 * If it's delalloc and we have nowhere to put it,
		 * throw it away.
		 */
		if (PageDelalloc(page))
			block_flushpage(page, 0);
		ClearPageUptodate(page);
		UnlockPage(page);
	}

	return ret;
}

STATIC int
__pb_block_prepare_write_async(
	pb_target_t		*target,
	struct inode		*inode,
	struct page		*page,
	unsigned		from,
	unsigned		to,
	int			at_eof,
	pagebuf_bmap_fn_t	bmap,
	page_buf_bmap_t		*mp,
	int			nmaps,
	int			flags)
{
	struct buffer_head	*bh, *head, *arr[MAX_BUF_PER_PAGE];
	unsigned long		offset;
	int			index = 0, i = 0, nr = 0, err = 0;
	int			bbits = target->pbr_blocksize_bits;
	char			*kaddr = kmap(page);
	page_buf_bmap_t		*tmp, maps[PBF_MAX_MAPS];

	if (!page->buffers)
		create_empty_buffers(page, target->pbr_device,
					   target->pbr_blocksize);

	bh = head = page->buffers;
	do {
		if (buffer_mapped(bh))
			continue;
		offset = i << bbits;
		if (!mp) {
remap:
			index = 0;
			mp = maps;
			err = bmap(inode,
				((loff_t)page->index << PAGE_CACHE_SHIFT) +
					offset,
				PAGE_CACHE_SIZE - offset,
				maps, PBF_MAX_MAPS, &nmaps, flags);
			if (err < 0)
				return err;
		}
		tmp = __pb_match_offset_to_mapping(page, mp, nmaps,
							offset, &index);
		if (!tmp || tmp->pbm_flags & (PBMF_HOLE|PBMF_DELAY)) {
			if (!tmp) {
				/*
				 * No mapping for this offset.  2 cases:
				 * - last mapping was at eof (zero to end)
				 * - we need more mappings (try, try again)
				 */
				if (!(mp[nmaps - 1].pbm_flags & PBMF_EOF)) {
					goto remap;
				}
			}
			zero_buffer_at_offset(target, page, bh, offset);
			continue;
		}
		__pb_map_buffer_at_offset(target, page, bh, offset, bbits, tmp);
		arr[nr++] = bh;
	} while (i++, (bh = bh->b_this_page) != head);

	/* Is the write over the entire page?  */
	if (from == 0 && to == PAGE_CACHE_SIZE)
		return 0;

	/*  Partial write. Is the page valid anyway?  */
	if (Page_Uptodate(page) || nr == 0)
		return 0;

	/*
	 * If writing at eof and the i_size at beginning of page
	 * then we can zero the page (or parts of it).
	 */
	if (at_eof && ((inode->i_size & ~PAGE_CACHE_MASK_LL) == 0)) {
		if (PAGE_CACHE_SIZE > to)
			memset(kaddr+to, 0, PAGE_CACHE_SIZE-to);
		if (0 < from)
			memset(kaddr, 0, from);
		if ((0 < from) || (PAGE_CACHE_SIZE > to))
			flush_dcache_page(page);
		return 0;
	}

	for (i = 0; i < nr; i++) {
		lock_buffer(arr[i]);
		arr[i]->b_end_io = end_pb_buffer_io_sync;
	}

	for (i = 0; i < nr; i++)
		submit_bh(READ, arr[i]);

	for (i = 0; i < nr; i++) {
		wait_on_buffer(arr[i]);
		if (!buffer_uptodate(arr[i]))
			err = -EIO;
	}

	return err;
}

int
pagebuf_prepare_write(
	pb_target_t		*target,
	struct page 		*page,
	unsigned		from,
	unsigned		to,
	pagebuf_bmap_fn_t	bmap)
{
	struct inode		*inode = page->mapping->host;
	int			err, at_eof;

	at_eof = page->index >= (inode->i_size >> PAGE_CACHE_SHIFT);
	err = __pb_block_prepare_write_async(target, inode, page, from, to,
					at_eof, bmap, NULL, 0, PBF_WRITE);
	if (err) {
		ClearPageUptodate(page);
		kunmap(page);
	}

	return err;
}


STATIC void
__pb_block_commit_write_async(
	pb_target_t		*target,
	struct inode 		*inode,
	struct page 		*page,
	int			partial)
{
	struct buffer_head	*bh, *head;
	int			nr = 0;

	/*
	 * Prepare write took care of reading/zero-out
	 * parts of page not covered by from/to. Page is now fully valid.
	 */
	SetPageUptodate(page);
	if (!page->buffers)
		create_empty_buffers(page, target->pbr_device,
					   target->pbr_blocksize);
	bh = head = page->buffers;
	do {
		if (buffer_mapped(bh)) {
			int need_balance_dirty = 0;

#ifdef PAGEBUF_DEBUG
			/* negative blocknr is always bad; */
			if (bh->b_blocknr < 0) {
				printk(
			"Warning: buffer 0x%p with weird blockno (%ld)\n",
					bh, bh->b_blocknr);
			}
#endif /* PAGEBUF_DEBUG */
			set_bit(BH_Uptodate, &bh->b_state);
			if (!buffer_dirty(bh))
				need_balance_dirty++;
			__mark_buffer_dirty(bh);

			if (need_balance_dirty) {
				buffer_insert_inode_data_queue(bh, inode);
				if (!partial)
					nr++;
			}
		}
		else {
			nr++;
			bh->b_state = (1 << BH_Delay) | (1 << BH_Mapped);
			__mark_buffer_dirty(bh);
			buffer_insert_inode_data_queue(bh, inode);
		}
	} while ((bh = bh->b_this_page) != head);

	if (nr)
		balance_dirty();
}

STATIC int
__pagebuf_do_delwri(
	pb_target_t		*target,
	struct file		*filp,		/* file to write */
	loff_t 			rounded_offset,	/* page-aligned file offset */
	unsigned long 		size,		/* size to write */
	char			*buf,
	size_t			len,
	loff_t			*lp,
	page_buf_bmap_t		*mp,		/* bmap for page */
	int			nmaps)
{
	struct inode		*inode = filp->f_dentry->d_inode;
	struct page		*page;
	unsigned long		done;
	int			err = 0, written = 0;
	loff_t			foff = *lp;
	char			*kaddr;

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

		err = __pb_block_prepare_write_async(target, inode, page,
			offset, offset + bytes,
			at_eof, NULL, mp, nmaps, PBF_WRITE);
		if (err)
			goto unlock;
		kaddr = page_address(page);

		err = __copy_from_user(kaddr + offset, buf, bytes);
		if (err)
			goto unlock;

		pagebuf_commit_write(target, page, offset, offset + bytes);

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
	struct pb_target	*target,
	struct file		*filp,	/* file to write                */
	char			*buf,	/* buffer address               */
	size_t			len,	/* size of buffer               */
	loff_t			*lp,	/* file offset to use and update */
	pagebuf_bmap_fn_t	bmap,	/* bmap function */
	int			pb_flags)	/* flags to bmap calls	*/
{
	struct inode		*inode = filp->f_dentry->d_inode;
	page_buf_bmap_t		map[PBF_MAX_MAPS];
	int			maps_returned, i;
	unsigned long		map_size, size;
	int			status = 0, written = 0;
	loff_t			foff = *lp;
	int			sync = filp->f_flags & (O_SYNC|O_DIRECT);

	/*
	 * While we have data to do, get a bunch of mappings for this
	 * file to blocks on disk (or in delalloc or holes or ...).
	 * For each map entry,
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
		 * (or delalloc/holes).
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
				map, PBF_MAX_MAPS, &maps_returned, pb_flags);
		if (status) {
			break;
		}

		/*
		 * Get the size of this mapping from the offset we
		 * care about to the end of the mapping. Then,
		 * reduce the size to lesser of the user's or the
		 * piece in the map.
		 *
		 * Holes mean we came to the end of the space returned
		 * from the file system. We need to go back and ask for
		 * more space.
		 */

		map_size = 0;
		for (i = 0; i < maps_returned; i++) {
			if (map[i].pbm_flags & PBMF_HOLE) {
				printk("HOLE ro 0x%Lx size 0x%lx mp 0x%p\n",
					rounded_offset, size, map);
				break;
			}
			map_size += map[i].pbm_bsize - map[i].pbm_delta;
		}
		if (i < maps_returned)
			break;
		size = min((unsigned long)map_size, size);

		/*
		 * Handle delwri or direct I/O
		 */
		if ((filp->f_flags & O_DIRECT) == 0) {
			status = __pagebuf_do_delwri(target, filp,
					rounded_offset, size, buf,
					len, &foff, map, maps_returned);
		} else {
			int	io_size = (int)min(size, (unsigned long) len);

			assert(maps_returned == 1);
			status = _pb_direct_io(target, inode, rounded_offset,
					io_size, map, buf, NULL, 1);
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
	pb_target_t		*target,
	struct file		*filp,	/* file to write                */
	char			*buf,	/* buffer address               */
	size_t			len,	/* size of buffer               */
	loff_t			*lp,	/* file offset to use and update */
	pagebuf_bmap_fn_t	bmap)	/* bmap function */
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
			status = _pagebuf_file_write(target, filp,
					buf, len, &foff, bmap, pb_flags);
			if (status > 0)
				written += status;
			break;
		}

		if (!PageLocked(page)) {
			PAGE_BUG(page);
		}

		at_eof = foff == inode->i_size;

		status = __pb_block_prepare_write_async(target, inode, page,
			offset, offset + bytes, at_eof, bmap,
			NULL, 0, pb_flags);
			
		if (status)
			goto unlock;

		kaddr = page_address(page);
		status = __copy_from_user(kaddr+offset, buf, bytes);

		if (status)
			goto unlock;

		pagebuf_commit_write(target, page, offset, offset + bytes);

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
	if (!page->mapping || !PageDelalloc(page)) {
		UnlockPage(page);
		page_cache_release(page);
		return NULL;
	}
	return page;
}

/* Actually push the data out to disk */
static void
submit_page_io(struct page *page)
{
	struct buffer_head	*bh, *head, *arr[MAX_BUF_PER_PAGE];
	int			i, nr = 0;

	bh = head = page->buffers;
	do {
		if (buffer_uptodate(bh))
			continue;
		assert(buffer_mapped(bh));
		assert(!buffer_delay(bh));
		arr[nr++] = bh;
	} while ((bh = bh->b_this_page) != head);

	if (!nr) {
		SetPageUptodate(page);
		UnlockPage(page);
		return;
	}

	for (i = 0; i < nr; i++) {
		bh = arr[i];
		lock_buffer(bh);
		set_buffer_async_io(bh);
		set_bit(BH_Uptodate, &bh->b_state);
		clear_bit(BH_Dirty, &bh->b_state);
	}

	for (i = 0; i < nr; i++)
		submit_bh(WRITE, arr[i]);

	SetPageUptodate(page);
}


STATIC int
map_page(
	struct pb_target	*target,
	struct inode		*inode,
	struct page		*page,
	page_buf_bmap_t		*maps,	/* mappings, may start as NULL */
	int			*nmaps,	/* number of mappings [in/out] */
	pagebuf_bmap_fn_t	bmap,	/* bmap function */
	int			flags)	/* allocation mode to use */
{
	struct buffer_head 	*bh, *head;
	page_buf_bmap_t		*mp = maps, *tmp;
	unsigned long		offset;
	int			index = 0, i = 0, nr;
	int			bbits = target->pbr_blocksize_bits;

	if (!page->buffers)
		create_empty_buffers(page, target->pbr_device,
					   target->pbr_blocksize);
	bh = head = page->buffers;
	do {
		offset = i << bbits;
		if (!mp) {
remap:
			index = 0;
			mp = maps;
			assert(bmap);
			nr = bmap(inode,
				((loff_t)page->index << PAGE_CACHE_SHIFT) +
					offset,
				PAGE_CACHE_SIZE - offset,
				maps, PBF_MAX_MAPS, nmaps, flags);
			if (nr)
				return nr;
#if 1	/* PAGEBUF_DEBUG? */
			for (nr = 0; nr < *nmaps; nr++) {
				if ((mp[nr].pbm_flags & PBMF_HOLE)) {
					printk(
				"map_page_buffers: writing delalloc page 0x%p "
				"with no extent index 0x%lx, map[%d]=0x%p\n",
						page, page->index, nr, &mp[nr]);
					BUG();
				}
			}
#endif
		}
		tmp = __pb_match_offset_to_mapping(page, mp, *nmaps,
							offset, &index);
		if (!tmp) {
			/*
			 * No mapping for this offset.  2 cases:
			 * - last mapping was at eof (zero to end)
			 * - we need more mappings (try, try again)
			 */
			if (!(mp[*nmaps - 1].pbm_flags & PBMF_EOF)) {
				goto remap;
			}
			zero_buffer_at_offset(target, page, bh, offset);
			continue;
		}
		assert(!(tmp->pbm_flags & PBMF_HOLE));
		assert(!(tmp->pbm_flags & PBMF_DELAY));
		__pb_map_buffer_at_offset(target, page, bh, offset, bbits, tmp);
	} while (i++, (bh = bh->b_this_page) != head);

	return 0;
}

/*
 * Allocate & map buffers for page given the extent map. Write it out.
 * except for the original page of a writepage, this is called on
 * delalloc pages only, for the original page it is possible that
 * the page has no mapping at all.
 */
STATIC void
convert_page(
	pb_target_t		*target,
	struct inode		*inode,
	struct page		*page,
	page_buf_bmap_t		*maps,
	int			*nmaps,
	unsigned long		flags,	/* allocation mode to use */
	pagebuf_bmap_fn_t	bmap,	/* bmap function */
	int			async_write)
{
	map_page(target, inode, page, maps, nmaps, bmap, flags);

	if (async_write) {
		submit_page_io(page);
	}

	page_cache_release(page);
}

/*
 * Convert & write out a cluster of pages in the same extent as defined
 * by mp and surrounding the start page.
 */
STATIC int
cluster_write(
	pb_target_t		*target,
	struct inode		*inode,
	struct page		*startpage,
	page_buf_bmap_t		*mp,
	int			nmaps,
	unsigned long		flags,	/* allocation mode to use */
	pagebuf_bmap_fn_t	bmap,	/* bmap function */
	int			async_write)
{
	unsigned long		tindex, tlast;
	struct page		*page;
	loff_t			sz = 0;
	int			i;

	if (startpage->index != 0) {
		tlast = mp[0].pbm_offset >> PAGE_CACHE_SHIFT;
		/*
		 * NB: This mapping is guaranteed to cover full pages
		 * by the above calculation and because its actually a
		 * mapping for "startpage" not "page".  Hence bmap can
		 * be passed in as NULL below, ensuring this is true.
		 */
		for (tindex = startpage->index-1; tindex >= tlast; tindex--) {
			if (!(page = probe_page(inode, tindex)))
				break;
			convert_page(target, inode, page,
						mp, &nmaps, flags, NULL, 1);
		}
	}

	convert_page(target, inode, startpage,
				mp, &nmaps, flags, bmap, async_write);

	for (i = 0; i < nmaps; i++)
		sz += mp[i].pbm_bsize;
	tlast = PAGE_CACHE_ALIGN_LL(mp[0].pbm_offset + sz);
	tlast >>= PAGE_CACHE_SHIFT;
	for (tindex = startpage->index + 1; tindex < tlast; tindex++) {
		if (!(page = probe_page(inode, tindex)))
			break;
		convert_page(target, inode, page,
					mp, &nmaps, flags, NULL, 1);
	}
	return 0;
}

STATIC int
pagebuf_delalloc_convert(
	pb_target_t		*target,
	struct inode		*inode,
	struct page		*page,	/* delalloc page to convert - locked */
	unsigned long		flags,	/* allocation mode to use */
	pagebuf_bmap_fn_t	bmap,	/* bmap function */
	int			async_write)
{
	struct buffer_head 	*bh, *head;
	page_buf_bmap_t		maps[PBF_MAX_MAPS];
	int			nmaps, nr = 0;

	/* Fast path for completely mapped page */
	if ((bh = head = page->buffers) != NULL) {
		do {
			if (!buffer_delay(bh) && buffer_mapped(bh))
				continue;
			nr++;
		} while ((bh = bh->b_this_page) != head && !nr);

		if (!nr) {
			if (async_write)
				submit_page_io(page);
			return 0;
		}
	}

	/*
	 * Get an initial mapping - the extent(s) returned are
	 * only guaranteed to cover the first block, but may
	 * also cover multiple pages surrounding this page (we
	 * want to write 'em all if they're delalloc) and also
	 * may end in the middle of the page (for block sizes
	 * less than the page size).  In that situation, we'll
	 * need to go back and get subsequent mappings later.
	 */
	nr = bmap(inode,
		((loff_t)page->index << PAGE_CACHE_SHIFT),
		PAGE_CACHE_SIZE, maps, PBF_MAX_MAPS, &nmaps, flags);
	if (nr)
		return nr;

	/*
	 * page needs to be setup as though find_page(...) returned it,
	 * which is a locked page with an extra reference.
	 */
	page_cache_get(page);
	return cluster_write(target, inode, page,
				maps, nmaps, flags, bmap, async_write);
}
