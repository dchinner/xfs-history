#ident	"$Revision: 1.8 $"

#include <sys/param.h>
#define _KERNEL
#include <sys/buf.h>
#undef _KERNEL
#include <sys/vnode.h>
#include <sys/uuid.h>
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
#include <bstring.h>
#include <stddef.h>
#endif

/*
 * Return a pointer to an initialized xfs_mount structure.
 */
xfs_mount_t *
xfs_mount_init(void)
{
	xfs_mount_t *mp;

	mp = kmem_zalloc(sizeof(*mp), 0);

	initnlock(&mp->m_ail_lock, "xfs_ail");
	initnlock(&mp->m_async_lock, "xfs_async");
	initnlock(&mp->m_ilock, "xfs_ilock");
	initnlock(&mp->m_ipinlock, "xfs_ipin");

	return (mp);
}
	
void
xfs_mount(xfs_mount_t *mp, dev_t dev)
{
	buf_t *buf;
	xfs_sb_t *sbp;

	mp->m_dev = dev;
	buf = bread(dev, XFS_SB_DADDR, howmany(sizeof(*sbp), BBSIZE));
	sbp = xfs_buf_to_sbp(buf);
	mp->m_sb = *sbp;
	mp->m_bsize = xfs_btod(sbp, 1);
	mp->m_agrotor = 0;
	brelse(buf);
	xfs_ihash_init(mp);
}

void
xfs_umount(xfs_mount_t *mp)
/* ARGSUSED */
{
}

void
xfs_mod_sb(xfs_trans_t *tp, int fields)
{
	buf_t		*buf;
	int		first;
	int		last;
	xfs_mount_t	*mp;
	xfs_sb_t	*sbp;
	static const int offsets[] = {
		offsetof(xfs_sb_t, sb_uuid),
		offsetof(xfs_sb_t, sb_dblocks),
		offsetof(xfs_sb_t, sb_blocksize),
		offsetof(xfs_sb_t, sb_magicnum),
		offsetof(xfs_sb_t, sb_rblocks),
		offsetof(xfs_sb_t, sb_rbitmap),
		offsetof(xfs_sb_t, sb_rsummary),
		offsetof(xfs_sb_t, sb_rootino),
		offsetof(xfs_sb_t, sb_rextsize),
		offsetof(xfs_sb_t, sb_agblocks),
		offsetof(xfs_sb_t, sb_agcount),
		offsetof(xfs_sb_t, sb_versionnum),
		offsetof(xfs_sb_t, sb_sectsize),
		offsetof(xfs_sb_t, sb_inodesize),
		offsetof(xfs_sb_t, sb_inopblock),
		offsetof(xfs_sb_t, sb_fname[0]),
		offsetof(xfs_sb_t, sb_fpack[0]),
		offsetof(xfs_sb_t, sb_blocklog),
		offsetof(xfs_sb_t, sb_sectlog),
		offsetof(xfs_sb_t, sb_inodelog),
		offsetof(xfs_sb_t, sb_inopblog),
		offsetof(xfs_sb_t, sb_smallfiles),
		offsetof(xfs_sb_t, sb_icount),
		offsetof(xfs_sb_t, sb_ifree),
		offsetof(xfs_sb_t, sb_fdblocks),
		offsetof(xfs_sb_t, sb_frextents),
		sizeof(xfs_sb_t)
	};

	mp = tp->t_mountp;
	buf = xfs_trans_bread(tp, mp->m_dev, XFS_SB_DADDR, mp->m_bsize);
	sbp = xfs_buf_to_sbp(buf);
	xfs_btree_offsets(fields, offsets, XFS_SB_NUM_BITS, &first, &last);
	bcopy((caddr_t)&mp->m_sb + first, (caddr_t)sbp + first, last - first + 1);
	xfs_trans_log_buf(tp, buf, first, last);
}
