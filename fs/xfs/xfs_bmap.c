#ident	"$Revision: 1.22 $"

#include <sys/param.h>
#include <sys/buf.h>
#include <sys/vnode.h>
#include <sys/uuid.h>
#include <sys/debug.h>
#include <sys/kmem.h>
#include <stddef.h>
#ifdef SIM
#include <bstring.h>
#else
#include <sys/systm.h>
#endif
#include "xfs_types.h"
#include "xfs_inum.h"
#include "xfs_log.h"
#include "xfs_trans.h"
#include "xfs_sb.h"
#include "xfs_ag.h"
#include "xfs_mount.h"
#include "xfs_alloc.h"
#include "xfs_ialloc.h"
#include "xfs_bmap.h"
#include "xfs_btree.h"
#include "xfs_dinode.h"
#include "xfs_inode_item.h"
#include "xfs_inode.h"
#include "xfs_extfree_item.h"
#ifdef SIM
#include "sim.h"
#endif

#if !defined(SIM) || !defined(XFSDEBUG)
#define	kmem_check()	/* dummy for memory-allocation checking */
#endif

zone_t *xfs_bmap_free_zone;

/*
 * Prototypes for internal btree functions.
 */

#ifdef XFSDEBUG
STATIC void
xfs_bmbt_kcheck(
	xfs_btree_cur_t *);

STATIC void
xfs_bmbt_kcheck_body(
	xfs_btree_cur_t *,
	xfs_btree_lblock_t *,
	int,
	xfs_bmbt_key_t *);

STATIC void
xfs_bmbt_kcheck_btree(
	xfs_btree_cur_t *,
	xfs_bmbt_ptr_t,
	int,
	xfs_bmbt_key_t *);

STATIC void
xfs_bmbt_rcheck(
	xfs_btree_cur_t *);

STATIC xfs_bmbt_ptr_t
xfs_bmbt_rcheck_body(
	xfs_btree_cur_t *,
	xfs_btree_lblock_t *,
	xfs_bmbt_ptr_t *,
	void *,
	int);

STATIC xfs_bmbt_ptr_t
xfs_bmbt_rcheck_btree(
	xfs_btree_cur_t *,
	xfs_bmbt_ptr_t,
	xfs_bmbt_ptr_t *,
	void *,
	int);

STATIC void
xfs_bmap_check_extents(
	xfs_inode_t		*ip);
#else
#define	xfs_bmbt_kcheck(a)
#define	xfs_bmbt_rcheck(a)
#define	xfs_bmap_check_extents(a)
#endif

STATIC int
xfs_bmbt_decrement(
	xfs_btree_cur_t *,
	int);

STATIC int
xfs_bmbt_delete(
	xfs_btree_cur_t *);

STATIC int
xfs_bmbt_delrec(
	xfs_btree_cur_t *,
	int);

STATIC int
xfs_bmbt_get_rec(
	xfs_btree_cur_t *,
	xfs_fsblock_t *,
	xfs_fsblock_t *,
	xfs_extlen_t *);

STATIC int
xfs_bmbt_increment(
	xfs_btree_cur_t *,
	int);

STATIC int
xfs_bmbt_insert(
	xfs_btree_cur_t *);

STATIC int
xfs_bmbt_insrec(
	xfs_btree_cur_t *,
	int,
	xfs_bmbt_ptr_t *,
	xfs_bmbt_rec_t *,
	xfs_btree_cur_t **);

STATIC void
xfs_bmbt_log_block(
	xfs_btree_cur_t *,
	buf_t *,
	int);

STATIC void
xfs_bmbt_log_keys(
	xfs_btree_cur_t *,
	buf_t *,
	int,
	int);

STATIC void
xfs_bmbt_log_ptrs(
	xfs_btree_cur_t *,
	buf_t *,
	int,
	int);

STATIC void
xfs_bmbt_log_recs(
	xfs_btree_cur_t *,
	buf_t *,
	int,
	int);

STATIC int
xfs_bmbt_lookup(
	xfs_btree_cur_t *,
	xfs_lookup_t);

STATIC int
xfs_bmbt_lookup_eq(
	xfs_btree_cur_t *,
	xfs_fsblock_t,
	xfs_fsblock_t,
	xfs_extlen_t);

STATIC int
xfs_bmbt_lookup_ge(
	xfs_btree_cur_t *,
	xfs_fsblock_t,
	xfs_fsblock_t,
	xfs_extlen_t);

STATIC int
xfs_bmbt_lookup_le(
	xfs_btree_cur_t *,
	xfs_fsblock_t,
	xfs_fsblock_t,
	xfs_extlen_t);

STATIC int
xfs_bmbt_lshift(
	xfs_btree_cur_t *,
	int);

STATIC int
xfs_bmbt_rshift(
	xfs_btree_cur_t *,
	int);

STATIC int
xfs_bmbt_split(
	xfs_btree_cur_t *,
	int,
	xfs_bmbt_ptr_t *,
	xfs_bmbt_key_t *,
	xfs_btree_cur_t **);

STATIC int
xfs_bmbt_update(
	xfs_btree_cur_t *,
	xfs_fsblock_t,
	xfs_fsblock_t,
	xfs_extlen_t);

STATIC void
xfs_bmbt_updkey(
	xfs_btree_cur_t *,
	xfs_bmbt_key_t *,
	int);

/*
 * Prototypes for internal bmap routines.
 */

STATIC int				/* inode logging flags */
xfs_bmap_add_extent(
	xfs_inode_t		*ip,	/* incore inode pointer */
	xfs_extnum_t		idx,	/* extent number to update/insert */
	xfs_btree_cur_t		*cur,	/* if null, not a btree */
	xfs_bmbt_irec_t		*new);	/* new data to put in extent list */

STATIC int
xfs_bmap_add_extent_delay_real(
	xfs_inode_t		*ip,	/* incore inode pointer */
	xfs_extnum_t		idx,	/* extent number to update/insert */
	xfs_btree_cur_t		*cur,	/* if null, not a btree */
	xfs_bmbt_irec_t		*new);	/* new data to put in extent list */

STATIC int
xfs_bmap_add_extent_hole_delay(
	xfs_inode_t		*ip,	/* incore inode pointer */
	xfs_extnum_t		idx,	/* extent number to update/insert */
	xfs_btree_cur_t		*cur,	/* if null, not a btree */
	xfs_bmbt_irec_t		*new);	/* new data to put in extent list */

STATIC int
xfs_bmap_add_extent_hole_real(
	xfs_inode_t		*ip,	/* incore inode pointer */
	xfs_extnum_t		idx,	/* extent number to update/insert */
	xfs_btree_cur_t		*cur,	/* if null, not a btree */
	xfs_bmbt_irec_t		*new);	/* new data to put in extent list */

/*
 * Add the extent to the list of extents to be free at transaction end.
 */
STATIC void
xfs_bmap_add_free(
	xfs_fsblock_t		bno,		/* fs block number of extent */
	xfs_extlen_t		len,		/* length of extent */
	xfs_bmap_free_t		**flist);	/* list of extents */

STATIC xfs_fsblock_t
xfs_bmap_alloc(
	xfs_trans_t		*tp,
	xfs_inode_t		*ip,
	int			eof,
	xfs_bmbt_irec_t		*prevp,
	xfs_bmbt_irec_t		*gotp,
	xfs_fsblock_t		firstblock,
	xfs_extlen_t		*alen,
	xfs_extlen_t		total,
	xfs_fsblock_t		off,
	int			wasdel);

STATIC int
xfs_bmap_btree_to_extents(
	xfs_trans_t		*tp,
	xfs_inode_t		*ip,
	xfs_btree_cur_t		*cur);

STATIC int				/* inode logging flags */
xfs_bmap_del_extent(
	xfs_inode_t		*ip,	/* incore inode pointer */
	xfs_extnum_t		idx,	/* extent number to update/insert */
	xfs_bmap_free_t		**flist,/* list of extents to be freed */
	xfs_btree_cur_t		*cur,	/* if null, not a btree */
	xfs_bmbt_irec_t		*new);	/* new data to put in extent list */

STATIC void
xfs_bmap_del_free(
	xfs_bmap_free_t		**flist,
	xfs_bmap_free_t		*prev,
	xfs_bmap_free_t		*free);

STATIC void
xfs_bmap_delete_exlist(
	xfs_inode_t	*ip,
	xfs_extnum_t	idx,
	xfs_extnum_t	count);

STATIC xfs_fsblock_t
xfs_bmap_extents_to_btree(
	xfs_trans_t	*tp,
	xfs_inode_t	*ip,
	xfs_fsblock_t	firstblock,
	xfs_bmap_free_t	**flist);

STATIC void
xfs_bmap_insert_exlist(
	xfs_inode_t	*ip,
	xfs_extnum_t	idx,
	xfs_extnum_t	count,
	xfs_bmbt_irec_t	*new);

STATIC xfs_fsblock_t
xfs_bmap_local_to_extents(
	xfs_trans_t	*tp,
	xfs_inode_t	*ip,
	xfs_fsblock_t	firstblock,
	xfs_extlen_t	total);
 
STATIC xfs_bmbt_rec_t *
xfs_bmap_search_extents(
	xfs_inode_t	*ip,
	xfs_fsblock_t	bno,
	int		*eofp,
	xfs_extnum_t	*lastxp,
	xfs_bmbt_irec_t	*gotp,
	xfs_bmbt_irec_t	*prevp);

/*
 * Debug functions.
 * These are macros in a non-debug kernel.
 */

#ifdef XFSDEBUG
int
XFS_BMAP_RBLOCK_DSIZE(
	int		lev,
	xfs_btree_cur_t	*cur)
{
	return cur->bc_private.b.inodesize - (int)sizeof(xfs_dinode_core_t);
}

int
XFS_BMAP_RBLOCK_ISIZE(
	int		lev,
	xfs_btree_cur_t	*cur)
{
	return (int)cur->bc_private.b.ip->i_broot_bytes;
}

int
XFS_BMAP_IBLOCK_SIZE(
	int		lev,
	xfs_btree_cur_t	*cur)
{
	return 1 << cur->bc_blocklog;
}

int
XFS_BMAP_BLOCK_DSIZE(
	int		lev,
	xfs_btree_cur_t	*cur)
{
	int		rval;

	if (lev == cur->bc_nlevels - 1)
		rval = XFS_BMAP_RBLOCK_DSIZE(lev, cur);
	else
		rval = XFS_BMAP_IBLOCK_SIZE(lev, cur);
	return rval;
}

int
XFS_BMAP_BLOCK_ISIZE(
	int		lev,
	xfs_btree_cur_t	*cur)
{
	int		rval;

	if (lev == cur->bc_nlevels - 1)
		rval = XFS_BMAP_RBLOCK_ISIZE(lev, cur);
	else
		rval = XFS_BMAP_IBLOCK_SIZE(lev, cur);
	return rval;
}

int
XFS_BMAP_BLOCK_DMAXRECS(
	int		lev,
	xfs_btree_cur_t	*cur)
{
	int		dsize;
	int		rval;

	dsize = XFS_BMAP_BLOCK_DSIZE(lev, cur);
	rval = XFS_BTREE_BLOCK_MAXRECS(dsize, xfs_bmbt, lev == 0);
	return rval;
}

int
XFS_BMAP_BLOCK_IMAXRECS(
	int		lev,
	xfs_btree_cur_t	*cur)
{
	int		isize;
	int		rval;

	isize = XFS_BMAP_BLOCK_ISIZE(lev, cur);
	rval = XFS_BTREE_BLOCK_MAXRECS(isize, xfs_bmbt, lev == 0);
	return rval;
}

int
XFS_BMAP_BLOCK_DMINRECS(
	int	   	lev,
	xfs_btree_cur_t	*cur)
{
	int		dsize;
	int		rval;

	dsize = XFS_BMAP_BLOCK_DSIZE(lev, cur);
	rval = XFS_BTREE_BLOCK_MINRECS(dsize, xfs_bmbt, lev == 0);
	return rval;
}

int
XFS_BMAP_BLOCK_IMINRECS(
	int		lev,
	xfs_btree_cur_t	*cur)
{
	int		isize;
	int		rval;

	isize = XFS_BMAP_BLOCK_ISIZE(lev, cur);
	rval = XFS_BTREE_BLOCK_MINRECS(isize, xfs_bmbt, lev == 0);
	return rval;
}

xfs_bmbt_rec_t *
XFS_BMAP_REC_DADDR(
	xfs_btree_lblock_t	*bb,
	int			i,
	xfs_btree_cur_t		*cur)
{
	int			dsize;
	xfs_bmbt_rec_t		*rval;

	dsize = XFS_BMAP_BLOCK_DSIZE(bb->bb_level, cur);
	rval = XFS_BTREE_REC_ADDR(dsize, xfs_bmbt, bb, i);
	return rval;
}

xfs_bmbt_rec_t *
XFS_BMAP_REC_IADDR(
	xfs_btree_lblock_t	*bb,
	int			i,
	xfs_btree_cur_t		*cur)
{
	int			isize;
	xfs_bmbt_rec_t		*rval;

	isize = XFS_BMAP_BLOCK_ISIZE(bb->bb_level, cur);
	rval = XFS_BTREE_REC_ADDR(isize, xfs_bmbt, bb, i);
	return rval;
}

xfs_bmbt_key_t *
XFS_BMAP_KEY_DADDR(
	xfs_btree_lblock_t	*bb,
	int			i,
	xfs_btree_cur_t		*cur)
{
	int			dsize;
	xfs_bmbt_key_t		*rval;

	dsize = XFS_BMAP_BLOCK_DSIZE(bb->bb_level, cur);
	rval = XFS_BTREE_KEY_ADDR(dsize, xfs_bmbt, bb, i);
	return rval;
}

xfs_bmbt_key_t *
XFS_BMAP_KEY_IADDR(
	xfs_btree_lblock_t	*bb,
	int			i,
	xfs_btree_cur_t		*cur)
{
	int			isize;
	xfs_bmbt_key_t		*rval;

	isize = XFS_BMAP_BLOCK_ISIZE(bb->bb_level, cur);
	rval = XFS_BTREE_KEY_ADDR(isize, xfs_bmbt, bb, i);
	return rval;
}

xfs_bmbt_ptr_t *
XFS_BMAP_PTR_DADDR(
	xfs_btree_lblock_t	*bb,
	int			i,
	xfs_btree_cur_t		*cur)
{
	int			dsize;
	xfs_bmbt_ptr_t		*rval;

	dsize = XFS_BMAP_BLOCK_DSIZE(bb->bb_level, cur);
	rval = XFS_BTREE_PTR_ADDR(dsize, xfs_bmbt, bb, i);
	return rval;
}

xfs_bmbt_ptr_t *
XFS_BMAP_PTR_IADDR(
	xfs_btree_lblock_t	*bb,
	int			i,
	xfs_btree_cur_t		*cur)
{
	int			isize;
	xfs_bmbt_ptr_t		*rval;

	isize = XFS_BMAP_BLOCK_ISIZE(bb->bb_level, cur);
	rval = XFS_BTREE_PTR_ADDR(isize, xfs_bmbt, bb, i);
	return rval;
}

/*
 * These are to be used when we know the size of the block and
 * we don't have a cursor.
 */
int
XFS_BMAP_BROOT_SIZE(
	int	isz)
{
	return isz - sizeof(xfs_dinode_core_t);
}

xfs_bmbt_rec_t *
XFS_BMAP_BROOT_REC_ADDR(
	xfs_btree_lblock_t	*bb,
	int			i,
	int			isz)
{
	xfs_bmbt_rec_t		*rval;

	rval = XFS_BTREE_REC_ADDR(isz, xfs_bmbt, bb, i);
	return rval;
}

xfs_bmbt_key_t *
XFS_BMAP_BROOT_KEY_ADDR(
	xfs_btree_lblock_t	*bb,
	int			i,
	int			isz)
{
	xfs_bmbt_key_t		*rval;

	rval = XFS_BTREE_KEY_ADDR(isz, xfs_bmbt, bb, i);
	return rval;
}

xfs_bmbt_ptr_t *
XFS_BMAP_BROOT_PTR_ADDR(
	xfs_btree_lblock_t	*bb,
	int			i,
	int			isz)
{
	xfs_bmbt_ptr_t		*rval;

	rval = XFS_BTREE_PTR_ADDR(isz, xfs_bmbt, bb, i);
	return rval;
}

int
XFS_BMAP_BROOT_NUMRECS(
	xfs_btree_lblock_t	*bb)
{
	return bb->bb_numrecs;
}

int
XFS_BMAP_BROOT_MAXRECS(
	int	sz)
{
	int	rval;

	rval = XFS_BTREE_BLOCK_MAXRECS(sz, xfs_bmbt, 0);
	return rval;
}

int
XFS_BMAP_BROOT_SPACE_CALC(
	int	nrecs)
{
	int	rval;

	rval = (int)(sizeof(xfs_btree_lblock_t) +
		     nrecs * (sizeof(xfs_bmbt_key_t) + sizeof(xfs_bmbt_ptr_t)));
	return rval;
}

int
XFS_BMAP_BROOT_SPACE(
	xfs_btree_lblock_t	*bb)
{
	int			rval;

	rval = XFS_BMAP_BROOT_SPACE_CALC(bb->bb_numrecs);
	return rval;
}

/*
 * Number of extent records that fit in the inode.
 */
int
XFS_BMAP_EXT_MAXRECS(
	int	isz)
{
	int	rval;

	rval = XFS_BMAP_BROOT_SIZE(isz) / sizeof(xfs_bmbt_rec_t);
	return rval;
}

xfs_btree_lblock_t *
xfs_bmbt_get_block(
	xfs_btree_cur_t		*cur,
	int			level,
	buf_t			**bpp)
{
	xfs_btree_lblock_t	*rval;

	if (level < cur->bc_nlevels - 1) {
		*bpp = cur->bc_bufs[level];
		rval = xfs_buf_to_lblock(*bpp);
	} else {
		*bpp = 0;
		rval = cur->bc_private.b.ip->i_broot;
	}
	return rval;
}
#endif	/* XFSDEBUG */

/*
 * Internal functions.
 */

/*
 * Decrement cursor by one record at the level.
 * For nonzero levels the leaf-ward information is untouched.
 */
STATIC int
xfs_bmbt_decrement(
	xfs_btree_cur_t		*cur,
	int			level)
{
	xfs_btree_lblock_t	*block;
	buf_t			*buf;
	xfs_bmbt_ptr_t		fsbno;
	int			lev;
	xfs_mount_t		*mp;
	xfs_trans_t		*tp;

	if (--cur->bc_ptrs[level] > 0)
		return 1;
	block = xfs_bmbt_get_block(cur, level, &buf);
	xfs_btree_check_lblock(cur, block, level);
	if (block->bb_leftsib == NULLFSBLOCK)
		return 0;
	for (lev = level + 1; lev < cur->bc_nlevels; lev++) {
		if (--cur->bc_ptrs[lev] > 0)
			break;
	}
	if (lev == cur->bc_nlevels)
		return 0;
	tp = cur->bc_tp;
	mp = cur->bc_mp;
	for (; lev > level; lev--) {
		block = xfs_bmbt_get_block(cur, lev, &buf);
		xfs_btree_check_lblock(cur, block, lev);
		fsbno = *XFS_BMAP_PTR_IADDR(block, cur->bc_ptrs[lev], cur);
		buf = xfs_btree_read_bufl(mp, tp, fsbno, 0);
		xfs_btree_setbuf(cur, lev - 1, buf);
		block = xfs_buf_to_lblock(buf);
		xfs_btree_check_lblock(cur, block, lev - 1);
		cur->bc_ptrs[lev - 1] = block->bb_numrecs;
	}
	return 1;
}

/*
 * Delete the record pointed to by cur.
 */
STATIC int
xfs_bmbt_delete(
	xfs_btree_cur_t	*cur)
{
	int		i;
	int		level;

	for (level = 0, i = 2; i == 2; level++)
		i = xfs_bmbt_delrec(cur, level);
	xfs_bmbt_kcheck(cur);
	return i;
}

/*
 * Delete record pointed to by cur/level.
 */
STATIC int
xfs_bmbt_delrec(
	xfs_btree_cur_t		*cur,
	int			level)
{
	xfs_btree_lblock_t	*block;
	xfs_bmbt_ptr_t		bno;
	buf_t			*buf;
	xfs_btree_lblock_t	*cblock;
	xfs_bmbt_key_t		*ckp;
	xfs_bmbt_ptr_t		*cpp;
	int			first;
	int			i;
	xfs_inode_t		*ip;
	xfs_bmbt_key_t		key;
	xfs_bmbt_key_t		*kp;
	int			last;
	xfs_bmbt_ptr_t		lbno;
	buf_t			*lbuf;
	xfs_btree_lblock_t	*left;
	xfs_bmbt_key_t		*lkp;
	xfs_bmbt_ptr_t		*lpp;
	int			lrecs;
	xfs_bmbt_rec_t		*lrp;
	xfs_mount_t		*mp;
	xfs_bmbt_ptr_t		*pp;
	int			ptr;
	xfs_bmbt_ptr_t		rbno;
	buf_t			*rbuf;
	xfs_btree_lblock_t	*right;
	xfs_bmbt_key_t		*rkp;
	xfs_bmbt_rec_t		*rp;
	xfs_bmbt_ptr_t		*rpp;
	xfs_btree_lblock_t	*rrblock;
	buf_t			*rrbuf;
	int			rrecs;
	xfs_bmbt_rec_t		*rrp;
	xfs_sb_t		*sbp;
	xfs_btree_cur_t		*tcur;
	xfs_trans_t		*tp;

	xfs_bmbt_rcheck(cur);
	tp = cur->bc_tp;
	mp = cur->bc_mp;
	sbp = &mp->m_sb;
	ptr = cur->bc_ptrs[level];
	if (ptr == 0)
		return 0;
	block = xfs_bmbt_get_block(cur, level, &buf);
	xfs_btree_check_lblock(cur, block, level);
	if (ptr > block->bb_numrecs)
		return 0;
	if (level > 0) {
		kp = XFS_BMAP_KEY_IADDR(block, 1, cur);
		pp = XFS_BMAP_PTR_IADDR(block, 1, cur);
		for (i = ptr; i < block->bb_numrecs; i++) {
			kp[i - 1] = kp[i];
			xfs_btree_check_lptr(cur, pp[i], level);
			pp[i - 1] = pp[i];
		}
		if (ptr < i) {
			xfs_bmbt_log_ptrs(cur, buf, ptr, i - 1);
			xfs_bmbt_log_keys(cur, buf, ptr, i - 1);
		}
	} else {
		rp = XFS_BMAP_REC_IADDR(block, 1, cur);
		for (i = ptr; i < block->bb_numrecs; i++)
			rp[i - 1] = rp[i];
		if (ptr < i)
			xfs_bmbt_log_recs(cur, buf, ptr, i - 1);
		if (ptr == 1) {
			key.br_startoff = xfs_bmbt_get_startoff(rp);
			kp = &key;
		}
	}
	block->bb_numrecs--;
	xfs_bmbt_log_block(cur, buf, XFS_BB_NUMRECS);
	/*
	 * We just did a join at the previous level.
	 * Make the cursor point to the good (left) key.
	 */
	if (level > 0)
		xfs_bmbt_decrement(cur, level);
	if (level == cur->bc_nlevels - 1) {
		/*
		 * Only do this if the next level will fit.
		 * Then the data must be copied up to the inode,
		 * instead of freeing the root you free the next level
		 */
		buf = cur->bc_bufs[level - 1];
		cblock = xfs_buf_to_lblock(buf);
		ip = cur->bc_private.b.ip;
		if (block->bb_numrecs == 1 && level > 1 &&
		    cblock->bb_numrecs <= XFS_BMAP_BLOCK_DMAXRECS(level, cur)) {
			ASSERT(cblock->bb_leftsib == NULLFSBLOCK);
			ASSERT(cblock->bb_rightsib == NULLFSBLOCK);
			if (i = (int)(cblock->bb_numrecs - XFS_BMAP_BLOCK_IMAXRECS(level, cur))) {
				xfs_iroot_realloc(ip, i);
				block = ip->i_broot;
			}
			*block = *cblock;
			kp = XFS_BMAP_KEY_IADDR(block, 1, cur);
			ckp = XFS_BMAP_KEY_IADDR(cblock, 1, cur);
			bcopy((caddr_t)ckp, (caddr_t)kp, block->bb_numrecs * (int)sizeof(*kp));
			pp = XFS_BMAP_PTR_IADDR(block, 1, cur);
			cpp = XFS_BMAP_PTR_IADDR(cblock, 1, cur);
			bcopy((caddr_t)cpp, (caddr_t)pp, block->bb_numrecs * (int)sizeof(*pp));
			xfs_bmap_add_free(xfs_daddr_to_fsb(sbp, buf->b_blkno), 1, cur->bc_private.b.flist);
			xfs_trans_log_inode(tp, ip, XFS_ILOG_BROOT);
			xfs_btree_setbuf(cur, level - 1, 0);
			cur->bc_nlevels--;
		} else
			xfs_iroot_realloc(ip, -1);
		xfs_bmbt_rcheck(cur);
		return 1;
	}
	if (ptr == 1)
		xfs_bmbt_updkey(cur, kp, level + 1);
	xfs_bmbt_rcheck(cur);
	if (block->bb_numrecs >= XFS_BMAP_BLOCK_IMINRECS(level, cur))
		return 1;
	rbno = block->bb_rightsib;
	lbno = block->bb_leftsib;
	ASSERT(rbno != NULLFSBLOCK || lbno != NULLFSBLOCK);
	tcur = xfs_btree_dup_cursor(cur);
	bno = NULLFSBLOCK;
	buf = cur->bc_bufs[level];
	if (rbno != NULLFSBLOCK) {
		xfs_btree_lastrec(tcur, level);
		xfs_bmbt_increment(tcur, level);
		xfs_btree_lastrec(tcur, level);
		rbuf = tcur->bc_bufs[level];
		right = xfs_buf_to_lblock(rbuf);
		xfs_btree_check_lblock(cur, right, level);
		bno = right->bb_leftsib;
		if (right->bb_numrecs - 1 >= XFS_BMAP_BLOCK_IMINRECS(level, cur)) {
			if (xfs_bmbt_lshift(tcur, level)) {
				ASSERT(block->bb_numrecs >= XFS_BMAP_BLOCK_IMINRECS(level, tcur));
				xfs_btree_del_cursor(tcur);
				return 1;
			}
		}
		rrecs = right->bb_numrecs;
		if (lbno != NULLFSBLOCK) {
			xfs_btree_firstrec(tcur, level);
			xfs_bmbt_decrement(tcur, level);
		}
	}
	if (lbno != NULLFSBLOCK) {
		xfs_btree_firstrec(tcur, level);
		xfs_bmbt_decrement(tcur, level);	/* to last */
		xfs_btree_firstrec(tcur, level);
		lbuf = tcur->bc_bufs[level];
		left = xfs_buf_to_lblock(lbuf);
		xfs_btree_check_lblock(cur, left, level);
		bno = left->bb_rightsib;
		if (left->bb_numrecs - 1 >= XFS_BMAP_BLOCK_IMINRECS(level, cur)) {
			if (xfs_bmbt_rshift(tcur, level)) {
				ASSERT(block->bb_numrecs >= XFS_BMAP_BLOCK_IMINRECS(level, tcur));
				xfs_btree_del_cursor(tcur);
				cur->bc_ptrs[level]++;
				return 1;
			}
		}
		lrecs = left->bb_numrecs;
	}
	xfs_btree_del_cursor(tcur);
	ASSERT(bno != NULLFSBLOCK);
	if (lbno != NULLFSBLOCK &&
	    lrecs + block->bb_numrecs <= XFS_BMAP_BLOCK_IMAXRECS(level, cur)) {
		rbno = bno;
		right = block;
		rbuf = buf;
		lbuf = xfs_btree_read_bufl(mp, tp, lbno, 0);
		left = xfs_buf_to_lblock(lbuf);
		xfs_btree_check_lblock(cur, left, level);
	} else if (rbno != NULLFSBLOCK &&
		   rrecs + block->bb_numrecs <= XFS_BMAP_BLOCK_IMAXRECS(level, cur)) {
		lbno = bno;
		left = block;
		lbuf = buf;
		rbuf = xfs_btree_read_bufl(mp, tp, rbno, 0);
		right = xfs_buf_to_lblock(rbuf);
		xfs_btree_check_lblock(cur, right, level);
	} else {
		xfs_bmbt_rcheck(cur);
		return 1;
	}
	if (level > 0) {
		lkp = XFS_BMAP_KEY_IADDR(left, left->bb_numrecs + 1, cur);
		lpp = XFS_BMAP_PTR_IADDR(left, left->bb_numrecs + 1, cur);
		rkp = XFS_BMAP_KEY_IADDR(right, 1, cur);
		rpp = XFS_BMAP_PTR_IADDR(right, 1, cur);
		for (i = 0; i < right->bb_numrecs; i++) {
			lkp[i] = rkp[i];
			xfs_btree_check_lptr(cur, rpp[i], level);
			lpp[i] = rpp[i];
		}
		xfs_bmbt_log_keys(cur, lbuf, left->bb_numrecs + 1, left->bb_numrecs + right->bb_numrecs);
		xfs_bmbt_log_ptrs(cur, lbuf, left->bb_numrecs + 1, left->bb_numrecs + right->bb_numrecs);
	} else {
		lrp = XFS_BMAP_REC_IADDR(left, left->bb_numrecs + 1, cur);
		rrp = XFS_BMAP_REC_IADDR(right, 1, cur);
		for (i = 0; i < right->bb_numrecs; i++)
			lrp[i] = rrp[i];
		xfs_bmbt_log_recs(cur, lbuf, left->bb_numrecs + 1, left->bb_numrecs + right->bb_numrecs);
	}
	if (buf != lbuf) {
		xfs_btree_setbuf(cur, level, lbuf);
		cur->bc_ptrs[level] += left->bb_numrecs;
	} else if (level + 1 < cur->bc_nlevels)
		xfs_bmbt_increment(cur, level + 1);
	left->bb_numrecs += right->bb_numrecs;
	left->bb_rightsib = right->bb_rightsib;
	xfs_bmbt_log_block(cur, lbuf, XFS_BB_RIGHTSIB | XFS_BB_NUMRECS);
	if (left->bb_rightsib != NULLFSBLOCK) {
		rrbuf = xfs_btree_read_bufl(mp, tp, left->bb_rightsib, 0);
		rrblock = xfs_buf_to_lblock(rrbuf);
		xfs_btree_check_lblock(cur, rrblock, level);
		rrblock->bb_leftsib = lbno;
		xfs_bmbt_log_block(cur, rrbuf, XFS_BB_LEFTSIB);
	}
	xfs_bmap_add_free(xfs_daddr_to_fsb(sbp, rbuf->b_blkno), 1, cur->bc_private.b.flist);
	xfs_bmbt_rcheck(cur);
	return 2;
}

/* 
 * Get the data from the pointed-to record.
 */
STATIC int
xfs_bmbt_get_rec(
	xfs_btree_cur_t		*cur,
	xfs_fsblock_t		*off,
	xfs_fsblock_t		*bno,
	xfs_extlen_t		*len)
{
	xfs_btree_lblock_t	*block;
	buf_t			*buf;
	int			ptr;
	xfs_bmbt_rec_t		*rp;

	block = xfs_bmbt_get_block(cur, 0, &buf);
	ptr = cur->bc_ptrs[0];
	xfs_btree_check_lblock(cur, block, 0);
	if (ptr > block->bb_numrecs || ptr <= 0)
		return 0;
	rp = XFS_BMAP_REC_IADDR(block, ptr, cur);
	*off = xfs_bmbt_get_startoff(rp);
	*bno = xfs_bmbt_get_startblock(rp);
	*len = xfs_bmbt_get_blockcount(rp);
	return 1;
}

/*
 * Increment cursor by one record at the level.
 * For nonzero levels the leaf-ward information is untouched.
 */
STATIC int
xfs_bmbt_increment(
	xfs_btree_cur_t		*cur,
	int			level)
{
	xfs_btree_lblock_t	*block;
	buf_t			*buf;
	xfs_bmbt_ptr_t		fsbno;
	int			lev;
	xfs_mount_t		*mp;
	xfs_trans_t		*tp;

	block = xfs_bmbt_get_block(cur, level, &buf);
	xfs_btree_check_lblock(cur, block, level);
	if (++cur->bc_ptrs[level] <= block->bb_numrecs)
		return 1;
	if (block->bb_rightsib == NULLFSBLOCK)
		return 0;
	for (lev = level + 1; lev < cur->bc_nlevels; lev++) {
		block = xfs_bmbt_get_block(cur, lev, &buf);
		xfs_btree_check_lblock(cur, block, lev);
		if (++cur->bc_ptrs[lev] <= block->bb_numrecs)
			break;
	}
	if (lev == cur->bc_nlevels)
		return 0;
	tp = cur->bc_tp;
	mp = cur->bc_mp;
	for (; lev > level; lev--) {
		block = xfs_bmbt_get_block(cur, lev, &buf);
		xfs_btree_check_lblock(cur, block, lev);
		fsbno = *XFS_BMAP_PTR_IADDR(block, cur->bc_ptrs[lev], cur);
		buf = xfs_btree_read_bufl(mp, tp, fsbno, 0);
		xfs_btree_setbuf(cur, lev - 1, buf);
		cur->bc_ptrs[lev - 1] = 1;
	}
	return 1;
}

/*
 * Insert the current record at the point referenced by cur.
 */
STATIC int
xfs_bmbt_insert(
	xfs_btree_cur_t	*cur)
{
	int		i;
	int		level;
	xfs_bmbt_ptr_t	nbno;
	xfs_btree_cur_t	*ncur;
	xfs_bmbt_rec_t	nrec;
	xfs_btree_cur_t	*pcur;

	level = 0;
	nbno = NULLFSBLOCK;
	xfs_bmbt_set_all(&nrec, cur->bc_rec.b);
	ncur = (xfs_btree_cur_t *)0;
	pcur = cur;
	do {
		i = xfs_bmbt_insrec(pcur, level++, &nbno, &nrec, &ncur);
		if (pcur != cur && (ncur || nbno == NULLFSBLOCK)) {
			cur->bc_nlevels = pcur->bc_nlevels;
			xfs_btree_del_cursor(pcur);
		}
		if (ncur) {
			pcur = ncur;
			ncur = (xfs_btree_cur_t *)0;
		}
	} while (nbno != NULLFSBLOCK);
	return i;
}

/*
 * Insert one record/level.  Return information to the caller
 * allowing the next level up to proceed if necessary.
 */
STATIC int
xfs_bmbt_insrec(
	xfs_btree_cur_t		*cur,
	int			level,
	xfs_bmbt_ptr_t		*bnop,
	xfs_bmbt_rec_t		*recp,
	xfs_btree_cur_t		**curp)
{
	xfs_fsblock_t		askbno;
	xfs_btree_lblock_t	*block;
	buf_t			*buf;
	xfs_btree_lblock_t	*cblock;
	xfs_bmbt_ptr_t		cbno;
	xfs_bmbt_key_t		*ckp;
	xfs_bmbt_ptr_t		*cpp;
	xfs_bmbt_rec_t		*crp;
	int			i;
	xfs_inode_t		*ip;
	xfs_bmbt_key_t		key;
	xfs_bmbt_key_t		*kp;
	xfs_mount_t		*mp;
	xfs_bmbt_ptr_t		nbno;
	xfs_btree_cur_t		*ncur = (xfs_btree_cur_t *)0;
	xfs_bmbt_key_t		nkey;
	xfs_bmbt_rec_t		nrec;
	int			optr;
	xfs_bmbt_ptr_t		*pp;
	int			ptr;
	xfs_bmbt_rec_t		*rp;
	xfs_sb_t		*sbp;
	xfs_trans_t		*tp;

	ASSERT(level < cur->bc_nlevels);
	xfs_bmbt_rcheck(cur);
	key.br_startoff = xfs_bmbt_get_startoff(recp);
	optr = ptr = cur->bc_ptrs[level];
	if (ptr == 0)
		return 0;
	block = xfs_bmbt_get_block(cur, level, &buf);
	xfs_btree_check_lblock(cur, block, level);
#ifdef XFSDEBUG
	if (ptr <= block->bb_numrecs) {
		if (level == 0) {
			rp = XFS_BMAP_REC_IADDR(block, ptr, cur);
			xfs_btree_check_rec(XFS_BTNUM_BMAP, recp, rp);
		} else {
			kp = XFS_BMAP_KEY_IADDR(block, ptr, cur);
			xfs_btree_check_key(XFS_BTNUM_BMAP, &key, kp);
		}
	}
#endif
	tp = cur->bc_tp;
	mp = cur->bc_mp;
	sbp = &mp->m_sb;
	nbno = NULLFSBLOCK;
	if (block->bb_numrecs == XFS_BMAP_BLOCK_IMAXRECS(level, cur)) {
		ip = cur->bc_private.b.ip;
		if (block->bb_numrecs < XFS_BMAP_BLOCK_DMAXRECS(level, cur)) {
			/*
			 * A root block, that can be made bigger.
			 */
			xfs_iroot_realloc(ip, 1);
			block = xfs_bmbt_get_block(cur, level, &buf);
		} else if (level == cur->bc_nlevels - 1) {
			/*
			 * Copy the root into a real block.
			 */
			askbno = cur->bc_private.b.firstblock;
			cbno = xfs_alloc_extent(tp, askbno, 1, XFS_ALLOCTYPE_START_BNO, 0, 0);
			if (cbno == NULLFSBLOCK)
				return 0;
			buf = xfs_btree_get_bufl(mp, tp, cbno, 0);
			cblock = xfs_buf_to_lblock(buf);
			*cblock = *block;
			block->bb_level++;
			block->bb_numrecs = 1;
			cur->bc_nlevels++;
			cur->bc_ptrs[level + 1] = 1;
			kp = XFS_BMAP_KEY_IADDR(block, 1, cur);
			ckp = XFS_BMAP_KEY_IADDR(cblock, 1, cur);
			bcopy(kp, ckp, cblock->bb_numrecs * (int)sizeof(*kp));
			pp = XFS_BMAP_PTR_IADDR(block, 1, cur);
			cpp = XFS_BMAP_PTR_IADDR(cblock, 1, cur);
			bcopy(pp, cpp, cblock->bb_numrecs * (int)sizeof(*pp));
			*pp = cbno;
			xfs_iroot_realloc(ip, 1 - cblock->bb_numrecs);
			xfs_btree_setbuf(cur, level, buf);
			/*
			 * Do all this logging at the end so that 
			 * the root is at the right level.
			 */
			xfs_bmbt_log_block(cur, buf, XFS_BB_ALL_BITS);
			xfs_bmbt_log_keys(cur, buf, 1, cblock->bb_numrecs);
			xfs_bmbt_log_ptrs(cur, buf, 1, cblock->bb_numrecs);
			xfs_trans_log_inode(tp, ip, XFS_ILOG_BROOT);
			block = cblock;
		} else {
			if (xfs_bmbt_rshift(cur, level)) {
				/* nothing */
			} else if (xfs_bmbt_lshift(cur, level)) {
				optr = ptr = cur->bc_ptrs[level];
			} else if (xfs_bmbt_split(cur, level, &nbno, &nkey, &ncur)) {
				block = xfs_bmbt_get_block(cur, level, &buf);
				xfs_btree_check_lblock(cur, block, level);
				ptr = cur->bc_ptrs[level];
				xfs_bmbt_set_startoff(&nrec, nkey.br_startoff);
			} else
				return 0;
		}
	}
	if (level > 0) {
		kp = XFS_BMAP_KEY_IADDR(block, 1, cur);
		pp = XFS_BMAP_PTR_IADDR(block, 1, cur);
		for (i = block->bb_numrecs; i >= ptr; i--) {
			kp[i] = kp[i - 1];
			xfs_btree_check_lptr(cur, pp[i - 1], level);
			pp[i] = pp[i - 1];
		}
		xfs_btree_check_lptr(cur, *bnop, level);
		kp[i] = key;
		pp[i] = *bnop;
		block->bb_numrecs++;
		xfs_bmbt_log_keys(cur, buf, ptr, block->bb_numrecs);
		xfs_bmbt_log_ptrs(cur, buf, ptr, block->bb_numrecs);
	} else {
		rp = XFS_BMAP_REC_IADDR(block, 1, cur);
		for (i = block->bb_numrecs; i >= ptr; i--)
			rp[i] = rp[i - 1];
		rp[i] = *recp;
		block->bb_numrecs++;
		xfs_bmbt_log_recs(cur, buf, ptr, block->bb_numrecs);
	}
	xfs_bmbt_log_block(cur, buf, XFS_BB_NUMRECS);
#ifdef XFSDEBUG
	if (ptr < block->bb_numrecs) {
		if (level == 0)
			xfs_btree_check_rec(XFS_BTNUM_BMAP, rp + i, rp + i + 1);
		else
			xfs_btree_check_key(XFS_BTNUM_BMAP, kp + i, kp + i + 1);
	}
#endif
	if (optr == 1)
		xfs_bmbt_updkey(cur, &key, level + 1);
	*bnop = nbno;
	xfs_bmbt_rcheck(cur);
	if (nbno != NULLFSBLOCK) {
		*recp = nrec;
		*curp = ncur;
	} else
		xfs_bmbt_kcheck(cur);
	return 1;
}

#ifdef XFSDEBUG
/*
 * Debug routine to check key consistency.
 */
STATIC void
xfs_bmbt_kcheck(
	xfs_btree_cur_t		*cur)
{
	xfs_btree_lblock_t	*block;
	xfs_dinode_core_t	*dip;
	xfs_inode_t		*ip;
	int			level;
	int			levels;

	ip = cur->bc_private.b.ip;
	dip = &ip->i_d;
	ASSERT(dip->di_format == XFS_DINODE_FMT_BTREE);
	block = ip->i_broot;
	level = block->bb_level;
	levels = level + 1;
	ASSERT(levels == cur->bc_nlevels);
	xfs_bmbt_kcheck_body(cur, block, level, (xfs_bmbt_key_t *)0);
}

STATIC void
xfs_bmbt_kcheck_body(
	xfs_btree_cur_t		*cur,
	xfs_btree_lblock_t	*block,
	int			level,
	xfs_bmbt_key_t		*keyp)
{
	int			i;
	xfs_bmbt_key_t		key;
	xfs_bmbt_key_t		*kp;
	xfs_bmbt_ptr_t		*pp;
	xfs_bmbt_rec_t		*rp;

	xfs_btree_check_lblock(cur, block, level);
	if (level > 0) {
		kp = XFS_BMAP_KEY_IADDR(block, 1, cur);
		if (keyp)
			key = *kp;
	} else {
		rp = XFS_BMAP_REC_IADDR(block, 1, cur);
		if (keyp)
			key.br_startoff = xfs_bmbt_get_startoff(rp);
	}
	if (keyp)
		ASSERT(bcmp(keyp, &key, sizeof(key)) == 0);
	if (level > 0) {
		pp = XFS_BMAP_PTR_IADDR(block, 1, cur);
		if (*pp != NULLFSBLOCK) {
			for (i = 1; i <= block->bb_numrecs; i++, pp++, kp++)
				xfs_bmbt_kcheck_btree(cur, *pp, level - 1, kp);
		}
	}
}

STATIC void
xfs_bmbt_kcheck_btree(
	xfs_btree_cur_t		*cur,
	xfs_bmbt_ptr_t		bno,
	int			level,
	xfs_bmbt_key_t		*kp)
{
	xfs_btree_lblock_t	*block;
	buf_t			*buf;
	xfs_trans_t		*tp;

	ASSERT(bno != NULLFSBLOCK);
	tp = cur->bc_tp;
	buf = xfs_btree_read_bufl(cur->bc_mp, tp, bno, 0);
	block = xfs_buf_to_lblock(buf);
	xfs_bmbt_kcheck_body(cur, block, level, kp);
	xfs_trans_brelse(tp, buf);
}
#endif

/*
 * Log fields from the btree block header.
 */
STATIC void
xfs_bmbt_log_block(
	xfs_btree_cur_t		*cur,
	buf_t			*buf,
	int			fields)
{
	int			first;
	int			last;
	xfs_trans_t		*tp;
	static const int	offsets[] = {
		offsetof(xfs_btree_lblock_t, bb_magic),
		offsetof(xfs_btree_lblock_t, bb_level),
		offsetof(xfs_btree_lblock_t, bb_numrecs),
		offsetof(xfs_btree_lblock_t, bb_leftsib),
		offsetof(xfs_btree_lblock_t, bb_rightsib),
		sizeof(xfs_btree_lblock_t)
	};

	tp = cur->bc_tp;
	if (buf) {
		xfs_btree_offsets(fields, offsets, XFS_BB_NUM_BITS, &first,
				  &last);
		xfs_trans_log_buf(tp, buf, first, last);
	} else
		xfs_trans_log_inode(tp, cur->bc_private.b.ip, XFS_ILOG_BROOT);
}

/*
 * Log key values from the btree block.
 */
STATIC void
xfs_bmbt_log_keys(
	xfs_btree_cur_t	*cur,
	buf_t		*buf,
	int		kfirst,
	int		klast)
{
	xfs_trans_t	*tp;

	tp = cur->bc_tp;
	if (buf) {
		xfs_btree_lblock_t *block;
		int first;
		int last;
		xfs_bmbt_key_t *kp;

		block = xfs_buf_to_lblock(buf);
		kp = XFS_BMAP_KEY_DADDR(block, 1, cur);
		first = (caddr_t)&kp[kfirst - 1] - (caddr_t)block;
		last = ((caddr_t)&kp[klast] - 1) - (caddr_t)block;
		xfs_trans_log_buf(tp, buf, first, last);
	} else {
		xfs_inode_t *ip;

		ip = cur->bc_private.b.ip;
		xfs_trans_log_inode(tp, ip, XFS_ILOG_BROOT);
	}
}

/*
 * Log pointer values from the btree block.
 */
STATIC void
xfs_bmbt_log_ptrs(
	xfs_btree_cur_t	*cur,
	buf_t		*buf,
	int		pfirst,
	int		plast)
{
	xfs_trans_t	*tp;

	tp = cur->bc_tp;
	if (buf) {
		xfs_btree_lblock_t *block;
		int first;
		int last;
		xfs_bmbt_ptr_t *pp;

		block = xfs_buf_to_lblock(buf);
		pp = XFS_BMAP_PTR_DADDR(block, 1, cur);
		first = (caddr_t)&pp[pfirst - 1] - (caddr_t)block;
		last = ((caddr_t)&pp[plast] - 1) - (caddr_t)block;
		xfs_trans_log_buf(tp, buf, first, last);
	} else {
		xfs_inode_t *ip;

		ip = cur->bc_private.b.ip;
		xfs_trans_log_inode(tp, ip, XFS_ILOG_BROOT);
	}
}

/*
 * Log record values from the btree block.
 */
STATIC void
xfs_bmbt_log_recs(
	xfs_btree_cur_t		*cur,
	buf_t			*buf,
	int			rfirst,
	int			rlast)
{
	xfs_btree_lblock_t	*block;
	int			first;
	int			last;
	xfs_bmbt_rec_t		*rp;
	xfs_trans_t		*tp;

	ASSERT(buf);
	tp = cur->bc_tp;
	block = xfs_buf_to_lblock(buf);
	rp = XFS_BMAP_REC_DADDR(block, 1, cur);
	first = (caddr_t)&rp[rfirst - 1] - (caddr_t)block;
	last = ((caddr_t)&rp[rlast] - 1) - (caddr_t)block;
	xfs_trans_log_buf(tp, buf, first, last);
}

/*
 * Lookup the record.  The cursor is made to point to it, based on dir.
 */
STATIC int
xfs_bmbt_lookup(
	xfs_btree_cur_t		*cur,
	xfs_lookup_t		dir)
{
	xfs_btree_lblock_t	*block;
	buf_t			*buf;
	daddr_t			d;
	int			diff;
	xfs_bmbt_ptr_t		fsbno;
	int			high;
	int			i;
	int			keyno;
	xfs_bmbt_key_t		*kkbase;
	xfs_bmbt_key_t		*kkp;
	xfs_bmbt_rec_t		*krbase;
	xfs_bmbt_rec_t		*krp;
	int			level;
	int			low;
	xfs_mount_t		*mp;
	xfs_bmbt_irec_t		*rp;
	xfs_sb_t		*sbp;
	xfs_fsblock_t		startoff;
	xfs_trans_t		*tp;

	xfs_bmbt_rcheck(cur);
	xfs_bmbt_kcheck(cur);
	tp = cur->bc_tp;
	mp = cur->bc_mp;
	sbp = &mp->m_sb;
	rp = &cur->bc_rec.b;
	for (level = cur->bc_nlevels - 1, diff = 1; level >= 0; level--) {
		if (level < cur->bc_nlevels - 1) {
			d = xfs_fsb_to_daddr(sbp, fsbno);
			buf = cur->bc_bufs[level];
			if (buf && buf->b_blkno != d)
				buf = (buf_t *)0;
			if (!buf) {
				buf = xfs_trans_read_buf(tp, mp->m_dev, d, mp->m_bsize, 0);
				xfs_btree_setbuf(cur, level, buf);
			}
		}
		block = xfs_bmbt_get_block(cur, level, &buf);
		xfs_btree_check_lblock(cur, block, level);
		if (diff == 0)
			keyno = 1;
		else {
			if (level > 0)
				kkbase = XFS_BMAP_KEY_IADDR(block, 1, cur);
			else
				krbase = XFS_BMAP_REC_IADDR(block, 1, cur);
			low = 1;
			if (!(high = block->bb_numrecs)) {
				ASSERT(level == 0);
				cur->bc_ptrs[0] = dir != XFS_LOOKUP_LE;
				return 0;
			}
			while (low <= high) {
				keyno = (low + high) >> 1;
				if (level > 0) {
					kkp = kkbase + keyno - 1;
					startoff = kkp->br_startoff;
				} else {
					krp = krbase + keyno - 1;
					startoff = xfs_bmbt_get_startoff(krp);
				}
				diff = (int)(startoff - rp->br_startoff);
				if (diff < 0)
					low = keyno + 1;
				else if (diff > 0)
					high = keyno - 1;
				else
					break;
			}
		}
		if (level > 0) {
			if (diff > 0 && --keyno < 1)
				keyno = 1;
			fsbno = *XFS_BMAP_PTR_IADDR(block, keyno, cur);
			xfs_btree_check_lptr(cur, fsbno, level);
			cur->bc_ptrs[level] = keyno;
		}
	}
	if (dir != XFS_LOOKUP_LE && diff < 0) {
		keyno++;
		/*
		 * If ge search and we went off the end of the block, but it's
		 * not the last block, we're in the wrong block.
		 */
		if (dir == XFS_LOOKUP_GE && keyno > block->bb_numrecs &&
		    block->bb_rightsib != NULLFSBLOCK) {
			cur->bc_ptrs[0] = keyno;
			i = xfs_bmbt_increment(cur, 0);
			ASSERT(i == 1);
			return 1;
		}
	}
	else if (dir == XFS_LOOKUP_LE && diff > 0)
		keyno--;
	cur->bc_ptrs[0] = keyno;
	if (keyno == 0 || keyno > block->bb_numrecs)
		return 0;
	else
		return dir != XFS_LOOKUP_EQ || diff == 0;
}

STATIC int
xfs_bmbt_lookup_eq(
	xfs_btree_cur_t	*cur,
	xfs_fsblock_t	off,
	xfs_fsblock_t	bno,
	xfs_extlen_t	len)
{
	cur->bc_rec.b.br_startoff = off;
	cur->bc_rec.b.br_startblock = bno;
	cur->bc_rec.b.br_blockcount = len;
	return xfs_bmbt_lookup(cur, XFS_LOOKUP_EQ);
}

STATIC int
xfs_bmbt_lookup_ge(
	xfs_btree_cur_t	*cur,
	xfs_fsblock_t	off,
	xfs_fsblock_t	bno,
	xfs_extlen_t	len)
{
	cur->bc_rec.b.br_startoff = off;
	cur->bc_rec.b.br_startblock = bno;
	cur->bc_rec.b.br_blockcount = len;
	return xfs_bmbt_lookup(cur, XFS_LOOKUP_GE);
}

STATIC int
xfs_bmbt_lookup_le(
	xfs_btree_cur_t	*cur,
	xfs_fsblock_t	off,
	xfs_fsblock_t	bno,
	xfs_extlen_t	len)
{
	cur->bc_rec.b.br_startoff = off;
	cur->bc_rec.b.br_startblock = bno;
	cur->bc_rec.b.br_blockcount = len;
	return xfs_bmbt_lookup(cur, XFS_LOOKUP_LE);
}

/*
 * Move 1 record left from cur/level if possible.
 * Update cur to reflect the new path.
 */
STATIC int
xfs_bmbt_lshift(
	xfs_btree_cur_t		*cur,
	int			level)
{
	int			first;
	int			i;
	xfs_bmbt_key_t		key;
	int			last;
	buf_t			*lbuf;
	xfs_btree_lblock_t	*left;
	xfs_bmbt_key_t		*lkp;
	xfs_bmbt_ptr_t		*lpp;
	xfs_bmbt_rec_t		*lrp;
	xfs_mount_t		*mp;
	int			nrec;
	buf_t			*rbuf;
	xfs_btree_lblock_t	*right;
	xfs_bmbt_key_t		*rkp;
	xfs_bmbt_ptr_t		*rpp;
	xfs_bmbt_rec_t		*rrp;
	xfs_trans_t		*tp;

	if (level == cur->bc_nlevels - 1)
		return 0;
	xfs_bmbt_rcheck(cur);
	rbuf = cur->bc_bufs[level];
	right = xfs_buf_to_lblock(rbuf);
	xfs_btree_check_lblock(cur, right, level);
	if (right->bb_leftsib == NULLFSBLOCK)
		return 0;
	if (cur->bc_ptrs[level] <= 1)
		return 0;
	tp = cur->bc_tp;
	mp = cur->bc_mp;
	lbuf = xfs_btree_read_bufl(mp, tp, right->bb_leftsib, 0);
	left = xfs_buf_to_lblock(lbuf);
	xfs_btree_check_lblock(cur, left, level);
	if (left->bb_numrecs == XFS_BMAP_BLOCK_IMAXRECS(level, cur))
		return 0;
	nrec = left->bb_numrecs + 1;
	if (level > 0) {
		lkp = XFS_BMAP_KEY_IADDR(left, left->bb_numrecs + 1, cur);
		rkp = XFS_BMAP_KEY_IADDR(right, 1, cur);
		*lkp = *rkp;
		xfs_bmbt_log_keys(cur, lbuf, nrec, nrec);
		lpp = XFS_BMAP_PTR_IADDR(left, left->bb_numrecs + 1, cur);
		rpp = XFS_BMAP_PTR_IADDR(right, 1, cur);
		xfs_btree_check_lptr(cur, *rpp, level);
		*lpp = *rpp;
		xfs_bmbt_log_ptrs(cur, lbuf, nrec, nrec);
	} else {
		lrp = XFS_BMAP_REC_IADDR(left, left->bb_numrecs + 1, cur);
		rrp = XFS_BMAP_REC_IADDR(right, 1, cur);
		*lrp = *rrp;
		xfs_bmbt_log_recs(cur, lbuf, nrec, nrec);
	}
	left->bb_numrecs++;
	xfs_bmbt_log_block(cur, lbuf, XFS_BB_NUMRECS);
#ifdef XFSDEBUG
	if (level > 0)
		xfs_btree_check_key(XFS_BTNUM_BMAP, lkp - 1, lkp);
	else
		xfs_btree_check_rec(XFS_BTNUM_BMAP, lrp - 1, lrp);
#endif
	right->bb_numrecs--;
	xfs_bmbt_log_block(cur, rbuf, XFS_BB_NUMRECS);
	if (level > 0) {
		for (i = 0; i < right->bb_numrecs; i++) {
			rkp[i] = rkp[i + 1];
			xfs_btree_check_lptr(cur, rpp[i + 1], level);
			rpp[i] = rpp[i + 1];
		}
		xfs_bmbt_log_keys(cur, rbuf, 1, right->bb_numrecs);
		xfs_bmbt_log_ptrs(cur, rbuf, 1, right->bb_numrecs);
	} else {
		for (i = 0; i < right->bb_numrecs; i++)
			rrp[i] = rrp[i + 1];
		xfs_bmbt_log_recs(cur, rbuf, 1, right->bb_numrecs);
		key.br_startoff = xfs_bmbt_get_startoff(rrp);
		rkp = &key;
	}
	xfs_bmbt_updkey(cur, rkp, level + 1);
	cur->bc_ptrs[level]--;
	xfs_bmbt_rcheck(cur);
	return 1;
}

#ifdef XFSDEBUG
STATIC void
xfs_bmbt_rcheck(
	xfs_btree_cur_t		*cur)
{
	xfs_btree_lblock_t	*block;
	xfs_bmbt_ptr_t		bno;
	xfs_dinode_core_t	*dip;
	xfs_bmbt_ptr_t		fbno;
	xfs_inode_t		*ip;
	xfs_bmbt_key_t		key;
	int			level;
	xfs_bmbt_rec_t		rec;
	void			*rp;
	xfs_sb_t		*sbp;

	sbp = &cur->bc_mp->m_sb;
	ip = cur->bc_private.b.ip;
	dip = &ip->i_d;
	ASSERT(dip->di_format == XFS_DINODE_FMT_BTREE);
	block = ip->i_broot;
	level = cur->bc_nlevels - 1;
	rp = level ? (void *)&key : (void *)&rec;
	xfs_bmbt_rcheck_body(cur, block, &bno, rp, level);
	for (level = block->bb_level - 1; level >= 0; level--, bno = fbno) {
		rp = level ? (void *)&key : (void *)&rec;
		bno = xfs_bmbt_rcheck_btree(cur, bno, &fbno, rp, level);
		while (bno != NULLFSBLOCK) {
			ASSERT(xfs_fsb_to_agno(sbp, bno) < sbp->sb_agcount);
			ASSERT(xfs_fsb_to_agbno(sbp, bno) < sbp->sb_agblocks);
			bno = xfs_bmbt_rcheck_btree(cur, bno, (xfs_bmbt_ptr_t *)0, rp, level);
		}
	}
}

STATIC xfs_bmbt_ptr_t
xfs_bmbt_rcheck_body(
	xfs_btree_cur_t		*cur,
	xfs_btree_lblock_t	*block,
	xfs_bmbt_ptr_t		*fbno,
	void			*rec,
	int			level)
{
	int			i;
	xfs_bmbt_key_t		*keyp;
	xfs_bmbt_key_t		*kp;
	xfs_bmbt_ptr_t		*pp;
	xfs_bmbt_ptr_t		rbno;
	xfs_bmbt_rec_t		*recp;
	xfs_bmbt_rec_t		*rp;

	xfs_btree_check_lblock(cur, block, level);
	if (fbno && block->bb_numrecs) {
		if (level > 0)
			*fbno = *XFS_BMAP_PTR_IADDR(block, 1, cur);
		else
			*fbno = NULLFSBLOCK;
	}
	rbno = block->bb_rightsib;
	if (level > 0)
		keyp = (xfs_bmbt_key_t *)rec;
	else
		recp = (xfs_bmbt_rec_t *)rec;
	for (i = 1; i <= block->bb_numrecs; i++) {
		if (level > 0) {
			kp = XFS_BMAP_KEY_IADDR(block, i, cur);
			if (i == 1 && !fbno)
				xfs_btree_check_key(XFS_BTNUM_BMAP, (void *)keyp, (void *)kp);
			else if (i > 1) {
				xfs_btree_check_key(XFS_BTNUM_BMAP, (void *)(kp - 1), (void *)kp);
				if (i == block->bb_numrecs)
					*keyp = *kp;
			}
			pp = XFS_BMAP_PTR_IADDR(block, i, cur);
			xfs_btree_check_lptr(cur, *pp, level);
		} else {
			rp = XFS_BMAP_REC_IADDR(block, i, cur);
			if (i == 1 && !fbno)
				xfs_btree_check_rec(XFS_BTNUM_BMAP, (void *)recp, (void *)rp);
			else if (i > 1) {
				xfs_btree_check_rec(XFS_BTNUM_BMAP, (void *)(rp - 1), (void *)rp);
				if (i == block->bb_numrecs)
					*recp = *rp;
			}
		}
	}
	return rbno;
}

STATIC xfs_bmbt_ptr_t
xfs_bmbt_rcheck_btree(
	xfs_btree_cur_t		*cur,
	xfs_bmbt_ptr_t		bno,
	xfs_bmbt_ptr_t		*fbno,
	void			*rec,
	int			level)
{
	xfs_btree_lblock_t	*block;
	buf_t			*buf;
	xfs_bmbt_ptr_t		rval;
	xfs_trans_t		*tp;

	tp = cur->bc_tp;
	buf = xfs_btree_read_bufl(cur->bc_mp, tp, bno, 0);
	block = xfs_buf_to_lblock(buf);
	rval = xfs_bmbt_rcheck_body(cur, block, fbno, rec, level);
	xfs_trans_brelse(tp, buf);
	return rval;
}
#endif

/*
 * Read in the allocation group header (free/alloc section)
 */
buf_t *
xfs_bmbt_read_agf(
	xfs_mount_t	*mp,
	xfs_trans_t	*tp,
	xfs_agnumber_t	agno)
{
	daddr_t		d;		/* disk block address */
	xfs_sb_t	*sbp;		/* superblock structure */

	ASSERT(agno != NULLAGNUMBER);
	sbp = &mp->m_sb;
	d = xfs_ag_daddr(sbp, agno, XFS_AGF_DADDR);
	return xfs_trans_read_buf(tp, mp->m_dev, d, 1, 0);
}

/*
 * Move 1 record right from cur/level if possible.
 * Update cur to reflect the new path.
 */
STATIC int
xfs_bmbt_rshift(
	xfs_btree_cur_t		*cur,
	int			level)
{
	int			first;
	int			i;
	xfs_bmbt_key_t		key;
	int			last;
	buf_t			*lbuf;
	xfs_btree_lblock_t	*left;
	xfs_bmbt_key_t		*lkp;
	xfs_bmbt_ptr_t		*lpp;
	xfs_bmbt_rec_t		*lrp;
	xfs_mount_t		*mp;
	buf_t			*rbuf;
	xfs_btree_lblock_t	*right;
	xfs_bmbt_key_t		*rkp;
	xfs_bmbt_ptr_t		*rpp;
	xfs_bmbt_rec_t		*rrp;
	xfs_btree_cur_t		*tcur;
	xfs_trans_t		*tp;

	if (level == cur->bc_nlevels - 1)
		return 0;
	xfs_bmbt_rcheck(cur);
	lbuf = cur->bc_bufs[level];
	left = xfs_buf_to_lblock(lbuf);
	xfs_btree_check_lblock(cur, left, level);
	if (left->bb_rightsib == NULLFSBLOCK)
		return 0;
	if (cur->bc_ptrs[level] >= left->bb_numrecs)
		return 0;
	tp = cur->bc_tp;
	mp = cur->bc_mp;
	rbuf = xfs_btree_read_bufl(mp, tp, left->bb_rightsib, 0);
	right = xfs_buf_to_lblock(rbuf);
	xfs_btree_check_lblock(cur, right, level);
	if (right->bb_numrecs == XFS_BMAP_BLOCK_IMAXRECS(level, cur))
		return 0;
	if (level > 0) {
		lkp = XFS_BMAP_KEY_IADDR(left, left->bb_numrecs, cur);
		lpp = XFS_BMAP_PTR_IADDR(left, left->bb_numrecs, cur);
		rkp = XFS_BMAP_KEY_IADDR(right, 1, cur);
		rpp = XFS_BMAP_PTR_IADDR(right, 1, cur);
		for (i = right->bb_numrecs - 1; i >= 0; i--) {
			rkp[i + 1] = rkp[i];
			xfs_btree_check_lptr(cur, rpp[i], level);
			rpp[i + 1] = rpp[i];
		}
		xfs_btree_check_lptr(cur, *lpp, level);
		*rkp = *lkp;
		*rpp = *lpp;
		xfs_bmbt_log_keys(cur, rbuf, 1, right->bb_numrecs + 1);
		xfs_bmbt_log_ptrs(cur, rbuf, 1, right->bb_numrecs + 1);
	} else {
		lrp = XFS_BMAP_REC_IADDR(left, left->bb_numrecs, cur);
		rrp = XFS_BMAP_REC_IADDR(right, 1, cur);
		for (i = right->bb_numrecs - 1; i >= 0; i--)
			rrp[i + 1] = rrp[i];
		*rrp = *lrp;
		xfs_bmbt_log_recs(cur, rbuf, 1, right->bb_numrecs + 1);
		key.br_startoff = xfs_bmbt_get_startoff(rrp);
		rkp = &key;
	}
	left->bb_numrecs--;
	xfs_bmbt_log_block(cur, lbuf, XFS_BB_NUMRECS);
	right->bb_numrecs++;
#ifdef XFSDEBUG
	if (level > 0)
		xfs_btree_check_key(XFS_BTNUM_BMAP, rkp, rkp + 1);
	else
		xfs_btree_check_rec(XFS_BTNUM_BMAP, rrp, rrp + 1);
#endif
	xfs_bmbt_log_block(cur, rbuf, XFS_BB_NUMRECS);
	tcur = xfs_btree_dup_cursor(cur);
	xfs_btree_lastrec(tcur, level);
	xfs_bmbt_increment(tcur, level);
	xfs_bmbt_updkey(tcur, rkp, level + 1);
	xfs_btree_del_cursor(tcur);
	xfs_bmbt_rcheck(cur);
	return 1;
}

/*
 * Split cur/level block in half.
 * Return new block number and its first record (to be inserted into parent).
 */
STATIC int
xfs_bmbt_split(
	xfs_btree_cur_t		*cur,
	int			level,
	xfs_bmbt_ptr_t		*bnop,
	xfs_bmbt_key_t		*keyp,
	xfs_btree_cur_t		**curp)
{
	daddr_t			d;
	int			first;
	int			i;
	xfs_bmbt_key_t		key;
	int			last;
	xfs_bmbt_ptr_t		lbno;
	buf_t			*lbuf;
	xfs_btree_lblock_t	*left;
	xfs_bmbt_key_t		*lkp;
	xfs_bmbt_ptr_t		*lpp;
	xfs_bmbt_rec_t		*lrp;
	xfs_mount_t		*mp;
	xfs_bmbt_ptr_t		rbno;
	buf_t			*rbuf;
	xfs_bmbt_rec_t		rec;
	xfs_btree_lblock_t	*right;
	xfs_bmbt_key_t		*rkp;
	xfs_bmbt_ptr_t		*rpp;
	xfs_btree_lblock_t	*rrblock;
	buf_t			*rrbuf;
	xfs_bmbt_rec_t		*rrp;
	xfs_sb_t		*sbp;
	xfs_trans_t		*tp;

	xfs_bmbt_rcheck(cur);
	tp = cur->bc_tp;
	mp = cur->bc_mp;
	sbp = &mp->m_sb;
	lbuf = cur->bc_bufs[level];
	lbno = xfs_daddr_to_fsb(sbp, lbuf->b_blkno);
	left = xfs_buf_to_lblock(lbuf);
	rbno = xfs_alloc_extent(tp, cur->bc_private.b.firstblock, 1,
				XFS_ALLOCTYPE_START_BNO, 0, 0);
	if (rbno == NULLFSBLOCK)
		return 0;
	rbuf = xfs_btree_get_bufl(mp, tp, rbno, 0);
	right = xfs_buf_to_lblock(rbuf);
	xfs_btree_check_lblock(cur, left, level);
	right->bb_magic = XFS_BMAP_MAGIC;
	right->bb_level = left->bb_level;
	right->bb_numrecs = (__uint16_t)(left->bb_numrecs / 2);
	if ((left->bb_numrecs & 1) && cur->bc_ptrs[level] <= right->bb_numrecs + 1)
		right->bb_numrecs++;
	i = left->bb_numrecs - right->bb_numrecs + 1;
	if (level > 0) {
		lkp = XFS_BMAP_KEY_IADDR(left, i, cur);
		lpp = XFS_BMAP_PTR_IADDR(left, i, cur);
		rkp = XFS_BMAP_KEY_IADDR(right, 1, cur);
		rpp = XFS_BMAP_PTR_IADDR(right, 1, cur);
		for (i = 0; i < right->bb_numrecs; i++) {
			rkp[i] = lkp[i];
			xfs_btree_check_lptr(cur, lpp[i], level);
			rpp[i] = lpp[i];
		}
		xfs_bmbt_log_keys(cur, rbuf, 1, right->bb_numrecs);
		xfs_bmbt_log_ptrs(cur, rbuf, 1, right->bb_numrecs);
		*keyp = *rkp;
	} else {
		lrp = XFS_BMAP_REC_IADDR(left, i, cur);
		rrp = XFS_BMAP_REC_IADDR(right, 1, cur);
		for (i = 0; i < right->bb_numrecs; i++)
			rrp[i] = lrp[i];
		xfs_bmbt_log_recs(cur, rbuf, 1, right->bb_numrecs);
		keyp->br_startoff = xfs_bmbt_get_startoff(rrp);
	}
	d = lbuf->b_blkno;
	left->bb_numrecs -= right->bb_numrecs;
	right->bb_rightsib = left->bb_rightsib;
	left->bb_rightsib = rbno;
	right->bb_leftsib = lbno;
	xfs_bmbt_log_block(cur, rbuf, XFS_BB_ALL_BITS);
	xfs_bmbt_log_block(cur, lbuf, XFS_BB_NUMRECS | XFS_BB_RIGHTSIB);
	if (right->bb_rightsib != NULLFSBLOCK) {
		rrbuf = xfs_btree_read_bufl(mp, tp, right->bb_rightsib, 0);
		rrblock = xfs_buf_to_lblock(rrbuf);
		xfs_btree_check_lblock(cur, rrblock, level);
		rrblock->bb_leftsib = rbno;
		xfs_bmbt_log_block(cur, rrbuf, XFS_BB_LEFTSIB);
	}
	if (cur->bc_ptrs[level] > left->bb_numrecs + 1) {
		xfs_btree_setbuf(cur, level, rbuf);
		cur->bc_ptrs[level] -= left->bb_numrecs;
	}
	if (level + 1 < cur->bc_nlevels) {
		*curp = xfs_btree_dup_cursor(cur);
		(*curp)->bc_ptrs[level + 1]++;
	}
	*bnop = rbno;
	xfs_bmbt_rcheck(cur);
	return 1;
}

/*
 * Update the record to the passed values.
 */
STATIC int
xfs_bmbt_update(
	xfs_btree_cur_t		*cur,
	xfs_fsblock_t		off,
	xfs_fsblock_t		bno,
	xfs_extlen_t		len)
{
	xfs_btree_lblock_t	*block;
	int			first;
	xfs_bmbt_key_t		key;
	int			last;
	int			ptr;
	xfs_bmbt_rec_t		*rp;
	xfs_trans_t		*tp;

	xfs_bmbt_rcheck(cur);
	block = xfs_bmbt_get_block(cur, 0, &buf);
	xfs_btree_check_lblock(cur, block, 0);
	ptr = cur->bc_ptrs[0];
	tp = cur->bc_tp;
	rp = XFS_BMAP_REC_IADDR(block, ptr, cur);
	xfs_bmbt_set_startoff(rp, off);
	xfs_bmbt_set_startblock(rp, bno);
	xfs_bmbt_set_blockcount(rp, len);
	xfs_bmbt_log_recs(cur, buf, ptr, ptr);
	if (ptr > 1)
		return 1;
	key.br_startoff = off;
	xfs_bmbt_updkey(cur, &key, 1);
	xfs_bmbt_rcheck(cur);
	xfs_bmbt_kcheck(cur);
	return 1;
}

/*
 * Update keys for the record.
 */
STATIC void
xfs_bmbt_updkey(
	xfs_btree_cur_t		*cur,
	xfs_bmbt_key_t		*keyp,
	int			level)
{
	xfs_btree_lblock_t	*block;
	buf_t			*buf;
	xfs_bmbt_key_t		*kp;
	int			ptr;
	xfs_trans_t		*tp;

	ASSERT(level >= 1);
	xfs_bmbt_rcheck(cur);
	tp = cur->bc_tp;
	for (ptr = 1; ptr == 1 && level < cur->bc_nlevels; level++) {
		block = xfs_bmbt_get_block(cur, level, &buf);
		xfs_btree_check_lblock(cur, block, level);
		ptr = cur->bc_ptrs[level];
		kp = XFS_BMAP_KEY_IADDR(block, ptr, cur);
		*kp = *keyp;
		xfs_bmbt_log_keys(cur, buf, ptr, ptr);
	}
	xfs_bmbt_rcheck(cur);
}

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
	xfs_btree_cur_t		*cur,	/* if null, not a btree */
	xfs_bmbt_irec_t		*new)	/* new data to put in extent list */
{
	xfs_extnum_t		nextents;
	xfs_bmbt_irec_t		prev;

	nextents = ip->i_bytes / sizeof(xfs_bmbt_rec_t);
	ASSERT(idx <= nextents);
	/*
	 * This is the first extent added to a new/empty file.
	 * Special case this one, so other routines get to assume there are
	 * already extents in the list.
	 */
	if (nextents == 0) {
		xfs_bmap_insert_exlist(ip, 0, 1, new);
		ASSERT(cur == NULL);
		ip->i_lastex = 0;
		kmem_check();
		if (new->br_startblock != NULLSTARTBLOCK) {
			ip->i_d.di_nextents = 1;
			return XFS_ILOG_CORE | XFS_ILOG_EXT;
		} else
			return 0;
	}
	/*
	 * Any kind of new delayed allocation goes here.
	 */
	if (new->br_startblock == NULLSTARTBLOCK)
		return xfs_bmap_add_extent_hole_delay(ip, idx, cur, new);
	/*
	 * Real allocation off the end of the file.
	 */
	if (idx == nextents)
		return xfs_bmap_add_extent_hole_real(ip, idx, cur, new);
	/*
	 * Get the record referred to by idx.
	 */
	xfs_bmbt_get_all(&ip->i_u1.iu_extents[idx], prev);
	/*
	 * If it's a real allocation record, and the new allocation ends
	 * after the start of the referred to record, then we're filling
	 * in a delayed allocation with a real one.
	 */
	if (new->br_startblock != NULLSTARTBLOCK &&
	    new->br_startoff + new->br_blockcount > prev.br_startoff)
		return xfs_bmap_add_extent_delay_real(ip, idx, cur, new);
	/*
	 * Otherwise we're filling in a hole with an allocation.
	 */
	return xfs_bmap_add_extent_hole_real(ip, idx, cur, new);
}

/*
 * Called by xfs_bmap_add_extent to handle cases converting a delayed
 * allocation to a real allocation.
 */
STATIC int
xfs_bmap_add_extent_delay_real(
	xfs_inode_t		*ip,	/* incore inode pointer */
	xfs_extnum_t		idx,	/* extent number to update/insert */
	xfs_btree_cur_t		*cur,	/* if null, not a btree */
	xfs_bmbt_irec_t		*new)	/* new data to put in extent list */
{
	xfs_bmbt_rec_t		*base;
	int			btree;
	int			end_eq;
	xfs_bmbt_rec_t		*ep;
	int			filling_in_all;
	int			filling_in_first;
	int			filling_in_last;
	int			filling_in_middle;
	xfs_bmbt_irec_t		left;
	int			left_contig;
	int			left_delay;
	xfs_fsblock_t		left_endblock;
	xfs_fsblock_t		left_endoff;
	int			left_valid;
	xfs_fsblock_t		new_endblock;
	xfs_fsblock_t		new_endoff;
	xfs_extnum_t		nextents;
	xfs_bmbt_irec_t		prev;
	xfs_fsblock_t		prev_endoff;
	xfs_bmbt_irec_t		right;
	int			right_contig;
	int			right_delay;
	int			right_valid;
	int			start_eq;

	nextents = ip->i_bytes / sizeof(xfs_bmbt_rec_t);
	btree = cur != NULL;
	/*
	 * Set up a bunch of variables to make the tests simpler.
	 */
	base = ip->i_u1.iu_extents;
	ep = &base[idx];
	xfs_bmbt_get_all(ep, prev);
	prev_endoff = prev.br_startoff + prev.br_blockcount;
	new_endoff = new->br_startoff + new->br_blockcount;
	new_endblock = new->br_startblock + new->br_blockcount;
	/*
	 * Check and set flags if this segment has a left neighbor
	 */
	if (left_valid = ep > base) {
		xfs_bmbt_get_all(ep - 1, left);
		if (!(left_delay = left.br_startblock == NULLSTARTBLOCK))
			left_endblock = left.br_startblock + left.br_blockcount;
		left_endoff = left.br_startoff + left.br_blockcount;
	}
	left_contig = left_valid && !left_delay &&
		      left_endoff == new->br_startoff &&
		      left_endblock == new->br_startblock;
	/*
	 * Check and set flags if this segment has a right neighbor
	 */
	if (right_valid = ep < &base[nextents - 1]) {
		xfs_bmbt_get_all(ep + 1, right);
		right_delay = right.br_startblock == NULLSTARTBLOCK;
	}
	right_contig = right_valid && !right_delay &&
		       new_endoff == right.br_startoff &&
		       new_endblock == right.br_startblock;
	/*
	 * Set flags determining what part of the previous delayed allocation
	 * extent is being replaced by a real allocation.
	 */
	start_eq = prev.br_startoff == new->br_startoff;
	end_eq = prev_endoff == new_endoff;
	filling_in_all = start_eq & end_eq;
	filling_in_first = start_eq & !end_eq;
	filling_in_last = !start_eq & end_eq;
	filling_in_middle = !start_eq & !end_eq;
	/*
	 * Filling in all of a previously delayed allocation extent.
	 * The left and right neighbors are both contiguous with new.
	 */
	if (filling_in_all && left_contig && right_contig) {
		xfs_bmbt_set_blockcount(ep - 1,
			left.br_blockcount + prev.br_blockcount +
			right.br_blockcount);
		xfs_bmap_delete_exlist(ip, idx, 2);
		ip->i_lastex = idx - 1;
		ip->i_d.di_nextents--;
		kmem_check();
		if (!btree)
			return XFS_ILOG_CORE | XFS_ILOG_EXT;
		xfs_bmbt_lookup_eq(cur, left.br_startoff, left.br_startblock,
			left.br_blockcount);
		xfs_bmbt_update(cur, left.br_startoff, left.br_startblock,
			left.br_blockcount + prev.br_blockcount +
			right.br_blockcount);
		xfs_bmbt_increment(cur, 0);
		xfs_bmbt_delete(cur);
		kmem_check();
		return XFS_ILOG_CORE;
	}
	/*
	 * Filling in all of a previously delayed allocation extent.
	 * The left neighbor is contiguous, the right is not.
	 */
	if (filling_in_all && left_contig && !right_contig) {
		xfs_bmbt_set_blockcount(ep - 1,
			left.br_blockcount + prev.br_blockcount);
		ip->i_lastex = idx - 1;
		xfs_bmap_delete_exlist(ip, idx, 1);
		kmem_check();
		if (!btree)
			return XFS_ILOG_EXT;
		xfs_bmbt_lookup_eq(cur, left.br_startoff, left.br_startblock,
			left.br_blockcount);
		xfs_bmbt_update(cur, left.br_startoff, left.br_startblock,
			left.br_blockcount + prev.br_blockcount);
		kmem_check();
		return 0;
	}
	/*
	 * Filling in all of a previously delayed allocation extent.
	 * The right neighbor is contiguous, the left is not.
	 */
	if (filling_in_all && !left_contig && right_contig) {
		xfs_bmbt_set_startblock(ep, new->br_startblock);
		xfs_bmbt_set_blockcount(ep,
			prev.br_blockcount + right.br_blockcount);
		ip->i_lastex = idx;
		xfs_bmap_delete_exlist(ip, idx + 1, 1);
		kmem_check();
		if (!btree)
			return XFS_ILOG_EXT;
		xfs_bmbt_lookup_eq(cur, right.br_startoff, right.br_startblock,
			right.br_blockcount);
		xfs_bmbt_update(cur, prev.br_startoff, new->br_startblock,
			prev.br_blockcount + right.br_blockcount);
		kmem_check();
		return 0;
	}
	/*
	 * Filling in all of a previously delayed allocation extent.
	 * Neither the left nor right neighbors are contiguous with the new one.
	 */
	if (filling_in_all && !left_contig && !right_contig) {
		xfs_bmbt_set_startblock(ep, new->br_startblock);
		ip->i_lastex = idx;
		ip->i_d.di_nextents++;
		kmem_check();
		if (!btree)
			return XFS_ILOG_CORE | XFS_ILOG_EXT;
		xfs_bmbt_lookup_eq(cur, new->br_startoff, new->br_startblock,
			new->br_blockcount);
		xfs_bmbt_insert(cur);
		kmem_check();
		return XFS_ILOG_CORE;
	}
	/*
	 * Filling in the first part of a previous delayed allocation.
	 * The left neighbor is contiguous.
	 */
	if (filling_in_first && left_contig) {
		xfs_bmbt_set_blockcount(ep - 1,
			left.br_blockcount + new->br_blockcount);
		xfs_bmbt_set_startoff(ep,
			prev.br_startoff + new->br_blockcount);
		xfs_bmbt_set_blockcount(ep,
			prev.br_blockcount - new->br_blockcount);
		ip->i_lastex = idx;
		kmem_check();
		if (!btree)
			return XFS_ILOG_EXT;
		xfs_bmbt_lookup_eq(cur, left.br_startoff, left.br_startblock,
			left.br_blockcount);
		xfs_bmbt_update(cur, left.br_startoff, left.br_startblock,
			left.br_blockcount + new->br_blockcount);
		kmem_check();
		return 0;
	}
	/*
	 * Filling in the first part of a previous delayed allocation.
	 * The left neighbor is not contiguous.
	 */
	if (filling_in_first && !left_contig) {
		xfs_bmbt_set_startoff(ep, new_endoff);
		xfs_bmbt_set_blockcount(ep,
			prev.br_blockcount - new->br_blockcount);
		xfs_bmap_insert_exlist(ip, idx, 1, new);
		ip->i_lastex = idx;
		ip->i_d.di_nextents++;
		kmem_check();
		if (!btree)
			return XFS_ILOG_CORE | XFS_ILOG_EXT;
		xfs_bmbt_lookup_eq(cur, new->br_startoff, new->br_startblock,
			new->br_blockcount);
		xfs_bmbt_insert(cur);
		kmem_check();
		return XFS_ILOG_CORE;
	}
	/*
	 * Filling in the last part of a previous delayed allocation.
	 * The right neighbor is contiguous with the new allocation.
	 */
	if (filling_in_last && right_contig) {
		xfs_bmbt_set_blockcount(ep,
			prev.br_blockcount - new->br_blockcount);
		xfs_bmbt_set_startoff(ep + 1, new->br_startoff);
		xfs_bmbt_set_startblock(ep + 1, new->br_startblock);
		xfs_bmbt_set_blockcount(ep + 1,
			new->br_blockcount + right.br_blockcount);
		ip->i_lastex = idx + 1;
		kmem_check();
		if (!btree)
			return XFS_ILOG_EXT;
		xfs_bmbt_lookup_eq(cur, right.br_startoff, right.br_startblock,
			right.br_blockcount);
		xfs_bmbt_update(cur, new->br_startoff, new->br_startblock,
			new->br_blockcount + right.br_blockcount);
		kmem_check();
		return 0;
	}
	/*
	 * Filling in the last part of a previous delayed allocation.
	 * The right neighbor is not contiguous.
	 */
	if (filling_in_last && !right_contig) {
		xfs_bmbt_set_blockcount(ep,
			prev.br_blockcount - new->br_blockcount);
		xfs_bmap_insert_exlist(ip, idx + 1, 1, new);
		ip->i_lastex = idx + 1;
		ip->i_d.di_nextents++;
		kmem_check();
		if (!btree)
			return XFS_ILOG_CORE | XFS_ILOG_EXT;
		xfs_bmbt_lookup_eq(cur, new->br_startoff, new->br_startblock, new->br_blockcount);
		xfs_bmbt_insert(cur);
		kmem_check();
		return XFS_ILOG_CORE;
	}
	/*
	 * Filling in the middle part of a previous delayed allocation.
	 * Contiguity is impossible here.
	 */
	if (filling_in_middle) {
		xfs_bmbt_irec_t temp[2];

		xfs_bmbt_set_blockcount(ep,
			new->br_startoff - prev.br_startoff);
		temp[0] = *new;
		temp[1].br_startoff = new_endoff;
		temp[1].br_startblock = NULLSTARTBLOCK;
		temp[1].br_blockcount = prev_endoff - new_endoff;
		xfs_bmap_insert_exlist(ip, idx + 1, 2, temp);
		ip->i_lastex = idx + 1;
		ip->i_d.di_nextents++;
		kmem_check();
		if (!btree)
			return XFS_ILOG_CORE | XFS_ILOG_EXT;
		xfs_bmbt_lookup_eq(cur, new->br_startoff, new->br_startblock,
			new->br_blockcount);
		xfs_bmbt_insert(cur);
		kmem_check();
		return XFS_ILOG_CORE;
	}
	ASSERT(0);
}

/*
 * Called by xfs_bmap_add_extent to handle cases converting a hole
 * to a delayed allocation.
 */
STATIC int
xfs_bmap_add_extent_hole_delay(
	xfs_inode_t		*ip,	/* incore inode pointer */
	xfs_extnum_t		idx,	/* extent number to update/insert */
	xfs_btree_cur_t		*cur,	/* if null, not a btree */
	xfs_bmbt_irec_t		*new)	/* new data to put in extent list */
{
	xfs_bmbt_rec_t		*base;
	xfs_bmbt_rec_t		*ep;
	xfs_bmbt_irec_t		left;
	int			left_contig;
	int			left_delay;
	xfs_fsblock_t		left_endoff;
	int			left_valid;
	xfs_fsblock_t		new_endoff;
	xfs_extnum_t		nextents;
	xfs_bmbt_irec_t		right;
	int			right_contig;
	int			right_delay;
	int			right_valid;

	nextents = ip->i_bytes / sizeof(xfs_bmbt_rec_t);
	ASSERT(idx <= nextents);
	base = ip->i_u1.iu_extents;
	new_endoff = new->br_startoff + new->br_blockcount;
	/*
	 * Deal with the end-of-file cases first.
	 */
	if (idx == nextents) {
		ep = &base[idx - 1];
		xfs_bmbt_get_all(ep, left);
		left_endoff = left.br_startoff + left.br_blockcount;
		left_delay = left.br_startblock == NULLSTARTBLOCK;
		/*
		 * At end of file, contiguous to old delayed allocation
		 */
		if (left_delay && left_endoff == new->br_startoff) {
			xfs_bmbt_set_blockcount(ep,
				left.br_blockcount + new->br_blockcount);
			ip->i_lastex = idx - 1;
			kmem_check();
			return 0;
		}
		/*
		 * At end of file, not contiguous to an old delayed allocation
		 */
		xfs_bmap_insert_exlist(ip, idx, 1, new);
		ip->i_lastex = idx;
		kmem_check();
		return 0;
	}
	ep = &base[idx];
	xfs_bmbt_get_all(ep, right);
	right_delay = right.br_startblock == NULLSTARTBLOCK;
	/*
	 * Check and set flags if this segment has a left neighbor
	 */
	if (left_valid = ep > base) {
		xfs_bmbt_get_all(ep - 1, left);
		left_delay = left.br_startblock == NULLSTARTBLOCK;
		left_endoff = left.br_startoff + left.br_blockcount;
	}
	/*
	 * We're inserting a delayed allocation between "left" and "right".
	 */
	left_contig = left_valid && left_delay &&
		      left_endoff == new->br_startoff;
	right_contig = right_delay && new_endoff == right.br_startoff;
	/*
	 * New allocation is contiguous with delayed allocations on the
	 * left and on the right.
	 * Merge all three into a single extent list entry.
	 */
	if (left_contig && right_contig) {
		xfs_bmbt_set_blockcount(ep - 1,
			left.br_blockcount + new->br_blockcount +
			right.br_blockcount);
		xfs_bmap_delete_exlist(ip, idx, 1);
		ip->i_lastex = idx - 1;
		kmem_check();
		return 0;
	}
	/*
	 * New allocation is contiguous with a delayed allocation on the left.
	 * Merge the new allocation with the left neighbor.
	 */
	if (left_contig && !right_contig) {
		xfs_bmbt_set_blockcount(ep - 1,
			left.br_blockcount + new->br_blockcount);
		ip->i_lastex = idx - 1;
		kmem_check();
		return 0;
	}
	/*
	 * New allocation is contiguous with a delayed allocation on the right.
	 * Merge the new allocation with the right neighbor.
	 */
	if (!left_contig && right_contig) {
		xfs_bmbt_set_startoff(ep, new->br_startoff);
		xfs_bmbt_set_blockcount(ep,
			new->br_blockcount + right.br_blockcount);
		ip->i_lastex = idx;
		kmem_check();
		return 0;
	}
	/*
	 * New allocation is not contiguous with another delayed allocation.
	 * Insert a new entry.
	 */
	if (!left_contig && !right_contig) {
		xfs_bmap_insert_exlist(ip, idx, 1, new);
		ip->i_lastex = idx;
		kmem_check();
		return 0;
	}
	ASSERT(0);
}

/*
 * Called by xfs_bmap_add_extent to handle cases converting a hole
 * to a real allocation.
 */
STATIC int
xfs_bmap_add_extent_hole_real(
	xfs_inode_t		*ip,	/* incore inode pointer */
	xfs_extnum_t		idx,	/* extent number to update/insert */
	xfs_btree_cur_t		*cur,	/* if null, not a btree */
	xfs_bmbt_irec_t		*new)	/* new data to put in extent list */
{
	xfs_bmbt_rec_t		*base;
	int			btree;
	xfs_bmbt_rec_t		*ep;
	xfs_bmbt_irec_t		left;
	int			left_contig;
	int			left_delay;
	xfs_fsblock_t		left_endblock;
	xfs_fsblock_t		left_endoff;
	int			left_valid;
	xfs_fsblock_t		new_endblock;
	xfs_fsblock_t		new_endoff;
	xfs_extnum_t		nextents;
	xfs_bmbt_irec_t		right;
	int			right_contig;
	int			right_delay;
	int			right_valid;

	nextents = ip->i_bytes / sizeof(xfs_bmbt_rec_t);
	ASSERT(idx <= nextents);
	base = ip->i_u1.iu_extents;
	btree = cur != NULL;
	new_endoff = new->br_startoff + new->br_blockcount;
	new_endblock = new->br_startblock + new->br_blockcount;
	/*
	 * Deal with the end-of-file cases first.
	 */
	if (idx == nextents) {
		ep = &base[idx - 1];
		xfs_bmbt_get_all(ep, left);
		left_endoff = left.br_startoff + left.br_blockcount;
		if (!(left_delay = left.br_startblock == NULLSTARTBLOCK))
			left_endblock = left.br_startblock + left.br_blockcount;
		/*
		 * At end of file, contiguous to old real allocation
		 */
		if (!left_delay && left_endoff == new->br_startoff &&
		    left_endblock == new->br_startblock) {
			xfs_bmbt_set_blockcount(ep,
				left.br_blockcount + new->br_blockcount);
			ip->i_lastex = idx - 1;
			kmem_check();
			if (!btree)
				return XFS_ILOG_EXT;
			xfs_bmbt_lookup_eq(cur, left.br_startoff,
				left.br_startblock, left.br_blockcount);
			xfs_bmbt_update(cur, left.br_startoff,
				left.br_startblock,
				left.br_blockcount + new->br_blockcount);
			kmem_check();
			return 0;
		}
		/*
		 * At end of file, not contiguous to an old real allocation
		 */
		xfs_bmap_insert_exlist(ip, idx, 1, new);
		ip->i_lastex = idx;
		ip->i_d.di_nextents++;
		kmem_check();
		if (!btree)
			return XFS_ILOG_CORE | XFS_ILOG_EXT;
		xfs_bmbt_lookup_eq(cur, new->br_startoff, new->br_startblock,
			new->br_blockcount);
		xfs_bmbt_insert(cur);
		kmem_check();
		return XFS_ILOG_CORE;
	}
	ep = &base[idx];
	xfs_bmbt_get_all(ep, right);
	right_delay = right.br_startblock == NULLSTARTBLOCK;
	/*
	 * Check and set flags if this segment has a left neighbor
	 */
	if (left_valid = ep > base) {
		xfs_bmbt_get_all(ep - 1, left);
		if (!(left_delay = left.br_startblock == NULLSTARTBLOCK))
			left_endblock = left.br_startblock + left.br_blockcount;
		left_endoff = left.br_startoff + left.br_blockcount;
	}
	/*
	 * We're inserting a real allocation between "left" and "right".
	 */
	left_contig = left_valid && !left_delay &&
		      left_endoff == new->br_startoff &&
		      left_endblock == new->br_startblock;
	right_contig = !right_delay && new_endoff == right.br_startoff &&
		       new_endblock == right.br_startblock;
	/*
	 * New allocation is contiguous with real allocations on the
	 * left and on the right.
	 * Merge all three into a single extent list entry.
	 */
	if (left_contig && right_contig) {
		xfs_bmbt_set_blockcount(ep - 1,
			left.br_blockcount + new->br_blockcount +
			right.br_blockcount);
		xfs_bmap_delete_exlist(ip, idx, 1);
		ip->i_lastex = idx - 1;
		ip->i_d.di_nextents--;
		kmem_check();
		if (!btree)
			return XFS_ILOG_CORE | XFS_ILOG_EXT;
		xfs_bmbt_lookup_eq(cur, left.br_startoff, left.br_startblock,
			left.br_blockcount);
		xfs_bmbt_update(cur, left.br_startoff, left.br_startblock, left.br_blockcount + new->br_blockcount + right.br_blockcount);
		xfs_bmbt_increment(cur, 0);
		xfs_bmbt_delete(cur);
		kmem_check();
		return XFS_ILOG_CORE;
	}
	/*
	 * New allocation is contiguous with a real allocation on the left.
	 * Merge the new allocation with the left neighbor.
	 */
	if (left_contig && !right_contig) {
		xfs_bmbt_set_blockcount(ep - 1, left.br_blockcount + new->br_blockcount);
		ip->i_lastex = idx - 1;
		kmem_check();
		if (!btree)
			return XFS_ILOG_EXT;
		xfs_bmbt_lookup_eq(cur, left.br_startoff, left.br_startblock,
			left.br_blockcount);
		xfs_bmbt_update(cur, left.br_startoff, left.br_startblock,
			left.br_blockcount + new->br_blockcount);
		kmem_check();
		return 0;
	}
	/*
	 * New allocation is contiguous with a real allocation on the right.
	 * Merge the new allocation with the right neighbor.
	 */
	if (!left_contig && right_contig) {
		xfs_bmbt_set_startoff(ep, new->br_startoff);
		xfs_bmbt_set_startblock(ep, new->br_startblock);
		xfs_bmbt_set_blockcount(ep,
			new->br_blockcount + right.br_blockcount);
		ip->i_lastex = idx;
		kmem_check();
		if (!btree)
			return XFS_ILOG_EXT;
		xfs_bmbt_lookup_eq(cur, right.br_startoff, right.br_startblock,
			right.br_blockcount);
		xfs_bmbt_update(cur, new->br_startoff, new->br_startblock,
			new->br_blockcount + right.br_blockcount);
		kmem_check();
		return 0;
	}
	/*
	 * New allocation is not contiguous with another real allocation.
	 * Insert a new entry.
	 */
	if (!left_contig && !right_contig) {
		xfs_bmap_insert_exlist(ip, idx, 1, new);
		ip->i_lastex = idx;
		ip->i_d.di_nextents++;
		kmem_check();
		if (!btree)
			return XFS_ILOG_CORE | XFS_ILOG_EXT;
		xfs_bmbt_lookup_eq(cur, new->br_startoff, new->br_startblock,
			new->br_blockcount);
		xfs_bmbt_insert(cur);
		kmem_check();
		return XFS_ILOG_CORE;
	}
}

/*
 * Add the extent to the list of extents to be free at transaction end.
 * The list is maintained sorted (by block number).
 */
STATIC void
xfs_bmap_add_free(
	xfs_fsblock_t		bno,		/* fs block number of extent */
	xfs_extlen_t		len,		/* length of extent */
	xfs_bmap_free_t		**flist)	/* list of extents */
{
	xfs_bmap_free_t		*cur;		/* current (next) element */
	xfs_bmap_free_t		*new;		/* new element */
	xfs_bmap_free_t		*prev;		/* previous element */

	if (!xfs_bmap_free_zone)
		xfs_bmap_free_zone = kmem_zone_init(sizeof(*cur), "xfs_bmap_free");
	new = kmem_zone_alloc(xfs_bmap_free_zone, KM_SLEEP);
	new->xbf_startblock = bno;
	new->xbf_blockcount = len;
	for (prev = NULL, cur = *flist; cur != NULL; prev = cur, cur = cur->xbf_next) {
		if (cur->xbf_startblock >= bno)
			break;
	}
	if (prev)
		prev->xbf_next = new;
	else
		*flist = new;
	new->xbf_next = cur;
	kmem_check();
}

STATIC xfs_fsblock_t
xfs_bmap_alloc(
	xfs_trans_t	*tp,
	xfs_inode_t	*ip,
	int		eof,
	xfs_bmbt_irec_t	*prevp,
	xfs_bmbt_irec_t	*gotp,
	xfs_fsblock_t	firstblock,
	xfs_extlen_t	*alen,
	xfs_extlen_t	total,
	xfs_fsblock_t	off,
	int		wasdel)
{
	xfs_fsblock_t	abno;
	xfs_fsblock_t	askbno;
	xfs_extlen_t	asklen;
	xfs_fsblock_t	bno;
	xfs_agnumber_t	fb_agno;
	xfs_fsblock_t	gotbno;
	xfs_fsblock_t	gotdiff;
	xfs_mount_t	*mp;
	int		nullfb;
	xfs_fsblock_t	prevbno;
	xfs_fsblock_t	prevdiff;
	xfs_sb_t	*sbp;
	xfs_alloctype_t	type;

	asklen = *alen;
	mp = ip->i_mount;
	sbp = &mp->m_sb;
	nullfb = firstblock == NULLFSBLOCK;
	fb_agno = nullfb ? NULLAGNUMBER : xfs_fsb_to_agno(sbp, firstblock);
	bno = nullfb ? xfs_ino_to_fsb(sbp, ip->i_ino) : firstblock;
	if (eof && prevp->br_startoff != NULLFSBLOCK &&
	    prevp->br_startblock != NULLSTARTBLOCK)
		bno = prevp->br_startblock + prevp->br_blockcount;
	else if (!eof) {
		if (prevp->br_startoff != NULLFSBLOCK &&
		    prevp->br_startblock != NULLSTARTBLOCK) {
			prevdiff = off - (prevp->br_startoff + prevp->br_blockcount);
			prevbno = prevp->br_startblock + prevp->br_blockcount + prevdiff;
			if (xfs_fsb_to_agno(sbp, prevbno) !=
			    xfs_fsb_to_agno(sbp, prevp->br_startblock) ||
			    xfs_fsb_to_agbno(sbp, prevbno) >= sbp->sb_dblocks)
				prevbno = prevp->br_startblock;
			if (!nullfb && xfs_fsb_to_agno(sbp, prevbno) != fb_agno)
				prevbno = NULLFSBLOCK;
		} else
			prevbno = NULLFSBLOCK;
		if (gotp->br_startblock != NULLSTARTBLOCK) {
			gotdiff = gotp->br_startoff - off;
			gotbno = gotp->br_startblock - gotdiff;
			if (xfs_fsb_to_agno(sbp, gotbno) !=
			    xfs_fsb_to_agno(sbp, gotp->br_startblock))
				gotbno = gotp->br_startblock;
			if (!nullfb && xfs_fsb_to_agno(sbp, gotbno) != fb_agno)
				gotbno = NULLFSBLOCK;
		} else
			gotbno = NULLFSBLOCK;
		if (prevbno != NULLFSBLOCK && gotbno != NULLFSBLOCK)
			bno = prevdiff <= gotdiff ? prevbno : gotbno;
		else if (prevbno != NULLFSBLOCK)
			bno = prevbno;
		else if (gotbno != NULLFSBLOCK)
			bno = gotbno;
	}
	if (nullfb || xfs_fsb_to_agno(sbp, bno) == fb_agno)
		askbno = bno;
	else
		askbno = firstblock;
	type = nullfb ? XFS_ALLOCTYPE_START_BNO : XFS_ALLOCTYPE_NEAR_BNO;
	abno = xfs_alloc_vextent(tp, askbno, 1, asklen, alen, type, total, wasdel);
	/* for debugging */ ASSERT(abno != NULLFSBLOCK);
	kmem_check();
	return abno;
}

STATIC int
xfs_bmap_btree_to_extents(
	xfs_trans_t		*tp,
	xfs_inode_t		*ip,
	xfs_btree_cur_t		*cur)
{
	/* return logflags */
	return 0;
}

#ifdef XFSDEBUG
STATIC void
xfs_bmap_check_extents(
	xfs_inode_t		*ip)
{
	xfs_bmbt_rec_t		*base;
	xfs_extnum_t		nextents;
	xfs_bmbt_rec_t		*rp;

	ASSERT(ip->i_flags & XFS_IEXTENTS);
	base = ip->i_u1.iu_extents;
	nextents = ip->i_bytes / sizeof(xfs_bmbt_rec_t);
	for (rp = base; rp < &base[nextents - 1]; rp++)
		xfs_btree_check_rec(XFS_BTNUM_BMAP, (void *)rp, (void *)(rp + 1));
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
	xfs_bmap_free_t		**flist,/* list of extents to be freed */
	xfs_btree_cur_t		*cur,	/* if null, not a btree */
	xfs_bmbt_irec_t		*del)	/* data to remove from extent list */
{
	xfs_fsblock_t		del_endblock;
	xfs_fsblock_t		del_endoff;
	int			delay;
	xfs_bmbt_rec_t		*ep;
	xfs_bmbt_irec_t		got;
	xfs_fsblock_t		got_endoff;
	xfs_extnum_t		nextents;

	nextents = ip->i_bytes / sizeof(xfs_bmbt_rec_t);
	ASSERT(idx >= 0 && idx < nextents);
	ep = &ip->i_u1.iu_extents[idx];
	xfs_bmbt_get_all(ep, got);
	del_endoff = del->br_startoff + del->br_blockcount;
	got_endoff = got.br_startoff + got.br_blockcount;
	ASSERT((del->br_startblock == NULLSTARTBLOCK) ==
	       (got.br_startblock == NULLSTARTBLOCK));
	delay = got.br_startblock == NULLSTARTBLOCK;
	if (!delay) {
		xfs_bmap_add_free(del->br_startblock, del->br_blockcount, flist);
		del_endblock = del->br_startblock + del->br_blockcount;
		if (cur)
			xfs_bmbt_lookup_eq(cur, got.br_startoff, got.br_startblock, got.br_blockcount);
	}
	/*
	 * Matches the whole extent.  Delete the entry.
	 */
	if (got.br_startoff == del->br_startoff && got_endoff == del_endoff) {
		xfs_bmap_delete_exlist(ip, idx, 1);
		kmem_check();
		if (delay)
			return 0;
		ip->i_d.di_nextents--;
		if (!cur)
			return XFS_ILOG_CORE | XFS_ILOG_EXT;
		xfs_bmbt_delete(cur);
		kmem_check();
		return XFS_ILOG_CORE;
	/*
	 * Deleting the first part of the extent.
	 */
	} else if (got.br_startoff == del->br_startoff &&
		   got_endoff > del_endoff) {
		xfs_bmbt_set_startoff(ep, del_endoff);
		xfs_bmbt_set_blockcount(ep, got.br_blockcount - del->br_blockcount);
		kmem_check();
		if (delay)
			return 0;
		xfs_bmbt_set_startblock(ep, del_endblock);
		if (!cur)
			return XFS_ILOG_EXT;
		xfs_bmbt_update(cur, del_endoff, del_endblock, got.br_blockcount - del->br_blockcount);
		kmem_check();
		return 0;
	/*
	 * Deleting the last part of the extent.
	 */
	} else if (got.br_startoff < del->br_startoff &&
		   got_endoff == del_endoff) {
		xfs_bmbt_set_blockcount(ep, got.br_blockcount - del->br_blockcount);
		kmem_check();
		if (delay)
			return 0;
		if (!cur)
			return XFS_ILOG_EXT;
		xfs_bmbt_update(cur, got.br_startoff, got.br_startblock, got.br_blockcount - del->br_blockcount);
		kmem_check();
		return 0;
	} else if (got.br_startoff < del->br_startoff &&
		   got_endoff > del_endoff) {
		xfs_bmbt_irec_t new;

		xfs_bmbt_set_blockcount(ep, del->br_startoff - got.br_startoff);
		new.br_startoff = del_endoff;
		if (delay)
			new.br_startblock = NULLSTARTBLOCK;
		else
			new.br_startblock = del_endblock;
		new.br_blockcount = got_endoff - new.br_startoff;
		xfs_bmap_insert_exlist(ip, idx + 1, 1, &new);
		kmem_check();
		if (delay)
			return 0;
		ip->i_d.di_nextents++;
		if (!cur)
			return XFS_ILOG_CORE | XFS_ILOG_EXT;
		xfs_bmbt_update(cur, got.br_startoff, got.br_startblock, del->br_startoff - got.br_startoff);
		xfs_bmbt_increment(cur, 0);
		cur->bc_rec.b = new;
		xfs_bmbt_insert(cur);
		kmem_check();
		return XFS_ILOG_CORE;
	} else
		ASSERT(0);
}

STATIC void
xfs_bmap_del_free(
	xfs_bmap_free_t		**flist,
	xfs_bmap_free_t		*prev,
	xfs_bmap_free_t		*free)
{
	if (prev)
		prev->xbf_next = free->xbf_next;
	else
		*flist = free->xbf_next;
	kmem_zone_free(xfs_bmap_free_zone, free);
	kmem_check();
}

STATIC void
xfs_bmap_delete_exlist(
	xfs_inode_t	*ip,
	xfs_extnum_t	idx,
	xfs_extnum_t	count)
{
	xfs_bmbt_rec_t	*ep;
	xfs_extnum_t	from;
	xfs_extnum_t	nextents;
	xfs_extnum_t	to;

	ASSERT(ip->i_flags & XFS_IEXTENTS);
	ep = ip->i_u1.iu_extents;
	nextents = ip->i_bytes / sizeof(xfs_bmbt_rec_t) - count;
	for (to = idx, from = to + count; to < nextents; from++, to++)
		ep[to] = ep[from];
	xfs_iext_realloc(ip, -count);
	kmem_check();
}

STATIC xfs_fsblock_t
xfs_bmap_extents_to_btree(
	xfs_trans_t		*tp,
	xfs_inode_t		*ip,
	xfs_fsblock_t		firstblock,
	xfs_bmap_free_t		**flist)
{
	xfs_btree_lblock_t	*ablock;
	xfs_fsblock_t		abno;
	buf_t			*abuf;
	xfs_bmbt_rec_t		*arp;
	xfs_btree_lblock_t	*block;
	xfs_btree_cur_t		*cur;
	xfs_bmbt_rec_t		*ep;
	xfs_extnum_t		i;
	xfs_bmbt_key_t		*kp;
	xfs_mount_t		*mp;
	xfs_extnum_t		nextents;
	xfs_bmbt_ptr_t		*pp;
	xfs_sb_t		*sbp;
	xfs_alloctype_t		type;

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
	block->bb_leftsib = block->bb_rightsib = NULLFSBLOCK;
	/*
	 * Need a cursor.  Can't allocate until bb_level is filled in.
	 */
	mp = ip->i_mount;
	sbp = &mp->m_sb;
	cur = xfs_btree_init_cursor(mp, tp, NULL, 0, XFS_BTNUM_BMAP, ip);
	cur->bc_private.b.firstblock = firstblock;
	cur->bc_private.b.flist = flist;
	/*
	 * Convert to a btree with two levels, one record in root.
	 */
	ip->i_d.di_format = XFS_DINODE_FMT_BTREE;
	type = firstblock == NULLFSBLOCK ?
		XFS_ALLOCTYPE_START_BNO : XFS_ALLOCTYPE_NEAR_BNO;
	abno = xfs_alloc_extent(tp, firstblock, 1, type, 0, 0);
	/*
	 * Allocation can't fail, the space was reserved.
	 */
	ASSERT(abno != NULLFSBLOCK);
	if (firstblock == NULLFSBLOCK)	
		firstblock = abno;
	abuf = xfs_btree_get_bufl(mp, tp, abno, 0);
	/*
	 * Fill in the child block.
	 */
	ablock = xfs_buf_to_lblock(abuf);
	ablock->bb_magic = XFS_BMAP_MAGIC;
	ablock->bb_level = 0;
	ablock->bb_numrecs = 0;
	ablock->bb_leftsib = ablock->bb_rightsib = NULLFSBLOCK;
	arp = XFS_BMAP_REC_IADDR(ablock, 1, cur);
	nextents = ip->i_bytes / sizeof(xfs_bmbt_rec_t);
	for (ep = ip->i_u1.iu_extents, i = 0; i < nextents; i++, ep++) {
		if (xfs_bmbt_get_startblock(ep) != NULLSTARTBLOCK) {
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
	*pp = abno;
	/*
	 * Do all this logging at the end so that 
	 * the root is at the right level.
	 */
	xfs_bmbt_log_block(cur, abuf, XFS_BB_ALL_BITS);
	xfs_bmbt_log_recs(cur, abuf, 1, ablock->bb_numrecs);
	xfs_trans_log_inode(tp, ip, XFS_ILOG_BROOT);
	xfs_bmbt_rcheck(cur);
	xfs_bmbt_kcheck(cur);
	xfs_btree_del_cursor(cur);
	kmem_check();
	return firstblock;
}

STATIC void
xfs_bmap_insert_exlist(
	xfs_inode_t	*ip,
	xfs_extnum_t	idx,
	xfs_extnum_t	count,
	xfs_bmbt_irec_t	*new)
{
	xfs_bmbt_rec_t	*ep;
	xfs_extnum_t	from;
	xfs_extnum_t	nextents;
	xfs_extnum_t	to;

	ASSERT(ip->i_flags & XFS_IEXTENTS);
	xfs_iext_realloc(ip, count);
	ep = ip->i_u1.iu_extents;
	nextents = ip->i_bytes / sizeof(xfs_bmbt_rec_t);
	for (to = nextents - 1, from = to - count; from >= idx; from--, to--)
		ep[to] = ep[from];
	for (to = idx; to < idx + count; to++, new++)
		xfs_bmbt_set_all(&ep[to], *new);
	kmem_check();
}

/*
 * Convert a local file to an extents file.
 * This code is sort of bogus, since the file data needs to get 
 * logged so it won't be lost.
 */
STATIC xfs_fsblock_t
xfs_bmap_local_to_extents(
	xfs_trans_t	*tp,
	xfs_inode_t	*ip,
	xfs_fsblock_t	firstblock,
	xfs_extlen_t	total)
{
	xfs_fsblock_t	askbno;
	xfs_fsblock_t	bno;
	buf_t		*buf;
	char		*cp;
	xfs_bmbt_rec_t	*ep;
	int		flags = 0;
	xfs_mount_t	*mp;
	xfs_sb_t	*sbp;
	xfs_alloctype_t	type;

	ASSERT(ip->i_d.di_format == XFS_DINODE_FMT_LOCAL);
	mp = ip->i_mount;
	sbp = &mp->m_sb;
	if (ip->i_bytes) {
		ASSERT(ip->i_flags & XFS_IINLINE);
		/*
		 * Allocate a block.  We know we need only one, since the
		 * file currently fits in an inode.
		 */
		if (firstblock == NULLFSBLOCK) {
			askbno = xfs_ino_to_fsb(sbp, ip->i_ino);
			type = XFS_ALLOCTYPE_START_BNO;
			total++;
		} else {
			askbno = firstblock;
			type = XFS_ALLOCTYPE_NEAR_BNO;
			total = 0;
		}
		bno = xfs_alloc_extent(tp, askbno, 1, type, total, 0);
		/* 
		 * Can't fail, the space was reserved.
		 */
		ASSERT(bno != NULLFSBLOCK);
		if (firstblock == NULLFSBLOCK)
			firstblock = bno;
		buf = xfs_btree_get_bufl(mp, tp, bno, 0);
		cp = (char *)buf->b_un.b_addr;
		bcopy(ip->i_u1.iu_data, cp, ip->i_bytes);
		xfs_idata_realloc(ip, -ip->i_bytes);
		xfs_iext_realloc(ip, 1);
		ep = ip->i_u1.iu_extents;
		xfs_bmbt_set_startoff(ep, 0);
		xfs_bmbt_set_startblock(ep, bno);
		xfs_bmbt_set_blockcount(ep, 1);
		ip->i_d.di_nextents = 1;
		flags |= XFS_ILOG_EXT;
	} else
		ASSERT(ip->i_d.di_nextents == 0);
	ip->i_flags &= ~XFS_IINLINE;
	ip->i_flags |= XFS_IEXTENTS;
	ip->i_d.di_format = XFS_DINODE_FMT_EXTENTS;
	flags |= XFS_ILOG_CORE;
	xfs_trans_log_inode(tp, ip, flags);
	kmem_check();
	return firstblock;
}

/*
 * Search the extents list for the inode, for the extent containing bno.
 * If bno lies in a hole, point to the next entry.  If bno lies past eof,
 * *eofp will be set, and *prevp will contain the last entry (null if none).
 * Else, *lastxp will be set to the index of the found
 * entry; *gotp will contain the entry.
 */
STATIC xfs_bmbt_rec_t *
xfs_bmap_search_extents(
	xfs_inode_t	*ip,
	xfs_fsblock_t	bno,
	int		*eofp,
	xfs_extnum_t	*lastxp,
	xfs_bmbt_irec_t	*gotp,
	xfs_bmbt_irec_t	*prevp)
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
	if (lastx != NULLEXTNUM && lastx < nextents) {
		base = &ip->i_u1.iu_extents[0];
		ep = base + lastx;
	} else
		ep = NULL;
	prevp->br_startoff = NULLFSBLOCK;
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
				xfs_bmbt_get_all(ep, *prevp);
				ep++;
				got.br_startoff = xfs_bmbt_get_startoff(ep);
				got.br_blockcount = xfs_bmbt_get_blockcount(ep);
			}
		} else {
			*eofp = 0;
			if (ep > base)
				xfs_bmbt_get_all(ep - 1, *prevp);
		}
	}
	if (ep)
		got.br_startblock = xfs_bmbt_get_startblock(ep);
	*lastxp = lastx;
	*gotp = got;
	kmem_check();
	return ep;
}

/*
 * Routine to be called at transaction's end by xfs_bmapi, xfs_bunmapi 
 * caller.  Frees all the extents that need freeing, which must be done
 * last due to locking considerations.
 */
void
xfs_bmap_finish(
	xfs_trans_t		**tp,		/* transaction pointer addr */
	xfs_bmap_free_t		**flist,	/* i/o: list extents to free */
	xfs_fsblock_t		firstblock)	/* controlled a.g. for allocs */
{
	xfs_efd_log_item_t	*efd;
	xfs_efi_log_item_t	*efi;
	xfs_agnumber_t		firstag;
	xfs_bmap_free_t		*free;
	xfs_mount_t		*mp;
	xfs_bmap_free_t		*next;
	xfs_trans_t		*ntp;
	xfs_bmap_free_t		*prev;
	xfs_sb_t		*sbp;

	if (*flist == NULL)
		return;
	ntp = *tp;
	mp = ntp->t_mountp;
	sbp = &mp->m_sb;
	firstag = xfs_fsb_to_agno(sbp, firstblock);
	for (prev = NULL, free = *flist; free != NULL; free = next) {
		next = free->xbf_next;
		if (xfs_fsb_to_agno(sbp, free->xbf_startblock) < firstag) {
			efi = xfs_trans_get_efi(ntp, 1);
			xfs_trans_log_efi_extent(ntp, efi, free->xbf_startblock, free->xbf_blockcount);
			free->xbf_efip = efi;
			prev = free;
			continue;
		}
		xfs_free_extent(ntp, free->xbf_startblock, free->xbf_blockcount);
		xfs_bmap_del_free(flist, prev, free);
	}
	xfs_trans_commit(ntp, 0);
	ntp = xfs_trans_alloc(mp, 0);
	xfs_trans_reserve(ntp, 128 /* blocks to allocate? */, 128/* log */, 0);
	for (free = *flist; free != NULL; free = next) {
		next = free->xbf_next;
		xfs_free_extent(ntp, free->xbf_startblock, free->xbf_blockcount);
		efd = xfs_trans_get_efd(ntp, free->xbf_efip, 1);
		xfs_trans_log_efd_extent(ntp, efd, free->xbf_startblock, free->xbf_blockcount);
		xfs_bmap_del_free(flist, NULL, free);
	}
	*tp = ntp;
	kmem_check();
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
	xfs_btree_lblock_t	*block;	/* current btree block */
	xfs_bmbt_ptr_t		bno;	/* block # of "block" */
	buf_t			*buf;	/* buffer for "block" */
	xfs_extnum_t		i;	/* index into the extents list */
	xfs_mount_t		*mp;	/* file system mount structure */
	xfs_extnum_t		room;	/* number of entries there's room for */
	xfs_sb_t		*sbp;	/* super block structure */
	xfs_bmbt_rec_t		*trp;	/* target record pointer */

	bno = NULLFSBLOCK;
	mp = ip->i_mount;
	sbp = &mp->m_sb;
	block = ip->i_broot;
	/*
	 * Root level must use BMAP_BROOT_PTR_ADDR macro to get ptr out.
	 */
	if (block->bb_level)
		bno = *XFS_BMAP_BROOT_PTR_ADDR(block, 1, sbp->sb_inodesize);
	/*
	 * Go down the tree until leaf level is reached, following the first
	 * pointer (leftmost) at each level.
	 */
	while (block->bb_level) {
		buf = xfs_btree_read_bufl(mp, tp, bno, 0);
		block = xfs_buf_to_lblock(buf);
		if (block->bb_level == 0)
			break;
		bno = *XFS_BTREE_PTR_ADDR(sbp->sb_blocksize, xfs_bmbt,
					  block, 1);
		xfs_trans_brelse(tp, buf);
	}
	/*
	 * Here with buf and block set to the leftmost leaf node in the tree.
	 */
	trp = ip->i_u1.iu_extents;
	room = ip->i_bytes / sizeof(*trp);
	i = 0;
	/*
	 * Loop over all leaf nodes.  Copy information to the extent list.
	 */
	for (;;) {
		xfs_bmbt_rec_t	*frp;
		xfs_bmbt_ptr_t	nextbno;

		ASSERT(i + block->bb_numrecs <= room);
		/*
		 * Copy records into the extent list.
		 */
		frp = XFS_BTREE_REC_ADDR(sbp->sb_blocksize, xfs_bmbt, block, 1);
		bcopy(frp, trp, block->bb_numrecs * sizeof(*frp));
		trp += block->bb_numrecs;
		i += block->bb_numrecs;
		/*
		 * Get the next leaf block's address.
		 */
		nextbno = block->bb_rightsib;
		/*
		 * bno can only be NULLFSBLOCK if the root block is also a leaf;
		 * we could make this illegal.
		 */
		if (bno != NULLFSBLOCK)
			xfs_trans_brelse(tp, buf);
		bno = nextbno;
		/*
		 * If we've reached the end, stop.
		 */
		if (bno == NULLFSBLOCK)
			break;
		buf = xfs_btree_read_bufl(mp, tp, bno, 0);
		block = xfs_buf_to_lblock(buf);
	}
	kmem_check();
}

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
	xfs_fsblock_t		bno,		/* starting file offs. mapped */
	xfs_extlen_t		len,		/* length to map in file */
	int			flags,		/* XFS_BMAPI_... */
	xfs_fsblock_t		firstblock,	/* controls a.g. for allocs */
	xfs_extlen_t		total,		/* total blocks needed */
	xfs_bmbt_irec_t		*mval,		/* output: map values */
	int			*nmap,		/* i/o: mval size/count */
	xfs_bmap_free_t		**flist)	/* i/o: list extents to free */
{
	xfs_fsblock_t		abno;
	xfs_extlen_t		alen;
	xfs_fsblock_t		aoff;
	xfs_fsblock_t		askbno;
	xfs_extlen_t		asklen;
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
	xfs_mount_t		*mp;
	int			n;
	xfs_bmbt_irec_t		prev;
	xfs_sb_t		*sbp;
	xfs_alloctype_t		type;
	int			wasdelay;
	int			wr;

	ASSERT(*nmap >= 1 && *nmap <= XFS_BMAP_MAX_NMAP);
	ASSERT(ip->i_d.di_format == XFS_DINODE_FMT_BTREE ||
	       ip->i_d.di_format == XFS_DINODE_FMT_EXTENTS ||
	       ip->i_d.di_format == XFS_DINODE_FMT_LOCAL);
	wr = (flags & XFS_BMAPI_WRITE) != 0;
	delay = (flags & XFS_BMAPI_DELAY) != 0;
	ASSERT(wr || !delay);
	if (ip->i_d.di_format == XFS_DINODE_FMT_LOCAL) {
		if (!wr) {
			/* change to assert later */
			*nmap = 0;
			kmem_check();
			return firstblock;
		}
		firstblock = xfs_bmap_local_to_extents(tp, ip, firstblock, total);
		logflags |= XFS_ILOG_CORE;
	}
	if (firstblock == NULLFSBLOCK) {
		type = XFS_ALLOCTYPE_START_BNO;
		if (ip->i_d.di_format == XFS_DINODE_FMT_BTREE)
			total += ip->i_broot->bb_level + 1;
		else
			total++;
	} else {
		type = XFS_ALLOCTYPE_NEAR_BNO;
		total = 0;
	}
	if (!(ip->i_flags & XFS_IEXTENTS))
		xfs_iread_extents(tp, ip);
	ep = xfs_bmap_search_extents(ip, bno, &eof, &lastx, &got, &prev);
	mp = ip->i_mount;
	sbp = &mp->m_sb;
	logflags = n = 0;
	end = bno + len;
	cur = NULL;
	while (bno < end && n < *nmap) {
		if (eof && !wr)
			break;
		inhole = eof || got.br_startoff > bno;
		wasdelay = !inhole && got.br_startblock == NULLSTARTBLOCK;
		/*
		 * First, deal with the hole before the allocated space 
		 * that we found, if any.
		 */
		if ((inhole || wasdelay) && wr) {
			/*
			 * For the wasdelay case, we could also just
			 * allocate the stuff asked for in this bmap call
			 * but that wouldn't be as good.
			 */
			alen = eof ? len :
				(wasdelay ? got.br_blockcount :
				 xfs_extlen_min(got.br_startoff - bno, len));
			aoff = wasdelay ? got.br_startoff : bno;
			if (delay) {
				if (xfs_mod_incore_sb(mp, XFS_SB_FDBLOCKS, -alen))
					break;
				abno = NULLSTARTBLOCK;
			} else {
				abno = xfs_bmap_alloc(tp, ip, eof, &prev, &got, firstblock, &alen, total, aoff, wasdelay);
				if (abno == NULLFSBLOCK)
					break;
				if (firstblock == NULLFSBLOCK)
					firstblock = abno;
				if ((ip->i_flags & XFS_IBROOT) && !cur) {
					cur = xfs_btree_init_cursor(mp, tp, NULL, 0, XFS_BTNUM_BMAP, ip);
					cur->bc_private.b.firstblock = firstblock;
					cur->bc_private.b.flist = flist;
				}
			}
			got.br_startoff = aoff;
			got.br_startblock = abno;
			got.br_blockcount = alen;
			logflags |= xfs_bmap_add_extent(ip, lastx, cur, &got);
			lastx = ip->i_lastex;
			ep = &ip->i_u1.iu_extents[lastx];
			xfs_bmbt_get_all(ep, got);
			/*
			 * Fall down into the found allocated space case.
			 */
		} else if (inhole) {
			/*
			 * Reading in a hole.
			 */
			mval->br_startoff = bno;
			mval->br_startblock = NULLSTARTBLOCK;
			mval->br_blockcount = xfs_extlen_min(len, got.br_startoff - bno);
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
		mval->br_startoff = bno;
		if (got.br_startblock == NULLSTARTBLOCK)
			mval->br_startblock = NULLSTARTBLOCK;
		else
			mval->br_startblock =
				got.br_startblock + (bno - got.br_startoff);
		mval->br_blockcount =
			xfs_extlen_min(len,
				got.br_blockcount - (bno - got.br_startoff));
		bno += mval->br_blockcount;
		len -= mval->br_blockcount;
		if (n > 0 && mval->br_startblock != NULLSTARTBLOCK &&
		    mval[-1].br_startblock != NULLSTARTBLOCK &&
		    mval->br_startblock == mval[-1].br_startblock + mval[-1].br_blockcount) {
			ASSERT(mval->br_startoff == mval[-1].br_startoff + mval[-1].br_blockcount);
			mval[-1].br_blockcount += mval->br_blockcount;
		} else {
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
		if (lastx >= ip->i_lastex) {
			eof = 1;
			prev = got;
		} else
			xfs_bmbt_get_all(ep, got);
	}
	ip->i_lastex = lastx;
	*nmap = n;
	/*
	 * Convert to a btree if necessary.
	 */
	if (ip->i_d.di_format == XFS_DINODE_FMT_EXTENTS &&
	    ip->i_d.di_nextents > XFS_BMAP_EXT_MAXRECS(sbp->sb_inodesize)) {
		firstblock = xfs_bmap_extents_to_btree(tp, ip, firstblock, flist);
		logflags |= XFS_ILOG_CORE;
	}
	/*
	 * Log everything.  Do this after conversion, there's no point in
	 * logging the extent list if we've converted to btree format.
	 */
	if ((logflags & XFS_ILOG_EXT) &&
	    ip->i_d.di_format != XFS_DINODE_FMT_EXTENTS)
		logflags &= ~XFS_ILOG_EXT;
	if (logflags)
		xfs_trans_log_inode(tp, ip, logflags);
	/* logging of the BROOT happens elsewhere */
	if (cur)
		xfs_btree_del_cursor(cur);
	kmem_check();
	return firstblock;
}

/*
 * Unmap (remove) blocks from a file.
 */
xfs_fsblock_t					/* first allocated block */
xfs_bunmapi(
	xfs_trans_t		*tp,		/* transaction pointer */
	struct xfs_inode	*ip,		/* incore inode */
	xfs_fsblock_t		bno,		/* starting offset to unmap */
	xfs_extlen_t		len,		/* length to unmap in file */
	xfs_fsblock_t		firstblock,	/* controls a.g. for allocs */
	xfs_bmap_free_t		**flist)	/* i/o: list extents to free */
{
	xfs_btree_cur_t		*cur;
	xfs_bmbt_irec_t		del;
	int			eof;
	xfs_bmbt_rec_t		*ep;
	xfs_bmbt_irec_t		got;
	xfs_extnum_t		i;
	xfs_extnum_t		lastx;
	int			logflags;
	xfs_mount_t		*mp;
	xfs_extnum_t		nextents;
	xfs_bmbt_irec_t		prev;
	xfs_sb_t		*sbp;
	xfs_fsblock_t		start;

	ASSERT(ip->i_d.di_format == XFS_DINODE_FMT_BTREE ||
	       ip->i_d.di_format == XFS_DINODE_FMT_EXTENTS);
	if (!(ip->i_flags & XFS_IEXTENTS))
		xfs_iread_extents(tp, ip);
	nextents = ip->i_bytes / sizeof(xfs_bmbt_rec_t);
	if (nextents == 0)
		return firstblock;
	start = bno;
	bno = start + len - 1;
	ep = xfs_bmap_search_extents(ip, bno, &eof, &lastx, &got, &prev);
	/*
	 * Check to see if the given block number is past the end of the
	 * file, back up to the last block if so...
	 * Do we want to reject this instead?
	 */
	if (eof) {
		lastx--;
		ep = &ip->i_u1.iu_extents[lastx];
		xfs_bmbt_get_all(ep, got);
		bno = got.br_startoff + got.br_blockcount - 1;
	}
	mp = ip->i_mount;
	sbp = &mp->m_sb;
	logflags = 0;
	if (ip->i_flags & XFS_IBROOT) {
		cur = xfs_btree_init_cursor(mp, tp, NULL, 0, XFS_BTNUM_BMAP, ip);
		cur->bc_private.b.firstblock = firstblock;
		cur->bc_private.b.flist = flist;
	} else
		cur = NULL;
	while (bno >= start && lastx >= 0) {
		/*
		 * Is the found extent after a hole in which bno lives?
		 * Just back up to the previous extent, if so.
		 */
		if (got.br_startoff > bno) {
			if (--lastx < 0)
				break;
			ep--;
			xfs_bmbt_get_all(ep, got);
			bno = got.br_startoff + got.br_blockcount - 1;
			if (bno < start)
				break;
		}
		/*
		 * Then deal with the (possibly delayed) allocated space
		 * we found.
		 */
		ASSERT(ep != NULL);
		del = got;
		if (got.br_startoff < start) {
			del.br_startoff = start;
			del.br_blockcount -= start - got.br_startoff;
			if (del.br_startblock != NULLSTARTBLOCK)
				del.br_startblock += start - got.br_startoff;
		}
		if (del.br_startoff + del.br_blockcount > start + len)
			del.br_blockcount = start + len - del.br_startoff;
		if (del.br_startblock == NULLSTARTBLOCK)
			xfs_mod_incore_sb(mp, XFS_SB_FDBLOCKS, del.br_blockcount);
			/* could fail?? */
		logflags |= xfs_bmap_del_extent(ip, lastx, flist, cur, &del);
		bno = del.br_startoff - 1;
		/*
		 * If not done go on to the next (previous) record.
		 */
		if (bno >= start && --lastx >= 0) {
			ep--;
			xfs_bmbt_get_all(ep, got);
		}
	}
	ip->i_lastex = lastx;
	/*
	 * Convert to a btree if necessary.
	 */
	if (ip->i_d.di_format == XFS_DINODE_FMT_EXTENTS &&
	    ip->i_d.di_nextents > XFS_BMAP_EXT_MAXRECS(sbp->sb_inodesize)) {
		firstblock = xfs_bmap_extents_to_btree(tp, ip, firstblock, flist);
		logflags |= XFS_ILOG_CORE;
	} 
	/* transform from btree to extents, give it cur */
	else if (ip->i_d.di_format == XFS_DINODE_FMT_BTREE &&
		 ip->i_d.di_nextents <= XFS_BMAP_EXT_MAXRECS(sbp->sb_inodesize)) {
		xfs_bmap_btree_to_extents(tp, ip, cur);
		logflags |= XFS_ILOG_CORE | XFS_ILOG_EXT;
	}
	/* transform from extents to local */
	if (cur)
		firstblock = cur->bc_private.b.firstblock;
	/*
	 * Log everything.  Do this after conversion, there's no point in
	 * logging the extent list if we've converted to btree format.
	 */
	if ((logflags & XFS_ILOG_EXT) &&
	    ip->i_d.di_format != XFS_DINODE_FMT_EXTENTS)
		logflags &= ~XFS_ILOG_EXT;
	if (logflags)
		xfs_trans_log_inode(tp, ip, logflags);
	/* logging of the BROOT happens elsewhere */
	if (cur)
		xfs_btree_del_cursor(cur);
	kmem_check();
	return firstblock;
}
