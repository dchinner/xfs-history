#ident	"$Revision: 1.13 $"

#include <sys/param.h>
#include <sys/stat.h>
#include <sys/buf.h>
#include <sys/vnode.h>
#include <sys/uuid.h>
#include <sys/debug.h>
#include <stddef.h>
#include "xfs_types.h"
#include "xfs_inum.h"
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
#ifdef SIM
#include "sim.h"
#endif

/*
 * Prototypes for internal routines.
 */

STATIC void
xfs_ialloc_log_agi(xfs_trans_t *tp,	/* transaction pointer */
		   buf_t *buf,		/* allocation group header buffer */
		   int fields);		/* bitmask of fields to log */

STATIC void
xfs_ialloc_log_di(xfs_trans_t *tp,	/* transaction pointer */
	          buf_t *buf,		/* inode buffer */
		  int off,		/* index of inode in buffer */
		  int fields);		/* bitmask of fields to log */

STATIC buf_t *				/* allocation group header buffer */
xfs_ialloc_read_agi(xfs_mount_t *mp,	/* file system mount structure */
		    xfs_trans_t *tp,	/* transaction pointer */
		    xfs_agnumber_t agno); /* allocation group number */

/*
 * Prototypes for per-allocation group routines.
 */

STATIC int				/* success/failure */
xfs_ialloc_ag_alloc(xfs_trans_t *tp,	/* transaction pointer */
		    buf_t *agbuf);	/* alloc grp buffer */

STATIC buf_t *				/* allocation group buffer */
xfs_ialloc_ag_select(xfs_trans_t *tp,	/* transaction pointer */
		     xfs_ino_t parent,	/* parent directory inode number */
		     int sameag,	/* =1 to force to same ag. as parent */
		     mode_t mode);	/* bits set to indicate file type */

/*
 * Internal functions.
 */

/*
 * Log specified fields for the ag hdr (inode section)
 */
STATIC void
xfs_ialloc_log_agi(xfs_trans_t *tp, buf_t *buf, int fields)
{
	int	first;			/* first byte number */
	int	last;			/* last byte number */
	static const int offsets[] = {	/* field starting offsets */
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
	xfs_trans_log_buf(tp, buf, first, last);
}

/*
 * Log specified fields for the inode given by buf and off.
 */
STATIC void
xfs_ialloc_log_di(xfs_trans_t *tp, buf_t *buf, int off, int fields)
{
	xfs_dinode_t		*dip;		/* disk inode */
	int			first;		/* first byte number */
	int			ioffset;	/* off in bytes */
	int			last;		/* last byte number */
	xfs_mount_t		*mp;		/* mount point structure */
	xfs_sb_t		*sbp;		/* superblock structure */
	static const int	offsets[] = {	/* field offsets */
						/* keep in sync with bits */
		offsetof(xfs_dinode_t, di_core) +
			offsetof(xfs_dinode_core_t, di_magic),
		offsetof(xfs_dinode_t, di_core) +
			offsetof(xfs_dinode_core_t, di_mode),
		offsetof(xfs_dinode_t, di_core) +
			offsetof(xfs_dinode_core_t, di_version),
		offsetof(xfs_dinode_t, di_core) +
			offsetof(xfs_dinode_core_t, di_format),
		offsetof(xfs_dinode_t, di_core) +
			offsetof(xfs_dinode_core_t, di_nlink),
		offsetof(xfs_dinode_t, di_core) +
			offsetof(xfs_dinode_core_t, di_uid),
		offsetof(xfs_dinode_t, di_core) +
			offsetof(xfs_dinode_core_t, di_gid),
		offsetof(xfs_dinode_t, di_core) +
			offsetof(xfs_dinode_core_t, di_nextents),
		offsetof(xfs_dinode_t, di_core) +
			offsetof(xfs_dinode_core_t, di_size),
		offsetof(xfs_dinode_t, di_core) +
			offsetof(xfs_dinode_core_t, di_uuid),
		offsetof(xfs_dinode_t, di_core) +
			offsetof(xfs_dinode_core_t, di_atime),
		offsetof(xfs_dinode_t, di_core) +
			offsetof(xfs_dinode_core_t, di_mtime),
		offsetof(xfs_dinode_t, di_core) +
			offsetof(xfs_dinode_core_t, di_ctime),
		offsetof(xfs_dinode_t, di_core) +
			offsetof(xfs_dinode_core_t, di_gen),
		offsetof(xfs_dinode_t, di_core) +
			offsetof(xfs_dinode_core_t, di_nexti),
		offsetof(xfs_dinode_t, di_u),
		sizeof(xfs_dinode_t)
	};

	mp = tp->t_mountp;
	sbp = &mp->m_sb;
	/*
	 * Get the inode-relative first and last bytes for these fields
	 */
	xfs_btree_offsets(fields, offsets, XFS_DI_NUM_BITS, &first, &last);
	/*
	 * Convert to buffer offsets and log it.
	 */
	dip = xfs_make_iptr(sbp, buf, off);
	ioffset = (caddr_t)dip - (caddr_t)xfs_buf_to_dinode(buf);
	first += ioffset;
	last += ioffset;
	xfs_trans_log_buf(tp, buf, first, last);
}

/*
 * Read in the allocation group header (inode allocation section)
 */
STATIC buf_t *
xfs_ialloc_read_agi(xfs_mount_t *mp, xfs_trans_t *tp, xfs_agnumber_t agno)
{
	daddr_t		d;		/* disk block address */
	xfs_sb_t	*sbp;		/* superblock structure */

	ASSERT(agno != NULLAGNUMBER);
	sbp = &mp->m_sb;
	d = xfs_ag_daddr(sbp, agno, XFS_AGI_DADDR);
	return xfs_trans_bread(tp, mp->m_dev, d, 1);
}

/*
 * Allocation group level functions.
 */

/*
 * Allocate new inodes in the allocation group specified by agbuf.
 * Return 0 for failure, 1 for success.
 */
STATIC int
xfs_ialloc_ag_alloc(xfs_trans_t *tp, buf_t *agbuf)
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
	xfs_agino_t	nextino;	/* next inode number, for loop */
	xfs_sb_t	*sbp;		/* filesystem superblock */
	xfs_agino_t	thisino;	/* current inode number, for loop */

	mp = tp->t_mountp;
	agi = xfs_buf_to_agi(agbuf);
	sbp = &mp->m_sb;
	/*
	 * Locking will ensure that we don't have two callers in here
	 * at one time.
	 */
	ASSERT(agi->agi_freelist == NULLAGINO);
	ASSERT(agi->agi_freecount == 0);
	/*
	 * Calculate the range of number of blocks to be allocated
	 */
	minnewblocks = XFS_IALLOC_MIN_ALLOC(sbp, agi);
	maxnewblocks = XFS_IALLOC_MAX_ALLOC(sbp, agi);
	newlen = minnewblocks << sbp->sb_inopblog;
	/*
	 * Figure out a good first block number to ask for.  These inodes 
	 * should follow the previous inodes, to get some locality.
	 */
	newbno = (agi->agi_last == NULLAGINO) ?
		XFS_AGI_BLOCK(sbp) :
		(xfs_agino_to_agbno(sbp, agi->agi_last) + 1);
	newfsbno = xfs_agb_to_fsb(sbp, agi->agi_seqno, newbno);
	/*
	 * Allocate a variable-sized extent.
	 */
	newfsbno = xfs_alloc_vextent(tp, newfsbno, minnewblocks, maxnewblocks,
				     &newblocks, XFS_ALLOCTYPE_NEAR_BNO);
	if (newfsbno == NULLFSBLOCK)
		return 0;
	/*
	 * Convert the results.
	 */
	newlen = newblocks << sbp->sb_inopblog;
	newbno = xfs_fsb_to_agbno(sbp, newfsbno);
	newino = xfs_offbno_to_agino(sbp, newbno, 0);
	/*
	 * Loop over the new blocks, filling in the inodes.
	 * Run both loops backwards, so that the inodes are linked together
	 * forwards, in the natural order.
	 */
	nextino = NULLAGINO;
	flag = XFS_DI_MAGIC | XFS_DI_MODE | XFS_DI_VERSION | XFS_DI_FORMAT |
	       XFS_DI_NEXTENTS | XFS_DI_SIZE | XFS_DI_NEXTI | XFS_DI_U;
	for (j = (int)newblocks - 1; j >= 0; j--) {
		/*
		 * Get the block.
		 */
		fbuf = xfs_btree_getblks(mp, tp, agi->agi_seqno, newbno + j);
		/*
		 * Loop over the inodes in this buffer.
		 */
		for (i = sbp->sb_inopblock - 1; i >= 0; i--) {
			thisino = xfs_offbno_to_agino(sbp, newbno + j, i);
			free = xfs_make_iptr(sbp, fbuf, i);
			free->di_core.di_magic = XFS_DINODE_MAGIC;
			free->di_core.di_mode = 0;
			free->di_core.di_version = XFS_DINODE_VERSION;
			free->di_core.di_format = XFS_DINODE_FMT_AGINO;
			free->di_core.di_nextents = 0;
			free->di_core.di_size = 0;
			free->di_core.di_nexti = nextino;
			free->di_u.di_next = nextino;
			xfs_ialloc_log_di(tp, fbuf, i, flag);
			/*
			 * Since both loops run backwards, the first time
			 * here, nextino is null, and we set the new last
			 * inode in the allocation group.
			 */
			if (nextino == NULLAGINO)
				agi->agi_last = thisino;
			nextino = thisino;
		}
	}
	/*
	 * Set logging flags for allocation group header.
	 */
	flag = XFS_AGI_COUNT | XFS_AGI_LAST | XFS_AGI_FREELIST |
	       XFS_AGI_FREECOUNT;
	if (agi->agi_first == NULLAGINO) {
		agi->agi_first = newino;
		flag |= XFS_AGI_FIRST;
	}
	/*
	 * Modify and log allocation group header fields
	 */
	agi->agi_freelist = newino;	/* new freelist header */
	agi->agi_count += newlen;	/* inode count */
	agi->agi_freecount = newlen;	/* free inode count */
	xfs_ialloc_log_agi(tp, agbuf, flag);
	/*
	 * Modify/log superblock values for inode count and inode free count.
	 */
	xfs_trans_mod_sb(tp, XFS_SB_ICOUNT, newlen);
	xfs_trans_mod_sb(tp, XFS_SB_IFREE, newlen);
	return 1;
}

/*
 * Select an allocation group to look for a free inode in, based on the parent
 * inode and then mode.  sameag==1 forces the allocation to the same group
 * as the parent.  Return the allocation group buffer.
 */
STATIC buf_t *
xfs_ialloc_ag_select(xfs_trans_t *tp, xfs_ino_t parent, int sameag, mode_t mode)
{
	buf_t		*agbuf;		/* allocation group header buffer */
	xfs_agnumber_t	agcount;	/* number of ag's in the filesystem */
	xfs_agnumber_t	agno;		/* current ag number */
	int		agoff;		/* ag number relative to parent's */
	xfs_agi_t	*agi;		/* allocation group header */
	int		doneleft;	/* done searching lower numbered ag's */
	int		doneright;	/* done "" higher numbered ag's */
	xfs_mount_t	*mp;		/* mount point structure */
	int		needspace;	/* file mode implies space allocated */
	xfs_agnumber_t	pagno;		/* parent ag number */
	xfs_sb_t	*sbp;		/* super block structure */

	/*
	 * Files of these types need at least one block if length > 0.
	 */
	needspace = S_ISDIR(mode) || S_ISREG(mode) || S_ISLNK(mode);
	mp = tp->t_mountp;
	sbp = &mp->m_sb;
	pagno = xfs_ino_to_agno(sbp, parent);
	agcount = sbp->sb_agcount;
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
	for (agoff = S_ISDIR(mode) != 0 && !sameag, doneleft = doneright = 0;
	     !doneleft || !doneright;
	     agoff = -agoff + (agoff >= 0)) {
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
			continue;
		/*
		 * If this one is off the end to the left, stop there.
		 */
		} else if (agoff < 0 && pagno < -agoff) {
			doneleft = 1;
			continue;
		}
		/*
		 * Must be a valid allocation group.
		 */
		agno = pagno + agoff;
		agbuf = xfs_ialloc_read_agi(mp, tp, agno);
		agi = xfs_buf_to_agi(agbuf);
		/*
		 * Is there enough free space for the file plus a block
		 * of inodes (if we need to allocate some)?
		 */
		if (xfs_alloc_ag_freeblks(mp, tp, agno) >=
		    needspace + (agi->agi_freecount == 0))
			return agbuf;
		xfs_trans_brelse(tp, agbuf);
		if (sameag)
			break;
	}
	return (buf_t *)0;
}

/* 
 * File system level functions.
 */

/*
 * Allocate an inode on disk.
 * Mode is used to tell whether the new inode will need space, and whether
 * it is a directory.
 * The sameag flag is used by mkfs only.
 */
xfs_ino_t
xfs_dialloc(xfs_trans_t *tp, xfs_ino_t parent, int sameag, mode_t mode)
{
	xfs_agblock_t	agbno;		/* starting block number of inodes */
	xfs_agnumber_t	agcount;	/* number of allocation groups */
	buf_t		*agbuf;		/* allocation group header's buffer */
	xfs_agino_t	agino;		/* ag-relative inode to be returned */
	xfs_agnumber_t	agno;		/* allocation group number */
	xfs_agi_t	*agi;		/* allocation group header structure */
	buf_t		*fbuf;		/* buffer containing inode */
	xfs_dinode_t	*free;		/* pointer into fbuf for our dinode */
	xfs_ino_t	ino;		/* fs-relative inode to be returned */
	xfs_mount_t	*mp;		/* mount point structure */
	int		off;		/* index of our inode in fbuf */
	xfs_sb_t	*sbp;		/* superblock structure */
	xfs_agnumber_t	tagno;		/* testing allocation group number */

	/*
	 * Select an initial allocation group for the inode allocation.
	 */
	agbuf = xfs_ialloc_ag_select(tp, parent, sameag, mode);
	/*
	 * Couldn't find an allocation group satisfying the criteria, give up.
	 */
	if (!agbuf)
		return NULLFSINO;
	mp = tp->t_mountp;
	sbp = &mp->m_sb;
	agcount = sbp->sb_agcount;
	agi = xfs_buf_to_agi(agbuf);
	agno = agi->agi_seqno;
	tagno = agno;
	/*
	 * Loop until we find an allocation group that either has free inodes
	 * or in which we can allocate some inodes.  Iterate through the
	 * allocation groups upward, wrapping at the end.
	 */
	while (agi->agi_freecount == 0) {
		/*
		 * Try to allocate some new inodes in the allocation group.
		 */
		if (xfs_ialloc_ag_alloc(tp, agbuf))
			break;
		/*
		 * If it failed, give up on this ag.
		 */
		xfs_trans_brelse(tp, agbuf);
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
		agbuf = xfs_ialloc_read_agi(mp, tp, tagno);
		agi = xfs_buf_to_agi(agbuf);
	}
	/*
	 * Here with an allocation group that has a free inode.
	 */
	agno = tagno;
	agino = agi->agi_freelist;
	agbno = xfs_agino_to_agbno(sbp, agino);
	off = xfs_agino_to_offset(sbp, agino);
	/*
	 * Get a buffer containing the free inode.
	 */
	fbuf = xfs_btree_breads(mp, tp, agno, agbno);
	free = xfs_make_iptr(sbp, fbuf, off);
	ASSERT(free->di_core.di_magic == XFS_DINODE_MAGIC);
	ASSERT(free->di_core.di_mode == 0);
	ASSERT(free->di_core.di_format == XFS_DINODE_FMT_AGINO);
	/*
	 * Remove the inode from the freelist, and decrement the counts.
	 */
	agi->agi_freelist = free->di_u.di_next;
	agi->agi_freecount--;
	xfs_ialloc_log_agi(tp, agbuf, XFS_AGI_FREECOUNT | XFS_AGI_FREELIST);
	xfs_trans_mod_sb(tp, XFS_SB_IFREE, -1);
	ino = xfs_agino_to_ino(sbp, agno, agino);
	return ino;
}

/*
 * Return the next (past agino) inode on the freelist for this allocation group
 * (given by agbuf). Used to traverse the whole list, e.g. for printing.
 */
xfs_agino_t
xfs_dialloc_next_free(xfs_mount_t *mp, xfs_trans_t *tp, buf_t *agbuf,
		      xfs_agino_t agino)
{
	xfs_agblock_t	agbno;	/* block number in allocation group */
	xfs_agi_t	*agi;	/* allocation group header */
	xfs_agnumber_t	agno;	/* allocation group number */
	buf_t		*fbuf;	/* buffer containing the free inode */
	xfs_dinode_t	*free;	/* pointer to the free inode */
	int		off;	/* index of inode in the buffer */
	xfs_sb_t	*sbp;	/* super block structure */

	sbp = &mp->m_sb;
	agi = xfs_buf_to_agi(agbuf);
	agno = agi->agi_seqno;
	agbno = xfs_agino_to_agbno(sbp, agino);
	off = xfs_agino_to_offset(sbp, agino);
	fbuf = xfs_btree_breads(mp, tp, agno, agbno);
	free = xfs_make_iptr(sbp, fbuf, off);
	agino = free->di_u.di_next;
	xfs_trans_brelse(tp, fbuf);
	return agino;
}

/*
 * Free disk inode.  Carefully avoid touching the incore inode, all
 * manipulations incore are the caller's responsibility.
 */
xfs_agino_t
xfs_difree(xfs_trans_t *tp, xfs_ino_t inode)
{
	xfs_agblock_t	agbno;	/* block number of inode relative to ag */
	buf_t		*agbuf;	/* buffer containing allocation group header */
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
	xfs_sb_t	*sbp;	/* superblock structure for filesystem */

	mp = tp->t_mountp;
	sbp = &mp->m_sb;
	/*
	 * Break up inode number into its components.
	 */
	agno = xfs_ino_to_agno(sbp, inode);
	ASSERT(agno < sbp->sb_agcount);
	agbno = xfs_ino_to_agbno(sbp, inode);
	off = xfs_ino_to_offset(sbp, inode);
	agino = xfs_offbno_to_agino(sbp, agbno, off);
	/*
	 * Get the allocation group header.
	 */
	agbuf = xfs_ialloc_read_agi(mp, tp, agno);
	agi = xfs_buf_to_agi(agbuf);
	ASSERT(agbno < agi->agi_length);
	/*
	 * Get the inode into a buffer
	 */
	fbuf = xfs_btree_breads(mp, tp, agno, agbno);
	free = xfs_make_iptr(sbp, fbuf, off);
	ASSERT(free->di_core.di_magic == XFS_DINODE_MAGIC);
	/*
	 * Look at other inodes in the same block; if there are any
	 * then insert this one after.  This increases the locality
	 * in the inode free list.
	 */
	for (flags = 0, found = 0, i = 0, ip = xfs_make_iptr(sbp, fbuf, i);
	     i < sbp->sb_inopblock;
	     i++, ip = xfs_make_iptr(sbp, fbuf, i)) {
		if (ip == free)
			continue;
		if (ip->di_core.di_format != XFS_DINODE_FMT_AGINO) 
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
		agi->agi_freelist = agino;
		flags |= XFS_AGI_FREELIST;
	}
	/*
	 * Log the change to the newly freed inode.
	 */
	xfs_ialloc_log_di(tp, fbuf, off, XFS_DI_U);
	/*
	 * Change the inode free counts and log the ag/sb changes.
	 */
	agi->agi_freecount++;
	flags |= XFS_AGI_FREECOUNT;
	xfs_ialloc_log_agi(tp, agbuf, flags);
	xfs_trans_mod_sb(tp, XFS_SB_IFREE, 1);
	/*
	 * Return the value to be stored in the incore inode's union.
	 * The caller must set the format field to XFS_DINODE_FMT_AGINO.
	 */
	return free->di_u.di_next;
}

/*
 * Return the location of the inode in bno/off, for mapping it into a buffer.
 */
void
xfs_dilocate(xfs_mount_t *mp, xfs_trans_t *tp, xfs_ino_t ino,
	     xfs_fsblock_t *bno, int *off)
{
	xfs_agblock_t	agbno;	/* block number of inode in the alloc group */
	xfs_agnumber_t	agno;	/* allocation group number */
	int		offset;	/* index of inode in its buffer */
	xfs_sb_t	*sbp;	/* super block structure for the filesystem */

	ASSERT(ino != NULLFSINO);
	sbp = &mp->m_sb;
	/*
	 * Split up the inode number into its parts.
	 */
	agno = xfs_ino_to_agno(sbp, ino);
	ASSERT(agno < sbp->sb_agcount);
	agbno = xfs_ino_to_agbno(sbp, ino);
	ASSERT(agbno < sbp->sb_agblocks);
	offset = xfs_ino_to_offset(sbp, ino);
	ASSERT(offset < sbp->sb_inopblock);
	/*
	 * Store the results in the output parameters.
	 */
	*bno = xfs_agb_to_fsb(sbp, agno, agbno);
	*off = offset;
}
