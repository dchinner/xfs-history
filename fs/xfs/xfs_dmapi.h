#ifndef _FS_XFS_DMAPI_H
#define _FS_XFS_DMAPI_H

#ident  "$Revision: 1.1 $"

/* Defines for determining if an event message should be sent. */
#define	DM_EVENT_ENABLED(vfsp, ip, event) ( \
	((vfsp)->vfs_flag & VFS_DMI) && \
		( ((ip)->i_d.di_dmevmask & (1 << event)) || \
		  ((ip)->i_mount->m_dmevmask & (1 << event)) ) \
	)

/*
 *	Macros to turn caller specified delay/block flags into
 *	dm_send_xxxx_event flag DM_FLAGS_NDELAY.
 */

#define	UIO_DELAY_FLAG(uiop) ((uiop->uio_fmode&(FNDELAY|FNONBLOCK)) ? \
			DM_FLAGS_NDELAY : 0)
#define AT_DELAY_FLAG(f) ((f&ATTR_NONBLOCK) ? DM_FLAGS_NDELAY : 0)


extern int
xfs_dm_send_data_event(
	dm_eventtype_t	event, 
	bhv_desc_t	*bdp,
	off_t		offset,
	size_t		length, 
	int		flags,
	vrwlock_t	*locktype);

extern int
xfs_dm_send_create_event(
	bhv_desc_t	*dir_bdp,
	char		*name,
	mode_t		new_mode,
	int		*good_event_sent);

extern int
xfs_dm_fcntl(
	bhv_desc_t	*bdp,
	void		*arg,
	int		flags,
	off_t		offset,
	cred_t		*credp,
	union rval	*rvalp);

extern int
xfs_dm_map(
	bhv_desc_t	*bdp,
        off_t           offset,
        size_t          length,
        dm_eventtype_t  max_event);

#endif  /* _FS_XFS_DMAPI_H */
