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
 *	page_buf_io.c
 *
 *	See generic comments about page_bufs in page_buf.c. This file deals with
 *	file I/O (reads & writes) including delayed allocation & direct IO.
 *
 *	Written by Steve Lord, Jim Mostek, Russell Cattelan
 *		    and Rajagopal Ananthanarayanan ("ananth") at SGI.
 *
 */

#include <linux/stddef.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/pagemap.h>
#include <linux/locks.h>

#include "page_buf.h"

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,5,9)
#define page_buffers(page)	((page)->buffers)
#define page_has_buffers(page)	((page)->buffers)
#endif

#undef assert
#ifdef PAGEBUF_DEBUG
# define assert(expr) \
	if (!(expr)) {						\
		printk("Assertion failed: %s\n%s::%s line %d\n",\
		#expr,__FILE__,__FUNCTION__,__LINE__);		\
		BUG();						\
	}
#else
# define assert(x)	do { } while (0)
#endif



/*
 * External declarations.
 */
extern int linvfs_pb_bmap(struct inode *, loff_t, ssize_t, page_buf_bmap_t *, int);

/*
 * Forward declarations.
 */
static int  pagebuf_delalloc_convert(struct inode *, struct page *, int, int);

/*
 * __pb_match_offset_to_mapping
 * Finds the corresponding mapping in block @map array of the
 * given @offset within a @page.
 */
static page_buf_bmap_t *
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

static void
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
	bh->b_dev = mp->pbm_target->pbr_kdev;
	set_bit(BH_Mapped, &bh->b_state);
	clear_bit(BH_Delay, &bh->b_state);
}

/*
 * Convert delalloc space to real space, do not flush the
 * data out to disk, that will be done by the caller.
 */
int
pagebuf_release_page(
	struct page		*page)
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

	ret = pagebuf_delalloc_convert(inode, page, 0, 0);

out:
	if (ret < 0) {
		block_flushpage(page, 0);
		ClearPageUptodate(page);

		return 0;
	}

	return 1;
}

/*
 * Convert delalloc or unmapped space to real space and flush out
 * to disk.
 */
int
pagebuf_write_full_page(
	struct page		*page,
	int			delalloc)
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
		create_empty_buffers(page, inode->i_dev, 1 << inode->i_blkbits);
	}

	ret = pagebuf_delalloc_convert(inode, page, 1, delalloc == 0);

out:
	if (ret < 0) {
		/*
		 * If it's delalloc and we have nowhere to put it,
		 * throw it away.
		 */
		if (delalloc)
			block_flushpage(page, 0);
		ClearPageUptodate(page);
		unlock_page(page);
	}

	return ret;
}

/*
 * Look for a page at index which is unlocked and not mapped
 * yet - clustering for mmap write case.
 */
static unsigned int
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
	if (page->mapping && PageDirty(page)) {
		if (!page_has_buffers(page)) {
			ret = PAGE_CACHE_SIZE;
		} else {
			struct buffer_head	*bh, *head;
			bh = head = page_buffers(page);
			do {
				if (buffer_mapped(bh) || !buffer_uptodate(bh)) {
					break;
				}
				ret += bh->b_size;
				if (ret >= pg_offset)
					break;
			} while ((bh = bh->b_this_page) != head);
		}
	}

	unlock_page(page);
	page_cache_release(page);
	return ret;
}

static unsigned int
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
static struct page *
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
	unlock_page(page);
	page_cache_release(page);
	return NULL;
}

static void
submit_page(
	struct page		*page,
	struct buffer_head	*bh_arr[],
	int			cnt)
{
	if (cnt) {
		struct buffer_head	*bh;
		int			i;

		for (i = 0; i < cnt; i++) {
			bh = bh_arr[i];
			set_buffer_async_io(bh);
			set_bit(BH_Uptodate, &bh->b_state);
			clear_bit(BH_Dirty, &bh->b_state);
		}

		for (i = 0; i < cnt; i++)
			submit_bh(WRITE, bh_arr[i]);
	} else
		unlock_page(page);
}

static int
map_page(
	struct inode		*inode,
	struct page		*page,
	page_buf_bmap_t		*maps,
	struct buffer_head	*bh_arr[],
	int			startio,
	int			all_bh)
{
	struct buffer_head	*bh, *head;
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
		if (!buffer_uptodate(bh))
			continue;
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
static void
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
	submit_page(page, bh_arr, cnt);
	page_cache_release(page);
}

/*
 * Convert & write out a cluster of pages in the same extent as defined
 * by mp and following the start page.
 */
static void
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
 * Calling this without allocate_space set means we are being asked to
 * flush a dirty buffer head. When called with async_write set then we
 * are coming from writepage. A writepage call with allocate_space set
 * means we are being asked to write out all of the page which is before
 * EOF and therefore need to allocate space for unmapped portions of the
 * page.
 */
static int
pagebuf_delalloc_convert(
	struct inode		*inode,		/* inode containing page */
	struct page		*page,		/* page to convert - locked */
	int			startio,	/* start io on the page */
	int			allocate_space)
{
	struct buffer_head	*bh, *head;
	struct buffer_head	*bh_arr[MAX_BUF_PER_PAGE];
	page_buf_bmap_t		*mp, map;
	int			i, cnt = 0;
	int			len, err;
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
		if (!buffer_uptodate(bh) && !startio) {
			goto next_bh;
		}

		if (mp) {
			mp = __pb_match_offset_to_mapping(page, &map, p_offset);
		}

		if (buffer_delay(bh)) {
			if (!mp) {
				err = linvfs_pb_bmap(inode, offset, len, &map,
						PBF_WRITE|PBF_FILE_ALLOCATE);
				if (err)
					goto error;
				mp = __pb_match_offset_to_mapping(page, &map,
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
				err = linvfs_pb_bmap(inode, offset, size, &map,
						PBF_WRITE|PBF_DIRECT);
				if (err)
					goto error;
				mp = __pb_match_offset_to_mapping(page, &map,
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

next_bh:
		offset += len;
		p_offset += len;
		bh = bh->b_this_page;
	} while (offset < end_offset);

	if (startio)
		submit_page(page, bh_arr, cnt);

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
