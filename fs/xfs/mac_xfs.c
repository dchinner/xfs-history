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
#ident	"$Revision: 1.7 $"

#include <sys/types.h>
#include <sys/param.h>
#include <sys/vnode.h>
#include <sys/debug.h>
#include <sys/mac_label.h>
#include <sys/systm.h>
#include <sys/attributes.h>
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

extern struct cred *sys_cred;
extern mac_label *mac_high_low_lp;

int
mac_xfs_iaccess( xfs_inode_t *ip, mode_t mode, struct cred *cr )
{
	struct mac_label mac;
	struct mac_label *mp = mac_high_low_lp;

	if (cr == NULL || sys_cred == NULL ) {
		return EACCES;
	}

	if (xfs_attr_fetch(ip, SGI_MAC_FILE, (char *)&mac,
			   sizeof(struct mac_label)) == 0) {
		if ((mp = mac_add_label(&mac)) == NULL) {
			return EACCES;
		}
	}

	return mac_access(mp, cr, mode);
}
