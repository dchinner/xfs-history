#ifndef _FS_XFS_TYPES_H
#define	_FS_XFS_TYPES_H

#ident	"$Revision: 1.10 $"

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
typedef __int32_t	xfs_extnum_t;	/* # of extents in a file */

typedef	xfs_fsblock_t	xfs_rtblock_t;	/* block/extent in realtime area */
typedef	xfs_fsblock_t	xfs_rtbit_t;	/* bit number in rt bitmap */
typedef	__int32_t	xfs_suminfo_t;	/* type of bitmap summary info */
typedef	__int32_t	xfs_rtword_t;	/* word type for bitmap manipulations */

typedef	__int64_t	xfs_lsn_t;	/* log sequence number */
typedef	__int32_t	xfs_tid_t;	/* transaction identifier */

typedef __psint_t	psint;
typedef __psunsigned_t	psuint;
typedef __scint_t	scint;
typedef __scunsigned_t	scuint;

#define	NULLFSBLOCK	((xfs_fsblock_t)-1)
#define	NULLAGBLOCK	((xfs_agblock_t)-1)
#define	NULLAGNUMBER	((xfs_agnumber_t)-1)
#define	NULLEXTNUM	((xfs_extnum_t)-1)

typedef enum {
	XFS_LOOKUP_EQi, XFS_LOOKUP_LEi, XFS_LOOKUP_GEi
} xfs_lookup_t;

typedef enum {
	XFS_BTNUM_BNOi, XFS_BTNUM_CNTi, XFS_BTNUM_BMAPi, XFS_BTNUM_MAX
} xfs_btnum_t;

#endif	/* !_FS_XFS_TYPES_H */
