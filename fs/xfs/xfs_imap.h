
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
#ifndef _FS_XFS_IMAP_H
#define	_FS_XFS_IMAP_H

#ident "$Revision$"

struct xfs_mount;
struct xfs_trans;

/*
 * This is the structure passed to xfs_imap() to map
 * an inode number to its on disk location.
 */
typedef struct xfs_imap {
	daddr_t		im_blkno;	/* starting BB of inode chunk */
	uint		im_len;		/* length in BBs of inode chunk */
	xfs_agblock_t	im_agblkno;	/* logical block of inode chunk in ag */
	ushort		im_ioffset;	/* inode offset in block in "inodes" */
	ushort		im_boffset;	/* inode offset in block in bytes */
} xfs_imap_t;
	
int	xfs_imap(struct xfs_mount *, struct xfs_trans *, xfs_ino_t,
		 xfs_imap_t *, uint);

#endif	/* !_FS_XFS_IMAP_H */
