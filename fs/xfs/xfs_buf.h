#ifndef __XFS_BUF_H__
#define __XFS_BUF_H__

#ifdef SIM
#define _USING_BUF_T
#else
#define _USING_BUF_T 
//#define _USING_PAGEBUF_T 
#endif

#ifdef _USING_BUF_T
#include <sys/buf.h>

typedef struct buf xfs_buf_t;
#define xfs_buf buf


/* These are just for xfs_syncsub... it sets an internal variable 
 * then passes it to VOP_FLUSH_PAGES or adds the flags to a newly gotten buf_t
 */
#define XFS_B_ASYNC  B_ASYNC
#define XFS_B_DELWRI B_DELWRI
#define XFS_B_READ   B_READ

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
#define XFS_BUF_GETERROR(x)      geterror(x)
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

#define XFS_BUF_AGE(x)        ((x)->b_flags |= B_AGE)

/* hmm what does the mean on linux? may go away */
#define XFS_BUF_PAGEIO(x)       ((x)->b_flags & B_PAGEIO)

#define XFS_BUF_BP_ISMAPPED(bp) ((bp->b_flags & (B_PAGEIO|B_MAPPED)) != B_PAGEIO)
#define XFS_BUF_IS_IOSPL(bp) (bp->b_flags & (B_GR_BUF|B_PRV_BUF))           
#define XFS_BUF_IS_GRIO( bp )               (bp->b_flags &   B_GR_BUF)

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
#define XFS_BUF_SET_START(buf)			\
			(buf)->b_start = lbolt

#define XFS_BUF_PTR(bp)	((bp)->b_un.b_addr)
#define XFS_BUF_ADDR(bp)	((bp)->b_blkno)
#define XFS_BUF_SET_ADDR(bp, blk)		\
			((bp)->b_blkno = blk)
#define XFS_BUF_COUNT(bp)	((bp)->b_bcount)
#define XFS_BUF_SET_COUNT(bp, cnt)		\
			((bp)->b_bcount = cnt)

#define XFS_BUF_SIZE(bp)	((bp)->b_bufsize)
#define XFS_BUF_SET_SIZE(bp, cnt)		\
			((bp)->b_bufsize = cnt)

/* setup the buffer target from a buftarg structure */
#define XFS_BUF_SET_TARGET(bp, target)		\
			(bp)->b_target = target; \
			(bp)->b_edev = (target)->dev
/* return the dev_t being used */
#define XFS_BUF_TARGET(bp)			\
			((bp)->b_edev)
#define XFS_BUF_SET_VTYPE_REF(bp, type, ref)	\
			(bp)->b_bvtype = (type); \
			(bp)->b_ref = (ref)
#define XFS_BUF_SET_VTYPE(bp, type)		\
			(bp)->b_bvtype = (type)
#define XFS_BUF_SET_REF(bp, ref)		\
			(bp)->b_ref = (ref)

#define XFS_BUF_ISPINNED(bp) ((bp)->b_pincount > 0)

int
xfs_bdstrat_cb(struct xfs_buf *bp);

#define xfs_buf_read(target, blkno, len, flags) \
		read_buf_targ(target, blkno, len, flags)

#define xfs_buf_get(target, blkno, len, flags) \
		get_buf_targ(target, blkno, len, flags)

#define xfs_bdwrite(mp, bp) \
	{ ((bp)->b_vp == NULL) ? (bp)->b_bdstrat = xfs_bdstrat_cb: 0; \
		(bp)->b_fsprivate3 = (mp); bdwrite(bp);}
#define xfs_bawrite(mp, bp) \
	{ ((bp)->b_vp == NULL) ? (bp)->b_bdstrat = xfs_bdstrat_cb: 0; \
		(bp)->b_fsprivate3 = (mp); bawrite(bp);}

#define xfs_buf_relse(bp)			\
		brelse(bp)

#define xfs_bpin(bp)                 \
            bpin(bp)

#define xfs_bunpin(bp)               \
            bunpin(bp)
#define	xfs_bwait_unpin(bp)          \
            bwait_unpin(bp)
#define xfs_bp_mapin(bp)             \
            bp_mapin(bp)

#endif /* _USING_BUF_T */

#ifdef _USING_PAGEBUF_T
#include <linux/page_buf.h>

/* These are just for xfs_syncsub... it sets an internal variable 
 * then passes it to VOP_FLUSH_PAGES or adds the flags to a newly gotten buf_t
 */
#define XFS_B_ASYNC  PBF_ASYNC
#define XFS_B_DELWRI PBF_DELWRI
#define XFS_B_READ   PBF_READ
#define XFS_B_STALE (1 << 31)
#define XFS_BUF_BFLAGS(x)        ((x)->pb_flags)  /* debugging routines might need this */
#define XFS_BUF_ZEROFLAGS(x)     ((x)->pb_flags = 0) 

#define XFS_BUF_STALE(x)	     ((x)->pb_flags |= XFS_B_STALE)
#define XFS_BUF_UNSTALE(x)	     ((x)->pb_flags &= ~XFS_B_STALE)
#define XFS_BUF_ISSTALE(x)	     ((x)->pb_flags & XFS_B_STALE)
#define XFS_BUF_SUPER_STALE(x)   (x)->pb_flags & XFS_B_STALE;\
                                 (x)->pb_flags &= ~(PBF_DELWRI|PBF_COMPLETED)

#define XFS_BUF_DELAYWRITE(x)	 ((x)->pb_flags |= PBF_DELWRI)
#define XFS_BUF_UNDELAYWRITE(x)	 ((x)->pb_flags &= ~PBF_DELWRI)
#define XFS_BUF_ISDELAYWRITE(x)	 ((x)->pb_flags & PBF_DELWRI)


#define XFS_BUF_ERROR(x,no)      pagebuf_ioerror(x,no)
#define XFS_BUF_GETERROR(x)      pagebuf_geterror(x)
#define XFS_BUF_ISERROR(x)       (pagebuf_geterror(x)?1:0)

#define XFS_BUF_DONE(x)          ((x)->pb_flags |= PBF_COMPLETED)
#define XFS_BUF_UNDONE(x)    	 ((x)->pb_flags &= ~PBF_COMPLETED)
#define XFS_BUF_ISDONE(x)	     ((x)->pb_flags & PBF_COMPLETED)

#define XFS_BUF_BUSY(x)          
#define XFS_BUF_UNBUSY(x)    	 
#define XFS_BUF_ISBUSY(x)	     1 /* only checked in asserts... keep it from tripping any */

#define XFS_BUF_ASYNC(x)         ((x)->pb_flags |= PBF_ASYNC)
#define XFS_BUF_UNASYNC(x)     	 ((x)->pb_flags &= ~PBF_ASYNC)
#define XFS_BUF_ISASYNC(x)       ((x)->pb_flags & PBF_ASYNC)

#define XFS_BUF_SHUT(x)          /* error! not implemeneted yet */
#define XFS_BUF_UNSHUT(x)     	 /* error! not implemeneted yet */
#define XFS_BUF_ISSHUT(x)        (0)

#define XFS_BUF_HOLD(x)        /* error! not implemeneted yet */  
#define XFS_BUF_UNHOLD(x)       /* error! not implemeneted yet */
#define XFS_BUF_ISHOLD(x)       (1)
/* this may go away... calling iostart will define read or write */
/* used for irix at the moment not needed for linux? */
#define XFS_BUF_READ(x)         ((x)->pb_flags |= PBF_READ)
#define XFS_BUF_UNREAD(x)       ((x)->pb_flags &= ~PBF_READ)
#define XFS_BUF_ISREAD(x)       ((x)->pb_flags & PBF_READ)

#define XFS_BUF_WRITE(x)        ((x)->pb_flags |= PBF_WRITE)
#define XFS_BUF_UNWRITE(x)      ((x)->pb_flags &= ~PBF_WRITE)
#define XFS_BUF_ISWRITE(x)      ((x)->pb_flags & PBF_WRITE)

#define XFS_BUF_UNCACHED(x)      /* error! not implemeneted yet */
#define XFS_BUF_UNUNCACHED(x)    /* error! not implemeneted yet */
#define XFS_BUF_ISUNCACHED(x)    (0) 

#define XFS_BUF_ISUNINITIAL(x)   ((x)->pb_flags & PBF_UNINITIAL)
#define XFS_BUF_UNUNINITIAL(x)   ((x)->pb_flags &= ~PBF_UNINITIAL)

#define XFS_BUF_AGE(x)        /* error! not implemeneted yet */

#define XFS_BUF_BP_ISMAPPED(bp)  1 /* error! not implemeneted yet */
#define XFS_BUF_IS_IOSPL(bp)     1 /* error! not implemeneted yet */
#define XFS_BUF_IS_GRIO(bp)      1 /* error! not implemeneted yet */

/* hmm what does the mean on linux? may go away */
#define XFS_BUF_PAGEIO(x)        /* error! not implemeneted yet */

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
			}				\
			(buf)->pb_iodone = (func)
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
#define XFS_BUF_FSPRIVATE3(buf, type)		1

#define XFS_BUF_SET_FSPRIVATE3(buf, value)
#define XFS_BUF_SET_START(buf)

#define XFS_BUF_PTR(bp)		((bp)->pb_addr)
#define XFS_BUF_ADDR(bp)	((bp)->pb_file_offset >> 9)
#define XFS_BUF_SET_ADDR(bp, blk)		\
			((bp)->pb_file_offset = (blk) << 9)
#define XFS_BUF_COUNT(bp)	((bp)->pb_count_desired)
#define XFS_BUF_SET_COUNT(bp, cnt)		\
			((bp)->pb_count_desired = cnt)
#define XFS_BUF_SIZE(bp)	((bp)->pb_buffer_length)
#define XFS_BUF_SET_SIZE(bp, cnt)		\
			((bp)->pb_buffer_length = cnt)
#define XFS_BUF_SET_VTYPE_REF(bp, type, ref)
#define XFS_BUF_SET_VTYPE(bp, type)
#define XFS_BUF_SET_REF(bp, ref)

#define XFS_BUF_ISPINNED(bp) 0 /* ERROR implement me */ 

/* setup the buffer target from a buftarg structure */
#define XFS_BUF_SET_TARGET(bp, target) 
/* return the dev_t being used */
#define XFS_BUF_TARGET(bp)  1
#define XFS_BUF_SET_VTYPE_REF(bp, type, ref)	
#define XFS_BUF_SET_VTYPE(bp, type)
#define XFS_BUF_SET_REF(bp, ref)	

#define xfs_buf_read(target, blkno, len, flags) \
		pagebuf_get((target)->inode, (blkno) << 9, (len) << 9, \
				PBF_LOCK | PBF_READ)
#define xfs_buf_get(target, blkno, len, flags) \
		pagebuf_get((target)->inode, (blkno) << 9, (len) << 9, \
				PBF_LOCK)
#define xfs_bdwrite(mp, bp)			\
			bp->pb_flags |= PBF_DELWRI; \
			pagebuf_rele(bp)

#define xfs_bawrite(mp, bp) pagebuf_iostart(bp, PBF_WRITE | PBF_ASYNC)

#define xfs_buf_relse(bp)	\
			pagebuf_rele(bp)

#define xfs_bpin(bp)                 \
     pagebuf_pin(bp)

#define xfs_bunpin(bp)               \
     pagebuf_unpin(bp)
#define	xfs_bwait_unpin(bp)          \
     pagebuf_wait_unpin(bp)

#define xfs_bp_mapin(bp)             \
        pagebuf_mapin(bp)

#endif /* _USING_PAGEBUF_T */

#endif
