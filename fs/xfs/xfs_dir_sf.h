#ifndef _FS_XFS_DIR_SF_H
#define	_FS_XFS_DIR_SF_H

#ident	"$Revision: 1.12 $"

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

#define XFS_DIR_SF_ENTSIZE_BYNAME(LEN)		/* space a name uses */ \
	(sizeof(xfs_dir_sf_entry_t)-1 + (LEN))
#define XFS_DIR_SF_ENTSIZE_BYENTRY(SFEP)	/* space an entry uses */ \
	(sizeof(xfs_dir_sf_entry_t)-1 + (SFEP)->namelen)
#define XFS_DIR_SF_NEXTENTRY(SFEP)		/* next entry in struct */ \
	((xfs_dir_sf_entry_t *) \
		((char *)(SFEP) + XFS_DIR_SF_ENTSIZE_BYENTRY(SFEP)))
#define XFS_DIR_SF_ALLFIT(COUNT, TOTALLEN)	/* will all entries fit? */ \
	(sizeof(xfs_dir_sf_hdr_t) + \
	       (sizeof(xfs_dir_sf_entry_t)-1)*(COUNT) + (TOTALLEN))

#endif	/* !XFS_XFS_DIR_SF_H */
