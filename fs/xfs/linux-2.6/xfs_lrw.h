/*
 * Copyright (c) 2000 Silicon Graphics, Inc.  All Rights Reserved.
 * 
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 * 
 * This program is distributed in the hope that it would be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * 
 * Further, this software is distributed without any warranty that it is
 * free of the rightful claim of any third person regarding infringement
 * or the like.  Any license provided herein, whether implied or
 * otherwise, applies only to this software file.  Patent licenses, if
 * any, provided herein do not apply to combinations of this program with
 * other software, or any other product whatsoever.
 * 
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write the Free Software Foundation, Inc., 59
 * Temple Place - Suite 330, Boston MA 02111-1307, USA.
 * 
 * Contact information: Silicon Graphics, Inc., 1600 Amphitheatre Pkwy,
 * Mountain View, CA  94043, or:
 * 
 * http://www.sgi.com 
 * 
 * For further information regarding this notice, see: 
 * 
 * http://oss.sgi.com/projects/GenInfo/SGIGPLNoticeExplan/
 */
/*
 *
 * $Header$
 * $Author$
 * $Id$
 *
 * $Log$
 * Revision 1.8  2000/03/29 01:45:59  kenmcd
 * Updated copyright and license notices, ready for open source release
 *
 * Revision 1.7  2000/03/28 20:31:53  lord
 * Simplify bmap code, add support for NEW extents.
 *
 * Revision 1.6  2000/03/25 01:15:49  lord
 * use linux security mechanisms
 * Merge of 2.3.42-xfs:slinx:46379a by ananth.
 *
 * Revision 1.5  2000/03/25 00:36:28  cattelan
 * Merge of 2.3.42-xfs:slinx:44186a by ananth.
 *
 *   Copied GPL from slinx-xfs tree.
 *
 * Revision 1.6  2000/03/18 22:51:26  lord
 * use linux security mechanisms
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
extern int xfs_iomap_write(xfs_iocore_t	*,off_t,size_t,pb_bmap_t *,int *,int,struct pm *);

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

#if 0
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
#endif

#define XFS_FSB_TO_DB_IO(io,fsb) \
		(((io)->io_flags & XFS_IOCORE_RT) ? \
		 XFS_FSB_TO_BB((io)->io_mount, (fsb)) : \
		 XFS_FSB_TO_DADDR((io)->io_mount, (fsb)))

#endif /* _XFS_LRW_H */
