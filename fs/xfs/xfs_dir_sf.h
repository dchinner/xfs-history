#ifndef _FS_XFS_DIR_SF_H
#define	_FS_XFS_DIR_SF_H

#ident	"$Revision: 1.1 $"

/*
 * xfs_dir_sf.h
 *
 * Directory layout when stored internal to an inode.
 *
 * Small directories are packed as tightly as possible so as to
 * fit into the literal area of the inode.
 */

typedef	__uint8_t xfs_dir_ino_t[sizeof(xfs_ino_t)];

/*
 * The parent directory has a dedicated field, and the self-pointer must
 * be calculated on the fly.
 *
 * Entries are packed toward the top as tight as possible.  The header
 * and the elements much be bcopy()'d out into a work area to get correct
 * alignment for the inode number fields.
 */
typedef struct xfs_dir_shortform {
	struct xfs_dir_sf_hdr {		/* constant-structure header block */
		xfs_dir_ino_t parent;	/* parent dir inode number */
		__uint8_t count;	/* count of active entries */
	} hdr;
	struct xfs_dir_sf_entry {
		xfs_dir_ino_t inumber;	/* referenced inode number */
		__uint8_t namelen;	/* actual length of name (no NULL) */
		__uint8_t name[1];	/* name */
	} list[1];			/* variable sized array */
} xfs_dir_shortform_t;
typedef struct xfs_dir_sf_hdr xfs_dir_sf_hdr_t;
typedef struct xfs_dir_sf_entry xfs_dir_sf_entry_t;

#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_DIR_SF_ENTSIZE_BYNAME)
int xfs_dir_sf_entsize_byname(int len);
#define XFS_DIR_SF_ENTSIZE_BYNAME(len)		xfs_dir_sf_entsize_byname(len)
#else
#define XFS_DIR_SF_ENTSIZE_BYNAME(len)		/* space a name uses */ \
	(sizeof(xfs_dir_sf_entry_t)-1 + (len))
#endif
#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_DIR_SF_ENTSIZE_BYENTRY)
int xfs_dir_sf_entsize_byentry(xfs_dir_sf_entry_t *sfep);
#define XFS_DIR_SF_ENTSIZE_BYENTRY(sfep)	xfs_dir_sf_entsize_byentry(sfep)
#else
#define XFS_DIR_SF_ENTSIZE_BYENTRY(sfep)	/* space an entry uses */ \
	(sizeof(xfs_dir_sf_entry_t)-1 + (sfep)->namelen)
#endif
#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_DIR_SF_NEXTENTRY)
xfs_dir_sf_entry_t *xfs_dir_sf_nextentry(xfs_dir_sf_entry_t *sfep);
#define XFS_DIR_SF_NEXTENTRY(sfep)		xfs_dir_sf_nextentry(sfep)
#else
#define XFS_DIR_SF_NEXTENTRY(sfep)		/* next entry in struct */ \
	((xfs_dir_sf_entry_t *) \
		((char *)(sfep) + XFS_DIR_SF_ENTSIZE_BYENTRY(sfep)))
#endif
#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_DIR_SF_ALLFIT)
int xfs_dir_sf_allfit(int count, int totallen);
#define XFS_DIR_SF_ALLFIT(count,totallen)	\
	xfs_dir_sf_allfit(count,totallen)
#else
#define XFS_DIR_SF_ALLFIT(count,totallen)	/* will all entries fit? */ \
	(sizeof(xfs_dir_sf_hdr_t) + \
	       (sizeof(xfs_dir_sf_entry_t)-1)*(count) + (totallen))
#endif

#endif	/* !XFS_XFS_DIR_SF_H */
