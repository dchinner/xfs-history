#ident	"$Revision: 1.40 $"

#include <sys/param.h>
#include <sys/stat.h>
#include <sys/vnode.h>
#include <sys/uuid.h>
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
#include "xfs_ialloc.h"
#include "xfs_bmap_btree.h"
#include "xfs_btree.h"
#include "xfs_dinode.h"
#include "xfs_inode_item.h"
#include "xfs_inode.h"
#include "xfs_alloc.h"
#ifdef SIM
#include "sim.h"
#endif

/*
 * Prototypes for internal routines.
 */

/*
 * Log specified fields for the ag hdr (inode section)
 */
STATIC void
xfs_ialloc_log_agi(
	xfs_trans_t	*tp,		/* transaction pointer */
	buf_t		*bp,		/* allocation group header buffer */
	int		fields);	/* bitmask of fields to log */

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
 * inode and then mode.  sameag==1 forces the allocation to the same group
 * as the parent.  Return the allocation group buffer.
 */
STATIC buf_t *				/* allocation group buffer */
xfs_ialloc_ag_select(
	xfs_trans_t	*tp,		/* transaction pointer */
	xfs_ino_t	parent,		/* parent directory inode number */
	int		sameag,		/* =1 to force to same ag. as parent */
	mode_t		mode);		/* bits set to indicate file type */

/*
 * Internal functions.
 */

/*
 * Log specified fields for the ag hdr (inode section)
 */
STATIC void
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
		offsetof(xfs_agi_t, agi_first),
		offsetof(xfs_agi_t, agi_last),
		offsetof(xfs_agi_t, agi_freelist),
		offsetof(xfs_agi_t, agi_freecount),
		sizeof(xfs_agi_t)
	};

	/*
	 * Compute byte offsets for the first and last fields.
	 */
	xfs_btree_offsets(fields, offsets, XFS_AGI_NUM_BITS, &first, &last);
	/*
	 * Log the allocation group inode header buffer.
	 */
	xfs_trans_log_buf(tp, bp, first, last);
}

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
		offsetof(xfs_dinode_core_t, di_nextents),
		offsetof(xfs_dinode_core_t, di_uuid),
		offsetof(xfs_dinode_core_t, di_size),
		offsetof(xfs_dinode_core_t, di_atime),
		offsetof(xfs_dinode_core_t, di_mtime),
		offsetof(xfs_dinode_core_t, di_ctime),
		offsetof(xfs_dinode_core_t, di_gen),
		offsetof(xfs_dinode_core_t, di_extsize),
		offsetof(xfs_dinode_core_t, di_flags),
		offsetof(xfs_dinode_core_t, di_nexti),
		offsetof(xfs_dinode_core_t, di_nblocks),
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
	buf_t		*bp;		/* return value */
	daddr_t		d;		/* disk block address */

	ASSERT(agno != NULLAGNUMBER);
	d = XFS_AG_DADDR(mp, agno, XFS_AGI_DADDR);
	bp = xfs_trans_read_buf(tp, mp->m_dev, d, 1, 0);
	ASSERT(bp && !geterror(bp));
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
	buf_t		*fbuf;		/* new free inodes' buffer */
	int		flag;		/* logging flag bits */
	xfs_dinode_t	*free;		/* new free inode structure */
	int		i;		/* inode counter */
	int		j;		/* block counter */
	xfs_extlen_t	maxnewblocks;	/* largest number of blocks to alloc */
	xfs_extlen_t	minnewblocks;	/* smallest number of blocks to alloc */
	xfs_mount_t	*mp;		/* mount point structure */
	xfs_extlen_t	newblocks;	/* actual number of blocks allocated */
	xfs_agblock_t	newbno;		/* new inodes starting block number */
	xfs_fsblock_t	newfsbno;	/* long form of newbno */
	xfs_agino_t	newino;		/* new first inode's number */
	xfs_agino_t	newlen;		/* new number of inodes */
	xfs_agino_t	thisino;	/* current inode number, for loop */
	static xfs_timestamp_t ztime;	/* zero xfs timestamp */
	static uuid_t	zuuid;		/* zero uuid */

	mp = tp->t_mountp;
	agi = XFS_BUF_TO_AGI(agbp);
	/*
	 * Locking will ensure that we don't have two callers in here
	 * at one time.
	 */
	ASSERT(agi->agi_freelist == NULLAGINO);
	ASSERT(agi->agi_freecount == 0);
	/*
	 * Calculate the range of number of blocks to be allocated
	 */
	minnewblocks = XFS_IALLOC_MIN_ALLOC(mp, agi);
	maxnewblocks = XFS_IALLOC_MAX_ALLOC(mp, agi);
	newlen = minnewblocks << mp->m_sb.sb_inopblog;
	/*
	 * Figure out a good first block number to ask for.  These inodes 
	 * should follow the previous inodes, to get some locality.
	 */
	newbno = (agi->agi_last == NULLAGINO) ?
		XFS_AGI_BLOCK(mp) :
		(XFS_AGINO_TO_AGBNO(mp, agi->agi_last) + 1);
	newfsbno = XFS_AGB_TO_FSB(mp, agi->agi_seqno, newbno);
	/*
	 * Allocate a variable-sized extent.
	 */
	for (;;) {
		newfsbno = xfs_alloc_vextent(tp, newfsbno, minnewblocks,
			maxnewblocks, &newblocks, XFS_ALLOCTYPE_NEAR_BNO,
			maxnewblocks, 0, 0, 1);
		if (newfsbno != NULLFSBLOCK)
			break;
		maxnewblocks >>= 1;
		/*
		 * Should this assert?  We should be guaranteed that the 
		 * minimum length allocation will work, by the select call.
		 */
		if (maxnewblocks < minnewblocks)
			return 0;
	}

	/*
	 * Convert the results.
	 */
	newlen = newblocks << mp->m_sb.sb_inopblog;
	newbno = XFS_FSB_TO_AGBNO(mp, newfsbno);
	newino = XFS_OFFBNO_TO_AGINO(mp, newbno, 0);
	/*
	 * Set logging flags for allocation group header.
	 */
	flag = XFS_AGI_COUNT | XFS_AGI_FREELIST |
	       XFS_AGI_FREECOUNT | XFS_AGI_FIRST;
	/*
	 * Loop over the new blocks, filling in the inodes.
	 * Run both loops backwards, so that the inodes are linked together
	 * forwards, in the natural order.
	 */
	for (j = (int)newblocks - 1; j >= 0; j--) {
		/*
		 * Get the block.
		 */
		fbuf = xfs_btree_get_bufs(mp, tp, agi->agi_seqno,
					  newbno + j, 0);
		/*
		 * Loop over the inodes in this buffer.
		 */
		for (i = mp->m_sb.sb_inopblock - 1; i >= 0; i--) {
			thisino = XFS_OFFBNO_TO_AGINO(mp, newbno + j, i);
			free = XFS_MAKE_IPTR(mp, fbuf, i);
			free->di_core.di_magic = XFS_DINODE_MAGIC;
			free->di_core.di_mode = 0;
			free->di_core.di_version = XFS_DINODE_VERSION;
			free->di_core.di_format = XFS_DINODE_FMT_AGINO;
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
			free->di_core.di_nexti = agi->agi_first;
			free->di_core.di_nblocks = 0;
			free->di_u.di_next = agi->agi_freelist;
			xfs_ialloc_log_di(tp, fbuf, i, XFS_DI_ALL_BITS);
			if (agi->agi_last == NULLAGINO) {
				agi->agi_last = thisino;
				flag |= XFS_AGI_LAST;
			}
			agi->agi_freelist = thisino;
			agi->agi_first = thisino;
			agi->agi_count++;
			agi->agi_freecount++;
		}
	}
	ASSERT(thisino == newino);
	/*
	 * Log allocation group header fields
	 */
	xfs_ialloc_log_agi(tp, agbp, flag);
	/*
	 * Modify/log superblock values for inode count and inode free count.
	 */
	xfs_trans_mod_sb(tp, XFS_TRANS_SB_ICOUNT, newlen);
	xfs_trans_mod_sb(tp, XFS_TRANS_SB_IFREE, newlen);
	return 1;
}

/*
 * Select an allocation group to look for a free inode in, based on the parent
 * inode and then mode.  sameag==1 forces the allocation to the same group
 * as the parent.  Return the allocation group buffer.
 */
STATIC buf_t *				/* allocation group buffer */
xfs_ialloc_ag_select(
	xfs_trans_t	*tp,		/* transaction pointer */
	xfs_ino_t	parent,		/* parent directory inode number */
	int		sameag,		/* =1 to force to same ag. as parent */
	mode_t		mode)		/* bits set to indicate file type */
{
	buf_t		*agbp;		/* allocation group header buffer */
	xfs_agnumber_t	agcount;	/* number of ag's in the filesystem */
	xfs_agnumber_t	agno;		/* current ag number */
	int		agoff;		/* ag number relative to parent's */
	xfs_agi_t	*agi;		/* allocation group header */
	int		doneleft;	/* done searching lower numbered ag's */
	int		doneright;	/* done "" higher numbered ag's */
	int		flags;		/* alloc buffer locking flags */
	xfs_mount_t	*mp;		/* mount point structure */
	int		needspace;	/* file mode implies space allocated */
	xfs_agnumber_t	pagno;		/* parent ag number */

	/*
	 * Files of these types need at least one block if length > 0.
	 */
	needspace = S_ISDIR(mode) || S_ISREG(mode) || S_ISLNK(mode);
	mp = tp->t_mountp;
	pagno = XFS_INO_TO_AGNO(mp, parent);
	agcount = mp->m_sb.sb_agcount;
	ASSERT(pagno < agcount);
	/*
	 * Loop through allocation groups, looking for one with a little
	 * free space in it.  Note we don't look for free inodes, exactly.
	 * Instead, we include whether there is a need to allocate inodes
	 * to mean that at least one block must be allocated for them, 
	 * if none are currently free.
	 *
	 * We iterate to the left (lower numbered allocation groups) and
	 * to the right (higher numbered) of the parent allocation group,
	 * alternating left and right.
	 */
	for (agoff = S_ISDIR(mode) != 0 && !sameag, doneleft = doneright = 0,
	     flags = sameag ? 0 : XFS_ALLOC_FLAG_TRYLOCK;
	     !doneleft || !doneright;
	     agoff = -agoff + (agoff <= 0)) {
		/*
		 * Skip this if we're already done going in that direction.
		 */
		if ((agoff > 0 && doneright) || (agoff < 0 && doneleft))
			continue;
		/*
		 * If this one is off the end to the right, stop there.
		 */
		if (agoff >= 0 && pagno + agoff >= agcount) {
			doneright = 1;
			if (doneleft && flags) {
				flags = 0;
				agoff = S_ISDIR(mode) != 0 && !sameag;
				doneleft = doneright = 0;
			}
			continue;
		/*
		 * If this one is off the end to the left, stop there.
		 */
		} else if (agoff < 0 && pagno < -agoff) {
			doneleft = 1;
			if (doneright && flags) {
				flags = 0;
				agoff = S_ISDIR(mode) != 0 && !sameag;
				doneleft = doneright = 0;
			}
			continue;
		}
		/*
		 * Must be a valid allocation group.
		 */
		agno = pagno + agoff;
		agbp = xfs_ialloc_read_agi(mp, tp, agno);
		agi = XFS_BUF_TO_AGI(agbp);
		ASSERT((agi->agi_freecount == 0) == (agi->agi_freelist == NULLAGINO));
		/*
		 * Is there enough free space for the file plus a block
		 * of inodes (if we need to allocate some)?
		 */
		if (xfs_alloc_ag_freeblks(mp, tp, agno, flags) >=
		    needspace + (agi->agi_freecount == 0))
			return agbp;
		xfs_trans_brelse(tp, agbp);
		if (sameag)
			break;
	}
	return (buf_t *)0;
}

/* 
 * Visible inode allocation functions.
 */

/*
 * Allocate an inode on disk.
 * Mode is used to tell whether the new inode will need space, and whether
 * it is a directory.
 * The sameag flag is used by mkfs only, to force the root directory
 * inode into the first allocation group.
 *
 * The arguments IO_agbp and alloc_done are defined to work within
 * the constraint of one allocation per transaction,
 * xfs_dialloc() is designed to be called twice if it has to do an
 * allocation to replenish the inode freelist.  On the first call,
 * IO_agbp should be set to NULL. If the freelist is not empty,
 * i.e., xfs_dialloc_ino() did not need to do an allocation, an inode
 * number is returned.  In this case, IO_agbp would be set to the 
 * current ag_buf and alloc_done set to false.
 * If an allocation needed to be done, xfs_dialloc would return
 * the current ag_buf in IO_agbp and set alloc_done to true.
 * The caller should then commit the current transaction, allocate a new
 * transaction, and call xfs_dialloc_ino() again, passing in the previous
 * value of IO_agbp.  IO_agbp should be held across the transactions.
 * Since the agbp is locked across the two calls, the second call is
 * guaranteed to have something on the freelist.
 *
 * Once we successfully pick an inode its number is returned and the
 * IO_agbp parameter contains the ag_buf for the inode's ag.  The caller
 * should get its reference to the in-core inode/vnode for the given ino
 * and then call xfs_dialloc_finish() to finalize the allocation.  This
 * must be done to satisfy the ordering of the inode/vnode locking with
 * that of the on-disk inode's buffer.  Since xfs_reclaim() must be able
 * to flush a potentially free inode, it forces the order of inode/vnode
 * locking before inode buffer locking.
 */
xfs_ino_t				/* inode number allocated */
xfs_dialloc_ino(
	xfs_trans_t	*tp,		/* transaction pointer */
	xfs_ino_t	parent,		/* parent inode (directory) */
	int		sameag,		/* 1 => must be in same a.g. */
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

	xfs_ino_t	ino;		/* fs-relative inode to be returned */
	xfs_mount_t	*mp;		/* mount point structure */
	xfs_agnumber_t	tagno;		/* testing allocation group number */

	if (*IO_agbp == NULL) {

		/*
		 * We do not have an agbp, so select an initial allocation
		 * group for inode allocation.
		 */
		agbp = xfs_ialloc_ag_select(tp, parent, sameag, mode);
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
		 * know that the allocation group's freelist is nonempty.
		 */
		agbp = *IO_agbp;
		agi = XFS_BUF_TO_AGI(agbp);
		ASSERT(agi->agi_magicnum == XFS_AGI_MAGIC);
		ASSERT(agi->agi_freecount > 0);
		ASSERT(agi->agi_freelist != NULLAGINO);
	}
	mp = tp->t_mountp;
	agcount = mp->m_sb.sb_agcount;
	agno = agi->agi_seqno;
	tagno = agno;
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
			ASSERT(agi->agi_freecount > 0 &&
			       agi->agi_freelist != NULLAGINO);
			*alloc_done = B_TRUE;
			*IO_agbp = agbp;
			return 0;
		}

		/*
		 * If it failed, give up on this ag.
		 */
		xfs_trans_brelse(tp, agbp);
		/*
		 * Can't try any other ag's, so give up.
		 */
		if (sameag)
			return NULLFSINO;
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
	*IO_agbp = agbp;
	agino = agi->agi_freelist;
	ASSERT(agino != NULLAGINO && agino != NULLAGINO_ALLOC);
	ino = XFS_AGINO_TO_INO(mp, tagno, agino);
	return ino;
}


/*
 * Finalize the allocation of an on-disk inode.  We need the
 * inode number returned by a previous call to xfs_dialloc_ino()
 * and the agi buffer returned in that call as well.  Here we
 * actually remove the on-disk inode from the free list and modify
 * the count of free inodes in the agi.
 *
 * We didn't do this earlier as part of xfs_dialloc_ino(), because the
 * in-core inode must be obtained BEFORE the on-disk buffer.  This
 * ordering is forced by xfs_reclaim() needing to flush out a dirty
 * inode (thereby locking the buffer for its on-disk copy) after the
 * vnode's RECLAIM bit is set and the inode is locked.
 */
void
xfs_dialloc_finish(
	xfs_trans_t	*tp,		/* transaction structure */
	xfs_ino_t	ino,		/* ino chosen by xfs_dialloc_ino() */
	buf_t		*agbp)		/* agi buf from xfs_dialloc_ino() */
{	 
	xfs_agblock_t	agbno;		/* starting block number of inode */
	xfs_agi_t	*agi;		/* allocation group header */
	xfs_agnumber_t	agno;		/* allocation group number */
	buf_t		*fbuf;		/* buffer containing inode */
	xfs_dinode_t	*free;		/* pointer into fbuf for dinode */
	xfs_mount_t	*mp;		/* mount point structure */
       	int		off;		/* index of free in fbuf */

	mp = tp->t_mountp;
	agno = XFS_INO_TO_AGNO(mp, ino);
	agbno = XFS_INO_TO_AGBNO(mp, ino);
	off = XFS_INO_TO_OFFSET(mp, ino);
	agi = XFS_BUF_TO_AGI(agbp);
	ASSERT(agi->agi_magicnum == XFS_AGI_MAGIC);
	/*
	 * Get a buffer containing the free inode.
	 */
	fbuf = xfs_btree_read_bufs(mp, tp, agno, agbno, 0);
	free = XFS_MAKE_IPTR(mp, fbuf, off);
	ASSERT(free->di_core.di_magic == XFS_DINODE_MAGIC);
	ASSERT(free->di_core.di_mode == 0);
	ASSERT(free->di_core.di_format == XFS_DINODE_FMT_AGINO);
	/*
	 * Remove the inode from the freelist, and decrement the counts.
	 */
	ASSERT((free->di_u.di_next == NULLAGINO) ==
	       (agi->agi_freecount == 1));
	ASSERT(free->di_u.di_next != NULLAGINO_ALLOC);
	agi->agi_freelist = free->di_u.di_next;
	/*
	 * Mark the inode in-buffer as free by setting its di_next to
	 * a reserved value.
	 */
	free->di_u.di_next = NULLAGINO_ALLOC;
	xfs_ialloc_log_di(tp, fbuf, off, XFS_DI_U);
	agi->agi_freecount--;
	xfs_ialloc_log_agi(tp, agbp, XFS_AGI_FREECOUNT | XFS_AGI_FREELIST);
	xfs_trans_mod_sb(tp, XFS_TRANS_SB_IFREE, -1);
	return;
}

/*
 * Return the next (past agino) inode on the freelist for this allocation group
 * (given by agbp).  Used to traverse the whole list, e.g. for printing.
 */
xfs_agino_t				/* a.g. inode next on freelist */
xfs_dialloc_next_free(
	xfs_mount_t	*mp,		/* filesystem mount structure */
	xfs_trans_t	*tp,		/* transaction pointer */
	buf_t		*agbp,		/* buffer for ag.inode header */
	xfs_agino_t	agino)		/* inode to get next free for */
{
	xfs_agblock_t	agbno;	/* block number in allocation group */
	xfs_agi_t	*agi;	/* allocation group header */
	xfs_agnumber_t	agno;	/* allocation group number */
	buf_t		*fbuf;	/* buffer containing the free inode */
	xfs_dinode_t	*free;	/* pointer to the free inode */
	int		off;	/* index of inode in the buffer */

	agi = XFS_BUF_TO_AGI(agbp);
	agno = agi->agi_seqno;
	agbno = XFS_AGINO_TO_AGBNO(mp, agino);
	off = XFS_AGINO_TO_OFFSET(mp, agino);
	fbuf = xfs_btree_read_bufs(mp, tp, agno, agbno, 0);
	free = XFS_MAKE_IPTR(mp, fbuf, off);
	agino = free->di_u.di_next;
	xfs_trans_brelse(tp, fbuf);
	return agino;
}

/*
 * Free disk inode.  Carefully avoids touching the incore inode, all
 * manipulations incore are the caller's responsibility.
 *
 * The caller is responsible for making the same changes to the in-core
 * inode that are made to the on-disk inode here.  This applies to the
 * di_format, di_mode, and di_flags fields.  It does not apply to the
 * di_u.di_next field.  This field is managed entirely by xfs_dialloc()
 * and xfs_difree() since it can be changed out from under the in-core
 * inodes that are free in order to preserve locality on the free list.
 */
void
xfs_difree(
	xfs_trans_t	*tp,		/* transaction pointer */
	xfs_ino_t	inode)		/* inode to be freed */
{
	xfs_agblock_t	agbno;	/* block number of inode relative to ag */
	buf_t		*agbp;	/* buffer containing allocation group header */
	xfs_agino_t	agino;	/* inode number relative to allocation group */
	xfs_agnumber_t	agno;	/* allocation group number */
	xfs_agi_t	*agi;	/* allocation group header */
	buf_t		*fbuf;	/* buffer containing inode to be freed */
	int		flags;	/* inode field logging flags */
	int		found;	/* free inode in same block is found */
	xfs_dinode_t	*free;	/* pointer into our inode's buffer */
	int		i;	/* index of ip in fbuf */
	xfs_dinode_t	*ip;	/* pointer to inodes in the buffer */
	xfs_mount_t	*mp;	/* mount structure for filesystem */
	int		off;	/* index of free in fbuf */

	mp = tp->t_mountp;
	/*
	 * Break up inode number into its components.
	 */
	agno = XFS_INO_TO_AGNO(mp, inode);
	ASSERT(agno < mp->m_sb.sb_agcount);
	agbno = XFS_INO_TO_AGBNO(mp, inode);
	off = XFS_INO_TO_OFFSET(mp, inode);
	agino = XFS_OFFBNO_TO_AGINO(mp, agbno, off);
	ASSERT(agino != NULLAGINO);
	/*
	 * Get the allocation group header.
	 */
	agbp = xfs_ialloc_read_agi(mp, tp, agno);
	agi = XFS_BUF_TO_AGI(agbp);
	ASSERT(agi->agi_magicnum == XFS_AGI_MAGIC);
	ASSERT(agbno < agi->agi_length);
	/*
	 * Get the inode into a buffer
	 */
	fbuf = xfs_btree_read_bufs(mp, tp, agno, agbno, 0);
	free = XFS_MAKE_IPTR(mp, fbuf, off);
	ASSERT(free->di_core.di_magic == XFS_DINODE_MAGIC);
	/*
	 * Look at other inodes in the same block; if there are any free
	 * then insert this one after.  This increases the locality
	 * in the inode free list.
	 */
	for (flags = 0, found = 0, i = 0, ip = XFS_MAKE_IPTR(mp, fbuf, i);
	     i < mp->m_sb.sb_inopblock;
	     i++, ip = XFS_MAKE_IPTR(mp, fbuf, i)) {
		if (ip == free)
			continue;
		if (ip->di_u.di_next == NULLAGINO_ALLOC ||
		    ip->di_core.di_format != XFS_DINODE_FMT_AGINO)
			continue;
		free->di_u.di_next = ip->di_u.di_next;
		ip->di_u.di_next = agino;
		xfs_ialloc_log_di(tp, fbuf, i, XFS_DI_U);
		found = 1;
		break;
	}
	/*
	 * Insert the inode to the freelist if a neighbor wasn't found.
	 */
	if (!found) {
		free->di_u.di_next = agi->agi_freelist;
		ASSERT(agino != NULLAGINO);
		agi->agi_freelist = agino;
		flags |= XFS_AGI_FREELIST;
	}
	/*
	 * Make the on-disk version of the inode look free.
	 */
	free->di_core.di_format = XFS_DINODE_FMT_AGINO;
	free->di_core.di_mode = 0;
	free->di_core.di_flags = 0;
	/*
	 * Log the change to the newly freed inode.
	 */
	xfs_ialloc_log_di(tp, fbuf, off, XFS_DI_U | XFS_DI_MODE |
			  XFS_DI_FORMAT | XFS_DI_FLAGS);
	/*
	 * Change the inode free counts and log the ag/sb changes.
	 */
	agi->agi_freecount++;
	flags |= XFS_AGI_FREECOUNT;
	xfs_ialloc_log_agi(tp, agbp, flags);
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
