#ifndef _FS_XFS_TYPES_H
#define	_FS_XFS_TYPES_H

#ident	"$Revision: 1.12 $"

/*
 * xFS types
 */

/*
 * Some types are conditional based on the selected configuration.
 * Set XFS_BIG_FILES=1 or 0 and XFS_BIG_FILESYSTEMS=1 or 0 depending
 * on the desired configuration.
 * XFS_BIG_FILES needs pgno_t to be 64 bits.
 * XFS_BIG_FILESYSTEMS needs daddr_t to be 64 bits.
 *
 * Expect these to be set from klocaldefs, or from the machine-type
 * defs files for the normal case.
 */

#ifndef XFS_BIG_FILES
#if defined(_K32U32) || defined(_K32U64)
#define	XFS_BIG_FILES		0
#else
#define	XFS_BIG_FILES		1
#endif
#endif

#ifndef XFS_BIG_FILESYSTEMS
#if defined(_K32U32) || defined(_K32U64)
#define	XFS_BIG_FILESYSTEMS	0
#else
#define	XFS_BIG_FILESYSTEMS	1
#endif
#endif

typedef int8_t		__int8_t;
typedef	u_int8_t	__uint8_t;

typedef	int16_t		__int16_t;
typedef	u_int16_t	__uint16_t;

typedef __uint32_t	xfs_agblock_t;	/* blockno in alloc. group */
typedef	__uint32_t	xfs_extlen_t;	/* extent length in blocks */
typedef	__uint32_t	xfs_agnumber_t;	/* allocation group number */
typedef __int32_t	xfs_extnum_t;	/* # of extents in a file */
typedef	__int64_t	xfs_fsize_t;	/* bytes in a file */

typedef	__int32_t	xfs_suminfo_t;	/* type of bitmap summary info */
#define	XFS_RTWORD_LL	(_MIPS_SZLONG == 64)
typedef	long		xfs_rtword_t;	/* word type for bitmap manipulations */

typedef	__int64_t	xfs_lsn_t;	/* log sequence number */
typedef	__int32_t	xfs_tid_t;	/* transaction identifier */

typedef __psint_t	psint;
typedef __psunsigned_t	psuint;
typedef __scint_t	scint;
typedef __scunsigned_t	scuint;

/*
 * These types are 64 bits on disk but are either 32 or 64 bits in memory.
 * Disk based types:
 */
typedef __uint64_t	xfs_dfsbno_t;	/* blockno in filesystem (agno|agbno) */
typedef __uint64_t	xfs_drfsbno_t;	/* blockno in filesystem (raw) */
typedef	__uint64_t	xfs_drtbno_t;	/* extent (block) in realtime area */
typedef	__uint64_t	xfs_dfiloff_t;	/* block number in a file */
/*
 * Memory based types are conditional.
 */
#if XFS_BIG_FILESYSTEMS
typedef	__uint64_t	xfs_fsblock_t;	/* blockno in filesystem (agno|agbno) */
typedef __uint64_t	xfs_rfsblock_t;	/* blockno in filesystem (raw) */
typedef __uint64_t	xfs_rtblock_t;	/* extent (block) in realtime area */
#else
typedef	__uint32_t	xfs_fsblock_t;	/* blockno in filesystem (agno|agbno) */
typedef __uint32_t	xfs_rfsblock_t;	/* blockno in filesystem (raw) */
typedef __uint32_t	xfs_rtblock_t;	/* extent (block) in realtime area */
#endif
#if XFS_BIG_FILES
typedef	__uint64_t	xfs_fileoff_t;	/* block number in a file */
#else
typedef	__uint32_t	xfs_fileoff_t;	/* block number in a file */
#endif

/*
 * Null values for the types.
 */
#define	NULLDFSBNO	((xfs_dfsbno_t)-1)
#define	NULLDRFSBNO	((xfs_drfsbno_t)-1)
#define	NULLDRTBNO	((xfs_drtbno_t)-1)
#define	NULLDFILOFF	((xfs_dfiloff_t)-1)

#define	NULLFSBLOCK	((xfs_fsblock_t)-1)
#define	NULLRFSBLOCK	((xfs_rfsblock_t)-1)
#define	NULLRTBLOCK	((xfs_rtblock_t)-1)
#define	NULLFILEOFF	((xfs_fileoff_t)-1)

#define	NULLAGBLOCK	((xfs_agblock_t)-1)
#define	NULLAGNUMBER	((xfs_agnumber_t)-1)
#define	NULLEXTNUM	((xfs_extnum_t)-1)

/*
 * Max values for extnum, extlen.
 */
#define	MAXEXTLEN	((xfs_extlen_t)0x001fffff)	/* 21 bits */
#define	MAXEXTNUM	((xfs_extnum_t)0x7fffffff)	/* signed int */

typedef enum {
	XFS_LOOKUP_EQi, XFS_LOOKUP_LEi, XFS_LOOKUP_GEi
} xfs_lookup_t;

typedef enum {
	XFS_BTNUM_BNOi, XFS_BTNUM_CNTi, XFS_BTNUM_BMAPi, XFS_BTNUM_INOi,
	XFS_BTNUM_MAX
} xfs_btnum_t;

#endif	/* !_FS_XFS_TYPES_H */
