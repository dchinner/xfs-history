#if __GNUC__ == 2 && __GNUC_MINOR == 95
#include "xfs_log.h"

xfs_lsn_t _lsn_cmp(xfs_lsn_t lsn1, xfs_lsn_t lsn2, xfs_arch_t arch)
{
	return _xfs_lsn_cmp(lsn1, lsn2, arch);
} 
#endif
