
/*
 * Copyright (C) 2000 Silicon Graphics, Inc.  All Rights Reserved.
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston MA 02111-1307, USA.
 */
/*
 *
 * $Header$
 * $Author$
 * $Id$
 *
 * $Log$
 * Revision 1.5  2000/03/25 00:36:28  cattelan
 * Merge of 2.3.42-xfs:slinx:44186a by ananth.
 *
 *   Copied GPL from slinx-xfs tree.
 *
 * Revision 1.5  2000/02/21 21:54:31  cattelan
 * Copied GPL from slinx-xfs tree.
 *
 * Revision 1.5  2000/02/21 03:16:16  kenmcd
 * Encumbrance review done.
 * Add copyright and license words consistent with GPL.
 * Refer to http://fsg.melbourne.sgi.com/reviews/ for details.
 *
 * Revision 1.4  2000/02/12 01:03:17  cattelan
 * new functions xfs_pb_getr xfs_pb_ngetr
 *
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
