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
#ifndef __XFS_DMAPI_H__
#define __XFS_DMAPI_H__

/*	Values used to define the on-disk version of dm_attrname_t. All
 *	on-disk attribute names start with the 8-byte string "SGI_DMI_".
 *
 *      In the on-disk inode, DMAPI attribute names consist of the user-provided
 *      name with the DMATTR_PREFIXSTRING pre-pended.  This string must NEVER be
 *      changed.
 */

#define DMATTR_PREFIXLEN	8
#define DMATTR_PREFIXSTRING	"SGI_DMI_"

#ifdef	__KERNEL__
/* Defines for determining if an event message should be sent. */
#define	DM_EVENT_ENABLED(vfsp, ip, event) ( \
	((vfsp)->vfs_flag & VFS_DMI) && \
		( ((ip)->i_d.di_dmevmask & (1 << event)) || \
		  ((ip)->i_mount->m_dmevmask & (1 << event)) ) \
	)

#define	DM_EVENT_ENABLED_IO(vfsp, io, event) ( \
	((vfsp)->vfs_flag & VFS_DMI) && \
		( ((io)->io_dmevmask & (1 << event)) || \
		  ((io)->io_mount->m_dmevmask & (1 << event)) ) \
	)

/*
 *	Macros to turn caller specified delay/block flags into
 *	dm_send_xxxx_event flag DM_FLAGS_NDELAY.
 */

#define	FILP_DELAY_FLAG(filp) ((filp->f_flags&(O_NDELAY|O_NONBLOCK)) ? \
			DM_FLAGS_NDELAY : 0)
#define AT_DELAY_FLAG(f) ((f&ATTR_NONBLOCK) ? DM_FLAGS_NDELAY : 0)


extern int
xfs_dm_mount(
	vfs_t		*vfsp,
	vnode_t		*mvp,
	char		*dir_name,
	char		*fsname);

int
xfs_dm_get_fsys_vector(
	bhv_desc_t	*bdp,
	dm_fcntl_vector_t *vecrq);

extern int
xfs_dm_send_data_event(
	dm_eventtype_t	event, 
	bhv_desc_t	*bdp,
	xfs_off_t	offset,
	size_t		length, 
	int		flags,
	vrwlock_t	*locktype);

extern int
xfs_dm_send_create_event(
	bhv_desc_t	*dir_bdp,
	char		*name,
	mode_t		new_mode,
	int		*good_event_sent);

int
xfs_dm_mapevent(
	bhv_desc_t	*bdp,
	int		flags,
	xfs_off_t	offset,
	dm_fcntl_mapevent_t *maprq);

#endif	/* __KERNEL__ */

#endif  /* __XFS_DMAPI_H__ */
