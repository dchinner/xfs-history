#include <sys/param.h>
#ifdef SIM
#define	_KERNEL 1
#endif
#include <sys/buf.h>
#include <sys/vnode.h>
#include <sys/pfdat.h>
#include <sys/cred.h>
#include <sys/grio.h>
#ifdef SIM
#undef _KERNEL
#endif
#include <sys/vfs.h>
#include <sys/debug.h>
#include <sys/errno.h>
#include <sys/stat.h>
#include <sys/mode.h>
#include <sys/uuid.h>
#include <sys/kmem.h>
#include <sys/ktrace.h>
#ifdef SIM
#include <bstring.h>
#include <stdio.h>
#else
#include <sys/systm.h>
#endif
#include <stddef.h>
#include "xfs_types.h"
#include "xfs_inum.h"
#include "xfs_log.h"
#include "xfs_trans.h"
#include "xfs_sb.h"
#include "xfs_ag.h"
#include "xfs_mount.h"
#include "xfs_alloc_btree.h"
#include "xfs_bmap_btree.h"
#include "xfs_ialloc_btree.h"
#include "xfs_btree.h"
#include "xfs_imap.h"
#include "xfs_ialloc.h"
#include "xfs_bmap.h"
#include "xfs_dinode.h"
#include "xfs_inode_item.h"
#include "xfs_inode.h"
#include "xfs_buf_item.h"
#include "xfs_bio.h"
#include "xfs_rw.h"
#include "xfs_error.h"

#ifdef SIM
#include "sim.h"
#endif


zone_t *xfs_inode_zone;

/*
 * Used in xfs_itruncate().  This number needs more thought.
 */
#define	XFS_ITRUNC_MAX_EXTENTS	128

STATIC buf_t *
xfs_itobp(
	xfs_mount_t	*mp,
	xfs_trans_t	*tp,
	xfs_inode_t	*ip,
	xfs_dinode_t	**dipp);

STATIC buf_t *
xfs_inotobp(
	xfs_mount_t	*mp,
	xfs_trans_t	*tp,
	xfs_ino_t	ino,
	xfs_dinode_t	**dipp);
	
STATIC void
xfs_iunlink_remove(
	xfs_trans_t	*tp,
	xfs_inode_t	*ip);

#ifdef DEBUG
STATIC void
xfs_validate_extents(
	xfs_bmbt_rec_t	*ep,
	int		nrecs);
#else /* DEBUG */
#define xfs_validate_extents(ep, nrecs)
#endif /* DEBUG */		     

/*
 * This routine is called to map an inode within a file
 * system to the buffer containing the on-disk version of the
 * inode.  It returns a pointer to the buffer containing the
 * on-disk inode, and in the dip parameter it returns a pointer
 * to the on-disk inode within that buffer.  The in-core inode
 * pointed to by the ip parameter contains the inode number to
 * retrieve, and we fill in the fields of the inode related to
 * xfs_imap() since we have that information here if they are
 * not already filled in.
 *
 * Use xfs_inotobp() to do most of the real work.
 */
STATIC buf_t *
xfs_itobp(
	xfs_mount_t	*mp,
	xfs_trans_t	*tp,
	xfs_inode_t	*ip,
	xfs_dinode_t	**dipp)
{
	buf_t		*bp;

	bp = xfs_inotobp(mp, tp, ip->i_ino, dipp);

	/*
	 * Fill in some of the fields of ip if they're not already set.
	 */
	if (ip->i_dev == 0) {
		ip->i_dev = mp->m_dev;
	}

	return (bp);
}

/*
 * This routine is called to map an inode number within a file
 * system to the buffer containing the on-disk version of the
 * inode.  It returns a pointer to the buffer containing the
 * on-disk inode, and in the dip parameter it returns a pointer
 * to the on-disk inode within that buffer.
 *
 * Use xfs_imap() to determine the size and location of the
 * buffer to read from disk.
 */
STATIC buf_t *
xfs_inotobp(
	xfs_mount_t	*mp,
	xfs_trans_t	*tp,
	xfs_ino_t	ino,
	xfs_dinode_t	**dipp)
{
	xfs_imap_t	imap;
	buf_t		*bp;
	dev_t		dev;

	/*
	 * Call the space managment code to find the location of the
	 * inode on disk.
	 */
	xfs_imap(mp, tp, ino, &imap);

	/*
	 * Read in the buffer.  If tp is NULL, xfs_trans_read_buf() will
	 * default to just a read_buf() call.
	 */
	dev = mp->m_dev;
	bp = xfs_trans_read_buf(tp, dev, imap.im_blkno, (int)imap.im_len, 0);

	/*
	 * We need to come up with a disk error handling policy.
	 */
	if (bp->b_flags & B_ERROR) {
		ASSERT(0);
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
xfs_iformat(
	xfs_mount_t	*mp,
	xfs_inode_t	*ip,
	xfs_dinode_t	*dip)
{
	register int		size;
	register int		nex;
	register int		nrecs;
	int			dinode_size;
	int			real_size;

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
			 * If we allocate a buffer for the data, make
			 * sure that its size is a multiple of 4 and
			 * record the real size in i_real_bytes.
			 */
			size = (int) ip->i_d.di_size;
			real_size = 0;
			if (size == 0) {
				ip->i_u1.iu_data = NULL;
			} else if (size <= sizeof(ip->i_u2.iu_inline_data)) {
				ip->i_u1.iu_data = ip->i_u2.iu_inline_data;
			} else {
				real_size = (((size + 3) >> 2) << 2);
				ip->i_u1.iu_data =
					(char*)kmem_alloc(real_size, KM_SLEEP);
			}
			ip->i_bytes = size;
			ip->i_real_bytes = real_size;
			if (size)
				bcopy(dip->di_u.di_c, ip->i_u1.iu_data, size);
			ip->i_flags &= ~XFS_IEXTENTS;
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
			real_size = 0;
			if (nex == 0) {
				ip->i_u1.iu_extents = NULL;
			} else if (nex <= XFS_INLINE_EXTS) {
				ip->i_u1.iu_extents = ip->i_u2.iu_inline_ext;
			} else {
				ip->i_u1.iu_extents = (xfs_bmbt_rec_t*)
						kmem_alloc(size, KM_SLEEP);
				real_size = size;
			}
			ip->i_bytes = size;
			ip->i_real_bytes = real_size;
			if (size) {
				xfs_validate_extents(dip->di_u.di_bmx, nex);
				bcopy(dip->di_u.di_bmx, ip->i_u1.iu_extents,
				      size);
				xfs_bmap_trace_exlist("xfs_iformat", ip, nex);
			}
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
			 * Copy and convert from the on-disk structure
			 * to the in-memory structure.
			 */
			xfs_bmdr_to_bmbt(&dip->di_u.di_bmbt,
				XFS_BMAP_BROOT_SIZE(dinode_size),
				ip->i_broot, size);
			ip->i_flags &= ~XFS_IEXTENTS;
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
xfs_iread(
	xfs_mount_t	*mp,
	xfs_trans_t	*tp,
	xfs_ino_t	ino)
{
	buf_t		*bp;
	xfs_dinode_t	*dip;
	xfs_inode_t	*ip;

	ASSERT(xfs_inode_zone != NULL);

	ip = kmem_zone_zalloc(xfs_inode_zone, KM_SLEEP);
	ip->i_ino = ino;

	/*
	 * Get pointer's to the on-disk inode and the buffer containing it.
	 * Pass in ip so it can fill in i_dev.
	 */
	bp = xfs_itobp(mp, tp, ip, &dip);

#ifndef SIM
	/*
	 * Initialize inode's trace buffers.
	 * Do this before xfs_iformat in case it adds entries.
	 */
	ip->i_xtrace = ktrace_alloc(XFS_BMAP_KTRACE_SIZE);
	ip->i_btrace = ktrace_alloc(XFS_BMBT_KTRACE_SIZE);
#endif

	/*
	 * If the on-disk inode is already linked to a directory
	 * entry, copy all of the inode into the in-core inode.
	 * xfs_iformat() handles copying in the inode format
	 * specific information.
	 * Otherwise, just get the truly permanent information.
	 */
	if (dip->di_core.di_mode != 0) {
		bcopy(&(dip->di_core), &(ip->i_d),sizeof(xfs_dinode_core_t));
		xfs_iformat(mp, ip, dip);
	} else {
		ip->i_d.di_magic = dip->di_core.di_magic;
		ip->i_d.di_version = dip->di_core.di_version;
		ip->i_d.di_gen = dip->di_core.di_gen;
	}	

	ip->i_delayed_blks = 0;

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
	 * on-disk inode, because it was acquired with xfs_trans_read_buf()
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
xfs_iread_extents(
	xfs_trans_t	*tp,
	xfs_inode_t	*ip)
{
	size_t size;

	ASSERT(ip->i_d.di_format == XFS_DINODE_FMT_BTREE);
	size = ip->i_d.di_nextents * sizeof(xfs_bmbt_rec_t);
	ip->i_u1.iu_extents = kmem_alloc(size, KM_SLEEP);
	ip->i_lastex = NULLEXTNUM;
	ip->i_bytes = ip->i_real_bytes = size;
	ip->i_flags |= XFS_IEXTENTS;
	xfs_bmap_read_extents(tp, ip);
	xfs_validate_extents(ip->i_u1.iu_extents, ip->i_d.di_nextents);
}

/*
 * Allocate an inode on disk and return a copy of it's in-core version.
 * The in-core inode is locked exclusively.  Set mode, nlink, and rdev
 * appropriately within the inode.  The uid and gid for the inode are
 * set according to the contents of the given cred structure.
 *
 * Use xfs_dialloc() to allocate the on-disk inode. If xfs_dialloc()
 * has a free inode available, call xfs_iget()
 * to obtain the in-core version of the allocated inode.  Finally,
 * fill in the inode and log its initial contents.  In this case,
 * ialloc_context would be set to NULL and call_again set to false.
 *
 * If xfs_dialloc() does not have an available inode,
 * it will replenish its supply by doing an allocation. Since we can
 * only do one allocation within a transaction without deadlocks, we 
 * must commit the current transaction before returning the inode itself.
 * In this case, therefore, we will set call_again to true and return.
 * The caller should then commit the current transaction, start a new
 * transaction, and call xfs_ialloc() again to actually get the inode.
 *
 * To ensure that some other process does not grab the inode that
 * was allocated during the first call to xfs_ialloc(), this routine
 * also returns the [locked] bp pointing to the head of the freelist
 * as ialloc_context.  The caller should hold this buffer across
 * the commit and pass it back into this routine on the second call.
 */
xfs_inode_t *
xfs_ialloc(
	xfs_trans_t	*tp,
	xfs_inode_t	*pip,
	mode_t		mode,
	ushort		nlink,
	dev_t		rdev,
	cred_t		*cr,
	buf_t		**ialloc_context,
	boolean_t	*call_again)
{
	xfs_ino_t	ino;
	xfs_inode_t	*ip;
	vnode_t		*vp;
	uint		flags;
	uint_t		status;
	__int32_t	curr_time;

	/*
	 * Call the space management code to pick
	 * the on-disk inode to be allocated.
	 */
#ifndef SIM
	ASSERT(pip != NULL);
#endif
	ino = xfs_dialloc(tp, pip ? pip->i_ino : 0, mode, ialloc_context,
			  call_again);
	if (*call_again || ino == NULLFSINO) {
                return NULL;
        }
	ASSERT(*ialloc_context == NULL);

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
	/*
	 * di_gen will have been taken care of in xfs_iread.
	 */
	ip->i_d.di_extsize = 0;
	ip->i_d.di_flags = 0;
	flags = XFS_ILOG_CORE;
	switch (mode & IFMT) {
	case IFIFO:
	case IFCHR:
	case IFBLK:
	case IFSOCK:
		ip->i_d.di_format = XFS_DINODE_FMT_DEV;
		ip->i_u2.iu_rdev = rdev;
		ip->i_flags = 0;
		flags |= XFS_ILOG_DEV;
		break;
	case IFREG:
	case IFDIR:
	case IFLNK:
		ip->i_d.di_format = XFS_DINODE_FMT_EXTENTS;
		ip->i_flags = XFS_IEXTENTS;
		ip->i_bytes = ip->i_real_bytes = 0;
		ip->i_u1.iu_extents = NULL;
		break;
	case IFMNT:
		ip->i_d.di_format = XFS_DINODE_FMT_UUID;
		ip->i_flags = 0;
		uuid_create(&ip->i_u2.iu_uuid, &status);
		flags |= XFS_ILOG_UUID;
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
 * Start the truncation of the file to new_size.  The new size
 * must be smaller than the current size.  This routine will
 * clear the buffer and page caches of file data in the removed
 * range, and xfs_itruncate_finish() will remove the underlying
 * disk blocks.
 *
 * The inode must have its I/O lock locked EXCLUSIVELY, and it
 * must NOT have the inode lock held at all.  This is because we're
 * calling into the buffer/page cache code and we can't hold the
 * inode lock when we do so.
 *
 * The flags parameter can have either the value XFS_ITRUNC_DEFINITE
 * or XFS_ITRUNC_MAYBE.  The XFS_ITRUNC_MAYBE value should be used
 * in the case that the caller is locking things out of order and
 * may not be able to call xfs_itruncate_finish() with the inode lock
 * held without dropping the I/O lock.  If the caller must drop the
 * I/O lock before calling xfs_itruncate_finish(), then xfs_itruncate_start()
 * must be called again with all the same restrictions as the initial
 * call.
 */
void
xfs_itruncate_start(
	xfs_inode_t	*ip,
	uint		flags,		    
	xfs_fsize_t	new_size)
{
	xfs_fsblock_t	first_block;
	xfs_fsblock_t	first_unmap_block;
	xfs_fsblock_t	last_block;
	xfs_fsize_t	last_byte;
	off_t		toss_start;
	xfs_extlen_t	unmap_len;
	xfs_mount_t	*mp;
	xfs_trans_t	*ntp;
	int		done;
	xfs_bmap_free_t	free_list;
	timestruc_t	tv;

	ASSERT(ismrlocked(&ip->i_iolock, MR_UPDATE) != 0);
	ASSERT(new_size < ip->i_d.di_size);
	ASSERT((flags == XFS_ITRUNC_DEFINITE) ||
	       (flags == XFS_ITRUNC_MAYBE));

	mp = ip->i_mount;
	/*
	 * Call ptossvp() or pflushinvalvp() to get rid of pages and buffers
	 * overlapping the region being removed.  We have to use
	 * the less efficient pflushinvalvp() in the case that the
	 * caller may not be able to finish the truncate without
	 * dropping the inode's I/O lock.  Make sure
	 * to catch any pages brought in by buffers overlapping
	 * the EOF by searching out beyond the isize by our
	 * writeio size. We round new_size up to a block boundary
	 * so that we don't toss things on the same block as
	 * new_size but before it.
	 */
	toss_start = XFS_B_TO_FSB(mp, new_size);
	toss_start = XFS_FSB_TO_B(mp, toss_start);
	last_byte = ip->i_d.di_size + (1 << mp->m_writeio_log);
	if (last_byte > toss_start) {
		if (flags & XFS_ITRUNC_DEFINITE) {
			ptossvp(XFS_ITOV(ip), toss_start, last_byte);
		} else {
			pflushinvalvp(XFS_ITOV(ip), toss_start, last_byte);
		}
	}
}		    

/*
 * Shrink the file to the given new_size.  The new
 * size must be smaller than the current size.
 * This will free up the underlying blocks
 * in the removed range after a call to xfs_itrunc_start().
 *
 * The transaction passed to this routine must have made
 * a permanent log reservation of at least XFS_ITRUNCATE_LOG_RES.
 * This routine may commit the given transaction and
 * start new ones, so make sure everything involved in
 * the transaction is tidy before calling here.
 * Some transaction will be returned to the caller to be
 * committed.  The incoming transaction must already include
 * the inode, and both inode locks must be held exclusively.
 * The inode must also be "held" within the transaction.  On
 * return the inode will be "held" within the returned transaction.
 * This routine does NOT require any disk space to be reserved
 * for it within the transaction.
 */
void
xfs_itruncate_finish(
	xfs_trans_t	**tp,
	xfs_inode_t	*ip,
	xfs_fsize_t	new_size)
{
	xfs_fsblock_t	first_block;
	xfs_fsblock_t	first_unmap_block;
	xfs_fsblock_t	last_block;
	xfs_extlen_t	unmap_len;
	xfs_mount_t	*mp;
	xfs_trans_t	*ntp;
	int		done;
	int		committed;
	xfs_bmap_free_t	free_list;
	timestruc_t	tv;

	ASSERT(ismrlocked(&ip->i_iolock, MR_UPDATE) != 0);
	ASSERT(ismrlocked(&ip->i_lock, MR_UPDATE) != 0);
	ASSERT(new_size < ip->i_d.di_size);
	ASSERT(*tp != NULL);
	ASSERT((*tp)->t_flags & XFS_TRANS_PERM_LOG_RES);
	ASSERT(ip->i_transp == *tp);
	ASSERT(ip->i_item.ili_flags & XFS_ILI_HOLD);

	ntp = *tp;
	mp = (ntp)->t_mountp;
	first_unmap_block = XFS_B_TO_FSB(mp, new_size);
	/*
	 * Subtract 1 from the size so that we get the correct
	 * last block when the size is a multiple of the block
	 * size.
	 */
	last_block = XFS_B_TO_FSBT(mp, ip->i_d.di_size - 1);
	if (first_unmap_block > last_block) {
		/*
		 * The old size and new size both fall on the same
		 * fs block, so there are no blocks to free.
		 * Just set the new size of the inode and return.
		 */
		ip->i_d.di_size = new_size;
		xfs_trans_log_inode(ntp, ip, XFS_ILOG_CORE);
		return;
	}
	done = 0;
	unmap_len = last_block - first_unmap_block + 1;
	while (!done) {
		/*
		 * Free up up to XFS_ITRUNC_MAX_EXTENTS.  xfs_bunmapi()
		 * will tell us whether it freed the entire range or
		 * not.
		 */
		XFS_BMAP_INIT(&free_list, &first_block);
		first_block = xfs_bunmapi(ntp, ip, first_unmap_block,
					  unmap_len, XFS_ITRUNC_MAX_EXTENTS,
					  first_block, &free_list, &done);

		/*
		 * Duplicate the transaction that has the permanent
		 * reservation and commit the old transaction.
		 */
		committed = xfs_bmap_finish(tp, &free_list, first_block);
		ntp = *tp;
		if (committed) {
			/*
			 * The first xact was committed,
			 * so add the inode to the new one.
			 * Mark it dirty so it will be logged
			 * and moved forward in the log as
			 * part of every commit.
			 */
			xfs_trans_ijoin(ntp, ip,
					XFS_ILOCK_EXCL | XFS_IOLOCK_EXCL);
			xfs_trans_ihold(ntp, ip);
			xfs_trans_log_inode(ntp, ip, XFS_ILOG_CORE);
		}
		ntp = xfs_trans_dup(ntp);
		xfs_trans_commit(*tp, 0);
		*tp = ntp;
		xfs_trans_reserve(ntp, 0, XFS_ITRUNCATE_LOG_RES(mp), 0,
				  XFS_TRANS_PERM_LOG_RES,
				  XFS_ITRUNCATE_LOG_COUNT);

		/*
		 * Add the inode being truncated to the next chained
		 * transaction.
		 */
		xfs_trans_ijoin(ntp, ip, XFS_ILOCK_EXCL | XFS_IOLOCK_EXCL);
		xfs_trans_ihold(ntp, ip);
	}
	ip->i_d.di_size = new_size;
	nanotime(&tv);
	ip->i_d.di_ctime.t_sec = tv.tv_sec;
	ip->i_d.di_mtime.t_sec = tv.tv_sec;
	xfs_trans_log_inode(ntp, ip, XFS_ILOG_CORE);
}


/*
 * xfs_igrow_start
 *
 * Do the first part of growing a file: zero any data in the last
 * block that is beyond the old EOF.  We need to do this before
 * the inode is joined to the transaction to modify the i_size.
 * That way we can drop the inode lock and call into the buffer
 * cache to get the buffer mapping the EOF.
 */
void
xfs_igrow_start(
	xfs_inode_t	*ip,
	xfs_fsize_t	new_size,
	cred_t		*credp)
{
	xfs_fsize_t	isize;

	ASSERT(ismrlocked(&(ip->i_lock), MR_UPDATE) != 0);
	ASSERT(ismrlocked(&(ip->i_iolock), MR_UPDATE) != 0);
	ASSERT(new_size > ip->i_d.di_size);

	isize = ip->i_d.di_size;
	if (isize != 0) {
		/*
		 * Zero any pages that may have been created by
		 * xfs_write_file() beyond the end of the file.
		 */
		xfs_zero_eof(ip, new_size, isize, credp);
	}
}

/*
 * xfs_igrow_finish
 *
 * This routine is called to extend the size of a file.
 * The inode must have both the iolock and the ilock locked
 * for update and it must be a part of the current transaction.
 * The xfs_igrow_start() function must have been called previously.
 */
void
xfs_igrow_finish(
	xfs_trans_t	*tp,
	xfs_inode_t	*ip,
	xfs_fsize_t	new_size)
{
	timestruc_t	tv;

	ASSERT(ismrlocked(&(ip->i_lock), MR_UPDATE) != 0);
	ASSERT(ismrlocked(&(ip->i_iolock), MR_UPDATE) != 0);
	ASSERT(ip->i_transp == tp);
	ASSERT(new_size > ip->i_d.di_size);

	/*
	 * Update the file size and inode change timestamp.
	 */
	ip->i_d.di_size = new_size;
	nanotime(&tv);
	ip->i_d.di_ctime.t_sec = tv.tv_sec;
	xfs_trans_log_inode(tp, ip, XFS_ILOG_CORE);

}


/*
 * This is called when the inode's link count goes to 0.
 * We place the on-disk inode on a list in the AGI.  It
 * will be pulled from this list when the inode is freed.
 */
void
xfs_iunlink(
	xfs_trans_t	*tp,
	xfs_inode_t	*ip)
{
	xfs_mount_t	*mp;
	xfs_agi_t	*agi;
	xfs_dinode_t	*dip;
	buf_t		*agibp;
	buf_t		*ibp;
	xfs_agnumber_t	agno;
	daddr_t		agdaddr;
	xfs_agino_t	agino;
	short		bucket_index;
	short		offset;
	
	ASSERT(ip->i_d.di_nlink == 0);
	ASSERT(ip->i_transp == tp);

	mp = tp->t_mountp;
	agno = XFS_INO_TO_AGNO(mp, ip->i_ino);
	agdaddr = XFS_AG_DADDR(mp, agno, XFS_AGI_DADDR);

	/*
	 * Get the agi buffer first.  It ensures lock ordering
	 * on the list.
	 */
	agibp = xfs_trans_read_buf(tp, mp->m_dev, agdaddr, 1, 0);
	agi = XFS_BUF_TO_AGI(agibp);
	ASSERT(agi->agi_magicnum == XFS_AGI_MAGIC);

	/*
	 * Get the index into the agi hash table for the
	 * list this inode will go on.
	 */
	agino = XFS_INO_TO_AGINO(mp, ip->i_ino);
	bucket_index = agino % XFS_AGI_UNLINKED_BUCKETS;

	if (agi->agi_unlinked[bucket_index] != NULLAGINO) {
		/*
		 * There is already another inode in the bucket we need
		 * to add ourselves to.  Add us at the front of the list.
		 * Here we put the head pointer into our next pointer,
		 * and then we fall through to point the head at us.
		 */
		ibp = xfs_itobp(mp, tp, ip, &dip);
		ASSERT(dip->di_next_unlinked == NULLAGINO);
		dip->di_next_unlinked = agi->agi_unlinked[bucket_index];
		offset = ((char *)dip - (char *)(ibp->b_un.b_addr)) +
			offsetof(xfs_dinode_t, di_next_unlinked);
		xfs_trans_log_buf(tp, ibp, offset,
				  (offset + sizeof(xfs_agino_t)));
	}

	/*
	 * Point the bucket head pointer at the inode being inserted.
	 */
	agi->agi_unlinked[bucket_index] = agino;
	offset = offsetof(xfs_agi_t, agi_unlinked) +
		(sizeof(xfs_agino_t) * bucket_index);
	xfs_trans_log_buf(tp, agibp, offset, (offset + sizeof(xfs_agino_t)));
}	    

/*
 * Pull the on-disk inode from the AGI unlinked list.
 */
STATIC void
xfs_iunlink_remove(
	xfs_trans_t	*tp,
	xfs_inode_t	*ip)
{
	xfs_ino_t	next_ino;
	xfs_mount_t	*mp;
	xfs_agi_t	*agi;
	xfs_dinode_t	*dip;
	buf_t		*agibp;
	buf_t		*ibp;
	xfs_agnumber_t	agno;
	daddr_t		agdaddr;
	xfs_agino_t	agino;
	xfs_agino_t	next_agino;
	buf_t		*last_ibp;
	xfs_dinode_t	*last_dip;
	short		bucket_index;
	short		offset;

	/*
	 * First pull the on-disk inode from the AGI unlinked list.
	 */
	mp = tp->t_mountp;
	agno = XFS_INO_TO_AGNO(mp, ip->i_ino);
	agdaddr = XFS_AG_DADDR(mp, agno, XFS_AGI_DADDR);

	/*
	 * Get the agi buffer first.  It ensures lock ordering
	 * on the list.
	 */
	agibp = xfs_trans_read_buf(tp, mp->m_dev, agdaddr, 1, 0);
	agi = XFS_BUF_TO_AGI(agibp);
	ASSERT(agi->agi_magicnum == XFS_AGI_MAGIC);

	/*
	 * Get the index into the agi hash table for the
	 * list this inode will go on.
	 */
	agino = XFS_INO_TO_AGINO(mp, ip->i_ino);
	bucket_index = agino % XFS_AGI_UNLINKED_BUCKETS;
	ASSERT(agi->agi_unlinked[bucket_index] != NULLAGINO);

	if (agi->agi_unlinked[bucket_index] == agino) {
		/*
		 * We're at the head of the list.  Get the inode's
		 * on-disk buffer to see if there is anyone after us
		 * on the list.  Only modify our next pointer if it
		 * is not already NULLAGINO.  This saves us the overhead
		 * of dealing with the buffer when there is no need to
		 * change it.
		 */
		ibp = xfs_itobp(mp, tp, ip, &dip);
		next_agino = dip->di_next_unlinked;
		if (next_agino != NULLAGINO) {
			dip->di_next_unlinked = NULLAGINO;
			offset = ((char *)dip - (char *)(ibp->b_un.b_addr)) +
				offsetof(xfs_dinode_t, di_next_unlinked);
			xfs_trans_log_buf(tp, ibp, offset,
					  (offset + sizeof(xfs_agino_t)));
		} else {
			xfs_trans_brelse(tp, ibp);
		}
		/*
		 * Point the bucket head pointer at the next inode.
		 */
		agi->agi_unlinked[bucket_index] = next_agino;
		offset = offsetof(xfs_agi_t, agi_unlinked) +
			(sizeof(xfs_agino_t) * bucket_index);
		xfs_trans_log_buf(tp, agibp, offset,
				  (offset + sizeof(xfs_agino_t)));
	} else {
		/*
		 * We need to search the list for the inode being freed.
		 */
		next_agino = agi->agi_unlinked[bucket_index];
		last_ibp = NULL;
		while (next_agino != agino) {
			/*
			 * If the last inode wasn't the one pointing to
			 * us, then release its buffer since we're not
			 * going to do anything with it.
			 */
			if (last_ibp != NULL) {
				xfs_trans_brelse(tp, last_ibp);
			}
			next_ino = XFS_AGINO_TO_INO(mp, agno, next_agino);
			last_ibp = xfs_inotobp(mp, tp, next_ino, &last_dip);
			next_agino = last_dip->di_next_unlinked;
			ASSERT(next_agino != NULLAGINO);
		}
		/*
		 * Now last_ibp points to the buffer previous to us on
		 * the unlinked list.  Pull us from the list.
		 */
		ibp = xfs_inotobp(mp, tp, ip->i_ino, &dip);
		next_agino = dip->di_next_unlinked;
		if (next_agino != NULLAGINO) {
			dip->di_next_unlinked = NULLAGINO;
			offset = ((char *)dip - (char *)(ibp->b_un.b_addr)) +
				offsetof(xfs_dinode_t, di_next_unlinked);
			xfs_trans_log_buf(tp, ibp, offset,
					  (offset + sizeof(xfs_agino_t)));
		} else {
			xfs_trans_brelse(tp, ibp);
		}
		/*
		 * Point the previous inode on the list to the next inode.
		 */
		last_dip->di_next_unlinked = next_agino;
		offset = ((char *)last_dip -
			  (char *)(last_ibp->b_un.b_addr)) +
			 offsetof(xfs_dinode_t, di_next_unlinked);
		xfs_trans_log_buf(tp, last_ibp, offset,
				  (offset + sizeof(xfs_agino_t)));
	}
}


/*
 * This is called to return an inode to the inode free list.
 * The inode should already be truncated to 0 length and have
 * no pages associated with it.  This routine also assumes that
 * the inode is already a part of the transaction.
 *
 * The on-disk copy of the inode will have been added to the list
 * of unlinked inodes in the AGI. We need to remove the inode from
 * that list atomically with respect to freeing it here.
 */
void
xfs_ifree(
	xfs_trans_t	*tp,
	xfs_inode_t	*ip)
{
	ASSERT(ismrlocked(&ip->i_lock, MR_UPDATE));
	ASSERT(ip->i_transp == tp);
	ASSERT(ip->i_d.di_nlink == 0);
	ASSERT(ip->i_d.di_nextents == 0);
	ASSERT((ip->i_d.di_size == 0) ||
	       ((ip->i_d.di_mode & IFMT) != IFREG));

	/*
	 * Pull the on-disk inode from the AGI unlinked list.
	 */
	xfs_iunlink_remove(tp, ip);

	xfs_difree(tp, ip->i_ino);
	ip->i_d.di_mode = 0;		/* mark incore inode as free */
	/*
	 * Bump the generation count so no one will be confused
	 * by reincarnations of this inode.
	 */
	ip->i_d.di_gen++;
	xfs_trans_log_inode(tp, ip, XFS_ILOG_CORE);
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
xfs_iroot_realloc(
	xfs_inode_t 	*ip,
	int 		rec_diff)
{
	int			cur_max;
	int			new_max;
	size_t			new_size;
	char			*np;
	char			*op;
	xfs_bmbt_block_t	*new_broot;

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
			ip->i_broot = (xfs_bmbt_block_t*)kmem_alloc(new_size,
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
		ip->i_broot = (xfs_bmbt_block_t *)
			      kmem_realloc(ip->i_broot, new_size, KM_SLEEP);
		op = (char *)XFS_BMAP_BROOT_PTR_ADDR(ip->i_broot, 1,
						      ip->i_broot_bytes);
		np = (char *)XFS_BMAP_BROOT_PTR_ADDR(ip->i_broot, 1,
						      new_size);
		ip->i_broot_bytes = new_size;
		ASSERT(ip->i_broot_bytes <= XFS_LITINO_BROOT(ip->i_mount));
		/*
		 * This depends on bcopy() handling overlapping buffers.
		 */
		bcopy(op, np, cur_max * (int)sizeof(xfs_dfsbno_t));
		return;
	}

	/*
	 * rec_diff is less than 0.  In this case, we are shrinking the
	 * i_broot buffer.  It must already exist.  If we go to zero
	 * records, just get rid of the root and clear the status bit.
	 */
	ASSERT((ip->i_broot != NULL) && (ip->i_broot_bytes > 0));
	cur_max = XFS_BMAP_BROOT_MAXRECS(ip->i_broot_bytes);
	new_max = cur_max + rec_diff;
	ASSERT(new_max >= 0);
	if (new_max > 0)
		new_size = (size_t)XFS_BMAP_BROOT_SPACE_CALC(new_max);
	else
		new_size = 0;
	if (new_size > 0) {
		new_broot = (xfs_bmbt_block_t *)kmem_alloc(new_size, KM_SLEEP);
		/*
		 * First copy over the btree block header.
		 */
		bcopy(ip->i_broot, new_broot, sizeof(xfs_bmbt_block_t));
	} else {
		new_broot = NULL;
		ip->i_flags &= ~XFS_IBROOT;
	}

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
		bcopy(op, np, new_max * (int)sizeof(xfs_dfsbno_t));
	}
	kmem_free(ip->i_broot, ip->i_broot_bytes);
	ip->i_broot = new_broot;
	ip->i_broot_bytes = new_size;
	ASSERT(ip->i_broot_bytes <= XFS_LITINO_BROOT(ip->i_mount));
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
xfs_iext_realloc(
	xfs_inode_t	*ip,
	int		ext_diff)
{
	int	byte_diff;
	int	new_size;
	uint	rnew_size;

	if (ext_diff == 0) {
		return;
	}

	byte_diff = ext_diff * (int)sizeof(xfs_bmbt_rec_t);
	new_size = (int)ip->i_bytes + byte_diff;
	ASSERT(new_size >= 0);

	if (new_size == 0) {
		if (ip->i_u1.iu_extents != ip->i_u2.iu_inline_ext) {
			ASSERT(ip->i_real_bytes != 0);
			kmem_free(ip->i_u1.iu_extents, ip->i_real_bytes);
		}
		ip->i_u1.iu_extents = NULL;
		rnew_size = 0;
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
				kmem_free(ip->i_u1.iu_extents,
					  ip->i_real_bytes);
			}
			ip->i_u1.iu_extents = ip->i_u2.iu_inline_ext;
		}
		rnew_size = 0;
	} else {
		rnew_size = new_size;
		if ((rnew_size & (rnew_size - 1)) != 0)
			rnew_size = xfs_iroundup(rnew_size);
		/*
		 * Stuck with malloc/realloc.
		 */
		if (ip->i_u1.iu_extents == ip->i_u2.iu_inline_ext) {
			ip->i_u1.iu_extents = (xfs_bmbt_rec_t *)
				kmem_alloc(rnew_size, KM_SLEEP);
			bcopy(ip->i_u2.iu_inline_ext, ip->i_u1.iu_extents,
			      sizeof(ip->i_u2.iu_inline_ext));
		} else if (rnew_size != ip->i_real_bytes) {
			ip->i_u1.iu_extents = (xfs_bmbt_rec_t *)
				kmem_realloc(ip->i_u1.iu_extents,
					     rnew_size, KM_SLEEP);
		}
	}
	ip->i_real_bytes = rnew_size;
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
xfs_idata_realloc(
	xfs_inode_t	*ip,
	int		byte_diff)
{
	int	new_size;
	int	real_size;

	if (byte_diff == 0) {
		return;
	}

	new_size = (int)ip->i_bytes + byte_diff;
	ASSERT(new_size >= 0);

	if (new_size == 0) {
		if (ip->i_u1.iu_data != ip->i_u2.iu_inline_data) {
			kmem_free(ip->i_u1.iu_data, ip->i_real_bytes);
		}
		ip->i_u1.iu_data = NULL;
		real_size = 0;
	} else if (new_size <= sizeof(ip->i_u2.iu_inline_data)) {
		/*
		 * If the valid extents/data can fit in iu_inline_ext/data,
		 * copy them from the malloc'd vector and free it.
		 */
		if (ip->i_u1.iu_data == NULL) {
			ip->i_u1.iu_data = ip->i_u2.iu_inline_data;
		} else if (ip->i_u1.iu_data != ip->i_u2.iu_inline_data) {
			ASSERT(ip->i_real_bytes != 0);
			bcopy(ip->i_u1.iu_data, ip->i_u2.iu_inline_data,
			      new_size);
			kmem_free(ip->i_u1.iu_data, ip->i_real_bytes);
			ip->i_u1.iu_data = ip->i_u2.iu_inline_data;
		}
		real_size = 0;
	} else {
		/*
		 * Stuck with malloc/realloc.
		 * For inline data, the underlying buffer must be
		 * a multiple of 4 bytes in size so that it can be
		 * logged and stay on word boundaries.  We enforce
		 * that here.
		 */
		real_size = (((new_size + 3) >> 2) << 2);
		if (ip->i_u1.iu_data == NULL) {
			ASSERT(ip->i_real_bytes == 0);
			ip->i_u1.iu_data = kmem_alloc(real_size, KM_SLEEP);
		} else if (ip->i_u1.iu_data != ip->i_u2.iu_inline_data) {
			/*
			 * Only do the realloc if the underlying size
			 * is really changing.
			 */
			if (ip->i_real_bytes != real_size) {
				ip->i_u1.iu_data =
					kmem_realloc(ip->i_u1.iu_data,
						     real_size, KM_SLEEP);
			}
		} else {
			ASSERT(ip->i_real_bytes == 0);
			ip->i_u1.iu_data = kmem_alloc(real_size, KM_SLEEP);
			bcopy(ip->i_u2.iu_inline_data, ip->i_u1.iu_data,
			      ip->i_bytes);
		}
	}
	ip->i_real_bytes = real_size;
	ip->i_bytes = new_size;
	ASSERT(ip->i_bytes <= XFS_LITINO(ip->i_mount));
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
xfs_imap(
	xfs_mount_t	*mp,
	xfs_trans_t	*tp,
	xfs_ino_t	ino,
	xfs_imap_t	*imap)
{
	xfs_fsblock_t fsbno;
	int off;

	xfs_dilocate(mp, tp, ino, &fsbno, &off);
	imap->im_blkno = XFS_FSB_TO_DADDR(mp, fsbno);
	imap->im_len = XFS_BTOD(mp, 1);
	imap->im_agblkno = XFS_FSB_TO_AGBNO(mp, fsbno);
	imap->im_ioffset = (ushort)off;
	imap->im_boffset = (ushort)(off << mp->m_sb.sb_inodelog);
	return 1;
}

/*
 * This is called free all the memory associated with an inode.
 * It must free the inode itself and any buffers allocated for
 * iu_extents/iu_data and i_broot.  It must also free the lock
 * associated with the inode.
 */
void
xfs_idestroy(
	xfs_inode_t	*ip)
{
	switch (ip->i_d.di_mode & IFMT) {
	case IFREG:
	case IFDIR:
	case IFLNK:
		if (ip->i_broot != NULL) {
			kmem_free(ip->i_broot, ip->i_broot_bytes);
			ip->i_broot = NULL;
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
				ASSERT(ip->i_real_bytes != 0);
				kmem_free(ip->i_u1.iu_data, ip->i_real_bytes);
				ip->i_u1.iu_data = NULL;
				ip->i_real_bytes = 0;
			}
		} else if ((ip->i_flags & XFS_IEXTENTS) &&
			   (ip->i_u1.iu_extents != NULL) &&
			   (ip->i_u1.iu_extents != ip->i_u2.iu_inline_ext)) {
			ASSERT(ip->i_real_bytes != 0);
			kmem_free(ip->i_u1.iu_extents, ip->i_real_bytes);
			ip->i_u1.iu_extents = NULL;
			ip->i_real_bytes = 0;
		}
		ASSERT(ip->i_u1.iu_extents == NULL ||
		       ip->i_u1.iu_extents == ip->i_u2.iu_inline_ext);
		ASSERT(ip->i_real_bytes == 0);
		break;
	}
	mrfree(&ip->i_lock);
	mrfree(&ip->i_iolock);
	mrfree(&ip->i_ticketlock);
	freesema(&ip->i_flock);
	freesema(&ip->i_pinsema);
#ifndef SIM
	ktrace_free(ip->i_btrace);
	ktrace_free(ip->i_xtrace);
#endif
	kmem_zone_free(xfs_inode_zone, ip);
}


/*
 * Increment the pin count of the given buffer.
 * This value is protected by ipinlock spinlock in the mount structure.
 */
void
xfs_ipin(
	xfs_inode_t	*ip)
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
xfs_iunpin(
	xfs_inode_t	*ip)
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
xfs_iunpin_wait(
	xfs_inode_t	*ip)
{
	int		s;
	xfs_mount_t	*mp;

	ASSERT(ismrlocked(&ip->i_lock, MR_UPDATE | MR_ACCESS));

	if (ip->i_pincount == 0) {
		return;
	}

	mp = ip->i_mount;
	/*
	 * Give the log a push so we don't wait here too long.
	 */
	xfs_log_force(mp, (xfs_lsn_t)0, XFS_LOG_FORCE);

	s = splockspl(mp->m_ipinlock, splhi);
	if (ip->i_pincount == 0) {
		spunlockspl(mp->m_ipinlock, s);
		return;
	}
	spunlockspl_psema(mp->m_ipinlock, s, &(ip->i_pinsema), PINOD);
	return;
}


#ifdef DEBUG
/*
 * Make sure that the extents in the given memory buffer
 * are valid.
 */
STATIC void
xfs_validate_extents(
	xfs_bmbt_rec_t	*ep,
	int		nrecs)
{
	xfs_bmbt_irec_t	irec;
	int		i;

	for (i = 0; i < nrecs; i++) {
		xfs_bmbt_get_all(ep, &irec);
		ep++;
	}
}
#endif /* DEBUG */

/*
 * xfs_iextents_copy()
 *
 * This is called to copy the REAL extents (as opposed to the delayed
 * allocation extents) from the inode into the given buffer.  It
 * returns the number of bytes copied into the buffer.
 *
 * If there are no delayed allocation extents, then we can just
 * bcopy() the extents into the buffer.  Otherwise, we need to
 * examine each extent in turn and skip those which are delayed.
 */
int
xfs_iextents_copy(
	xfs_inode_t	*ip,
	char	 	*buffer)
{
	xfs_fsblock_t	start_block;
	xfs_bmbt_rec_t	*ep;
	xfs_bmbt_rec_t	*dest_ep;
	xfs_mount_t	*mp;
	int		nrecs;
	int		i;
	int		copied;

	ASSERT(ismrlocked(&ip->i_lock, MR_UPDATE|MR_ACCESS));
	ASSERT(ip->i_bytes > 0);

	mp = ip->i_mount;
	nrecs = ip->i_bytes / sizeof(xfs_bmbt_rec_t);
	xfs_bmap_trace_exlist("xfs_iflush", ip, nrecs);
	ASSERT(nrecs > 0);
	if (nrecs == ip->i_d.di_nextents) {
		/*
		 * There are no delayed allocation extents,
		 * so just copy everything.
		 */
		ASSERT(ip->i_bytes <= XFS_LITINO(mp));
		ASSERT(ip->i_bytes ==
		       (ip->i_d.di_nextents * sizeof(xfs_bmbt_rec_t)));
		bcopy(ip->i_u1.iu_extents, buffer, ip->i_bytes);
		xfs_validate_extents((xfs_bmbt_rec_t*)buffer, nrecs);
		return ip->i_bytes;
	}

	/*
	 * There are some delayed allocation extents in the
	 * inode, so copy the extents one at a time and skip
	 * the delayed ones.  There must be at least one
	 * non-delayed extent.
	 */
	ASSERT(nrecs > ip->i_d.di_nextents);
	ep = (xfs_bmbt_rec_t *)(ip->i_u1.iu_extents);
	dest_ep = (xfs_bmbt_rec_t *)buffer;
	copied = 0;
	for (i = 0; i < nrecs; i++) {
		start_block = xfs_bmbt_get_startblock(ep);
		if (ISNULLSTARTBLOCK(start_block)) {
			/*
			 * It's a delayed allocation extent, so skip it.
			 */
			ep++;
			continue;
		}

		*dest_ep = *ep;
		dest_ep++;
		ep++;
		copied++;
	}
	ASSERT(copied != 0);
	ASSERT(copied == ip->i_d.di_nextents);
	ASSERT((copied * sizeof(xfs_bmbt_rec_t)) <= XFS_LITINO(mp));
	xfs_validate_extents((xfs_bmbt_rec_t*)buffer, copied);

	return (copied * sizeof(xfs_bmbt_rec_t));
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
xfs_iflush(
	xfs_inode_t	*ip,
	uint		flags)
{
	xfs_inode_log_item_t	*iip;
	buf_t			*bp;
	xfs_dinode_t		*dip;
	xfs_mount_t		*mp;
	int			s;

	ASSERT(ismrlocked(&ip->i_lock, MR_UPDATE|MR_ACCESS));
	ASSERT(valusema(&ip->i_flock) <= 0);

	iip = &ip->i_item;
	mp = ip->i_mount;

	/*
	 * If the inode isn't dirty, then just release the inode
	 * flush lock and do nothing.
	 */
	if ((ip->i_update_core == 0) &&
	    !(iip->ili_format.ilf_fields & XFS_ILOG_ALL)) {
		xfs_ifunlock(ip);
		return;
	}

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
	bp = xfs_itobp(mp, NULL, ip, &dip);

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
		if ((iip->ili_format.ilf_fields & XFS_ILOG_DATA) &&
		    (ip->i_bytes > 0)) {
			ASSERT(ip->i_u1.iu_data != NULL);
			ASSERT(ip->i_bytes <= XFS_LITINO(mp));
			bcopy(ip->i_u1.iu_data, dip->di_u.di_c, ip->i_bytes);
		}
		break;
	case XFS_DINODE_FMT_EXTENTS:
		ASSERT((ip->i_flags & XFS_IEXTENTS) ||
		       !(iip->ili_format.ilf_fields & XFS_ILOG_EXT));
		ASSERT((ip->i_u1.iu_extents != NULL) || (ip->i_bytes == 0));
		ASSERT((ip->i_u1.iu_extents == NULL) || (ip->i_bytes > 0));
		if ((iip->ili_format.ilf_fields & XFS_ILOG_EXT) &&
		    (ip->i_bytes > 0)) {
			ASSERT(ip->i_d.di_nextents > 0);
			(void) xfs_iextents_copy(ip,
						 (char *)dip->di_u.di_bmx);
		}
		break;
	case XFS_DINODE_FMT_BTREE:
		if ((iip->ili_format.ilf_fields & XFS_ILOG_BROOT) &&
		    (ip->i_broot_bytes > 0)) {
			ASSERT(ip->i_broot != NULL);
			ASSERT(ip->i_broot_bytes <= XFS_LITINO_BROOT(mp));
			xfs_bmbt_to_bmdr(ip->i_broot, ip->i_broot_bytes,
			   &(dip->di_u.di_bmbt),
			   XFS_BMAP_BROOT_SIZE(mp->m_sb.sb_inodesize));
		}
		break;

	case XFS_DINODE_FMT_DEV:
		if (iip->ili_format.ilf_fields & XFS_ILOG_DEV) {
			dip->di_u.di_dev = ip->i_u2.iu_rdev;
		}
		break;
		
	case XFS_DINODE_FMT_UUID:
		if (iip->ili_format.ilf_fields & XFS_ILOG_UUID) {
			dip->di_u.di_muuid = ip->i_u2.iu_uuid;
		}
		break;

	default:
		ASSERT(0);
		break;
	}
	
	/*
	 * We've recorded everything logged in the inode, so we'd
	 * like to clear the ilf_fields bits so we don't log and
	 * flush things unnecessarily.  However, we can't stop
	 * logging all this information until the data we've copied
	 * into the disk buffer is written to disk.  If we did we might
	 * overwrite the copy of the inode in the log with all the
	 * data after re-logging only part of it, and in the face of
	 * a crash we wouldn't have all the data we need to recover.
	 *
	 * What we do is move the bits to the ili_last_fields field.
	 * When logging the inode, these bits are moved back to the
	 * ilf_fields field.  In the xfs_iflush_done() routine we
	 * clear ili_last_fields, since we know that the information
	 * those bits represent is permanently on disk.  As long as
	 * the flush completes before the inode is logged again, then
	 * both ilf_fields and ili_last_fields will be cleared.
	 *
	 * We can play with the ilf_fields bits here, because the inode
	 * lock must be held exclusively in order to set bits there
	 * and the flush lock protects the ili_last_fields bits.
	 * Set ili_logged so the flush done
	 * routine can tell whether or not to look in the AIL.
	 * Also, store the current LSN of the inode so that we can tell
	 * whether the item has moved in the AIL from xfs_iflush_done().
	 * In order to read the lsn we need the AIL lock, because
	 * it is a 64 bit value that cannot be read atomically.
	 */
	if (iip->ili_format.ilf_fields != 0) {
		iip->ili_last_fields = iip->ili_format.ilf_fields;
		iip->ili_format.ilf_fields = 0;
		iip->ili_logged = 1;

		ASSERT(sizeof(xfs_lsn_t) == 8);	/* don't lock if it shrinks */
		s = AIL_LOCK(mp);
		iip->ili_flush_lsn = iip->ili_item.li_lsn;
		AIL_UNLOCK(mp, s);
	}

	/*
	 * Attach the function xfs_iflush_done to the inode's
	 * buffer.  This will remove the inode from the AIL
	 * and unlock the inode's flush lock when the inode is
	 * completely written to disk.
	 */
	xfs_buf_attach_iodone(bp, (void(*)(buf_t*,xfs_log_item_t*))
			      xfs_iflush_done, (xfs_log_item_t *)iip);

	/*
	 * If the buffer is pinned then push on the log so we won't
	 * get stuck waiting in the write for too long.
	 */
	if (bp->b_pincount > 0) {
		xfs_log_force(mp, (xfs_lsn_t)0, XFS_LOG_FORCE);
	}

	if (flags & B_DELWRI) {
		xfs_bdwrite(bp);
	} else if (flags & B_ASYNC) {
		xfs_bawrite(bp);
	} else {
		xfs_bwrite(bp);
	}

	return;
}

/*
 * Flush all inactive inodes in mp.  Return true if no user references
 * were found, false otherwise.
 */
int
xfs_iflush_all(
	xfs_mount_t	*mp,
	int		flag)
{
	int		busy;
	int		done;
	xfs_inode_t	*ip;
	vmap_t		vmap;
	vnode_t		*vp;

	busy = done = 0;
	while (!done) {
		XFS_MOUNT_ILOCK(mp);
		for (ip = mp->m_inodes; ip; ip = ip->i_mnext) {
			/*
			 * It's up to our caller to purge the root (and
			 * when implemented, quotas) vnodes later.
			 */
			vp = XFS_ITOV(ip);
			if (vp->v_count != 0) {
				if (vp->v_count == 1 && ip == mp->m_rootip)
					continue;
				if (!(flag & XFS_FLUSH_ALL)) {
					busy = 1;
					done = 1;
					break;
				}
				/*
				 * Ignore busy inodes but continue flushing
				 * others.
				 */
				continue;
			}
			/*
			 * Sample vp mapping while holding mp locked on MP
			 * systems, so we don't purge a reclaimed or
			 * nonexistent vnode.
			 */
			VMAP(vp, vmap);
			XFS_MOUNT_IUNLOCK(mp);
			vn_purge(vp, &vmap);
			break;
		}
		if (!ip)
			done = 1;
	}
	XFS_MOUNT_IUNLOCK(mp);
	return !busy;
}

#ifdef SIM
void
xfs_iprint(
	xfs_inode_t	*ip)
{
	xfs_dinode_core_t *dip;
	xfs_bmbt_rec_t *ep;
	xfs_extnum_t i;
	xfs_extnum_t nextents;
	xfs_mount_t *mp;

	printf("Inode %x\n", ip);
	printf("    i_dev %x\n", (uint)ip->i_dev);
	printf("    i_ino %llx\n", ip->i_ino);

	printf("    i_flags %x ", ip->i_flags);
	if (ip->i_flags & XFS_IEXTENTS) {
		printf("EXTENTS ");
	}
	printf("\n");

	printf("    i_vcode %x\n", ip->i_vcode);
	printf("    i_mapcnt %x\n", ip->i_mapcnt);
	printf("    i_bytes %d\n", ip->i_bytes);
	printf("    i_u1.iu_extents/iu_data %x\n", ip->i_u1.iu_extents);
	if (ip->i_flags & XFS_IEXTENTS) {
		nextents = ip->i_bytes / sizeof(*ep);
		mp = ip->i_mount;
		for (ep = ip->i_u1.iu_extents, i = 0; i < nextents; i++, ep++) {
			xfs_bmbt_irec_t rec;

			xfs_bmbt_get_all(ep, &rec);
			printf("\t%d: startoff %lld, startblock 0x%llx, blockcount %d\n",
				i, (xfs_dfiloff_t)rec.br_startoff,
				(xfs_dfsbno_t)rec.br_startblock,
				rec.br_blockcount);
		}
	}
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
	printf("   di_nextents %d\n", dip->di_nextents);
	printf("   di_size %lld\n", dip->di_size);
	printf("   di_gen %x\n", dip->di_gen);
	printf("   di_extsize %d\n", dip->di_extsize);
	printf("   di_flags %x\n", dip->di_flags);
	printf("   di_nblocks %lld\n", dip->di_nblocks);
}
#endif	/* SIM */

/*
 * xfs_iaccess: check accessibility of inode/cred for mode.
 */
int
xfs_iaccess(
	xfs_inode_t	*ip,
	mode_t		mode,
	cred_t		*cr)
{
#if 0
	/*
	 * For TIRIX: Verify that the label allows access.
	 */
	if (_MAC_XFS_IACCESS(ip, cr, mode))
		return XFS_ERROR(EACCESS);
#endif
	
	if ((mode & IWRITE) && !WRITEALLOWED(XFS_ITOV(ip), cr))
		return XFS_ERROR(EROFS);
	if (cr->cr_uid == 0)
		return 0;
	if (cr->cr_uid != ip->i_d.di_uid) {
		mode >>= 3;
		if (!groupmember(ip->i_d.di_gid, cr))
			mode >>= 3;
	}
	if ((ip->i_d.di_mode & mode) != mode)
		return XFS_ERROR(EACCES);
	return 0;
}

/*
 * xfs_iroundup: round up argument to next power of two
 */
uint
xfs_iroundup(
	uint	v)
{
	int i;
	uint m;

	if ((v & (v - 1)) == 0)
		return v;
	ASSERT((v & 0x80000000) == 0);
	if ((v & (v + 1)) == 0)
		return v + 1;
	for (i = 0, m = 1; i < 31; i++, m <<= 1) {
		if (v & m)
			continue;
		v |= m;
		if ((v & (v + 1)) == 0)
			return v + 1;
	}
	ASSERT(0);
}
