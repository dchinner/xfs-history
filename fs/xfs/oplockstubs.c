/*
 * Copyright (C) 1999 Silicon Graphics, Inc.  All Rights Reserved.
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
/* oplocks are not included in Mini Root */

#include <linux/errno.h>

int
oplock_fcntl(void)
{
	return (EINVAL);
}

void
oplock_fs_create(void)
{
	return;
}

#ifdef CELL_CAPABLE
void
oplock_cxfs_check() { }

int
oplock_cxfs_req()
{
	return(0);
}
#endif	/* CELL_CAPABLE */
