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
 *
 * $Header: /ptools/plroot/slinx/2.4.0-test1-xfs/.RCS/PL/linux/fs/xfs/linux/RCS/xfs_globals.c,v 1.14 2000/06/15 03:01:24 nathans Exp $
 * $Author: nathans $
 * $Id: xfs_globals.c,v 1.14 2000/06/15 03:01:24 nathans Exp $
 *
 * Revision 1.12  2000/06/09 02:50:02  kenmcd
 * Updated copyright and license notices, ready for open source release
 * Merge of 2.3.99pre2-xfs:slinx:55821a by ananth.
 *
 */

/*
 * This file contains globals needed by XFS that were normally defined
 * somewhere else in IRIX.
 */

#include <xfs_os_defs.h>

#include <sys/types.h>
#include <sys/vfs.h>
#include <linux/xfs_cred.h>

int		mac_enabled = 0;
int		acl_enabled = 0;
int		ncsize = 792;
int		xpg4_sticky_dir = 1; /* see xfs_stickytest */
int		imon_enabled;
uint64_t	xfs_panic_mask;		/* set to cause more panics */
int		xfs_nfs_io_units = 10;	/* Ignore for now. Affects NFS performance. */
struct var {
	int	v_buf;	/* Nbr of I/O buffers.             */
	int	v_hbuf;	/* Nbr of hash buffers to allocate */
} v = { 512, 8 };

dev_t           rootdev = NODEV;
int             restricted_chown = 0;
int     	scache_linemask = 0x1f;       /* second level cache line size mask */
int		imon_enabled;
prid_t		dfltprid;
long            physmem;

#ifdef SIM
time_t		jiffies;
#endif

