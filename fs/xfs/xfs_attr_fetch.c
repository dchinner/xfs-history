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
#ident	"$Revision: 1.4 $"

#include <sys/types.h>
#include <sys/param.h>
#include <sys/vnode.h>
#include <sys/vfs.h>
#include <sys/errno.h>
#include <sys/debug.h>
#include <sys/cmn_err.h>
#include <sys/systm.h>
#include <sys/mac_label.h>
#include <sys/acl.h>
#include <sys/capability.h>
#include <sys/attributes.h>
#include <sys/kmem.h>
#include <sys/uuid.h>
#include "xfs_buf.h"
#include "xfs_macros.h"
#include "xfs_types.h"
#include "xfs_inum.h"
#include "xfs_log.h"
#include "xfs_trans.h"
#include "xfs_sb.h"
#include "xfs_ag.h"
#include "xfs_dir.h"
#include "xfs_dir2.h"
#include "xfs_mount.h"
#include "xfs_alloc_btree.h"
#include "xfs_bmap_btree.h"
#include "xfs_ialloc_btree.h"
#include "xfs_itable.h"
#include "xfs_btree.h"
#include "xfs_ialloc.h"
#include "xfs_alloc.h"
#include "xfs_bmap.h"
#include "xfs_attr_sf.h"
#include "xfs_dir_sf.h"
#include "xfs_dir2_sf.h"
#include "xfs_dinode.h"
#include "xfs_inode_item.h"
#include "xfs_inode.h"
#include "xfs_da_btree.h"
#include "xfs_attr.h"
#include "xfs_attr_leaf.h"

int
xfs_attr_fetch(xfs_inode_t *ip, char *name, char *value, int valuelen)
{
	xfs_da_args_t args;
	int error;

	/*
	 * Do the argument setup for the xfs_attr routines.
	 */
	bzero((char *)&args, sizeof(args));
	args.dp = ip;
	args.flags = ATTR_ROOT;
	args.whichfork = XFS_ATTR_FORK;
	args.name = name;
	args.namelen = strlen(name);
	args.value = value;
	args.valuelen = valuelen;
	args.hashval = xfs_da_hashname(args.name, args.namelen);
	args.oknoent = 1;

	/*
	 * Decide on what work routines to call based on the inode size.
	 */
	if (XFS_IFORK_Q(args.dp) == 0)
		error = ENOATTR;
	else if (args.dp->i_d.di_aformat == XFS_DINODE_FMT_LOCAL)
		error = xfs_attr_shortform_getvalue(&args);
	else if (xfs_bmap_one_block(args.dp, XFS_ATTR_FORK))
		error = xfs_attr_leaf_get(&args);
	else
		error = xfs_attr_node_get(&args);

	if (error == EEXIST)
		error = 0;

	return(error);
}
