#ident "$Revision: 1.2 $"

/*
 * XFS bit manipulation routines, used only in realtime code.
 */

#include <sys/types.h>
#include "xfs_bit.h"

/*
 * xfs_lowbit32: get low bit set out of 32-bit argument, -1 if none set.
 */
int
xfs_lowbit32(
	__uint32_t	v)
{
	if (v & 0x0000ffff)
		if (v & 0x000000ff)
			return xfs_lowbit[v & 0xff];
		else
			return 8 + xfs_lowbit[(v >> 8) & 0xff];
	else if (v & 0xffff0000)
		if (v & 0x00ff0000)
			return 16 + xfs_lowbit[(v >> 16) & 0xff];
		else
			return 24 + xfs_lowbit[(v >> 24) & 0xff];
	else
		return -1;
}

/*
 * xfs_highbit64: get high bit set out of 64-bit argument, -1 if none set.
 */
int
xfs_highbit64(
	__uint64_t	v)
{
#if (_MIPS_SIM == _ABIN32 || _MIPS_SIM == _ABI64)
	if (v & 0xffffffff00000000)
		if (v & 0xffff000000000000)
			if (v & 0xff00000000000000)
				return 56 + xfs_highbit[(v >> 56) & 0xff];
			else
				return 48 + xfs_highbit[(v >> 48) & 0xff];
		else
			if (v & 0x0000ff0000000000)
				return 40 + xfs_highbit[(v >> 40) & 0xff];
			else
				return 32 + xfs_highbit[(v >> 32) & 0xff];
	else if (v & 0x00000000ffffffff)
		if (v & 0x00000000ffff0000)
			if (v & 0x00000000ff000000)
				return 24 + xfs_highbit[(v >> 24) & 0xff];
			else
				return 16 + xfs_highbit[(v >> 16) & 0xff];
		else
			if (v & 0x000000000000ff00)
				return 8 + xfs_highbit[(v >> 8) & 0xff];
			else
				return xfs_highbit[v & 0xff];
	else
		return -1;
#else
	__uint32_t	vw;

	if (vw = v >> 32)
		if (vw & 0xffff0000)
			if (vw & 0xff000000)
				return 56 + xfs_highbit[(vw >> 24) & 0xff];
			else
				return 48 + xfs_highbit[(vw >> 16) & 0xff];
		else
			if (vw & 0x0000ff00)
				return 40 + xfs_highbit[(vw >> 8) & 0xff];
			else
				return 32 + xfs_highbit[vw & 0xff];
	else if (vw = v)
		if (vw & 0xffff0000)
			if (vw & 0xff000000)
				return 24 + xfs_highbit[(vw >> 24) & 0xff];
			else
				return 16 + xfs_highbit[(vw >> 16) & 0xff];
		else
			if (vw & 0x0000ff00)
				return 8 + xfs_highbit[(vw >> 8) & 0xff];
			else
				return xfs_highbit[vw & 0xff];
	else
		return -1;
#endif
}
