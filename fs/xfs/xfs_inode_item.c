#ident "$Revision: 1.49 $"

/*
 * This file contains the implementation of the xfs_inode_log_item.
 * It contains the item operations used to manipulate the inode log
 * items as well as utility routines used by the inode specific
 * transaction routines.
 */


#ifdef SIM
#define _KERNEL 1
#endif
#include <sys/param.h>
#include <sys/buf.h>
#include <sys/vnode.h>
#include <sys/grio.h>
#include <sys/user.h>
#ifdef SIM
#undef _KERNEL
#endif
#include <sys/debug.h>
#ifndef SIM
#include <sys/systm.h>
#else
#include <bstring.h>
#endif
#include <sys/kmem.h>
#include "xfs_types.h"
#include "xfs_inum.h"
#include "xfs_log.h"
#include "xfs_trans.h"
#include "xfs_buf_item.h"
#include "xfs_sb.h"
#include "xfs_mount.h"
#include "xfs_trans_priv.h"
#include "xfs_ag.h"
#include "xfs_alloc_btree.h"
#include "xfs_bmap_btree.h"
#include "xfs_ialloc_btree.h"
#include "xfs_btree.h"
#include "xfs_ialloc.h"
#include "xfs_attr_sf.h"
#include "xfs_dir_sf.h"
#include "xfs_dinode.h"
#include "xfs_inode_item.h"
#include "xfs_inode.h"
#ifdef SIM
#include "sim.h"
#endif


/*
 * This returns the number of iovecs needed to log the given inode item.
 *
 * We need one iovec for the inode log format structure, one for the
 * inode core, and possibly one for the inode data/extents/b-tree root
 * and one for the inode attribute data/extents/b-tree root.
 */
uint
xfs_inode_item_size(
	xfs_inode_log_item_t	*iip)
{
	uint		nvecs;
	xfs_inode_t	*ip;

	ip = iip->ili_inode;
	nvecs = 2;

	/*
	 * Only log the data/extents/b-tree root if there is something
	 * left to log.
	 */
	iip->ili_format.ilf_fields |= XFS_ILOG_CORE;

	switch (ip->i_d.di_format) {
	case XFS_DINODE_FMT_EXTENTS:
		iip->ili_format.ilf_fields &=
			~(XFS_ILOG_DDATA | XFS_ILOG_DBROOT |
			  XFS_ILOG_DEV | XFS_ILOG_UUID);
		if ((iip->ili_format.ilf_fields & XFS_ILOG_DEXT) &&
		    (ip->i_d.di_nextents > 0) &&
		    (ip->i_df.if_bytes > 0)) {
			ASSERT(ip->i_df.if_u1.if_extents != NULL);
			nvecs++;
		} else {
			iip->ili_format.ilf_fields &= ~XFS_ILOG_DEXT;
		}
		break;

	case XFS_DINODE_FMT_BTREE:
		iip->ili_format.ilf_fields &=
			~(XFS_ILOG_DDATA | XFS_ILOG_DEXT |
			  XFS_ILOG_DEV | XFS_ILOG_UUID);
		if ((iip->ili_format.ilf_fields & XFS_ILOG_DBROOT) &&
		    (ip->i_df.if_broot_bytes > 0)) {
			ASSERT(ip->i_df.if_broot != NULL);
			nvecs++;
		} else {
			iip->ili_format.ilf_fields &= ~XFS_ILOG_DBROOT;
		}
		break;

	case XFS_DINODE_FMT_LOCAL:
		iip->ili_format.ilf_fields &=
			~(XFS_ILOG_DEXT | XFS_ILOG_DBROOT |
			  XFS_ILOG_DEV | XFS_ILOG_UUID);
		if ((iip->ili_format.ilf_fields & XFS_ILOG_DDATA) &&
		    (ip->i_df.if_bytes > 0)) {
			ASSERT(ip->i_df.if_u1.if_data != NULL);
			ASSERT(ip->i_d.di_size > 0);
			nvecs++;
		} else {
			iip->ili_format.ilf_fields &= ~XFS_ILOG_DDATA;
		}
		break;

	case XFS_DINODE_FMT_DEV:
		iip->ili_format.ilf_fields &=
			~(XFS_ILOG_DDATA | XFS_ILOG_DBROOT |
			  XFS_ILOG_DEXT | XFS_ILOG_UUID);
		break;

	case XFS_DINODE_FMT_UUID:
		iip->ili_format.ilf_fields &=
			~(XFS_ILOG_DDATA | XFS_ILOG_DBROOT |
			  XFS_ILOG_DEXT | XFS_ILOG_DEV);
		break;

	default:
		ASSERT(0);
		break;
	}

	/*
	 * If there are no attributes associated with this file,
	 * then there cannot be anything more to log.
	 */
	if (!XFS_IFORK_Q(ip)) {
		return nvecs;
	}

	/*
	 * Log any necessary attribute data.
	 */
	switch (ip->i_d.di_aformat) {
	case XFS_DINODE_FMT_EXTENTS:
		iip->ili_format.ilf_fields &=
			~(XFS_ILOG_ADATA | XFS_ILOG_ABROOT);
		if ((iip->ili_format.ilf_fields & XFS_ILOG_AEXT) &&
		    (ip->i_d.di_anextents > 0) &&
		    (ip->i_afp->if_bytes > 0)) {
			ASSERT(ip->i_afp->if_u1.if_extents != NULL);
			nvecs++;
		} else {
			iip->ili_format.ilf_fields &= ~XFS_ILOG_AEXT;
		}
		break;

	case XFS_DINODE_FMT_BTREE:
		iip->ili_format.ilf_fields &=
			~(XFS_ILOG_ADATA | XFS_ILOG_AEXT);
		if ((iip->ili_format.ilf_fields & XFS_ILOG_ABROOT) &&
		    (ip->i_afp->if_broot_bytes > 0)) {
			ASSERT(ip->i_afp->if_broot != NULL);
			nvecs++;
		} else {
			iip->ili_format.ilf_fields &= ~XFS_ILOG_ABROOT;
		}
		break;

	case XFS_DINODE_FMT_LOCAL:
		iip->ili_format.ilf_fields &=
			~(XFS_ILOG_AEXT | XFS_ILOG_ABROOT);
		if ((iip->ili_format.ilf_fields & XFS_ILOG_ADATA) &&
		    (ip->i_afp->if_bytes > 0)) {
			ASSERT(ip->i_afp->if_u1.if_data != NULL);
			nvecs++;
		} else {
			iip->ili_format.ilf_fields &= ~XFS_ILOG_ADATA;
		}
		break;

	default:
		ASSERT(0);
		break;
	}

	return nvecs; 
}

/*
 * This is called to fill in the vector of log iovecs for the
 * given inode log item.  It fills the first item with an inode
 * log format structure, the second with the on-disk inode structure,
 * and a possible third and/or fourth with the inode data/extents/b-tree
 * root and inode attributes data/extents/b-tree root.
 */
void
xfs_inode_item_format(
	xfs_inode_log_item_t	*iip,
	xfs_log_iovec_t		*log_vector)
{
	uint			nvecs;
	xfs_log_iovec_t		*vecp;
	xfs_inode_t		*ip;
	size_t			data_bytes;
	xfs_bmbt_rec_32_t	*ext_buffer;
	int			nrecs;

	ip = iip->ili_inode;
	vecp = log_vector;

	vecp->i_addr = (caddr_t)&iip->ili_format;
	vecp->i_len  = sizeof(xfs_inode_log_format_t);
	vecp++;
	nvecs	     = 1;

	vecp->i_addr = (caddr_t)&ip->i_d;
	vecp->i_len  = sizeof(xfs_dinode_core_t);
	vecp++;
	nvecs++;
	iip->ili_format.ilf_fields |= XFS_ILOG_CORE;

	switch (ip->i_d.di_format) {
	case XFS_DINODE_FMT_EXTENTS:
		ASSERT(!(iip->ili_format.ilf_fields &
			 (XFS_ILOG_DDATA | XFS_ILOG_DBROOT |
			  XFS_ILOG_DEV | XFS_ILOG_UUID)));
		if (iip->ili_format.ilf_fields & XFS_ILOG_DEXT) {
			ASSERT(ip->i_df.if_bytes > 0);
			ASSERT(ip->i_df.if_u1.if_extents != NULL);
			ASSERT(ip->i_d.di_nextents > 0);
			ASSERT(iip->ili_extents_buf == NULL);
			nrecs = ip->i_df.if_bytes / sizeof(xfs_bmbt_rec_t);
			ASSERT(nrecs > 0);
			if (nrecs == ip->i_d.di_nextents) {
				/*
				 * There are no delayed allocation
				 * extents, so just point to the
				 * real extents array.
				 */
				vecp->i_addr =
					(char *)(ip->i_df.if_u1.if_extents);
				vecp->i_len = ip->i_df.if_bytes;
			} else {
				/*
				 * There are delayed allocation extents
				 * in the inode.  Use xfs_iextents_copy()
				 * to copy only the real extents into
				 * a separate buffer.  We'll free the
				 * buffer in the unlock routine.
				 */
				ext_buffer = kmem_alloc(ip->i_df.if_bytes,
					KM_SLEEP);
				iip->ili_extents_buf = ext_buffer;
				vecp->i_addr = (caddr_t)ext_buffer;
				vecp->i_len = xfs_iextents_copy(ip, ext_buffer,
					XFS_DATA_FORK);
			}
			ASSERT(vecp->i_len <= ip->i_df.if_bytes);
			iip->ili_format.ilf_dsize = vecp->i_len;
			vecp++;
			nvecs++;
		}
		break;

	case XFS_DINODE_FMT_BTREE:
		ASSERT(!(iip->ili_format.ilf_fields &
			 (XFS_ILOG_DDATA | XFS_ILOG_DEXT |
			  XFS_ILOG_DEV | XFS_ILOG_UUID)));
		if (iip->ili_format.ilf_fields & XFS_ILOG_DBROOT) {
			ASSERT(ip->i_df.if_broot_bytes > 0);
			ASSERT(ip->i_df.if_broot != NULL);
			vecp->i_addr = (caddr_t)ip->i_df.if_broot;
			vecp->i_len = ip->i_df.if_broot_bytes;
			vecp++;
			nvecs++;
			iip->ili_format.ilf_dsize = ip->i_df.if_broot_bytes;
		}
		break;

	case XFS_DINODE_FMT_LOCAL:
		ASSERT(!(iip->ili_format.ilf_fields &
			 (XFS_ILOG_DBROOT | XFS_ILOG_DEXT |
			  XFS_ILOG_DEV | XFS_ILOG_UUID)));
		if (iip->ili_format.ilf_fields & XFS_ILOG_DDATA) {
			ASSERT(ip->i_df.if_bytes > 0);
			ASSERT(ip->i_df.if_u1.if_data != NULL);
			ASSERT(ip->i_d.di_size > 0);

			vecp->i_addr = (caddr_t)ip->i_df.if_u1.if_data;
			/*
			 * Round i_bytes up to a word boundary.
			 * The underlying memory is guaranteed to
			 * to be there by xfs_idata_realloc().
			 */
			data_bytes = roundup(ip->i_df.if_bytes, 4);
			ASSERT((ip->i_df.if_real_bytes == 0) ||
			       (ip->i_df.if_real_bytes == data_bytes));
			vecp->i_len = data_bytes;
			vecp++;
			nvecs++;
			iip->ili_format.ilf_dsize = data_bytes;
		}
		break;

	case XFS_DINODE_FMT_DEV:
		ASSERT(!(iip->ili_format.ilf_fields &
			 (XFS_ILOG_DBROOT | XFS_ILOG_DEXT |
			  XFS_ILOG_DDATA | XFS_ILOG_UUID)));
		if (iip->ili_format.ilf_fields & XFS_ILOG_DEV) {
			iip->ili_format.ilf_u.ilfu_rdev =
				ip->i_df.if_u2.if_rdev;
		}
		break;

	case XFS_DINODE_FMT_UUID:
		ASSERT(!(iip->ili_format.ilf_fields &
			 (XFS_ILOG_DBROOT | XFS_ILOG_DEXT |
			  XFS_ILOG_DDATA | XFS_ILOG_DEV)));
		if (iip->ili_format.ilf_fields & XFS_ILOG_UUID) {
			iip->ili_format.ilf_u.ilfu_uuid =
				ip->i_df.if_u2.if_uuid;
		}
		break;

	default:
		ASSERT(0);
		break;
	}

	/*
	 * If there are no attributes associated with the file,
	 * then we're done.
	 */
	if (!XFS_IFORK_Q(ip)) {
		ASSERT(nvecs == iip->ili_item.li_desc->lid_size);
		iip->ili_format.ilf_size = nvecs;
		return;
	}

	switch (ip->i_d.di_aformat) {
	case XFS_DINODE_FMT_EXTENTS:
		ASSERT(!(iip->ili_format.ilf_fields &
			 (XFS_ILOG_ADATA | XFS_ILOG_ABROOT)));
		if (iip->ili_format.ilf_fields & XFS_ILOG_AEXT) {
			ASSERT(ip->i_afp->if_bytes > 0);
			ASSERT(ip->i_afp->if_u1.if_extents != NULL);
			ASSERT(ip->i_d.di_anextents > 0);
#ifdef DEBUG
			nrecs = ip->i_afp->if_bytes / sizeof(xfs_bmbt_rec_t);
#endif
			ASSERT(nrecs > 0);
			ASSERT(nrecs == ip->i_d.di_anextents);
			/*
			 * There are not delayed allocation extents
			 * for attributes, so just point at the array.
			 */
			vecp->i_addr = (char *)(ip->i_afp->if_u1.if_extents);
			vecp->i_len = ip->i_afp->if_bytes;
			iip->ili_format.ilf_asize = vecp->i_len;
			vecp++;
			nvecs++;
		}
		break;

	case XFS_DINODE_FMT_BTREE:
		ASSERT(!(iip->ili_format.ilf_fields &
			 (XFS_ILOG_ADATA | XFS_ILOG_AEXT)));
		if (iip->ili_format.ilf_fields & XFS_ILOG_ABROOT) {
			ASSERT(ip->i_afp->if_broot_bytes > 0);
			ASSERT(ip->i_afp->if_broot != NULL);
			vecp->i_addr = (caddr_t)ip->i_afp->if_broot;
			vecp->i_len = ip->i_afp->if_broot_bytes;
			vecp++;
			nvecs++;
			iip->ili_format.ilf_asize = ip->i_afp->if_broot_bytes;
		}
		break;

	case XFS_DINODE_FMT_LOCAL:
		ASSERT(!(iip->ili_format.ilf_fields &
			 (XFS_ILOG_ABROOT | XFS_ILOG_AEXT)));
		if (iip->ili_format.ilf_fields & XFS_ILOG_ADATA) {
			ASSERT(ip->i_afp->if_bytes > 0);
			ASSERT(ip->i_afp->if_u1.if_data != NULL);

			vecp->i_addr = (caddr_t)ip->i_afp->if_u1.if_data;
			/*
			 * Round i_bytes up to a word boundary.
			 * The underlying memory is guaranteed to
			 * to be there by xfs_idata_realloc().
			 */
			data_bytes = roundup(ip->i_afp->if_bytes, 4);
			ASSERT((ip->i_afp->if_real_bytes == 0) ||
			       (ip->i_afp->if_real_bytes == data_bytes));
			vecp->i_len = data_bytes;
			vecp++;
			nvecs++;
			iip->ili_format.ilf_asize = data_bytes;
		}
		break;

	default:
		ASSERT(0);
		break;
	}

	ASSERT(nvecs == iip->ili_item.li_desc->lid_size);
	iip->ili_format.ilf_size = nvecs;
}
	

/*
 * This is called to pin the inode associated with the inode log
 * item in memory so it cannot be written out.  Do this by calling
 * xfs_ipin() to bump the pin count in the inode while holding the
 * inode pin lock.
 */
void
xfs_inode_item_pin(
	xfs_inode_log_item_t	*iip)
{
	ASSERT(ismrlocked(&(iip->ili_inode->i_lock), MR_UPDATE));
	xfs_ipin(iip->ili_inode);
}


/*
 * This is called to unpin the inode associated with the inode log
 * item which was previously pinned with a call to xfs_inode_item_pin().
 * Just call xfs_iunpin() on the inode to do this.
 */
void
xfs_inode_item_unpin(
	xfs_inode_log_item_t	*iip)
{
	xfs_iunpin(iip->ili_inode);
}

/*
 * This is called to attempt to lock the inode associated with this
 * inode log item.  Don't sleep on the inode lock or the flush lock.
 * If the flush lock is already held, indicating that the inode has
 * been or is in the process of being flushed, then see if we can
 * find the inode's buffer in the buffer cache without sleeping.  If
 * we can and it is marked delayed write, then we want to send it out.
 * We delay doing so until the push routine, though, to avoid sleeping
 * in any device strategy routines.
 */
uint
xfs_inode_item_trylock(
	xfs_inode_log_item_t	*iip)
{
	register xfs_inode_t	*ip;
	xfs_mount_t		*mp;
	buf_t			*bp;
	int			flushed;

	ip = iip->ili_inode;

	if (ip->i_pincount > 0) {
		return XFS_ITEM_PINNED;
	}

	if (!xfs_ilock_nowait(ip, XFS_ILOCK_SHARED)) {
		return XFS_ITEM_LOCKED;
	}

	if (!xfs_iflock_nowait(ip)) {
		/*
		 * The inode is already being flushed.  It may have been
		 * flushed delayed write, however, and we don't want to
		 * get stuck waiting for that to complete.  So, we check
		 * to see if we can lock the inode's buffer without sleeping.
		 * If we can and it is marked for delayed write, then we
		 * hold it and send it out from the push routine.  We don't
		 * want to do that now since we might sleep in the device
		 * strategy routine.  Make sure to only return success
		 * if we set iip->ili_bp ourselves.  If someone else is
		 * doing it then we don't want to go to the push routine
		 * and duplicate their efforts.
		 */
		flushed = 0;
		if (iip->ili_bp == NULL) {
			mp = ip->i_mount;
			bp = incore(mp->m_dev, ip->i_blkno,
				    ip->i_len, INCORE_TRYLOCK);
			if (bp != NULL) {
				if (bp->b_flags & B_DELWRI) {
					iip->ili_bp = bp;
					iip->ili_bp_owner = u.u_procp;
					flushed = 1;
				} else {
					brelse(bp);
				}
			}
		}
		/*
		 * Make sure to set the no notify flag so iunlock
		 * doesn't call back into the AIL code on the unlock.
		 * That would double trip on the AIL lock.
		 */
		xfs_iunlock(ip, (XFS_ILOCK_SHARED | XFS_IUNLOCK_NONOTIFY));
		if (flushed) {
			return XFS_ITEM_SUCCESS;
		} else {
			return XFS_ITEM_FLUSHING;
		}
	}

	return XFS_ITEM_SUCCESS;
}

/*
 * Unlock the inode associated with the inode log item.
 * Clear the fields of the inode and inode log item that
 * are specific to the current transaction.  If the
 * hold flags is set, do not unlock the inode.  
 */
void
xfs_inode_item_unlock(
	xfs_inode_log_item_t	*iip)
{
	uint		hold;
	uint		iolocked;
	uint		lock_flags;
	xfs_inode_t	*ip;

	ASSERT(ismrlocked(&(iip->ili_inode->i_lock), MR_UPDATE));
	ASSERT((!(iip->ili_inode->i_item.ili_flags &
		  XFS_ILI_IOLOCKED_EXCL)) ||
	       ismrlocked(&(iip->ili_inode->i_iolock), MR_UPDATE));
	ASSERT((!(iip->ili_inode->i_item.ili_flags &
		  XFS_ILI_IOLOCKED_SHARED)) ||
	       ismrlocked(&(iip->ili_inode->i_iolock), MR_ACCESS));
	/*
	 * Clear the transaction pointer in the inode.
	 */
	ip = iip->ili_inode;
	ip->i_transp = NULL;

	/*
	 * If the inode needed a separate buffer with which to log
	 * its extents, then free it now.
	 */
	/* FIXME */
	if (iip->ili_extents_buf != NULL) {
		ASSERT(ip->i_d.di_format == XFS_DINODE_FMT_EXTENTS);
		ASSERT(ip->i_d.di_nextents > 0);
		ASSERT(iip->ili_format.ilf_fields & XFS_ILOG_DEXT);
		ASSERT(ip->i_df.if_bytes > 0);
		kmem_free(iip->ili_extents_buf, ip->i_df.if_bytes);
		iip->ili_extents_buf = NULL;
	}

	/*
	 * Figure out if we should unlock the inode or not.
	 */
	hold = iip->ili_flags & XFS_ILI_HOLD;

	/*
	 * Before clearing out the flags, remember whether we
	 * are holding the inode's IO lock.
	 */
	iolocked = iip->ili_flags & XFS_ILI_IOLOCKED_ANY;

	/*
	 * Clear out the fields of the inode log item particular
	 * to the current transaction.
	 */
	iip->ili_ilock_recur = 0;
	iip->ili_iolock_recur = 0;
	iip->ili_flags = 0;

	/*
	 * Unlock the inode if XFS_ILI_HOLD was not set.
	 */
	if (!hold) {
		lock_flags = XFS_ILOCK_EXCL;
		if (iolocked & XFS_ILI_IOLOCKED_EXCL) {
			lock_flags |= XFS_IOLOCK_EXCL;
		} else if (iolocked & XFS_ILI_IOLOCKED_SHARED) {
			lock_flags |= XFS_IOLOCK_SHARED;
		}
		xfs_iput(iip->ili_inode, lock_flags);
	}
}

/*
 * This is called to find out where the oldest active copy of the
 * inode log item in the on disk log resides now that the last log
 * write of it completed at the given lsn.  Since we always re-log
 * all dirty data in an inode, the latest copy in the on disk log
 * is the only one that matters.  Therefore, simply return the
 * given lsn.
 */
/*ARGSUSED*/
xfs_lsn_t
xfs_inode_item_committed(
	xfs_inode_log_item_t	*iip,
	xfs_lsn_t		lsn)
{
	return (lsn);
}

/*
 * The transaction with the inode locked has aborted.  The inode
 * must not be dirty within the transaction.  We simply unlock just
 * as if the transaction had been cancelled.
 */
void
xfs_inode_item_abort(
	xfs_inode_log_item_t	*iip)
{
	xfs_inode_item_unlock(iip);
	return;
}


/*
 * This is called to asynchronously write the inode associated with this
 * inode log item out to disk. The inode will already have been locked by
 * a successful call to xfs_inode_item_trylock().  If the inode still has
 * some of its fields marked as logged, then write it out.  If not, then
 * just unlock the inode.
 */
void
xfs_inode_item_push(
	xfs_inode_log_item_t	*iip)
{
	buf_t		*bp;
	xfs_inode_t	*ip;

	ASSERT((iip->ili_bp != NULL) ||
	       (ismrlocked(&(iip->ili_inode->i_lock), MR_ACCESS)));
	ASSERT((iip->ili_bp != NULL) ||
	       (valusema(&(iip->ili_inode->i_flock)) <= 0));

	/*
	 * If the inode item's buffer pointer is non-NULL, then we were
	 * unable to lock the inode in the trylock routine but we were
	 * able to lock the inode's buffer and it was marked delayed write.
	 * We didn't want to write it out from the trylock routine, because
	 * the device strategy routine might sleep.  Instead we stored it
	 * and write it out here.
	 */
	bp = iip->ili_bp;
	if ((bp != NULL) && (iip->ili_bp_owner == u.u_procp)) {
		ASSERT(iip->ili_bp->b_flags & B_BUSY);
		iip->ili_bp = NULL;
		iip->ili_bp_owner = NULL;
#ifndef SIM
		buftrace("INODE ITEM PUSH", bp);
#endif
		bawrite(bp);
		return;
	}

	/*
	 * Since we were able to lock the inode's flush lock and
	 * we found it on the AIL, the inode must be dirty.  This
	 * is because the inode is removed from the AIL while still
	 * holding the flush lock in xfs_iflush_done().  Thus, if
	 * we found it in the AIL and were able to obtain the flush
	 * lock without sleeping, then there must not have been
	 * anyone in the process of flushing the inode.
	 */
	ip = iip->ili_inode;
	ASSERT(iip->ili_format.ilf_fields != 0);

	/*
	 * Write out the inode.  The completion routine will
	 * pull it from the AIL, mark it clean, and xfs_iput()
	 * the inode.
	 */
	xfs_iflush(ip, XFS_IFLUSH_DELWRI);
	xfs_iunlock(ip, XFS_ILOCK_SHARED);
}

/*
 * This is the ops vector shared by all buf log items.
 */
struct xfs_item_ops xfs_inode_item_ops = {
	(uint(*)(xfs_log_item_t*))xfs_inode_item_size,
	(void(*)(xfs_log_item_t*, xfs_log_iovec_t*))xfs_inode_item_format,
	(void(*)(xfs_log_item_t*))xfs_inode_item_pin,
	(void(*)(xfs_log_item_t*))xfs_inode_item_unpin,
	(uint(*)(xfs_log_item_t*))xfs_inode_item_trylock,
	(void(*)(xfs_log_item_t*))xfs_inode_item_unlock,
	(xfs_lsn_t(*)(xfs_log_item_t*, xfs_lsn_t))xfs_inode_item_committed,
	(void(*)(xfs_log_item_t*))xfs_inode_item_push,
	(void(*)(xfs_log_item_t*))xfs_inode_item_abort
};


/*
 * Initialize the inode log item for a newly allocated (in-core) inode.
 */
void
xfs_inode_item_init(
	xfs_inode_t	*ip,
	xfs_mount_t	*mp)
{
	xfs_inode_log_item_t	*iip;

	iip = &ip->i_item;

	iip->ili_item.li_type = XFS_LI_INODE;
	iip->ili_item.li_ops = &xfs_inode_item_ops;
	iip->ili_item.li_mountp = mp;
	iip->ili_inode = ip;
	iip->ili_extents_buf = NULL;
	iip->ili_bp = NULL;
	iip->ili_format.ilf_type = XFS_LI_INODE;
	iip->ili_format.ilf_ino = ip->i_ino;
	iip->ili_format.ilf_blkno = ip->i_blkno;
	iip->ili_format.ilf_len = ip->i_len;
	iip->ili_format.ilf_boffset = ip->i_boffset;
}


/*
 * This is the inode flushing I/O completion routine.  It is called
 * from interrupt level when the buffer containing the inode is
 * flushed to disk.  It is responsible for removing the inode item
 * from the AIL if it has not been re-logged, and unlocking the inode's
 * flush lock.
 */
/*ARGSUSED*/
void
xfs_iflush_done(
	buf_t			*bp,
	xfs_inode_log_item_t	*iip)
{
	xfs_inode_t	*ip;
	int		s;

	ip = iip->ili_inode;

	/*
	 * We only want to pull the item from the AIL if it is
	 * actually there and its location in the log has not
	 * changed since we started the flush.  Thus, we only bother
	 * if the ili_logged flag is set and the inode's lsn has not
	 * changed.  First we check the lsn outside
	 * the lock since it's cheaper, and then we recheck while
	 * holding the lock before removing the inode from the AIL.
	 */
	if (iip->ili_logged &&
	    (iip->ili_item.li_lsn == iip->ili_flush_lsn)) {
		s = AIL_LOCK(ip->i_mount);
		if (iip->ili_item.li_lsn == iip->ili_flush_lsn) {
			/*
			 * xfs_trans_delete_ail() drops the AIL lock.
			 */
			xfs_trans_delete_ail(ip->i_mount,
					     (xfs_log_item_t*)iip, s);
		} else {
			AIL_UNLOCK(ip->i_mount, s);
		}
	}
	
	iip->ili_logged = 0;

	/*
	 * Clear the ili_last_fields bits now that we know that the
	 * data corresponding to them is safely on disk.
	 */
	iip->ili_last_fields = 0;

	/*
	 * Release the inode's flush lock since we're done with it.
	 */
	xfs_ifunlock(ip);

	return;
}




