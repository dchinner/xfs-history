
/*
 * This file contains the implementation of the xfs_efi_log_item
 * and xfs_efd_log_item items.
 */


#include <sys/param.h>
#ifdef SIM
#define _KERNEL
#endif
#include <sys/buf.h>
#include <sys/vnode.h>
#ifdef SIM
#undef _KERNEL
#endif
#include <sys/debug.h>
#include <sys/uuid.h>
#include <sys/kmem.h>
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
#include "xfs_bio.h"
#include "xfs_sb.h"
#include "xfs_mount.h"
#include "xfs_trans_priv.h"
#include "xfs_extfree_item.h"

#ifdef SIM
#include "sim.h"
#endif

STATIC void	xfs_efi_release(xfs_efi_log_item_t *, uint);
STATIC uint	xfs_efi_item_size(xfs_efi_log_item_t *);
STATIC void	xfs_efi_item_format(xfs_efi_log_item_t *, xfs_log_iovec_t *);
STATIC void	xfs_efi_item_pin(xfs_efi_log_item_t *);
STATIC void	xfs_efi_item_unpin(xfs_efi_log_item_t *);
STATIC uint	xfs_efi_item_trylock(xfs_efi_log_item_t *);
STATIC void	xfs_efi_item_unlock(xfs_efi_log_item_t *);
STATIC xfs_lsn_t	xfs_efi_item_committed(xfs_efi_log_item_t *,
					       xfs_lsn_t lsn);
STATIC void	xfs_efi_item_push(xfs_efi_log_item_t *);
STATIC void	xfs_efi_release(xfs_efi_log_item_t *, uint);

STATIC uint	xfs_efd_item_size(xfs_efd_log_item_t *);
STATIC void	xfs_efd_item_format(xfs_efd_log_item_t *, xfs_log_iovec_t *);
STATIC void	xfs_efd_item_pin(xfs_efd_log_item_t *);
STATIC void	xfs_efd_item_unpin(xfs_efd_log_item_t *);
STATIC uint	xfs_efd_item_trylock(xfs_efd_log_item_t *);
STATIC void	xfs_efd_item_unlock(xfs_efd_log_item_t *);
STATIC xfs_lsn_t	xfs_efd_item_committed(xfs_efd_log_item_t *,
					       xfs_lsn_t lsn);
STATIC void	xfs_efd_item_push(xfs_efd_log_item_t *);



/*
 * This returns the number of iovecs needed to log the given efi item.
 * We only need 1 iovec for an efi item.  It just logs the efi_log_format
 * structure.
 */
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
STATIC void
xfs_efi_item_pin(xfs_efi_log_item_t *efip)
{
	return;
}


/*
 * Since pinning has no meaning for an efi item, unpinning does
 * not either.
 */
STATIC void
xfs_efi_item_unpin(xfs_efi_log_item_t *efip)
{
	return;
}

/*
 * Efi items have no locking or pushing, so return failure
 * so that the caller doesn't bother with us.
 */
STATIC uint
xfs_efi_item_trylock(xfs_efi_log_item_t *efip)
{
	return 0;
}

/*
 * Efi items have no locking, so just return.
 */
STATIC void
xfs_efi_item_unlock(xfs_efi_log_item_t *efip)
{
	return;
}

/*
 * The efi item is logged only once in its lifetime, so always
 * just return the given lsn.
 */
STATIC xfs_lsn_t
xfs_efi_item_committed(xfs_efi_log_item_t *efip, xfs_lsn_t lsn)
{
	return (lsn);
}


/*
 * There isn't much you can do to push on an efi item.  It is simply
 * stuck waiting for all of its corresponding efd items to be
 * committed to disk.
 */
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
	(void(*)(xfs_log_item_t*))xfs_efi_item_push
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
	size = sizeof(xfs_efi_log_item_t) +
	       ((nextents - 1) * sizeof(xfs_extent_t));
	efip = (xfs_efi_log_item_t*)kmem_zalloc(size, KM_SLEEP);

	efip->efi_item.li_type = XFS_LI_EFI;
	efip->efi_item.li_ops = &xfs_efi_item_ops;
	efip->efi_item.li_mountp = mp;
	efip->efi_format.efi_nextents = nextents;

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
STATIC void
xfs_efi_release(xfs_efi_log_item_t	*efip,
		uint			nextents)
{
	xfs_mount_t	*mp;
	int		s;
	int		extents_left;
	uint		size;

	ASSERT(efip->efi_next_extent > 0);

	mp = efip->efi_item.li_mountp;
	s = AIL_LOCK(mp);
	ASSERT(efip->efi_next_extent >= nextents);
	efip->efi_next_extent -= nextents;
	extents_left = efip->efi_next_extent;
	if (extents_left == 0) {
		xfs_trans_delete_ail(mp, (xfs_log_item_t *)efip);
	}
	AIL_UNLOCK(mp, s);

	if (extents_left == 0) {
		size = sizeof(xfs_efi_log_item_t);
		size += (efip->efi_format.efi_nextents - 1) *
			sizeof(xfs_extent_t);
		kmem_free(efip, size);
	}
}








/*
 * This returns the number of iovecs needed to log the given efd item.
 * We only need 1 iovec for an efd item.  It just logs the efd_log_format
 * structure.
 */
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
STATIC void
xfs_efd_item_pin(xfs_efd_log_item_t *efdp)
{
	return;
}


/*
 * Since pinning has no meaning for an efd item, unpinning does
 * not either.
 */
STATIC void
xfs_efd_item_unpin(xfs_efd_log_item_t *efdp)
{
	return;
}

/*
 * Efd items have no locking, so just return success.
 */
STATIC uint
xfs_efd_item_trylock(xfs_efd_log_item_t *efdp)
{
	return 0;
}

/*
 * Efd items have no locking or pushing, so return failure
 * so that the caller doesn't bother with us.
 */
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
STATIC xfs_lsn_t
xfs_efd_item_committed(xfs_efd_log_item_t *efdp, xfs_lsn_t lsn)
{
	uint	size;

	xfs_efi_release(efdp->efd_efip, efdp->efd_format.efd_nextents);

	size = sizeof(xfs_efd_log_item_t);
	size += (efdp->efd_format.efd_nextents - 1) * sizeof(xfs_extent_t);
	kmem_free(efdp, size);

	return (xfs_lsn_t)-1;
}


/*
 * There isn't much you can do to push on an efd item.  It is simply
 * stuck waiting for the log to be flushed to disk.
 */
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
	(void(*)(xfs_log_item_t*))xfs_efd_item_push
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
	size = sizeof(xfs_efd_log_item_t) +
	       ((nextents - 1) * sizeof(xfs_extent_t));
	efdp = (xfs_efd_log_item_t*)kmem_zalloc(size, KM_SLEEP);

	efdp->efd_item.li_type = XFS_LI_EFD;
	efdp->efd_item.li_ops = &xfs_efd_item_ops;
	efdp->efd_item.li_mountp = mp;
	efdp->efd_efip = efip;
	efdp->efd_format.efd_nextents = nextents;

	return (efdp);
}





