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
#ifndef __XFS_VFS_H__
#define __XFS_VFS_H__

#include <linux/vfs.h>
#ifdef __KERNEL_STRICT_NAMES
typedef __kernel_fsid_t fsid_t;
#endif

struct statvfs;
struct vnode;
struct cred;
struct super_block;
struct fid;
struct dm_fcntl_vector;

typedef struct vfs {
	u_int		vfs_flag;	/* flags */
	dev_t		vfs_dev;	/* device of mounted VFS */
	short		vfs_busycnt;	/* # of accesses in progress */
	sv_t		vfs_wait;	/* busy mount point sync */
	u_int		vfs_bsize;	/* native block size */
	int		vfs_fstype;	/* file system type index */
	fsid_t		vfs_fsid;	/* file system id */
	fsid_t		*vfs_altfsid;	/* An ID fixed for life of FS */
	bhv_head_t	vfs_bh;		/* head of vfs behavior chain */
	struct super_block *vfs_super;	/* pointer to super block structure */
#if CELL_CAPABLE
        __uint64_t      vfs_opsflags;           /* vfsops/vnodeops flags */
        struct vfs      *vfs_next;              /* next VFS in VFS list */
        struct vfs      **vfs_prevp;            /* ptr to previous vfs_next */
        struct vnode    *vfs_vnodecovered;      /* vnode mounted on */
        cell_t          vfs_cell;       /* device cell for this FS */
        char            *vfs_expinfo;   /* data exported by home instance
                                           of file system. */
        size_t          vfs_eisize;     /* amount of data exported by
                                           home instance of file system */
#endif
} vfs_t;

#define vfs_fbhv        vfs_bh.bh_first         /* 1st on vfs behavior chain */
#define VFS_FOPS(vfsp)  \
        ((vfsops_t *)((vfsp)->vfs_fbhv->bd_ops))/* ops for 1st behavior */


#define bhvtovfs(bdp)	((struct vfs *)BHV_VOBJ(bdp))
#define VFS_BHVHEAD(vfsp) (&(vfsp)->vfs_bh)

#define VFS_FSTYPE_ANY		(-1)	/* fstype arg to vfs_devsearch* , 	
					   vfs_busydev if the filesystem type
					   is irrelevant for the search */

#define VFS_INIT(vfsp) { \
	sv_init(&(vfsp)->vfs_wait, SV_DEFAULT, "vfs_wait"); \
        bhv_head_init(VFS_BHVHEAD(vfsp),"vfs"); \
}

#define VFS_FREE(vfsp) {        \
        sv_destroy(&(vfsp)->vfs_wait); \
        bhv_head_destroy(VFS_BHVHEAD(vfsp)); \
}


#define VFS_RDONLY	0x0001		/* read-only vfs */
#define VFS_GRPID	0x0008		/* group-ID assigned from directory */
#define VFS_REMOUNT	0x0010		/* modify mount options only */
#define VFS_NOTRUNC	0x0020		/* does not truncate long file names */
#define VFS_MLOCK       0x0100          /* lock vfs so that subtree is stable */
#define VFS_MWAIT       0x0200          /* waiting for access lock */
#define VFS_MWANT       0x0400          /* waiting for update */

#define VFS_LOCAL	0x1000		/* local filesystem, for find */
#define VFS_OFFLINE	0x2000		/* filesystem is being unmounted */
#define VFS_DMI		0x4000		/* filesystem has the DMI enabled */

#define SYNC_NOWAIT     0x0000          /* start delayed writes */
#define SYNC_ATTR	0x0001		/* sync attributes */
#define SYNC_CLOSE	0x0002		/* close file system down */
#define SYNC_DELWRI	0x0004		/* look at delayed writes */
#define SYNC_WAIT	0x0008		/* wait for i/o to complete */
#define SYNC_FSDATA	0x0020		/* flush fs data (e.g. superblocks) */
#define SYNC_BDFLUSH	0x0010		/* BDFLUSH is calling -- don't block */
#define SYNC_PDFLUSH	0x0040		/* push v_dpages */


struct mounta {	
	char		*spec;
	char		*dir;
	sysarg_t	flags;
	char		*dataptr;
	sysarg_t	datalen;
};

typedef struct vfsops {
#ifdef CELL_CAPABLE
        bhv_position_t  vf_position;    /* position within behavior chain */
#endif
        int     (*vfs_mount)(struct vfs *, struct vnode *,
                             struct mounta *, char *, struct cred *);
                                        /* mount file system */
        int     (*vfs_rootinit)(struct vfs *);
                                        /* 1st mount of root fs */
        int     (*vfs_dounmount)(bhv_desc_t *, int, struct vnode *,
                                 struct cred *);
                                        /* preparation and unmount */
        int     (*vfs_unmount)(bhv_desc_t *, int, struct cred *);
                                        /* unmount file system */
        int     (*vfs_root)(bhv_desc_t *, struct vnode **);
                                        /* get root vnode */
        int     (*vfs_statvfs)(bhv_desc_t *, struct statvfs *, struct vnode *);
                                        /* get file system statistics */
        int     (*vfs_sync)(bhv_desc_t *, int, struct cred *);
                                        /* flush files */
	int	(*vfs_vget)(bhv_desc_t *, struct vnode **, struct fid *);
					/* get vnode from fid */
        int     (*vfs_get_vnode)(bhv_desc_t *, struct vnode **, xfs_ino_t);
                                        /* get vnode using an ino_t */
	int	(*vfs_dmapi_mount)(struct vfs *, struct vnode *,
				   char *, char *);
					/* send dmapi mount event */
	int	(*vfs_dmapi_fsys_vector)(bhv_desc_t *,
					 struct dm_fcntl_vector *);
#ifdef CELL_CAPABLE
        int     vfs_ops_magic;          /* magic number for intfc extensions */
        __uint64_t   vfs_ops_version;   /* interface version number */
        __uint64_t   vfs_ops_flags;     /* interface flags */
#endif
} vfsops_t;

#define VFS_DOUNMOUNT(vfsp,f,vp,cr, rv) \
{       \
        BHV_READ_LOCK(&(vfsp)->vfs_bh); \
        rv = (*(VFS_FOPS(vfsp)->vfs_dounmount))((vfsp)->vfs_fbhv, f, vp, cr);   \
        BHV_READ_UNLOCK(&(vfsp)->vfs_bh); \
}
#define VFS_UNMOUNT(vfsp,f,cr, rv)      \
{       \
        BHV_READ_LOCK(&(vfsp)->vfs_bh); \
        rv = (*(VFS_FOPS(vfsp)->vfs_unmount))((vfsp)->vfs_fbhv, f, cr);         \
        BHV_READ_UNLOCK(&(vfsp)->vfs_bh); \
}
#define VFS_ROOT(vfsp, vpp, rv)         \
{       \
        BHV_READ_LOCK(&(vfsp)->vfs_bh); \
        rv = (*(VFS_FOPS(vfsp)->vfs_root))((vfsp)->vfs_fbhv, vpp);      \
        BHV_READ_UNLOCK(&(vfsp)->vfs_bh); \
}
#define VFS_STATVFS(vfsp, sp, vp, rv)   \
{       \
        BHV_READ_LOCK(&(vfsp)->vfs_bh); \
        rv = (*(VFS_FOPS(vfsp)->vfs_statvfs))((vfsp)->vfs_fbhv, sp, vp);        \
        BHV_READ_UNLOCK(&(vfsp)->vfs_bh); \
}
#define VFS_SYNC(vfsp, flag, cr, rv) \
{       \
        BHV_READ_LOCK(&(vfsp)->vfs_bh); \
        rv = (*(VFS_FOPS(vfsp)->vfs_sync))((vfsp)->vfs_fbhv, flag, cr); \
        BHV_READ_UNLOCK(&(vfsp)->vfs_bh); \
}
#define VFS_VGET(vfsp, vpp, fidp, rv) \
{       \
        BHV_READ_LOCK(&(vfsp)->vfs_bh); \
        rv = (*(VFS_FOPS(vfsp)->vfs_vget))((vfsp)->vfs_fbhv, vpp, fidp);  \
        BHV_READ_UNLOCK(&(vfsp)->vfs_bh); \
}
#define VFS_GET_VNODE(vfsp, vpp, ino, rv) \
{       \
        BHV_READ_LOCK(&(vfsp)->vfs_bh); \
        rv = (*(VFS_FOPS(vfsp)->vfs_get_vnode))((vfsp)->vfs_fbhv, vpp, ino);        \
        BHV_READ_UNLOCK(&(vfsp)->vfs_bh); \
}
#define VFS_DMAPI_FSYS_VECTOR(vfsp, df, rv) \
{       \
        BHV_READ_LOCK(&(vfsp)->vfs_bh); \
        rv = (*(VFS_FOPS(vfsp)->vfs_dmapi_fsys_vector))((vfsp)->vfs_fbhv, df);        \
        BHV_READ_UNLOCK(&(vfsp)->vfs_bh); \
}


#define VFSOPS_DMAPI_MOUNT(vfs_op, vfsp, mvp, dir_name, fsname, rv) \
        rv = (*(vfs_op)->vfs_dmapi_mount)(vfsp, mvp, dir_name, fsname)
#define VFSOPS_MOUNT(vfs_op, vfsp, mvp, uap, attrs, cr, rv) \
        rv = (*(vfs_op)->vfs_mount)(vfsp, mvp, uap, attrs, cr)

#define VFS_REMOVEBHV(vfsp, bdp)\
{	\
	bhv_remove(VFS_BHVHEAD(vfsp), bdp); \
}

#define PVFS_UNMOUNT(bdp,f,cr, rv)      \
{       \
        rv = (*((vfsops_t *)(bdp)->bd_ops)->vfs_unmount)(bdp, f, cr);   \
}

#define PVFS_SYNC(bdp, flag, cr, rv) \
{       \
        rv = (*((vfsops_t *)(bdp)->bd_ops)->vfs_sync)(bdp, flag, cr);   \
}

extern vfs_t    *vfs_allocate(void);    /* Allocate a new vfs. */
extern void     vfs_deallocate(vfs_t *);/* Deallocate a vfs. */
extern void	vfs_insertbhv(vfs_t *, bhv_desc_t *, vfsops_t *, void *);
                                        /* link vfs base behavior with vfs */
extern int      vfs_lock_offline(struct vfs *);
extern int      vfs_lock(struct vfs *); /* lock and unlock a vfs */
extern void     vfs_unlock(struct vfs *);
extern int      vfs_busy(struct vfs *); /* mark busy for serial sync/unmount */
extern void     vfs_unbusy(struct vfs *);
extern vfs_t	*vfs_busydev(dev_t, int);/* to keep dev from unmounting */
extern vfs_t 	*vfs_devsearch(dev_t, int);/* find vfs given device & opt. type */
extern void	vfs_setflag(struct vfs *, unsigned long);

#endif	/* __XFS_VFS_H__ */
