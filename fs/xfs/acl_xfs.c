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
#ident	"$Revision: 1.5 $"

#include <sys/types.h>
#include <sys/param.h>
#include <sys/vnode.h>
#include <sys/errno.h>
#include <sys/debug.h>
#include <sys/cmn_err.h>
#include <sys/eag.h>
#include <sys/mac_label.h>
#include <sys/systm.h>
#include <sys/attributes.h>
#include <sys/capability.h>
#include <sys/acl.h>
#include <sys/uuid.h>
#include "xfs_buf.h"
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

int
acl_xfs_iaccess( xfs_inode_t *ip, mode_t mode, struct cred *cr )
{
	struct acl acl;

	/*
	 * If the file has no ACL return -1.
	 */
	if (xfs_attr_fetch(ip, SGI_ACL_FILE, (char *)&acl, sizeof(struct acl)))
		return -1;

	/*
	 * If the file has an empty ACL return -1.
	 */
	if (acl.acl_cnt == ACL_NOT_PRESENT)
		return -1;

	/*
	 * Synchronize ACL with mode bits
	 */
	acl_sync_mode(ip->i_d.di_mode, &acl);

	return acl_access(ip->i_d.di_uid, ip->i_d.di_gid, &acl, mode, cr);
}
