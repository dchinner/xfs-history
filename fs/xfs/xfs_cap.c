/*
 * Copyright (c) 2002 Silicon Graphics, Inc.  All Rights Reserved.
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

#include <xfs.h>

#define XATTR_CAP_BITS	(sizeof(posix_cap_xattr_value) * NBBY)
#define XFS_CAP_BITS	(sizeof(xfs_cap_value_t) * NBBY)

STATIC int xfs_cap_allow_set(vnode_t *);
STATIC int inline xfs_cap_lookup_xfs(uint);
STATIC int inline xfs_cap_lookup_xattr(uint);


/*
 * Test for existence of capability attribute as efficiently as possible.
 */
int
xfs_cap_vhascap(
	vnode_t		*vp)
{
	int		error;
	int		len = sizeof(xfs_cap_set_t);
	int		flags = ATTR_KERNOVAL|ATTR_ROOT;

	VOP_ATTR_GET(vp, SGI_CAP_FILE, NULL, &len, flags, sys_cred, error);
	return (error == 0);
}

/*
 * Convert from extended attribute representation to in-memory for XFS.
 */
STATIC int
posix_cap_xattr_to_xfs(
	posix_cap_xattr		*src,
	size_t			size,
	xfs_cap_set_t		*dest)
{
	posix_cap_xattr_value	*src_value;
	xfs_cap_value_t		*dest_value;
	size_t			map_size;
	int			n;

	if (!src || !dest)
		return EINVAL;

	if (size < sizeof(posix_cap_xattr))
		return EINVAL;

	if (src->c_version != cpu_to_le32(POSIX_CAP_XATTR_VERSION))
		return EINVAL;

	memset(dest, 0, sizeof(xfs_cap_set_t));
	src_value = &src->c_effective;
	dest_value = (xfs_cap_value_t *)dest;
	map_size = XATTR_CAP_BITS >> BIT_TO_WORD_SHIFT;

	/*
	 * Iterate through the capability bits, setting corresponding XFS bits
	 */
	for (n = 0; n < 3; n++, src_value++, dest_value++) {
		uint	setbit = xfs_next_bit(src_value, map_size, 0);

		while (setbit != -1) {
			int	bit = xfs_cap_lookup_xattr(setbit);

			if (bit == -1) {
#ifdef DEBUG
				printk("XFS cannot map VFS capability bit %u\n",
					setbit);
#endif
				return EINVAL;
			}
			*dest_value |= (1 << bit);
			setbit = xfs_next_bit(src_value, map_size, setbit + 1);
		}
	}
	return 0;
}

/*
 * Convert from in-memory XFS to extended attribute representation.
 */
STATIC int
posix_cap_xfs_to_xattr(
	xfs_cap_set_t		*src,
	posix_cap_xattr		*xattr_cap,
	size_t			size)
{
	posix_cap_xattr_value	*dest_value;
	xfs_cap_value_t		*src_value;
	size_t			new_size = posix_cap_xattr_size();
	size_t			map_size;
	int			n;

	if (size < new_size)
		return -ERANGE;	

	memset(xattr_cap, 0, sizeof(posix_cap_xattr));
	xattr_cap->c_version = cpu_to_le32(POSIX_CAP_XATTR_VERSION);
	dest_value = (posix_cap_xattr_value *)&xattr_cap->c_effective;
	src_value = (xfs_cap_value_t *)src;
	map_size = XFS_CAP_BITS >> BIT_TO_WORD_SHIFT;

	/*
	 * Iterate through the XFS bits, setting corresponding capability bits
	 */
	for (n = 0; n < 3; n++, dest_value++, src_value++) {
		uint	setbit = xfs_next_bit((uint *)src_value, map_size, 0);

		while (setbit != -1) {
			int	bit = xfs_cap_lookup_xfs(setbit);

			if (bit == -1) {
#ifdef DEBUG
				printk("XFS cannot map XFS capability bit %u\n",
					setbit);
#endif
				return -EINVAL;
			}
			*dest_value |= (1 << bit);
			setbit = xfs_next_bit((uint *)src_value,
						map_size, setbit + 1);
		}
	}
	return new_size;
}

int
xfs_cap_vget(
	vnode_t		*vp,
	void		*cap,
	size_t		size)
{
	int		error;
	int		len = sizeof(xfs_cap_set_t);
	int		flags = ATTR_ROOT;
	xfs_cap_set_t	xfs_cap = { 0 };
	posix_cap_xattr	*xattr_cap = cap;

	VN_HOLD(vp);
	if ((error = _MAC_VACCESS(vp, get_current_cred(), VREAD)))
		goto out;

	if (!size)
		flags |= ATTR_KERNOVAL;
	VOP_ATTR_GET(vp, SGI_CAP_FILE, (char *)&xfs_cap,
			&len, flags, sys_cred, error);
	if (error)
		goto out;
	ASSERT(len == sizeof(xfs_cap_set_t));

	error = (size)? -posix_cap_xattr_size() :
			-posix_cap_xfs_to_xattr(&xfs_cap, xattr_cap, size);
out:
	VN_RELE(vp);
	return -error;
}

int
xfs_cap_vremove(
	vnode_t		*vp)
{
	int		error;

	VN_HOLD(vp);
	error = xfs_cap_allow_set(vp);
	if (!error) {
		VOP_ATTR_REMOVE(vp, SGI_CAP_FILE, ATTR_ROOT, sys_cred, error);
		if (error == ENOATTR)
			error = 0;	/* 'scool */
	}
	VN_RELE(vp);
	return -error;
}

int
xfs_cap_vset(
	vnode_t			*vp,
	void			*cap,
	size_t			size)
{
	posix_cap_xattr		*xattr_cap = cap;
	xfs_cap_set_t		xfs_cap;
	int			error;

	if (!cap)
		return -EINVAL;

	error = posix_cap_xattr_to_xfs(xattr_cap, size, &xfs_cap);
	if (error)
		return -error;

	VN_HOLD(vp);
	error = xfs_cap_allow_set(vp);
	if (error)
		goto out;

	VOP_ATTR_SET(vp, SGI_CAP_FILE, (char *)&xfs_cap,
			sizeof(xfs_cap_set_t), ATTR_ROOT, sys_cred, error);
out:
	VN_RELE(vp);
	return -error;
}

STATIC int
xfs_cap_allow_set(
	vnode_t		*vp)
{
	vattr_t		va;
	int		error;

	if (vp->v_vfsp->vfs_flag & VFS_RDONLY)
		return EROFS;
	if ((error = _MAC_VACCESS(vp, NULL, VWRITE)))
		return error;
	va.va_mask = AT_UID;
	VOP_GETATTR(vp, &va, 0, NULL, error);
	if (error)
		return error;
	if (va.va_uid != current->fsuid && !capable(CAP_FOWNER))
		return EPERM;
	return error;
}


/*
 * This translates between the XFS (ondisk) capability bits and the
 * corresponding VFS equivalents.
 * 
 * On IRIX, CAP_DAC_OVERRIDE is defined as:
 * 	(CAP_DAC_WRITE|CAP_DAC_READ_SEARCH|CAP_FOWNER)
 * But theres no CAP_DAC_WRITE under Linux and CAP_DAC_OVERRIDE is a
 * distinct bit.  So, we'll just define a XFS_CAP_LINUX_DAC_OVERRIDE
 * but we may need to revisit this I guess.
 */
STATIC const char xfs_to_xattr_capbits[] = {
	-1,			/* Currently unused		0 */
	CAP_CHOWN,		/* XFS_CAP_CHOWN		1 */
	-1,			/* XFS_CAP_DAC_WRITE		2 */
	CAP_DAC_READ_SEARCH,	/* XFS_CAP_DAC_READ_SEARCH	3 */
	CAP_FOWNER,		/* XFS_CAP_FOWNER		4 */
	CAP_FSETID,		/* XFS_CAP_FSETID		5 */
	CAP_KILL,		/* XFS_CAP_KILL			6 */
	-1,			/* Currently unused		7 */
	-1,			/* XFS_CAP_SETFPRIV		8 */
	CAP_SETPCAP,		/* XFS_CAP_SETPPRIV		9 */
	CAP_SETGID,		/* XFS_CAP_SETGID		10 */
	CAP_SETUID,		/* XFS_CAP_SETUID		11 */
	-1,			/* XFS_CAP_MAC_DOWNGRADE	12 */
	-1,			/* XFS_CAP_MAC_READ		13 */
	-1,			/* XFS_CAP_MAC_RELABEL_SUBJ	14 */
	-1,			/* XFS_CAP_MAC_WRITE		15 */
	-1,			/* XFS_CAP_MAC_UPGRADE		16 */
	-1,			/* Currently unused		17 */
	-1,			/* Currently unused		18 */
	-1,			/* Currently unused		19 */
	-1,			/* Currently unused		20 */
	-1,			/* Currently unused		21 */
	-1,			/* XFS_CAP_AUDIT_CONTROL	22 */
	-1,			/* XFS_CAP_AUDIT_WRITE		23 */
	-1,			/* XFS_CAP_MAC_MLD		24 */
	-1,			/* XFS_CAP_MEMORY_MGT		25 */
	-1,			/* XFS_CAP_SWAP_MGT		26 */
	CAP_SYS_TIME,		/* XFS_CAP_TIME_MGT		27 */
	-1,			/* XFS_CAP_SYSINFO_MGT		28 */
	-1,			/* XFS_CAP_MOUNT_MGT		29 */
	-1,			/* XFS_CAP_QUOTA_MGT		30 */
	-1,			/* XFS_CAP_PRIV_PORT		31 */
	-1,			/* XFS_CAP_STREAMS_MGT		32 */
	CAP_SYS_NICE,		/* XFS_CAP_SCHED_MGT		33 */
	-1,			/* XFS_CAP_PROC_MGT		34 */
	-1,			/* XFS_CAP_SVIPC_MGT		35 */
	CAP_NET_ADMIN,		/* XFS_CAP_NETWORK_MGT		36 */
	CAP_MKNOD,		/* XFS_CAP_DEVICE_MGT		37 */
	CAP_SYS_PACCT,		/* XFS_CAP_ACCT_MGT		38 */
	CAP_SYS_BOOT,		/* XFS_CAP_SHUTDOWN		39 */
	CAP_SYS_CHROOT,		/* XFS_CAP_CHROOT		40 */
	-1,			/* XFS_CAP_DAC_EXECUTE		41 */
	-1,			/* XFS_CAP_MAC_RELABEL_OPEN	42 */
	-1,			/* Currently unused		43 */
	-1,			/* XFS_CAP_XTCB			44 */
	CAP_DAC_OVERRIDE,	/* XFS_CAP_LINUX_DAC_OVERRIDE	45 */
	CAP_LINUX_IMMUTABLE,	/* XFS_CAP_LINUX_IMMUTABLE	46 */
	CAP_NET_BIND_SERVICE,	/* XFS_CAP_LINUX_NET_BIND_SERVICE 47 */
	CAP_NET_BROADCAST,	/* XFS_CAP_LINUX_NET_BROADCAST	48 */
	CAP_NET_RAW,		/* XFS_CAP_LINUX_NET_RAW	49 */
	CAP_IPC_LOCK,		/* XFS_CAP_LINUX_IPC_LOCK	50 */
	CAP_IPC_OWNER,		/* XFS_CAP_LINUX_IPC_OWNER	51 */
	CAP_SYS_MODULE,		/* XFS_CAP_LINUX_SYS_MODULE	52 */
	CAP_SYS_RAWIO,		/* XFS_CAP_LINUX_SYS_RAWIO	53 */
	CAP_SYS_PTRACE,		/* XFS_CAP_LINUX_SYS_PTRACE	54 */
	CAP_SYS_ADMIN,		/* XFS_CAP_LINUX_SYS_ADMIN	55 */
	CAP_SYS_RESOURCE,	/* XFS_CAP_LINUX_SYS_RESOURCE	56 */
	CAP_SYS_TTY_CONFIG,	/* XFS_CAP_LINUX_SYS_TTY_CONFIG	57 */
	CAP_LEASE,		/* XFS_CAP_LINUX_LEASE		58 */
				/* Currently unused	59 ---> 63 */
};

STATIC int inline
xfs_cap_lookup_xfs(uint bit)
{
	if (bit > sizeof(xfs_to_xattr_capbits)/sizeof(char))
		return -1;
	return xfs_to_xattr_capbits[bit];
}


/*
 * This translates between the VFS capability bits and their
 * corresponding XFS (ondisk) equivalents.
 */
STATIC const char xattr_to_xfs_capbits[] = {
	XFS_CAP_CHOWN,			/* CAP_CHOWN            0 */
	XFS_CAP_LINUX_DAC_OVERRIDE,	/* CAP_DAC_OVERRIDE     1 */
	XFS_CAP_DAC_READ_SEARCH,	/* CAP_DAC_READ_SEARCH  2 */
	XFS_CAP_FOWNER,			/* CAP_FOWNER           3 */
	XFS_CAP_FSETID,			/* CAP_FSETID           4 */
	XFS_CAP_KILL,			/* CAP_KILL             5 */
	XFS_CAP_SETGID,			/* CAP_SETGID           6 */
	XFS_CAP_SETUID,			/* CAP_SETUID           7 */
	XFS_CAP_SETPPRIV,		/* CAP_SETPCAP          8 */
	XFS_CAP_LINUX_IMMUTABLE,	/* CAP_LINUX_IMMUTABLE  9 */
	XFS_CAP_LINUX_NET_BIND_SERVICE,	/* CAP_NET_BIND_SERVICE 10 */
	XFS_CAP_LINUX_NET_BROADCAST,	/* CAP_NET_BROADCAST    11 */
	XFS_CAP_NETWORK_MGT,		/* CAP_NET_ADMIN        12 */
	XFS_CAP_LINUX_NET_RAW,		/* CAP_NET_RAW          13 */
	XFS_CAP_LINUX_IPC_LOCK,		/* CAP_IPC_LOCK         14 */
	XFS_CAP_LINUX_IPC_OWNER,	/* CAP_IPC_OWNER        15 */
	XFS_CAP_LINUX_SYS_MODULE,	/* CAP_SYS_MODULE       16 */
	XFS_CAP_LINUX_SYS_RAWIO,	/* CAP_SYS_RAWIO        17 */
	XFS_CAP_CHROOT,			/* CAP_SYS_CHROOT       18 */
	XFS_CAP_LINUX_SYS_PTRACE,	/* CAP_SYS_PTRACE       19 */
	XFS_CAP_ACCT_MGT,		/* CAP_SYS_PACCT        20 */
	XFS_CAP_LINUX_SYS_ADMIN,	/* CAP_SYS_ADMIN        21 */
	XFS_CAP_SHUTDOWN,		/* CAP_SYS_BOOT         22 */
	XFS_CAP_SCHED_MGT,		/* CAP_SYS_NICE         23 */
	XFS_CAP_LINUX_SYS_RESOURCE,	/* CAP_SYS_RESOURCE     24 */
	XFS_CAP_TIME_MGT,		/* CAP_SYS_TIME         25 */
	XFS_CAP_LINUX_SYS_TTY_CONFIG,	/* CAP_SYS_TTY_CONFIG   26 */
	XFS_CAP_DEVICE_MGT,		/* CAP_MKNOD            27 */
	XFS_CAP_LINUX_LEASE,		/* CAP_LEASE            28 */
					/* Currently unused 29->31 */
};

STATIC int inline
xfs_cap_lookup_xattr(uint bit)
{
	if (bit > sizeof(xattr_to_xfs_capbits)/sizeof(char))
		return -1;
	return xattr_to_xfs_capbits[bit];
}
