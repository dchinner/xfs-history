#ifndef __XFS_ARCH_H__
#define __XFS_ARCH_H__

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

#ident "$Id$"

/* includes */

#include <linux/autoconf.h>
#include "xfs_types.h"
#include <endian.h>

/* supported architecures */
  
#define ARCH_MIPS	0
#define ARCH_INTEL_IA32	1
#define ARCH_SPARC	2
#define ARCH_UNKNOWN    127

/* if these are wrong, it's very foncusing */

#if !defined(__LITTLE_ENDIAN) || !defined(__BIG_ENDIAN) || !defined(__BYTE_ORDER)
#error endian defines are screwy
#endif

#ifndef XFS_BIG_FILESYSTEMS
#error XFS_BIG_FILESYSTEMS must be defined true or false
#endif

/* select native architecture */
  
#ifdef CONFIG_X86
#define ARCH_NOCONVERT ARCH_INTEL_IA32

#if __BYTE_ORDER == __BIG_ENDIAN
#error big endian X86!?
#endif

#elif defined(__sparc__)
#define ARCH_NOCONVERT ARCH_SPARC
#else
#error attempt to define XFS native architecture on non-supported platform
#endif

/* supported modes */

#define XFS_ARCH_MODE_MIPS 0
#define XFS_ARCH_MODE_NATIVE 1
#define XFS_ARCH_MODE_MULTI 2

/*
 * development hack:
 * define one of:
 *    OVERRIDE_ARCH_MIPS
 *    OVERRIDE_ARCH_NATIVE
 *    OVERRIDE_ARCH_MULTI
 *
 * to override the CONFIG_XFS_ARCH parameters
 */

#ifndef XFS_ARCH_MODE

#ifdef OVERRIDE_ARCH_MIPS
#define XFS_ARCH_MODE XFS_ARCH_MODE_MIPS
#define XFS_MODE "O-MIPS"
#endif

#ifdef OVERRIDE_ARCH_MULTI
#define XFS_ARCH_MODE XFS_ARCH_MODE_MULTI
#define XFS_MODE "O-MULTI"
#endif

#ifdef OVERRIDE_ARCH_NATIVE
#define XFS_ARCH_MODE XFS_ARCH_MODE_NATIVE
#define XFS_MODE "O-NATIVE"
#endif
#endif

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
 
#ifndef XFS_ARCH_MODE

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

#ifdef CONFIG_XFS_ARCH_MIPS
#define XFS_ARCH_MODE XFS_ARCH_MODE_MIPS
#define XFS_MODE "MIPS"
#endif

#ifdef CONFIG_XFS_ARCH_NATIVE
#define XFS_ARCH_MODE XFS_ARCH_MODE_NATIVE
#define XFS_MODE "NATIVE"
#endif

#ifdef CONFIG_XFS_ARCH_MULTI
#error xfs architecture mode "multi" not currently supported
/* #define XFS_ARCH_MODE XFS_ARCH_MODE_MULTI */
/* #define XFS_MODE "MULTI" */
#endif

#endif

#if XFS_ARCH_MODE == XFS_ARCH_MODE_MULTI
#define ARCH_SUPPORTED(A) ((A)==ARCH_MIPS||(A)==ARCH_NOCONVERT)
#define XFS_ARCH_DEFAULT (ARCH_NOCONVERT)
#else
#if XFS_ARCH_MODE == XFS_ARCH_MODE_NATIVE
#define ARCH_SUPPORTED(A) ((A)==ARCH_NOCONVERT)
#define XFS_ARCH_DEFAULT (ARCH_NOCONVERT)
#else
#if XFS_ARCH_MODE == XFS_ARCH_MODE_MIPS
#define ARCH_SUPPORTED(A) ((A)==ARCH_MIPS)
#define XFS_ARCH_DEFAULT (ARCH_MIPS)
#else
#error Error in XFS architecture mode selection
#endif
#endif
#endif
   
/* generic swapping macros */

#define INT_SWAP16(A) ((typeof(A))(__swab16((__u16)A)))
#define INT_SWAP32(A) ((typeof(A))(__swab32((__u32)A)))
#define INT_SWAP64(A) ((typeof(A))(__swab64((__u64)A)))

#define INT_SWAP(type, var) \
    ((sizeof(type) == 8) ? INT_SWAP64(var) : \
    ((sizeof(type) == 4) ? INT_SWAP32(var) : \
    ((sizeof(type) == 2) ? INT_SWAP16(var) : \
    (var))))
  

#define INT_SWAP_UNALIGNED_32(from,to) \
    { \
        ((__u8*)(to))[0] = ((__u8*)(from))[3]; \
        ((__u8*)(to))[1] = ((__u8*)(from))[2]; \
        ((__u8*)(to))[2] = ((__u8*)(from))[1]; \
        ((__u8*)(to))[3] = ((__u8*)(from))[0]; \
    }

#define INT_SWAP_UNALIGNED_64(from,to) \
    { \
        INT_SWAP_UNALIGNED_32( ((__u8*)(from)) + 4, ((__u8*)(to))); \
        INT_SWAP_UNALIGNED_32( ((__u8*)(from)), ((__u8*)(to)) + 4); \
    }

/* 
 * get and set integers from potentially unaligned locations
 */
        
#define INT_GET_UNALIGNED_16_LE(pointer) \
   ((__u16)((((__u8*)(pointer))[0]      ) | (((__u8*)(pointer))[1] << 8 )))
#define INT_GET_UNALIGNED_16_BE(pointer) \
   ((__u16)((((__u8*)(pointer))[0] << 8) | (((__u8*)(pointer))[1])))
#define INT_SET_UNALIGNED_16_LE(pointer,value) \
    { \
        ((__u8*)(pointer))[0] = (((value)     ) & 0xff); \
        ((__u8*)(pointer))[1] = (((value) >> 8) & 0xff); \
    }
#define INT_SET_UNALIGNED_16_BE(pointer,value) \
    { \
        ((__u8*)(pointer))[0] = (((value) >> 8) & 0xff); \
        ((__u8*)(pointer))[1] = (((value)     ) & 0xff); \
    }
   
#define INT_GET_UNALIGNED_32_LE(pointer) \
   ((__u32)((((__u8*)(pointer))[0]      ) | (((__u8*)(pointer))[1] << 8 ) \
           |(((__u8*)(pointer))[2] << 16) | (((__u8*)(pointer))[3] << 24)))
#define INT_GET_UNALIGNED_32_BE(pointer) \
   ((__u32)((((__u8*)(pointer))[0] << 24) | (((__u8*)(pointer))[1] << 16) \
           |(((__u8*)(pointer))[2] << 8)  | (((__u8*)(pointer))[3]      )))
    
#define INT_GET_UNALIGNED_64_LE(pointer) \
   (((__u64)(INT_GET_UNALIGNED_32_LE(((__u8*)(pointer))+4)) << 32 ) \
   |((__u64)(INT_GET_UNALIGNED_32_LE(((__u8*)(pointer))  ))       ))
#define INT_GET_UNALIGNED_64_BE(pointer) \
   (((__u64)(INT_GET_UNALIGNED_32_BE(((__u8*)(pointer))  )) << 32  ) \
   |((__u64)(INT_GET_UNALIGNED_32_BE(((__u8*)(pointer))+4))        ))
   
/*
 * now pick the right ones for our MACHINE ARCHITECTURE
 */
   
#if __BYTE_ORDER == __LITTLE_ENDIAN 
#define INT_GET_UNALIGNED_16(pointer)       INT_GET_UNALIGNED_16_LE(pointer)
#define INT_SET_UNALIGNED_16(pointer,value) INT_SET_UNALIGNED_16_LE(pointer,value)
#define INT_GET_UNALIGNED_32(pointer)       INT_GET_UNALIGNED_32_LE(pointer)
#define INT_GET_UNALIGNED_64(pointer)       INT_GET_UNALIGNED_64_LE(pointer)
#else
#define INT_GET_UNALIGNED_16(pointer)       INT_GET_UNALIGNED_16_BE(pointer)
#define INT_SET_UNALIGNED_16(pointer,value) INT_SET_UNALIGNED_16_BE(pointer,value)
#define INT_GET_UNALIGNED_32(pointer)       INT_GET_UNALIGNED_32_BE(pointer)
#define INT_GET_UNALIGNED_64(pointer)       INT_GET_UNALIGNED_64_BE(pointer)
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

#if XFS_ARCH_MODE == XFS_ARCH_MODE_NATIVE

/*
 *  case 1 - fast path - all macros support NATIVE MACHINE architecture only
 */
 
#define ARCH_GET(REF) (ARCH_NOCONVERT)

#define INT_GET(reference,arch) \
    (reference)
#define INT_SET(reference,arch,valueref) \
    ((reference) = (valueref))
#define INT_MODX(reference,arch,code) \
    ((reference) code)
#define INT_MOD(reference,arch,delta) \
    INT_MODX(reference,arch,+=(delta))
#define INT_COPY(srcref,srcarch,dstref,dstarch) \
    ((dstref) = (srcref))
#define INT_ISZERO(reference,arch) \
    ((reference) == 0)
#define INT_ZERO(reference,arch) \
    ((reference) = 0)
    
#define DIRINO4_GET_ARCH(pointer,arch) \
    (INT_GET_UNALIGNED_32(pointer))

#if XFS_BIG_FILESYSTEMS
#define DIRINO_GET_ARCH(pointer,arch) \
    INT_GET_UNALIGNED_64(pointer)
#else
/* MACHINE ARCHITECTURE dependent */
#if __BYTE_ORDER == __LITTLE_ENDIAN 
#define DIRINO_GET_ARCH(pointer,arch) \
    DIRINO4_GET_ARCH((((__u8*)pointer)+4),arch)
#else
#define DIRINO_GET_ARCH(pointer,arch) \
    DIRINO4_GET_ARCH(pointer,arch)
#endif
#endif    

#define DIRINO_COPY_ARCH(from,to,arch) \
    bcopy(from,to,sizeof(xfs_dir_ino_t))
#define DIRINO4_COPY_ARCH(from,to,arch) \
    bcopy(from,to,sizeof(xfs_dir2_ino4_t))
    
#define INT_GET_UNALIGNED_16_ARCH(pointer,arch) \
    INT_GET_UNALIGNED_16(pointer)
#define INT_SET_UNALIGNED_16_ARCH(pointer,value,arch) \
    INT_SET_UNALIGNED_16(pointer,value)
    
#endif

/*
 *  case 2 - support only MIPS XFS on X86. 
 *           Therefore we always endian swap and ignore the architectures
 *
 *   case 3 - support multiple architectures
 *            we have to check architectures and swap accordinly
 *
 * assumptions:
 *   - only two architectures, ARCH_MIPS and ARCH_INTEL_IA32
 *   - ARCH_NOCONVERT == ARCH_INTEL_IA32
 *   - ARCH_UNKNOWN implies ARCH_MIPS
 *
 * given only two architectures, and since we _must_ check against
 * ARCH_NOCONVERT in both cases, we can use the same macros here
 *
 */
 
#if XFS_ARCH_MODE == XFS_ARCH_MODE_MIPS || XFS_ARCH_MODE == XFS_ARCH_MODE_MULTI

#if XFS_ARCH_MODE == XFS_ARCH_MODE_MIPS
#define ARCH_GET(REF) (ARCH_MIPS)
#endif
    
#if XFS_ARCH_MODE == XFS_ARCH_MODE_MULTI
#define ARCH_GET(REF) (REF)
#endif
 
#define INT_GET(reference,arch) \
    (((arch) == ARCH_NOCONVERT) \
        ? \
            (reference) \
        : \
            INT_SWAP((reference),(reference)) \
    )
    
#define INT_SET(reference,arch,valueref) \
    ( \
        ((reference) = (valueref)), \
        ( \
           ((arch) != ARCH_NOCONVERT) ? \
               (reference) = INT_SWAP((reference),(reference)) \
           : 0 \
        ), \
        INT_GET(reference, arch) \
    )

#define INT_MODX(reference,arch,code) \
    (((arch) == ARCH_NOCONVERT) \
        ? \
            ((reference) code) \
        : \
            ( \
                (reference) = INT_GET((reference),arch) , \
                ((reference) code), \
                INT_SET(reference, arch, reference) \
            ) \
    )
#define INT_MOD(reference,arch,delta) \
    INT_MODX(reference,arch,+=(delta))
    
#define INT_COPY(srcref,srcarch,dstref,dstarch) \
    if ((dstarch) == ARCH_NOCONVERT) { \
        (dstref) = INT_GET(srcref,srcarch); \
    } else if ((srcarch) == ARCH_NOCONVERT) { \
        INT_SET(dstref,dstarch,srcref); \
    } else { \
        (dstref) = INT_GET(srcref,srcarch); \
        INT_SET(dstref,dstarch,dstref); \
    }

#define INT_ISZERO(reference,arch) \
    ((reference) == 0)
    
#define INT_ZERO(reference,arch) \
    ((reference) = 0)
    
#define DIRINO4_GET_ARCH(pointer,arch) \
    ( ((arch) == ARCH_NOCONVERT) \
        ? \
            (INT_GET_UNALIGNED_32(pointer)) \
        : \
            (INT_GET_UNALIGNED_32_BE(pointer)) \
    )
    
#if XFS_BIG_FILESYSTEMS
#define DIRINO_GET_ARCH(pointer,arch) \
    ( ((arch) == ARCH_NOCONVERT) \
        ? \
            (INT_GET_UNALIGNED_64(pointer)) \
        : \
            (INT_GET_UNALIGNED_64_BE(pointer)) \
    )
#else
/* MACHINE ARCHITECTURE dependent */
#if __BYTE_ORDER == __LITTLE_ENDIAN 
#define DIRINO_GET_ARCH(pointer,arch) \
    DIRINO4_GET_ARCH((((__u8*)pointer)+4),arch)
#else
#define DIRINO_GET_ARCH(pointer,arch) \
    DIRINO4_GET_ARCH(pointer,arch)
#endif
#endif    

#define DIRINO_COPY_ARCH(from,to,arch) \
    if ((arch) == ARCH_NOCONVERT) { \
        bcopy(from,to,sizeof(xfs_ino_t)); \
    } else { \
        INT_SWAP_UNALIGNED_64(from,to); \
    }
#define DIRINO4_COPY_ARCH(from,to,arch) \
    if ((arch) == ARCH_NOCONVERT) { \
        bcopy(from,to,sizeof(xfs_dir2_ino4_t)); \
    } else { \
        INT_SWAP_UNALIGNED_32(from,to); \
    }
    
#define INT_GET_UNALIGNED_16_ARCH(pointer,arch) \
    ( ((arch) == ARCH_NOCONVERT) \
        ? \
            (INT_GET_UNALIGNED_16(pointer)) \
        : \
            (INT_GET_UNALIGNED_16_BE(pointer)) \
    )
#define INT_SET_UNALIGNED_16_ARCH(pointer,value,arch) \
    if ((arch) == ARCH_NOCONVERT) { \
        INT_SET_UNALIGNED_16(pointer,value); \
    } else { \
        INT_SET_UNALIGNED_16_BE(pointer,value); \
    }

#endif
            
#endif /* __XFS_ARCH_H__ */


