#ident	"$Revision$"

#include <sys/param.h>
#include <sys/buf.h>
#include <sys/errno.h>
#include <sys/vnode.h>
#include <sys/systm.h>
#include <sys/sema.h>
#include <specfs/snode.h>
#include <sys/immu.h>
#include <sys/kmem.h>
#include <sys/time.h>
#include <sys/debug.h>
#include <sys/file.h>
#include <sys/vfs.h>
#include <sys/syssgi.h>
#include <sys/capability.h>
#include <sys/kthread.h>
#include <sys/uuid.h>
#include "xfs_macros.h"
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
#include "xfs_attr_sf.h"
#include "xfs_dir_sf.h"
#include "xfs_dinode.h"
#include "xfs_inode.h"
#include "xfs_ialloc.h"
#include "xfs_itable.h"
#include "xfs_error.h"

/*
 * Return stat information for one inode.
 * Return 1 for success, 0 for failure in the stat parameter.
 */
STATIC int
xfs_bulkstat_one(
	xfs_mount_t	*mp,		/* mount point for filesystem */
	xfs_trans_t	*tp,		/* transaction pointer */
	xfs_ino_t	ino,		/* inode number to get data for */
	void		*buffer,	/* buffer to place output in */
	daddr_t		bno)		/* starting bno of inode cluster */
{
	xfs_bstat_t	*buf;		/* return buffer */
	int		error;		/* error value */
	xfs_inode_t	*ip;		/* incore inode pointer */

	buf = (xfs_bstat_t *)buffer;
	if (ino == mp->m_sb.sb_rbmino || ino == mp->m_sb.sb_rsumino) {
		return 0;
	}
	error = xfs_iget(mp, tp, ino, XFS_ILOCK_SHARED, &ip, bno);
	if (error) {
		return 0;
	}
	ASSERT(ip != NULL);
	ASSERT(ip->i_blkno != (daddr_t)0);
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
	buf->bs_xflags =
		((ip->i_d.di_flags & XFS_DIFLAG_REALTIME) ?
			XFS_XFLAG_REALTIME : 0) |
		((ip->i_d.di_flags & XFS_DIFLAG_PREALLOC) ?
			XFS_XFLAG_PREALLOC : 0) |
		(XFS_IFORK_Q(ip) ?
			XFS_XFLAG_HASATTR : 0);
	buf->bs_extsize = ip->i_d.di_extsize << mp->m_sb.sb_blocklog;
	buf->bs_extents = ip->i_d.di_nextents;
	buf->bs_gen = ip->i_d.di_gen;
	bzero(buf->bs_pad, sizeof(buf->bs_pad));
	buf->bs_dmevmask = ip->i_d.di_dmevmask;
	buf->bs_dmstate = ip->i_d.di_dmstate;
	buf->bs_aextents = ip->i_d.di_anextents;
	switch (ip->i_d.di_format) {
	case XFS_DINODE_FMT_DEV:
		buf->bs_rdev = ip->i_df.if_u2.if_rdev;
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
	xfs_mount_t		*mp,	/* mount point for filesystem */
	xfs_trans_t		*tp,	/* transaction pointer */
	ino64_t			*lastinop, /* last inode returned */
	int			*ubcountp, /* size of buffer/count returned */
	bulkstat_one_pf		formatter, /* func that'd fill a single buf */
	size_t			statstruct_size, /* sizeof struct filling */
	caddr_t			ubuffer, /* buffer with inode stats */
	int			*done)	/* 1 if there're more stats to get */
{
	xfs_agblock_t		agbno;	/* allocation group block number */
	buf_t			*agbp;	/* agi header buffer */
	xfs_agi_t		*agi;	/* agi header data */
	xfs_agino_t		agino;	/* inode # in allocation group */
	xfs_agnumber_t		agno;	/* allocation group number */
	daddr_t			bno;	/* inode cluster start daddr */
	int			chunkidx; /* current index into inode chunk */
	xfs_btree_cur_t		*cur;	/* btree cursor for ialloc btree */
	int			end_of_ag; /* set if we've seen the ag end */
	int			error;	/* error code */
	__int32_t		gcnt;	/* current btree rec's count */
	xfs_inofree_t		gfree;	/* current btree rec's free mask */
	xfs_agino_t		gino;	/* current btree rec's start inode */
	int			i;	/* loop index */
	int			icount;	/* count of inodes good in irbuf */
	xfs_ino_t		ino;	/* inode number (filesystem) */
	xfs_inobt_rec_t		*irbp;	/* current irec buffer pointer */
	xfs_inobt_rec_t		*irbuf;	/* start of irec buffer */
	xfs_inobt_rec_t		*irbufend; /* end of good irec buffer entries */
	xfs_ino_t		lastino; /* last inode number returned */
	int			nbcluster; /* # of blocks in a cluster */
	int			nicluster; /* # of inodes in a cluster */
	int			nimask;	/* mask for inode clusters */
	int			nirbuf;	/* size of irbuf */
	int			tmp;	/* result value from btree calls */
	int			ubcount; /* size of user's buffer */
	int			ubleft;	/* spaces left in user's buffer */
	caddr_t			ubufp;	/* current pointer into user's buffer */

	/*
	 * Get the last inode value, see if there's nothing to do.
	 */
	ino = (xfs_ino_t)*lastinop;
	agno = XFS_INO_TO_AGNO(mp, ino);
	agino = XFS_INO_TO_AGINO(mp, ino);
	if (agno >= mp->m_sb.sb_agcount) {
		*done = 1;
		*ubcountp = 0;
		return 0;
	}
	ubcount = ubleft = *ubcountp;
	*ubcountp = 0;
	*done = 0;
	ubufp = ubuffer;
	nicluster = mp->m_sb.sb_blocksize >= XFS_INODE_CLUSTER_SIZE ?
		mp->m_sb.sb_inopblock :
		(XFS_INODE_CLUSTER_SIZE >> mp->m_sb.sb_inodelog);
	nimask = ~(nicluster - 1);
	nbcluster = nicluster >> mp->m_sb.sb_inopblog;
	/* 
	 * Lock down the user's buffer.
	 */
	if (error = useracc(ubuffer, ubcount * statstruct_size, B_READ, NULL))
		return error;
	/*
	 * Allocate a page-sized buffer for inode btree records.
	 * We could try allocating something smaller, but for normal
	 * calls we'll always (potentially) need the whole page.
	 */
	irbuf = kmem_alloc(NBPC, KM_SLEEP);
	nirbuf = NBPC / sizeof(*irbuf);
	/* 
	 * Loop over the allocation groups, starting from the last 
	 * inode returned; 0 means start of the allocation group.
	 */
	while (ubleft > 0 && agno < mp->m_sb.sb_agcount) {
		mrlock(&mp->m_peraglock, MR_ACCESS, PINOD);
		error = xfs_ialloc_read_agi(mp, tp, agno, &agbp);
		mrunlock(&mp->m_peraglock);
		if (error) {
			/*
			 * Skip this allocation group and go to the next one.
			 */
			agno++;
			agino = 0;
			continue;
		}
		agi = XFS_BUF_TO_AGI(agbp);
		/*
		 * Allocate and initialize a btree cursor for ialloc btree.
		 */
		cur = xfs_btree_init_cursor(mp, tp, agbp, agno, XFS_BTNUM_INO,
			(xfs_inode_t *)0, 0);
		irbp = irbuf;
		irbufend = irbuf + nirbuf;
		end_of_ag = 0;
		/*
		 * If we're returning in the middle of an allocation group,
		 * we need to get the remainder of the chunk we're in.
		 */
		if (agino > 0) {
			/*
			 * Lookup the inode chunk that this inode lives in.
			 */
			error = xfs_inobt_lookup_le(cur, agino, 0, 0, &tmp);
			if (!error &&	/* no I/O error */
			    tmp &&	/* lookup succeeded */
					/* got the record, should always work */
			    xfs_inobt_get_rec(cur, &gino, &gcnt, &gfree) &&
					/* this is the right chunk */
			    agino < gino + XFS_INODES_PER_CHUNK &&
					/* lastino was not last in chunk */
			    (chunkidx = agino - gino + 1) <
				    XFS_INODES_PER_CHUNK &&
					/* there are some left allocated */
			    XFS_INOBT_MASKN(chunkidx,
				    XFS_INODES_PER_CHUNK - chunkidx) & ~gfree) {
				/*
				 * Grab the chunk record.  Mark all the
				 * uninteresting inodes (because they're 
				 * before our start point) free.
				 */
				for (i = 0; i < chunkidx; i++) {
					if (XFS_INOBT_MASK(i) & ~gfree)
						gcnt++;
				}
				gfree |= XFS_INOBT_MASKN(0, chunkidx);
				irbp->ir_startino = gino;
				irbp->ir_freecount = gcnt;
				irbp->ir_free = gfree;
				irbp++;
				agino = gino + XFS_INODES_PER_CHUNK;
				icount = XFS_INODES_PER_CHUNK - gcnt;
			} else {
				/*
				 * If any of those tests failed, bump the
				 * inode number (just in case).
				 */
				agino++;
				icount = 0;
			}
			/*
			 * In any case, increment to the next record.
			 */
			if (!error)
				error = xfs_inobt_increment(cur, 0, &tmp);
		} else {
			/*
			 * Start of ag.  Lookup the first inode chunk.
			 */
			error = xfs_inobt_lookup_ge(cur, 0, 0, 0, &tmp);
			icount = 0;
		}
		/*
		 * Loop through inode btree records in this ag,
		 * until we run out of inodes or space in the buffer.
		 */
		while (irbp < irbufend && icount < ubcount) {
			/*
			 * Loop as long as we're unable to read the
			 * inode btree.
			 */
			while (error) {
				agino += XFS_INODES_PER_CHUNK;
				if (XFS_AGINO_TO_AGBNO(mp, agino) >=
				    agi->agi_length)
					break;
				error = xfs_inobt_lookup_ge(cur, agino, 0, 0,
							    &tmp);
			}
			/*
			 * If ran off the end of the ag either with an error,
			 * or the normal way, set end and stop collecting.
			 */
			if (error ||
			    !xfs_inobt_get_rec(cur, &gino, &gcnt, &gfree)) {
				end_of_ag = 1;
				break;
			}
			/*
			 * If this chunk has any allocated inodes, save it.
			 */
			if (gcnt < XFS_INODES_PER_CHUNK) {
				irbp->ir_startino = gino;
				irbp->ir_freecount = gcnt;
				irbp->ir_free = gfree;
				irbp++;
				icount += XFS_INODES_PER_CHUNK - gcnt;
			}
			/*
			 * Set agino to after this chunk and bump the cursor.
			 */
			agino = gino + XFS_INODES_PER_CHUNK;
			error = xfs_inobt_increment(cur, 0, &tmp);
		}
		/*
		 * Drop the btree buffers and the agi buffer.
		 * We can't hold any of the locks these represent
		 * when calling iget.
		 */
		xfs_btree_del_cursor(cur);
		xfs_trans_brelse(tp, agbp);
		/*
		 * Now format all the good inodes into the user's buffer.
		 */
		irbufend = irbp;
		for (irbp = irbuf; irbp < irbufend && ubleft > 0; irbp++) {
			/*
			 * Read-ahead the next chunk's worth of inodes.
			 */
			if (&irbp[1] < irbufend) {
				/*
				 * Loop over all clusters in the next chunk.
				 * Do a readahead if there are any allocated
				 * inodes in that cluster.
				 */
				for (agbno = XFS_AGINO_TO_AGBNO(mp,
							irbp[1].ir_startino),
				     chunkidx = 0;
				     chunkidx < XFS_INODES_PER_CHUNK;
				     chunkidx += nicluster,
				     agbno += nbcluster) {
					if (XFS_INOBT_MASKN(chunkidx,
							    nicluster) &
					    ~(irbp[1].ir_free))
						xfs_btree_reada_bufs(mp, agno,
							agbno, nbcluster);
				}
			}
			/*
			 * Now process this chunk of inodes.
			 */
			for (agino = irbp->ir_startino, chunkidx = 0;
			     ubleft > 0 &&
				irbp->ir_freecount < XFS_INODES_PER_CHUNK;
			     chunkidx++, agino++) {
				ASSERT(chunkidx < XFS_INODES_PER_CHUNK);
				/*
				 * Recompute agbno if this is the
				 * first inode of the cluster.
				 */
				if ((chunkidx & (nicluster - 1)) == 0)
					agbno = XFS_AGINO_TO_AGBNO(mp,
							irbp->ir_startino) +
						((chunkidx & nimask) >>
						 mp->m_sb.sb_inopblog);
				/*
				 * Skip if this inode is free.
				 */
				if (XFS_INOBT_MASK(chunkidx) & irbp->ir_free)
					continue;
				/*
				 * Count used inodes as free so we can tell
				 * when the chunk is used up.
				 */
				irbp->ir_freecount++;
				ino = XFS_AGINO_TO_INO(mp, agno, agino);
				bno = XFS_AGB_TO_DADDR(mp, agno, agbno);
				/*
				 * iget the inode and fill in a single buffer.
				 * See: xfs_bulkstat_one() & dm_bulkstat_one()
				 */
				if (!(*formatter)(mp, tp, ino, ubufp, bno))
					continue;
				ubufp += statstruct_size; 
				ubleft--;
				lastino = ino;
			}
		}
		/*
		 * Set up for the next loop iteration.
		 */
		if (ubleft > 0) {
			if (end_of_ag) {
				agno++;
				agino = 0;
			} else
				agino = XFS_INO_TO_AGINO(mp, lastino);
		} else
			break;
	}
	/*
	 * Done, we're either out of filesystem or space to put the data.
	 */
	kmem_free(irbuf, NBPC);
	unuseracc(ubuffer, ubcount * statstruct_size, B_READ);
	*ubcountp = ubcount - ubleft;
	if (agno >= mp->m_sb.sb_agcount) {
		/*
		 * If we ran out of filesystem, mark lastino as off
		 * the end of the filesystem, so the next call
		 * will return immediately.
		 */
		*lastinop = (ino64_t)XFS_AGINO_TO_INO(mp, agno, 0);
		*done = 1;
	} else
		*lastinop = (ino64_t)lastino;
	return 0;
}

/*
 * Return stat information in bulk (by-inode) for the filesystem.
 * Special case for non-sequential one inode bulkstat.
 */
STATIC int				/* error status */
xfs_bulkstat_single(
	xfs_mount_t		*mp,	/* mount point for filesystem */
	ino64_t			*lastino, /* last inode returned */
	int			*count,	/* size of buffer/count returned */
	caddr_t			buffer,	/* buffer with inode stats */
	int			*done)	/* 1 if there're more stats to get */
{
	xfs_bstat_t		bstat;	/* one bulkstat result structure */
	xfs_ino_t		ino;	/* filesystem inode number */

	ino = (xfs_ino_t)*lastino + 1;
	if (!xfs_bulkstat_one(mp, NULL, ino, &bstat, 0)) {
		return xfs_bulkstat(mp, NULL, lastino, count, xfs_bulkstat_one,
			sizeof(bstat), buffer, done);
	}
	if (copyout(&bstat, buffer, sizeof(bstat))) {
		*done = 0;
		*count = 0;
		return EFAULT;
	}
	*done = 0;
	*count = 1;
	*lastino = ino;
	return 0;
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
	int		tmp;

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
			error = xfs_ialloc_read_agi(mp, tp, agno, &agbp);
			mrunlock(&mp->m_peraglock);
			if (error) {
				/*
				 * If we can't read the AGI of this ag,
				 * then just skip to the next one.
				 */
				ASSERT(cur == NULL);
				agbp = NULL;
				agno++;
				agino = 0;
				continue;
			}
			cur = xfs_btree_init_cursor(mp, tp, agbp, agno,
				XFS_BTNUM_INO, (xfs_inode_t *)0, 0);
			error = xfs_inobt_lookup_ge(cur, agino, 0, 0, &tmp);
			if (error) {
				xfs_btree_del_cursor(cur);
				cur = NULL;
				xfs_trans_brelse(tp, agbp);
				agbp = NULL;
				/*
				 * Move up the the last inode in the current
				 * chunk.  The lookup_ge will always get
				 * us the first inode in the next chunk.
				 */
				agino += XFS_INODES_PER_CHUNK - 1;
				continue;
			}
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
		if (left) {
			error = xfs_inobt_increment(cur, 0, &tmp);
			if (error) {
				xfs_btree_del_cursor(cur);
				cur = NULL;
				xfs_trans_brelse(tp, agbp);
				agbp = NULL;
				/*
				 * The agino value has already been bumped.
				 * Just try to skip up to it.
				 */
				agino += XFS_INODES_PER_CHUNK;
				continue;
			}
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
int						/* error status */
xfs_fd_to_mp(
	int		fd,			/* file descriptor to convert */
	int		wperm,			/* need write perm on device */
	xfs_mount_t	**mpp)			/* return mount pointer */
{
	int		error;
	file_t		*fp;
	vfs_t		*vfsp;
	vnode_t		*vp;
	extern vfsops_t	xfs_vfsops;

	if (error = getf(fd, &fp))
		return XFS_ERROR(error);
	vp = fp->f_vnode;
	if (vp->v_type == VBLK || vp->v_type == VCHR) {
		if (wperm && !(fp->f_flag & FWRITE))
			return XFS_ERROR(EPERM);
		vfsp = vfs_devsearch(vp->v_rdev);
		if (vfsp == NULL)
			return XFS_ERROR(ENOTBLK);
	} else {
		if (!_CAP_ABLE(CAP_DEVICE_MGT))
			return XFS_ERROR(EPERM);
		vfsp = vp->v_vfsp;
	}
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

	if (error = xfs_fd_to_mp(fd, 0, &mp))
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
		if (count == 1)
			error = xfs_bulkstat_single(mp, &inlast, &count,
				ubuffer, &done);
		else
			error = xfs_bulkstat(mp, NULL, &inlast, &count, 
				(bulkstat_one_pf)xfs_bulkstat_one,
				sizeof(xfs_bstat_t), ubuffer, &done);
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
