#ident	"$Revision$"

#include <sys/types.h>
#define _KERNEL
#include <sys/buf.h>
#undef _KERNEL
#include "xfs_sb.h"
#include "xfs_ag.h"
#include "xfs_alloc.h"
#include "xfs_ialloc.h"
#include "sim.h"
#include <bstring.h>

void
xfs_mount(xfs_mount_t *mp, dev_t dev)
{
	buf_t *buf;
	xfs_sb_t *sbp;

	mp->m_dev = dev;
	buf = bread(dev, XFS_SB_DADDR, howmany(sizeof(*sbp), BBSIZE));
	sbp = xfs_buf_to_sbp(buf);
	mp->m_sb = kmem_alloc(sizeof(*sbp), 0);
	*(mp->m_sb) = *sbp;
	mp->m_bsize = xfs_btod(sbp, 1);
	mp->m_agrotor = 0;
	brelse(buf);
}

void
xfs_umount(xfs_mount_t *mp)
{
	kmem_free(mp->m_sb, sizeof(xfs_sb_t));
}

void
xfs_mod_sb(xfs_trans_t *tp, int first, int last)
{
	buf_t		*buf;
	xfs_mount_t	*mp;
	xfs_sb_t	*sbp;

	mp = tp->t_mountp;
	buf = xfs_trans_bread(tp, mp->m_dev, XFS_SB_DADDR, mp->m_bsize);
	sbp = xfs_buf_to_sbp(buf);
	bcopy((caddr_t)mp->m_sb + first, (caddr_t)sbp + first, last - first + 1);
	xfs_trans_log_buf(tp, buf, first, last);
}
