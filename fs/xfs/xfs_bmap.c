#ident	"$Revision: 1.101 $"

#include <sys/param.h>
#include <sys/buf.h>
#include <sys/vnode.h>
#include <sys/debug.h>
#include <sys/kmem.h>
#include <sys/ktrace.h>
#include <sys/fcntl.h>
#include <sys/errno.h>
#ifdef SIM
#include <stddef.h>
#include <bstring.h>
#else
#include <sys/systm.h>
#endif
#ifdef SIM
#define _KERNEL
#endif
#include <sys/uuid.h>
#include <sys/grio.h>
#include <sys/pfdat.h>
#ifdef SIM
#undef _KERNEL
#endif
#include "xfs_types.h"
#include "xfs_inum.h"
#include "xfs_log.h"
#include "xfs_trans.h"
#include "xfs_sb.h"
#include "xfs_ag.h"
#include "xfs_mount.h"
#include "xfs_alloc_btree.h"
#include "xfs_bmap_btree.h"
#include "xfs_ialloc_btree.h"
#include "xfs_btree.h"
#include "xfs_ialloc.h"
#include "xfs_dinode.h"
#include "xfs_inode_item.h"
#include "xfs_inode.h"
#include "xfs_extfree_item.h"
#include "xfs_bmap.h"
#include "xfs_alloc.h"
#include "xfs_rtalloc.h"
#include "xfs_error.h"
#ifdef SIM
#include "sim.h"
#endif

#if !defined(SIM) || !defined(XFSDEBUG)
#define	kmem_check()	/* dummy for memory-allocation checking */
#endif

ktrace_t	*xfs_bmap_trace_buf;

zone_t *xfs_bmap_free_item_zone;

/*
 * Prototypes for internal bmap routines.
 */

/*
 * Called by xfs_bmapi to update extent list structure and the btree
 * after allocating space (or doing a delayed allocation).
 */
STATIC int				/* inode logging flags */
xfs_bmap_add_extent(
	xfs_inode_t		*ip,	/* incore inode pointer */
	xfs_extnum_t		idx,	/* extent number to update/insert */
	xfs_btree_cur_t		**curp,	/* if *curp is null, not a btree */
	xfs_bmbt_irec_t		*new,	/* new data to put in extent list */
	xfs_fsblock_t		*first,	/* pointer to firstblock variable */
	xfs_bmap_free_t		*flist,	/* list of extents to be freed */
	int			lowspace);/* set if running in low space mode */

/*
 * Called by xfs_bmap_add_extent to handle cases converting a delayed
 * allocation to a real allocation.
 */
STATIC int				/* inode logging flags */
xfs_bmap_add_extent_delay_real(
	xfs_inode_t		*ip,	/* incore inode pointer */
	xfs_extnum_t		idx,	/* extent number to update/insert */
	xfs_btree_cur_t		**curp,	/* if *curp is null, not a btree */
	xfs_bmbt_irec_t		*new,	/* new data to put in extent list */
	xfs_extlen_t		*dnew,	/* new delayed-alloc indirect blocks */
	xfs_fsblock_t		*first,	/* pointer to firstblock variable */
	xfs_bmap_free_t		*flist,	/* list of extents to be freed */
	int			lowspace);/* set if running in low space mode */

/*
 * Called by xfs_bmap_add_extent to handle cases converting a hole
 * to a delayed allocation.
 */
STATIC int				/* inode logging flags */
xfs_bmap_add_extent_hole_delay(
	xfs_inode_t		*ip,	/* incore inode pointer */
	xfs_extnum_t		idx,	/* extent number to update/insert */
	xfs_btree_cur_t		*cur,	/* if null, not a btree */
	xfs_bmbt_irec_t		*new);	/* new data to put in extent list */

/*
 * Called by xfs_bmap_add_extent to handle cases converting a hole
 * to a real allocation.
 */
STATIC int				/* inode logging flags */
xfs_bmap_add_extent_hole_real(
	xfs_inode_t		*ip,	/* incore inode pointer */
	xfs_extnum_t		idx,	/* extent number to update/insert */
	xfs_btree_cur_t		*cur,	/* if null, not a btree */
	xfs_bmbt_irec_t		*new);	/* new data to put in extent list */

/*
 * xfs_bmap_alloc is called by xfs_bmapi to allocate an extent for a file.
 * It figures out where to ask the underlying allocator to put the new extent.
 */
STATIC void
xfs_bmap_alloc(
	xfs_bmalloca_t		*ap);	/* bmap alloc argument struct */

/*
 * Transform a btree format file with only one leaf node, where the
 * extents list will fit in the inode, into an extents format file.
 * Since the extent list is already in-core, all we have to do is
 * give up the space for the btree root and pitch the leaf block.
 */
STATIC int				/* inode logging flags */
xfs_bmap_btree_to_extents(
	xfs_trans_t		*tp,	/* transaction pointer */
	xfs_inode_t		*ip,	/* incore inode pointer */
	xfs_btree_cur_t		*cur);	/* btree cursor */

#ifdef DEBUG
/*
 * Check that the extents list for the inode ip is in the right order.
 */
STATIC void
xfs_bmap_check_extents(
	xfs_inode_t		*ip);		/* incore inode pointer */
#else
#define	xfs_bmap_check_extents(ip)
#endif

/*
 * Called by xfs_bmapi to update extent list structure and the btree
 * after removing space (or undoing a delayed allocation).
 */
STATIC int				/* inode logging flags */
xfs_bmap_del_extent(
	xfs_inode_t		*ip,	/* incore inode pointer */
	xfs_extnum_t		idx,	/* extent number to update/insert */
	xfs_bmap_free_t		*flist,	/* list of extents to be freed */
	xfs_btree_cur_t		*cur,	/* if null, not a btree */
	xfs_bmbt_irec_t		*new);	/* new data to put in extent list */

/*
 * Remove the entry "free" from the free item list.  Prev points to the
 * previous entry, unless "free" is the head of the list.
 */
STATIC void
xfs_bmap_del_free(
	xfs_bmap_free_t		*flist,	/* free item list header */
	xfs_bmap_free_item_t	*prev,	/* previous item on list, if any */
	xfs_bmap_free_item_t	*free);	/* list item to be freed */

/*
 * Remove count entries from the extents array for inode "ip", starting
 * at index "idx".  Copies the remaining items down over the deleted ones,
 * and gives back the excess memory.
 */
STATIC void
xfs_bmap_delete_exlist(
	xfs_inode_t	*ip,		/* incode inode pointer */
	xfs_extnum_t	idx,		/* starting delete index */
	xfs_extnum_t	count);		/* count of items to delete */

/*
 * Convert an extents-format file into a btree-format file.
 * The new file will have a root block (in the inode) and a single child block.
 */
STATIC int					/* inode logging flags */
xfs_bmap_extents_to_btree(
	xfs_trans_t		*tp,		/* transaction pointer */
	xfs_inode_t		*ip,		/* incore inode pointer */
	xfs_fsblock_t		*firstblock,	/* first-block-allocated */
	xfs_bmap_free_t		*flist,		/* blocks freed in xaction */
	xfs_btree_cur_t		**curp,		/* cursor returned to caller */
	int			wasdel,		/* converting a delayed alloc */
	int			lowspace);	/* running in low-space mode */

/*
 * Insert new item(s) in the extent list for inode "ip".
 * Count new items are inserted at offset idx.
 */
STATIC void
xfs_bmap_insert_exlist(
	xfs_inode_t	*ip,		/* incore inode pointer */
	xfs_extnum_t	idx,		/* starting index of new items */
	xfs_extnum_t	count,		/* number of inserted items */
	xfs_bmbt_irec_t	*new);		/* items to insert */

/*
 * Convert a local file to an extents file.
 * This code is sort of bogus, since the file data needs to get 
 * logged so it won't be lost.  The bmap-level manipulations are ok, though.
 */
STATIC int
xfs_bmap_local_to_extents(
	xfs_trans_t	*tp,		/* transaction pointer */
	xfs_inode_t	*ip,		/* incore inode pointer */
	xfs_fsblock_t	*firstblock,	/* first block allocated in xaction */
	xfs_extlen_t	total);		/* total blocks needed by transaction */
 
/*
 * Search the extents list for the inode, for the extent containing bno.
 * If bno lies in a hole, point to the next entry.  If bno lies past eof,
 * *eofp will be set, and *prevp will contain the last entry (null if none).
 * Else, *lastxp will be set to the index of the found
 * entry; *gotp will contain the entry.
 */
STATIC xfs_bmbt_rec_t *			/* pointer to found extent entry */
xfs_bmap_search_extents(
	xfs_inode_t	*ip,		/* incore inode pointer */
	xfs_fsblock_t	bno,		/* block number searched for */
	int		*eofp,		/* out: end of file found */
	xfs_extnum_t	*lastxp,	/* out: last extent index */
	xfs_bmbt_irec_t	*gotp,		/* out: extent entry found */
	xfs_bmbt_irec_t	*prevp);	/* out: previous extent entry found */

#if defined(DEBUG) && !defined(SIM)
/*
 * Add a bmap trace buffer entry.  Base routine for the others.
 */
STATIC void
xfs_bmap_trace_addentry(
	int		opcode,		/* operation */
	char		*fname,		/* function name */
	char		*desc,		/* operation description */
	xfs_inode_t	*ip,		/* incore inode pointer */
	xfs_extnum_t	idx,		/* index of entry(entries) deleted */
	xfs_extnum_t	cnt,		/* count of entries deleted, 1 or 2 */
	xfs_bmbt_rec_t	*r1,		/* first record */
	xfs_bmbt_rec_t	*r2);		/* second record or null */

/*
 * Add bmap trace entry prior to a call to xfs_bmap_delete_exlist.
 */
STATIC void
xfs_bmap_trace_delete(
	char		*fname,		/* function name */
	char		*desc,		/* operation description */
	xfs_inode_t	*ip,		/* incore inode pointer */
	xfs_extnum_t	idx,		/* index of entry(entries) deleted */
	xfs_extnum_t	cnt);		/* count of entries deleted, 1 or 2 */

/*
 * Add bmap trace entry prior to a call to xfs_bmap_insert_exlist, or
 * reading in the extents list from the disk (in the btree).
 */
STATIC void
xfs_bmap_trace_insert(
	char		*fname,		/* function name */
	char		*desc,		/* operation description */
	xfs_inode_t	*ip,		/* incore inode pointer */
	xfs_extnum_t	idx,		/* index of entry(entries) inserted */
	xfs_extnum_t	cnt,		/* count of entries inserted, 1 or 2 */
	xfs_bmbt_irec_t	*r1,		/* inserted record 1 */
	xfs_bmbt_irec_t	*r2);		/* inserted record 2 or null */

/*
 * Add bmap trace entry after updating an extent list entry in place.
 */
STATIC void
xfs_bmap_trace_post_update(
	char		*fname,		/* function name */
	char		*desc,		/* operation description */
	xfs_inode_t	*ip,		/* incore inode pointer */
	xfs_extnum_t	idx);		/* index of entry updated */

/*
 * Add bmap trace entry prior to updating an extent list entry in place.
 */
STATIC void
xfs_bmap_trace_pre_update(
	char		*fname,		/* function name */
	char		*desc,		/* operation description */
	xfs_inode_t	*ip,		/* incore inode pointer */
	xfs_extnum_t	idx);		/* index of entry to be updated */
#else
#define	xfs_bmap_trace_delete(f,d,ip,i,c)
#define	xfs_bmap_trace_insert(f,d,ip,i,c,r1,r2)
#define	xfs_bmap_trace_post_update(f,d,ip,i)
#define	xfs_bmap_trace_pre_update(f,d,ip,i)
#endif	/* DEBUG && !SIM */

/*
 * Compute the worst-case number of indirect blocks that will be used
 * for ip's delayed extent of length "len".
 */
STATIC xfs_extlen_t
xfs_bmap_worst_indlen(
	xfs_inode_t		*ip,	/* incore inode pointer */
	xfs_extlen_t		len);	/* delayed extent length */


/*
 * Bmap internal routines.
 */

/*
 * Called by xfs_bmapi to update extent list structure and the btree
 * after allocating space (or doing a delayed allocation).
 */
STATIC int				/* inode logging flags */
xfs_bmap_add_extent(
	xfs_inode_t		*ip,	/* incore inode pointer */
	xfs_extnum_t		idx,	/* extent number to update/insert */
	xfs_btree_cur_t		**curp,	/* if *curp is null, not a btree */
	xfs_bmbt_irec_t		*new,	/* new data to put in extent list */
	xfs_fsblock_t		*first,	/* pointer to firstblock variable */
	xfs_bmap_free_t		*flist,	/* list of extents to be freed */
	int			lowspace)/* set if running in low space mode */
{
	xfs_btree_cur_t		*cur;	/* btree cursor or null */
	xfs_extlen_t		da_new; /* new count del alloc blocks used */
	xfs_extlen_t		da_old; /* old count del alloc blocks used */
#if defined(DEBUG) && !defined(SIM)
	static char		fname[] = "xfs_bmap_add_extent";
#endif
	int			logflags; /* returned value */
	xfs_mount_t		*mp;	/* mount point structure */
	xfs_extnum_t		nextents; /* number of extents in file now */
	xfs_bmbt_irec_t		prev;	/* old extent at offset idx */

	cur = *curp;
	nextents = ip->i_bytes / sizeof(xfs_bmbt_rec_t);
	ASSERT(idx <= nextents);
	da_old = da_new = 0;
	/*
	 * This is the first extent added to a new/empty file.
	 * Special case this one, so other routines get to assume there are
	 * already extents in the list.
	 */
	if (nextents == 0) {
		xfs_bmap_trace_insert(fname, "insert empty", ip, 0, 1, new,
			NULL);
		xfs_bmap_insert_exlist(ip, 0, 1, new);
		ASSERT(cur == NULL);
		ip->i_lastex = 0;
		kmem_check();
		if (!ISNULLSTARTBLOCK(new->br_startblock)) {
			ip->i_d.di_nextents = 1;
			logflags = XFS_ILOG_CORE | XFS_ILOG_EXT;
		} else
			logflags = 0;
	}
	/*
	 * Any kind of new delayed allocation goes here.
	 */
	else if (ISNULLSTARTBLOCK(new->br_startblock)) {
		if (cur)
			ASSERT((cur->bc_private.b.flags &
				XFS_BTCUR_BPRV_WASDEL) == 0);
		logflags = xfs_bmap_add_extent_hole_delay(ip, idx, cur, new);
	}
	/*
	 * Real allocation off the end of the file.
	 */
	else if (idx == nextents) {
		if (cur)
			ASSERT((cur->bc_private.b.flags &
				XFS_BTCUR_BPRV_WASDEL) == 0);
		logflags = xfs_bmap_add_extent_hole_real(ip, idx, cur, new);
	} else {
		/*
		 * Get the record referred to by idx.
		 */
		xfs_bmbt_get_all(&ip->i_u1.iu_extents[idx], &prev);
		/*
		 * If it's a real allocation record, and the new allocation ends
		 * after the start of the referred to record, then we're filling
		 * in a delayed allocation with a real one.
		 */
		if (!ISNULLSTARTBLOCK(new->br_startblock) &&
		    new->br_startoff + new->br_blockcount > prev.br_startoff) {
			ASSERT(ISNULLSTARTBLOCK(prev.br_startblock));
			da_old = STARTBLOCKVAL(prev.br_startblock);
			if (cur)
				ASSERT(cur->bc_private.b.flags &
					XFS_BTCUR_BPRV_WASDEL);
			logflags = xfs_bmap_add_extent_delay_real(ip, idx,
				&cur, new, &da_new, first, flist, lowspace);
			ASSERT(*curp == cur || *curp == NULL);
		}
		/*
		 * Otherwise we're filling in a hole with an allocation.
		 */
		else {
			if (cur)
				ASSERT((cur->bc_private.b.flags &
					XFS_BTCUR_BPRV_WASDEL) == 0);
			logflags = xfs_bmap_add_extent_hole_real(ip, idx,
				cur, new);
		}
	}
	/*
	 * Convert to a btree if necessary.
	 */
	mp = ip->i_mount;
	if (ip->i_d.di_format == XFS_DINODE_FMT_EXTENTS &&
	    ip->i_d.di_nextents > XFS_BMAP_EXT_MAXRECS(mp)) {
		ASSERT(cur == NULL);
		logflags |= xfs_bmap_extents_to_btree(ip->i_transp, ip,
			first, flist, &cur, da_old > 0, lowspace);
	}
	/*
	 * Adjust for changes in reserved delayed indirect blocks.
	 */
	if (da_old || da_new) {
		xfs_extlen_t	nblks;

		nblks = da_new;
		if (cur)
			nblks += cur->bc_private.b.allocated;
		ASSERT(nblks <= da_old);
		if (nblks < da_old)
			xfs_mod_incore_sb(mp, XFS_SB_FDBLOCKS, da_old - nblks);
	}
	/*
	 * Clear out the allocated field, done with it now in any case.
	 */
	if (cur) {
		cur->bc_private.b.allocated = 0;
		*curp = cur;
	}
	return logflags;
}

/*
 * Called by xfs_bmap_add_extent to handle cases converting a delayed
 * allocation to a real allocation.
 */
STATIC int				/* inode logging flags */
xfs_bmap_add_extent_delay_real(
	xfs_inode_t		*ip,	/* incore inode pointer */
	xfs_extnum_t		idx,	/* extent number to update/insert */
	xfs_btree_cur_t		**curp,	/* if *curp is null, not a btree */
	xfs_bmbt_irec_t		*new,	/* new data to put in extent list */
	xfs_extlen_t		*dnew,	/* new delayed-alloc indirect blocks */
	xfs_fsblock_t		*first,	/* pointer to firstblock variable */
	xfs_bmap_free_t		*flist,	/* list of extents to be freed */
	int			lowspace)/* set if running in low space mode */
{
	xfs_bmbt_rec_t		*base;	/* base of extent entry list */
	xfs_btree_cur_t		*cur;	/* btree cursor */
	int			diff;	/* temp value */
	xfs_bmbt_rec_t		*ep;	/* extent entry for idx */
#if defined(DEBUG) && !defined(SIM)
	static char		fname[] = "xfs_bmap_add_extent_delay_real";
#endif
	xfs_fsblock_t		new_endoff;	/* end offset of new entry */
	xfs_bmbt_irec_t		r[3];	/* neighbor extent entries */
					/* left is 0, right is 1, prev is 2 */
	int			rval;	/* return value */
	int			state;	/* state bits, accessed thru macros */
	xfs_extlen_t		temp;	/* value for dnew calculations */
	xfs_extlen_t		temp2;	/* value for dnew calculations */
	enum {				/* bit number definitions for state */
		LEFT_CONTIG,	RIGHT_CONTIG,
		LEFT_FILLING,	RIGHT_FILLING,
		LEFT_DELAY,	RIGHT_DELAY,
		LEFT_VALID,	RIGHT_VALID
	};

#define	LEFT		r[0]
#define	RIGHT		r[1]
#define	PREV		r[2]
#define	MASK(b)		(1 << (b))
#define	MASK2(a,b)	(MASK(a) | MASK(b))
#define	MASK3(a,b,c)	(MASK2(a,b) | MASK(c))
#define	MASK4(a,b,c,d)	(MASK3(a,b,c) | MASK(d))
#define	STATE_SET(b,v)	((v) ? (state |= MASK(b)) : (state &= ~MASK(b)))
#define	STATE_TEST(b)	(state & MASK(b))
#define	STATE_SET_TEST(b,v)	((v) ? ((state |= MASK(b)), 1) : \
				       ((state &= ~MASK(b)), 0))
#define	SWITCH_STATE		\
	(state & MASK4(LEFT_FILLING, RIGHT_FILLING, LEFT_CONTIG, RIGHT_CONTIG))

	/*
	 * Set up a bunch of variables to make the tests simpler.
	 */
	cur = *curp;
	base = ip->i_u1.iu_extents;
	ep = &base[idx];
	xfs_bmbt_get_all(ep, &PREV);
	new_endoff = new->br_startoff + new->br_blockcount;
	ASSERT(PREV.br_startoff <= new->br_startoff);
	ASSERT(PREV.br_startoff + PREV.br_blockcount >= new_endoff);
	/*
	 * Set flags determining what part of the previous delayed allocation
	 * extent is being replaced by a real allocation.
	 */
	STATE_SET(LEFT_FILLING, PREV.br_startoff == new->br_startoff);
	STATE_SET(RIGHT_FILLING,
		PREV.br_startoff + PREV.br_blockcount == new_endoff);
	/*
	 * Check and set flags if this segment has a left neighbor.
	 * Don't set contiguous if the combined extent would be too large.
	 */
	if (STATE_SET_TEST(LEFT_VALID, idx > 0)) {
		xfs_bmbt_get_all(ep - 1, &LEFT);
		STATE_SET(LEFT_DELAY, ISNULLSTARTBLOCK(LEFT.br_startblock));
	}
	STATE_SET(LEFT_CONTIG, 
		STATE_TEST(LEFT_VALID) && !STATE_TEST(LEFT_DELAY) &&
		LEFT.br_startoff + LEFT.br_blockcount == new->br_startoff &&
		LEFT.br_startblock + LEFT.br_blockcount == new->br_startblock &&
		LEFT.br_blockcount + new->br_blockcount <= MAXEXTLEN);
	/*
	 * Check and set flags if this segment has a right neighbor.
	 * Don't set contiguous if the combined extent would be too large.
	 * Also check for all-three-contiguous being too large.
	 */
	if (STATE_SET_TEST(RIGHT_VALID,
		idx < ip->i_bytes / sizeof(xfs_bmbt_rec_t) - 1)) {
		xfs_bmbt_get_all(ep + 1, &RIGHT);
		STATE_SET(RIGHT_DELAY, ISNULLSTARTBLOCK(RIGHT.br_startblock));
	}
	STATE_SET(RIGHT_CONTIG, 
		STATE_TEST(RIGHT_VALID) && !STATE_TEST(RIGHT_DELAY) &&
		new_endoff == RIGHT.br_startoff &&
		new->br_startblock + new->br_blockcount ==
		    RIGHT.br_startblock &&
		new->br_blockcount + RIGHT.br_blockcount <= MAXEXTLEN &&
		((state & MASK3(LEFT_CONTIG, LEFT_FILLING, RIGHT_FILLING)) !=
		  MASK3(LEFT_CONTIG, LEFT_FILLING, RIGHT_FILLING) ||
		 LEFT.br_blockcount + new->br_blockcount + RIGHT.br_blockcount
		     <= MAXEXTLEN));
	/*
	 * Switch out based on the FILLING and CONTIG state bits.
	 */
	switch (SWITCH_STATE) {

	case MASK4(LEFT_FILLING, RIGHT_FILLING, LEFT_CONTIG, RIGHT_CONTIG):
		/*
		 * Filling in all of a previously delayed allocation extent.
		 * The left and right neighbors are both contiguous with new.
		 */
		xfs_bmap_trace_pre_update(fname, "LF|RF|LC|RC", ip, idx - 1);
		xfs_bmbt_set_blockcount(ep - 1,
			LEFT.br_blockcount + PREV.br_blockcount +
			RIGHT.br_blockcount);
		xfs_bmap_trace_post_update(fname, "LF|RF|LC|RC", ip, idx - 1);
		xfs_bmap_trace_delete(fname, "LF|RF|LC|RC", ip, idx, 2);
		xfs_bmap_delete_exlist(ip, idx, 2);
		ip->i_lastex = idx - 1;
		ip->i_d.di_nextents--;
		if (cur == NULL)
			rval = XFS_ILOG_CORE | XFS_ILOG_EXT;
		else {
			xfs_bmbt_lookup_eq(cur, RIGHT.br_startoff,
				RIGHT.br_startblock, RIGHT.br_blockcount);
			xfs_bmbt_delete(cur);
			xfs_bmbt_decrement(cur, 0);
			xfs_bmbt_update(cur, LEFT.br_startoff,
				LEFT.br_startblock,
				LEFT.br_blockcount + PREV.br_blockcount +
				RIGHT.br_blockcount);
			rval = XFS_ILOG_CORE;
		}
		*dnew = 0;
		break;

	case MASK3(LEFT_FILLING, RIGHT_FILLING, LEFT_CONTIG):
		/*
		 * Filling in all of a previously delayed allocation extent.
		 * The left neighbor is contiguous, the right is not.
		 */
		xfs_bmap_trace_pre_update(fname, "LF|RF|LC", ip, idx - 1);
		xfs_bmbt_set_blockcount(ep - 1,
			LEFT.br_blockcount + PREV.br_blockcount);
		xfs_bmap_trace_post_update(fname, "LF|RF|LC", ip, idx - 1);
		ip->i_lastex = idx - 1;
		xfs_bmap_trace_delete(fname, "LF|RF|LC", ip, idx, 1);
		xfs_bmap_delete_exlist(ip, idx, 1);
		if (cur == NULL)
			rval = XFS_ILOG_EXT;
		else {
			xfs_bmbt_lookup_eq(cur, LEFT.br_startoff,
				LEFT.br_startblock, LEFT.br_blockcount);
			xfs_bmbt_update(cur, LEFT.br_startoff,
				LEFT.br_startblock,
				LEFT.br_blockcount + PREV.br_blockcount);
			rval = 0;
		}
		*dnew = 0;
		break;

	case MASK3(LEFT_FILLING, RIGHT_FILLING, RIGHT_CONTIG):
		/*
		 * Filling in all of a previously delayed allocation extent.
		 * The right neighbor is contiguous, the left is not.
		 */
		xfs_bmap_trace_pre_update(fname, "LF|RF|RC", ip, idx);
		xfs_bmbt_set_startblock(ep, new->br_startblock);
		xfs_bmbt_set_blockcount(ep,
			PREV.br_blockcount + RIGHT.br_blockcount);
		xfs_bmap_trace_post_update(fname, "LF|RF|RC", ip, idx);
		ip->i_lastex = idx;
		xfs_bmap_trace_delete(fname, "LF|RF|RC", ip, idx + 1, 1);
		xfs_bmap_delete_exlist(ip, idx + 1, 1);
		if (cur == NULL)
			rval = XFS_ILOG_EXT;
		else {
			xfs_bmbt_lookup_eq(cur, RIGHT.br_startoff,
				RIGHT.br_startblock, RIGHT.br_blockcount);
			xfs_bmbt_update(cur, PREV.br_startoff,
				new->br_startblock,
				PREV.br_blockcount + RIGHT.br_blockcount);
			rval = 0;
		}
		*dnew = 0;
		break;

	case MASK2(LEFT_FILLING, RIGHT_FILLING):
		/*
		 * Filling in all of a previously delayed allocation extent.
		 * Neither the left nor right neighbors are contiguous with
		 * the new one.
		 */
		xfs_bmap_trace_pre_update(fname, "LF|RF", ip, idx);
		xfs_bmbt_set_startblock(ep, new->br_startblock);
		xfs_bmap_trace_post_update(fname, "LF|RF", ip, idx);
		ip->i_lastex = idx;
		ip->i_d.di_nextents++;
		if (cur == NULL)
			rval = XFS_ILOG_CORE | XFS_ILOG_EXT;
		else {
			xfs_bmbt_lookup_eq(cur, new->br_startoff,
				new->br_startblock, new->br_blockcount);
			xfs_bmbt_insert(cur);
			rval = XFS_ILOG_CORE;
		}
		*dnew = 0;
		break;

	case MASK2(LEFT_FILLING, LEFT_CONTIG):
		/*
		 * Filling in the first part of a previous delayed allocation.
		 * The left neighbor is contiguous.
		 */
		xfs_bmap_trace_pre_update(fname, "LF|LC", ip, idx - 1);
		xfs_bmbt_set_blockcount(ep - 1,
			LEFT.br_blockcount + new->br_blockcount);
		xfs_bmbt_set_startoff(ep,
			PREV.br_startoff + new->br_blockcount);
		xfs_bmap_trace_post_update(fname, "LF|LC", ip, idx - 1);
		temp = PREV.br_blockcount - new->br_blockcount;
		xfs_bmap_trace_pre_update(fname, "LF|LC", ip, idx);
		xfs_bmbt_set_blockcount(ep, temp);
		ip->i_lastex = idx - 1;
		if (cur == NULL)
			rval = XFS_ILOG_EXT;
		else {
			xfs_bmbt_lookup_eq(cur, LEFT.br_startoff,
				LEFT.br_startblock, LEFT.br_blockcount);
			xfs_bmbt_update(cur, LEFT.br_startoff,
				LEFT.br_startblock,
				LEFT.br_blockcount + new->br_blockcount);
			rval = 0;
		}
		temp = XFS_EXTLEN_MIN(xfs_bmap_worst_indlen(ip, temp),
			STARTBLOCKVAL(PREV.br_startblock));
		xfs_bmbt_set_startblock(ep, NULLSTARTBLOCK(temp));
		xfs_bmap_trace_post_update(fname, "LF|LC", ip, idx);
		*dnew = temp;
		break;

	case MASK(LEFT_FILLING):
		/*
		 * Filling in the first part of a previous delayed allocation.
		 * The left neighbor is not contiguous.
		 */
		xfs_bmap_trace_pre_update(fname, "LF", ip, idx);
		xfs_bmbt_set_startoff(ep, new_endoff);
		temp = PREV.br_blockcount - new->br_blockcount;
		xfs_bmbt_set_blockcount(ep, temp);
		xfs_bmap_trace_insert(fname, "LF", ip, idx, 1, new, NULL);
		xfs_bmap_insert_exlist(ip, idx, 1, new);
		ip->i_lastex = idx;
		ip->i_d.di_nextents++;
		if (cur == NULL)
			rval = XFS_ILOG_CORE | XFS_ILOG_EXT;
		else {
			xfs_bmbt_lookup_eq(cur, new->br_startoff,
				new->br_startblock, new->br_blockcount);
			xfs_bmbt_insert(cur);
			rval = XFS_ILOG_CORE;
		}
		if (ip->i_d.di_format == XFS_DINODE_FMT_EXTENTS &&
		    ip->i_d.di_nextents > XFS_BMAP_EXT_MAXRECS(ip->i_mount))
			rval |= xfs_bmap_extents_to_btree(ip->i_transp, ip,
				first, flist, &cur, 1, lowspace);
		temp = XFS_EXTLEN_MIN(xfs_bmap_worst_indlen(ip, temp),
			STARTBLOCKVAL(PREV.br_startblock) -
			(cur ? cur->bc_private.b.allocated : 0));
		base = ip->i_u1.iu_extents;
		ep = &base[idx + 1];
		xfs_bmbt_set_startblock(ep, NULLSTARTBLOCK(temp));
		xfs_bmap_trace_post_update(fname, "LF", ip, idx + 1);
		*dnew = temp;
		break;

	case MASK2(RIGHT_FILLING, RIGHT_CONTIG):
		/*
		 * Filling in the last part of a previous delayed allocation.
		 * The right neighbor is contiguous with the new allocation.
		 */
		temp = PREV.br_blockcount - new->br_blockcount;
		xfs_bmap_trace_pre_update(fname, "RF|RC", ip, idx);
		xfs_bmap_trace_pre_update(fname, "RF|RC", ip, idx + 1);
		xfs_bmbt_set_blockcount(ep, temp);
		xfs_bmbt_set_startoff(ep + 1, new->br_startoff);
		xfs_bmbt_set_startblock(ep + 1, new->br_startblock);
		xfs_bmbt_set_blockcount(ep + 1,
			new->br_blockcount + RIGHT.br_blockcount);
		xfs_bmap_trace_post_update(fname, "RF|RC", ip, idx + 1);
		ip->i_lastex = idx + 1;
		if (cur == NULL)
			rval = XFS_ILOG_EXT;
		else {
			xfs_bmbt_lookup_eq(cur, RIGHT.br_startoff,
				RIGHT.br_startblock, RIGHT.br_blockcount);
			xfs_bmbt_update(cur, new->br_startoff,
				new->br_startblock,
				new->br_blockcount + RIGHT.br_blockcount);
			rval = 0;
		}
		temp = XFS_EXTLEN_MIN(xfs_bmap_worst_indlen(ip, temp),
			STARTBLOCKVAL(PREV.br_startblock));
		xfs_bmbt_set_startblock(ep, NULLSTARTBLOCK(temp));
		xfs_bmap_trace_post_update(fname, "RF|RC", ip, idx);
		*dnew = temp;
		break;

	case MASK(RIGHT_FILLING):
		/*
		 * Filling in the last part of a previous delayed allocation.
		 * The right neighbor is not contiguous.
		 */
		temp = PREV.br_blockcount - new->br_blockcount;
		xfs_bmap_trace_pre_update(fname, "RF", ip, idx);
		xfs_bmbt_set_blockcount(ep, temp);
		xfs_bmap_trace_insert(fname, "RF", ip, idx + 1, 1,
			new, NULL);
		xfs_bmap_insert_exlist(ip, idx + 1, 1, new);
		ip->i_lastex = idx + 1;
		ip->i_d.di_nextents++;
		if (cur == NULL)
			rval = XFS_ILOG_CORE | XFS_ILOG_EXT;
		else {
			xfs_bmbt_lookup_eq(cur, new->br_startoff,
				new->br_startblock, new->br_blockcount);
			xfs_bmbt_insert(cur);
			rval = XFS_ILOG_CORE;
		}
		if (ip->i_d.di_format == XFS_DINODE_FMT_EXTENTS &&
		    ip->i_d.di_nextents > XFS_BMAP_EXT_MAXRECS(ip->i_mount))
			rval |= xfs_bmap_extents_to_btree(ip->i_transp, ip,
				first, flist, &cur, 1, lowspace);
		temp = XFS_EXTLEN_MIN(xfs_bmap_worst_indlen(ip, temp),
			STARTBLOCKVAL(PREV.br_startblock) -
			(cur ? cur->bc_private.b.allocated : 0));
		base = ip->i_u1.iu_extents;
		ep = &base[idx];
		xfs_bmbt_set_startblock(ep, NULLSTARTBLOCK(temp));
		xfs_bmap_trace_post_update(fname, "RF", ip, idx);
		*dnew = temp;
		break;

	case 0:
		/*
		 * Filling in the middle part of a previous delayed allocation.
		 * Contiguity is impossible here.
		 * This case is avoided almost all the time.
		 */
		temp = new->br_startoff - PREV.br_startoff;
		xfs_bmap_trace_pre_update(fname, "0", ip, idx);
		xfs_bmbt_set_blockcount(ep, temp);
		r[0] = *new;
		r[1].br_startoff = new_endoff;
		temp2 = PREV.br_startoff + PREV.br_blockcount - new_endoff;
		r[1].br_blockcount = temp2;
		xfs_bmap_trace_insert(fname, "0", ip, idx + 1, 2, &r[0], &r[1]);
		xfs_bmap_insert_exlist(ip, idx + 1, 2, &r[0]);
		ip->i_lastex = idx + 1;
		ip->i_d.di_nextents++;
		if (cur == NULL)
			rval = XFS_ILOG_CORE | XFS_ILOG_EXT;
		else {
			xfs_bmbt_lookup_eq(cur, new->br_startoff,
				new->br_startblock, new->br_blockcount);
			xfs_bmbt_insert(cur);
			rval = XFS_ILOG_CORE;
		}
		if (ip->i_d.di_format == XFS_DINODE_FMT_EXTENTS &&
		    ip->i_d.di_nextents > XFS_BMAP_EXT_MAXRECS(ip->i_mount))
			rval |= xfs_bmap_extents_to_btree(ip->i_transp, ip,
				first, flist, &cur, 1, lowspace);
		temp = xfs_bmap_worst_indlen(ip, temp);
		temp2 = xfs_bmap_worst_indlen(ip, temp2);
		diff = temp + temp2 - STARTBLOCKVAL(PREV.br_startblock) -
			(cur ? cur->bc_private.b.allocated : 0);
		if (diff > 0 &&
		    xfs_mod_incore_sb(ip->i_mount, XFS_SB_FDBLOCKS, -diff)) {
			/*
			 * Ick gross gag me with a spoon.
			 */
			ASSERT(0);	/* want to see if this ever happens! */
			while (diff > 0) {
				if (temp) {
					temp--;
					diff--;
					if (!diff ||
					    !xfs_mod_incore_sb(ip->i_mount,
						    XFS_SB_FDBLOCKS, -diff))
						break;
				}
				if (temp2) {
					temp2--;
					diff--;
					if (!diff ||
					    !xfs_mod_incore_sb(ip->i_mount,
						    XFS_SB_FDBLOCKS, -diff))
						break;
				}
			}
		}
		base = ip->i_u1.iu_extents;
		ep = &base[idx];
		xfs_bmbt_set_startblock(ep, NULLSTARTBLOCK(temp));
		xfs_bmap_trace_post_update(fname, "0", ip, idx);
		xfs_bmap_trace_pre_update(fname, "0", ip, idx + 2);
		xfs_bmbt_set_startblock(ep + 2, NULLSTARTBLOCK(temp2));
		xfs_bmap_trace_post_update(fname, "0", ip, idx + 2);
		*dnew = temp + temp2;
		break;

	case MASK3(LEFT_FILLING, LEFT_CONTIG, RIGHT_CONTIG):
	case MASK3(RIGHT_FILLING, LEFT_CONTIG, RIGHT_CONTIG):
	case MASK2(LEFT_FILLING, RIGHT_CONTIG):
	case MASK2(RIGHT_FILLING, LEFT_CONTIG):
	case MASK2(LEFT_CONTIG, RIGHT_CONTIG):
	case MASK(LEFT_CONTIG):
	case MASK(RIGHT_CONTIG):
		/*
		 * These cases are all impossible.
		 */
		ASSERT(0);
	}
	*curp = cur;
	kmem_check();
	return rval;
#undef	LEFT
#undef	RIGHT
#undef	PREV
#undef	MASK
#undef	MASK2
#undef	MASK3
#undef	MASK4
#undef	STATE_SET
#undef	STATE_TEST
#undef	STATE_SET_TEST
#undef	SWITCH_STATE
}

/*
 * Called by xfs_bmap_add_extent to handle cases converting a hole
 * to a delayed allocation.
 */
STATIC int				/* inode logging flags */
xfs_bmap_add_extent_hole_delay(
	xfs_inode_t		*ip,	/* incore inode pointer */
	xfs_extnum_t		idx,	/* extent number to update/insert */
	xfs_btree_cur_t		*cur,	/* if null, not a btree */
	xfs_bmbt_irec_t		*new)	/* new data to put in extent list */
{
	xfs_bmbt_rec_t		*base;	/* base of extent entry list */
	xfs_bmbt_rec_t		*ep;	/* extent list entry for idx */
#if defined(DEBUG) && !defined(SIM)
	static char		fname[] = "xfs_bmap_add_extent_hole_delay";
#endif
	xfs_bmbt_irec_t		left;	/* left neighbor extent entry */
	xfs_extlen_t		newlen;	/* new indirect size */
	xfs_extlen_t		oldlen;	/* old indirect size */
	xfs_bmbt_irec_t		right;	/* right neighbor extent entry */
	int			state;	/* state bits, accessed thru macros */
	xfs_extlen_t		temp;	/* temp for indirect calculations */
	enum {				/* bit number definitions for state */
		LEFT_CONTIG,	RIGHT_CONTIG,
		LEFT_DELAY,	RIGHT_DELAY,
		LEFT_VALID,	RIGHT_VALID
	};

#define	MASK(b)			(1 << (b))
#define	MASK2(a,b)		(MASK(a) | MASK(b))
#define	STATE_SET(b,v)		((v) ? (state |= MASK(b)) : (state &= ~MASK(b)))
#define	STATE_TEST(b)		(state & MASK(b))
#define	STATE_SET_TEST(b,v)	((v) ? ((state |= MASK(b)), 1) : \
				       ((state &= ~MASK(b)), 0))
#define	SWITCH_STATE		(state & MASK2(LEFT_CONTIG, RIGHT_CONTIG))

	base = ip->i_u1.iu_extents;
	ep = &base[idx];
	ASSERT(ISNULLSTARTBLOCK(new->br_startblock));
	/*
	 * Check and set flags if this segment has a left neighbor
	 */
	if (STATE_SET_TEST(LEFT_VALID, idx > 0)) {
		xfs_bmbt_get_all(ep - 1, &left);
		STATE_SET(LEFT_DELAY, ISNULLSTARTBLOCK(left.br_startblock));
	}
	/*
	 * Check and set flags if the current (right) segment exists.
	 * If it doesn't exist, we're converting the hole at end-of-file.
	 */
	if (STATE_SET_TEST(RIGHT_VALID,
		idx < ip->i_bytes / sizeof(xfs_bmbt_rec_t))) {
		xfs_bmbt_get_all(ep, &right);
		STATE_SET(RIGHT_DELAY, ISNULLSTARTBLOCK(right.br_startblock));
	}
	/*
	 * Set contiguity flags on the left and right neighbors.
	 * Don't let extents get too large, even if the pieces are contiguous.
	 */
	STATE_SET(LEFT_CONTIG, 
		STATE_TEST(LEFT_VALID) && STATE_TEST(LEFT_DELAY) &&
		left.br_startoff + left.br_blockcount == new->br_startoff &&
		left.br_blockcount + new->br_blockcount <= MAXEXTLEN);
	STATE_SET(RIGHT_CONTIG,
		STATE_TEST(RIGHT_VALID) && STATE_TEST(RIGHT_DELAY) &&
		new->br_startoff + new->br_blockcount == right.br_startoff &&
		new->br_blockcount + right.br_blockcount <= MAXEXTLEN &&
		(!STATE_TEST(LEFT_CONTIG) ||
		 (left.br_blockcount + new->br_blockcount +
		     right.br_blockcount <= MAXEXTLEN)));
	/*
	 * Switch out based on the contiguity flags.
	 */
	switch (SWITCH_STATE) {

	case MASK2(LEFT_CONTIG, RIGHT_CONTIG):
		/*
		 * New allocation is contiguous with delayed allocations
		 * on the left and on the right.
		 * Merge all three into a single extent list entry.
		 */
		temp = left.br_blockcount + new->br_blockcount +
			right.br_blockcount;
		xfs_bmap_trace_pre_update(fname, "LC|RC", ip, idx - 1);
		xfs_bmbt_set_blockcount(ep - 1, temp);
		oldlen = STARTBLOCKVAL(left.br_startblock) +
			STARTBLOCKVAL(new->br_startblock) +
			STARTBLOCKVAL(right.br_startblock);
		newlen = xfs_bmap_worst_indlen(ip, temp);
		xfs_bmbt_set_startblock(ep - 1, NULLSTARTBLOCK(newlen));
		xfs_bmap_trace_post_update(fname, "LC|RC", ip, idx - 1);
		xfs_bmap_trace_delete(fname, "LC|RC", ip, idx, 1);
		xfs_bmap_delete_exlist(ip, idx, 1);
		ip->i_lastex = idx - 1;
		break;

	case MASK(LEFT_CONTIG):
		/*
		 * New allocation is contiguous with a delayed allocation
		 * on the left.
		 * Merge the new allocation with the left neighbor.
		 */
		temp = left.br_blockcount + new->br_blockcount;
		xfs_bmap_trace_pre_update(fname, "LC", ip, idx - 1);
		xfs_bmbt_set_blockcount(ep - 1, temp);
		oldlen = STARTBLOCKVAL(left.br_startblock) +
			STARTBLOCKVAL(new->br_startblock);
		newlen = xfs_bmap_worst_indlen(ip, temp);
		xfs_bmbt_set_startblock(ep - 1, NULLSTARTBLOCK(newlen));
		xfs_bmap_trace_post_update(fname, "LC", ip, idx - 1);
		ip->i_lastex = idx - 1;
		break;

	case MASK(RIGHT_CONTIG):
		/*
		 * New allocation is contiguous with a delayed allocation
		 * on the right.
		 * Merge the new allocation with the right neighbor.
		 */
		temp = new->br_blockcount + right.br_blockcount;
		xfs_bmap_trace_pre_update(fname, "RC", ip, idx);
		xfs_bmbt_set_startoff(ep, new->br_startoff);
		xfs_bmbt_set_blockcount(ep, temp);
		oldlen = STARTBLOCKVAL(new->br_startblock) +
			STARTBLOCKVAL(right.br_startblock);
		newlen = xfs_bmap_worst_indlen(ip, temp);
		xfs_bmbt_set_startblock(ep, NULLSTARTBLOCK(newlen));
		xfs_bmap_trace_post_update(fname, "RC", ip, idx);
		ip->i_lastex = idx;
		break;

	case 0:
		/*
		 * New allocation is not contiguous with another
		 * delayed allocation.
		 * Insert a new entry.
		 */
		oldlen = newlen = 0;
		xfs_bmap_trace_insert(fname, "0", ip, idx, 1, new, NULL);
		xfs_bmap_insert_exlist(ip, idx, 1, new);
		ip->i_lastex = idx;
		break;
	}
	if (oldlen != newlen) {
		ASSERT(oldlen > newlen);
		xfs_mod_incore_sb(ip->i_mount, XFS_SB_FDBLOCKS,
			oldlen - newlen);
	}
	kmem_check();
	return 0;
#undef	MASK
#undef	MASK2
#undef	STATE_SET
#undef	STATE_TEST
#undef	STATE_SET_TEST
#undef	SWITCH_STATE
}

/*
 * Called by xfs_bmap_add_extent to handle cases converting a hole
 * to a real allocation.
 */
STATIC int				/* inode logging flags */
xfs_bmap_add_extent_hole_real(
	xfs_inode_t		*ip,	/* incore inode pointer */
	xfs_extnum_t		idx,	/* extent number to update/insert */
	xfs_btree_cur_t		*cur,	/* if null, not a btree */
	xfs_bmbt_irec_t		*new)	/* new data to put in extent list */
{
	xfs_bmbt_rec_t		*ep;	/* pointer to extent entry ins. point */
#if defined(DEBUG) && !defined(SIM)
	static char		fname[] = "xfs_bmap_add_extent_hole_real";
#endif
	xfs_bmbt_irec_t		left;	/* left neighbor extent entry */
	xfs_bmbt_irec_t		right;	/* right neighbor extent entry */
	int			state;	/* state bits, accessed thru macros */
	enum {				/* bit number definitions for state */
		LEFT_CONTIG,	RIGHT_CONTIG,
		LEFT_DELAY,	RIGHT_DELAY,
		LEFT_VALID,	RIGHT_VALID
	};

#define	MASK(b)			(1 << (b))
#define	MASK2(a,b)		(MASK(a) | MASK(b))
#define	STATE_SET(b,v)		((v) ? (state |= MASK(b)) : (state &= ~MASK(b)))
#define	STATE_TEST(b)		(state & MASK(b))
#define	STATE_SET_TEST(b,v)	((v) ? ((state |= MASK(b)), 1) : \
				       ((state &= ~MASK(b)), 0))
#define	SWITCH_STATE		(state & MASK2(LEFT_CONTIG, RIGHT_CONTIG))

	ASSERT(idx <= ip->i_bytes / sizeof(xfs_bmbt_rec_t));
	ep = &ip->i_u1.iu_extents[idx];
	/*
	 * Check and set flags if this segment has a left neighbor.
	 */
	if (STATE_SET_TEST(LEFT_VALID, idx > 0)) {
		xfs_bmbt_get_all(ep - 1, &left);
		STATE_SET(LEFT_DELAY, ISNULLSTARTBLOCK(left.br_startblock));
	}
	/*
	 * Check and set flags if this segment has a current value.
	 * Not true if we're inserting into the "hole" at eof.
	 */
	if (STATE_SET_TEST(RIGHT_VALID,
			   idx < ip->i_bytes / sizeof(xfs_bmbt_rec_t))) {
		xfs_bmbt_get_all(ep, &right);
		STATE_SET(RIGHT_DELAY, ISNULLSTARTBLOCK(right.br_startblock));
	}
	/*
	 * We're inserting a real allocation between "left" and "right".
	 * Set the contiguity flags.  Don't let extents get too large.
	 */
	STATE_SET(LEFT_CONTIG, 
		STATE_TEST(LEFT_VALID) && !STATE_TEST(LEFT_DELAY) &&
		left.br_startoff + left.br_blockcount == new->br_startoff &&
		left.br_startblock + left.br_blockcount == new->br_startblock &&
		left.br_blockcount + new->br_blockcount <= MAXEXTLEN);
	STATE_SET(RIGHT_CONTIG,
		STATE_TEST(RIGHT_VALID) && !STATE_TEST(RIGHT_DELAY) &&
		new->br_startoff + new->br_blockcount == right.br_startoff &&
		new->br_startblock + new->br_blockcount ==
		    right.br_startblock &&
		new->br_blockcount + right.br_blockcount <= MAXEXTLEN &&
		(!STATE_TEST(LEFT_CONTIG) ||
		 left.br_blockcount + new->br_blockcount +
		     right.br_blockcount <= MAXEXTLEN));

	/*
	 * Select which case we're in here, and implement it.
	 */
	switch (SWITCH_STATE) {

	case MASK2(LEFT_CONTIG, RIGHT_CONTIG):
		/*
		 * New allocation is contiguous with real allocations on the
		 * left and on the right.
		 * Merge all three into a single extent list entry.
		 */
		xfs_bmap_trace_pre_update(fname, "LC|RC", ip, idx - 1);
		xfs_bmbt_set_blockcount(ep - 1,
			left.br_blockcount + new->br_blockcount +
			right.br_blockcount);
		xfs_bmap_trace_post_update(fname, "LC|RC", ip, idx - 1);
		xfs_bmap_trace_delete(fname, "LC|RC", ip,
			idx, 1);
		xfs_bmap_delete_exlist(ip, idx, 1);
		ip->i_lastex = idx - 1;
		ip->i_d.di_nextents--;
		kmem_check();
		if (cur == NULL)
			return XFS_ILOG_CORE | XFS_ILOG_EXT;
		xfs_bmbt_lookup_eq(cur, right.br_startoff, right.br_startblock,
			right.br_blockcount);
		xfs_bmbt_delete(cur);
		xfs_bmbt_decrement(cur, 0);
		xfs_bmbt_update(cur, left.br_startoff, left.br_startblock,
			left.br_blockcount + new->br_blockcount +
			right.br_blockcount);
		kmem_check();
		return XFS_ILOG_CORE;

	case MASK(LEFT_CONTIG):
		/*
		 * New allocation is contiguous with a real allocation
		 * on the left.
		 * Merge the new allocation with the left neighbor.
		 */
		xfs_bmap_trace_pre_update(fname, "LC", ip, idx - 1);
		xfs_bmbt_set_blockcount(ep - 1,
			left.br_blockcount + new->br_blockcount);
		xfs_bmap_trace_post_update(fname, "LC", ip, idx - 1);
		ip->i_lastex = idx - 1;
		kmem_check();
		if (cur == NULL)
			return XFS_ILOG_EXT;
		xfs_bmbt_lookup_eq(cur, left.br_startoff, left.br_startblock,
			left.br_blockcount);
		xfs_bmbt_update(cur, left.br_startoff, left.br_startblock,
			left.br_blockcount + new->br_blockcount);
		kmem_check();
		return 0;

	case MASK(RIGHT_CONTIG):
		/*
		 * New allocation is contiguous with a real allocation
		 * on the right.
		 * Merge the new allocation with the right neighbor.
		 */
		xfs_bmap_trace_pre_update(fname, "RC", ip, idx);
		xfs_bmbt_set_startoff(ep, new->br_startoff);
		xfs_bmbt_set_startblock(ep, new->br_startblock);
		xfs_bmbt_set_blockcount(ep,
			new->br_blockcount + right.br_blockcount);
		xfs_bmap_trace_post_update(fname, "RC", ip, idx);
		ip->i_lastex = idx;
		kmem_check();
		if (cur == NULL)
			return XFS_ILOG_EXT;
		xfs_bmbt_lookup_eq(cur, right.br_startoff, right.br_startblock,
			right.br_blockcount);
		xfs_bmbt_update(cur, new->br_startoff, new->br_startblock,
			new->br_blockcount + right.br_blockcount);
		kmem_check();
		return 0;

	case 0:
		/*
		 * New allocation is not contiguous with another
		 * real allocation.
		 * Insert a new entry.
		 */
		xfs_bmap_trace_insert(fname, "0", ip, idx, 1, new, NULL);
		xfs_bmap_insert_exlist(ip, idx, 1, new);
		ip->i_lastex = idx;
		ip->i_d.di_nextents++;
		kmem_check();
		if (cur == NULL)
			return XFS_ILOG_CORE | XFS_ILOG_EXT;
		xfs_bmbt_lookup_eq(cur, new->br_startoff, new->br_startblock,
			new->br_blockcount);
		xfs_bmbt_insert(cur);
		kmem_check();
		return XFS_ILOG_CORE;
	}
#undef	MASK
#undef	MASK2
#undef	STATE_SET
#undef	STATE_TEST
#undef	STATE_SET_TEST
#undef	SWITCH_STATE
}

/*
 * xfs_bmap_alloc is called by xfs_bmapi to allocate an extent for a file.
 * It figures out where to ask the underlying allocator to put the new extent.
 */
STATIC void
xfs_bmap_alloc(
	xfs_bmalloca_t	*ap)		/* bmap alloc argument struct */
{
	xfs_fsblock_t	adjust;		/* adjustment to block numbers */
	xfs_agnumber_t	fb_agno;	/* ag number of ap->firstblock */
	xfs_mount_t	*mp;		/* mount point structure */
	int		nullfb;		/* true if ap->firstblock isn't set */
	xfs_extlen_t	prod;		/* product factor for allocators */
	int		rt;		/* true if inode is realtime */

	/*
	 * Set up variables.
	 */
	mp = ap->ip->i_mount;
	nullfb = ap->firstblock == NULLFSBLOCK;
	rt = ap->ip->i_d.di_flags & XFS_DIFLAG_REALTIME;
	fb_agno = nullfb ? NULLAGNUMBER : XFS_FSB_TO_AGNO(mp, ap->firstblock);
	ap->rval = rt ? 0 :
		(nullfb ? XFS_INO_TO_FSB(mp, ap->ip->i_ino) : ap->firstblock);
	/*
	 * If allocating at eof, and there's a previous real block,
	 * try to use it's last block as our starting point.
	 */
	if (ap->eof && ap->prevp->br_startoff != NULLFILEOFF &&
	    !ISNULLSTARTBLOCK(ap->prevp->br_startblock)) {
		ap->rval = ap->prevp->br_startblock + ap->prevp->br_blockcount;
		/*
		 * Adjust for the gap between prevp and us.
		 */
		adjust = ap->off -
			(ap->prevp->br_startoff + ap->prevp->br_blockcount);
		ap->rval += adjust;
		if (!adjust)
			adjust = 1;
		/*
		 * Reject adjustment if the resulting block is illegal.
		 */
		if ((!rt &&
		     (XFS_FSB_TO_AGNO(mp, ap->rval) !=
		       XFS_FSB_TO_AGNO(mp, ap->prevp->br_startblock) ||
		      XFS_FSB_TO_AGBNO(mp, ap->rval) >=
		       mp->m_sb.sb_agblocks)) ||
		    (rt && ap->rval >= mp->m_sb.sb_rextents))
			ap->rval -= adjust;
	}
	/*
	 * If not at eof, then compare the two neighbor blocks.
	 * Figure out whether either one gives us a good starting point,
	 * and pick the better one.
	 */
	else if (!ap->eof) {
		xfs_fsblock_t	gotbno;		/* right side block number */
		xfs_fsblock_t	gotdiff;	/* right side difference */
		xfs_fsblock_t	prevbno;	/* left side block number */
		xfs_fsblock_t	prevdiff;	/* left side difference */

		/*
		 * If there's a previous (left) block, select a requested
		 * start block based on it.
		 */
		if (ap->prevp->br_startoff != NULLFILEOFF &&
		    !ISNULLSTARTBLOCK(ap->prevp->br_startblock)) {
			/*
			 * Calculate gap to end of previous block.
			 */
			prevdiff = ap->off -
				(ap->prevp->br_startoff +
				 ap->prevp->br_blockcount);
			/*
			 * Figure the startblock based on the previous block's
			 * end and the gap size.
			 */
			prevbno = ap->prevp->br_startblock +
				ap->prevp->br_blockcount + prevdiff;
			adjust = prevdiff ? prevdiff : 1;
			/*
			 * Heuristic!
			 * If the gap is large relative to the piece we're
			 * allocating, or using it gives us an illegal block
			 * number, then just use the end of the previous block.
			 */
			if (prevdiff > 2 * ap->alen ||
			    (!rt &&
			     (XFS_FSB_TO_AGNO(mp, prevbno) !=
			      XFS_FSB_TO_AGNO(mp, ap->prevp->br_startblock) ||
			      XFS_FSB_TO_AGBNO(mp, prevbno) >=
			      mp->m_sb.sb_agblocks)) ||
			    (rt && prevbno >= mp->m_sb.sb_rextents)) {
				prevbno -= adjust;
				prevdiff +- adjust;
			}
			/*
			 * If the firstblock forbids it, can't use it, 
			 * must use default.
			 */
			if (!rt && !nullfb &&
			    XFS_FSB_TO_AGNO(mp, prevbno) != fb_agno)
				prevbno = NULLFSBLOCK;
		}
		/*
		 * No previous block, just default.
		 */
		else
			prevbno = NULLFSBLOCK;
		/*
		 * If there's a following (right) block, select a requested
		 * start block based on it.
		 */
		if (!ISNULLSTARTBLOCK(ap->gotp->br_startblock)) {
			/*
			 * Calculate gap to start of next block.
			 */
			gotdiff = ap->gotp->br_startoff - ap->off;
			/*
			 * Figure the startblock based on the next block's
			 * start and the gap size.
			 */
			gotbno = ap->gotp->br_startblock - gotdiff;
			/*
			 * Heuristic!
			 * If the gap is large relative to the piece we're
			 * allocating, or using it gives us an illegal block
			 * number, then just use the start of the next block
			 * offset by our length.
			 */
			if (gotdiff > 2 * ap->alen ||
			    (!rt &&
			     (XFS_FSB_TO_AGNO(mp, gotbno) !=
			      XFS_FSB_TO_AGNO(mp, ap->gotp->br_startblock))) ||
			    (rt && gotbno >= mp->m_sb.sb_rextents)) {
				gotbno = ap->gotp->br_startblock - ap->alen;
				gotdiff = ap->alen;
				if ((!rt &&
				     (XFS_FSB_TO_AGNO(mp, gotbno) !=
				      XFS_FSB_TO_AGNO(mp,
					      ap->gotp->br_startblock))) ||
				    (rt && gotbno >= mp->m_sb.sb_rextents)) {
					gotbno = XFS_AGB_TO_FSB(mp,
						XFS_FSB_TO_AGNO(mp,
						ap->gotp->br_startblock), 0);
					gotdiff = ap->gotp->br_startblock -
						gotbno;
				}
			}
			/*
			 * If the firstblock forbids it, can't use it, 
			 * must use default.
			 */
			if (!rt && !nullfb &&
			    XFS_FSB_TO_AGNO(mp, gotbno) != fb_agno)
				gotbno = NULLFSBLOCK;
		}
		/*
		 * No next block, just default.
		 */
		else
			gotbno = NULLFSBLOCK;
		/*
		 * If both valid, pick the better one, else the only good
		 * one, else ap->rval is already set (to 0 or the inode block).
		 */
		if (prevbno != NULLFSBLOCK && gotbno != NULLFSBLOCK)
			ap->rval = prevdiff <= gotdiff ? prevbno : gotbno;
		else if (prevbno != NULLFSBLOCK)
			ap->rval = prevbno;
		else if (gotbno != NULLFSBLOCK)
			ap->rval = gotbno;
	}
	/*
	 * If allowed, use ap->rval; otherwise must use firstblock since
	 * it's in the right allocation group.
	 */
	if (nullfb || rt || XFS_FSB_TO_AGNO(mp, ap->rval) == fb_agno)
		;
	else
		ap->rval = ap->firstblock;
	/*
	 * Realtime allocation, done through xfs_rtallocate_extent.
	 */
	if (rt) {
		xfs_extlen_t	ralen;
		xfs_alloctype_t	type;		/* allocation type flag */

		ASSERT(ap->userdata == 1);
		type = ap->rval == 0 ?
			XFS_ALLOCTYPE_ANY_AG : XFS_ALLOCTYPE_NEAR_BNO;
		if (ap->ip->i_d.di_extsize)
			prod = ap->ip->i_d.di_extsize / mp->m_sb.sb_rextsize;
		else
			prod = 1;
		ap->alen = (ap->alen + mp->m_sb.sb_rextsize - 1) /
			mp->m_sb.sb_rextsize;
		ap->rval /= mp->m_sb.sb_rextsize;
		ap->rval = xfs_rtallocate_extent(ap->tp, ap->rval, 1, ap->alen,
			&ralen, type, ap->wasdel, prod);
		if (ap->rval != NULLFSBLOCK) {
			ap->rval *= mp->m_sb.sb_rextsize;
			ralen *= mp->m_sb.sb_rextsize;
			ap->alen = ralen;
			ap->ip->i_d.di_nblocks += ralen;
			xfs_trans_log_inode(ap->tp, ap->ip, XFS_ILOG_CORE);
			if (ap->wasdel)
				ap->ip->i_delayed_blks -= ralen;
		} else
			ap->alen = 0;
	}
	/*
	 * Normal allocation, done through xfs_alloc_vextent.
	 */
	else {
		xfs_alloc_arg_t	*args;

		args = xfs_alloc_arg_alloc();
		args->tp = ap->tp;
		args->mp = mp;
		args->fsbno = ap->rval;
		args->minlen = ap->minlen;
		args->maxlen = ap->alen;
		if (nullfb) {
			args->type = XFS_ALLOCTYPE_START_BNO;
			args->total = ap->total;
		}
		else if (ap->low) {
			args->type = XFS_ALLOCTYPE_FIRST_AG;
			args->total = 1;
		} else {
			args->type = XFS_ALLOCTYPE_NEAR_BNO;
			args->total = ap->total;
		}
		if (ap->ip->i_d.di_extsize) {
			args->prod = ap->ip->i_d.di_extsize;
			args->mod = 0;
		} else if (mp->m_sb.sb_blocksize >= NBPP) {
			args->prod = 1;
			args->mod = 0;
		} else {
			args->prod = NBPP >> mp->m_sb.sb_blocklog;
			if (args->mod = ap->off % args->prod)
				args->mod = args->prod - args->mod;
		}
		args->minleft = ap->minleft;
		args->wasdel = ap->wasdel;
		args->isfl = 0;
		args->userdata = ap->userdata;
		xfs_alloc_vextent(args);
		if (args->fsbno == NULLFSBLOCK && nullfb) {
			args->fsbno = 0;
			args->minlen = 1;
			args->type = XFS_ALLOCTYPE_FIRST_AG;
			args->total = 1;
			args->minleft = 0;
			xfs_alloc_vextent(args);
			ap->rval = args->fsbno;
			ap->low = 1;
		} else
			ap->rval = args->fsbno;
		if (ap->rval != NULLFSBLOCK) {
			ap->firstblock = ap->rval;
			ap->alen = args->len;
			ap->ip->i_d.di_nblocks += args->len;
			xfs_trans_log_inode(ap->tp, ap->ip, XFS_ILOG_CORE);
			if (ap->wasdel)
				ap->ip->i_delayed_blks -= args->len;
		} else
			ap->alen = 0;
		xfs_alloc_arg_free(args);
	}
	kmem_check();
}

/*
 * Transform a btree format file with only one leaf node, where the
 * extents list will fit in the inode, into an extents format file.
 * Since the extent list is already in-core, all we have to do is
 * give up the space for the btree root and pitch the leaf block.
 */
STATIC int				/* inode logging flags */
xfs_bmap_btree_to_extents(
	xfs_trans_t		*tp,	/* transaction pointer */
	xfs_inode_t		*ip,	/* incore inode pointer */
	xfs_btree_cur_t		*cur)	/* btree cursor */
{
	xfs_bmbt_block_t	*cblock;/* child btree block */
	xfs_fsblock_t		cbno;	/* child block number */
	buf_t			*cbp;	/* child block's buffer */
	xfs_mount_t		*mp;	/* mount point structure */
	xfs_bmbt_ptr_t		*pp;	/* ptr to block address */
	xfs_bmbt_block_t	*rblock;/* root btree block */

	ASSERT(ip->i_flags & XFS_IEXTENTS);
	ASSERT(ip->i_d.di_format == XFS_DINODE_FMT_BTREE);
	rblock = ip->i_broot;
	ASSERT(rblock->bb_level == 1);
	ASSERT(rblock->bb_numrecs == 1);
	ASSERT(XFS_BMAP_BROOT_MAXRECS(ip->i_broot_bytes) == 1);
	mp = ip->i_mount;
	pp = XFS_BMAP_BROOT_PTR_ADDR(rblock, 1, ip->i_broot_bytes);
	xfs_btree_check_lptr(cur, *pp, 1);
	cbno = *pp;
	cbp = xfs_btree_read_bufl(mp, tp, cbno, 0);
	cblock = XFS_BUF_TO_BMBT_BLOCK(cbp);
	ASSERT(cblock->bb_level == 0);
	xfs_bmap_add_free(cbno, 1, cur->bc_private.b.flist, mp);
	ip->i_d.di_nblocks--;
	xfs_trans_binval(tp, cbp);
	if (cur->bc_bufs[0] == cbp)
		cur->bc_bufs[0] = NULL;
	xfs_iroot_realloc(ip, -1);
	ASSERT(ip->i_broot == NULL);
	ASSERT((ip->i_flags & XFS_IBROOT) == 0);
	ip->i_d.di_format = XFS_DINODE_FMT_EXTENTS;
	return XFS_ILOG_CORE | XFS_ILOG_EXT;
}

#ifdef DEBUG
/*
 * Check that the extents list for the inode ip is in the right order.
 */
STATIC void
xfs_bmap_check_extents(
	xfs_inode_t		*ip)		/* incore inode pointer */
{
	xfs_bmbt_rec_t		*base;		/* base of extents list */
	xfs_bmbt_rec_t		*ep;		/* current extent entry */
	xfs_extnum_t		nextents;	/* number of extents in list */

	ASSERT(ip->i_flags & XFS_IEXTENTS);
	base = ip->i_u1.iu_extents;
	nextents = ip->i_bytes / sizeof(xfs_bmbt_rec_t);
	for (ep = base; ep < &base[nextents - 1]; ep++)
		xfs_btree_check_rec(XFS_BTNUM_BMAP, (void *)ep,
			(void *)(ep + 1));
}
#endif

/*
 * Called by xfs_bmapi to update extent list structure and the btree
 * after removing space (or undoing a delayed allocation).
 */
STATIC int				/* inode logging flags */
xfs_bmap_del_extent(
	xfs_inode_t		*ip,	/* incore inode pointer */
	xfs_extnum_t		idx,	/* extent number to update/delete */
	xfs_bmap_free_t		*flist,	/* list of extents to be freed */
	xfs_btree_cur_t		*cur,	/* if null, not a btree */
	xfs_bmbt_irec_t		*del)	/* data to remove from extent list */
{
	xfs_extlen_t		da_new;	/* new delay-alloc indirect blocks */
	xfs_extlen_t		da_old;	/* old delay-alloc indirect blocks */
	xfs_fsblock_t		del_endblock;	/* first block past del */
	xfs_fsblock_t		del_endoff;	/* first offset past del */
	int			delay;	/* current block is delayed allocated */
	xfs_bmbt_rec_t		*ep;	/* current extent entry pointer */
	int			flags;	/* inode logging flags */
#if defined(DEBUG) && !defined(SIM)
	static char		fname[] = "xfs_bmap_del_extent";
#endif
	xfs_bmbt_irec_t		got;	/* current extent entry */
	xfs_fsblock_t		got_endoff;	/* first offset past got */
	xfs_bmbt_irec_t		new;	/* new record to be inserted */
	xfs_extnum_t		nextents;	/* number of extents in list */
	xfs_extlen_t		temp;	/* for indirect length calculations */
	xfs_extlen_t		temp2;	/* for indirect length calculations */

	nextents = ip->i_bytes / sizeof(xfs_bmbt_rec_t);
	ASSERT(idx >= 0 && idx < nextents);
	ep = &ip->i_u1.iu_extents[idx];
	xfs_bmbt_get_all(ep, &got);
	del_endoff = del->br_startoff + del->br_blockcount;
	got_endoff = got.br_startoff + got.br_blockcount;
	delay = ISNULLSTARTBLOCK(got.br_startblock);
	ASSERT(ISNULLSTARTBLOCK(del->br_startblock) == delay);
	flags = 0;
	/*
	 * If deleting a real allocation, must free up the disk space.
	 */
	if (!delay) {
		/*
		 * Realtime allocation.  Free it and update di_nblocks.
		 */
		if (ip->i_d.di_flags & XFS_DIFLAG_REALTIME) {
			xfs_fsblock_t	bno;
			xfs_extlen_t	len;
			xfs_mount_t	*mp;
	
			mp = ip->i_mount;

			ASSERT((del->br_blockcount % mp->m_sb.sb_rextsize) == 0);
			ASSERT((del->br_startblock % mp->m_sb.sb_rextsize) == 0);
			
			bno = del->br_startblock / mp->m_sb.sb_rextsize;
			len = del->br_blockcount / mp->m_sb.sb_rextsize;

			xfs_rtfree_extent(ip->i_transp, bno, len);
			ip->i_d.di_nblocks -=
				del->br_blockcount * mp->m_sb.sb_rextsize;
		}
		/*
		 * Ordinary allocation.  Add it to list of extents to be
		 * freed at the end of the transaction, and update di_nblocks.
		 */
		else {
			xfs_bmap_add_free(del->br_startblock,
				del->br_blockcount, flist, ip->i_mount);
			ip->i_d.di_nblocks -= del->br_blockcount;
		}
		flags = XFS_ILOG_CORE;
		/*
		 * Set up del_endblock and cur for later.
		 */
		del_endblock = del->br_startblock + del->br_blockcount;
		if (cur)
			xfs_bmbt_lookup_eq(cur, got.br_startoff,
				got.br_startblock, got.br_blockcount);
		da_old = da_new = 0;
	} else {
		da_old = STARTBLOCKVAL(got.br_startblock);
		da_new = 0;
	}
	/*
	 * Set flag value to use in switch statement.
	 * Left-contig is 2, right-contig is 1.
	 */
	switch (((got.br_startoff == del->br_startoff) << 1) |
		(got_endoff == del_endoff)) {

	case 3:
		/*
		 * Matches the whole extent.  Delete the entry.
		 */
		xfs_bmap_trace_delete(fname, "3", ip, idx, 1);
		xfs_bmap_delete_exlist(ip, idx, 1);
		ip->i_lastex = idx;
		if (delay)
			break;
		ip->i_d.di_nextents--;
		flags |= XFS_ILOG_CORE;
		if (!cur) {
			flags |= XFS_ILOG_EXT;
			break;
		}
		xfs_bmbt_delete(cur);
		break;

	case 2:
		/*
		 * Deleting the first part of the extent.
		 */
		xfs_bmap_trace_pre_update(fname, "2", ip, idx);
		xfs_bmbt_set_startoff(ep, del_endoff);
		temp = got.br_blockcount - del->br_blockcount;
		xfs_bmbt_set_blockcount(ep, temp);
		ip->i_lastex = idx;
		if (delay) {
			temp = xfs_bmap_worst_indlen(ip, temp);
			xfs_bmbt_set_startblock(ep, NULLSTARTBLOCK(temp));
			xfs_bmap_trace_post_update(fname, "2", ip, idx);
			da_new = temp;
			break;
		}
		xfs_bmbt_set_startblock(ep, del_endblock);
		xfs_bmap_trace_post_update(fname, "2", ip, idx);
		if (!cur) {
			flags |= XFS_ILOG_EXT;
			break;
		}
		xfs_bmbt_update(cur, del_endoff, del_endblock,
			got.br_blockcount - del->br_blockcount);
		break;

	case 1:
		/*
		 * Deleting the last part of the extent.
		 */
		temp = got.br_blockcount - del->br_blockcount;
		xfs_bmap_trace_pre_update(fname, "1", ip, idx);
		xfs_bmbt_set_blockcount(ep, temp);
		ip->i_lastex = idx;
		if (delay) {
			temp = xfs_bmap_worst_indlen(ip, temp);
			xfs_bmbt_set_startblock(ep, NULLSTARTBLOCK(temp));
			xfs_bmap_trace_post_update(fname, "1", ip, idx);
			da_new = temp;
			break;
		}
		xfs_bmap_trace_post_update(fname, "1", ip, idx);
		if (!cur) {
			flags |= XFS_ILOG_EXT;
			break;
		}
		xfs_bmbt_update(cur, got.br_startoff, got.br_startblock,
			got.br_blockcount - del->br_blockcount);
		break;
	
	case 0:
		/*
		 * Deleting the middle of the extent.
		 */
		temp = del->br_startoff - got.br_startoff;
		xfs_bmap_trace_pre_update(fname, "0", ip, idx);
		xfs_bmbt_set_blockcount(ep, temp);
		new.br_startoff = del_endoff;
		temp2 = got_endoff - del_endoff;
		new.br_blockcount = temp2;
		if (!delay) {
			new.br_startblock = del_endblock;
			if (cur) {
				xfs_bmbt_update(cur, got.br_startoff,
					got.br_startblock,
					del->br_startoff - got.br_startoff);
				xfs_bmbt_increment(cur, 0);
				cur->bc_rec.b = new;
				xfs_bmbt_insert(cur);
			} else
				flags |= XFS_ILOG_EXT;
			flags |= XFS_ILOG_CORE;
			ip->i_d.di_nextents++;
		} else {
			temp = xfs_bmap_worst_indlen(ip, temp);
			xfs_bmbt_set_startblock(ep, NULLSTARTBLOCK(temp));
			temp2 = xfs_bmap_worst_indlen(ip, temp2);
			new.br_startblock = NULLSTARTBLOCK(temp2);
			da_new = temp + temp2;
			while (da_new > da_old) {
				if (temp) {
					temp--;
					da_new--;
					xfs_bmbt_set_startblock(ep,
						NULLSTARTBLOCK(temp));
				}
				if (da_new == da_old)
					break;
				if (temp2) {
					temp2--;
					da_new--;
					new.br_startblock = 
						NULLSTARTBLOCK(temp2);
				}
			}
		}
		xfs_bmap_trace_post_update(fname, "0", ip, idx);
		xfs_bmap_trace_insert(fname, "0", ip, idx + 1, 1, &new, NULL);
		xfs_bmap_insert_exlist(ip, idx + 1, 1, &new);
		ip->i_lastex = idx + 1;
		break;
	}
	/*
	 * Account for change in delayed indirect blocks.
	 */
	if (da_old != da_new) {
		ASSERT(da_old > da_new);
		xfs_mod_incore_sb(ip->i_mount, XFS_SB_FDBLOCKS,
			da_old - da_new);
	}
	kmem_check();
	return flags;
}

/*
 * Remove the entry "free" from the free item list.  Prev points to the
 * previous entry, unless "free" is the head of the list.
 */
STATIC void
xfs_bmap_del_free(
	xfs_bmap_free_t		*flist,	/* free item list header */
	xfs_bmap_free_item_t	*prev,	/* previous item on list, if any */
	xfs_bmap_free_item_t	*free)	/* list item to be freed */
{
	if (prev)
		prev->xbfi_next = free->xbfi_next;
	else
		flist->xbf_first = free->xbfi_next;
	flist->xbf_count--;
	kmem_zone_free(xfs_bmap_free_item_zone, free);
	kmem_check();
}

/*
 * Remove count entries from the extents array for inode "ip", starting
 * at index "idx".  Copies the remaining items down over the deleted ones,
 * and gives back the excess memory.
 */
STATIC void
xfs_bmap_delete_exlist(
	xfs_inode_t	*ip,		/* incore inode pointer */
	xfs_extnum_t	idx,		/* starting delete index */
	xfs_extnum_t	count)		/* count of items to delete */
{
	xfs_bmbt_rec_t	*base;		/* base of extent list */
	xfs_extnum_t	from;		/* from counter in copy */
	xfs_extnum_t	nextents;	/* number of extents in list after */
	xfs_extnum_t	to;		/* to counter in copy */

	ASSERT(ip->i_flags & XFS_IEXTENTS);
	base = ip->i_u1.iu_extents;
	nextents = ip->i_bytes / sizeof(xfs_bmbt_rec_t) - count;
	for (to = idx, from = to + count; to < nextents; from++, to++)
		base[to] = base[from];
	xfs_iext_realloc(ip, -count);
	kmem_check();
}

/*
 * Convert an extents-format file into a btree-format file.
 * The new file will have a root block (in the inode) and a single child block.
 */
STATIC int					/* inode logging flags */
xfs_bmap_extents_to_btree(
	xfs_trans_t		*tp,		/* transaction pointer */
	xfs_inode_t		*ip,		/* incore inode pointer */
	xfs_fsblock_t		*firstblock,	/* first-block-allocated */
	xfs_bmap_free_t		*flist,		/* blocks freed in xaction */
	xfs_btree_cur_t		**curp,		/* cursor returned to caller */
	int			wasdel,		/* converting a delayed alloc */
	int			lowspace)	/* running in low-space mode */
{
	xfs_bmbt_block_t	*ablock;
	buf_t			*abp;
	xfs_alloc_arg_t		*args;
	xfs_bmbt_rec_t		*arp;
	xfs_bmbt_block_t	*block;
	xfs_btree_cur_t		*cur;
	xfs_bmbt_rec_t		*ep;
	xfs_extnum_t		i;
	xfs_bmbt_key_t		*kp;
	xfs_mount_t		*mp;
	xfs_extnum_t		nextents;
	xfs_bmbt_ptr_t		*pp;

	ASSERT(ip->i_d.di_format == XFS_DINODE_FMT_EXTENTS);
	/*
	 * Make space in the inode incore.
	 */
	xfs_iroot_realloc(ip, 1);
	ip->i_flags |= XFS_IBROOT;
	/*
	 * Fill in the root.
	 */
	block = ip->i_broot;
	block->bb_magic = XFS_BMAP_MAGIC;
	block->bb_level = 1;
	block->bb_numrecs = 1;
	block->bb_leftsib = block->bb_rightsib = NULLDFSBNO;
	/*
	 * Need a cursor.  Can't allocate until bb_level is filled in.
	 */
	mp = ip->i_mount;
	cur = xfs_btree_init_cursor(mp, tp, NULL, 0, XFS_BTNUM_BMAP, ip);
	cur->bc_private.b.firstblock = *firstblock;
	cur->bc_private.b.flist = flist;
	cur->bc_private.b.flags =
		(wasdel ? XFS_BTCUR_BPRV_WASDEL : 0) |
		(lowspace ? XFS_BTCUR_BPRV_LOWSPC : 0);
	/*
	 * Convert to a btree with two levels, one record in root.
	 */
	ip->i_d.di_format = XFS_DINODE_FMT_BTREE;
	args = xfs_alloc_arg_alloc();
	args->tp = tp;
	args->mp = mp;
	if (*firstblock == NULLFSBLOCK) {
		args->type = XFS_ALLOCTYPE_START_BNO;
		args->fsbno = XFS_INO_TO_FSB(mp, ip->i_ino);
	} else if (lowspace) {
		args->type = XFS_ALLOCTYPE_START_BNO;
		args->fsbno = *firstblock;
	} else {
		args->type = XFS_ALLOCTYPE_NEAR_BNO;
		args->fsbno = *firstblock;
	}
	args->minlen = args->maxlen = args->prod = 1;
	args->total = args->minleft = args->mod = args->isfl = 0;
	args->wasdel = wasdel;
	xfs_alloc_vextent(args);
	/*
	 * Allocation can't fail, the space was reserved.
	 */
	ASSERT(args->fsbno != NULLFSBLOCK);
	*firstblock = args->fsbno;
	cur->bc_private.b.allocated++;
	ip->i_d.di_nblocks++;
	abp = xfs_btree_get_bufl(mp, tp, args->fsbno, 0);
	/*
	 * Fill in the child block.
	 */
	ablock = XFS_BUF_TO_BMBT_BLOCK(abp);
	ablock->bb_magic = XFS_BMAP_MAGIC;
	ablock->bb_level = 0;
	ablock->bb_numrecs = 0;
	ablock->bb_leftsib = ablock->bb_rightsib = NULLDFSBNO;
	arp = XFS_BMAP_REC_IADDR(ablock, 1, cur);
	nextents = ip->i_bytes / sizeof(xfs_bmbt_rec_t);
	for (ep = ip->i_u1.iu_extents, i = 0; i < nextents; i++, ep++) {
		if (!ISNULLSTARTBLOCK(xfs_bmbt_get_startblock(ep))) {
			*arp++ = *ep;
			ablock->bb_numrecs++;
		}
	}
	ASSERT(ablock->bb_numrecs == ip->i_d.di_nextents);
	/*
	 * Fill in the root key and pointer.
	 */
	kp = XFS_BMAP_KEY_IADDR(block, 1, cur);
	arp = XFS_BMAP_REC_IADDR(ablock, 1, cur);
	kp->br_startoff = xfs_bmbt_get_startoff(arp);
	pp = XFS_BMAP_PTR_IADDR(block, 1, cur);
	*pp = args->fsbno;
	xfs_alloc_arg_free(args);
	/*
	 * Do all this logging at the end so that 
	 * the root is at the right level.
	 */
	xfs_bmbt_log_block(cur, abp, XFS_BB_ALL_BITS);
	xfs_bmbt_log_recs(cur, abp, 1, ablock->bb_numrecs);
	xfs_bmbt_rcheck(cur);
	xfs_bmbt_kcheck(cur);
	*curp = cur;
	kmem_check();
	return XFS_ILOG_CORE | XFS_ILOG_BROOT;
}

/*
 * Insert new item(s) in the extent list for inode "ip".
 * Count new items are inserted at offset idx.
 */
STATIC void
xfs_bmap_insert_exlist(
	xfs_inode_t	*ip,		/* incore inode pointer */
	xfs_extnum_t	idx,		/* starting index of new items */
	xfs_extnum_t	count,		/* number of inserted items */
	xfs_bmbt_irec_t	*new)		/* items to insert */
{
	xfs_bmbt_rec_t	*base;
	xfs_extnum_t	from;
	xfs_extnum_t	nextents;
	xfs_extnum_t	to;

	ASSERT(ip->i_flags & XFS_IEXTENTS);
	xfs_iext_realloc(ip, count);
	base = ip->i_u1.iu_extents;
	nextents = ip->i_bytes / sizeof(xfs_bmbt_rec_t);
	for (to = nextents - 1, from = to - count; from >= idx; from--, to--)
		base[to] = base[from];
	for (to = idx; to < idx + count; to++, new++)
		xfs_bmbt_set_all(&base[to], new);
	kmem_check();
}

/*
 * Convert a local file to an extents file.
 * This code is sort of bogus, since the file data needs to get 
 * logged so it won't be lost.  The bmap-level manipulations are ok, though.
 */
STATIC int
xfs_bmap_local_to_extents(
	xfs_trans_t	*tp,		/* transaction pointer */
	xfs_inode_t	*ip,		/* incore inode pointer */
	xfs_fsblock_t	*firstblock,	/* first block allocated in xaction */
	xfs_extlen_t	total)		/* total blocks needed by transaction */
{
	int		flags = 0;
#if defined(DEBUG) && !defined(SIM)
	static char	fname[] = "xfs_bmap_local_to_extents";
#endif

	ASSERT(ip->i_d.di_format == XFS_DINODE_FMT_LOCAL);
	if (ip->i_bytes) {
		xfs_alloc_arg_t	*args;
		buf_t		*bp;
		xfs_bmbt_rec_t	*ep;

		args = xfs_alloc_arg_alloc();
		args->tp = tp;
		args->mp = ip->i_mount;
		ASSERT(ip->i_flags & XFS_IINLINE);
		/*
		 * Allocate a block.  We know we need only one, since the
		 * file currently fits in an inode.
		 */
		if (*firstblock == NULLFSBLOCK) {
			args->fsbno = XFS_INO_TO_FSB(args->mp, ip->i_ino);
			args->type = XFS_ALLOCTYPE_START_BNO;
		} else {
			args->fsbno = *firstblock;
			args->type = XFS_ALLOCTYPE_NEAR_BNO;
		}
		args->total = total;
		args->mod = args->minleft = args->wasdel = args->isfl = 0;
		args->minlen = args->maxlen = args->prod = 1;
		xfs_alloc_vextent(args);
		/* 
		 * Can't fail, the space was reserved.
		 */
		ASSERT(args->fsbno != NULLFSBLOCK);
		ASSERT(args->len == 1);
		*firstblock = args->fsbno;
		bp = xfs_btree_get_bufl(args->mp, tp, args->fsbno, 0);
		bcopy(ip->i_u1.iu_data, (char *)bp->b_un.b_addr, ip->i_bytes);
		xfs_idata_realloc(ip, -ip->i_bytes);
		xfs_iext_realloc(ip, 1);
		ep = ip->i_u1.iu_extents;
		xfs_bmbt_set_startoff(ep, 0);
		xfs_bmbt_set_startblock(ep, args->fsbno);
		xfs_bmbt_set_blockcount(ep, 1);
		xfs_bmap_trace_post_update(fname, "new", ip, 0);
		ip->i_d.di_nextents = 1;
		ip->i_d.di_nblocks = 1;
		flags |= XFS_ILOG_EXT;
		xfs_alloc_arg_free(args);
	} else
		ASSERT(ip->i_d.di_nextents == 0);
	ip->i_flags &= ~XFS_IINLINE;
	ip->i_flags |= XFS_IEXTENTS;
	ip->i_d.di_format = XFS_DINODE_FMT_EXTENTS;
	flags |= XFS_ILOG_CORE;
	kmem_check();
	return flags;
}

/*
 * Search the extents list for the inode, for the extent containing bno.
 * If bno lies in a hole, point to the next entry.  If bno lies past eof,
 * *eofp will be set, and *prevp will contain the last entry (null if none).
 * Else, *lastxp will be set to the index of the found
 * entry; *gotp will contain the entry.
 */
STATIC xfs_bmbt_rec_t *			/* pointer to found extent entry */
xfs_bmap_search_extents(
	xfs_inode_t	*ip,		/* incore inode pointer */
	xfs_fsblock_t	bno,		/* block number searched for */
	int		*eofp,		/* out: end of file found */
	xfs_extnum_t	*lastxp,	/* out: last extent index */
	xfs_bmbt_irec_t	*gotp,		/* out: extent entry found */
	xfs_bmbt_irec_t	*prevp)		/* out: previous extent entry found */
{
	xfs_bmbt_rec_t	*base;
	xfs_bmbt_rec_t	*ep;
	xfs_bmbt_irec_t	got;
	int		high;
	xfs_extnum_t	lastx;
	int		low;
	xfs_extnum_t	nextents;

	lastx = ip->i_lastex;
	nextents = ip->i_bytes / sizeof(xfs_bmbt_rec_t);
	base = &ip->i_u1.iu_extents[0];
	if (lastx != NULLEXTNUM && lastx < nextents) {
		ep = base + lastx;
	} else
		ep = NULL;
	prevp->br_startoff = NULLFILEOFF;
	if (ep && bno >= (got.br_startoff = xfs_bmbt_get_startoff(ep)) &&
	    bno < got.br_startoff +
		  (got.br_blockcount = xfs_bmbt_get_blockcount(ep))) {
		*eofp = 0;
	} else if (ep && lastx < nextents - 1 &&
		   bno >= (got.br_startoff = xfs_bmbt_get_startoff(ep + 1)) &&
		   bno < got.br_startoff +
			(got.br_blockcount = xfs_bmbt_get_blockcount(ep + 1))) {
		lastx++;
		ep++;
		*eofp = 0;
	} else if (nextents == 0) {
		*eofp = 1;
	} else {
		/* binary search the extents array */
		low = 0;
		high = nextents - 1;
		while (low <= high) {
			lastx = (low + high) >> 1;
			ep = base + lastx;
			got.br_startoff = xfs_bmbt_get_startoff(ep);
			got.br_blockcount = xfs_bmbt_get_blockcount(ep);
			if (bno < got.br_startoff)
				high = lastx - 1;
			else if (bno >= got.br_startoff + got.br_blockcount)
				low = lastx + 1;
			else {
				got.br_startblock = xfs_bmbt_get_startblock(ep);
				*eofp = 0;
				*lastxp = lastx;
				*gotp = got;
				kmem_check();
				return ep;
			}
		}
		if (bno >= got.br_startoff + got.br_blockcount) {
			lastx++;
			if (lastx == nextents) {
				*eofp = 1;
				got.br_startblock = xfs_bmbt_get_startblock(ep);
				*prevp = got;
				ep = NULL;
			} else {
				*eofp = 0;
				xfs_bmbt_get_all(ep, prevp);
				ep++;
				got.br_startoff = xfs_bmbt_get_startoff(ep);
				got.br_blockcount = xfs_bmbt_get_blockcount(ep);
			}
		} else {
			*eofp = 0;
			if (ep > base)
				xfs_bmbt_get_all(ep - 1, prevp);
		}
	}
	if (ep)
		got.br_startblock = xfs_bmbt_get_startblock(ep);
	*lastxp = lastx;
	*gotp = got;
	kmem_check();
	return ep;
}

#if defined(DEBUG) && !defined(SIM)
/*
 * Add a bmap trace buffer entry.  Base routine for the others.
 */
STATIC void
xfs_bmap_trace_addentry(
	int		opcode,		/* operation */
	char		*fname,		/* function name */
	char		*desc,		/* operation description */
	xfs_inode_t	*ip,		/* incore inode pointer */
	xfs_extnum_t	idx,		/* index of entry(entries) deleted */
	xfs_extnum_t	cnt,		/* count of entries deleted, 1 or 2 */
	xfs_bmbt_rec_t	*r1,		/* first record */
	xfs_bmbt_rec_t	*r2)		/* second record or null */
{
	xfs_bmbt_rec_t	tr2;

	ASSERT(cnt == 1 || cnt == 2);
	ASSERT(r1 != NULL);
	if (cnt == 1) {
		ASSERT(r2 == NULL);
		r2 = &tr2;
		bzero(&tr2, sizeof(tr2));
	} else
		ASSERT(r2 != NULL);
	ktrace_enter(xfs_bmap_trace_buf,
		(void *)opcode, (void *)fname, (void *)desc, (void *)ip,
		(void *)idx, (void *)cnt,
		(void *)(ip->i_ino >> 32), (void *)(int)(ip->i_ino),
		(void *)r1->l0, (void *)r1->l1,
		(void *)r1->l2, (void *)r1->l3,
		(void *)r2->l0, (void *)r2->l1,
		(void *)r2->l2, (void *)r2->l3);
	ASSERT(ip->i_xtrace);
	ktrace_enter(ip->i_xtrace,
		(void *)opcode, (void *)fname, (void *)desc, (void *)ip,
		(void *)idx, (void *)cnt,
		(void *)(ip->i_ino >> 32), (void *)(int)(ip->i_ino),
		(void *)r1->l0, (void *)r1->l1,
		(void *)r1->l2, (void *)r1->l3,
		(void *)r2->l0, (void *)r2->l1,
		(void *)r2->l2, (void *)r2->l3);
}
	
/*
 * Add bmap trace entry prior to a call to xfs_bmap_delete_exlist.
 */
STATIC void
xfs_bmap_trace_delete(
	char		*fname,		/* function name */
	char		*desc,		/* operation description */
	xfs_inode_t	*ip,		/* incore inode pointer */
	xfs_extnum_t	idx,		/* index of entry(entries) deleted */
	xfs_extnum_t	cnt)		/* count of entries deleted, 1 or 2 */
{
	xfs_bmap_trace_addentry(XFS_BMAP_KTRACE_DELETE, fname, desc, ip, idx,
		cnt, &ip->i_u1.iu_extents[idx],
		cnt == 2 ? &ip->i_u1.iu_extents[idx + 1] : NULL);
}

/*
 * Add bmap trace entry prior to a call to xfs_bmap_insert_exlist, or
 * reading in the extents list from the disk (in the btree).
 */
STATIC void
xfs_bmap_trace_insert(
	char		*fname,		/* function name */
	char		*desc,		/* operation description */
	xfs_inode_t	*ip,		/* incore inode pointer */
	xfs_extnum_t	idx,		/* index of entry(entries) inserted */
	xfs_extnum_t	cnt,		/* count of entries inserted, 1 or 2 */
	xfs_bmbt_irec_t	*r1,		/* inserted record 1 */
	xfs_bmbt_irec_t	*r2)		/* inserted record 2 or null */
{
	xfs_bmbt_rec_t	tr1;		/* compressed record 1 */
	xfs_bmbt_rec_t	tr2;		/* compressed record 2 if needed */

	xfs_bmbt_set_all(&tr1, r1);
	if (cnt == 2) {
		ASSERT(r2 != NULL);
		xfs_bmbt_set_all(&tr2, r2);
	} else {
		ASSERT(cnt == 1);
		ASSERT(r2 == NULL);
	}
	xfs_bmap_trace_addentry(XFS_BMAP_KTRACE_INSERT, fname, desc, ip, idx,
		cnt, &tr1, cnt == 2 ? &tr2 : NULL);
}

/*
 * Add bmap trace entry after updating an extent list entry in place.
 */
STATIC void
xfs_bmap_trace_post_update(
	char		*fname,		/* function name */
	char		*desc,		/* operation description */
	xfs_inode_t	*ip,		/* incore inode pointer */
	xfs_extnum_t	idx)		/* index of entry updated */
{
	xfs_bmap_trace_addentry(XFS_BMAP_KTRACE_POST_UP, fname, desc, ip, idx,
		1, &ip->i_u1.iu_extents[idx], NULL);
}

/*
 * Add bmap trace entry prior to updating an extent list entry in place.
 */
STATIC void
xfs_bmap_trace_pre_update(
	char		*fname,		/* function name */
	char		*desc,		/* operation description */
	xfs_inode_t	*ip,		/* incore inode pointer */
	xfs_extnum_t	idx)		/* index of entry to be updated */
{
	xfs_bmap_trace_addentry(XFS_BMAP_KTRACE_PRE_UP, fname, desc, ip, idx, 1,
		&ip->i_u1.iu_extents[idx], NULL);
}
#endif	/* DEBUG && !SIM */

/*
 * Compute the worst-case number of indirect blocks that will be used
 * for ip's delayed extent of length "len".
 */
STATIC xfs_extlen_t
xfs_bmap_worst_indlen(
	xfs_inode_t	*ip,		/* incore inode pointer */
	xfs_extlen_t	len)		/* delayed extent length */
{
	int		level;
	int		maxrecs;
	xfs_mount_t	*mp;
	xfs_extlen_t	rval;

	mp = ip->i_mount;
	maxrecs = XFS_BTREE_BLOCK_MAXRECS(mp->m_sb.sb_blocksize, xfs_bmbt, 1);
	for (level = 0, rval = 0; level < XFS_BM_MAXLEVELS(mp); level++) {
		len = (len + maxrecs - 1) / maxrecs;
		rval += len;
		if (len == 1)
			return rval + (XFS_BM_MAXLEVELS(mp) - level - 1);
		if (level == 0)
			maxrecs = XFS_BTREE_BLOCK_MAXRECS(mp->m_sb.sb_blocksize,
				xfs_bmbt, 0);
	}
	return rval;
}

/*
 * Add the extent to the list of extents to be free at transaction end.
 * The list is maintained sorted (by block number).
 */
void
xfs_bmap_add_free(
	xfs_fsblock_t		bno,		/* fs block number of extent */
	xfs_extlen_t		len,		/* length of extent */
	xfs_bmap_free_t		*flist,		/* list of extents */
	xfs_mount_t		*mp)		/* mount point structure */
{
	xfs_bmap_free_item_t	*cur;		/* current (next) element */
	xfs_bmap_free_item_t	*new;		/* new element */
	xfs_bmap_free_item_t	*prev;		/* previous element */
#ifdef DEBUG
	xfs_agnumber_t		agno;
	xfs_agblock_t		agbno;

	ASSERT(bno != NULLFSBLOCK);
	ASSERT(!ISNULLSTARTBLOCK(bno));
	agno = XFS_FSB_TO_AGNO(mp, bno);
	agbno = XFS_FSB_TO_AGBNO(mp, bno);
	ASSERT(agno < mp->m_sb.sb_agcount);
	ASSERT(agbno < mp->m_sb.sb_agblocks);
	ASSERT(len < mp->m_sb.sb_agblocks);
	ASSERT(agbno + len <= mp->m_sb.sb_agblocks);
#endif
	ASSERT(xfs_bmap_free_item_zone != NULL);
	new = kmem_zone_alloc(xfs_bmap_free_item_zone, KM_SLEEP);
	new->xbfi_startblock = bno;
	new->xbfi_blockcount = len;
	for (prev = NULL, cur = flist->xbf_first;
	     cur != NULL;
	     prev = cur, cur = cur->xbfi_next) {
		if (cur->xbfi_startblock >= bno)
			break;
	}
	if (prev)
		prev->xbfi_next = new;
	else
		flist->xbf_first = new;
	new->xbfi_next = cur;
	flist->xbf_count++;
	kmem_check();
}

/* 
 * Compute and fill in the value of the maximum depth of a bmap btree
 * in this filesystem.  Done once, during mount.
 */
void
xfs_bmap_compute_maxlevels(
	xfs_mount_t	*mp)		/* file system mount structure */
{
	int		level;
	uint		maxblocks;
	uint		maxleafents;
	int		maxrootrecs;
	int		minleafrecs;
	int		minnoderecs;

	/*
	 * The maximum number of extents in a file, hence the maximum
	 * number of leaf entries, is controlled by the type of di_nextents
	 * (a signed 32-bit number, xfs_extnum_t).
	 */
	maxleafents = MAXEXTNUM;
	minleafrecs = mp->m_bmap_dmnr[0];
	minnoderecs = mp->m_bmap_dmnr[1];
	maxrootrecs = mp->m_bmap_dmxr[3];
	maxblocks = (maxleafents + minleafrecs - 1) / minleafrecs;
	for (level = 1; maxblocks > 1; level++) {
		if (maxblocks <= maxrootrecs)
			maxblocks = 1;
		else
			maxblocks = (maxblocks + minnoderecs - 1) / minnoderecs;
	}
	mp->m_bm_maxlevels = level;
}

/*
 * Routine to be called at transaction's end by xfs_bmapi, xfs_bunmapi 
 * caller.  Frees all the extents that need freeing, which must be done
 * last due to locking considerations.  We never free any extents in
 * the first transaction and we make the first a synchronous transaction
 * so that we can always be sure that the changes to structures pointing
 * to the freed block are permanent before we actually free the blocks.
 * This is necessary to prevent blocks from being reallocated and written
 * to before the free and reallocation are actually permanent.
 *
 * Return 1 if the given transaction was committed and a new one
 * started, and 0 otherwise.
 */
int
xfs_bmap_finish(
	xfs_trans_t		**tp,		/* transaction pointer addr */
	xfs_bmap_free_t		*flist,		/* i/o: list extents to free */
	xfs_fsblock_t		firstblock)	/* controlled ag for allocs */
{
	unsigned int		blkres;
	xfs_efd_log_item_t	*efd;
	xfs_efi_log_item_t	*efi;
	xfs_agnumber_t		firstag;
	xfs_bmap_free_item_t	*free;
	unsigned int		logres;
	unsigned int		logcount;
	xfs_mount_t		*mp;
	xfs_bmap_free_item_t	*next;
	xfs_trans_t		*ntp;
	xfs_bmap_free_item_t	*prev;
	unsigned int		rtblkres;

	ASSERT((*tp)->t_flags & XFS_TRANS_PERM_LOG_RES);

	if (flist->xbf_count == 0)
		return 0;
	ntp = *tp;
	mp = ntp->t_mountp;
	xfs_trans_set_sync(ntp);
	efi = xfs_trans_get_efi(ntp, flist->xbf_count);
	for (free = flist->xbf_first; free; free = free->xbfi_next)
		xfs_trans_log_efi_extent(ntp, efi, free->xbfi_startblock,
			free->xbfi_blockcount);
	logres = ntp->t_log_res;
	logcount = ntp->t_log_count;
	blkres = ntp->t_blk_res - ntp->t_blk_res_used;
	rtblkres = ntp->t_rtx_res - ntp->t_rtx_res_used;
	ntp = xfs_trans_dup(*tp);
	xfs_trans_commit(*tp, 0);
	xfs_trans_reserve(ntp, blkres, logres, rtblkres,
			  XFS_TRANS_PERM_LOG_RES, logcount);
	efd = xfs_trans_get_efd(ntp, efi, flist->xbf_count);
	for (free = flist->xbf_first; free != NULL; free = next) {
		next = free->xbfi_next;
		xfs_free_extent(ntp, free->xbfi_startblock,
			free->xbfi_blockcount);
		xfs_trans_log_efd_extent(ntp, efd, free->xbfi_startblock,
			free->xbfi_blockcount);
		xfs_bmap_del_free(flist, NULL, free);
	}
	*tp = ntp;
	kmem_check();
	return 1;
}

/*
 * Returns the file-relative block number of the first unused block in the file.
 * This is the lowest-address hole if the file has holes, else the first block
 * past the end of file.
 * Return 0 if the file is currently local (in-inode).
 */
xfs_fileoff_t
xfs_bmap_first_unused(
	xfs_trans_t	*tp,			/* transaction pointer */
	xfs_inode_t	*ip)			/* incore inode */
{
	xfs_bmbt_rec_t	*base;			/* base of extent array */
	xfs_bmbt_rec_t	*ep;			/* pointer to an extent entry */
	xfs_fsblock_t	lastaddr;		/* last block number seen */
	xfs_fsblock_t	off;			/* offset for this block */
	xfs_extnum_t	nextents;		/* number of extent entries */

	ASSERT(ip->i_d.di_format == XFS_DINODE_FMT_BTREE ||
	       ip->i_d.di_format == XFS_DINODE_FMT_EXTENTS ||
	       ip->i_d.di_format == XFS_DINODE_FMT_LOCAL);
	if (ip->i_d.di_format == XFS_DINODE_FMT_LOCAL)
		return 0;
	if (!(ip->i_flags & XFS_IEXTENTS))
		xfs_iread_extents(tp, ip);
	nextents = ip->i_bytes / sizeof(xfs_bmbt_rec_t);
	base = &ip->i_u1.iu_extents[0];
	for (lastaddr = 0, ep = base; ep < &base[nextents]; ep++) {
		if (lastaddr < (off = xfs_bmbt_get_startoff(ep)))
			return lastaddr;
		lastaddr = off + xfs_bmbt_get_blockcount(ep);
	}
	return lastaddr;
}

/*
 * Returns the file-relative block number of the first block past eof in
 * the file.  This is not based on i_size, it is based on the extent list.
 * Returns 0 for local files, as they do not have an extent list.
 */
xfs_fileoff_t
xfs_bmap_last_offset(
	xfs_trans_t	*tp,			/* transaction pointer */
	xfs_inode_t	*ip)			/* incore inode */
{
	xfs_bmbt_rec_t	*base;			/* base of extent array */
	xfs_bmbt_rec_t	*ep;			/* pointer to last extent */
	xfs_extnum_t	nextents;		/* number of extent entries */

	ASSERT(ip->i_d.di_format == XFS_DINODE_FMT_BTREE ||
	       ip->i_d.di_format == XFS_DINODE_FMT_EXTENTS ||
	       ip->i_d.di_format == XFS_DINODE_FMT_LOCAL);
	if (ip->i_d.di_format == XFS_DINODE_FMT_LOCAL)
		return 0;
	if (!(ip->i_flags & XFS_IEXTENTS))
		xfs_iread_extents(tp, ip);
	nextents = ip->i_bytes / sizeof(xfs_bmbt_rec_t);
	if (!nextents)
		return 0;
	base = &ip->i_u1.iu_extents[0];
	ASSERT(base != NULL);
	ep = &base[nextents - 1];
	return xfs_bmbt_get_startoff(ep) + xfs_bmbt_get_blockcount(ep);
}

/*
 * Read in the extents to iu_extents.
 * All inode fields are set up by caller, we just traverse the btree
 * and copy the records in.
 */
void
xfs_bmap_read_extents(
	xfs_trans_t		*tp,	/* transaction pointer */
	xfs_inode_t		*ip)	/* incore inode */
{
	xfs_bmbt_block_t	*block;	/* current btree block */
	xfs_fsblock_t		bno;	/* block # of "block" */
	buf_t			*bp;	/* buffer for "block" */
#if defined(DEBUG) && !defined(SIM)
	static char		fname[] = "xfs_bmap_read_extents";
#endif
	xfs_extnum_t		i;	/* index into the extents list */
	int			level;	/* btree level, for checking */
	xfs_mount_t		*mp;	/* file system mount structure */
	xfs_bmbt_ptr_t		*pp;	/* pointer to block address */
	xfs_extnum_t		room;	/* number of entries there's room for */
	xfs_bmbt_rec_t		*trp;	/* target record pointer */

	bno = NULLFSBLOCK;
	mp = ip->i_mount;
	block = ip->i_broot;
	/*
	 * Root level must use BMAP_BROOT_PTR_ADDR macro to get ptr out.
	 */
	ASSERT(block->bb_level > 0);
	level = block->bb_level;
	pp = XFS_BMAP_BROOT_PTR_ADDR(block, 1, ip->i_broot_bytes);
	ASSERT(*pp != NULLDFSBNO);
	ASSERT(XFS_FSB_TO_AGNO(mp, *pp) < mp->m_sb.sb_agcount);
	ASSERT(XFS_FSB_TO_AGBNO(mp, *pp) < mp->m_sb.sb_agblocks);
	bno = *pp;
	/*
	 * Go down the tree until leaf level is reached, following the first
	 * pointer (leftmost) at each level.
	 */
	while (level-- > 0) {
		bp = xfs_btree_read_bufl(mp, tp, bno, 0);
		block = XFS_BUF_TO_BMBT_BLOCK(bp);
		ASSERT(block->bb_level == level);
		if (level == 0)
			break;
		pp = XFS_BTREE_PTR_ADDR(mp->m_sb.sb_blocksize, xfs_bmbt, block,
			1, mp->m_bmap_dmxr[1]);
		ASSERT(*pp != NULLDFSBNO);
		ASSERT(XFS_FSB_TO_AGNO(mp, *pp) < mp->m_sb.sb_agcount);
		ASSERT(XFS_FSB_TO_AGBNO(mp, *pp) < mp->m_sb.sb_agblocks);
		bno = *pp;
		xfs_trans_brelse(tp, bp);
	}
	/*
	 * Here with bp and block set to the leftmost leaf node in the tree.
	 */
	room = ip->i_bytes / sizeof(*trp);
	trp = ip->i_u1.iu_extents;
	i = 0;
	/*
	 * Loop over all leaf nodes.  Copy information to the extent list.
	 */
	for (;;) {
		xfs_bmbt_rec_t	*frp;
		xfs_fsblock_t	nextbno;

		ASSERT(i + block->bb_numrecs <= room);
		/*
		 * Copy records into the extent list.
		 */
		frp = XFS_BTREE_REC_ADDR(mp->m_sb.sb_blocksize, xfs_bmbt, block,
			1, mp->m_bmap_dmxr[0]);
		bcopy(frp, trp, block->bb_numrecs * sizeof(*frp));
		trp += block->bb_numrecs;
		i += block->bb_numrecs;
		/*
		 * Get the next leaf block's address.
		 */
		nextbno = block->bb_rightsib;
		xfs_trans_brelse(tp, bp);
		bno = nextbno;
		/*
		 * If we've reached the end, stop.
		 */
		if (bno == NULLFSBLOCK)
			break;
		bp = xfs_btree_read_bufl(mp, tp, bno, 0);
		block = XFS_BUF_TO_BMBT_BLOCK(bp);
	}
	ASSERT(i == ip->i_bytes / sizeof(*trp));
	ASSERT(i == ip->i_d.di_nextents);
	xfs_bmap_trace_exlist(fname, ip, i);
	kmem_check();
}

#if defined(DEBUG) && !defined(SIM)
/*
 * Add bmap trace insert entries for all the contents of the extent list.
 */
void
xfs_bmap_trace_exlist(
	char		*fname,		/* function name */
	xfs_inode_t	*ip,		/* incore inode pointer */
	xfs_extnum_t	cnt)		/* count of entries in the list */
{
	xfs_bmbt_rec_t	*base;		/* base of extent list */
	xfs_bmbt_rec_t	*ep;		/* current entry in extent list */
	xfs_extnum_t	idx;		/* extent list entry number */
	xfs_bmbt_irec_t	s;		/* extent list record */

	ASSERT(cnt == ip->i_bytes / sizeof(*base));
	base = ip->i_u1.iu_extents;
	for (idx = 0, ep = base; idx < cnt; idx++, ep++) {
		xfs_bmbt_get_all(ep, &s);
		xfs_bmap_trace_insert(fname, "exlist", ip, idx, 1, &s, NULL);
	}
}
#endif

/*
 * Map file blocks to filesystem blocks.
 * File range is given by the bno/len pair.
 * Adds blocks to file if a write ("flags & XFS_BMAPI_WRITE" set)
 * into a hole or past eof.
 * Only allocates blocks from a single allocation group,
 * to avoid locking problems.
 * The return value from the first call in a transaction must be remembered
 * and presented to subsequent calls in "firstblock".  An upper bound for
 * the number of blocks to be allocated is supplied to the first call 
 * in "total"; if no allocation group has that many free blocks then 
 * the call will fail (return NULLFSBLOCK).
 */
xfs_fsblock_t					/* first allocated block */
xfs_bmapi(
	xfs_trans_t		*tp,		/* transaction pointer */
	xfs_inode_t		*ip,		/* incore inode */
	xfs_fileoff_t		bno,		/* starting file offs. mapped */
	xfs_extlen_t		len,		/* length to map in file */
	int			flags,		/* XFS_BMAPI_... */
	xfs_fsblock_t		firstblock,	/* controls a.g. for allocs */
	xfs_extlen_t		total,		/* total blocks needed */
	xfs_bmbt_irec_t		*mval,		/* output: map values */
	int			*nmap,		/* i/o: mval size/count */
	xfs_bmap_free_t		*flist)		/* i/o: list extents to free */
{
	xfs_fsblock_t		abno;
	xfs_extlen_t		alen;
	xfs_fsblock_t		aoff;
	xfs_fsblock_t		askbno;
	xfs_extlen_t		asklen;
	xfs_bmalloca_t		*bma;
	xfs_btree_cur_t		*cur;
	int			delay;
	xfs_fsblock_t		end;
	int			eof;
	xfs_bmbt_rec_t		*ep;
	xfs_bmbt_irec_t		got;
	xfs_extnum_t		i;
	int			inhole;
	xfs_extnum_t		lastx;
	int			logflags;
	int			lowspace;
	xfs_extlen_t		minleft;
	xfs_extlen_t		minlen;
	xfs_mount_t		*mp;
	int			n;
	xfs_extnum_t		nextents;
	xfs_fileoff_t		obno;
	xfs_bmbt_irec_t		prev;
	int			trim;
	xfs_alloctype_t		type;
	int			userdata;
	int			wasdelay;
	int			wr;

	ASSERT(*nmap >= 1);
	ASSERT(*nmap <= XFS_BMAP_MAX_NMAP || !(flags & XFS_BMAPI_WRITE));
	ASSERT(ip->i_d.di_format == XFS_DINODE_FMT_BTREE ||
	       ip->i_d.di_format == XFS_DINODE_FMT_EXTENTS ||
	       ip->i_d.di_format == XFS_DINODE_FMT_LOCAL);
	wr = (flags & XFS_BMAPI_WRITE) != 0;
	delay = (flags & XFS_BMAPI_DELAY) != 0;
	trim = (flags & XFS_BMAPI_ENTIRE) == 0;
	userdata = (flags & XFS_BMAPI_METADATA) == 0;
	ASSERT(wr || !delay);
	logflags = 0;
	lowspace = 0;
	bma = NULL;
	if (ip->i_d.di_format == XFS_DINODE_FMT_LOCAL) {
		if (!wr) {
			/* change to assert later */
			*nmap = 0;
			kmem_check();
			return firstblock;
		}
		logflags |= xfs_bmap_local_to_extents(tp, ip, &firstblock,
			total);
	}
	if (firstblock == NULLFSBLOCK) {
		type = XFS_ALLOCTYPE_START_BNO;
		if (ip->i_d.di_format == XFS_DINODE_FMT_BTREE)
			minleft = ip->i_broot->bb_level + 1;
		else
			minleft = 1;
	} else {
		type = XFS_ALLOCTYPE_NEAR_BNO;
		minleft = 0;
	}
	if (!(ip->i_flags & XFS_IEXTENTS))
		xfs_iread_extents(tp, ip);
	ep = xfs_bmap_search_extents(ip, bno, &eof, &lastx, &got, &prev);
	nextents = ip->i_bytes / sizeof(xfs_bmbt_rec_t);
	mp = ip->i_mount;
	n = 0;
	end = bno + len;
	cur = NULL;
	obno = bno;
	while (bno < end && n < *nmap) {
		/*
		 * Reading past eof, act as though there's a hole
		 * up to end.
		 */
		if (eof && !wr)
			got.br_startoff = end;
		inhole = eof || got.br_startoff > bno;
		wasdelay = !inhole && !delay &&
			ISNULLSTARTBLOCK(got.br_startblock);
		/*
		 * First, deal with the hole before the allocated space 
		 * that we found, if any.
		 */
		if ((inhole || wasdelay) && wr) {
			xfs_extlen_t	indlen;

			/*
			 * For the wasdelay case, we could also just
			 * allocate the stuff asked for in this bmap call
			 * but that wouldn't be as good.
			 */
			if (wasdelay) {
				alen = got.br_blockcount;
				aoff = got.br_startoff;
				minlen = (bno - aoff) + 1;
			} else {
				alen = XFS_EXTLEN_MIN(len, MAXEXTLEN);
				if (!eof)
					alen = XFS_EXTLEN_MIN(alen,
						got.br_startoff - bno);
				aoff = bno;
				minlen = 1;
			}
			if (delay) {
				indlen = xfs_bmap_worst_indlen(ip, alen);
				ASSERT(indlen > 0);
				if (xfs_mod_incore_sb(mp, XFS_SB_FDBLOCKS,
						      -(alen + indlen)))
					break;
				ip->i_delayed_blks += alen;
				abno = NULLSTARTBLOCK(indlen);
			} else {
				/*
				 * If first time, allocate and fill in
				 * once-only bma fields.
				 */
				if (bma == NULL) {
					bma = kmem_alloc(sizeof(*bma),
						KM_SLEEP);
					bma->tp = tp;
					bma->ip = ip;
					bma->prevp = &prev;
					bma->gotp = &got;
					bma->total = total;
					bma->userdata = userdata;
					bma->minleft = minleft;
				}
				/*
				 * Fill in changeable bma fields.
				 */
				bma->eof = eof;
				bma->firstblock = firstblock;
				bma->alen = alen;
				bma->off = aoff;
				bma->wasdel = wasdelay;
				bma->minlen = minlen;
				bma->low = lowspace;
				/*
				 * Call allocator.
				 */
				xfs_bmap_alloc(bma);
				/*
				 * Copy out result fields.
				 */
				abno = bma->rval;
				firstblock = bma->firstblock;
				alen = bma->alen;
				lowspace = bma->low;
				if (abno == NULLFSBLOCK)
					break;
				if ((ip->i_flags & XFS_IBROOT) && !cur) {
					cur = xfs_btree_init_cursor(mp, tp,
						NULL, 0, XFS_BTNUM_BMAP, ip);
					cur->bc_private.b.firstblock =
						firstblock;
					cur->bc_private.b.flist = flist;
				}
			}
			if (cur)
				cur->bc_private.b.flags =
					(wasdelay ? XFS_BTCUR_BPRV_WASDEL : 0) |
					(lowspace ? XFS_BTCUR_BPRV_LOWSPC : 0);
			got.br_startoff = aoff;
			got.br_startblock = abno;
			got.br_blockcount = alen;
			logflags |= xfs_bmap_add_extent(ip, lastx, &cur, &got,
				&firstblock, flist, lowspace);
			lastx = ip->i_lastex;
			ep = &ip->i_u1.iu_extents[lastx];
			nextents = ip->i_bytes / sizeof(xfs_bmbt_rec_t);
			xfs_bmbt_get_all(ep, &got);
			ASSERT(got.br_startoff <= aoff);
			ASSERT(got.br_startoff + got.br_blockcount >= aoff + alen);
#ifdef DEBUG
			if (delay) {
				ASSERT(ISNULLSTARTBLOCK(got.br_startblock));
				ASSERT(STARTBLOCKVAL(got.br_startblock) > 0);
			}
#endif
			/*
			 * Fall down into the found allocated space case.
			 */
		} else if (inhole) {
			/*
			 * Reading in a hole.
			 */
			mval->br_startoff = bno;
			mval->br_startblock = HOLESTARTBLOCK;
			mval->br_blockcount = XFS_EXTLEN_MIN(len, got.br_startoff - bno);
			bno += mval->br_blockcount;
			len -= mval->br_blockcount;
			mval++;
			n++;
			continue;
		}
		/*
		 * Then deal with the allocated space we found.
		 */
		ASSERT(ep != NULL);
		if (trim && (got.br_startoff + got.br_blockcount > obno)) {
			bno = XFS_FILEOFF_MAX(bno, obno);
			ASSERT((bno >= obno) || (n == 0));
			mval->br_startoff = bno;
			if (ISNULLSTARTBLOCK(got.br_startblock)) {
				ASSERT(!wr || delay);
				mval->br_startblock = DELAYSTARTBLOCK;
			} else
				mval->br_startblock = got.br_startblock +
					(bno - got.br_startoff);
			mval->br_blockcount =
				XFS_EXTLEN_MIN(len, got.br_blockcount -
					(bno - got.br_startoff));
		} else {
			*mval = got;
			if (ISNULLSTARTBLOCK(mval->br_startblock)) {
				ASSERT(!wr || delay);
				mval->br_startblock = DELAYSTARTBLOCK;
			}
		}
		bno = mval->br_startoff + mval->br_blockcount;
		len = end - bno;
		if (n > 0 && mval->br_startoff == mval[-1].br_startoff) {
			ASSERT(mval->br_startblock == mval[-1].br_startblock);
			ASSERT(mval->br_blockcount > mval[-1].br_blockcount);
			mval[-1].br_blockcount = mval->br_blockcount;
		} else if (n > 0 && mval->br_startblock != DELAYSTARTBLOCK &&
			   mval[-1].br_startblock != DELAYSTARTBLOCK &&
			   mval[-1].br_startblock != HOLESTARTBLOCK &&
			   mval->br_startblock ==
			   mval[-1].br_startblock + mval[-1].br_blockcount) {
			ASSERT(mval->br_startoff ==
			       mval[-1].br_startoff + mval[-1].br_blockcount);
			mval[-1].br_blockcount += mval->br_blockcount;
		} else if (n > 0 &&
			   mval->br_startblock == DELAYSTARTBLOCK &&
			   mval[-1].br_startblock == DELAYSTARTBLOCK &&
			   mval->br_startoff ==
			   mval[-1].br_startoff + mval[-1].br_blockcount) {
			mval[-1].br_blockcount += mval->br_blockcount;
		} else if (!((n == 0) &&
			     ((mval->br_startoff + mval->br_blockcount) <=
			      obno))) {
			mval++;
			n++;
		}
		/*
		 * If we're done, stop now.
		 */
		if (bno >= end || n >= *nmap)
			break;
		/*
		 * Else go on to the next record.
		 */
		ep++;
		lastx++;
		if (lastx >= nextents) {
			eof = 1;
			prev = got;
		} else
			xfs_bmbt_get_all(ep, &got);
	}
	if (bma)
		kmem_free(bma, sizeof(*bma));
	ip->i_lastex = lastx;
	*nmap = n;
	/*
	 * Transform from btree to extents, give it cur.
	 */
	if (ip->i_d.di_format == XFS_DINODE_FMT_BTREE &&
		 ip->i_d.di_nextents <= XFS_BMAP_EXT_MAXRECS(mp))
		logflags |= xfs_bmap_btree_to_extents(tp, ip, cur);
	/*
	 * Log everything.  Do this after conversion, there's no point in
	 * logging the extent list if we've converted to btree format.
	 */
	if ((logflags & XFS_ILOG_EXT) &&
	    ip->i_d.di_format != XFS_DINODE_FMT_EXTENTS)
		logflags &= ~XFS_ILOG_EXT;
	else if ((logflags & XFS_ILOG_BROOT) &&
		 ip->i_d.di_format != XFS_DINODE_FMT_BTREE)
		logflags &= ~XFS_ILOG_BROOT;
	if (logflags)
		xfs_trans_log_inode(tp, ip, logflags);
	if (cur) {
		firstblock = cur->bc_private.b.firstblock;
		xfs_btree_del_cursor(cur);
	}
	kmem_check();
	return firstblock;
}

/*
 * Unmap (remove) blocks from a file.
 * If nexts is nonzero then the number of extents to remove is limited to
 * that value.  If not all extents in the block range can be removed then
 * *done is set.
 */
xfs_fsblock_t					/* first allocated block */
xfs_bunmapi(
	xfs_trans_t		*tp,		/* transaction pointer */
	struct xfs_inode	*ip,		/* incore inode */
	xfs_fileoff_t		bno,		/* starting offset to unmap */
	xfs_extlen_t		len,		/* length to unmap in file */
	xfs_extnum_t		nexts,		/* number of extents max */
	xfs_fsblock_t		firstblock,	/* controls a.g. for allocs */
	xfs_bmap_free_t		*flist,		/* i/o: list extents to free */
	int			*done)		/* set if not done yet */
{
	xfs_btree_cur_t		*cur;
	xfs_bmbt_irec_t		del;
	int			eof;
	xfs_bmbt_rec_t		*ep;
	xfs_extnum_t		extno;
	xfs_bmbt_irec_t		got;
	xfs_extnum_t		i;
	xfs_extnum_t		lastx;
	int			logflags;
	xfs_mount_t		*mp;
	xfs_extnum_t		nextents;
	xfs_bmbt_irec_t		prev;
	xfs_fsblock_t		start;

	ASSERT(ip->i_d.di_format == XFS_DINODE_FMT_BTREE ||
	       ip->i_d.di_format == XFS_DINODE_FMT_EXTENTS);
	if (!(ip->i_flags & XFS_IEXTENTS))
		xfs_iread_extents(tp, ip);
	nextents = ip->i_bytes / sizeof(xfs_bmbt_rec_t);
	if (nextents == 0) {
		*done = 1;
		return firstblock;
	}
	start = bno;
	bno = start + len - 1;
	ep = xfs_bmap_search_extents(ip, bno, &eof, &lastx, &got, &prev);
	/*
	 * Check to see if the given block number is past the end of the
	 * file, back up to the last block if so...
	 */
	if (eof) {
		lastx--;
		ep = &ip->i_u1.iu_extents[lastx];
		xfs_bmbt_get_all(ep, &got);
		bno = got.br_startoff + got.br_blockcount - 1;
	}
	mp = ip->i_mount;
	logflags = 0;
	if (ip->i_flags & XFS_IBROOT) {
		ASSERT(ip->i_d.di_format == XFS_DINODE_FMT_BTREE);
		cur = xfs_btree_init_cursor(mp, tp, NULL, 0,
			XFS_BTNUM_BMAP, ip);
		cur->bc_private.b.firstblock = firstblock;
		cur->bc_private.b.flist = flist;
		cur->bc_private.b.flags = 0;
	} else
		cur = NULL;
	extno = 0;
	while (bno != (xfs_fsblock_t)-1 && bno >= start && lastx >= 0 &&
	       (nexts == 0 || extno < nexts)) {
		/*
		 * Is the found extent after a hole in which bno lives?
		 * Just back up to the previous extent, if so.
		 */
		if (got.br_startoff > bno) {
			if (--lastx < 0)
				break;
			ep--;
			xfs_bmbt_get_all(ep, &got);
		}
		/*
		 * Is the last block of this extent before the range
		 * we're supposed to delete?  If so, we're done.
		 */
		bno = got.br_startoff + got.br_blockcount - 1;
		if (bno < start)
			break;
		/*
		 * Then deal with the (possibly delayed) allocated space
		 * we found.
		 */
		ASSERT(ep != NULL);
		del = got;
		if (got.br_startoff < start) {
			del.br_startoff = start;
			del.br_blockcount -= start - got.br_startoff;
			if (!ISNULLSTARTBLOCK(del.br_startblock))
				del.br_startblock += start - got.br_startoff;
		}
		if (del.br_startoff + del.br_blockcount > start + len)
			del.br_blockcount = start + len - del.br_startoff;
		if (ISNULLSTARTBLOCK(del.br_startblock)) {
			ASSERT(STARTBLOCKVAL(del.br_startblock) > 0);
			xfs_mod_incore_sb(mp, XFS_SB_FDBLOCKS,
				del.br_blockcount);
			ip->i_delayed_blks -= del.br_blockcount;
			if (cur)
				cur->bc_private.b.flags |=
					XFS_BTCUR_BPRV_WASDEL;
		} else if (cur)
			cur->bc_private.b.flags &= ~XFS_BTCUR_BPRV_WASDEL;
		logflags |= xfs_bmap_del_extent(ip, lastx, flist, cur, &del);
		bno = del.br_startoff - 1;
		lastx = ip->i_lastex;
		/*
		 * If not done go on to the next (previous) record.
		 * Reset ep in case the extents array was re-alloced.
		 */
		ep = &ip->i_u1.iu_extents[lastx];
		if (bno != (xfs_fsblock_t)-1 && bno >= start && --lastx >= 0) {
			ep--;
			xfs_bmbt_get_all(ep, &got);
			extno++;
		}
	}
	ip->i_lastex = lastx;
	*done = bno == (xfs_fsblock_t)-1 || bno < start || lastx < 0;
	/*
	 * Convert to a btree if necessary.
	 */
	if (ip->i_d.di_format == XFS_DINODE_FMT_EXTENTS &&
	    ip->i_d.di_nextents > XFS_BMAP_EXT_MAXRECS(mp))
		logflags |= xfs_bmap_extents_to_btree(tp, ip, &firstblock,
			flist, &cur, 0, 0);
	/*
	 * transform from btree to extents, give it cur
	 */
	else if (ip->i_d.di_format == XFS_DINODE_FMT_BTREE &&
		 ip->i_d.di_nextents <= XFS_BMAP_EXT_MAXRECS(mp))
		logflags |= xfs_bmap_btree_to_extents(tp, ip, cur);
	/* transform from extents to local */
	/*
	 * Log everything.  Do this after conversion, there's no point in
	 * logging the extent list if we've converted to btree format.
	 */
	if ((logflags & XFS_ILOG_EXT) &&
	    ip->i_d.di_format != XFS_DINODE_FMT_EXTENTS)
		logflags &= ~XFS_ILOG_EXT;
	else if ((logflags & XFS_ILOG_BROOT) &&
		 ip->i_d.di_format != XFS_DINODE_FMT_BTREE)
		logflags &= ~XFS_ILOG_BROOT;
	if (logflags)
		xfs_trans_log_inode(tp, ip, logflags);
	if (cur) {
		firstblock = cur->bc_private.b.firstblock;
		cur->bc_private.b.allocated = 0;
		xfs_btree_del_cursor(cur);
	}
	kmem_check();
	return firstblock;
}

/*
 * Fcntl interface to xfs_bmapi.
 */
int						/* error code */
xfs_getbmap(
	vnode_t			*vp,		/* vnode pointer */
	struct getbmap		*bmv,		/* user bmap structure */
	void			*ap)		/* pointer to user's array */
{
	__int64_t		bmvend;		/* last block requested */
	int			error;		/* return value */
	int			i;		/* extent number */
	xfs_inode_t		*ip;		/* xfs incore inode pointer */
	int			lock;		/* lock state */
	xfs_bmbt_irec_t		*map;		/* buffer for user's data */
	int			nex;		/* # of extents can do */
	int			nmap;		/* number of map entries */
	struct getbmap		out;		/* output structure */
	xfs_trans_t		*tp;		/* transaction pointer */

	ip = XFS_VTOI(vp);
	if (ip->i_d.di_format != XFS_DINODE_FMT_EXTENTS &&
	    ip->i_d.di_format != XFS_DINODE_FMT_BTREE)
		return XFS_ERROR(EINVAL);
	if (bmv->bmv_length == -1)
		bmv->bmv_length = OFFTOBB(ip->i_d.di_size);
	bmvend = bmv->bmv_offset + bmv->bmv_length;
	nex = bmv->bmv_count - 1;
	ASSERT(nex > 0);
	if (bmv->bmv_length < 0)
		return XFS_ERROR(EINVAL);
	if (bmv->bmv_length == 0) {
		bmv->bmv_entries = 0;
		return 0;
	}
	xfs_ilock(ip, XFS_IOLOCK_SHARED);
	if (ip->i_delayed_blks)
		pflushvp(vp, (off_t)ip->i_d.di_size, 0);
	lock = xfs_ilock_map_shared(ip);
	/*
	 * Don't let nex be bigger than the number of extents
	 * we can have assuming alternating holes and real extents.
	 */
	if (nex > ip->i_d.di_nextents * 2 + 1)
		nex = ip->i_d.di_nextents * 2 + 1;
	/*
	 * This is potentially a lot of memory.
	 * We need to put this whole thing in a loop, to limit the memory.
	 */
	map = kmem_alloc(nex * sizeof(*map), KM_SLEEP);
	bmv->bmv_entries = 0;
	nmap = nex;
	(void)xfs_bmapi(NULL, ip, XFS_BB_TO_FSBT(ip->i_mount, bmv->bmv_offset),
		XFS_BB_TO_FSB(ip->i_mount, bmv->bmv_length), 0, NULLFSBLOCK, 0,
		map, &nmap, NULL);
	xfs_iunlock_map_shared(ip, lock);
	xfs_iunlock(ip, XFS_IOLOCK_SHARED);
	ASSERT(nmap <= nex);
	for (error = i = 0; i < nmap; i++) {
		out.bmv_offset = XFS_FSB_TO_BB(ip->i_mount, map[i].br_startoff);
		out.bmv_length =
			XFS_FSB_TO_BB(ip->i_mount, map[i].br_blockcount);
		ASSERT(map[i].br_startblock != DELAYSTARTBLOCK);
		if (map[i].br_startblock == HOLESTARTBLOCK)
			out.bmv_block = -1;
		else
			out.bmv_block = XFS_FSB_TO_BB(ip->i_mount,
				map[i].br_startblock);
		if (copyout(&out, ap, sizeof(out))) {
			error = XFS_ERROR(EFAULT);
			break;
		}
		bmv->bmv_offset = out.bmv_offset + out.bmv_length;
		bmv->bmv_length = bmvend - bmv->bmv_offset;
		bmv->bmv_entries++;
		ap = (void *)((struct getbmap *)ap + 1);
	}
	kmem_free(map, nex * sizeof(*map));
	return error;
}
