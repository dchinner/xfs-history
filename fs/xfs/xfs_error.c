#ident "$Revision: 1.10 $"

#include "sys/types.h"
#include "sys/pda.h"
#include "sys/errno.h"
#include "sys/debug.h"
#ifdef SIM
#include <stdlib.h>
#else
#include "sys/systm.h"
#include "sys/cmn_err.h"
#endif
#include "xfs_error.h"

#ifdef DEBUG
int	xfs_etrap[XFS_ERROR_NTRAP] = { 0 }; /* We used to trap { EIO } */

int
xfs_error_trap(int e)
{
	int i;

	if (!e)
		return 0;
	for (i = 0; i < XFS_ERROR_NTRAP; i++) {
		if (xfs_etrap[i] == 0)
			break;
		if (e != xfs_etrap[i])
			continue;
#ifdef SIM
		abort();
#else
		cmn_err(CE_NOTE, "xfs_error_trap: error %d", e);
		debug_stop_all_cpus((void *)-1LL);
		debug("xfs");
#endif
		break;
	}
	return e;
}
#endif
