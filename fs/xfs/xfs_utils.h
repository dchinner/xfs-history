#ifndef __XFS_UTILS_H__
#define __XFS_UTILS_H__

#define IRELE(ip)	VN_RELE(XFS_ITOV(ip))
#define IHOLD(ip)	VN_HOLD(XFS_ITOV(ip))
#define	ITRACE(ip)	vn_trace_ref(XFS_ITOV(ip), __FILE__, __LINE__, \
				(inst_t *)__return_address)

#define DLF_IGET	0x01	/* get entry inode if name lookup succeeds */
#define	DLF_NODNLC	0x02	/* don't use the dnlc */
#define	DLF_LOCK_SHARED	0x04	/* directory locked shared */

#ifndef SIM
extern int
xfs_rename(
	bhv_desc_t	*src_dir_bdp,
	char		*src_name,
	vnode_t		*target_dir_vp,
	char		*target_name,
	struct pathname	*target_pnp,
	cred_t		*credp);

extern int
xfs_dir_lookup_int(
	xfs_trans_t  		*tp,
	bhv_desc_t     		*dir_bdp,
	int		 	flags,
	char         		*name,
	struct pathname   	*pnp,
	xfs_ino_t    		*inum,
	xfs_inode_t  		**ipp,
	struct ncfastdata	*fd,
	uint			*dir_unlocked);

extern int
xfs_truncate_file(
	xfs_mount_t	*mp,
	xfs_inode_t	*ip);

extern int 	
xfs_dir_ialloc(
	xfs_trans_t 	**tpp, 
	xfs_inode_t 	*dp, 
	mode_t 		mode, 
	nlink_t 	nlink, 
	dev_t 		rdev,
	cred_t 		*credp,
	prid_t 		prid,
	int		okalloc,
	xfs_inode_t	**ipp,
	int 		*committed);

extern int
xfs_stickytest(
	xfs_inode_t	*dp,
	xfs_inode_t	*ip,
	cred_t		*cr);

extern int
xfs_droplink(
	xfs_trans_t	*tp,
	xfs_inode_t	*ip);

extern int
xfs_bumplink(
	xfs_trans_t	*tp,
	xfs_inode_t	*ip);

extern void
xfs_bump_ino_vers2(
	xfs_trans_t	*tp,
	xfs_inode_t	*ip);

#endif /* !SIM */

#endif /* XFS_UTILS_H */

