#ifndef __XFS_BUF_H__
#define __XFS_BUF_H__

#ifdef SIM
#define _USING_BUF_T
#else
#define _USING_BUF_T
#endif

#ifdef _USING_BUF_T
#include <sys/buf.h>

typedef struct buf xfs_buf_t;
#define xfs_buf buf


#define XFS_BUF_BFLAGS(x)            ((x)->b_flags)  /* debugging routines might need this */
#define XFS_BUF_ZEROFLAGS(x)            ((x)->b_flags = 0) 
#define XFS_BUF_STALE(x)	     ((x)->b_flags |= B_STALE)
#define XFS_BUF_UNSTALE(x)	     ((x)->b_flags &= ~B_STALE)
#define XFS_BUF_ISSTALE(x)	     ((x)->b_flags & B_STALE)
#define XFS_BUF_SUPER_STALE(x)   (x)->b_flags |= B_STALE;\
                                 (x)->b_flags &= ~(B_DELWRI|B_DONE)

#define XFS_BUF_DELAYWRITE(x)	     ((x)->b_flags |= B_DELWRI)
#define XFS_BUF_UNDELAYWRITE(x)	 ((x)->b_flags &= ~B_DELWRI)
#define XFS_BUF_ISDELAYWRITE(x)	 ((x)->b_flags & B_DELWRI)


#define XFS_BUF_ERROR(x,no)      bioerror(x,no)
#define XFS_BUF_GETERROR(x)      geterror(x);
#define XFS_BUF_ISERROR(x)       ((x)->b_flags & B_ERROR)

#define XFS_BUF_DONE(x)          ((x)->b_flags |= B_DONE)
#define XFS_BUF_UNDONE(x)    	 ((x)->b_flags &= ~B_DONE)
#define XFS_BUF_ISDONE(x)	     ((x)->b_flags & B_DONE)

#define XFS_BUF_BUSY(x)          ((x)->b_flags |= B_BUSY)
#define XFS_BUF_UNBUSY(x)    	 ((x)->b_flags &= ~B_BUSY)
#define XFS_BUF_ISBUSY(x)	     ((x)->b_flags & B_BUSY)

#define XFS_BUF_ASYNC(x)         ((x)->b_flags |= B_ASYNC)
#define XFS_BUF_UNASYNC(x)     	 ((x)->b_flags &= ~B_ASYNC)
#define XFS_BUF_ISASYNC(x)       ((x)->b_flags & B_ASYNC)

#define XFS_BUF_SHUT(x)         ((x)->b_flags |= B_XFS_SHUT)
#define XFS_BUF_UNSHUT(x)     	 ((x)->b_flags &= ~B_XFS_SHUT)
#define XFS_BUF_ISSHUT(x)       ((x)->b_flags & B_XFS_SHUT)

#define XFS_BUF_HOLD(x)         ((x)->b_flags |= B_HOLD)
#define XFS_BUF_UNHOLD(x)       ((x)->b_flags &= ~B_HOLD)
#define XFS_BUF_ISHOLD(x)       ((x)->b_flags & B_HOLD)
/* this may go away... calling iostart will define read or write */
/* used for irix at the moment not needed for linux? */
#define XFS_BUF_READ(x)         ((x)->b_flags |= B_READ)
#define XFS_BUF_UNREAD(x)       ((x)->b_flags &= ~B_READ)
#define XFS_BUF_ISREAD(x)       ((x)->b_flags & B_READ)

#define XFS_BUF_WRITE(x)        ((x)->b_flags |= B_WRITE)
#define XFS_BUF_UNWRITE(x)      ((x)->b_flags &= ~B_WRITE)
#define XFS_BUF_ISWRITE(x)      ((x)->b_flags & B_WRITE)

#define XFS_BUF_UNCACHED(x)      ((x)->b_flags |= B_UNCACHED)
#define XFS_BUF_UNUNCACHED(x)    ((x)->b_flags &= ~B_UNCACHED)
#define XFS_BUF_ISUNCACHED(x)    ((x)->b_flags & B_UNCACHED)

#define XFS_BUF_ISUNINITIAL(x)   ((x)->b_flags & B_UNINITIAL)
#define XFS_BUF_UNUNINITIAL(x)   ((x)->b_flags &= ~B_UNINITIAL)

/* hmm what does the mean on linux? may go away */
#define XFS_BUF_PAGEIO(x)       ((x)->b_flags & B_PAGEIO)

#define XFS_BUF_IODONE_FUNC(buf)	(buf)->b_iodone
#define XFS_BUF_SET_IODONE_FUNC(buf, func)	\
			if ((buf)->b_iodone == NULL) { \
				(buf)->b_iodone = (func); \
			}
#define XFS_BUF_CLR_IODONE_FUNC(buf)		\
			(buf)->b_iodone = NULL
#define XFS_BUF_SET_BDSTRAT_FUNC(buf, func)	\
			if ((buf)->b_bdstrat == NULL) { \
				(buf)->b_bdstrat = (func); \
			}
#define XFS_BUF_CLR_BDSTRAT_FUNC(buf)		\
			(buf)->b_bdstrat = NULL

#define XFS_BUF_FSPRIVATE(buf, type)		\
			((type)(buf)->b_fsprivate)
#define XFS_BUF_SET_FSPRIVATE(buf, value)	\
			(buf)->b_fsprivate = (void *)(value)
#define XFS_BUF_FSPRIVATE2(buf, type)		\
			((type)(buf)->b_fsprivate2)
#define XFS_BUF_SET_FSPRIVATE2(buf, value)	\
			(buf)->b_fsprivate2 = (void *)(value)
#define XFS_BUF_FSPRIVATE3(buf, type)		\
			((type)(buf)->b_fsprivate3)
#define XFS_BUF_SET_FSPRIVATE3(buf, value)	\
			(buf)->b_fsprivate3 = (void *)(value)

#define XFS_BUF_PTR(bp)	((bp)->b_un.b_addr)
#define XFS_BUF_ADDR(bp)	((bp)->b_blkno)
#define XFS_BUF_SET_ADDR(bp, blk)		\
			((bp)->b_blkno = blk)
#define XFS_BUF_COUNT(bp)	((bp)->b_bcount)
#define XFS_BUF_SET_COUNT(bp, cnt)		\
			((bp)->b_bcount = cnt)
#define XFS_BUF_SET_VTYPE_REF(bp, type, ref)	\
			(bp)->b_bvtype = (type); \
			(bp)->b_ref = (ref)
#define XFS_BUF_SET_VTYPE(bp, type)		\
			(bp)->b_bvtype = (type)
#define XFS_BUF_SET_REF(bp, ref)		\
			(bp)->b_ref = (ref)

int
xfs_bdstrat_cb(struct xfs_buf *bp);

#define xfs_bdwrite(mp, bp) \
	{ ((bp)->b_vp == NULL) ? (bp)->b_bdstrat = xfs_bdstrat_cb: 0; \
		(bp)->b_fsprivate3 = (mp); bdwrite(bp);}
#define xfs_bawrite(mp, bp) \
	{ ((bp)->b_vp == NULL) ? (bp)->b_bdstrat = xfs_bdstrat_cb: 0; \
		(bp)->b_fsprivate3 = (mp); bawrite(bp);}

#define xfs_buf_relse(bp)			\
		brelse(bp)

#endif /* _USING_BUF_T */

#ifdef _USING_PAGEBUF_T
#include <linux/page_buf.h>

typedef struct page_buf_s xfs_buf_t;
#define xfs_buf page_buf_s

struct inode;

typedef struct buftarg {
	struct inode	*inode;
	dev_t		dev;
} buftarg_t;

#define XFS_BUF_IODONE_FUNC(buf)	(buf)->pb_iodone
#define XFS_BUF_SET_IODONE_FUNC(buf, func)	\
			if ((buf)->pb_iodone == NULL) { \
				pagebuf_hold(buf); \
				(buf)->pb_iodone = (func); \
			}
#define XFS_BUF_CLR_IODONE_FUNC(buf)		\
			if ((buf)->pb_iodone) { \
				(buf)->pb_iodone = NULL; \
				pagebuf_rele(buf); \
			}
#define XFS_BUF_SET_BDSTRAT_FUNC(buf, func)
#define XFS_BUF_CLR_BDSTRAT_FUNC(buf)

#define XFS_BUF_FSPRIVATE(buf, type)		\
			((type)(buf)->pb_fspriv)
#define XFS_BUF_SET_FSPRIVATE(buf, value)	\
			(buf)->pb_fspriv = (void *)(value)
#define XFS_BUF_FSPRIVATE2(buf, type)		\
			((type)(buf)->pb_fspriv2)
#define XFS_BUF_SET_FSPRIVATE2(buf, value)	\
			(buf)->pb_fspriv2 = (void *)(value)
#define XFS_BUF_FSPRIVATE3(buf, type)		\
			((type)(buf)->pb_target.i_sb)
#define XFS_BUF_SET_FSPRIVATE3(buf, value)

#define XFS_BUF_PTR(bp)		((bp)->pb_addr)
#define XFS_BUF_ADDR(bp)	((bp)->pb_file_offset >> 9)
#define XFS_BUF_SET_ADDR(bp, blk)		\
			((bp)->pb_file_offset = (blk) << 9)
#define XFS_BUF_COUNT(bp)	((bp)->pb_count_desired)
#define XFS_BUF_SET_COUNT(bp, cnt)		\
			((bp)->pb_count_desired = cnt)
#define XFS_BUF_SET_VTYPE_REF(bp, type, ref)
#define XFS_BUF_SET_VTYPE(bp, type)
#define XFS_BUF_SET_REF(bp, ref)

#define xfs_bdwrite(mp, bp) pagebuf_iostart(bp, PBF_DELWRI)
#define xfs_bawrite(mp, bp) pagebuf_iostart(bp, PBF_WRITE | PBF_ASYNC)

#define xfs_buf_relse(bp)	\
			pagebuf_rele(bp)

#endif /* _USING_PAGEBUF_T */

#endif
