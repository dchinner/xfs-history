#ident "$Revision: 1.7 $"

#include "sys/types.h"
#include "sys/pda.h"
#include "sys/errno.h"
#include "sys/debug.h"
#ifdef SIM
#include <stdlib.h>
#else
#include "sys/systm.h"
#endif
#include "xfs_error.h"

#ifdef DEBUG
int	xfs_etrap[XFS_ERROR_NTRAP] = { EIO };

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
		debug_stop_all_cpus();
		debug("xfs");
#endif
		break;
	}
	return e;
}
#endif
