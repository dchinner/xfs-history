#ifndef __XFS_ARCH_H__
#define __XFS_ARCH_H__

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

#ident "$Id$"

/* supported architecures */
  
#define ARCH_MIPS	0
#define ARCH_INTEL_IA32	1

/* possible future architectures */

#define ARCH_INTEL_IA64	2
#define ARCH_ALPHA	3

/*
 * exactly one of these three options should be enabled:
 *
 *  #ifdef CONFIG_XFS_ARCH_MIPS
 *     - support MIPS architecture ONLY
 *  #ifdef CONFIG_XFS_ARCH_NATIVE
 *     - support NATIVE architecture ONLY
 *  #ifdef CONFIG_XFS_ARCH_MULTI
 *     - support other architectures
 */
 
#include <linux/autoconf.h>

/* select native architecture */
  
#ifdef CONFIG_X86
#define XFS_ARCH_NATIVE ARCH_INTEL_IA32
#else
#error support for XFS on non X86 architectures not yet implemented
#endif

/* check for multiple defines */

#if (defined(CONFIG_XFS_ARCH_MIPS) && defined(CONFIG_XFS_ARCH_NATIVE)) || \
    (defined(CONFIG_XFS_ARCH_MIPS) && defined(CONFIG_XFS_ARCH_MULTI)) || \
    (defined(CONFIG_XFS_ARCH_MULTI) && defined(CONFIG_XFS_ARCH_NATIVE)) 
    
#error multiple xfs architecture options enabled

#endif

/* check for no defines */

#if (!defined(CONFIG_XFS_ARCH_MIPS) \
	&& !defined(CONFIG_XFS_ARCH_NATIVE) \
	&& !defined(CONFIG_XFS_ARCH_MULTI))

#error no xfs architecture selected

#endif
   
/* define supported architectures */

#ifdef CONFIG_XFS_ARCH_MIPS
#define ARCH_SUPPORTED(A) ((A)==ARCH_MIPS)
#define XFS_MODE "MIPS"
#endif

#ifdef CONFIG_XFS_ARCH_NATIVE
#define ARCH_SUPPORTED(A) ((A)==XFS_ARCH_NATIVE)
#define XFS_MODE "NATIVE"
#endif

#ifdef CONFIG_XFS_ARCH_MULTI
#define ARCH_SUPPORTED(A) ((A)==ARCH_MIPS || (A)==ARCH_INTEL_IA32)
#define XFS_MODE "MULTI"
#endif

/*
 *   define one of three sets of macros depending on the native
 *   and supported architectures
 *
 *       case 1 - Native only macros - no architecture conversiosn
 *       case 2 - Swapping only macros - _always_ endian swap
 *       case 3 - multiple architecture support - swap when
 *                specified architecture(s) require it.
 */


#ifdef CONFIG_XFS_ARCH_NATIVE

/*
 *  case 1 - fast path - all macros support NATIVE architecture only
 */

#define INT_GET(reference,arch) \
        (reference)
#define INT_SET(reference,arch,valueref) \
        ((reference) = (valueref))
#define INT_MOD(reference,arch,delta) \
        ((reference) += (delta))
#define INT_COPY(srcref,srcarch,dstref,dstarch) \
        ((dstref) = (srcref))
#define INT_ISZERO(reference,arch) \
	((reference) == 0)
#define INT_ZERO(reference,arch) \
	((reference) = 0)

#else

/* if we're not NATIVE, we need the INT_SWAP macro */

/* include endian swap macros */

#include <asm/byteorder.h>

#define INT_SWAP16(A) ((typeof(A))(__swab16((__u16)A)))
#define INT_SWAP32(A) ((typeof(A))(__swab32((__u32)A)))
#define INT_SWAP64(A) ((typeof(A))(__swab64((__u64)A)))

#define INT_SWAP(type, var) \
  ((sizeof(type) == 8) ? INT_SWAP64(var) : \
  ((sizeof(type) == 4) ? INT_SWAP32(var) : \
  ((sizeof(type) == 2) ? INT_SWAP16(var) : \
  (var))))
  
#endif

/*
 *  case 2 - support only MIPS XFS on X86. 
 *           Therefore we always endian swap and ignore the architectures
 */
#ifdef CONFIG_XFS_ARCH_MIPS
 
#define INT_GET(reference,arch) \
    INT_SWAP((reference),(reference))
    
#define INT_SET(reference,arch,valueref) \
    ( \
        ((reference) = (valueref)), \
        ((reference) = INT_SWAP((reference),(reference))), \
        (valueref) \
    )
#define INT_MOD(reference,arch,delta) \
    ( \
        ((reference) = INT_GET((reference),arch)) , \
        ((reference) += (delta)) , \
        INT_SET(reference, arch, reference) \
    )
#define INT_COPY(srcref,srcarch,dstref,dstarch) \
    (dstref) = INT_GET(srcref,srcarch)
#define INT_ISZERO(reference,arch) \
	((reference) == 0)
#define INT_ZERO(reference,arch) \
	((reference) = 0)
 
#endif       

/*
 *   case 3 - support multiple architectures
 *            we have to check architectures and swap accordinly
 */
#ifdef CONFIG_XFS_ARCH_MULTI
 
#define INT_GET(reference,arch) \
    (((arch) == XFS_ARCH_NATIVE) \
        ? \
            (reference) \
        : \
            INT_SWAP((reference),(reference)) \
    )
    
#define INT_SET(reference,arch,valueref) \
    ( \
        ((reference) = (valueref)), \
        ( \
           ((arch) != XFS_ARCH_NATIVE) ? \
               (reference) = INT_SWAP((reference),(reference)) \
           : 0 \
        ), \
        (valueref) \
    )

#define INT_MOD(reference,arch,delta) \
    (((arch) == XFS_ARCH_NATIVE) \
        ? \
            ((reference) += (delta)) \
        : \
            ( \
                (reference) = INT_GET((reference),arch) , \
                ((reference) += (delta)) , \
                INT_SET(reference, arch, reference) \
            ) \
    )
    
#define INT_COPY(srcref,srcarch,dstref,dstarch) \
    if ((dstarch) == XFS_ARCH_NATIVE) { \
        (dstref) = INT_GET(srcref,srcarch); \
    } else if ((srcarch) == XFS_ARCH_NATIVE) { \
        INT_SET(dstref,dstarch,srcref); \
    } else { \
        (dstref) = INT_GET(srcref,srcarch); \
        INT_SET(dstref,dstarch,dstref); \
    }

#define INT_ISZERO(reference,arch) \
	((reference) == 0)
    
#define INT_ZERO(reference,arch) \
	((reference) = 0)

#endif
        
#endif /* __XFS_ARCH_H__ */


