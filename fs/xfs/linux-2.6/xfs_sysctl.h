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

#ifndef __XFS_SYSCNTL_H__
#define __XFS_SYSCNTL_H__

#include <linux/sysctl.h>

/*
 * Tunable xfs parameters
 */

#define XFS_PARAM	2

typedef union xfs_param {
	struct {
		ulong	refcache_size;	/* Size of nfs refcache */
		ulong	refcache_purge;	/* # of entries to purge each time */
	} xfs_un;
	ulong data[XFS_PARAM];
} xfs_param_t;

enum
{
        XFS_REFCACHE_SIZE = 1,
        XFS_REFCACHE_PURGE = 2,
};

extern xfs_param_t	xfs_params;

void	xfs_sysctl_register(void);
void	xfs_sysctl_unregister(void);
int	xfs_refcache_resize_proc_handler(
		ctl_table *ctl, int write, struct file * filp,
		void *buffer, size_t *lenp);

#endif /* __XFS_SYSCNTL_H__ */
