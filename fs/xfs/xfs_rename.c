#ident "$Revision: 1.2 $"

#include <sys/types.h>
#include <sys/uuid.h>
#include <sys/vfs.h>
#include <sys/vnode.h>
#include <sys/systm.h>
#include <sys/dnlc.h>
#include <sys/param.h>
#include <sys/pathname.h>
#include <sys/cred.h>
#include <sys/errno.h>
#include <sys/dmi.h>
#include <sys/dmi_kern.h>

#include "xfs_macros.h"
#include "xfs_types.h"
#include "xfs_inum.h"
#include "xfs_log.h"
#include "xfs_trans.h"
#include "xfs_sb.h"
#include "xfs_mount.h"
#include "xfs_bmap_btree.h"
#include "xfs_bmap.h"
#include "xfs_attr_sf.h"
#include "xfs_dir_sf.h"
#include "xfs_dinode.h"
#include "xfs_inode_item.h"
#include "xfs_inode.h"
#include "xfs_error.h"
#include "xfs_quota.h"
#include "xfs_dir.h"
#include "xfs_rw.h"
#include "xfs_utils.h"
#include "xfs_trans_space.h"
#include "xfs_da_btree.h"
#include "xfs_dir_leaf.h"

#ifndef SIM
mutex_t	xfs_ancestormon;		/* initialized in xfs_init */
#endif



/*
 * Given an array of up to 4 inode pointers, unlock the pointed to inodes.
 * If there are fewer than 4 entries in the array, the empty entries will
 * be at the end and will have NULL pointers in them.
 */
STATIC void
xfs_rename_unlock4(
	xfs_inode_t	**i_tab)
{
	int	i;

	xfs_iunlock(i_tab[0], XFS_ILOCK_EXCL);
	for (i = 1; i < 4; i++) {
		if (i_tab[i] == NULL) {
			break;
		}
		/*
		 * Watch out for duplicate entries in the table.
		 */
		if (i_tab[i] != i_tab[i-1]) {
			xfs_iunlock(i_tab[i], XFS_ILOCK_EXCL);
		}
	}
}

/*
 * Given an array of up to 4 inode pointers, lock the pointed to inodes.
 * If there are fewer than 4 entries in the array, the empty entries will
 * be at the end and will have NULL pointers in them.  The array of inodes
 * is already sorted by inode number, so we've already taken care of lock
 * ordering.
 */
STATIC void
xfs_rename_relock4(
	xfs_inode_t	**i_tab)
{
	int	i;

	xfs_ilock(i_tab[0], XFS_ILOCK_EXCL);
	for (i = 1; i < 4; i++) {
		if (i_tab[i] == NULL) {
			break;
		}
		/*
		 * Watch out for duplicate entries in the table.
		 */
		if (i_tab[i] != i_tab[i-1]) {
			xfs_ilock(i_tab[i], XFS_ILOCK_EXCL);
		}
	}
}

/*
 * Compare the i_gen fields of the inodes pointed to in the array of
 * inode pointers to the gen values stored at the same offset in the
 * array of generation counts.  Return 0 if they are the same and 1
 * if they are different.
 *
 * There are a maximum of 4 entries in the array.  If there are
 * fewer than that, the first empty entry will have a NULL pointer.
 */
STATIC int
xfs_rename_compare_gencounts(
	xfs_inode_t	**i_tab,
	int		*i_gencounts)
{
	int	i;
	int	compare;

	compare = 0;
	for (i = 0; i < 4; i++) {
		if (i_tab[i] == NULL) {
			break;
		}
		if (i_tab[i]->i_gen != i_gencounts[i]) {
			compare = 1;
			break;
		}
	}
	return compare;
}

/*
 * The following routine will acquire the locks required for a rename
 * operation. The code understands the semantics of renames and will
 * validate that name1 exists under dp1 & that name2 may or may not
 * exist under dp2.
 *
 * We are renaming dp1/name1 to dp2/name2.
 *
 * Return ENOENT if dp1 does not exist, other lookup errors, or 0 for success.
 * Return EAGAIN if the caller needs to try again.
 */
STATIC int
xfs_lock_for_rename(
	xfs_inode_t	*dp1,	/* old (source) directory inode */
	xfs_inode_t	*dp2,	/* new (target) directory inode */
	char		*name1,	/* old entry name */
	char		*name2,	/* new entry name */
	xfs_inode_t	**ipp1,	/* inode of old entry */
	xfs_inode_t	**ipp2,	/* inode of new entry, if it 
		           	   already exists, NULL otherwise. */
	xfs_inode_t	**i_tab,/* array of inode returned, sorted */
	int		*i_gencounts) /* array of inode gen counts */
{
	xfs_inode_t		*ip1, *ip2, *temp;
	xfs_ino_t		inum1, inum2;
	unsigned long		dir_gen1, dir_gen2;
	int			error;
	int			num_inodes;
	int			i, j;
	uint			lock_mode;
	uint			dir_unlocked;
	uint			lookup_flags;
	struct ncfastdata	fastdata;

	ip2 = NULL;

	/*
	 * First, find out the current inums of the entries so that we
	 * can determine the initial locking order.  We'll have to 
	 * sanity check stuff after all the locks have been acquired
	 * to see if we still have the right inodes, directories, etc.
	 */
        lock_mode = xfs_ilock_map_shared(dp1);

	/*
	 * We don't want to do lookups in unlinked directories.
	 */
	if (dp1->i_d.di_nlink == 0) {
		xfs_iunlock_map_shared(dp1, lock_mode);
		return XFS_ERROR(ENOENT);
	}

	lookup_flags = DLF_IGET;
	if (lock_mode == XFS_ILOCK_SHARED) {
		lookup_flags |= DLF_LOCK_SHARED;
	}
        error = xfs_dir_lookup_int(NULL, XFS_ITOBHV(dp1), lookup_flags,
				   name1, NULL, &inum1, &ip1,
				   &fastdata, &dir_unlocked);

	/*
	 * Save the current generation so that we can detect if it's
	 * modified between when we drop the lock & reacquire it down
	 * below.  We only need to do this for the src directory since
	 * the target entry does not need to exist yet. 
	 */
	dir_gen1 = dp1->i_gen;
	xfs_iunlock_map_shared(dp1, lock_mode);
        if (error) {
                return error;
	}
	ASSERT (ip1);
	ITRACE(ip1);

        lock_mode = xfs_ilock_map_shared(dp2);

	/*
	 * We don't want to do lookups in unlinked directories.
	 */
	if (dp2->i_d.di_nlink == 0) {
		xfs_iunlock_map_shared(dp2, lock_mode);
		return XFS_ERROR(ENOENT);
	}

	lookup_flags = DLF_IGET;
	if (lock_mode == XFS_ILOCK_SHARED) {
		lookup_flags |= DLF_LOCK_SHARED;
	}
        error = xfs_dir_lookup_int(NULL, XFS_ITOBHV(dp2), lookup_flags,
				   name2, NULL, &inum2, &ip2, &fastdata,
				   &dir_unlocked);
	dir_gen2 = dp2->i_gen;
        xfs_iunlock_map_shared(dp2, lock_mode);
	if (error == ENOENT) {		/* target does not need to exist. */
		inum2 = 0;
	} else if (error) {
		IRELE (ip1);
                return error;
	} else {
		ITRACE(ip2);
	}

	/*
	 * i_tab contains a list of pointers to inodes.  We initialize
	 * the table here & we'll sort it.  We will then use it to 
	 * order the acquisition of the inode locks.
	 *
	 * Note that the table may contain duplicates.  e.g., dp1 == dp2.
	 */
        i_tab[0] = dp1;
        i_tab[1] = dp2;
        i_tab[2] = ip1;
	if (inum2 == 0) {
		num_inodes = 3;
		i_tab[3] = NULL;
	} else {
		num_inodes = 4;
        	i_tab[3] = ip2;
	}

	/*
	 * Sort the elements via bubble sort.  (Remember, there are at
	 * most 4 elements to sort, so this is adequate.)
	 */
	for (i=0; i < num_inodes; i++) {
		for (j=1; j < num_inodes; j++) {
			if (i_tab[j]->i_ino < i_tab[j-1]->i_ino) {
				temp = i_tab[j];
				i_tab[j] = i_tab[j-1];
				i_tab[j-1] = temp;
			}
		}
	}

	/*
	 * Lock all the inodes in exclusive mode. If an inode appears
	 * twice in the list, it will only be locked once.
	 */
	xfs_ilock (i_tab[0], XFS_ILOCK_EXCL);
	for (i=1; i < num_inodes; i++) {
		if (i_tab[i] != i_tab[i-1])
			xfs_ilock(i_tab[i], XFS_ILOCK_EXCL);
	}

	/*
	 * See if either of the directories was modified during the
	 * interval between when the locks were released and when
	 * they were reacquired.
	 */
	if (dp1->i_gen != dir_gen1 || dp2->i_gen != dir_gen2) {
		/*
		 * Someone else may have linked in a new inode
		 * with the same name.  If so, we'll need to
		 * release our locks & go through the whole
		 * thing again.
		 */
		xfs_iunlock(i_tab[0], XFS_ILOCK_EXCL);
		for (i=1; i < num_inodes; i++) {
			if (i_tab[i] != i_tab[i-1])
				xfs_iunlock(i_tab[i], XFS_ILOCK_EXCL);
		}
		if (num_inodes == 4) {
			IRELE (ip2);
		}
		IRELE (ip1);
		return XFS_ERROR(EAGAIN);
        }


	/*
	 * Set the return value.  Return the gen counts of the inodes in
	 * i_tab in i_gencounts.  Null out any unused entries in i_tab.
	 */
	*ipp1 = *ipp2 = NULL;
	for (i=0; i < num_inodes; i++) {
		if (i_tab[i]->i_ino == inum1) {
			*ipp1 = i_tab[i];
		}
		if (i_tab[i]->i_ino == inum2) {
			*ipp2 = i_tab[i];
		}
		i_gencounts[i] = i_tab[i]->i_gen;
	}
	for (;i < 4; i++) {
		i_tab[i] = NULL;
	}
	return 0;
}

/*
 * xfs_rename_ancestor_check.
 *
 * Routine called by xfs_rename to make sure that we are not moving
 * a directory under one of its children. This would have the effect
 * of orphaning the whole directory subtree.
 *
 * If two calls to xfs_rename_ancestor_check overlapped execution, the 
 * partially completed work of one call could be invalidated by the
 * rename that activated the other.  To avoid this, we serialize
 * using xfs_ancestormon.
 *
 * The caller, xfs_rename(), must have already acquired the
 * xfs_ancestormon mutex and unlocked the inodes.  It is up
 * to xfs_rename() to re-validate its inodes after this call
 * returns since they have been unlocked.
 */
/*ARGSUSED*/
STATIC int
xfs_rename_ancestor_check(
	xfs_inode_t *src_dp, 
	xfs_inode_t *src_ip,
	xfs_inode_t *target_dp,
	xfs_inode_t *target_ip)
{
	xfs_mount_t		*mp;
	xfs_inode_t		*ip;
	xfs_ino_t		parent_ino;
	xfs_ino_t		root_ino;
	int			error = 0;
	struct ncfastdata	fastdata;

	mp = src_dp->i_mount;
	root_ino = mp->m_sb.sb_rootino;

	/*
	 * We know that all the inodes other than target_ip are directories
	 * at this point.  We can treat target_ip like a directory whether
	 * it is or not, though, since if it is not i_gen won't change.
	 */
	ASSERT((src_dp->i_d.di_mode & IFMT) == IFDIR);
	ASSERT((target_dp->i_d.di_mode & IFMT) == IFDIR);
	ASSERT((src_ip == NULL) || ((src_ip->i_d.di_mode & IFMT) == IFDIR));

	/*
	 * Assert that all the relationships that were checked by our
	 * caller are true!
	 */
	ASSERT(src_ip != src_dp);
	ASSERT(target_ip != target_dp);
	ASSERT(src_ip != target_ip);
	ASSERT(src_dp != target_dp);

	/*
	 * Ascend the target_dp's ancestor line, stopping if we
	 * either encounter src_ip (failed check), or reached the
	 * root of the filesystem.
	 * If we discover an anomaly, e.g., ".." missing, return
	 * ENOENT.
	 *
	 * In this loop we need to lock the inodes exclusively since
	 * we can't really get at the xfs_ilock_map_shared() interface
	 * through xfs_dir_lookup_int() and xfs_iget().
	 */
	ip = target_dp;
	xfs_ilock(ip, XFS_ILOCK_EXCL);

	while (ip->i_ino != root_ino) {

		if (ip == src_ip) {
			error = XFS_ERROR(EINVAL);
			break;
		}
		if (ip->i_d.di_nlink == 0) {
			/*
			 * The directory has been removed, don't do
			 * any lookups in it.
			 */
			error = XFS_ERROR(ENOENT);
			break;
		}
		error = xfs_dir_lookup_int(NULL, XFS_ITOBHV(ip), 0, "..",
				NULL, &parent_ino, NULL, &fastdata, NULL);
		if (error) {
			break;
		}
		if (parent_ino == ip->i_ino) {
			prdev("Directory inode %lld has bad parent link",
                              ip->i_dev, ip->i_ino);
                        error = XFS_ERROR(ENOENT);
                        break;
		}

		/*
		 * Now release ip and get its parent inode.
		 * If we're on the first pass through this loop and
		 * ip == target_dp, then we do not want to release
		 * the vnode reference.
		 */
		xfs_iunlock(ip, XFS_ILOCK_EXCL);
		if (ip != target_dp)
			IRELE(ip);

		error = xfs_iget(mp, NULL, parent_ino, XFS_ILOCK_EXCL, &ip, 0);
		if (error) {
			goto error_return;
		}
		ASSERT(ip != NULL);
		if (((ip->i_d.di_mode & IFMT) != IFDIR) ||
		    (ip->i_d.di_nlink == 0)) {
                        prdev("Ancestor inode %d is not a directory",
			      ip->i_dev, ip->i_ino);
                        error = XFS_ERROR(ENOTDIR);
                        break;
                }
	}

	/*
	 * Release the lock on the inode, taking care to decrement the 
	 * vnode reference count only if ip != target_dp.
	 */
	xfs_iunlock(ip, XFS_ILOCK_EXCL);
	if (ip != target_dp) {
		IRELE(ip);
	}

 error_return:
	return error;
}

int rename_which_error_return = 0;

/*
 * Do all the mundane error checking for xfs_rename().  The code
 * assumes that all of the non-NULL inodes have already been locked.
 */
STATIC int
xfs_rename_error_checks(
	xfs_inode_t	*src_dp,
	xfs_inode_t	*target_dp,
	xfs_inode_t	*src_ip,
	xfs_inode_t	*target_ip,
	char		*src_name,
	char		*target_name,			
	cred_t		*credp,
	int		*status)			
{
	int	src_is_directory;
	int	error;

	*status = 0;
	error = 0;
	/*
	 * If the target directory has been removed, we can't create any
	 * more files in it.  We don't need to check the source dir,
	 * because it was checked in xfs_lock_for_rename() while looking
	 * for the source inode.  If it had been removed the source
	 * dir's gen count would have been bumped removing the last entry
	 * and then we'd have noticed that its link count had gone to zero.
	 */
	if (target_dp->i_d.di_nlink == 0) {
		error = XFS_ERROR(ENOENT);
		*status = __LINE__;
		goto error_return;
	}
	if (error = xfs_iaccess(src_dp, IEXEC | IWRITE, credp)) {
		*status = __LINE__;
                goto error_return;
	}
	if (error = xfs_stickytest(src_dp, src_ip, credp)) {
		*status = __LINE__;
		goto error_return;
	}

	if (target_dp && (src_dp != target_dp)) {
		if (error = xfs_iaccess(target_dp, IEXEC | IWRITE, credp)) {
			*status = __LINE__;
			goto error_return;
		}
		if ((target_ip != NULL) &&
		    (error = xfs_stickytest(target_dp, target_ip, credp))) {
			*status = __LINE__;
			goto error_return;
		}
	} else {
		if ((target_ip != NULL) &&
		    (error = xfs_stickytest(src_dp, target_ip, credp))) {
			*status = __LINE__;
                        goto error_return;
		}
	}

	if ((src_ip == src_dp) || (target_ip == target_dp)) {
		error = XFS_ERROR(EINVAL);
		*status = __LINE__;
		goto error_return;
	}

	/*
	 * Source and target are identical.
	 */
	if (src_ip == target_ip) {
		/*
		 * There is no error in this case, but we want to get
		 * out anyway.  Set error to 0 so that no error will
		 * be returned, but set *status so that the caller
		 * will know that it should give up on the rename.
		 */
		error = 0;
		*status = __LINE__;
		goto error_return;
	}

	/*
	 * Directory renames require special checks.
	 */
	src_is_directory = ((src_ip->i_d.di_mode & IFMT) == IFDIR);

	if (src_is_directory) {

		ASSERT(src_ip->i_d.di_nlink >= 2);

		/*
		 * Check for link count overflow on target_dp
		 */
		if (target_ip == NULL && (src_dp != target_dp) &&
		    target_dp->i_d.di_nlink >= XFS_MAXLINK) {
			error = XFS_ERROR(EMLINK);
			*status = __LINE__;
			goto error_return;
		}
		    
		/*
		 * Cannot rename ".."
		 */
		if ((src_name[0] == '.') && (src_name[1] == '.') &&
		    (src_name[2] == '\0')) {
			error = XFS_ERROR(EINVAL);
			*status = __LINE__;
			goto error_return;
		}
                if ((target_name[0] == '.') && (target_name[1] == '.') &&
                    (target_name[2] == '\0')) {
                        error = XFS_ERROR(EINVAL);
			*status = __LINE__;
                        goto error_return;
                }

	}

 error_return:
	return error;
}

/*
 * Perform error checking on the target inode after the ancestor check
 * has been done in xfs_rename().
 */
STATIC int
xfs_rename_target_checks(
	xfs_inode_t	*target_ip,
	int		src_is_directory)
{
	int	error;

	error = 0;
	/*
	 * If target exists and it's a directory, check that both
	 * target and source are directories and that target can be
	 * destroyed, or that neither is a directory.
	 */
	if ((target_ip->i_d.di_mode & IFMT) == IFDIR) {

		/*
		 * Make sure src is a directory.
		 */
		if (!src_is_directory) {
			error = XFS_ERROR(EISDIR);
			rename_which_error_return = __LINE__;
			goto error_return;
		}

		/*
		 * Make sure target dir is empty.
		 */
		if (!(xfs_dir_isempty(target_ip)) || 
		    (target_ip->i_d.di_nlink > 2)) {
			error = XFS_ERROR(EEXIST);
			rename_which_error_return = __LINE__;
			goto error_return;
		}

		if (XFS_ITOV(target_ip)->v_vfsmountedhere) {
			error = XFS_ERROR(EBUSY);
			rename_which_error_return = __LINE__;
			goto error_return;
		}

	} else {
		if (src_is_directory) {
			error = XFS_ERROR(ENOTDIR);
			rename_which_error_return = __LINE__;
			goto error_return;
		}
	}

 error_return:
	return error;
}


/*
 * xfs_rename
 */
int
xfs_rename(
	bhv_desc_t	*src_dir_bdp,
	char		*src_name,
	vnode_t		*target_dir_vp,
	char		*target_name,
	pathname_t	*target_pnp,
	cred_t		*credp)
{
	xfs_trans_t	*tp;
	xfs_inode_t	*src_dp, *target_dp, *src_ip, *target_ip;
	xfs_mount_t	*mp;
	int		new_parent;		/* moving to a new dir */
	int		src_is_directory;	/* src_name is a directory */
	int		error;		
        xfs_bmap_free_t free_list;
        xfs_fsblock_t   first_block;
	int		cancel_flags;
	int		committed;
	int		status;
	int		ancestor_checked;
	xfs_inode_t	*inodes[4];
	int		gencounts[4];
	int		target_ip_dropped = 0;	/* dropped target_ip link? */
	int		src_dp_dropped = 0;	/* dropped src_dp link? */
	vnode_t 	*src_dir_vp;
	bhv_desc_t	*target_dir_bdp;

	src_dir_vp = BHV_TO_VNODE(src_dir_bdp);
	vn_trace_entry(src_dir_vp, "xfs_rename", (inst_t *)__return_address);
	vn_trace_entry(target_dir_vp, "xfs_rename", (inst_t *)__return_address);

	/*
	 * Find the XFS behavior descriptor for the target directory
	 * vnode since it was not handed to us.
	 */
	target_dir_bdp = vn_bhv_lookup_unlocked(VN_BHV_HEAD(target_dir_vp), 
						&xfs_vnodeops);
	if (target_dir_bdp == NULL) {
		return XFS_ERROR(EXDEV);
	}
	if (DM_EVENT_ENABLED(src_dir_vp->v_vfsp, XFS_BHVTOI(src_dir_bdp),
			     DM_RENAME) ||
	    DM_EVENT_ENABLED(target_dir_vp->v_vfsp,
			     XFS_BHVTOI(target_dir_bdp), DM_RENAME)) {
		error = dm_namesp_event(DM_RENAME, src_dir_vp, target_dir_vp,
					 src_name, target_name, 0, 0);
		if (error) {
			return error;
		}
	}

 start_over:
	/*
	 * Lock all the participating inodes. Depending upon whether
	 * the target_name exists in the target directory, and
	 * whether the target directory is the same as the source
	 * directory, we can lock from 2 to 4 inodes.
	 * xfs_lock_for_rename() will return ENOENT if src_name
	 * does not exist in the source directory.
	 */
	tp = NULL;
	src_dp = XFS_BHVTOI(src_dir_bdp);
        target_dp = XFS_BHVTOI(target_dir_bdp);
	do {
		error = xfs_lock_for_rename(src_dp, target_dp, src_name,
				target_name, &src_ip, &target_ip, inodes,
				gencounts);
	} while (error == EAGAIN);
	if (error) {
		rename_which_error_return = __LINE__;
		/*
		 * We have nothing locked, no inode references, and
		 * no transaction, so just get out.
		 */
		return error;
	}

	ASSERT(src_ip != NULL);

	error = xfs_rename_error_checks(src_dp, target_dp, src_ip,
			target_ip, src_name, target_name, credp, &status);
	if (error || status) {
		rename_which_error_return = status;
		goto unlock_rele_return;
	}

	new_parent = (src_dp != target_dp);
	src_is_directory = ((src_ip->i_d.di_mode & IFMT) == IFDIR);

	/*
	 * Drop the locks on our inodes so that we can do the ancestor
	 * check if necessary and start the transaction.  We have already
	 * saved the i_gen fields of each of the inode in the gencounts
	 * array when we called xfs_lock_for_rename().
	 */
	xfs_rename_unlock4(inodes);
	if (src_is_directory && (src_dp != target_dp)) {
		/*
		 * Check whether the rename would orphan the tree
		 * rooted at src_ip by moving it under itself.
		 *
		 * We use the xfs_ancestormon mutex to serialize all
		 * renames that might be doing this.  This prevents
		 * simultaneous renames from tricking each other into
		 * thinking that things are OK when they are not.
		 * In order for the serialization to work, we need to
		 * reacquire the inode locks before dropping the
		 * mutex.  We can't do that until after starting up
		 * the transaction and acquiring our log reservation.
		 *
		 * It is OK to hold the mutex across the log reservation
		 * call, because we don't hold any resources when
		 * obtaining the mutex.  This means that we can't hold
		 * anyone up as we wait for the mutex.
		 */
		mutex_lock(&xfs_ancestormon, PINOD);
		error = xfs_rename_ancestor_check(src_dp, src_ip,
						  target_dp, target_ip);
		if (error) {
			rename_which_error_return = __LINE__;
			mutex_unlock(&xfs_ancestormon);
			goto rele_return;
		}
		ancestor_checked = 1;
	} else {
		ancestor_checked = 0;
	}

	XFS_BMAP_INIT(&free_list, &first_block);
	mp = src_dp->i_mount;
	tp = xfs_trans_alloc(mp, XFS_TRANS_RENAME);
	cancel_flags = XFS_TRANS_RELEASE_LOG_RES;
        if (error = xfs_trans_reserve(tp, XFS_RENAME_SPACE_RES(mp, target_name),
			XFS_RENAME_LOG_RES(mp), 0, XFS_TRANS_PERM_LOG_RES,
			XFS_RENAME_LOG_COUNT)) {
		rename_which_error_return = __LINE__;
		xfs_trans_cancel(tp, 0);
		if (ancestor_checked) {
			mutex_unlock(&xfs_ancestormon);
		}
                goto rele_return;
	}

	/* 
	 * Attach the dquots to the inodes
	 */
	if (XFS_IS_QUOTA_ON(mp)) {
		if (error = xfs_qm_vop_rename_dqattach(inodes)) {
			xfs_trans_cancel(tp, cancel_flags);
			rename_which_error_return = __LINE__;
			if (ancestor_checked) 
				mutex_unlock(&xfs_ancestormon);
			goto rele_return;
		}
	}

	/*
	 * Reacquire the inode locks we dropped above.  Then check the
	 * generation counts on the inodes.  If any of them have changed,
	 * we cancel the transaction and start over from the top.
	 */
	xfs_rename_relock4(inodes);
	if (ancestor_checked) {
		mutex_unlock(&xfs_ancestormon);
	}		
	if (xfs_rename_compare_gencounts(inodes, gencounts)) {
		xfs_trans_cancel(tp, cancel_flags);
		xfs_rename_unlock4(inodes);
		IRELE(src_ip);
		if (target_ip != NULL) {
			IRELE(target_ip);
		}
		goto start_over;
	}

	/*
	 * Join all the inodes to the transaction. From this point on,
	 * we can rely on either trans_commit or trans_cancel to unlock
	 * them.  Note that we need to add a vnode reference to the
	 * directories since trans_commit & trans_cancel will decrement
	 * them when they unlock the inodes.  Also, we need to be careful
	 * not to add an inode to the transaction more than once.
	 */
        VN_HOLD(src_dir_vp);
        xfs_trans_ijoin(tp, src_dp, XFS_ILOCK_EXCL);
        if (new_parent) {
                VN_HOLD(target_dir_vp);
                xfs_trans_ijoin(tp, target_dp, XFS_ILOCK_EXCL);
        }
	if ((src_ip != src_dp) && (src_ip != target_dp)) {
		xfs_trans_ijoin(tp, src_ip, XFS_ILOCK_EXCL);
	}
        if ((target_ip != NULL) &&
	    (target_ip != src_ip) &&
	    (target_ip != src_dp) &&
	    (target_ip != target_dp)) {
                xfs_trans_ijoin(tp, target_ip, XFS_ILOCK_EXCL);
	}

	/*
	 * We should be in the same file system.
	 */
	ASSERT(src_dp->i_mount == target_dp->i_mount);

	/*
	 * We have to redo all of the checks we did above, because we've
	 * unlocked all of the inodes in the interim.
	 */
	error = xfs_rename_error_checks(src_dp, target_dp, src_ip,
			target_ip, src_name, target_name, credp, &status);
	if (error || status) {
		rename_which_error_return = status;
		goto error_return;
	}

	/*
	 * Set up the target.
	 */
	if (target_ip == NULL) {

		/*
		 * If target does not exist and the rename crosses
		 * directories, adjust the target directory link count
		 * to account for the ".." reference from the new entry.
		 */
		error = xfs_dir_createname(tp, target_dp, target_name,
					   src_ip->i_ino, &first_block,
					   &free_list, MAX_EXT_NEEDED);
		if (error) {
			rename_which_error_return = __LINE__;
			goto abort_return;
		}
		xfs_ichgtime(target_dp, XFS_ICHGTIME_MOD | XFS_ICHGTIME_CHG);

		if (new_parent && src_is_directory) {
			error = xfs_bumplink(tp, target_dp);
			if (error) {
				rename_which_error_return = __LINE__;
				goto abort_return;
			}
		}
	} else { /* target_ip != NULL */

		error = xfs_rename_target_checks(target_ip,
						 src_is_directory);
		if (error) {
			goto error_return;
		}

		/*
		 * Purge all dnlc references to the old target.
		 */
		dnlc_purge_vp(XFS_ITOV(target_ip));

		/*
		 * Link the source inode under the target name.
		 * If the source inode is a directory and we are moving
		 * it across directories, its ".." entry will be 
		 * inconsistent until we replace that down below.
		 *
		 * In case there is already an entry with the same
		 * name at the destination directory, remove it first.
		 */
		error = xfs_dir_replace(tp, target_dp, target_name,
			((target_pnp != NULL) ? target_pnp->pn_complen :
			 strlen(target_name)), src_ip->i_ino);
		if (error) {
			rename_which_error_return = __LINE__;
			goto abort_return;
		}
		xfs_ichgtime(target_dp, XFS_ICHGTIME_MOD | XFS_ICHGTIME_CHG);

		dnlc_enter(target_dir_vp, target_name, XFS_ITOBHV(src_ip),
			   credp);

		/*
		 * Decrement the link count on the target since the target
		 * dir no longer points to it.
		 */
		error = xfs_droplink(tp, target_ip);
		if (error) {
			rename_which_error_return = __LINE__;
			goto abort_return;;
		}
		target_ip_dropped = 1;

		if (src_is_directory) {
			/*
			 * Drop the link from the old "." entry.
			 */
			error = xfs_droplink(tp, target_ip);
			if (error) {
				rename_which_error_return = __LINE__;
				goto abort_return;
			}
		} 

	} /* target_ip != NULL */

	/*
	 * Remove the source.
	 */
	if (new_parent && src_is_directory) {

		/*
		 * Rewrite the ".." entry to point to the new 
	 	 * directory.
		 */
		error = xfs_dir_replace(tp, src_ip, "..", 2,
					target_dp->i_ino);
		if (error) {
			rename_which_error_return = __LINE__;
			goto abort_return;
		}
		xfs_ichgtime(src_ip, XFS_ICHGTIME_MOD | XFS_ICHGTIME_CHG);

		dnlc_remove(XFS_ITOV(src_ip), "..");
	} else {
		/*
		 * We always want to hit the ctime on the source inode.
		 * We do it in the if clause above for the 'new_parent &&
		 * src_is_directory' case, and here we get all the other
		 * cases.  This isn't strictly required by the standards
		 * since the source inode isn't really being changed,
		 * but old unix file systems did it and some incremental
		 * backup programs won't work without it.
		 */
		xfs_ichgtime(src_ip, XFS_ICHGTIME_CHG);
	}

	/*
	 * Adjust the link count on src_dp.  This is necessary when
	 * renaming a directory, either within one parent when
	 * the target existed, or across two parent directories.
	 */
	if (src_is_directory && (new_parent || target_ip != NULL)) {

		/*
		 * Decrement link count on src_directory since the
		 * entry that's moved no longer points to it.
		 */
		error = xfs_droplink(tp, src_dp);
		if (error) {
			rename_which_error_return = __LINE__;
			goto abort_return;
		}
		src_dp_dropped = 1;
	}

	error = xfs_dir_removename(tp, src_dp, src_name, &first_block,
				   &free_list, MAX_EXT_NEEDED);
	if (error) {
		rename_which_error_return = __LINE__;
		goto abort_return;
	}
	xfs_ichgtime(src_dp, XFS_ICHGTIME_MOD | XFS_ICHGTIME_CHG);

	dnlc_remove(src_dir_vp, src_name);

	/*
	 * Update the generation counts on all the directory inodes
	 * that we're modifying.
	 */
	src_dp->i_gen++;
	xfs_trans_log_inode(tp, src_dp, XFS_ILOG_CORE);

	if (new_parent) {
                target_dp->i_gen++;
                xfs_trans_log_inode(tp, target_dp, XFS_ILOG_CORE);
	}

	/*
	 * If there was a target inode, take an extra reference on
	 * it here so that it doesn't go to xfs_inactive() from
	 * within the commit.
	 */
	if (target_ip != NULL) {
		IHOLD(target_ip);
	}

	/*
	 * If this is a synchronous mount, make sure that the
	 * rename transaction goes to disk before returning to
	 * the user.
	 */
	if (mp->m_flags & XFS_MOUNT_WSYNC) {
		xfs_trans_set_sync(tp);
	}

	error = xfs_bmap_finish(&tp, &free_list, first_block, &committed);
	if (error) {
		xfs_bmap_cancel(&free_list);
		xfs_trans_cancel(tp, (XFS_TRANS_RELEASE_LOG_RES |
				 XFS_TRANS_ABORT));
		if (target_ip != NULL) {
			IRELE(target_ip);
		}
		return error;
	}
	/*
	 * Take refs. for vop_link_removed calls below.  No need to worry 
	 * about directory refs. because the caller holds them.
	 */
	if (target_ip_dropped) 
		IHOLD(target_ip);
	if (src_dp_dropped)
		IHOLD(src_ip);

	/*
	 * trans_commit will unlock src_ip, target_ip & decrement
	 * the vnode references.
	 */
	error = xfs_trans_commit(tp, XFS_TRANS_RELEASE_LOG_RES);
	if (target_ip != NULL) {
#ifndef SIM
		xfs_refcache_purge_ip(target_ip);
#endif
		IRELE(target_ip);
	}
	/*
	 * Let interposed file systems know about removed links.
	 */
	if (target_ip_dropped) {
		VOP_LINK_REMOVED(XFS_ITOV(target_ip), target_dir_vp, 
				 (target_ip)->i_d.di_nlink==0);
		IRELE(target_ip);
	}
	if (src_dp_dropped) {
		VOP_LINK_REMOVED(src_dir_vp, XFS_ITOV(src_ip), 
				 (src_dp)->i_d.di_nlink==0);
		IRELE(src_ip);
	}

	if (error) {
		return error;
	}

	if (DM_EVENT_ENABLED(src_dir_vp->v_vfsp, XFS_BHVTOI(src_dir_bdp),
			     DM_POSTRENAME) ||
	    DM_EVENT_ENABLED(target_dir_vp->v_vfsp,
			     XFS_BHVTOI(target_dir_bdp), DM_POSTRENAME)) {
		(void) dm_namesp_event(DM_POSTRENAME, src_dir_vp,
			target_dir_vp, src_name, target_name, 0, 0);
	}

	return 0;

 abort_return:
	cancel_flags |= XFS_TRANS_ABORT;
	/* FALLTHROUGH */
 error_return:
	xfs_bmap_cancel(&free_list);
	xfs_trans_cancel(tp, cancel_flags);
	return error;

 unlock_rele_return:
	xfs_rename_unlock4(inodes);
	/* FALLTHROUGH */
 rele_return:
	IRELE(src_ip);
	if (target_ip != NULL) {
		IRELE(target_ip);
	}
	return error;
}
