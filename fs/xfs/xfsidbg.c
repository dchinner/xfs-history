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
#ident	"$Revision$"

#include <xfs_os_defs.h>

#include <sys/types.h>
#include <linux/version.h>
#include <linux/module.h>
#include <linux/kdb.h>
#include <linux/kdbprivate.h>

#include <linux/xfs_to_linux.h>
#include <linux/mm.h>
#include <linux/linux_to_xfs.h>


#include <sys/vfs.h>
#include <sys/vnode.h>
#include <sys/attributes.h>
#include <sys/uuid.h>
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
#include "xfs_alloc.h"
#include "xfs_ag.h"
#include "xfs_alloc_btree.h"
#include "xfs_bmap_btree.h"
#include "xfs_ialloc_btree.h"
#include "xfs_btree.h"
#include "xfs_buf_item.h"
#include "xfs_extfree_item.h"
#include "xfs_inode_item.h"
#include "xfs_bmap.h"
#include "xfs_attr_sf.h"
#include "xfs_dir_sf.h"
#include "xfs_dir2_sf.h"
#include "xfs_dinode.h"
#include "xfs_inode.h"
#include "xfs_da_btree.h"
#include "xfs_attr.h"
#include "xfs_attr_leaf.h"
#include "xfs_dir_leaf.h"
#include "xfs_dir2_data.h"
#include "xfs_dir2_leaf.h"
#include "xfs_dir2_block.h"
#include "xfs_dir2_node.h"
#include "xfs_log_priv.h"
#include "xfs_log_recover.h"
#include "xfs_rw.h"
#include "xfs_bit.h"
#include "xfs_quota.h"
#include "xfs_dqblk.h"
#include "xfs_dquot_item.h"
#include "xfs_dquot.h"
#include "xfs_qm.h"
#include "xfs_quota_priv.h"

/*
 * External functions & data not in header files.
 */

static xfs_qm_t	*xfs_Gqm = NULL;

/*
 * Command table functions.
 */
static void	xfsidbg_xagf(xfs_agf_t *);
static void	xfsidbg_xagi(xfs_agi_t *);
static void	xfsidbg_xaildump(xfs_mount_t *);
static void	xfsidbg_xalloc(xfs_alloc_arg_t *);
static void	xfsidbg_xattrcontext(xfs_attr_list_context_t *);
static void	xfsidbg_xattrleaf(xfs_attr_leafblock_t *);
static void	xfsidbg_xattrsf(xfs_attr_shortform_t *);
static void 	xfsidbg_xbirec(xfs_bmbt_irec_t *r);
static void	xfsidbg_xbmalla(xfs_bmalloca_t *);
static void	xfsidbg_xbrec(xfs_bmbt_rec_64_t *);
static void	xfsidbg_xbroot(xfs_inode_t *);
static void	xfsidbg_xbroota(xfs_inode_t *);
static void	xfsidbg_xbtcur(xfs_btree_cur_t *);
static void	xfsidbg_xbuf(xfs_buf_t *);
static void	xfsidbg_xbuf_real(xfs_buf_t *, int);
static void	xfsidbg_xchash(xfs_mount_t *mp);
static void	xfsidbg_xchashlist(xfs_chashlist_t *chl);
static void	xfsidbg_xdaargs(xfs_da_args_t *);
static void	xfsidbg_xdabuf(xfs_dabuf_t *);
static void	xfsidbg_xdanode(xfs_da_intnode_t *);
static void	xfsidbg_xdastate(xfs_da_state_t *);
static void	xfsidbg_xdirleaf(xfs_dir_leafblock_t *);
static void	xfsidbg_xdirsf(xfs_dir_shortform_t *);
static void	xfsidbg_xdir2free(xfs_dir2_free_t *);
static void	xfsidbg_xdir2sf(xfs_dir2_sf_t *);
static void	xfsidbg_xexlist(xfs_inode_t *);
static void	xfsidbg_xflist(xfs_bmap_free_t *);
static void	xfsidbg_xgaplist(xfs_inode_t *);
static void	xfsidbg_xhelp(void);
static void 	xfsidbg_xiclog(xlog_in_core_t *);
static void	xfsidbg_xiclogall(xlog_in_core_t *);
static void	xfsidbg_xiclogcb(xlog_in_core_t *);
static void	xfsidbg_xihash(xfs_mount_t *mp);
static void	xfsidbg_xinodes(xfs_mount_t *);
static void	xfsidbg_xlog(xlog_t *);
static void	xfsidbg_xlog_ritem(xlog_recover_item_t *);
static void	xfsidbg_xlog_rtrans(xlog_recover_t *);
static void	xfsidbg_xlog_rtrans_entire(xlog_recover_t *);
static void	xfsidbg_xlog_tic(xlog_ticket_t *);
static void	xfsidbg_xlogitem(xfs_log_item_t *);
static void	xfsidbg_xmount(xfs_mount_t *);
static void 	xfsidbg_xnode(xfs_inode_t *ip);
static void 	xfsidbg_xcore(xfs_iocore_t *io);
static void	xfsidbg_xperag(xfs_mount_t *);
static void	xfsidbg_xqm(void);
static void	xfsidbg_xqm_diskdq(xfs_disk_dquot_t *);
static void	xfsidbg_xqm_dqattached_inos(xfs_mount_t *);
static void	xfsidbg_xqm_dquot(xfs_dquot_t *);
static void	xfsidbg_xqm_freelist_print(xfs_frlist_t *qlist, char *title);
static void	xfsidbg_xqm_freelist(void);
static void	xfsidbg_xqm_htab(void);
static void	xfsidbg_xqm_mplist(xfs_mount_t *);
static void	xfsidbg_xqm_qinfo(xfs_mount_t *mp);
static void	xfsidbg_xqm_tpdqinfo(xfs_trans_t *tp);
static void	xfsidbg_xsb(xfs_sb_t *);
static void	xfsidbg_xtp(xfs_trans_t *);
static void	xfsidbg_xtrans_res(xfs_mount_t *);

/* kdb wrappers */

static int	kdbm_xfs_xagf(
	int	argc,
	const char **argv,
	const char **envp,
	struct pt_regs *regs)
{
	unsigned long addr;
	int nextarg = 1;
	long offset = 0;
	int diag;

	if (argc != 1)
		return KDB_ARGCOUNT;
	diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL, regs);
	if (diag)
		return diag;

	xfsidbg_xagf((xfs_agf_t *)addr);
	return 0;
}

static int	kdbm_xfs_xagi(
	int	argc,
	const char **argv,
	const char **envp,
	struct pt_regs *regs)
{
	unsigned long addr;
	int nextarg = 1;
	long offset = 0;
	int diag;

	if (argc != 1)
		return KDB_ARGCOUNT;
	diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL, regs);
	if (diag)
		return diag;

	xfsidbg_xagi((xfs_agi_t *)addr);
	return 0;
}

static int	kdbm_xfs_xaildump(
	int	argc,
	const char **argv,
	const char **envp,
	struct pt_regs *regs)
{
	unsigned long addr;
	int nextarg = 1;
	long offset = 0;
	int diag;

	if (argc != 1)
		return KDB_ARGCOUNT;
	diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL, regs);
	if (diag)
		return diag;

	xfsidbg_xaildump((xfs_mount_t *) addr);
	return 0;
}

static int	kdbm_xfs_xalloc(
	int	argc,
	const char **argv,
	const char **envp,
	struct pt_regs *regs)
{
	unsigned long addr;
	int nextarg = 1;
	long offset = 0;
	int diag;

	if (argc != 1)
		return KDB_ARGCOUNT;
	diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL, regs);
	if (diag)
		return diag;

	xfsidbg_xalloc((xfs_alloc_arg_t *) addr);
	return 0;
}

static int	kdbm_xfs_xattrcontext(
	int	argc,
	const char **argv,
	const char **envp,
	struct pt_regs *regs)
{
	unsigned long addr;
	int nextarg = 1;
	long offset = 0;
	int diag;

	if (argc != 1)
		return KDB_ARGCOUNT;
	diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL, regs);
	if (diag)
		return diag;

	xfsidbg_xattrcontext((xfs_attr_list_context_t *) addr);
	return 0;
}

static int	kdbm_xfs_xattrleaf(
	int	argc,
	const char **argv,
	const char **envp,
	struct pt_regs *regs)
{
	unsigned long addr;
	int nextarg = 1;
	long offset = 0;
	int diag;

	if (argc != 1)
		return KDB_ARGCOUNT;
	diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL, regs);
	if (diag)
		return diag;

	xfsidbg_xattrleaf((struct xfs_attr_leafblock *) addr);
	return 0;
}

static int	kdbm_xfs_xattrsf(
	int	argc,
	const char **argv,
	const char **envp,
	struct pt_regs *regs)
{
	unsigned long addr;
	int nextarg = 1;
	long offset = 0;
	int diag;

	if (argc != 1)
		return KDB_ARGCOUNT;
	diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL, regs);
	if (diag)
		return diag;

	xfsidbg_xattrsf((struct xfs_attr_shortform *) addr);
	return 0;
}

static int 	kdbm_xfs_xbirec(
	int	argc,
	const char **argv,
	const char **envp,
	struct pt_regs *regs)
{
	unsigned long addr;
	int nextarg = 1;
	long offset = 0;
	int diag;

	if (argc != 1)
		return KDB_ARGCOUNT;
	diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL, regs);
	if (diag)
		return diag;

	xfsidbg_xbirec((xfs_bmbt_irec_t *) addr);
	return 0;
}

static int	kdbm_xfs_xbmalla(
	int	argc,
	const char **argv,
	const char **envp,
	struct pt_regs *regs)
{
	unsigned long addr;
	int nextarg = 1;
	long offset = 0;
	int diag;

	if (argc != 1)
		return KDB_ARGCOUNT;
	diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL, regs);
	if (diag)
		return diag;

	xfsidbg_xbmalla((xfs_bmalloca_t *)addr);
	return 0;
}

static int	kdbm_xfs_xbrec(
	int	argc,
	const char **argv,
	const char **envp,
	struct pt_regs *regs)
{
	unsigned long addr;
	int nextarg = 1;
	long offset = 0;
	int diag;

	if (argc != 1)
		return KDB_ARGCOUNT;
	diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL, regs);
	if (diag)
		return diag;

	xfsidbg_xbrec((xfs_bmbt_rec_64_t *) addr);
	return 0;
}

static int	kdbm_xfs_xbroot(
	int	argc,
	const char **argv,
	const char **envp,
	struct pt_regs *regs)
{
	unsigned long addr;
	int nextarg = 1;
	long offset = 0;
	int diag;

	if (argc != 1)
		return KDB_ARGCOUNT;
	diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL, regs);
	if (diag)
		return diag;

	xfsidbg_xbroot((xfs_inode_t *) addr);
	return 0;
}

static int	kdbm_xfs_xbroota(
	int	argc,
	const char **argv,
	const char **envp,
	struct pt_regs *regs)
{
	unsigned long addr;
	int nextarg = 1;
	long offset = 0;
	int diag;

	if (argc != 1)
		return KDB_ARGCOUNT;
	diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL, regs);
	if (diag)
		return diag;

	xfsidbg_xbroota((xfs_inode_t *) addr);
	return 0;
}

static int	kdbm_xfs_xbtcur(
	int	argc,
	const char **argv,
	const char **envp,
	struct pt_regs *regs)
{
	unsigned long addr;
	int nextarg = 1;
	long offset = 0;
	int diag;

	if (argc != 1)
		return KDB_ARGCOUNT;
	diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL, regs);
	if (diag)
		return diag;

	xfsidbg_xbtcur((xfs_btree_cur_t *) addr);
	return 0;
}

static int	kdbm_xfs_xbuf(
	int	argc,
	const char **argv,
	const char **envp,
	struct pt_regs *regs)
{
	unsigned long addr;
	int nextarg = 1;
	long offset = 0;
	int diag;

	if (argc != 1)
		return KDB_ARGCOUNT;
	diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL, regs);
	if (diag)
		return diag;

	xfsidbg_xbuf((xfs_buf_t *) addr);
	return 0;
}


static int	kdbm_xfs_xchash(
	int	argc,
	const char **argv,
	const char **envp,
	struct pt_regs *regs)
{
	unsigned long addr;
	int nextarg = 1;
	long offset = 0;
	int diag;

	if (argc != 1)
		return KDB_ARGCOUNT;
	diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL, regs);
	if (diag)
		return diag;

	xfsidbg_xchash((xfs_mount_t *) addr);
	return 0;
}

static int	kdbm_xfs_xchashlist(
	int	argc,
	const char **argv,
	const char **envp,
	struct pt_regs *regs)
{
	unsigned long addr;
	int nextarg = 1;
	long offset = 0;
	int diag;

	if (argc != 1)
		return KDB_ARGCOUNT;
	diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL, regs);
	if (diag)
		return diag;

	xfsidbg_xchashlist((xfs_chashlist_t *) addr);
	return 0;
}


static int	kdbm_xfs_xdaargs(
	int	argc,
	const char **argv,
	const char **envp,
	struct pt_regs *regs)
{
	unsigned long addr;
	int nextarg = 1;
	long offset = 0;
	int diag;

	if (argc != 1)
		return KDB_ARGCOUNT;
	diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL, regs);
	if (diag)
		return diag;

	xfsidbg_xdaargs((xfs_da_args_t *) addr);
	return 0;
}

static int	kdbm_xfs_xdabuf(
	int	argc,
	const char **argv,
	const char **envp,
	struct pt_regs *regs)
{
	unsigned long addr;
	int nextarg = 1;
	long offset = 0;
	int diag;

	if (argc != 1)
		return KDB_ARGCOUNT;
	diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL, regs);
	if (diag)
		return diag;

	xfsidbg_xdabuf((xfs_dabuf_t *) addr);
	return 0;
}

static int	kdbm_xfs_xdanode(
	int	argc,
	const char **argv,
	const char **envp,
	struct pt_regs *regs)
{
	unsigned long addr;
	int nextarg = 1;
	long offset = 0;
	int diag;

	if (argc != 1)
		return KDB_ARGCOUNT;
	diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL, regs);
	if (diag)
		return diag;

	xfsidbg_xdanode((xfs_da_intnode_t *) addr);
	return 0;
}

static int	kdbm_xfs_xdastate(
	int	argc,
	const char **argv,
	const char **envp,
	struct pt_regs *regs)
{
	unsigned long addr;
	int nextarg = 1;
	long offset = 0;
	int diag;

	if (argc != 1)
		return KDB_ARGCOUNT;
	diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL, regs);
	if (diag)
		return diag;

	xfsidbg_xdastate((xfs_da_state_t *) addr);
	return 0;
}

static int	kdbm_xfs_xdirleaf(
	int	argc,
	const char **argv,
	const char **envp,
	struct pt_regs *regs)
{
	unsigned long addr;
	int nextarg = 1;
	long offset = 0;
	int diag;

	if (argc != 1)
		return KDB_ARGCOUNT;
	diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL, regs);
	if (diag)
		return diag;

	xfsidbg_xdirleaf((xfs_dir_leafblock_t *) addr);
	return 0;
}

static int	kdbm_xfs_xdirsf(
	int	argc,
	const char **argv,
	const char **envp,
	struct pt_regs *regs)
{
	unsigned long addr;
	int nextarg = 1;
	long offset = 0;
	int diag;

	if (argc != 1)
		return KDB_ARGCOUNT;
	diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL, regs);
	if (diag)
		return diag;

	xfsidbg_xdirsf((xfs_dir_shortform_t *) addr);
	return 0;
}

static int	kdbm_xfs_xdir2free(
	int	argc,
	const char **argv,
	const char **envp,
	struct pt_regs *regs)
{
	unsigned long addr;
	int nextarg = 1;
	long offset = 0;
	int diag;

	if (argc != 1)
		return KDB_ARGCOUNT;
	diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL, regs);
	if (diag)
		return diag;

	xfsidbg_xdir2free((xfs_dir2_free_t *) addr);
	return 0;
}

static int	kdbm_xfs_xdir2sf(
	int	argc,
	const char **argv,
	const char **envp,
	struct pt_regs *regs)
{
	unsigned long addr;
	int nextarg = 1;
	long offset = 0;
	int diag;

	if (argc != 1)
		return KDB_ARGCOUNT;
	diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL, regs);
	if (diag)
		return diag;

	xfsidbg_xdir2sf((xfs_dir2_sf_t *) addr);
	return 0;
}

static int	kdbm_xfs_xexlist(
	int	argc,
	const char **argv,
	const char **envp,
	struct pt_regs *regs)
{
	unsigned long addr;
	int nextarg = 1;
	long offset = 0;
	int diag;

	if (argc != 1)
		return KDB_ARGCOUNT;
	diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL, regs);
	if (diag)
		return diag;

	xfsidbg_xexlist((xfs_inode_t *) addr);
	return 0;
}

static int	kdbm_xfs_xflist(
	int	argc,
	const char **argv,
	const char **envp,
	struct pt_regs *regs)
{
	unsigned long addr;
	int nextarg = 1;
	long offset = 0;
	int diag;

	if (argc != 1)
		return KDB_ARGCOUNT;
	diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL, regs);
	if (diag)
		return diag;

	xfsidbg_xflist((xfs_bmap_free_t *) addr);
	return 0;
}

static int	kdbm_xfs_xgaplist(
	int	argc,
	const char **argv,
	const char **envp,
	struct pt_regs *regs)
{
	unsigned long addr;
	int nextarg = 1;
	long offset = 0;
	int diag;

	if (argc != 1)
		return KDB_ARGCOUNT;
	diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL, regs);
	if (diag)
		return diag;

	xfsidbg_xgaplist((xfs_inode_t *) addr);
	return 0;
}

static int	kdbm_xfs_xhelp(
	int	argc,
	const char **argv,
	const char **envp,
	struct pt_regs *regs)
{
	if (argc != 0)
		return KDB_ARGCOUNT;

	xfsidbg_xhelp();
	return 0;
}

static int 	kdbm_xfs_xiclog(
	int	argc,
	const char **argv,
	const char **envp,
	struct pt_regs *regs)
{
	unsigned long addr;
	int nextarg = 1;
	long offset = 0;
	int diag;

	if (argc != 1)
		return KDB_ARGCOUNT;
	diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL, regs);
	if (diag)
		return diag;

	xfsidbg_xiclog((xlog_in_core_t *) addr);
	return 0;
}

static int	kdbm_xfs_xiclogall(
	int	argc,
	const char **argv,
	const char **envp,
	struct pt_regs *regs)
{
	unsigned long addr;
	int nextarg = 1;
	long offset = 0;
	int diag;

	if (argc != 1)
		return KDB_ARGCOUNT;
	diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL, regs);
	if (diag)
		return diag;

	xfsidbg_xiclogall((xlog_in_core_t *) addr);
	return 0;
}

static int	kdbm_xfs_xiclogcb(
	int	argc,
	const char **argv,
	const char **envp,
	struct pt_regs *regs)
{
	unsigned long addr;
	int nextarg = 1;
	long offset = 0;
	int diag;

	if (argc != 1)
		return KDB_ARGCOUNT;
	diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL, regs);
	if (diag)
		return diag;

	xfsidbg_xiclogcb((xlog_in_core_t *) addr);
	return 0;
}

static int	kdbm_xfs_xihash(
	int	argc,
	const char **argv,
	const char **envp,
	struct pt_regs *regs)
{
	unsigned long addr;
	int nextarg = 1;
	long offset = 0;
	int diag;

	if (argc != 1)
		return KDB_ARGCOUNT;
	diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL, regs);
	if (diag)
		return diag;

	xfsidbg_xihash((xfs_mount_t *) addr);
	return 0;
}

static int	kdbm_xfs_xinodes(
	int	argc,
	const char **argv,
	const char **envp,
	struct pt_regs *regs)
{
	unsigned long addr;
	int nextarg = 1;
	long offset = 0;
	int diag;

	if (argc != 1)
		return KDB_ARGCOUNT;
	diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL, regs);
	if (diag)
		return diag;

	xfsidbg_xinodes((xfs_mount_t *) addr);
	return 0;
}

static int	kdbm_xfs_xlog(
	int	argc,
	const char **argv,
	const char **envp,
	struct pt_regs *regs)
{
	unsigned long addr;
	int nextarg = 1;
	long offset = 0;
	int diag;

	if (argc != 1)
		return KDB_ARGCOUNT;
	diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL, regs);
	if (diag)
		return diag;

	xfsidbg_xlog((xlog_t *) addr);
	return 0;
}

static int	kdbm_xfs_xlog_ritem(
	int	argc,
	const char **argv,
	const char **envp,
	struct pt_regs *regs)
{
	unsigned long addr;
	int nextarg = 1;
	long offset = 0;
	int diag;

	if (argc != 1)
		return KDB_ARGCOUNT;
	diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL, regs);
	if (diag)
		return diag;

	xfsidbg_xlog_ritem((xlog_recover_item_t *) addr);
	return 0;
}

static int	kdbm_xfs_xlog_rtrans(
	int	argc,
	const char **argv,
	const char **envp,
	struct pt_regs *regs)
{
	unsigned long addr;
	int nextarg = 1;
	long offset = 0;
	int diag;

	if (argc != 1)
		return KDB_ARGCOUNT;
	diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL, regs);
	if (diag)
		return diag;

	xfsidbg_xlog_rtrans((xlog_recover_t *) addr);
	return 0;
}

static int	kdbm_xfs_xlog_rtrans_entire(
	int	argc,
	const char **argv,
	const char **envp,
	struct pt_regs *regs)
{
	unsigned long addr;
	int nextarg = 1;
	long offset = 0;
	int diag;

	if (argc != 1)
		return KDB_ARGCOUNT;
	diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL, regs);
	if (diag)
		return diag;

	xfsidbg_xlog_rtrans_entire((xlog_recover_t *) addr);
	return 0;
}

static int	kdbm_xfs_xlog_tic(
	int	argc,
	const char **argv,
	const char **envp,
	struct pt_regs *regs)
{
	unsigned long addr;
	int nextarg = 1;
	long offset = 0;
	int diag;

	if (argc != 1)
		return KDB_ARGCOUNT;
	diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL, regs);
	if (diag)
		return diag;

	xfsidbg_xlog_tic((xlog_ticket_t *) addr);
	return 0;
}

static int	kdbm_xfs_xlogitem(
	int	argc,
	const char **argv,
	const char **envp,
	struct pt_regs *regs)
{
	unsigned long addr;
	int nextarg = 1;
	long offset = 0;
	int diag;

	if (argc != 1)
		return KDB_ARGCOUNT;
	diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL, regs);
	if (diag)
		return diag;

	xfsidbg_xlogitem((xfs_log_item_t *) addr);
	return 0;
}

static int	kdbm_xfs_xmount(
	int	argc,
	const char **argv,
	const char **envp,
	struct pt_regs *regs)
{
	unsigned long addr;
	int nextarg = 1;
	long offset = 0;
	int diag;

	if (argc != 1)
		return KDB_ARGCOUNT;
	diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL, regs);
	if (diag)
		return diag;

	xfsidbg_xmount((xfs_mount_t *) addr);
	return 0;
}

static int 	kdbm_xfs_xnode(
	int	argc,
	const char **argv,
	const char **envp,
	struct pt_regs *regs)
{
	unsigned long addr;
	int nextarg = 1;
	long offset = 0;
	int diag;

	if (argc != 1)
		return KDB_ARGCOUNT;
	diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL, regs);
	if (diag)
		return diag;

	xfsidbg_xnode((xfs_inode_t *) addr);
	return 0;
}

static int	kdbm_xfs_xcore(
	int	argc,
	const char **argv,
	const char **envp,
	struct pt_regs *regs)
{
	unsigned long addr;
	int nextarg = 1;
	long offset = 0;
	int diag;

	if (argc != 1)
		return KDB_ARGCOUNT;
	diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL, regs);
	if (diag)
		return diag;

	xfsidbg_xcore((xfs_iocore_t *) addr);
	return 0;
}

static int	kdbm_xfs_xperag(
	int	argc,
	const char **argv,
	const char **envp,
	struct pt_regs *regs)
{
	unsigned long addr;
	int nextarg = 1;
	long offset = 0;
	int diag;

	if (argc != 1)
		return KDB_ARGCOUNT;
	diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL, regs);
	if (diag)
		return diag;

	xfsidbg_xperag((xfs_mount_t *) addr);
	return 0;
}

static int	kdbm_xfs_xqm(
	int	argc,
	const char **argv,
	const char **envp,
	struct pt_regs *regs)
{
	if (argc != 0)
		return KDB_ARGCOUNT;

	xfsidbg_xqm();
	return 0;
}

static int	kdbm_xfs_xqm_diskdq(
	int	argc,
	const char **argv,
	const char **envp,
	struct pt_regs *regs)
{
	unsigned long addr;
	int nextarg = 1;
	long offset = 0;
	int diag;

	if (argc != 1)
		return KDB_ARGCOUNT;
	diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL, regs);
	if (diag)
		return diag;

	xfsidbg_xqm_diskdq((xfs_disk_dquot_t *) addr);
	return 0;
}

static int	kdbm_xfs_xqm_dqattached_inos(
	int	argc,
	const char **argv,
	const char **envp,
	struct pt_regs *regs)
{
	unsigned long addr;
	int nextarg = 1;
	long offset = 0;
	int diag;

	if (argc != 1)
		return KDB_ARGCOUNT;
	diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL, regs);
	if (diag)
		return diag;

	xfsidbg_xqm_dqattached_inos((xfs_mount_t *) addr);
	return 0;
}

static int	kdbm_xfs_xqm_dquot(
	int	argc,
	const char **argv,
	const char **envp,
	struct pt_regs *regs)
{
	unsigned long addr;
	int nextarg = 1;
	long offset = 0;
	int diag;

	if (argc != 1)
		return KDB_ARGCOUNT;
	diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL, regs);
	if (diag)
		return diag;

	xfsidbg_xqm_dquot((xfs_dquot_t *) addr);
	return 0;
}

static int	kdbm_xfs_xqm_freelist(
	int	argc,
	const char **argv,
	const char **envp,
	struct pt_regs *regs)
{
	if (argc != 0)
		return KDB_ARGCOUNT;

	xfsidbg_xqm_freelist();
	return 0;
}

static int	kdbm_xfs_xqm_htab(
	int	argc,
	const char **argv,
	const char **envp,
	struct pt_regs *regs)
{
	if (argc != 0)
		return KDB_ARGCOUNT;

	xfsidbg_xqm_htab();
	return 0;
}

static int	kdbm_xfs_xqm_mplist(
	int	argc,
	const char **argv,
	const char **envp,
	struct pt_regs *regs)
{
	unsigned long addr;
	int nextarg = 1;
	long offset = 0;
	int diag;

	if (argc != 1)
		return KDB_ARGCOUNT;
	diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL, regs);
	if (diag)
		return diag;

	xfsidbg_xqm_mplist((xfs_mount_t *) addr);
	return 0;
}

static int	kdbm_xfs_xqm_qinfo(
	int	argc,
	const char **argv,
	const char **envp,
	struct pt_regs *regs)
{
	unsigned long addr;
	int nextarg = 1;
	long offset = 0;
	int diag;

	if (argc != 1)
		return KDB_ARGCOUNT;
	diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL, regs);
	if (diag)
		return diag;

	xfsidbg_xqm_qinfo((xfs_mount_t *) addr);
	return 0;
}

static int	kdbm_xfs_xqm_tpdqinfo(
	int	argc,
	const char **argv,
	const char **envp,
	struct pt_regs *regs)
{
	unsigned long addr;
	int nextarg = 1;
	long offset = 0;
	int diag;

	if (argc != 1)
		return KDB_ARGCOUNT;
	diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL, regs);
	if (diag)
		return diag;

	xfsidbg_xqm_tpdqinfo((xfs_trans_t *) addr);
	return 0;
}

static int	kdbm_xfs_xsb(
	int	argc,
	const char **argv,
	const char **envp,
	struct pt_regs *regs)
{
	unsigned long addr;
	int nextarg = 1;
	long offset = 0;
	int diag;

	if (argc != 1)
		return KDB_ARGCOUNT;
	diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL, regs);
	if (diag)
		return diag;

	xfsidbg_xsb((xfs_sb_t *) addr);
	return 0;
}

static int	kdbm_xfs_xtp(
	int	argc,
	const char **argv,
	const char **envp,
	struct pt_regs *regs)
{
	unsigned long addr;
	int nextarg = 1;
	long offset = 0;
	int diag;

	if (argc != 1)
		return KDB_ARGCOUNT;
	diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL, regs);
	if (diag)
		return diag;

	xfsidbg_xtp((xfs_trans_t *) addr);
	return 0;
}

static int	kdbm_xfs_xtrans_res(
	int	argc,
	const char **argv,
	const char **envp,
	struct pt_regs *regs)
{
	unsigned long addr;
	int nextarg = 1;
	long offset = 0;
	int diag;

	if (argc != 1)
		return KDB_ARGCOUNT;
	diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL, regs);
	if (diag)
		return diag;

	xfsidbg_xtrans_res((xfs_mount_t *) addr);
	return 0;
}

static char *vnode_type[] = {
	"VNON", "VREG", "VDIR", "VBLK", "VLNK", "VFIFO", "VBAD", "VSOCK"
};

static int	kdbm_vnode(
	int	argc,
	const char **argv,
	const char **envp,
	struct pt_regs *regs)
{
	unsigned long addr;
	int nextarg = 1;
	long offset = 0;
	int diag;
	vnode_t		*vp;
	bhv_desc_t	*bh;
	char		*symname;

	if (argc != 1)
		return KDB_ARGCOUNT;
	diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL, regs);
	if (diag)
		return diag;

	vp = (vnode_t *)addr;
	printk("vnode: 0x%p v_count %d type %s\n", vp, vp->v_count,
		vnode_type[vp->v_type]);
	bh = vp->v_bh.bh_first;
	symname = kdbnearsym((unsigned int)bh->bd_ops);
	printk("   v_inode 0x%p v_bh->bh_first 0x%p pobj 0x%p ops %s\n",
		vp->v_inode, bh, bh->bd_pdata, symname ? symname : "???");
	return 0;
}





static struct xif {
	char	*name;
	int	(*func)(int, const char **, const char **, struct pt_regs *);
	char	*args;
	char	*help;
} xfsidbg_funcs[] = {
  {  "xagf",	kdbm_xfs_xagf,	"<agf>",
				"Dump XFS allocation group freespace" },
  {  "xagi",	kdbm_xfs_xagi,	"<agi>",
				"Dump XFS allocation group inode" },
  {  "xail",	kdbm_xfs_xaildump,	"<xfs_mount_t>",
				"Dump XFS AIL for a mountpoint" },
  {  "xalloc",	kdbm_xfs_xalloc,	"<xfs_alloc_arg_t>",
				"Dump XFS allocation args structure" },
  {  "xattrcx",	kdbm_xfs_xattrcontext,	"<xfs_attr_list_context_t>",
				"Dump XFS attr_list context struct"},
  {  "xattrlf",	kdbm_xfs_xattrleaf,	"<xfs_attr_leafblock_t>",
				"Dump XFS attribute leaf block"},
  {  "xattrsf",	kdbm_xfs_xattrsf,	"<xfs_attr_shortform_t>",
				"Dump XFS attribute shortform"},
  {  "xbirec",	kdbm_xfs_xbirec,	"<xfs_bmbt_irec_t",
				"Dump XFS bmap incore record"},
  {  "xbmalla",	kdbm_xfs_xbmalla,	"<xfs_bmalloca_t>",
				"Dump XFS bmalloc args structure"},
  {  "xbrec",	kdbm_xfs_xbrec,		"<xfs_bmbt_rec_64_t",
			 	"Dump XFS bmap record"},
  {  "xbroot",	kdbm_xfs_xbroot,	"<xfs_inode_t>",
			 	"Dump XFS bmap btree root (data)"},
  {  "xbroota",	kdbm_xfs_xbroota, 	"<xfs_inode_t>",
				"Dump XFS bmap btree root (attr)"},
  {  "xbtcur",	kdbm_xfs_xbtcur,	"<xfs_btree_cur_t>",
				"Dump XFS btree cursor"},
  {  "xbuf",	kdbm_xfs_xbuf,		"<xfs_buf_t>",
				"Dump XFS data from a buffer"},
  {  "xchash",	kdbm_xfs_xchash,	"<xfs_mount_t>",
				"Dump XFS cluster hash"},
  {  "xchlist",	kdbm_xfs_xchashlist,	"<xfs_chashlist_t>",
				"Dump XFS cluster hash list"},
  {  "xd2free",	kdbm_xfs_xdir2free,	"<xfs_dir2_free_t>",
				"Dump XFS directory v2 freemap"},
  {  "xdaargs",	kdbm_xfs_xdaargs,	"<xfs_da_args_t>",
				"Dump XFS dir/attr args structure"},
  {  "xdabuf",	kdbm_xfs_xdabuf,	"<xfs_dabuf_t>",
				"Dump XFS dir/attr buf structure"},
  {  "xdanode",	kdbm_xfs_xdanode,	"<xfs_da_intnode_t>",
				"Dump XFS dir/attr node block"},
  {  "xdastat",	kdbm_xfs_xdastate,	"<xfs_da_state_t>",
				"Dump XFS dir/attr state_blk struct"},
  {  "xdirlf",	kdbm_xfs_xdirleaf,	"<xfs_dir_leafblock_t>",
				"Dump XFS directory leaf block"},
  {  "xdirsf",	kdbm_xfs_xdirsf,	"<xfs_dir_shortform_t>",
				"Dump XFS directory shortform"},
  {  "xdir2sf",	kdbm_xfs_xdir2sf,	"<xfs_dir2_sf_t>",
				"Dump XFS directory v2 shortform"},
  {  "xdiskdq",	kdbm_xfs_xqm_diskdq,	"<xfs_disk_dquot_t>",
				"Dump XFS ondisk dquot (quota) struct"},
  {  "xdqatt",	kdbm_xfs_xqm_dqattached_inos,	"<xfs_mount_t>",
				 "All incore inodes with dquots"},
  {  "xdqinfo",	kdbm_xfs_xqm_tpdqinfo, 	"<xfs_trans_t>",
				"Dump dqinfo structure of a trans"},
  {  "xdquot",	kdbm_xfs_xqm_dquot,	"<xfs_dquot_t>",
				"Dump XFS dquot (quota) structure"},
  {  "xexlist",	kdbm_xfs_xexlist,	"<xfs_inode_t>",
				"Dump XFS bmap extents in inode"},
  {  "xflist",	kdbm_xfs_xflist,	"<xfs_bmap_free_t>",
				"Dump XFS to-be-freed extent list"},
  {  "xgaplst",	kdbm_xfs_xgaplist,	"<xfs_inode_t>",
				"Dump inode gap list"},
  {  "xhelp",	kdbm_xfs_xhelp,		"",
				"Print idbg-xfs help"},
  {  "xicall",	kdbm_xfs_xiclogall,	"<xlog_in_core_t>",
				"Dump All XFS in-core logs"},
  {  "xiclog",	kdbm_xfs_xiclog,	"<xlog_in_core_t>",
				"Dump XFS in-core log"},
  {  "xihash",	kdbm_xfs_xihash, 	"<xfs_mount_t>",
				"Dump XFS inode hash statistics"},
  {  "xinodes",	kdbm_xfs_xinodes,	"<xfs_mount_t>",
			 	"Dump XFS inodes per mount"},
  {  "xl_rcit",	kdbm_xfs_xlog_ritem,	"<xlog_recover_item_t>",
				"Dump XFS recovery item"},
  {  "xl_rctr",	kdbm_xfs_xlog_rtrans,	"<xlog_recover_t>",
				"Dump XFS recovery transaction"},
  {  "xl_rctr2",kdbm_xfs_xlog_rtrans_entire,	"<xlog_recover_t>",
				"Dump entire recovery transaction"},
  {  "xl_tic",	kdbm_xfs_xlog_tic,	"<xlog_ticket_t>",
				"Dump XFS log ticket"},
  {  "xlog",	kdbm_xfs_xlog,	"<xlog_t>",
				"Dump XFS log"},
  {  "xlogcb",	kdbm_xfs_xiclogcb,	"<xlog_in_core_t>",
				"Dump XFS in-core log callbacks"},
  {  "xlogitm",	kdbm_xfs_xlogitem,	"<xfs_log_item_t>",
				"Dump XFS log item structure"},
  {  "xmount",	kdbm_xfs_xmount,	"<xfs_mount_t>",
				"Dump XFS mount structure"},
  {  "xnode",	kdbm_xfs_xnode,		"<xfs_inode_t>",
				"Dump XFS inode"},
  {  "xiocore",	kdbm_xfs_xcore,		"<xfs_iocore_t>",
				"Dump XFS iocore"},
  {  "xperag",	kdbm_xfs_xperag,	"<xfs_mount_t>",
				"Dump XFS per-allocation group data"},
  {  "xqinfo",  kdbm_xfs_xqm_qinfo,	"<xfs_mount_t>",
				"Dump mount->m_quotainfo structure"},
  {  "xqm",	kdbm_xfs_xqm,		"",
				"Dump XFS quota manager structure"},
  {  "xqmfree",	kdbm_xfs_xqm_freelist,	"",
				"Dump XFS global freelist of dquots"},
  {  "xqmhtab",	kdbm_xfs_xqm_htab,	"",
				"Dump XFS hashtable of dquots"},
  {  "xqmplist",kdbm_xfs_xqm_mplist,	"<xfs_mount_t>",
				"Dump XFS all dquots of a f/s"},
  {  "xsb",	kdbm_xfs_xsb,		"<xfs_sb_t>",
				"Dump XFS superblock"},
  {  "xtp",	kdbm_xfs_xtp,		"<xfs_trans_t>",
				"Dump XFS transaction structure"},
  {  "xtrres",	kdbm_xfs_xtrans_res,	"<xfs_mount_t>",
				"Dump XFS reservation values"},
  {  "vnode",	kdbm_vnode,	"<vnode>", "Dump vnode"},
  {  0,		0,	0 }
};

void
printflags(register uint64_t flags,
        register char **strings,
        register char *name)
{
        register uint64_t mask = 1;

        if (name)
                printk("%s 0x%Lx <", name, flags);
        while (flags != 0 && *strings) {
                if (mask & flags) {
                        printk("%s ", *strings);
                        flags &= ~mask;
                }
                mask <<= 1;
                strings++;
        }
        if (name)
                printk("> ");
        return;
}


int
init_module(void)
{
	struct xif	*p;

	for (p = xfsidbg_funcs; p->name; p++)
		kdb_register(p->name, p->func, p->args, p->help, 0);
	return 0;
}

void
cleanup_module(void)
{
	struct xif	*p;

	for (p = xfsidbg_funcs; p->name; p++)
		kdb_unregister(p->name);
}

/*
 * Argument to xfs_alloc routines, for allocation type.
 */
static char *xfs_alloctype[] = {
	"any_ag", "first_ag", "start_ag", "this_ag",
	"start_bno", "near_bno", "this_bno"
};


/*
 * Prototypes for static functions.
 */
static void xfs_broot(xfs_inode_t *ip, xfs_ifork_t *f);
static void xfs_btalloc(xfs_alloc_block_t *bt, int bsz);
static void xfs_btbmap(xfs_bmbt_block_t *bt, int bsz);
static void xfs_btino(xfs_inobt_block_t *bt, int bsz);
static void xfs_buf_item_print(xfs_buf_log_item_t *blip, int summary);
static void xfs_convert_extent(xfs_bmbt_rec_64_t *rp, xfs_dfiloff_t *op,
			xfs_dfsbno_t *sp, xfs_dfilblks_t *cp, int *fp);
static void xfs_dastate_path(xfs_da_state_path_t *p);
static void xfs_dir2data(void *addr, int size);
static void xfs_dir2leaf(xfs_dir2_leaf_t *leaf, int size);
static void xfs_dquot_item_print(xfs_dq_logitem_t *lip, int summary);
static void xfs_efd_item_print(xfs_efd_log_item_t *efdp, int summary);
static void xfs_efi_item_print(xfs_efi_log_item_t *efip, int summary);
static char *xfs_fmtformat(xfs_dinode_fmt_t f);
static char *xfs_fmtfsblock(xfs_fsblock_t bno, xfs_mount_t *mp);
static char *xfs_fmtino(xfs_ino_t ino, xfs_mount_t *mp);
static char *xfs_fmtlsn(xfs_lsn_t *lsnp);
static char *xfs_fmtmode(int m);
static char *xfs_fmtsize(size_t i);
static char *xfs_fmtuuid(uuid_t *);
static void xfs_inode_item_print(xfs_inode_log_item_t *ilip, int summary);
static void xfs_inodebuf(xfs_buf_t *bp);
static void xfs_prdinode(xfs_dinode_t *di, int coreonly);
static void xfs_prdinode_core(xfs_dinode_core_t *dip);
static void xfs_qoff_item_print(xfs_qoff_logitem_t *lip, int summary);
static void xfs_xexlist_fork(xfs_inode_t *ip, int whichfork);
static void xfs_xnode_fork(char *name, xfs_ifork_t *f);

/*
 * Static functions.
 */


/*
 * Print an xfs in-inode bmap btree root.
 */
static void
xfs_broot(xfs_inode_t *ip, xfs_ifork_t *f)
{
	xfs_bmbt_block_t	*broot;
	int			format;
	int			i;
	xfs_bmbt_key_t		*kp;
	xfs_bmbt_ptr_t		*pp;

	format = f == &ip->i_df ? ip->i_d.di_format : ip->i_d.di_aformat;
	if ((f->if_flags & XFS_IFBROOT) == 0 ||
	    format != XFS_DINODE_FMT_BTREE) {
		printk("inode 0x%p not btree format\n", ip); 
		return;
	}
	broot = f->if_broot;
	printk("block @0x%p magic %x level %d numrecs %d\n",
		broot, broot->bb_magic, broot->bb_level, broot->bb_numrecs);
	kp = XFS_BMAP_BROOT_KEY_ADDR(broot, 1, f->if_broot_bytes);
	pp = XFS_BMAP_BROOT_PTR_ADDR(broot, 1, f->if_broot_bytes);
	for (i = 1; i <= broot->bb_numrecs; i++)
		printk("\t%d: startoff %Ld ptr %Lx %s\n",
			i, kp[i - 1].br_startoff, pp[i - 1],
			xfs_fmtfsblock(pp[i - 1], ip->i_mount));
}

/*
 * Print allocation btree block.
 */
static void
xfs_btalloc(xfs_alloc_block_t *bt, int bsz)
{
	int i;

	printk("magic 0x%x level %d numrecs %d leftsib 0x%x rightsib 0x%x\n",
		bt->bb_magic, bt->bb_level, bt->bb_numrecs,
		bt->bb_leftsib, bt->bb_rightsib);
	if (bt->bb_level == 0) {

		for (i = 1; i <= bt->bb_numrecs; i++) {
			xfs_alloc_rec_t *r;

			r = XFS_BTREE_REC_ADDR(bsz, xfs_alloc, bt, i, 0);
			printk("rec %d startblock 0x%x blockcount %d\n",
				i, r->ar_startblock, r->ar_blockcount);
		}
	} else {
		int mxr;

		mxr = XFS_BTREE_BLOCK_MAXRECS(bsz, xfs_alloc, 0);
		for (i = 1; i <= bt->bb_numrecs; i++) {
			xfs_alloc_key_t *k;
			xfs_alloc_ptr_t *p;

			k = XFS_BTREE_KEY_ADDR(bsz, xfs_alloc, bt, i, mxr);
			p = XFS_BTREE_PTR_ADDR(bsz, xfs_alloc, bt, i, mxr);
			printk("key %d startblock 0x%x blockcount %d ptr 0x%x\n",
				i, k->ar_startblock, k->ar_blockcount, *p);
		}
	}
}

/*
 * Print a bmap btree block.
 */
static void
xfs_btbmap(xfs_bmbt_block_t *bt, int bsz)
{
	int i;

	printk("magic 0x%x level %d numrecs %d leftsib %Lx ",
		bt->bb_magic, bt->bb_level, bt->bb_numrecs,
		bt->bb_leftsib);
	printk("rightsib %Lx\n", bt->bb_rightsib);
	if (bt->bb_level == 0) {

		for (i = 1; i <= bt->bb_numrecs; i++) {
			xfs_bmbt_rec_64_t *r;
			xfs_dfiloff_t o;
			xfs_dfsbno_t s;
			xfs_dfilblks_t c;
			int fl;

			r = (xfs_bmbt_rec_64_t *)XFS_BTREE_REC_ADDR(bsz,
				xfs_bmbt, bt, i, 0);
			xfs_convert_extent(r, &o, &s, &c, &fl);
			printk("rec %d startoff %Ld ", i, o);
			printk("startblock %Lx ", s);
			printk("blockcount %Ld flag %d\n", c, fl);
		}
	} else {
		int mxr;

		mxr = XFS_BTREE_BLOCK_MAXRECS(bsz, xfs_bmbt, 0);
		for (i = 1; i <= bt->bb_numrecs; i++) {
			xfs_bmbt_key_t *k;
			xfs_bmbt_ptr_t *p;

			k = XFS_BTREE_KEY_ADDR(bsz, xfs_bmbt, bt, i, mxr);
			p = XFS_BTREE_PTR_ADDR(bsz, xfs_bmbt, bt, i, mxr);
			printk("key %d startoff %Ld ",
				i, k->br_startoff);
			printk("ptr %Lx\n", *p);
		}
	}
}

/*
 * Print an inode btree block.
 */
static void
xfs_btino(xfs_inobt_block_t *bt, int bsz)
{
	int i;

	printk("magic 0x%x level %d numrecs %d leftsib 0x%x rightsib 0x%x\n",
		bt->bb_magic, bt->bb_level, bt->bb_numrecs,
		bt->bb_leftsib, bt->bb_rightsib);
	if (bt->bb_level == 0) {

		for (i = 1; i <= bt->bb_numrecs; i++) {
			xfs_inobt_rec_t *r;

			r = XFS_BTREE_REC_ADDR(bsz, xfs_inobt, bt, i, 0);
			printk("rec %d startino 0x%x freecount %d, free %Lx\n",
				i, r->ir_startino, r->ir_freecount,
				r->ir_free);
		}
	} else {
		int mxr;

		mxr = XFS_BTREE_BLOCK_MAXRECS(bsz, xfs_inobt, 0);
		for (i = 1; i <= bt->bb_numrecs; i++) {
			xfs_inobt_key_t *k;
			xfs_inobt_ptr_t *p;

			k = XFS_BTREE_KEY_ADDR(bsz, xfs_inobt, bt, i, mxr);
			p = XFS_BTREE_PTR_ADDR(bsz, xfs_inobt, bt, i, mxr);
			printk("key %d startino 0x%x ptr 0x%x\n",
				i, k->ir_startino, *p);
		}
	}
}

/*
 * Print a buf log item.
 */
static void
xfs_buf_item_print(xfs_buf_log_item_t *blip, int summary)
{
	static char *bli_flags[] = {
		"hold",		/* 0x1 */
		"dirty",	/* 0x2 */
		"stale",	/* 0x4 */
		"logged",	/* 0x8 */
		"ialloc",	/* 0x10 */
		0
		};
	static char *blf_flags[] = {
		"inode",	/* 0x1 */
		"cancel",	/* 0x2 */
		0
		};

	if (summary) {
		printk("buf 0x%p blkno 0x%Lx ", blip->bli_buf,
			     blip->bli_format.blf_blkno);
		printflags(blip->bli_flags, bli_flags, "flags:");
		printk("\n   ");
		xfsidbg_xbuf_real(blip->bli_buf, 1);
		return;
	}
	printk("buf 0x%p recur %d refcount %d flags:",
		blip->bli_buf, blip->bli_recur, blip->bli_refcount);
	printflags(blip->bli_flags, bli_flags, NULL);
	printk("\n");
	printk("size %d blkno 0x%Lx len 0x%x map size %d map 0x%p\n",
		blip->bli_format.blf_size, blip->bli_format.blf_blkno,
		(uint) blip->bli_format.blf_len, blip->bli_format.blf_map_size,
		&(blip->bli_format.blf_data_map[0]));
	printk("blf flags: ");
	printflags((uint)blip->bli_format.blf_flags, blf_flags, NULL);
#ifdef XFS_TRANS_DEBUG
	printk("orig 0x%x logged 0x%x",
		blip->bli_orig, blip->bli_logged);
#endif
	printk("\n");
}

/*
 * Convert an external extent descriptor to internal form.
 */
static void
xfs_convert_extent(xfs_bmbt_rec_64_t *rp, xfs_dfiloff_t *op, xfs_dfsbno_t *sp,
		   xfs_dfilblks_t *cp, int *fp)
{
	xfs_dfiloff_t o;
	xfs_dfsbno_t s;
	xfs_dfilblks_t c;
	int flag;

	flag = (int)((rp->l0) >> (64 - 1 ));
	o = ((xfs_fileoff_t)rp->l0 &
			   (((__uint64_t)1 << ( 64 - 1  )) - 1) ) >> 9;
	s = (((xfs_fsblock_t)rp->l0 & (((__uint64_t)1 << ( 9 )) - 1) ) << 43) | 
			   (((xfs_fsblock_t)rp->l1) >> 21);
	c = (xfs_filblks_t)(rp->l1 & (((__uint64_t)1 << ( 21 )) - 1) );
	*op = o;
	*sp = s;
	*cp = c;
	*fp = flag;
}


/*
 * Print an xfs_da_state_path structure.
 */
static void
xfs_dastate_path(xfs_da_state_path_t *p)
{
	int i;

	printk("active %d\n", p->active);
	for (i = 0; i < XFS_DA_NODE_MAXDEPTH; i++) {
#if XFS_BIG_FILES
		printk(" blk %d bp 0x%p blkno 0x%x",
#else
		printk(" blk %d bp 0x%p blkno 0x%Lx",
#endif
			i, p->blk[i].bp, p->blk[i].blkno);
		printk(" index %d hashval 0x%x ",
			p->blk[i].index, (uint_t)p->blk[i].hashval);
		switch(p->blk[i].magic) {
		case XFS_DA_NODE_MAGIC:		printk("NODE\n");	break;
		case XFS_DIR_LEAF_MAGIC:	printk("DIR\n");	break;
		case XFS_ATTR_LEAF_MAGIC:	printk("ATTR\n");	break;
		case XFS_DIR2_LEAFN_MAGIC:	printk("DIR2\n");	break;
		default:			printk("type ??\n");	break;
		}
	}
}


/*
 * Print an efd log item.
 */
static void
xfs_efd_item_print(xfs_efd_log_item_t *efdp, int summary)
{
	int		i;
	xfs_extent_t	*ep;

	if (summary) {
		printk("Extent Free Done: ID 0x%Lx nextents %d (at 0x%p)\n",
				efdp->efd_format.efd_efi_id,
				efdp->efd_format.efd_nextents, efdp);
		return;
	}
	printk("size %d nextents %d next extent %d efip 0x%p\n",
		efdp->efd_format.efd_size, efdp->efd_format.efd_nextents,
		efdp->efd_next_extent, efdp->efd_efip);
	printk("efi_id 0x%Lx\n", efdp->efd_format.efd_efi_id);
	printk("efd extents:\n");
	ep = &(efdp->efd_format.efd_extents[0]);
	for (i = 0; i < efdp->efd_next_extent; i++, ep++) {
		printk("    block %Lx len %d\n",
			ep->ext_start, ep->ext_len);
	}
}

/*
 * Print an efi log item.
 */
static void
xfs_efi_item_print(xfs_efi_log_item_t *efip, int summary)
{
	int		i;
	xfs_extent_t	*ep;
	static char *efi_flags[] = {
		"recovered",	/* 0x1 */
		"committed",	/* 0x2 */
		"cancelled",	/* 0x4 */
		0,
		};

	if (summary) {
		printk("Extent Free Intention: ID 0x%Lx nextents %d (at 0x%p)\n",
				efip->efi_format.efi_id,
				efip->efi_format.efi_nextents, efip);
		return;
	}
	printk("size %d nextents %d next extent %d\n",
		efip->efi_format.efi_size, efip->efi_format.efi_nextents,
		efip->efi_next_extent);
	printk("id %Lx", efip->efi_format.efi_id);
	printflags(efip->efi_flags, efi_flags, "flags :");
	printk("\n");
	printk("efi extents:\n");
	ep = &(efip->efi_format.efi_extents[0]);
	for (i = 0; i < efip->efi_next_extent; i++, ep++) {
		printk("    block %Lx len %d\n",
			ep->ext_start, ep->ext_len);
	}
}

/*
 * Format inode "format" into a static buffer & return it.
 */
static char *
xfs_fmtformat(xfs_dinode_fmt_t f)
{
	static char *t[] = {
		"dev",
		"local",
		"extents",
		"btree",
		"uuid"
	};

	return t[f];
}

/*
 * Format fsblock number into a static buffer & return it.
 */
static char *
xfs_fmtfsblock(xfs_fsblock_t bno, xfs_mount_t *mp)
{
	static char rval[50];

	if (bno == NULLFSBLOCK)
		sprintf(rval, "NULLFSBLOCK");
	else if (ISNULLSTARTBLOCK(bno))
		sprintf(rval, "NULLSTARTBLOCK(%Ld)", STARTBLOCKVAL(bno));
	else if (mp)
		sprintf(rval, "%Ld[%x:%x]", (xfs_dfsbno_t)bno,
			XFS_FSB_TO_AGNO(mp, bno), XFS_FSB_TO_AGBNO(mp, bno));
	else
		sprintf(rval, "%Ld", (xfs_dfsbno_t)bno);
	return rval;
}

/*
 * Format inode number into a static buffer & return it.
 */
static char *
xfs_fmtino(xfs_ino_t ino, xfs_mount_t *mp)
{
	static char rval[50];

	if (mp)
		sprintf(rval, "%Ld[%x:%x:%x]", ino, XFS_INO_TO_AGNO(mp, ino),
			XFS_INO_TO_AGBNO(mp, ino), XFS_INO_TO_OFFSET(mp, ino));
	else
		sprintf(rval, "%Ld", ino);
	return rval;
}

/*
 * Format an lsn for printing into a static buffer & return it.
 */
static char *
xfs_fmtlsn(xfs_lsn_t *lsnp)
{
	uint		*wordp;
	uint		*word2p;
	static char	buf[20];

	wordp = (uint *)lsnp;
	word2p = wordp++;
	sprintf(buf, "[%x:%x]", *wordp, *word2p);

	return buf;
}

/*
 * Format file mode into a static buffer & return it.
 */
static char *
xfs_fmtmode(int m)
{
	static char rval[16];

	sprintf(rval, "%c%c%c%c%c%c%c%c%c%c%c%c%c",
		"?fc?dxb?r?l?S?m?"[(m & IFMT) >> 12],
		m & ISUID ? 'u' : '-',
		m & ISGID ? 'g' : '-',
		m & ISVTX ? 'v' : '-',
		m & IREAD ? 'r' : '-',
		m & IWRITE ? 'w' : '-',
		m & IEXEC ? 'x' : '-',
		m & (IREAD >> 3) ? 'r' : '-',
		m & (IWRITE >> 3) ? 'w' : '-',
		m & (IEXEC >> 3) ? 'x' : '-',
		m & (IREAD >> 6) ? 'r' : '-',
		m & (IWRITE >> 6) ? 'w' : '-',
		m & (IEXEC >> 6) ? 'x' : '-');
	return rval;
}

/*
 * Format a size into a static buffer & return it.
 */
static char *
xfs_fmtsize(size_t i)
{
	static char rval[20];

	/* size_t is 32 bits in 32-bit kernel, 64 bits in 64-bit kernel */
	sprintf(rval, "0x%x", i);
	return rval;
}

/*
 * Format a uuid into a static buffer & return it.  This doesn't
 * use the formatted value, it probably should (see C library).
 */
static char *
xfs_fmtuuid(uuid_t *uu)
{
	static char rval[40];
	uint *ip = (uint *)uu;

	ASSERT(sizeof(*uu) == 16);
	sprintf(rval, "%32x:%32x:%32x:%32x", ip[0], ip[1], ip[2], ip[3]);
	return rval;
}

/*
 * Print an inode log item.
 */
static void
xfs_inode_item_print(xfs_inode_log_item_t *ilip, int summary)
{
	static char *ili_flags[] = {
		"hold",		/* 0x1 */
		"iolock excl",	/* 0x2 */
		"iolock shrd",	/* 0x4 */
		0
		};
	static char *ilf_fields[] = {
		"core",		/* 0x001 */
		"ddata",	/* 0x002 */
		"dexts",	/* 0x004 */
		"dbroot",	/* 0x008 */
		"dev",		/* 0x010 */
		"uuid",		/* 0x020 */
		"adata",	/* 0x040 */
		"aext",		/* 0x080 */
		"abroot",	/* 0x100 */
		0
		};

	if (summary) {
		printk("inode 0x%p logged %d ",
			ilip->ili_inode, ilip->ili_logged);
		printflags(ilip->ili_flags, ili_flags, "flags:");
		printflags(ilip->ili_format.ilf_fields, ilf_fields, "format:");
		printflags(ilip->ili_last_fields, ilf_fields, "lastfield:");
		printk("\n");
		return;
	}
	printk("inode 0x%p ino 0x%Lx logged %d flags: ",
		ilip->ili_inode, ilip->ili_format.ilf_ino, ilip->ili_logged);
	printflags(ilip->ili_flags, ili_flags, NULL);
	printk("\n");
	printk("ilock recur %d iolock recur %d ext buf 0x%p\n",
		ilip->ili_ilock_recur, ilip->ili_iolock_recur,
		ilip->ili_extents_buf);
#ifdef XFS_TRANS_DEBUG
	printk("root bytes %d root orig 0x%x\n",
		ilip->ili_root_size, ilip->ili_orig_root);
#endif
	printk("size %d fields: ", ilip->ili_format.ilf_size);
	printflags(ilip->ili_format.ilf_fields, ilf_fields, "formatfield");
	printk(" last fields: ");
	printflags(ilip->ili_last_fields, ilf_fields, "lastfield");
	printk("\n");
	printk("dsize %d, asize %d, rdev 0x%x\n",
		ilip->ili_format.ilf_dsize,
		ilip->ili_format.ilf_asize,
		ilip->ili_format.ilf_u.ilfu_rdev);
	printk("blkno 0x%Lx len 0x%x boffset 0x%x\n",
		ilip->ili_format.ilf_blkno,
		ilip->ili_format.ilf_len,
		ilip->ili_format.ilf_boffset);
}

/*
 * Print a dquot log item.
 */
/* ARGSUSED */
static void
xfs_dquot_item_print(xfs_dq_logitem_t *lip, int summary)
{
	printk("dquot 0x%p\n",
		lip->qli_dquot);

}

/*
 * Print a quotaoff log item.
 */
/* ARGSUSED */
static void
xfs_qoff_item_print(xfs_qoff_logitem_t *lip, int summary)
{
	printk("start qoff item 0x%p flags 0x%x\n",
		lip->qql_start_lip, lip->qql_format.qf_flags);

}

/*
 * Print buffer full of inodes.
 */
static void
xfs_inodebuf(xfs_buf_t *bp)
{
#ifndef __linux__
	xfs_dinode_t *di;
	int n, i;
	vfs_t *vfsp;
	bhv_desc_t *bdp;
	xfs_mount_t *mp;
	extern int  xfs_fstype;

	vfsp = vfs_devsearch_nolock(bp->b_edev, xfs_fstype);
	if (!vfsp)
		return;
	bdp = bhv_lookup_unlocked(VFS_BHVHEAD(vfsp), &xfs_vfsops);
	mp = XFS_BHVTOM(bdp);
	n = XFS_BUF_COUNT(bp) >> mp->m_sb.sb_inodelog;
	for (i = 0, di = (xfs_dinode_t *)XFS_BUF_PTR(bp);
	     i < n;
	     i++, di = (xfs_dinode_t *)((char *)di + mp->m_sb.sb_inodesize)) {
		xfs_prdinode(di, 0);
	}
#endif
}


/*
 * Print disk inode.
 */
static void
xfs_prdinode(xfs_dinode_t *di, int coreonly)
{
	xfs_prdinode_core(&di->di_core);
	if (!coreonly)
		printk("next_unlinked 0x%x u@0x%p\n", di->di_next_unlinked,
			&di->di_u);
}

/*
 * Print disk inode core.
 */
static void
xfs_prdinode_core(xfs_dinode_core_t *dip)
{
	static char *diflags[] = {
		"realtime",		/* XFS_DIFLAG_REALTIME */
		"prealloc",		/* XFS_DIFLAG_PREALLOC */
		NULL
	};

	printk("magic 0x%x mode 0%o (%s) version 0x%x format 0x%x (%s)\n",
		dip->di_magic, dip->di_mode, xfs_fmtmode(dip->di_mode),
		dip->di_version, dip->di_format,
		xfs_fmtformat((xfs_dinode_fmt_t)dip->di_format));
	printk("nlink 0x%x uid 0x%x gid 0x%x projid 0x%x\n",
		dip->di_nlink, dip->di_uid, dip->di_gid,
		(uint)dip->di_projid);
	printk("atime 0x%x:%x mtime 0x%x:%x ctime 0x%x:%x\n",
		dip->di_atime.t_sec, dip->di_atime.t_nsec,
		dip->di_mtime.t_sec, dip->di_mtime.t_nsec,
		dip->di_ctime.t_sec, dip->di_ctime.t_nsec);
	printk("size 0x%Lx ", dip->di_size);
	printk("nblocks %Ld extsize 0x%x nextents 0x%x anextents 0x%x\n",
		dip->di_nblocks, dip->di_extsize,
		dip->di_nextents, dip->di_anextents);
	printk("forkoff %d aformat 0x%x (%s) dmevmask 0x%x dmstate 0x%x ",
		dip->di_forkoff, dip->di_aformat,
		xfs_fmtformat((xfs_dinode_fmt_t)dip->di_aformat),
		dip->di_dmevmask, dip->di_dmstate);
	printflags(dip->di_flags, diflags, "flags");
	printk("gen 0x%x\n", dip->di_gen);
}

/*
 * Print xfs extent list for a fork.
 */
static void
xfs_xexlist_fork(xfs_inode_t *ip, int whichfork)
{
	int nextents, i;
	xfs_dfiloff_t o;
	xfs_dfsbno_t s;
	xfs_dfilblks_t c;
	int flag;
	xfs_ifork_t *ifp;

	ifp = XFS_IFORK_PTR(ip, whichfork);
	if (ifp->if_flags & XFS_IFEXTENTS) {
		nextents = ifp->if_bytes / sizeof(xfs_bmbt_rec_64_t);
		printk("inode 0x%p %cf extents 0x%p nextents 0x%x\n",
			ip, "da"[whichfork], ifp->if_u1.if_extents, nextents);
		for (i = 0; i < nextents; i++) {
			xfs_convert_extent(
				(xfs_bmbt_rec_64_t *)&ifp->if_u1.if_extents[i],
				&o, &s, &c, &flag);
			printk(
		"%d: startoff %Ld startblock %s blockcount %Ld flag %d\n",
				i, o, xfs_fmtfsblock(s, ip->i_mount), c, flag);
		}
	}
}

static void
xfs_xnode_fork(char *name, xfs_ifork_t *f)
{
	static char *tab_flags[] = {
		"inline",	/* XFS_IFINLINE */
		"extents",	/* XFS_IFEXTENTS */
		"broot",	/* XFS_IFBROOT */
		NULL
	};
	int *p;

	printk("%s fork", name);
	if (f == NULL) {
		printk(" empty\n");
		return;
	} else
		printk("\n");
	printk(" bytes %s ", xfs_fmtsize(f->if_bytes));
	printk("real_bytes %s lastex 0x%x u1:%s 0x%p\n",
		xfs_fmtsize(f->if_real_bytes), f->if_lastex,
		f->if_flags & XFS_IFINLINE ? "data" : "extents",
		f->if_flags & XFS_IFINLINE ?
			f->if_u1.if_data :
			(char *)f->if_u1.if_extents);
	printk(" broot 0x%p broot_bytes %s ext_max %d ",
		f->if_broot, xfs_fmtsize(f->if_broot_bytes), f->if_ext_max);
	printflags(f->if_flags, tab_flags, "flags");
	printk("\n");
	printk(" u2");
	for (p = (int *)&f->if_u2;
	     p < (int *)((char *)&f->if_u2 + XFS_INLINE_DATA);
	     p++)
		printk(" 0x%x", *p);
	printk("\n");
}

/*
 * Command-level xfs-idbg functions.
 */

/*
 * Print xfs allocation group freespace header.
 */
static void
xfsidbg_xagf(xfs_agf_t *agf)
{
	printk("magicnum 0x%x versionnum 0x%x seqno 0x%x length 0x%x\n",
		INT_GET(agf->agf_magicnum, ARCH_UNKNOWN),
		INT_GET(agf->agf_versionnum, ARCH_UNKNOWN),
		INT_GET(agf->agf_seqno, ARCH_UNKNOWN),
		INT_GET(agf->agf_length, ARCH_UNKNOWN));
	printk("roots b 0x%x c 0x%x levels b %d c %d\n",
		INT_GET(agf->agf_roots[XFS_BTNUM_BNO], ARCH_UNKNOWN),
		INT_GET(agf->agf_roots[XFS_BTNUM_CNT], ARCH_UNKNOWN),
		INT_GET(agf->agf_levels[XFS_BTNUM_BNO], ARCH_UNKNOWN),
		INT_GET(agf->agf_levels[XFS_BTNUM_CNT], ARCH_UNKNOWN));
	printk("flfirst %d fllast %d flcount %d freeblks %d longest %d\n",
		INT_GET(agf->agf_flfirst, ARCH_UNKNOWN),
		INT_GET(agf->agf_fllast, ARCH_UNKNOWN),
		INT_GET(agf->agf_flcount, ARCH_UNKNOWN),
		INT_GET(agf->agf_freeblks, ARCH_UNKNOWN),
		INT_GET(agf->agf_longest, ARCH_UNKNOWN));
}

/*
 * Print xfs allocation group inode header.
 */
static void
xfsidbg_xagi(xfs_agi_t *agi)
{
    	int	i;
	int	j;

	printk("magicnum 0x%x versionnum 0x%x seqno 0x%x length 0x%x\n",
		INT_GET(agi->agi_magicnum, ARCH_UNKNOWN),
		INT_GET(agi->agi_versionnum, ARCH_UNKNOWN),
		INT_GET(agi->agi_seqno, ARCH_UNKNOWN),
		INT_GET(agi->agi_length, ARCH_UNKNOWN));
	printk("count 0x%x root 0x%x level 0x%x\n",
		INT_GET(agi->agi_count, ARCH_UNKNOWN),
		INT_GET(agi->agi_root, ARCH_UNKNOWN),
		INT_GET(agi->agi_level, ARCH_UNKNOWN));
	printk("freecount 0x%x newino 0x%x dirino 0x%x\n",
		INT_GET(agi->agi_freecount, ARCH_UNKNOWN),
		INT_GET(agi->agi_newino, ARCH_UNKNOWN),
		INT_GET(agi->agi_dirino, ARCH_UNKNOWN));

	printk("unlinked buckets\n");
	for (i = 0; i < XFS_AGI_UNLINKED_BUCKETS; i++) {
		for (j = 0; j < 4; j++, i++) {
			printk("0x%08x ",
				INT_GET(agi->agi_unlinked[i], ARCH_UNKNOWN));
		}
		printk("\n");
	}
}


/*
 * Print an allocation argument structure for XFS.
 */
static void
xfsidbg_xalloc(xfs_alloc_arg_t *args)
{
	printk("tp 0x%p mp 0x%p agbp 0x%p pag 0x%p fsbno %s\n",
		args->tp, args->mp, args->agbp, args->pag,
		xfs_fmtfsblock(args->fsbno, args->mp));
	printk("agno 0x%x agbno 0x%x minlen 0x%x maxlen 0x%x mod 0x%x\n",
		args->agno, args->agbno, args->minlen, args->maxlen, args->mod);
	printk("prod 0x%x minleft 0x%x total 0x%x alignment 0x%x\n",
		args->prod, args->minleft, args->total, args->alignment);
	printk("minalignslop 0x%x len 0x%x type %s otype %s wasdel %d\n",
		args->minalignslop, args->len, xfs_alloctype[args->type],
		xfs_alloctype[args->otype], args->wasdel);
	printk("wasfromfl %d isfl %d userdata %d\n",
		args->wasfromfl, args->isfl, args->userdata);
}



/*
 * Print an attr_list() context structure.
 */
static void
xfsidbg_xattrcontext(struct xfs_attr_list_context *context)
{
	static char *attr_arg_flags[] = {
		"DONTFOLLOW",	/* 0x0001 */
		"?",		/* 0x0002 */
		"?",		/* 0x0004 */
		"?",		/* 0x0008 */
		"CREATE",	/* 0x0010 */
		"?",		/* 0x0020 */
		"?",		/* 0x0040 */
		"?",		/* 0x0080 */
		"?",		/* 0x0100 */
		"?",		/* 0x0200 */
		"?",		/* 0x0400 */
		"?",		/* 0x0800 */
		"KERNOTIME",	/* 0x1000 */
		NULL
	};

	printk("dp 0x%p, dupcnt %d, resynch %d",
		    context->dp, context->dupcnt, context->resynch);
	printflags((__psunsigned_t)context->flags, attr_arg_flags, ", flags");
	printk("\ncursor h/b/o 0x%x/0x%x/%d -- p/p/i 0x%x/0x%x/0x%x\n",
			  context->cursor->hashval, context->cursor->blkno,
			  context->cursor->offset, context->cursor->pad1,
			  context->cursor->pad2, context->cursor->initted);
	printk("alist 0x%p, bufsize 0x%x, count %d, firstu 0x%x\n",
		       context->alist, context->bufsize, context->count,
		       context->firstu);
}

/*
 * Print attribute leaf block.
 */
static void
xfsidbg_xattrleaf(struct xfs_attr_leafblock *leaf)
{
	struct xfs_attr_leaf_hdr *h;
	struct xfs_da_blkinfo *i;
	struct xfs_attr_leaf_map *m;
	struct xfs_attr_leaf_entry *e;
	struct xfs_attr_leaf_name_local *l;
	struct xfs_attr_leaf_name_remote *r;
	int j, k;

	h = &leaf->hdr;
	i = &h->info;
	printk("hdr info forw 0x%x back 0x%x magic 0x%x\n",
		i->forw, i->back, i->magic);
	printk("hdr count %d usedbytes %d firstused %d holes %d\n",
		h->count, h->usedbytes, h->firstused, h->holes);
	for (j = 0, m = h->freemap; j < XFS_ATTR_LEAF_MAPSIZE; j++, m++) {
		printk("hdr freemap %d base %d size %d\n",
			j, m->base, m->size);
	}
	for (j = 0, e = leaf->entries; j < h->count; j++, e++) {
		printk("[%2d] hash 0x%x nameidx %d flags 0x%x",
			j, e->hashval, e->nameidx, e->flags);
		if (e->flags & XFS_ATTR_LOCAL)
			printk("LOCAL ");
		if (e->flags & XFS_ATTR_ROOT)
			printk("ROOT ");
		if (e->flags & XFS_ATTR_INCOMPLETE)
			printk("INCOMPLETE ");
		k = ~(XFS_ATTR_LOCAL | XFS_ATTR_ROOT | XFS_ATTR_INCOMPLETE);
		if ((e->flags & k) != 0)
			printk("0x%x", e->flags & k);
		printk(">\n     name \"");
		if (e->flags & XFS_ATTR_LOCAL) {
			l = XFS_ATTR_LEAF_NAME_LOCAL(leaf, j);
			for (k = 0; k < l->namelen; k++)
				printk("%c", l->nameval[k]);
			printk("\"(%d) value \"", l->namelen);
			for (k = 0; (k < l->valuelen) && (k < 32); k++)
				printk("%c", l->nameval[l->namelen + k]);
			if (k == 32)
				printk("...");
			printk("\"(%d)\n", l->valuelen);
		} else {
			r = XFS_ATTR_LEAF_NAME_REMOTE(leaf, j);
			for (k = 0; k < r->namelen; k++)
				printk("%c", r->name[k]);
			printk("\"(%d) value blk 0x%x len %d\n",
				   r->namelen, r->valueblk, r->valuelen);
		}
	}
}

/*
 * Print a shortform attribute list.
 */
static void
xfsidbg_xattrsf(struct xfs_attr_shortform *s)
{
	struct xfs_attr_sf_hdr *sfh;
	struct xfs_attr_sf_entry *sfe;
	int i, j;

	sfh = &s->hdr;
	printk("hdr count %d\n", sfh->count);
	for (i = 0, sfe = s->list; i < sfh->count; i++) {
		printk("entry %d namelen %d name \"", i, sfe->namelen);
		for (j = 0; j < sfe->namelen; j++)
			printk("%c", sfe->nameval[j]);
		printk("\" valuelen %d value \"", sfe->valuelen);
		for (j = 0; (j < sfe->valuelen) && (j < 32); j++)
			printk("%c", sfe->nameval[sfe->namelen + j]);
		if (j == 32)
			printk("...");
		printk("\"\n");
		sfe = XFS_ATTR_SF_NEXTENTRY(sfe);
	}
}


/*
 * Print xfs bmap internal record
 */
static void
xfsidbg_xbirec(xfs_bmbt_irec_t *r)
{
	printk(
	"startoff %Ld startblock %Lx blockcount %Ld state %Ld\n",
		(__uint64_t)r->br_startoff,
		(__uint64_t)r->br_startblock,
		(__uint64_t)r->br_blockcount,
		(__uint64_t)r->br_state);
}


/*
 * Print a bmap alloc argument structure for XFS.
 */
static void
xfsidbg_xbmalla(xfs_bmalloca_t *a)
{
	printk("tp 0x%p ip 0x%p eof %d prevp 0x%p\n",
		a->tp, a->ip, a->eof, a->prevp);
	printk("gotp 0x%p firstblock %s alen %d total %d\n",
		a->gotp, xfs_fmtfsblock(a->firstblock, a->ip->i_mount),
		a->alen, a->total);
	printk("off %s wasdel %d userdata %d minlen %d\n",
		xfs_fmtfsblock(a->off, a->ip->i_mount), a->wasdel,
		a->userdata, a->minlen);
	printk("minleft %d low %d rval %s aeof %d\n",
		a->minleft, a->low, xfs_fmtfsblock(a->rval, a->ip->i_mount),
		a->aeof);
}


/*
 * Print xfs bmap record
 */
static void
xfsidbg_xbrec(xfs_bmbt_rec_64_t *r)
{
	xfs_dfiloff_t o;
	xfs_dfsbno_t s;
	xfs_dfilblks_t c;
	int flag;

	xfs_convert_extent(r, &o, &s, &c, &flag);
	printk("startoff %Ld startblock %Lx blockcount %Ld flag %d\n",
		o, s, c, flag);
}

/*
 * Print an xfs in-inode bmap btree root (data fork).
 */
static void
xfsidbg_xbroot(xfs_inode_t *ip)
{
	xfs_broot(ip, &ip->i_df);
}

/*
 * Print an xfs in-inode bmap btree root (attribute fork).
 */
static void
xfsidbg_xbroota(xfs_inode_t *ip)
{
	if (ip->i_afp)
		xfs_broot(ip, ip->i_afp);
}

/* 
 * Print xfs btree cursor.
 */
static void
xfsidbg_xbtcur(xfs_btree_cur_t *c)
{
	int l;

	printk("tp 0x%p mp 0x%p\n",
		c->bc_tp,
		c->bc_mp);
	if (c->bc_btnum == XFS_BTNUM_BMAP) {
		printk("rec.b ");
		xfsidbg_xbirec(&c->bc_rec.b);
	} else if (c->bc_btnum == XFS_BTNUM_INO) {
		printk("rec.i startino 0x%x freecount 0x%x free %Lx\n",
			c->bc_rec.i.ir_startino, c->bc_rec.i.ir_freecount,
			c->bc_rec.i.ir_free);
	} else {
		printk("rec.a startblock 0x%x blockcount 0x%x\n",
			c->bc_rec.a.ar_startblock,
			c->bc_rec.a.ar_blockcount);
	}
	printk("bufs");
	for (l = 0; l < c->bc_nlevels; l++)
		printk(" 0x%p", c->bc_bufs[l]);
	printk("\n");
	printk("ptrs");
	for (l = 0; l < c->bc_nlevels; l++)
		printk(" 0x%x", c->bc_ptrs[l]);
	printk("  ra");
	for (l = 0; l < c->bc_nlevels; l++)
		printk(" %d", c->bc_ra[l]);
	printk("\n");
	printk("nlevels %d btnum %s blocklog %d\n",
		c->bc_nlevels,
		c->bc_btnum == XFS_BTNUM_BNO ? "bno" :
		(c->bc_btnum == XFS_BTNUM_CNT ? "cnt" :
		 (c->bc_btnum == XFS_BTNUM_BMAP ? "bmap" : "ino")),
		c->bc_blocklog);
	if (c->bc_btnum == XFS_BTNUM_BMAP) {
		printk("private forksize 0x%x whichfork %d ip 0x%p flags %d\n",
			c->bc_private.b.forksize,
			c->bc_private.b.whichfork,
			c->bc_private.b.ip,
			c->bc_private.b.flags);
		printk("private firstblock %s flist 0x%p allocated 0x%x\n",
			xfs_fmtfsblock(c->bc_private.b.firstblock, c->bc_mp),
			c->bc_private.b.flist,
			c->bc_private.b.allocated);
	} else if (c->bc_btnum == XFS_BTNUM_INO) {
		printk("private agbp 0x%p agno 0x%x\n",
			c->bc_private.i.agbp,
			c->bc_private.i.agno);
	} else {
		printk("private agbp 0x%p agno 0x%x\n",
			c->bc_private.a.agbp,
			c->bc_private.a.agno);
	}
}	

/*
 * Figure out what kind of xfs block the buffer contains, 
 * and invoke a print routine.
 */
static void
xfsidbg_xbuf(xfs_buf_t *bp)
{
	xfsidbg_xbuf_real(bp, 0);
}

/*
 * Figure out what kind of xfs block the buffer contains, 
 * and invoke a print routine (if asked to).
 */
static void
xfsidbg_xbuf_real(xfs_buf_t *bp, int summary)
{
	void *d;
	xfs_agf_t *agf;
	xfs_agi_t *agi;
	xfs_sb_t *sb;
	xfs_alloc_block_t *bta;
	xfs_bmbt_block_t *btb;
	xfs_inobt_block_t *bti;
	xfs_attr_leafblock_t *aleaf;
	xfs_dir_leafblock_t *dleaf;
	xfs_da_intnode_t *node;
	xfs_dinode_t *di;
	xfs_disk_dquot_t *dqb;
	xfs_dir2_block_t *d2block;
	xfs_dir2_data_t *d2data;
	xfs_dir2_leaf_t *d2leaf;
	xfs_dir2_free_t *d2free;

	d = XFS_BUF_PTR(bp);
	if (INT_GET((agf = d)->agf_magicnum, ARCH_UNKNOWN) == XFS_AGF_MAGIC) {
		if (summary) {
			printk("freespace hdr for AG %d (at 0x%p)\n",
				INT_GET(agf->agf_seqno, ARCH_UNKNOWN), agf);
		} else {
			printk("buf 0x%p agf 0x%p\n", bp, agf);
			xfsidbg_xagf(agf);
		}
	} else if (INT_GET((agi = d)->agi_magicnum, ARCH_UNKNOWN) == XFS_AGI_MAGIC) {
		if (summary) {
			printk("Inode hdr for AG %d (at 0x%p)\n",
			       INT_GET(agi->agi_seqno, ARCH_UNKNOWN), agi);
		} else {
			printk("buf 0x%p agi 0x%p\n", bp, agi);
			xfsidbg_xagi(agi);
		}
	} else if ((bta = d)->bb_magic == XFS_ABTB_MAGIC) {
		if (summary) {
			printk("Alloc BNO Btree blk, level %d (at 0x%p)\n",
				       bta->bb_level, bta);
		} else {
			printk("buf 0x%p abtbno 0x%p\n", bp, bta);
			xfs_btalloc(bta, XFS_BUF_COUNT(bp));
		}
	} else if ((bta = d)->bb_magic == XFS_ABTC_MAGIC) {
		if (summary) {
			printk("Alloc COUNT Btree blk, level %d (at 0x%p)\n",
				       bta->bb_level, bta);
		} else {
			printk("buf 0x%p abtcnt 0x%p\n", bp, bta);
			xfs_btalloc(bta, XFS_BUF_COUNT(bp));
		}
	} else if ((btb = d)->bb_magic == XFS_BMAP_MAGIC) {
		if (summary) {
			printk("Bmap Btree blk, level %d (at 0x%p)\n",
				      btb->bb_level, btb);
		} else {
			printk("buf 0x%p bmapbt 0x%p\n", bp, btb);
			xfs_btbmap(btb, XFS_BUF_COUNT(bp));
		}
	} else if ((bti = d)->bb_magic == XFS_IBT_MAGIC) {
		if (summary) {
			printk("Inode Btree blk, level %d (at 0x%p)\n",
				       bti->bb_level, bti);
		} else {
			printk("buf 0x%p inobt 0x%p\n", bp, bti);
			xfs_btino(bti, XFS_BUF_COUNT(bp));
		}
	} else if ((aleaf = d)->hdr.info.magic == XFS_ATTR_LEAF_MAGIC) {
		if (summary) {
			printk("Attr Leaf, 1st hash 0x%x (at 0x%p)\n",
				      aleaf->entries[0].hashval, aleaf);
		} else {
			printk("buf 0x%p attr leaf 0x%p\n", bp, aleaf);
			xfsidbg_xattrleaf(aleaf);
		}
	} else if (INT_GET((dleaf = d)->hdr.info.magic, ARCH_UNKNOWN) == XFS_DIR_LEAF_MAGIC) {
		if (summary) {
			printk("Dir Leaf, 1st hash 0x%x (at 0x%p)\n",
				     dleaf->entries[0].hashval, dleaf);
		} else {
			printk("buf 0x%p dir leaf 0x%p\n", bp, dleaf);
			xfsidbg_xdirleaf(dleaf);
		}
	} else if (INT_GET((node = d)->hdr.info.magic, ARCH_UNKNOWN) == XFS_DA_NODE_MAGIC) {
		if (summary) {
			printk("Dir/Attr Node, level %d, 1st hash 0x%x (at 0x%p)\n",
			      node->hdr.level, node->btree[0].hashval, node);
		} else {
			printk("buf 0x%p dir/attr node 0x%p\n", bp, node);
			xfsidbg_xdanode(node);
		}
	} else if (INT_GET((di = d)->di_core.di_magic, ARCH_UNKNOWN) == XFS_DINODE_MAGIC) {
		if (summary) {
			printk("Disk Inode (at 0x%p)\n", di);
		} else {
			printk("buf 0x%p dinode 0x%p\n", bp, di);
			xfs_inodebuf(bp);
		}
	} else if (INT_GET((sb = d)->sb_magicnum, ARCH_UNKNOWN) == XFS_SB_MAGIC) {
		if (summary) {
			printk("Superblock (at 0x%p)\n", sb);
		} else {
			printk("buf 0x%p sb 0x%p\n", bp, sb);
			xfsidbg_xsb(sb);
		}
	} else if ((dqb = d)->d_magic == XFS_DQUOT_MAGIC) {
#define XFSIDBG_DQTYPESTR(d)     (((d)->d_flags & XFS_DQ_USER) ? "USR" : \
                                 (((d)->d_flags & XFS_DQ_PROJ) ? "PRJ" : "???"))

		printk("Quota blk starting ID [%d], type %s at 0x%p\n",
			dqb->d_id, XFSIDBG_DQTYPESTR(dqb), dqb);
		
	} else if (INT_GET((d2block = d)->hdr.magic, ARCH_UNKNOWN) == XFS_DIR2_BLOCK_MAGIC) {
		if (summary) {
			printk("Dir2 block (at 0x%p)\n", d2block);
		} else {
			printk("buf 0x%p dir2 block 0x%p\n", bp, d2block);
			xfs_dir2data((void *)d2block, XFS_BUF_COUNT(bp));
		}
	} else if (INT_GET((d2data = d)->hdr.magic, ARCH_UNKNOWN) == XFS_DIR2_DATA_MAGIC) {
		if (summary) {
			printk("Dir2 data (at 0x%p)\n", d2data);
		} else {
			printk("buf 0x%p dir2 data 0x%p\n", bp, d2data);
			xfs_dir2data((void *)d2data, XFS_BUF_COUNT(bp));
		}
	} else if (INT_GET((d2leaf = d)->hdr.info.magic, ARCH_UNKNOWN) == XFS_DIR2_LEAF1_MAGIC) {
		if (summary) {
			printk("Dir2 leaf(1) (at 0x%p)\n", d2leaf);
		} else {
			printk("buf 0x%p dir2 leaf 0x%p\n", bp, d2leaf);
			xfs_dir2leaf(d2leaf, XFS_BUF_COUNT(bp));
		}
	} else if (INT_GET(d2leaf->hdr.info.magic, ARCH_UNKNOWN) == XFS_DIR2_LEAFN_MAGIC) {
		if (summary) {
			printk("Dir2 leaf(n) (at 0x%p)\n", d2leaf);
		} else {
			printk("buf 0x%p dir2 leaf 0x%p\n", bp, d2leaf);
			xfs_dir2leaf(d2leaf, XFS_BUF_COUNT(bp));
		}
	} else if (INT_GET((d2free = d)->hdr.magic, ARCH_UNKNOWN) == XFS_DIR2_FREE_MAGIC) {
		if (summary) {
			printk("Dir2 free (at 0x%p)\n", d2free);
		} else {
			printk("buf 0x%p dir2 free 0x%p\n", bp, d2free);
			xfsidbg_xdir2free(d2free);
		}
	} else {
		printk("buf 0x%p unknown 0x%p\n", bp, d);
	}
}


/*
 * Print an xfs_da_args structure.
 */
static void
xfsidbg_xdaargs(xfs_da_args_t *n)
{
	char *ch;
	int i;

	printk(" name \"");
	for (i = 0; i < n->namelen; i++) {
		printk("%c", n->name[i]);
	}
	printk("\"(%d) value ", n->namelen);
	if (n->value) {
		printk("\"");
		ch = n->value;
		for (i = 0; (i < n->valuelen) && (i < 32); ch++, i++) {
			switch(*ch) {
			case '\n':	printk("\n");		break;
			case '\b':	printk("\b");		break;
			case '\t':	printk("\t");		break;
			default:	printk("%c", *ch);	break;
			}
		}
		if (i == 32)
			printk("...");
		printk("\"(%d)\n", n->valuelen);
	} else {
		printk("(NULL)(%d)\n", n->valuelen);
	}
	printk(" hashval 0x%x whichfork %d flags <",
		  (uint_t)n->hashval, n->whichfork);
	if (n->flags & ATTR_ROOT)
		printk("ROOT ");
	if (n->flags & ATTR_CREATE)
		printk("CREATE ");
	if (n->flags & ATTR_REPLACE)
		printk("REPLACE ");
	if (n->flags & XFS_ATTR_INCOMPLETE)
		printk("INCOMPLETE ");
	i = ~(ATTR_ROOT | ATTR_CREATE | ATTR_REPLACE | XFS_ATTR_INCOMPLETE);
	if ((n->flags & i) != 0)
		printk("0x%x", n->flags & i);
	printk(">\n");
	printk(" rename %d justcheck %d addname %d oknoent %d\n",
		  n->rename, n->justcheck, n->addname, n->oknoent);
	printk(" leaf: blkno %d index %d rmtblkno %d rmtblkcnt %d\n",
		  n->blkno, n->index, n->rmtblkno, n->rmtblkcnt);
	printk(" leaf2: blkno %d index %d rmtblkno %d rmtblkcnt %d\n",
		  n->blkno2, n->index2, n->rmtblkno2, n->rmtblkcnt2);
	printk(" inumber %Ld dp 0x%p firstblock 0x%p flist 0x%p\n",
		  n->inumber, n->dp, n->firstblock, n->flist);
	printk(" trans 0x%p total %d\n",
		  n->trans, n->total);
}

/*
 * Print a da buffer structure.
 */
static void
xfsidbg_xdabuf(xfs_dabuf_t *dabuf)
{
	int	i;

	printk("nbuf %d dirty %d bbcount %d data 0x%p bps",
		dabuf->nbuf, dabuf->dirty, dabuf->bbcount, dabuf->data);
	for (i = 0; i < dabuf->nbuf; i++)
		printk(" %d:0x%p", i, dabuf->bps[i]);
	printk("\n");
#ifdef XFS_DABUF_DEBUG
	printk(" ra 0x%x prev 0x%x next 0x%x dev 0x%x blkno 0x%x\n",
		dabuf->ra, dabuf->prev, dabuf->next, dabuf->dev, dabuf->blkno);
#endif
}

/*
 * Print a directory/attribute internal node block.
 */
static void
xfsidbg_xdanode(struct xfs_da_intnode *node)
{
	xfs_da_node_hdr_t *h;
	xfs_da_blkinfo_t *i;
	xfs_da_node_entry_t *e;
	int j;

	h = &node->hdr;
	i = &h->info;
	printk("hdr info forw 0x%x back 0x%x magic 0x%x\n",
		INT_GET(i->forw, ARCH_UNKNOWN), INT_GET(i->back, ARCH_UNKNOWN), INT_GET(i->magic, ARCH_UNKNOWN));
	printk("hdr count %d level %d\n",
		INT_GET(h->count, ARCH_UNKNOWN), INT_GET(h->level, ARCH_UNKNOWN));
	for (j = 0, e = node->btree; j < INT_GET(h->count, ARCH_UNKNOWN); j++, e++) {
		printk("btree %d hashval 0x%x before 0x%x\n",
			j, (uint_t)INT_GET(e->hashval, ARCH_UNKNOWN), INT_GET(e->before, ARCH_UNKNOWN));
	}
}

/*
 * Print an xfs_da_state_blk structure.
 */
static void
xfsidbg_xdastate(xfs_da_state_t *s)
{
	xfs_da_state_blk_t *eblk;

	printk("args 0x%p mp 0x%p blocksize %d inleaf %d\n",
		s->args, s->mp, s->blocksize, s->inleaf);
	if (s->args)
		xfsidbg_xdaargs(s->args);
	
	printk("path:  ");
	xfs_dastate_path(&s->path);

	printk("altpath:  ");
	xfs_dastate_path(&s->altpath);

	eblk = &s->extrablk;
	printk("extra: valid %d, after %d\n", s->extravalid, s->extraafter);
#if XFS_BIG_FILES
	printk(" bp 0x%p blkno 0x%x ", eblk->bp, eblk->blkno);
#else
	printk(" bp 0x%x blkno 0x%x ", eblk->bp, eblk->blkno);
#endif
	printk("index %d hashval 0x%x\n", eblk->index, (uint_t)eblk->hashval);
}

/*
 * Print a directory leaf block.
 */
static void
xfsidbg_xdirleaf(xfs_dir_leafblock_t *leaf)
{
	xfs_dir_leaf_hdr_t *h;
	xfs_da_blkinfo_t *i;
	xfs_dir_leaf_map_t *m;
	xfs_dir_leaf_entry_t *e;
	xfs_dir_leaf_name_t *n;
	int j, k;
	xfs_ino_t ino;

	h = &leaf->hdr;
	i = &h->info;
	printk("hdr info forw 0x%x back 0x%x magic 0x%x\n",
		INT_GET(i->forw, ARCH_UNKNOWN), INT_GET(i->back, ARCH_UNKNOWN), INT_GET(i->magic, ARCH_UNKNOWN));
	printk("hdr count %d namebytes %d firstused %d holes %d\n",
		INT_GET(h->count, ARCH_UNKNOWN), INT_GET(h->namebytes, ARCH_UNKNOWN), INT_GET(h->firstused, ARCH_UNKNOWN), h->holes);
	for (j = 0, m = h->freemap; j < XFS_DIR_LEAF_MAPSIZE; j++, m++) {
		printk("hdr freemap %d base %d size %d\n",
			j, INT_GET(m->base, ARCH_UNKNOWN), INT_GET(m->size, ARCH_UNKNOWN));
	}
	for (j = 0, e = leaf->entries; j < INT_GET(h->count, ARCH_UNKNOWN); j++, e++) {
		n = XFS_DIR_LEAF_NAMESTRUCT(leaf, INT_GET(e->nameidx, ARCH_UNKNOWN));
		XFS_DIR_SF_GET_DIRINO_ARCH(&n->inumber, &ino, ARCH_UNKNOWN);
		printk("leaf %d hashval 0x%x nameidx %d inumber %Ld ",
			j, (uint_t)INT_GET(e->hashval, ARCH_UNKNOWN), INT_GET(e->nameidx, ARCH_UNKNOWN), ino);
		printk("namelen %d name \"", e->namelen);
		for (k = 0; k < e->namelen; k++)
			printk("%c", n->name[k]);
		printk("\"\n");
	}
}

/*
 * Print a directory v2 data block, single or multiple.
 */
static void
xfs_dir2data(void *addr, int size)
{
	xfs_dir2_data_t *db;
	xfs_dir2_block_t *bb;
	xfs_dir2_data_hdr_t *h;
	xfs_dir2_data_free_t *m;
	xfs_dir2_data_entry_t *e;
	xfs_dir2_data_unused_t *u;
	xfs_dir2_leaf_entry_t *l;
	int j, k;
	char *p;
	char *t;
	xfs_dir2_block_tail_t *tail;

	db = (xfs_dir2_data_t *)addr;
	bb = (xfs_dir2_block_t *)addr;
	h = &db->hdr;
	printk("hdr magic 0x%x (%s)\nhdr bestfree", INT_GET(h->magic, ARCH_UNKNOWN),
		INT_GET(h->magic, ARCH_UNKNOWN) == XFS_DIR2_DATA_MAGIC ? "DATA" :
			(INT_GET(h->magic, ARCH_UNKNOWN) == XFS_DIR2_BLOCK_MAGIC ? "BLOCK" : ""));
	for (j = 0, m = h->bestfree; j < XFS_DIR2_DATA_FD_COUNT; j++, m++) {
		printk(" %d: 0x%x@0x%x", j, INT_GET(m->length, ARCH_UNKNOWN), INT_GET(m->offset, ARCH_UNKNOWN));
	}
	printk("\n");
	if (INT_GET(h->magic, ARCH_UNKNOWN) == XFS_DIR2_DATA_MAGIC)
		t = (char *)db + size;
	else {
		/* XFS_DIR2_BLOCK_TAIL_P */
		tail = (xfs_dir2_block_tail_t *)
		       ((char *)bb + size - sizeof(xfs_dir2_block_tail_t));
		l = XFS_DIR2_BLOCK_LEAF_P_ARCH(tail, ARCH_UNKNOWN);
		t = (char *)l;
	}
	for (p = (char *)(h + 1); p < t; ) {
		u = (xfs_dir2_data_unused_t *)p;
		if (u->freetag == XFS_DIR2_DATA_FREE_TAG) {
			printk("0x%x unused freetag 0x%x length 0x%x tag 0x%x\n",
				p - (char *)addr, INT_GET(u->freetag, ARCH_UNKNOWN), INT_GET(u->length, ARCH_UNKNOWN),
				*XFS_DIR2_DATA_UNUSED_TAG_P_ARCH(u, ARCH_UNKNOWN));
			p += INT_GET(u->length, ARCH_UNKNOWN);
			continue;
		}
		e = (xfs_dir2_data_entry_t *)p;
		printk("0x%x entry inumber %Ld namelen %d name \"",
			p - (char *)addr, INT_GET(e->inumber, ARCH_UNKNOWN), e->namelen);
		for (k = 0; k < e->namelen; k++)
			printk("%c", e->name[k]);
		printk("\" tag 0x%x\n", *XFS_DIR2_DATA_ENTRY_TAG_P(e));
		p += XFS_DIR2_DATA_ENTSIZE(e->namelen);
	}
	if (INT_GET(h->magic, ARCH_UNKNOWN) == XFS_DIR2_DATA_MAGIC)
		return;
	for (j = 0; j < tail->count; j++, l++) {
		printk("0x%x leaf %d hashval 0x%x address 0x%x (byte 0x%x)\n",
			(char *)l - (char *)addr, j,
			(uint_t)INT_GET(l->hashval, ARCH_UNKNOWN), INT_GET(l->address, ARCH_UNKNOWN),
			/* XFS_DIR2_DATAPTR_TO_BYTE */
			l->address << XFS_DIR2_DATA_ALIGN_LOG);
	}
	printk("0x%x tail count %d\n",
		(char *)tail - (char *)addr, INT_GET(tail->count, ARCH_UNKNOWN));
}

static void
xfs_dir2leaf(xfs_dir2_leaf_t *leaf, int size)
{
	xfs_dir2_leaf_hdr_t *h;
	xfs_da_blkinfo_t *i;
	xfs_dir2_leaf_entry_t *e;
	xfs_dir2_data_off_t *b;
	xfs_dir2_leaf_tail_t *t;
	int j;

	h = &leaf->hdr;
	i = &h->info;
	e = leaf->ents;
	printk("hdr info forw 0x%x back 0x%x magic 0x%x\n",
		INT_GET(i->forw, ARCH_UNKNOWN), INT_GET(i->back, ARCH_UNKNOWN), INT_GET(i->magic, ARCH_UNKNOWN));
	printk("hdr count %d stale %d\n", INT_GET(h->count, ARCH_UNKNOWN), INT_GET(h->stale, ARCH_UNKNOWN));
	for (j = 0; j < INT_GET(h->count, ARCH_UNKNOWN); j++, e++) {
		printk("0x%x ent %d hashval 0x%x address 0x%x (byte 0x%x)\n",
			(char *)e - (char *)leaf, j,
			(uint_t)INT_GET(e->hashval, ARCH_UNKNOWN), INT_GET(e->address, ARCH_UNKNOWN),
			/* XFS_DIR2_DATAPTR_TO_BYTE */
			INT_GET(e->address, ARCH_UNKNOWN) << XFS_DIR2_DATA_ALIGN_LOG);
	}
	if (INT_GET(i->magic, ARCH_UNKNOWN) == XFS_DIR2_LEAFN_MAGIC)
		return;
	/* XFS_DIR2_LEAF_TAIL_P */
	t = (xfs_dir2_leaf_tail_t *)((char *)leaf + size - sizeof(*t));
	b = XFS_DIR2_LEAF_BESTS_P_ARCH(t, ARCH_UNKNOWN);
	for (j = 0; j < INT_GET(t->bestcount, ARCH_UNKNOWN); j++, b++) {
		printk("0x%x best %d 0x%x\n",
			(char *)b - (char *)leaf, j, INT_GET(*b, ARCH_UNKNOWN));
	}
	printk("tail bestcount %d\n", INT_GET(t->bestcount, ARCH_UNKNOWN));
}

/*
 * Print a shortform directory.
 */
static void
xfsidbg_xdirsf(xfs_dir_shortform_t *s)
{
	xfs_dir_sf_hdr_t *sfh;
	xfs_dir_sf_entry_t *sfe;
	xfs_ino_t ino;
	int i, j;

	sfh = &s->hdr;
	XFS_DIR_SF_GET_DIRINO_ARCH(&sfh->parent, &ino, ARCH_UNKNOWN);
	printk("hdr parent %Ld", ino);
	printk(" count %d\n", sfh->count);
	for (i = 0, sfe = s->list; i < sfh->count; i++) {
		XFS_DIR_SF_GET_DIRINO_ARCH(&sfe->inumber, &ino, ARCH_UNKNOWN);
		printk("entry %d inumber %Ld", i, ino);
		printk(" namelen %d name \"", sfe->namelen);
		for (j = 0; j < sfe->namelen; j++)
			printk("%c", sfe->name[j]);
		printk("\"\n");
		sfe = XFS_DIR_SF_NEXTENTRY(sfe);
	}
}

/*
 * Print a shortform v2 directory.
 */
static void
xfsidbg_xdir2sf(xfs_dir2_sf_t *s)
{
	xfs_dir2_sf_hdr_t *sfh;
	xfs_dir2_sf_entry_t *sfe;
	xfs_ino_t ino;
	int i, j;

	sfh = &s->hdr;
	ino = XFS_DIR2_SF_GET_INUMBER_ARCH(s, &sfh->parent, ARCH_UNKNOWN);
	printk("hdr count %d i8count %d parent %Ld\n",
		sfh->count, sfh->i8count, ino);
	for (i = 0, sfe = XFS_DIR2_SF_FIRSTENTRY(s); i < sfh->count; i++) {
		ino = XFS_DIR2_SF_GET_INUMBER_ARCH(s, XFS_DIR2_SF_INUMBERP(sfe), ARCH_UNKNOWN);
		printk("entry %d inumber %Ld offset 0x%x namelen %d name \"",
			i, ino, XFS_DIR2_SF_GET_OFFSET_ARCH(sfe, ARCH_UNKNOWN), sfe->namelen);
		for (j = 0; j < sfe->namelen; j++)
			printk("%c", sfe->name[j]);
		printk("\"\n");
		sfe = XFS_DIR2_SF_NEXTENTRY(s, sfe);
	}
}

/*
 * Print a node-form v2 directory freemap block.
 */
static void
xfsidbg_xdir2free(xfs_dir2_free_t *f)
{
	int	i;

	printk("hdr magic 0x%x firstdb %d nvalid %d nused %d\n",
		INT_GET(f->hdr.magic, ARCH_UNKNOWN), INT_GET(f->hdr.firstdb, ARCH_UNKNOWN), INT_GET(f->hdr.nvalid, ARCH_UNKNOWN), INT_GET(f->hdr.nused, ARCH_UNKNOWN));
	for (i = 0; i < INT_GET(f->hdr.nvalid, ARCH_UNKNOWN); i++) {
		printk("entry %d db %d count %d\n",
			i, i + INT_GET(f->hdr.firstdb, ARCH_UNKNOWN), INT_GET(f->bests[i], ARCH_UNKNOWN));
	}
}


/*
 * Print xfs extent list.
 */
static void
xfsidbg_xexlist(xfs_inode_t *ip)
{
	xfs_xexlist_fork(ip, XFS_DATA_FORK);
	if (XFS_IFORK_Q(ip))
		xfs_xexlist_fork(ip, XFS_ATTR_FORK);
}

/*
 * Print an xfs free-extent list.
 */
static void
xfsidbg_xflist(xfs_bmap_free_t *flist)
{
	xfs_bmap_free_item_t	*item;

	printk("flist@0x%p: first 0x%p count %d low %d\n", flist,
		flist->xbf_first, flist->xbf_count, flist->xbf_low);
	for (item = flist->xbf_first; item; item = item->xbfi_next) {
		printk("item@0x%p: startblock %Lx blockcount %d", item,
			(xfs_dfsbno_t)item->xbfi_startblock,
			item->xbfi_blockcount);
	}
}

/*
 * Print out the list of xfs_gap_ts in ip->i_iocore.io_gap_list.
 */
static void
xfsidbg_xgaplist(xfs_inode_t *ip)
{
	xfs_gap_t	*curr_gap;

	curr_gap = ip->i_iocore.io_gap_list;
	if (curr_gap == NULL) {
		printk("Gap list is empty for inode 0x%p\n", ip);
		return;
	}

	while (curr_gap != NULL) {
		printk("gap 0x%p next 0x%p offset 0x%Lx count 0x%x\n",
		       curr_gap, curr_gap->xg_next, curr_gap->xg_offset_fsb,
		       curr_gap->xg_count_fsb);
		curr_gap = curr_gap->xg_next;
	}
}

/*
 * Print out the help messages for these functions.
 */
static void
xfsidbg_xhelp(void)
{
	struct xif	*p;

	for (p = xfsidbg_funcs; p->name; p++)
		printk("%-16s %s %s\n", p->name, p->args, p->help);
}

/*
 * Print out an XFS in-core log structure.
 */
static void
xfsidbg_xiclog(xlog_in_core_t *iclog)
{
	int i;
	static char *ic_flags[] = {
		"ACTIVE",	/* 0x0001 */
		"WANT_SYNC",	/* 0x0002 */
		"SYNCING",	/* 0X0004 */
		"DONE_SYNC",	/* 0X0008 */
		"DO_CALLBACK",	/* 0X0010 */
		"CALLBACK",	/* 0X0020 */
		"DIRTY",	/* 0X0040 */
		"IOERROR",	/* 0X0080 */
		"NOTUSED",	/* 0X8000 */
		0
	};

	printk("xlog_in_core/header at 0x%p\n", iclog);
	printk("magicno: %x  cycle: %d  version: %d  lsn: 0x%Lx\n",
		iclog->ic_header.h_magicno, iclog->ic_header.h_cycle,
		iclog->ic_header.h_version, iclog->ic_header.h_lsn);
	printk("tail_lsn: 0x%Lx  len: %d  prev_block: %d  num_ops: %d\n",
		iclog->ic_header.h_tail_lsn, iclog->ic_header.h_len,
		iclog->ic_header.h_prev_block, iclog->ic_header.h_num_logops);
	printk("cycle_data: ");
	for (i=0; i<(iclog->ic_size>>BBSHIFT); i++) {
		printk("%x  ", iclog->ic_header.h_cycle_data[i]);
	}
	printk("\n");
	printk("--------------------------------------------------\n");
	printk("data: 0x%p  &forcesema: 0x%p  next: 0x%p bp: 0x%p\n",
		iclog->ic_data, &iclog->ic_forcesema, iclog->ic_next,
		iclog->ic_bp);
	printk("log: 0x%p  callb: 0x%p  callb_tail: 0x%p  roundoff: %d\n",
		iclog->ic_log, iclog->ic_callback, iclog->ic_callback_tail,
		iclog->ic_roundoff);
	printk("size: %d  (OFFSET: %d) trace: 0x%p  refcnt: %d  bwritecnt: %d",
		iclog->ic_size, iclog->ic_offset,
		NULL,
		iclog->ic_refcnt, iclog->ic_bwritecnt);
	printk("  state: ");
	if (iclog->ic_state & XLOG_STATE_ALL)
		printflags(iclog->ic_state, ic_flags,"state");
	else
		printk("ILLEGAL");
	printk("\n");
}	/* xfsidbg_xiclog */


/*
 * Print all incore logs.
 */
static void
xfsidbg_xiclogall(xlog_in_core_t *iclog)
{
    xlog_in_core_t *first_iclog = iclog;

    do {
	xfsidbg_xiclog(iclog);
	printk("=================================================\n");
	iclog = iclog->ic_next;
    } while (iclog != first_iclog);
}	/* xfsidbg_xiclogall */

/*
 * Print out the callback structures attached to an iclog.
 */
static void
xfsidbg_xiclogcb(xlog_in_core_t *iclog)
{
	xfs_log_callback_t	*cb;

	for (cb = iclog->ic_callback; cb != NULL; cb = cb->cb_next) {
		printk("func ");
		kdbnearsym((unsigned long)cb->cb_func /* , NULL, NULL */);
		printk(" arg 0x%p next 0x%p\n", cb->cb_arg, cb->cb_next);
	}
}


/*
 * Print all of the inodes attached to the given mount structure.
 */
static void
xfsidbg_xinodes(xfs_mount_t *mp)
{
	xfs_inode_t	*ip;

	printk("xfs_mount at 0x%p\n", mp);
	ip = mp->m_inodes;
	if (ip != NULL) {
		do {
			if (ip->i_mount == NULL) {
				ip = ip->i_mnext;
				continue;
			}
			printk("\n");
			xfsidbg_xnode(ip);
			ip = ip->i_mnext;
		} while (ip != mp->m_inodes);
	}
	printk("\nEnd of Inodes\n");
}

static char *
xfsidbg_get_cstate(int state)
{
	switch(state) {
	case  XLOG_STATE_COVER_IDLE:
		return("idle");
	case  XLOG_STATE_COVER_NEED:
		return("need");
	case  XLOG_STATE_COVER_DONE:
		return("done");
	case  XLOG_STATE_COVER_NEED2:
		return("need2");
	case  XLOG_STATE_COVER_DONE2:
		return("done2");
	default:
		return("unknown");
	}
}

/*
 * Print out an XFS log structure.
 */
static void
xfsidbg_xlog(xlog_t *log)
{
	int rbytes;
	int wbytes;
	static char *t_flags[] = {
		"CHKSUM_MISMATCH",	/* 0x01 */
		"ACTIVE_RECOVERY",	/* 0x02 */
		"RECOVERY_NEEDED",	/* 0x04 */
		"IO_ERROR",		/* 0x08 */
		0
	};

	printk("xlog at 0x%p\n", log);
	printk("&flushsm: 0x%p  tic_cnt: %d  tic_tcnt: %d  \n",
		&log->l_flushsema, log->l_ticket_cnt, log->l_ticket_tcnt);
	printk("freelist: 0x%p  tail: 0x%p  ICLOG: 0x%p  \n",
		log->l_freelist, log->l_tail, log->l_iclog);
	printk("&icloglock: 0x%p  tail_lsn: 0x%Lx  last_sync_lsn: 0x%Lx \n",
		&log->l_icloglock, log->l_tail_lsn, log->l_last_sync_lsn);
	printk("mp: 0x%p  xbuf: 0x%p  roundoff: %d  l_covered_state: %s \n",
		log->l_mp, log->l_xbuf, log->l_roundoff,
			xfsidbg_get_cstate(log->l_covered_state));
	printk("flags: ");
	printflags(log->l_flags, t_flags,"log");
	printk("  dev: 0x%x logBBstart: %Ld logsize: %d logBBsize: %d\n",
		log->l_dev, log->l_logBBstart, log->l_logsize,log->l_logBBsize);
     printk("curr_cycle: %d  prev_cycle: %d  curr_block: %d  prev_block: %d\n",
	     log->l_curr_cycle, log->l_prev_cycle, log->l_curr_block,
	     log->l_prev_block);
	printk("iclog_bak: 0x%p  iclog_size: 0x%x (%d)  num iclogs: %d\n",
		log->l_iclog_bak, log->l_iclog_size, log->l_iclog_size,
		log->l_iclog_bufs);
	printk("&grant_lock: 0x%p  resHeadQ: 0x%p  wrHeadQ: 0x%p\n",
		&log->l_grant_lock, log->l_reserve_headq, log->l_write_headq);
	printk("GResCycle: %d  GResBytes: %d  GWrCycle: %d  GWrBytes: %d\n",
		log->l_grant_reserve_cycle, log->l_grant_reserve_bytes,
		log->l_grant_write_cycle, log->l_grant_write_bytes);
	rbytes = log->l_grant_reserve_bytes + log->l_roundoff;
	wbytes = log->l_grant_write_bytes + log->l_roundoff;
       printk("GResBlocks: %d  GResRemain: %d  GWrBlocks: %d  GWrRemain: %d\n",
	       rbytes / BBSIZE, rbytes % BBSIZE,
	       wbytes / BBSIZE, wbytes % BBSIZE);
}	/* xfsidbg_xlog */


/*
 * Print out an XFS recovery transaction
 */
static void
xfsidbg_xlog_ritem(xlog_recover_item_t *item)
{
	int i = XLOG_MAX_REGIONS_IN_ITEM;

	printk("(xlog_recover_item 0x%p) ", item);
	printk("next: 0x%p prev: 0x%p type: %d cnt: %d ttl: %d\n",
		item->ri_next, item->ri_prev, item->ri_type, item->ri_cnt,
		item->ri_total);
	for ( ; i > 0; i--) {
		if (!item->ri_buf[XLOG_MAX_REGIONS_IN_ITEM-i].i_addr)
			break;
		printk("a: 0x%p l: %d ",
			item->ri_buf[XLOG_MAX_REGIONS_IN_ITEM-i].i_addr,
			item->ri_buf[XLOG_MAX_REGIONS_IN_ITEM-i].i_len);
	}
	printk("\n");
}	/* xfsidbg_xlog_ritem */

/*
 * Print out an XFS recovery transaction
 */
static void
xfsidbg_xlog_rtrans(xlog_recover_t *trans)
{
	xlog_recover_item_t *rip, *first_rip;

	printk("(xlog_recover 0x%p) ", trans);
	printk("tid: %x type: %d items: %d ttid: 0x%x  ",
		trans->r_log_tid, trans->r_theader.th_type,
		trans->r_theader.th_num_items, trans->r_theader.th_tid);
	printk("itemq: 0x%p\n", trans->r_itemq);
	if (trans->r_itemq) {
		rip = first_rip = trans->r_itemq;
		do {
			printk("(recovery item: 0x%p) ", rip);
			printk("type: %d cnt: %d total: %d\n",
				rip->ri_type, rip->ri_cnt, rip->ri_total);
			rip = rip->ri_next;
		} while (rip != first_rip);
	}
}	/* xfsidbg_xlog_rtrans */
 
static void
xfsidbg_xlog_buf_logitem(xlog_recover_item_t *item)
{
	xfs_buf_log_format_t	*buf_f;
	int			i, j;
	int			bit;
	int			nbits;
	unsigned int		*data_map;
	unsigned int		map_size;
	int			size;

	buf_f = (xfs_buf_log_format_t *)item->ri_buf[0].i_addr;
	if (buf_f->blf_flags & XFS_BLI_INODE_BUF) {
		printk("\tINODE BUF <blkno=0x%Lx, len=0x%x>\n",
			buf_f->blf_blkno, buf_f->blf_len);
	} else if (buf_f->blf_flags & (XFS_BLI_UDQUOT_BUF | XFS_BLI_PDQUOT_BUF)) {
		printk("\tDQUOT BUF <blkno=0x%Lx, len=0x%x>\n",
			buf_f->blf_blkno, buf_f->blf_len);
	} else {
		printk("\tREG BUF <blkno=0x%Lx, len=0x%x>\n",
			buf_f->blf_blkno, buf_f->blf_len);
		data_map = buf_f->blf_data_map;
		map_size = buf_f->blf_map_size;
		bit = 0;
		i = 1;  /* 0 is the buf format structure */
		while (1) {
			size = 1;
			printk("\t\tlogbuf.i_addr 0x%p, size 0x%xB\n",
				item->ri_buf[i].i_addr, size);
			printk("\t\t\t\"");
			for (j=0; j<8 && j<size; j++) {
				printk("%c", ((char *)item->ri_buf[i].i_addr)[j]);
			}
			printk("...\"\n");
			i++;
			bit += nbits;
		}
			
	}
}

/*
 * Print out an ENTIRE XFS recovery transaction
 */
static void
xfsidbg_xlog_rtrans_entire(xlog_recover_t *trans)
{
	xlog_recover_item_t *item, *first_rip;

	printk("(Recovering Xact 0x%p) ", trans);
	printk("tid: %x type: %d nitems: %d ttid: 0x%x  ",
		trans->r_log_tid, trans->r_theader.th_type,
		trans->r_theader.th_num_items, trans->r_theader.th_tid);
	printk("itemq: 0x%p\n", trans->r_itemq);
	if (trans->r_itemq) {
		item = first_rip = trans->r_itemq;
		do {
			/* 
			   printk("(recovery item: 0x%x) ", item);
			   printk("type: %d cnt: %d total: %d\n",
				   item->ri_type, item->ri_cnt, item->ri_total); 
				   */
			if ((ITEM_TYPE(item) == XFS_LI_BUF) ||
			    (ITEM_TYPE(item) == XFS_LI_6_1_BUF) ||
			    (ITEM_TYPE(item) == XFS_LI_5_3_BUF)) {
				printk("BUF:");
				xfsidbg_xlog_buf_logitem(item);
			} else if ((ITEM_TYPE(item) == XFS_LI_INODE) ||
				   (ITEM_TYPE(item) == XFS_LI_6_1_INODE) ||
				   (ITEM_TYPE(item) == XFS_LI_5_3_INODE)) {
				printk("INODE:\n");
			} else if (ITEM_TYPE(item) == XFS_LI_EFI) {
				printk("EFI:\n");
			} else if (ITEM_TYPE(item) == XFS_LI_EFD) {
				printk("EFD:\n");
			} else if (ITEM_TYPE(item) == XFS_LI_DQUOT) {
				printk("DQUOT:\n");
			} else if ((ITEM_TYPE(item) == XFS_LI_QUOTAOFF)) {
				printk("QUOTAOFF:\n");
			} else {
				printk("UNKNOWN LOGITEM 0x%x\n", ITEM_TYPE(item));
			}
			item = item->ri_next;
		} while (item != first_rip);
	}
}	/* xfsidbg_xlog_rtrans */

/*
 * Print out an XFS ticket structure.
 */
static void
xfsidbg_xlog_tic(xlog_ticket_t *tic)
{
	static char *t_flags[] = {
		"INIT",		/* 0x1 */
		"PERM_RES",	/* 0x2 */
		"IN_Q",		/* 0x4 */
		0
	};

	printk("xlog_ticket at 0x%p\n", tic);
	printk("next: 0x%p  prev: 0x%p  tid: 0x%x  \n",
		tic->t_next, tic->t_prev, tic->t_tid);
	printk("curr_res: %d  unit_res: %d  ocnt: %d  cnt: %d\n",
		tic->t_curr_res, tic->t_unit_res, (int)tic->t_ocnt,
		(int)tic->t_cnt);
	printk("clientid: %c  \n", tic->t_clientid);
	printflags(tic->t_flags, t_flags,"ticket");
	printk("\n");
}	/* xfsidbg_xlog_tic */

/*
 * Print out a single log item.
 */
static void
xfsidbg_xlogitem(xfs_log_item_t *lip)
{
	xfs_log_item_t	*bio_lip;
	static char *lid_type[] = {
		"???",		/* 0 */
		"5-3-buf",	/* 1 */
		"5-3-inode",	/* 2 */
		"efi",		/* 3 */
		"efd",		/* 4 */
		"iunlink",	/* 5 */
		"6-1-inode",	/* 6 */
		"6-1-buf",	/* 7 */
		"inode",	/* 8 */
		"buf",		/* 9 */
		"dquot",	/* 10 */
		0
		};
	static char *li_flags[] = {
		"in ail",	/* 0x1 */
		0
		};

	printk("type %s mountp 0x%p flags ",
		lid_type[lip->li_type - XFS_LI_5_3_BUF + 1],
		lip->li_mountp);
	printflags((uint)(lip->li_flags), li_flags,"log");
	printk("\n");
	printk("ail forw 0x%p ail back 0x%p lsn %s desc %p ops 0x%p\n",
		lip->li_ail.ail_forw, lip->li_ail.ail_back,
		xfs_fmtlsn(&(lip->li_lsn)), lip->li_desc, lip->li_ops);
	printk("iodonefunc &0x%p\n", lip->li_cb);
	if (lip->li_type == XFS_LI_BUF) {
		bio_lip = lip->li_bio_list;
		if (bio_lip != NULL) {
			printk("iodone list:\n");
		}
		while (bio_lip != NULL) {
			printk("item 0x%p func 0x%p\n",
				bio_lip, bio_lip->li_cb);
			bio_lip = bio_lip->li_bio_list;
		}
	}
	switch (lip->li_type) {
	case XFS_LI_BUF:
		xfs_buf_item_print((xfs_buf_log_item_t *)lip, 0);
		break;
	case XFS_LI_INODE:
		xfs_inode_item_print((xfs_inode_log_item_t *)lip, 0);
		break;
	case XFS_LI_EFI:
		xfs_efi_item_print((xfs_efi_log_item_t *)lip, 0);
		break;
	case XFS_LI_EFD:
		xfs_efd_item_print((xfs_efd_log_item_t *)lip, 0);
		break;
	case XFS_LI_DQUOT:
		xfs_dquot_item_print((xfs_dq_logitem_t *)lip, 0);
		break;
	case XFS_LI_QUOTAOFF:
		xfs_qoff_item_print((xfs_qoff_logitem_t *)lip, 0);
		break;
		
	default:
		printk("Unknown item type %d\n", lip->li_type);
		break;
	}
}

/*
 * Print out a summary of the AIL hanging off of a mount struct.
 */
static void
xfsidbg_xaildump(xfs_mount_t *mp)
{
	xfs_log_item_t *lip;
	static char *lid_type[] = {
		"???",		/* 0 */
		"5-3-buf",	/* 1 */
		"5-3-inode",	/* 2 */
		"efi",		/* 3 */
		"efd",		/* 4 */
		"iunlink",	/* 5 */
		"6-1-inode",	/* 6 */
		"6-1-buf",	/* 7 */
		"inode",	/* 8 */
		"buf",		/* 9 */
		"dquot",        /* 10 */
		0
		};
	static char *li_flags[] = {
		"in ail",	/* 0x1 */
		0
		};
	int count;

	if ((mp->m_ail.ail_forw == NULL) ||
	    (mp->m_ail.ail_forw == (xfs_log_item_t *)&mp->m_ail)) {
		printk("AIL is empty\n");
		return;
	}
	printk("AIL for mp 0x%p, oldest first\n", mp);
	lip = (xfs_log_item_t*)mp->m_ail.ail_forw;
	for (count = 0; lip; count++) {
		printk("[%d] type %s ", count,
			      lid_type[lip->li_type - XFS_LI_5_3_BUF + 1]);
		printflags((uint)(lip->li_flags), li_flags, "flags:");
		printk("  lsn %s\n   ", xfs_fmtlsn(&(lip->li_lsn)));
		switch (lip->li_type) {
		case XFS_LI_BUF:
			xfs_buf_item_print((xfs_buf_log_item_t *)lip, 1);
			break;
		case XFS_LI_INODE:
			xfs_inode_item_print((xfs_inode_log_item_t *)lip, 1);
			break;
		case XFS_LI_EFI:
			xfs_efi_item_print((xfs_efi_log_item_t *)lip, 1);
			break;
		case XFS_LI_EFD:
			xfs_efd_item_print((xfs_efd_log_item_t *)lip, 1);
			break;
		case XFS_LI_DQUOT:
			xfs_dquot_item_print((xfs_dq_logitem_t *)lip, 1);
			break;
		case XFS_LI_QUOTAOFF:
			xfs_qoff_item_print((xfs_qoff_logitem_t *)lip, 1);
			break;	
		default:
			printk("Unknown item type %d\n", lip->li_type);
			break;
		}

		if (lip->li_ail.ail_forw == (xfs_log_item_t*)&mp->m_ail) {
			lip = NULL;
		} else {
			lip = lip->li_ail.ail_forw;
		}
	}
}

/*
 * Print xfs mount structure.
 */
static void
xfsidbg_xmount(xfs_mount_t *mp)
{
	static char *xmount_flags[] = {
		"WSYNC",	/* 0x0001 */
		"INO64",	/* 0x0002 */
		"RQCHK",        /* 0x0004 */
		"FSCLEAN",	/* 0x0008 */
		"FSSHUTDN",	/* 0x0010 */
		"NOATIME",	/* 0x0020 */
		"RETERR",	/* 0x0040 */
		"NOALIGN",	/* 0x0080 */
		"UNSHRD",	/* 0x0100 */
		"RGSTRD",	/* 0x0200 */
		"NORECVR",	/* 0x0400 */
		"SHRD",		/* 0x0800 */
		"IOSZ",		/* 0x1000 */
		"DSYNC",	/* 0x2000 */
		0
	};

	static char *quota_flags[] = {
		"UQ",		/* 0x0001 */
		"UQE", 		/* 0x0002 */
		"UQCHKD",     	/* 0x0004 */
		"PQ",		/* 0x0008 */
		"PQE", 		/* 0x0010 */
		"PQCHKD",     	/* 0x0020 */
		"UQACTV",	/* 0x0040 */
		"PQACTV",	/* 0x0080 */
		0
	};

	printk("xfs_mount at 0x%p\n", mp);
	printk("vfsp 0x%p tid 0x%x ail_lock 0x%p &ail 0x%p\n",
		XFS_MTOVFS(mp), mp->m_tid, &mp->m_ail_lock, &mp->m_ail);
	printk("ail_gen 0x%x &sb 0x%p\n",
		mp->m_ail_gen, &mp->m_sb);
	printk("sb_lock 0x%p sb_bp 0x%p dev 0x%x logdev 0x%x rtdev 0x%x\n",
		&mp->m_sb_lock, mp->m_sb_bp, mp->m_dev, mp->m_logdev,
		mp->m_rtdev);
	printk("bsize %d agfrotor %d agirotor %d ihash 0x%p ihsize %d\n",
		mp->m_bsize, mp->m_agfrotor, mp->m_agirotor,
		mp->m_ihash, mp->m_ihsize);
	printk("inodes 0x%p ilock 0x%p ireclaims 0x%x\n",
		mp->m_inodes, &mp->m_ilock, mp->m_ireclaims);
	printk("readio_log 0x%x readio_blocks 0x%x ",
		mp->m_readio_log, mp->m_readio_blocks);
	printk("writeio_log 0x%x writeio_blocks 0x%x\n",
		mp->m_writeio_log, mp->m_writeio_blocks);
	printk("logbufs %d logbsize %d LOG 0x%p\n", mp->m_logbufs,
		mp->m_logbsize, mp->m_log);
	printk("rsumlevels 0x%x rsumsize 0x%x rbmip 0x%p rsumip 0x%p\n",
		mp->m_rsumlevels, mp->m_rsumsize, mp->m_rbmip, mp->m_rsumip);
	printk("rootip 0x%p\n", mp->m_rootip);
	printk("dircook_elog %d blkbit_log %d blkbb_log %d agno_log %d\n",
		mp->m_dircook_elog, mp->m_blkbit_log, mp->m_blkbb_log,
		mp->m_agno_log);
	printk("agino_log %d nreadaheads %d inode cluster size %d\n",
		mp->m_agino_log, mp->m_nreadaheads,
		mp->m_inode_cluster_size);
	printk("blockmask 0x%x blockwsize 0x%x blockwmask 0x%x\n",
		mp->m_blockmask, mp->m_blockwsize, mp->m_blockwmask);
	printk("alloc_mxr[lf,nd] %d %d alloc_mnr[lf,nd] %d %d\n",
		mp->m_alloc_mxr[0], mp->m_alloc_mxr[1],
		mp->m_alloc_mnr[0], mp->m_alloc_mnr[1]);
	printk("bmap_dmxr[lfnr,ndnr] %d %d bmap_dmnr[lfnr,ndnr] %d %d\n",
		mp->m_bmap_dmxr[0], mp->m_bmap_dmxr[1],
		mp->m_bmap_dmnr[0], mp->m_bmap_dmnr[1]);
	printk("inobt_mxr[lf,nd] %d %d inobt_mnr[lf,nd] %d %d\n",
		mp->m_inobt_mxr[0], mp->m_inobt_mxr[1],
		mp->m_inobt_mnr[0], mp->m_inobt_mnr[1]);
	printk("ag_maxlevels %d bm_maxlevels[d,a] %d %d in_maxlevels %d\n",
		mp->m_ag_maxlevels, mp->m_bm_maxlevels[0],
		mp->m_bm_maxlevels[1], mp->m_in_maxlevels);
	printk("perag 0x%p &peraglock 0x%p &growlock 0x%p\n",
		mp->m_perag, &mp->m_peraglock, &mp->m_growlock);
	printflags(mp->m_flags, xmount_flags,"flags");
	printk("ialloc_inos %d ialloc_blks %d litino %d\n",
		mp->m_ialloc_inos, mp->m_ialloc_blks, mp->m_litino);
	printk("attroffset %d da_node_ents %d maxicount %Ld inoalign_mask %d\n",
		mp->m_attroffset, mp->m_da_node_ents, mp->m_maxicount,
		mp->m_inoalign_mask);
	printk("resblks %Ld resblks_avail %Ld\n", mp->m_resblks, 
		mp->m_resblks_avail);
#if XFS_BIG_FILESYSTEMS
	printk(" inoadd %Lx\n", mp->m_inoadd);
#else
	printk("\n");
#endif
	if (mp->m_quotainfo)
		printk("quotainfo 0x%p (uqip = 0x%p, pqip = 0x%p)\n",
			mp->m_quotainfo, 
			mp->m_quotainfo->qi_uquotaip,
			mp->m_quotainfo->qi_pquotaip);
	else 
		printk("quotainfo NULL\n");
	printflags(mp->m_qflags, quota_flags,"quotaflags");
	printk("\n");
	printk("dalign %d swidth %d sinoalign %d attr_magicpct %d dir_magicpct %d\n", 
		mp->m_dalign, mp->m_swidth, mp->m_sinoalign,
		mp->m_attr_magicpct, mp->m_dir_magicpct);
	printk("mk_sharedro %d dirversion %d dirblkfsbs %d &dirops 0x%p\n",
		mp->m_mk_sharedro, mp->m_dirversion, mp->m_dirblkfsbs,
		&mp->m_dirops);
	printk("dirblksize %d dirdatablk 0x%Lx dirleafblk 0x%Lx dirfreeblk 0x%Lx\n",
		mp->m_dirblksize,
		(xfs_dfiloff_t)mp->m_dirdatablk,
		(xfs_dfiloff_t)mp->m_dirleafblk,
		(xfs_dfiloff_t)mp->m_dirfreeblk);
	printk("chsize %d chash 0x%p\n",
		mp->m_chsize, mp->m_chash);
	if (mp->m_fsname != NULL)
		printk("mountpoint \"%s\"\n", mp->m_fsname);
	else
		printk("No name!!!\n");
		
}

static void
xfsidbg_xihash(xfs_mount_t *mp)
{
	xfs_ihash_t	*ih;
	int		i;
	int		j;
	int		total;
	int		numzeros;
	xfs_inode_t	*ip;
	int		*hist;
	int		hist_bytes = mp->m_ihsize * sizeof(int);
	int		hist2[21];
	void		*kmalloc(size_t, int);
	void		kfree_s(void *, size_t);

	hist = (int *) kmalloc(hist_bytes, GFP_KERNEL);
        ASSERT(hist);
	for (i = 0; i < mp->m_ihsize; i++) {
		ih = mp->m_ihash + i;
		j = 0;
		for (ip = ih->ih_next; ip != NULL; ip = ip->i_next)
			j++;
		hist[i] = j;
	}

	numzeros = total = 0;

	for (i = 0; i < 21; i++)
		hist2[i] = 0;

	for (i = 0; i < mp->m_ihsize; i++)  {
		printk("%d ", hist[i]);
		total += hist[i];
		numzeros += hist[i] == 0 ? 1 : 0;
		if (hist[i] > 20)
			j = 20;
		else
			j = hist[i];
		ASSERT(j <= 20);
		hist2[j]++;
	}

	printk("\n");

	printk("total inodes = %d, average length = %d, adjusted average = %d \n",
		total, total / mp->m_ihsize,
		total / (mp->m_ihsize - numzeros));

	for (i = 0; i < 21; i++)  {
		printk("%d - %d , ", i, hist2[i]);
	}
	printk("\n");
	kfree_s(hist, hist_bytes);
}

/*
 * Command to print xfs inodes: kp xnode <addr>
 */
static void
xfsidbg_xnode(xfs_inode_t *ip)
{
	static char *tab_flags[] = {
		"grio",		/* XFS_IGRIO */
		"uiosize",	/* XFS_IUIOSZ */
		NULL
	};

	printk("hash 0x%p next 0x%p prevp 0x%p mount 0x%p\n",
		ip->i_hash,
		ip->i_next,
		ip->i_prevp,
		ip->i_mount);
	printk("mnext 0x%p mprev 0x%p vnode 0x%p \n",
		ip->i_mnext,
		ip->i_mprev,
		XFS_ITOV(ip));
	printk("dev %x ino %s\n",
		ip->i_dev,
		xfs_fmtino(ip->i_ino, ip->i_mount));
	printk("blkno 0x%Lx len 0x%x boffset 0x%x\n",
		ip->i_blkno,
		ip->i_len,
		ip->i_boffset);
	printk("transp 0x%p &itemp 0x%p\n",
		ip->i_transp,
		ip->i_itemp);
	printk("&lock 0x%p lock_ra 0x%p &iolock 0x%p\n",
		&ip->i_lock,
		ip->i_ilock_ra,
		&ip->i_iolock);
	printk("udquotp 0x%p pdquotp 0x%p\n",
		ip->i_udquot, ip->i_pdquot);
	printk("&flock 0x%p (%d) &pinlock 0x%p pincount 0x%x &pinsema 0x%p\n",
		&ip->i_flock, valusema(&ip->i_flock),
		&ip->i_ipinlock,
		ip->i_pincount,
		&ip->i_pinsema);
	printk("&rlock 0x%p\n", &ip->i_iocore.io_rlock);
	printk("next_offset %Lx ", ip->i_iocore.io_next_offset);
	printk("io_offset %Lx reada_blkno %s io_size 0x%x\n",
		ip->i_iocore.io_offset,
		xfs_fmtfsblock(ip->i_iocore.io_reada_blkno, ip->i_mount),
		ip->i_iocore.io_size);
	printk("last_req_sz 0x%x new_size %Lx\n",
		ip->i_iocore.io_last_req_sz, ip->i_iocore.io_new_size);
	printk("write off %Lx gap list 0x%p\n",
		ip->i_iocore.io_write_offset, ip->i_iocore.io_gap_list);
	printk(
"readiolog %u, readioblocks %u, writeiolog %u, writeioblocks %u, maxiolog %u\n",
		(unsigned int) ip->i_iocore.io_readio_log,
		ip->i_iocore.io_readio_blocks,
		(unsigned int) ip->i_iocore.io_writeio_log,
		ip->i_iocore.io_writeio_blocks,
		(unsigned int) ip->i_iocore.io_max_io_log);
	printflags((int)ip->i_flags, tab_flags, "flags");
	printk("\n");
	printk("update_core 0x%x update size 0x%x\n",
		(int)(ip->i_update_core), (int) ip->i_update_size);
	printk("gen 0x%x qbufs %d delayed blks %d",
		ip->i_gen,
		ip->i_iocore.io_queued_bufs,
		ip->i_delayed_blks);
	printk("\n");
	printk("chash 0x%p cnext 0x%p cprev 0x%p\n",
		ip->i_chash,
		ip->i_cnext,
		ip->i_cprev);
	xfs_xnode_fork("data", &ip->i_df);
	xfs_xnode_fork("attr", ip->i_afp);
	printk("\n");
	xfs_prdinode_core(&ip->i_d);
}

static void
xfsidbg_xcore(xfs_iocore_t *io)
{
	if (IO_IS_XFS(io)) {
		printk("io_obj 0x%p (xinode) io_mount 0x%p\n",
			io->io_obj, io->io_mount);
	} else {
		printk("io_obj 0x%p (dcxvn) io_mount 0x%p\n",
			io->io_obj, io->io_mount);
	}
	printk("&lock 0x%p &iolock 0x%p &flock 0x%p &rlock 0x%p\n",
		io->io_lock, io->io_iolock,
		io->io_flock, &io->io_rlock);
	printk("next_offset %Lx ", io->io_next_offset);
	printk("io_offset %Lx reada_blkno %s io_size 0x%x\n",
		io->io_offset,
		xfs_fmtfsblock(io->io_reada_blkno, io->io_mount),
		io->io_size);
	printk("last_req_sz 0x%x new_size %Lx\n",
		io->io_last_req_sz, io->io_new_size);
	printk("write off %Lx gap list 0x%p\n",
		io->io_write_offset, io->io_gap_list);
	printk(
"readiolog %u, readioblocks %u, writeiolog %u, writeioblocks %u, maxiolog %u\n",
		(unsigned int) io->io_readio_log,
		io->io_readio_blocks,
		(unsigned int) io->io_writeio_log,
		io->io_writeio_blocks,
		(unsigned int) io->io_max_io_log);
}

/*
 * Command to print xfs inode cluster hash table: kp xchash <addr>
 */
static void
xfsidbg_xchash(xfs_mount_t *mp)
{
	int		i;
	xfs_chash_t	*ch;

	printk("m_chash 0x%p size %d\n",
		mp->m_chash, mp->m_chsize);
	for (i = 0; i < mp->m_chsize; i++) {
		ch = mp->m_chash + i;
		printk("[%3d] ch 0x%p chashlist 0x%p\n", i, ch, ch->ch_list);
		xfsidbg_xchashlist(ch->ch_list);
	}
}

/*
 * Command to print xfs inode cluster hash list: kp xchashlist <addr>
 */
static void
xfsidbg_xchashlist(xfs_chashlist_t *chl)
{
	xfs_inode_t	*ip;

	while (chl != NULL) {
		printk("hashlist inode 0x%p blkno %Ld ",
		       chl->chl_ip, chl->chl_blkno);

		printk("\n");

		/* print inodes on chashlist */
		ip = chl->chl_ip;
		do {
			printk("0x%p ", ip);
			ip = ip->i_cnext;
		} while (ip != chl->chl_ip);
		printk("\n");

		chl=chl->chl_next;
	}
}

/*
 * Print xfs per-ag data structures for filesystem.
 */
static void
xfsidbg_xperag(xfs_mount_t *mp)
{
	xfs_agnumber_t	agno;
	xfs_perag_t	*pag;

	pag = mp->m_perag;
	for (agno = 0; agno < mp->m_sb.sb_agcount; agno++, pag++) {
		printk("ag %d f_init %d i_init %d\n",
			agno, pag->pagf_init, pag->pagi_init);
		if (pag->pagf_init)
			printk(
	"    f_levels[b,c] %d,%d f_flcount %d f_freeblks %d f_longest %d\n",
				pag->pagf_levels[XFS_BTNUM_BNOi],
				pag->pagf_levels[XFS_BTNUM_CNTi],
				pag->pagf_flcount, pag->pagf_freeblks,
				pag->pagf_longest);
		if (pag->pagi_init)
			printk("    i_freecount %d\n", pag->pagi_freecount);
	}
}



static void
xfsidbg_xqm()
{

	if (xfs_Gqm == NULL) {
		printk("NULL XQM!!\n");
		return;
	}

	printk("usrhtab 0x%p\tprjhtab 0x%p\tndqfree 0x%x\thashmask 0x%x\n",
		xfs_Gqm->qm_usr_dqhtable,
		xfs_Gqm->qm_prj_dqhtable,
		xfs_Gqm->qm_dqfreelist.qh_nelems,
		xfs_Gqm->qm_dqhashmask);
	printk("&freelist 0x%p, totaldquots 0x%x nrefs 0x%x\n",
		&xfs_Gqm->qm_dqfreelist,
		xfs_Gqm->qm_totaldquots,
		xfs_Gqm->qm_nrefs);
}

static void
xfsidbg_xqm_diskdq(xfs_disk_dquot_t *d)
{
	printk("magic 0x%x\tversion 0x%x\tID 0x%x (%d)\t\n", d->d_magic,
		d->d_version, d->d_id, d->d_id);
	printk("blk_hard 0x%x\tblk_soft 0x%x\tino_hard 0x%x\tino_soft 0x%x\n",
		(int)d->d_blk_hardlimit, (int)d->d_blk_softlimit,
		(int)d->d_ino_hardlimit, (int)d->d_ino_softlimit);
	printk("bcount 0x%x (%d) icount 0x%x (%d)\n",
		(int)d->d_bcount, (int)d->d_bcount, 
		(int)d->d_icount, (int)d->d_icount);
	printk("btimer 0x%x itimer 0x%x \n",
		(int)d->d_btimer, (int)d->d_itimer);
}

static void	
xfsidbg_xqm_dquot(xfs_dquot_t *dqp)
{
	static char *qflags[] = {
		"USR",
		"PRJ",
		"LCKD",
		"FLKD",
		"DIRTY",
		"WANT",	
		"INACT",
		"MARKER",
		0
	};
	printk("mount 0x%p hash 0x%p pdquotp 0x%p HL_next 0x%p HL_prevp 0x%p\n",
		dqp->q_mount,
		dqp->q_hash,
		dqp->q_pdquot,
		dqp->HL_NEXT,
		dqp->HL_PREVP);
	printk("MPL_next 0x%p MPL_prevp 0x%p FL_next 0x%p FL_prev 0x%p\n",
		dqp->MPL_NEXT,
		dqp->MPL_PREVP,
		dqp->dq_flnext,
		dqp->dq_flprev);

	printk("nrefs 0x%x, res_bcount %d, ", 
		dqp->q_nrefs, (int) dqp->q_res_bcount);
	printflags(dqp->dq_flags, qflags, "flags:");
	printk("\nblkno 0x%x\tdev 0x%x\tboffset 0x%x\n", (int) dqp->q_blkno, 
		(int) dqp->q_dev, (int) dqp->q_bufoffset);
	printk("qlock 0x%p  flock 0x%p (%s) pincount 0x%x\n",
		&dqp->q_qlock,
		&dqp->q_flock, 
		(valusema(&dqp->q_flock) <= 0) ? "LCK" : "UNLKD",
		dqp->q_pincount);
	printk("disk-dquot 0x%p\n", &dqp->q_core);
	xfsidbg_xqm_diskdq(&dqp->q_core);
	
}


#define XQMIDBG_LIST_PRINT(l, NXT) \
{ \
	  xfs_dquot_t	*dqp;\
	  int i = 0; \
	  printk("[#%d dquots]\n", (int) (l)->qh_nelems); \
	  for (dqp = (l)->qh_next; dqp != NULL; dqp = dqp->NXT) {\
	   printk( \
	      "\t%d. [0x%p] \"%d (%s)\"\t blks = %d, inos = %d refs = %d\n", \
			 ++i, dqp, (int) dqp->q_core.d_id, \
		         DQFLAGTO_TYPESTR(dqp),      \
			 (int) dqp->q_core.d_bcount, \
			 (int) dqp->q_core.d_icount, \
                         (int) dqp->q_nrefs); }\
	  printk("\n"); \
}

static void
xfsidbg_xqm_dqattached_inos(xfs_mount_t	*mp)
{
	xfs_inode_t	*ip;
	int		n = 0;

	ip = mp->m_inodes;
	do {
		if (ip->i_mount == NULL) {
			ip = ip->i_mnext;
			continue;
		}
		if (ip->i_udquot || ip->i_pdquot) {
			n++;
			printk("inode = 0x%p, ino %d: udq 0x%p, pdq 0x%p\n", 
				ip, (int) ip->i_ino, ip->i_udquot, ip->i_pdquot);
		}
		ip = ip->i_mnext;
	} while (ip != mp->m_inodes);
	printk("\nNumber of inodes with dquots attached: %d\n", n);

}


static void
xfsidbg_xqm_freelist_print(xfs_frlist_t *qlist, char *title) 
{
	xfs_dquot_t *dq;
	int i = 0;
	printk("%s (#%d)\n", title, (int) qlist->qh_nelems);		
	FOREACH_DQUOT_IN_FREELIST(dq, qlist) {
		printk("\t%d.\t\"%d (%s:0x%p)\"\t bcnt = %d, icnt = %d "
		       "refs = %d\n",  
		       ++i, (int) dq->q_core.d_id,
		       DQFLAGTO_TYPESTR(dq), dq,     
		       (int) dq->q_core.d_bcount, 
		       (int) dq->q_core.d_icount, 
		       (int) dq->q_nrefs);
	}
}

static void	
xfsidbg_xqm_freelist(void)
{
	if (xfs_Gqm) {
		xfsidbg_xqm_freelist_print(&(xfs_Gqm->qm_dqfreelist), "Freelist");
	} else
		printk("NULL XQM!!\n");
}

static void	
xfsidbg_xqm_mplist(xfs_mount_t *mp)
{
	if (mp->m_quotainfo == NULL) {
		printk("NULL quotainfo\n");
		return;
	}
	
	XQMIDBG_LIST_PRINT(&(mp->m_quotainfo->qi_dqlist), MPL_NEXT);

}

static void
xfsidbg_xqm_htab(void)
{
	int		i;
	xfs_dqhash_t	*h;

	if (xfs_Gqm == NULL) {
		printk("NULL XQM!!\n");
		return;
	}
	for (i = 0; i <= xfs_Gqm->qm_dqhashmask; i++) {
		h = &xfs_Gqm->qm_usr_dqhtable[i];
		if (h->qh_next) {
			printk("USR %d: ", i);
			XQMIDBG_LIST_PRINT(h, HL_NEXT);
		}
	}
	for (i = 0; i <= xfs_Gqm->qm_dqhashmask; i++) {
		h = &xfs_Gqm->qm_prj_dqhtable[i];
		if (h->qh_next) {
			printk("PRJ %d: ", i);
			XQMIDBG_LIST_PRINT(h, HL_NEXT);
		}
	}
}


static void
xfsidbg_xqm_qinfo(xfs_mount_t *mp)
{
	if (mp == NULL || mp->m_quotainfo == NULL) {
		printk("NULL quotainfo\n");
		return;
	}
	
	printk("uqip 0x%p, pqip 0x%p, &pinlock 0x%p &dqlist 0x%p\n",
		mp->m_quotainfo->qi_uquotaip,
		mp->m_quotainfo->qi_pquotaip,
		&mp->m_quotainfo->qi_pinlock,
		&mp->m_quotainfo->qi_dqlist);

	printk("nreclaims %d, btmlimit 0x%x, itmlimit 0x%x, RTbtmlim 0x%x\n",
		(int)mp->m_quotainfo->qi_dqreclaims,
		(int)mp->m_quotainfo->qi_btimelimit,
		(int)mp->m_quotainfo->qi_itimelimit,
		(int)mp->m_quotainfo->qi_rtbtimelimit);

	printk("bwarnlim 0x%x, iwarnlim 0x%x, &qofflock 0x%p, "
		"chunklen 0x%x, dqperchunk 0x%x\n",
		(int)mp->m_quotainfo->qi_bwarnlimit,
		(int)mp->m_quotainfo->qi_iwarnlimit,
		&mp->m_quotainfo->qi_quotaofflock,
		(int)mp->m_quotainfo->qi_dqchunklen,
		(int)mp->m_quotainfo->qi_dqperchunk);
}

static void
xfsidbg_xqm_tpdqinfo(xfs_trans_t *tp)
{
	xfs_dqtrx_t 	*qa, *q;
	int		i,j;

	printk("dqinfo 0x%p\n", tp->t_dqinfo);
	if (! tp->t_dqinfo)
		return;
	printk("USR: \n");
	qa = tp->t_dqinfo->dqa_usrdquots;
	for (j = 0; j < 2; j++) {
		for (i = 0; i < XFS_QM_TRANS_MAXDQS; i++) {
			if (qa[i].qt_dquot == NULL)
				break;
			q = &qa[i];
			printk(
  "\"%d\"[0x%p]: bres %d, bres-used %d, bdelta %d, del-delta %d, icnt-delta %d\n",
				(int) q->qt_dquot->q_core.d_id,
				q->qt_dquot,
				(int) q->qt_blk_res,
				(int) q->qt_blk_res_used,
				(int) q->qt_bcount_delta,
				(int) q->qt_delbcnt_delta,
				(int) q->qt_icount_delta);
		}
		if (j == 0) {
			qa = tp->t_dqinfo->dqa_prjdquots;
			printk("PRJ: \n");
		}
	}
				
}	



/*
 * Print xfs superblock.
 */
static void
xfsidbg_xsb(xfs_sb_t *sbp)
{
	printk("magicnum 0x%x blocksize 0x%x dblocks %Ld rblocks %Ld\n",
		sbp->sb_magicnum, sbp->sb_blocksize,
		sbp->sb_dblocks, sbp->sb_rblocks);
	printk("rextents %Ld uuid %s logstart %s\n",
		sbp->sb_rextents,
		xfs_fmtuuid(&sbp->sb_uuid),
		xfs_fmtfsblock(sbp->sb_logstart, NULL));
	printk("rootino %s ",
		xfs_fmtino(sbp->sb_rootino, NULL));
	printk("rbmino %s ",
		xfs_fmtino(sbp->sb_rbmino, NULL));
	printk("rsumino %s\n",
		xfs_fmtino(sbp->sb_rsumino, NULL));
	printk("rextsize 0x%x agblocks 0x%x agcount 0x%x rbmblocks 0x%x\n",
		sbp->sb_rextsize,
		sbp->sb_agblocks,
		sbp->sb_agcount,
		sbp->sb_rbmblocks);
	printk("logblocks 0x%x versionnum 0x%x sectsize 0x%x inodesize 0x%x\n",
		sbp->sb_logblocks,
		sbp->sb_versionnum,
		sbp->sb_sectsize,
		sbp->sb_inodesize);
	printk("inopblock 0x%x blocklog 0x%x sectlog 0x%x inodelog 0x%x\n",
		sbp->sb_inopblock,
		sbp->sb_blocklog,
		sbp->sb_sectlog,
		sbp->sb_inodelog);
	printk("inopblog %d agblklog %d rextslog %d inprogress %d imax_pct %d\n",
		sbp->sb_inopblog,
		sbp->sb_agblklog,
		sbp->sb_rextslog,
		sbp->sb_inprogress,
		sbp->sb_imax_pct);
	printk("icount %Lx ifree %Lx fdblocks %Lx frextents %Lx\n",
		sbp->sb_icount,
		sbp->sb_ifree,
		sbp->sb_fdblocks,
		sbp->sb_frextents);
	printk("uquotino %s ", xfs_fmtino(sbp->sb_uquotino, NULL));
	printk("pquotino %s ", xfs_fmtino(sbp->sb_pquotino, NULL));
	printk("qflags 0x%x flags 0x%x shared_vn %d inoaligmt %d\n",
		sbp->sb_qflags, sbp->sb_flags, sbp->sb_shared_vn,
		sbp->sb_inoalignmt);
	printk("unit %d width %d dirblklog %d\n",
		sbp->sb_unit, sbp->sb_width, sbp->sb_dirblklog);
}


/*
 * Print out an XFS transaction structure.  Print summaries for
 * each of the items.
 */
static void
xfsidbg_xtp(xfs_trans_t *tp)
{
	xfs_log_item_chunk_t	*licp;
	xfs_log_item_desc_t	*lidp;
	int			i;
	int			chunk;
	static char *xtp_flags[] = {
		"dirty",	/* 0x1 */
		"sb_dirty",	/* 0x2 */
		"perm_log_res",	/* 0x4 */
		"sync",         /* 0x08 */
		"dq_dirty",     /* 0x10 */
		0
		};
	static char *lid_flags[] = {
		"dirty",	/* 0x1 */
		"pinned",	/* 0x2 */
		"sync unlock",	/* 0x4 */
		0
		};

	printk("tp 0x%p type ", tp);
	switch (tp->t_type) {
	case XFS_TRANS_SETATTR_NOT_SIZE: printk("SETATTR_NOT_SIZE");	break;
	case XFS_TRANS_SETATTR_SIZE:	printk("SETATTR_SIZE");	break;
	case XFS_TRANS_INACTIVE:	printk("INACTIVE");		break;
	case XFS_TRANS_CREATE:		printk("CREATE");		break;
	case XFS_TRANS_CREATE_TRUNC:	printk("CREATE_TRUNC");	break;
	case XFS_TRANS_TRUNCATE_FILE:	printk("TRUNCATE_FILE");	break;
	case XFS_TRANS_REMOVE:		printk("REMOVE");		break;
	case XFS_TRANS_LINK:		printk("LINK");		break;
	case XFS_TRANS_RENAME:		printk("RENAME");		break;
	case XFS_TRANS_MKDIR:		printk("MKDIR");		break;
	case XFS_TRANS_RMDIR:		printk("RMDIR");		break;
	case XFS_TRANS_SYMLINK:		printk("SYMLINK");		break;
	case XFS_TRANS_SET_DMATTRS:	printk("SET_DMATTRS");		break;
	case XFS_TRANS_GROWFS:		printk("GROWFS");		break;
	case XFS_TRANS_STRAT_WRITE:	printk("STRAT_WRITE");		break;
	case XFS_TRANS_DIOSTRAT:	printk("DIOSTRAT");		break;
	case XFS_TRANS_WRITE_SYNC:	printk("WRITE_SYNC");		break;
	case XFS_TRANS_WRITEID:		printk("WRITEID");		break;
	case XFS_TRANS_ADDAFORK:	printk("ADDAFORK");		break;
	case XFS_TRANS_ATTRINVAL:	printk("ATTRINVAL");		break;
	case XFS_TRANS_ATRUNCATE:	printk("ATRUNCATE");		break;
	case XFS_TRANS_ATTR_SET:	printk("ATTR_SET");		break;
	case XFS_TRANS_ATTR_RM:		printk("ATTR_RM");		break;
	case XFS_TRANS_ATTR_FLAG:	printk("ATTR_FLAG");		break;
	case XFS_TRANS_CLEAR_AGI_BUCKET:  printk("CLEAR_AGI_BUCKET");	break;
	case XFS_TRANS_QM_SBCHANGE:	printk("QM_SBCHANGE"); 	break;
	case XFS_TRANS_QM_QUOTAOFF:	printk("QM_QUOTAOFF"); 	break;
	case XFS_TRANS_QM_DQALLOC:	printk("QM_DQALLOC");		break;
	case XFS_TRANS_QM_SETQLIM:	printk("QM_SETQLIM");		break;
	case XFS_TRANS_QM_DQCLUSTER:	printk("QM_DQCLUSTER");	break;
	case XFS_TRANS_QM_QINOCREATE:	printk("QM_QINOCREATE");	break;
	case XFS_TRANS_QM_QUOTAOFF_END:	printk("QM_QOFF_END");		break;
	case XFS_TRANS_SB_UNIT:		printk("SB_UNIT");		break;
	case XFS_TRANS_FSYNC_TS:	printk("FSYNC_TS");		break;
	case XFS_TRANS_GROWFSRT_ALLOC:	printk("GROWFSRT_ALLOC");	break;
	case XFS_TRANS_GROWFSRT_ZERO:	printk("GROWFSRT_ZERO");	break;
	case XFS_TRANS_GROWFSRT_FREE:	printk("GROWFSRT_FREE");	break;

	default:			printk("0x%x", tp->t_type);	break;
	}
	printk(" mount 0x%p\n", tp->t_mountp);
	printk("flags ");
	printflags(tp->t_flags, xtp_flags,"xtp");
	printk("\n");
	printk("callback 0x%p forw 0x%p back 0x%p\n",
		&tp->t_logcb, tp->t_forw, tp->t_back);
	printk("log res %d block res %d block res used %d\n",
		tp->t_log_res, tp->t_blk_res, tp->t_blk_res_used);
	printk("rt res %d rt res used %d\n", tp->t_rtx_res,
		tp->t_rtx_res_used);
	printk("ticket 0x%x lsn %s\n",
		(uint32_t) tp->t_ticket, xfs_fmtlsn(&tp->t_lsn));
	printk("callback 0x%p callarg 0x%p\n",
		tp->t_callback, tp->t_callarg);
	printk("icount delta %ld ifree delta %ld\n",
		tp->t_icount_delta, tp->t_ifree_delta);
	printk("blocks delta %ld res blocks delta %ld\n",
		tp->t_fdblocks_delta, tp->t_res_fdblocks_delta);
	printk("rt delta %ld res rt delta %ld\n",
		tp->t_frextents_delta, tp->t_res_frextents_delta);
	printk("ag freeblks delta %ld ag flist delta %ld ag btree delta %ld\n",
		tp->t_ag_freeblks_delta, tp->t_ag_flist_delta,
		tp->t_ag_btree_delta);
	printk("dblocks delta %ld agcount delta %ld imaxpct delta %ld\n",
		tp->t_dblocks_delta, tp->t_agcount_delta, tp->t_imaxpct_delta);
	printk("rextsize delta %ld rbmblocks delta %ld\n",
		tp->t_rextsize_delta, tp->t_rbmblocks_delta);
	printk("rblocks delta %ld rextents delta %ld rextslog delta %ld\n",
		tp->t_rblocks_delta, tp->t_rextents_delta,
		tp->t_rextslog_delta);
	printk("dqinfo 0x%p\n", tp->t_dqinfo);
	printk("log items:\n");
	licp = &tp->t_items;
	chunk = 0;
	while (licp != NULL) {
		if (XFS_LIC_ARE_ALL_FREE(licp)) {
			licp = licp->lic_next;
			chunk++;
			continue;
		}
		for (i = 0; i < licp->lic_unused; i++) {
			if (XFS_LIC_ISFREE(licp, i)) {
				continue;
			}

			lidp = XFS_LIC_SLOT(licp, i);
			printk("\n");
			printk("chunk %d index %d item 0x%p size %d\n",
				chunk, i, lidp->lid_item, lidp->lid_size);
			printk("flags ");
			printflags(lidp->lid_flags, lid_flags,"lic");
			printk("\n");
			xfsidbg_xlogitem(lidp->lid_item);
		}
		chunk++;
		licp = licp->lic_next;
	}
}

static void
xfsidbg_xtrans_res(
	xfs_mount_t	*mp)
{
	xfs_trans_reservations_t	*xtrp;

	xtrp = &mp->m_reservations;
	printk("write: %d\ttruncate: %d\trename: %d\n",
		xtrp->tr_write, xtrp->tr_itruncate, xtrp->tr_rename);
	printk("link: %d\tremove: %d\tsymlink: %d\n",
		xtrp->tr_link, xtrp->tr_remove, xtrp->tr_symlink);
	printk("create: %d\tmkdir: %d\tifree: %d\n",
		xtrp->tr_create, xtrp->tr_mkdir, xtrp->tr_ifree);
	printk("ichange: %d\tgrowdata: %d\tswrite: %d\n",
		xtrp->tr_ichange, xtrp->tr_growdata, xtrp->tr_swrite);
	printk("addafork: %d\twriteid: %d\tattrinval: %d\n",
		xtrp->tr_addafork, xtrp->tr_writeid, xtrp->tr_attrinval);
	printk("attrset: %d\tattrrm: %d\tclearagi: %d\n",
		xtrp->tr_attrset, xtrp->tr_attrrm, xtrp->tr_clearagi);
	printk("growrtalloc: %d\tgrowrtzero: %d\tgrowrtfree: %d\n",
		xtrp->tr_growrtalloc, xtrp->tr_growrtzero, xtrp->tr_growrtfree);
}

