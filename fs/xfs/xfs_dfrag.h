
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
#ifndef _FS_XFS_DFRAG_H
#define	_FS_XFS_DFRAG_H

#ident "$Id$"

/*
 * Structure passed to xfs_swapext
 */

typedef struct xfs_swapext
{
	__int64_t	sx_version;	/* version */	
	__int64_t	sx_fdtarget;	/* fd of target file */
	__int64_t	sx_fdtmp;	/* fd of tmp file */
	off64_t		sx_offset; 	/* offset into file */
	off64_t		sx_length; 	/* leng from offset */
	char		sx_pad[16];	/* pad space, unused */
	xfs_bstat_t	sx_stat;	/* stat of target b4 copy */
} xfs_swapext_t;

/* 
 * Version flag
 */
#define XFS_SX_VERSION		0


#ifdef _KERNEL
/*
 * Prototypes for visible xfs_dfrag.c routines.
 */

/*
 * Syssgi interface for xfs_swapext
 */
int	xfs_swapext(struct xfs_swapext *sx);

#endif	/* _KERNEL */

#endif	/* !_FS_XFS_DFRAG_H */
