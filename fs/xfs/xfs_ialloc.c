#ident	"$Revision: 1.54 $"

#include <sys/param.h>
#include <sys/stat.h>
#include <sys/vnode.h>
#include <sys/debug.h>
#include <stddef.h>
#include "xfs_types.h"
#include "xfs_inum.h"
#ifdef SIM
#define _KERNEL
#endif
#include <sys/buf.h>
#include <sys/uuid.h>
#include <sys/grio.h>
#ifdef SIM
#undef _KERNEL
#endif
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
#include "xfs_alloc.h"
#include "xfs_bit.h"
#ifdef SIM
#include "sim.h"
#endif

/*
 * Prototypes for internal routines.
 */

/*
 * Log specified fields for the inode given by bp and off.
 */
STATIC void
xfs_ialloc_log_di(
	xfs_trans_t	*tp,		/* transaction pointer */
	buf_t		*bp,		/* inode buffer */
	int		off,		/* index of inode in buffer */
	int		fields);	/* bitmask of fields to log */

/*
 * Read in the allocation group header (inode allocation section)
 */
STATIC buf_t *				/* allocation group hdr buf */
xfs_ialloc_read_agi(
	xfs_mount_t	*mp,		/* file system mount structure */
	xfs_trans_t	*tp,		/* transaction pointer */
	xfs_agnumber_t	agno);		/* allocation group number */

/*
 * Prototypes for per-allocation group routines.
 */

/*
 * Allocate new inodes in the allocation group specified by agbp.
 * Return 0 for failure, 1 for success.
 */
STATIC int				/* success/failure */
xfs_ialloc_ag_alloc(
	xfs_trans_t	*tp,		/* transaction pointer */
	buf_t		*agbp);		/* alloc group buffer */

/*
 * Select an allocation group to look for a free inode in, based on the parent
 * inode and then mode.  Return the allocation group buffer.
 */
STATIC buf_t *				/* allocation group buffer */
xfs_ialloc_ag_select(
	xfs_trans_t	*tp,		/* transaction pointer */
	xfs_ino_t	parent,		/* parent directory inode number */
	mode_t		mode);		/* bits set to indicate file type */

/*
 * Internal functions.
 */

/*
 * Log specified fields for the inode given by bp and off.
 */
STATIC void
xfs_ialloc_log_di(
	xfs_trans_t	*tp,		/* transaction pointer */
	buf_t		*bp,		/* inode buffer */
	int		off,		/* index of inode in buffer */
	int		fields)		/* bitmask of fields to log */
{
	xfs_dinode_t		*dip;		/* disk inode */
	int			first;		/* first byte number */
	int			ioffset;	/* off in bytes */
	int			last;		/* last byte number */
	xfs_mount_t		*mp;		/* mount point structure */
	static const int	offsets[] = {	/* field offsets */
						/* keep in sync with bits */
		offsetof(xfs_dinode_core_t, di_magic),
		offsetof(xfs_dinode_core_t, di_mode),
		offsetof(xfs_dinode_core_t, di_version),
		offsetof(xfs_dinode_core_t, di_format),
		offsetof(xfs_dinode_core_t, di_nlink),
		offsetof(xfs_dinode_core_t, di_uid),
		offsetof(xfs_dinode_core_t, di_gid),
		offsetof(xfs_dinode_core_t, di_uuid),
		offsetof(xfs_dinode_core_t, di_size),
		offsetof(xfs_dinode_core_t, di_nextents),
		offsetof(xfs_dinode_core_t, di_atime),
		offsetof(xfs_dinode_core_t, di_mtime),
		offsetof(xfs_dinode_core_t, di_ctime),
		offsetof(xfs_dinode_core_t, di_gen),
		offsetof(xfs_dinode_core_t, di_extsize),
		offsetof(xfs_dinode_core_t, di_flags),
		offsetof(xfs_dinode_core_t, di_nblocks),
		offsetof(xfs_dinode_t, di_next_unlinked),
		offsetof(xfs_dinode_t, di_u),
		sizeof(xfs_dinode_t)
	};

	ASSERT(offsetof(xfs_dinode_t, di_core) == 0);
	mp = tp->t_mountp;
	/*
	 * Get the inode-relative first and last bytes for these fields
	 */
	xfs_btree_offsets(fields, offsets, XFS_DI_NUM_BITS, &first, &last);
	/*
	 * Convert to buffer offsets and log it.
	 */
	dip = XFS_MAKE_IPTR(mp, bp, off);
	ioffset = (caddr_t)dip - (caddr_t)XFS_BUF_TO_DINODE(bp);
	first += ioffset;
	last += ioffset;
	xfs_trans_log_buf(tp, bp, first, last);
}

/*
 * Read in the allocation group header (inode allocation section)
 */
STATIC buf_t *				/* allocation group hdr buf */
xfs_ialloc_read_agi(
	xfs_mount_t	*mp,		/* file system mount structure */
	xfs_trans_t	*tp,		/* transaction pointer */
	xfs_agnumber_t	agno)		/* allocation group number */
{
	xfs_agi_t	*agi;		/* allocation group header */
	buf_t		*bp;		/* return value */
	daddr_t		d;		/* disk block address */
	xfs_perag_t	*pag;		/* per allocation group data */

	ASSERT(agno != NULLAGNUMBER);
	d = XFS_AG_DADDR(mp, agno, XFS_AGI_DADDR);
	bp = xfs_trans_read_buf(tp, mp->m_dev, d, 1, 0);
	ASSERT(bp && !geterror(bp));
	pag = &mp->m_perag[agno];
	agi = XFS_BUF_TO_AGI(bp);
	if (!pag->pagi_init) {
		pag->pagi_freecount = agi->agi_freecount;
		pag->pagi_init = 1;
	} else
		ASSERT(pag->pagi_freecount == agi->agi_freecount);
	return bp;
}

/*
 * Allocation group level functions.
 */

/*
 * Allocate new inodes in the allocation group specified by agbp.
 * Return 0 for failure, 1 for success.
 */
STATIC int				/* success/failure */
xfs_ialloc_ag_alloc(
	xfs_trans_t	*tp,		/* transaction pointer */
	buf_t		*agbp)		/* alloc group buffer */
{
	xfs_agi_t	*agi;		/* allocation group header */
	xfs_alloc_arg_t	*args;		/* allocation argument structure */
	xfs_btree_cur_t	*cur;		/* inode btree cursor */
	buf_t		*fbuf;		/* new free inodes' buffer */
	xfs_dinode_t	*free;		/* new free inode structure */
	int		i;		/* inode counter */
	int		j;		/* block counter */
	xfs_agino_t	newino;		/* new first inode's number */
	xfs_agino_t	newlen;		/* new number of inodes */
	xfs_agino_t	thisino;	/* current inode number, for loop */
	static xfs_timestamp_t ztime;	/* zero xfs timestamp */
	static uuid_t	zuuid;		/* zero uuid */

	args = xfs_alloc_arg_alloc();
	args->tp = tp;
	args->mp = tp->t_mountp;
	agi = XFS_BUF_TO_AGI(agbp);
	/*
	 * Locking will ensure that we don't have two callers in here
	 * at one time.
	 */
	newlen = XFS_IALLOC_INODES(args->mp);
	args->minlen = args->maxlen = XFS_IALLOC_BLOCKS(args->mp);
	/*
	 * Need to figure out where to allocate the inode blocks.
	 * Ideally they should be spaced out through the a.g.
	 * For now, just allocate blocks up front.
	 */
	args->agbno = agi->agi_root;
	args->fsbno = XFS_AGB_TO_FSB(args->mp, agi->agi_seqno, args->agbno);
	/*
	 * Allocate a fixed-size extent of inodes.
	 */
	args->type = XFS_ALLOCTYPE_NEAR_BNO;
	args->mod = args->minleft = args->total = args->wasdel = args->isfl =
		args->userdata = 0;
	args->prod = 1;
	xfs_alloc_vextent(args);
	if (args->fsbno == NULLFSBLOCK) {
		xfs_alloc_arg_free(args);
		return 0;
	}
	ASSERT(args->len == args->minlen);
	/*
	 * Convert the results.
	 */
	newino = XFS_OFFBNO_TO_AGINO(args->mp, args->agbno, 0);
	/*
	 * Loop over the new block(s), filling in the inodes.
	 * Run both loops backwards, so that the inodes are linked together
	 * forwards, in the natural order.
	 */
	for (j = (int)args->len - 1; j >= 0; j--) {
		/*
		 * Get the block.
		 */
		fbuf = xfs_btree_get_bufs(args->mp, tp, agi->agi_seqno,
					  args->agbno + j, 0);
		/*
		 * Loop over the inodes in this buffer.
		 */
		for (i = args->mp->m_sb.sb_inopblock - 1; i >= 0; i--) {
			thisino =
				XFS_OFFBNO_TO_AGINO(args->mp, args->agbno + j, i);
			free = XFS_MAKE_IPTR(args->mp, fbuf, i);
			free->di_core.di_magic = XFS_DINODE_MAGIC;
			free->di_core.di_mode = 0;
			free->di_core.di_version = XFS_DINODE_VERSION;
			free->di_core.di_format = 0;
			free->di_core.di_nlink = 0;
			free->di_core.di_uid = 0;
			free->di_core.di_gid = 0;
			free->di_core.di_nextents = 0;
			free->di_core.di_uuid = zuuid;
			free->di_core.di_size = 0;
			free->di_core.di_atime = ztime;
			free->di_core.di_mtime = ztime;
			free->di_core.di_ctime = ztime;
			free->di_core.di_gen = 0;
			free->di_core.di_extsize = 0;
			free->di_core.di_flags = 0;
			free->di_core.di_nblocks = 0;
			free->di_next_unlinked = NULLAGINO;
			xfs_ialloc_log_di(tp, fbuf, i,
					  (XFS_DI_CORE_BITS |
					   XFS_DI_NEXT_UNLINKED));
			agi->agi_count++;
			agi->agi_freecount++;
		}
	}
	args->mp->m_perag[agi->agi_seqno].pagi_freecount += newlen;
	ASSERT(thisino == newino);
	agi->agi_newino = newino;
	/*
	 * Insert a record describing the new inode chunk into the btree.
	 */
	cur = xfs_btree_init_cursor(args->mp, tp, agbp, agi->agi_seqno,
		XFS_BTNUM_INO, (xfs_inode_t *)0);
	xfs_alloc_arg_free(args);
	i = xfs_inobt_lookup_eq(cur, newino, newlen, XFS_INOBT_ALL_FREE);
	ASSERT(i == 0);
	i = xfs_inobt_insert(cur);
	ASSERT(i == 1);
	xfs_btree_del_cursor(cur);
	/*
	 * Log allocation group header fields
	 */
	xfs_ialloc_log_agi(tp, agbp,
		XFS_AGI_COUNT | XFS_AGI_FREECOUNT | XFS_AGI_NEWINO);
	/*
	 * Modify/log superblock values for inode count and inode free count.
	 */
	xfs_trans_mod_sb(tp, XFS_TRANS_SB_ICOUNT, newlen);
	xfs_trans_mod_sb(tp, XFS_TRANS_SB_IFREE, newlen);
	return 1;
}

/*
 * Select an allocation group to look for a free inode in, based on the parent
 * inode and then mode.  Return the allocation group buffer.
 */
STATIC buf_t *				/* allocation group buffer */
xfs_ialloc_ag_select(
	xfs_trans_t	*tp,		/* transaction pointer */
	xfs_ino_t	parent,		/* parent directory inode number */
	mode_t		mode)		/* bits set to indicate file type */
{
	buf_t		*agbp;		/* allocation group header buffer */
	xfs_agnumber_t	agcount;	/* number of ag's in the filesystem */
	xfs_agnumber_t	agno;		/* current ag number */
	xfs_agi_t	*agi;		/* allocation group header */
	int		flags;		/* alloc buffer locking flags */
	xfs_extlen_t	ineed;		/* blocks needed for inode allocation */
	xfs_extlen_t	longest;	/* longest extent available */
	xfs_mount_t	*mp;		/* mount point structure */
	int		needspace;	/* file mode implies space allocated */
	xfs_perag_t	*pag;		/* per allocation group data */
	xfs_agnumber_t	pagno;		/* parent (starting) ag number */

	/*
	 * Files of these types need at least one block if length > 0
	 * (and they won't fit in the inode, but that's hard to figure out).
	 */
	needspace = S_ISDIR(mode) || S_ISREG(mode) || S_ISLNK(mode);
	mp = tp->t_mountp;
	if (S_ISDIR(mode))
		pagno = mp->m_agirotor;
	else
		pagno = XFS_INO_TO_AGNO(mp, parent);
	agcount = mp->m_sb.sb_agcount;
	ASSERT(pagno < agcount);
	/*
	 * Loop through allocation groups, looking for one with a little
	 * free space in it.  Note we don't look for free inodes, exactly.
	 * Instead, we include whether there is a need to allocate inodes
	 * to mean that blocks must be allocated for them, 
	 * if none are currently free.
	 */
	agno = pagno;
	flags = XFS_ALLOC_FLAG_TRYLOCK;
	while (1) {
		pag = &mp->m_perag[agno];
		if (!pag->pagi_init)
			agbp = xfs_ialloc_read_agi(mp, tp, agno);
		else
			agbp = NULL;
		if (!pag->pagf_init) {
			if (agbp == NULL)
				agbp = xfs_ialloc_read_agi(mp, tp, agno);
			xfs_alloc_pagf_init(mp, tp, agno, flags);
		}
		/*
		 * Is there enough free space for the file plus a block
		 * of inodes (if we need to allocate some)?
		 */
		ineed = pag->pagi_freecount ? 0 : XFS_IALLOC_BLOCKS(mp);
		if (!(longest = pag->pagf_longest))
			longest = pag->pagf_flcount > 0;
		if (pag->pagf_init &&
		    pag->pagf_freeblks >= needspace + ineed &&
		    longest >= ineed) {
			if (agbp == NULL)
				agbp = xfs_ialloc_read_agi(mp, tp, agno);
			if (S_ISDIR(mode))
				mp->m_agirotor =
					agno + 1 == agcount ? 0 : agno + 1;
			return agbp;
		}
		if (agbp)
			xfs_trans_brelse(tp, agbp);
		agno++;
		if (agno == agcount)
			agno = 0;
		if (agno == pagno)
			if (flags)
				flags = 0;
			else
				return (buf_t *)0;
	}
}

/* 
 * Visible inode allocation functions.
 */

/*
 * Allocate an inode on disk.
 * Mode is used to tell whether the new inode will need space, and whether
 * it is a directory.
 *
 * The arguments IO_agbp and alloc_done are defined to work within
 * the constraint of one allocation per transaction.
 * xfs_dialloc() is designed to be called twice if it has to do an
 * allocation to make more free inodes.  On the first call,
 * IO_agbp should be set to NULL. If an inode is available,
 * i.e., xfs_dialloc() did not need to do an allocation, an inode
 * number is returned.  In this case, IO_agbp would be set to the 
 * current ag_buf and alloc_done set to false.
 * If an allocation needed to be done, xfs_dialloc would return
 * the current ag_buf in IO_agbp and set alloc_done to true.
 * The caller should then commit the current transaction, allocate a new
 * transaction, and call xfs_dialloc() again, passing in the previous
 * value of IO_agbp.  IO_agbp should be held across the transactions.
 * Since the agbp is locked across the two calls, the second call is
 * guaranteed to have a free inode available.
 *
 * Once we successfully pick an inode its number is returned and the
 * on-disk data structures are updated.  The inode itself is not read
 * in, since doing so would break ordering constraints with xfs_reclaim.
 */
xfs_ino_t				/* inode number allocated */
xfs_dialloc(
	xfs_trans_t	*tp,		/* transaction pointer */
	xfs_ino_t	parent,		/* parent inode (directory) */
	mode_t		mode,		/* mode bits for new inode */
	buf_t		**IO_agbp,	/* in/out ag header's buffer */
	boolean_t	*alloc_done)	/* true if we needed to replenish
					   inode freelist */
{
	xfs_agnumber_t	agcount;	/* number of allocation groups */
	buf_t		*agbp;		/* allocation group header's buffer */
	xfs_agino_t	agino;		/* ag-relative inode to be returned */
	xfs_agnumber_t	agno;		/* allocation group number */
	xfs_agi_t	*agi;		/* allocation group header structure */
	xfs_btree_cur_t	*cur;		/* inode allocation btree cursor */
	int		flags;		/* flags for logging agi */
	int		i;		/* result code */
	xfs_ino_t	ino;		/* fs-relative inode to be returned */
	int		j;		/* result code */
	xfs_mount_t	*mp;		/* file system mount structure */
	int		offset;		/* index of inode in chunk */
	xfs_agino_t	pagino;		/* parent's a.g. relative inode # */
	xfs_agnumber_t	pagno;		/* parent's allocation group number */
	xfs_inobt_rec_t	rec;		/* inode allocation record */
	xfs_agnumber_t	tagno;		/* testing allocation group number */
	xfs_btree_cur_t	*tcur;		/* temp cursor */
	xfs_inobt_rec_t	trec;		/* temp inode allocation record */

	if (*IO_agbp == NULL) {
		/*
		 * We do not have an agbp, so select an initial allocation
		 * group for inode allocation.
		 */
		agbp = xfs_ialloc_ag_select(tp, parent, mode);
		/*
		 * Couldn't find an allocation group satisfying the 
		 * criteria, give up.
		 */
		if (!agbp)
			return NULLFSINO;
		agi = XFS_BUF_TO_AGI(agbp);
		ASSERT(agi->agi_magicnum == XFS_AGI_MAGIC);
	} else {
		/*
		 * Continue where we left off before.  In this case, we 
		 * know that the allocation group has free inodes.
		 */
		agbp = *IO_agbp;
		agi = XFS_BUF_TO_AGI(agbp);
		ASSERT(agi->agi_magicnum == XFS_AGI_MAGIC);
		ASSERT(agi->agi_freecount > 0);
	}
	mp = tp->t_mountp;
	agcount = mp->m_sb.sb_agcount;
	agno = agi->agi_seqno;
	tagno = agno;
	pagno = XFS_INO_TO_AGNO(mp, parent);
	pagino = XFS_INO_TO_AGINO(mp, parent);
	/*
	 * Loop until we find an allocation group that either has free inodes
	 * or in which we can allocate some inodes.  Iterate through the
	 * allocation groups upward, wrapping at the end.
	 */
	*alloc_done = B_FALSE;
	while (agi->agi_freecount == 0) {
		/*
		 * Try to allocate some new inodes in the allocation group.
		 */
		if (xfs_ialloc_ag_alloc(tp, agbp)) {
			/*
			 * We successfully allocated some inodes, return
			 * the current context to the caller so that it
			 * can commit the current transaction and call
			 * us again where we left off.
			 */
			ASSERT(agi->agi_freecount > 0);
			*alloc_done = B_TRUE;
			*IO_agbp = agbp;
			return 0;
		}
		/*
		 * If it failed, give up on this ag.
		 */
		xfs_trans_brelse(tp, agbp);
		/*
		 * Go on to the next ag: get its ag header.
		 */
		if (++tagno == agcount)
			tagno = 0;
		if (tagno == agno)
			return NULLFSINO;
		agbp = xfs_ialloc_read_agi(mp, tp, tagno);
		agi = XFS_BUF_TO_AGI(agbp);
		ASSERT(agi->agi_magicnum == XFS_AGI_MAGIC);
	}
	/*
	 * Here with an allocation group that has a free inode.
	 */
	*IO_agbp = NULL;
	cur = xfs_btree_init_cursor(mp, tp, agbp, agi->agi_seqno, XFS_BTNUM_INO,
		(xfs_inode_t *)0);
	/*
	 * If pagino is 0 (this is the root inode allocation) use newino.
	 * This must work because we've just allocated some.
	 */
	if (!pagino)
		pagino = agi->agi_newino;
	/*
	 * If in the same a.g. as the parent, try to get near the parent.
	 */
	if (pagno == agno &&
	    (i = xfs_inobt_lookup_le(cur, pagino, 0, 0)) &&
	    (j = xfs_inobt_get_rec(cur, &rec.ir_startino, &rec.ir_freecount,
			&rec.ir_free)) &&
	    rec.ir_freecount > 0) {
		/*
		 * Found a free inode in the same chunk as parent, done.
		 */
	}
	/*
	 * In the same a.g. as parent, but parent's chunk is full.
	 */
	else if (pagno == agno) {
		int	doneleft;	/* done, searching to the left */
		int	doneright;	/* done, searching to the right */

		ASSERT(i == 1);
		ASSERT(j == 1);
		/*
		 * Duplicate the cursor, search left & right simultaneously.
		 */
		tcur = xfs_btree_dup_cursor(cur);
		/*
		 * Search left with tcur, back up 1 record.
		 */
		doneleft = !xfs_inobt_decrement(tcur, 0);
		if (!doneleft) {
			i = xfs_inobt_get_rec(tcur, &trec.ir_startino,
				&trec.ir_freecount, &trec.ir_free);
			ASSERT(i);
		}
		/* 
		 * Search right with cur, go forward 1 record.
		 */
		doneright = !xfs_inobt_increment(cur, 0);
		if (!doneright) {
			i = xfs_inobt_get_rec(cur, &rec.ir_startino,
				&rec.ir_freecount, &rec.ir_free);
			ASSERT(i);
		}
		/*
		 * Loop until we find the closest inode chunk with a free one.
		 */
		while (!doneleft || !doneright) {
			int	useleft;  /* using left inode chunk this time */

			/*
			 * Figure out which block is closer, if both are valid.
			 */
			if (!doneleft && !doneright)
				useleft =
					pagino -
					    (trec.ir_startino +
					     XFS_INODES_PER_CHUNK - 1) <
					rec.ir_startino - pagino;
			else
				useleft = !doneleft;
			/*
			 * If checking the left, does it have free inodes?
			 */
			if (useleft && trec.ir_freecount) {
				/*
				 * Yes, set it up as the chunk to use.
				 */
				rec = trec;
				xfs_btree_del_cursor(cur);
				cur = tcur;
				break;
			}
			/*
			 * If checking the right, does it have free inodes?
			 */
			if (!useleft && rec.ir_freecount) {
				/*
				 * Yes, it's already set up.
				 */
				xfs_btree_del_cursor(tcur);
				break;
			}
			/*
			 * If used the left, get another one further left.
			 */
			if (useleft) {
				doneleft = !xfs_inobt_decrement(tcur, 0);
				if (!doneleft) {
					i = xfs_inobt_get_rec(tcur,
						&trec.ir_startino,
						&trec.ir_freecount,
						&trec.ir_free);
					ASSERT(i);
				}
			}
			/*
			 * If used the right, get another one further right.
			 */
			else {
				doneright = !xfs_inobt_increment(cur, 0);
				if (!doneright) {
					i = xfs_inobt_get_rec(cur,
						&rec.ir_startino,
						&rec.ir_freecount,
						&rec.ir_free);
					ASSERT(i);
				}
			}
		}
		ASSERT(!doneleft || !doneright);
	}
	/*
	 * In a different a.g. from the parent.
	 * See if the most recently allocated block has any free.
	 */
	else if (agi->agi_newino != NULLAGINO &&
		   xfs_inobt_lookup_eq(cur, agi->agi_newino, 0, 0) &&
		   xfs_inobt_get_rec(cur, &rec.ir_startino, &rec.ir_freecount,
			   &rec.ir_free) &&
		   rec.ir_freecount > 0) {
		/*
		 * The last chunk allocated in the group still has a free inode.
		 */
	}
	/*
	 * None left in the last group, search the whole a.g.
	 */
	else {
		i = xfs_inobt_lookup_ge(cur, 0, 0, 0);
		ASSERT(i == 1);
		while (1) {
			i = xfs_inobt_get_rec(cur, &rec.ir_startino,
				&rec.ir_freecount, &rec.ir_free);
			ASSERT(i == 1);
			if (rec.ir_freecount > 0)
				break;
			i = xfs_inobt_increment(cur, 0);
			ASSERT(i == 1);
		}
	}
	offset = XFS_IALLOC_FIND_FREE(&rec.ir_free);
	ASSERT(offset >= 0);
	ASSERT(XFS_AGINO_TO_OFFSET(mp, rec.ir_startino) == 0);
	ino = XFS_AGINO_TO_INO(mp, tagno, rec.ir_startino + offset);
	XFS_INOBT_CLR_FREE(&rec, offset);
	rec.ir_freecount--;
	xfs_inobt_update(cur, rec.ir_startino, rec.ir_freecount, rec.ir_free);
	xfs_btree_del_cursor(cur);
	agi->agi_freecount--;
	mp->m_perag[tagno].pagi_freecount--;
	xfs_ialloc_log_agi(tp, agbp, XFS_AGI_FREECOUNT);
	xfs_trans_mod_sb(tp, XFS_TRANS_SB_IFREE, -1);
	return ino;
}

/*
 * Free disk inode.  Carefully avoids touching the incore inode, all
 * manipulations incore are the caller's responsibility.
 * The on-disk inode is not changed by this operation, only the
 * btree (free inode mask) is changed.
 */
void
xfs_difree(
	xfs_trans_t	*tp,		/* transaction pointer */
	xfs_ino_t	inode)		/* inode to be freed */
{
	xfs_agblock_t	agbno;	/* block number containing inode */
	buf_t		*agbp;	/* buffer containing allocation group header */
	xfs_agino_t	agino;	/* inode number relative to allocation group */
	xfs_agnumber_t	agno;	/* allocation group number */
	xfs_agi_t	*agi;	/* allocation group header */
	xfs_btree_cur_t	*cur;	/* inode btree cursor */
	int		i;	/* result code */
	xfs_mount_t	*mp;	/* mount structure for filesystem */
	int		off;	/* offset of inode in inode chunk */
	xfs_inobt_rec_t	rec;	/* btree record */

	mp = tp->t_mountp;
	/*
	 * Break up inode number into its components.
	 */
	agno = XFS_INO_TO_AGNO(mp, inode);
	ASSERT(agno < mp->m_sb.sb_agcount);
	agino = XFS_INO_TO_AGINO(mp, inode);
	ASSERT(agino != NULLAGINO);
	agbno = XFS_AGINO_TO_AGBNO(mp, agino);
	/*
	 * Get the allocation group header.
	 */
	agbp = xfs_ialloc_read_agi(mp, tp, agno);
	agi = XFS_BUF_TO_AGI(agbp);
	ASSERT(agi->agi_magicnum == XFS_AGI_MAGIC);
	ASSERT(agbno < agi->agi_length);
	/*
	 * Initialize the cursor.
	 */
	cur = xfs_btree_init_cursor(mp, tp, agbp, agno, XFS_BTNUM_INO,
		(xfs_inode_t *)0);
	/*
	 * Look for the entry describing this inode.
	 */
	i = xfs_inobt_lookup_le(cur, agino, 0, 0);
	ASSERT(i == 1);
	i = xfs_inobt_get_rec(cur, &rec.ir_startino, &rec.ir_freecount,
		&rec.ir_free);
	ASSERT(i == 1);
	/*
	 * Get the offset in the inode chunk.
	 */
	off = agino - rec.ir_startino;
	ASSERT(off >= 0 && off < XFS_INODES_PER_CHUNK);
	ASSERT(!XFS_INOBT_IS_FREE(&rec, off));
	/*
	 * Mark the inode free & increment the count.
	 */
	XFS_INOBT_SET_FREE(&rec, off);
	rec.ir_freecount++;
	i = xfs_inobt_update(cur, rec.ir_startino, rec.ir_freecount,
		rec.ir_free);
	ASSERT(i == 1);
	xfs_btree_del_cursor(cur);
	/*
	 * Change the inode free counts and log the ag/sb changes.
	 */
	agi->agi_freecount++;
	mp->m_perag[agno].pagi_freecount++;
	xfs_ialloc_log_agi(tp, agbp, XFS_AGI_FREECOUNT);
	xfs_trans_mod_sb(tp, XFS_TRANS_SB_IFREE, 1);
}

/*
 * Return the location of the inode in bno/off, for mapping it into a buffer.
 */
void
xfs_dilocate(
	xfs_mount_t	*mp,	/* file system mount structure */
	xfs_trans_t	*tp,	/* transaction pointer */
	xfs_ino_t	ino,	/* inode to locate */
	xfs_fsblock_t	*bno,	/* output: block containing inode */
	int		*off)	/* output: index in block of inode */
{
	xfs_agblock_t	agbno;	/* block number of inode in the alloc group */
	xfs_agnumber_t	agno;	/* allocation group number */
	int		offset;	/* index of inode in its buffer */

	ASSERT(ino != NULLFSINO);
	/*
	 * Split up the inode number into its parts.
	 */
	agno = XFS_INO_TO_AGNO(mp, ino);
	ASSERT(agno < mp->m_sb.sb_agcount);
	agbno = XFS_INO_TO_AGBNO(mp, ino);
	ASSERT(agbno < mp->m_sb.sb_agblocks);
	offset = XFS_INO_TO_OFFSET(mp, ino);
	ASSERT(offset < mp->m_sb.sb_inopblock);
	/*
	 * Store the results in the output parameters.
	 */
	*bno = XFS_AGB_TO_FSB(mp, agno, agbno);
	*off = offset;
}

/*
 * Compute and fill in value of m_in_maxlevels.
 */
void
xfs_ialloc_compute_maxlevels(
	xfs_mount_t	*mp)		/* file system mount structure */
{
	int		level;
	uint		maxblocks;
	uint		maxleafents;
	int		minleafrecs;
	int		minnoderecs;

	maxleafents = (1LL << XFS_INO_AGINO_BITS(mp)) / XFS_INODES_PER_CHUNK;
	minleafrecs = mp->m_alloc_mnr[0];
	minnoderecs = mp->m_alloc_mnr[1];
	maxblocks = (maxleafents + minleafrecs - 1) / minleafrecs;
	for (level = 1; maxblocks > 1; level++)
		maxblocks = (maxblocks + minnoderecs - 1) / minnoderecs;
	mp->m_in_maxlevels = level;
}

/*
 * Log specified fields for the ag hdr (inode section)
 */
void
xfs_ialloc_log_agi(
	xfs_trans_t	*tp,		/* transaction pointer */
	buf_t		*bp,		/* allocation group header buffer */
	int		fields)		/* bitmask of fields to log */
{
	int			first;		/* first byte number */
	int			last;		/* last byte number */
	static const int	offsets[] = {	/* field starting offsets */
					/* keep in sync with bit definitions */
		offsetof(xfs_agi_t, agi_magicnum),
		offsetof(xfs_agi_t, agi_versionnum),
		offsetof(xfs_agi_t, agi_seqno),
		offsetof(xfs_agi_t, agi_length),
		offsetof(xfs_agi_t, agi_count),
		offsetof(xfs_agi_t, agi_root),
		offsetof(xfs_agi_t, agi_level),
		offsetof(xfs_agi_t, agi_freecount),
		offsetof(xfs_agi_t, agi_newino),
		offsetof(xfs_agi_t, agi_unlinked),
		sizeof(xfs_agi_t)
	};
#ifdef DEBUG
	xfs_agi_t		*agi;	/* allocation group header */

	agi = XFS_BUF_TO_AGI(bp);
	ASSERT(agi->agi_magicnum == XFS_AGI_MAGIC);
#endif
	/*
	 * Compute byte offsets for the first and last fields.
	 */
	xfs_btree_offsets(fields, offsets, XFS_AGI_NUM_BITS, &first, &last);
	/*
	 * Log the allocation group inode header buffer.
	 */
	xfs_trans_log_buf(tp, bp, first, last);
}
