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
#ifndef _FS_XFS_FSOPS_H
#define	_FS_XFS_FSOPS_H

#include <linux/xfs_fs.h>

#ident	"$Revision: 1.14 $"

/*
 * Minimum and maximum sizes need for growth checks
 */
#define	XFS_MIN_LOG_BLOCKS	512
#define	XFS_MIN_LOG_BYTES	(256 * 1024)
#define	XFS_MIN_AG_BLOCKS	64
#define	XFS_MAX_LOG_BLOCKS	(64 * 1024)
#define	XFS_MAX_LOG_BYTES	(128 * 1024 * 1024)

/*
 * Input and output structures
 */

/* Input for growfs data op */
typedef struct xfs_growfs_data
{
	__uint64_t	newblocks;	/* new data subvol size, fsblocks */
	__uint32_t	imaxpct;	/* new inode space percentage limit */
} xfs_growfs_data_t;

/* Input for growfs log op */
typedef struct xfs_growfs_log
{
	__uint32_t	newblocks;	/* new log size, fsblocks */
	__int32_t	isint;		/* 1 if new log is internal */
} xfs_growfs_log_t;

/* Input for growfs rt op */
typedef struct xfs_growfs_rt
{
	__uint64_t	newblocks;	/* new realtime size, fsblocks */
	__uint32_t	extsize;	/* new realtime extent size, fsblocks */
} xfs_growfs_rt_t;

#ifdef _KERNEL

int
xfs_fs_geometry(
	xfs_mount_t		*mp,
	xfs_fsop_geom_t 	*geo,
	int			new_version);

int
xfs_fs_counts(
	xfs_mount_t		*mp,
	xfs_fsop_counts_t	*cnt);

int
xfs_reserve_blocks(
	xfs_mount_t *mp, 
	__uint64_t *inval, 
	xfs_fsop_resblks_t *outval);

        
#endif	/* _KERNEL */

#endif	/* _FS_XFS_FSOPS_H */
