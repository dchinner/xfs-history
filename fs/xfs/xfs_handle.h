#ifndef _XFS_HANDLE_H
#define _XFS_HANDLE_H

#ident	"$Revision: 1.5 $"

/*
 *  Ok.  This file contains stuff that defines a general handle
 *  mechanism, except that it's only used by xfs at the moment.
 *  Imagine that it's in some generic place.  The same goes for
 *  xfs_handle.c; it's would-be generic but xfs-only right now.
 */

#ifdef _KERNEL

typedef	struct handle {
	fsid_t	ha_fsid;		/* unique file system identifier */
	fid_t	ha_fid;			/* file system specific file ID */
} handle_t;

/*
 *  Kernel only prototypes.
 */

int	path_to_handle		(char *path, void *hbuf, size_t *hlen);
int	path_to_fshandle	(char *path, void *hbuf, size_t *hlen);
int	fd_to_handle		(int	 fd, void *hbuf, size_t *hlen);
int	open_by_handle		(void *hbuf, size_t hlen, int rw, rval_t *rvp);
int	readlink_by_handle	(void *hanp, size_t hlen, void *buf, size_t bs,
				 rval_t *rvp);
int	gethandle		(void *hanp, size_t hlen, handle_t *handlep);

struct vnode *	handle_to_vp (handle_t *handlep);
int		vp_to_handle (vnode_t *vp, handle_t *handlep);

struct vfs *	altgetvfs (fsid_t *fsid);

/*
 *  Kernel #defines.
 */

#define	HSIZE(handle)	(((char *) &(handle).ha_fid.fid_data - \
			  (char *) &(handle)) + (handle).ha_fid.fid_len)
#define	HANDLE_CMP(h1, h2)	bcmp(h1, h2, sizeof (handle_t))

#define	FSHSIZE		sizeof (fsid_t)
#define	ISFSHANDLE	((handle)->ha_fid.fid_len == 0)

#endif	/* _KERNEL */

#ifndef _KERNEL

/*
 *  Prototypes.
 */

int	path_to_handle		(char *path, void **hanp, size_t *hlen);
int	path_to_fshandle	(char *path, void **hanp, size_t *hlen);
int	fd_to_handle		(int	 fd, void **hanp, size_t *hlen);
int	handle_to_fshandle	(void *hanp, size_t hlen, void **fshanp,
				 size_t *fshlen);
void	free_handle		(void *hanp, size_t hlen);
int	open_by_handle		(void *hanp, size_t hlen, int rw);
int	readlink_by_handle	(void *hanp, size_t hlen, void *buf, size_t bs);

#endif	/* notdef _KERNEL */

#endif	/* _XFS_HANDLE_H */
