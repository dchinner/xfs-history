#ifndef	_XFS_RW_H
#define	_XFS_RW_H

#ident "$Revision: 1.15 $"

struct bmapval;
struct buf;
struct cred;
struct uio;
struct vnode;
struct xfs_inode;
struct xfs_mount;
struct xfs_trans;

/*
 * This is a structure used to hold the local variables used
 * in xfs_strat_write().  We dynamically allocate this to reduce
 * the amount of stack space we use.  This is here in the public
 * header file since it is exported to xfs_init() so it can
 * initialize the zone allocator.
 */
typedef struct xfs_strat_write_locals {
	xfs_fileoff_t		offset_fsb;
	xfs_fileoff_t  		map_start_fsb;
	xfs_fileoff_t		imap_offset;
	xfs_fsblock_t		first_block;
	xfs_fsize_t		real_size;
	xfs_filblks_t		count_fsb;
	xfs_extlen_t		imap_blocks;
	off_t			last_rbp_offset;
	xfs_extlen_t		last_rbp_bcount;
	daddr_t			last_rbp_blkno;
	int			rbp_count;
	int			x;
	caddr_t			datap;
	struct buf		*rbp;
	struct xfs_mount	*mp;
	struct xfs_inode	*ip;
	struct xfs_trans	*tp;
	int			error;
	xfs_bmap_free_t		free_list;
	xfs_bmbt_irec_t		*imapp;
	int			rbp_offset;
	int			rbp_len;
	int			set_lead;
	int			s;
	int			loops;
	int			imap_index;
	int			nimaps;
	xfs_bmbt_irec_t		imap[XFS_BMAP_MAX_NMAP];
} xfs_strat_write_locals_t;

/*
 * This structure is used to communicate which extents of a file
 * were holes when a write started from xfs_write_file() to
 * xfs_strat_read().  This is necessary so that we can know which
 * blocks need to be zeroed when they are read in in xfs_strat_read()
 * if they weren\'t allocated when the buffer given to xfs_strat_read()
 * was mapped.
 *
 * We keep a list of these attached to the inode.  The list is
 * protected by the inode lock and the fact that the io lock is
 * held exclusively by writers.
 */
typedef struct xfs_gap {
	struct xfs_gap	*xg_next;
	xfs_fileoff_t	xg_offset_fsb;
	xfs_extlen_t	xg_count_fsb;
} xfs_gap_t;

/*
 * Count of bmaps allocated in one call to the xfs_bmap_zone
 * allocator.
 */
#define	XFS_ZONE_NBMAPS	4

/*
 * Maximum size of a buffer that we\'ll map.  Making this
 * too big will degrade performance due to the number of
 * pages which need to be gathered.  Making it too small
 * will prevent us from doing large I/O\'s to hardware that
 * needs it.
 *
 * This is currently set to 512 KB.
 */
#define	XFS_MAX_BMAP_LEN_BB	1024
#define	XFS_MAX_BMAP_LEN_BYTES	524288

/*
 * Convert the given file system block to a disk block.
 * We have to treat it differently based on whether the
 * file is a real time file or not, because the bmap code
 * does.
 */
#define	XFS_FSB_TO_DB(mp, ip, fsb) \
		(((ip)->i_d.di_flags & XFS_DIFLAG_REALTIME) ? \
		 XFS_FSB_TO_BB((mp), (fsb)) : XFS_FSB_TO_DADDR((mp), (fsb)))
     
/*
 * Defines for the trace mechanisms in xfs_rw.c.
 */
#define	XFS_RW_KTRACE_SIZE	64
#define	XFS_STRAT_KTRACE_SIZE	64
#define	XFS_STRAT_GTRACE_SIZE	512

#define	XFS_READ_ENTER		1
#define	XFS_WRITE_ENTER		2
#define XFS_IOMAP_READ_ENTER	3
#define	XFS_IOMAP_WRITE_ENTER	4
#define	XFS_IOMAP_READ_MAP	5
#define	XFS_IOMAP_WRITE_MAP	6
#define	XFS_IOMAP_WRITE_NOSPACE	7
#define	XFS_ITRUNC_START	8
#define	XFS_ITRUNC_FINISH1	9
#define	XFS_ITRUNC_FINISH2	10
#define	XFS_CTRUNC1		11
#define	XFS_CTRUNC2		12
#define	XFS_CTRUNC3		13
#define	XFS_CTRUNC4		14
#define	XFS_CTRUNC5		15
#define	XFS_CTRUNC6		16     

#define	XFS_STRAT_ENTER		1
#define	XFS_STRAT_FAST		2
#define	XFS_STRAT_SUB		3

/*
 * Prototypes for functions in xfs_rw.c.
 */
int
xfs_read(struct vnode	*vp,
	 struct uio	*uiop,
	 int		ioflag,
	 struct cred	*credp);

int
xfs_write(struct vnode	*vp,
	  struct uio	*uiop,
	  int		ioflag,
	  struct cred	*credp);

void
xfs_strategy(struct vnode	*vp,
	     struct buf		*bp);

int
xfs_bmap(struct vnode	*vp,
	 off_t		offset,
	 ssize_t	count,
	 int		flags,
	 struct cred	*credp,
	 struct bmapval	*bmapp,
	 int		*nbmaps);

int
xfs_zero_eof(struct xfs_inode	*ip,
	     off_t		offset,
	     xfs_fsize_t	isize,
	     struct cred	*credp);



#endif /* _XFS_RW_H */
