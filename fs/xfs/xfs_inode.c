#include <sys/param.h>
#ifdef SIM
#define	_KERNEL
#endif
#include <sys/buf.h>
#ifdef SIM
#undef _KERNEL
#endif
#include <sys/debug.h>
#include <sys/errno.h>
#include <sys/stat.h>
#include <sys/mode.h>
#include <sys/vnode.h>
#include <sys/cred.h>
#include <sys/uuid.h>
#ifdef SIM
#include <bstring.h>
#include <stdio.h>
#else
#include <sys/systm.h>
#include <sys/kmem.h>
#endif
#include "xfs_types.h"
#include "xfs_inum.h"
#include "xfs_trans.h"
#include "xfs_sb.h"
#include "xfs_mount.h"
#include "xfs_alloc.h"
#include "xfs_ialloc.h"
#include "xfs_ag.h"
#include "xfs_bmap.h"
#include "xfs_imap.h"
#include "xfs_btree.h"
#include "xfs_dinode.h"
#include "xfs_inode_item.h"
#include "xfs_inode.h"
#include "xfs_buf_item.h"
#include "xfs_bio.h"

#ifdef SIM
#include "sim.h"
#endif


struct zone *xfs_inode_zone;



/*
 * This routine is called to map an inode number within a file
 * system to the buffer containing the on-disk version of the
 * inode.  It returns a pointer to the buffer containing the
 * on-disk inode, and in the dip parameter it returns a pointer
 * to the on-disk inode within that buffer.  The in-core inode
 * pointed to by the ip parameter contains the inode number to
 * retrieve, and we fill in the fields of the inode related to
 * xfs_imap() since we have that information here if they are
 * not already filled in.
 *
 * Use xfs_imap() to determine the size and location of the
 * buffer to read from disk.
 */
buf_t *
xfs_itobp(xfs_mount_t	*mp,
	  xfs_trans_t	*tp,
	  xfs_inode_t	*ip,
	  xfs_dinode_t	**dipp)
{
	xfs_imap_t	imap;
	buf_t		*bp;
	dev_t		dev;

	/*
	 * Call the space managment code to find the location of the
	 * inode on disk.
	 */
	xfs_imap(mp, tp, ip->i_ino, &imap);

	/*
	 * Read in the buffer.  If tp is NULL, xfs_trans_bread() will
	 * default to just a bread() call.
	 */
	dev = mp->m_dev;
	bp = xfs_trans_bread(tp, dev, imap.im_blkno, (int)imap.im_len);

	/*
	 * We need to come up with a disk error handling policy.
	 */
	if (bp->b_flags & B_ERROR) {
		ASSERT(0);
	}

	/*
	 * Fill in some of the fields of ip if they're not already set.
	 */
	if (ip->i_dev == 0) {
		ip->i_dev = dev;
		ip->i_bno = imap.im_agblkno;
		ip->i_index = imap.im_ioffset;
	}

	/*
	 * Set *dipp to point to the on-disk inode in the buffer.
	 */
	*dipp = (xfs_dinode_t *)(bp->b_un.b_addr + imap.im_boffset);
	return (bp);
}

/*
 * Move inode type and inode format specific information from the
 * on-disk inode to the in-core inode.  For fifos, devs, and sockets
 * this means set iu_rdev to the proper value.  For files, directories,
 * and symlinks this means to bring in the in-line data or extent
 * pointers.  For a file in B-tree format, only the root is immediately
 * brought in-core.  The rest will be in-lined in iu_extents when it
 * is first referenced (see xfs_iread_extents()).
 */
void
xfs_iformat(xfs_mount_t		*mp,
	    xfs_inode_t		*ip,
	    xfs_dinode_t	*dip)
{
	register int		size;
	register int		nex;
	register int		nrecs;
	register char		*cp;
	int			csize;
	xfs_btree_lblock_t	*rootbp;
	xfs_bmbt_rec_t		*recp;
	xfs_fsblock_t		*ptrp;
	int			dinode_size;

	switch (ip->i_d.di_mode & IFMT) {
	case IFIFO:
	case IFCHR:
	case IFBLK:
	case IFSOCK:
		ASSERT(dip->di_core.di_format == XFS_DINODE_FMT_DEV);
		ip->i_d.di_size = 0;
		ip->i_u2.iu_rdev = dip->di_u.di_dev;
		return;

	case IFREG:
	case IFLNK:
	case IFDIR:

		switch (dip->di_core.di_format) {
		case XFS_DINODE_FMT_LOCAL:
			/*
			 * The file is in-lined in the on-disk inode.
			 * If it fits into iu_inline_data, then copy
			 * it there, otherwise allocate a buffer for it
			 * and copy the data there.  Either way, set
			 * iu_data to point at the data.
			 */
			size = (int) ip->i_d.di_size;
			if (size <= sizeof(ip->i_u2.iu_inline_data)) {
				ip->i_bytes = sizeof(ip->i_u2.iu_inline_data);
				ip->i_u1.iu_data = ip->i_u2.iu_inline_data;
			} else {
				ip->i_u1.iu_data = (char*)kmem_alloc(size,
								     KM_SLEEP);
				ip->i_bytes = size;
			}
			if (size)
				bcopy(dip->di_u.di_c, ip->i_u1.iu_data, size);
			ip->i_flags |= XFS_IINLINE;
			return;

		case XFS_DINODE_FMT_EXTENTS:
			/*
			 * The file consists of a set of extents all
			 * of which fit into the on-disk inode.
			 * If there are few enough extents to fit into
			 * the iu_inline_ext, then copy them there.
			 * Otherwise allocate a buffer for them and copy
			 * them into it.  Either way, set iu_extents
			 * to point at the extents.
			 */
			nex = (int)dip->di_core.di_nextents;
			size = nex * (int)sizeof(xfs_bmbt_rec_t);
			if (nex <= XFS_INLINE_EXTS) {
				ip->i_bytes = sizeof(ip->i_u2.iu_inline_ext);
				ip->i_u1.iu_extents = ip->i_u2.iu_inline_ext;
			} else {
				ip->i_u1.iu_extents = (xfs_bmbt_rec_t*)
						kmem_alloc(size, KM_SLEEP);
				ip->i_bytes = size;
			}
			if (size)
				bcopy(&dip->di_u.di_bmx, ip->i_u1.iu_extents, size); 
			ip->i_flags |= XFS_IEXTENTS;
			return;

		case XFS_DINODE_FMT_BTREE:
			/*
			 * The file has too many extents to fit into
			 * the inode, so they are in B-tree format.
			 * Allocate a buffer for the root of the B-tree
			 * and copy the root into it.  The i_extents
			 * field will remain NULL until all of the
			 * extents are read in (when they are needed).
			 */
			size = XFS_BMAP_BROOT_SPACE(&(dip->di_u.di_bmbt));
			nrecs = XFS_BMAP_BROOT_NUMRECS(&(dip->di_u.di_bmbt));
			ASSERT(nrecs > 0);
			ip->i_broot_bytes = size;
			ip->i_broot = kmem_alloc(size, KM_SLEEP);
			dinode_size = mp->m_sb.sb_inodesize;

			/*
			 * Copy the btree block header first.
			 */
			rootbp = &(dip->di_u.di_bmbt);
			cp = (char *)ip->i_broot;
			csize = sizeof(xfs_btree_lblock_t);
			bcopy(rootbp, cp, csize);

			/*
			 * Get the addr of the first record into recp
			 * and copy the records.
			 */
			recp = XFS_BMAP_BROOT_REC_ADDR(rootbp, 1, dinode_size);
			cp = (char *)XFS_BMAP_BROOT_REC_ADDR(ip->i_broot, 1,
							     size);
			csize = nrecs * (int)sizeof(xfs_bmbt_rec_t);
			bcopy(recp, cp, csize);

			/*
			 * Get the addr of the first pointer into ptrp
			 * and copy the pointers.
			 */
			ptrp = XFS_BMAP_BROOT_PTR_ADDR(rootbp, 1, dinode_size);
			cp = (char *)XFS_BMAP_BROOT_PTR_ADDR(ip->i_broot, 1,
							     size);
			csize = nrecs * (int)sizeof(*ptrp);
			bcopy(ptrp, cp, csize);
			ip->i_flags |= XFS_IBROOT;
			return;

		default:
			ASSERT(0);
			return;
		}

	default:
		ASSERT(0);
		return;
	}
}
			
				

/*
 * Given a mount structure and an inode number, return a pointer
 * to a newly allocated in-core inode coresponding to the given
 * inode number.
 * 
 * Initialize the inode's attributes and extent pointers if it
 * already has them (it will not if the inode has no links).
 */
xfs_inode_t *
xfs_iread(xfs_mount_t	*mp,
	  xfs_trans_t	*tp,
	  xfs_ino_t	ino)
{
	buf_t		*bp;
	xfs_dinode_t	*dip;
	xfs_inode_t	*ip;

#ifndef SIM
	ip = (xfs_inode_t *)kmem_zone_zalloc(xfs_inode_zone, KM_SLEEP);
#else
	ip = (xfs_inode_t *)kmem_zalloc(sizeof(xfs_inode_t), KM_SLEEP);
#endif
	ip->i_ino = ino;

	/*
	 * Get pointer's to the on-disk inode and the buffer containing it.
	 * Pass in ip so it can fill in i_dev, i_bno, and i_index.
	 */
	bp = xfs_itobp(mp, tp, ip, &dip);

	/*
	 * If the on-disk inode is already linked to a directory
	 * entry, copy all of the inode into the in-core inode.
	 * xfs_iformat() handles copying in the inode format
	 * specific information.
	 * Otherwise, just get the truly permanent information.
	 */
	if (dip->di_core.di_mode != 0) {
		bcopy(&(dip->di_core), &(ip->i_d), sizeof(xfs_dinode_core_t));
		xfs_iformat(mp, ip, dip);
	} else {
		ip->i_d.di_magic = dip->di_core.di_magic;
		ip->i_d.di_version = dip->di_core.di_version;
		ip->i_d.di_gen = dip->di_core.di_gen;
	}	

/*
	ASSERT(!((ip->i_d.di_nlink == 0) && (ip->i_d.di_mode == 0)));
*/
	ASSERT(ip->i_d.di_nlink >= 0);

	/*
	 * Mark the buffer containing the inode as something to keep
	 * around for a while.  This helps to keep recently accessed
	 * meta-data in-core longer.
	 */
	bp->b_ref = XFS_INOREF;

	/*
	 * Use xfs_trans_brelse() to release the buffer containing the
	 * on-disk inode, because it was acquired with xfs_trans_bread()
	 * in xfs_itobp() above.  If tp is NULL, this is just a normal
	 * brelse().  If we're within a transaction, then xfs_trans_brelse()
	 * will only release the buffer if it is not dirty within the
	 * transaction.  It will be OK to release the buffer in this case,
	 * because inodes on disk are never destroyed and we will be
	 * locking the new in-core inode before putting it in the hash
	 * table where other processes can find it.  Thus we don't have
	 * to worry about the inode being changed just because we released
	 * the buffer.
	 */
	xfs_trans_brelse(tp, bp);
	return (ip);
}

/*
 * Read in extents from a btree-format inode.
 * Allocate and fill in iu_extents.  Real work is done in xfs_bmap.c.
 */
void
xfs_iread_extents(xfs_mount_t	*mp,
		  xfs_trans_t	*tp,
		  xfs_inode_t	*ip)
{
	size_t size;

	ASSERT(ip->i_d.di_format == XFS_DINODE_FMT_BTREE);
	size = ip->i_d.di_nextents * sizeof(xfs_bmbt_rec_t);
	ip->i_u1.iu_extents = kmem_alloc(size, KM_SLEEP);
	ip->i_lastex = NULLEXTNUM;
	ip->i_bytes = size;
	ip->i_flags |= XFS_IEXTENTS;
	xfs_bmap_read_extents(mp, tp, ip);
}

/*
 * Allocate an inode on disk and return a copy of it's in-core version.
 * The in-core inode is locked exclusively.  Set mode, nlink, and rdev
 * appropriately within the inode.  The uid and gid for the inode are
 * set according to the contents of the given cred structure.
 *
 * Use xfs_dialloc() to allocate the on-disk inode and xfs_iget()
 * to obtain the in-core version of the allocated inode.  Finally,
 * hand the in-core inode off to xfs_iinit() to fill in the proper
 * values and log the initial contents of the inode.
 */
xfs_inode_t *
xfs_ialloc(xfs_trans_t	*tp,
	   xfs_inode_t	*pip,
	   mode_t	mode,
	   ushort	nlink,
	   dev_t	rdev,
	   struct cred	*cr)
{
	xfs_ino_t	ino;
	xfs_inode_t	*ip;
	vnode_t		*vp;
	uint		flags;
	error_status_t	status;
	__int32_t	curr_time;

	/*
	 * Call the space management code to allocate
	 * the on-disk inode.
	 */
	ino = xfs_dialloc(tp, pip->i_ino, 0, mode);
	ASSERT(ino != NULLFSINO);

	/*
	 * Get the in-core inode with the lock held exclusively.
	 * This is because we're setting fields here we need
	 * to prevent others from looking at until we're done.
	 */
	ip = xfs_trans_iget(tp->t_mountp, tp, ino, XFS_ILOCK_EXCL);
	ASSERT(ip != NULL);

	vp = XFS_ITOV(ip); 
	vp->v_type = IFTOVT(mode);
	vp->v_rdev = rdev;
	ip->i_d.di_mode = (__uint16_t)mode;
	ip->i_d.di_nlink = (__int16_t)nlink;
	ip->i_d.di_uid = (__uint16_t)cr->cr_uid;
	ip->i_d.di_gid = (__uint16_t)cr->cr_gid;
	ip->i_d.di_size = 0;
	ip->i_d.di_nextents = 0;
	curr_time = (__int32_t)time;
	ip->i_d.di_atime.t_sec = curr_time;
	ip->i_d.di_mtime.t_sec = curr_time;
	ip->i_d.di_ctime.t_sec = curr_time;
	uuid_create(&ip->i_d.di_uuid, &status);
	flags = XFS_ILOG_CORE;
	switch (mode & IFMT) {
	case IFIFO:
	case IFCHR:
	case IFBLK:
	case IFSOCK:
		ip->i_d.di_format = XFS_DINODE_FMT_DEV;
		ip->i_u2.iu_rdev = rdev;
		flags |= XFS_ILOG_DEV;
		break;
	case IFREG:
	case IFDIR:
	case IFLNK:
/*		ip->i_d.di_format = XFS_DINODE_FMT_LOCAL; */
		ip->i_d.di_format = XFS_DINODE_FMT_EXTENTS;
		ip->i_flags |= XFS_IEXTENTS;
		break;
	case IFMNT:
		ip->i_d.di_format = XFS_DINODE_FMT_UUID;
		break;
	default:
		ASSERT(0);
	}

	/*
	 * Log the new values stuffed into the inode.
	 */
	xfs_trans_log_inode(tp, ip, flags);
	return ip;
}


/*
 * Reallocate the space for i_broot based on the number of records
 * being added or deleted as indicated in rec_diff.  Move the records
 * and pointers in i_broot to fit the new size.  When shrinking this
 * will eliminate holes between the records and pointers created by
 * the caller.  When growing this will create holes to be filled in
 * by the caller.
 *
 * The caller must not request to add more records than would fit in
 * the on-disk inode root.  If the i_broot is currently NULL, then
 * if we adding records one will be allocated.  The caller must also
 * not request that the number of records go below zero, although
 * it can go to zero.
 *
 * ip -- the inode whose i_broot area is changing
 * ext_diff -- the change in the number of records, positive or negative,
 *	 requested for the i_broot array.
 */
void
xfs_iroot_realloc(xfs_inode_t *ip, int rec_diff)
{
	int			cur_max;
	int			new_max;
	size_t			new_size;
	char			*np;
	char			*op;
	xfs_btree_lblock_t	*new_broot;

	/*
	 * Handle the degenerate case quietly.
	 */
	if (rec_diff == 0) {
		return;
	}

	if (rec_diff > 0) {
		/*
		 * If there wasn't any memory allocated before, just
		 * allocate it now and get out.
		 */
		if (ip->i_broot_bytes == 0) {
			new_size = (size_t)XFS_BMAP_BROOT_SPACE_CALC(rec_diff);
			ip->i_broot = (xfs_btree_lblock_t*)kmem_alloc(new_size,
								     KM_SLEEP);
			ip->i_broot_bytes = new_size;
			return;
		}

		/*
		 * If there is already an existing i_broot, then we need
		 * to realloc() it and shift the pointers to their new
		 * location.  The records don't change location because
		 * they are kept butted up against the btree block header.
		 */
		cur_max = XFS_BMAP_BROOT_MAXRECS(ip->i_broot_bytes);
		new_max = cur_max + rec_diff;
		new_size = (size_t)XFS_BMAP_BROOT_SPACE_CALC(new_max);
		ip->i_broot = (xfs_btree_lblock_t *)
			      kmem_realloc(ip->i_broot, new_size, KM_SLEEP);
		op = (char *)XFS_BMAP_BROOT_PTR_ADDR(ip->i_broot, 1,
						      ip->i_broot_bytes);
		np = (char *)XFS_BMAP_BROOT_PTR_ADDR(ip->i_broot, 1,
						      new_size);
		ip->i_broot_bytes = new_size;
		/*
		 * This depends on bcopy() handling overlapping buffers.
		 */
		bcopy(op, np, cur_max * (int)sizeof(xfs_fsblock_t));
		return;
	}

	/*
	 * rec_diff is less than 0.  In this case, we are shrinking the
	 * i_broot buffer.  It must already exist.  If we go to zero
	 * records, just eliminate the space for the records and pointers
	 * but leave space for the btree block header.
	 */
	ASSERT((ip->i_broot != NULL) && (ip->i_broot_bytes > 0));
	cur_max = XFS_BMAP_BROOT_MAXRECS(ip->i_broot_bytes);
	new_max = cur_max + rec_diff;
	new_size = (size_t)XFS_BMAP_BROOT_SPACE_CALC(new_max);
	ASSERT(new_max >= 0);
	new_broot = (xfs_btree_lblock_t *)kmem_alloc(new_size, KM_SLEEP);
	/*
	 * First copy over the btree block header.
	 */
	bcopy(ip->i_broot, new_broot, sizeof(xfs_btree_lblock_t));

	/*
	 * Only copy the records and pointers if there are any.
	 */
	if (new_max > 0) {
		/*
		 * First copy the records.
		 */
		op = (char *)XFS_BMAP_BROOT_REC_ADDR(ip->i_broot, 1,
						     ip->i_broot_bytes);
		np = (char *)XFS_BMAP_BROOT_REC_ADDR(new_broot, 1, new_size);
		bcopy(op, np, new_max * (int)sizeof(xfs_bmbt_rec_t));	

		/*
		 * Then copy the pointers.
		 */
		op = (char *)XFS_BMAP_BROOT_PTR_ADDR(ip->i_broot, 1,
						     ip->i_broot_bytes);
		np = (char *)XFS_BMAP_BROOT_PTR_ADDR(new_broot, 1, new_size);
		bcopy(op, np, new_max * (int)sizeof(xfs_fsblock_t));	
	}
	kmem_free(ip->i_broot, ip->i_broot_bytes);
	ip->i_broot = new_broot;
	ip->i_broot_bytes = new_size;
	return;
}
	
	
/*
 * This is called when the amount of space needed for iu_extents
 * is increased or decreased.  The change in size is indicated by
 * the number of extents that need to be added or deleted in the
 * ext_diff parameter.
 *
 * If the amount of space needed has decreased below the size of the
 * inline buffer, then switch to using the inline buffer.  Othewise,
 * use kmem_realloc() or kmem_alloc() to adjust the size of the buffer
 * to what is needed.
 *
 * ip -- the inode whose iu_extents area is changing
 * ext_diff -- the change in the number of extents, positive or negative,
 *	 requested for the iu_extents array.
 */
void
xfs_iext_realloc(xfs_inode_t	*ip,
		 int		ext_diff)
{
	int	byte_diff;
	int	new_size;

	if (ext_diff == 0) {
		return;
	}

	byte_diff = ext_diff * (int)sizeof(xfs_bmbt_rec_t);
	new_size = (int)ip->i_bytes + byte_diff;
	ASSERT(new_size >= 0);

	if (new_size == 0) {
		if (ip->i_u1.iu_extents != ip->i_u2.iu_inline_ext) {
			kmem_free(ip->i_u1.iu_extents, ip->i_bytes);
		}
		ip->i_u1.iu_extents = NULL;
	} else if (new_size <= sizeof(ip->i_u2.iu_inline_ext)) {
		/*
		 * If the valid extents can fit in iu_inline_ext,
		 * copy them from the malloc'd vector and free it.
		 */
		if (ip->i_u1.iu_extents != ip->i_u2.iu_inline_ext) {
			/*
			 * For now, empty files are format EXTENTS,
			 * so the iu_extents pointer is null.
			 */
			if (ip->i_u1.iu_extents) {
				bcopy(ip->i_u1.iu_extents,
				      ip->i_u2.iu_inline_ext, new_size);
				kmem_free(ip->i_u1.iu_extents, ip->i_bytes);
			}
			ip->i_u1.iu_extents = ip->i_u2.iu_inline_ext;
		}
	} else {
		/*
		 * Stuck with malloc/realloc.
		 */
		if (ip->i_u1.iu_extents != ip->i_u2.iu_inline_ext) {
			ip->i_u1.iu_extents = (xfs_bmbt_rec_t *)
					      kmem_realloc(ip->i_u1.iu_extents,
							   new_size, KM_SLEEP);
		} else {
			ip->i_u1.iu_extents = (xfs_bmbt_rec_t *)
					      kmem_alloc(new_size, KM_SLEEP);
			bcopy(ip->i_u2.iu_inline_ext, ip->i_u1.iu_extents,
			      sizeof(ip->i_u2.iu_inline_ext));
		}
	}
	ip->i_bytes = new_size;
}

		
/*
 * This is called when the amount of space needed for iu_data
 * is increased or decreased.  The change in size is indicated by
 * the number of bytes that need to be added or deleted in the
 * byte_diff parameter.
 *
 * If the amount of space needed has decreased below the size of the
 * inline buffer, then switch to using the inline buffer.  Othewise,
 * use kmem_realloc() or kmem_alloc() to adjust the size of the buffer
 * to what is needed.
 *
 * ip -- the inode whose iu_data area is changing
 * byte_diff -- the change in the number of bytes, positive or negative,
 *	 requested for the iu_data array.
 */
void
xfs_idata_realloc(xfs_inode_t	*ip,
		  int		byte_diff)
{
	int	new_size;

	if (byte_diff == 0) {
		return;
	}

	new_size = (int)ip->i_bytes + byte_diff;
	ASSERT(new_size >= 0);

	if (new_size == 0) {
		if (ip->i_u1.iu_data != ip->i_u2.iu_inline_data) {
			kmem_free(ip->i_u1.iu_data, ip->i_bytes);
		}
		ip->i_u1.iu_data = NULL;
	} else if (new_size <= sizeof(ip->i_u2.iu_inline_data)) {
		/*
		 * If the valid extents/data can fit in iu_inline_ext/data,
		 * copy them from the malloc'd vector and free it.
		 */
		if (ip->i_u1.iu_data != ip->i_u2.iu_inline_data) {
			bcopy(ip->i_u1.iu_data, ip->i_u2.iu_inline_data,
			      new_size);
			kmem_free(ip->i_u1.iu_data, ip->i_bytes);
			ip->i_u1.iu_data = ip->i_u2.iu_inline_data;
		}
	} else {
		/*
		 * Stuck with malloc/realloc.
		 */
		if (ip->i_u1.iu_data != ip->i_u2.iu_inline_data) {
			ip->i_u1.iu_data = (char *)
					   kmem_realloc(ip->i_u1.iu_data,
							new_size, KM_SLEEP);
		} else {
			ip->i_u1.iu_data = (char *)kmem_alloc(new_size,
							      KM_SLEEP);
			bcopy(ip->i_u2.iu_inline_data, ip->i_u1.iu_data,
			      sizeof(ip->i_u2.iu_inline_data));
		}
	}
	ip->i_bytes = new_size;
}

		


/*
 * Map inode to disk block and offset.
 *
 * mp -- the mount point structure for the current file system
 * tp -- the current transaction
 * ino -- the inode number of the inode to be located
 * imap -- this structure is filled in with the information necessary
 *	 to retrieve the given inode from disk
 */
int
xfs_imap(xfs_mount_t	*mp,
	 xfs_trans_t	*tp,
	 xfs_ino_t	ino,
	 xfs_imap_t	*imap)
{
	xfs_fsblock_t fsbno;
	int off;
	xfs_sb_t *sbp;

	xfs_dilocate(mp, tp, ino, &fsbno, &off);
	sbp = &mp->m_sb;
	imap->im_blkno = xfs_fsb_to_daddr(sbp, fsbno);
	imap->im_len = xfs_btod(sbp, 1);
	imap->im_agblkno = xfs_fsb_to_agbno(sbp, fsbno);
	imap->im_ioffset = (ushort)off;
	imap->im_boffset = (ushort)(off << sbp->sb_inodelog);
	return 1;
}

/*
 * This is called free all the memory associated with an inode.
 * It must free the inode itself and any buffers allocated for
 * iu_extents/iu_data and i_broot.  It must also free the lock
 * associated with the inode.
 */
void
xfs_idestroy(xfs_inode_t *ip)
{
	switch (ip->i_d.di_mode & IFMT) {
	case IFREG:
	case IFDIR:
	case IFLNK:
		if (ip->i_broot != NULL) {
			kmem_free(ip->i_broot, ip->i_broot_bytes);
		}

		/*
		 * If the format is local, then we can't have an extents
		 * array so just look for an inline data array.  If we're
		 * not local then we may or may not have an extents list,
		 * so check and free it up if we do.
		 */
		if (ip->i_d.di_format == XFS_DINODE_FMT_LOCAL) {
			if ((ip->i_u1.iu_data != ip->i_u2.iu_inline_data) && 
			    (ip->i_u1.iu_data != NULL)) {
				kmem_free(ip->i_u1.iu_data, ip->i_bytes);
			}
		} else if ((ip->i_flags & XFS_IEXTENTS) &&
			   (ip->i_u1.iu_extents != NULL) &&
			   (ip->i_u1.iu_extents != ip->i_u2.iu_inline_ext)) {
			kmem_free(ip->i_u1.iu_extents, ip->i_bytes);
		}
		break;
	}
	mrfree(&ip->i_lock);
	mrfree(&ip->i_iolock);
	freesema(&ip->i_flock);
	freesema(&ip->i_pinsema);
#ifndef SIM
	kmem_zone_free(xfs_inode_zone, ip);
#else
	kmem_free(ip, sizeof(xfs_inode_t));
#endif
}


/*
 * Increment the pin count of the given buffer.
 * This value is protected by ipinlock spinlock in the mount structure.
 */
void
xfs_ipin(xfs_inode_t *ip)
{
	int		s;
	xfs_mount_t	*mp;

	ASSERT(ismrlocked(&ip->i_lock, MR_UPDATE));

	mp = ip->i_mount;
	s = splockspl(mp->m_ipinlock, splhi);
	ip->i_pincount++;
	spunlockspl(mp->m_ipinlock, s);	
	return;
}

/*
 * Decrement the pin count of the given inode, and wake up
 * anyone in xfs_iwait_unpin() if the count goes to 0.  The
 * inode must have been previoulsy pinned with a call to xfs_ipin().
 */
void
xfs_iunpin(xfs_inode_t *ip)
{
	int		s;
	xfs_mount_t	*mp;

	ASSERT(ip->i_pincount > 0);

	mp = ip->i_mount;
	s = splockspl(mp->m_ipinlock, splhi);
	ip->i_pincount--;
	if (ip->i_pincount == 0) {
		while (cvsema(&(ip->i_pinsema)) != 0) {
			;
		}
	}
	spunlockspl(mp->m_ipinlock, s);
	return;
}

/*
 * This is called to wait for the given inode to be unpinned.
 * It will sleep until this happens.  The caller must have the
 * inode locked in at least shared mode so that the buffer cannot
 * be subsequently pinned once someone is waiting for it to be
 * unpinned.
 *
 * The ipinlock in the mount structure is used to guard the pincount
 * values of all inodes in a file system.  The i_pinsema is used to
 * sleep until the inode is unpinned.
 */
void
xfs_iunpin_wait(xfs_inode_t *ip)
{
	int		s;
	xfs_mount_t	*mp;

	ASSERT(ismrlocked(&ip->i_lock, MR_UPDATE | MR_ACCESS));

	if (ip->i_pincount == 0) {
		return;
	}

	mp = ip->i_mount;
	s = splockspl(mp->m_ipinlock, splhi);
	if (ip->i_pincount == 0) {
		spunlockspl(mp->m_ipinlock, s);
		return;
	}
	spunlockspl_psema(mp->m_ipinlock, s, &(ip->i_pinsema), PINOD);
	return;
}


/*
 * xfs_iflush() will write a modified inode's changes out to the
 * inode's on disk home.  The caller must have the inode lock held
 * in at least shared mode and the inode flush semaphore must be
 * held as well.  The inode lock will still be held upon return from
 * the call and the caller is free to unlock it.
 * The inode flush lock will be unlocked when the inode reaches the disk.
 * The flags can be one of B_ASYNC, B_DELWRI, or 0.
 * These indicate the way in which the inode's buffer should be
 * written out (0 indicating a synchronous write).
 */
void
xfs_iflush(xfs_inode_t	*ip,
	   uint		flags)
{
	xfs_inode_log_item_t	*iip;
	buf_t			*bp;
	int			brsize;
	xfs_dinode_t		*dip;
	int			nr;
	char			*pd;
	char			*pi;
	int			s;

	ASSERT(ismrlocked(&ip->i_lock, MR_UPDATE|MR_ACCESS));
	ASSERT(valusema(&ip->i_flock) <= 0);

	iip = &ip->i_item;

	/*
	 * We can't flush the inode until it is unpinned, so
	 * wait for it.  We know noone new can pin it, because
	 * we are holding the inode lock shared and you need
	 * to hold it exclusively to pin the inode.
	 */
	xfs_iunpin_wait(ip);

	/*
	 * Get the buffer containing the on-disk inode.
	 */
	bp = xfs_itobp(ip->i_mount, NULL, ip, &dip);

	/*
	 * Clear i_update_core before copying out the data.
	 * This is for coordination with our timestamp updates
	 * that don't hold the inode lock. They will always
	 * update the timestamps BEFORE setting i_update_core,
	 * so if we clear i_update_core after they set it we
	 * are guaranteed to see their updates to the timestamps.
	 * I believe that this depends on strongly ordered memory
	 * semantics, but we have that.
	 */
	ip->i_update_core = 0;

	/*
	 * Copy the dirty parts of the inode into the on-disk
	 * inode.  We always copy out the core of the inode,
	 * because if the inode is dirty at all the core must
	 * be.
	 */
	bcopy(&(ip->i_d), &(dip->di_core), sizeof(xfs_dinode_core_t));

	/*
	 * Each of the following cases stores data into the same region
	 * of the on-disk inode, so only one of them can be valid at
	 * any given time. While it is possible to have conflicting formats
	 * and log flags, e.g. having XFS_ILOG_DATA set when the inode is
	 * in EXTENTS format, this can only happen when the inode has
	 * changed formats after being modified but before being flushed.
	 * In these cases, the format always takes precedence, because the
	 * format indicates the current state of the inode.
	 */
	switch (ip->i_d.di_format) {
	case XFS_DINODE_FMT_LOCAL:
		if ((iip->ili_fields & XFS_ILOG_DATA) && (ip->i_bytes > 0)) {
			ASSERT(ip->i_u1.iu_data != NULL);
			bcopy(ip->i_u1.iu_data, dip->di_u.di_c, ip->i_bytes);
		}
		break;
	case XFS_DINODE_FMT_EXTENTS:
		ASSERT(ip->i_flags & XFS_IEXTENTS);
		ASSERT((ip->i_u1.iu_extents != NULL) || (ip->i_bytes == 0));
		ASSERT((ip->i_u1.iu_extents == NULL) || (ip->i_bytes > 0));
		if ((iip->ili_fields & XFS_ILOG_EXT) && (ip->i_bytes > 0)) {
			bcopy(ip->i_u1.iu_extents, dip->di_u.di_bmx,
			      ip->i_bytes);
		}
		break;
	case XFS_DINODE_FMT_BTREE:
		if ((iip->ili_fields & XFS_ILOG_BROOT) &&
		    (ip->i_broot_bytes > 0)) {
			ASSERT(ip->i_broot != NULL);
			/*
			 * First copy over the btree block header.
			 */
			bcopy(ip->i_broot, &(dip->di_u.di_bmbt),
			      sizeof(xfs_btree_lblock_t));
			/*
			 * Now copy records and pointers, if there are any.
			 */
			if (nr = ip->i_broot->bb_numrecs) {
				brsize = XFS_BMAP_BROOT_SIZE(ip->i_mount->m_sb.sb_inodesize);
				/*
				 * First copy the records.
				 */
				pi = (char *)XFS_BMAP_BROOT_REC_ADDR(ip->i_broot, 1, ip->i_broot_bytes);
				pd = (char *)XFS_BMAP_BROOT_REC_ADDR(&dip->di_u.di_bmbt, 1, brsize);
				bcopy(pi, pd, nr * sizeof(xfs_bmbt_rec_t));
				/*
				 * Next copy the pointers.
				 */
				pi = (char *)XFS_BMAP_BROOT_PTR_ADDR(ip->i_broot, 1, ip->i_broot_bytes);
				pd = (char *)XFS_BMAP_BROOT_PTR_ADDR(&dip->di_u.di_bmbt, 1, brsize);
				bcopy(pi, pd, nr * sizeof(xfs_fsblock_t));
			}
		}
		break;
	case XFS_DINODE_FMT_DEV:
		if (iip->ili_fields & XFS_ILOG_DEV) {
			dip->di_u.di_dev = ip->i_u2.iu_rdev;
		}
		break;
	default:
		ASSERT(0);
		break;
	}

	/*
	 * If ili_fields is set, then set ili_ref so that xfs_iflush_done()
	 * will know to drop the reference taken on the inode in
	 * xfs_trans_log_inode().  The ili_ref field is guarded by
	 * the inode's i_flock.  Then we're done looking at
	 * ili_fields, so clear it.  We can do this since the lock
	 * must be held exclusively in order to set bits in this field.
	 * Also, store the current LSN of the inode so that we can tell
	 * whether the item has moved in the AIL from xfs_iflush_done().
	 * In order to read the lsn we need the AIL lock, because
	 * it is a 64 bit value that cannot be read atomically.
	 */
	if (iip->ili_fields != 0) {
		iip->ili_ref = 1;
		iip->ili_fields = 0;
	}
	ASSERT(sizeof(xfs_lsn_t) == 8);	/* don't need lock if it shrinks */
	s = AIL_LOCK(ip->i_mount);
	iip->ili_flush_lsn = iip->ili_item.li_lsn;
	AIL_UNLOCK(ip->i_mount, s);

	/*
	 * Attach the function xfs_iflush_done to the inode's
	 * buffer.  This will remove the inode from the AIL
	 * and unlock the inode's flush lock when the inode is
	 * completely written to disk.
	 */
	xfs_buf_attach_iodone(bp, (void(*)(buf_t*,xfs_log_item_t*))
			      xfs_iflush_done, (xfs_log_item_t *)iip);

	if (flags & B_DELWRI) {
		xfs_bdwrite(bp);
	} else if (flags & B_ASYNC) {
		xfs_bawrite(bp);
	} else {
		xfs_bwrite(bp);
	}

	return;
}


void
xfs_iprint(xfs_inode_t *ip)
{
	xfs_dinode_core_t *dip;

	printf("Inode %x\n", ip);
	printf("    i_dev %x\n", (uint)ip->i_dev);
	printf("    i_ino %llx\n", ip->i_ino);
	printf("    i_bno %x\n", (uint)ip->i_bno);
	printf("    i_index %d\n", ip->i_index);

	printf("    i_flags %x ", ip->i_flags);
	if (ip->i_flags & XFS_IEXTENTS) {
		printf("EXTENTS ");
	}
	printf("\n");

	printf("    i_vcode %x\n", ip->i_vcode);
	printf("    i_mapcnt %x\n", ip->i_mapcnt);
	printf("    i_bytes %d\n", ip->i_bytes);
	printf("    i_u1.iu_extents/iu_data %x\n", ip->i_u1.iu_extents);
	printf("    i_broot %x\n", ip->i_broot);
	printf("    i_broot_bytes %x\n", ip->i_broot_bytes);

	dip = &(ip->i_d);
	printf("\nOn disk portion\n");
	printf("    di_magic %x\n", dip->di_magic);
	printf("    di_mode %o\n", dip->di_mode);
	printf("    di_version %x\n", (uint)dip->di_version);
	switch (ip->i_d.di_format) {
	case XFS_DINODE_FMT_LOCAL:
		printf("    Inline inode\n");
		break;
	case XFS_DINODE_FMT_EXTENTS:
		printf("    Extents inode\n");
		break;
	case XFS_DINODE_FMT_BTREE:
		printf("    B-tree inode\n");
		break;
	default:
		printf("    Other inode\n");
		break;
	}
	printf("   di_nlink %x\n", dip->di_nlink);
	printf("   di_uid %d\n", dip->di_uid); 
	printf("   di_gid %d\n", dip->di_gid);
	printf("   di_size %d\n", (int)dip->di_size);
	printf("   di_nextents %d\n", (int)dip->di_nextents);
	printf("   di_gen %x\n", dip->di_gen);
}
	










