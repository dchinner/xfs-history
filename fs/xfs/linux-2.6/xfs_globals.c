
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
/*
 *
 * $Header$
 * $Author$
 * $Id$
 *
 * $Log$
 * Revision 1.8  2000/03/25 00:36:28  cattelan
 * Merge of 2.3.42-xfs:slinx:44186a by ananth.
 *
 *   Copied GPL from slinx-xfs tree.
 *
 * Revision 1.8  2000/02/21 21:54:31  cattelan
 * Copied GPL from slinx-xfs tree.
 *
 * Revision 1.8  2000/02/21 03:16:16  kenmcd
 * Encumbrance review done.
 * Add copyright and license words consistent with GPL.
 * Refer to http://fsg.melbourne.sgi.com/reviews/ for details.
 *
 * Revision 1.7  1999/11/30 23:26:23  lord
 * remove lbolt definition, using jiffies
 *
 * Revision 1.6  1999/09/29 17:55:19  lord
 * Fill in var structure for buffer cache sizes.
 *
 * Revision 1.5  1999/09/03 00:40:44  mostek
 * A bunch more (physmem, maxdmasz, lbolt, ...
 *
 * Revision 1.3  1999/09/02 22:24:14  cattelan
 * No Message Supplied
 *
 * Revision 1.1  1999/09/01 00:35:47  mostek
 * Add some globals that came from systune. Systune doesn't exist
 * on Linux.
 *
 */

/*
 * This file contains globals needed by XFS that were normally defined
 * somewhere else in IRIX.
 */

#include <sys/types.h>
#include <sys/var.h>
#include <sys/vfs.h>
#include <sys/sysinfo.h>
#include <sys/cred.h>

int    		xfs_refcache_percent = 100;
int		mac_enabled = 0;
int		acl_enabled = 0;
int		ncsize = 792;
int		nclrus = 0;
int		xpg4_sticky_dir = 1; /* see xfs_stickytest */
int		imon_enabled;
int		xfsd_pri = 0x5c;	/* priority of xfsd thread(s) */
uint64_t	xfs_panic_mask;		/* set to cause more panics */
int		xfs_nfs_io_units = 10;	/* Ignore for now. Affects NFS performance. */
vfssw_t		vfssw[2] = { { "xfs" } };	/* vfssw for XFS only */
struct var	v = {
		  512,	/* v_buf * Nbr of I/O buffers.                  */
		  8,	/* v_hbuf * Nbr of hash buffers to allocate.     */
		  /* v_maxdmaszi * Max dma unbroken dma transfer size. */
		};

struct syswait	syswait;
dev_t           rootdev = NODEV;
int             restricted_chown = 0;
int     	scache_linemask = 0x1f;       /* second level cache line size mask */
int		imon_enabled;
prid_t		dfltprid;
long            physmem;
int		maxdmasz = 0x401;	/* Obviously needs to be set dynamically */
#ifdef SIM
time_t		jiffies;
#endif
struct ksa	ksa;
struct ksa      *ksaptr = &ksa;        /* ptr to kernel system activities buf*/

