#ifndef _FS_XFS_ATTR_SF_H
#define	_FS_XFS_ATTR_SF_H

#ident	"$Revision: 1.3 $"

/*
 * xfs_attr_sf.h
 *
 * Attribute storage when stored inside the inode.
 *
 * Small attribute lists are packed as tightly as possible so as
 * to fit into the literal area of the inode.
 */

struct xfs_inode;

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
		__uint8_t flags;	/* flags bits (see xfs_attr_leaf.h) */
		__uint8_t nameval[1];	/* name & value bytes concatenated */
	} list[1];			/* variable sized array */
} xfs_attr_shortform_t;
typedef struct xfs_attr_sf_hdr xfs_attr_sf_hdr_t;
typedef struct xfs_attr_sf_entry xfs_attr_sf_entry_t;

#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_ATTR_SF_ENTSIZE_BYNAME)
int xfs_attr_sf_entsize_byname(int nlen, int vlen);
#define XFS_ATTR_SF_ENTSIZE_BYNAME(nlen,vlen)	\
	xfs_attr_sf_entsize_byname(nlen,vlen)
#else
#define XFS_ATTR_SF_ENTSIZE_BYNAME(nlen,vlen)	/* space name/value uses */ \
	(sizeof(xfs_attr_sf_entry_t)-1 + (nlen)+(vlen))
#endif
#define XFS_ATTR_SF_ENTSIZE_MAX			/* max space for name&value */ \
	((1 << (NBBY*sizeof(__uint8_t))) - 1)
#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_ATTR_SF_ENTSIZE)
int xfs_attr_sf_entsize(xfs_attr_sf_entry_t *sfep);
#define XFS_ATTR_SF_ENTSIZE(sfep)	xfs_attr_sf_entsize(sfep)
#else
#define XFS_ATTR_SF_ENTSIZE(sfep)		/* space an entry uses */ \
	(sizeof(xfs_attr_sf_entry_t)-1 + (sfep)->namelen+(sfep)->valuelen)
#endif
#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_ATTR_SF_NEXTENTRY)
xfs_attr_sf_entry_t *xfs_attr_sf_nextentry(xfs_attr_sf_entry_t *sfep);
#define XFS_ATTR_SF_NEXTENTRY(sfep)	xfs_attr_sf_nextentry(sfep)
#else
#define XFS_ATTR_SF_NEXTENTRY(sfep)		/* next entry in struct */ \
	((xfs_attr_sf_entry_t *) \
		((char *)(sfep) + XFS_ATTR_SF_ENTSIZE(sfep)))
#endif
#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_ATTR_SF_TOTSIZE)
int xfs_attr_sf_totsize(struct xfs_inode *dp);
#define XFS_ATTR_SF_TOTSIZE(dp)		xfs_attr_sf_totsize(dp)
#else
#define XFS_ATTR_SF_TOTSIZE(dp)			/* total space in use */ \
	(((xfs_attr_shortform_t *)((dp)->i_afp->if_u1.if_data))->hdr.totsize)
#endif

#endif	/* !FS_XFS_ATTR_SF_H */
