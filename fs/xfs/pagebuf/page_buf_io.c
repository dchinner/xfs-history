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
#define PBF_MAX_MAPS		1 /* TODO: XFS_BMAP_MAX_NMAP? */

/*
 * Forward declarations.
 */
STATIC int  pagebuf_delalloc_convert(
		struct inode *, struct page *, pagebuf_bmap_fn_t, int, int);

/*
 *	pagebuf_flush: write back cached pages to disk.
 */
void
pagebuf_flush(
	struct inode		*ip,	/* inode for range              */
	loff_t			ioff,	/* first location in range      */
	page_buf_flags_t	bflags)	/* unused                       */
{
	filemap_fdatasync(ip->i_mapping);
	fsync_inode_data_buffers(ip);
	filemap_fdatawait(ip->i_mapping);
}

/*
 *	pagebuf_inval: invalidate from page-cache (no implied write back).
 */
void
pagebuf_inval(
	struct inode		*ip,	/* inode for range              */
	loff_t			ioff,	/* first location in range      */
	page_buf_flags_t	bflags)	/* unused                       */
{
	truncate_inode_pages(ip->i_mapping, ioff);
}

/*
 *	pagebuf_flushinval: write & invalidate buffered storage
 */
void
pagebuf_flushinval(
	struct inode		*ip,	/* inode for range             	*/
	loff_t			ioff,	/* first location in range     	*/
	page_buf_flags_t	bflags)	/* unused                       */
{
	pagebuf_flush(ip, ioff, bflags);
	truncate_inode_pages(ip->i_mapping, ioff);
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
	struct inode		*ip,	/* inode owning buffer		*/
	page_buf_t		*pb,	/* buffer to zero               */
	off_t			boff,	/* offset in buffer             */
	size_t			bsize,	/* size of data to zero         */
	loff_t			end_size)	/* max file size to set	*/
{
	loff_t			cboff, pos;
	size_t			cpoff, csize;
	struct page		*page;
	struct address_space	*mapping;
	char			*kaddr;

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

		mapping = page->mapping;

		kaddr = kmap(page);
		mapping->a_ops->prepare_write(NULL, page, cpoff, cpoff+csize);

		memset((void *) (kaddr + cpoff), 0, csize);
		flush_dcache_page(page);
		mapping->a_ops->commit_write(NULL, page, cpoff, cpoff+csize);
		pos = ((loff_t)page->index << PAGE_CACHE_SHIFT) +
			cpoff + csize;
		if (pos > ip->i_size)
			ip->i_size = pos < end_size ? pos : end_size;

		kunmap(page);
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
	int			rdwr)
{
	page_buf_t	*pb;
	struct kiobuf	*kp;
	int		rval;
	int		pb_flags;
	off_t		offset;
	size_t		max_io = pb_params.p_un.max_dio << PAGE_SHIFT;

	pb_size = min(pb_size, max_io);

	pb_flags = (rdwr ? PBF_WRITE : PBF_READ) | PBF_FORCEIO | _PBF_LOCKABLE;

	pb = pagebuf_lookup(target, inode, rounded_offset, pb_size, pb_flags);
	if (!pb) {
		return 0;
	}

	offset = mp->pbm_delta >> 9;
	pb->pb_dev = mp->pbm_dev;
	pb->pb_bn = mp->pbm_bn + offset;

	/* Do our own allocation to avoid the buffer_head overhead */
	kp = kmalloc(sizeof(struct kiobuf), SLAB_KERNEL);
	if (!kp) {
		pb->pb_flags &= ~_PBF_LOCKABLE;
		pagebuf_rele(pb);
		return 0;
	}
	memset(kp, 0, sizeof(struct kiobuf));
	kp->array_len = KIO_STATIC_PAGES;
	kp->maplist = kp->map_array;
	rval = map_user_kiobuf(rdwr ? WRITE : READ,
			kp, (unsigned long) user_addr, pb_size);
	if (rval == 0) {
		pb->pb_pages = kp->maplist;
		pb->pb_page_count = kp->nr_pages;
		pb->pb_offset = kp->offset;
		rval = pagebuf_iostart(pb, pb_flags);
		unmap_kiobuf(kp);
	}
	if (kp->array_len > KIO_STATIC_PAGES) {
		kfree(kp->maplist);
	}
	kfree(kp);

	pb->pb_flags &= ~_PBF_LOCKABLE;
	pagebuf_rele(pb);

	return rval ? 0 : pb_size;
}

/*
 * Return size in bytes of maximum direct I/O done in one call
 */
size_t
pagebuf_max_direct(void)
{
	return pb_params.p_un.max_dio << PAGE_SHIFT;
}

STATIC int
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
		 *
		 * Once we know the on disk and/or in memory representation,
		 * we can better do the I/Os in chunks or ...
		 */

		rounded_offset = foff;
		rounded_size = len + (foff - rounded_offset);
		size = rounded_size;

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

		{
			int	io_size = (int)min(size, (unsigned long) len);

			status = _pb_direct_io(target, inode, rounded_offset,
					io_size, map, buf, 1);
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
	struct inode		*inode = filp->f_dentry->d_inode;
	unsigned long		index;
	int			status = 0, written = 0, page_fault;
	loff_t			foff;
	struct page		*page;
	int			pb_flags;
	int			direct = filp->f_flags & O_DIRECT;

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
		unsigned long		bytes, offset;
		struct address_space	*mapping;
		char			*kaddr;

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

		if (direct) {
			status = _pagebuf_file_write(target, filp,
					buf, len, &foff, bmap, pb_flags);
			if (status > 0)
				written += status;
			break;
		} else {
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

			page = grab_cache_page(&inode->i_data, index);
			if (!page) {
				status = -ENOMEM;
				break;
			}
		}

		if (!PageLocked(page)) {
			PAGE_BUG(page);
		}

		mapping = page->mapping;
		kaddr = kmap(page);
		status = mapping->a_ops->prepare_write(NULL, page,
			offset, offset + bytes);
			
		if (status) {
			ClearPageUptodate(page);
			goto unlock;
		}

		page_fault = __copy_from_user(kaddr + offset, buf, bytes);
		flush_dcache_page(page);

		status = mapping->a_ops->commit_write(NULL, page, offset, offset + bytes);

		if (page_fault) {
			status = -EFAULT;
			goto unlock;
		}

		if (status >= 0) {
			len -= bytes;
			buf += bytes;
			foff += bytes;
			written += bytes;
		}

unlock:
		kunmap(page);
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

/** 
 * The remainder of the file is concerned with converting delayed allocate
 * and mmap space in a file to real space.
 **/

/*
 * __pb_match_offset_to_mapping
 * Finds the corresponding mapping in block @map array of the
 * given @offset within a @page.
 */
STATIC page_buf_bmap_t *
__pb_match_offset_to_mapping(
	struct page		*page,
	page_buf_bmap_t		*map,
	unsigned long		offset)
{
	loff_t			full_offset;	/* offset from start of file */

	assert(offset < PAGE_CACHE_SIZE);

	full_offset = page->index;		/* NB: using 64bit number */
	full_offset <<= PAGE_CACHE_SHIFT;	/* offset from file start */
	full_offset += offset;			/* offset from page start */

	if (full_offset < map->pbm_offset)
		return NULL;
	if (map->pbm_offset + map->pbm_bsize > full_offset)
		return map;
	return NULL;
}

STATIC void
__pb_map_buffer_at_offset(
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
	delta += offset;
	delta -= mp->pbm_offset;
	delta >>= block_bits;

	sector_shift = block_bits - 9;
	bn = mp->pbm_bn >> sector_shift;
	bn += delta;
	assert((bn << sector_shift) >= mp->pbm_bn);

	lock_buffer(bh);
	bh->b_blocknr = bn;
	bh->b_dev = mp->pbm_dev;
	set_bit(BH_Mapped, &bh->b_state);
	clear_bit(BH_Delay, &bh->b_state);
}

/*
 * Convert delalloc space to real space, do not flush the
 * data out to disk, that will be done by the caller.
 */
void
pagebuf_release_page(
	struct page		*page,
	pagebuf_bmap_fn_t	bmap)
{
	struct inode		*inode = (struct inode*)page->mapping->host;

	pagebuf_delalloc_convert(inode, page, bmap, 0, 0);
}

/*
 * Convert delalloc or unmapped space to real space and flush out
 * to disk. Called in two circumstances, with and without PageDelalloc
 * set. Having PageDelalloc means we are actually being called to 
 * flush a delalloc buffer, we need to convert this buffer and the
 * surrounding space, but not allocate space for anything which is
 * not already delalloc. In the other case we need to allocate space
 * for all the page before EOF.
 */
int
pagebuf_write_full_page(
	struct page		*page,
	int			delalloc,
	pagebuf_bmap_fn_t	bmap)
{
	struct inode		*inode = (struct inode*)page->mapping->host;
	unsigned long		end_index = inode->i_size >> PAGE_CACHE_SHIFT;
	int			ret;

	/* Are we off the end of the file ? */
	if (page->index >= end_index) {
		unsigned offset = inode->i_size & (PAGE_CACHE_SIZE-1);
		if ((page->index >= end_index+1) || !offset) {
			ret =  -EIO;
			goto out;
		}
	}

	if (!page_has_buffers(page)) {
		create_empty_buffers(page, inode->i_dev,
					   1 << inode->i_blkbits);
	}

	ret = pagebuf_delalloc_convert(inode, page, bmap,
					1, !PageDelalloc(page));

out:
	if (ret < 0) {
		/*
		 * If it's delalloc and we have nowhere to put it,
		 * throw it away.
		 */
		if (delalloc)
			block_flushpage(page, 0);
		ClearPageUptodate(page);
		UnlockPage(page);
	}

	return ret;
}

/*
 * Look for a page at index which is unlocked and not mapped
 * yet - clustering for mmap write case.
 */
STATIC unsigned int
probe_unmapped_page(
	struct address_space	*mapping,
	unsigned long		index,
	unsigned int		pg_offset)
{ 
	struct page		*page;
	int			ret = 0;

	page = find_get_page(mapping, index);
	if (!page)
		return 0;
	if (TryLockPage(page)) {
		page_cache_release(page);
		return 0;
	}
	if (page->mapping) {
		if (!page_has_buffers(page)) {
			ret = PAGE_CACHE_SIZE;
		} else {
			struct buffer_head	*bh, *head;
			bh = head = page_buffers(page);
			do {
				if (buffer_mapped(bh)) {
					break;
				}
				ret += bh->b_size;
				if (ret >= pg_offset)
					break;
			} while ((bh = bh->b_this_page) != head);
		}
	}

	UnlockPage(page);
	page_cache_release(page);
	return ret;
}

STATIC unsigned int
probe_unmapped_cluster(
	struct inode		*inode,
	struct page		*startpage,
	struct buffer_head	*bh,
	struct buffer_head	*head)
{
	unsigned long		tindex, tlast;
	unsigned int		len, total = 0;
	struct address_space	*mapping = inode->i_mapping;

	/* First sum forwards in this page */
	do {
		if (buffer_mapped(bh))
			break;
		total += bh->b_size;
	} while ((bh = bh->b_this_page) != head);

	/* if we reached the end of the page, sum forwards in
	 * following pages.
	 */
	if (bh == head) {
		tlast = inode->i_size >> PAGE_CACHE_SHIFT;
		for (tindex = startpage->index + 1; tindex < tlast; tindex++) {
			len = probe_unmapped_page(mapping, tindex,
							PAGE_CACHE_SIZE);
			if (!len)
				break;
			total += len;
		}
		if ((tindex == tlast) && (inode->i_size & ~PAGE_CACHE_MASK)) {
			len = probe_unmapped_page(mapping, tindex,
					inode->i_size & ~PAGE_CACHE_MASK);
			total += len;
		}
	}
	return total;
}

/*
 * Probe for a given page (index) in the inode & test if it is delayed.
 * Returns page locked and with an extra reference count.
 */
STATIC struct page *
probe_page(
	struct inode		*inode,
	unsigned long		index)
{
	struct page		*page;

	page = find_get_page(inode->i_mapping, index);
	if (!page)
		return NULL;
	if (TryLockPage(page)) {
		page_cache_release(page);
		return NULL;
	}
	if (page->mapping && page_has_buffers(page)) {
		struct buffer_head	*bh, *head;
		bh = head = page_buffers(page);
		do {
			if (buffer_delay(bh))
				return page;
		} while ((bh = bh->b_this_page) != head);
	}
	UnlockPage(page);
	page_cache_release(page);
	return NULL;
}

STATIC void
write_bh_array(
	struct buffer_head	*bh_arr[],
	int			cnt)
{
	int			i;
	struct buffer_head	*bh;

	for (i = 0; i < cnt; i++) {
		bh = bh_arr[i];
		set_buffer_async_io(bh);
		set_bit(BH_Uptodate, &bh->b_state);
		clear_bit(BH_Dirty, &bh->b_state);
	}
	for (i = 0; i < cnt; i++)
		submit_bh(WRITE, bh_arr[i]);
}

STATIC int
map_page(
	struct inode		*inode,
	struct page		*page,
	page_buf_bmap_t		*maps,
	struct buffer_head	*bh_arr[],
	int			startio,
	int			all_bh)
{
	struct buffer_head 	*bh, *head;
	page_buf_bmap_t		*mp = maps, *tmp;
	unsigned long		end, offset, end_index;
	int			i = 0, index = 0;
	int			bbits = inode->i_blkbits;

	end_index = inode->i_size >> PAGE_CACHE_SHIFT;
	if (page->index < end_index) {
		end = PAGE_CACHE_SIZE;
	} else {
		end = inode->i_size & (PAGE_CACHE_SIZE-1);
	}
	bh = head = page_buffers(page);
	do {
		offset = i << bbits;
		if (buffer_mapped(bh) && !buffer_delay(bh) && all_bh) {
			if (startio && (offset < end)) {
				lock_buffer(bh);
				bh_arr[index++] = bh;
			}
			continue;
		}
		tmp = __pb_match_offset_to_mapping(page, mp, offset);
		if (!tmp)
			continue;
		assert(!(tmp->pbm_flags & PBMF_HOLE));
		assert(!(tmp->pbm_flags & PBMF_DELAY));
		__pb_map_buffer_at_offset(page, bh, offset, bbits, tmp);
		if (startio && (offset < end)) {
			bh_arr[index++] = bh;
		} else {
			unlock_buffer(bh);
		}
	} while (i++, (bh = bh->b_this_page) != head);

	return index;
}

/*
 * Allocate & map buffers for page given the extent map. Write it out.
 * except for the original page of a writepage, this is called on
 * delalloc pages only, for the original page it is possible that
 * the page has no mapping at all.
 */
STATIC void
convert_page(
	struct inode		*inode,
	struct page		*page,
	page_buf_bmap_t		*maps,
	int			startio,
	int			all_bh)
{
	struct buffer_head	*bh_arr[MAX_BUF_PER_PAGE];
	int			cnt;

	cnt = map_page(inode, page, maps, bh_arr, startio, all_bh);
	if (cnt) {
		write_bh_array(bh_arr, cnt);
	} else {
		UnlockPage(page);
	}

	page_cache_release(page);
}

/*
 * Convert & write out a cluster of pages in the same extent as defined
 * by mp and following the start page.
 */
STATIC void
cluster_write(
	struct inode		*inode,
	unsigned long		tindex,
	page_buf_bmap_t		*mp,
	int			startio,
	int			all_bh)
{
	unsigned long		tlast;
	struct page		*page;

	tlast = (mp->pbm_offset + mp->pbm_bsize) >> PAGE_CACHE_SHIFT;
	for (; tindex < tlast; tindex++) {
		if (!(page = probe_page(inode, tindex)))
			break;
		convert_page(inode, page, mp, startio, all_bh);
	}
}

/*
 * Calling this with Delalloc set means we are being asked to flush
 * a dirty buffer head. When called with async_write set then we are
 * coming from writepage. A writepage call without Delalloc set means
 * we are being asked to write out all of the page which is before EOF
 * and therefore need to allocate space for unmapped portions of the
 * page.
 */
STATIC int
pagebuf_delalloc_convert(
	struct inode		*inode,		/* inode containing page */
	struct page		*page,		/* page to convert - locked */
	pagebuf_bmap_fn_t	bmap,		/* bmap function */
	int			startio,	/* start io on the page */
	int			allocate_space)
{
	struct buffer_head 	*bh, *head;
	struct buffer_head	*bh_arr[MAX_BUF_PER_PAGE];
	page_buf_bmap_t		*mp, maps[PBF_MAX_MAPS];
	int			i, cnt = 0;
	int			len, err, nmaps;
	unsigned long		p_offset = 0;
	loff_t			offset;
	loff_t			end_offset;

	offset = (loff_t)page->index << PAGE_CACHE_SHIFT;
	end_offset = offset + PAGE_CACHE_SIZE;
	if (end_offset > inode->i_size)
		end_offset = inode->i_size;

	bh = head = page_buffers(page);
	mp = NULL;

	len = bh->b_size;
	do {
		if (mp) {
			mp = __pb_match_offset_to_mapping(page, maps, p_offset);
		}
		if (buffer_delay(bh)) {
			if (!mp) {
				err = bmap(inode, offset, len, maps,
						PBF_MAX_MAPS, &nmaps,
						PBF_WRITE|PBF_FILE_ALLOCATE);
				if (err)
					goto error;
				mp = __pb_match_offset_to_mapping(page, maps,
								p_offset);
			}
			if (mp) {
				__pb_map_buffer_at_offset(page, bh, p_offset,
					inode->i_blkbits, mp);
				if (startio) {
					bh_arr[cnt++] = bh;
				} else {
					unlock_buffer(bh);
				}
			}
		} else if (!buffer_mapped(bh) && allocate_space) {
			int	size;

			/* Getting here implies an unmapped buffer was found,
			 * and we are in a path where we need to write the
			 * whole page out.
			 */
			if (!mp) {
				size = probe_unmapped_cluster(inode, page,
								bh, head);
				err = bmap(inode, offset, size, maps,
						PBF_MAX_MAPS, &nmaps,
						PBF_WRITE|PBF_DIRECT);
				if (err)
					goto error;
				mp = __pb_match_offset_to_mapping(page, maps,
								p_offset);
			}
			if (mp) {
				__pb_map_buffer_at_offset(page, bh, p_offset,
					inode->i_blkbits, mp);
				if (startio) {
					bh_arr[cnt++] = bh;
				} else {
					unlock_buffer(bh);
				}
			}
		} else if (startio && buffer_mapped(bh)) {
			if (buffer_dirty(bh) || allocate_space) {
				lock_buffer(bh);
				bh_arr[cnt++] = bh;
			}
		}

		offset += len;
		p_offset += len;
		bh = bh->b_this_page;
	} while (offset < end_offset);

	if (cnt) {
		write_bh_array(bh_arr, cnt);
	} else if (startio) {
		UnlockPage(page);
	}
	if (mp)
		cluster_write(inode, page->index + 1, mp,
				startio, allocate_space);

	return 0;

error:
	for (i = 0; i < cnt; i++) {
		unlock_buffer(bh_arr[i]);
	}

	return err;
}
