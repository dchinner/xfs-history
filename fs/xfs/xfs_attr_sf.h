#ifndef _FS_XFS_ATTR_SF_H
#define	_FS_XFS_ATTR_SF_H

#ident	"$Revision: 1.1 $"

/*
 * xfs_attr_sf.h
 *
 * Attribute storage when stored inside the inode.
 *
 * Small attribute lists are packed as tightly as possible so as
 * to fit into the literal area of the inode.
 */

/*
 * Entries are packed toward the top as tight as possible.
 */
typedef struct xfs_attr_shortform {
	struct xfs_attr_sf_hdr {	/* constant-structure header block */
		__uint16_t totsize;	/* total bytes in shortform list */
		__uint8_t count;	/* count of active entries */
	} hdr;
	struct xfs_attr_sf_entry {
		__uint8_t namelen;	/* actual length of name (no NULL) */
		__uint8_t valuelen;	/* actual length of value (no NULL) */
		__uint8_t nameval[1];	/* name & value bytes concatenated */
	} list[1];			/* variable sized array */
} xfs_attr_shortform_t;
typedef struct xfs_attr_sf_hdr xfs_attr_sf_hdr_t;
typedef struct xfs_attr_sf_entry xfs_attr_sf_entry_t;

#define XFS_ATTR_SF_ENTSIZE_BYNAME(NLEN, VLEN)	/* space name/value uses */ \
	(sizeof(xfs_attr_sf_entry_t)-1 + (NLEN)+(VLEN))
#define XFS_ATTR_SF_ENTSIZE_MAX			/* max space for name&value */ \
	((1 << sizeof(xfs_attr_sf_entry_t.namelen)) - 2)
#define XFS_ATTR_SF_ENTSIZE(SFEP)		/* space an entry uses */ \
	(sizeof(xfs_attr_sf_entry_t)-1 + (SFEP)->namelen+(SFEP)->valuelen)
#define XFS_ATTR_SF_NEXTENTRY(SFEP)		/* next entry in struct */ \
	((xfs_attr_sf_entry_t *) \
		((char *)(SFEP) + XFS_ATTR_SF_ENTSIZE(SFEP)))

#endif	/* !FS_XFS_ATTR_SF_H */
