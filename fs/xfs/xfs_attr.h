#ifndef _FS_XFS_ATTR_H
#define	_FS_XFS_ATTR_H

#ident	"$Revision: 1.20 $"

/*
 * xfs_attr.h
 *
 * Large attribute lists are structured around Btrees where all the data
 * elements are in the leaf nodes.  Attribute names are hashed into an int,
 * then that int is used as the index into the Btree.  Since the hashval
 * of an attribute name may not be unique, we may have duplicate keys.
 * The internal links in the Btree are logical block offsets into the file.
 *
 * Small attribute lists use a different format and are packed as tightly
 * as possible so as to fit into the literal area of the inode.
 */

/*
 * Provide a way to find the last attribute that was returned in the
 * last syscall so that we can get the next one without missing any.
 */
struct attrlist_cursor_kern {
	u_int32_t	hashval;	/* hash value of this entry */
	u_int32_t	blkno;		/* block containing entry */
	u_int32_t	index;		/* index of entry in block */
	u_int16_t	pad1;		/* padding to match user-level */
	u_int8_t	pad2;		/* padding to match user-level */
	u_int8_t	initted;	/* T/F: cursor has been initialized */
};
typedef struct attrlist_cursor_kern attrlist_cursor_kern_t;

/*
 * Kernel versions of the multi-attribute operation structure,
 * one for each of the 32bit and 64bit ABIs.
 */
struct attr_multiop_kern_32 {
	__int32_t	am_opcode;	/* which operation to perform */
	__int32_t	am_error;	/* [out arg] result of this sub-op */
	app32_ptr_t	am_attrname;	/* attribute name to work with */
	app32_ptr_t	am_attrvalue;	/* [in/out arg] attribute value */
	__int32_t	am_length;	/* [in/out arg] length of value */
	__int32_t	am_flags;	/* flags (bit-wise OR of lists below) */
	
};
struct attr_multiop_kern_64 {
	__int32_t	am_opcode;	/* which operation to perform */
	__int32_t	am_error;	/* [out arg] result of this sub-op */
	app64_ptr_t	am_attrname;	/* attribute name to work with */
	app64_ptr_t	am_attrvalue;	/* [in/out arg] attribute value */
	__int32_t	am_length;	/* [in/out arg] length of value */
	__int32_t	am_flags;	/* flags (bit-wise OR of lists below) */
	
};


/*========================================================================
 * Function prototypes for the kernel.
 *========================================================================*/


struct xfs_da_name;
struct uio;
struct xfs_bmap_free;

/*
 * Overall external interface routines.
 */
int xfs_attr_get(vnode_t *, char *, char *, int *, int, struct cred *);
int xfs_attr_set(vnode_t *, char *, char *, int, int, struct cred *);
int xfs_attr_remove(vnode_t *, char *, int, struct cred *);
int xfs_attr_list(vnode_t *, char *, int, int, attrlist_cursor_kern_t *,
			  struct cred *);
#endif	/* !_FS_XFS_ATTR_H */
