
#include <sys/types.h>
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
#include "xfs_btree.h"
#include "xfs_dinode.h"
#include "xfs_inode.h"

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
	register char		*bp;
	xfs_btree_block_t	*rootbp;
	xfs_bmbt_rec_t		*recp;
	xfs_agblock_t		*ptrp;
	int			inode_size;

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
			inode_size = mp->m_sb->sb_inodesize;

			/*
			 * Copy the btree block header first.
			 */
			rootbp = &(dip->di_u.di_bmbt);
			bp = (char *)ip->i_broot;
			size = sizeof(xfs_btree_block_t);
			bcopy(rootbp, bp, size);

			/*
			 * Get the addr of the first record into recp
			 * and copy the records.
			 */
			recp = XFS_BMAP_BROOT_REC_ADDR(rootbp, 0, inode_size);
			bp += size;
			size = nrecs * sizeof(xfs_bmbt_rec_t);
			bcopy(recp, bp, size);

			/*
			 * Get the addr of the first pointer into ptrp
			 * and copy the pointers.
			 */
			ptrp = XFS_BMAP_BROOT_PTR_ADDR(rootbp, 0, inode_size);
			bp += size;
			size = nrecs * sizeof(xfs_agblock_t);
			bcopy(ptrp, bp, size);
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
	 * Otherwise, just get the generation number.
	 */
	if (dip->di_core.di_mode != 0) {
		bcopy(&(dip->di_core), &(ip->i_d), sizeof(xfs_dinode_core_t));
		xfs_iformat(mp, ip, dip);
	} else {
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















