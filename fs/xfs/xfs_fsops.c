#ident	"$Revision: 1.12 $"

#include <sys/param.h>
#include <sys/buf.h>
#include <sys/debug.h>
#include <sys/errno.h>
#include <sys/file.h>
#include <sys/kmem.h>
#include <sys/sema.h>
#include <sys/systm.h>
#include <sys/vfs.h>
#include <sys/vnode.h>
#include "xfs_types.h"
#include "xfs_inum.h"
#include "xfs_log.h"
#include "xfs_trans.h"
#include "xfs_sb.h"
#include "xfs_mount.h"
#include "xfs_ag.h"
#include "xfs_alloc_btree.h"
#include "xfs_bmap_btree.h"
#include "xfs_ialloc_btree.h"
#include "xfs_btree.h"
#include "xfs_error.h"
#include "xfs_alloc.h"
#include "xfs_ialloc.h"
#include "xfs_fsops.h"
#include "xfs_itable.h"

/*
 * File system operations
 */

STATIC int
xfs_fs_geometry(
	xfs_mount_t		*mp,
	xfs_fsop_geom_t		*geo)
{
	geo->blocksize = mp->m_sb.sb_blocksize;
	geo->rtextsize = mp->m_sb.sb_rextsize;
	geo->agblocks = mp->m_sb.sb_agblocks;
	geo->agcount = mp->m_sb.sb_agcount;
	geo->logblocks = mp->m_sb.sb_logblocks;
	geo->sectsize = mp->m_sb.sb_sectsize;
	geo->inodesize = mp->m_sb.sb_inodesize;
	geo->datablocks = mp->m_sb.sb_dblocks;
	geo->rtblocks = mp->m_sb.sb_rblocks;
	geo->rtextents = mp->m_sb.sb_rextents;
	geo->logstart = mp->m_sb.sb_logstart;
	geo->uuid = mp->m_sb.sb_uuid;
	return 0;
}

STATIC int
xfs_growfs_data(
	xfs_mount_t		*mp,		/* mount point for filesystem */
	xfs_growfs_data_t	*in)		/* growfs data input struct */
{
	xfs_agf_t		*agf;
	xfs_agi_t		*agi;
	xfs_agnumber_t		agno;
	xfs_extlen_t		agsize;
	xfs_alloc_rec_t		*arec;
	xfs_btree_sblock_t	*block;
	buf_t			*bp;
	int			bsize;
	int			bucket;
	int			error;
	xfs_agnumber_t		nagcount;
	xfs_rfsblock_t		nb;
	xfs_rfsblock_t		new;
	xfs_rfsblock_t		nfree;
	xfs_agnumber_t		oagcount;
	xfs_sb_t		*sbp;
	int			sectbb;
	xfs_trans_t		*tp;

	nb = in->newblocks;
	if (nb <= mp->m_sb.sb_dblocks)
		return XFS_ERROR(EINVAL);
	bp = read_buf(mp->m_dev, XFS_FSB_TO_BB(mp, nb) - 1, 1, 0);
	if (bp == NULL)
		return XFS_ERROR(EINVAL);
	error = geterror(bp);
	brelse(bp);
	if (error)
		return XFS_ERROR(error);
	nagcount = (nb / mp->m_sb.sb_agblocks) +
		   ((nb % mp->m_sb.sb_agblocks) != 0);
	if (nb % mp->m_sb.sb_agblocks < XFS_MIN_AG_BLOCKS) {
		nagcount--;
		nb = nagcount * mp->m_sb.sb_agblocks;
		if (nb <= mp->m_sb.sb_dblocks)
			return XFS_ERROR(EINVAL);
	}
	new = nb - mp->m_sb.sb_dblocks;
	oagcount = mp->m_sb.sb_agcount;
	if (nagcount > oagcount) {
		mrlock(&mp->m_peraglock, MR_UPDATE, PINOD);
		mp->m_perag = kmem_realloc(mp->m_perag, sizeof(xfs_perag_t) * nagcount, KM_SLEEP);
		bzero(&mp->m_perag[oagcount], (nagcount - oagcount) * sizeof(xfs_perag_t));
		mrunlock(&mp->m_peraglock);
	}
	tp = xfs_trans_alloc(mp, XFS_TRANS_GROWFS);
	if (error = xfs_trans_reserve(tp, 2 * XFS_AG_MAXLEVELS(mp),
			XFS_GROWDATA_LOG_RES(mp), 0, 0, 0)) {
		xfs_trans_cancel(tp, 0);
		return error;
	}
	/* new ag's */
	sectbb = BTOBB(mp->m_sb.sb_sectsize);
	bsize = mp->m_sb.sb_blocksize;
	nfree = 0;
	for (agno = nagcount - 1; agno >= oagcount; agno--, new -= agsize) {
		/*
		 * AG freelist header block
		 */
		bp = get_buf(mp->m_dev, XFS_AG_DADDR(mp, agno, XFS_AGF_DADDR), sectbb, 0);
		agf = XFS_BUF_TO_AGF(bp);
		bzero(agf, mp->m_sb.sb_sectsize);
		agf->agf_magicnum = XFS_AGF_MAGIC;
		agf->agf_versionnum = XFS_AGF_VERSION;
		agf->agf_seqno = agno;
		if (agno == nagcount - 1)
			agsize = nb - (agno * mp->m_sb.sb_agblocks);
		else
			agsize = mp->m_sb.sb_agblocks;
		agf->agf_length = agsize;
		agf->agf_roots[XFS_BTNUM_BNOi] = XFS_BNO_BLOCK(mp);
		agf->agf_roots[XFS_BTNUM_CNTi] = XFS_CNT_BLOCK(mp);
		agf->agf_levels[XFS_BTNUM_BNOi] = 1;
		agf->agf_levels[XFS_BTNUM_CNTi] = 1;
		agf->agf_flfirst = 0;
		agf->agf_fllast = XFS_AGFL_SIZE - 1;
		agf->agf_flcount = 0;
		agf->agf_freeblks = agf->agf_length - XFS_PREALLOC_BLOCKS(mp);
		agf->agf_longest = agf->agf_freeblks;
		error = bwrite(bp);
		if (error) {
			goto error0;
		}
		/*
		 * AG inode header block
		 */
		bp = get_buf(mp->m_dev, XFS_AG_DADDR(mp, agno, XFS_AGI_DADDR), sectbb, 0);
		agi = XFS_BUF_TO_AGI(bp);
		bzero(agi, mp->m_sb.sb_sectsize);
		agi->agi_magicnum = XFS_AGI_MAGIC;
		agi->agi_versionnum = XFS_AGI_VERSION;
		agi->agi_seqno = agno;
		agi->agi_length = agsize;
		agi->agi_count = 0;
		agi->agi_root = XFS_IBT_BLOCK(mp);
		agi->agi_level = 1;
		agi->agi_freecount = 0;
		agi->agi_newino = NULLAGINO;
		agi->agi_dirino = NULLAGINO;
		for (bucket = 0; bucket < XFS_AGI_UNLINKED_BUCKETS; bucket++)
			agi->agi_unlinked[bucket] = NULLAGINO;
		error = bwrite(bp);
		if (error) {
			goto error0;
		}
		/*
		 * BNO btree root block
		 */
		bp = get_buf(mp->m_dev,
			XFS_AGB_TO_DADDR(mp, agno, XFS_BNO_BLOCK(mp)),
			BTOBB(bsize), 0);
		block = XFS_BUF_TO_SBLOCK(bp);
		bzero(block, bsize);
		block->bb_magic = XFS_ABTB_MAGIC;
		block->bb_level = 0;
		block->bb_numrecs = 1;
		block->bb_leftsib = block->bb_rightsib = NULLAGBLOCK;
		arec = XFS_BTREE_REC_ADDR(bsize, xfs_alloc, block, 1,
			mp->m_alloc_mxr[0]);
		arec->ar_startblock = XFS_PREALLOC_BLOCKS(mp);
		arec->ar_blockcount = agsize - arec->ar_startblock;
		error = bwrite(bp);
		if (error) {
			goto error0;
		}
		/*
		 * CNT btree root block
		 */
		bp = get_buf(mp->m_dev,
			XFS_AGB_TO_DADDR(mp, agno, XFS_CNT_BLOCK(mp)),
			BTOBB(bsize), 0);
		block = XFS_BUF_TO_SBLOCK(bp);
		bzero(block, bsize);
		block->bb_magic = XFS_ABTC_MAGIC;
		block->bb_level = 0;
		block->bb_numrecs = 1;
		block->bb_leftsib = block->bb_rightsib = NULLAGBLOCK;
		arec = XFS_BTREE_REC_ADDR(bsize, xfs_alloc, block, 1,
			mp->m_alloc_mxr[0]);
		arec->ar_startblock = XFS_PREALLOC_BLOCKS(mp);
		arec->ar_blockcount = agsize - arec->ar_startblock;
		nfree += arec->ar_blockcount;
		error = bwrite(bp);
		if (error) {
			goto error0;
		}
		/*
		 * INO btree root block
		 */
		bp = get_buf(mp->m_dev,
			XFS_AGB_TO_DADDR(mp, agno, XFS_IBT_BLOCK(mp)),
			BTOBB(bsize), 0);
		block = XFS_BUF_TO_SBLOCK(bp);
		bzero(block, bsize);
		block->bb_magic = XFS_IBT_MAGIC;
		block->bb_level = 0;
		block->bb_numrecs = 0;
		block->bb_leftsib = block->bb_rightsib = NULLAGBLOCK;
		error = bwrite(bp);
		if (error) {
			goto error0;
		}		
	}
	xfs_trans_agblocks_delta(tp, nfree);
	/*
	 * There are new blocks in the old last a.g.
	 */
	if (new) {
		/*
		 * Change the agi length.
		 */
		error = xfs_ialloc_read_agi(mp, tp, agno, &bp);
		if (error) {
			goto error0;
		}
		ASSERT(bp);
		agi = XFS_BUF_TO_AGI(bp);
		agi->agi_length += new;
		xfs_ialloc_log_agi(tp, bp, XFS_AGI_LENGTH);
		/*
		 * Change agf length.
		 */
		error = xfs_alloc_read_agf(mp, tp, agno, 0, &bp);
		if (error) {
			goto error0;
		}
		ASSERT(bp);
		agf = XFS_BUF_TO_AGF(bp);
		agf->agf_length += new;
		/*
		 * Free the new space.
		 */
		error = xfs_free_extent(tp,
			XFS_AGB_TO_FSB(mp, agno, agf->agf_length - new), new);
		if (error) {
			goto error0;
		}
	}
	if (nagcount > oagcount)
		xfs_trans_mod_sb(tp, XFS_TRANS_SB_AGCOUNT, nagcount - oagcount);
	xfs_trans_mod_sb(tp, XFS_TRANS_SB_DBLOCKS, nb - mp->m_sb.sb_dblocks);
	if (nfree)
		xfs_trans_mod_sb(tp, XFS_TRANS_SB_FDBLOCKS, nfree);
	error = xfs_trans_commit(tp, 0);
	if (error) {
		return error;
	}
	for (agno = 1; agno < nagcount; agno++) {
		bp = read_buf(mp->m_dev,
			XFS_AGB_TO_DADDR(mp, agno, XFS_SB_BLOCK(mp)),
			BTOBB(bsize), 0);
		sbp = XFS_BUF_TO_SBP(bp);
		*sbp = mp->m_sb;
		/*
		 * Don't worry about errors in writing out the alternate
		 * superblocks.  The real work is already done and committed.
		 */
		bwrite(bp);
	}
	return 0;

 error0:
	xfs_trans_cancel(tp, XFS_TRANS_ABORT);
	return error;
}

STATIC int
xfs_growfs_log(
	xfs_mount_t		*mp,	/* mount point for filesystem */
	xfs_growfs_log_t	*in)	/* growfs log input struct */
{
	xfs_extlen_t		nb;

	nb = in->newblocks;
	if (nb < XFS_MIN_LOG_BLOCKS || nb < XFS_B_TO_FSB(mp, XFS_MIN_LOG_BYTES))
		return XFS_ERROR(EINVAL);
	if (nb == mp->m_sb.sb_logblocks &&
	    in->isint == (mp->m_sb.sb_logstart != 0))
		return XFS_ERROR(EINVAL);
	/*
	 * Moving the log is hard, need new interfaces to sync
	 * the log first, hold off all activity while moving it.
	 * Can have shorter or longer log in the same space,
	 * or transform internal to external log or vice versa.
	 */
	return XFS_ERROR(ENOSYS);
}

STATIC int
xfs_growfs_rt(
	xfs_mount_t	*mp,		/* mount point for filesystem */
	xfs_growfs_rt_t	*in)		/* growfs rt input struct */
{
	xfs_rfsblock_t	nb;

	nb = in->newblocks;
	if (nb <= mp->m_sb.sb_rblocks)
		return XFS_ERROR(EINVAL);
	if (mp->m_sb.sb_rblocks && (in->extsize != mp->m_sb.sb_rextsize))
		return XFS_ERROR(EINVAL);
	/*
	 * If a realtime area is being added to the fs for the first time
	 * then it won't have been opened at mount, so we need to do
	 * that here.  Otherwise we can check the device size as for data.
	 */
	return XFS_ERROR(ENOSYS);
}

STATIC int
xfs_fs_counts(
	xfs_mount_t		*mp,
	xfs_fsop_counts_t	*cnt)
{
	int			s;

	s = XFS_SB_LOCK(mp);
	cnt->freedata = mp->m_sb.sb_fdblocks;
	cnt->freertx = mp->m_sb.sb_frextents;
	cnt->freeino = mp->m_sb.sb_ifree;
	cnt->allocino = mp->m_sb.sb_icount;
	XFS_SB_UNLOCK(mp, s);
	return 0;
}

int					/* error status */
xfs_fsoperations(
	int		fd,		/* file descriptor for fs */
	int		opcode,		/* operation code */
	void		*in,		/* input structure */
	void		*out)		/* output structure */
{
	int		error;
	void		*inb;
	xfs_mount_t	*mp;
	void		*outb;
	static int	cisize[XFS_FSOPS_COUNT] =
	{
		0,				/* XFS_FS_GEOMETRY */
		sizeof(xfs_growfs_data_t),	/* XFS_GROWFS_DATA */
		sizeof(xfs_growfs_log_t),	/* XFS_GROWFS_LOG */
		sizeof(xfs_growfs_rt_t),	/* XFS_GROWFS_RT */
		0,				/* XFS_FS_COUNTS */
	};
	static int	cosize[XFS_FSOPS_COUNT] =
	{
		sizeof(xfs_fsop_geom_t),	/* XFS_FS_GEOMETRY */
		0,				/* XFS_GROWFS_DATA */
		0,				/* XFS_GROWFS_LOG */
		0,				/* XFS_GROWFS_RT */
		sizeof(xfs_fsop_counts_t),	/* XFS_FS_COUNTS */
	};
	static int	wperm[XFS_FSOPS_COUNT] =
	{
		0,	/* XFS_FS_GEOMETRY */
		1,	/* XFS_GROWFS_DATA */
		1,	/* XFS_GROWFS_LOG */
		1,	/* XFS_GROWFS_RT */
		0,	/* XFS_FS_COUNTS */
	};

	if (opcode < 0 || opcode >= XFS_FSOPS_COUNT)
		return XFS_ERROR(EINVAL);
	if (error = xfs_fd_to_mp(fd, wperm[opcode], &mp))
		return error;
	if (cisize[opcode]) {
		inb = kmem_alloc(cisize[opcode], KM_SLEEP);
		if (error = copyin(in, inb, cisize[opcode])) {
			kmem_free(inb, cisize[opcode]);
			return XFS_ERROR(error);
		}
	} else
		inb = NULL;
	if (cosize[opcode])
		outb = kmem_alloc(cosize[opcode], KM_SLEEP);
	else
		outb = NULL;
	switch (opcode)
	{
	case XFS_FS_GEOMETRY:
		error = xfs_fs_geometry(mp, (xfs_fsop_geom_t *)outb);
		break;
	case XFS_GROWFS_DATA:
		if (!cpsema(&mp->m_growlock))
			return XFS_ERROR(EWOULDBLOCK);
		error = xfs_growfs_data(mp, (xfs_growfs_data_t *)inb);
		vsema(&mp->m_growlock);
		break;
	case XFS_GROWFS_LOG:
		if (!cpsema(&mp->m_growlock))
			return XFS_ERROR(EWOULDBLOCK);
		error = xfs_growfs_log(mp, (xfs_growfs_log_t *)inb);
		vsema(&mp->m_growlock);
		break;
	case XFS_GROWFS_RT:
		if (!cpsema(&mp->m_growlock))
			return XFS_ERROR(EWOULDBLOCK);
		error = xfs_growfs_rt(mp, (xfs_growfs_rt_t *)inb);
		vsema(&mp->m_growlock);
		break;
	case XFS_FS_COUNTS:
		error = xfs_fs_counts(mp, (xfs_fsop_counts_t *)outb);
		break;
	default:
		error = XFS_ERROR(EINVAL);
		break;
	}
	if (inb)
		kmem_free(inb, cisize[opcode]);
	if (!error && outb) {
		error = copyout(outb, out, cosize[opcode]);
		kmem_free(outb, cosize[opcode]);
	}
	return error;
}
