#ifndef _FS_XFS_INUM_H
#define	_FS_XFS_INUM_H

#ident	"$Revision$"

/*
 * Inode number format:
 * low inopblog bits - offset in block
 * next agblklog bits - block number in ag
 * next 32 bits - ag number
 * high 32-agblklog-inopblog bits - 0
 */
typedef	__uint64_t	xfs_ino_t;	/* inode number */
typedef	__uint32_t	xfs_agino_t;	/* within allocation grp inode number */

#define	NULLFSINO	((xfs_ino_t)-1)
#define	NULLAGINO	((xfs_agino_t)-1)

#define	xfs_ino_mask(k)	((1 << (k)) - 1)
#define	xfs_ino_offset_bits(s)	((s)->sb_inopblog)
#define	xfs_ino_agbno_bits(s)	((s)->sb_agblklog)
#define	xfs_ino_agino_bits(s)	(xfs_ino_offset_bits(s) + xfs_ino_agbno_bits(s))
#define	xfs_ino_agno_bits(s)	32

#define	xfs_ino_to_agno(s,i)	((xfs_agnumber_t)((i) >> xfs_ino_agino_bits(s)))
#define	xfs_ino_to_agino(s,i)	\
	((xfs_agino_t)(i) & xfs_ino_mask(xfs_ino_agino_bits(s)))
#define	xfs_ino_to_agbno(s,i)	\
	(((xfs_agblock_t)(i) >> xfs_ino_offset_bits(s)) & \
	 xfs_ino_mask(xfs_ino_agbno_bits(s)))
#define	xfs_ino_to_offset(s,i)	\
	((int)(i) & xfs_ino_mask(xfs_ino_offset_bits(s)))
#define	xfs_ino_to_fsb(s,i)	\
	xfs_agb_to_fsb(s, xfs_ino_to_agno(s,i), xfs_ino_to_agbno(s,i))

#define	xfs_agino_to_ino(s,a,i)	\
	(((xfs_ino_t)(a) << xfs_ino_agino_bits(s)) | (i))
#define	xfs_agino_to_agbno(s,i)	((i) >> xfs_ino_offset_bits(s))
#define	xfs_agino_to_offset(s,i)	\
	((i) & xfs_ino_mask(xfs_ino_offset_bits(s)))

#define	xfs_offbno_to_agino(s,b,o)	\
	((xfs_agino_t)(((b) << xfs_ino_offset_bits(s)) | (o)))

#endif	/* !_FS_XFS_INUM_H */
