
#include <sys/param.h>
#define	_KERNEL
#include <sys/buf.h>
#undef _KERNEL
#include <sys/debug.h>
#include <sys/errno.h>
#ifndef SIM
#include <sys/kmem.h>
#endif
#include <sys/mode.h>
#include <sys/vnode.h>
#include <sys/cred.h>
#include "xfs_types.h"
#include "xfs_inum.h"
#include "xfs.h"
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
#include "xfs_inode.h"
#include "xfs_inode_item.h"

#ifdef SIM
#include "sim.h"
#include "bstring.h"
#endif






/*
 * This routine is called to map an inode number within a file
 * system to the buffer containing the on-disk version of the
 * inode.  It returns a pointer to the buffer containing the
 * on-disk inode, and in the dip parameter it returns a pointer
 * to the on-disk inode within that buffer.  The in-core inode
 * pointed to by the ip parameter contains the inode number to
 * retrieve, and we fill in the fields of the inode related to
 * xfs_imap() since we have that information here.
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
	bp = xfs_trans_bread(tp, dev, imap.im_blkno, imap.im_len);

	/*
	 * We need to come up with a disk error handling policy.
	 */
	if (bp->b_flags & B_ERROR) {
		ASSERT(0);
	}

	/*
	 * Fill in some of the fields of ip.
	 */
	ip->i_dev = dev;
	ip->i_bno = imap.im_agblkno;
	ip->i_index = imap.im_ioffset;

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
xfs_iformat(xfs_mount_t *mp, xfs_inode_t *ip, xfs_dinode_t *dip)
{
	register int		size;
	register int		nex;
	register int		nrecs;
	register char		*cp;
	int			csize;
	xfs_btree_block_t	*rootbp;
	xfs_bmbt_rec_t		*recp;
	xfs_agblock_t		*ptrp;
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
			bcopy(dip->di_u.di_c, ip->i_u1.iu_data, size);
			ip->i_flags |= XFS_IINLINE;
			return;

		case XFS_DINODE_FMT_EXTENTS:
			/*
			 * The file consists of a set of extents all
			 * of which fit into the on-disk inode.
			 * If there are few enought extents to fit into
			 * the iu_inline_ext, then copy them there.
			 * Otherwise allocate a buffer for them and copy
			 * them into it.  Either way, set iu_extents
			 * to point at the extents.
			 */
			nex = dip->di_core.di_nextents;
			size = nex * sizeof(xfs_extdesc_t);
			if (nex <= XFS_INLINE_EXTS) {
				ip->i_bytes = sizeof(ip->i_u2.iu_inline_ext);
				ip->i_u1.iu_extents = ip->i_u2.iu_inline_ext;
			} else {
				ip->i_u1.iu_extents = (xfs_extdesc_t*)
						kmem_alloc(size, KM_SLEEP);
				ip->i_bytes = size;
			}
			bcopy(&(dip->di_u.di_bmx), ip->i_u1.iu_extents, size); 
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
			ip->i_broot_bytes = size;
			ip->i_broot = kmem_alloc(size, KM_SLEEP);
			dinode_size = mp->m_sb.sb_inodesize;

			/*
			 * Copy the btree block header first.
			 */
			rootbp = &(dip->di_u.di_bmbt);
			cp = (char *)ip->i_broot;
			csize = sizeof(xfs_btree_block_t);
			bcopy(rootbp, cp, csize);

			/*
			 * Get the addr of the first record into recp
			 * and copy the records.
			 */
			recp = XFS_BMAP_BROOT_REC_ADDR(rootbp, 1, dinode_size);
			cp = (char *)XFS_BMAP_BROOT_REC_ADDR(ip->i_broot, 1,
							     size);
			csize = nrecs * sizeof(xfs_bmbt_rec_t);
			bcopy(recp, cp, csize);

			/*
			 * Get the addr of the first pointer into ptrp
			 * and copy the pointers.
			 */
			ptrp = XFS_BMAP_BROOT_PTR_ADDR(rootbp, 1, dinode_size);
			cp = (char *)XFS_BMAP_BROOT_PTR_ADDR(ip->i_broot, 1,
							     size);
			csize = nrecs * sizeof(xfs_agblock_t);
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
xfs_iread(xfs_mount_t *mp, xfs_trans_t *tp, xfs_ino_t ino)
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

	ASSERT(!((ip->i_d.di_nlink == 0) && (ip->i_d.di_mode == 0)));
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

	/*
	 * Call the space management code to allocate
	 * the on-disk inode.  The 1 value for the
	 * sameag parameter may not be quite correct
	 * here.
	 */
	ino = xfs_dialloc(tp, pip->i_ino, 1, mode);

	/*
	 * Get the in-core inode with the lock held exclusively.
	 * This is because we're setting fields here we need
	 * to prevent others from looking at until we're done.
	 */
	ip = xfs_iget(tp->t_mountp, tp, ino, XFS_ILOCK_EXCL);
	ASSERT(ip != NULL);

#ifdef NOTYET
	vp = XFS_ITOV(ip); 
	vp->v_type = IFTOVT(mode);
	vp->v_rdev = rdev;
#endif
	ip->i_d.di_mode = mode;
	ip->i_d.di_nlink = nlink;
	ip->i_d.di_uid = cr->cr_uid;
	ip->i_d.di_gid = cr->cr_gid;
	ip->i_d.di_size = 0;
	ip->i_d.di_nextents = 0;
#ifdef NOTYET
	/*
	 * Does this global actually exist?
	 */
	ip->i_d.di_atime = time;
	ip->i_d.di_mtime = time;
	ip->i_d.di_ctime = time;
	ip->i_d.di_uuid = uuid_gen();	/* ????? */
#endif
	flags = XFS_ILOG_META;
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
		ip->i_d.di_format = XFS_DINODE_FMT_LOCAL;
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
#ifdef NOTYET
	xfs_trans_log_inode(tp, ip, flags, 0, 0);
#endif
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
 */
void
xfs_iroot_realloc(xfs_inode_t *ip, int rec_diff)
{
	int			cur_max;
	int			new_max;
	size_t			new_size;
	char			*np;
	char			*op;
	xfs_btree_block_t	*new_broot;

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
			ip->i_broot = (xfs_btree_block_t*)kmem_alloc(new_size,
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
		ip->i_broot = (xfs_btree_block_t *)
			      kmem_realloc(ip->i_broot, new_size, KM_SLEEP);
		op = (char *)XFS_BMAP_BROOT_PTR_ADDR(ip->i_broot, 1,
						      ip->i_broot_bytes);
		np = (char *)XFS_BMAP_BROOT_PTR_ADDR(ip->i_broot, 1,
						      new_size);
		ip->i_broot_bytes = new_size;
		/*
		 * This depends on bcopy() handling overlapping buffers.
		 */
		bcopy(op, np, cur_max * sizeof(xfs_agblock_t));
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
	new_broot = (xfs_btree_block_t *)kmem_alloc(new_size, KM_SLEEP);
	/*
	 * First copy over the btree block header.
	 */
	bcopy(ip->i_broot, new_broot, sizeof(xfs_btree_block_t));

	/*
	 * Only copy the records and pointers if there are any.
	 */
	if (new_max > 0) {
		/*
		 * First copy then records.
		 */
		op = (char *)XFS_BMAP_BROOT_REC_ADDR(ip->i_broot, 1,
						     ip->i_broot_bytes);
		np = (char *)XFS_BMAP_BROOT_REC_ADDR(new_broot, 1, new_size);
		bcopy(op, np, new_max * sizeof(xfs_bmbt_rec_t));	

		/*
		 * Then copy the pointers.
		 */
		op = (char *)XFS_BMAP_BROOT_PTR_ADDR(ip->i_broot, 1,
						     ip->i_broot_bytes);
		np = (char *)XFS_BMAP_BROOT_PTR_ADDR(new_broot, 1, new_size);
		bcopy(op, np, new_max * sizeof(xfs_bmbt_rec_t));	
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
 */
void
xfs_iext_realloc(xfs_inode_t *ip, int ext_diff)
{
	int	byte_diff;
	int	new_size;

	if (ext_diff == 0) {
		return;
	}

	byte_diff = ext_diff * sizeof(xfs_extdesc_t);
	new_size = ip->i_bytes + byte_diff;
	/*
	 * If the valid extents can fit in iu_inline_ext,
	 * copy them from the malloc'd vector and free it.
	 */
	if (new_size <= sizeof(ip->i_u2.iu_inline_ext)) {
		if (ip->i_u1.iu_extents != ip->i_u2.iu_inline_ext) {
			bcopy(ip->i_u1.iu_extents, ip->i_u2.iu_inline_ext,
			      new_size);
			kmem_free(ip->i_u1.iu_extents, ip->i_bytes);
			ip->i_u1.iu_extents = ip->i_u2.iu_inline_ext;
		}
	} else {
		/*
		 * Stuck with malloc/realloc.
		 */
		if (ip->i_u1.iu_extents != ip->i_u2.iu_inline_ext) {
			ip->i_u1.iu_extents = (xfs_extdesc_t *)
					      kmem_realloc(ip->i_u1.iu_extents,
							   new_size, KM_SLEEP);
		} else {
			ip->i_u1.iu_extents = (xfs_extdesc_t *)
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
 */
void
xfs_idata_realloc(xfs_inode_t *ip, int byte_diff)
{
	int	new_size;

	if (byte_diff == 0) {
		return;
	}

	new_size = ip->i_bytes + byte_diff;
	ASSERT(new_size >= 0);
	/*
	 * If the valid extents/data can fit in iu_inline_ext/data,
	 * copy them from the malloc'd vector and free it.
	 */
	if (new_size <= sizeof(ip->i_u2.iu_inline_data)) {
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
 */
int
xfs_imap(xfs_mount_t *mp, xfs_trans_t *tp, xfs_ino_t ino, xfs_imap_t *imap)
{
	xfs_fsblock_t fsbno;
	int off;
	xfs_sb_t *sbp;

	if (!xfs_dilocate(mp, tp, ino, &fsbno, &off))
		return 0;
	sbp = &mp->m_sb;
	imap->im_blkno = xfs_fsb_to_daddr(sbp, fsbno);
	imap->im_len = xfs_btod(sbp, 1);
	imap->im_agblkno = xfs_fsb_to_agbno(sbp, fsbno);
	imap->im_ioffset = off;
	imap->im_boffset = off << sbp->sb_inodelog;
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
		if (ip->i_flags & XFS_IBROOT) {
			ASSERT(ip->i_broot != NULL);
			kmem_free(ip->i_broot, ip->i_broot_bytes);
		}
		if (ip->i_flags & XFS_IEXTENTS) {
			if (ip->i_u1.iu_extents != ip->i_u2.iu_inline_ext) {
				kmem_free(ip->i_u1.iu_extents, ip->i_bytes);
			}
		} else if (ip->i_flags & XFS_IINLINE) {
			if (ip->i_u1.iu_data != ip->i_u2.iu_inline_data) {
				kmem_free(ip->i_u1.iu_data, ip->i_bytes);
			}
		}
		break;
	}
	mrfree(&ip->i_lock);
#ifndef SIM
	kmem_zone_free(xfs_inode_zone, ip);
#else
	kmem_free(ip, sizeof(xfs_inode_t));
#endif
}
