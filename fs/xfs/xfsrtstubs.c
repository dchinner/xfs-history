
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
#ident "$Revision: 1.5 $"
/*
 * xfsrt stubs
 */
#include <sys/types.h>
#include <sys/uuid.h>
#include <sys/vfs.h>
#include <sys/vnode.h>
#include "xfs_buf.h"
#include <ksys/behavior.h>
#include <xfs_types.h>
#include <xfs_inum.h>
#include <xfs_sb.h>
#include <xfs_log.h>
#include <xfs_trans.h>
#include <xfs_dir.h>
#include <xfs_mount.h>

extern int nopkg(void);

int xfs_rtallocate_extent(void) { return nopkg(); }
int xfs_rtfree_extent(void) { return nopkg(); }
int xfs_growfs_rt(void) { return nopkg(); }
int xfs_rtpick_extent(void) { return nopkg(); }

int
xfs_rtmount_init(xfs_mount_t *mp)
{
	if (mp->m_sb.sb_rblocks == 0)
		return 0;
	return nopkg();
}

int
xfs_rtmount_inodes(xfs_mount_t *mp)
{
	if (mp->m_sb.sb_rblocks == 0)
		return 0;
	return nopkg();
}
