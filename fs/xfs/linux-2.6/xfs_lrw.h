/*
 *
 * $Header: $
 * $Author: $
 * $Id: $
 *
 * $Log: $
 */


#ifndef _XFS_LRW_H
#define _XFS_LRW_H

#define XFS_IOMAP_READ_ENTER	3
/*
 * Maximum count of bmaps used by read and write paths.
 */
#define	XFS_MAX_RW_NBMAPS	4

extern int xfs_bmap(bhv_desc_t *,off_t,ssize_t,int,pb_bmap_t *,int *);
extern int xfs_iomap_read(xfs_iocore_t *,off_t,size_t,pb_bmap_t *,int *,struct pm *,int  *,unsigned int);
STATIC int xfs_iomap_write(xfs_iocore_t	*,off_t,size_t,pb_bmap_t *,int *,int,struct pm *);
STATIC int xfs_iomap_extra(xfs_iocore_t	*,off_t,size_t,pb_bmap_t *,int *,struct pm	*);
#endif /* _XFS_LRW_H */

#define XFS_FSB_TO_DB_IO(io,fsb) \
		(((io)->io_flags & XFS_IOCORE_RT) ? \
		 XFS_FSB_TO_BB((io)->io_mount, (fsb)) : \
		 XFS_FSB_TO_DADDR((io)->io_mount, (fsb)))
