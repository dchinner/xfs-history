
/*
 * Copyright (C) 1999 Silicon Graphics, Inc.  All Rights Reserved.
 * 
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as published
 * by the Free Software Fondation.
 * 
 * This program is distributed in the hope that it would be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  Further, any license provided herein,
 * whether implied or otherwise, is limited to this program in accordance with
 * the express provisions of the GNU General Public License.  Patent licenses,
 * if any, provided herein do not apply to combinations of this program with
 * other product or programs, or any other product whatsoever.  This program is
 * distributed without any warranty that the program is delivered free of the
 * rightful claim of any third person by way of infringement or the like.  See
 * the GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write the Free Software Foundation, Inc., 59 Temple
 * Place - Suite 330, Boston MA 02111-1307, USA.
 */
#ident "$Revision: 1.8 $"
#if defined(__linux__)
#include <xfs_linux.h>
#endif

#include <sys/types.h>
#include <sys/sema.h>
#include <sys/uuid.h>
#include <sys/vfs.h>
#include <sys/vnode.h>
#include "xfs_buf.h"

#include "xfs_macros.h"
#include "xfs_types.h"
#include "xfs_inum.h"
#include "xfs_log.h"
#include "xfs_trans.h"
#include "xfs_sb.h"
#include "xfs_dir.h"
#include "xfs_dir2.h"
#include "xfs_mount.h"
#include "xfs_bmap_btree.h"
#include "xfs_attr_sf.h"
#include "xfs_dir_sf.h"
#include "xfs_dir2_sf.h"
#include "xfs_dinode.h"
#include "xfs_inode.h"

/*
 * Return 32 bits of the Inode number from the xfs fid structure.
 */
unsigned int
xfs_get_fid_ino(struct fid *fidp)
{
	return ((xfs_fid_t *)fidp)->fid_ino;
}

/*
 * Return the generation number from the xfs fid structure.
 */
unsigned int
xfs_get_fid_gen(struct fid *fidp)
{
	return ((xfs_fid_t *)fidp)->fid_gen;
}

/*
 * Initialize the fid structure field with values for inode and generation
 * numbers. (Only works for 32-bit inode numbers!)
 */
void
xfs_set_fid_fields(struct fid *fidp, unsigned int ino, unsigned int gen)
{
	xfs_fid_t *xfsfid = (xfs_fid_t *) fidp;

	xfsfid->fid_len = sizeof(xfs_fid_t) - sizeof(xfsfid->fid_len);
	xfsfid->fid_pad = 0;
	xfsfid->fid_gen = gen;
	xfsfid->fid_ino = ino;
}

/*
 * Return the filesystem name from the XFS mount structure.
 */
char *
xfs_get_mount_fsname(struct vfs *vfsp)
{
	char *fsname;
	bhv_desc_t *bdp;

	bdp = bhv_lookup_unlocked(VFS_BHVHEAD(vfsp), &xfs_vfsops);

	if (bdp == NULL)
		return("Unknown");

	fsname = (char *)(XFS_BHVTOM(bdp))->m_fsname;

	return fsname;
}
