#ident	"$Revision: 1.13 $"

#include <sys/param.h>
#include <sys/buf.h>
#include <sys/errno.h>
#include <sys/vnode.h>
#include <sys/systm.h>
#include <sys/sema.h>
#include <fs/specfs/snode.h>
#include <sys/immu.h>
#include <sys/kmem.h>
#include <sys/time.h>
#include <sys/debug.h>
#include <sys/file.h>
#include <sys/vfs.h>
#include <sys/syssgi.h>
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
#include "xfs_dinode.h"
#include "xfs_inode_item.h"
#include "xfs_inode.h"
#include "xfs_ialloc.h"
#include "xfs_itable.h"
#include "xfs_error.h"


/* 
 * xfs_bulkstat() is used to fill in xfs_bstat structures as well as dm_stat
 * structures (by the dmi library). This is a pointer to a formatter function
 * that will iget the inode and fill in the appropriate structure.
 * see xfs_bulkstat_one() and dm_bulkstat_one() in dmi_xfs.c
 */
typedef int (*bulkstat_one_pf)(xfs_mount_t	*mp, 
			       xfs_trans_t	*tp,
			       xfs_ino_t   	ino,
			       void	     	*buffer);


/*
 * Return stat information for one inode.
 * Return 1 for success, 0 for failure.
 */
STATIC int
xfs_bulkstat_one(
	xfs_mount_t	*mp,		/* mount point for filesystem */
	xfs_trans_t	*tp,		/* transaction pointer */
	xfs_ino_t	ino,		/* inode number to get data for */
	void		*buffer)	/* buffer to place output in */
{
	xfs_inode_t	*ip;
	xfs_bstat_t	*buf;

	buf = (xfs_bstat_t *)buffer;
	if (ino == mp->m_sb.sb_rbmino || ino == mp->m_sb.sb_rsumino)
		return 0;
	ip = xfs_iget(mp, tp, ino, XFS_ILOCK_SHARED);
	ASSERT(ip != NULL);
	if (ip->i_d.di_mode == 0) {
		xfs_iput(ip, XFS_ILOCK_SHARED);
		return 0;
	}
	buf->bs_ino = ip->i_ino;
	buf->bs_mode = ip->i_d.di_mode;
	buf->bs_nlink = ip->i_d.di_nlink;
	buf->bs_uid = ip->i_d.di_uid;
	buf->bs_gid = ip->i_d.di_gid;
	buf->bs_size = ip->i_d.di_size;
	buf->bs_atime.tv_sec = ip->i_d.di_atime.t_sec;
	buf->bs_atime.tv_nsec = ip->i_d.di_atime.t_nsec;
	buf->bs_mtime.tv_sec = ip->i_d.di_mtime.t_sec;
	buf->bs_mtime.tv_nsec = ip->i_d.di_mtime.t_nsec;
	buf->bs_ctime.tv_sec = ip->i_d.di_ctime.t_sec;
	buf->bs_ctime.tv_nsec = ip->i_d.di_ctime.t_nsec;
	
	/* convert di_flags to bs_xflags.
	 */
	buf->bs_xflags = 0;
	if (ip->i_d.di_flags & XFS_DIFLAG_REALTIME) {
		buf->bs_xflags |= XFS_XFLAG_REALTIME;
	}
	buf->bs_extsize = ip->i_d.di_extsize << mp->m_sb.sb_blocklog;
	buf->bs_extents = ip->i_d.di_nextents;
	buf->bs_gen = ip->i_d.di_gen;
	buf->bs_uuid = ip->i_d.di_uuid;
	buf->bs_dmevmask = ip->i_d.di_dmevmask;
	buf->bs_dmstate = ip->i_d.di_dmstate;
	buf->bs_padding = 0;
	switch (ip->i_d.di_format) {
	case XFS_DINODE_FMT_DEV:
		buf->bs_rdev = ip->i_u2.iu_rdev;
		buf->bs_blksize = BLKDEV_IOSIZE;
		buf->bs_blocks = 0;
		break;
	case XFS_DINODE_FMT_LOCAL:
	case XFS_DINODE_FMT_UUID:
		buf->bs_rdev = 0;
		buf->bs_blksize = mp->m_sb.sb_blocksize;
		buf->bs_blocks = 0;
		break;
	case XFS_DINODE_FMT_EXTENTS:
	case XFS_DINODE_FMT_BTREE:
		buf->bs_rdev = 0;
		buf->bs_blksize = mp->m_sb.sb_blocksize;
		buf->bs_blocks = ip->i_d.di_nblocks + ip->i_delayed_blks;
		break;
	}
	xfs_iput(ip, XFS_ILOCK_SHARED);
	return 1;
}

/*
 * Return stat information in bulk (by-inode) for the filesystem.
 */
int				/* error status */
xfs_bulkstat(
	xfs_mount_t	*mp,		/* mount point for filesystem */
	xfs_trans_t	*tp,		/* transaction pointer */
	ino64_t		*lastino,	/* last inode returned */
	int		*count,		/* size of buffer/count returned */
	bulkstat_one_pf formatter,	/* func that'd fill a single buf */
	size_t		statstruct_size,/* sizeof struct that we're filling */
	caddr_t		ubuffer,	/* buffer with inode stats */
	int		*done)		/* 1 if there're more stats to get */
{
	buf_t		*agbp;
	xfs_agino_t	agino;
	xfs_agnumber_t	agno;
	int		bcount;
	caddr_t		buffer;
	caddr_t		bufp;	
	int		nbufs;
	xfs_btree_cur_t	*cur;
	int		error;
	__int32_t	gcnt;
	xfs_inofree_t	gfree;
	xfs_agino_t	gino;
	int		i;
	xfs_ino_t	ino;
	int		left;
	int		res;

	ino = (xfs_ino_t)*lastino;
	agno = XFS_INO_TO_AGNO(mp, ino);
	agino = XFS_INO_TO_AGINO(mp, ino);
	left = *count;
	*count = 0;
	*done = 0;
	bcount = MIN(left, NBPP / statstruct_size);
	bufp = buffer = kmem_alloc(bcount * statstruct_size, KM_SLEEP);
	error = nbufs = 0;
	cur = NULL;
	agbp = NULL;
	if (agno >= mp->m_sb.sb_agcount) {
		*done = 1;
		return 0;
	}
	if (agino != 0) {
		mrlock(&mp->m_peraglock, MR_ACCESS, PINOD);
		agbp = xfs_ialloc_read_agi(mp, tp, agno);
		mrunlock(&mp->m_peraglock);
		cur = xfs_btree_init_cursor(mp, tp, agbp, agno,
			XFS_BTNUM_INO, (xfs_inode_t *)0);
		(void)xfs_inobt_lookup_le(cur, agino, 0, 0);
		if (!xfs_inobt_get_rec(cur, &gino, &gcnt, &gfree))
			i = XFS_INODES_PER_CHUNK;
		else
			i = agino - gino + 1;
		xfs_trans_brelse(tp, agbp);
		xfs_btree_del_cursor(cur);
	} else
		i = XFS_INODES_PER_CHUNK;
	while (left > 0 && agno < mp->m_sb.sb_agcount) {
		if (i >= XFS_INODES_PER_CHUNK) {
			mrlock(&mp->m_peraglock, MR_ACCESS, PINOD);
			agbp = xfs_ialloc_read_agi(mp, tp, agno);
			mrunlock(&mp->m_peraglock);
			cur = xfs_btree_init_cursor(mp, tp, agbp, agno,
				XFS_BTNUM_INO, (xfs_inode_t *)0);
			(void)xfs_inobt_lookup_ge(cur, agino, 0, 0);
			res = xfs_inobt_get_rec(cur, &gino, &gcnt, &gfree);
			xfs_trans_brelse(tp, agbp);
			xfs_btree_del_cursor(cur);
			if (!res) {
				agno++;
				agino = 0;
				i = XFS_INODES_PER_CHUNK;
				continue;
			}
			i = 0;
		}
		agino = gino + i;
		if (gfree & XFS_INOBT_MASK(i)) {
			i++;
			continue;
		}
		ino = XFS_AGINO_TO_INO(mp, agno, agino);
		
		/*
		 * iget the inode and fill in a single buffer.
		 * See: xfs_bulkstat_one() & dm_bulkstat_one()
		 */
		if (! (*formatter)(mp, tp, ino, bufp)) {
			i++;
			continue;
		}
		bufp += statstruct_size; 
		i++;
		nbufs++;
		left--;
		if (nbufs == bcount) {
			if (copyout(buffer, ubuffer, 
				    nbufs * statstruct_size)){
				error = EFAULT;
				break;
			}
			ubuffer += nbufs * statstruct_size;
			*count += nbufs;
			nbufs = 0;
			bufp = buffer;
		}
	}

	if (!error) {
		if (nbufs) {
			if (copyout(buffer, ubuffer,
				    nbufs * statstruct_size))
				error = EFAULT;
			else
				*count += nbufs;
		}
		*lastino = XFS_AGINO_TO_INO(mp, agno, agino);
		if (agno >= mp->m_sb.sb_agcount)
			*done = 1;
	}
	kmem_free(buffer, bcount * statstruct_size);
	return error;
}

/*
 * Return inode number table for the filesystem.
 */
STATIC int				/* error status */
xfs_inumbers(
	xfs_mount_t	*mp,		/* mount point for filesystem */
	xfs_trans_t	*tp,		/* transaction pointer */
	ino64_t		*lastino,	/* last inode returned */
	int		*count,		/* size of buffer/count returned */
	caddr_t		ubuffer)	/* buffer with inode descriptions */
{
	buf_t		*agbp;
	xfs_agino_t	agino;
	xfs_agnumber_t	agno;
	int		bcount;
	xfs_inogrp_t	*buffer;
	int		bufidx;
	xfs_btree_cur_t	*cur;
	int		error;
	__int32_t	gcnt;
	xfs_inofree_t	gfree;
	xfs_agino_t	gino;
	xfs_ino_t	ino;
	int		left;

	ino = (xfs_ino_t)*lastino;
	agno = XFS_INO_TO_AGNO(mp, ino);
	agino = XFS_INO_TO_AGINO(mp, ino);
	left = *count;
	*count = 0;
	bcount = MIN(left, NBPP / sizeof(*buffer));
	buffer = kmem_alloc(bcount * sizeof(*buffer), KM_SLEEP);
	error = bufidx = 0;
	cur = NULL;
	agbp = NULL;
	while (left > 0 && agno < mp->m_sb.sb_agcount) {
		if (agbp == NULL) {
			mrlock(&mp->m_peraglock, MR_ACCESS, PINOD);
			agbp = xfs_ialloc_read_agi(mp, tp, agno);
			mrunlock(&mp->m_peraglock);
			cur = xfs_btree_init_cursor(mp, tp, agbp, agno,
				XFS_BTNUM_INO, (xfs_inode_t *)0);
			(void)xfs_inobt_lookup_ge(cur, agino, 0, 0);
		}
		if (!xfs_inobt_get_rec(cur, &gino, &gcnt, &gfree)) {
			xfs_trans_brelse(tp, agbp);
			agbp = NULL;
			xfs_btree_del_cursor(cur);
			cur = NULL;
			agno++;
			agino = 0;
			continue;
		}
		agino = gino + XFS_INODES_PER_CHUNK - 1;
		buffer[bufidx].xi_startino = XFS_AGINO_TO_INO(mp, agno, gino);
		buffer[bufidx].xi_alloccount = XFS_INODES_PER_CHUNK - gcnt;
		buffer[bufidx].xi_allocmask = ~gfree;
		bufidx++;
		left--;
		if (bufidx == bcount) {
			if (copyout((caddr_t)buffer, ubuffer,
					bufidx * sizeof(*buffer))) {
				error = EFAULT;
				break;
			}
			ubuffer += bufidx * sizeof(*buffer);
			*count += bufidx;
			bufidx = 0;
		}
		if (left)
			(void)xfs_inobt_increment(cur, 0);
	}
	if (!error) {
		if (bufidx) {
			if (copyout((caddr_t)buffer, ubuffer,
					bufidx * sizeof(*buffer)))
				error = EFAULT;
			else
				*count += bufidx;
		}
		*lastino = XFS_AGINO_TO_INO(mp, agno, agino);
	}
	kmem_free(buffer, bcount * sizeof(*buffer));
	if (cur)
		xfs_btree_del_cursor(cur);
	if (agbp)
		xfs_trans_brelse(tp, agbp);
	return error;
}

/*
 * Convert file descriptor of a file in the filesystem to
 * a mount structure pointer.
 */
int
xfs_fd_to_mp(int fd, xfs_mount_t **mpp)
{
	int		error;
	file_t		*fp;
	vfs_t		*vfsp;
	vnode_t		*vp;
	extern vfsops_t	xfs_vfsops;

	if (error = getf(fd, &fp))
		return XFS_ERROR(error);
	vp = fp->f_vnode;
	vfsp = vp->v_vfsp;
	if (vfsp->vfs_op != &xfs_vfsops)
		return XFS_ERROR(EINVAL);
	*mpp = vfsp->vfs_data;
	return 0;
}

/*
 * Syssgi interface for bulkstat and inode-table.
 */
int					/* error status */
xfs_itable(
	int		opc,		/* op code */
	int		fd,		/* file descriptor of file in fs. */
	caddr_t		lastip,		/* last inode number pointer */
	int		icount,		/* count of entries in buffer */
	caddr_t		ubuffer,	/* buffer with inode descriptions */
	caddr_t		ocount)		/* output count */
{
	int		count;		/* count of records returned */
	int		error;		/* error return value */
	ino64_t		inlast;		/* last inode number */
	xfs_mount_t	*mp;		/* mount point for filesystem */
	int		done;		/* = 1 if there are more stats to get 
					   and if bulkstat should be called
					   again. This is unused in syssgi
					   but used in dmi */

	if (error = xfs_fd_to_mp(fd, &mp))
		return error;
	if (copyin((void *)lastip, &inlast, sizeof(inlast)))
		return XFS_ERROR(EFAULT);
	if ((count = icount) <= 0)
		return XFS_ERROR(EINVAL);
	switch (opc) {
	case SGI_FS_INUMBERS:
		error = xfs_inumbers(mp, NULL, &inlast, &count, ubuffer);
		break;
	case SGI_FS_BULKSTAT:
		error = xfs_bulkstat(mp, NULL, &inlast, &count, 
				     (bulkstat_one_pf) xfs_bulkstat_one,
				     sizeof(xfs_bstat_t),
				     ubuffer,
				     &done);
		break;
	}
	if (error)
		return error;
	if (copyout(&inlast, (void *)lastip, sizeof(inlast)))
		return XFS_ERROR(EFAULT);
	if (copyout(&count, (void *)ocount, sizeof(count)))
		return XFS_ERROR(EFAULT);
	return 0;
}
