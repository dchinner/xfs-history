

#ifndef	_XFS_RW_H
#define	_XFS_RW_H


/*
 * This is a structure used to hold the local variables used
 * in xfs_strat_write().  We dynamically allocate this to reduce
 * the amount of stack space we use.  This is here in the public
 * header file since it is exported to xfs_init() so it can
 * initialize the zone allocator.
 */
typedef struct xfs_strat_write_locals {
	xfs_fileoff_t	offset_fsb;
	xfs_fileoff_t   map_start_fsb;
	xfs_fileoff_t	imap_offset;
	xfs_fsblock_t	first_block;
	xfs_fsize_t	real_size;
	xfs_extlen_t	count_fsb;
	xfs_extlen_t	imap_blocks;
	int		x;
	caddr_t		datap;
	buf_t		*rbp;
	xfs_mount_t	*mp;
	xfs_inode_t	*ip;
	xfs_trans_t	*tp;
	int		error;
	xfs_bmap_free_t	free_list;
	xfs_bmbt_irec_t	*imapp;
	int		rbp_offset;
	int		rbp_len;
	int		set_lead;
	int		s;
	int		imap_index;
	int		nimaps;
	xfs_bmbt_irec_t	imap[XFS_BMAP_MAX_NMAP];
} xfs_strat_write_locals_t;

/*
 * Count of bmaps allocated in one call to the xfs_bmap_zone
 * allocator.
 */
#define	XFS_ZONE_NBMAPS	4

/*
 * Prototypes for functions in xfs_rw.c.
 */
int
xfs_read(vnode_t	*vp,
	 uio_t		*uiop,
	 int		ioflag,
	 cred_t		*credp);

int
xfs_write(vnode_t	*vp,
	  uio_t		*uiop,
	  int		ioflag,
	  cred_t	*credp);

void
xfs_strategy(vnode_t	*vp,
	     buf_t	*bp);

int
xfs_bmap(vnode_t	*vp,
	 off_t		offset,
	 ssize_t	count,
	 int		flags,
	 cred_t		*credp,
	 struct bmapval	*bmapp,
	 int		*nbmaps);

void
xfs_zero_eof(xfs_inode_t	*ip,
	     off_t		offset,
	     xfs_fsize_t	isize,
	     cred_t		*credp);



#endif /* _XFS_RW_H */
