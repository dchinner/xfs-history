/*
 * Copyright (c) 2000 Silicon Graphics, Inc.  All Rights Reserved.
 * 
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 * 
 * This program is distributed in the hope that it would be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * 
 * Further, this software is distributed without any warranty that it is
 * free of the rightful claim of any third person regarding infringement
 * or the like.  Any license provided herein, whether implied or
 * otherwise, applies only to this software file.  Patent licenses, if
 * any, provided herein do not apply to combinations of this program with
 * other software, or any other product whatsoever.
 * 
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write the Free Software Foundation, Inc., 59
 * Temple Place - Suite 330, Boston MA 02111-1307, USA.
 * 
 * Contact information: Silicon Graphics, Inc., 1600 Amphitheatre Pkwy,
 * Mountain View, CA  94043, or:
 * 
 * http://www.sgi.com 
 * 
 * For further information regarding this notice, see: 
 * 
 * http://oss.sgi.com/projects/GenInfo/SGIGPLNoticeExplan/
 */
#ifndef __XFS_BUF_H__
#define __XFS_BUF_H__

#ifdef SIM
#include <sys/buf.h>
#include <linux/xfs_sema.h>
#include <sys/kmem.h>

typedef struct buf xfs_buf_t;
#define xfs_buf buf


/* These are just for xfs_syncsub... it sets an internal variable 
 * then passes it to VOP_FLUSH_PAGES or adds the flags to a newly gotten buf_t
 */
#define XFS_B_ASYNC  B_ASYNC
#define XFS_B_DELWRI B_DELWRI
#define XFS_B_READ   B_READ
#define XFS_B_WRITE   B_WRITE
#define XFS_B_STALE   B_STALE
#define XFS_BUF_TRYLOCK	BUF_TRYLOCK
#define XFS_INCORE_TRYLOCK	INCORE_TRYLOCK
#define XFS_BUF_LOCK	0
#define XFS_BUF_MAPPED	0

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
#define XFS_BUF_ISBUSY(x)	 ((x)->b_flags & B_BUSY)

#define XFS_BUF_ASYNC(x)         ((x)->b_flags |= B_ASYNC)
#define XFS_BUF_UNASYNC(x)     	 ((x)->b_flags &= ~B_ASYNC)
#define XFS_BUF_ISASYNC(x)       ((x)->b_flags & B_ASYNC)

#define XFS_BUF_SHUT(x)          ((x)->b_flags |= B_XFS_SHUT)
#define XFS_BUF_UNSHUT(x)     	 ((x)->b_flags &= ~B_XFS_SHUT)
#define XFS_BUF_ISSHUT(x)        ((x)->b_flags & B_XFS_SHUT)

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

#define XFS_BUF_SET_BRELSE_FUNC(buf, value) \
            (buf)->b_relse = value

#define XFS_BUF_ADDR(bp)	((bp)->b_blkno)
#define XFS_BUF_OFFSET(bp)	((bp)->b_offset)
#define XFS_BUF_SET_ADDR(bp, blk)		\
			((bp)->b_blkno = blk)
#define XFS_BUF_COUNT(bp)	((bp)->b_bcount)
#define XFS_BUF_SET_COUNT(bp, cnt)		\
			((bp)->b_bcount = cnt)
#define XFS_BUF_PTR(bp)	((bp)->b_un.b_addr)

#define xfs_buf_offset(bp, offset)	(XFS_BUF_PTR(bp) + (offset))

#define XFS_BUF_SET_PTR(bp, val, count)		\
			((bp)->b_un.b_addr = (val)); \
			XFS_BUF_SET_COUNT(bp, count)

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

#define XFS_BUF_VALUSEMA(bp) valusema(&bp->b_lock)
#define XFS_BUF_CPSEMA(bp)   cpsema(&bp->b_lock)
#define XFS_BUF_PSEMA(bp,x)  psema(&bp->b_lock,x) 
#define XFS_BUF_VSEMA(bp)    vsema(&bp->b_lock)
#define XFS_BUF_V_IODONESEMA(bp) vsema(&bp->b_iodonesema)

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

#define xfs_xfsd_list_evict(x)       \
        _xfs_xfsd_list_evict(x) 

#define xfs_buftrace(x,y)            \
            buftrace(x,y)

#define xfs_biodone(bp)             \
            biodone(bp)

#define xfs_incore(buftarg,blkno,len,lockit) \
            incore(buftarg.dev,blkno,len,lockit)

/* already a function xfs_bwrite... fix this */
#define XFS_bwrite(bp)              \
            bwrite(bp)

#define XFS_bdwrite(bp)              \
            bdwrite(bp)

#define xfs_iowait(bp)              \
            iowait(bp)   

#define xfs_binval(buftarg)             \
            binval(buftarg.dev)

#define XFS_bflush(buftarg)           \
            bflush(buftarg.dev) 

#define xfs_incore_relse(buftarg,delwri_only,wait) \
            incore_relse(buftarg.dev,delwri_only,wait)

#define xfs_incore_match(buftarg,blkno,len,field,value) \
            incore_match(buftarg.dev,blkno,len,field,value)

#define xfs_baread(target, rablkno, ralen) \
            baread(target, rablkno, ralen)

extern void pdflush(struct vnode *, uint64_t);
#define XFS_pdflush(vnode,flags) \
            pdflush(vnode,flags)

/* mp isn't needed for irix just pagebuf at this point */
#define XFS_getrbuf(sleep,mp)\
            getrbuf(sleep)

#define XFS_ngetrbuf(len,mp)\
            ngetrbuf(len)

#define XFS_freerbuf(bp) \
            freerbuf(bp)

#define XFS_nfreerbuf(bp) \
		    nfreerbuf(bp)

#define xfs_trigger_io()

#endif /* SIM */

#ifndef SIM
#include <linux/xfs_sema.h>
#include <sys/kmem.h>
#include <linux/page_buf.h>

/* These are just for xfs_syncsub... it sets an internal variable 
 * then passes it to VOP_FLUSH_PAGES or adds the flags to a newly gotten buf_t
 */
#define XFS_B_ASYNC  PBF_ASYNC
#define XFS_B_DELWRI PBF_DELWRI
#define XFS_B_READ   PBF_READ
#define XFS_B_WRITE  PBF_WRITE
#define XFS_B_STALE (1 << 31)
#define XFS_BUF_TRYLOCK		PBF_TRYLOCK
#define XFS_INCORE_TRYLOCK	PBF_TRYLOCK
#define XFS_BUF_LOCK		PBF_LOCK
#define XFS_BUF_MAPPED		PBF_MAPPED

#define BUF_BUSY	0

#define XFS_BUF_BFLAGS(x)        ((x)->pb_flags)  /* debugging routines might need this */
#define XFS_BUF_ZEROFLAGS(x)	\
	((x)->pb_flags &= ~(PBF_READ|PBF_WRITE|PBF_ASYNC|PBF_SYNC|PBF_DELWRI))

#define XFS_BUF_STALE(x)	     ((x)->pb_flags |= XFS_B_STALE)
#define XFS_BUF_UNSTALE(x)	     ((x)->pb_flags &= ~XFS_B_STALE)
#define XFS_BUF_ISSTALE(x)	     ((x)->pb_flags & XFS_B_STALE)
#define XFS_BUF_SUPER_STALE(x)   (x)->pb_flags |= XFS_B_STALE;\
				 xfs_buf_undelay(x);\
                                 (x)->pb_flags &= ~(PBF_PARTIAL|PBF_NONE)

static inline void xfs_buf_undelay(page_buf_t *pb)
{
	if (pb->pb_list.next != &pb->pb_list) {
		pagebuf_delwri_dequeue(pb);

		/*
		 * Usually the hold count would be at least 2
		 * here, with the irritating exception of
		 * the superblock case.
		 */
		if (pb->pb_hold > 1 )
			pagebuf_rele(pb);

	} else {
		pb->pb_flags &= ~PBF_DELWRI;
	}
}

#define XFS_BUF_DELAYWRITE(x)	 ((x)->pb_flags |= PBF_DELWRI)
#define XFS_BUF_UNDELAYWRITE(x)	 xfs_buf_undelay(x)
#define XFS_BUF_ISDELAYWRITE(x)	 ((x)->pb_flags & PBF_DELWRI)


#define XFS_BUF_ERROR(x,no)      pagebuf_ioerror(x,no)
#define XFS_BUF_GETERROR(x)      pagebuf_geterror(x)
#define XFS_BUF_ISERROR(x)       (pagebuf_geterror(x)?1:0)

#define XFS_BUF_DONE(x)          ((x)->pb_flags &= ~(PBF_PARTIAL|PBF_NONE))
#define XFS_BUF_UNDONE(x)    	 ((x)->pb_flags |= PBF_PARTIAL|PBF_NONE)
#define XFS_BUF_ISDONE(x)	 (!(PBF_NOT_DONE(x)))

#define XFS_BUF_BUSY(x)          ((x)->pb_flags |= PBF_FORCEIO)
#define XFS_BUF_UNBUSY(x)    	 ((x)->pb_flags &= ~PBF_FORCEIO)
#define XFS_BUF_ISBUSY(x)	 (1)

#define XFS_BUF_ASYNC(x)         ((x)->pb_flags |= PBF_ASYNC)
#define XFS_BUF_UNASYNC(x)     	 ((x)->pb_flags &= ~PBF_ASYNC)
#define XFS_BUF_ISASYNC(x)       ((x)->pb_flags & PBF_ASYNC)

#define XFS_BUF_SHUT(x)          printk("XFS_BUF_SHUT not implemented yet\n") 
#define XFS_BUF_UNSHUT(x)     	 printk("XFS_BUF_UNSHUT not implemented yet\n")
#define XFS_BUF_ISSHUT(x)        (0)


#define XFS_BUF_HOLD(x)		pagebuf_hold(x)  
/*#define XFS_BUF_UNHOLD(x)       printk("XFS_BUF_UNHOLD not implemented yet\n")
  #define XFS_BUF_ISHOLD(x)       (1)
*/
/* this may go away... calling iostart will define read or write */
/* used for irix at the moment not needed for linux? */
#define XFS_BUF_READ(x)         ((x)->pb_flags |= PBF_READ)
#define XFS_BUF_UNREAD(x)       ((x)->pb_flags &= ~PBF_READ)
#define XFS_BUF_ISREAD(x)       ((x)->pb_flags & PBF_READ)

#define XFS_BUF_WRITE(x)        ((x)->pb_flags |= PBF_WRITE)
#define XFS_BUF_UNWRITE(x)      ((x)->pb_flags &= ~PBF_WRITE)
#define XFS_BUF_ISWRITE(x)      ((x)->pb_flags & PBF_WRITE)

#define XFS_BUF_UNCACHED(x)      printk("XFS_BUF_UNCACHED not implemented yet\n")
#define XFS_BUF_UNUNCACHED(x)    printk("XFS_BUF_UNUNCACHED not implemented yet\n")
#define XFS_BUF_ISUNCACHED(x)    (0) 

#define XFS_BUF_ISUNINITIAL(x)   ((x)->pb_flags & PBF_UNINITIAL)
#define XFS_BUF_UNUNINITIAL(x)   ((x)->pb_flags &= ~PBF_UNINITIAL)

#define XFS_BUF_AGE(x)           printk("XFS_BUF_AGE not implemented yet\n")

#define XFS_BUF_BP_ISMAPPED(bp)  1
#define XFS_BUF_IS_IOSPL(bp)     xfs_buf_iospl() 
#define XFS_BUF_IS_GRIO(bp)      ((bp)->pb_flags & PBF_GRIO)

/* hmm what does the mean on linux? may go away */
#define XFS_BUF_PAGEIO(x)        printk("XFS_BUF_PAGEIO not implemented yet\n") 
/*
 * Flags for incore_match() and findchunk_match().
 */
#define BUF_FSPRIV      0x1
#define BUF_FSPRIV2     0x2  

typedef struct page_buf_s xfs_buf_t;
#define xfs_buf page_buf_s

struct inode;

typedef struct buftarg {
	struct inode	*inode;
	dev_t		dev;
} buftarg_t;

typedef struct bfidev {
  /* this is for CXFS server relocation... probably won't need this for a while 01/06/2000 RMC */
} bfidev_t; 

struct xfs_mount;

extern page_buf_t *xfs_pb_getr(int, struct xfs_mount *);
extern page_buf_t *xfs_pb_getr(int, struct xfs_mount *);
extern void xfs_pb_freer(page_buf_t *);
extern void xfs_pb_nfreer(page_buf_t *);

#define XFS_BUF_IODONE_FUNC(buf)	(buf)->pb_iodone
#define XFS_BUF_SET_IODONE_FUNC(buf, func)	\
			pagebuf_set_iodone(buf, func)
#define XFS_BUF_CLR_IODONE_FUNC(buf)		\
			pagebuf_set_iodone(buf, NULL)
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
#define XFS_BUF_FSPRIVATE3(buf, type)	NULL	

#define XFS_BUF_SET_FSPRIVATE3(buf, value)
#define XFS_BUF_SET_START(buf)

#define XFS_BUF_SET_BRELSE_FUNC(buf, value) \
			(buf)->pb_relse = (value)


/* this may need to be linux caddr_t, with the define is was irix_caddr_
 * so lets convert it to xfs_caddr_t for now
 */

#define XFS_BUF_PTR(bp)		(xfs_caddr_t)((bp)->pb_addr)

extern inline xfs_caddr_t xfs_buf_offset(page_buf_t *bp, off_t offset)
{
	if (bp->pb_flags & PBF_MAPPED) return XFS_BUF_PTR(bp) + offset;

	return (xfs_caddr_t) pagebuf_offset(bp, offset);
}

#define XFS_BUF_SET_PTR(bp, val, count)		\
				pagebuf_associate_memory(bp, val, count)
#define XFS_BUF_ADDR(bp)	((bp)->pb_bn)
#define XFS_BUF_OFFSET(bp)	((bp)->pb_file_offset >> 9)
#define XFS_BUF_SET_ADDR(bp, blk)		\
			((bp)->pb_bn = (page_buf_daddr_t)(blk))
#define XFS_BUF_COUNT(bp)	((bp)->pb_count_desired)
#define XFS_BUF_SET_COUNT(bp, cnt)		\
			((bp)->pb_count_desired = cnt)
#define XFS_BUF_SIZE(bp)	((bp)->pb_buffer_length)
#define XFS_BUF_SET_SIZE(bp, cnt)		\
			((bp)->pb_buffer_length = cnt)
#define XFS_BUF_SET_VTYPE_REF(bp, type, ref)
#define XFS_BUF_SET_VTYPE(bp, type)
#define XFS_BUF_SET_REF(bp, ref)

#define XFS_BUF_ISPINNED(bp)   pagebuf_ispin(bp)

#define XFS_BUF_VALUSEMA(bp) 	pagebuf_lock_value(bp)
#define XFS_BUF_CPSEMA(bp)   	(pagebuf_cond_lock(bp) == 0)
#define XFS_BUF_VSEMA(bp)   	pagebuf_unlock(bp)
#define XFS_BUF_PSEMA(bp,x)	pagebuf_lock(bp)
#define XFS_BUF_V_IODONESEMA(bp) 

/* setup the buffer target from a buftarg structure */
#define XFS_BUF_SET_TARGET(bp, target) (bp)->pb_target = (target)->inode;
/* return the dev_t being used */
extern dev_t	XFS_pb_target(page_buf_t *);

#define XFS_BUF_TARGET(bp)  XFS_pb_target(bp)
#define XFS_BUF_SET_VTYPE_REF(bp, type, ref)	
#define XFS_BUF_SET_VTYPE(bp, type)
#define XFS_BUF_SET_REF(bp, ref)	

#define xfs_buf_read(target, blkno, len, flags) \
		pagebuf_get((target)->inode, (blkno) << 9, (len) << 9, \
			PBF_LOCK | PBF_READ | PBF_MAPPED | PBF_MAPPABLE)
#define xfs_buf_get(target, blkno, len, flags) \
		pagebuf_get((target)->inode, (blkno) << 9, (len) << 9, \
			PBF_LOCK | PBF_MAPPED | PBF_MAPPABLE)

#define xfs_buf_read_flags(target, blkno, len, flags) \
		pagebuf_get((target)->inode, (blkno) << 9, (len) << 9, \
			PBF_READ | PBF_MAPPABLE | flags)
#define xfs_buf_get_flags(target, blkno, len, flags) \
		pagebuf_get((target)->inode, (blkno) << 9, (len) << 9, \
			PBF_MAPPABLE | flags)

static inline int	xfs_bawrite(void *mp, page_buf_t *bp)
{
	xfs_buf_undelay(bp);
	return pagebuf_iostart(bp, PBF_WRITE | PBF_ASYNC);
}

static inline void	xfs_buf_relse(page_buf_t *bp)
{
	if (bp->pb_relse == NULL)
		pagebuf_unlock(bp);

	pagebuf_rele(bp);
}


#define xfs_bpin(bp)            \
			pagebuf_pin(bp)

#define xfs_bunpin(bp)          \
			pagebuf_unpin(bp)

#define	xfs_bwait_unpin(bp)     \
			pagebuf_wait_unpin(bp)

#define xfs_bp_mapin(bp)             \
			pagebuf_mapin(bp)

#define xfs_xfsd_list_evict(x)        {  /* printk("xfs_xfsd_list_evict not implemented\n"); */ }

#ifdef PAGEBUF_TRACE
#define PB_DEFINE_TRACES
#include <linux/page_buf_trace.h>

#define xfs_buftrace(id, bp)	PB_TRACE(bp, (void *)id)
#else
#define xfs_buftrace(id, bp)
#endif


#define xfs_biodone(pb)             \
            pagebuf_iodone(pb)

#define xfs_incore(buftarg,blkno,len,lockit) \
            pagebuf_find(buftarg.inode,blkno<<9,len<<9,lockit)

void xfs_trigger_io(void);


static inline int	XFS_bwrite(page_buf_t *pb)
{
	int	sync = (pb->pb_flags & PBF_ASYNC) == 0;
	int	error;

	pb->pb_flags |= PBF_SYNC;

	xfs_buf_undelay(pb);

	pagebuf_iorequest(pb);

	if (sync) {
		error = pagebuf_iowait(pb);
		xfs_buf_relse(pb);
	} else {
		xfs_trigger_io();
		error = 0;
	}

	return error;
}


#define XFS_bdwrite(pb)              \
            pagebuf_iostart(pb, PBF_DELWRI | PBF_ASYNC)

#define xfs_bdwrite(mp, bp)	XFS_bdwrite(bp)

#define xfs_iowait(pb)              \
	    pagebuf_iowait(pb)

#define xfs_binval(buftarg) /* NOT used with pagebufs... do nothing */

extern void XFS_bflush(buftarg_t);

#define xfs_incore_relse(buftarg,delwri_only,wait)	\
       _xfs_incore_relse(buftarg,delwri_only,wait)
extern int _xfs_incore_relse(buftarg_t *, int, int);
/*
 * Go through all incore buffers, and release buffers
 * if they belong to the given device. This is used in
 * filesystem error handling to preserve the consistency
 * of its metadata.
 */
#define xfs_incore_match(buftarg,blkno,len,field,value) _xfs_incore_match(buftarg,blkno,len,field,value) 

#define xfs_baread(target, rablkno, ralen)  \
		pagebuf_get((target)->inode, (rablkno) << 9, (ralen) << 9, \
				PBF_TRYLOCK | PBF_READ | PBF_ASYNC)
	

#define XFS_pdflush(vnode,flags) printk("XFS_pdflush not implemeneted\n")

#define chunkread(vnode,bmap,nmaps,cred) printk("chunkread... whaa shouldn't be here at all\n")
#define getchunk(vnode,bmap,cred) printk("chunkread... whaa shouldn't be here at all\n")

struct xfs_mount;

page_buf_t * xfs_pb_getr(int sleep, struct xfs_mount *mp);
page_buf_t * xfs_pb_ngetr(int len, struct xfs_mount *mp); 
void xfs_pb_freer(page_buf_t *bp);
void xfs_pb_nfreer(page_buf_t *bp);


#define XFS_getrbuf(sleep,mp) xfs_pb_getr(sleep,mp)

#define XFS_ngetrbuf(len,mp) xfs_pb_ngetr(len,mp) 

#define XFS_freerbuf(bp)  xfs_pb_freer(bp) 

#define XFS_nfreerbuf(bp) xfs_pb_nfreer(bp)


#endif /* !SIM */

#endif
