#ident "$Revision: 1.23 $"

/*
 * This file contains the implementation of the xfs_efi_log_item
 * and xfs_efd_log_item items.
 */


#ifdef SIM
#define _KERNEL	1
#endif
#include <sys/param.h>
#include <sys/buf.h>
#include <sys/vnode.h>
#include <sys/debug.h>
#ifdef SIM
#undef _KERNEL
#endif
#include <sys/kmem.h>
#include <sys/uuid.h>
#ifndef SIM
#include <sys/systm.h>
#else
#include <bstring.h>
#endif
#include "xfs_types.h"
#include "xfs_inum.h"
#include "xfs_log.h"
#include "xfs_trans.h"
#include "xfs_buf_item.h"
#include "xfs_sb.h"
#include "xfs_mount.h"
#include "xfs_trans_priv.h"
#include "xfs_extfree_item.h"

#ifdef SIM
#include "sim.h"
#endif

zone_t	*xfs_efi_zone;
zone_t	*xfs_efd_zone;

STATIC uint	xfs_efi_item_size(xfs_efi_log_item_t *);
STATIC void	xfs_efi_item_format(xfs_efi_log_item_t *, xfs_log_iovec_t *);
STATIC void	xfs_efi_item_pin(xfs_efi_log_item_t *);
STATIC void	xfs_efi_item_unpin(xfs_efi_log_item_t *);
STATIC uint	xfs_efi_item_trylock(xfs_efi_log_item_t *);
STATIC void	xfs_efi_item_unlock(xfs_efi_log_item_t *);
STATIC xfs_lsn_t	xfs_efi_item_committed(xfs_efi_log_item_t *,
					       xfs_lsn_t lsn);
STATIC void	xfs_efi_item_abort(xfs_efi_log_item_t *);
STATIC void	xfs_efi_item_push(xfs_efi_log_item_t *);
STATIC void	xfs_efi_cancel(xfs_efi_log_item_t *);

STATIC uint	xfs_efd_item_size(xfs_efd_log_item_t *);
STATIC void	xfs_efd_item_format(xfs_efd_log_item_t *, xfs_log_iovec_t *);
STATIC void	xfs_efd_item_pin(xfs_efd_log_item_t *);
STATIC void	xfs_efd_item_unpin(xfs_efd_log_item_t *);
STATIC uint	xfs_efd_item_trylock(xfs_efd_log_item_t *);
STATIC void	xfs_efd_item_unlock(xfs_efd_log_item_t *);
STATIC void	xfs_efd_item_abort(xfs_efd_log_item_t *);
STATIC xfs_lsn_t	xfs_efd_item_committed(xfs_efd_log_item_t *,
					       xfs_lsn_t lsn);
STATIC void	xfs_efd_item_push(xfs_efd_log_item_t *);



/*
 * This returns the number of iovecs needed to log the given efi item.
 * We only need 1 iovec for an efi item.  It just logs the efi_log_format
 * structure.
 */
/*ARGSUSED*/
STATIC uint
xfs_efi_item_size(xfs_efi_log_item_t *efip)
{
	return 1; 
}

/*
 * This is called to fill in the vector of log iovecs for the
 * given efi log item. We use only 1 iovec, and we point that
 * at the efi_log_format structure embedded in the efi item.
 * It is at this point that we assert that all of the extent
 * slots in the efi item have been filled.
 */
STATIC void
xfs_efi_item_format(xfs_efi_log_item_t	*efip,
		    xfs_log_iovec_t	*log_vector)
{
	uint	size;

	ASSERT(efip->efi_next_extent == efip->efi_format.efi_nextents);

	efip->efi_format.efi_type = XFS_LI_EFI;

	size = sizeof(xfs_efi_log_format_t);
	size += (efip->efi_format.efi_nextents - 1) * sizeof(xfs_extent_t);
	efip->efi_format.efi_size = 1;

	log_vector->i_addr = (caddr_t)&(efip->efi_format);
	log_vector->i_len = size;
	ASSERT(size >= sizeof(xfs_efi_log_format_t));
}
	

/*
 * Pinning has no meaning for an efi item, so just return.
 */
/*ARGSUSED*/
STATIC void
xfs_efi_item_pin(xfs_efi_log_item_t *efip)
{
	return;
}


/*
 * While EFIs cannot really be pinned, the unpin operation is the
 * last place at which the EFI is manipulated during a transaction.
 * Here we coordinate with xfs_efi_cancel() to determine who gets to
 * free the EFI.
 */
/*ARGSUSED*/
STATIC void
xfs_efi_item_unpin(xfs_efi_log_item_t *efip)
{
	int		s;
	int		nexts;
	int		size;
	xfs_mount_t	*mp;

	mp = efip->efi_item.li_mountp;
	s = AIL_LOCK(mp);
	if (efip->efi_flags & XFS_EFI_CANCELED) {
		/*
		 * xfs_trans_delete_ail() drops the AIL lock.
		 */
		xfs_trans_delete_ail(mp, (xfs_log_item_t *)efip, s);

		nexts = efip->efi_format.efi_nextents;
		if (nexts > XFS_EFI_MAX_FAST_EXTENTS) {
			size = sizeof(xfs_efi_log_item_t);
			size += (nexts - 1) * sizeof(xfs_extent_t);
			kmem_free(efip, size);
		} else {
			kmem_zone_free(xfs_efi_zone, efip);
		}
	} else {
		efip->efi_flags |= XFS_EFI_COMMITTED;
		AIL_UNLOCK(mp, s);
	}

	return;
}

/*
 * Efi items have no locking or pushing.  However, since EFIs are
 * pulled from the AIL when their corresponding EFDs are committed
 * to disk, their situation is very similar to being pinned.  Return
 * XFS_ITEM_PINNED so that the caller will eventually flush the log.
 * This should help in getting the EFI out of the AIL.
 */
/*ARGSUSED*/
STATIC uint
xfs_efi_item_trylock(xfs_efi_log_item_t *efip)
{
	return XFS_ITEM_PINNED;
}

/*
 * Efi items have no locking, so just return.
 */
/*ARGSUSED*/
STATIC void
xfs_efi_item_unlock(xfs_efi_log_item_t *efip)
{
	return;
}

/*
 * The EFI is logged only once and cannot be moved in the log, so
 * simply return the lsn at which it's been logged.  The canceled
 * flag is not paid any attention here.  Checking for that is delayed
 * until the EFI is unpinned.
 */
/*ARGSUSED*/
STATIC xfs_lsn_t
xfs_efi_item_committed(xfs_efi_log_item_t *efip, xfs_lsn_t lsn)
{
	return lsn;
}

/*
 * This is called when the transaction logging the EFI is aborted.
 * Free up the EFI and return.
 */
STATIC void
xfs_efi_item_abort(xfs_efi_log_item_t *efip)
{
	int	nexts;
	int	size;

	nexts = efip->efi_format.efi_nextents;
	if (nexts > XFS_EFI_MAX_FAST_EXTENTS) {
		size = sizeof(xfs_efi_log_item_t);
		size += (nexts - 1) * sizeof(xfs_extent_t);
		kmem_free(efip, size);
	} else {
		kmem_zone_free(xfs_efi_zone, efip);
	}
	return;
}

/*
 * There isn't much you can do to push on an efi item.  It is simply
 * stuck waiting for all of its corresponding efd items to be
 * committed to disk.
 */
/*ARGSUSED*/
STATIC void
xfs_efi_item_push(xfs_efi_log_item_t *efip)
{
	return;
}

/*
 * This is the ops vector shared by all efi log items.
 */
struct xfs_item_ops xfs_efi_item_ops = {
	(uint(*)(xfs_log_item_t*))xfs_efi_item_size,
	(void(*)(xfs_log_item_t*, xfs_log_iovec_t*))xfs_efi_item_format,
	(void(*)(xfs_log_item_t*))xfs_efi_item_pin,
	(void(*)(xfs_log_item_t*))xfs_efi_item_unpin,
	(uint(*)(xfs_log_item_t*))xfs_efi_item_trylock,
	(void(*)(xfs_log_item_t*))xfs_efi_item_unlock,
	(xfs_lsn_t(*)(xfs_log_item_t*, xfs_lsn_t))xfs_efi_item_committed,
	(void(*)(xfs_log_item_t*))xfs_efi_item_push,
	(void(*)(xfs_log_item_t*))xfs_efi_item_abort
};


/*
 * Allocate and initialize an efi item with the given number of extents.
 */
xfs_efi_log_item_t *
xfs_efi_init(xfs_mount_t	*mp,
	     uint		nextents)
	     
{
	xfs_efi_log_item_t	*efip;
	uint			size;

	ASSERT(nextents > 0);
	if (nextents > XFS_EFI_MAX_FAST_EXTENTS) {
		size = sizeof(xfs_efi_log_item_t) +
			((nextents - 1) * sizeof(xfs_extent_t));
		efip = (xfs_efi_log_item_t*)kmem_zalloc(size, KM_SLEEP);
	} else {
		efip = (xfs_efi_log_item_t*)kmem_zone_zalloc(xfs_efi_zone,
							     KM_SLEEP);
	}

	efip->efi_item.li_type = XFS_LI_EFI;
	efip->efi_item.li_ops = &xfs_efi_item_ops;
	efip->efi_item.li_mountp = mp;
	efip->efi_format.efi_nextents = nextents;
	efip->efi_format.efi_id = (__uint64_t)efip;

	return (efip);
}

/*
 * This is called by the efd item code below to release references to
 * the given efi item.  Each efd calls this with the number of
 * extents that it has logged, and when the sum of these reaches
 * the total number of extents logged by this efi item we can free
 * the efi item.
 *
 * Freeing the efi item requires that we remove it from the AIL.
 * We'll use the AIL lock to protect our counters as well as
 * the removal from the AIL.
 */
void
xfs_efi_release(xfs_efi_log_item_t	*efip,
		uint			nextents)
{
	xfs_mount_t	*mp;
	int		s;
	int		extents_left;
	uint		size;
	int		nexts;

	ASSERT(efip->efi_next_extent > 0);
	ASSERT(efip->efi_flags & XFS_EFI_COMMITTED);

	mp = efip->efi_item.li_mountp;
	s = AIL_LOCK(mp);
	ASSERT(efip->efi_next_extent >= nextents);
	efip->efi_next_extent -= nextents;
	extents_left = efip->efi_next_extent;
	if (extents_left == 0) {
		/*
		 * xfs_trans_delete_ail() drops the AIL lock.
		 */
		xfs_trans_delete_ail(mp, (xfs_log_item_t *)efip, s);
	} else {
		AIL_UNLOCK(mp, s);
	}

	if (extents_left == 0) {
		nexts = efip->efi_format.efi_nextents;
		if (nexts > XFS_EFI_MAX_FAST_EXTENTS) {
			size = sizeof(xfs_efi_log_item_t);
			size += (nexts - 1) * sizeof(xfs_extent_t);
			kmem_free(efip, size);
		} else {
			kmem_zone_free(xfs_efi_zone, efip);
		}
	}
}

/*
 * This is called when the transaction that should be committing the
 * EFD corresponding to the given EFI is aborted.  The committed and
 * canceled flags are used to coordinate the freeing of the EFI and
 * the references by the transaction that committed it.
 */
STATIC void
xfs_efi_cancel(
	xfs_efi_log_item_t	*efip)
{
	int		s;
	int		nexts;
	int		size;
	xfs_mount_t	*mp;

	mp = efip->efi_item.li_mountp;
	s = AIL_LOCK(mp);
	if (efip->efi_flags & XFS_EFI_COMMITTED) {
		/*
		 * xfs_trans_delete_ail() drops the AIL lock.
		 */
		xfs_trans_delete_ail(mp, (xfs_log_item_t *)efip, s);

		nexts = efip->efi_format.efi_nextents;
		if (nexts > XFS_EFI_MAX_FAST_EXTENTS) {
			size = sizeof(xfs_efi_log_item_t);
			size += (nexts - 1) * sizeof(xfs_extent_t);
			kmem_free(efip, size);
		} else {
			kmem_zone_free(xfs_efi_zone, efip);
		}
	} else {
		efip->efi_flags |= XFS_EFI_CANCELED;
		AIL_UNLOCK(mp, s);
	}

	return;
}





/*
 * This returns the number of iovecs needed to log the given efd item.
 * We only need 1 iovec for an efd item.  It just logs the efd_log_format
 * structure.
 */
/*ARGSUSED*/
STATIC uint
xfs_efd_item_size(xfs_efd_log_item_t *efdp)
{
	return 1; 
}

/*
 * This is called to fill in the vector of log iovecs for the
 * given efd log item. We use only 1 iovec, and we point that
 * at the efd_log_format structure embedded in the efd item.
 * It is at this point that we assert that all of the extent
 * slots in the efd item have been filled.
 */
STATIC void
xfs_efd_item_format(xfs_efd_log_item_t	*efdp,
		    xfs_log_iovec_t	*log_vector)
{
	uint	size;

	ASSERT(efdp->efd_next_extent == efdp->efd_format.efd_nextents);

	efdp->efd_format.efd_type = XFS_LI_EFD;

	size = sizeof(xfs_efd_log_format_t);
	size += (efdp->efd_format.efd_nextents - 1) * sizeof(xfs_extent_t);
	efdp->efd_format.efd_size = 1;

	log_vector->i_addr = (caddr_t)&(efdp->efd_format);
	log_vector->i_len = size;
	ASSERT(size >= sizeof(xfs_efd_log_format_t));
}
	

/*
 * Pinning has no meaning for an efd item, so just return.
 */
/*ARGSUSED*/
STATIC void
xfs_efd_item_pin(xfs_efd_log_item_t *efdp)
{
	return;
}


/*
 * Since pinning has no meaning for an efd item, unpinning does
 * not either.
 */
/*ARGSUSED*/
STATIC void
xfs_efd_item_unpin(xfs_efd_log_item_t *efdp)
{
	return;
}

/*
 * Efd items have no locking, so just return success.
 */
/*ARGSUSED*/
STATIC uint
xfs_efd_item_trylock(xfs_efd_log_item_t *efdp)
{
	return XFS_ITEM_LOCKED;
}

/*
 * Efd items have no locking or pushing, so return failure
 * so that the caller doesn't bother with us.
 */
/*ARGSUSED*/
STATIC void
xfs_efd_item_unlock(xfs_efd_log_item_t *efdp)
{
	return;
}

/*
 * When the efd item is committed to disk, all we need to do
 * is delete our reference to our partner efi item and then
 * free ourselves.  Since we're freeing ourselves we must
 * return -1 to keep the transaction code from further referencing
 * this item.
 */
/*ARGSUSED*/
STATIC xfs_lsn_t
xfs_efd_item_committed(xfs_efd_log_item_t *efdp, xfs_lsn_t lsn)
{
	uint	size;
	int	nexts;

	xfs_efi_release(efdp->efd_efip, efdp->efd_format.efd_nextents);

	nexts = efdp->efd_format.efd_nextents;
	if (nexts > XFS_EFD_MAX_FAST_EXTENTS) {
		size = sizeof(xfs_efd_log_item_t);
		size += (nexts - 1) * sizeof(xfs_extent_t);
		kmem_free(efdp, size);
	} else {
		kmem_zone_free(xfs_efd_zone, efdp);
	}

	return (xfs_lsn_t)-1;
}

/*
 * The transaction of which this EFD is a part has been aborted.
 * Inform its companion EFI of this fact and then clean up after
 * ourselves.
 */
STATIC void
xfs_efd_item_abort(xfs_efd_log_item_t *efdp)
{
	int	nexts;
	int	size;

	xfs_efi_cancel(efdp->efd_efip);

	nexts = efdp->efd_format.efd_nextents;
	if (nexts > XFS_EFD_MAX_FAST_EXTENTS) {
		size = sizeof(xfs_efd_log_item_t);
		size += (nexts - 1) * sizeof(xfs_extent_t);
		kmem_free(efdp, size);
	} else {
		kmem_zone_free(xfs_efd_zone, efdp);
	}
	return;
}

/*
 * There isn't much you can do to push on an efd item.  It is simply
 * stuck waiting for the log to be flushed to disk.
 */
/*ARGSUSED*/
STATIC void
xfs_efd_item_push(xfs_efd_log_item_t *efdp)
{
	return;
}

/*
 * This is the ops vector shared by all efd log items.
 */
struct xfs_item_ops xfs_efd_item_ops = {
	(uint(*)(xfs_log_item_t*))xfs_efd_item_size,
	(void(*)(xfs_log_item_t*, xfs_log_iovec_t*))xfs_efd_item_format,
	(void(*)(xfs_log_item_t*))xfs_efd_item_pin,
	(void(*)(xfs_log_item_t*))xfs_efd_item_unpin,
	(uint(*)(xfs_log_item_t*))xfs_efd_item_trylock,
	(void(*)(xfs_log_item_t*))xfs_efd_item_unlock,
	(xfs_lsn_t(*)(xfs_log_item_t*, xfs_lsn_t))xfs_efd_item_committed,
	(void(*)(xfs_log_item_t*))xfs_efd_item_push,
	(void(*)(xfs_log_item_t*))xfs_efd_item_abort
};


/*
 * Allocate and initialize an efd item with the given number of extents.
 */
xfs_efd_log_item_t *
xfs_efd_init(xfs_mount_t	*mp,
	     xfs_efi_log_item_t	*efip,
	     uint		nextents)
	     
{
	xfs_efd_log_item_t	*efdp;
	uint			size;

	ASSERT(nextents > 0);
	if (nextents > XFS_EFD_MAX_FAST_EXTENTS) {
		size = sizeof(xfs_efd_log_item_t) +
			((nextents - 1) * sizeof(xfs_extent_t));
		efdp = (xfs_efd_log_item_t*)kmem_zalloc(size, KM_SLEEP);
	} else {
		efdp = (xfs_efd_log_item_t*)kmem_zone_zalloc(xfs_efd_zone,
							     KM_SLEEP);
	}

	efdp->efd_item.li_type = XFS_LI_EFD;
	efdp->efd_item.li_ops = &xfs_efd_item_ops;
	efdp->efd_item.li_mountp = mp;
	efdp->efd_efip = efip;
	efdp->efd_format.efd_nextents = nextents;
	efdp->efd_format.efd_efi_id = efip->efi_format.efi_id;

	return (efdp);
}





