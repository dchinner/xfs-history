
/*
 * This file contains the implementation of the xfs_buf_log_item.
 * It contains the item operations used to manipulate the buf log
 * items as well as utility routines used by the buffer specific
 * transaction routines.
 */


#include <sys/param.h>
#define _KERNEL
#include <sys/buf.h>
#undef _KERNEL
#include <sys/vnode.h>
#include <sys/debug.h>
#include <sys/uuid.h>
#include "xfs_types.h"
#include "xfs_inum.h"
#include "xfs.h"
#include "xfs_trans.h"
#include "xfs_buf_item.h"
#include "xfs_bio.h"
#include "xfs_sb.h"
#include "xfs_mount.h"
#include "xfs_trans_priv.h"
#ifdef SIM
#include <bstring.h>
#include "sim.h"
#else
#include <sys/systm.h>
#endif

#define	ROUNDUP32(x)		(((x) + 31) & ~31)
#define	ROUNDUPNBWORD(x)	(((x) + (NBWORD - 1)) & ~NBWORD)


STATIC int	xfs_buf_item_bits(uint *, int, int);
STATIC void	xfs_buf_item_set_bit(uint *, int, int);
STATIC int	xfs_buf_item_next_bit(uint *, int, int);

/*
 * This returns the amount of space needed to log the given buf
 * log item.
 *
 * It calculates this by adding the amount of space need to log
 * the dirty XFS_BLI_CHUNK byte chunks of the buffer to the amount of space
 * needed for an xfs_buf_log_format structure.
 */
uint
xfs_buf_item_size(xfs_buf_log_item_t *bip)
{
	uint	base_size;
	uint	dirty_chunks;
	uint	align_size;
	uint	total_size;

	/*
	 * The size of the base structure is the size of the
	 * declared structure plus the space for the extra words
	 * of the bitmap.  We subtract one from the map size, because
	 * the first element of the bitmap is accounted for in the
	 * size of the base structure.
	 */
	base_size = sizeof(xfs_buf_log_format_t) +
		    ((bip->bli_map_size - 1) * sizeof(uint));

	/*
	 * Count the number of bits in the dirty chunk map.
	 * Each bit corresponds to XFS_BLI_CHUNK bytes of data which
	 * need to be logged.
	 */
	dirty_chunks = xfs_buf_item_bits((uint *)&bip->bli_dirty_map,
					 (int)bip->bli_map_size, 0);

	/*
	 * Calculate the number of bytes needed to align the chunks on
	 * a 32 byte boundary.  This is the base size of the structure
	 * rounded up to a 32 byte boundary minus the size of the structure.
	 */
	align_size = ROUNDUP32(base_size) - base_size;

	/*
	 * The total size is simply the sum of all three of these pieces.
	 */
	total_size = base_size + (dirty_chunks * XFS_BLI_CHUNK) + align_size;
	return (total_size);
}

/*
 * This is called to write the image of the buf log item into the in
 * core log.  If the entire image does not fit, then it should write
 * part of it and return a key which will allow it to write the rest
 * next time.  The routine should return how much space is needed to
 * log the rest of the image if it does not fit in the given space.
 *
 * The contents of *keyp must be -1 the first time this is called.
 */
uint
xfs_buf_item_format(xfs_buf_log_item_t *bip, caddr_t buffer,
		    uint buffer_size, int *keyp)
{
	uint			base_size;
	uint			align_size;
	uint			total_size;
	uint			bits_left;
	uint			buffer_offset;
	int			set_bit;
	xfs_buf_log_format_t	*blfp;
	buf_t			*bp;
	caddr_t			chunkp;

	/*
	 * The size of the base structure is the size of the
	 * declared structure plus the space for the extra words
	 * of the bitmap.  We subtract one from the map size, because
	 * the first element of the bitmap is accounted for in the
	 * size of the base structure.
	 */
	base_size = sizeof(xfs_buf_log_format_t) +
		    ((bip->bli_map_size - 1) * sizeof(uint));

	/*
	 * Calculate the number of bytes needed to align the chunks on
	 * a 32 byte boundary.  This is the base size of the structure
	 * rounded up to a 32 byte boundary minus the size of the structure.
	 */
	align_size = ROUNDUP32(base_size) - base_size;

	bp = bip->bli_buf;

	/*
	 * Initialize the buf log format header.  Zero the bitmap so we
	 * can just set the bits as we add them and not worry about
	 * extraneous bits.
	 */
	blfp = (xfs_buf_log_format_t*)buffer;
	blfp->blf_type = XFS_LI_BUF;
	blfp->blf_size = buffer_size;
	blfp->blf_blkno = bp->b_blkno;
	blfp->blf_dev = bp->b_edev;
	blfp->blf_map_size = bip->bli_map_size;
	bzero(&blfp->blf_data_map, (int)(blfp->blf_map_size * sizeof(uint)));

	/*
	 * Make sure the buffer is mapped to virtual memory so we can
	 * copy it into the log.  Do I need to call bp_mapout() later?
	 */
#ifndef SIM
	if (!BP_ISMAPPED(bp)) {
		bp_mapin(bp);
	}
#endif

	/*
	 * Copy as many chunks as will fit into given buffer.
	 * The key should contain either -1 on the first call
	 * or the bit to start searching from minus 1 in the case
	 * that the first call could not get all the data into the
	 * log in one write.
	 */
	chunkp = buffer + (base_size + align_size);
	set_bit = *keyp;
	while ((chunkp + XFS_BLI_CHUNK) <= (buffer + buffer_size)) {
		/*
		 * This takes the bit number to start looking from and
		 * returns the next set bit from there.  It returns -1
		 * if there are no more bits set or the start bit is
		 * beyond the end of the bitmap.
		 */
		set_bit = xfs_buf_item_next_bit(bip->bli_dirty_map,
						(int)bip->bli_map_size,
						set_bit + 1);
		if (set_bit == -1) {
			break;
		}

		/*
		 * Set the same bit in the log format bit map and copy
		 * that chunk into the log.
		 */
		xfs_buf_item_set_bit(blfp->blf_data_map,
				     (int)blfp->blf_map_size,
				     set_bit);
		buffer_offset = set_bit * XFS_BLI_CHUNK;
		bcopy(bp->b_un.b_addr + buffer_offset, chunkp, XFS_BLI_CHUNK);
		chunkp += XFS_BLI_CHUNK;
	}
	ASSERT(chunkp <= (buffer + buffer_size));

	/*
	 * When set_bit is -1 we know we got all the dirty chunks in the
	 * buffer into the log, so return indicating there is nothing more
	 * to do.
	 */
	if (set_bit == -1) {
		return (0);
	}

	/*
	 * We ran out of space but we don't know if we got all the chunks.
	 * There may or may not be more chunks to record in the log.
	 * Count the number of bits left.  If there are none, then
	 * we're done.  If there are more, return the amount of space
	 * we still need and set the key to the value of set_bit. This
	 * will make the next call start looking from the bit after the
	 * last set bit we found and logged.
	 */
	bits_left = xfs_buf_item_bits(bip->bli_dirty_map,
				      (int)bip->bli_map_size,
				      set_bit + 1);
	if (bits_left == 0) {
		return (0);
	}
	*keyp = set_bit;
	total_size = base_size + align_size + (bits_left * XFS_BLI_CHUNK);
	return (total_size);
}

/*
 * This is called to pin the buffer associated with the buf log
 * item in memory so it cannot be written out.  Simply call bpin()
 * on the buffer to do this.
 */
void
xfs_buf_item_pin(xfs_buf_log_item_t *bip)
{
	buf_t	*bp;

	bp = bip->bli_buf;
	ASSERT(bp->b_flags & B_BUSY);
	bpin(bp);
}


/*
 * This is called to unpin the buffer associated with the buf log
 * item which was previously pinned with a call to xfs_buf_item_pin().
 * Just call bunpin() on the buffer to do this.
 */
void
xfs_buf_item_unpin(xfs_buf_log_item_t *bip)
{
	buf_t	*bp;

	bp = bip->bli_buf;
	ASSERT(bp != NULL);
	ASSERT((xfs_buf_log_item_t*)(bp->b_fsprivate) == bip);
	bunpin(bp);
}

/*
 * This is called to attempt to lock the buffer associated with this
 * buf log item.  Don't sleep on the buffer lock.  If we can't get
 * the lock right away, return 0.  If we can get the lock, pull the
 * buffer from the free list, mark it busy, and return 1.
 */
uint
xfs_buf_item_trylock(xfs_buf_log_item_t *bip)
{
	buf_t	*bp;

	bp = bip->bli_buf;
	if (!cpsema(&bp->b_lock)) {
		return (0);
	}

	/*
	 * Remove the buffer from the free list.
	 * This needs to be added and exported from the buffer cache.
	 */
	notavail(bp);

	return (1);
}

/*
 * Release the buffer associated with the buf log item.
 * If there is no dirty logged data associated with the
 * buffer recorded in the buf log item, then free the
 * buf log item and remove the reference to it in the
 * buffer.
 *
 * This call ignores the recursion count.  It is only called
 * when the buffer should REALLY be unlocked, regardless
 * of the recursion count.
 *
 * If the XFS_BLI_HOLD flag is set in the buf log item, then
 * free the log item if necessary but do not unlock the buffer.
 * This is for support of xfs_trans_bhold(). Make sure the
 * XFS_BLI_HOLD field is cleared if we don't free the item.
 */
void
xfs_buf_item_unlock(xfs_buf_log_item_t *bip)
{
	buf_t	*bp;
	uint	hold;

	bp = bip->bli_buf;

	/*
	 * Clear the buffer's association with this transaction.
	 */
	bp->b_fsprivate2 = NULL;

	/*
	 * Before possibly freeing the buf item, determine if we should
	 * release the buffer at the end of this routine.
	 */
	hold = bip->bli_flags & XFS_BLI_HOLD;

	/*
	 * If the buf item isn't tracking any data, free it.
	 * Otherwise, if XFS_BLI_HOLD is set clear it.
	 */
	if (xfs_buf_item_bits(bip->bli_dirty_map, (int)bip->bli_map_size, 0) == 0) {
		xfs_buf_item_relse(bp);
	} else if (hold) {
		bip->bli_flags &= ~XFS_BLI_HOLD;
	}

	/*
	 * Release the buffer if XFS_BLI_HOLD was not set.
	 */
	if (!hold) {
		xfs_brelse(bp);
	}
}

/*
 * This is called to find out where the oldest active copy of the
 * buf log item in the on disk log resides now that the last log
 * write of it completed at the given lsn.  Since we always re-log
 * all dirty data in a buffer, the latest copy in the on disk log
 * is the only one that matters.  Therefore, simply return the
 * given lsn.
 */
xfs_lsn_t
xfs_buf_item_committed(xfs_buf_log_item_t *bip, xfs_lsn_t lsn)
/* ARGSUSED */
{
	return (lsn);
}


/*
 * This is called to asynchronously write the buffer associated with this
 * buf log item out to disk. The buffer will already have been locked by
 * a successful call to xfs_buf_item_trylock().  If the buffer still has
 * B_DELWRI set, then get it going out to disk with a call to bawrite().
 * If not, then just release the buffer.
 */
void
xfs_buf_item_push(xfs_buf_log_item_t *bip)
{
	buf_t	*bp;

	bp = bip->bli_buf;
	if (bp->b_flags & B_DELWRI) {
		bawrite(bp);
	} else {
		brelse(bp);
	}
}

/*
 * This is the ops vector shared by all buf log items.
 */
struct xfs_item_ops xfs_buf_item_ops = {
	(uint(*)(xfs_log_item_t*))xfs_buf_item_size,
	(uint(*)(xfs_log_item_t*, caddr_t, uint, int*))xfs_buf_item_format,
	(void(*)(xfs_log_item_t*))xfs_buf_item_pin,
	(void(*)(xfs_log_item_t*))xfs_buf_item_unpin,
	(uint(*)(xfs_log_item_t*))xfs_buf_item_trylock,
	(void(*)(xfs_log_item_t*))xfs_buf_item_unlock,
	(xfs_lsn_t(*)(xfs_log_item_t*, xfs_lsn_t))xfs_buf_item_committed,
	(void(*)(xfs_log_item_t*))xfs_buf_item_push
};


/*
 * Allocate a new buf log item to go with the given buffer.
 * Set the buffer's b_fsprivate field to point to the new
 * buf log item.
 */
void
xfs_buf_item_init(buf_t *bp, struct xfs_mount *mp)
{
	xfs_buf_log_item_t	*bip;
	int			chunks;
	int			map_size;

	ASSERT(bp->b_fsprivate == NULL);

	/*
	 * chunks is the number of XFS_BLI_CHUNK size pieces
	 * the buffer can be divided into. Make sure not to
	 * truncate any pieces.  map_size is the size of the
	 * bitmap needed to describe the chunks of the buffer.
	 */
	chunks = (int)((bp->b_bcount + (XFS_BLI_CHUNK - 1)) >> XFS_BLI_SHIFT);
	map_size = (chunks + NBWORD) >> BIT_TO_WORD_SHIFT;

	/*
	 * Since the bitmap adds to the end of the structure, add
	 * the space for the bitmap to the allocation.  The first
	 * word of the bitmap is in the structure, so only allocate
	 * map_size - 1 extra words.
	 */
	bip = (xfs_buf_log_item_t*)kmem_zalloc(sizeof(xfs_buf_log_item_t) +
					       ((map_size - 1) * sizeof(int)),
					       0);
	bip->bli_item.li_type = XFS_LI_BUF;
	bip->bli_item.li_ops = &xfs_buf_item_ops;
	bip->bli_item.li_mountp = mp;
	bip->bli_buf = bp;
	bip->bli_map_size = map_size;

	/*
	 * Point the buffer at the transaction.
	 */
	bp->b_fsprivate = bip;
}


/*
 * Mark bytes first through last inclusive as dirty in the buf
 * item's bitmap.
 */
void
xfs_buf_item_log(xfs_buf_log_item_t *bip, uint first, uint last)
{
	uint		first_bit;
	uint		last_bit;
	uint		bits_to_set;
	uint		bits_set;
	uint		word_num;
	uint		*wordp;
	uint		bit;
	uint		end_bit;
	uint		mask;

	/*
	 * Mark the item as having some dirty data for
	 * quick reference in xfs_buf_item_dirty.
	 */
	bip->bli_flags |= XFS_BLI_DIRTY;

	/*
	 * Convert byte offsets to bit numbers.
	 */
	first_bit = first >> XFS_BLI_SHIFT;
	last_bit = last >> XFS_BLI_SHIFT;

	/*
	 * Calculate the total number of bits to be set.
	 */
	bits_to_set = last_bit - first_bit + 1;	

	/*
	 * Get a pointer to the first word in the bitmap
	 * to set a bit in.
	 */
	word_num = first_bit >> BIT_TO_WORD_SHIFT;
	wordp = &(bip->bli_dirty_map[word_num]);

	/*
	 * Calculate the starting bit in the first word.
	 */
	bit = first_bit & (NBWORD - 1);

	/*
	 * First set any bits in the first word of our range.
	 * If it starts at bit 0 of the word, it will be
	 * set below rather than here.  That is what the variable
	 * bit tells us. The variable bits_set tracks the number
	 * of bits that have been set so far.  End_bit is the number
	 * of the last bit to be set in this word plus one.
	 */
	if (bit) {
		end_bit = MIN(bit + bits_to_set, NBWORD);
		mask = ((1 << (end_bit - bit)) - 1) << bit;
		*wordp |= mask;
		wordp++;
		bits_set = end_bit - bit;
	} else {
		bits_set = 0;
	}

	/*
	 * Now set bits a whole word at a time that are between
	 * first_bit and last_bit.
	 */
	while ((bits_to_set - bits_set) >= NBWORD) {
		*wordp |= 0xffffffff;
		bits_set += NBWORD;
		wordp++;
	}

	/*
	 * Finally, set any bits left to be set in one last partial word.
	 */
	end_bit = bits_to_set - bits_set;
	if (end_bit) {
		mask = (1 << end_bit) - 1;
		*wordp |= mask;
	}
}

/*
 * Count the number of bits set in the bitmap starting with bit
 * start_bit.  Size is the size of the bitmap in bytes.
 *
 * Do the counting by mapping a byte value to the number of set
 * bits for that value using the byte_to_bits array, i.e.
 * byte_to_bits[0] == 0, byte_to_bits[1] == 1, byte_to_bits[2] == 1,
 * byte_to_bits[3] == 2, etc.
 */
STATIC int
xfs_buf_item_bits(uint *map, int size, int start_bit)
{
	register int	bits;
	register char	*bytep;
	register char	*end_map;
	int		byte_bit;
	extern char	byte_to_bits[];

	bits = 0;
	end_map = (char*)(map + size);
	bytep = (char*)(map + (start_bit & ~0x7));
	byte_bit = start_bit & 0x7;

	/*
	 * If the caller fell off the end of the map, return 0.
	 */
	if (bytep >= end_map) {
		return (0);
	}

	/*
	 * If start_bit is not byte aligned, then process the
	 * first byte separately.
	 */
	if (byte_bit != 0) {
		/*
		 * Shift off the bits we don't want to look at,
		 * before indexing into byte_to_bits.
		 */
		bits += byte_to_bits[(*bytep >> byte_bit)];
		bytep++;
	}

	/*
	 * Count the bits in each byte until the end of the bitmap.
	 */
	while (bytep < end_map) {
		bits += byte_to_bits[*bytep];
		bytep++;
	}

	return (bits);
}
	
/*
 * This takes the bit number to start looking from and
 * returns the next set bit from there.  It returns -1
 * if there are no more bits set or the start bit is
 * beyond the end of the bitmap.
 *
 * Size is the number of words, not bytes, in the bitmap.
 */
STATIC int
xfs_buf_item_next_bit(uint *map, int size, int start_bit)
{
	int	next_bit;
	uint	*wordp;
	uint	*end_map;
	int	word_bit;
	uint	word;

	end_map = map + size;
	wordp = map + (start_bit >> BIT_TO_WORD_SHIFT);
	word_bit = start_bit & (NBWORD - 1);

	/*
	 * If the caller has stepped beyond the end of the bitmap,
	 * return -1.
	 */
	if (wordp >= end_map) {
		return (-1);
	}

	next_bit = start_bit;

	/*
	 * If the start_bit does not start on a word boundary,
	 * check the remainder of the starting word first.
	 */
	if (word_bit != 0) {
		word = *wordp >> word_bit;
		while (word != 0) {
			if (word & 1) {
				return (next_bit);
			}
			word = word >> 1;
			next_bit++;	
		}
		/*
		 * Since we don't know how many bits we looked at before
		 * word became 0, just set next_bit to the start of the
		 * next word.
		 */
		wordp++;
		next_bit = ROUNDUPNBWORD(start_bit); 
	}

	/*
	 * Do word at a time checking for bits until the end of the map.
	 */
	while (wordp < end_map) {
		/*
		 * If the current word is empty, skip it.
		 */
		if (*wordp == 0) {
			wordp++;
			next_bit += NBWORD;
			continue;
		}

		/*
		 * We know we've got a bit in this word, find it.
		 */
		word = *wordp;
		while (1) {
			if (word & 1) {
				return (next_bit);
			}
			next_bit++;
			word = word >> 1;
		}
	}

	/*
	 * If there were no more bits in the bitmap, return -1.
	 */
	return (-1);
}

/*
 * Set the specified bit in the given bitmap.
 */
STATIC void
xfs_buf_item_set_bit(uint *map, int size, int bit)
/* ARGSUSED */
{
	uint	*wordp;
	int	word_bit;

	wordp = map + (bit >> BIT_TO_WORD_SHIFT);
	word_bit = bit & (NBWORD - 1);

	*wordp |= 1 << word_bit;
}
		
/*
 * Return 1 if the buffer has some data that has been logged (at any
 * point, not just the current transaction) and 0 if not.
 */
uint
xfs_buf_item_dirty(xfs_buf_log_item_t *bip)
{
	return (bip->bli_flags & XFS_BLI_DIRTY);
}

/*
 * This is called when the buf log item is no longer needed.  It should
 * free the buf log item associated with the given buffer and clear
 * the buffer's pointer to the buf log item.
 */
void
xfs_buf_item_relse(buf_t *bp)
{
	xfs_buf_log_item_t	*bip;

	bip = (xfs_buf_log_item_t*)bp->b_fsprivate;
	kmem_free(bip, sizeof(xfs_buf_log_item_t) +
		  ((bip->bli_map_size - 1) * sizeof(int)));
	bp->b_fsprivate = NULL;
}



/*
 * This is the iodone() function for buffers which have been
 * logged.  It is called when they are eventually flushed out.
 * It should remove the buf item from the AIL, and free the buf item.
 * It should then clear the b_iodone field of the buffer and 
 * call iodone() with the buffer so that it will receive normal
 * iodone() processing.
 */ 
void
xfs_buf_iodone(buf_t *bp)
{
	xfs_buf_log_item_t	*bip;
	struct xfs_mount	*mp;
	int			s;

	ASSERT(bp->b_fsprivate != NULL);
	
	bip = (xfs_buf_log_item_t*)bp->b_fsprivate;
	ASSERT(bip->bli_buf == bp);

	mp = bip->bli_item.li_mountp;
	s = AIL_LOCK(mp);
	xfs_trans_delete_ail(mp, (xfs_log_item_t *)bip);
	AIL_UNLOCK(mp, s);

	kmem_free(bip, sizeof(xfs_buf_log_item_t) +
		  ((bip->bli_map_size - 1) * sizeof(int)));
	bp->b_fsprivate = NULL;

	bp->b_iodone = NULL;
	iodone(bp);
}




