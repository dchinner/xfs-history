


#ifndef _XFS_TRANS_PRIV_H
#define	_XFS_TRANS_PRIV_H

/*
 * From xfs_trans_async.c
 */
int		xfs_trans_any_async(xfs_mount_t *);
void		xfs_trans_add_async(xfs_trans_t *);
xfs_trans_t	*xfs_trans_get_async(xfs_mount_t *);

/*
 * From xfs_trans_item.c
 */
xfs_log_item_desc_t	*xfs_trans_add_item(xfs_trans_t *,
					    xfs_log_item_t *);
void			xfs_trans_free_item(xfs_trans_t *,
					    xfs_log_item_desc_t *);
xfs_log_item_desc_t	*xfs_trans_find_item(xfs_trans_t *,
					     xfs_log_item_t *);
xfs_log_item_desc_t	*xfs_trans_first_item(xfs_trans_t *);
xfs_log_item_desc_t	*xfs_trans_next_item(xfs_trans_t *,
					     xfs_log_item_desc_t *);
void			xfs_trans_free_items(xfs_trans_t *);
void			xfs_trans_unlock_items(xfs_trans_t *);

/*
 * From xfs_trans_ail.c
 */
xfs_lsn_t	xfs_trans_push_ail(xfs_mount_t *, xfs_lsn_t);
void		xfs_trans_update_ail(xfs_mount_t *, xfs_log_item_t *,
				     xfs_lsn_t);
void		xfs_trans_delete_ail(xfs_mount_t *, xfs_log_item_t *);
xfs_log_item_t	*xfs_trans_first_ail(xfs_mount_t *, int *);
xfs_log_item_t	*xfs_trans_next_ail(xfs_mount_t *, xfs_log_item_t *,
				    int *, int *);


#endif	/* _XFS_TRANS_PRIV_H */
