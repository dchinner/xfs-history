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
#define	NULLAGINO_ALLOC	((xfs_agino_t)0)

#define	xfs_ino_mask(k)	((1 << (k)) - 1)
#define	xfs_ino_offset_bits(mp)	((mp)->m_sb.sb_inopblog)
#define	xfs_ino_agbno_bits(mp)	((mp)->m_sb.sb_agblklog)
#define	xfs_ino_agino_bits(mp)	\
	(xfs_ino_offset_bits(mp) + xfs_ino_agbno_bits(mp))
#define	xfs_ino_agno_bits(mp)	32

#define	xfs_ino_to_agno(mp,i)	\
	((xfs_agnumber_t)((i) >> xfs_ino_agino_bits(mp)))
#define	xfs_ino_to_agino(mp,i)	\
	((xfs_agino_t)(i) & xfs_ino_mask(xfs_ino_agino_bits(mp)))
#define	xfs_ino_to_agbno(mp,i)	\
	(((xfs_agblock_t)(i) >> xfs_ino_offset_bits(mp)) & \
	 xfs_ino_mask(xfs_ino_agbno_bits(mp)))
#define	xfs_ino_to_offset(mp,i)	\
	((int)(i) & xfs_ino_mask(xfs_ino_offset_bits(mp)))
#define	xfs_ino_to_fsb(mp,i)	\
	xfs_agb_to_fsb(mp, xfs_ino_to_agno(mp,i), xfs_ino_to_agbno(mp,i))

#define	xfs_agino_to_ino(mp,a,i)	\
	(((xfs_ino_t)(a) << xfs_ino_agino_bits(mp)) | (i))
#define	xfs_agino_to_agbno(mp,i)	((i) >> xfs_ino_offset_bits(mp))
#define	xfs_agino_to_offset(mp,i)	\
	((i) & xfs_ino_mask(xfs_ino_offset_bits(mp)))

#define	xfs_offbno_to_agino(mp,b,o)	\
	((xfs_agino_t)(((b) << xfs_ino_offset_bits(mp)) | (o)))

#endif	/* !_FS_XFS_INUM_H */
