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

#include <linux/ctype.h>
#include <linux/kdb.h>
#include <linux/kdbprivate.h>
#include <linux/mm.h>
#include <linux/init.h>
#include <linux/version.h>

#include "xfs_fs.h"
#include "xfs_types.h"
#include "xfs_bit.h"
#include "xfs_log.h"
#include "xfs_inum.h"
#include "xfs_trans.h"
#include "xfs_trans_priv.h"
#include "xfs_sb.h"
#include "xfs_ag.h"
#include "xfs_dir2.h"
#include "xfs_dmapi.h"
#include "xfs_mount.h"
#include "xfs_alloc.h"
#include "xfs_da_btree.h"
#include "xfs_alloc_btree.h"
#include "xfs_bmap_btree.h"
#include "xfs_ialloc_btree.h"
#include "xfs_dir2_sf.h"
#include "xfs_attr_sf.h"
#include "xfs_dinode.h"
#include "xfs_inode.h"
#include "xfs_btree.h"
#include "xfs_btree_trace.h"
#include "xfs_buf_item.h"
#include "xfs_inode_item.h"
#include "xfs_extfree_item.h"
#include "xfs_rw.h"
#include "xfs_bmap.h"
#include "xfs_attr.h"
#include "xfs_attr_leaf.h"
#include "xfs_dir2_data.h"
#include "xfs_dir2_leaf.h"
#include "xfs_dir2_block.h"
#include "xfs_dir2_node.h"
#include "xfs_dir2_trace.h"
#include "xfs_log_priv.h"
#include "xfs_log_recover.h"
#include "xfs_quota.h"
#include "quota/xfs_qm.h"
#include "xfs_iomap.h"
#include "xfs_buf.h"
#include "xfs_filestream.h"

MODULE_AUTHOR("Silicon Graphics, Inc.");
MODULE_DESCRIPTION("Additional kdb commands for debugging XFS");
MODULE_LICENSE("GPL");

#define qprintf	kdb_printf

/*
 * Command table functions. (tracing)
 */
#ifdef XFS_ALLOC_TRACE
static void	xfsidbg_xalatrace(int);
static void	xfsidbg_xalbtrace(xfs_agblock_t);
static void	xfsidbg_xalgtrace(xfs_agnumber_t);
static void	xfsidbg_xalmtrace(xfs_mount_t *);
static void	xfsidbg_xalttrace(int);
#endif
#ifdef XFS_ATTR_TRACE
static void	xfsidbg_xattrtrace(int);
#endif
#ifdef XFS_BLI_TRACE
static void	xfsidbg_xblitrace(xfs_buf_log_item_t *);
#endif
#ifdef XFS_BMAP_TRACE
static void	xfsidbg_xbtatrace(int, struct ktrace *, int);
static void	xfsidbg_xbmitrace(xfs_inode_t *);
static void	xfsidbg_xbmstrace(xfs_inode_t *);
static void	xfsidbg_xbxatrace(int);
static void	xfsidbg_xbxitrace(xfs_inode_t *);
static void	xfsidbg_xbxstrace(xfs_inode_t *);
#endif
#ifdef XFS_ILOCK_TRACE
static void	xfsidbg_xilock_trace(xfs_inode_t *);
#endif
#ifdef XFS_DIR2_TRACE
static void	xfsidbg_xdir2atrace(int);
static void	xfsidbg_xdir2itrace(xfs_inode_t *);
#endif
#ifdef XFS_LOG_TRACE
static void	xfsidbg_xiclogtrace(xlog_in_core_t *);
static void	xfsidbg_xlog_granttrace(xlog_t *);
#endif
#ifdef XFS_DQUOT_TRACE
static void	xfsidbg_xqm_dqtrace(xfs_dquot_t *);
#endif
#ifdef XFS_FILESTREAMS_TRACE
static void	xfsidbg_filestreams_trace(int);
#endif
#ifdef	XFS_INODE_TRACE
/*
 * Print a inode trace entry.
 */
static int	xfs_itrace_pr_entry(ktrace_entry_t *ktep);
#endif


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
static void	xfsidbg_xbirec(xfs_bmbt_irec_t *r);
static void	xfsidbg_xbmalla(xfs_bmalloca_t *);
static void	xfsidbg_xbmbtrec(xfs_bmbt_rec_host_t *);
static void	xfsidbg_xbroot(xfs_inode_t *);
static void	xfsidbg_xbroota(xfs_inode_t *);
static void	xfsidbg_xbtcur(xfs_btree_cur_t *);
static void	xfsidbg_xbuf(xfs_buf_t *);
static void	xfsidbg_xbuf_real(xfs_buf_t *, int);
static void	xfsidbg_xarg(int);
static void	xfsidbg_xchksum(uint *);
static void	xfsidbg_xdaargs(xfs_da_args_t *);
static void	xfsidbg_xdabuf(xfs_dabuf_t *);
static void	xfsidbg_xdanode(xfs_da_intnode_t *);
static void	xfsidbg_xdastate(xfs_da_state_t *);
static void	xfsidbg_xdir2free(xfs_dir2_free_t *);
static void	xfsidbg_xdir2sf(xfs_dir2_sf_t *);
static void	xfsidbg_xexlist(xfs_inode_t *);
static void	xfsidbg_xflist(xfs_bmap_free_t *);
static void	xfsidbg_xhelp(void);
static void	xfsidbg_xiclog(xlog_in_core_t *);
static void	xfsidbg_xiclogall(xlog_in_core_t *);
static void	xfsidbg_xiclogcb(xlog_in_core_t *);
static void	xfsidbg_xinodes(xfs_mount_t *);
static void	xfsidbg_delayed_blocks(xfs_mount_t *);
static void	xfsidbg_xinodes_quiesce(xfs_mount_t *);
static void	xfsidbg_xlog(xlog_t *);
static void	xfsidbg_xlog_ritem(xlog_recover_item_t *);
static void	xfsidbg_xlog_rtrans(xlog_recover_t *);
static void	xfsidbg_xlog_rtrans_entire(xlog_recover_t *);
static void	xfsidbg_xlog_tic(xlog_ticket_t *);
static void	xfsidbg_xlogitem(xfs_log_item_t *);
static void	xfsidbg_xmount(xfs_mount_t *);
static void	xfsidbg_xnode(xfs_inode_t *ip);
static void	xfsidbg_xperag(xfs_mount_t *);
static void	xfsidbg_xqm_diskdq(xfs_disk_dquot_t *);
static void	xfsidbg_xqm_dqattached_inos(xfs_mount_t *);
static void	xfsidbg_xqm_dquot(xfs_dquot_t *);
static void	xfsidbg_xqm_mplist(xfs_mount_t *);
static void	xfsidbg_xqm_qinfo(xfs_mount_t *mp);
static void	xfsidbg_xqm_tpdqinfo(xfs_trans_t *tp);
static void	xfsidbg_xsb(xfs_sb_t *);
static void	xfsidbg_xsb_convert(xfs_dsb_t *);
static void	xfsidbg_xtp(xfs_trans_t *);
static void	xfsidbg_xtrans_res(xfs_mount_t *);
#ifdef CONFIG_XFS_QUOTA
static void	xfsidbg_xqm(void);
static void	xfsidbg_xqm_htab(void);
static void	xfsidbg_xqm_freelist_print(xfs_frlist_t *qlist, char *title);
static void	xfsidbg_xqm_freelist(void);
#endif

#ifdef XFS_BMAP_TRACE
static void	xfs_convert_extent(xfs_bmbt_rec_32_t *, xfs_dfiloff_t *,
				xfs_dfsbno_t *, xfs_dfilblks_t *, int *);
#endif

/*
 * Prototypes for static functions.
 */
#ifdef XFS_ALLOC_TRACE
static int	xfs_alloc_trace_entry(ktrace_entry_t *ktep);
#endif
#ifdef XFS_ATTR_TRACE
static int	xfs_attr_trace_entry(ktrace_entry_t *ktep);
#endif
#ifdef XFS_BMAP_TRACE
static int	xfs_bmap_trace_entry(ktrace_entry_t *ktep);
#endif
#ifdef XFS_DIR2_TRACE
static int	xfs_dir2_trace_entry(ktrace_entry_t *ktep);
#endif
#ifdef XFS_FILESTREAMS_TRACE
static void	xfs_filestreams_trace_entry(ktrace_entry_t *ktep);
#endif
#ifdef XFS_RW_TRACE
static void	xfs_bunmap_trace_entry(ktrace_entry_t   *ktep);
static void	xfs_rw_enter_trace_entry(ktrace_entry_t *ktep);
static void	xfs_page_trace_entry(ktrace_entry_t *ktep);
static int	xfs_rw_trace_entry(ktrace_entry_t *ktep);
#endif
static void	xfs_broot(xfs_inode_t *ip, xfs_ifork_t *f);
static void	xfs_buf_item_print(xfs_buf_log_item_t *blip, int summary);
static void	xfs_dastate_path(xfs_da_state_path_t *p);
static void	xfs_dir2data(void *addr, int size);
static void	xfs_dir2leaf(xfs_dir2_leaf_t *leaf, int size);
static void	xfs_dquot_item_print(xfs_dq_logitem_t *lip, int summary);
static void	xfs_efd_item_print(xfs_efd_log_item_t *efdp, int summary);
static void	xfs_efi_item_print(xfs_efi_log_item_t *efip, int summary);
static char *	xfs_fmtformat(xfs_dinode_fmt_t f);
char *		xfs_fmtfsblock(xfs_fsblock_t bno, xfs_mount_t *mp);
static char *	xfs_fmtino(xfs_ino_t ino, xfs_mount_t *mp);
static char *	xfs_fmtlsn(xfs_lsn_t *lsnp);
static char *	xfs_fmtmode(int m);
static char *	xfs_fmtsize(size_t i);
static char *	xfs_fmtuuid(uuid_t *);
static void	xfs_inode_item_print(xfs_inode_log_item_t *ilip, int summary);
static void	xfs_inodebuf(xfs_buf_t *bp);
static void	xfs_prdinode_incore(xfs_icdinode_t *dip);
static void	xfs_qoff_item_print(xfs_qoff_logitem_t *lip, int summary);
static void	xfs_xexlist_fork(xfs_inode_t *ip, int whichfork);
static void	xfs_xnode_fork(char *name, xfs_ifork_t *f);


/* kdb wrappers */

static int	kdbm_xfs_xagf(
	int	argc,
	const char **argv)
{
	unsigned long addr;
	int nextarg = 1;
	long offset = 0;
	int diag;

	if (argc != 1)
		return KDB_ARGCOUNT;
	diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL);
	if (diag)
		return diag;

	xfsidbg_xagf((xfs_agf_t *)addr);
	return 0;
}

static int	kdbm_xfs_xagi(
	int	argc,
	const char **argv)
{
	unsigned long addr;
	int nextarg = 1;
	long offset = 0;
	int diag;

	if (argc != 1)
		return KDB_ARGCOUNT;
	diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL);
	if (diag)
		return diag;

	xfsidbg_xagi((xfs_agi_t *)addr);
	return 0;
}

static int	kdbm_xfs_xaildump(
	int	argc,
	const char **argv)
{
	unsigned long addr;
	int nextarg = 1;
	long offset = 0;
	int diag;

	if (argc != 1)
		return KDB_ARGCOUNT;
	diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL);
	if (diag)
		return diag;

	xfsidbg_xaildump((xfs_mount_t *) addr);
	return 0;
}

#ifdef XFS_ALLOC_TRACE
static int	kdbm_xfs_xalatrace(
	int	argc,
	const char **argv)
{
	unsigned long addr;
	int nextarg = 1;
	long offset = 0;
	int diag;

	if (argc != 1)
		return KDB_ARGCOUNT;

	diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL);
	if (diag)
		return diag;

	xfsidbg_xalatrace((int) addr);
	return 0;
}

static int	kdbm_xfs_xalbtrace(
	int	argc,
	const char **argv)
{
	unsigned long addr;
	int nextarg = 1;
	long offset = 0;
	int diag;

	if (argc != 1)
		return KDB_ARGCOUNT;

	diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL);
	if (diag)
		return diag;

	xfsidbg_xalbtrace((xfs_agblock_t) addr);
	return 0;
}

static int	kdbm_xfs_xalgtrace(
	int	argc,
	const char **argv)
{
	unsigned long addr;
	int nextarg = 1;
	long offset = 0;
	int diag;

	if (argc != 1)
		return KDB_ARGCOUNT;

	diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL);
	if (diag)
		return diag;

	xfsidbg_xalgtrace((xfs_agnumber_t) addr);
	return 0;
}
#endif

#ifdef XFS_ATTR_TRACE
static int	kdbm_xfs_xattrtrace(
	int	argc,
	const char **argv)
{
	unsigned long addr;
	int nextarg = 1;
	long offset = 0;
	int diag;

	if (argc != 1)
		return KDB_ARGCOUNT;

	diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL);
	if (diag)
		return diag;

	xfsidbg_xattrtrace((int) addr);
	return 0;
}
#endif

#ifdef XFS_BLI_TRACE
static int	kdbm_xfs_xblitrace(
	int	argc,
	const char **argv)
{
	unsigned long addr;
	int nextarg = 1;
	long offset = 0;
	int diag;

	if (argc != 1)
		return KDB_ARGCOUNT;

	diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL);
	if (diag)
		return diag;

	xfsidbg_xblitrace((xfs_buf_log_item_t *) addr);
	return 0;
}
#endif

#ifdef XFS_BMAP_TRACE
static int	kdbm_xfs_xbmatrace(
	int	argc,
	const char **argv)
{
	unsigned long addr;
	int nextarg = 1;
	long offset = 0;
	int diag;

	if (argc != 1)
		return KDB_ARGCOUNT;

	diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL);
	if (diag)
		return diag;

	xfsidbg_xbtatrace(XFS_BTNUM_BMAP, xfs_bmbt_trace_buf, (int)addr);
	return 0;
}

static int	kdbm_xfs_xbtaatrace(
	int	argc,
	const char **argv)
{
	unsigned long addr;
	int nextarg = 1;
	long offset = 0;
	int diag;

	if (argc != 1)
		return KDB_ARGCOUNT;

	diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL);
	if (diag)
		return diag;

	/* also contains XFS_BTNUM_CNT */
	xfsidbg_xbtatrace(XFS_BTNUM_BNO, xfs_allocbt_trace_buf, (int)addr);
	return 0;
}

static int	kdbm_xfs_xbtiatrace(
	int	argc,
	const char **argv)
{
	unsigned long addr;
	int nextarg = 1;
	long offset = 0;
	int diag;

	if (argc != 1)
		return KDB_ARGCOUNT;

	diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL);
	if (diag)
		return diag;

	xfsidbg_xbtatrace(XFS_BTNUM_INO, xfs_inobt_trace_buf, (int)addr);
	return 0;
}


static int	kdbm_xfs_xbmitrace(
	int	argc,
	const char **argv)
{
	unsigned long addr;
	int nextarg = 1;
	long offset = 0;
	int diag;

	if (argc != 1)
		return KDB_ARGCOUNT;

	diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL);
	if (diag)
		return diag;

	xfsidbg_xbmitrace((xfs_inode_t *) addr);
	return 0;
}

static int	kdbm_xfs_xbmstrace(
	int	argc,
	const char **argv)
{
	unsigned long addr;
	int nextarg = 1;
	long offset = 0;
	int diag;

	if (argc != 1)
		return KDB_ARGCOUNT;

	diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL);
	if (diag)
		return diag;

	xfsidbg_xbmstrace((xfs_inode_t *) addr);
	return 0;
}

static int	kdbm_xfs_xbxatrace(
	int	argc,
	const char **argv)
{
	unsigned long addr;
	int nextarg = 1;
	long offset = 0;
	int diag;

	if (argc != 1)
		return KDB_ARGCOUNT;

	diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL);
	if (diag)
		return diag;

	xfsidbg_xbxatrace((int) addr);
	return 0;
}

static int	kdbm_xfs_xbxitrace(
	int	argc,
	const char **argv)
{
	unsigned long addr;
	int nextarg = 1;
	long offset = 0;
	int diag;

	if (argc != 1)
		return KDB_ARGCOUNT;

	diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL);
	if (diag)
		return diag;

	xfsidbg_xbxitrace((xfs_inode_t *) addr);
	return 0;
}

static int	kdbm_xfs_xbxstrace(
	int	argc,
	const char **argv)
{
	unsigned long addr;
	int nextarg = 1;
	long offset = 0;
	int diag;

	if (argc != 1)
		return KDB_ARGCOUNT;

	diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL);
	if (diag)
		return diag;

	xfsidbg_xbxstrace((xfs_inode_t *) addr);
	return 0;
}
#endif

#ifdef XFS_DIR2_TRACE
static int	kdbm_xfs_xdir2atrace(
	int	argc,
	const char **argv)
{
	unsigned long addr;
	int nextarg = 1;
	long offset = 0;
	int diag;

	if (argc != 1)
		return KDB_ARGCOUNT;

	diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL);
	if (diag)
		return diag;

	xfsidbg_xdir2atrace((int) addr);
	return 0;
}

static int	kdbm_xfs_xdir2itrace(
	int	argc,
	const char **argv)
{
	unsigned long addr;
	int nextarg = 1;
	long offset = 0;
	int diag;

	if (argc != 1)
		return KDB_ARGCOUNT;

	diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL);
	if (diag)
		return diag;

	xfsidbg_xdir2itrace((xfs_inode_t *) addr);
	return 0;
}
#endif

#ifdef XFS_LOG_TRACE
static int	kdbm_xfs_xiclogtrace(
	int	argc,
	const char **argv)
{
	unsigned long addr;
	int nextarg = 1;
	long offset = 0;
	int diag;

	if (argc != 1)
		return KDB_ARGCOUNT;

	diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL);
	if (diag)
		return diag;

	xfsidbg_xiclogtrace((xlog_in_core_t *) addr);
	return 0;
}
#endif

#ifdef XFS_ILOCK_TRACE
static int	kdbm_xfs_xilock_trace(
	int	argc,
	const char **argv)
{
	unsigned long addr;
	int nextarg = 1;
	long offset = 0;
	int diag;

	if (argc != 1)
		return KDB_ARGCOUNT;

	diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL);
	if (diag)
		return diag;

	xfsidbg_xilock_trace((xfs_inode_t *) addr);
	return 0;
}
#endif

#ifdef XFS_LOG_TRACE
static int	kdbm_xfs_xlog_granttrace(
	int	argc,
	const char **argv)
{
	unsigned long addr;
	int nextarg = 1;
	long offset = 0;
	int diag;

	if (argc != 1)
		return KDB_ARGCOUNT;

	diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL);
	if (diag)
		return diag;

	xfsidbg_xlog_granttrace((xlog_t *) addr);
	return 0;
}
#endif

#ifdef XFS_DQUOT_TRACE
static int	kdbm_xfs_xqm_dqtrace(
	int	argc,
	const char **argv)
{
	unsigned long addr;
	int nextarg = 1;
	long offset = 0;
	int diag;

	if (argc != 1)
		return KDB_ARGCOUNT;

	diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL);
	if (diag)
		return diag;

	xfsidbg_xqm_dqtrace((xfs_dquot_t *) addr);
	return 0;
}
#endif

#ifdef XFS_RW_TRACE
static int	kdbm_xfs_xrwtrace(
	int	argc,
	const char **argv)
{
	unsigned long	addr;
	int		nextarg = 1;
	long 		offset = 0;
	int 		diag;
	ktrace_entry_t	*ktep;
	ktrace_snap_t	kts;
	xfs_inode_t	*ip;

	if (argc != 1)
		return KDB_ARGCOUNT;

	diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL);
	if (diag)
		return diag;

	ip = (xfs_inode_t *) addr;
	if (ip->i_rwtrace == NULL) {
		qprintf("The inode trace buffer is not initialized\n");
		return 0;
	}
	qprintf("i_rwtrace = 0x%p\n", ip->i_rwtrace);
	ktep = ktrace_first(ip->i_rwtrace, &kts);
	while (ktep != NULL) {
		if (xfs_rw_trace_entry(ktep))
			qprintf("\n");
		ktep = ktrace_next(ip->i_rwtrace, &kts);
	}
	return 0;
}
#endif

static int	kdbm_xfs_xalloc(
	int	argc,
	const char **argv)
{
	unsigned long addr;
	int nextarg = 1;
	long offset = 0;
	int diag;

	if (argc != 1)
		return KDB_ARGCOUNT;
	diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL);
	if (diag)
		return diag;

	xfsidbg_xalloc((xfs_alloc_arg_t *) addr);
	return 0;
}

#ifdef XFS_ALLOC_TRACE
static int	kdbm_xfs_xalmtrace(
	int	argc,
	const char **argv)
{
	unsigned long addr;
	int nextarg = 1;
	long offset = 0;
	int diag;

	if (argc != 1)
		return KDB_ARGCOUNT;
	diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL);
	if (diag)
		return diag;

	xfsidbg_xalmtrace((xfs_mount_t *) addr);
	return 0;
}

static int	kdbm_xfs_xalttrace(
	int	argc,
	const char **argv)
{
	unsigned long addr;
	int nextarg = 1;
	long offset = 0;
	int diag;

	if (argc != 1)
		return KDB_ARGCOUNT;
	diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL);
	if (diag)
		return diag;

	xfsidbg_xalttrace((int) addr);
	return 0;
}
#endif /* XFS_ALLOC_TRACE */

#ifdef XFS_FILESTREAMS_TRACE
static int	kdbm_xfs_xfstrmtrace(
	int	argc,
	const char **argv)
{
	unsigned long addr;
	int nextarg = 1;
	long offset = 0;
	int diag;

	if (argc != 1)
		return KDB_ARGCOUNT;
	diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL);
	if (diag)
		return diag;

	xfsidbg_filestreams_trace((int) addr);
	return 0;
}
#endif /* XFS_FILESTREAMS_TRACE */

static int	kdbm_xfs_xattrcontext(
	int	argc,
	const char **argv)
{
	unsigned long addr;
	int nextarg = 1;
	long offset = 0;
	int diag;

	if (argc != 1)
		return KDB_ARGCOUNT;
	diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL);
	if (diag)
		return diag;

	xfsidbg_xattrcontext((xfs_attr_list_context_t *) addr);
	return 0;
}

static int	kdbm_xfs_xattrleaf(
	int	argc,
	const char **argv)
{
	unsigned long addr;
	int nextarg = 1;
	long offset = 0;
	int diag;

	if (argc != 1)
		return KDB_ARGCOUNT;
	diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL);
	if (diag)
		return diag;

	xfsidbg_xattrleaf((xfs_attr_leafblock_t *) addr);
	return 0;
}

static int	kdbm_xfs_xattrsf(
	int	argc,
	const char **argv)
{
	unsigned long addr;
	int nextarg = 1;
	long offset = 0;
	int diag;

	if (argc != 1)
		return KDB_ARGCOUNT;
	diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL);
	if (diag)
		return diag;

	xfsidbg_xattrsf((xfs_attr_shortform_t *) addr);
	return 0;
}

static int	kdbm_xfs_xbirec(
	int	argc,
	const char **argv)
{
	unsigned long addr;
	int nextarg = 1;
	long offset = 0;
	int diag;

	if (argc != 1)
		return KDB_ARGCOUNT;
	diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL);
	if (diag)
		return diag;

	xfsidbg_xbirec((xfs_bmbt_irec_t *) addr);
	return 0;
}

static int	kdbm_xfs_xbmalla(
	int	argc,
	const char **argv)
{
	unsigned long addr;
	int nextarg = 1;
	long offset = 0;
	int diag;

	if (argc != 1)
		return KDB_ARGCOUNT;
	diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL);
	if (diag)
		return diag;

	xfsidbg_xbmalla((xfs_bmalloca_t *)addr);
	return 0;
}

static int	kdbm_xfs_xbrec(
	int	argc,
	const char **argv)
{
	unsigned long addr;
	int nextarg = 1;
	long offset = 0;
	int diag;

	if (argc != 1)
		return KDB_ARGCOUNT;
	diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL);
	if (diag)
		return diag;

	xfsidbg_xbmbtrec((xfs_bmbt_rec_host_t *) addr);
	return 0;
}

static int	kdbm_xfs_xbroot(
	int	argc,
	const char **argv)
{
	unsigned long addr;
	int nextarg = 1;
	long offset = 0;
	int diag;

	if (argc != 1)
		return KDB_ARGCOUNT;
	diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL);
	if (diag)
		return diag;

	xfsidbg_xbroot((xfs_inode_t *) addr);
	return 0;
}

static int	kdbm_xfs_xbroota(
	int	argc,
	const char **argv)
{
	unsigned long addr;
	int nextarg = 1;
	long offset = 0;
	int diag;

	if (argc != 1)
		return KDB_ARGCOUNT;
	diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL);
	if (diag)
		return diag;

	xfsidbg_xbroota((xfs_inode_t *) addr);
	return 0;
}

static int	kdbm_xfs_xbtcur(
	int	argc,
	const char **argv)
{
	unsigned long addr;
	int nextarg = 1;
	long offset = 0;
	int diag;

	if (argc != 1)
		return KDB_ARGCOUNT;
	diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL);
	if (diag)
		return diag;

	xfsidbg_xbtcur((xfs_btree_cur_t *) addr);
	return 0;
}

static int	kdbm_xfs_xbuf(
	int	argc,
	const char **argv)
{
	unsigned long addr;
	int nextarg = 1;
	long offset = 0;
	int diag;

	if (argc != 1)
		return KDB_ARGCOUNT;
	diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL);
	if (diag)
		return diag;

	xfsidbg_xbuf((xfs_buf_t *) addr);
	return 0;
}


static int	kdbm_xfs_xarg(
	int	argc,
	const char **argv)
{
	unsigned long addr;
	int nextarg = 1;
	long offset = 0;
	int diag;

	if (argc != 1)
		return KDB_ARGCOUNT;
	diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL);
	if (diag)
		return diag;

	xfsidbg_xarg((int) addr);
	return 0;
}

static int	kdbm_xfs_xchksum(
	int	argc,
	const char **argv)
{
	unsigned long addr;
	int nextarg = 1;
	long offset = 0;
	int diag;

	if (argc != 1)
		return KDB_ARGCOUNT;
	diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL);
	if (diag)
		return diag;

	xfsidbg_xchksum((uint *) addr);
	return 0;
}

static int	kdbm_xfs_xdaargs(
	int	argc,
	const char **argv)
{
	unsigned long addr;
	int nextarg = 1;
	long offset = 0;
	int diag;

	if (argc != 1)
		return KDB_ARGCOUNT;
	diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL);
	if (diag)
		return diag;

	xfsidbg_xdaargs((xfs_da_args_t *) addr);
	return 0;
}

static int	kdbm_xfs_xdabuf(
	int	argc,
	const char **argv)
{
	unsigned long addr;
	int nextarg = 1;
	long offset = 0;
	int diag;

	if (argc != 1)
		return KDB_ARGCOUNT;
	diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL);
	if (diag)
		return diag;

	xfsidbg_xdabuf((xfs_dabuf_t *) addr);
	return 0;
}

static int	kdbm_xfs_xdanode(
	int	argc,
	const char **argv)
{
	unsigned long addr;
	int nextarg = 1;
	long offset = 0;
	int diag;

	if (argc != 1)
		return KDB_ARGCOUNT;
	diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL);
	if (diag)
		return diag;

	xfsidbg_xdanode((xfs_da_intnode_t *) addr);
	return 0;
}

static int	kdbm_xfs_xdastate(
	int	argc,
	const char **argv)
{
	unsigned long addr;
	int nextarg = 1;
	long offset = 0;
	int diag;

	if (argc != 1)
		return KDB_ARGCOUNT;
	diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL);
	if (diag)
		return diag;

	xfsidbg_xdastate((xfs_da_state_t *) addr);
	return 0;
}

static int	kdbm_xfs_xdir2free(
	int	argc,
	const char **argv)
{
	unsigned long addr;
	int nextarg = 1;
	long offset = 0;
	int diag;

	if (argc != 1)
		return KDB_ARGCOUNT;
	diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL);
	if (diag)
		return diag;

	xfsidbg_xdir2free((xfs_dir2_free_t *) addr);
	return 0;
}

static int	kdbm_xfs_xdir2sf(
	int	argc,
	const char **argv)
{
	unsigned long addr;
	int nextarg = 1;
	long offset = 0;
	int diag;

	if (argc != 1)
		return KDB_ARGCOUNT;
	diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL);
	if (diag)
		return diag;

	xfsidbg_xdir2sf((xfs_dir2_sf_t *) addr);
	return 0;
}

static int	kdbm_xfs_xexlist(
	int	argc,
	const char **argv)
{
	unsigned long addr;
	int nextarg = 1;
	long offset = 0;
	int diag;

	if (argc != 1)
		return KDB_ARGCOUNT;
	diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL);
	if (diag)
		return diag;

	xfsidbg_xexlist((xfs_inode_t *) addr);
	return 0;
}

static int	kdbm_xfs_xflist(
	int	argc,
	const char **argv)
{
	unsigned long addr;
	int nextarg = 1;
	long offset = 0;
	int diag;

	if (argc != 1)
		return KDB_ARGCOUNT;
	diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL);
	if (diag)
		return diag;

	xfsidbg_xflist((xfs_bmap_free_t *) addr);
	return 0;
}

static int	kdbm_xfs_xhelp(
	int	argc,
	const char **argv)
{
	if (argc != 0)
		return KDB_ARGCOUNT;

	xfsidbg_xhelp();
	return 0;
}

static int	kdbm_xfs_xiclog(
	int	argc,
	const char **argv)
{
	unsigned long addr;
	int nextarg = 1;
	long offset = 0;
	int diag;

	if (argc != 1)
		return KDB_ARGCOUNT;
	diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL);
	if (diag)
		return diag;

	xfsidbg_xiclog((xlog_in_core_t *) addr);
	return 0;
}

static int	kdbm_xfs_xiclogall(
	int	argc,
	const char **argv)
{
	unsigned long addr;
	int nextarg = 1;
	long offset = 0;
	int diag;

	if (argc != 1)
		return KDB_ARGCOUNT;
	diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL);
	if (diag)
		return diag;

	xfsidbg_xiclogall((xlog_in_core_t *) addr);
	return 0;
}

static int	kdbm_xfs_xiclogcb(
	int	argc,
	const char **argv)
{
	unsigned long addr;
	int nextarg = 1;
	long offset = 0;
	int diag;

	if (argc != 1)
		return KDB_ARGCOUNT;
	diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL);
	if (diag)
		return diag;

	xfsidbg_xiclogcb((xlog_in_core_t *) addr);
	return 0;
}

static int	kdbm_xfs_xinodes(
	int	argc,
	const char **argv)
{
	unsigned long addr;
	int nextarg = 1;
	long offset = 0;
	int diag;

	if (argc != 1)
		return KDB_ARGCOUNT;
	diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL);
	if (diag)
		return diag;

	xfsidbg_xinodes((xfs_mount_t *) addr);
	return 0;
}

static int	kdbm_xfs_delayed_blocks(
	int	argc,
	const char **argv)
{
	unsigned long addr;
	int nextarg = 1;
	long offset = 0;
	int diag;

	if (argc != 1)
		return KDB_ARGCOUNT;
	diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL);
	if (diag)
		return diag;

	xfsidbg_delayed_blocks((xfs_mount_t *) addr);
	return 0;
}


static int	kdbm_xfs_xinodes_quiesce(
	int	argc,
	const char **argv)
{
	unsigned long addr;
	int nextarg = 1;
	long offset = 0;
	int diag;

	if (argc != 1)
		return KDB_ARGCOUNT;
	diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL);
	if (diag)
		return diag;

	xfsidbg_xinodes_quiesce((xfs_mount_t *) addr);
	return 0;
}

static int	kdbm_xfs_xlog(
	int	argc,
	const char **argv)
{
	unsigned long addr;
	int nextarg = 1;
	long offset = 0;
	int diag;

	if (argc != 1)
		return KDB_ARGCOUNT;
	diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL);
	if (diag)
		return diag;

	xfsidbg_xlog((xlog_t *) addr);
	return 0;
}

static int	kdbm_xfs_xlog_ritem(
	int	argc,
	const char **argv)
{
	unsigned long addr;
	int nextarg = 1;
	long offset = 0;
	int diag;

	if (argc != 1)
		return KDB_ARGCOUNT;
	diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL);
	if (diag)
		return diag;

	xfsidbg_xlog_ritem((xlog_recover_item_t *) addr);
	return 0;
}

static int	kdbm_xfs_xlog_rtrans(
	int	argc,
	const char **argv)
{
	unsigned long addr;
	int nextarg = 1;
	long offset = 0;
	int diag;

	if (argc != 1)
		return KDB_ARGCOUNT;
	diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL);
	if (diag)
		return diag;

	xfsidbg_xlog_rtrans((xlog_recover_t *) addr);
	return 0;
}

static int	kdbm_xfs_xlog_rtrans_entire(
	int	argc,
	const char **argv)
{
	unsigned long addr;
	int nextarg = 1;
	long offset = 0;
	int diag;

	if (argc != 1)
		return KDB_ARGCOUNT;
	diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL);
	if (diag)
		return diag;

	xfsidbg_xlog_rtrans_entire((xlog_recover_t *) addr);
	return 0;
}

static int	kdbm_xfs_xlog_tic(
	int	argc,
	const char **argv)
{
	unsigned long addr;
	int nextarg = 1;
	long offset = 0;
	int diag;

	if (argc != 1)
		return KDB_ARGCOUNT;
	diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL);
	if (diag)
		return diag;

	xfsidbg_xlog_tic((xlog_ticket_t *) addr);
	return 0;
}

static int	kdbm_xfs_xlogitem(
	int	argc,
	const char **argv)
{
	unsigned long addr;
	int nextarg = 1;
	long offset = 0;
	int diag;

	if (argc != 1)
		return KDB_ARGCOUNT;
	diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL);
	if (diag)
		return diag;

	xfsidbg_xlogitem((xfs_log_item_t *) addr);
	return 0;
}

static int	kdbm_xfs_xmount(
	int	argc,
	const char **argv)
{
	unsigned long addr;
	int nextarg = 1;
	long offset = 0;
	int diag;

	if (argc != 1)
		return KDB_ARGCOUNT;
	diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL);
	if (diag)
		return diag;

	xfsidbg_xmount((xfs_mount_t *) addr);
	return 0;
}

static int	kdbm_xfs_xnode(
	int	argc,
	const char **argv)
{
	xfs_inode_t *ip;
	unsigned long addr;
	int nextarg = 1;
	long offset = 0;
	int diag;
#ifdef	XFS_INODE_TRACE
	ktrace_entry_t	*ktep;
	ktrace_snap_t	kts;
#endif

	if (argc != 1)
		return KDB_ARGCOUNT;
	diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL);
	if (diag)
		return diag;
	ip = (xfs_inode_t *)addr;

	xfsidbg_xnode(ip);

#ifdef	XFS_INODE_TRACE
	kdb_printf("--> itrace @ 0x%lx/0x%p\n", addr, ip->i_trace);
	if (ip->i_trace == NULL)
		return 0;
	ktep = ktrace_first(ip->i_trace, &kts);
	while (ktep != NULL) {
		if (xfs_itrace_pr_entry(ktep))
			kdb_printf("\n");

		ktep = ktrace_next(ip->i_trace, &kts);
	}
#endif	/* XFS_INODE_TRACE */
	return 0;
}

static int	kdbm_xfs_xperag(
	int	argc,
	const char **argv)
{
	unsigned long addr;
	int nextarg = 1;
	long offset = 0;
	int diag;

	if (argc != 1)
		return KDB_ARGCOUNT;
	diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL);
	if (diag)
		return diag;

	xfsidbg_xperag((xfs_mount_t *) addr);
	return 0;
}

static int	kdbm_xfs_xqm_diskdq(
	int	argc,
	const char **argv)
{
	unsigned long addr;
	int nextarg = 1;
	long offset = 0;
	int diag;

	if (argc != 1)
		return KDB_ARGCOUNT;
	diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL);
	if (diag)
		return diag;

	xfsidbg_xqm_diskdq((xfs_disk_dquot_t *) addr);
	return 0;
}

static int	kdbm_xfs_xqm_dqattached_inos(
	int	argc,
	const char **argv)
{
	unsigned long addr;
	int nextarg = 1;
	long offset = 0;
	int diag;

	if (argc != 1)
		return KDB_ARGCOUNT;
	diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL);
	if (diag)
		return diag;

	xfsidbg_xqm_dqattached_inos((xfs_mount_t *) addr);
	return 0;
}

static int	kdbm_xfs_xqm_dquot(
	int	argc,
	const char **argv)
{
	unsigned long addr;
	int nextarg = 1;
	long offset = 0;
	int diag;

	if (argc != 1)
		return KDB_ARGCOUNT;
	diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL);
	if (diag)
		return diag;

	xfsidbg_xqm_dquot((xfs_dquot_t *) addr);
	return 0;
}

#ifdef	CONFIG_XFS_QUOTA
static int	kdbm_xfs_xqm(
	int	argc,
	const char **argv)
{
	if (argc != 0)
		return KDB_ARGCOUNT;

	xfsidbg_xqm();
	return 0;
}

static int	kdbm_xfs_xqm_freelist(
	int	argc,
	const char **argv)
{
	if (argc != 0)
		return KDB_ARGCOUNT;

	xfsidbg_xqm_freelist();
	return 0;
}

static int	kdbm_xfs_xqm_htab(
	int	argc,
	const char **argv)
{
	if (argc != 0)
		return KDB_ARGCOUNT;

	xfsidbg_xqm_htab();
	return 0;
}
#endif

static int	kdbm_xfs_xqm_mplist(
	int	argc,
	const char **argv)
{
	unsigned long addr;
	int nextarg = 1;
	long offset = 0;
	int diag;

	if (argc != 1)
		return KDB_ARGCOUNT;
	diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL);
	if (diag)
		return diag;

	xfsidbg_xqm_mplist((xfs_mount_t *) addr);
	return 0;
}

static int	kdbm_xfs_xqm_qinfo(
	int	argc,
	const char **argv)
{
	unsigned long addr;
	int nextarg = 1;
	long offset = 0;
	int diag;

	if (argc != 1)
		return KDB_ARGCOUNT;
	diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL);
	if (diag)
		return diag;

	xfsidbg_xqm_qinfo((xfs_mount_t *) addr);
	return 0;
}

static int	kdbm_xfs_xqm_tpdqinfo(
	int	argc,
	const char **argv)
{
	unsigned long addr;
	int nextarg = 1;
	long offset = 0;
	int diag;

	if (argc != 1)
		return KDB_ARGCOUNT;
	diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL);
	if (diag)
		return diag;

	xfsidbg_xqm_tpdqinfo((xfs_trans_t *) addr);
	return 0;
}

static int	kdbm_xfs_xsb(
	int	argc,
	const char **argv)
{
	unsigned long addr;
	unsigned long convert=0;
	int nextarg = 1;
	long offset = 0;
	int diag;

	if (argc != 1 && argc!=2)
		return KDB_ARGCOUNT;
	diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL);
	if (diag)
		return diag;
	if (argc==2) {
	    /* extra argument - conversion flag */
	    diag = kdbgetaddrarg(argc, argv, &nextarg, &convert, &offset, NULL);
	    if (diag)
		    return diag;
	}

	if (convert)
		xfsidbg_xsb_convert((xfs_dsb_t *) addr);
	else
		xfsidbg_xsb((xfs_sb_t *) addr);
	return 0;
}

static int	kdbm_xfs_xtp(
	int	argc,
	const char **argv)
{
	unsigned long addr;
	int nextarg = 1;
	long offset = 0;
	int diag;

	if (argc != 1)
		return KDB_ARGCOUNT;
	diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL);
	if (diag)
		return diag;

	xfsidbg_xtp((xfs_trans_t *) addr);
	return 0;
}

static int	kdbm_xfs_xtrans_res(
	int	argc,
	const char **argv)
{
	unsigned long addr;
	int nextarg = 1;
	long offset = 0;
	int diag;

	if (argc != 1)
		return KDB_ARGCOUNT;
	diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL);
	if (diag)
		return diag;

	xfsidbg_xtrans_res((xfs_mount_t *) addr);
	return 0;
}

static void
printflags(register uint64_t flags,
	register char **strings,
	register char *name)
{
	register uint64_t mask = 1;

	if (name)
		kdb_printf("%s 0x%llx <", name, (unsigned long long)flags);

	while (flags != 0 && *strings) {
		if (mask & flags) {
			kdb_printf("%s ", *strings);
			flags &= ~mask;
		}
		mask <<= 1;
		strings++;
	}

	if (name)
		kdb_printf("> ");

	return;
}

#ifdef	XFS_INODE_TRACE
/*
 * Print a inode trace entry.
 */
static int
xfs_itrace_pr_entry(ktrace_entry_t *ktep)
{
	char		funcname[128];
	kdb_symtab_t	symtab;


	if ((__psint_t)ktep->val[0] == 0)
		return 0;

	if (kdbnearsym((unsigned long)ktep->val[8], &symtab)) {
		unsigned long offval;

		offval = (unsigned long)ktep->val[8] - symtab.sym_start;

		if (offval)
			sprintf(funcname, "%s+0x%lx", symtab.sym_name, offval);
		else
			sprintf(funcname, "%s", symtab.sym_name);
	} else
		funcname[0] = '\0';


	switch ((__psint_t)ktep->val[0]) {
	case INODE_KTRACE_ENTRY:
		kdb_printf("entry to %s i_count = %ld",
						(char *)ktep->val[1],
						(long)ktep->val[3]);
		break;

	case INODE_KTRACE_EXIT:
		kdb_printf("exit from %s i_count = %lu",
						(char *)ktep->val[1],
						(long)ktep->val[3]);
		break;

	case INODE_KTRACE_HOLD:
		if ((__psint_t)ktep->val[3] != 1)
			kdb_printf("hold @%s:%ld(%s) i_count %ld => %ld ",
						(char *)ktep->val[1],
						(long)ktep->val[2],
						funcname,
						(long)ktep->val[3] - 1,
						(long)ktep->val[3]);
		else
			kdb_printf("get @%s:%ld(%s) i_count = %ld",
						(char *)ktep->val[1],
						(long)ktep->val[2],
						funcname,
						(long)ktep->val[3]);
		break;

	case INODE_KTRACE_REF:
		kdb_printf("ref @%s:%ld(%s) i_count = %ld",
						(char *)ktep->val[1],
						(long)ktep->val[2],
						funcname,
						(long)ktep->val[3]);
		break;

	case INODE_KTRACE_RELE:
		if ((__psint_t)ktep->val[3] != 1)
			kdb_printf("rele @%s:%ld(%s) i_count %ld => %ld ",
						(char *)ktep->val[1],
						(long)ktep->val[2],
						funcname,
						(long)ktep->val[3],
						(long)ktep->val[3] - 1);
		else
			kdb_printf("free @%s:%ld(%s) i_count = %ld",
						(char *)ktep->val[1],
						(long)ktep->val[2],
						funcname,
						(long)ktep->val[3]);
		break;

	default:
		kdb_printf("unknown intrace record\n");
		return 1;
	}

	kdb_printf("\n");

	kdb_printf("  cpu = %ld pid = %ld ",
			(long)ktep->val[6], (long)ktep->val[7]);

	if (kdbnearsym((unsigned long)ktep->val[4], &symtab)) {
		unsigned long offval;

		offval = (unsigned long)ktep->val[4] - symtab.sym_start;

		if (offval)
			kdb_printf("  ra = %s+0x%lx", symtab.sym_name, offval);
		else
			kdb_printf("  ra = %s", symtab.sym_name);
	} else
		kdb_printf("  ra = ?? 0x%p", (void *)ktep->val[4]);

	return 1;
}


/*
 * Print out the trace buffer attached to the given inode.
 */
static int	kdbm_iptrace(
	int	argc,
	const char **argv)
{
	int		diag;
	int		nextarg = 1;
	long		offset = 0;
	unsigned long	addr;
	xfs_inode_t	*ip;
	ktrace_entry_t	*ktep;
	ktrace_snap_t	kts;


	if (argc != 1)
		return KDB_ARGCOUNT;

	diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL);

	if (diag)
		return diag;

	ip = (xfs_inode_t *)addr;

	if (ip->i_trace == NULL) {
		kdb_printf("The inode trace buffer is not initialized\n");

		return 0;
	}

	kdb_printf("iptrace ip 0x%p\n", ip);

	ktep = ktrace_first(ip->i_trace, &kts);

	while (ktep != NULL) {
		if (xfs_itrace_pr_entry(ktep))
			kdb_printf("\n");

		ktep = ktrace_next(ip->i_trace, &kts);
	}

	return 0;
}
/*
 * Print out the trace buffer attached to the given inode.
 */
static int	kdbm_iptraceaddr(
	int	argc,
	const char **argv)
{
	int		diag;
	int		nextarg = 1;
	long		offset = 0;
	unsigned long	addr;
	struct ktrace	*kt;
	ktrace_entry_t	*ktep;
	ktrace_snap_t	kts;


	if (argc != 1)
		return KDB_ARGCOUNT;

	diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL);

	if (diag)
		return diag;

	kt = (struct ktrace *)addr;

	kdb_printf("iptraceaddr kt 0x%p\n", kt);

	ktep = ktrace_first(kt, &kts);

	while (ktep != NULL) {
		if (xfs_itrace_pr_entry(ktep))
			kdb_printf("\n");

		ktep = ktrace_next(kt, &kts);
	}

	return 0;
}
#endif	/* XFS_INODE_TRACE */


static char	*bp_flag_vals[] = {
/*  0 */ "READ", "WRITE", "MAPPED", "<unknown(3)>", "ASYNC",
/*  5 */ "DONE", "DELWRI",  "STALE", "FS_MANAGED", "<unknown(9)>",
/* 10 */ "<unknown(10)>", "ORDERED", "READ_AHEAD", "<unknown(13)>", "LOCK",
/* 15 */ "TRYLOCK", "DONT_BLOCK", "PAGE_CACHE", "PAGES", "RUN_QUEUES",
/* 20 */ "<unknown(20)>", "DELWRI_Q", "PAGE_LOCKED",
	 NULL };

static char	*iomap_flag_vals[] = {
	"EOF", "HOLE", "DELAY", "INVALID0x08",
	"INVALID0x10", "UNWRITTEN", "NEW", "INVALID0x80",
	NULL };


static char	*map_flags(unsigned long flags, char *mapping[])
{
	static	char	buffer[256];
	int	index;
	int	offset = 12;

	buffer[0] = '\0';

	for (index = 0; flags && mapping[index]; flags >>= 1, index++) {
		if (flags & 1) {
			if ((offset + strlen(mapping[index]) + 1) >= 80) {
				strcat(buffer, "\n	    ");
				offset = 12;
			} else if (offset > 12) {
				strcat(buffer, " ");
				offset++;
			}
			strcat(buffer, mapping[index]);
			offset += strlen(mapping[index]);
		}
	}

	return (buffer);
}

static char	*bp_flags(xfs_buf_flags_t bp_flag)
{
	return(map_flags((unsigned long) bp_flag, bp_flag_vals));
}

static int
kdbm_bp_flags(int argc, const char **argv)
{
	unsigned long flags;
	int diag;

	if (argc != 1)
		return KDB_ARGCOUNT;

	diag = kdbgetularg(argv[1], &flags);
	if (diag)
		return diag;

	kdb_printf("bp flags 0x%lx = %s\n", flags, bp_flags(flags));

	return 0;
}

static void
print_xfs_buf(
	xfs_buf_t	*bp,
	unsigned long	addr)
{
	unsigned long	age = (xfs_buf_age_centisecs * HZ) / 100;

	kdb_printf("xfs_buf_t at 0x%lx\n", addr);
	kdb_printf("  b_flags %s\n", bp_flags(bp->b_flags));
	kdb_printf("  b_target 0x%p b_hold %d b_next 0x%p b_prev 0x%p\n",
		   bp->b_target, bp->b_hold.counter,
		   list_entry(bp->b_list.next, xfs_buf_t, b_list),
		   list_entry(bp->b_list.prev, xfs_buf_t, b_list));
	kdb_printf("  b_hash 0x%p b_hash_next 0x%p b_hash_prev 0x%p\n",
		   bp->b_hash,
		   list_entry(bp->b_hash_list.next, xfs_buf_t, b_hash_list),
		   list_entry(bp->b_hash_list.prev, xfs_buf_t, b_hash_list));
	kdb_printf("  b_file_offset 0x%llx b_buffer_length 0x%llx b_addr 0x%p\n",
		   (unsigned long long) bp->b_file_offset,
		   (unsigned long long) bp->b_buffer_length,
		   bp->b_addr);
	kdb_printf("  b_bn 0x%llx b_count_desired 0x%lx\n",
		   (unsigned long long)bp->b_bn,
		   (unsigned long) bp->b_count_desired);
	kdb_printf("  b_queuetime %ld (now=%ld/age=%ld) b_io_remaining %d\n",
		   bp->b_queuetime, jiffies, bp->b_queuetime + age,
		   bp->b_io_remaining.counter);
	kdb_printf("  b_page_count %u b_offset 0x%x b_pages 0x%p b_error %u\n",
		   bp->b_page_count, bp->b_offset,
		   bp->b_pages, bp->b_error);
	kdb_printf("  &b_iowait 0x%p (%d) b_sema (%d) b_pincount (%d)\n",
		   &bp->b_iowait, bp->b_iowait.done,
		   bp->b_sema.count,
		   bp->b_pin_count.counter);
#ifdef XFS_BUF_LOCK_TRACKING
	kdb_printf("  last holder %d\n", bp->b_last_holder);
#endif
	if (bp->b_fspriv || bp->b_fspriv2) {
		kdb_printf(  "  b_fspriv 0x%p b_fspriv2 0x%p\n",
			   bp->b_fspriv, bp->b_fspriv2);
	}
}

static int
kdbm_bp(int argc, const char **argv)
{
	xfs_buf_t bp;
	unsigned long addr;
	long	offset=0;
	int nextarg;
	int diag;

	if (argc != 1)
		return KDB_ARGCOUNT;

	nextarg = 1;
	if ((diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL)) ||
	    (diag = kdb_getarea(bp, addr)))
		return diag;

	print_xfs_buf(&bp, addr);

	return 0;
}

static int
kdbm_bpdelay(int argc, const char **argv)
{
	struct list_head	*xfs_buftarg_list = xfs_get_buftarg_list();
	struct list_head	*curr, *next;
	xfs_buftarg_t		*tp, *n;
	xfs_buf_t		bp;
	unsigned long		addr, verbose = 0;
	int			diag, count = 0;

	if (argc > 1)
		return KDB_ARGCOUNT;

	if (argc == 1) {
		if ((diag = kdbgetularg(argv[1], &verbose))) {
			return diag;
		}
	}

	if (!verbose) {
		kdb_printf("index bp       pin   queuetime\n");
	}


	list_for_each_entry_safe(tp, n, xfs_buftarg_list, bt_list) {
		list_for_each_safe(curr, next, &tp->bt_delwrite_queue) {
			addr = (unsigned long)list_entry(curr, xfs_buf_t, b_list);
			if ((diag = kdb_getarea(bp, addr)))
				return diag;

			if (verbose) {
				print_xfs_buf(&bp, addr);
			} else {
				kdb_printf("%4d  0x%lx   %d   %ld\n",
					count++, addr,
					bp.b_pin_count.counter,
					bp.b_queuetime);
			}
		}
	}
	return 0;
}

static int
kdbm_iomap(int argc, const char **argv)
{
	xfs_iomap_t iomap;
	unsigned long addr;
	long offset=0;
	int nextarg;
	int diag;

	if (argc != 1)
		return KDB_ARGCOUNT;

	nextarg = 1;
	if ((diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL)))
		return diag;
	if ((diag = kdb_getarea(iomap, addr)))
		return diag;

	kdb_printf("iomap_t at 0x%lx\n", addr);
	kdb_printf("  bn 0x%llx offset 0x%Lx delta 0x%lx bsize 0x%llx\n",
		(long long) iomap.iomap_bn, (long long)iomap.iomap_offset,
		(unsigned long)iomap.iomap_delta, (long long)iomap.iomap_bsize);
	kdb_printf("  iomap_flags %s\n",
		map_flags(iomap.iomap_flags, iomap_flag_vals));

	return 0;
}

#ifdef XFS_BUF_TRACE
static int xfs_buf_trace_entry(ktrace_entry_t *ktep)
{
	unsigned long long daddr;

	daddr = ((unsigned long long)(unsigned long)ktep->val[8] << 32)
		| ((unsigned long long)(unsigned long)ktep->val[9]);

	kdb_printf("bp 0x%p [%s] (hold %lu lock %ld) data 0x%p",
		ktep->val[0],
		(char *)ktep->val[1],
		(unsigned long)ktep->val[3],
		(long)ktep->val[4],
		ktep->val[6]);
	kdb_symbol_print((unsigned long)ktep->val[7], NULL,
		KDB_SP_SPACEB|KDB_SP_PAREN|KDB_SP_NEWLINE);
	kdb_printf("    offset 0x%llx size 0x%lx task 0x%p\n",
		daddr, (long)ktep->val[10], ktep->val[5]);
	kdb_printf("    flags: %s\n", bp_flags((int)(long)ktep->val[2]));
	return 1;
}

static int
kdbm_bptrace_offset(int argc, const char **argv)
{
	long		mask = 0;
	unsigned long	got_offset = 0, offset = 0;
	int		diag;
	ktrace_entry_t	*ktep;
	ktrace_snap_t	kts;

	if (argc > 2)
		return KDB_ARGCOUNT;

	if (argc > 0) {
		diag = kdbgetularg(argv[1], &offset);
		if (diag)
			return diag;
		got_offset = 1;		/* allows tracing offset zero */
	}

	if (argc > 1) {
		diag = kdbgetularg(argv[1], &mask);	/* sign extent mask */
		if (diag)
			return diag;
	}

	ktep = ktrace_first(xfs_buf_trace_buf, &kts);
	do {
		unsigned long long daddr;

		if (ktep == NULL)
			break;
		daddr = ((unsigned long long)(unsigned long)ktep->val[8] << 32)
			| ((unsigned long long)(unsigned long)ktep->val[9]);
		if (got_offset && ((daddr & ~mask) != offset))
			continue;
		if (xfs_buf_trace_entry(ktep))
			kdb_printf("\n");
	} while ((ktep = ktrace_next(xfs_buf_trace_buf, &kts)) != NULL);
	return 0;
}

static int
kdbm_bptrace(int argc, const char **argv)
{
	unsigned long	addr = 0;
	int		diag, nextarg;
	long		offset = 0;
	char		*event_match = NULL;
	ktrace_entry_t	*ktep;
	ktrace_snap_t	kts;

	if (argc > 1)
		return KDB_ARGCOUNT;

	if (argc == 1) {
		if (isupper(argv[1][0]) || islower(argv[1][0])) {
			event_match = (char *)argv[1];
			kdb_printf("event match on \"%s\"\n", event_match);
			argc = 0;
		} else {
			nextarg = 1;
			diag = kdbgetaddrarg(argc, argv,
					&nextarg, &addr, &offset, NULL);
			if (diag) {
				kdb_printf("non-numeric arg: %s\n", argv[1]);
				return diag;
			}
		}
	}

	ktep = ktrace_first(xfs_buf_trace_buf, &kts);
	do {
		if (ktep == NULL)
			break;
		if (addr && (ktep->val[0] != (void *)addr))
			continue;
		if (event_match && strcmp((char *)ktep->val[1], event_match))
			continue;
		if (xfs_buf_trace_entry(ktep))
			qprintf("\n");
	} while ((ktep = ktrace_next(xfs_buf_trace_buf, &kts)) != NULL);

	return 0;
}
#endif

struct xif {
	char	*name;
	int	(*func)(int, const char **);
	char	*args;
	char	*help;
};

static struct xif xfsidbg_funcs[] = {
#ifdef XFS_INODE_TRACE
  {  "iptrace",	kdbm_iptrace,	"<iptrace>", "Dump inode Trace"},
  {  "iptraceaddr",	kdbm_iptraceaddr, "<iptrace>",
				"Dump inode Trace by Address"},
#endif
  {  "xagf",	kdbm_xfs_xagf,	"<agf>",
				"Dump XFS allocation group freespace" },
  {  "xagi",	kdbm_xfs_xagi,	"<agi>",
				"Dump XFS allocation group inode" },
  {  "xail",	kdbm_xfs_xaildump,	"<xfs_mount_t>",
				"Dump XFS AIL for a mountpoint" },
#ifdef XFS_ALLOC_TRACE
  {  "xalatrc",	kdbm_xfs_xalatrace,	"<count>",
				"Dump XFS alloc count trace" },
  {  "xalbtrc",	kdbm_xfs_xalbtrace,	"<xfs_agblock_t>",
				"Dump XFS alloc block trace" },
  {  "xalgtrc",	kdbm_xfs_xalgtrace,	"<xfs_agnumber_t>",
				"Dump XFS alloc alloc-group trace" },
#endif

  {  "xalloc",	kdbm_xfs_xalloc,	"<xfs_alloc_arg_t>",
				"Dump XFS allocation args structure" },
#ifdef XFS_ALLOC_TRACE
  {  "xalmtrc",	kdbm_xfs_xalmtrace,	"<xfs_mount_t>",
				"Dump XFS alloc mount-point trace" },
  {  "xalttrc",	kdbm_xfs_xalttrace,	"<tag>",
				"Dump XFS alloc trace by tag number" },
#endif
  {  "xarg",	kdbm_xfs_xarg,		"<value>",
				"Input XFS argument for next function" },
  {  "xattrcx",	kdbm_xfs_xattrcontext,	"<xfs_attr_list_context_t>",
				"Dump XFS attr_list context struct"},
  {  "xattrlf",	kdbm_xfs_xattrleaf,	"<xfs_attr_leafblock_t>",
				"Dump XFS attribute leaf block"},
  {  "xattrsf",	kdbm_xfs_xattrsf,	"<xfs_attr_shortform_t>",
				"Dump XFS attribute shortform"},
#ifdef XFS_ATTR_TRACE
  {  "xattrtr",	kdbm_xfs_xattrtrace,	"<count>",
				"Dump XFS attribute attr_list() trace" },
#endif
  {  "xbirec",	kdbm_xfs_xbirec,	"<xfs_bmbt_irec_t",
				"Dump XFS bmap incore record"},
#ifdef XFS_BLI_TRACE
  {  "xblitrc",	kdbm_xfs_xblitrace,	"<xfs_buf_log_item_t>",
				"Dump XFS buf log item trace" },
#endif
  {  "xbmalla",	kdbm_xfs_xbmalla,	"<xfs_bmalloca_t>",
				"Dump XFS bmalloc args structure"},
#ifdef XFS_BMAP_TRACE
  {  "xbmatrc",	kdbm_xfs_xbmatrace,	"<count>",
				"Dump XFS bmap btree count trace" },
  {  "xbmitrc",	kdbm_xfs_xbmitrace,	"<xfs_inode_t>",
				"Dump XFS bmap btree per-inode trace" },
  {  "xbmstrc",	kdbm_xfs_xbmstrace,	"<xfs_inode_t>",
				"Dump XFS bmap btree inode trace" },
  {  "xbtaatrc", kdbm_xfs_xbtaatrace,	"<count>",
				"Dump XFS alloc btree count trace" },
  {  "xbtiatrc", kdbm_xfs_xbtiatrace,	"<count>",
				"Dump XFS inobt btree count trace" },
#endif
  {  "xbrec",	kdbm_xfs_xbrec,		"<xfs_bmbt_rec_64_t>",
				"Dump XFS bmap record"},
  {  "xbroot",	kdbm_xfs_xbroot,	"<xfs_inode_t>",
				"Dump XFS bmap btree root (data)"},
  {  "xbroota",	kdbm_xfs_xbroota,	"<xfs_inode_t>",
				"Dump XFS bmap btree root (attr)"},
  {  "xbtcur",	kdbm_xfs_xbtcur,	"<xfs_btree_cur_t>",
				"Dump XFS btree cursor"},
  {  "xbuf",	kdbm_xfs_xbuf,		"<xfs_buf_t>",
				"Dump XFS data from a buffer"},
#ifdef XFS_BMAP_TRACE
  {  "xbxatrc",	kdbm_xfs_xbxatrace,	"<count>",
				"Dump XFS bmap extent count trace" },
  {  "xbxitrc",	kdbm_xfs_xbxitrace,	"<xfs_inode_t>",
				"Dump XFS bmap extent per-inode trace" },
  {  "xbxstrc",	kdbm_xfs_xbxstrace,	"<xfs_inode_t>",
				"Dump XFS bmap extent inode trace" },
#endif
  {  "xchksum",	kdbm_xfs_xchksum,	"<addr>", "Dump chksum" },
#ifdef XFS_DIR2_TRACE
  {  "xd2atrc",	kdbm_xfs_xdir2atrace,	"<count>",
				"Dump XFS directory v2 count trace" },
#endif
  {  "xd2free",	kdbm_xfs_xdir2free,	"<xfs_dir2_free_t>",
				"Dump XFS directory v2 freemap"},
#ifdef XFS_DIR2_TRACE
  {  "xd2itrc",	kdbm_xfs_xdir2itrace,	"<xfs_inode_t>",
				"Dump XFS directory v2 per-inode trace" },
#endif
  {  "xdaargs",	kdbm_xfs_xdaargs,	"<xfs_da_args_t>",
				"Dump XFS dir/attr args structure"},
  {  "xdabuf",	kdbm_xfs_xdabuf,	"<xfs_dabuf_t>",
				"Dump XFS dir/attr buf structure"},
  {  "xdanode",	kdbm_xfs_xdanode,	"<xfs_da_intnode_t>",
				"Dump XFS dir/attr node block"},
  {  "xdastat",	kdbm_xfs_xdastate,	"<xfs_da_state_t>",
				"Dump XFS dir/attr state_blk struct"},
  {  "xdelay",	kdbm_xfs_delayed_blocks,	"<xfs_mount_t>",
				"Dump delayed block totals"},
  {  "xdir2sf",	kdbm_xfs_xdir2sf,	"<xfs_dir2_sf_t>",
				"Dump XFS directory v2 shortform"},
  {  "xdiskdq",	kdbm_xfs_xqm_diskdq,	"<xfs_disk_dquot_t>",
				"Dump XFS ondisk dquot (quota) struct"},
  {  "xdqatt",	kdbm_xfs_xqm_dqattached_inos,	"<xfs_mount_t>",
				 "All incore inodes with dquots"},
  {  "xdqinfo",	kdbm_xfs_xqm_tpdqinfo,	"<xfs_trans_t>",
				"Dump dqinfo structure of a trans"},
#ifdef XFS_DQUOT_TRACE
  {  "xdqtrace",kdbm_xfs_xqm_dqtrace,	"<xfs_dquot_t>",
				"Dump trace of a given dquot" },
#endif
  {  "xdquot",	kdbm_xfs_xqm_dquot,	"<xfs_dquot_t>",
				"Dump XFS dquot (quota) structure"},
  {  "xexlist",	kdbm_xfs_xexlist,	"<xfs_inode_t>",
				"Dump XFS bmap extents in inode"},
  {  "xflist",	kdbm_xfs_xflist,	"<xfs_bmap_free_t>",
				"Dump XFS to-be-freed extent records"},
#ifdef XFS_FILESTREAMS_TRACE
  {  "xfstrmtrc",kdbm_xfs_xfstrmtrace,	"",
				"Dump filestreams trace buffer"},
#endif
  {  "xhelp",	kdbm_xfs_xhelp,		"",
				"Print idbg-xfs help"},
  {  "xicall",	kdbm_xfs_xiclogall,	"<xlog_in_core_t>",
				"Dump All XFS in-core logs"},
  {  "xiclog",	kdbm_xfs_xiclog,	"<xlog_in_core_t>",
				"Dump XFS in-core log"},
#ifdef XFS_LOG_TRACE
  {  "xictrc",	kdbm_xfs_xiclogtrace,	"<xlog_in_core_t>",
				"Dump XFS in-core log trace" },
#endif
#ifdef XFS_ILOCK_TRACE
  {  "xilocktrc",kdbm_xfs_xilock_trace,	"<xfs_inode_t>",
				"Dump XFS ilock trace" },
#endif
  {  "xinodes",	kdbm_xfs_xinodes,	"<xfs_mount_t>",
				"Dump XFS inodes per mount"},
  {  "xquiesce",kdbm_xfs_xinodes_quiesce, "<xfs_mount_t>",
				"Dump non-quiesced XFS inodes per mount"},
#ifdef XFS_LOG_TRACE
  {  "xl_grtr",	kdbm_xfs_xlog_granttrace,	"<xlog_t>",
				"Dump XFS log grant trace" },
#endif
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
  {  "xperag",	kdbm_xfs_xperag,	"<xfs_mount_t>",
				"Dump XFS per-allocation group data"},
  {  "xqinfo",  kdbm_xfs_xqm_qinfo,	"<xfs_mount_t>",
				"Dump mount->m_quotainfo structure"},
#ifdef	CONFIG_XFS_QUOTA
  {  "xqm",	kdbm_xfs_xqm,		"",
				"Dump XFS quota manager structure"},
  {  "xqmfree",	kdbm_xfs_xqm_freelist,	"",
				"Dump XFS global freelist of dquots"},
  {  "xqmhtab",	kdbm_xfs_xqm_htab,	"",
				"Dump XFS hashtable of dquots"},
#endif	/* CONFIG_XFS_QUOTA */
  {  "xqmplist",kdbm_xfs_xqm_mplist,	"<xfs_mount_t>",
				"Dump XFS all dquots of a f/s"},
#ifdef XFS_RW_TRACE
  {  "xrwtrc",	kdbm_xfs_xrwtrace,	"<xfs_inode_t>",
				"Dump XFS inode read/write trace" },
#endif
  {  "xsb",	kdbm_xfs_xsb,		"<xfs_sb_t> <cnv>",
				"Dump XFS superblock"},
  {  "xtp",	kdbm_xfs_xtp,		"<xfs_trans_t>",
				"Dump XFS transaction structure"},
  {  "xtrres",	kdbm_xfs_xtrans_res,	"<xfs_mount_t>",
				"Dump XFS reservation values"},
  {  NULL,		NULL,	NULL }
};

static struct xif xfsbuf_funcs[] = {
  {  "xbp",	kdbm_bp,	"<vaddr>",	"Display xfs_buf_t" },
  {  "xbpflags",kdbm_bp_flags,	"<flags>",	"Display xfs_buf flags" },
  {  "xiomap",	kdbm_iomap,	"<xfs_iomap_t *>",	"Display IOmap" },
  {  "xbpdelay",kdbm_bpdelay,	"0|1",		"Display delwri buffers" },
#ifdef XFS_BUF_TRACE
  {  "xbptrace",kdbm_bptrace,	"<vaddr>|<count>",	"xfs_buf_t trace" },
  {  "xbptroff",kdbm_bptrace_offset, "<daddr> [<mask>]","xfs_buf_t trace" },
#endif
  {  NULL,		NULL,	NULL }
};

static int
__init xfsidbg_init(void)
{
	struct xif	*p;

	for (p = xfsidbg_funcs; p->name; p++)
		kdb_register(p->name, p->func, p->args, p->help, 0);
	for (p = xfsbuf_funcs; p->name; p++)
		kdb_register(p->name, p->func, p->args, p->help, 0);
	return 0;
}

static void
__exit xfsidbg_exit(void)
{
	struct xif	*p;

	for (p = xfsidbg_funcs; p->name; p++)
		kdb_unregister(p->name);
	for (p = xfsbuf_funcs; p->name; p++)
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
 * Static functions.
 */

#ifdef XFS_ALLOC_TRACE
/*
 * Print xfs alloc trace buffer entry.
 */
static int
xfs_alloc_trace_entry(ktrace_entry_t *ktep)
{
	static char *modagf_flags[] = {
		"magicnum",
		"versionnum",
		"seqno",
		"length",
		"roots",
		"levels",
		"flfirst",
		"fllast",
		"flcount",
		"freeblks",
		"longest",
		NULL
	};

	if (((__psint_t)ktep->val[0] & 0xffff) == 0)
		return 0;
	switch ((long)ktep->val[0] & 0xffffL) {
	case XFS_ALLOC_KTRACE_ALLOC:
		kdb_printf("alloc %s[%s %ld] mp 0x%p\n",
			(char *)ktep->val[1],
			ktep->val[2] ? (char *)ktep->val[2] : "",
			(long)ktep->val[0] >> 16,
			(xfs_mount_t *)ktep->val[3]);
		kdb_printf(
	"agno %ld agbno %ld minlen %ld maxlen %ld mod %ld prod %ld minleft %ld\n",
			(long)ktep->val[4],
			(long)ktep->val[5],
			(long)ktep->val[6],
			(long)ktep->val[7],
			(long)ktep->val[8],
			(long)ktep->val[9],
			(long)ktep->val[10]);
		kdb_printf("total %ld alignment %ld len %ld type %s otype %s\n",
			(long)ktep->val[11],
			(long)ktep->val[12],
			(long)ktep->val[13],
			xfs_alloctype[((__psint_t)ktep->val[14]) >> 16],
			xfs_alloctype[((__psint_t)ktep->val[14]) & 0xffff]);
		kdb_printf("wasdel %d wasfromfl %d isfl %d userdata %d\n",
			((__psint_t)ktep->val[15] & (1 << 3)) != 0,
			((__psint_t)ktep->val[15] & (1 << 2)) != 0,
			((__psint_t)ktep->val[15] & (1 << 1)) != 0,
			((__psint_t)ktep->val[15] & (1 << 0)) != 0);
		break;
	case XFS_ALLOC_KTRACE_FREE:
		kdb_printf("free %s[%s %ld] mp 0x%p\n",
			(char *)ktep->val[1],
			ktep->val[2] ? (char *)ktep->val[2] : "",
			(long)ktep->val[0] >> 16,
			(xfs_mount_t *)ktep->val[3]);
		kdb_printf("agno %ld agbno %ld len %ld isfl %d\n",
			(long)ktep->val[4],
			(long)ktep->val[5],
			(long)ktep->val[6],
			(__psint_t)ktep->val[7] != 0);
		break;
	case XFS_ALLOC_KTRACE_MODAGF:
		kdb_printf("modagf %s[%s %ld] mp 0x%p\n",
			(char *)ktep->val[1],
			ktep->val[2] ? (char *)ktep->val[2] : "",
			(long)ktep->val[0] >> 16,
			(xfs_mount_t *)ktep->val[3]);
		printflags((__psint_t)ktep->val[4], modagf_flags, "modified");
		kdb_printf("seqno %lu length %lu roots b %lu c %lu\n",
			(unsigned long)ktep->val[5],
			(unsigned long)ktep->val[6],
			(unsigned long)ktep->val[7],
			(unsigned long)ktep->val[8]);
		kdb_printf("levels b %lu c %lu flfirst %lu fllast %lu flcount %lu\n",
			(unsigned long)ktep->val[9],
			(unsigned long)ktep->val[10],
			(unsigned long)ktep->val[11],
			(unsigned long)ktep->val[12],
			(unsigned long)ktep->val[13]);
		kdb_printf("freeblks %lu longest %lu\n",
			(unsigned long)ktep->val[14],
			(unsigned long)ktep->val[15]);
		break;

	case XFS_ALLOC_KTRACE_UNBUSY:
		kdb_printf("unbusy %s [%s %ld] mp 0x%p\n",
			(char *)ktep->val[1],
			ktep->val[2] ? (char *)ktep->val[2] : "",
			(long)ktep->val[0] >> 16,
			(xfs_mount_t *)ktep->val[3]);
		kdb_printf("      agno %lu slot %lu tp 0x%p\n",
			(unsigned long)ktep->val[4],
			(unsigned long)ktep->val[7],
			(xfs_trans_t *)ktep->val[8]);
		break;
	case XFS_ALLOC_KTRACE_BUSY:
		kdb_printf("busy %s [%s %ld] mp 0x%p\n",
			(char *)ktep->val[1],
			ktep->val[2] ? (char *)ktep->val[2] : "",
			(long)ktep->val[0] >> 16,
			(xfs_mount_t *)ktep->val[3]);
		kdb_printf("      agno %lu agbno %lu len %lu slot %lu tp 0x%p\n",
			(unsigned long)ktep->val[4],
			(unsigned long)ktep->val[5],
			(unsigned long)ktep->val[6],
			(unsigned long)ktep->val[7],
			(xfs_trans_t *)ktep->val[8]);
		break;
	case XFS_ALLOC_KTRACE_BUSYSEARCH:
		kdb_printf("busy-search %s [%s %ld] mp 0x%p\n",
			(char *)ktep->val[1],
			ktep->val[2] ? (char *)ktep->val[2] : "",
			(long)ktep->val[0] >> 16,
			(xfs_mount_t *)ktep->val[3]);
		kdb_printf("      agno %ld agbno %ld len %ld slot %ld tp 0x%p\n",
			(unsigned long)ktep->val[4],
			(unsigned long)ktep->val[5],
			(unsigned long)ktep->val[6],
			(unsigned long)ktep->val[7],
			(xfs_trans_t *)ktep->val[8]);
		break;
	default:
		kdb_printf("unknown alloc trace record\n");
		break;
	}
	return 1;
}
#endif /* XFS_ALLOC_TRACE */

#ifdef XFS_ATTR_TRACE
/*
 * Print an attribute trace buffer entry.
 */
static int
xfs_attr_trace_entry(ktrace_entry_t *ktep)
{
	static char *attr_arg_flags[] = {
		"DONTFOLLOW",	/* 0x0001 */
		"ROOT",		/* 0x0002 */
		"TRUSTED",	/* 0x0004 */
		"SECURE",	/* 0x0008 */
		"CREATE",	/* 0x0010 */
		"REPLACE",	/* 0x0020 */
		"?",		/* 0x0040 */
		"?",		/* 0x0080 */
		"SYSTEM",	/* 0x0100 */
		"?",		/* 0x0200 */
		"?",		/* 0x0400 */
		"?",		/* 0x0800 */
		"KERNOTIME",	/* 0x1000 */
		"KERNOVAL",	/* 0x2000 */
		"KERNAMELS",	/* 0x4000 */
		"KERNFULLS",	/* 0x8000 */
		NULL
	};

	if (!ktep->val[0])
		return 0;

	qprintf("-- %s: cursor h/b/o 0x%lx/0x%lx/%lu, dupcnt %lu, dp 0x%p\n",
		 (char *)ktep->val[1],
		 (unsigned long)ktep->val[3],
		 (unsigned long)ktep->val[4],
		 (unsigned long)ktep->val[5],
		 (unsigned long)ktep->val[11],
		 (xfs_inode_t *)ktep->val[2]);
	qprintf("   alist 0x%p, size %lu, count %lu, firstu %lu, Llen %lu",
		 (attrlist_t *)ktep->val[6],
		 (unsigned long)ktep->val[7],
		 (unsigned long)ktep->val[8],
		 (unsigned long)ktep->val[9],
		 (unsigned long)ktep->val[10]);
	printflags((__psunsigned_t)(ktep->val[12]), attr_arg_flags, ", flags");
	qprintf("\n");

	switch ((__psint_t)ktep->val[0]) {
	case XFS_ATTR_KTRACE_L_C:
		break;
	case XFS_ATTR_KTRACE_L_CN:
		qprintf("   node: count %lu, 1st hash 0x%lx, last hash 0x%lx\n",
			 (unsigned long)ktep->val[13],
			 (unsigned long)ktep->val[14],
			 (unsigned long)ktep->val[15]);
		break;
	case XFS_ATTR_KTRACE_L_CB:
		qprintf("   btree: hash 0x%lx, blkno 0x%lx\n",
			 (unsigned long)ktep->val[13],
			 (unsigned long)ktep->val[14]);
		break;
	case XFS_ATTR_KTRACE_L_CL:
		qprintf("   leaf: count %ld, 1st hash 0x%lx, last hash 0x%lx\n",
			 (unsigned long)ktep->val[13],
			 (unsigned long)ktep->val[14],
			 (unsigned long)ktep->val[15]);
		break;
	default:
		qprintf("   unknown attr trace record format\n");
		break;
	}
	return 1;
}
#endif /* XFS_ATTR_TRACE */

#ifdef XFS_BMAP_TRACE
/*
 * Print xfs bmap extent trace buffer entry.
 */
static int
xfs_bmap_trace_entry(ktrace_entry_t *ktep)
{
	xfs_dfsbno_t	    b;
	xfs_dfilblks_t	  c;
	xfs_inode_t	     *ip;
	xfs_ino_t	       ino;
	xfs_dfiloff_t	   o;
	int		     flag;
	int		     opcode;
	static char	     *ops[] = { "del", "ins", "pre", "post" };
	xfs_bmbt_rec_32_t       r;
	int		     whichfork;

	opcode = ((__psint_t)ktep->val[0]) & 0xffff;
	if (opcode == 0)
		return 0;
	whichfork = ((__psint_t)ktep->val[0]) >> 16;
	ip = (xfs_inode_t *)ktep->val[3];
	ino = ((xfs_ino_t)(unsigned long)ktep->val[6] << 32) |
		((xfs_ino_t)(unsigned long)ktep->val[7]);
	qprintf("%s %s:%s ip %p ino %s %cf\n",
		ops[opcode - 1], (char *)ktep->val[1],
		(char *)ktep->val[2], ip, xfs_fmtino(ino, ip->i_mount),
		"da"[whichfork]);
	r.l0 = (xfs_bmbt_rec_base_t)(unsigned long)ktep->val[8];
	r.l1 = (xfs_bmbt_rec_base_t)(unsigned long)ktep->val[9];
	r.l2 = (xfs_bmbt_rec_base_t)(unsigned long)ktep->val[10];
	r.l3 = (xfs_bmbt_rec_base_t)(unsigned long)ktep->val[11];
	xfs_convert_extent(&r, &o, &b, &c, &flag);
	qprintf(" idx %ld offset %lld block %s",
		(long)ktep->val[4], o,
		xfs_fmtfsblock((xfs_fsblock_t)b, ip->i_mount));
	qprintf(" count %lld flag %d\n", c, flag);
	if ((__psint_t)ktep->val[5] != 2)
		return 1;
	r.l0 = (xfs_bmbt_rec_base_t)(unsigned long)ktep->val[12];
	r.l1 = (xfs_bmbt_rec_base_t)(unsigned long)ktep->val[13];
	r.l2 = (xfs_bmbt_rec_base_t)(unsigned long)ktep->val[14];
	r.l3 = (xfs_bmbt_rec_base_t)(unsigned long)ktep->val[15];
	xfs_convert_extent(&r, &o, &b, &c, &flag);
	qprintf(" offset %lld block %s", o,
		xfs_fmtfsblock((xfs_fsblock_t)b, ip->i_mount));
	qprintf(" count %lld flag %d\n", c, flag);
	return 1;
}

static void
xfsidbg_btree_trace_record(
	int			btnum,
	unsigned long		l0,
	unsigned long		l1,
	unsigned long		l2,
	unsigned long		l3,
	unsigned long		l4,
	unsigned long		l5)
{
	switch (btnum) {
	case XFS_BTNUM_BMAP:
	{
		struct xfs_bmbt_irec s;

		s.br_startoff = ((xfs_dfiloff_t)l0 << 32) | (xfs_dfiloff_t)l1;
		s.br_startblock = ((xfs_dfsbno_t)l2 << 32) | (xfs_dfsbno_t)l3;
		s.br_blockcount = ((xfs_dfilblks_t)l4 << 32) | (xfs_dfilblks_t)l5;

		xfsidbg_xbirec(&s);
		break;
	}
	case XFS_BTNUM_BNO:
	case XFS_BTNUM_CNT:
		qprintf(" startblock = %d, blockcount = %d\n",
			(unsigned int)l0, (unsigned int)l2);
		break;
	case XFS_BTNUM_INO:
		qprintf(" startino = %d, freecount = %d, free = %lld\n",
			(unsigned int)l0, (unsigned int)l2,
			((xfs_inofree_t)l4 << 32) | (xfs_inofree_t)l5);
		break;
	default:
		break;
	}
}

static void
xfsidbg_btree_trace_key(
	int			btnum,
	unsigned long		l0,
	unsigned long		l1,
	unsigned long		l2,
	unsigned long		l3)
{
	switch (btnum) {
	case XFS_BTNUM_BMAP:
		qprintf(" startoff 0x%x%08x\n", (unsigned int)l0, (unsigned int)l1);
		break;
	case XFS_BTNUM_BNO:
	case XFS_BTNUM_CNT:
		qprintf(" startblock %d blockcount %d\n",
			(unsigned int)l0, (unsigned int)l3);
		break;
	case XFS_BTNUM_INO:
		qprintf(" startino %d\n", (unsigned int)l0);
		break;
	default:
		break;
	}
}

static void
xfsidbg_btree_trace_cursor(
	int			btnum,
	unsigned long		l0,
	unsigned long		l1,
	unsigned long		l2,
	unsigned long		l3,
	unsigned long		l4)
{
	switch (btnum) {
	case XFS_BTNUM_BMAP:
	{
		struct xfs_bmbt_rec_host r;

		qprintf(" nlevels %ld flags %ld allocated %ld ",
			(l0 >> 24) & 0xff, (l0 >> 16) & 0xff, l0 & 0xffff);

		r.l0 = (unsigned long long)l1 << 32 | l2;
		r.l1 = (unsigned long long)l3 << 32 | l4;

		xfsidbg_xbmbtrec(&r);
		break;
	}
	case XFS_BTNUM_BNO:
	case XFS_BTNUM_CNT:
		qprintf(" agno %d startblock %d blockcount %d\n",
			(unsigned int)l0, (unsigned int)l1, (unsigned int)l3);
		break;
	case XFS_BTNUM_INO:
		qprintf(" agno %d startino %d free %lld\n",
			(unsigned int)l0, (unsigned int)l1,
			((xfs_inofree_t)l3 << 32) | (xfs_inofree_t)l4);
		break;
	default:
		break;
	}
}

/*
 * Print xfs bmap btree trace buffer entry.
 */
static int
xfs_btree_trace_entry(
	int			btnum,
	ktrace_entry_t	  	*ktep)
{
	int			line;
	int			type;
	int			whichfork;

	type = (__psint_t)ktep->val[0] & 0xff;
	if (type == 0)
		return 0;
	whichfork = ((__psint_t)ktep->val[0] >> 8) & 0xff;
	line = ((__psint_t)ktep->val[0] >> 16) & 0xffff;
	qprintf("%s[%s@%d] ip 0x%p %cf cur 0x%p\n",
		(char *)ktep->val[1],
		(char *)ktep->val[2],
		line,
		(xfs_inode_t *)ktep->val[3],
		"da"[whichfork],
		(xfs_btree_cur_t *)ktep->val[4]);
	switch (type) {
	case XFS_BTREE_KTRACE_ARGBI:
		qprintf(" buf 0x%p i %ld\n",
			(xfs_buf_t *)ktep->val[5],
			(long)ktep->val[6]);
		break;
	case XFS_BTREE_KTRACE_ARGBII:
		qprintf(" buf 0x%p i0 %ld i1 %ld\n",
			(xfs_buf_t *)ktep->val[5],
			(long)ktep->val[6],
			(long)ktep->val[7]);
		break;
	case XFS_BTREE_KTRACE_ARGFFFI:
		qprintf(" o 0x%x%08x b 0x%x%08x i 0x%x%08x j %ld\n",
			(unsigned int)(long)ktep->val[5],
			(unsigned int)(long)ktep->val[6],
			(unsigned int)(long)ktep->val[7],
			(unsigned int)(long)ktep->val[8],
			(unsigned int)(long)ktep->val[9],
			(unsigned int)(long)ktep->val[10],
			(long)ktep->val[11]);
		break;
	case XFS_BTREE_KTRACE_ARGI:
		qprintf(" i 0x%lx\n",
			(long)ktep->val[5]);
		break;
	case XFS_BTREE_KTRACE_ARGIPK:
		qprintf(" i 0x%lx f 0x%x%08x o 0x%x%08x\n",
			(long)ktep->val[5],
			(unsigned int)(long)ktep->val[6],
			(unsigned int)(long)ktep->val[7],
			(unsigned int)(long)ktep->val[8],
			(unsigned int)(long)ktep->val[9]);
		break;
	case XFS_BTREE_KTRACE_ARGIPR:
		qprintf(" i 0x%lx f 0x%x%08x ",
			(long)ktep->val[5],
			(unsigned int)(long)ktep->val[6],
			(unsigned int)(long)ktep->val[7]);
		xfsidbg_btree_trace_record(btnum,
					 (unsigned long)ktep->val[8],
					 (unsigned long)ktep->val[9],
					 (unsigned long)ktep->val[10],
					 (unsigned long)ktep->val[11],
					 (unsigned long)ktep->val[12],
					 (unsigned long)ktep->val[13]);
		break;
	case XFS_BTREE_KTRACE_ARGIK:
		qprintf(" i 0x%lx\n", (long)ktep->val[5]);
		xfsidbg_btree_trace_key(btnum,
				      (unsigned long)ktep->val[6],
				      (unsigned long)ktep->val[7],
				      (unsigned long)ktep->val[8],
				      (unsigned long)ktep->val[9]);
		break;
	case XFS_BTREE_KTRACE_ARGR:
		xfsidbg_btree_trace_record(btnum,
					 (unsigned long)ktep->val[5],
					 (unsigned long)ktep->val[6],
					 (unsigned long)ktep->val[7],
					 (unsigned long)ktep->val[8],
					 (unsigned long)ktep->val[9],
					 (unsigned long)ktep->val[10]);
		break;
	case XFS_BTREE_KTRACE_CUR:
		xfsidbg_btree_trace_cursor(btnum,
					  (unsigned long)ktep->val[5],
					  (unsigned long)ktep->val[6],
					  (unsigned long)ktep->val[7],
					  (unsigned long)ktep->val[8],
					  (unsigned long)ktep->val[9]);

		qprintf(" bufs 0x%p 0x%p 0x%p 0x%p ",
			(xfs_buf_t *)ktep->val[10],
			(xfs_buf_t *)ktep->val[11],
			(xfs_buf_t *)ktep->val[12],
			(xfs_buf_t *)ktep->val[13]);
		qprintf("ptrs %ld %ld %ld %ld\n",
			(long)ktep->val[14] >> 16,
			(long)ktep->val[14] & 0xffff,
			(long)ktep->val[15] >> 16,
			(long)ktep->val[15] & 0xffff);
		break;
	default:
		qprintf("unknown bmbt trace record\n");
		break;
	}
	return 1;
}
#endif

struct xfsidbg_btree {
	size_t			block_len;
	size_t			key_len;
	size_t			rec_len;
	size_t			ptr_len;
	void			(*print_block)(struct xfs_btree_block *);
	void			(*print_rec)(int i, union xfs_btree_rec *);
	void			(*print_key)(int i, union xfs_btree_key *,
					     union xfs_btree_ptr *);
};

/* calculate max records.  Only for non-leaves. */
static int
xfsidbg_maxrecs(struct xfsidbg_btree *bt, int blocksize)
{
	blocksize -= bt->block_len;

	return blocksize / (bt->key_len + bt->ptr_len);
}


static union xfs_btree_key *
xfsidbg_btree_key_addr(struct xfsidbg_btree *bt,
		struct xfs_btree_block *block, int index)
{
	return (union xfs_btree_key *)
		((char *)block +
		 bt->block_len +
		 (index - 1) * bt->key_len);
}

static union xfs_btree_rec *
xfsidbg_btree_rec_addr(struct xfsidbg_btree *bt,
	struct xfs_btree_block *block, int index)
{
	return (union xfs_btree_rec *)
		((char *)block +
		 bt->block_len +
		 (index - 1) * bt->rec_len);
}

static union xfs_btree_ptr *
xfsidbg_btree_ptr_addr(struct xfsidbg_btree *bt,
	struct xfs_btree_block *block, int index, int maxrecs)
{
	return (union xfs_btree_ptr *)
		((char *)block +
		 bt->block_len +
		 maxrecs * bt->key_len +
		 (index - 1) * bt->ptr_len);
}

/*
 * Print a btree block.
 */
static void
xfs_btblock(struct xfs_btree_block *block, int blocksize, struct xfsidbg_btree *bt)
{
	int i;

	bt->print_block(block);

	if (!block->bb_level) {
		for (i = 1; i <= be16_to_cpu(block->bb_numrecs); i++)
			bt->print_rec(i, xfsidbg_btree_rec_addr(bt, block, i));
	} else {
		int mxr;

		mxr = xfsidbg_maxrecs(bt, blocksize);
		for (i = 1; i <= xfs_btree_get_numrecs(block); i++) {
			bt->print_key(i, xfsidbg_btree_key_addr(bt, block, i),
				xfsidbg_btree_ptr_addr(bt, block, i, mxr));
		}
	}
}

static void
xfsidbg_print_btree_sblock(struct xfs_btree_block *block)
{
	kdb_printf("magic 0x%x level %d numrecs %d leftsib 0x%x rightsib 0x%x\n",
			be32_to_cpu(block->bb_magic),
			be16_to_cpu(block->bb_level),
			be16_to_cpu(block->bb_numrecs),
			be32_to_cpu(block->bb_u.s.bb_leftsib),
			be32_to_cpu(block->bb_u.s.bb_rightsib));
}

static void
xfsidbg_print_btree_lblock(struct xfs_btree_block *block)
{
	kdb_printf("magic 0x%x level %d numrecs %d leftsib %Lx rightsib %Lx\n",
			be32_to_cpu(block->bb_magic),
			be16_to_cpu(block->bb_level),
			be16_to_cpu(block->bb_numrecs),
			(unsigned long long)be64_to_cpu(block->bb_u.l.bb_leftsib),
			(unsigned long long)be64_to_cpu(block->bb_u.l.bb_rightsib));
}

static void
xfsidbg_print_alloc_rec(int i, union xfs_btree_rec *rec)
{
	kdb_printf("rec %d startblock 0x%x blockcount %d\n", i,
			be32_to_cpu(rec->alloc.ar_startblock),
			be32_to_cpu(rec->alloc.ar_blockcount));
}

static void
xfsidbg_print_alloc_key(int i, union xfs_btree_key *key,
		union xfs_btree_ptr *ptr)
{
	kdb_printf("key %d startblock 0x%x blockcount %d ptr 0x%x\n", i,
			be32_to_cpu(key->alloc.ar_startblock),
			be32_to_cpu(key->alloc.ar_blockcount),
			be32_to_cpu(ptr->s));
}

static struct xfsidbg_btree xfsidbg_allocbt = {
	.block_len	= XFS_BTREE_SBLOCK_LEN,
	.key_len	= sizeof(xfs_alloc_key_t),
	.rec_len	= sizeof(xfs_alloc_rec_t),
	.ptr_len	= sizeof(__be32),
	.print_block	= xfsidbg_print_btree_sblock,
	.print_rec	= xfsidbg_print_alloc_rec,
	.print_key	= xfsidbg_print_alloc_key,
};

static void
xfsidbg_print_bmbt_rec(int i, union xfs_btree_rec *rec)
{
	xfs_bmbt_irec_t	irec;

	xfs_bmbt_disk_get_all(&rec->bmbt, &irec);

	kdb_printf("rec %d startoff %Ld startblock %Lx blockcount %Ld flag %d\n",
			i, irec.br_startoff,
			(__uint64_t)irec.br_startblock,
			irec.br_blockcount, irec.br_state);
}

static void
xfsidbg_print_bmbt_key(int i, union xfs_btree_key *key,
		union xfs_btree_ptr *ptr)
{
	kdb_printf("key %d startoff %Ld ", i,
			(unsigned long long)be64_to_cpu(key->bmbt.br_startoff));
	kdb_printf("ptr %Lx\n", (unsigned long long)be64_to_cpu(ptr->l));
}

static struct xfsidbg_btree xfsidbg_bmbt = {
	.block_len	= XFS_BTREE_LBLOCK_LEN,
	.key_len	= sizeof(xfs_bmbt_key_t),
	.rec_len	= sizeof(xfs_bmbt_rec_t),
	.ptr_len	= sizeof(__be64),
	.print_block	= xfsidbg_print_btree_lblock,
	.print_rec	= xfsidbg_print_bmbt_rec,
	.print_key	= xfsidbg_print_bmbt_key,
};

static void
xfsidbg_print_inobt_rec(int i, union xfs_btree_rec *rec)
{
	kdb_printf("rec %d startino 0x%x freecount %d, free %Lx\n", i,
			be32_to_cpu(rec->inobt.ir_startino),
			be32_to_cpu(rec->inobt.ir_freecount),
			(unsigned long long)be64_to_cpu(rec->inobt.ir_free));
}

static void
xfsidbg_print_inobt_key(int i, union xfs_btree_key *key,
		union xfs_btree_ptr *ptr)
{
	kdb_printf("key %d startino 0x%x ptr 0x%x\n", i,
			be32_to_cpu(key->inobt.ir_startino),
			be32_to_cpu(ptr->s));
}

static struct xfsidbg_btree xfsidbg_inobtbt = {
	.block_len	= XFS_BTREE_SBLOCK_LEN,
	.key_len	= sizeof(xfs_inobt_key_t),
	.rec_len	= sizeof(xfs_inobt_rec_t),
	.ptr_len	= sizeof(__be32),
	.print_block	= xfsidbg_print_btree_sblock,
	.print_rec	= xfsidbg_print_inobt_rec,
	.print_key	= xfsidbg_print_inobt_key,
};

/*
 * Print an xfs in-inode bmap btree root.
 */
static void
xfs_broot(xfs_inode_t *ip, xfs_ifork_t *f)
{
	int			format;

	format = f == &ip->i_df ? ip->i_d.di_format : ip->i_d.di_aformat;
	if ((f->if_flags & XFS_IFBROOT) == 0 ||
	    format != XFS_DINODE_FMT_BTREE) {
		kdb_printf("inode 0x%p not btree format\n", ip);
		return;
	}

	xfs_btblock(f->if_broot, f->if_broot_bytes, &xfsidbg_bmbt);
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
		"inode_stale",  /* 0x20 */
		NULL
		};
	static char *blf_flags[] = {
		"inode",	/* 0x1 */
		"cancel",	/* 0x2 */
		NULL
		};

	if (summary) {
		kdb_printf("buf 0x%p blkno 0x%Lx ", blip->bli_buf,
			     blip->bli_format.blf_blkno);
		printflags(blip->bli_flags, bli_flags, "flags:");
		kdb_printf("\n   ");
		xfsidbg_xbuf_real(blip->bli_buf, 1);
		return;
	}
	kdb_printf("buf 0x%p recur %d refcount %d flags:",
		blip->bli_buf, blip->bli_recur,
		atomic_read(&blip->bli_refcount));
	printflags(blip->bli_flags, bli_flags, NULL);
	kdb_printf("\n");
	kdb_printf("size %d blkno 0x%Lx len 0x%x map size %d map 0x%p\n",
		blip->bli_format.blf_size, blip->bli_format.blf_blkno,
		(uint) blip->bli_format.blf_len, blip->bli_format.blf_map_size,
		&(blip->bli_format.blf_data_map[0]));
	kdb_printf("blf flags: ");
	printflags((uint)blip->bli_format.blf_flags, blf_flags, NULL);
#ifdef XFS_TRANS_DEBUG
	kdb_printf("orig 0x%x logged 0x%x",
		blip->bli_orig, blip->bli_logged);
#endif
	kdb_printf("\n");
}

#ifdef XFS_BMAP_TRACE
/*
 * Convert an external extent descriptor to internal form.
 */
static void
xfs_convert_extent(xfs_bmbt_rec_32_t *rp, xfs_dfiloff_t *op, xfs_dfsbno_t *sp,
		   xfs_dfilblks_t *cp, int *fp)
{
	xfs_dfiloff_t o;
	xfs_dfsbno_t s;
	xfs_dfilblks_t c;
	int flag;

	flag = (((xfs_dfiloff_t)rp->l0) >> 31) & 1;
	o = ((((xfs_dfiloff_t)rp->l0) & 0x7fffffff) << 23) |
	    (((xfs_dfiloff_t)rp->l1) >> 9);
	s = (((xfs_dfsbno_t)(rp->l1 & 0x000001ff)) << 43) |
	    (((xfs_dfsbno_t)rp->l2) << 11) |
	    (((xfs_dfsbno_t)rp->l3) >> 21);
	c = (xfs_dfilblks_t)(rp->l3 & 0x001fffff);
	*op = o;
	*sp = s;
	*cp = c;
	*fp = flag;
}
#endif

#ifdef XFS_RW_TRACE
/*
 * Print itrunc entry trace.
 */
static void
xfs_ctrunc_trace_entry(ktrace_entry_t	*ktep)
{
	qprintf("ip 0x%p cpu %ld\n",
		(xfs_inode_t *)(unsigned long)ktep->val[1], (long)ktep->val[2]);
}
#endif

/*
 * Print an xfs_da_state_path structure.
 */
static void
xfs_dastate_path(xfs_da_state_path_t *p)
{
	int i;

	kdb_printf("active %d\n", p->active);
	for (i = 0; i < XFS_DA_NODE_MAXDEPTH; i++) {
		kdb_printf(" blk %d bp 0x%p blkno 0x%x",
			i, p->blk[i].bp, p->blk[i].blkno);
		kdb_printf(" index %d hashval 0x%x ",
			p->blk[i].index, (uint_t)p->blk[i].hashval);
		switch(p->blk[i].magic) {
		case XFS_DA_NODE_MAGIC:		kdb_printf("NODE\n");	break;
		case XFS_ATTR_LEAF_MAGIC:	kdb_printf("ATTR\n");	break;
		case XFS_DIR2_LEAFN_MAGIC:	kdb_printf("DIR2\n");	break;
		default:			kdb_printf("type ?\n");	break;
		}
	}
}

#ifdef XFS_DIR2_TRACE
/*
 * Print a xfs v2 directory trace buffer entry.
 */
static int
xfs_dir2_trace_entry(ktrace_entry_t *ktep)
{
	char	    *cp;
	int	     i;
	int	     len;

	if (!ktep->val[0])
		return 0;
	cp = (char *)&ktep->val[10];
	qprintf("%s: '", (char *)ktep->val[1]);
	len = min((__psint_t)ktep->val[9], (__psint_t)sizeof(ktep->val[10])*6);
	for (i = 0; i < len; i++)
		qprintf("%c", cp[i]);
	qprintf("'(%ld)", (long)ktep->val[9]);
	if ((__psunsigned_t)ktep->val[0] != XFS_DIR2_KTRACE_ARGS_BIBII)
		qprintf(" hashval 0x%llx inumber %lld dp 0x%p tp 0x%p check %d",
			(__uint64_t)(unsigned long)ktep->val[2],
			(__int64_t)(unsigned long)ktep->val[3],
			ktep->val[4], ktep->val[5],
			(int)(__psint_t)ktep->val[6]);
	switch ((__psunsigned_t)ktep->val[0]) {
	case XFS_DIR2_KTRACE_ARGS:
		break;
	case XFS_DIR2_KTRACE_ARGS_B:
		qprintf(" bp 0x%p", ktep->val[7]);
		break;
	case XFS_DIR2_KTRACE_ARGS_BB:
		qprintf(" lbp 0x%p dbp 0x%p", ktep->val[7], ktep->val[8]);
		break;
	case XFS_DIR2_KTRACE_ARGS_BIBII:
		qprintf(" dp 0x%p tp 0x%p srcbp 0x%p srci %d dstbp 0x%p dsti %d count %d",
			ktep->val[2], ktep->val[3], ktep->val[4],
			(int)(__psint_t)ktep->val[5], ktep->val[6],
			(int)(__psint_t)ktep->val[7],
			(int)(__psint_t)ktep->val[8]);
		break;
	case XFS_DIR2_KTRACE_ARGS_DB:
		qprintf(" db 0x%x bp 0x%p",
			(xfs_dir2_db_t)(unsigned long)ktep->val[7],
			ktep->val[8]);
		break;
	case XFS_DIR2_KTRACE_ARGS_I:
		qprintf(" i 0x%lx", (unsigned long)ktep->val[7]);
		break;
	case XFS_DIR2_KTRACE_ARGS_S:
		qprintf(" s 0x%x", (int)(__psint_t)ktep->val[7]);
		break;
	case XFS_DIR2_KTRACE_ARGS_SB:
		qprintf(" s 0x%x bp 0x%p", (int)(__psint_t)ktep->val[7],
			ktep->val[8]);
		break;
	default:
		qprintf("unknown dirv2 trace record format");
		break;
	}
	return 1;
}
#endif

/*
 * Print an efd log item.
 */
static void
xfs_efd_item_print(xfs_efd_log_item_t *efdp, int summary)
{
	int		i;
	xfs_extent_t	*ep;

	if (summary) {
		kdb_printf("Extent Free Done: ID 0x%Lx nextents %d (at 0x%p)\n",
				efdp->efd_format.efd_efi_id,
				efdp->efd_format.efd_nextents, efdp);
		return;
	}
	kdb_printf("size %d nextents %d next extent %d efip 0x%p\n",
		efdp->efd_format.efd_size, efdp->efd_format.efd_nextents,
		efdp->efd_next_extent, efdp->efd_efip);
	kdb_printf("efi_id 0x%Lx\n", efdp->efd_format.efd_efi_id);
	kdb_printf("efd extents:\n");
	ep = &(efdp->efd_format.efd_extents[0]);
	for (i = 0; i < efdp->efd_next_extent; i++, ep++) {
		kdb_printf("    block %Lx len %d\n",
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
		NULL,
		};

	if (summary) {
		kdb_printf("Extent Free Intention: ID 0x%Lx nextents %d (at 0x%p)\n",
				efip->efi_format.efi_id,
				efip->efi_format.efi_nextents, efip);
		return;
	}
	kdb_printf("size %d nextents %d next extent %d\n",
		efip->efi_format.efi_size, efip->efi_format.efi_nextents,
		efip->efi_next_extent);
	kdb_printf("id %Lx", efip->efi_format.efi_id);
	printflags(efip->efi_flags, efi_flags, "flags :");
	kdb_printf("\n");
	kdb_printf("efi extents:\n");
	ep = &(efip->efi_format.efi_extents[0]);
	for (i = 0; i < efip->efi_next_extent; i++, ep++) {
		kdb_printf("    block %Lx len %d\n",
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
char *
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
		sprintf(rval, "%llu[%x:%x:%x]",
			(unsigned long long) ino,
			XFS_INO_TO_AGNO(mp, ino),
			XFS_INO_TO_AGBNO(mp, ino),
			XFS_INO_TO_OFFSET(mp, ino));
	else
		sprintf(rval, "%llu", (unsigned long long) ino);
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
	sprintf(buf, "[%u:%u]", *wordp, *word2p);

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
		"?fc?dxb?r?l?S?m?"[(m & S_IFMT) >> 12],
		m & S_ISUID ? 'u' : '-',
		m & S_ISGID ? 'g' : '-',
		m & S_ISVTX ? 'v' : '-',
		m & S_IRUSR ? 'r' : '-',
		m & S_IWUSR ? 'w' : '-',
		m & S_IXUSR ? 'x' : '-',
		m & S_IRGRP ? 'r' : '-',
		m & S_IWGRP ? 'w' : '-',
		m & S_IXGRP ? 'x' : '-',
		m & S_IROTH ? 'r' : '-',
		m & S_IWOTH ? 'w' : '-',
		m & S_IXOTH ? 'x' : '-');
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
	sprintf(rval, "0x%lx", (unsigned long) i);
	return rval;
}

/*
 * Format a uuid into a static buffer & return it.
 */
static char *
xfs_fmtuuid(uuid_t *uu)
{
	static char rval[40];
	char	*o	  = rval;
	char	*i	  = (unsigned char*)uu;
	int	 b;

	for (b=0;b<16;b++) {
	    o+=sprintf(o, "%02x", *i++);
	    if (b==3||b==5||b==7||b==9) *o++='-';
	}
	*o='\0';

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
		NULL
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
		NULL
		};

	if (summary) {
		kdb_printf("inode 0x%p logged %d ",
			ilip->ili_inode, ilip->ili_logged);
		printflags(ilip->ili_flags, ili_flags, "flags:");
		printflags(ilip->ili_format.ilf_fields, ilf_fields, "format:");
		printflags(ilip->ili_last_fields, ilf_fields, "lastfield:");
		kdb_printf("\n");
		return;
	}
	kdb_printf("inode 0x%p ino 0x%llu pushbuf %d logged %d flags: ",
		ilip->ili_inode, (unsigned long long) ilip->ili_format.ilf_ino,
		ilip->ili_pushbuf_flag, ilip->ili_logged);
	printflags(ilip->ili_flags, ili_flags, NULL);
	kdb_printf("\n");
	kdb_printf("ilock recur %d iolock recur %d ext buf 0x%p\n",
		ilip->ili_ilock_recur, ilip->ili_iolock_recur,
		ilip->ili_extents_buf);
#ifdef XFS_TRANS_DEBUG
	kdb_printf("root bytes %d root orig 0x%x\n",
		ilip->ili_root_size, ilip->ili_orig_root);
#endif
	kdb_printf("size %d ", ilip->ili_format.ilf_size);
	printflags(ilip->ili_format.ilf_fields, ilf_fields, "fields:");
	printflags(ilip->ili_last_fields, ilf_fields, " last fields: ");
	kdb_printf("\n");
	kdb_printf(" flush lsn %s last lsn %s\n",
		xfs_fmtlsn(&(ilip->ili_flush_lsn)),
		xfs_fmtlsn(&(ilip->ili_last_lsn)));
	kdb_printf("dsize %d, asize %d, rdev 0x%x\n",
		ilip->ili_format.ilf_dsize,
		ilip->ili_format.ilf_asize,
		ilip->ili_format.ilf_u.ilfu_rdev);
	kdb_printf("blkno 0x%Lx len 0x%x boffset 0x%x\n",
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
	kdb_printf("dquot 0x%p\n",
		lip->qli_dquot);

}

/*
 * Print a quotaoff log item.
 */
/* ARGSUSED */
static void
xfs_qoff_item_print(xfs_qoff_logitem_t *lip, int summary)
{
	kdb_printf("start qoff item 0x%p flags 0x%x\n",
		lip->qql_start_lip, lip->qql_format.qf_flags);

}

/*
 * Print buffer full of inodes.
 */
static void
xfs_inodebuf(xfs_buf_t *bp)
{
	xfs_dinode_t *di;
	xfs_icdinode_t dic;
	int n, i;

	n = XFS_BUF_COUNT(bp) >> 8;
	for (i = 0; i < n; i++) {
		di = (xfs_dinode_t *)xfs_buf_offset(bp,
					i * 256);

		xfs_dinode_from_disk(&dic, di);
		xfs_prdinode_incore(&dic);
		kdb_printf("next_unlinked 0x%x u@0x%p\n",
			   be32_to_cpu(di->di_next_unlinked), XFS_DFORK_DPTR(di));
	}
}

#ifdef XFS_RW_TRACE
/*
 * Print iomap entry trace.
 */
static void
xfs_iomap_enter_trace_entry(ktrace_entry_t *ktep)
{
	qprintf("ip 0x%p size 0x%x%x offset 0x%x%x count 0x%x\n",
		ktep->val[1],
		(unsigned int)(long)ktep->val[2],
		(unsigned int)(long)ktep->val[3],
		(unsigned int)(long)ktep->val[4],
		(unsigned int)(long)ktep->val[5],
		(unsigned int)(long)ktep->val[6]);
	qprintf("io new size 0x%x%x pid=%d\n",
		(unsigned int)(long)ktep->val[7],
		(unsigned int)(long)ktep->val[8],
		(unsigned int)(long)ktep->val[9]);
}

/*
 * Print iomap map trace.
 */
static void
xfs_iomap_map_trace_entry(ktrace_entry_t *ktep)
{
	static char *bmapi_flags[] = {
		"read",		/* BMAPI_READ */
		"write",	/* BMAPI_WRITE */
		"allocate",	/* BMAPI_ALLOCATE */
		"unwritten",	/* BMAPI_UNWRITTEN */
		"ignstate",	/* BMAPI_IGNSTATE */
		"direct",	/* BMAPI_DIRECT */
		"mmap",		/* BMAPI_MMAP */
		"sync",		/* BMAPI_SYNC */
		"trylock",	/* BMAPI_TRYLOCK */
		"device",	/* BMAPI_DEVICE */
		NULL
	};

	qprintf("ip 0x%p size 0x%x%x offset 0x%x%x count 0x%x\n",
		ktep->val[1],
		(unsigned int)(long)ktep->val[2],
		(unsigned int)(long)ktep->val[3],
		(unsigned int)(long)ktep->val[4],
		(unsigned int)(long)ktep->val[5],
		(unsigned int)(long)ktep->val[6]);
	printflags((__psint_t)ktep->val[7], bmapi_flags, "bmapi flags");
	qprintf("iomap off 0x%x%x delta 0x%x bsize 0x%x bno 0x%x\n",
		(unsigned int)(long)ktep->val[8],
		(unsigned int)(long)ktep->val[9],
		(unsigned int)(long)ktep->val[10],
		(unsigned int)(long)ktep->val[11],
		(unsigned int)(long)ktep->val[12]);
	qprintf("imap off 0x%x count 0x%x block 0x%x\n",
		(unsigned int)(long)ktep->val[13],
		(unsigned int)(long)ktep->val[14],
		(unsigned int)(long)ktep->val[15]);
}

/*
 * Print itrunc entry trace.
 */
static void
xfs_itrunc_trace_entry(ktrace_entry_t   *ktep)
{
	qprintf("ip 0x%p size 0x%x%x flag %ld new size 0x%x%x\n",
		ktep->val[1],
		(unsigned int)(long)ktep->val[2],
		(unsigned int)(long)ktep->val[3],
		(long)ktep->val[4],
		(unsigned int)(long)ktep->val[5],
		(unsigned int)(long)ktep->val[6]);
	qprintf("toss start 0x%x%x toss finish 0x%x%x cpu id %ld pid %d\n",
		(unsigned int)(long)ktep->val[7],
		(unsigned int)(long)ktep->val[8],
		(unsigned int)(long)ktep->val[9],
		(unsigned int)(long)ktep->val[10],
		(long)ktep->val[11],
		(unsigned int)(long)ktep->val[12]);
}

/*
 * Print bunmap entry trace.
 */
static void
xfs_bunmap_trace_entry(ktrace_entry_t   *ktep)
{
	static char *bunmapi_flags[] = {
		"write",	/* 0x01 */
		"delay",	/* 0x02 */
		"entire",       /* 0x04 */
		"metadata",     /* 0x08 */
		"exact",	/* 0x10 */
		"attrfork",     /* 0x20 */
		"async",	/* 0x40 */
		"rsvblocks",    /* 0x80 */
		NULL
	};

	qprintf("ip 0x%p size 0x%x%x bno 0x%x%x len 0x%x cpu id %ld\n",
		ktep->val[1],
		(unsigned int)(long)ktep->val[2],
		(unsigned int)(long)ktep->val[3],
		(unsigned int)(long)ktep->val[4],
		(unsigned int)(long)ktep->val[5],
		(unsigned int)(long)ktep->val[6],
		(long)ktep->val[8]);
	qprintf("ra 0x%p pid %d ", ktep->val[9], (int)(long)ktep->val[10]);
	printflags((__psint_t)ktep->val[7], bunmapi_flags, "flags");
}

/*
 * Print inval_cached_pages entry trace.
 */
static void
xfs_inval_cached_trace_entry(ktrace_entry_t     *ktep)
{
	qprintf("ip 0x%p offset 0x%x%x len 0x%x%x first 0x%x%x last 0x%x%x pid %d\n",
		ktep->val[1],
		(unsigned int)(long)ktep->val[2],
		(unsigned int)(long)ktep->val[3],
		(unsigned int)(long)ktep->val[4],
		(unsigned int)(long)ktep->val[5],
		(unsigned int)(long)ktep->val[6],
		(unsigned int)(long)ktep->val[7],
		(unsigned int)(long)ktep->val[8],
		(unsigned int)(long)ktep->val[9],
		(unsigned int)(long)ktep->val[10]);
}
#endif


/*
 * Print disk inode core.
 */
static void
xfs_prdinode_incore(xfs_icdinode_t *dip)
{
	static char *diflags[] = {
		"realtime",		/* XFS_DIFLAG_REALTIME */
		"prealloc",		/* XFS_DIFLAG_PREALLOC */
		"newrtbm",		/* XFS_DIFLAG_NEWRTBM */
		"immutable",		/* XFS_DIFLAG_IMMUTABLE */
		"append",		/* XFS_DIFLAG_APPEND */
		"sync",			/* XFS_DIFLAG_SYNC */
		"noatime",		/* XFS_DIFLAG_NOATIME */
		"nodump",		/* XFS_DIFLAG_NODUMP */
		"rtinherit",		/* XFS_DIFLAG_RTINHERIT */
		"projinherit",		/* XFS_DIFLAG_PROJINHERIT */
		"nosymlinks",		/* XFS_DIFLAG_NOSYMLINKS */
		"extsize",		/* XFS_DIFLAG_EXTSIZE */
		"extszinherit",		/* XFS_DIFLAG_EXTSZINHERIT */
		NULL
	};

	kdb_printf("magic 0x%x mode 0%o (%s) version 0x%x format 0x%x (%s)\n",
		dip->di_magic, dip->di_mode,
		xfs_fmtmode(dip->di_mode),
		dip->di_version, dip->di_format,
		xfs_fmtformat((xfs_dinode_fmt_t)dip->di_format));
	kdb_printf("nlink %d uid %d gid %d projid %d flushiter %u\n",
		dip->di_nlink,
		dip->di_uid,
		dip->di_gid,
		(uint)dip->di_projid,
		(uint)dip->di_flushiter);
	kdb_printf("atime %u:%u mtime %ud:%u ctime %u:%u\n",
		dip->di_atime.t_sec, dip->di_atime.t_nsec,
		dip->di_mtime.t_sec, dip->di_mtime.t_nsec,
		dip->di_ctime.t_sec, dip->di_ctime.t_nsec);
	kdb_printf("size %Ld ", dip->di_size);
	kdb_printf("nblocks %Ld extsize 0x%x nextents 0x%x anextents 0x%x\n",
		dip->di_nblocks, dip->di_extsize, dip->di_nextents,
		dip->di_anextents);
	kdb_printf("forkoff %d aformat 0x%x (%s) dmevmask 0x%x dmstate 0x%x ",
		dip->di_forkoff, dip->di_aformat,
		xfs_fmtformat((xfs_dinode_fmt_t)dip->di_aformat),
		dip->di_dmevmask, dip->di_dmstate);
	printflags(dip->di_flags, diflags, "flags");
	kdb_printf("gen 0x%x\n", dip->di_gen);
}

#ifdef XFS_RW_TRACE
/*
 * Print read/write entry trace.
 */
static void
xfs_rw_enter_trace_entry(ktrace_entry_t *ktep)
{
	qprintf("ip 0x%p size 0x%x%x ptr 0x%p size %lu\n",
		ktep->val[1],
		(unsigned int)(long)ktep->val[2],
		(unsigned int)(long)ktep->val[3],
		ktep->val[4],
		(unsigned long)ktep->val[5]);
	qprintf("io offset 0x%x%x ioflags 0x%x new size 0x%x%x pid %d\n",
		(unsigned int)(long)ktep->val[6],
		(unsigned int)(long)ktep->val[7],
		(unsigned int)(long)ktep->val[8],
		(unsigned int)(long)ktep->val[9],
		(unsigned int)(long)ktep->val[10],
		(unsigned int)(long)ktep->val[11]);
}

/*
 * Print page write/release trace.
 */
static void
xfs_page_trace_entry(ktrace_entry_t *ktep)
{
	qprintf("ip 0x%p inode 0x%p page 0x%p\n",
		ktep->val[1], ktep->val[2], ktep->val[3]);
	qprintf("pgoff 0x%x di_size 0x%x%x isize 0x%x%x offset 0x%x%x\n",
		(unsigned int)(long)ktep->val[4],
		(unsigned int)(long)ktep->val[5],
		(unsigned int)(long)ktep->val[6],
		(unsigned int)(long)ktep->val[7],
		(unsigned int)(long)ktep->val[8],
		(unsigned int)(long)ktep->val[9],
		(unsigned int)(long)ktep->val[10]);
	qprintf("delalloc %d unmapped %d unwritten %d pid %d\n",
		(unsigned int)(long)ktep->val[11],
		(unsigned int)(long)ktep->val[12],
		(unsigned int)(long)ktep->val[13],
		(unsigned int)(long)ktep->val[14]);
}

/*
 * Print read/write trace entry.
 */
static int
xfs_rw_trace_entry(ktrace_entry_t *ktep)
{
	switch ( (long)ktep->val[0] ) {
	case XFS_READ_ENTER:
		qprintf("READ ENTER:\n");
		xfs_rw_enter_trace_entry(ktep);
		break;
	case XFS_WRITE_ENTER:
		qprintf("WRITE ENTER:\n");
		xfs_rw_enter_trace_entry(ktep);
		break;
	case XFS_IOMAP_READ_ENTER:
		qprintf("IOMAP READ ENTER:\n");
		xfs_iomap_enter_trace_entry(ktep);
		break;
	case XFS_IOMAP_WRITE_ENTER:
		qprintf("IOMAP WRITE ENTER:\n");
		xfs_iomap_enter_trace_entry(ktep);
		break;
	case XFS_IOMAP_WRITE_NOSPACE:
		qprintf("IOMAP WRITE NOSPACE:\n");
		xfs_iomap_enter_trace_entry(ktep);
		break;
	case XFS_IOMAP_READ_MAP:
		qprintf("IOMAP READ MAP:\n");
		xfs_iomap_map_trace_entry(ktep);
		break;
	case XFS_IOMAP_WRITE_MAP:
		qprintf("IOMAP WRITE MAP:\n");
		xfs_iomap_map_trace_entry(ktep);
		break;
	case XFS_ITRUNC_START:
		qprintf("ITRUNC START:\n");
		xfs_itrunc_trace_entry(ktep);
		break;
	case XFS_ITRUNC_FINISH1:
		qprintf("ITRUNC FINISH1:\n");
		xfs_itrunc_trace_entry(ktep);
		break;
	case XFS_ITRUNC_FINISH2:
		qprintf("ITRUNC FINISH2:\n");
		xfs_itrunc_trace_entry(ktep);
		break;
	case XFS_CTRUNC1:
		qprintf("CTRUNC1:\n");
		xfs_ctrunc_trace_entry(ktep);
		break;
	case XFS_CTRUNC2:
		qprintf("CTRUNC2:\n");
		xfs_ctrunc_trace_entry(ktep);
		break;
	case XFS_CTRUNC3:
		qprintf("CTRUNC3:\n");
		xfs_ctrunc_trace_entry(ktep);
		break;
	case XFS_CTRUNC4:
		qprintf("CTRUNC4:\n");
		xfs_ctrunc_trace_entry(ktep);
		break;
	case XFS_CTRUNC5:
		qprintf("CTRUNC5:\n");
		xfs_ctrunc_trace_entry(ktep);
		break;
	case XFS_CTRUNC6:
		qprintf("CTRUNC6:\n");
		xfs_ctrunc_trace_entry(ktep);
		break;
	case XFS_BUNMAP:
		qprintf("BUNMAP:\n");
		xfs_bunmap_trace_entry(ktep);
		break;
	case XFS_INVAL_CACHED:
		qprintf("INVAL CACHED:\n");
		xfs_inval_cached_trace_entry(ktep);
		break;
	case XFS_DIORD_ENTER:
		qprintf("DIORD ENTER:\n");
		xfs_rw_enter_trace_entry(ktep);
		break;
	case XFS_DIOWR_ENTER:
		qprintf("DIOWR ENTER:\n");
		xfs_rw_enter_trace_entry(ktep);
		break;
	case XFS_WRITEPAGE_ENTER:
		qprintf("PAGE WRITE:\n");
		xfs_page_trace_entry(ktep);
		break;
	case XFS_RELEASEPAGE_ENTER:
		qprintf("PAGE RELEASE:\n");
		xfs_page_trace_entry(ktep);
		break;
	case XFS_INVALIDPAGE_ENTER:
		qprintf("PAGE INVALIDATE:\n");
		xfs_page_trace_entry(ktep);
		break;
	case XFS_IOMAP_ALLOC_ENTER:
		qprintf("ALLOC ENTER:\n");
		xfs_iomap_enter_trace_entry(ktep);
		break;
	case XFS_IOMAP_ALLOC_MAP:
		qprintf("ALLOC MAP:\n");
		xfs_iomap_map_trace_entry(ktep);
		break;
	case XFS_IOMAP_UNWRITTEN:
		qprintf("UNWRITTEN:\n");
		xfs_iomap_enter_trace_entry(ktep);
		break;

	default:
		qprintf("UNKNOWN RW TRACE\n");
		return 0;
	}

	return 1;
}
#endif

/*
 * Print xfs extent records for a fork.
 */
static void
xfs_xexlist_fork(xfs_inode_t *ip, int whichfork)
{
	int nextents, i;
	xfs_ifork_t *ifp;
	xfs_bmbt_irec_t irec;

	ifp = XFS_IFORK_PTR(ip, whichfork);
	if (ifp->if_flags & XFS_IFEXTENTS) {
		nextents = ifp->if_bytes / (uint)sizeof(xfs_bmbt_rec_t);
		kdb_printf("inode 0x%p %cf extents 0x%p nextents 0x%x\n",
			ip, "da"[whichfork], xfs_iext_get_ext(ifp, 0),
			nextents);
		for (i = 0; i < nextents; i++) {
			xfs_bmbt_get_all(xfs_iext_get_ext(ifp, i), &irec);
			kdb_printf(
		"%d: startoff %Ld startblock %s blockcount %Ld flag %d\n",
			i, irec.br_startoff,
			xfs_fmtfsblock(irec.br_startblock, ip->i_mount),
			irec.br_blockcount, irec.br_state);
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

	kdb_printf("%s fork", name);
	if (f == NULL) {
		kdb_printf(" empty\n");
		return;
	} else
		kdb_printf("\n");
	kdb_printf(" bytes %s ", xfs_fmtsize(f->if_bytes));
	kdb_printf("real_bytes %s lastex 0x%x u1:%s 0x%p\n",
		xfs_fmtsize(f->if_real_bytes), f->if_lastex,
		f->if_flags & XFS_IFINLINE ? "data" : "extents",
		f->if_flags & XFS_IFINLINE ?
			f->if_u1.if_data :
			(char *)f->if_u1.if_extents);
	kdb_printf(" broot 0x%p broot_bytes %s ext_max %d ",
		f->if_broot, xfs_fmtsize(f->if_broot_bytes), f->if_ext_max);
	printflags(f->if_flags, tab_flags, "flags");
	kdb_printf("\n");
	kdb_printf(" u2");
	for (p = (int *)&f->if_u2;
	     p < (int *)((char *)&f->if_u2 + XFS_INLINE_DATA);
	     p++)
		kdb_printf(" 0x%x", *p);
	kdb_printf("\n");
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
	kdb_printf("magicnum 0x%x versionnum 0x%x seqno 0x%x length 0x%x\n",
		be32_to_cpu(agf->agf_magicnum),
		be32_to_cpu(agf->agf_versionnum),
		be32_to_cpu(agf->agf_seqno),
		be32_to_cpu(agf->agf_length));
	kdb_printf("roots b 0x%x c 0x%x levels b %d c %d\n",
		be32_to_cpu(agf->agf_roots[XFS_BTNUM_BNO]),
		be32_to_cpu(agf->agf_roots[XFS_BTNUM_CNT]),
		be32_to_cpu(agf->agf_levels[XFS_BTNUM_BNO]),
		be32_to_cpu(agf->agf_levels[XFS_BTNUM_CNT]));
	kdb_printf("flfirst %d fllast %d flcount %d freeblks %d longest %d\n",
		be32_to_cpu(agf->agf_flfirst),
		be32_to_cpu(agf->agf_fllast),
		be32_to_cpu(agf->agf_flcount),
		be32_to_cpu(agf->agf_freeblks),
		be32_to_cpu(agf->agf_longest));
}

/*
 * Print xfs allocation group inode header.
 */
static void
xfsidbg_xagi(xfs_agi_t *agi)
{
	int	i;
	int	j;

	kdb_printf("magicnum 0x%x versionnum 0x%x seqno 0x%x length 0x%x\n",
		be32_to_cpu(agi->agi_magicnum),
		be32_to_cpu(agi->agi_versionnum),
		be32_to_cpu(agi->agi_seqno),
		be32_to_cpu(agi->agi_length));
	kdb_printf("count 0x%x root 0x%x level 0x%x\n",
		be32_to_cpu(agi->agi_count),
		be32_to_cpu(agi->agi_root),
		be32_to_cpu(agi->agi_level));
	kdb_printf("freecount 0x%x newino 0x%x dirino 0x%x\n",
		be32_to_cpu(agi->agi_freecount),
		be32_to_cpu(agi->agi_newino),
		be32_to_cpu(agi->agi_dirino));

	kdb_printf("unlinked buckets\n");
	for (i = 0; i < XFS_AGI_UNLINKED_BUCKETS; i++) {
		for (j = 0; j < 4; j++, i++) {
			kdb_printf("0x%08x ",
				be32_to_cpu(agi->agi_unlinked[i]));
		}
		kdb_printf("\n");
	}
}

#ifdef XFS_ALLOC_TRACE
/*
 * Print out the last "count" entries in the allocation trace buffer.
 */
static void
xfsidbg_xalatrace(int count)
{
	ktrace_entry_t  *ktep;
	ktrace_snap_t   kts;
	int	     nentries;
	int	     skip_entries;

	if (xfs_alloc_trace_buf == NULL) {
		qprintf("The xfs alloc trace buffer is not initialized\n");
		return;
	}
	nentries = ktrace_nentries(xfs_alloc_trace_buf);
	if (count == -1) {
		count = nentries;
	}
	if ((count <= 0) || (count > nentries)) {
		qprintf("Invalid count.  There are %d entries.\n", nentries);
		return;
	}

	ktep = ktrace_first(xfs_alloc_trace_buf, &kts);
	if (count != nentries) {
		/*
		 * Skip the total minus the number to look at minus one
		 * for the entry returned by ktrace_first().
		 */
		skip_entries = nentries - count - 1;
		ktep = ktrace_skip(xfs_alloc_trace_buf, skip_entries, &kts);
		if (ktep == NULL) {
			qprintf("Skipped them all\n");
			return;
		}
	}
	while (ktep != NULL) {
		if (xfs_alloc_trace_entry(ktep))
			qprintf("\n");
		ktep = ktrace_next(xfs_alloc_trace_buf, &kts);
	}
}

/*
 * Print out all the entries in the alloc trace buf corresponding
 * to the given block number.
 */
static void
xfsidbg_xalbtrace(xfs_agblock_t bno)
{
	ktrace_entry_t  *ktep;
	ktrace_snap_t   kts;

	if (xfs_alloc_trace_buf == NULL) {
		qprintf("The xfs alloc trace buffer is not initialized\n");
		return;
	}

	ktep = ktrace_first(xfs_alloc_trace_buf, &kts);
	while (ktep != NULL) {
		switch ((__psint_t)ktep->val[0]) {
		case XFS_ALLOC_KTRACE_ALLOC:
		case XFS_ALLOC_KTRACE_FREE:
			if (bno >= (xfs_agblock_t)((__psint_t)ktep->val[5]) &&
			    bno < (xfs_agblock_t)((__psint_t)ktep->val[5]) +
				  (xfs_extlen_t)((__psint_t)ktep->val[13])) {
				(void)xfs_alloc_trace_entry(ktep);
				qprintf("\n");
			}
			break;
		}
		ktep = ktrace_next(xfs_alloc_trace_buf, &kts);
	}
}

/*
 * Print out all the entries in the alloc trace buf corresponding
 * to the given allocation group.
 */
static void
xfsidbg_xalgtrace(xfs_agnumber_t agno)
{
	ktrace_entry_t  *ktep;
	ktrace_snap_t   kts;

	if (xfs_alloc_trace_buf == NULL) {
		qprintf("The xfs alloc trace buffer is not initialized\n");
		return;
	}

	ktep = ktrace_first(xfs_alloc_trace_buf, &kts);
	while (ktep != NULL) {
		if (  (__psint_t)ktep->val[0] &&
		      ((xfs_agnumber_t)((__psint_t)ktep->val[4])) == agno ) {
			(void)xfs_alloc_trace_entry(ktep);
			qprintf("\n");
		}
		ktep = ktrace_next(xfs_alloc_trace_buf, &kts);
	}
}
#endif

/*
 * Print an allocation argument structure for XFS.
 */
static void
xfsidbg_xalloc(xfs_alloc_arg_t *args)
{
	kdb_printf("tp 0x%p mp 0x%p agbp 0x%p pag 0x%p fsbno %s\n",
		args->tp, args->mp, args->agbp, args->pag,
		xfs_fmtfsblock(args->fsbno, args->mp));
	kdb_printf("agno 0x%x agbno 0x%x minlen 0x%x maxlen 0x%x mod 0x%x\n",
		args->agno, args->agbno, args->minlen, args->maxlen, args->mod);
	kdb_printf("prod 0x%x minleft 0x%x total 0x%x alignment 0x%x\n",
		args->prod, args->minleft, args->total, args->alignment);
	kdb_printf("minalignslop 0x%x len 0x%x type %s otype %s wasdel %d\n",
		args->minalignslop, args->len, xfs_alloctype[args->type],
		xfs_alloctype[args->otype], args->wasdel);
	kdb_printf("wasfromfl %d isfl %d userdata %d\n",
		args->wasfromfl, args->isfl, args->userdata);
}

#ifdef XFS_ALLOC_TRACE
/*
 * Print out all the entries in the alloc trace buf corresponding
 * to the given mount point.
 */
static void
xfsidbg_xalmtrace(xfs_mount_t *mp)
{
	ktrace_entry_t	*ktep;
	ktrace_snap_t	kts;

	if (xfs_alloc_trace_buf == NULL) {
		kdb_printf("The xfs alloc trace buffer is not initialized\n");
		return;
	}

	ktep = ktrace_first(xfs_alloc_trace_buf, &kts);
	while (ktep != NULL) {
		if ((__psint_t)ktep->val[0] &&
		    (xfs_mount_t *)ktep->val[3] == mp) {
			(void)xfs_alloc_trace_entry(ktep);
			kdb_printf("\n");
		}
		ktep = ktrace_next(xfs_alloc_trace_buf, &kts);
	}
}

/*
 * Print out all the entries in the alloc trace buf corresponding
 * to the given entry type.
 */
static void
xfsidbg_xalttrace(int tag)
{
	ktrace_entry_t  *ktep;
	ktrace_snap_t   kts;

	if (xfs_alloc_trace_buf == NULL) {
		qprintf("The xfs alloc trace buffer is not initialized\n");
		return;
	}

	ktep = ktrace_first(xfs_alloc_trace_buf, &kts);
	while (ktep != NULL) {
		if ((__psint_t)ktep->val[0] &&
		    ((long)ktep->val[0] & 0xffffL) == (long)tag) {
			(void)xfs_alloc_trace_entry(ktep);
			qprintf("\n");
		}
		ktep = ktrace_next(xfs_alloc_trace_buf, &kts);
	}
}
#endif

static int xargument = 0;

/*
 * Set xtra argument, used by xchksum.
 */
static void
xfsidbg_xarg(int xarg)
{
	if (xarg == -1)
		qprintf("xargument: %d\n", xargument);
	else
		xargument = xarg;
}       /* xfsidbg_xarg */

/*
 * Print an attr_list() context structure.
 */
static void
xfsidbg_xattrcontext(xfs_attr_list_context_t *context)
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

	kdb_printf("dp 0x%p, dupcnt %d, resynch %d",
		    context->dp, context->dupcnt, context->resynch);
	printflags((__psunsigned_t)context->flags, attr_arg_flags, ", flags");
	kdb_printf("\ncursor h/b/o 0x%x/0x%x/%d -- p/p/i 0x%x/0x%x/0x%x\n",
			  context->cursor->hashval, context->cursor->blkno,
			  context->cursor->offset, context->cursor->pad1,
			  context->cursor->pad2, context->cursor->initted);
	kdb_printf("alist 0x%p, bufsize 0x%x, count %zd, firstu 0x%x\n",
		       context->alist, context->bufsize, context->count,
		       context->firstu);
}

/*
 * Print attribute leaf block.
 */
static void
xfsidbg_xattrleaf(xfs_attr_leafblock_t *leaf)
{
	xfs_attr_leaf_hdr_t *h;
	xfs_da_blkinfo_t *i;
	xfs_attr_leaf_map_t *m;
	xfs_attr_leaf_entry_t *e;
	xfs_attr_leaf_name_local_t *l;
	xfs_attr_leaf_name_remote_t *r;
	int j, k;

	h = &leaf->hdr;
	i = &h->info;
	kdb_printf("hdr info forw 0x%x back 0x%x magic 0x%x\n",
		i->forw, i->back, i->magic);
	kdb_printf("hdr count %d usedbytes %d firstused %d holes %d\n",
		be16_to_cpu(h->count),
		be16_to_cpu(h->usedbytes),
		be16_to_cpu(h->firstused), h->holes);
	for (j = 0, m = h->freemap; j < XFS_ATTR_LEAF_MAPSIZE; j++, m++) {
		kdb_printf("hdr freemap %d base %d size %d\n",
			j, be16_to_cpu(m->base),
			be16_to_cpu(m->size));
	}
	for (j = 0, e = leaf->entries; j < be16_to_cpu(h->count); j++, e++) {
		kdb_printf("[%2d] hash 0x%x nameidx %d flags 0x%x",
			j, be32_to_cpu(e->hashval),
			be16_to_cpu(e->nameidx), e->flags);
		if (e->flags & XFS_ATTR_LOCAL)
			kdb_printf("LOCAL ");
		if (e->flags & XFS_ATTR_ROOT)
			kdb_printf("ROOT ");
		if (e->flags & XFS_ATTR_SECURE)
			kdb_printf("SECURE ");
		if (e->flags & XFS_ATTR_INCOMPLETE)
			kdb_printf("INCOMPLETE ");
		k = ~(XFS_ATTR_LOCAL | XFS_ATTR_ROOT |
			XFS_ATTR_SECURE | XFS_ATTR_INCOMPLETE);
		if ((e->flags & k) != 0)
			kdb_printf("0x%x", e->flags & k);
		kdb_printf(">\n     name \"");
		if (e->flags & XFS_ATTR_LOCAL) {
			l = XFS_ATTR_LEAF_NAME_LOCAL(leaf, j);
			for (k = 0; k < l->namelen; k++)
				kdb_printf("%c", l->nameval[k]);
			kdb_printf("\"(%d) value \"", l->namelen);
			for (k = 0; (k < be16_to_cpu(l->valuelen)) && (k < 32); k++)
				kdb_printf("%c", l->nameval[l->namelen + k]);
			if (k == 32)
				kdb_printf("...");
			kdb_printf("\"(%d)\n", be16_to_cpu(l->valuelen));
		} else {
			r = XFS_ATTR_LEAF_NAME_REMOTE(leaf, j);
			for (k = 0; k < r->namelen; k++)
				kdb_printf("%c", r->name[k]);
			kdb_printf("\"(%d) value blk 0x%x len %d\n",
				    r->namelen,
				    be32_to_cpu(r->valueblk),
				    be32_to_cpu(r->valuelen));
		}
	}
}

/*
 * Print a shortform attribute list.
 */
static void
xfsidbg_xattrsf(xfs_attr_shortform_t *s)
{
	xfs_attr_sf_hdr_t *sfh;
	xfs_attr_sf_entry_t *sfe;
	int i, j;

	sfh = &s->hdr;
	kdb_printf("hdr count %d\n", sfh->count);
	for (i = 0, sfe = s->list; i < sfh->count; i++) {
		kdb_printf("entry %d namelen %d name \"", i, sfe->namelen);
		for (j = 0; j < sfe->namelen; j++)
			kdb_printf("%c", sfe->nameval[j]);
		kdb_printf("\" valuelen %d value \"", sfe->valuelen);
		for (j = 0; (j < sfe->valuelen) && (j < 32); j++)
			kdb_printf("%c", sfe->nameval[sfe->namelen + j]);
		if (j == 32)
			kdb_printf("...");
		kdb_printf("\"\n");
		sfe = XFS_ATTR_SF_NEXTENTRY(sfe);
	}
}

#ifdef XFS_ATTR_TRACE
/*
 * Print out the last "count" entries in the attribute trace buffer.
 */
static void
xfsidbg_xattrtrace(int count)
{
	ktrace_entry_t  *ktep;
	ktrace_snap_t   kts;
	int	     nentries;
	int	     skip_entries;

	if (xfs_attr_trace_buf == NULL) {
		qprintf("The xfs attribute trace buffer is not initialized\n");
		return;
	}
	nentries = ktrace_nentries(xfs_attr_trace_buf);
	if (count == -1) {
		count = nentries;
	}
	if ((count <= 0) || (count > nentries)) {
		qprintf("Invalid count.  There are %d entries.\n", nentries);
		return;
	}

	ktep = ktrace_first(xfs_attr_trace_buf, &kts);
	if (count != nentries) {
		/*
		 * Skip the total minus the number to look at minus one
		 * for the entry returned by ktrace_first().
		 */
		skip_entries = nentries - count - 1;
		ktep = ktrace_skip(xfs_attr_trace_buf, skip_entries, &kts);
		if (ktep == NULL) {
			qprintf("Skipped them all\n");
			return;
		}
	}
	while (ktep != NULL) {
		xfs_attr_trace_entry(ktep);
		ktep = ktrace_next(xfs_attr_trace_buf, &kts);
	}
}
#endif

/*
 * Print xfs bmap internal record
 */
static void
xfsidbg_xbirec(xfs_bmbt_irec_t *r)
{
	kdb_printf(
	"startoff %Ld startblock %Lx blockcount %Ld state %Ld\n",
		(__uint64_t)r->br_startoff,
		(__uint64_t)r->br_startblock,
		(__uint64_t)r->br_blockcount,
		(__uint64_t)r->br_state);
}

#ifdef XFS_BLI_TRACE
/*
 * Print out the buf log item trace for the given buf log item.
 */
static void
xfsidbg_xblitrace(xfs_buf_log_item_t *bip)
{
	ktrace_entry_t  *ktep;
	ktrace_snap_t   kts;
	uint64_t	flags;
	static char *xbli_flags[] = {
		"hold",		/* 0x01 */
		"dirty",	/* 0x02 */
		"stale",	/* 0x04 */
		"logged",	/* 0x08 */
		NULL
		};
	static char *xli_flags[] = {
		"in ail",       /* 0x1 */
		NULL
		};

	if (bip->bli_trace == NULL) {
		qprintf("The bli trace buffer is not initialized\n");
		return;
	}

	ktep = ktrace_first(bip->bli_trace, &kts);
	while (ktep != NULL) {
		qprintf("%s bp 0x%p flags ",
			(char *)ktep->val[0], ktep->val[1]);
		printflags((__psint_t)(ktep->val[2]), xbli_flags, "xbli");
		qprintf("\n");
		qprintf("recur %ld refcount %ld blkno 0x%lx bcount 0x%lx\n",
			(long)ktep->val[3], (long)ktep->val[4],
			(unsigned long)ktep->val[5],
			(unsigned long)ktep->val[6]);
		flags = (((uint64_t)(unsigned long)ktep->val[7] << 32) &
					0xFFFFFFFF00000000ULL) |
			(((uint64_t)(unsigned long)ktep->val[8]) &
					0x00000000FFFFFFFFULL);
		qprintf("bp flags ");
		printflags(flags, bp_flag_vals, NULL);
		qprintf("\n");
		qprintf("fspriv 0x%p fspriv2 0x%p pincount %ld iodone 0x%p\n",
			ktep->val[9], ktep->val[10],
			(long)ktep->val[11], ktep->val[12]);
		qprintf("lockval %ld lid 0x%lx log item flags ",
			(long)ktep->val[13], (unsigned long)ktep->val[14]);
		printflags((__psint_t)(ktep->val[15]), xli_flags, "xli");
		qprintf("\n");

		ktep = ktrace_next(bip->bli_trace, &kts);
	}
}
#endif

/*
 * Print a bmap alloc argument structure for XFS.
 */
static void
xfsidbg_xbmalla(xfs_bmalloca_t *a)
{
	kdb_printf("tp 0x%p ip 0x%p eof %d prevp 0x%p\n",
		a->tp, a->ip, a->eof, a->prevp);
	kdb_printf("gotp 0x%p firstblock %s alen %d total %d\n",
		a->gotp, xfs_fmtfsblock(a->firstblock, a->ip->i_mount),
		a->alen, a->total);
	kdb_printf("off %s wasdel %d userdata %d minlen %d\n",
		xfs_fmtfsblock(a->off, a->ip->i_mount), a->wasdel,
		a->userdata, a->minlen);
	kdb_printf("minleft %d low %d rval %s aeof %d conv %d\n",
		a->minleft, a->low, xfs_fmtfsblock(a->rval, a->ip->i_mount),
		a->aeof, a->conv);
}

#ifdef XFS_BMAP_TRACE
/*
 * Print out the last "count" entries in a btree trace buffer.
 * The "a" is for "all" inodes.
 */
static void
xfsidbg_xbtatrace(int btnum, struct ktrace *trace_buf, int count)
{
	ktrace_entry_t  *ktep;
	ktrace_snap_t   kts;
	int	     nentries;
	int	     skip_entries;

	if (trace_buf == NULL) {
		qprintf("The xfs bmap btree trace buffer is not initialized\n");		return;
	}
	nentries = ktrace_nentries(trace_buf);
	if (count == -1) {
		count = nentries;
	}
	if ((count <= 0) || (count > nentries)) {
		qprintf("Invalid count.  There are %d entries.\n", nentries);
		return;
	}

	ktep = ktrace_first(trace_buf, &kts);
	if (count != nentries) {
		/*
		 * Skip the total minus the number to look at minus one
		 * for the entry returned by ktrace_first().
		 */
		skip_entries = nentries - count - 1;
		ktep = ktrace_skip(trace_buf, skip_entries, &kts);
		if (ktep == NULL) {
			qprintf("Skipped them all\n");
			return;
		}
	}
	while (ktep != NULL) {
		if (xfs_btree_trace_entry(btnum, ktep))
			qprintf("\n");
		ktep = ktrace_next(trace_buf, &kts);
	}
}

/*
 * Print out the bmap btree trace buffer attached to the given inode.
 */
static void
xfsidbg_xbmitrace(xfs_inode_t *ip)
{
	ktrace_entry_t  *ktep;
	ktrace_snap_t   kts;

	if (ip->i_btrace == NULL) {
		qprintf("The inode trace buffer is not initialized\n");
		return;
	}

	ktep = ktrace_first(ip->i_btrace, &kts);
	while (ktep != NULL) {
		if (xfs_btree_trace_entry(XFS_BTNUM_BMAP, ktep))
			qprintf("\n");
		ktep = ktrace_next(ip->i_btrace, &kts);
	}
}

/*
 * Print out all the entries in the bmap btree trace buf corresponding
 * to the given inode.  The "s" is for "single" inode.
 */
static void
xfsidbg_xbmstrace(xfs_inode_t *ip)
{
	ktrace_entry_t  *ktep;
	ktrace_snap_t   kts;

	if (xfs_bmbt_trace_buf == NULL) {
		qprintf("The xfs bmap btree trace buffer is not initialized\n");		return;
	}

	ktep = ktrace_first(xfs_bmbt_trace_buf, &kts);
	while (ktep != NULL) {
		if ((xfs_inode_t *)(ktep->val[2]) == ip) {
			if (xfs_btree_trace_entry(XFS_BTNUM_BMAP, ktep))
				qprintf("\n");
		}
		ktep = ktrace_next(xfs_bmbt_trace_buf, &kts);
	}
}
#endif

/*
 * Print xfs bmap record
 */
static void
xfsidbg_xbmbtrec(xfs_bmbt_rec_host_t *r)
{
	xfs_bmbt_irec_t	irec;

	xfs_bmbt_get_all(r, &irec);
	kdb_printf("startoff %Ld startblock %Lx blockcount %Ld flag %d\n",
		irec.br_startoff, (__uint64_t)irec.br_startblock,
		irec.br_blockcount, irec.br_state);
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

	kdb_printf("tp 0x%p mp 0x%p\n",
		c->bc_tp,
		c->bc_mp);
	if (c->bc_btnum == XFS_BTNUM_BMAP) {
		kdb_printf("rec.b ");
		xfsidbg_xbirec(&c->bc_rec.b);
	} else if (c->bc_btnum == XFS_BTNUM_INO) {
		kdb_printf("rec.i startino 0x%x freecount 0x%x free %Lx\n",
			c->bc_rec.i.ir_startino, c->bc_rec.i.ir_freecount,
			c->bc_rec.i.ir_free);
	} else {
		kdb_printf("rec.a startblock 0x%x blockcount 0x%x\n",
			c->bc_rec.a.ar_startblock,
			c->bc_rec.a.ar_blockcount);
	}
	kdb_printf("bufs");
	for (l = 0; l < c->bc_nlevels; l++)
		kdb_printf(" 0x%p", c->bc_bufs[l]);
	kdb_printf("\n");
	kdb_printf("ptrs");
	for (l = 0; l < c->bc_nlevels; l++)
		kdb_printf(" 0x%x", c->bc_ptrs[l]);
	kdb_printf("  ra");
	for (l = 0; l < c->bc_nlevels; l++)
		kdb_printf(" %d", c->bc_ra[l]);
	kdb_printf("\n");
	kdb_printf("nlevels %d btnum %s blocklog %d\n",
		c->bc_nlevels,
		c->bc_btnum == XFS_BTNUM_BNO ? "bno" :
		(c->bc_btnum == XFS_BTNUM_CNT ? "cnt" :
		 (c->bc_btnum == XFS_BTNUM_BMAP ? "bmap" : "ino")),
		c->bc_blocklog);
	if (c->bc_btnum == XFS_BTNUM_BMAP) {
		kdb_printf("private forksize 0x%x whichfork %d ip 0x%p flags %d\n",
			c->bc_private.b.forksize,
			c->bc_private.b.whichfork,
			c->bc_private.b.ip,
			c->bc_private.b.flags);
		kdb_printf("private firstblock %s flist 0x%p allocated 0x%x\n",
			xfs_fmtfsblock(c->bc_private.b.firstblock, c->bc_mp),
			c->bc_private.b.flist,
			c->bc_private.b.allocated);
	} else {
		kdb_printf("private agbp 0x%p agno 0x%x\n",
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
	xfs_dsb_t *sb;
	struct xfs_btree_block *bt;
	xfs_attr_leafblock_t *aleaf;
	xfs_da_intnode_t *node;
	xfs_dinode_t *di;
	xfs_disk_dquot_t *dqb;
	xfs_dir2_block_t *d2block;
	xfs_dir2_data_t *d2data;
	xfs_dir2_leaf_t *d2leaf;
	xfs_dir2_free_t *d2free;

	d = XFS_BUF_PTR(bp);
	if (be32_to_cpu((agf = d)->agf_magicnum) == XFS_AGF_MAGIC) {
		if (summary) {
			kdb_printf("freespace hdr for AG %d (at 0x%p)\n",
				be32_to_cpu(agf->agf_seqno), agf);
		} else {
			kdb_printf("buf 0x%p agf 0x%p\n", bp, agf);
			xfsidbg_xagf(agf);
		}
	} else if (be32_to_cpu((agi = d)->agi_magicnum) == XFS_AGI_MAGIC) {
		if (summary) {
			kdb_printf("Inode hdr for AG %d (at 0x%p)\n",
			       be32_to_cpu(agi->agi_seqno), agi);
		} else {
			kdb_printf("buf 0x%p agi 0x%p\n", bp, agi);
			xfsidbg_xagi(agi);
		}
	} else if (be32_to_cpu((bt = d)->bb_magic) == XFS_ABTB_MAGIC) {
		if (summary) {
			kdb_printf("Alloc BNO Btree blk, level %d (at 0x%p)\n",
				       be16_to_cpu(bt->bb_level), bt);
		} else {
			kdb_printf("buf 0x%p abtbno 0x%p\n", bp, bt);
			xfs_btblock(bt, XFS_BUF_COUNT(bp), &xfsidbg_allocbt);
		}
	} else if (be32_to_cpu((bt = d)->bb_magic) == XFS_ABTC_MAGIC) {
		if (summary) {
			kdb_printf("Alloc COUNT Btree blk, level %d (at 0x%p)\n",
				       be16_to_cpu(bt->bb_level), bt);
		} else {
			kdb_printf("buf 0x%p abtcnt 0x%p\n", bp, bt);
			xfs_btblock(bt, XFS_BUF_COUNT(bp), &xfsidbg_allocbt);
		}
	} else if (be32_to_cpu((bt = d)->bb_magic) == XFS_BMAP_MAGIC) {
		if (summary) {
			kdb_printf("Bmap Btree blk, level %d (at 0x%p)\n",
				      be16_to_cpu(bt->bb_level), bt);
		} else {
			kdb_printf("buf 0x%p bmapbt 0x%p\n", bp, bt);
			xfs_btblock(bt, XFS_BUF_COUNT(bp), &xfsidbg_bmbt);
		}
	} else if (be32_to_cpu((bt = d)->bb_magic) == XFS_IBT_MAGIC) {
		if (summary) {
			kdb_printf("Inode Btree blk, level %d (at 0x%p)\n",
				       be16_to_cpu(bt->bb_level), bt);
		} else {
			kdb_printf("buf 0x%p inobt 0x%p\n", bp, bt);
			xfs_btblock(bt, XFS_BUF_COUNT(bp), &xfsidbg_inobtbt);
		}
	} else if (be16_to_cpu((aleaf = d)->hdr.info.magic) == XFS_ATTR_LEAF_MAGIC) {
		if (summary) {
			kdb_printf("Attr Leaf, 1st hash 0x%x (at 0x%p)\n",
				      be32_to_cpu(aleaf->entries[0].hashval), aleaf);
		} else {
			kdb_printf("buf 0x%p attr leaf 0x%p\n", bp, aleaf);
			xfsidbg_xattrleaf(aleaf);
		}
	} else if (be16_to_cpu((node = d)->hdr.info.magic) == XFS_DA_NODE_MAGIC) {
		if (summary) {
			kdb_printf("Dir/Attr Node, level %d, 1st hash 0x%x (at 0x%p)\n",
			      node->hdr.level, node->btree[0].hashval, node);
		} else {
			kdb_printf("buf 0x%p dir/attr node 0x%p\n", bp, node);
			xfsidbg_xdanode(node);
		}
	} else if (be16_to_cpu((di = d)->di_magic) == XFS_DINODE_MAGIC) {
		if (summary) {
			kdb_printf("Disk Inode (at 0x%p)\n", di);
		} else {
			kdb_printf("buf 0x%p dinode 0x%p\n", bp, di);
			xfs_inodebuf(bp);
		}
	} else if (be32_to_cpu((sb = d)->sb_magicnum) == XFS_SB_MAGIC) {
		if (summary) {
			kdb_printf("Superblock (at 0x%p)\n", sb);
		} else {
			kdb_printf("buf 0x%p sb 0x%p\n", bp, sb);
			/* SB in a buffer - we need to convert */
			xfsidbg_xsb_convert(sb);
		}
	} else if ((dqb = d)->d_magic == cpu_to_be16(XFS_DQUOT_MAGIC)) {
#define XFSIDBG_DQTYPESTR(d)     \
	(((d)->d_flags & XFS_DQ_USER) ? "USR" : \
	(((d)->d_flags & XFS_DQ_GROUP) ? "GRP" : \
	(((d)->d_flags & XFS_DQ_PROJ) ? "PRJ" : "???")))
		kdb_printf("Quota blk starting ID [%d], type %s at 0x%p\n",
			be32_to_cpu(dqb->d_id), XFSIDBG_DQTYPESTR(dqb), dqb);

	} else if (be32_to_cpu((d2block = d)->hdr.magic) == XFS_DIR2_BLOCK_MAGIC) {
		if (summary) {
			kdb_printf("Dir2 block (at 0x%p)\n", d2block);
		} else {
			kdb_printf("buf 0x%p dir2 block 0x%p\n", bp, d2block);
			xfs_dir2data((void *)d2block, XFS_BUF_COUNT(bp));
		}
	} else if (be32_to_cpu((d2data = d)->hdr.magic) == XFS_DIR2_DATA_MAGIC) {
		if (summary) {
			kdb_printf("Dir2 data (at 0x%p)\n", d2data);
		} else {
			kdb_printf("buf 0x%p dir2 data 0x%p\n", bp, d2data);
			xfs_dir2data((void *)d2data, XFS_BUF_COUNT(bp));
		}
	} else if (be16_to_cpu((d2leaf = d)->hdr.info.magic) == XFS_DIR2_LEAF1_MAGIC) {
		if (summary) {
			kdb_printf("Dir2 leaf(1) (at 0x%p)\n", d2leaf);
		} else {
			kdb_printf("buf 0x%p dir2 leaf 0x%p\n", bp, d2leaf);
			xfs_dir2leaf(d2leaf, XFS_BUF_COUNT(bp));
		}
	} else if (be16_to_cpu(d2leaf->hdr.info.magic) == XFS_DIR2_LEAFN_MAGIC) {
		if (summary) {
			kdb_printf("Dir2 leaf(n) (at 0x%p)\n", d2leaf);
		} else {
			kdb_printf("buf 0x%p dir2 leaf 0x%p\n", bp, d2leaf);
			xfs_dir2leaf(d2leaf, XFS_BUF_COUNT(bp));
		}
	} else if (be32_to_cpu((d2free = d)->hdr.magic) == XFS_DIR2_FREE_MAGIC) {
		if (summary) {
			kdb_printf("Dir2 free (at 0x%p)\n", d2free);
		} else {
			kdb_printf("buf 0x%p dir2 free 0x%p\n", bp, d2free);
			xfsidbg_xdir2free(d2free);
		}
	} else {
		kdb_printf("buf 0x%p unknown 0x%p\n", bp, d);
	}
}

#ifdef XFS_BMAP_TRACE
/*
 * Print out the last "count" entries in the bmap extent trace buffer.
 * The "a" is for "all" inodes.
 */
static void
xfsidbg_xbxatrace(int count)
{
	ktrace_entry_t  *ktep;
	ktrace_snap_t   kts;
	int	     nentries;
	int	     skip_entries;

	if (xfs_bmap_trace_buf == NULL) {
		qprintf("The xfs bmap extent trace buffer is not initialized\n");
		return;
	}
	nentries = ktrace_nentries(xfs_bmap_trace_buf);
	if (count == -1) {
		count = nentries;
	}
	if ((count <= 0) || (count > nentries)) {
		qprintf("Invalid count.  There are %d entries.\n", nentries);
		return;
	}

	ktep = ktrace_first(xfs_bmap_trace_buf, &kts);
	if (count != nentries) {
		/*
		 * Skip the total minus the number to look at minus one
		 * for the entry returned by ktrace_first().
		 */
		skip_entries = nentries - count - 1;
		ktep = ktrace_skip(xfs_bmap_trace_buf, skip_entries, &kts);
		if (ktep == NULL) {
			qprintf("Skipped them all\n");
			return;
		}
	}
	while (ktep != NULL) {
		if (xfs_bmap_trace_entry(ktep))
			qprintf("\n");
		ktep = ktrace_next(xfs_bmap_trace_buf, &kts);
	}
}

/*
 * Print out the bmap extent trace buffer attached to the given inode.
 */
static void
xfsidbg_xbxitrace(xfs_inode_t *ip)
{
	ktrace_entry_t  *ktep;
	ktrace_snap_t   kts;
	if (ip->i_xtrace == NULL) {
		qprintf("The inode trace buffer is not initialized\n");
		return;
	}

	ktep = ktrace_first(ip->i_xtrace, &kts);
	while (ktep != NULL) {
		if (xfs_bmap_trace_entry(ktep))
			qprintf("\n");
		ktep = ktrace_next(ip->i_xtrace, &kts);
	}
}

/*
 * Print out all the entries in the bmap extent trace buf corresponding
 * to the given inode.  The "s" is for "single" inode.
 */
static void
xfsidbg_xbxstrace(xfs_inode_t *ip)
{
	ktrace_entry_t  *ktep;
	ktrace_snap_t   kts;

	if (xfs_bmap_trace_buf == NULL) {
		qprintf("The xfs bmap extent trace buffer is not initialized\n");
		return;
	}

	ktep = ktrace_first(xfs_bmap_trace_buf, &kts);
	while (ktep != NULL) {
		if ((xfs_inode_t *)(ktep->val[3]) == ip) {
			if (xfs_bmap_trace_entry(ktep))
				qprintf("\n");
		}
		ktep = ktrace_next(xfs_bmap_trace_buf, &kts);
	}
}
#endif

#ifdef XFS_ILOCK_TRACE
/*
 * Print out the ilock trace buffer attached to the given inode.
 */
static void
xfsidbg_xilock_trace(xfs_inode_t *ip)
{
	static char *xiflags[] = {
		"IOLOCK_EXCL",
		"IOLOCK_SHAR",
		"ILOCK_EXCL",
		"ILOCK_SHAR",
		"IUNLK_NONOT",
		NULL
	};

	ktrace_entry_t  *ktep;
	ktrace_snap_t   kts;
	if (ip->i_lock_trace == NULL) {
		qprintf("The inode ilock trace buffer is not initialized\n");
		return;
	}

	ktep = ktrace_first(ip->i_lock_trace, &kts);
	while (ktep != NULL) {
		 if ((__psint_t)ktep->val[0] &&
		     (__psint_t)ktep->val[7] == 0) {
			 printflags((__psint_t)ktep->val[2], xiflags,"Flags ");
			 if ((__psint_t)ktep->val[1] == 1)
				 qprintf("LOCK\n");
			 else if ((__psint_t)ktep->val[1] == 2)
				 qprintf("LOCK SHARED\n");
			 else if ((__psint_t)ktep->val[1] == 3)
				 qprintf("UNLOCK\n");
			qprintf("ip 0x%p %lld %ld\n",
				ktep->val[0], (unsigned long long)
				((xfs_inode_t*)ktep->val[0])->i_ino,
				(long)ktep->val[6]);
			 qprintf("raddr 0x%p\n", ktep->val[3]);
			 qprintf("  Pid %ld, cpu %ld\n",
				 (long)ktep->val[5],
				 (long)ktep->val[4]);
			 qprintf("-----------------------\n");
		 } else if ((__psint_t)ktep->val[7] == 1) {
			if ((__psint_t)ktep->val[1] == 1)
				qprintf("LOCK ");
			else if ((__psint_t)ktep->val[1] == 2)
				qprintf("TRYLOCK %ld ",
					(long)ktep->val[2]);
			else if ((__psint_t)ktep->val[1] == 3)
				qprintf("UNLOCK ");
			else     qprintf("UNKNOWN ");
			qprintf("ip 0x%p %lld %ld\n",
				ktep->val[0], (unsigned long long)
				((xfs_inode_t*)ktep->val[0])->i_ino,
				(long)ktep->val[6]);
			qprintf("raddr 0x%p\n", ktep->val[3]);
			qprintf("  Pid %ld, cpu %ld\n",
				(long)ktep->val[5],
				(long)ktep->val[4]);
			qprintf("-----------------------\n");
		 }

		 ktep = ktrace_next(ip->i_lock_trace, &kts);
	}
}
#endif

#ifdef XFS_FILESTREAMS_TRACE
static void
xfs_filestreams_trace_entry(ktrace_entry_t *ktep)
{
	xfs_inode_t	*ip, *pip;

	/* function:line#[pid]: */
	kdb_printf("%s:%lu[%lu]: ", (char *)ktep->val[1],
			((unsigned long)ktep->val[0] >> 16) & 0xffff,
			(unsigned long)ktep->val[2]);
	switch ((unsigned long)ktep->val[0] & 0xffff) {
	case XFS_FSTRM_KTRACE_INFO:
		break;
	case XFS_FSTRM_KTRACE_AGSCAN:
		kdb_printf("scanning AG %ld[%ld]",
				(long)ktep->val[4], (long)ktep->val[5]);
		break;
	case XFS_FSTRM_KTRACE_AGPICK1:
		kdb_printf("using max_ag %ld[1] with maxfree %ld",
				(long)ktep->val[4], (long)ktep->val[5]);
		break;
	case XFS_FSTRM_KTRACE_AGPICK2:

		kdb_printf("startag %ld newag %ld[%ld] free %ld scanned %ld"
				" flags 0x%lx",
				(long)ktep->val[4], (long)ktep->val[5],
				(long)ktep->val[6], (long)ktep->val[7],
				(long)ktep->val[8], (long)ktep->val[9]);
		break;
	case XFS_FSTRM_KTRACE_UPDATE:
		ip = (xfs_inode_t *)ktep->val[4];
		if ((__psint_t)ktep->val[5] != (__psint_t)ktep->val[7])
			kdb_printf("found ip %p ino %llu, AG %ld[%ld] ->"
				" %ld[%ld]", ip, (unsigned long long)ip->i_ino,
				(long)ktep->val[7], (long)ktep->val[8],
				(long)ktep->val[5], (long)ktep->val[6]);
		else
			kdb_printf("found ip %p ino %llu, AG %ld[%ld]",
				ip, (unsigned long long)ip->i_ino,
				(long)ktep->val[5], (long)ktep->val[6]);
		break;

	case XFS_FSTRM_KTRACE_FREE:
		ip = (xfs_inode_t *)ktep->val[4];
		pip = (xfs_inode_t *)ktep->val[5];
		if (ip->i_d.di_mode & S_IFDIR)
			kdb_printf("deleting dip %p ino %llu, AG %ld[%ld]",
			       ip, (unsigned long long)ip->i_ino,
			       (long)ktep->val[6], (long)ktep->val[7]);
		else
			kdb_printf("deleting file %p ino %llu, pip %p ino %llu"
				", AG %ld[%ld]",
				ip, (unsigned long long)ip->i_ino,
				pip, (unsigned long long)(pip ?  pip->i_ino : 0),
				(long)ktep->val[6], (long)ktep->val[7]);
		break;

	case XFS_FSTRM_KTRACE_ITEM_LOOKUP:
		ip = (xfs_inode_t *)ktep->val[4];
		pip = (xfs_inode_t *)ktep->val[5];
		if (!pip) {
			kdb_printf("lookup on %s ip %p ino %llu failed, returning %ld",
			       ip->i_d.di_mode & S_IFREG ? "file" : "dir", ip,
			       (unsigned long long)ip->i_ino, (long)ktep->val[6]);
		} else if (ip->i_d.di_mode & S_IFREG)
			kdb_printf("lookup on file ip %p ino %llu dir %p"
				" dino %llu got AG %ld[%ld]",
				ip, (unsigned long long)ip->i_ino,
				pip, (unsigned long long)pip->i_ino,
				(long)ktep->val[6], (long)ktep->val[7]);
		else
			kdb_printf("lookup on dir ip %p ino %llu got AG %ld[%ld]",
				ip, (unsigned long long)ip->i_ino,
				(long)ktep->val[6], (long)ktep->val[7]);
		break;

	case XFS_FSTRM_KTRACE_ASSOCIATE:
		ip = (xfs_inode_t *)ktep->val[4];
		pip = (xfs_inode_t *)ktep->val[5];
		kdb_printf("pip %p ino %llu and ip %p ino %llu given ag %ld[%ld]",
				pip, (unsigned long long)pip->i_ino,
				ip, (unsigned long long)ip->i_ino,
				(long)ktep->val[6], (long)ktep->val[7]);
		break;

	case XFS_FSTRM_KTRACE_MOVEAG:
		ip = ktep->val[4];
		pip = ktep->val[5];
		if ((long)ktep->val[6] != NULLAGNUMBER)
			kdb_printf("dir %p ino %llu to file ip %p ino %llu has"
				" moved %ld[%ld] -> %ld[%ld]",
				pip, (unsigned long long)pip->i_ino,
				ip, (unsigned long long)ip->i_ino,
				(long)ktep->val[6], (long)ktep->val[7],
				(long)ktep->val[8], (long)ktep->val[9]);
		else
			kdb_printf("pip %p ino %llu and ip %p ino %llu moved"
				" to new ag %ld[%ld]",
				pip, (unsigned long long)pip->i_ino,
				ip, (unsigned long long)ip->i_ino,
				(long)ktep->val[8], (long)ktep->val[9]);
		break;

	case XFS_FSTRM_KTRACE_ORPHAN:
		ip = ktep->val[4];
		kdb_printf("gave ag %ld to orphan ip %p ino %llu",
				(unsigned long)ktep->val[5],
				ip, (unsigned long long)ip->i_ino);
		break;
	default:
		kdb_printf("unknown trace type 0x%lx",
				(unsigned long)ktep->val[0] & 0xffff);
	}
	kdb_printf("\n");
}

static void
xfsidbg_filestreams_trace(int count)
{
	ktrace_entry_t  *ktep;
	ktrace_snap_t   kts;
	int	     nentries;
	int	     skip_entries;

	if (xfs_filestreams_trace_buf == NULL) {
		qprintf("The xfs inode lock trace buffer is not initialized\n");
		return;
	}
	nentries = ktrace_nentries(xfs_filestreams_trace_buf);
	if (count == -1) {
		count = nentries;
	}
	if ((count <= 0) || (count > nentries)) {
		qprintf("Invalid count.  There are %d entries.\n", nentries);
		return;
	}

	ktep = ktrace_first(xfs_filestreams_trace_buf, &kts);
	if (count != nentries) {
		/*
		 * Skip the total minus the number to look at minus one
		 * for the entry returned by ktrace_first().
		 */
		skip_entries = nentries - count - 1;
		ktep = ktrace_skip(xfs_filestreams_trace_buf, skip_entries, &kts);
		if (ktep == NULL) {
			qprintf("Skipped them all\n");
			return;
		}
	}
	while (ktep != NULL) {
		xfs_filestreams_trace_entry(ktep);
		ktep = ktrace_next(xfs_filestreams_trace_buf, &kts);
	}
}
#endif
/*
 * Compute & print buffer's checksum.
 */
static void
xfsidbg_xchksum(uint *addr)
{
	uint i, chksum = 0;

	if (((__psint_t)addr) == ((__psint_t)-1)) {
		qprintf("USAGE xchksum <address>\n");
		qprintf("       length is set with xarg\n");
	} else {
		for (i=0; i<xargument; i++) {
			chksum ^= *addr;
			addr++;
		}
		qprintf("chksum (0x%x)  length (%d)\n", chksum, xargument);
	}
}       /* xfsidbg_xchksum */

/*
 * Print an xfs_da_args structure.
 */
static void
xfsidbg_xdaargs(xfs_da_args_t *n)
{
	char *ch;
	int i;

	kdb_printf(" name \"");
	for (i = 0; i < n->namelen; i++) {
		kdb_printf("%c", n->name[i]);
	}
	kdb_printf("\"(%d) value ", n->namelen);
	if (n->value) {
		kdb_printf("\"");
		ch = n->value;
		for (i = 0; (i < n->valuelen) && (i < 32); ch++, i++) {
			switch(*ch) {
			case '\n':	kdb_printf("\n");		break;
			case '\b':	kdb_printf("\b");		break;
			case '\t':	kdb_printf("\t");		break;
			default:	kdb_printf("%c", *ch);	break;
			}
		}
		if (i == 32)
			kdb_printf("...");
		kdb_printf("\"(%d)\n", n->valuelen);
	} else {
		kdb_printf("(NULL)(%d)\n", n->valuelen);
	}
	kdb_printf(" hashval 0x%x whichfork %d flags <",
		  (uint_t)n->hashval, n->whichfork);
	if (n->flags & ATTR_ROOT)
		kdb_printf("ROOT ");
	if (n->flags & ATTR_SECURE)
		kdb_printf("SECURE ");
	if (n->flags & ATTR_CREATE)
		kdb_printf("CREATE ");
	if (n->flags & ATTR_REPLACE)
		kdb_printf("REPLACE ");
	if (n->flags & XFS_ATTR_INCOMPLETE)
		kdb_printf("INCOMPLETE ");
	i = ~(ATTR_ROOT | ATTR_SECURE |
		ATTR_CREATE | ATTR_REPLACE | XFS_ATTR_INCOMPLETE);
	if ((n->flags & i) != 0)
		kdb_printf("0x%x", n->flags & i);
	kdb_printf(">\n");
	kdb_printf(" rename %d justcheck %d addname %d oknoent %d\n",
		  (n->op_flags & XFS_DA_OP_RENAME) != 0,
		  (n->op_flags & XFS_DA_OP_JUSTCHECK) != 0,
		  (n->op_flags & XFS_DA_OP_ADDNAME) != 0,
		  (n->op_flags & XFS_DA_OP_OKNOENT) != 0);
	kdb_printf(" leaf: blkno %d index %d rmtblkno %d rmtblkcnt %d\n",
		  n->blkno, n->index, n->rmtblkno, n->rmtblkcnt);
	kdb_printf(" leaf2: blkno %d index %d rmtblkno %d rmtblkcnt %d\n",
		  n->blkno2, n->index2, n->rmtblkno2, n->rmtblkcnt2);
	kdb_printf(" inumber %llu dp 0x%p firstblock 0x%p flist 0x%p\n",
		  (unsigned long long) n->inumber,
		  n->dp, n->firstblock, n->flist);
	kdb_printf(" trans 0x%p total %d\n",
		  n->trans, n->total);
}

/*
 * Print a da buffer structure.
 */
static void
xfsidbg_xdabuf(xfs_dabuf_t *dabuf)
{
	int	i;

	kdb_printf("nbuf %d dirty %d bbcount %d data 0x%p bps",
		dabuf->nbuf, dabuf->dirty, dabuf->bbcount, dabuf->data);
	for (i = 0; i < dabuf->nbuf; i++)
		kdb_printf(" %d:0x%p", i, dabuf->bps[i]);
	kdb_printf("\n");
#ifdef XFS_DABUF_DEBUG
	kdb_printf(" ra 0x%x prev 0x%x next 0x%x dev 0x%x blkno 0x%x\n",
		dabuf->ra, dabuf->prev, dabuf->next, dabuf->dev, dabuf->blkno);
#endif
}

/*
 * Print a directory/attribute internal node block.
 */
static void
xfsidbg_xdanode(xfs_da_intnode_t *node)
{
	xfs_da_node_hdr_t *h;
	xfs_da_blkinfo_t *i;
	xfs_da_node_entry_t *e;
	int j;

	h = &node->hdr;
	i = &h->info;
	kdb_printf("hdr info forw 0x%x back 0x%x magic 0x%x\n",
		be32_to_cpu(i->forw), be32_to_cpu(i->back), be16_to_cpu(i->magic));
	kdb_printf("hdr count %d level %d\n",
		be16_to_cpu(h->count), be16_to_cpu(h->level));
	for (j = 0, e = node->btree; j < be16_to_cpu(h->count); j++, e++) {
		kdb_printf("btree %d hashval 0x%x before 0x%x\n",
			j, be32_to_cpu(e->hashval), be32_to_cpu(e->before));
	}
}

/*
 * Print an xfs_da_state_blk structure.
 */
static void
xfsidbg_xdastate(xfs_da_state_t *s)
{
	xfs_da_state_blk_t *eblk;

	kdb_printf("args 0x%p mp 0x%p blocksize %u node_ents %u inleaf %u\n",
		s->args, s->mp, s->blocksize, s->node_ents, s->inleaf);
	if (s->args)
		xfsidbg_xdaargs(s->args);

	kdb_printf("path:  ");
	xfs_dastate_path(&s->path);

	kdb_printf("altpath:  ");
	xfs_dastate_path(&s->altpath);

	eblk = &s->extrablk;
	kdb_printf("extra: valid %d, after %d\n", s->extravalid, s->extraafter);
	kdb_printf(" bp 0x%p blkno 0x%x ", eblk->bp, eblk->blkno);
	kdb_printf("index %d hashval 0x%x\n", eblk->index, (uint_t)eblk->hashval);
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
	xfs_dir2_leaf_entry_t *l=NULL;
	int j, k;
	char *p;
	char *t;
	xfs_dir2_block_tail_t *tail=NULL;

	db = (xfs_dir2_data_t *)addr;
	bb = (xfs_dir2_block_t *)addr;
	h = &db->hdr;
	kdb_printf("hdr magic 0x%x (%s)\nhdr bestfree", be32_to_cpu(h->magic),
		be32_to_cpu(h->magic) == XFS_DIR2_DATA_MAGIC ? "DATA" :
			(be32_to_cpu(h->magic) == XFS_DIR2_BLOCK_MAGIC ? "BLOCK" : ""));
	for (j = 0, m = h->bestfree; j < XFS_DIR2_DATA_FD_COUNT; j++, m++) {
		kdb_printf(" %d: 0x%x@0x%x", j,
			   be16_to_cpu(m->length),
			   be16_to_cpu(m->offset));
	}
	kdb_printf("\n");
	if (be32_to_cpu(h->magic) == XFS_DIR2_DATA_MAGIC)
		t = (char *)db + size;
	else {
		/* XFS_DIR2_BLOCK_TAIL_P */
		tail = (xfs_dir2_block_tail_t *)
		       ((char *)bb + size - sizeof(xfs_dir2_block_tail_t));
		l = xfs_dir2_block_leaf_p(tail);
		t = (char *)l;
	}
	for (p = (char *)(h + 1); p < t; ) {
		u = (xfs_dir2_data_unused_t *)p;
		if (be16_to_cpu(u->freetag) == XFS_DIR2_DATA_FREE_TAG) {
			kdb_printf("0x%lx unused freetag 0x%x length 0x%x tag 0x%x\n",
				(unsigned long) (p - (char *)addr),
				be16_to_cpu(u->freetag),
				be16_to_cpu(u->length),
				be16_to_cpu(*xfs_dir2_data_unused_tag_p(u)));
			p += be16_to_cpu(u->length);
			continue;
		}
		e = (xfs_dir2_data_entry_t *)p;
		kdb_printf("0x%lx entry inumber %llu namelen %d name \"",
			(unsigned long) (p - (char *)addr),
			(unsigned long long) be64_to_cpu(e->inumber),
			e->namelen);
		for (k = 0; k < e->namelen; k++)
			kdb_printf("%c", e->name[k]);
		kdb_printf("\" tag 0x%x\n", be16_to_cpu(*xfs_dir2_data_entry_tag_p(e)));
		p += xfs_dir2_data_entsize(e->namelen);
	}
	if (be32_to_cpu(h->magic) == XFS_DIR2_DATA_MAGIC)
		return;
	for (j = 0; j < be32_to_cpu(tail->count); j++, l++) {
		kdb_printf("0x%lx leaf %d hashval 0x%x address 0x%x (byte 0x%x)\n",
			(unsigned long) ((char *)l - (char *)addr), j,
			be32_to_cpu(l->hashval),
			be32_to_cpu(l->address),
			/* XFS_DIR2_DATAPTR_TO_BYTE */
			be32_to_cpu(l->address) << XFS_DIR2_DATA_ALIGN_LOG);
	}
	kdb_printf("0x%lx tail count %d\n",
		(unsigned long) ((char *)tail - (char *)addr),
		be32_to_cpu(tail->count));
}

static void
xfs_dir2leaf(xfs_dir2_leaf_t *leaf, int size)
{
	xfs_dir2_leaf_hdr_t *h;
	xfs_da_blkinfo_t *i;
	xfs_dir2_leaf_entry_t *e;
	xfs_dir2_leaf_tail_t *t;
	__be16 *b;
	int j;

	h = &leaf->hdr;
	i = &h->info;
	e = leaf->ents;
	kdb_printf("hdr info forw 0x%x back 0x%x magic 0x%x\n",
		be32_to_cpu(i->forw), be32_to_cpu(i->back), be16_to_cpu(i->magic));
	kdb_printf("hdr count %d stale %d\n", be16_to_cpu(h->count), be16_to_cpu(h->stale));
	for (j = 0; j < be16_to_cpu(h->count); j++, e++) {
		kdb_printf("0x%lx ent %d hashval 0x%x address 0x%x (byte 0x%x)\n",
			(unsigned long) ((char *)e - (char *)leaf), j,
			be32_to_cpu(e->hashval),
			be32_to_cpu(e->address),
			/* XFS_DIR2_DATAPTR_TO_BYTE */
			be32_to_cpu(e->address) << XFS_DIR2_DATA_ALIGN_LOG);
	}
	if (be16_to_cpu(i->magic) == XFS_DIR2_LEAFN_MAGIC)
		return;
	/* XFS_DIR2_LEAF_TAIL_P */
	t = (xfs_dir2_leaf_tail_t *)((char *)leaf + size - sizeof(*t));
	b = xfs_dir2_leaf_bests_p(t);
	for (j = 0; j < be32_to_cpu(t->bestcount); j++, b++) {
		kdb_printf("0x%lx best %d 0x%x\n",
			(unsigned long) ((char *)b - (char *)leaf), j,
			be16_to_cpu(*b));
	}
	kdb_printf("tail bestcount %d\n", be32_to_cpu(t->bestcount));
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
	ino = xfs_dir2_sf_get_inumber(s, &sfh->parent);
	kdb_printf("hdr count %d i8count %d parent %llu\n",
		sfh->count, sfh->i8count, (unsigned long long) ino);
	for (i = 0, sfe = xfs_dir2_sf_firstentry(s); i < sfh->count; i++) {
		ino = xfs_dir2_sf_get_inumber(s, xfs_dir2_sf_inumberp(sfe));
		kdb_printf("entry %d inumber %llu offset 0x%x namelen %d name \"",
			i, (unsigned long long) ino,
			xfs_dir2_sf_get_offset(sfe),
			sfe->namelen);
		for (j = 0; j < sfe->namelen; j++)
			kdb_printf("%c", sfe->name[j]);
		kdb_printf("\"\n");
		sfe = xfs_dir2_sf_nextentry(s, sfe);
	}
}

/*
 * Print a node-form v2 directory freemap block.
 */
static void
xfsidbg_xdir2free(xfs_dir2_free_t *f)
{
	int	i;

	kdb_printf("hdr magic 0x%x firstdb %d nvalid %d nused %d\n",
		   be32_to_cpu(f->hdr.magic), be32_to_cpu(f->hdr.firstdb),
		   be32_to_cpu(f->hdr.nvalid), be32_to_cpu(f->hdr.nused));
	for (i = 0; i < be32_to_cpu(f->hdr.nvalid); i++) {
		kdb_printf("entry %d db %d count %d\n",
			i, i + be32_to_cpu(f->hdr.firstdb), be16_to_cpu(f->bests[i]));
	}
}

#ifdef XFS_DIR2_TRACE
/*
 * Print out the last "count" entries in the directory v2 trace buffer.
 */
static void
xfsidbg_xdir2atrace(int count)
{
	ktrace_entry_t  *ktep;
	ktrace_snap_t   kts;
	int	     nentries;
	int	     skip_entries;

	if (xfs_dir2_trace_buf == NULL) {
		qprintf("The xfs dirv2 trace buffer is not initialized\n");
		return;
	}
	nentries = ktrace_nentries(xfs_dir2_trace_buf);
	if (count == -1) {
		count = nentries;
	}
	if ((count <= 0) || (count > nentries)) {
		qprintf("Invalid count.  There are %d entries.\n", nentries);
		return;
	}

	ktep = ktrace_first(xfs_dir2_trace_buf, &kts);
	if (count != nentries) {
		/*
		 * Skip the total minus the number to look at minus one
		 * for the entry returned by ktrace_first().
		 */
		skip_entries = nentries - count - 1;
		ktep = ktrace_skip(xfs_dir2_trace_buf, skip_entries, &kts);
		if (ktep == NULL) {
			qprintf("Skipped them all\n");
			return;
		}
	}
	while (ktep != NULL) {
		if (xfs_dir2_trace_entry(ktep))
			qprintf("\n");
		ktep = ktrace_next(xfs_dir2_trace_buf, &kts);
	}
}

/*
 * Print out the directory v2 trace buffer attached to the given inode.
 */
static void
xfsidbg_xdir2itrace(xfs_inode_t *ip)
{
	ktrace_entry_t  *ktep;
	ktrace_snap_t   kts;

	if (ip->i_dir_trace == NULL) {
		qprintf("The inode trace buffer is not initialized\n");
		return;
	}

	ktep = ktrace_first(ip->i_dir_trace, &kts);
	while (ktep != NULL) {
		if (xfs_dir2_trace_entry(ktep))
			qprintf("\n");
		ktep = ktrace_next(ip->i_dir_trace, &kts);
	}
}
#endif

/*
 * Print xfs extent records.
 */
static void
xfsidbg_xexlist(xfs_inode_t *ip)
{
	xfs_xexlist_fork(ip, XFS_DATA_FORK);
	if (XFS_IFORK_Q(ip))
		xfs_xexlist_fork(ip, XFS_ATTR_FORK);
}

/*
 * Print an xfs free-extent records.
 */
static void
xfsidbg_xflist(xfs_bmap_free_t *flist)
{
	xfs_bmap_free_item_t	*item;

	kdb_printf("flist@0x%p: first 0x%p count %d low %d\n", flist,
		flist->xbf_first, flist->xbf_count, flist->xbf_low);
	for (item = flist->xbf_first; item; item = item->xbfi_next) {
		kdb_printf("item@0x%p: startblock %Lx blockcount %d", item,
			(xfs_dfsbno_t)item->xbfi_startblock,
			item->xbfi_blockcount);
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
		kdb_printf("%-16s %s %s\n", p->name, p->args, p->help);
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
		NULL
	};

	kdb_printf("xlog_in_core/header at 0x%p/0x%p\n",
		iclog, iclog->ic_data);
	kdb_printf("magicno: %x  cycle: %d  version: %d  lsn: 0x%Lx\n",
		be32_to_cpu(iclog->ic_header.h_magicno),
		be32_to_cpu(iclog->ic_header.h_cycle),
		be32_to_cpu(iclog->ic_header.h_version),
		(unsigned long long)be64_to_cpu(iclog->ic_header.h_lsn));
	kdb_printf("tail_lsn: 0x%Lx  len: %d  prev_block: %d  num_ops: %d\n",
		(unsigned long long)be64_to_cpu(iclog->ic_header.h_tail_lsn),
		be32_to_cpu(iclog->ic_header.h_len),
		be32_to_cpu(iclog->ic_header.h_prev_block),
		be32_to_cpu(iclog->ic_header.h_num_logops));
	kdb_printf("cycle_data: ");
	for (i=0; i<(iclog->ic_size>>BBSHIFT); i++) {
		kdb_printf("%x  ", be32_to_cpu(iclog->ic_header.h_cycle_data[i]));
	}
	kdb_printf("\n");
	kdb_printf("size: %d\n", be32_to_cpu(iclog->ic_header.h_size));
	kdb_printf("\n");
	kdb_printf("--------------------------------------------------\n");
	kdb_printf("data: 0x%p next: 0x%p bp: 0x%p\n",
		iclog->ic_datap, iclog->ic_next, iclog->ic_bp);
	kdb_printf("log: 0x%p  callb: 0x%p  callb_tail: 0x%p\n",
		iclog->ic_log, iclog->ic_callback, iclog->ic_callback_tail);
	kdb_printf("size: %d (OFFSET: %d) trace: 0x%p refcnt: %d bwritecnt: %d",
		iclog->ic_size, iclog->ic_offset,
#ifdef XFS_LOG_TRACE
		iclog->ic_trace,
#else
		NULL,
#endif
		atomic_read(&iclog->ic_refcnt), iclog->ic_bwritecnt);
	if (iclog->ic_state & XLOG_STATE_ALL)
		printflags(iclog->ic_state, ic_flags, " state:");
	else
		kdb_printf(" state: INVALID 0x%x", iclog->ic_state);
	kdb_printf("\n");
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
	kdb_printf("=================================================\n");
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
	kdb_symtab_t		 symtab;

	for (cb = iclog->ic_callback; cb != NULL; cb = cb->cb_next) {

		if (kdbnearsym((unsigned long)cb->cb_func, &symtab)) {
			unsigned long offval;

			offval = (unsigned long)cb->cb_func - symtab.sym_start;

			if (offval)
				kdb_printf("func = %s+0x%lx",
							symtab.sym_name,
							offval);
			else
				kdb_printf("func = %s", symtab.sym_name);
		} else
			kdb_printf("func = ?? 0x%p", (void *)cb->cb_func);

		kdb_printf(" arg 0x%p next 0x%p\n", cb->cb_arg, cb->cb_next);
	}
}

#ifdef XFS_LOG_TRACE
/*
 * Print trace from incore log.
 */
static void
xfsidbg_xiclogtrace(xlog_in_core_t *iclog)
{
	ktrace_entry_t  *ktep;
	ktrace_snap_t   kts;
	ktrace_t	*kt = iclog->ic_trace;

	qprintf("iclog->ic_trace 0x%p\n", kt);
	ktep = ktrace_first(kt, &kts);
	while (ktep != NULL) {
		switch ((__psint_t)ktep->val[0]) {
		    case XLOG_TRACE_GRAB_FLUSH: {
			    qprintf("grabbing semaphore\n");
			    break;
		    }
		    case XLOG_TRACE_REL_FLUSH: {
			    qprintf("releasing semaphore\n");
			    break;
		    }
		    case XLOG_TRACE_SLEEP_FLUSH: {
			    qprintf("sleeping on semaphore\n");
			    break;
		    }
		    case XLOG_TRACE_WAKE_FLUSH: {
			    qprintf("waking up on semaphore\n");
			    break;
		    }
		    default: {
		    }
		}
		ktep = ktrace_next(kt, &kts);
	}
}       /* xfsidbg_xiclogtrace */
#endif

/*
 * Print all of the inodes attached to the given mount structure.
 */
static void
xfsidbg_xinodes(xfs_mount_t *mp)
{
	int		i;

	kdb_printf("xfs_mount at 0x%p\n", mp);
	for (i = 0; i < mp->m_sb.sb_agcount; i++) {
		xfs_perag_t	*pag = &mp->m_perag[i];
		xfs_inode_t	*ip = NULL;
		int		first_index = 0;
		int		nr_found;

		if (!pag->pag_ici_init)
			continue;
		do {
			nr_found = radix_tree_gang_lookup(&pag->pag_ici_root,
						(void**)&ip, first_index, 1);
			if (!nr_found)
				break;
			/* update the index for the next lookup */
			first_index = XFS_INO_TO_AGINO(mp, ip->i_ino + 1);
			kdb_printf("\n");
			xfsidbg_xnode(ip);
		} while (nr_found);
	}
	kdb_printf("\nEnd of Inodes\n");
}

static void
xfsidbg_delayed_blocks(xfs_mount_t *mp)
{
	unsigned int	total = 0;
	unsigned int	icount = 0;
	int		i;

	for (i = 0; i < mp->m_sb.sb_agcount; i++) {
		xfs_perag_t	*pag = &mp->m_perag[i];
		xfs_inode_t	*ip = NULL;
		int		first_index = 0;
		int		nr_found;

		if (!pag->pag_ici_init)
			continue;
		do {
			nr_found = radix_tree_gang_lookup(&pag->pag_ici_root,
						(void**)&ip, first_index, 1);
			if (!nr_found)
				break;
			/* update the index for the next lookup */
			first_index = XFS_INO_TO_AGINO(mp, ip->i_ino + 1);
			if (ip->i_delayed_blks) {
				total += ip->i_delayed_blks;
				icount++;
			}
		} while (nr_found);
	}
	kdb_printf("delayed blocks total: %d in %d inodes\n", total, icount);
}

static void
xfsidbg_xinodes_quiesce(xfs_mount_t *mp)
{
	int		i;

	kdb_printf("xfs_mount at 0x%p\n", mp);
	for (i = 0; i < mp->m_sb.sb_agcount; i++) {
		xfs_perag_t	*pag = &mp->m_perag[i];
		xfs_inode_t	*ip = NULL;
		int		first_index = 0;
		int		nr_found;

		if (!pag->pag_ici_init)
			continue;
		do {
			nr_found = radix_tree_gang_lookup(&pag->pag_ici_root,
						(void**)&ip, first_index, 1);
			if (!nr_found)
				break;
			/* update the index for the next lookup */
			first_index = XFS_INO_TO_AGINO(mp, ip->i_ino + 1);
			if (!(ip->i_flags & XFS_IQUIESCE)) {
				kdb_printf("ip 0x%p not quiesced\n", ip);
			}
		} while (nr_found);
	}
	kdb_printf("\nEnd of Inodes\n");
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
	static char *t_flags[] = {
		"CHKSUM_MISMATCH",	/* 0x01 */
		"ACTIVE_RECOVERY",	/* 0x02 */
		"RECOVERY_NEEDED",	/* 0x04 */
		"IO_ERROR",		/* 0x08 */
		NULL
	};

	kdb_printf("xlog at 0x%p\n", log);
	kdb_printf("&flush_wait: 0x%p  ICLOG: 0x%p  \n",
		&log->l_flush_wait, log->l_iclog);
	kdb_printf("&icloglock: 0x%p  tail_lsn: %s  last_sync_lsn: %s \n",
		&log->l_icloglock, xfs_fmtlsn(&log->l_tail_lsn),
		xfs_fmtlsn(&log->l_last_sync_lsn));
	kdb_printf("mp: 0x%p  xbuf: 0x%p  l_covered_state: %s \n",
		log->l_mp, log->l_xbuf,
			xfsidbg_get_cstate(log->l_covered_state));
	kdb_printf("flags: ");
	printflags(log->l_flags, t_flags,"log");
	kdb_printf("  logBBstart: %lld logsize: %d logBBsize: %d\n",
		(long long) log->l_logBBstart,
		log->l_logsize,log->l_logBBsize);
	kdb_printf("curr_cycle: %d  prev_cycle: %d  curr_block: %d  prev_block: %d\n",
	     log->l_curr_cycle, log->l_prev_cycle, log->l_curr_block,
	     log->l_prev_block);
	kdb_printf("iclog_bak: 0x%p  iclog_size: 0x%x (%d)  num iclogs: %d\n",
#ifdef DEBUG
		log->l_iclog_bak,
#else
		NULL,
#endif
		log->l_iclog_size, log->l_iclog_size, log->l_iclog_bufs);
	kdb_printf("l_iclog_hsize %d l_iclog_heads %d\n",
		log->l_iclog_hsize, log->l_iclog_heads);
	kdb_printf("l_sectbb_log %u l_sectbb_mask %u\n",
		log->l_sectbb_log, log->l_sectbb_mask);
	kdb_printf("&grant_lock: 0x%p  resHeadQ: 0x%p  wrHeadQ: 0x%p\n",
		&log->l_grant_lock, log->l_reserve_headq, log->l_write_headq);
	kdb_printf("GResCycle: %d  GResBytes: %d  GWrCycle: %d  GWrBytes: %d\n",
		log->l_grant_reserve_cycle, log->l_grant_reserve_bytes,
		log->l_grant_write_cycle, log->l_grant_write_bytes);
	qprintf("GResBlocks: %d GResRemain: %d  GWrBlocks: %d GWrRemain: %d\n",
		(int)BTOBBT(log->l_grant_reserve_bytes),
		log->l_grant_reserve_bytes % BBSIZE,
		(int)BTOBBT(log->l_grant_write_bytes),
		log->l_grant_write_bytes % BBSIZE);
#ifdef XFS_LOG_TRACE
	qprintf("grant_trace: use xlog value\n");
#endif
}	/* xfsidbg_xlog */

static void
xfsidbg_print_trans_type(unsigned int t_type)
{
	switch (t_type) {
	case XFS_TRANS_SETATTR_NOT_SIZE: kdb_printf("SETATTR_NOT_SIZE");break;
	case XFS_TRANS_SETATTR_SIZE:	kdb_printf("SETATTR_SIZE");	break;
	case XFS_TRANS_INACTIVE:	kdb_printf("INACTIVE");		break;
	case XFS_TRANS_CREATE:		kdb_printf("CREATE");		break;
	case XFS_TRANS_CREATE_TRUNC:	kdb_printf("CREATE_TRUNC");	break;
	case XFS_TRANS_TRUNCATE_FILE:	kdb_printf("TRUNCATE_FILE");	break;
	case XFS_TRANS_REMOVE:		kdb_printf("REMOVE");		break;
	case XFS_TRANS_LINK:		kdb_printf("LINK");		break;
	case XFS_TRANS_RENAME:		kdb_printf("RENAME");		break;
	case XFS_TRANS_MKDIR:		kdb_printf("MKDIR");		break;
	case XFS_TRANS_RMDIR:		kdb_printf("RMDIR");		break;
	case XFS_TRANS_SYMLINK:		kdb_printf("SYMLINK");		break;
	case XFS_TRANS_SET_DMATTRS:	kdb_printf("SET_DMATTRS");	break;
	case XFS_TRANS_GROWFS:		kdb_printf("GROWFS");		break;
	case XFS_TRANS_STRAT_WRITE:	kdb_printf("STRAT_WRITE");	break;
	case XFS_TRANS_DIOSTRAT:	kdb_printf("DIOSTRAT");		break;
	case XFS_TRANS_WRITE_SYNC:	kdb_printf("WRITE_SYNC");	break;
	case XFS_TRANS_WRITEID:		kdb_printf("WRITEID");		break;
	case XFS_TRANS_ADDAFORK:	kdb_printf("ADDAFORK");		break;
	case XFS_TRANS_ATTRINVAL:	kdb_printf("ATTRINVAL");	break;
	case XFS_TRANS_ATRUNCATE:	kdb_printf("ATRUNCATE");	break;
	case XFS_TRANS_ATTR_SET:	kdb_printf("ATTR_SET");		break;
	case XFS_TRANS_ATTR_RM:		kdb_printf("ATTR_RM");		break;
	case XFS_TRANS_ATTR_FLAG:	kdb_printf("ATTR_FLAG");	break;
	case XFS_TRANS_CLEAR_AGI_BUCKET:kdb_printf("CLEAR_AGI_BUCKET");	break;
	case XFS_TRANS_QM_SBCHANGE:	kdb_printf("QM_SBCHANGE");	break;
	case XFS_TRANS_QM_QUOTAOFF:	kdb_printf("QM_QUOTAOFF");	break;
	case XFS_TRANS_QM_DQALLOC:	kdb_printf("QM_DQALLOC");	break;
	case XFS_TRANS_QM_SETQLIM:	kdb_printf("QM_SETQLIM");	break;
	case XFS_TRANS_QM_DQCLUSTER:	kdb_printf("QM_DQCLUSTER");	break;
	case XFS_TRANS_QM_QINOCREATE:	kdb_printf("QM_QINOCREATE");	break;
	case XFS_TRANS_QM_QUOTAOFF_END:	kdb_printf("QM_QOFF_END");	break;
	case XFS_TRANS_SB_UNIT:		kdb_printf("SB_UNIT");		break;
	case XFS_TRANS_FSYNC_TS:	kdb_printf("FSYNC_TS");		break;
	case XFS_TRANS_GROWFSRT_ALLOC:	kdb_printf("GROWFSRT_ALLOC");	break;
	case XFS_TRANS_GROWFSRT_ZERO:	kdb_printf("GROWFSRT_ZERO");	break;
	case XFS_TRANS_GROWFSRT_FREE:	kdb_printf("GROWFSRT_FREE");	break;
  	case XFS_TRANS_SWAPEXT:		kdb_printf("SWAPEXT");		break;
	case XFS_TRANS_SB_COUNT:	kdb_printf("SB_COUNT");		break;
 	case XFS_TRANS_DUMMY1:		kdb_printf("DUMMY1");		break;
 	case XFS_TRANS_DUMMY2:		kdb_printf("DUMMY2");		break;
 	case XLOG_UNMOUNT_REC_TYPE:	kdb_printf("UNMOUNT");		break;
	default:			kdb_printf("unknown(0x%x)", t_type); break;
	}
}

#ifdef XFS_LOG_TRACE
/*
 * Print grant trace for a log.
 */
static void
xfsidbg_xlog_granttrace(xlog_t *log)
{
	ktrace_entry_t  *ktep;
	ktrace_snap_t   kts;
	ktrace_t	*kt;
	int		i = 0;
	unsigned long	cnts,t_ocnt, t_cnt;

	if (((__psint_t)log) == ((__psint_t)-1)) {
		qprintf("Usage: xl_grtr <log>\n");
		return;
	}
	if ((kt = log->l_grant_trace))
		qprintf("log->l_grant_trace 0x%p\n", kt);
	else {
		qprintf("log->l_grant_trace is empty!\n");
		return;
	}
	ktep = ktrace_first(kt, &kts);
	while (ktep != NULL) {
		/* split cnts into two parts: cnt and ocnt */
		cnts = (unsigned long)ktep->val[13];
		t_ocnt = 0xff & cnts;
		t_cnt =  cnts >> 8;

		qprintf("%d: %s [", i++, (char *)ktep->val[11]);
		xfsidbg_print_trans_type((unsigned long)ktep->val[12]);
		qprintf("]\n");
		qprintf("  t_ocnt = %lu, t_cnt = %lu, t_curr_res = %lu, "
			"t_unit_res = %lu\n",
			t_ocnt, t_cnt, (unsigned long)ktep->val[14],
			(unsigned long)ktep->val[15]);
		qprintf("  tic:0x%p resQ:0x%p wrQ:0x%p ",
			ktep->val[0], ktep->val[1], ktep->val[2]);
		qprintf("  GrResC:%ld GrResB:%ld GrWrC:%ld GrWrB:%ld \n",
			(long)ktep->val[3], (long)ktep->val[4],
			(long)ktep->val[5], (long)ktep->val[6]);
		qprintf("  HeadC:%ld HeadB:%ld TailC:%ld TailB:%ld\n",
			(long)ktep->val[7], (long)ktep->val[8],
			(long)ktep->val[9], (long)ktep->val[10]);
		ktep = ktrace_next(kt, &kts);
	}
}       /* xfsidbg_xlog_granttrace */
#endif

/*
 * Print out an XFS recovery transaction
 */
static void
xfsidbg_xlog_ritem(xlog_recover_item_t *item)
{
	int i = XLOG_MAX_REGIONS_IN_ITEM;

	kdb_printf("(xlog_recover_item 0x%p) ", item);
	kdb_printf("next: 0x%p prev: 0x%p type: %d cnt: %d ttl: %d\n",
		item->ri_next, item->ri_prev, ITEM_TYPE(item), item->ri_cnt,
		item->ri_total);
	for ( ; i > 0; i--) {
		if (!item->ri_buf[XLOG_MAX_REGIONS_IN_ITEM-i].i_addr)
			break;
		kdb_printf("a: 0x%p l: %d ",
			item->ri_buf[XLOG_MAX_REGIONS_IN_ITEM-i].i_addr,
			item->ri_buf[XLOG_MAX_REGIONS_IN_ITEM-i].i_len);
	}
	kdb_printf("\n");
}	/* xfsidbg_xlog_ritem */

/*
 * Print out an XFS recovery transaction
 */
static void
xfsidbg_xlog_rtrans(xlog_recover_t *trans)
{
	xlog_recover_item_t *rip, *first_rip;

	kdb_printf("(xlog_recover 0x%p) ", trans);
	kdb_printf("tid: %x type: %d items: %d ttid: 0x%x  ",
		trans->r_log_tid, trans->r_theader.th_type,
		trans->r_theader.th_num_items, trans->r_theader.th_tid);
	kdb_printf("itemq: 0x%p\n", trans->r_itemq);
	if (trans->r_itemq) {
		rip = first_rip = trans->r_itemq;
		do {
			kdb_printf("(recovery item: 0x%p) ", rip);
			kdb_printf("type: %d cnt: %d total: %d\n",
				ITEM_TYPE(rip), rip->ri_cnt, rip->ri_total);
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
		kdb_printf("\tINODE BUF <blkno=0x%Lx, len=0x%x>\n",
			buf_f->blf_blkno, buf_f->blf_len);
	} else if (buf_f->blf_flags &
		  (XFS_BLI_UDQUOT_BUF|XFS_BLI_PDQUOT_BUF|XFS_BLI_GDQUOT_BUF)) {
		kdb_printf("\tDQUOT BUF <blkno=0x%Lx, len=0x%x>\n",
			buf_f->blf_blkno, buf_f->blf_len);
	} else {
		data_map = buf_f->blf_data_map;
		map_size = buf_f->blf_map_size;
		kdb_printf("\tREG BUF <blkno=0x%Lx, len=0x%x map 0x%p size %d>\n",
			buf_f->blf_blkno, buf_f->blf_len, data_map, map_size);
		bit = 0;
		i = 0;  /* 0 is the buf format structure */
		while (1) {
			bit = xfs_next_bit(data_map, map_size, bit);
			if (bit == -1)
				break;
			nbits = xfs_contig_bits(data_map, map_size, bit);
			size = ((uint)bit << XFS_BLI_SHIFT)+(nbits<<XFS_BLI_SHIFT);
			kdb_printf("\t\tlogbuf.i_addr 0x%p, size 0x%x\n",
				item->ri_buf[i].i_addr, size);
			kdb_printf("\t\t\t\"");
			for (j=0; j<8 && j<size; j++) {
				kdb_printf("%02x", ((char *)item->ri_buf[i].i_addr)[j]);
			}
			kdb_printf("...\"\n");
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

	kdb_printf("(Recovering Xact 0x%p) ", trans);
	kdb_printf("tid: %x type: %d nitems: %d ttid: 0x%x  ",
		trans->r_log_tid, trans->r_theader.th_type,
		trans->r_theader.th_num_items, trans->r_theader.th_tid);
	kdb_printf("itemq: 0x%p\n", trans->r_itemq);
	if (trans->r_itemq) {
		item = first_rip = trans->r_itemq;
		do {
			/*
			   kdb_printf("(recovery item: 0x%x) ", item);
			   kdb_printf("type: %d cnt: %d total: %d\n",
				   item->ri_type, item->ri_cnt, item->ri_total);
				   */
			if (ITEM_TYPE(item) == XFS_LI_BUF) {
				kdb_printf("BUF:");
				xfsidbg_xlog_buf_logitem(item);
			} else if (ITEM_TYPE(item) == XFS_LI_INODE) {
				kdb_printf("INODE:\n");
			} else if (ITEM_TYPE(item) == XFS_LI_EFI) {
				kdb_printf("EFI:\n");
			} else if (ITEM_TYPE(item) == XFS_LI_EFD) {
				kdb_printf("EFD:\n");
			} else if (ITEM_TYPE(item) == XFS_LI_DQUOT) {
				kdb_printf("DQUOT:\n");
			} else if ((ITEM_TYPE(item) == XFS_LI_QUOTAOFF)) {
				kdb_printf("QUOTAOFF:\n");
			} else {
				kdb_printf("UNKNOWN LOGITEM 0x%x\n", ITEM_TYPE(item));
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
		NULL
	};

	kdb_printf("xlog_ticket at 0x%p\n", tic);
	kdb_printf("next: 0x%p  prev: 0x%p  tid: 0x%x  \n",
		tic->t_next, tic->t_prev, tic->t_tid);
	kdb_printf("curr_res: %d  unit_res: %d  ocnt: %d  cnt: %d\n",
		tic->t_curr_res, tic->t_unit_res, (int)tic->t_ocnt,
		(int)tic->t_cnt);
	kdb_printf("clientid: %c  \n", tic->t_clientid);
	printflags(tic->t_flags, t_flags,"ticket");
	kdb_printf("\n");
	qprintf("trans type: ");
	xfsidbg_print_trans_type(tic->t_trans_type);
	qprintf("\n");
}	/* xfsidbg_xlog_tic */

static char *
xfsidbg_item_type_str(xfs_log_item_t *lip)
{
	static char *lid_type[] = {
		"efi",		/* 0 */
		"efd",		/* 1 */
		"iunlink",	/* 2 */
		"6-1-inode",	/* 3 */
		"6-1-buf",	/* 4 */
		"inode",	/* 5 */
		"buf",		/* 6 */
		"dquot",	/* 7 */
		"quotaoff",	/* 8 */
		NULL
		};
	return (lip->li_type < XFS_LI_EFI || lip->li_type > XFS_LI_QUOTAOFF) ? "???"
	       : lid_type[lip->li_type - XFS_LI_EFI];
}

/*
 * Print out a single log item.
 */
static void
xfsidbg_xlogitem(xfs_log_item_t *lip)
{
	xfs_log_item_t	*bio_lip;
	static char *li_flags[] = {
		"in ail",	/* 0x1 */
		NULL
		};

	kdb_printf("type %s mountp 0x%p flags ",
		xfsidbg_item_type_str(lip), lip->li_mountp);
	printflags((uint)(lip->li_flags), li_flags,"log");
	kdb_printf("\n");
	kdb_printf("ail forw 0x%p ail back 0x%p lsn %s\ndesc %p ops 0x%p",
		lip->li_ail.next, lip->li_ail.prev,
		xfs_fmtlsn(&(lip->li_lsn)), lip->li_desc, lip->li_ops);
	kdb_printf(" iodonefunc &0x%p\n", lip->li_cb);
	if (lip->li_type == XFS_LI_BUF) {
		bio_lip = lip->li_bio_list;
		if (bio_lip != NULL) {
			kdb_printf("iodone list:\n");
		}
		while (bio_lip != NULL) {
			kdb_printf("item 0x%p func 0x%p\n",
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
		kdb_printf("Unknown item type %d\n", lip->li_type);
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
	static char *li_flags[] = {
		"in ail",	/* 0x1 */
		NULL
		};
	int count = 0;

	if (list_empty(&mp->m_ail->xa_ail)) {
		kdb_printf("AIL is empty\n");
		return;
	}
	kdb_printf("AIL for mp 0x%p, oldest first\n", mp);
	list_for_each_entry(lip, &mp->m_ail->xa_ail, li_ail) {
		kdb_printf("[%d] type %s ", count, xfsidbg_item_type_str(lip));
		printflags((uint)(lip->li_flags), li_flags, "flags:");
		kdb_printf("  lsn %s\n   ", xfs_fmtlsn(&(lip->li_lsn)));
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
			kdb_printf("Unknown item type %d\n", lip->li_type);
			break;
		}
		count++;
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
		"DMAPI",	/* 0x0004 */
		"WAS_CLEAN",	/* 0x0008 */
		"FSSHUTDOWN",	/* 0x0010 */
		"NOATIME",	/* 0x0020 */
		"RETERR",	/* 0x0040 */
		"NOALIGN",	/* 0x0080 */
		"ATTR2",	/* 0x0100 */
		"GRPID",	/* 0x0200 */
		"NORECOVERY",	/* 0x0400 */
		"SHARED",	/* 0x0800 */
		"DFLT_IOSIZE",	/* 0x1000 */
		"OSYNCISOSYNC",	/* 0x2000 */
		"32BITINODES",	/* 0x4000 */
		"SMALL_INUMS",	/* 0x8000 */
		"NOUUID",	/* 0x10000 */
		"BARRIER",	/* 0x20000 */
		"IKEEP",	/* 0x40000 */
		"SWALLOC",	/* 0x80000 */
		"RDONLY", 	/* 0x100000 */
		"DIRSYNC",	/* 0x200000 */
		"COMPAT_IOSIZE",/* 0x400000 */
		NULL
	};

	static char *quota_flags[] = {
		"UQ",		/* 0x0001 */
		"UQE",		/* 0x0002 */
		"UQCHKD",	/* 0x0004 */
		"PQ",		/* 0x0008 */
		"GQE",		/* 0x0010 */
		"GQCHKD",	/* 0x0020 */
		"GQ",		/* 0x0040 */
		"UQACTV",	/* 0x0080 */
		"GQACTV",	/* 0x0100 */
		"QMAYBE",	/* 0x0200 */
		NULL
	};

	kdb_printf("xfs_mount at 0x%p\n", mp);
	kdb_printf("tid 0x%x ail 0x%p &sb 0x%p\n",
		mp->m_tid, mp->m_ail, &mp->m_sb);
	kdb_printf("sb_lock 0x%p sb_bp 0x%p dev 0x%x logdev 0x%x rtdev 0x%x\n",
		&mp->m_sb_lock, mp->m_sb_bp,
		mp->m_ddev_targp ? mp->m_ddev_targp->bt_dev : 0,
		mp->m_logdev_targp ? mp->m_logdev_targp->bt_dev : 0,
		mp->m_rtdev_targp ? mp->m_rtdev_targp->bt_dev : 0);
	kdb_printf("bsize %d agfrotor %d xfs_rotorstep %d agirotor %d\n",
		mp->m_bsize, mp->m_agfrotor, xfs_rotorstep, mp->m_agirotor);
	kdb_printf("ireclaims 0x%x\n", mp->m_ireclaims);
	kdb_printf("readio_log 0x%x readio_blocks 0x%x ",
		mp->m_readio_log, mp->m_readio_blocks);
	kdb_printf("writeio_log 0x%x writeio_blocks 0x%x\n",
		mp->m_writeio_log, mp->m_writeio_blocks);
	kdb_printf("logbufs %d logbsize %d LOG 0x%p\n", mp->m_logbufs,
		mp->m_logbsize, mp->m_log);
	kdb_printf("rsumlevels 0x%x rsumsize 0x%x rbmip 0x%p rsumip 0x%p\n",
		mp->m_rsumlevels, mp->m_rsumsize, mp->m_rbmip, mp->m_rsumip);
	kdb_printf("rootip 0x%p\n", mp->m_rootip);
	kdb_printf("blkbit_log %d blkbb_log %d agno_log %d\n",
		mp->m_blkbit_log, mp->m_blkbb_log, mp->m_agno_log);
	kdb_printf("agino_log %d inode cluster size %d\n",
		mp->m_agino_log, mp->m_inode_cluster_size);
	kdb_printf("blockmask 0x%x blockwsize 0x%x blockwmask 0x%x\n",
		mp->m_blockmask, mp->m_blockwsize, mp->m_blockwmask);
	kdb_printf("alloc_mxr[lf,nd] %d %d alloc_mnr[lf,nd] %d %d\n",
		mp->m_alloc_mxr[0], mp->m_alloc_mxr[1],
		mp->m_alloc_mnr[0], mp->m_alloc_mnr[1]);
	kdb_printf("bmap_dmxr[lfnr,ndnr] %d %d bmap_dmnr[lfnr,ndnr] %d %d\n",
		mp->m_bmap_dmxr[0], mp->m_bmap_dmxr[1],
		mp->m_bmap_dmnr[0], mp->m_bmap_dmnr[1]);
	kdb_printf("inobt_mxr[lf,nd] %d %d inobt_mnr[lf,nd] %d %d\n",
		mp->m_inobt_mxr[0], mp->m_inobt_mxr[1],
		mp->m_inobt_mnr[0], mp->m_inobt_mnr[1]);
	kdb_printf("ag_maxlevels %d bm_maxlevels[d,a] %d %d in_maxlevels %d\n",
		mp->m_ag_maxlevels, mp->m_bm_maxlevels[0],
		mp->m_bm_maxlevels[1], mp->m_in_maxlevels);
	kdb_printf("perag 0x%p &peraglock 0x%p &growlock 0x%p\n",
		mp->m_perag, &mp->m_peraglock, &mp->m_growlock);
	printflags(mp->m_flags, xmount_flags,"flags");
	kdb_printf("ialloc_inos %d ialloc_blks %d litino %d\n",
		mp->m_ialloc_inos, mp->m_ialloc_blks, mp->m_litino);
	kdb_printf("dir_node_ents %u attr_node_ents %u\n",
		mp->m_dir_node_ents, mp->m_attr_node_ents);
	kdb_printf("attroffset %d maxicount %Ld inoalign_mask %d\n",
		mp->m_attroffset, mp->m_maxicount, mp->m_inoalign_mask);
	kdb_printf("resblks %Ld resblks_avail %Ld\n", mp->m_resblks,
		mp->m_resblks_avail);
#if XFS_BIG_INUMS
	kdb_printf(" inoadd %llx\n", (unsigned long long) mp->m_inoadd);
#else
	kdb_printf("\n");
#endif
	if (mp->m_quotainfo)
		kdb_printf("quotainfo 0x%p (uqip = 0x%p, gqip = 0x%p)\n",
			mp->m_quotainfo,
			mp->m_quotainfo->qi_uquotaip,
			mp->m_quotainfo->qi_gquotaip);
	else
		kdb_printf("quotainfo NULL\n");
	printflags(mp->m_qflags, quota_flags,"quotaflags");
	kdb_printf("\n");
	kdb_printf("dalign %d swidth %d sinoalign %d attr_magicpct %d dir_magicpct %d\n",
		mp->m_dalign, mp->m_swidth, mp->m_sinoalign,
		mp->m_attr_magicpct, mp->m_dir_magicpct);
	kdb_printf("mk_sharedro %d inode_quiesce %d sectbb_log %d\n",
		mp->m_mk_sharedro, mp->m_inode_quiesce, mp->m_sectbb_log);
	kdb_printf("dirblkfsbs %d\n", mp->m_dirblkfsbs);
	kdb_printf("dirblksize %d dirdatablk 0x%Lx dirleafblk 0x%Lx dirfreeblk 0x%Lx\n",
		mp->m_dirblksize,
		(xfs_dfiloff_t)mp->m_dirdatablk,
		(xfs_dfiloff_t)mp->m_dirleafblk,
		(xfs_dfiloff_t)mp->m_dirfreeblk);
	if (mp->m_fsname != NULL)
		kdb_printf("mountpoint \"%s\"\n", mp->m_fsname);
	else
		kdb_printf("No name!!!\n");

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
		"quiesce",	/* XFS_IQUIESCE */
		"reclaim",	/* XFS_IRECLAIM */
		"stale",	/* XFS_ISTALE */
		"modified",	/* XFS_IMODIFIED */
		"truncated",	/* XFS_ITRUNCATED */
		NULL
	};

	kdb_printf("mount 0x%p vnode 0x%p \n", ip->i_mount, VFS_I(ip));
	kdb_printf("dev %x ino %s\n",
		ip->i_mount->m_ddev_targp->bt_dev,
		xfs_fmtino(ip->i_ino, ip->i_mount));
	kdb_printf("blkno 0x%llx len 0x%x boffset 0x%x\n",
		(long long) ip->i_imap.im_blkno,
		ip->i_imap.im_len,
		ip->i_imap.im_boffset);
	kdb_printf("transp 0x%p &itemp 0x%p\n",
		ip->i_transp,
		ip->i_itemp);
	kdb_printf("&lock 0x%p &iolock 0x%p ",
		&ip->i_lock,
		&ip->i_iolock);
	kdb_printf("&flush 0x%p (%d) pincount 0x%x\n",
		&ip->i_flush, ip->i_flush.done,
		xfs_ipincount(ip));
	kdb_printf("udquotp 0x%p gdquotp 0x%p\n",
		ip->i_udquot, ip->i_gdquot);
	kdb_printf("new_size %Ld\n", ip->i_new_size);
	printflags((int)ip->i_flags, tab_flags, "flags");
	kdb_printf("\n");
	kdb_printf("update_core %d update size %d\n",
		(int)(ip->i_update_core), (int) ip->i_update_size);
	kdb_printf("delayed blks %d",
		ip->i_delayed_blks);
	kdb_printf("size %lld\n",
		ip->i_size);

#ifdef	XFS_INODE_TRACE
	qprintf(" trace 0x%p\n", ip->i_trace);
#endif
#ifdef XFS_BMAP_TRACE
	qprintf(" bmap_trace 0x%p\n", ip->i_xtrace);
#endif
#ifdef XFS_BTREE_TRACE
	qprintf(" bmbt trace 0x%p\n", ip->i_btrace);
#endif
#ifdef XFS_RW_TRACE
	qprintf(" rw trace 0x%p\n", ip->i_rwtrace);
#endif
#ifdef XFS_ILOCK_TRACE
	qprintf(" ilock trace 0x%p\n", ip->i_lock_trace);
#endif
#ifdef XFS_DIR2_TRACE
	qprintf(" dir trace 0x%p\n", ip->i_dir_trace);
#endif
	kdb_printf("\n");
	xfs_xnode_fork("data", &ip->i_df);
	xfs_xnode_fork("attr", ip->i_afp);
	kdb_printf("\n");
	xfs_prdinode_incore(&ip->i_d);
}

/*
 * Print xfs per-ag data structures for filesystem.
 */
static void
xfsidbg_xperag(xfs_mount_t *mp)
{
	xfs_agnumber_t	agno;
	xfs_perag_t	*pag;
	int		busy;

	pag = mp->m_perag;
	for (agno = 0; agno < mp->m_sb.sb_agcount; agno++, pag++) {
		kdb_printf("ag %d f_init %d i_init %d\n",
			agno, pag->pagf_init, pag->pagi_init);
		if (pag->pagf_init)
			kdb_printf(
	"    f_levels[b,c] %d,%d f_flcount %d f_freeblks %d f_longest %d\n"
	"    f__metadata %d\n",
				pag->pagf_levels[XFS_BTNUM_BNOi],
				pag->pagf_levels[XFS_BTNUM_CNTi],
				pag->pagf_flcount, pag->pagf_freeblks,
				pag->pagf_longest, pag->pagf_metadata);
		if (pag->pagi_init)
			kdb_printf("    i_freecount %d i_inodeok %d\n",
				pag->pagi_freecount, pag->pagi_inodeok);
		if (pag->pagf_init) {
			for (busy = 0; busy < XFS_PAGB_NUM_SLOTS; busy++) {
				if (pag->pagb_list[busy].busy_length != 0) {
					kdb_printf(
		"	 %04d: start %d length %d tp 0x%p\n",
					    busy,
					    pag->pagb_list[busy].busy_start,
					    pag->pagb_list[busy].busy_length,
					    pag->pagb_list[busy].busy_tp);
				}
			}
		}
	}
}

#ifdef CONFIG_XFS_QUOTA
static void
xfsidbg_xqm(void)
{
	if (xfs_Gqm == NULL) {
		kdb_printf("NULL XQM!!\n");
		return;
	}

	kdb_printf("usrhtab 0x%p grphtab 0x%p  ndqfree 0x%x  hashmask 0x%x\n",
		xfs_Gqm->qm_usr_dqhtable,
		xfs_Gqm->qm_grp_dqhtable,
		xfs_Gqm->qm_dqfreelist.qh_nelems,
		xfs_Gqm->qm_dqhashmask);
	kdb_printf("&freelist 0x%p, totaldquots 0x%x nrefs 0x%x\n",
		&xfs_Gqm->qm_dqfreelist,
		atomic_read(&xfs_Gqm->qm_totaldquots),
		xfs_Gqm->qm_nrefs);
}
#endif

static void
xfsidbg_xqm_diskdq(xfs_disk_dquot_t *d)
{
	kdb_printf("magic 0x%x\tversion 0x%x\tID 0x%x (%d)\t\n",
		be16_to_cpu(d->d_magic), d->d_version,
		be32_to_cpu(d->d_id), be32_to_cpu(d->d_id));
	kdb_printf("bhard 0x%llx\tbsoft 0x%llx\tihard 0x%llx\tisoft 0x%llx\n",
		(unsigned long long)be64_to_cpu(d->d_blk_hardlimit),
		(unsigned long long)be64_to_cpu(d->d_blk_softlimit),
		(unsigned long long)be64_to_cpu(d->d_ino_hardlimit),
		(unsigned long long)be64_to_cpu(d->d_ino_softlimit));
	kdb_printf("bcount 0x%llx icount 0x%llx\n",
		(unsigned long long)be64_to_cpu(d->d_bcount),
		(unsigned long long)be64_to_cpu(d->d_icount));
	kdb_printf("btimer 0x%x itimer 0x%x \n",
		be32_to_cpu(d->d_btimer),
		be32_to_cpu(d->d_itimer));
}

static char *xdq_flags[] = {
	"USER",		/* XFS_DQ_USER */
	"PROJ",		/* XFS_DQ_PROJ */
	"GROUP",	/* XFS_DQ_GROUP */
	"FLKD",		/* XFS_DQ_FLOCKED */
	"DIRTY",	/* XFS_DQ_DIRTY */
	"WANT",		/* XFS_DQ_WANT */
	"INACT",	/* XFS_DQ_INACTIVE */
	"MARKER",	/* XFS_DQ_MARKER */
	NULL
};

static void
xfsidbg_xqm_dquot(xfs_dquot_t *dqp)
{
	kdb_printf("mount 0x%p hash 0x%p gdquotp 0x%p HL_next 0x%p HL_prevp 0x%p\n",
		dqp->q_mount,
		dqp->q_hash,
		dqp->q_gdquot,
		dqp->HL_NEXT,
		dqp->HL_PREVP);
	kdb_printf("MPL_next 0x%p MPL_prevp 0x%p FL_next 0x%p FL_prev 0x%p\n",
		dqp->MPL_NEXT,
		dqp->MPL_PREVP,
		dqp->dq_flnext,
		dqp->dq_flprev);

	kdb_printf("nrefs 0x%x blkno 0x%llx boffset 0x%x ", dqp->q_nrefs,
		(unsigned long long)dqp->q_blkno, dqp->q_bufoffset);
	printflags(dqp->dq_flags, xdq_flags, "flags:");
	kdb_printf("res_bcount %llu res_icount %llu res_rtbcount %llu\n",
		(unsigned long long)dqp->q_res_bcount,
		(unsigned long long)dqp->q_res_icount,
		(unsigned long long)dqp->q_res_rtbcount);
	kdb_printf("qlock 0x%p  &q_flush 0x%p (%d) pincount 0x%x\n",
		&dqp->q_qlock, &dqp->q_flush,
		dqp->q_flush.done, atomic_read(&dqp->q_pincount));
#ifdef XFS_DQUOT_TRACE
	qprintf("dqtrace 0x%p\n", dqp->q_trace);
#endif
	kdb_printf("disk-dquot 0x%p\n", &dqp->q_core);
	xfsidbg_xqm_diskdq(&dqp->q_core);

}


#define XQMIDBG_LIST_PRINT(l, NXT) \
{ \
	  xfs_dquot_t	*dqp;\
	  int i = 0; \
	  kdb_printf("[#%d dquots]\n", (int) (l)->qh_nelems); \
	  for (dqp = (l)->qh_next; dqp != NULL; dqp = dqp->NXT) {\
	   kdb_printf( \
	      "\t%d. [0x%p] \"%d (%s)\"\t blks = %d, inos = %d refs = %d\n", \
			 ++i, dqp, (int) be32_to_cpu(dqp->q_core.d_id), \
			 DQFLAGTO_TYPESTR(dqp),      \
			 (int) be64_to_cpu(dqp->q_core.d_bcount), \
			 (int) be64_to_cpu(dqp->q_core.d_icount), \
			 (int) dqp->q_nrefs); }\
	  kdb_printf("\n"); \
}

static void
xfsidbg_xqm_dqattached_inos(xfs_mount_t	*mp)
{
	int		i, n = 0;

	kdb_printf("xfs_mount at 0x%p\n", mp);
	for (i = 0; i < mp->m_sb.sb_agcount; i++) {
		xfs_perag_t	*pag = &mp->m_perag[i];
		xfs_inode_t	*ip = NULL;
		int		first_index = 0;
		int		nr_found;

		if (!pag->pag_ici_init)
			continue;
		do {
			nr_found = radix_tree_gang_lookup(&pag->pag_ici_root,
						(void**)&ip, first_index, 1);
			if (!nr_found)
				break;
			/* update the index for the next lookup */
			first_index = XFS_INO_TO_AGINO(mp, ip->i_ino + 1);
			if (ip->i_udquot || ip->i_gdquot) {
				n++;
				kdb_printf("inode = 0x%p, ino %d: udq 0x%p, gdq 0x%p\n",
					ip, (int)ip->i_ino, ip->i_udquot, ip->i_gdquot);
			}
			kdb_printf("\n");
			xfsidbg_xnode(ip);
		} while (nr_found);
	}
	kdb_printf("\nNumber of inodes with dquots attached: %d\n", n);
}

#ifdef CONFIG_XFS_QUOTA
static void
xfsidbg_xqm_freelist_print(xfs_frlist_t *qlist, char *title)
{
	xfs_dquot_t *dq;
	int i = 0;
	kdb_printf("%s (#%d)\n", title, (int) qlist->qh_nelems);
	FOREACH_DQUOT_IN_FREELIST(dq, qlist) {
		kdb_printf("\t%d.\t\"%d (%s:0x%p)\"\t bcnt = %d, icnt = %d "
		       "refs = %d\n",
		       ++i, (int) be32_to_cpu(dq->q_core.d_id),
		       DQFLAGTO_TYPESTR(dq), dq,
		       (int) be64_to_cpu(dq->q_core.d_bcount),
		       (int) be64_to_cpu(dq->q_core.d_icount),
		       (int) dq->q_nrefs);
	}
}

static void
xfsidbg_xqm_freelist(void)
{
	if (xfs_Gqm) {
		xfsidbg_xqm_freelist_print(&(xfs_Gqm->qm_dqfreelist), "Freelist");
	} else
		kdb_printf("NULL XQM!!\n");
}

static void
xfsidbg_xqm_htab(void)
{
	int		i;
	xfs_dqhash_t	*h;

	if (xfs_Gqm == NULL) {
		kdb_printf("NULL XQM!!\n");
		return;
	}
	for (i = 0; i <= xfs_Gqm->qm_dqhashmask; i++) {
		h = &xfs_Gqm->qm_usr_dqhtable[i];
		if (h->qh_next) {
			kdb_printf("USR %d: ", i);
			XQMIDBG_LIST_PRINT(h, HL_NEXT);
		}
	}
	for (i = 0; i <= xfs_Gqm->qm_dqhashmask; i++) {
		h = &xfs_Gqm->qm_grp_dqhtable[i];
		if (h->qh_next) {
			kdb_printf("GRP/PRJ %d: ", i);
			XQMIDBG_LIST_PRINT(h, HL_NEXT);
		}
	}
}
#endif

#ifdef XFS_DQUOT_TRACE
static int
xfsidbg_xqm_pr_dqentry(ktrace_entry_t *ktep)
{
	if ((__psint_t)ktep->val[0] == 0)
		return 0;
	switch ((__psint_t)ktep->val[0]) {
	      case DQUOT_KTRACE_ENTRY:
		qprintf("[%ld] %s\t",
			(long)ktep->val[12], /* pid */
			(char *)ktep->val[1]);
		printflags((__psint_t)ktep->val[3], xdq_flags,"flgs ");
		qprintf("\nnrefs = %u, "
			"flags = 0x%x, "
			"id = %d, "
			"res_bc = 0x%x\n"
			"bcnt = 0x%x [0x%x | 0x%x], "
			"icnt = 0x%x [0x%x | 0x%x]\n"
			"@ %ld\n",
			(unsigned int)(long)ktep->val[2], /* nrefs */
			(unsigned int)(long)ktep->val[3], /* flags */
			(unsigned int)(long)ktep->val[11], /* ID */
			(unsigned int)(long)ktep->val[4], /* res_bc */
			(unsigned int)(long)ktep->val[5], /* bcnt */
			(unsigned int)(long)ktep->val[8], /* bsoft */
			(unsigned int)(long)ktep->val[7], /* bhard */
			(unsigned int)(long)ktep->val[6], /* icnt */
			(unsigned int)(long)ktep->val[10], /* isoft */
			(unsigned int)(long)ktep->val[9], /* ihard */
			(long) ktep->val[13] /* time */
			);
		break;

	      default:
		qprintf("unknown dqtrace record\n");
		break;
	}
	return (1);
}

void
xfsidbg_xqm_dqtrace(xfs_dquot_t *dqp)
{
	ktrace_entry_t	*ktep;
	ktrace_snap_t	kts;

	if (dqp->q_trace == NULL) {
		qprintf("The xfs dquot trace buffer is not initialized\n");
		return;
	}
	qprintf("xdqtrace dquot 0x%p\n", dqp);

	ktep = ktrace_first(dqp->q_trace, &kts);
	while (ktep != NULL) {
		if (xfsidbg_xqm_pr_dqentry(ktep))
			qprintf("---------------------------------\n");
		ktep = ktrace_next(dqp->q_trace, &kts);
	}
}
#endif

static void
xfsidbg_xqm_mplist(xfs_mount_t *mp)
{
	if (mp->m_quotainfo == NULL) {
		kdb_printf("NULL quotainfo\n");
		return;
	}

	XQMIDBG_LIST_PRINT(&(mp->m_quotainfo->qi_dqlist), MPL_NEXT);

}


static void
xfsidbg_xqm_qinfo(xfs_mount_t *mp)
{
	if (mp == NULL || mp->m_quotainfo == NULL) {
		kdb_printf("NULL quotainfo\n");
		return;
	}

	kdb_printf("uqip 0x%p, gqip 0x%p, &dqlist 0x%p\n",
		mp->m_quotainfo->qi_uquotaip,
		mp->m_quotainfo->qi_gquotaip,
		&mp->m_quotainfo->qi_dqlist);

	kdb_printf("btmlimit 0x%x, itmlimit 0x%x, RTbtmlim 0x%x\n",
		(int)mp->m_quotainfo->qi_btimelimit,
		(int)mp->m_quotainfo->qi_itimelimit,
		(int)mp->m_quotainfo->qi_rtbtimelimit);

	kdb_printf("bwarnlim 0x%x, iwarnlim 0x%x, RTbwarnlim 0x%x\n",
		(int)mp->m_quotainfo->qi_bwarnlimit,
		(int)mp->m_quotainfo->qi_iwarnlimit,
		(int)mp->m_quotainfo->qi_rtbwarnlimit);

	kdb_printf("nreclaims %d, &qofflock 0x%p, chunklen 0x%x, dqperchunk 0x%x\n",
		(int)mp->m_quotainfo->qi_dqreclaims,
		&mp->m_quotainfo->qi_quotaofflock,
		(int)mp->m_quotainfo->qi_dqchunklen,
		(int)mp->m_quotainfo->qi_dqperchunk);
}

static void
xfsidbg_xqm_tpdqinfo(xfs_trans_t *tp)
{
	xfs_dqtrx_t	*qa, *q;
	int		i,j;

	kdb_printf("dqinfo 0x%p\n", tp->t_dqinfo);
	if (! tp->t_dqinfo)
		return;
	kdb_printf("USR: \n");
	qa = tp->t_dqinfo->dqa_usrdquots;
	for (j = 0; j < 2; j++) {
		for (i = 0; i < XFS_QM_TRANS_MAXDQS; i++) {
			if (qa[i].qt_dquot == NULL)
				break;
			q = &qa[i];
			kdb_printf(
  "\"%d\"[0x%p]: bres %d, bres-used %d, bdelta %d, del-delta %d, icnt-delta %d\n",
				(int) be32_to_cpu(q->qt_dquot->q_core.d_id),
				q->qt_dquot,
				(int) q->qt_blk_res,
				(int) q->qt_blk_res_used,
				(int) q->qt_bcount_delta,
				(int) q->qt_delbcnt_delta,
				(int) q->qt_icount_delta);
		}
		if (j == 0) {
			qa = tp->t_dqinfo->dqa_grpdquots;
			kdb_printf("GRP/PRJ: \n");
		}
	}
}

/*
 * Print xfs superblock.
 */
static void
xfsidbg_xsb(xfs_sb_t *sbp)
{
	kdb_printf("magicnum 0x%x blocksize 0x%x dblocks %Ld rblocks %Ld\n",
		sbp->sb_magicnum, sbp->sb_blocksize,
		sbp->sb_dblocks, sbp->sb_rblocks);
	kdb_printf("rextents %Ld uuid %s logstart %s\n",
		sbp->sb_rextents, xfs_fmtuuid(&sbp->sb_uuid),
		xfs_fmtfsblock(sbp->sb_logstart, NULL));
	kdb_printf("rootino %s ", xfs_fmtino(sbp->sb_rootino, NULL));
	kdb_printf("rbmino %s ", xfs_fmtino(sbp->sb_rbmino, NULL));
	kdb_printf("rsumino %s\n", xfs_fmtino(sbp->sb_rsumino, NULL));
	kdb_printf("rextsize 0x%x agblocks 0x%x agcount 0x%x rbmblocks 0x%x\n",
		sbp->sb_rextsize, sbp->sb_agblocks, sbp->sb_agcount,
		sbp->sb_rbmblocks);
	kdb_printf("logblocks 0x%x versionnum 0x%x sectsize 0x%x inodesize 0x%x\n",
		sbp->sb_logblocks, sbp->sb_versionnum, sbp->sb_sectsize,
		sbp->sb_inodesize);
	kdb_printf("inopblock 0x%x blocklog 0x%x sectlog 0x%x inodelog 0x%x\n",
		sbp->sb_inopblock, sbp->sb_blocklog, sbp->sb_sectlog,
		sbp->sb_inodelog);
	kdb_printf("inopblog %d agblklog %d rextslog %d inprogress %d imax_pct %d\n",
		sbp->sb_inopblog, sbp->sb_agblklog, sbp->sb_rextslog,
		sbp->sb_inprogress, sbp->sb_imax_pct);
	kdb_printf("icount %Lx ifree %Lx fdblocks %Lx frextents %Lx\n",
		sbp->sb_icount, sbp->sb_ifree,
		sbp->sb_fdblocks, sbp->sb_frextents);
	kdb_printf("uquotino %s ", xfs_fmtino(sbp->sb_uquotino, NULL));
	kdb_printf("gquotino %s ", xfs_fmtino(sbp->sb_gquotino, NULL));
	kdb_printf("qflags 0x%x flags 0x%x shared_vn %d inoaligmt %d\n",
		sbp->sb_qflags, sbp->sb_flags, sbp->sb_shared_vn,
		sbp->sb_inoalignmt);
	kdb_printf("unit %d width %d dirblklog %d\n",
		sbp->sb_unit, sbp->sb_width, sbp->sb_dirblklog);
	kdb_printf("log sunit %d\n", sbp->sb_logsunit);
}

static void
xfsidbg_xsb_convert(xfs_dsb_t *sbp)
{
	xfs_sb_t sb;

	xfs_sb_from_disk(&sb, sbp);

	kdb_printf("<converted>\n");
	xfsidbg_xsb(&sb);
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
	xfs_log_busy_chunk_t	*lbcp;
	int			i;
	int			chunk;
	static char *xtp_flags[] = {
		"dirty",	/* 0x1 */
		"sb_dirty",	/* 0x2 */
		"perm_log_res",	/* 0x4 */
		"sync",		/* 0x08 */
		"dq_dirty",     /* 0x10 */
		NULL
		};
	static char *lid_flags[] = {
		"dirty",	/* 0x1 */
		"pinned",	/* 0x2 */
		"sync unlock",	/* 0x4 */
		"buf stale",	/* 0x8 */
		NULL
		};

	kdb_printf("tp 0x%p type ", tp);
	xfsidbg_print_trans_type(tp->t_type);
	kdb_printf(" mount 0x%p callback 0x%p\n", tp->t_mountp, &tp->t_logcb);
	kdb_printf("flags ");
	printflags(tp->t_flags, xtp_flags, "xtp");
	kdb_printf("\n");
	kdb_printf("log res %d block res %d block res used %d\n",
		tp->t_log_res, tp->t_blk_res, tp->t_blk_res_used);
	kdb_printf("rt res %d rt res used %d\n", tp->t_rtx_res,
		tp->t_rtx_res_used);
	kdb_printf("ticket 0x%lx lsn %s commit_lsn %s\n",
		(unsigned long) tp->t_ticket,
		xfs_fmtlsn(&tp->t_lsn),
		xfs_fmtlsn(&tp->t_commit_lsn));
	kdb_printf("callback 0x%p callarg 0x%p\n",
		tp->t_callback, tp->t_callarg);
	kdb_printf("icount delta %lld ifree delta %lld\n",
		(long long)tp->t_icount_delta,
		(long long)tp->t_ifree_delta);
	kdb_printf("blocks delta %lld res blocks delta %lld\n",
		(long long)tp->t_fdblocks_delta,
		(long long)tp->t_res_fdblocks_delta);
	kdb_printf("rt delta %lld res rt delta %lld\n",
		(long long)(long long)tp->t_frextents_delta,
		(long long)tp->t_res_frextents_delta);
#ifdef DEBUG
	kdb_printf("ag freeblks delta %lld ag flist delta %lld ag btree delta %lld\n",
		(long long)tp->t_ag_freeblks_delta,
		(long long)tp->t_ag_flist_delta,
		(long long)tp->t_ag_btree_delta);
#endif
	kdb_printf("dblocks delta %lld agcount delta %lld imaxpct delta %lld\n",
		(long long)tp->t_dblocks_delta,
		(long long)tp->t_agcount_delta,
		(long long)tp->t_imaxpct_delta);
	kdb_printf("rextsize delta %lld rbmblocks delta %lld\n",
		(long long)tp->t_rextsize_delta,
		(long long)tp->t_rbmblocks_delta);
	kdb_printf("rblocks delta %lld rextents delta %lld rextslog delta %lld\n",
		(long long)tp->t_rblocks_delta,
		(long long)tp->t_rextents_delta,
		(long long)tp->t_rextslog_delta);
	kdb_printf("dqinfo 0x%p\n", tp->t_dqinfo);
	kdb_printf("log items:\n");
	licp = &tp->t_items;
	chunk = 0;
	while (licp != NULL) {
		if (xfs_lic_are_all_free(licp)) {
			licp = licp->lic_next;
			chunk++;
			continue;
		}
		for (i = 0; i < licp->lic_unused; i++) {
			if (xfs_lic_isfree(licp, i)) {
				continue;
			}

			lidp = xfs_lic_slot(licp, i);
			kdb_printf("\n");
			kdb_printf("chunk %d index %d item 0x%p size %d\n",
				chunk, i, lidp->lid_item, lidp->lid_size);
			kdb_printf("flags ");
			printflags(lidp->lid_flags, lid_flags,"lic");
			kdb_printf("\n");
			xfsidbg_xlogitem(lidp->lid_item);
		}
		chunk++;
		licp = licp->lic_next;
	}

	kdb_printf("log busy free %d, list:\n", tp->t_busy_free);
	lbcp = &tp->t_busy;
	chunk = 0;
	while (lbcp != NULL) {
		kdb_printf("Chunk %d at 0x%p next 0x%p free 0x%08x unused %d\n",
			chunk, lbcp, lbcp->lbc_next, lbcp->lbc_free,
			lbcp->lbc_unused);
		for (i = 0; i < XFS_LBC_NUM_SLOTS; i++) {
			kdb_printf("  %02d: ag %d idx %d\n",
				i,
				lbcp->lbc_busy[i].lbc_ag,
				lbcp->lbc_busy[i].lbc_idx);
		}
		lbcp = lbcp->lbc_next;
	}
}

static void
xfsidbg_xtrans_res(
	xfs_mount_t	*mp)
{
	xfs_trans_reservations_t	*xtrp;

	xtrp = &mp->m_reservations;
	kdb_printf("write: %d\ttruncate: %d\trename: %d\n",
		xtrp->tr_write, xtrp->tr_itruncate, xtrp->tr_rename);
	kdb_printf("link: %d\tremove: %d\tsymlink: %d\n",
		xtrp->tr_link, xtrp->tr_remove, xtrp->tr_symlink);
	kdb_printf("create: %d\tmkdir: %d\tifree: %d\n",
		xtrp->tr_create, xtrp->tr_mkdir, xtrp->tr_ifree);
	kdb_printf("ichange: %d\tgrowdata: %d\tswrite: %d\n",
		xtrp->tr_ichange, xtrp->tr_growdata, xtrp->tr_swrite);
	kdb_printf("addafork: %d\twriteid: %d\tattrinval: %d\n",
		xtrp->tr_addafork, xtrp->tr_writeid, xtrp->tr_attrinval);
	kdb_printf("attrset: %d\tattrrm: %d\tclearagi: %d\n",
		xtrp->tr_attrset, xtrp->tr_attrrm, xtrp->tr_clearagi);
	kdb_printf("growrtalloc: %d\tgrowrtzero: %d\tgrowrtfree: %d\n",
		xtrp->tr_growrtalloc, xtrp->tr_growrtzero, xtrp->tr_growrtfree);
}

module_init(xfsidbg_init)
module_exit(xfsidbg_exit)
