/**************************************************************************
 *									  *
 *	 	Copyright (C) 1994-1995 Silicon Graphics, Inc.		  *
 *									  *
 *  These coded	instructions, statements, and computer programs	 contain  *
 *  unpublished	 proprietary  information of Silicon Graphics, Inc., and  *
 *  are	protected by Federal copyright law.  They  may	not be disclosed  *
 *  to	third  parties	or copied or duplicated	in any form, in	whole or  *
 *  in part, without the prior written consent of Silicon Graphics, Inc.  *
 *									  *
 **************************************************************************/
#ident	"$Revision: 1.46 $"

#include <sys/param.h>
#include <sys/buf.h>
#include <sys/cred.h>
#include <sys/debug.h>
#include <sys/dirent.h>
#include <sys/idbg.h>
#include <sys/idbgentry.h>
#include <sys/ktrace.h>
#include <sys/sema.h>
#include <sys/systm.h>
#include <sys/vfs.h>
#include <sys/vnode.h>
#include <sys/pvnode.h>
#include <sys/kmem.h>
#include <sys/attributes.h>
#include <sys/uuid.h>
#include "xfs_types.h"
#include "xfs_inum.h"
#include "xfs_log.h"
#include "xfs_trans.h"
#include "xfs_sb.h"
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
#include "xfs_dinode.h"
#include "xfs_inode.h"
#include "xfs_da_btree.h"
#include "xfs_attr.h"
#include "xfs_attr_leaf.h"
#include "xfs_dir.h"
#include "xfs_dir_leaf.h"
#include "xfs_log_priv.h"
#include "xfs_log_recover.h"
#include "xfs_rw.h"
#include "xfs_bit.h"

/*
 * External functions & data not in header files.
 */
#ifdef DEBUG
extern ktrace_entry_t	*ktrace_first(ktrace_t *, ktrace_snap_t *);
extern int		ktrace_nentries(ktrace_t *);
extern ktrace_entry_t	*ktrace_next(ktrace_t *, ktrace_snap_t *);
extern ktrace_entry_t	*ktrace_skip(ktrace_t *, int, ktrace_snap_t *);
extern char		*tab_bflags[];
extern char		*tab_bflags2[];
#endif

extern struct vfsops	xfs_vfsops;
extern struct vnodeops	xfs_vnodeops;

/*
 * Command table functions.
 */
static void	xfsidbg_xagf(xfs_agf_t *);
static void	xfsidbg_xagi(xfs_agi_t *);
#ifdef DEBUG
static void	xfsidbg_xaildump(xfs_mount_t *);
static void	xfsidbg_xalatrace(int);
static void	xfsidbg_xalbtrace(xfs_agblock_t);
static void	xfsidbg_xalgtrace(xfs_agnumber_t );
#endif
static void	xfsidbg_xalloc(xfs_alloc_arg_t *);
#ifdef DEBUG
static void	xfsidbg_xalmtrace(xfs_mount_t *);
static void	xfsidbg_xalttrace(int);
#endif
static void 	xfsidbg_xarg(int);
static void	xfsidbg_xattrcontext(xfs_attr_list_context_t *);
static void	xfsidbg_xattrleaf(xfs_attr_leafblock_t *);
static void	xfsidbg_xattrsf(xfs_attr_shortform_t *);
#ifdef DEBUG
static void	xfsidbg_xattrtrace(int);
#endif
static void 	xfsidbg_xbirec(xfs_bmbt_irec_t *r);
#ifdef DEBUG
static void	xfsidbg_xblitrace(xfs_buf_log_item_t *);
#endif
static void	xfsidbg_xbmalla(xfs_bmalloca_t *);
#ifdef DEBUG
static void	xfsidbg_xbmatrace(int);
static void	xfsidbg_xbmitrace(xfs_inode_t *);
static void	xfsidbg_xbmstrace(xfs_inode_t *);
#endif
static void	xfsidbg_xbrec(xfs_bmbt_rec_32_t *);
static void	xfsidbg_xbroot(xfs_inode_t *);
static void	xfsidbg_xbroota(xfs_inode_t *);
static void	xfsidbg_xbtcur(xfs_btree_cur_t *);
static void	xfsidbg_xbuf(buf_t *);
static void	xfsidbg_xbuf_real(buf_t *, int);
#ifdef DEBUG
static void	xfsidbg_xbxatrace(int);
static void	xfsidbg_xbxitrace(xfs_inode_t *);
static void	xfsidbg_xbxstrace(xfs_inode_t *);
#endif
static void 	xfsidbg_xchksum(uint *);
static void	xfsidbg_xdaargs(xfs_da_args_t *);
static void	xfsidbg_xdanode(xfs_da_intnode_t *);
static void	xfsidbg_xdastate(xfs_da_state_t *);
static void	xfsidbg_xdirleaf(xfs_dir_leafblock_t *);
static void	xfsidbg_xdirsf(xfs_dir_shortform_t *);
#ifdef DEBUG
static void	xfsidbg_xdirtrace(int);
#endif /* DEBUG */
static void	xfsidbg_xexlist(xfs_inode_t *);
static void	xfsidbg_xfindi(__psunsigned_t);
static void	xfsidbg_xflist(xfs_bmap_free_t *);
static void	xfsidbg_xgaplist(xfs_inode_t *);
static void	xfsidbg_xhelp(void);
static void 	xfsidbg_xiclog(xlog_in_core_t *);
static void	xfsidbg_xiclogall(xlog_in_core_t *);
static void	xfsidbg_xiclogcb(xlog_in_core_t *);
#ifdef DEBUG
static void	xfsidbg_xiclogtrace(xlog_in_core_t *);
#endif
static void	xfsidbg_xinodes(xfs_mount_t *mp);
static void	xfsidbg_xlog(xlog_t *);
#ifdef DEBUG
static void	xfsidbg_xlog_granttrace(xlog_t *);
#endif
static void	xfsidbg_xlog_ritem(xlog_recover_item_t *);
static void	xfsidbg_xlog_rtrans(xlog_recover_t *);
static void	xfsidbg_xlog_tic(xlog_ticket_t *);
static void	xfsidbg_xlogitem(xfs_log_item_t *);
static void	xfsidbg_xmount(xfs_mount_t *);
static void 	xfsidbg_xnode(xfs_inode_t *ip);
static void	xfsidbg_xperag(xfs_mount_t *);
#ifdef NOTYET
static void	xfsidbg_xrange(xfs_range_t *);
static void	xfsidbg_xrangelocks(xfs_inode_t *);
#endif
#ifdef DEBUG
static void 	xfsidbg_xrwtrace(xfs_inode_t *);
#endif
static void	xfsidbg_xsb(xfs_sb_t *);
#ifdef DEBUG
static void 	xfsidbg_xstrat_atrace(int);
static void 	xfsidbg_xstrat_itrace(xfs_inode_t *);
static void 	xfsidbg_xstrat_strace(xfs_inode_t *);
#endif
static void	xfsidbg_xtp(xfs_trans_t *);
static void	xfsidbg_xtrans_res(xfs_mount_t *);

#define	VD	(void (*)())

static struct xif {
	char	*name;
	void	(*func)();
	char	*help;
} xfsidbg_funcs[] = {
    "xagf",	VD xfsidbg_xagf,	"Dump XFS allocation group freespace",
    "xagi",	VD xfsidbg_xagi,	"Dump XFS allocation group inode",
#ifdef DEBUG
    "xail",	VD xfsidbg_xaildump,	"Dump XFS AIL for a mountpoint",
    "xalatrc",	VD xfsidbg_xalatrace,	"Dump XFS alloc count trace",
    "xalbtrc",	VD xfsidbg_xalbtrace,	"Dump XFS alloc block trace",
    "xalgtrc",	VD xfsidbg_xalgtrace,	"Dump XFS alloc alloc-group trace",
#endif
    "xalloc",	VD xfsidbg_xalloc,	"Dump XFS allocation args structure",
#ifdef DEBUG
    "xalmtrc",	VD xfsidbg_xalmtrace,	"Dump XFS alloc mount-point trace",
    "xalttrc",	VD xfsidbg_xalttrace,	"Dump XFS alloc entry-type trace",
#endif
    "xarg",	VD xfsidbg_xarg,	"Input XFS argument for next function",
    "xattrcx",	VD xfsidbg_xattrcontext,"Dump XFS attr_list context struct",
    "xattrlf",	VD xfsidbg_xattrleaf,	"Dump XFS attribute leaf block",
    "xattrsf",	VD xfsidbg_xattrsf,	"Dump XFS attribute shortform",
#ifdef DEBUG
    "xattrtr",	VD xfsidbg_xattrtrace,	"Dump XFS attribute attr_list() trace",
#endif
    "xbirec",	VD xfsidbg_xbirec,	"Dump XFS bmap incore record",
#ifdef DEBUG
    "xblitrc",	VD xfsidbg_xblitrace,	"Dump XFS buf log item trace",
#endif
    "xbmalla",	VD xfsidbg_xbmalla,	"Dump XFS bmalloc args structure",
#ifdef DEBUG
    "xbmatrc",	VD xfsidbg_xbmatrace,	"Dump XFS bmap btree count trace",
    "xbmitrc",	VD xfsidbg_xbmitrace,	"Dump XFS bmap btree per-inode trace",
    "xbmstrc",	VD xfsidbg_xbmstrace, 	"Dump XFS bmap btree inode trace",
#endif
    "xbrec",	VD xfsidbg_xbrec, 	"Dump XFS bmap record",
    "xbroot",	VD xfsidbg_xbroot, 	"Dump XFS bmap btree root (data)",
    "xbroota",	VD xfsidbg_xbroota, 	"Dump XFS bmap btree root (attr)",
    "xbtcur",	VD xfsidbg_xbtcur,	"Dump XFS btree cursor",
    "xbuf",	VD xfsidbg_xbuf,	"Dump XFS data from a buffer",
#ifdef DEBUG
    "xbxatrc",	VD xfsidbg_xbxatrace,	"Dump XFS bmap extent count trace",
    "xbxitrc",	VD xfsidbg_xbxitrace,	"Dump XFS bmap extent per-inode trace",
    "xbxstrc",	VD xfsidbg_xbxstrace,	"Dump XFS bmap extent inode trace",
#endif
    "xchksum",	VD xfsidbg_xchksum,	"Dump chksum",
    "xdaargs",	VD xfsidbg_xdaargs,	"Dump XFS dir/attr args structure",
    "xdanode",	VD xfsidbg_xdanode,	"Dump XFS dir/attr node block",
    "xdastat",	VD xfsidbg_xdastate,	"Dump XFS dir/attr state_blk struct",
    "xdirlf",	VD xfsidbg_xdirleaf,	"Dump XFS directory leaf block",
    "xdirsf",	VD xfsidbg_xdirsf,	"Dump XFS directory shortform",
#ifdef DEBUG
    "xdirtrc",	VD xfsidbg_xdirtrace,	"Dump XFS directory getdents() trace",
#endif
    "xexlist",	VD xfsidbg_xexlist,	"Dump XFS bmap extents in inode",
    "xfindi",	VD xfsidbg_xfindi,	"Find XFS inode by inum",
    "xflist",	VD xfsidbg_xflist,	"Dump XFS to-be-freed extent list",
    "xgaplst",	VD xfsidbg_xgaplist,	"Dump inode gap list",
    "xhelp",	VD xfsidbg_xhelp,	"Print idbg-xfs help",
    "xicall",	VD xfsidbg_xiclogall,	"Dump All XFS in-core logs",
    "xiclog",	VD xfsidbg_xiclog,	"Dump XFS in-core log",
#ifdef DEBUG
    "xictrc",	VD xfsidbg_xiclogtrace,	"Dump XFS in-core log trace",
#endif
    "xinodes",	VD xfsidbg_xinodes, 	"Dump XFS inodes per mount",
#ifdef DEBUG
    "xl_grtr",	VD xfsidbg_xlog_granttrace,"Dump XFS log grant trace",
#endif
    "xl_rcit",	VD xfsidbg_xlog_ritem,	"Dump XFS recovery item",
    "xl_rctr",	VD xfsidbg_xlog_rtrans,	"Dump XFS recovery transaction",
    "xl_tic",	VD xfsidbg_xlog_tic,	"Dump XFS log ticket",
    "xlog",	VD xfsidbg_xlog,	"Dump XFS log",
    "xlogcb",	VD xfsidbg_xiclogcb,	"Dump XFS in-core log callbacks",
    "xlogitm",	VD xfsidbg_xlogitem,	"Dump XFS log item structure",
    "xmount",	VD xfsidbg_xmount,	"Dump XFS mount structure",
    "xnode",	VD xfsidbg_xnode,	"Dump XFS inode",
    "xperag",	VD xfsidbg_xperag,	"Dump XFS per-allocation group data",
#ifdef NOTYET
    "xrange",   VD xfsidbg_xrange,	"Dump an xfs_range structure",
    "xranges",  VD xfsidbg_xrangelocks,	"Dump all range locks of an inode",
#endif
#ifdef DEBUG
    "xrwtrc",	VD xfsidbg_xrwtrace,	"Dump XFS inode read/write trace",
#endif
    "xsb",	VD xfsidbg_xsb,		"Dump XFS superblock",
#ifdef DEBUG
    "xstrata",  VD xfsidbg_xstrat_atrace,"Dump xfs_strat trace for all inodes",
    "xstrati",  VD xfsidbg_xstrat_itrace,"Dump xfs_strat trace for an inode",
    "xstrats",  VD xfsidbg_xstrat_strace,"Dump xfs_strat glbl trace for inode",
#endif
    "xtp",	VD xfsidbg_xtp,		"Dump XFS transaction structure",
    "xtrres",	VD xfsidbg_xtrans_res,	"Dump XFS reservation values",
    0,		0,	0
};

/* 
 * Filesystem switch functions.
 */
static void	xfsidbg_vfs_data_print(void *);
static void	xfsidbg_vfs_vnodes_print(vfs_t *, int);
static void	xfsidbg_vnode_data_print(void *);
static vnode_t	*xfsidbg_vnode_find(vfs_t *, vnumber_t);

static idbgfssw_t xfsidbgfssw = {
	NULL, "xfs", &xfs_vnodeops, &xfs_vfsops,
	xfsidbg_vfs_data_print,
	xfsidbg_vfs_vnodes_print,
	xfsidbg_vnode_data_print,
	xfsidbg_vnode_find
};

/*
 * Initialization routine.
 */
void
xfsidbginit(void)
{
	struct xif	*p;

	for (p = xfsidbg_funcs; p->name; p++)
		idbg_addfunc(p->name, p->func);
	idbg_addfssw(&xfsidbgfssw);
}

/*
 * Unload routine.
 */
int
xfsidbgunload(void)
{
	struct xif	*p;

	for (p = xfsidbg_funcs; p->name; p++)
		idbg_delfunc(p->func);
	idbg_delfssw(&xfsidbgfssw);
	return 0;
}

/*
 * Argument to xfs_alloc routines, for allocation type.
 */
static char *xfs_alloctype[] = {
	"any_ag", "first_ag", "start_ag", "this_ag",
	"start_bno", "near_bno", "this_bno"
};

static int xargument = 0;

/*
 * Prototypes for static functions.
 */
#ifdef DEBUG
static int xfs_alloc_trace_entry(ktrace_entry_t *ktep);
static int xfs_attr_trace_entry(ktrace_entry_t *ktep);
static int xfs_bmap_trace_entry(ktrace_entry_t *ktep);
static int xfs_bmbt_trace_entry(ktrace_entry_t *ktep);
#endif
static void xfs_broot(xfs_inode_t *ip, xfs_ifork_t *f);
static void xfs_btalloc(xfs_alloc_block_t *bt, int bsz);
static void xfs_btbmap(xfs_bmbt_block_t *bt, int bsz);
static void xfs_btino(xfs_inobt_block_t *bt, int bsz);
static void xfs_buf_item_print(xfs_buf_log_item_t *blip, int summary);
static void xfs_convert_extent(xfs_bmbt_rec_32_t *rp, xfs_dfiloff_t *op,
			       xfs_dfsbno_t *sp, xfs_dfilblks_t *cp);
static void xfs_dastate_path(xfs_da_state_path_t *p);
#ifdef DEBUG
static int xfs_dir_trace_entry(ktrace_entry_t *ktep);
#endif
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
static void xfs_inodebuf(buf_t *bp);
#ifdef DEBUG
static void xfs_iomap_enter_trace_entry(ktrace_entry_t *ktep);
static void xfs_iomap_map_trace_entry(ktrace_entry_t *ktep);
#endif
static void xfs_prdinode(xfs_dinode_t *di, int coreonly);
static void xfs_prdinode_core(xfs_dinode_core_t *dip);
#ifdef DEBUG
static void xfs_rw_enter_trace_entry(ktrace_entry_t *ktep);
static int xfs_rw_trace_entry(ktrace_entry_t *ktep);
static void xfs_strat_enter_trace_entry(ktrace_entry_t *ktep);
static void xfs_strat_sub_trace_entry(ktrace_entry_t *ktep);
static int xfs_strat_trace_entry(ktrace_entry_t *ktep);
#endif
static void xfs_xexlist_fork(xfs_inode_t *ip, int whichfork);
static void xfs_xnode_fork(char *name, xfs_ifork_t *f);

/*
 * Static functions.
 */

#ifdef DEBUG
/*
 * Print xfs alloc trace buffer entry.
 */
static int
xfs_alloc_trace_entry(ktrace_entry_t *ktep)
{		  
	if ((long)ktep->val[0] == 0)
		return 0;
	switch ((long)ktep->val[0]) {
	case XFS_ALLOC_KTRACE_ALLOC:
		qprintf("alloc %s[%s] mp 0x%x agno %d agbno 0x%x\n",
			(char *)ktep->val[1],
			ktep->val[2] ? (char *)ktep->val[2] : "",
			(xfs_mount_t *)ktep->val[3], 
			(__psint_t)ktep->val[4],
			(__psint_t)ktep->val[5]);
		qprintf("minlen %d maxlen %d mod %d prod %d minleft %d\n",
			(__psint_t)ktep->val[6], 
			(__psint_t)ktep->val[7], 
			(__psint_t)ktep->val[8],
			(__psint_t)ktep->val[9], 
			(__psint_t)ktep->val[10]);
		qprintf("total %d len %d type %s otype %s wasdel %d isfl %d\n",
			(__psint_t)ktep->val[11], 
			(__psint_t)ktep->val[12],
			xfs_alloctype[((__psint_t)ktep->val[13]) >> 16],
			xfs_alloctype[((__psint_t)ktep->val[13]) & 0xffff],
			(__psint_t)ktep->val[14], 
			(__psint_t)ktep->val[15]);
		break;
	case XFS_ALLOC_KTRACE_FREE:
		qprintf("free %s[%s] mp 0x%x agno %d\n",
			(char *)ktep->val[1],
			ktep->val[2] ? (char *)ktep->val[2] : "",
			(xfs_mount_t *)ktep->val[3], 
			(__psint_t)ktep->val[4]);
		qprintf("agbno 0x%x len %d isfl %d\n",
			(__psint_t)ktep->val[5], 
			(__psint_t)ktep->val[12],
			(__psint_t)ktep->val[15]);
		break;
	case XFS_ALLOC_KTRACE_MODAGF:
		qprintf("modagf %s[%s] mp 0x%x flags 0x%x\n",
			(char *)ktep->val[1],
			ktep->val[2] ? (char *)ktep->val[2] : "",
			(xfs_mount_t *)ktep->val[3], 
			(__psint_t)ktep->val[4]);
		qprintf("seqno %d length 0x%x\n",
			(__psint_t)ktep->val[5], 
			(__psint_t)ktep->val[6]);
		qprintf("bno root 0x%x cnt root 0x%x bno lvl %d cnt lvl %d\n",
			(__psint_t)ktep->val[7], 
			(__psint_t)ktep->val[8],
			(__psint_t)ktep->val[9], 
			(__psint_t)ktep->val[10]);
		qprintf("flfirst %d fllast %d flcount %d\n",
			(__psint_t)ktep->val[11], 
			(__psint_t)ktep->val[12],
			(__psint_t)ktep->val[13]);
		qprintf("freeblks %d longest %d\n",
			(__psint_t)ktep->val[14], 
			(__psint_t)ktep->val[15]);
		break;
	default:
		qprintf("unknown alloc trace record\n");
		break;
	}
	return 1;
}

/*
 * Print xfs a directory trace buffer entry.
 */
static int
xfs_attr_trace_entry(ktrace_entry_t *ktep)
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
		"?",		/* 0x0420 */
		"?",		/* 0x0800 */
		"KERNOTIME",	/* 0x1000 */
		NULL
	};

	if (!ktep->val[0])
		return 0;

	qprintf("-- %s: cursor h/b/o 0x%x/0x%x/%d, dupcnt %d, dp 0x%x\n",
		 (char *)ktep->val[1],
		 (__psunsigned_t)ktep->val[3],
		 (__psunsigned_t)ktep->val[4],
		 (__psunsigned_t)ktep->val[5],
		 (__psunsigned_t)ktep->val[11],
		 (__psunsigned_t)ktep->val[2]);
	qprintf("   alist 0x%x, size %d, count %d, firstu %d, Llen %d",
		 (__psunsigned_t)ktep->val[6],
		 (__psunsigned_t)ktep->val[7],
		 (__psunsigned_t)ktep->val[8],
		 (__psunsigned_t)ktep->val[9],
		 (__psunsigned_t)ktep->val[10]);
	printflags((__psunsigned_t)(ktep->val[12]), attr_arg_flags, ", flags");
	qprintf("\n");

	switch ((__psint_t)ktep->val[0]) {
	case XFS_ATTR_KTRACE_L_C:
		break;
	case XFS_ATTR_KTRACE_L_CN:
		qprintf("   node: count %d, 1st hash 0x%x, last hash 0x%x\n",
			 (__psunsigned_t)ktep->val[13],
			 (__psunsigned_t)ktep->val[14],
			 (__psunsigned_t)ktep->val[15]);
		break;
	case XFS_ATTR_KTRACE_L_CB:
		qprintf("   btree: hash 0x%x, blkno 0x%x\n",
			 (__psunsigned_t)ktep->val[13],
			 (__psunsigned_t)ktep->val[14]);
		break;
	case XFS_ATTR_KTRACE_L_CL:
		qprintf("   leaf: count %d, 1st hash 0x%x, last hash 0x%x\n",
			 (__psunsigned_t)ktep->val[13],
			 (__psunsigned_t)ktep->val[14],
			 (__psunsigned_t)ktep->val[15]);
		break;
	default:
		qprintf("   unknown attr trace record format\n");
		break;
	}
	return 1;
}

/*
 * Print xfs bmap extent trace buffer entry.
 */
static int
xfs_bmap_trace_entry(ktrace_entry_t *ktep)
{		  
	xfs_dfsbno_t		b;
	xfs_dfilblks_t		c;
	xfs_inode_t		*ip;
	xfs_ino_t		ino;
	xfs_dfiloff_t		o;
	int			opcode;
	static char		*ops[] = { "del", "ins", "pre", "post" };
	xfs_bmbt_rec_32_t	r;
	int			whichfork;

	opcode = ((__psint_t)ktep->val[0]) & 0xffff;
	if (opcode == 0)
		return 0;
	whichfork = ((__psint_t)ktep->val[0]) >> 16;
	ip = (xfs_inode_t *)ktep->val[3];
	ino = ((xfs_ino_t)ktep->val[6] << 32) | ((xfs_ino_t)ktep->val[7]);
	qprintf("%s %s:%s ip %x ino %s %cf idx %d\n",
		ops[opcode - 1], (char *)ktep->val[1],
		(char *)ktep->val[2], ip, xfs_fmtino(ino, ip->i_mount),
		"da"[whichfork], (__psint_t)ktep->val[4]);
	r.l0 = (xfs_bmbt_rec_base_t)ktep->val[8];
	r.l1 = (xfs_bmbt_rec_base_t)ktep->val[9];
	r.l2 = (xfs_bmbt_rec_base_t)ktep->val[10];
	r.l3 = (xfs_bmbt_rec_base_t)ktep->val[11];
	xfs_convert_extent(&r, &o, &b, &c);
	qprintf(" offset %lld block %s", o,
		xfs_fmtfsblock((xfs_fsblock_t)b, ip->i_mount));
	qprintf(" count %lld\n", c);
	if ((__psint_t)ktep->val[5] != 2)
		return 1;
	r.l0 = (xfs_bmbt_rec_base_t)ktep->val[12];
	r.l1 = (xfs_bmbt_rec_base_t)ktep->val[13];
	r.l2 = (xfs_bmbt_rec_base_t)ktep->val[14];
	r.l3 = (xfs_bmbt_rec_base_t)ktep->val[15];
	xfs_convert_extent(&r, &o, &b, &c);
	qprintf(" offset %lld block %s", o,
		xfs_fmtfsblock((xfs_fsblock_t)b, ip->i_mount));
	qprintf(" count %lld\n", c);
	return 1;
}

/*
 * Print xfs bmap btree trace buffer entry.
 */
static int
xfs_bmbt_trace_entry(
	ktrace_entry_t		*ktep)
{
	xfs_bmbt_rec_32_t	r;
	xfs_bmbt_irec_t		s;
	int			type;
	int			whichfork;

	type = (__psint_t)ktep->val[0] & 0xffff;
	whichfork = (__psint_t)ktep->val[0] >> 16;
	if (type == 0)
		return 0;
	qprintf("%s ip 0x%x %cf cur 0x%x",
		(char *)ktep->val[1],
		(xfs_inode_t *)ktep->val[2],
		"da"[whichfork],
		(xfs_btree_cur_t *)ktep->val[3]);
	switch (type) {
	case XFS_BMBT_KTRACE_ARGBI:
		qprintf(" buf 0x%x i %d\n",
			(buf_t *)ktep->val[4],
			(__psint_t)ktep->val[5]);
		break;
	case XFS_BMBT_KTRACE_ARGBII:
		qprintf(" buf 0x%x i0 %d i1 %d\n",
			(buf_t *)ktep->val[4],
			(__psint_t)ktep->val[5],
			(__psint_t)ktep->val[6]);
		break;
	case XFS_BMBT_KTRACE_ARGFFF:
		qprintf(" o 0x%x%08x b 0x%x%08x i 0x%x%08x\n",
			(__psint_t)ktep->val[4], 
			(__psint_t)ktep->val[5],
			(__psint_t)ktep->val[6], 
			(__psint_t)ktep->val[7],
			(__psint_t)ktep->val[8],
			(__psint_t)ktep->val[9]);
		break;
	case XFS_BMBT_KTRACE_ARGI:
		qprintf(" i 0x%x\n",
			(__psint_t)ktep->val[4]);
		break;
	case XFS_BMBT_KTRACE_ARGIFK:
		qprintf(" i 0x%x f 0x%x%08x o 0x%x%08x",
			(__psint_t)ktep->val[4],
			(__psint_t)ktep->val[5], (__psint_t)ktep->val[6],
			(__psint_t)ktep->val[7], (__psint_t)ktep->val[8]);
		break;
	case XFS_BMBT_KTRACE_ARGIFR:
		qprintf(" i 0x%x f 0x%x%08x ",
			(__psint_t)ktep->val[4],
			(__psint_t)ktep->val[5], (__psint_t)ktep->val[6]);
		s.br_startoff =
			(xfs_fileoff_t)(((xfs_dfiloff_t)ktep->val[7] << 32) |
					(xfs_dfiloff_t)ktep->val[8]);
		s.br_startblock =
			(xfs_fsblock_t)(((xfs_dfsbno_t)ktep->val[9] << 32) |
					(xfs_dfsbno_t)ktep->val[10]);
		s.br_blockcount =
			(xfs_filblks_t)(((xfs_dfilblks_t)ktep->val[11] << 32) |
					(xfs_dfilblks_t)ktep->val[12]);
		xfsidbg_xbirec(&s);
		break;
	case XFS_BMBT_KTRACE_ARGIK:
		qprintf(" i 0x%x o 0x%x%08x ",
			(__psint_t)ktep->val[4],
			(__psint_t)ktep->val[5], (__psint_t)ktep->val[6]);
		break;
	case XFS_BMBT_KTRACE_CUR:
		qprintf(" nlevels %d flags %d allocated %d\n",
			(__psint_t)ktep->val[4] >> 16,
			(__psint_t)ktep->val[4] & 0xffff,
			(__psint_t)ktep->val[5]);
		r.l0 = (xfs_bmbt_rec_base_t)ktep->val[6];
		r.l1 = (xfs_bmbt_rec_base_t)ktep->val[7];
		r.l2 = (xfs_bmbt_rec_base_t)ktep->val[8];
		r.l3 = (xfs_bmbt_rec_base_t)ktep->val[9];
		xfsidbg_xbrec(&r);
		qprintf("bufs 0x%x 0x%x 0x%x 0x%x ",
			(buf_t *)ktep->val[10],
			(buf_t *)ktep->val[11],
			(buf_t *)ktep->val[12],
			(buf_t *)ktep->val[13]);
		qprintf("ptrs %d %d %d %d\n",
			(__psint_t)ktep->val[14] >> 16,
			(__psint_t)ktep->val[14] & 0xffff,
			(__psint_t)ktep->val[15] >> 16,
			(__psint_t)ktep->val[15] & 0xffff);
		break;
	default:
		qprintf("unknown bmbt trace record\n");
		break;
	}
	return 1;
}
#endif	/* DEBUG */

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
		qprintf("inode 0x%x not btree format\n", ip); 
		return;
	}
	broot = f->if_broot;
	qprintf("block @0x%x magic %x level %d numrecs %d\n",
		broot, broot->bb_magic, broot->bb_level, broot->bb_numrecs);
	kp = XFS_BMAP_BROOT_KEY_ADDR(broot, 1, f->if_broot_bytes);
	pp = XFS_BMAP_BROOT_PTR_ADDR(broot, 1, f->if_broot_bytes);
	for (i = 1; i <= broot->bb_numrecs; i++)
		qprintf("\t%d: startoff %lld ptr %llx\n",
			i, kp[i - 1].br_startoff, pp[i - 1]);
}

/*
 * Print allocation btree block.
 */
static void
xfs_btalloc(xfs_alloc_block_t *bt, int bsz)
{
	int i;

	qprintf("magic 0x%x level %d numrecs %d leftsib 0x%x rightsib 0x%x\n",
		bt->bb_magic, bt->bb_level, bt->bb_numrecs,
		bt->bb_leftsib, bt->bb_rightsib);
	if (bt->bb_level == 0) {

		for (i = 1; i <= bt->bb_numrecs; i++) {
			xfs_alloc_rec_t *r;

			r = XFS_BTREE_REC_ADDR(bsz, xfs_alloc, bt, i, 0);
			qprintf("rec %d startblock 0x%x blockcount %d\n",
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
			qprintf("key %d startblock 0x%x blockcount %d ptr 0x%x\n",
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

	qprintf("magic 0x%x level %d numrecs %d leftsib %llx ",
		bt->bb_magic, bt->bb_level, bt->bb_numrecs,
		bt->bb_leftsib);
	qprintf("rightsib %llx\n", bt->bb_rightsib);
	if (bt->bb_level == 0) {

		for (i = 1; i <= bt->bb_numrecs; i++) {
			xfs_bmbt_rec_32_t *r;
			xfs_dfiloff_t o;
			xfs_dfsbno_t s;
			xfs_dfilblks_t c;

			r = (xfs_bmbt_rec_32_t *)XFS_BTREE_REC_ADDR(bsz,
				xfs_bmbt, bt, i, 0);
			xfs_convert_extent(r, &o, &s, &c);
			qprintf("rec %d startoff %lld ", i, o);
			qprintf("startblock %llx ", s);
			qprintf("blockcount %lld\n", c);
		}
	} else {
		int mxr;

		mxr = XFS_BTREE_BLOCK_MAXRECS(bsz, xfs_bmbt, 0);
		for (i = 1; i <= bt->bb_numrecs; i++) {
			xfs_bmbt_key_t *k;
			xfs_bmbt_ptr_t *p;

			k = XFS_BTREE_KEY_ADDR(bsz, xfs_bmbt, bt, i, mxr);
			p = XFS_BTREE_PTR_ADDR(bsz, xfs_bmbt, bt, i, mxr);
			qprintf("key %d startoff %lld ",
				i, k->br_startoff);
			qprintf("ptr %llx\n", *p);
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

	qprintf("magic 0x%x level %d numrecs %d leftsib 0x%x rightsib 0x%x\n",
		bt->bb_magic, bt->bb_level, bt->bb_numrecs,
		bt->bb_leftsib, bt->bb_rightsib);
	if (bt->bb_level == 0) {

		for (i = 1; i <= bt->bb_numrecs; i++) {
			xfs_inobt_rec_t *r;

			r = XFS_BTREE_REC_ADDR(bsz, xfs_inobt, bt, i, 0);
			qprintf("rec %d startino 0x%x freecount %d, free %llx\n",
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
			qprintf("key %d startino 0x%x ptr 0x%x\n",
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
		qprintf("buf 0x%x blkno 0x%llx ", blip->bli_buf,
			     blip->bli_format.blf_blkno);
		printflags(blip->bli_flags, bli_flags, "flags:");
		qprintf("\n   ");
		xfsidbg_xbuf_real(blip->bli_buf, 1);
		return;
	}
	qprintf("buf 0x%x recur %d refcount %d flags:",
		blip->bli_buf, blip->bli_recur, blip->bli_refcount);
	printflags(blip->bli_flags, bli_flags, NULL);
	qprintf("\n");
	qprintf("size %d blkno 0x%llx len 0x%hx map size %d map 0x%x\n",
		blip->bli_format.blf_size, blip->bli_format.blf_blkno,
		blip->bli_format.blf_len, blip->bli_format.blf_map_size,
		&(blip->bli_format.blf_data_map[0]));
	qprintf("blf flags: ");
	printflags((uint)blip->bli_format.blf_flags, blf_flags, NULL);
#ifdef XFS_TRANS_DEBUG
	qprintf("orig 0x%x logged 0x%x",
		blip->bli_orig, blip->bli_logged);
#endif
	qprintf("\n");
}

/*
 * Convert an external extent descriptor to internal form.
 */
static void
xfs_convert_extent(xfs_bmbt_rec_32_t *rp, xfs_dfiloff_t *op, xfs_dfsbno_t *sp,
		   xfs_dfilblks_t *cp)
{
	xfs_dfiloff_t o;
	xfs_dfsbno_t s;
	xfs_dfilblks_t c;

	o = (((xfs_dfiloff_t)rp->l0) << 23) |
	    (((xfs_dfiloff_t)rp->l1) >> 9);
	s = (((xfs_dfsbno_t)(rp->l1 & 0x000001ff)) << 43) |
	    (((xfs_dfsbno_t)rp->l2) << 11) |
	    (((xfs_dfsbno_t)rp->l3) >> 21);
	c = (xfs_dfilblks_t)(rp->l3 & 0x001fffff);
	*op = o;
	*sp = s;
	*cp = c;
}

#ifdef DEBUG
/*
 * Print itrunc entry trace.
 */
static void
xfs_ctrunc_trace_entry(ktrace_entry_t	*ktep)
{
	qprintf("ip 0x%x cpu %d\n",
		ktep->val[1], ktep->val[2]);
}
#endif	/* DEBUG */

/*
 * Print an xfs_da_state_path structure.
 */
static void
xfs_dastate_path(xfs_da_state_path_t *p)
{
	int i;

	qprintf("active %d\n", p->active);
	for (i = 0; i < XFS_DA_NODE_MAXDEPTH; i++) {
#if XFS_BIG_FILES
		qprintf(" blk %d bp 0x%x blkno 0x%llx",
#else
		qprintf(" blk %d bp 0x%x blkno 0x%x",
#endif
			i, p->blk[i].bp, p->blk[i].blkno);
		qprintf(" index %d hashval 0x%x ",
			p->blk[i].index, p->blk[i].hashval);
		switch(p->blk[i].magic) {
		case XFS_DA_NODE_MAGIC:		qprintf("NODE\n");	break;
		case XFS_DIR_LEAF_MAGIC:	qprintf("DIR\n");	break;
		case XFS_ATTR_LEAF_MAGIC:	qprintf("ATTR\n");	break;
		default:			qprintf("type ??\n");	break;
		}
	}
}

#ifdef DEBUG
/*
 * Print xfs a directory trace buffer entry.
 */
static int
xfs_dir_trace_entry(ktrace_entry_t *ktep)
{
	xfs_mount_t *mp;
	__uint32_t hash;
	off_t cookie;

	if (!ktep->val[0] || !ktep->val[1])
		return 0;

	mp = (xfs_mount_t *)ktep->val[3];
	cookie = (__psunsigned_t)ktep->val[4];
	cookie <<= 32;
	cookie |= (__psunsigned_t)ktep->val[5];
	qprintf("%s -- dp=0x%x b/e/h=%d/%d/0x%08x resid=0x%x ",
		    (char *)ktep->val[1], (xfs_inode_t *)ktep->val[2],
		    (__psint_t)XFS_DA_COOKIE_BNO(mp, cookie),
		    (__psint_t)XFS_DA_COOKIE_ENTRY(mp, cookie),
		    (__psunsigned_t)XFS_DA_COOKIE_HASH(mp, cookie),
		    (__psint_t)ktep->val[6]);

	switch ((__psint_t)ktep->val[0]) {
	case XFS_DIR_KTRACE_G_DU:
		break;
	case XFS_DIR_KTRACE_G_DUB:
		qprintf("bno=%d", (__psint_t)ktep->val[7]);
		break;
	case XFS_DIR_KTRACE_G_DUN:
		qprintf("forw=%d, cnt=%d, 0x%08x - 0x%08x",
			      (__psint_t)ktep->val[7],
			      (__psint_t)ktep->val[8],
			      (__psunsigned_t)ktep->val[9],
			      (__psunsigned_t)ktep->val[10]);
		break;
	case XFS_DIR_KTRACE_G_DUL:
		qprintf("forw=%d, cnt=%d, 0x%08x - 0x%08x",
			      (__psint_t)ktep->val[7],
			      (__psint_t)ktep->val[8],
			      (__psunsigned_t)ktep->val[9],
			      (__psunsigned_t)ktep->val[10]);
		break;
	case XFS_DIR_KTRACE_G_DUE:
		qprintf("entry hashval 0x%08x", (__psunsigned_t)ktep->val[7]);
		break;
	case XFS_DIR_KTRACE_G_DUC:
		cookie = (__psunsigned_t)ktep->val[7];
		cookie <<= 32;
		cookie |= (__psunsigned_t)ktep->val[8];
		hash = XFS_DA_COOKIE_HASH(mp, cookie);
		qprintf("b/e/h=%d/%d/0x%08x",
		    (__psint_t)XFS_DA_COOKIE_BNO(mp, cookie),
		    (__psint_t)XFS_DA_COOKIE_ENTRY(mp, cookie),
		    hash);
		break;
	default:
		qprintf("unknown dir trace record format");
		break;
	}
	return 1;
}
#endif	/* DEBUG */

/*
 * Print an efd log item.
 */
static void
xfs_efd_item_print(xfs_efd_log_item_t *efdp, int summary)
{
	int		i;
	xfs_extent_t	*ep;

	if (summary) {
		qprintf("Extent Free Done: ID 0x%llx nextents %d (at 0x%x)\n",
				efdp->efd_format.efd_efi_id,
				efdp->efd_format.efd_nextents, efdp);
		return;
	}
	qprintf("size %d nextents %d next extent %d efip 0x%x\n",
		efdp->efd_format.efd_size, efdp->efd_format.efd_nextents,
		efdp->efd_next_extent, efdp->efd_efip);
	qprintf("efi_id 0x%llx\n", efdp->efd_format.efd_efi_id);
	qprintf("efd extents:\n");
	ep = &(efdp->efd_format.efd_extents[0]);
	for (i = 0; i < efdp->efd_next_extent; i++, ep++) {
		qprintf("    block %llx len %d\n",
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
		qprintf("Extent Free Intention: ID 0x%llx nextents %d (at 0x%x)\n",
				efip->efi_format.efi_id,
				efip->efi_format.efi_nextents, efip);
		return;
	}
	qprintf("size %d nextents %d next extent %d\n",
		efip->efi_format.efi_size, efip->efi_format.efi_nextents,
		efip->efi_next_extent);
	qprintf("id %llx flags: ", efip->efi_format.efi_id);
	printflags(efip->efi_flags, efi_flags, NULL);
	qprintf("\n");
	qprintf("efi extents:\n");
	ep = &(efip->efi_format.efi_extents[0]);
	for (i = 0; i < efip->efi_next_extent; i++, ep++) {
		qprintf("    block %llx len %d\n",
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
		sprintf(rval, "NULLSTARTBLOCK(%d)", STARTBLOCKVAL(bno));
	else if (mp)
		sprintf(rval, "%lld[%x:%x]", (xfs_dfsbno_t)bno,
			XFS_FSB_TO_AGNO(mp, bno), XFS_FSB_TO_AGBNO(mp, bno));
	else
		sprintf(rval, "%lld", (xfs_dfsbno_t)bno);
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
		sprintf(rval, "%lld[%x:%x:%x]", ino, XFS_INO_TO_AGNO(mp, ino),
			XFS_INO_TO_AGBNO(mp, ino), XFS_INO_TO_OFFSET(mp, ino));
	else
		sprintf(rval, "%lld", ino);
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
	sprintf(rval, "%w32x:%w32x:%w32x:%w32x", ip[0], ip[1], ip[2], ip[3]);
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
		qprintf("inode 0x%x logged %d ",
			ilip->ili_inode, ilip->ili_logged);
		printflags(ilip->ili_flags, ili_flags, "flags:");
		printflags(ilip->ili_format.ilf_fields, ilf_fields, "format:");
		printflags(ilip->ili_last_fields, ilf_fields, "lastfield:");
		qprintf("\n");
		return;
	}
	qprintf("inode 0x%x ino 0x%llx logged %d flags: ",
		ilip->ili_inode, ilip->ili_format.ilf_ino, ilip->ili_logged);
	printflags(ilip->ili_flags, ili_flags, NULL);
	qprintf("\n");
	qprintf("ilock recur %d iolock recur %d ext buf 0x%x\n",
		ilip->ili_ilock_recur, ilip->ili_iolock_recur,
		ilip->ili_extents_buf);
#ifdef XFS_TRANS_DEBUG
	qprintf("root bytes %d root orig 0x%x\n",
		ilip->ili_root_size, ilip->ili_orig_root);
#endif
	qprintf("inode buf 0x%x\n", ilip->ili_bp);
	qprintf("size %d fields: ", ilip->ili_format.ilf_size);
	printflags(ilip->ili_format.ilf_fields, ilf_fields, "formatfield");
	qprintf(" last fields: ");
	printflags(ilip->ili_last_fields, ilf_fields, "lastfield");
	qprintf("\n");
	qprintf("dsize %d, asize %d, rdev 0x%x\n",
		ilip->ili_format.ilf_dsize,
		ilip->ili_format.ilf_asize,
		ilip->ili_format.ilf_u.ilfu_rdev);
	qprintf("blkno 0x%llx len 0x%x boffset 0x%x\n",
		ilip->ili_format.ilf_blkno,
		ilip->ili_format.ilf_len,
		ilip->ili_format.ilf_boffset);
}

/*
 * Print buffer full of inodes.
 */
static void
xfs_inodebuf(buf_t *bp)
{
	xfs_dinode_t *di;
	int n, i;
	vfs_t *vfsp;
	bhv_desc_t *bdp;
	xfs_mount_t *mp;

	vfsp = vfs_devsearch_nolock(bp->b_edev);
	if (!vfsp)
		return;
	bdp = bhv_lookup_unlocked(VFS_BHVHEAD(vfsp), &xfs_vfsops);
	mp = XFS_BHVTOM(bdp);
	n = bp->b_bcount >> mp->m_sb.sb_inodelog;
	for (i = 0, di = (xfs_dinode_t *)bp->b_un.b_addr;
	     i < n;
	     i++, di = (xfs_dinode_t *)((char *)di + mp->m_sb.sb_inodesize)) {
		xfs_prdinode(di, 0);
	}
}

#ifdef DEBUG
/*
 * Print iomap entry trace.
 */
static void
xfs_iomap_enter_trace_entry(ktrace_entry_t *ktep)
{
	qprintf("ip 0x%x size 0x%x%x offset 0x%x%x count 0x%x\n",
		ktep->val[1], ktep->val[2], ktep->val[3], ktep->val[4],
		ktep->val[5], ktep->val[6]);
	qprintf("next offset 0x%x%x io offset 0x%x%x\n",
		ktep->val[7], ktep->val[8], ktep->val[9], ktep->val[10]);
	qprintf("io size 0x%x last req sz 0x%x new size 0x%x%x\n",
		ktep->val[11], ktep->val[12], ktep->val[13], ktep->val[14]);
}

/*
 * Print iomap map trace.
 */
static void
xfs_iomap_map_trace_entry(ktrace_entry_t *ktep)
{
	qprintf("ip 0x%x size 0x%x%x offset 0x%x%x count 0x%x\n",
		ktep->val[1], ktep->val[2], ktep->val[3], ktep->val[4],
		ktep->val[5], ktep->val[6]);
	qprintf("bmap off 0x%x%x len 0x%x pboff 0x%x pbsize 0x%x bno 0x%x\n",
		ktep->val[7], ktep->val[8], ktep->val[9], ktep->val[10],
		ktep->val[11], ktep->val[12]);
	qprintf("imap off 0x%x count 0x%x block 0x%x\n",
		ktep->val[13], ktep->val[14], ktep->val[15]);
}

/*
 * Print itrunc entry trace.
 */
static void
xfs_itrunc_trace_entry(ktrace_entry_t	*ktep)
{
	qprintf("ip 0x%x size 0x%x%x flag %d new size 0x%x%x\n",
		ktep->val[1], ktep->val[2], ktep->val[3], ktep->val[4],
		ktep->val[5], ktep->val[6]);
	qprintf("toss start 0x%x%x toss finish 0x%x%x cpu id %d\n",
		ktep->val[7], ktep->val[8], ktep->val[9], ktep->val[10],
		ktep->val[11]);
}
#endif	/* DEBUG */

/*
 * Print disk inode.
 */
static void
xfs_prdinode(xfs_dinode_t *di, int coreonly)
{
	xfs_prdinode_core(&di->di_core);
	if (!coreonly)
		qprintf("next_unlinked 0x%x u@0x%x\n", di->di_next_unlinked,
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

	qprintf("magic 0x%x mode 0%o (%s) version 0x%x format 0x%x (%s)\n",
		dip->di_magic, dip->di_mode, xfs_fmtmode(dip->di_mode),
		dip->di_version, dip->di_format,
		xfs_fmtformat((xfs_dinode_fmt_t)dip->di_format));
	qprintf("nlink 0x%x uid 0x%x gid 0x%x projid 0x%x\n",
		dip->di_nlink, dip->di_uid, dip->di_gid,
		(uint)dip->di_projid);
	qprintf("atime 0x%x:%x mtime 0x%x:%x ctime 0x%x:%x\n",
		dip->di_atime.t_sec, dip->di_atime.t_nsec,
		dip->di_mtime.t_sec, dip->di_mtime.t_nsec,
		dip->di_ctime.t_sec, dip->di_ctime.t_nsec);
	qprintf("size %llx ", dip->di_size);
	qprintf("nblocks %lld extsize 0x%x nextents 0x%x anextents 0x%x\n",
		dip->di_nblocks, dip->di_extsize,
		dip->di_nextents, dip->di_anextents);
	qprintf("forkoff %d aformat 0x%x (%s) dmevmask 0x%x dmstate 0x%x ",
		dip->di_forkoff, dip->di_aformat,
		xfs_fmtformat((xfs_dinode_fmt_t)dip->di_aformat),
		dip->di_dmevmask, dip->di_dmstate);
	printflags(dip->di_flags, diflags, "flags");
	qprintf("gen 0x%x\n", dip->di_gen);
}

#ifdef DEBUG
/*
 * Print read/write entry trace.
 */
static void
xfs_rw_enter_trace_entry(ktrace_entry_t	*ktep)
{
	qprintf("ip 0x%x size 0x%x%x uio offset 0x%x%x uio count %x\n",
		ktep->val[1], ktep->val[2], ktep->val[3], ktep->val[4],
		ktep->val[5], ktep->val[6]);
	qprintf("ioflags 0x%x next offset 0x%x%x io offset 0x%x%x\n",
		ktep->val[7], ktep->val[8], ktep->val[9], ktep->val[10],
		ktep->val[11]);
	qprintf("io size 0x%x last req sz 0x%x new size 0x%x%x\n",
		ktep->val[12], ktep->val[13], ktep->val[14], ktep->val[15]);
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

	default:
		return 0;
	}

	return 1;
}

/*
 * Print xfs r/w strategy enter trace entry.
 */
static void
xfs_strat_enter_trace_entry(ktrace_entry_t *ktep)
{
	qprintf("inode 0x%x size 0x%x%x bp 0x%x\n",
		ktep->val[1], ktep->val[2], ktep->val[3], ktep->val[4]);
	qprintf("bp offset 0x%x%x bcount 0x%x bufsize 0x%x blkno 0x%x\n",
		ktep->val[5], ktep->val[6], ktep->val[7], ktep->val[8],
		ktep->val[9]);
	qprintf("bp flags ");
	printflags((int)((unsigned long)ktep->val[10]), tab_bflags,"bflags");
	qprintf("\n");
	qprintf("bp pages 0x%x pfdat pageno 0x%x\n",
		ktep->val[11], ktep->val[12]);
}

/*
 * Print xfs r/w strategy sub trace entry.
 */
static void
xfs_strat_sub_trace_entry(ktrace_entry_t *ktep)
{
	qprintf("inode 0x%x size 0x%x%x bp 0x%x rbp 0x%x\n",
		ktep->val[1], ktep->val[2], ktep->val[3], ktep->val[4],
		ktep->val[5]);
	qprintf("rbp bp offset 0x%x%x bcount 0x%x blkno 0x%x\n",
		ktep->val[6], ktep->val[7], ktep->val[8], ktep->val[9]);
	qprintf("rbp flags ");
	printflags((unsigned long)ktep->val[10], 
		tab_bflags, "bflags");
	qprintf("\n");
	qprintf("rbp b_addr 0x%x pages 0x%x\n",
		ktep->val[11], ktep->val[12]);
	qprintf("lastoff 0x%x lastbcount 0x%x lastblkno 0x%x\n",
		ktep->val[13], ktep->val[14], ktep->val[15]);
}

/*
 * Print xfs strategy trace entry.
 */
static int
xfs_strat_trace_entry(ktrace_entry_t *ktep)
{
	switch( (long)ktep->val[0] ) {
	case XFS_STRAT_ENTER:
		qprintf("STRAT ENTER:\n");
		xfs_strat_enter_trace_entry(ktep);
		break;
	case XFS_STRAT_FAST:
		qprintf("STRAT FAST:\n");
		xfs_strat_enter_trace_entry(ktep);
		break;
	case XFS_STRAT_SUB:
		qprintf("STRAT SUB:\n");
		xfs_strat_sub_trace_entry(ktep);
		break;
	default:
		return 0;
	}
	return 1;
}
#endif	/* DEBUG */

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
	xfs_ifork_t *ifp;

	ifp = XFS_IFORK_PTR(ip, whichfork);
	if (ifp->if_flags & XFS_IFEXTENTS) {
		nextents = ifp->if_bytes / sizeof(xfs_bmbt_rec_32_t);
		qprintf("inode 0x%x %cf extents 0x%x nextents 0x%x\n",
			ip, "da"[whichfork], ifp->if_u1.if_extents, nextents);
		for (i = 0; i < nextents; i++) {
			xfs_convert_extent(
				(xfs_bmbt_rec_32_t *)&ifp->if_u1.if_extents[i],
				&o, &s, &c);
			qprintf(
			"%d: startoff %lld startblock %s blockcount %lld\n",
				i, o, xfs_fmtfsblock(s, ip->i_mount), c);
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

	qprintf("%s fork", name);
	if (f == NULL) {
		qprintf(" empty\n");
		return;
	} else
		qprintf("\n");
	qprintf(" bytes %s ", xfs_fmtsize(f->if_bytes));
	qprintf("real_bytes %s lastex 0x%x u1:%s 0x%x\n",
		xfs_fmtsize(f->if_real_bytes), f->if_lastex,
		f->if_flags & XFS_IFINLINE ? "data" : "extents",
		f->if_flags & XFS_IFINLINE ?
			f->if_u1.if_data :
			(char *)f->if_u1.if_extents);
	qprintf(" broot 0x%x broot_bytes %s ext_max %d ",
		f->if_broot, xfs_fmtsize(f->if_broot_bytes), f->if_ext_max);
	printflags(f->if_flags, tab_flags, "flags");
	qprintf("\n");
	qprintf(" u2");
	for (p = (int *)&f->if_u2;
	     p < (int *)((char *)&f->if_u2 + XFS_INLINE_DATA);
	     p++)
		qprintf(" 0x%w32x", *p);
	qprintf("\n");
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
	qprintf("magicnum 0x%x versionnum 0x%x seqno 0x%x length 0x%x\n",
		agf->agf_magicnum,
		agf->agf_versionnum,
		agf->agf_seqno,
		agf->agf_length);
	qprintf("roots b 0x%x c 0x%x levels b %d c %d\n",
		agf->agf_roots[XFS_BTNUM_BNO],
		agf->agf_roots[XFS_BTNUM_CNT],
		agf->agf_levels[XFS_BTNUM_BNO],
		agf->agf_levels[XFS_BTNUM_CNT]);
	qprintf("flfirst %d fllast %d flcount %d freeblks %d longest %d\n",
		agf->agf_flfirst, agf->agf_fllast, agf->agf_flcount,
		agf->agf_freeblks, agf->agf_longest);
}

/*
 * Print xfs allocation group inode header.
 */
static void
xfsidbg_xagi(xfs_agi_t *agi)
{
    	int	i;
	int	j;

	qprintf("magicnum 0x%x versionnum 0x%x seqno 0x%x length 0x%x\n",
		agi->agi_magicnum, agi->agi_versionnum,
		agi->agi_seqno, agi->agi_length);
	qprintf("count 0x%x root 0x%x level 0x%x\n",
		agi->agi_count, agi->agi_root, agi->agi_level);
	qprintf("freecount 0x%x newino 0x%x dirino 0x%x\n",
		agi->agi_freecount, agi->agi_newino, agi->agi_dirino);

	qprintf("unlinked buckets\n");
	for (i = 0; i < XFS_AGI_UNLINKED_BUCKETS; i++) {
		for (j = 0; j < 4; j++, i++) {
			qprintf("0x%08x ", agi->agi_unlinked[i]);
		}
		qprintf("\n");
	}
}

#ifdef DEBUG
/*
 * Print out the last "count" entries in the allocation trace buffer.
 * The "a" is for "all" entries.
 */
static void
xfsidbg_xalatrace(int count)
{
	ktrace_entry_t	*ktep;
	ktrace_snap_t	kts;
	int		nentries;
	int		skip_entries;
	extern ktrace_t	*xfs_alloc_trace_buf;

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
	ktrace_entry_t	*ktep;
	ktrace_snap_t	kts;
	extern ktrace_t	*xfs_alloc_trace_buf;

	if (xfs_alloc_trace_buf == NULL) {
		qprintf("The xfs alloc trace buffer is not initialized\n");
		return;
	}

	ktep = ktrace_first(xfs_alloc_trace_buf, &kts);
	while (ktep != NULL) {
		switch ((__psint_t)ktep->val[0]) {
		case XFS_ALLOC_KTRACE_ALLOC:
		case XFS_ALLOC_KTRACE_FREE:
			if ((xfs_agblock_t)((__psint_t)ktep->val[5]) <= bno &&
			    bno < (xfs_agblock_t)((__psint_t)ktep->val[5]) +
				  (xfs_extlen_t)((__psint_t)ktep->val[12])) {
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
	ktrace_entry_t	*ktep;
	ktrace_snap_t	kts;
	extern ktrace_t	*xfs_alloc_trace_buf;

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
#endif	/* DEBUG */

/*
 * Print an allocation argument structure for XFS.
 */
static void
xfsidbg_xalloc(xfs_alloc_arg_t *args)
{
	qprintf("tp 0x%x mp 0x%x agbp 0x%x fsbno %s\n",
		args->tp, args->mp, args->agbp,
		xfs_fmtfsblock(args->fsbno, args->mp));
	qprintf("agno 0x%x agbno 0x%x minlen 0x%x maxlen 0x%x\n",
		args->agno, args->agbno, args->minlen, args->maxlen);
	qprintf("mod 0x%x prod 0x%x minleft 0x%x total 0x%x\n",
		args->mod, args->prod, args->minleft, args->total);
	qprintf("len 0x%x type %s wasdel %d isfl %d userdata %d\n",
		args->len, xfs_alloctype[args->type], args->wasdel, args->isfl,
		args->userdata);
}

#ifdef DEBUG
/*
 * Print out all the entries in the alloc trace buf corresponding
 * to the given mount point.
 */
static void
xfsidbg_xalmtrace(xfs_mount_t *mp)
{
	ktrace_entry_t	*ktep;
	ktrace_snap_t	kts;
	extern ktrace_t	*xfs_alloc_trace_buf;

	if (xfs_alloc_trace_buf == NULL) {
		qprintf("The xfs alloc trace buffer is not initialized\n");
		return;
	}

	ktep = ktrace_first(xfs_alloc_trace_buf, &kts);
	while (ktep != NULL) {
		if ((__psint_t)ktep->val[0] && (xfs_mount_t *)ktep->val[3] == mp) {
			(void)xfs_alloc_trace_entry(ktep);
			qprintf("\n");
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
	ktrace_entry_t	*ktep;
	ktrace_snap_t	kts;
	extern ktrace_t	*xfs_alloc_trace_buf;

	if (xfs_alloc_trace_buf == NULL) {
		qprintf("The xfs alloc trace buffer is not initialized\n");
		return;
	}

	ktep = ktrace_first(xfs_alloc_trace_buf, &kts);
	while (ktep != NULL) {
		if (((int)((__psint_t)ktep->val[0])) == tag) {
			(void)xfs_alloc_trace_entry(ktep);
			qprintf("\n");
		}
		ktep = ktrace_next(xfs_alloc_trace_buf, &kts);
	}
}
#endif	/* DEBUG */

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
}	/* xfsidbg_xarg */

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

	qprintf("dp 0x%x, dupcnt %d, resynch %d",
		    context->dp, context->dupcnt, context->resynch);
	printflags((__psunsigned_t)context->flags, attr_arg_flags, ", flags");
	qprintf("\ncursor h/b/o 0x%x/0x%x/%d -- p/p/i 0x%x/0x%x/0x%x\n",
			  context->cursor->hashval, context->cursor->blkno,
			  context->cursor->offset, context->cursor->pad1,
			  context->cursor->pad2, context->cursor->initted);
	qprintf("alist 0x%x, bufsize 0x%x, count %d, firstu 0x%x\n",
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
	qprintf("hdr info forw 0x%x back 0x%x magic 0x%x\n",
		i->forw, i->back, i->magic);
	qprintf("hdr count %d usedbytes %d firstused %d holes %d\n",
		h->count, h->usedbytes, h->firstused, h->holes);
	for (j = 0, m = h->freemap; j < XFS_ATTR_LEAF_MAPSIZE; j++, m++) {
		qprintf("hdr freemap %d base %d size %d\n",
			j, m->base, m->size);
	}
	for (j = 0, e = leaf->entries; j < h->count; j++, e++) {
		qprintf("[%2d] hash 0x%x nameidx %d flags <",
			j, e->hashval, e->nameidx, e->flags);
		if (e->flags & XFS_ATTR_LOCAL)
			qprintf("LOCAL ");
		if (e->flags & XFS_ATTR_ROOT)
			qprintf("ROOT ");
		if (e->flags & XFS_ATTR_INCOMPLETE)
			qprintf("INCOMPLETE ");
		k = ~(XFS_ATTR_LOCAL | XFS_ATTR_ROOT | XFS_ATTR_INCOMPLETE);
		if ((e->flags & k) != 0)
			qprintf("0x%x", e->flags & k);
		qprintf(">\n     name \"");
		if (e->flags & XFS_ATTR_LOCAL) {
			l = XFS_ATTR_LEAF_NAME_LOCAL(leaf, j);
			for (k = 0; k < l->namelen; k++)
				qprintf("%c", l->nameval[k]);
			qprintf("\"(%d) value \"", l->namelen);
			for (k = 0; (k < l->valuelen) && (k < 32); k++)
				qprintf("%c", l->nameval[l->namelen + k]);
			if (k == 32)
				qprintf("...");
			qprintf("\"(%d)\n", l->valuelen);
		} else {
			r = XFS_ATTR_LEAF_NAME_REMOTE(leaf, j);
			for (k = 0; k < r->namelen; k++)
				qprintf("%c", r->name[k]);
			qprintf("\"(%d) value blk 0x%x len %d\n",
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
	qprintf("hdr count %d\n", sfh->count);
	for (i = 0, sfe = s->list; i < sfh->count; i++) {
		qprintf("entry %d namelen %d name \"", i, sfe->namelen);
		for (j = 0; j < sfe->namelen; j++)
			qprintf("%c", sfe->nameval[j]);
		qprintf("\" valuelen %d value \"", sfe->valuelen);
		for (j = 0; (j < sfe->valuelen) && (j < 32); j++)
			qprintf("%c", sfe->nameval[sfe->namelen + j]);
		if (j == 32)
			qprintf("...");
		qprintf("\"\n");
		sfe = XFS_ATTR_SF_NEXTENTRY(sfe);
	}
}

#ifdef DEBUG
/*
 * Print out the last "count" entries in the attribute trace buffer.
 */
static void
xfsidbg_xattrtrace(int count)
{
	ktrace_entry_t	*ktep;
	ktrace_snap_t	kts;
	int		nentries;
	int		skip_entries;
	extern ktrace_t	*xfs_attr_trace_buf;

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
#endif /* DEBUG */

/*
 * Print xfs bmap internal record
 */
static void
xfsidbg_xbirec(xfs_bmbt_irec_t *r)
{
	qprintf("startoff %lld ", r->br_startoff);
	qprintf("startblock %llx ", r->br_startblock);
	qprintf("blockcount %lld\n", r->br_blockcount);
}

#ifdef DEBUG
/*
 * Print out the buf log item trace for the given buf log item.
 */
static void
xfsidbg_xblitrace(xfs_buf_log_item_t *bip)
{
	ktrace_entry_t	*ktep;
	ktrace_snap_t	kts;
	static char *xbli_flags[] = {
		"hold",		/* 0x1 */
		"dirty",	/* 0x2 */
		"stale",	/* 0x4 */
		"logged",	/* 0x8 */
		0
		};
	static char *xli_flags[] = {
		"in ail",	/* 0x1 */
		0
		};

	if (bip->bli_trace == NULL) {
		qprintf("The bli trace buffer is not initialized\n");
		return;
	}

	ktep = ktrace_first(bip->bli_trace, &kts);
	while (ktep != NULL) {
		qprintf(
"%s bp 0x%x flags ",
			ktep->val[0], ktep->val[1]);
		printflags((__psint_t)(ktep->val[2]), xbli_flags,"xbli");
		qprintf("\n");
		qprintf(
"recur %d refcount %d blkno 0x%x bcount 0x%x\n",
			ktep->val[3], ktep->val[4],
			ktep->val[5], ktep->val[6]);
		qprintf(
"bp flags ");
		printflags((__psint_t)(ktep->val[7]), tab_bflags,0);
		qprintf(
" bp flags2 ");
		printflags((__psint_t)(ktep->val[8]), tab_bflags2,0);
		qprintf("\n");
		qprintf(
"fsprivate 0x%x fsprivate2 0x%x pincount %d iodone 0x%x\n",
			ktep->val[9], ktep->val[10],
			ktep->val[11], ktep->val[12]);
		qprintf(
"lockval %d lid 0x%x log item flags ",
			ktep->val[13], ktep->val[14]);
		printflags((__psint_t)(ktep->val[15]), xli_flags,"xli");
		qprintf("\n");

		ktep = ktrace_next(bip->bli_trace, &kts);
	}
}
#endif	/* DEBUG */

/*
 * Print a bmap alloc argument structure for XFS.
 */
static void
xfsidbg_xbmalla(xfs_bmalloca_t *a)
{
	qprintf("tp 0x%x ip 0x%x eof %d prevp 0x%x\n",
		a->tp, a->ip, a->eof, a->prevp);
	qprintf("gotp 0x%x firstblock %s alen %d total %d\n",
		a->gotp, xfs_fmtfsblock(a->firstblock, a->ip->i_mount),
		a->alen, a->total);
	qprintf("off %s wasdel %d userdata %d minlen %d\n",
		xfs_fmtfsblock(a->off, a->ip->i_mount), a->wasdel,
		a->userdata, a->minlen);
	qprintf("minleft %d low %d rval %s\n",
		a->minleft, a->low, xfs_fmtfsblock(a->rval, a->ip->i_mount));
}

#ifdef DEBUG
/*
 * Print out the last "count" entries in the bmap btree trace buffer.
 * The "a" is for "all" inodes.
 */
static void
xfsidbg_xbmatrace(int count)
{
	ktrace_entry_t	*ktep;
	ktrace_snap_t	kts;
	int		nentries;
	int		skip_entries;
	extern ktrace_t	*xfs_bmbt_trace_buf;

	if (xfs_bmbt_trace_buf == NULL) {
		qprintf("The xfs bmap btree trace buffer is not initialized\n");
		return;
	}
	nentries = ktrace_nentries(xfs_bmbt_trace_buf);
	if (count == -1) {
		count = nentries;
	}
	if ((count <= 0) || (count > nentries)) {
		qprintf("Invalid count.  There are %d entries.\n", nentries);
		return;
	}

	ktep = ktrace_first(xfs_bmbt_trace_buf, &kts);
	if (count != nentries) {
		/*
		 * Skip the total minus the number to look at minus one
		 * for the entry returned by ktrace_first().
		 */
		skip_entries = nentries - count - 1;
		ktep = ktrace_skip(xfs_bmbt_trace_buf, skip_entries, &kts);
		if (ktep == NULL) {
			qprintf("Skipped them all\n");
			return;
		}
	}
	while (ktep != NULL) {
		if (xfs_bmbt_trace_entry(ktep))
			qprintf("\n");
		ktep = ktrace_next(xfs_bmbt_trace_buf, &kts);
	}
}

/*
 * Print out the bmap btree trace buffer attached to the given inode.
 */
static void
xfsidbg_xbmitrace(xfs_inode_t *ip)
{
	ktrace_entry_t	*ktep;
	ktrace_snap_t	kts;

	if (ip->i_btrace == NULL) {
		qprintf("The inode trace buffer is not initialized\n");
		return;
	}

	ktep = ktrace_first(ip->i_btrace, &kts);
	while (ktep != NULL) {
		if (xfs_bmbt_trace_entry(ktep))
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
	ktrace_entry_t	*ktep;
	ktrace_snap_t	kts;
	extern ktrace_t	*xfs_bmbt_trace_buf;

	if (xfs_bmbt_trace_buf == NULL) {
		qprintf("The xfs bmap btree trace buffer is not initialized\n");
		return;
	}

	ktep = ktrace_first(xfs_bmbt_trace_buf, &kts);
	while (ktep != NULL) {
		if ((xfs_inode_t *)(ktep->val[2]) == ip) {
			if (xfs_bmbt_trace_entry(ktep))
				qprintf("\n");
		}
		ktep = ktrace_next(xfs_bmbt_trace_buf, &kts);
	}
}
#endif	/* DEBUG */

/*
 * Print xfs bmap record
 */
static void
xfsidbg_xbrec(xfs_bmbt_rec_32_t *r)
{
	xfs_dfiloff_t o;
	xfs_dfsbno_t s;
	xfs_dfilblks_t c;

	xfs_convert_extent(r, &o, &s, &c);
	qprintf("startoff %lld ", o);
	qprintf("startblock %llx ", s);
	qprintf("blockcount %lld\n", c);
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

	qprintf("tp 0x%x mp 0x%x\n",
		c->bc_tp,
		c->bc_mp);
	if (c->bc_btnum == XFS_BTNUM_BMAP) {
		qprintf("rec.b ");
		xfsidbg_xbirec(&c->bc_rec.b);
	} else if (c->bc_btnum == XFS_BTNUM_INO) {
		qprintf("rec.i startino 0x%x freecount 0x%x free %llx\n",
			c->bc_rec.i.ir_startino, c->bc_rec.i.ir_freecount,
			c->bc_rec.i.ir_free);
	} else {
		qprintf("rec.a startblock 0x%x blockcount 0x%x\n",
			c->bc_rec.a.ar_startblock,
			c->bc_rec.a.ar_blockcount);
	}
	qprintf("bufs");
	for (l = 0; l < c->bc_nlevels; l++)
		qprintf(" 0x%x", c->bc_bufs[l]);
	qprintf("\n");
	qprintf("ptrs");
	for (l = 0; l < c->bc_nlevels; l++)
		qprintf(" 0x%x", c->bc_ptrs[l]);
	qprintf("  ra");
	for (l = 0; l < c->bc_nlevels; l++)
		qprintf(" %d", c->bc_ra[l]);
	qprintf("\n");
	qprintf("nlevels %d btnum %s blocklog %d\n",
		c->bc_nlevels,
		c->bc_btnum == XFS_BTNUM_BNO ? "bno" :
		(c->bc_btnum == XFS_BTNUM_CNT ? "cnt" :
		 (c->bc_btnum == XFS_BTNUM_BMAP ? "bmap" : "ino")),
		c->bc_blocklog);
	if (c->bc_btnum == XFS_BTNUM_BMAP) {
		qprintf("private forksize 0x%x whichfork %d ip 0x%x flags %d\n",
			c->bc_private.b.forksize,
			c->bc_private.b.whichfork,
			c->bc_private.b.ip,
			c->bc_private.b.flags);
		qprintf("private firstblock %s flist 0x%x allocated 0x%x\n",
			xfs_fmtfsblock(c->bc_private.b.firstblock, c->bc_mp),
			c->bc_private.b.flist,
			c->bc_private.b.allocated);
	} else if (c->bc_btnum == XFS_BTNUM_INO) {
		qprintf("private agbp 0x%x agno 0x%x\n",
			c->bc_private.i.agbp,
			c->bc_private.i.agno);
	} else {
		qprintf("private agbp 0x%x agno 0x%x\n",
			c->bc_private.a.agbp,
			c->bc_private.a.agno);
	}
}	

/*
 * Figure out what kind of xfs block the buffer contains, 
 * and invoke a print routine.
 */
static void
xfsidbg_xbuf(buf_t *bp)
{
	xfsidbg_xbuf_real(bp, 0);
}

/*
 * Figure out what kind of xfs block the buffer contains, 
 * and invoke a print routine (if asked to).
 */
static void
xfsidbg_xbuf_real(buf_t *bp, int summary)
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

	d = bp->b_un.b_addr;
	if ((agf = d)->agf_magicnum == XFS_AGF_MAGIC) {
		if (summary) {
			qprintf("freespace hdr for AG %d (at 0x%x)\n",
					   agf->agf_seqno, agf);
		} else {
			qprintf("buf 0x%x agf 0x%x\n", bp, agf);
			xfsidbg_xagf(agf);
		}
	} else if ((agi = d)->agi_magicnum == XFS_AGI_MAGIC) {
		if (summary) {
			qprintf("Inode hdr for AG %d (at 0x%x)\n",
				       agi->agi_seqno, agi);
		} else {
			qprintf("buf 0x%x agi 0x%x\n", bp, agi);
			xfsidbg_xagi(agi);
		}
	} else if ((bta = d)->bb_magic == XFS_ABTB_MAGIC) {
		if (summary) {
			qprintf("Alloc BNO Btree blk, level %d (at 0x%x)\n",
				       bta->bb_level, bta);
		} else {
			qprintf("buf 0x%x abtbno 0x%x\n", bp, bta);
			xfs_btalloc(bta, BBTOB(bp->b_bcount));
		}
	} else if ((bta = d)->bb_magic == XFS_ABTC_MAGIC) {
		if (summary) {
			qprintf("Alloc COUNT Btree blk, level %d (at 0x%x)\n",
				       bta->bb_level, bta);
		} else {
			qprintf("buf 0x%x abtcnt 0x%x\n", bp, bta);
			xfs_btalloc(bta, BBTOB(bp->b_bcount));
		}
	} else if ((btb = d)->bb_magic == XFS_BMAP_MAGIC) {
		if (summary) {
			qprintf("Bmap Btree blk, level %d (at 0x%x)\n",
				      btb->bb_level, btb);
		} else {
			qprintf("buf 0x%x bmapbt 0x%x\n", bp, btb);
			xfs_btbmap(btb, BBTOB(bp->b_bcount));
		}
	} else if ((bti = d)->bb_magic == XFS_IBT_MAGIC) {
		if (summary) {
			qprintf("Inode Btree blk, level %d (at 0x%x)\n",
				       bti->bb_level, bti);
		} else {
			qprintf("buf 0x%x inobt 0x%x\n", bp, bti);
			xfs_btino(bti, BBTOB(bp->b_bcount));
		}
	} else if ((aleaf = d)->hdr.info.magic == XFS_ATTR_LEAF_MAGIC) {
		if (summary) {
			qprintf("Attr Leaf, 1st hash 0x%x (at 0x%x)\n",
				      aleaf->entries[0].hashval, aleaf);
		} else {
			qprintf("buf 0x%x attr leaf 0x%x\n", bp, aleaf);
			xfsidbg_xattrleaf(aleaf);
		}
	} else if ((dleaf = d)->hdr.info.magic == XFS_DIR_LEAF_MAGIC) {
		if (summary) {
			qprintf("Dir Leaf, 1st hash 0x%x (at 0x%x)\n",
				     dleaf->entries[0].hashval, dleaf);
		} else {
			qprintf("buf 0x%x dir leaf 0x%x\n", bp, dleaf);
			xfsidbg_xdirleaf(dleaf);
		}
	} else if ((node = d)->hdr.info.magic == XFS_DA_NODE_MAGIC) {
		if (summary) {
			qprintf("Dir/Attr Node, level %d, 1st hash 0x%x (at 0x%x)\n",
			      node->hdr.level, node->btree[0].hashval, node);
		} else {
			qprintf("buf 0x%x dir/attr node 0x%x\n", bp, node);
			xfsidbg_xdanode(node);
		}
	} else if ((di = d)->di_core.di_magic == XFS_DINODE_MAGIC) {
		if (summary) {
			qprintf("Disk Inode (at 0x%x)\n", di);
		} else {
			qprintf("buf 0x%x dinode 0x%x\n", bp, di);
			xfs_inodebuf(bp);
		}
	} else if ((sb = d)->sb_magicnum == XFS_SB_MAGIC) {
		if (summary) {
			qprintf("Superblock (at 0x%x)\n", sb);
		} else {
			qprintf("buf 0x%x sb 0x%x\n", bp, sb);
			xfsidbg_xsb(sb);
		}
	} else {
		qprintf("buf 0x%x unknown 0x%x\n", bp, d);
	}
}

#ifdef DEBUG
/*
 * Print out the last "count" entries in the bmap extent trace buffer.
 * The "a" is for "all" inodes.
 */
static void
xfsidbg_xbxatrace(int count)
{
	ktrace_entry_t	*ktep;
	ktrace_snap_t	kts;
	int		nentries;
	int		skip_entries;
	extern ktrace_t	*xfs_bmap_trace_buf;

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
	ktrace_entry_t	*ktep;
	ktrace_snap_t	kts;
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
	ktrace_entry_t	*ktep;
	ktrace_snap_t	kts;
	extern ktrace_t	*xfs_bmap_trace_buf;

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
#endif	/* DEBUG */

/*
 * Compute & print buffer's checksum.
 */
static void
xfsidbg_xchksum(uint *addr)
{
	uint i, chksum;

	if (((__psint_t)addr) == ((__psint_t)-1)) {
		qprintf("USAGE xchksum <address>\n");
		qprintf("	length is set with xarg\n");
	} else {
		for (i=0; i<xargument; i++) {
			chksum ^= *addr;
			addr++;
		}
		qprintf("chksum (0x%x)  length (%d)\n", chksum, xargument);
	}
}	/* xfsidbg_xchksum */

/*
 * Print an xfs_da_args structure.
 */
static void
xfsidbg_xdaargs(xfs_da_args_t *n)
{
	char *ch;
	int i;

	qprintf(" name \"", n->namelen);
	for (i = 0; i < n->namelen; i++) {
		qprintf("%c", n->name[i]);
	}
	qprintf("\"(%d) value ", n->namelen);
	if (n->value) {
		qprintf("\"");
		ch = n->value;
		for (i = 0; (i < n->valuelen) && (i < 32); ch++, i++) {
			switch(*ch) {
			case '\n':	qprintf("\n");		break;
			case '\b':	qprintf("\b");		break;
			case '\t':	qprintf("\t");		break;
			default:	qprintf("%c", *ch);	break;
			}
		}
		if (i == 32)
			qprintf("...");
		qprintf("\"(%d)\n", n->valuelen);
	} else {
		qprintf("(NULL)(%d)\n", n->valuelen);
	}
	qprintf(" hashval 0x%x whichfork %d flags <",
		  n->hashval, n->whichfork);
	if (n->flags & ATTR_ROOT)
		qprintf("ROOT ");
	if (n->flags & ATTR_CREATE)
		qprintf("CREATE ");
	if (n->flags & ATTR_REPLACE)
		qprintf("REPLACE ");
	if (n->flags & XFS_ATTR_INCOMPLETE)
		qprintf("INCOMPLETE ");
	i = ~(ATTR_ROOT | ATTR_CREATE | ATTR_REPLACE | XFS_ATTR_INCOMPLETE);
	if ((n->flags & i) != 0)
		qprintf("0x%x", n->flags & i);
	qprintf("> rename %d\n", n->rename);
	qprintf(" leaf: blkno %d index %d rmtblkno %d rmtblkcnt %d\n",
		  n->blkno, n->index, n->rmtblkno, n->rmtblkcnt);
	qprintf(" leaf2: blkno %d index %d rmtblkno %d rmtblkcnt %d\n",
		  n->blkno2, n->index2, n->rmtblkno2, n->rmtblkcnt2);
	qprintf(" inumber %lld dp 0x%x firstblock 0x%x flist 0x%x total %d\n",
		  n->inumber, n->dp, n->firstblock, n->flist, n->total);
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
	qprintf("hdr info forw 0x%x back 0x%x magic 0x%x\n",
		i->forw, i->back, i->magic);
	qprintf("hdr count %d level %d\n",
		h->count, h->level);
	for (j = 0, e = node->btree; j < h->count; j++, e++) {
		qprintf("btree %d hashval 0x%x before 0x%x\n",
			j, e->hashval, e->before);
	}
}

/*
 * Print an xfs_da_state_blk structure.
 */
static void
xfsidbg_xdastate(xfs_da_state_t *s)
{
	xfs_da_state_blk_t *eblk;

	qprintf("args 0x%x mp 0x%x blocksize %d inleaf %d\n",
		s->args, s->mp, s->blocksize, s->inleaf);
	if (s->args)
		xfsidbg_xdaargs(s->args);
	
	qprintf("path:  ");
	xfs_dastate_path(&s->path);

	qprintf("altpath:  ");
	xfs_dastate_path(&s->altpath);

	eblk = &s->extrablk;
	qprintf("extra: valid %d, after %d\n", s->extravalid, s->extraafter);
#if XFS_BIG_FILES
	qprintf(" bp 0x%x blkno 0x%llx ", eblk->bp, eblk->blkno);
#else
	qprintf(" bp 0x%x blkno 0x%x ", eblk->bp, eblk->blkno);
#endif
	qprintf("index %d hashval 0x%x\n", eblk->index, eblk->hashval);
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
	qprintf("hdr info forw 0x%x back 0x%x magic 0x%x\n",
		i->forw, i->back, i->magic);
	qprintf("hdr count %d namebytes %d firstused %d holes %d\n",
		h->count, h->namebytes, h->firstused, h->holes);
	for (j = 0, m = h->freemap; j < XFS_DIR_LEAF_MAPSIZE; j++, m++) {
		qprintf("hdr freemap %d base %d size %d\n",
			j, m->base, m->size);
	}
	for (j = 0, e = leaf->entries; j < h->count; j++, e++) {
		n = XFS_DIR_LEAF_NAMESTRUCT(leaf, e->nameidx);
		XFS_DIR_SF_GET_DIRINO(&n->inumber, &ino);
		qprintf("leaf %d hashval 0x%x nameidx %d inumber %lld ",
			j, e->hashval, e->nameidx, ino);
		qprintf("namelen %d name \"", e->namelen);
		for (k = 0; k < e->namelen; k++)
			qprintf("%c", n->name[k]);
		qprintf("\"\n");
	}
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
	XFS_DIR_SF_GET_DIRINO(&sfh->parent, &ino);
	qprintf("hdr parent %lld", ino);
	qprintf(" count %d\n", sfh->count);
	for (i = 0, sfe = s->list; i < sfh->count; i++) {
		XFS_DIR_SF_GET_DIRINO(&sfe->inumber, &ino);
		qprintf("entry %d inumber %lld", i, ino);
		qprintf(" namelen %d name \"", sfe->namelen);
		for (j = 0; j < sfe->namelen; j++)
			qprintf("%c", sfe->name[j]);
		qprintf("\"\n");
		sfe = XFS_DIR_SF_NEXTENTRY(sfe);
	}
}


#ifdef DEBUG
/*
 * Print out the last "count" entries in the directory trace buffer.
 */
static void
xfsidbg_xdirtrace(int count)
{
	ktrace_entry_t	*ktep;
	ktrace_snap_t	kts;
	int		nentries;
	int		skip_entries;
	extern ktrace_t	*xfs_dir_trace_buf;

	if (xfs_dir_trace_buf == NULL) {
		qprintf("The xfs directory trace buffer is not initialized\n");
		return;
	}
	nentries = ktrace_nentries(xfs_dir_trace_buf);
	if (count == -1) {
		count = nentries;
	}
	if ((count <= 0) || (count > nentries)) {
		qprintf("Invalid count.  There are %d entries.\n", nentries);
		return;
	}

	ktep = ktrace_first(xfs_dir_trace_buf, &kts);
	if (count != nentries) {
		/*
		 * Skip the total minus the number to look at minus one
		 * for the entry returned by ktrace_first().
		 */
		skip_entries = nentries - count - 1;
		ktep = ktrace_skip(xfs_dir_trace_buf, skip_entries, &kts);
		if (ktep == NULL) {
			qprintf("Skipped them all\n");
			return;
		}
	}
	while (ktep != NULL) {
		if (xfs_dir_trace_entry(ktep))
			qprintf("\n");
		ktep = ktrace_next(xfs_dir_trace_buf, &kts);
	}
}
#endif /* DEBUG */

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
 * Find any incore XFS inodes with the given inode number.
 * We can only handle 64 bit inode numbers in 64 bit kernels.
 */
static void
xfsidbg_xfindi(__psunsigned_t ino)
{
	xfs_ino_t	xino;
	vfs_t		*vfsp;
	bhv_desc_t	*bdp;
	xfs_mount_t	*mp;
	xfs_inode_t	*ip;

	xino = (xfs_ino_t)ino;

	for (vfsp = rootvfs; (vfsp != NULL); vfsp = vfsp->vfs_next) {
		bdp = bhv_lookup_unlocked(VFS_BHVHEAD(vfsp), &xfs_vfsops);
		if (bdp) {
			mp = XFS_BHVTOM(bdp);
			ip = mp->m_inodes;
			do {
				if (ip->i_ino == xino) {
					xfsidbg_xnode(ip);
				}
				ip = ip->i_mnext;
			} while (ip != mp->m_inodes);
		}
	}
}

/*
 * Print an xfs free-extent list.
 */
static void
xfsidbg_xflist(xfs_bmap_free_t *flist)
{
	xfs_bmap_free_item_t	*item;

	qprintf("flist@0x%x: first 0x%x count %d\n", flist,
		flist->xbf_first, flist->xbf_count);
	for (item = flist->xbf_first; item; item = item->xbfi_next) {
		qprintf("item@0x%x: startblock %llx blockcount %d", item,
			(xfs_dfsbno_t)item->xbfi_startblock,
			item->xbfi_blockcount);
	}
}

/*
 * Print out the list of xfs_gap_ts in ip->i_gap_list.
 */
static void
xfsidbg_xgaplist(xfs_inode_t *ip)
{
	xfs_gap_t	*curr_gap;

	curr_gap = ip->i_gap_list;
	if (curr_gap == NULL) {
		qprintf("Gap list is empty for inode 0x%x\n", ip);
		return;
	}

	while (curr_gap != NULL) {
		qprintf("gap 0x%x next 0x%x offset 0x%x count 0x%x\n",
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
		qprintf("%s %s\n", padstr(p->name, 16), p->help);
}

/*
 * Print out an XFS in-core log structure.
 */
static void
xfsidbg_xiclog(xlog_in_core_t *iclog)
{
	int i;
	static char *ic_flags[] = {
		"ACTIVE",	/* 0x01 */
		"WANT_SYNC",	/* 0x02 */
		"SYNCING",	/* 0X04 */
		"DONE_SYNC",	/* 0X08 */
		"CALLBACK",	/* 0X10 */
		"DIRTY",	/* 0X20 */
		"NOTUSED",	/* 0X40 */
		0
	};

	qprintf("xlog_in_core/header at 0x%x\n", iclog);
	qprintf("magicno: %x  cycle: %d  version: %d  lsn: 0x%llx\n",
		iclog->ic_header.h_magicno, iclog->ic_header.h_cycle,
		iclog->ic_header.h_version, iclog->ic_header.h_lsn);
	qprintf("tail_lsn: 0x%llx  len: %d  prev_block: %d  num_ops: %d\n",
		iclog->ic_header.h_tail_lsn, iclog->ic_header.h_len,
		iclog->ic_header.h_prev_block, iclog->ic_header.h_num_logops);
	qprintf("cycle_data: ");
	for (i=0; i<(iclog->ic_size>>BBSHIFT); i++) {
		qprintf("%x  ", iclog->ic_header.h_cycle_data[i]);
	}
	qprintf("\n");
	qprintf("--------------------------------------------------\n");
	qprintf("data: 0x%x  &forcesema: 0x%x  next: 0x%x bp: 0x%x\n",
		iclog->ic_data, &iclog->ic_forcesema, iclog->ic_next,
		iclog->ic_bp);
	qprintf("log: 0x%x  callb: 0x%x  callb_tail: 0x%x  roundoff: %d\n",
		iclog->ic_log, iclog->ic_callback, iclog->ic_callback_tail,
		iclog->ic_roundoff);
	qprintf("size: %d  (OFFSET: %d) trace: 0x%x  refcnt: %d  bwritecnt: %d",
		iclog->ic_size, iclog->ic_offset,
#ifdef DEBUG
		iclog->ic_trace,
#else
		NULL,
#endif
		iclog->ic_refcnt, iclog->ic_bwritecnt);
	qprintf("  state: ");
	if (iclog->ic_state & XLOG_STATE_ALL)
		printflags(iclog->ic_state, ic_flags,"state");
	else
		qprintf("ILLEGAL");
	qprintf("\n");
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
	qprintf("=================================================\n");
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
		qprintf("func ");
		prsymoff((void *)cb->cb_func, NULL, NULL);
		qprintf(" arg 0x%x next 0x%x\n", cb->cb_arg, cb->cb_next);
	}
}

#ifdef DEBUG
/*
 * Print trace from incore log.
 */
static void
xfsidbg_xiclogtrace(xlog_in_core_t *iclog)
{
	ktrace_entry_t	*ktep;
	ktrace_snap_t	kts;
	ktrace_t	*kt = iclog->ic_trace;

	qprintf("iclog->ic_trace 0x%x\n", kt);
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
}	/* xfsidbg_xiclogtrace */
#endif	/* DEBUG */

/*
 * Print all of the inodes attached to the given mount structure.
 */
static void
xfsidbg_xinodes(xfs_mount_t *mp)
{
	xfs_inode_t	*ip;

	qprintf("xfs_mount at 0x%x\n", mp);
	ip = mp->m_inodes;
	if (ip != NULL) {
		do {
			qprintf("\n");
			xfsidbg_xnode(ip);
			ip = ip->i_mnext;
		} while (ip != mp->m_inodes);
	}
	qprintf("\nEnd of Inodes\n");
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
		"CHKSUM_MISMATCH",	/* 0x1 */
		"ACTIVE_RECOVERY",	/* 0x2 */
		0
	};

	qprintf("xlog at 0x%x\n", log);
	qprintf("&flushsm: 0x%x  tic_cnt: %d  tic_tcnt: %d  \n",
		&log->l_flushsema, log->l_ticket_cnt, log->l_ticket_tcnt);
	qprintf("freelist: 0x%x  tail: 0x%x  ICLOG: 0x%x  \n",
		log->l_freelist, log->l_tail, log->l_iclog);
	qprintf("&icloglock: 0x%x  tail_lsn: 0x%llx  last_sync_lsn: 0x%llx \n",
		&log->l_icloglock, log->l_tail_lsn, log->l_last_sync_lsn);
	qprintf("mp: 0x%x  xbuf: 0x%x  roundoff: %d  ",
		log->l_mp, log->l_xbuf, log->l_roundoff);
	qprintf("flags: ");
	printflags(log->l_flags, t_flags,"log");
	qprintf("\n");
	qprintf("dev: 0x%x  logBBstart: %d  logsize: %d  logBBsize: %d\n",
		log->l_dev, log->l_logBBstart, log->l_logsize,log->l_logBBsize);
     qprintf("curr_cycle: %d  prev_cycle: %d  curr_block: %d  prev_block: %d\n",
	     log->l_curr_cycle, log->l_prev_cycle, log->l_curr_block,
	     log->l_prev_block);
	qprintf("iclog_bak: 0x%x  iclog_size: 0x%x (%d)  num iclogs: %d\n",
		log->l_iclog_bak, log->l_iclog_size, log->l_iclog_size,
		log->l_iclog_bufs);
	qprintf("&grant_lock: 0x%x  resHeadQ: 0x%x  wrHeadQ: 0x%x\n",
		&log->l_grant_lock, log->l_reserve_headq, log->l_write_headq);
	qprintf("GResCycle: %d  GResBytes: %d  GWrCycle: %d  GWrBytes: %d\n",
		log->l_grant_reserve_cycle, log->l_grant_reserve_bytes,
		log->l_grant_write_cycle, log->l_grant_write_bytes);
	rbytes = log->l_grant_reserve_bytes + log->l_roundoff;
	wbytes = log->l_grant_write_bytes + log->l_roundoff;
       qprintf("GResBlocks: %d  GResRemain: %d  GWrBlocks: %d  GWrRemain: %d\n",
	       rbytes / BBSIZE, rbytes % BBSIZE,
	       wbytes / BBSIZE, wbytes % BBSIZE);
#ifdef DEBUG
	qprintf("trace: 0x%x  grant_trace: use xlog value\n",
		log->l_trace);
#endif
}	/* xfsidbg_xlog */

#ifdef DEBUG
/*
 * Print grant trace for a log.
 */
static void
xfsidbg_xlog_granttrace(xlog_t *log)
{
	ktrace_entry_t	*ktep;
	ktrace_snap_t	kts;
	ktrace_t	*kt;

	if (((__psint_t)log) == ((__psint_t)-1)) {
		qprintf("Usage: xl_grtr <log>\n");
		return;
	}
	if (kt = log->l_grant_trace)
		qprintf("log->l_grant_trace 0x%x\n", kt);
	else {
		qprintf("log->l_grant_trace is empty!\n");
		return;
	}
	ktep = ktrace_first(kt, &kts);
	while (ktep != NULL) {
		qprintf("%s\n", ktep->val[11]);
		qprintf("  tic:0x%x resQ:0x%x wrQ:0x%x ",
			ktep->val[0], ktep->val[1], ktep->val[2]);
		qprintf("  GrResC:%d GrResB:%d GrWrC:%d GrWrB:%d \n",
			ktep->val[3], ktep->val[4], ktep->val[5], ktep->val[6]);
		qprintf("  HeadC:%d HeadB:%d TailC:%d TailB:%d\n",
			ktep->val[7], ktep->val[8], ktep->val[9], ktep->val[10]);
		ktep = ktrace_next(kt, &kts);
	}
}	/* xfsidbg_xlog_granttrace */
#endif	/* DEBUG */

/*
 * Print out an XFS recovery transaction
 */
static void
xfsidbg_xlog_ritem(xlog_recover_item_t *item)
{
	int i = XLOG_MAX_REGIONS_IN_ITEM;

	qprintf("(xlog_recover_item 0x%x) ", item);
	qprintf("next: 0x%x prev: 0x%x type: %d cnt: %d ttl: %d\n",
		item->ri_next, item->ri_prev, item->ri_type, item->ri_cnt,
		item->ri_total);
	for ( ; i > 0; i--) {
		if (!item->ri_buf[XLOG_MAX_REGIONS_IN_ITEM-i].i_addr)
			break;
		qprintf("a: 0x%x l: %d ",
			item->ri_buf[XLOG_MAX_REGIONS_IN_ITEM-i].i_addr,
			item->ri_buf[XLOG_MAX_REGIONS_IN_ITEM-i].i_len);
	}
	qprintf("\n");
}	/* xfsidbg_xlog_ritem */

/*
 * Print out an XFS recovery transaction
 */
static void
xfsidbg_xlog_rtrans(xlog_recover_t *trans)
{
	xlog_recover_item_t *rip, *first_rip;

	qprintf("(xlog_recover 0x%x) ", trans);
	qprintf("tid: %x type: %d items: %d ttid: 0x%x  ",
		trans->r_log_tid, trans->r_theader.th_type,
		trans->r_theader.th_num_items, trans->r_theader.th_tid);
	qprintf("itemq: 0x%x\n", trans->r_itemq);
	if (trans->r_itemq) {
		rip = first_rip = trans->r_itemq;
		do {
			qprintf("(recovery item: 0x%x) ", rip);
			qprintf("type: %d cnt: %d total: %d\n",
				rip->ri_type, rip->ri_cnt, rip->ri_total);
			rip = rip->ri_next;
		} while (rip != first_rip);
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

	qprintf("xlog_ticket at 0x%x\n", tic);
	qprintf("next: 0x%x  prev: 0x%x  tid: 0x%x  \n",
		tic->t_next, tic->t_prev, tic->t_tid);
	qprintf("curr_res: %d  unit_res: %d  ocnt: %d  cnt: %d\n",
		tic->t_curr_res, tic->t_unit_res, (int)tic->t_ocnt,
		(int)tic->t_cnt);
	qprintf("clientid: %c  \n", tic->t_clientid);
	printflags(tic->t_flags, t_flags,"ticket");
	qprintf("\n");
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
		0
		};
	static char *li_flags[] = {
		"in ail",	/* 0x1 */
		0
		};

	qprintf("type %s mountp 0x%x flags ",
		lid_type[lip->li_type - XFS_LI_5_3_BUF + 1],
		lip->li_mountp);
	printflags((uint)(lip->li_flags), li_flags,"log");
	qprintf("\n");
	qprintf("ail forw 0x%x ail back 0x%x lsn %s desc %x ops 0x%x\n",
		lip->li_ail.ail_forw, lip->li_ail.ail_back,
		xfs_fmtlsn(&(lip->li_lsn)), lip->li_desc, lip->li_ops);
	qprintf("iodonefunc &0x%x\n", lip->li_cb);
	if (lip->li_type == XFS_LI_BUF) {
		bio_lip = lip->li_bio_list;
		if (bio_lip != NULL) {
			qprintf("iodone list:\n");
		}
		while (bio_lip != NULL) {
			qprintf("item 0x%x func 0x%x\n",
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
	default:
		qprintf("Unknown item type %d\n", lip->li_type);
		break;
	}
}

#ifdef DEBUG
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
		0
		};
	static char *li_flags[] = {
		"in ail",	/* 0x1 */
		0
		};
	int count;

	if ((mp->m_ail.ail_forw == NULL) ||
	    (mp->m_ail.ail_forw == (xfs_log_item_t *)&mp->m_ail)) {
		qprintf("AIL is empty\n");
		return;
	}
	qprintf("AIL for mp 0x%x, oldest first\n", mp);
	lip = (xfs_log_item_t*)mp->m_ail.ail_forw;
	for (count = 0; lip; count++) {
		qprintf("[%d] type %s ", count,
			      lid_type[lip->li_type - XFS_LI_5_3_BUF + 1]);
		printflags((uint)(lip->li_flags), li_flags, "flags:");
		qprintf("  lsn %s\n   ", xfs_fmtlsn(&(lip->li_lsn)));
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
		default:
			qprintf("Unknown item type %d\n", lip->li_type);
			break;
		}

		if (lip->li_ail.ail_forw == (xfs_log_item_t*)&mp->m_ail) {
			lip = NULL;
		} else {
			lip = lip->li_ail.ail_forw;
		}
	}
}
#endif	/* DEBUG */

/*
 * Print xfs mount structure.
 */
static void
xfsidbg_xmount(xfs_mount_t *mp)
{
	static char *xmount_flags[] = {
		"wsync",	/* 0x1 */
		"ino64",	/* 0x2 */
		0
		};

	qprintf("xfs_mount at 0x%x\n", mp);
	qprintf("vfsp 0x%x tid 0x%x ail_lock 0x%x &ail 0x%x\n",
		XFS_MTOVFS(mp), mp->m_tid, mp->m_ail_lock, &mp->m_ail);
	qprintf("ail_gen 0x%x &sb 0x%x\n",
		mp->m_ail_gen, &mp->m_sb);
	qprintf("sb_lock 0x%x sb_bp 0x%x dev 0x%x logdev 0x%x rtdev 0x%x\n",
		mp->m_sb_lock, mp->m_sb_bp, mp->m_dev, mp->m_logdev,
		mp->m_rtdev);
	qprintf("bsize %d agfrotor %d agirotor %d ipinlock 0x%x ihash 0x%x\n",
		mp->m_bsize, mp->m_agfrotor, mp->m_agirotor,
		mp->m_ipinlock, mp->m_ihash);
	qprintf("ihashmask 0x%x inodes 0x%x ilock 0x%x ireclaims 0x%x\n",
		mp->m_ihashmask, mp->m_inodes, mp->m_ilock, mp->m_ireclaims);
	qprintf("readio_log 0x%x readio_blocks 0x%x ",
		mp->m_readio_log, mp->m_readio_blocks);
	qprintf("writeio_log 0x%x writeio_blocks 0x%x\n",
		mp->m_writeio_log, mp->m_writeio_blocks);
	qprintf("logbufs %d logbsize %d LOG 0x%x\n", mp->m_logbufs,
		mp->m_logbsize, mp->m_log);
	qprintf("rsumlevels 0x%x rsumsize 0x%x rbmip 0x%x rsumip 0x%x\n",
		mp->m_rsumlevels, mp->m_rsumsize, mp->m_rbmip, mp->m_rsumip);
	qprintf("rootip 0x%x ddevp 0x%x logdevp 0x%x rtdevp 0x%x\n",
		mp->m_rootip, mp->m_ddevp, mp->m_logdevp, mp->m_rtdevp);
	qprintf("dircook_elog %d blkbit_log %d blkbb_log %d agno_log %d\n",
		mp->m_dircook_elog, mp->m_blkbit_log, mp->m_blkbb_log,
		mp->m_agno_log);
	qprintf("agino_log %d nreadaheads %d inode cluster size %d\n",
		mp->m_agino_log, mp->m_nreadaheads,
		mp->m_inode_cluster_size);
	qprintf("blockmask 0x%x blockwsize 0x%x blockwmask 0x%x\n",
		mp->m_blockmask, mp->m_blockwsize, mp->m_blockwmask);
	qprintf("alloc_mxr[lf,nd] %d %d alloc_mnr[lf,nd] %d %d\n",
		mp->m_alloc_mxr[0], mp->m_alloc_mxr[1],
		mp->m_alloc_mnr[0], mp->m_alloc_mnr[1]);
	qprintf("bmap_dmxr[lfnr,ndnr] %d %d bmap_dmnr[lfnr,ndnr] %d %d\n",
		mp->m_bmap_dmxr[0], mp->m_bmap_dmxr[1],
		mp->m_bmap_dmnr[0], mp->m_bmap_dmnr[1]);
	qprintf("inobt_mxr[lf,nd] %d %d inobt_mnr[lf,nd] %d %d\n",
		mp->m_inobt_mxr[0], mp->m_inobt_mxr[1],
		mp->m_inobt_mnr[0], mp->m_inobt_mnr[1]);
	qprintf("ag_maxlevels %d bm_maxlevels[d,a] %d %d in_maxlevels %d\n",
		mp->m_ag_maxlevels, mp->m_bm_maxlevels[0],
		mp->m_bm_maxlevels[1], mp->m_in_maxlevels);
	qprintf("perag 0x%x &peraglock 0x%x &growlock 0x%x rbmrotor %d\n",
		mp->m_perag, &mp->m_peraglock, &mp->m_growlock, mp->m_rbmrotor);
	printflags(mp->m_flags, xmount_flags,"flags");
	qprintf("ialloc_inos %d ialloc_blks %d litino %d\n",
		mp->m_ialloc_inos, mp->m_ialloc_blks, mp->m_litino);
	qprintf("attroffset %d da_node_ents %d maxicount %lld",
		mp->m_attroffset, mp->m_da_node_ents, mp->m_maxicount);
#if XFS_BIG_FILESYSTEMS
	qprintf(" inoadd %llx\n", mp->m_inoadd);
#else
	qprintf("\n");
#endif
}


/*
 * Command to print xfs inodes: kp xnode <addr>
 */
static void
xfsidbg_xnode(xfs_inode_t *ip)
{
	static char *tab_flags[] = {
		"grio",		/* XFS_IGRIO */
		NULL
	};

	qprintf("hash 0x%x next 0x%x prevp 0x%x mount 0x%x\n",
		ip->i_hash,
		ip->i_next,
		ip->i_prevp,
		ip->i_mount);
	qprintf("mnext 0x%x mprev 0x%x vnode 0x%x \n",
		ip->i_mnext,
		ip->i_mprev,
		XFS_ITOV(ip));
	qprintf("dev %x ino %s\n",
		ip->i_dev,
		xfs_fmtino(ip->i_ino, ip->i_mount));
	qprintf("blkno 0x%x len 0x%x boffset 0x%x\n",
		ip->i_blkno,
		ip->i_len,
		ip->i_boffset);
	qprintf("transp 0x%x &itemp 0x%x &lock 0x%x &iolock 0x%x\n",
		ip->i_transp,
		ip->i_itemp,
		&ip->i_lock,
		&ip->i_iolock);
	qprintf("&flock 0x%x (%d) pincount 0x%x &pinsema 0x%x\n",
		&ip->i_flock, valusema(&ip->i_flock),
		ip->i_pincount,
		&ip->i_pinsema);
#ifdef NOTYET
	qprintf("range lock splock 0x%x sleep 0x%x list head 0x%x\n",
		&ip->i_range_lock.r_spinlock, &ip->i_range_lock.r_sleep,
		ip->i_range_lock.r_range_list);
#endif
	qprintf("next_offset %llx ", ip->i_next_offset);
	qprintf("io_offset %llx reada_blkno %s io_size 0x%x\n",
		ip->i_io_offset,
		xfs_fmtfsblock(ip->i_reada_blkno, ip->i_mount),
		ip->i_io_size);
	qprintf("last_req_sz 0x%x new_size %llx\n",
		ip->i_last_req_sz, ip->i_new_size);
	qprintf("write off %llx gap list 0x%x ",
		ip->i_write_offset, ip->i_gap_list);
	printflags((int)ip->i_flags, tab_flags, "flags");
	qprintf("\n");
#ifdef DEBUG
	qprintf("mapcnt 0x%x update_core 0x%x\n",
		ip->i_mapcnt,
		(int)(ip->i_update_core));
#else
	qprintf("update_core 0x%x\n", (int)(ip->i_update_core));
#endif
	qprintf("gen 0x%x qbufs %d delayed blks %d\n",
		ip->i_gen,
		ip->i_queued_bufs,
		ip->i_delayed_blks);
	xfs_xnode_fork("data", &ip->i_df);
	xfs_xnode_fork("attr", ip->i_afp);
	qprintf("\n");
	xfs_prdinode_core(&ip->i_d);
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
		qprintf("ag %d f_init %d i_init %d\n",
			agno, pag->pagf_init, pag->pagi_init);
		if (pag->pagf_init)
			qprintf(
	"    f_levels[b,c] %d,%d f_flcount %d f_freeblks %d f_longest %d\n",
				pag->pagf_levels[XFS_BTNUM_BNOi],
				pag->pagf_levels[XFS_BTNUM_CNTi],
				pag->pagf_flcount, pag->pagf_freeblks,
				pag->pagf_longest);
		if (pag->pagi_init)
			qprintf("    i_freecount %d\n", pag->pagi_freecount);
	}
}

#ifdef NOTYET
/*
 * Print out a single xfs_range_t.
 */
static void
xfsidbg_xrange(xfs_range_t *rp)
{
	qprintf("range lock 0x%x forw 0x%x back 0x%x\n",
		rp, rp->r_forw, rp->r_back);
	qprintf("offset 0x%x len 0x%x sleepers %d\n",
		(uint)(rp->r_off), (uint)(rp->r_len), rp->r_sleepers);
}

/*
 * Print out the list of range locks held for a given inode.
 */
static void
xfsidbg_xrangelocks(xfs_inode_t *ip)
{
	xfs_range_lock_t	*rlp;
	xfs_range_t		*rp;
	xfs_range_t		*firstp;

	qprintf("range lock splock 0x%x sleep 0x%x list head 0x%x\n\n",
		&ip->i_range_lock.r_spinlock, &ip->i_range_lock.r_sleep,
		ip->i_range_lock.r_range_list);

	firstp = ip->i_range_lock.r_range_list;
	if (firstp == NULL) {
		qprintf("No range locks held\n");
		return;
	}

	rp = firstp;
	do {
		xfsidbg_xrange(rp);
		rp = rp->r_forw;
	} while (rp != firstp);
}
#endif /* NOTYET */

#ifdef DEBUG
int	rwentries = 64;

/*
 * Print out the read/write trace buffer attached to the given inode.
 */
static void
xfsidbg_xrwtrace(xfs_inode_t *ip)
{
	ktrace_entry_t	*ktep;
	ktrace_snap_t	kts;
	int		nentries;
	int		skip_entries;
	int		count;

	if (ip->i_rwtrace == NULL) {
		qprintf("The inode trace buffer is not initialized\n");
		return;
	}
	nentries = ktrace_nentries(ip->i_rwtrace);
	count = rwentries;
	if (count == -1) {
		count = nentries;
	}
	if ((count <= 0) || (count > nentries)) {
		qprintf("Invalid count.  There are %d entries.\n", nentries);
		return;
	}

	ktep = ktrace_first(ip->i_rwtrace, &kts);
	if (count != nentries) {
		/*
		 * Skip the total minus the number to look at minus one
		 * for the entry returned by ktrace_first().
		 */
		skip_entries = nentries - count - 1;
		ktep = ktrace_skip(ip->i_rwtrace, skip_entries, &kts);
		if (ktep == NULL) {
			qprintf("Skipped them all\n");
			return;
		}
	}
	while (ktep != NULL) {
		DELAY(1000);
		if (xfs_rw_trace_entry(ktep))
			qprintf("\n");
		ktep = ktrace_next(ip->i_rwtrace, &kts);
	}
}
#endif	/* DEBUG */

/*
 * Print xfs superblock.
 */
static void
xfsidbg_xsb(xfs_sb_t *sbp)
{
	qprintf("magicnum 0x%x blocksize 0x%x dblocks %lld rblocks %lld\n",
		sbp->sb_magicnum, sbp->sb_blocksize,
		sbp->sb_dblocks, sbp->sb_rblocks);
	qprintf("rextents %lld uuid %s logstart %s\n",
		sbp->sb_rextents,
		xfs_fmtuuid(&sbp->sb_uuid),
		xfs_fmtfsblock(sbp->sb_logstart, NULL));
	qprintf("rootino %s ",
		xfs_fmtino(sbp->sb_rootino, NULL));
	qprintf("rbmino %s ",
		xfs_fmtino(sbp->sb_rbmino, NULL));
	qprintf("rsumino %s\n",
		xfs_fmtino(sbp->sb_rsumino, NULL));
	qprintf("rextsize 0x%x agblocks 0x%x agcount 0x%x rbmblocks 0x%x\n",
		sbp->sb_rextsize,
		sbp->sb_agblocks,
		sbp->sb_agcount,
		sbp->sb_rbmblocks);
	qprintf("logblocks 0x%x versionnum 0x%x sectsize 0x%x inodesize 0x%x\n",
		sbp->sb_logblocks,
		sbp->sb_versionnum,
		sbp->sb_sectsize,
		sbp->sb_inodesize);
	qprintf("inopblock 0x%x blocklog 0x%x sectlog 0x%x inodelog 0x%x\n",
		sbp->sb_inopblock,
		sbp->sb_blocklog,
		sbp->sb_sectlog,
		sbp->sb_inodelog);
	qprintf("inopblog %d agblklog %d rextslog %d inprogress %d imax_pct %d\n",
		sbp->sb_inopblog,
		sbp->sb_agblklog,
		sbp->sb_rextslog,
		sbp->sb_inprogress,
		sbp->sb_imax_pct);
	qprintf("icount %llx ifree %llx fdblocks %llx frextents %llx\n",
		sbp->sb_icount,
		sbp->sb_ifree,
		sbp->sb_fdblocks,
		sbp->sb_frextents);
}

#ifdef DEBUG
/*
 * Print all xfs strategy trace entries.
 */
static void
xfsidbg_xstrat_atrace(int count)
{
	ktrace_entry_t	*ktep;
	ktrace_snap_t	kts;
	int		nentries;
	int		skip_entries;
	extern ktrace_t	*xfs_strat_trace_buf;

	if (xfs_strat_trace_buf== NULL) {
		qprintf("The strat trace buffer is not initialized\n");
		return;
	}
	nentries = ktrace_nentries(xfs_strat_trace_buf);
	if (count == -1) {
		count = nentries;
	}
	if ((count <= 0) || (count > nentries)) {
		qprintf("Invalid count.  There are %d entries.\n", nentries);
		return;
	}

	ktep = ktrace_first(xfs_strat_trace_buf, &kts);
	if (count != nentries) {
		/*
		 * Skip the total minus the number to look at minus one
		 * for the entry returned by ktrace_first().
		 */
		skip_entries = nentries - count - 1;
		ktep = ktrace_skip(xfs_strat_trace_buf, skip_entries, &kts);
		if (ktep == NULL) {
			qprintf("Skipped them all\n");
			return;
		}
	}
	while (ktep != NULL) {
		qprintf("\n");
		xfs_strat_trace_entry(ktep);
		ktep = ktrace_next(xfs_strat_trace_buf, &kts);
	}
}

/*
 * Print xfs strategy trace per-inode.
 */
static void
xfsidbg_xstrat_itrace(xfs_inode_t *ip)
{
	ktrace_entry_t	*ktep;
	ktrace_snap_t	kts;

	if (ip->i_rwtrace == NULL) {
		qprintf("The inode trace buffer is not initialized\n");
		return;
	}

	ktep = ktrace_first(ip->i_strat_trace, &kts);
	while (ktep != NULL) {
		if (xfs_strat_trace_entry(ktep))
			qprintf("\n");
		ktep = ktrace_next(ip->i_strat_trace, &kts);
	}
}

/*
 * Print global strategy trace entries for a particular inode.
 */
static void
xfsidbg_xstrat_strace(xfs_inode_t *ip)
{
	ktrace_entry_t	*ktep;
	ktrace_snap_t	kts;
	extern ktrace_t	*xfs_strat_trace_buf;

	if (xfs_strat_trace_buf == NULL) {
		qprintf("The strat trace buffer is not initialized\n");
		return;
	}

	qprintf("xstrats ip 0x%x\n", ip);
	ktep = ktrace_first(xfs_strat_trace_buf, &kts);
	while (ktep != NULL) {
		if ((xfs_inode_t*)(ktep->val[1]) == ip) {
			qprintf("\n");
			xfs_strat_trace_entry(ktep);
		}

		ktep = ktrace_next(xfs_strat_trace_buf, &kts);
	}
}
#endif	/* DEBUG */

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
		"first",	/* 0x08 */
		"second",	/* 0x10 */
		"continued",	/* 0x20 */
		0
		};
	static char *lid_flags[] = {
		"dirty",	/* 0x1 */
		"pinned",	/* 0x2 */
		"sync unlock",	/* 0x4 */
		0
		};

	qprintf("tp 0x%x type ", tp);
	switch (tp->t_type) {
	case XFS_TRANS_SETATTR_NOT_SIZE: qprintf("SETATTR_NOT_SIZE");	break;
	case XFS_TRANS_SETATTR_SIZE:	qprintf("SETATTR_SIZE");	break;
	case XFS_TRANS_INACTIVE:	qprintf("INACTIVE");		break;
	case XFS_TRANS_CREATE:		qprintf("CREATE");		break;
	case XFS_TRANS_CREATE_TRUNC:	qprintf("CREATE_TRUNC");	break;
	case XFS_TRANS_TRUNCATE_FILE:	qprintf("TRUNCATE_FILE");	break;
	case XFS_TRANS_REMOVE:		qprintf("REMOVE");		break;
	case XFS_TRANS_LINK:		qprintf("LINK");		break;
	case XFS_TRANS_RENAME:		qprintf("RENAME");		break;
	case XFS_TRANS_MKDIR:		qprintf("MKDIR");		break;
	case XFS_TRANS_RMDIR:		qprintf("RMDIR");		break;
	case XFS_TRANS_SYMLINK:		qprintf("SYMLINK");		break;
	case XFS_TRANS_SET_DMATTRS:	qprintf("SET_DMATTRS");		break;
	case XFS_TRANS_GROWFS:		qprintf("GROWFS");		break;
	case XFS_TRANS_STRAT_WRITE:	qprintf("STRAT_WRITE");		break;
	case XFS_TRANS_DIOSTRAT:	qprintf("DIOSTRAT");		break;
	case XFS_TRANS_WRITE_SYNC:	qprintf("WRITE_SYNC");		break;
	case XFS_TRANS_WRITEID:		qprintf("WRITEID");		break;
	case XFS_TRANS_ADDAFORK:	qprintf("ADDAFORK");		break;
	case XFS_TRANS_ATTRINVAL:	qprintf("ATTRINVAL");		break;
	case XFS_ATRUNCATE:		qprintf("ATRUNCATE");		break;
	case XFS_TRANS_ATTR_SET:	qprintf("ATTR_SET");		break;
	case XFS_TRANS_ATTR_RM:		qprintf("ATTR_RM");		break;
	case XFS_TRANS_ATTR_FLAG:	qprintf("ATTR_FLAG");		break;
	case XFS_TRANS_CLEAR_AGI_BUCKET:  qprintf("CLEAR_AGI_BUCKET");	break;
	default:			qprintf("0x%x", tp->t_type);	break;
	}
	qprintf(" mount 0x%x\n", tp->t_mountp);
	qprintf("flags ");
	printflags(tp->t_flags, xtp_flags,"xtp");
	qprintf("\n");
	qprintf("callback 0x%x forw 0x%x back 0x%x\n",
		&tp->t_logcb, tp->t_forw, tp->t_back);
	qprintf("log res %d block res %d block res used %d\n",
		tp->t_log_res, tp->t_blk_res, tp->t_blk_res_used);
	qprintf("rt res %d rt res used %d\n", tp->t_rtx_res,
		tp->t_rtx_res_used);
	qprintf("ticket 0x%x lsn %s\n",
		tp->t_ticket, xfs_fmtlsn(&tp->t_lsn));
	qprintf("callback 0x%x callarg 0x%x\n",
		tp->t_callback, tp->t_callarg);
	qprintf("icount delta %d ifree delta %d\n",
		tp->t_icount_delta, tp->t_ifree_delta);
	qprintf("blocks delta %d res blocks delta %d\n",
		tp->t_fdblocks_delta, tp->t_res_fdblocks_delta);
	qprintf("rt delta %d res rt delta %d\n",
		tp->t_frextents_delta, tp->t_res_frextents_delta);
	qprintf("ag freeblks delta %d ag flist delta %d ag btree delta %d\n",
		tp->t_ag_freeblks_delta, tp->t_ag_flist_delta,
		tp->t_ag_btree_delta);
	qprintf("dblocks delta %d agcount delta %d imaxpct delta %d\n",
		tp->t_dblocks_delta, tp->t_agcount_delta, tp->t_imaxpct_delta);
	qprintf("log items:\n");
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
			qprintf("\n");
			qprintf("chunk %d index %d item 0x%x size %d\n",
				chunk, i, lidp->lid_item, lidp->lid_size);
			qprintf("flags ");
			printflags(lidp->lid_flags, lid_flags,"lic");
			qprintf("\n");
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
	qprintf("write: %d\ttruncate: %d\trename: %d\n",
		xtrp->tr_write, xtrp->tr_itruncate, xtrp->tr_rename);
	qprintf("link: %d\tremove: %d\tsymlink: %d\n",
		xtrp->tr_link, xtrp->tr_remove, xtrp->tr_symlink);
	qprintf("create: %d\tmkdir: %d\tifree: %d\n",
		xtrp->tr_create, xtrp->tr_mkdir, xtrp->tr_ifree);
	qprintf("ichange: %d\tgrowdata: %d\tswrite: %d\n",
		xtrp->tr_ichange, xtrp->tr_growdata, xtrp->tr_swrite);
	qprintf("addafork: %d\twriteid: %d\tattrinval: %d\n",
		xtrp->tr_addafork, xtrp->tr_writeid, xtrp->tr_attrinval);
	qprintf("attrset: %d\tattrrm: %d\n",
		xtrp->tr_attrset, xtrp->tr_attrrm);
	qprintf("clearagi: %d\n", xtrp->tr_clearagi);
}

/*
 * Routines for idbgfssw.
 */

/*
 * Print private data for vfs.
 */
static void
xfsidbg_vfs_data_print(void *p)
{
	xfsidbg_xmount((xfs_mount_t *)p);
}

/*
 * Given a vfs print all its vnodes.
 * Called from vlist command.
 */
static void
xfsidbg_vfs_vnodes_print(vfs_t *vfsp, int all)
{
	bhv_desc_t *bdp;
	xfs_mount_t *mp;
	xfs_inode_t *xp;

	bdp = bhv_lookup_unlocked(VFS_BHVHEAD(vfsp), &xfs_vfsops);
	mp = XFS_BHVTOM(bdp);
	xp = mp->m_inodes;
	if (xp != NULL) {
		do {
			_prvn(XFS_ITOV(xp), all);
			xp = xp->i_mnext;
		} while (xp != mp->m_inodes);
	}
}

/*
 * Print xfs inode corresponding to this vnode.
 * Called from vnode command.
 */
static void
xfsidbg_vnode_data_print(void *p)
{
	xfsidbg_xnode((xfs_inode_t *)p);
}

/*
 * Find vnode by number in xfs filesystem
 * Called from vnode command.
 */
static vnode_t *
xfsidbg_vnode_find(vfs_t *vfsp, vnumber_t vnum)
{
	bhv_desc_t *bdp;
	xfs_mount_t	*mp;
	xfs_inode_t	*xp;

	bdp = bhv_lookup_unlocked(VFS_BHVHEAD(vfsp), &xfs_vfsops);
	mp = XFS_BHVTOM(bdp);
	xp = mp->m_inodes;
	if (xp != NULL) {
		do {
			if (XFS_ITOV(xp)->v_number == vnum)
				return XFS_ITOV(xp);
			xp = xp->i_mnext;
		} while (xp != mp->m_inodes);
	}
	return NULL;
}
