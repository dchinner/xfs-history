#ifndef _FS_XFS_GROW_H
#define	_FS_XFS_GROW_H

#ident	"$Revision$"

/*
 * File system growth interfaces
 */

#define	XFS_FS_GEOMETRY		0	/* get filesystem geometry */
#define	XFS_GROWFS_DATA		1	/* grow data area */
#define	XFS_GROWFS_LOG		2	/* grow log, new log is internal */
#define	XFS_GROWFS_RT		3	/* grow realtime area */
#define	XFS_FSOPS_COUNT		4	/* count of operations */

/*
 * Minimum sizes need for growth checks
 */
#define	XFS_MIN_LOG_BLOCKS	512
#define	XFS_MIN_LOG_BYTES	(256 * 1024)
#define	XFS_MIN_AG_BLOCKS	64

/*
 * Input and output structures
 */

/* Output for XFS_FS_GEOMETRY */
typedef struct xfs_fsop_geom
{
	__uint32_t	blocksize;
	__uint32_t	rtextsize;
	__uint32_t	agblocks;
	__uint32_t	agcount;
	__uint32_t	logblocks;
	__uint32_t	sectsize;
	__uint32_t	inodesize;
	__uint64_t	datablocks;
	__uint64_t	rtblocks;
	__uint64_t	rtextents;
	__uint64_t	logstart;
	uuid_t		uuid;
} xfs_fsop_geom_t;

/* Input for growfs data op */
typedef struct xfs_growfs_data
{
	__uint64_t	newblocks;
} xfs_growfs_data_t;

/* Input for growfs log op */
typedef struct xfs_growfs_log
{
	__uint32_t	newblocks;
	int		isint;
} xfs_growfs_log_t;

/* Input for growfs rt op */
typedef struct xfs_growfs_rt
{
	__uint64_t	newblocks;
	__uint32_t	extsize;
} xfs_growfs_rt_t;

#ifdef _KERNEL
int					/* error status */
xfs_fsoperations(
	int		fd,		/* file descriptor for fs */
	int		opcode,		/* operation code */
	void		*in,		/* input structure */
	void		*out);		/* output structure */
#endif	/* _KERNEL */

#endif	/* _FS_XFS_GROW_H */
