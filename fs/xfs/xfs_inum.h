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

#define	XFS_INO_MASK(k)	((1 << (k)) - 1)
#define	XFS_INO_OFFSET_BITS(mp)	((mp)->m_sb.sb_inopblog)
#define	XFS_INO_AGBNO_BITS(mp)	((mp)->m_sb.sb_agblklog)
#define	XFS_INO_AGINO_BITS(mp)	\
	(XFS_INO_OFFSET_BITS(mp) + XFS_INO_AGBNO_BITS(mp))
#define	XFS_INO_AGNO_BITS(mp)	32

#define	XFS_INO_TO_AGNO(mp,i)	\
	((xfs_agnumber_t)((i) >> XFS_INO_AGINO_BITS(mp)))
#define	XFS_INO_TO_AGINO(mp,i)	\
	((xfs_agino_t)(i) & XFS_INO_MASK(XFS_INO_AGINO_BITS(mp)))
#define	XFS_INO_TO_AGBNO(mp,i)	\
	(((xfs_agblock_t)(i) >> XFS_INO_OFFSET_BITS(mp)) & \
	 XFS_INO_MASK(XFS_INO_AGBNO_BITS(mp)))
#define	XFS_INO_TO_OFFSET(mp,i)	\
	((int)(i) & XFS_INO_MASK(XFS_INO_OFFSET_BITS(mp)))
#define	XFS_INO_TO_FSB(mp,i)	\
	XFS_AGB_TO_FSB(mp, XFS_INO_TO_AGNO(mp,i), XFS_INO_TO_AGBNO(mp,i))

#define	XFS_AGINO_TO_INO(mp,a,i)	\
	(((xfs_ino_t)(a) << XFS_INO_AGINO_BITS(mp)) | (i))
#define	XFS_AGINO_TO_AGBNO(mp,i)	((i) >> XFS_INO_OFFSET_BITS(mp))
#define	XFS_AGINO_TO_OFFSET(mp,i)	\
	((i) & XFS_INO_MASK(XFS_INO_OFFSET_BITS(mp)))

#define	XFS_OFFBNO_TO_AGINO(mp,b,o)	\
	((xfs_agino_t)(((b) << XFS_INO_OFFSET_BITS(mp)) | (o)))

#endif	/* !_FS_XFS_INUM_H */
