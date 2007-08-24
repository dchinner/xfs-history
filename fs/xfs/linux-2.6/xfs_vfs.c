/*
 * Copyright (c) 2000-2005 Silicon Graphics, Inc.
 * All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write the Free Software Foundation,
 * Inc.,  51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */
#include "xfs.h"
#include "xfs_fs.h"
#include "xfs_inum.h"
#include "xfs_log.h"
#include "xfs_clnt.h"
#include "xfs_trans.h"
#include "xfs_sb.h"
#include "xfs_ag.h"
#include "xfs_dir2.h"
#include "xfs_imap.h"
#include "xfs_alloc.h"
#include "xfs_dmapi.h"
#include "xfs_mount.h"
#include "xfs_quota.h"

bhv_vfs_t *
vfs_allocate(
	struct super_block	*sb)
{
	struct bhv_vfs		*vfsp;

	vfsp = kmem_zalloc(sizeof(bhv_vfs_t), KM_SLEEP);

	vfsp->vfs_super = sb;
	sb->s_fs_info = vfsp;

	return vfsp;
}

bhv_vfs_t *
vfs_from_sb(
	struct super_block	*sb)
{
	return (bhv_vfs_t *)sb->s_fs_info;
}

void
vfs_deallocate(
	struct bhv_vfs		*vfsp)
{
	kmem_free(vfsp, sizeof(bhv_vfs_t));
}
