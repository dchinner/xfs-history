#ident	"$Revision: 1.5 $"

#include <sys/param.h>
#include <sys/buf.h>
#include <sys/errno.h>
#include <sys/immu.h>
#include <sys/kmem.h>
#include <sys/sema.h>
#include <sys/time.h>
#include <sys/vnode.h>
#include <sys/debug.h>
#ifndef SIM
#include <sys/systm.h>
#else
#define _KERNEL 1
#endif
#include <fs/specfs/snode.h>
#ifdef SIM
#undef _KERNEL
#endif
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
#ifdef SIM
#include "sim.h"
#endif

/*
 * Return stat information for one inode.
 * Return 1 for success, 0 for failure.
 */
STATIC int
xfs_bulkstat_one(
	xfs_mount_t	*mp,		/* mount point for filesystem */
	xfs_trans_t	*tp,		/* transaction pointer */
	xfs_ino_t	ino,		/* inode number to get data for */
	xfs_bstat_t	*buf)		/* buffer to place output in */
{
	xfs_inode_t	*ip;

	ip = xfs_iget(mp, tp, ino, XFS_ILOCK_SHARED);
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
	buf->bs_xflags = ip->i_d.di_flags;
	buf->bs_extsize = ip->i_d.di_extsize;
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
int					/* error status */
xfs_bulkstat(
	xfs_mount_t	*mp,		/* mount point for filesystem */
	xfs_trans_t	*tp,		/* transaction pointer */
	ino64_t		*lastino,	/* last inode returned */
	int		*count,		/* size of buffer/count returned */
	caddr_t		ubuffer)	/* buffer with inode stats */
{
	buf_t		*agbp;
	xfs_agi_t	*agi;
	xfs_agino_t	agino;
	xfs_agnumber_t	agno;
	int		bcount;
	xfs_bstat_t	*buffer;
	int		bufidx;
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
	bcount = MIN(left, NBPP / sizeof(*buffer));
	buffer = kmem_alloc(bcount * sizeof(*buffer), KM_SLEEP);
	error = bufidx = 0;
	cur = NULL;
	agbp = NULL;
	if (agno >= mp->m_sb.sb_agcount)
		return 0;
	if (agino != 0) {
		agbp = xfs_ialloc_read_agi(mp, tp, agno);
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
			agbp = xfs_ialloc_read_agi(mp, tp, agno);
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
		if (!xfs_bulkstat_one(mp, tp, ino, &buffer[bufidx])) {
			i++;
			continue;
		}
		i++;
		bufidx++;
		left--;
		if (bufidx == bcount) {
			if (copyout((caddr_t)buffer, ubuffer, bufidx * sizeof(*buffer))) {
				error = EFAULT;
				break;
			}
			ubuffer += bufidx * sizeof(*buffer);
			*count += bufidx;
			bufidx = 0;
		}
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
	return error;
}

/*
 * Return inode number table for the filesystem.
 */
int					/* error status */
xfs_inumbers(
	xfs_mount_t	*mp,		/* mount point for filesystem */
	xfs_trans_t	*tp,		/* transaction pointer */
	ino64_t		*lastino,	/* last inode returned */
	int		*count,		/* size of buffer/count returned */
	caddr_t		ubuffer)	/* buffer with inode descriptions */
{
	buf_t		*agbp;
	xfs_agi_t	*agi;
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
			agbp = xfs_ialloc_read_agi(mp, tp, agno);
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
