
/*
 * Copyright (C) 2000 Silicon Graphics, Inc.  All Rights Reserved.
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston MA 02111-1307, USA.
 */
#ident "$Revision: 1.4 $"

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
	int		i;

	if (v & 0x0000ffff)
		if (v & 0x000000ff)
			i = 0;
		else
			i = 8;
	else if (v & 0xffff0000)
		if (v & 0x00ff0000)
			i = 16;
		else
			i = 24;
	else
		return -1;
	return i + xfs_lowbit[(v >> i) & 0xff];
}

/*
 * xfs_highbit64: get high bit set out of 64-bit argument, -1 if none set.
 */
int
xfs_highbit64(
	__uint64_t	v)
{
	int		i;
#if (_MIPS_SIM == _ABIN32 || _MIPS_SIM == _ABI64)
	if (v & 0xffffffff00000000)
		if (v & 0xffff000000000000)
			if (v & 0xff00000000000000)
				i = 56;
			else
				i = 48;
		else
			if (v & 0x0000ff0000000000)
				i = 40;
			else
				i = 32;
	else if (v & 0x00000000ffffffff)
		if (v & 0x00000000ffff0000)
			if (v & 0x00000000ff000000)
				i = 24;
			else
				i = 16;
		else
			if (v & 0x000000000000ff00)
				i = 8;
			else
				i = 0;
	else
		return -1;
	return i + xfs_highbit[(v >> i) & 0xff];
#else
	__uint32_t	vw;

	if (vw = v >> 32) {
		if (vw & 0xffff0000)
			if (vw & 0xff000000)
				i = 56;
			else
				i = 48;
		else
			if (vw & 0x0000ff00)
				i = 40;
			else
				i = 32;
		return i + xfs_highbit[(vw >> (i - 32)) & 0xff];
	} else if (vw = v) {
		if (vw & 0xffff0000)
			if (vw & 0xff000000)
				i = 24;
			else
				i = 16;
		else
			if (vw & 0x0000ff00)
				i = 8;
			else
				i = 0;
		return i + xfs_highbit[(vw >> i) & 0xff];
	} else
		return -1;
#endif
}
