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
 
#ifndef SIM
#include <linux/autoconf.h>
#else
#define CONFIG_XFS_ARCH_MULTI
#endif

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

/* generic */

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
 * get a 16/32/64 bit unsigned number from a potentially unaligned address 
 * both little and big endian versions
 */
        
#define INT_GET_UNALIGNED_16_LE(pointer) \
   ((__u16)((((__u8*)(pointer))[0]      ) | (((__u8*)(pointer))[1] << 8 )))
#define INT_GET_UNALIGNED_32_LE(pointer) \
   ((__u32)((((__u8*)(pointer))[0]      ) | (((__u8*)(pointer))[1] << 8 ) \
           |(((__u8*)(pointer))[2] << 16) | (((__u8*)(pointer))[3] << 24)))
#define INT_GET_UNALIGNED_64_LE(pointer) \
   (((__u64)(INT_GET_UNALIGNED_32_LE(((__u8*)(pointer))+4)) << 32 ) \
   |((__u64)(INT_GET_UNALIGNED_32_LE(((__u8*)(pointer))  ))       ))
   
#define INT_GET_UNALIGNED_16_BE(pointer) \
   ((__u16)((((__u8*)(pointer))[0] << 8) | (((__u8*)(pointer))[1])))
#define INT_GET_UNALIGNED_32_BE(pointer) \
   ((__u32)((((__u8*)(pointer))[0] << 24) | (((__u8*)(pointer))[1] << 16) \
           |(((__u8*)(pointer))[2] << 8)  | (((__u8*)(pointer))[3]      )))
#define INT_GET_UNALIGNED_64_BE(pointer) \
   (((__u64)(INT_GET_UNALIGNED_32_BE(((__u8*)(pointer))  )) << 32  ) \
   |((__u64)(INT_GET_UNALIGNED_32_BE(((__u8*)(pointer))+4))        ))
   
/*
 * now pick the right ones for our MACHINE ARCHITECTURE
 */
   
#ifdef __LITTLE_ENDIAN 
#define INT_GET_UNALIGNED_16_NATIVE(pointer) INT_GET_UNALIGNED_16_LE(pointer)
#define INT_GET_UNALIGNED_32_NATIVE(pointer) INT_GET_UNALIGNED_32_LE(pointer)
#define INT_GET_UNALIGNED_64_NATIVE(pointer) INT_GET_UNALIGNED_64_LE(pointer)
#else
#define INT_GET_UNALIGNED_16_NATIVE(pointer) INT_GET_UNALIGNED_16_BE(pointer)
#define INT_GET_UNALIGNED_32_NATIVE(pointer) INT_GET_UNALIGNED_32_BE(pointer)
#define INT_GET_UNALIGNED_64_NATIVE(pointer) INT_GET_UNALIGNED_64_BE(pointer)
#endif

/*
 * put a 16 bit unsigned number to a potentially unaligned address
 * both little and big versions
 */
 
#define INT_SET_UNALIGNED_16_BE(pointer,value) \
    { \
        ((__u8*)(pointer))[0] = (((value) >> 8) & 0xff); \
        ((__u8*)(pointer))[1] = (((value)     ) & 0xff); \
    }
#define INT_SET_UNALIGNED_16_LE(pointer,value) \
    { \
        ((__u8*)(pointer))[0] = (((value)     ) & 0xff); \
        ((__u8*)(pointer))[1] = (((value) >> 8) & 0xff); \
    }
 
/*
 * now pick the right ones for our MACHINE ARCHITECTURE
 */
   
#ifdef __LITTLE_ENDIAN 
#define INT_SET_UNALIGNED_16_NATIVE(pointer,value) INT_SET_UNALIGNED_16_LE(pointer,value)
#else
#define INT_SET_UNALIGNED_16_NATIVE(pointer,value) INT_SET_UNALIGNED_16_BE(pointer,value)
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
   
#define DIRINO4_GET(pointer,arch) \
    INT_GET_UNALIGNED_32_NATIVE(pointer)
    
#ifdef XFS_BIG_FILESYSTEMS
#define DIRINO_GET(pointer,arch) \
    INT_GET_UNALIGNED_64_NATIVE(pointer)
#else
#define DIRINO_GET(pointer,arch) \
    DIRINO4_GET(pointer,arch)
#endif    

#define DIRINO_COPY(from,to,arch) \
    bcopy(from,to,sizeof(xfs_dir_ino_t))
#define DIRINO4_COPY(from,to,arch) \
    bcopy(from,to,sizeof(xfs_dir2_ino4_t))
    
#define INT_GET_UNALIGNED_16(pointer,arch) \
    INT_GET_UNALIGNED_16_NATIVE(pointer)
#define INT_SET_UNALIGNED_16(pointer,value,arch) \
    INT_SET_UNALIGNED_16_NATIVE(pointer,value)

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
        
#define DIRINO4_GET(pointer,arch) \
    INT_GET_UNALIGNED_32_BE(pointer)
    
#ifdef XFS_BIG_FILESYSTEMS
#define DIRINO_GET(pointer,arch) \
    INT_GET_UNALIGNED_64_BE(pointer)
#else
#define DIRINO_GET(pointer,arch) \
    DIRINO4_GET(pointer,arch)
#endif    

#define DIRINO_COPY(from,to,arch) \
    INT_SWAP_UNALIGNED_64(from,to)
#define DIRINO4_COPY(from,to,arch) \
    INT_SWAP_UNALIGNED_32(from,to)
    
#define INT_GET_UNALIGNED_16(pointer,arch) \
    INT_GET_UNALIGNED_16_BE(pointer)
#define INT_SET_UNALIGNED_16(pointer,value,arch) \
    INT_SET_UNALIGNED_16_BE(pointer,value)
 
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
    
#define DIRINO4_GET(pointer,arch) \
    ( ((arch) == ARCH_MIPS) \
        ? \
            (INT_GET_UNALIGNED_32_BE(pointer)) \
        : \
            (INT_GET_UNALIGNED_32_LE(pointer)) \
    )
    
#ifdef XFS_BIG_FILESYSTEMS
#define DIRINO_GET(pointer,arch) \
    ( ((arch) == ARCH_MIPS) \
        ? \
            (INT_GET_UNALIGNED_64_BE(pointer)) \
        : \
            (INT_GET_UNALIGNED_64_LE(pointer)) \
    )
#else
#define DIRINO_GET(pointer,arch) \
    DIRINO4_GET(pointer,arch)
#endif    

#define DIRINO_COPY(from,to,arch) \
    if ((arch) == XFS_ARCH_NATIVE) { \
        bcopy(from,to,sizeof(xfs_ino_t)); \
    } else { \
        INT_SWAP_UNALIGNED_64(from,to); \
    }
#define DIRINO4_COPY(from,to,arch) \
    if ((arch) == XFS_ARCH_NATIVE) { \
        bcopy(from,to,sizeof(xfs_dir2_ino4_t)); \
    } else { \
        INT_SWAP_UNALIGNED_32(from,to); \
    }
    
#define INT_GET_UNALIGNED_16(pointer,arch) \
    ( ((arch) == ARCH_MIPS) \
        ? \
            (INT_GET_UNALIGNED_16_BE(pointer)) \
        : \
            (INT_GET_UNALIGNED_16_LE(pointer)) \
    )
#define INT_SET_UNALIGNED_16(pointer,value,arch) \
    if ((arch) == ARCH_MIPS) { \
        INT_SET_UNALIGNED_16_BE(pointer,value); \
    } else { \
        INT_SET_UNALIGNED_16_LE(pointer,value); \
    }

#endif
        
#endif /* __XFS_ARCH_H__ */


