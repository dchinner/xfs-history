#ident "$Revision$"

#include "sys/types.h"
#include "sys/pda.h"
#include "sys/errno.h"
#ifdef SIM
#include <stdlib.h>
#else
#include "sys/systm.h"
#endif
#include "xfs_error.h"

#ifdef DEBUG
int	xfs_etrap[XFS_ERROR_NTRAP] = { ENOSPC };

#ifndef SIM
extern void panicspin(void);
#endif

int
xfs_error_trap(int e)
{
	int i;
#ifndef SIM
	int cpu;
	int id;
#endif

	for (i = 0; i < XFS_ERROR_NTRAP; i++) {
		if (xfs_etrap[i] == 0)
			break;
		if (e != xfs_etrap[i])
			continue;
#ifdef SIM
		abort();
#else
		for (cpu = 0; cpu < maxcpus; cpu++) {
			if (((id = pdaindr[cpu].CpuId) != -1) && (id != cpuid())) {
				cpuaction(id, (cpuacfunc_t)panicspin, A_NOW);
			}
		}
		debug("ring");
#endif
		break;
	}
	return e;
}
#endif
