#ident	"$Revision$"

#include <sys/param.h>
#define _KERNEL
#include <sys/buf.h>
#undef _KERNEL
#include <sys/vnode.h>
#include "xfs_types.h"
#include "xfs_inum.h"
#include "xfs.h"
#include "xfs_trans.h"
#include "xfs_sb.h"
#include "xfs_ag.h"
#include "xfs_mount.h"
#include "xfs_alloc.h"
#include "xfs_ialloc.h"

#ifdef SIM
#include "sim.h"
#include <bstring.h>
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
}

void
xfs_umount(xfs_mount_t *mp)
/* ARGSUSED */
{
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
	bcopy((caddr_t)&mp->m_sb + first, (caddr_t)sbp + first, last - first + 1);
	xfs_trans_log_buf(tp, buf, first, last);
}
