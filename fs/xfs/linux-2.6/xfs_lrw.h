/*
 *
 * $Header: /ptools/plroot/pingu/slinx-xfs/kern/fs/xfs/linux/RCS/xfs_lrw.h,v 1.3 2000/02/08 04:47:02 mostek Exp $
 * $Author: mostek $
 * $Id: xfs_lrw.h,v 1.3 2000/02/08 04:47:02 mostek Exp $
 *
 * $Log: xfs_lrw.h,v $
 * Revision 1.3  2000/02/08 04:47:02  mostek
 * Fix warnings.
 *
 * Revision 1.1  1999/12/29 21:14:01  cattelan
 * new file
 *
 */


#ifndef _XFS_LRW_H
#define _XFS_LRW_H

#define XFS_IOMAP_READ_ENTER	3
/*
 * Maximum count of bmaps used by read and write paths.
 */
#define	XFS_MAX_RW_NBMAPS	4

extern int xfs_bmap(bhv_desc_t *,off_t,ssize_t,int,pb_bmap_t *,int *);
extern int xfs_iomap_read(xfs_iocore_t *,off_t,size_t,pb_bmap_t *,int *,struct pm *);
STATIC int xfs_iomap_write(xfs_iocore_t	*,off_t,size_t,pb_bmap_t *,int *,int,struct pm *);
STATIC int xfs_iomap_extra(xfs_iocore_t	*,off_t,size_t,pb_bmap_t *,int *,struct pm	*);
/*
 * Needed by xfs_rw.c
 */
void
xfs_rwlock(
	bhv_desc_t	*bdp,
	vrwlock_t	write_lock);

void
xfs_rwlockf(
	bhv_desc_t	*bdp,
	vrwlock_t	write_lock,
	int		flags);

void
xfs_rwunlock(
	bhv_desc_t	*bdp,
	vrwlock_t	write_lock);

void
xfs_rwunlockf(
	bhv_desc_t	*bdp,
	vrwlock_t	write_lock,
	int		flags);

page_buf_t *
xfs_pb_getr(
			int sleep, 
			xfs_mount_t *mp);
page_buf_t *
xfs_pb_ngetr(
			 int len, 
			 xfs_mount_t *mp);

void
xfs_pb_freer(
			 page_buf_t *bp); 
void
xfs_pb_nfreer(
			  page_buf_t *bp);

#define XFS_FSB_TO_DB_IO(io,fsb) \
		(((io)->io_flags & XFS_IOCORE_RT) ? \
		 XFS_FSB_TO_BB((io)->io_mount, (fsb)) : \
		 XFS_FSB_TO_DADDR((io)->io_mount, (fsb)))

#endif /* _XFS_LRW_H */
