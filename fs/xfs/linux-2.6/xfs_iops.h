
/*
 * Copyright (C) 1999 Silicon Graphics, Inc.  All Rights Reserved.
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
#ifndef XFS_LINUX_OPS_INODE_DOT_H
#define XFS_LINUX_OPS_INODE_DOT_H

extern struct inode_operations linvfs_file_inode_operations;
extern struct inode_operations linvfs_dir_inode_operations;
extern struct inode_operations linvfs_symlink_inode_operations;

extern int linvfs_revalidate(struct dentry *);

#endif  /*  XFS_LINUX_OPS_INODE_DOT_H  */


