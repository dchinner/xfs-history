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

/*
 * This file contains random functions which map IRIX stuff to a
 * Linux implemetation.  All the memory allocator mappings are
 * here, as are some procfs and ktrace routines.
 */

#include <xfs.h>
#include <linux/sysctl.h>
#include <linux/smp_lock.h>
#include <linux/time.h>
#include <linux/proc_fs.h>

/*
 * Tunable xfs parameters
 */

unsigned long xfs_min[XFS_PARAM] = {                     0,                      0 };
unsigned long xfs_max[XFS_PARAM] = { XFS_REFCACHE_SIZE_MAX,  XFS_REFCACHE_SIZE_MAX };

xfs_param_t xfs_params = {{ 128, 32 }};

static struct ctl_table_header *xfs_table_header;

/* proc handlers */

extern void xfs_refcache_resize(int xfs_refcache_new_size);

int
xfs_refcache_resize_proc_handler(ctl_table *ctl, int write, struct file * filp,
		       void *buffer, size_t *lenp)
{
	int	ret;
	int	*valp = ctl->data;
	int	xfs_refcache_new_size;
	int	xfs_refcache_old_size = *valp;

	ret = proc_dointvec_minmax(ctl, write, filp, buffer, lenp);
	xfs_refcache_new_size = *valp;

	if (write && xfs_refcache_new_size != xfs_refcache_old_size) {
		xfs_refcache_resize(xfs_refcache_new_size);
		/* Don't purge more than size of the cache */
		if (xfs_refcache_new_size < xfs_params.xfs_un.refcache_purge)
			xfs_params.xfs_un.refcache_purge = xfs_refcache_new_size;
	}

	return ret;
}

static ctl_table xfs_table[] = {
	{XFS_REFCACHE_SIZE, "refcache_size", &xfs_params.data[0],
	sizeof(int), 0644, NULL, &xfs_refcache_resize_proc_handler,
	&sysctl_intvec, NULL, &xfs_min[0], &xfs_max[0]},
	
	{XFS_REFCACHE_PURGE, "refcache_purge", &xfs_params.data[1],
	sizeof(int), 0644, NULL, &proc_doulongvec_minmax,
	&sysctl_intvec, NULL, &xfs_min[1], &xfs_params.xfs_un.refcache_size},
	
	{0}
};

static ctl_table xfs_dir_table[] = {
	{FS_XFS, "xfs", NULL, 0, 0555, xfs_table},
	{0}
};

static ctl_table xfs_root_table[] = {
	{CTL_FS, "fs",  NULL, 0, 0555, xfs_dir_table},
	{0}
};

void
xfs_sysctl_register(void)
{
	xfs_table_header = register_sysctl_table(xfs_root_table, 1);
}

void
xfs_sysctl_unregister(void)
{
	if (xfs_table_header)
		unregister_sysctl_table(xfs_table_header);
}
