
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
#ifndef XFS_LINUX_OPS_SUPER_DOT_H
#define XFS_LINUX_OPS_SUPER_DOT_H

void
linvfs_inode_attr_in(
	struct inode	*inode);
int
fs_dounmount(
        bhv_desc_t      *bdp,
        int             flags,
        vnode_t         *rootvp,
        cred_t          *cr);

#endif  /*  XFS_LINUX_OPS_SUPER_DOT_H  */


