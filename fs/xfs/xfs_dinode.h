
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
#ifndef _FS_XFS_DINODE_H
#define	_FS_XFS_DINODE_H

#include <xfs_arch.h>

#ident "$Revision$"

struct xfs_buf;
struct xfs_mount;

#define	XFS_DINODE_VERSION_1	1
#define	XFS_DINODE_VERSION_2	2
#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_DINODE_GOOD_VERSION)
int xfs_dinode_good_version(int v);
#define XFS_DINODE_GOOD_VERSION(v)	xfs_dinode_good_version(v)
#else
#define XFS_DINODE_GOOD_VERSION(v)	(((v) == XFS_DINODE_VERSION_1) || \
					 ((v) == XFS_DINODE_VERSION_2))
#endif
#define	XFS_DINODE_MAGIC	0x494e	/* 'IN' */

/*
 * Disk inode structure.
 * This is just the header; the inode is expanded to fill a variable size
 * with the last field expanding.  It is split into the core and "other"
 * because we only need the core part in the in-core inode.
 */
typedef struct xfs_timestamp {
	__int32_t	t_sec;		/* timestamp seconds */
	__int32_t	t_nsec;		/* timestamp nanoseconds */
} xfs_timestamp_t;

/*
 * Note: Coordinate changes to this structure with the XFS_DI_* #defines
 * below and the offsets table in xfs_ialloc_log_di().
 */
typedef struct xfs_dinode_core
{
	__uint16_t	di_magic;	/* inode magic # = XFS_DINODE_MAGIC */
	__uint16_t	di_mode;	/* mode and type of file */
	__int8_t	di_version;	/* inode version */
	__int8_t	di_format;	/* format of di_c data */
	__uint16_t	di_onlink;	/* old number of links to file */
	__uint32_t	di_uid;		/* owner's user id */
	__uint32_t	di_gid;		/* owner's group id */
	__uint32_t	di_nlink;	/* number of links to file */
	__uint16_t	di_projid;	/* owner's project id */
	__uint8_t	di_pad[10];	/* unused, zeroed space */
	xfs_timestamp_t	di_atime;	/* time last accessed */
	xfs_timestamp_t	di_mtime;	/* time last modified */
	xfs_timestamp_t	di_ctime;	/* time created/inode modified */
	xfs_fsize_t	di_size;	/* number of bytes in file */
	xfs_drfsbno_t	di_nblocks;	/* # of direct & btree blocks used */
	xfs_extlen_t	di_extsize;	/* basic/minimum extent size for file */
	xfs_extnum_t	di_nextents;	/* number of extents in data fork */
	xfs_aextnum_t	di_anextents;	/* number of extents in attribute fork*/
	__uint8_t	di_forkoff;	/* attr fork offs, <<3 for 64b align */
	__int8_t	di_aformat;	/* format of attr fork's data */
	__uint32_t	di_dmevmask;	/* DMIG event mask */
	__uint16_t	di_dmstate;	/* DMIG state info */
	__uint16_t	di_flags;	/* random flags, XFS_DIFLAG_... */
	__uint32_t	di_gen;		/* generation number */
} xfs_dinode_core_t;

typedef struct xfs_dinode
{
	xfs_dinode_core_t	di_core;
	/*
	 * In adding anything between the core and the union, be
	 * sure to update the macros like XFS_LITINO below and
	 * XFS_BMAP_RBLOCK_DSIZE in xfs_bmap_btree.h.
	 */
	xfs_agino_t		di_next_unlinked;/* agi unlinked list ptr */
	union {
		xfs_bmdr_block_t di_bmbt;	/* btree root block */
		xfs_bmbt_rec_32_t di_bmx[1];	/* extent list */
		xfs_dir_shortform_t di_dirsf;	/* shortform directory */
		xfs_dir2_sf_t	di_dir2sf;	/* shortform directory v2 */
		char		di_c[1];	/* local contents */
		dev_t		di_dev;		/* device for IFCHR/IFBLK */
		uuid_t		di_muuid;	/* mount point value */
		char		di_symlink[1];	/* local symbolic link */
	}		di_u;
	union {
		xfs_bmdr_block_t di_abmbt;	/* btree root block */
		xfs_bmbt_rec_32_t di_abmx[1];	/* extent list */
		xfs_attr_shortform_t di_attrsf;	/* shortform attribute list */
	}		di_a;
} xfs_dinode_t;

/*
 * The 32 bit link count in the inode theoretically maxes out at UINT_MAX.
 * Since the pathconf interface is signed, we use 2^31 - 1 instead.
 * The old inode format had a 16 bit link count, so its maximum is USHRT_MAX.
 */
#define	XFS_MAXLINK		((1U << 31) - 1U)
#define	XFS_MAXLINK_1		65535U

/*
 * Bit names for logging disk inodes only
 */
#define	XFS_DI_MAGIC		0x0000001
#define	XFS_DI_MODE		0x0000002
#define	XFS_DI_VERSION		0x0000004
#define	XFS_DI_FORMAT		0x0000008
#define	XFS_DI_ONLINK		0x0000010
#define	XFS_DI_UID		0x0000020
#define	XFS_DI_GID		0x0000040
#define	XFS_DI_NLINK		0x0000080
#define	XFS_DI_PROJID		0x0000100
#define	XFS_DI_PAD		0x0000200
#define	XFS_DI_ATIME		0x0000400
#define	XFS_DI_MTIME		0x0000800
#define	XFS_DI_CTIME		0x0001000
#define	XFS_DI_SIZE		0x0002000
#define	XFS_DI_NBLOCKS		0x0004000
#define	XFS_DI_EXTSIZE		0x0008000
#define	XFS_DI_NEXTENTS		0x0010000
#define	XFS_DI_NAEXTENTS	0x0020000
#define	XFS_DI_FORKOFF		0x0040000
#define	XFS_DI_AFORMAT		0x0080000
#define	XFS_DI_DMEVMASK		0x0100000
#define	XFS_DI_DMSTATE		0x0200000
#define	XFS_DI_FLAGS		0x0400000
#define	XFS_DI_GEN		0x0800000
#define	XFS_DI_NEXT_UNLINKED	0x1000000
#define	XFS_DI_U		0x2000000
#define	XFS_DI_A		0x4000000
#define	XFS_DI_NUM_BITS		27
#define	XFS_DI_ALL_BITS		((1 << XFS_DI_NUM_BITS) - 1)
#define	XFS_DI_CORE_BITS	(XFS_DI_ALL_BITS & ~(XFS_DI_U|XFS_DI_A))

/*
 * Values for di_format
 */
typedef enum xfs_dinode_fmt
{
	XFS_DINODE_FMT_DEV,		/* CHR, BLK: di_dev */
	XFS_DINODE_FMT_LOCAL,		/* DIR, REG: di_c */
					/* LNK: di_symlink */
	XFS_DINODE_FMT_EXTENTS,		/* DIR, REG, LNK: di_bmx */
	XFS_DINODE_FMT_BTREE,		/* DIR, REG, LNK: di_bmbt */
	XFS_DINODE_FMT_UUID 		/* MNT: di_uuid */
} xfs_dinode_fmt_t;

/*
 * Inode minimum and maximum sizes.
 */
#define	XFS_DINODE_MIN_LOG	8
#define	XFS_DINODE_MAX_LOG	11
#define	XFS_DINODE_MIN_SIZE	(1 << XFS_DINODE_MIN_LOG)
#define	XFS_DINODE_MAX_SIZE	(1 << XFS_DINODE_MAX_LOG)

/*
 * Inode size for given fs.
 */
#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_LITINO)
int xfs_litino(struct xfs_mount *mp);
#define	XFS_LITINO(mp)		xfs_litino(mp)
#else
#define	XFS_LITINO(mp)	((mp)->m_litino)
#endif
#define	XFS_BROOT_SIZE_ADJ	\
	(sizeof(xfs_bmbt_block_t) - sizeof(xfs_bmdr_block_t))

/*
 * Fork identifiers.  Here so utilities can use them without including
 * xfs_inode.h.
 */
#define	XFS_DATA_FORK	0
#define	XFS_ATTR_FORK	1

/*
 * temporary defines
 */

#define XFS_FGET1(a,b) (a)
#define XFS_FSET1(a,b,c) ((a) = (c))

#define XFS_FGET2(a,b) INT_GET(a,b)
#define XFS_FSET2(a,b,c) INT_SET(a,b,c)

/*
 * Inode data & attribute fork sizes, per inode.
 */
#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_CFORK_Q)
int xfs_cfork_q_arch(xfs_dinode_core_t *dcp, xfs_arch_t arch);
#define	XFS_CFORK_Q_ARCH(dcp,arch)          xfs_cfork_q_arch(dcp,arch)
#define	XFS_CFORK_Q(dcp)                    XFS_CFORK_Q_ARCH(dcp,XFS_ARCH_NATIVE)
#else
#define	XFS_CFORK_Q_X(dcp,f,arch)	    (f((dcp)->di_forkoff, arch) != 0)
#define XFS_CFORK_Q(dcp)                    XFS_CFORK_Q_X(dcp,XFS_FGET1,0)
#define XFS_CFORK_Q_ARCH(dcp,arch)          XFS_CFORK_Q_X(dcp,XFS_FGET2,arch)
#endif
#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_CFORK_BOFF)
int xfs_cfork_boff_arch(xfs_dinode_core_t *dcp, xfs_arch_t arch);
#define	XFS_CFORK_BOFF_ARCH(dcp,arch)	    xfs_cfork_boff_arch(dcp,arch)
#define	XFS_CFORK_BOFF(dcp)	            XFS_CFORK_BOFF(dcp,XFS_ARCH_NATIVE)
#else
#define	XFS_CFORK_BOFF_X(dcp,f,arch)	    ((int)(f((dcp)->di_forkoff, arch) << 3))
#define XFS_CFORK_BOFF(dcp)                 XFS_CFORK_BOFF_X(dcp,XFS_FGET1,0)
#define XFS_CFORK_BOFF_ARCH(dcp,arch)       XFS_CFORK_BOFF_X(dcp,XFS_FGET2,arch)
#endif
#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_CFORK_DSIZE)
int xfs_cfork_dsize_arch(xfs_dinode_core_t *dcp, struct xfs_mount *mp, xfs_arch_t arch);
#define	XFS_CFORK_DSIZE_ARCH(dcp,mp,arch)   xfs_cfork_dsize_arch(dcp,mp,arch)
#define	XFS_CFORK_DSIZE(dcp,mp)             XFS_CFORK_DSIZE_ARCH(dcp,mp,XFS_ARCH_NATIVE)
#else
#define	XFS_CFORK_DSIZE_X(dcp,mp,f,arch) \
	(XFS_CFORK_Q_ARCH(dcp, arch) ? XFS_CFORK_BOFF_ARCH(dcp, arch) : XFS_LITINO(mp))
#define XFS_CFORK_DSIZE(dcp,mp)             XFS_CFORK_DSIZE_X(dcp,mp,XFS_FGET1,0)
#define XFS_CFORK_DSIZE_ARCH(dcp,mp,arch)   XFS_CFORK_DSIZE_X(dcp,mp,XFS_FGET2,arch)
#endif
#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_CFORK_ASIZE)
int xfs_cfork_asize_arch(xfs_dinode_core_t *dcp, struct xfs_mount *mp, xfs_arch_t arch);
#define	XFS_CFORK_ASIZE_ARCH(dcp,mp,arch)   xfs_cfork_asize_arch(dcp,mp,arch)
#define	XFS_CFORK_ASIZE(dcp,mp)             XFS_CFORK_ASIZE_ARCH(dcp,mp,XFS_ARCH_NATIVE) 
#else
#define	XFS_CFORK_ASIZE_X(dcp,mp,f,arch) \
	(XFS_CFORK_Q_ARCH(dcp, arch) ? XFS_LITINO(mp) - XFS_CFORK_BOFF_ARCH(dcp, arch) : 0)
#define XFS_CFORK_ASIZE(dcp,mp)             XFS_CFORK_ASIZE_X(dcp,mp,XFS_FGET1,0)
#define XFS_CFORK_ASIZE_ARCH(dcp,mp,arch)   XFS_CFORK_ASIZE_X(dcp,mp,XFS_FGET2,arch)
#endif
#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_CFORK_SIZE)
int xfs_cfork_size_arch(xfs_dinode_core_t *dcp, struct xfs_mount *mp, int w, xfs_arch_t arch);
#define	XFS_CFORK_SIZE_ARCH(dcp,mp,w,arch)  xfs_cfork_size_arch(dcp,mp,w,arch)
#define	XFS_CFORK_SIZE(dcp,mp,w)            XFS_CFORK_SIZE_ARCH(dcp,mp,w,XFS_ARCH_NATIVE)
#else
#define	XFS_CFORK_SIZE_X(dcp,mp,w,f,arch) \
	((w) == XFS_DATA_FORK ? \
		XFS_CFORK_DSIZE_ARCH(dcp, mp, arch) : XFS_CFORK_ASIZE_ARCH(dcp, mp, arch))
#define XFS_CFORK_SIZE(dcp,mp,w)            XFS_CFORK_SIZE_X(dcp,mp,w,XFS_FGET1,0)
#define XFS_CFORK_SIZE_ARCH(dcp,mp,w,arch)  XFS_CFORK_SIZE_X(dcp,mp,w,XFS_FGET2,arch)
#endif

#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_DFORK_DSIZE)
int xfs_dfork_dsize_arch(xfs_dinode_t *dip, struct xfs_mount *mp, xfs_arch_t arch);
#define	XFS_DFORK_DSIZE_ARCH(dip,mp,arch)   xfs_dfork_dsize_arch(dip,mp,arch)
#define	XFS_DFORK_DSIZE(dip,mp)             XFS_DFORK_DSIZE_ARCH(dip,mp,XFS_ARCH_NATIVE)
#else
#define	XFS_DFORK_DSIZE_X(dip,mp,f,arch)    XFS_CFORK_DSIZE_ARCH(&(dip)->di_core, mp, arch)
#define XFS_DFORK_DSIZE(dip,mp)             XFS_DFORK_DSIZE_X(dip,mp,XFS_FGET1,0)
#define XFS_DFORK_DSIZE_ARCH(dip,mp,arch)   XFS_DFORK_DSIZE_X(dip,mp,XFS_FGET2,arch)
#endif
#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_DFORK_ASIZE)
int xfs_dfork_asize_arch(xfs_dinode_t *dip, struct xfs_mount *mp, xfs_arch_t arch);
#define	XFS_DFORK_ASIZE_ARCH(dip,mp,arch)   xfs_dfork_asize_arch(dip,mp,arch)
#define	XFS_DFORK_ASIZE(dip,mp)             XFS_DFORK_ASIZE_ARCH(dip,mp,XFS_ARCH_NATIVE)
#else
#define	XFS_DFORK_ASIZE_X(dip,mp,f,arch)    XFS_CFORK_ASIZE_ARCH(&(dip)->di_core, mp, arch)
#define XFS_DFORK_ASIZE(dip,mp)             XFS_DFORK_ASIZE_X(dip,mp,XFS_FGET1,0)
#define XFS_DFORK_ASIZE_ARCH(dip,mp,arch)   XFS_DFORK_ASIZE_X(dip,mp,XFS_FGET2,arch)
#endif
#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_DFORK_SIZE)
int xfs_dfork_size_arch(xfs_dinode_t *dip, struct xfs_mount *mp, int w, xfs_arch_t arch);
#define	XFS_DFORK_SIZE_ARCH(dip,mp,w,arch)  xfs_dfork_size_arch(dip,mp,w,arch)
#define	XFS_DFORK_SIZE(dip,mp,w)            XFS_DFORK_SIZE_ARCH(dip,mp,w,XFS_ARCH_NATIVE) 
#else
#define	XFS_DFORK_SIZE_X(dip,mp,w,f,arch)   XFS_CFORK_SIZE_ARCH(&(dip)->di_core, mp, w, arch)
#define XFS_DFORK_SIZE(dip,mp,w)            XFS_DFORK_SIZE_X(dip,mp,w,XFS_FGET1,0)
#define XFS_DFORK_SIZE_ARCH(dip,mp,w,arch)  XFS_DFORK_SIZE_X(dip,mp,w,XFS_FGET2,arch)
#endif

/*
 * Macros for accessing per-fork disk inode information.
 */
#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_DFORK_Q)
int xfs_dfork_q_arch(xfs_dinode_t *dip, xfs_arch_t arch);
#define	XFS_DFORK_Q_ARCH(dip,arch)	    xfs_dfork_q_arch(dip,arch)
#define	XFS_DFORK_Q(dip)	            XFS_DFORK_Q_ARCH(dip,XFS_ARCH_NATIVE)
#else
#define	XFS_DFORK_Q_X(dip,f,arch)	    XFS_CFORK_Q_ARCH(&(dip)->di_core, arch)
#define XFS_DFORK_Q(dip)                    XFS_DFORK_Q_X(dip,XFS_FGET1,0)
#define XFS_DFORK_Q_ARCH(dip,arch)          XFS_DFORK_Q_X(dip,XFS_FGET2,arch)
#endif
#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_DFORK_BOFF)
int xfs_dfork_boff_arch(xfs_dinode_t *dip, xfs_arch_t arch);
#define	XFS_DFORK_BOFF_ARCH(dip,arch)	    xfs_dfork_boff_arch(dip,arch)
#define	XFS_DFORK_BOFF(dip)	            XFS_DFORK_BOFF_ARCH(dip,XFS_ARCH_NATIVE)
#else
#define	XFS_DFORK_BOFF_X(dip,f,arch)	    XFS_CFORK_BOFF_ARCH(&(dip)->di_core, arch)
#define XFS_DFORK_BOFF(dip)                 XFS_DFORK_BOFF_X(dip,XFS_FGET1,0)
#define XFS_DFORK_BOFF_ARCH(dip,arch)       XFS_DFORK_BOFF_X(dip,XFS_FGET2,arch)
#endif
#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_DFORK_DPTR)
char *xfs_dfork_dptr_arch(xfs_dinode_t *dip, xfs_arch_t arch);
#define	XFS_DFORK_DPTR_ARCH(dip,arch)	    xfs_dfork_dptr_arch(dip,arch)
#define	XFS_DFORK_DPTR(dip)	            XFS_DFORK_DPTR_ARCH(dip,XFS_ARCH_NATIVE)
#else
#define	XFS_DFORK_DPTR_X(dip,f,arch)	((dip)->di_u.di_c)
#define XFS_DFORK_DPTR(dip)                 XFS_DFORK_DPTR_X(dip,XFS_FGET1,0)
#define XFS_DFORK_DPTR_ARCH(dip,arch)       XFS_DFORK_DPTR_X(dip,XFS_FGET2,arch)
#endif
#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_DFORK_APTR)
char *xfs_dfork_aptr_arch(xfs_dinode_t *dip, xfs_arch_t arch);
#define	XFS_DFORK_APTR_ARCH(dip,arch)       xfs_dfork_aptr_arch(dip,arch)
#define	XFS_DFORK_APTR(dip)                 XFS_DFORK_APTR_ARCH(dip,XFS_ARCH_NATIVE) 
#else
#define	XFS_DFORK_APTR_X(dip,f,arch)	((dip)->di_u.di_c + XFS_DFORK_BOFF_ARCH(dip, arch))
#define XFS_DFORK_APTR(dip)                 XFS_DFORK_APTR_X(dip,XFS_FGET1,0)
#define XFS_DFORK_APTR_ARCH(dip,arch)       XFS_DFORK_APTR_X(dip,XFS_FGET2,arch)
#endif
#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_DFORK_PTR)
char *xfs_dfork_ptr_arch(xfs_dinode_t *dip, int w, xfs_arch_t arch);
#define	XFS_DFORK_PTR_ARCH(dip,w,arch)      xfs_dfork_ptr_arch(dip,w,arch)
#define	XFS_DFORK_PTR(dip,w)                XFS_DFORK_PTR_ARCH(dip,w,XFS_ARCH_NATIVE)
#else
#define	XFS_DFORK_PTR_X(dip,w,f,arch)	\
	((w) == XFS_DATA_FORK ? XFS_DFORK_DPTR_ARCH(dip, arch) : XFS_DFORK_APTR_ARCH(dip, arch))
#define XFS_DFORK_PTR(dip,w)                XFS_DFORK_PTR_X(dip,w,XFS_FGET1,0)
#define XFS_DFORK_PTR_ARCH(dip,w,arch)      XFS_DFORK_PTR_X(dip,w,XFS_FGET2,arch)
#endif
#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_CFORK_FORMAT)
int xfs_cfork_format_arch(xfs_dinode_core_t *dcp, int w, xfs_arch_t arch);
#define	XFS_CFORK_FORMAT_ARCH(dcp,w,arch)   xfs_cfork_format_arch(dcp,w,arch)
#define	XFS_CFORK_FORMAT(dcp,w)             XFS_CFORK_FORMAT_ARCH(dcp,w,XFS_ARCH_NATIVE)
#else
#define	XFS_CFORK_FORMAT_X(dcp,w,f,arch) \
	((w) == XFS_DATA_FORK ? f((dcp)->di_format, arch) : f((dcp)->di_aformat, arch))
#define XFS_CFORK_FORMAT(dcp,w)             XFS_CFORK_FORMAT_X(dcp,w,XFS_FGET1,0)
#define XFS_CFORK_FORMAT_ARCH(dcp,w,arch)   XFS_CFORK_FORMAT_X(dcp,w,XFS_FGET2,arch)
#endif
#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_CFORK_FMT_SET)
void xfs_cfork_fmt_set_arch(xfs_dinode_core_t *dcp, int w, int n, xfs_arch_t arch);
#define	XFS_CFORK_FMT_SET_ARCH(dcp,w,n,arch) xfs_cfork_fmt_set_arch(dcp,w,n,arch)
#define	XFS_CFORK_FMT_SET(dcp,w,n)           XFS_CFORK_FMT_SET_ARCH(dcp,w,n,XFS_ARCH_NATIVE)
#else
#define	XFS_CFORK_FMT_SET_X(dcp,w,n,f,arch) \
	((w) == XFS_DATA_FORK ? \
		(f((dcp)->di_format, arch, (n))) : \
		(f((dcp)->di_aformat, arch, (n))))
#define XFS_CFORK_FMT_SET(dcp,w,n)           XFS_CFORK_FMT_SET_X(dcp,w,n,XFS_FSET1,0)
#define XFS_CFORK_FMT_SET_ARCH(dcp,w,n,arch) XFS_CFORK_FMT_SET_X(dcp,w,n,XFS_FSET2,arch)
#endif
#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_CFORK_NEXTENTS)
int xfs_cfork_nextents_arch(xfs_dinode_core_t *dcp, int w, xfs_arch_t arch);
#define	XFS_CFORK_NEXTENTS_ARCH(dcp,w,arch)  xfs_cfork_nextents_arch(dcp,w,arch)
#define	XFS_CFORK_NEXTENTS(dcp,w)            XFS_CFORK_NEXTENTS_ARCH(dcp,w,XFS_ARCH_NATIVE)
#else
#define	XFS_CFORK_NEXTENTS_X(dcp,w,f,arch) \
	((w) == XFS_DATA_FORK ? f((dcp)->di_nextents, arch) : f((dcp)->di_anextents, arch))
#define XFS_CFORK_NEXTENTS(dcp,w)            XFS_CFORK_NEXTENTS_X(dcp,w,XFS_FGET1,0) 
#define XFS_CFORK_NEXTENTS_ARCH(dcp,w,arch)  XFS_CFORK_NEXTENTS_X(dcp,w,XFS_FGET2,arch) 
#endif
#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_CFORK_NEXT_SET)
void xfs_cfork_next_set_arch(xfs_dinode_core_t *dcp, int w, int n, xfs_arch_t arch);
#define	XFS_CFORK_NEXT_SET_ARCH(dcp,w,n,arch)	xfs_cfork_next_set_arch(dcp,w,n,arch)
#define	XFS_CFORK_NEXT_SET(dcp,w,n)	        XFS_CFORK_NEXT_SET_ARCH(dcp,w,n,XFS_ARCH_NATIVE)
#else
#define	XFS_CFORK_NEXT_SET_X(dcp,w,n,f,arch) \
	((w) == XFS_DATA_FORK ? \
		(f((dcp)->di_nextents, arch, (n))) : \
		(f((dcp)->di_anextents, arch, (n))))
#define XFS_CFORK_NEXT_SET(dcp,w,n)             XFS_CFORK_NEXT_SET_X(dcp,w,n,XFS_FSET1,0)
#define XFS_CFORK_NEXT_SET_ARCH(dcp,w,n,arch)   XFS_CFORK_NEXT_SET_X(dcp,w,n,XFS_FSET2,arch)
#endif
#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_DFORK_FORMAT)
int xfs_dfork_format_arch(xfs_dinode_t *dip, int w, xfs_arch_t arch);
#define	XFS_DFORK_FORMAT_ARCH(dip,w,arch)   xfs_dfork_format_arch(dip,w,arch)
#define	XFS_DFORK_FORMAT(dip,w)             XFS_DFORK_FORMAT_ARCH(dip,w,XFS_ARCH_NATIVE)
#else
#define	XFS_DFORK_FORMAT_X(dip,w,f,arch)    XFS_CFORK_FORMAT_ARCH(&(dip)->di_core, w, arch)
#define XFS_DFORK_FORMAT(dip,w)             XFS_DFORK_FORMAT_X(dip,w,XFS_FGET1,0)
#define XFS_DFORK_FORMAT_ARCH(dip,w,arch)   XFS_DFORK_FORMAT_X(dip,w,XFS_FGET2,arch)
#endif
#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_DFORK_FMT_SET)
void xfs_dfork_fmt_set_arch(xfs_dinode_t *dip, int w, int n, xfs_arch_t arch);
#define	XFS_DFORK_FMT_SET_ARCH(dip,w,n,arch)    xfs_dfork_fmt_set_arch(dip,w,n,arch)
#define	XFS_DFORK_FMT_SET(dip,w,n)              xfs_dfork_fmt_set_arch(dip,w,n,XFS_ARCH_NATIVE)
#else
#define	XFS_DFORK_FMT_SET_X(dip,w,n,f,arch)	XFS_CFORK_FMT_SET_ARCH(&(dip)->di_core, w, n, arch)
#define XFS_DFORK_FMT_SET(dip,w,n)              XFS_DFORK_FMT_SET_X(dip,w,n,XFS_FSET1,0)
#define XFS_DFORK_FMT_SET_ARCH(dip,w,n,arch)    XFS_DFORK_FMT_SET_X(dip,w,n,XFS_FSET2,arch)
#endif
#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_DFORK_NEXTENTS)
int xfs_dfork_nextents_arch(xfs_dinode_t *dip, int w, xfs_arch_t arch);
#define	XFS_DFORK_NEXTENTS_ARCH(dip,w,arch) xfs_dfork_nextents_arch(dip,w,arch)
#define	XFS_DFORK_NEXTENTS(dip,w)           XFS_DFORK_NEXTENTS_ARCH(dip,w,XFS_ARCH_NATIVE)
#else
#define	XFS_DFORK_NEXTENTS_X(dip,w,f,arch)  XFS_CFORK_NEXTENTS_ARCH(&(dip)->di_core, w, arch)
#define XFS_DFORK_NEXTENTS(dip,w)           XFS_DFORK_NEXTENTS_X(dip,w,XFS_FGET1,0)
#define XFS_DFORK_NEXTENTS_ARCH(dip,w,arch) XFS_DFORK_NEXTENTS_X(dip,w,XFS_FGET2,arch)
#endif
#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_DFORK_NEXT_SET)
void xfs_dfork_next_set_arch(xfs_dinode_t *dip, int w, int n, xfs_arch_t arch);
#define	XFS_DFORK_NEXT_SET_ARCH(dip,w,n,arch)   xfs_dfork_next_set_arch(dip,w,n,arch)
#define	XFS_DFORK_NEXT_SET(dip,w,n)             XFS_DFORK_NEXT_SET_ARCH(dip,w,n,XFS_ARCH_NATIVE)
#else
#define	XFS_DFORK_NEXT_SET_X(dip,w,n,f,arch)	XFS_CFORK_NEXT_SET_ARCH(&(dip)->di_core, w, n, arch)
#define XFS_DFORK_NEXT_SET(dip,w,n)             XFS_DFORK_NEXT_SET_X(dip,w,n,XFS_FSET1,0)
#define XFS_DFORK_NEXT_SET_ARCH(dip,w,n,arch)   XFS_DFORK_NEXT_SET_X(dip,w,n,XFS_FSET2,arch)
#endif

/*
 * File types (mode field)
 */
#define	IFMT		0170000		/* type of file */
#define	IFIFO		0010000		/* named pipe (fifo) */
#define	IFCHR		0020000		/* character special */
#define	IFDIR		0040000		/* directory */
#define	IFBLK		0060000		/* block special */
#define	IFREG		0100000		/* regular */
#define	IFLNK		0120000		/* symbolic link */
#define	IFSOCK		0140000		/* socket */
#define	IFMNT		0160000		/* mount point */

/*
 * File execution and access modes.
 */
#define	ISUID		04000		/* set user id on execution */
#define	ISGID		02000		/* set group id on execution */
#define	ISVTX		01000		/* sticky directory */
#define	IREAD		0400		/* read, write, execute permissions */
#define	IWRITE		0200
#define	IEXEC		0100

#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_BUF_TO_DINODE)
xfs_dinode_t *xfs_buf_to_dinode(struct xfs_buf *bp);
#define	XFS_BUF_TO_DINODE(bp)	xfs_buf_to_dinode(bp)
#else
#define	XFS_BUF_TO_DINODE(bp)	((xfs_dinode_t *)(XFS_BUF_PTR(bp)))
#endif

/*
 * Values for di_flags
 * There should be a one-to-one correspondence between these flags and the
 * XFS_XFLAG_s.
 */
#define XFS_DIFLAG_REALTIME_BIT	0	/* file's blocks come from rt area */
#define XFS_DIFLAG_PREALLOC_BIT	1	/* file space has been preallocated */
#define	XFS_DIFLAG_NEWRTBM_BIT	2	/* for rtbitmap inode, new format */
#define XFS_DIFLAG_REALTIME     (1 << XFS_DIFLAG_REALTIME_BIT)
#define XFS_DIFLAG_PREALLOC	(1 << XFS_DIFLAG_PREALLOC_BIT)
#define	XFS_DIFLAG_NEWRTBM	(1 << XFS_DIFLAG_NEWRTBM_BIT)
#define XFS_DIFLAG_ALL  \
	(XFS_DIFLAG_REALTIME|XFS_DIFLAG_PREALLOC|XFS_DIFLAG_NEWRTBM)

#endif	/* _FS_XFS_DINODE_H */
