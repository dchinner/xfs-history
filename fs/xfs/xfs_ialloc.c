#ident	"$Revision: 1.12 $"

#include <sys/param.h>
#include <sys/stat.h>		/* should really? */
#include <sys/buf.h>
#include <sys/vnode.h>
#include <sys/uuid.h>
#include <sys/debug.h>
#include <stddef.h>
#ifdef SIM
#include <bstring.h>
#endif
#include "xfs_types.h"
#include "xfs_inum.h"
#include "xfs.h"
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
void xfs_ialloc_log_di(xfs_trans_t *, buf_t *, int, int);

/*
 * Prototypes for per-ag routines.
 */
int xfs_ialloc_ag_alloc(xfs_trans_t *, buf_t *);
buf_t *xfs_ialloc_ag_select(xfs_trans_t *, xfs_ino_t, int, mode_t);

/*
 * Internal functions.
 */

void
xfs_ialloc_log_di(xfs_trans_t *tp, buf_t *buf, int off, int fields)
{
	xfs_dinode_t *dip;
	int first;
	int ioffset;
	int last;
	xfs_mount_t *mp;
	xfs_sb_t *sbp;
	static const int offsets[] = {
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
			offsetof(xfs_dinode_core_t, di_size),
		offsetof(xfs_dinode_t, di_core) +
			offsetof(xfs_dinode_core_t, di_uuid),
		offsetof(xfs_dinode_t, di_core) +
			offsetof(xfs_dinode_core_t, di_nextents),
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
	dip = xfs_make_iptr(sbp, buf, off);
	ioffset = (caddr_t)dip - (caddr_t)xfs_buf_to_dinode(buf);
	xfs_btree_offsets(fields, offsets, XFS_DI_NUM_BITS, &first, &last);
	xfs_trans_log_buf(tp, buf, first, last);
}

/*
 * Allocation group level functions.
 */

int
xfs_ialloc_ag_alloc(xfs_trans_t *tp, buf_t *agbuf)
{
	xfs_aghdr_t *agp;
	buf_t *fbuf;
	int flag;
	xfs_dinode_t *free;
	int i;
	int j;
	xfs_extlen_t maxnewblocks;
	xfs_extlen_t minnewblocks;
	xfs_mount_t *mp;
	xfs_extlen_t newblocks;
	xfs_agblock_t newbno;
	xfs_fsblock_t newfsbno;
	xfs_agino_t newino;
	xfs_agino_t newlen;
	xfs_agino_t nextino;
	xfs_sb_t *sbp;
	xfs_agino_t thisino;

	mp = tp->t_mountp;
	agp = xfs_buf_to_agp(agbuf);
	sbp = &mp->m_sb;
	minnewblocks = XFS_IALLOC_MIN_ALLOC(sbp, agp);
	maxnewblocks = XFS_IALLOC_MAX_ALLOC(sbp, agp);
	newlen = minnewblocks << sbp->sb_inopblog;
	newbno = (agp->ag_ilast == NULLAGINO) ?
		XFS_AGH_BLOCK : (xfs_agino_to_agbno(sbp, agp->ag_ilast) + 1);
	newfsbno = xfs_agb_to_fsb(sbp, agp->ag_seqno, newbno);
	newfsbno = xfs_alloc_vextent(tp, newfsbno, minnewblocks, maxnewblocks, &newblocks, XFS_ALLOCTYPE_NEAR_BNO);
	if (newfsbno == NULLFSBLOCK)
		return 0;
	newlen = newblocks << sbp->sb_inopblog;
	newbno = xfs_fsb_to_agbno(sbp, newfsbno);
	newino = xfs_offbno_to_agino(sbp, newbno, 0);
	nextino = NULLAGINO;
	for (j = (int)newblocks - 1; j >= 0; j--) {
		fbuf = xfs_btree_bread(mp, tp, agp->ag_seqno, newbno + j);
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
			xfs_ialloc_log_di(tp, fbuf, i, XFS_DI_MAGIC | XFS_DI_MODE | XFS_DI_VERSION | XFS_DI_FORMAT | XFS_DI_NEXTENTS | XFS_DI_SIZE | XFS_DI_NEXTI | XFS_DI_U);
			if (nextino == NULLAGINO)
				agp->ag_ilast = thisino;
			nextino = thisino;
		}
	}
	flag = XFS_AG_ICOUNT | XFS_AG_ILAST | XFS_AG_IFLIST | XFS_AG_IFCOUNT;
	if (agp->ag_ifirst == NULLAGINO) {
		agp->ag_ifirst = newino;
		flag |= XFS_AG_IFIRST;
	}
	agp->ag_iflist = newino;
	agp->ag_icount += newlen;
	agp->ag_ifcount += newlen;
	xfs_btree_log_ag(tp, agbuf, flag);
	xfs_trans_mod_sb(tp, XFS_SB_ICOUNT, newlen);
	xfs_trans_mod_sb(tp, XFS_SB_IFREE, newlen);
	return 1;
}

/*
 * Select an allocation group for the inode allocation.
 * Return the ag buffer for it.
 */
buf_t *
xfs_ialloc_ag_select(xfs_trans_t *tp, xfs_ino_t parent, int sameag, mode_t mode)
{
	buf_t *agbuf;
	xfs_agnumber_t agcount;
	xfs_agnumber_t agno;
	int agoff;
	xfs_aghdr_t *agp;
	int doneleft;
	int doneright;
	xfs_mount_t *mp;
	int needspace;
	xfs_agnumber_t pagno;
	xfs_sb_t *sbp;

	needspace = S_ISDIR(mode) || S_ISREG(mode) || S_ISLNK(mode);
	mp = tp->t_mountp;
	sbp = &mp->m_sb;
	pagno = xfs_ino_to_agno(sbp, parent);
	agcount = sbp->sb_agcount;
	if (pagno >= agcount)
		return (buf_t *)0;
	for (agoff = S_ISDIR(mode) != 0 && !sameag, doneleft = doneright = 0;
	     !doneleft || !doneright;
	     agoff = -agoff + (agoff >= 0)) {
		if ((agoff > 0 && doneright) || (agoff < 0 && doneleft))
			continue;
		if (agoff >= 0 && pagno + agoff >= agcount) {
			doneright = 1;
			continue;
		} else if (agoff < 0 && pagno < -agoff) {
			doneleft = 1;
			continue;
		}
		agno = pagno + agoff;
		agbuf = xfs_btree_bread(mp, tp, agno, XFS_AGH_BLOCK);
		agp = xfs_buf_to_agp(agbuf);
		if (agp->ag_freeblks >= needspace + (agp->ag_ifcount == 0))
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
	xfs_agblock_t agbno;
	xfs_agnumber_t agcount;
	buf_t *agbuf;
	xfs_agino_t agino;
	xfs_agnumber_t agno;
	xfs_aghdr_t *agp;
	buf_t *fbuf;
	xfs_dinode_t *free;
	xfs_ino_t ino;
	xfs_mount_t *mp;
	int off;
	xfs_sb_t *sbp;
	xfs_agnumber_t tagno;

	agbuf = xfs_ialloc_ag_select(tp, parent, sameag, mode);
	if (!agbuf)
		return NULLFSINO;
	mp = tp->t_mountp;
	sbp = &mp->m_sb;
	agcount = sbp->sb_agcount;
	agp = xfs_buf_to_agp(agbuf);
	agno = agp->ag_seqno;
	tagno = agno;
	while (agp->ag_ifcount == 0) {
		if (xfs_ialloc_ag_alloc(tp, agbuf))
			break;
		xfs_trans_brelse(tp, agbuf);
		if (sameag)
			return NULLFSINO;
		if (++tagno == agcount)
			tagno = 0;
		if (tagno == agno)
			return NULLFSINO;
		agbuf = xfs_btree_bread(mp, tp, tagno, XFS_AGH_BLOCK);
		agp = xfs_buf_to_agp(agbuf);
	}
	agno = tagno;
	agino = agp->ag_iflist;
	agbno = xfs_agino_to_agbno(sbp, agino);
	off = xfs_agino_to_offset(sbp, agino);
	fbuf = xfs_btree_bread(mp, tp, agno, agbno);
	free = xfs_make_iptr(sbp, fbuf, off);
	ASSERT(free->di_core.di_magic == XFS_DINODE_MAGIC);
	ASSERT(free->di_core.di_mode == 0);
	ASSERT(free->di_core.di_format == XFS_DINODE_FMT_AGINO);
	agp->ag_iflist = free->di_u.di_next;
	agp->ag_ifcount--;
	xfs_btree_log_ag(tp, agbuf, XFS_AG_IFCOUNT | XFS_AG_IFLIST);
	xfs_trans_mod_sb(tp, XFS_SB_IFREE, -1);
	ino = xfs_agino_to_ino(sbp, agno, agino);
	return ino;
}

/*
 * Return the next inode on the freelist.
 * Used to traverse the whole list, e.g. for printing.
 */
xfs_agino_t
xfs_dialloc_next_free(xfs_mount_t *mp, xfs_trans_t *tp, buf_t *agbuf, xfs_agino_t agino)
{
	xfs_agblock_t agbno;
	xfs_agnumber_t agno;
	xfs_aghdr_t *agp;
	buf_t *fbuf;
	xfs_dinode_t *free;
	int off;
	xfs_sb_t *sbp;

	sbp = &mp->m_sb;
	agp = xfs_buf_to_agp(agbuf);
	agno = agp->ag_seqno;
	agbno = xfs_agino_to_agbno(sbp, agino);
	off = xfs_agino_to_offset(sbp, agino);
	fbuf = xfs_btree_bread(mp, tp, agno, agbno);
	free = xfs_make_iptr(sbp, fbuf, off);
	agino = free->di_u.di_next;
	xfs_trans_brelse(tp, fbuf);
	return agino;
}

/*
 * Free disk inode.
 */
int
xfs_difree(xfs_trans_t *tp, xfs_ino_t inode)
{
	xfs_agblock_t agbno;
	buf_t *agbuf;
	xfs_agino_t agino;
	xfs_agnumber_t agno;
	xfs_aghdr_t *agp;
	buf_t *fbuf;
	xfs_dinode_t *free;
	xfs_mount_t *mp;
	int off;
	xfs_sb_t *sbp;

	mp = tp->t_mountp;
	sbp = &mp->m_sb;
	agno = xfs_ino_to_agno(sbp, inode);
	if (agno >= sbp->sb_agcount)
		return 0;
	agbno = xfs_ino_to_agbno(sbp, inode);
	off = xfs_ino_to_offset(sbp, inode);
	agino = xfs_offbno_to_agino(sbp, agbno, off);
	agbuf = xfs_btree_bread(mp, tp, agno, XFS_AGH_BLOCK);
	agp = xfs_buf_to_agp(agbuf);
	if (agbno >= agp->ag_length)
		return 0;
	fbuf = xfs_btree_bread(mp, tp, agno, agbno);
	free = xfs_make_iptr(sbp, fbuf, off);
	ASSERT(free->di_core.di_magic == XFS_DINODE_MAGIC);
	ASSERT(free->di_core.di_mode != 0);
	free->di_core.di_mode = 0;
	free->di_core.di_format = XFS_DINODE_FMT_AGINO;
	free->di_u.di_next = agp->ag_iflist;
	xfs_ialloc_log_di(tp, fbuf, off, XFS_DI_MODE | XFS_DI_FORMAT | XFS_DI_U);
	agp->ag_iflist = agino;
	agp->ag_ifcount++;
	xfs_btree_log_ag(tp, agbuf, XFS_AG_IFLIST | XFS_AG_IFCOUNT);
	xfs_trans_mod_sb(tp, XFS_SB_IFREE, 1);
	return 1;
}

/*
 * Return the location of the inode, for mapping it into a buffer.
 */
int
xfs_dilocate(xfs_mount_t *mp, xfs_trans_t *tp, xfs_ino_t ino, xfs_fsblock_t *bno, int *off)
{
	xfs_agblock_t agbno;
	xfs_agnumber_t agno;
	int offset;
	xfs_sb_t *sbp;

	ASSERT(ino != NULLFSINO);
	sbp = &mp->m_sb;
	agno = xfs_ino_to_agno(sbp, ino);
	ASSERT(agno < sbp->sb_agcount);
	agbno = xfs_ino_to_agbno(sbp, ino);
	ASSERT(agbno < sbp->sb_agblocks);
	offset = xfs_ino_to_offset(sbp, ino);
	ASSERT(offset < sbp->sb_inopblock);
	*bno = xfs_agb_to_fsb(sbp, agno, agbno);
	*off = offset;
	return 1;
}
