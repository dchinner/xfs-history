#ifndef _FS_XFS_TYPES_H
#define	_FS_XFS_TYPES_H

#ident	"$Revision: 1.3 $"

/*
 * xFS types
 */

typedef int8_t		__int8_t;
typedef	u_int8_t	__uint8_t;

typedef	int16_t		__int16_t;
typedef	u_int16_t	__uint16_t;

typedef __uint64_t	xfs_fsblock_t;	/* blockno in filesystem */
typedef __uint32_t	xfs_agblock_t;	/* blockno in alloc. group */
typedef	__uint32_t	xfs_extlen_t;	/* extent length in blocks */
typedef	__uint32_t	xfs_agnumber_t;	/* allocation group number */
typedef __int64_t	xfs_extnum_t;	/* # of extents in a file */

#define	NULLFSBLOCK	((xfs_fsblock_t)-1)
#define	NULLAGBLOCK	((xfs_agblock_t)-1)
#define	NULLAGNUMBER	((xfs_agnumber_t)-1)
#define	NULLEXTNUM	((xfs_extnum_t)-1)

typedef enum { XFS_LOOKUP_EQ, XFS_LOOKUP_LE, XFS_LOOKUP_GE } xfs_lookup_t;

#endif	/* !_FS_XFS_TYPES_H */
