
#include "types.h"
#include <sys/sema.h>
#include "buf.h"
#include "vnode.h"
#include "param.h"
#include <sys/debug.h>
#include "xfs.h"
#include "xfs_trans.h"

#ifdef SIM
#include "sim.h"
#endif


/*
 * This is called to determine if there are any pending
 * async transactions for the given file system.
 * Just return a value based on the value
 * of the list pointer in the xfs_mount structure.
 */
int
xfs_trans_any_async(struct xfs_mount *mp)
{
	return (mp->m_async_trans != NULL);
}


/*
 * Add the given transaction to the list of asynchronous
 * transactions to be committed.
 */
void
xfs_trans_add_async(xfs_trans_t *tp)
{
	int			s;
	lock_t			async_lock;
	struct xfs_mount	*mp;

	mp = tp->t_mountp;
	async_lock = mp->m_async_lock;
	s = splockspl(async_lock, splhi);
	tp->t_forw = mp->m_async_trans;
	mp->m_async_trans = tp;
	spunlockspl(async_lock, s);
}

/*
 * Return the list of asynchronous transactions hung off
 * of the given mount structure. If there is nothing on
 * the list, return NULL.
 */
xfs_trans_t *
xfs_trans_get_async(struct xfs_mount *mp)
{
	int			s;
	lock_t			async_lock;
	xfs_trans_t		*async_list;

	/*
	 * If the list is NULL, return without bothering
	 * with the lock.
	 */
	if (mp->m_async_trans == NULL) {
		return (NULL);
	}

	/*
	 * Pull off the entire list and return it.
	 * If it's NULL, we return NULL which is fine.
	 */
	async_lock = mp->m_async_lock;
	s = splockspl(async_lock, splhi);
	async_list = mp->m_async_trans;
	mp->m_async_trans = NULL;
	spunlockspl(async_lock, s);

	return (async_list);
}




