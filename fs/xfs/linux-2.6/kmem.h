/*
 * Copyright (c) 2000 Silicon Graphics, Inc.  All Rights Reserved.
 * 
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 * 
 * This program is distributed in the hope that it would be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * 
 * Further, this software is distributed without any warranty that it is
 * free of the rightful claim of any third person regarding infringement
 * or the like.  Any license provided herein, whether implied or
 * otherwise, applies only to this software file.  Patent licenses, if
 * any, provided herein do not apply to combinations of this program with
 * other software, or any other product whatsoever.
 * 
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write the Free Software Foundation, Inc., 59
 * Temple Place - Suite 330, Boston MA 02111-1307, USA.
 * 
 * Contact information: Silicon Graphics, Inc., 1600 Amphitheatre Pkwy,
 * Mountain View, CA  94043, or:
 * 
 * http://www.sgi.com 
 * 
 * For further information regarding this notice, see: 
 * 
 * http://oss.sgi.com/projects/GenInfo/SGIGPLNoticeExplan/
 */
#ifndef __XFS_SUPPORT_KMEM_H__
#define __XFS_SUPPORT_KMEM_H__


/*
 * memory management routines
 */
#define KM_SLEEP        0x0001
#define KM_NOSLEEP      0x0002	/* must match VM_NOSLEEP */
#define KM_PHYSCONTIG	0x0008
#define KM_CACHEALIGN	0x0010	/* guarantee that memory is cache aligned */

#define VM_NOSLEEP	KM_NOSLEEP
#define VM_PHYSCONTIG	KM_PHYSCONTIG
#define VM_CACHEALIGN	KM_CACHEALIGN
#define VM_DIRECT	KM_PHYSCONTIG
#define VM_UNCACHED	0x0020

typedef struct kmem_cache_s kmem_zone_t;

extern kmem_zone_t  *kmem_zone_init(int, char *);
extern void	    *kmem_zone_zalloc(kmem_zone_t *, int);
extern void	    *kmem_zone_alloc(kmem_zone_t *, int);
extern void         kmem_zone_free(kmem_zone_t *, void *);

extern void	    *kmem_alloc(size_t, int);
extern void	    *kmem_realloc(void *, size_t, size_t, int);
extern void	    *kmem_zalloc(size_t, int);
extern void         kmem_free(void *, size_t);

typedef void	    (*kmem_shake_func_t)(void);

extern void	    kmem_shake_register(kmem_shake_func_t);
extern void	    kmem_shake_deregister(kmem_shake_func_t);

#endif /* __XFS_SUPPORT_KMEM_H__ */
