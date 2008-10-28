/*
 * Copyright (c) 2004-2005 Silicon Graphics, Inc.
 * All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write the Free Software Foundation,
 * Inc.,  51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */
#include "xfs.h"
#include "xfs_fs.h"
#include "xfs_bit.h"
#include "xfs_buf.h"
#include "xfs_log.h"
#include "xfs_imap.h"
#include "xfs_inum.h"
#include "xfs_trans.h"
#include "xfs_sb.h"
#include "xfs_ag.h"
#include "xfs_dir2.h"
#include "xfs_alloc.h"
#include "xfs_dmapi.h"
#include "xfs_quota.h"
#include "xfs_mount.h"
#include "xfs_da_btree.h"
#include "xfs_bmap_btree.h"
#include "xfs_alloc_btree.h"
#include "xfs_ialloc_btree.h"
#include "xfs_dir2_sf.h"
#include "xfs_attr_sf.h"
#include "xfs_dinode.h"
#include "xfs_inode.h"
#include "xfs_btree.h"
#include "xfs_btree_trace.h"
#include "xfs_ialloc.h"
#include "xfs_bmap.h"
#include "xfs_rtalloc.h"
#include "xfs_error.h"
#include "xfs_itable.h"
#include "xfs_rw.h"
#include "xfs_dir2_data.h"
#include "xfs_dir2_leaf.h"
#include "xfs_dir2_block.h"
#include "xfs_dir2_node.h"
#include "xfs_dir2_trace.h"
#include "xfs_acl.h"
#include "xfs_attr.h"
#include "xfs_attr_leaf.h"
#include "xfs_inode_item.h"
#include "xfs_buf_item.h"
#include "xfs_extfree_item.h"
#include "xfs_log_priv.h"
#include "xfs_trans_priv.h"
#include "xfs_trans_space.h"
#include "xfs_utils.h"
#include "xfs_iomap.h"
#include "xfs_filestream.h"
#include "xfs_vnodeops.h"
#include "xfs_vfsops.h"
#include "support/ktrace.h"


/*
 * Export symbols used for XFS tracing
 */

#if defined(CONFIG_XFS_TRACE)
EXPORT_SYMBOL(ktrace_enter);
EXPORT_SYMBOL(ktrace_free);
EXPORT_SYMBOL(ktrace_alloc);
EXPORT_SYMBOL(ktrace_skip);
EXPORT_SYMBOL(ktrace_nentries);
EXPORT_SYMBOL(ktrace_first);
EXPORT_SYMBOL(ktrace_next);
#endif

#ifdef XFS_INODE_TRACE
EXPORT_SYMBOL(_xfs_itrace_ref);
EXPORT_SYMBOL(_xfs_itrace_entry);
EXPORT_SYMBOL(_xfs_itrace_exit);
EXPORT_SYMBOL(xfs_itrace_hold);
EXPORT_SYMBOL(xfs_itrace_rele);
#endif

#ifdef XFS_ILOCK_TRACE
EXPORT_SYMBOL(xfs_ilock_trace_buf);
#endif
#ifdef XFS_ALLOC_TRACE
EXPORT_SYMBOL(xfs_alloc_trace_buf);
#endif
#ifdef XFS_BMAP_TRACE
EXPORT_SYMBOL(xfs_bmap_trace_buf);
#endif
#ifdef XFS_BTREE_TRACE
EXPORT_SYMBOL(xfs_allocbt_trace_buf);
EXPORT_SYMBOL(xfs_inobt_trace_buf);
EXPORT_SYMBOL(xfs_bmbt_trace_buf);
#endif
#ifdef XFS_ATTR_TRACE
EXPORT_SYMBOL(xfs_attr_trace_buf);
#endif
#ifdef XFS_DIR2_TRACE
EXPORT_SYMBOL(xfs_dir2_trace_buf);
#endif
#ifdef XFS_FILESTREAMS_TRACE
EXPORT_SYMBOL(xfs_filestreams_trace_buf);
#endif

#ifdef XFS_BUF_TRACE
extern ktrace_t *xfs_buf_trace_buf;
EXPORT_SYMBOL(xfs_buf_trace_buf);
EXPORT_SYMBOL(xfs_buf_trace);
#endif


/*
 * Export symbols used for XFS debugging
 */
EXPORT_SYMBOL(xfs_next_bit);
EXPORT_SYMBOL(xfs_contig_bits);
EXPORT_SYMBOL(xfs_bmbt_get_all);
EXPORT_SYMBOL(xfs_params);
#ifndef XFS_NATIVE_HOST
EXPORT_SYMBOL(xfs_bmbt_disk_get_all);
#endif

#if defined(CONFIG_KDB_MODULES)
EXPORT_SYMBOL(xfs_get_buftarg_list);
#endif

/*
 * Export symbols used by XFS behavior modules.
 */

EXPORT_SYMBOL(assfail);
EXPORT_SYMBOL(cmn_err);
EXPORT_SYMBOL(xfs_flush_pages);
EXPORT_SYMBOL(xfs_flushinval_pages);
EXPORT_SYMBOL(xfs_tosspages);
EXPORT_SYMBOL(fs_noval);
EXPORT_SYMBOL(icmn_err);
EXPORT_SYMBOL(kmem_alloc);
EXPORT_SYMBOL(kmem_free);
EXPORT_SYMBOL(kmem_realloc);
EXPORT_SYMBOL(kmem_zalloc);
EXPORT_SYMBOL(kmem_zone_alloc);
EXPORT_SYMBOL(kmem_zalloc_greedy);
EXPORT_SYMBOL(kmem_zone_zalloc);
EXPORT_SYMBOL(xfs_address_space_operations);
EXPORT_SYMBOL(xfs_dir_file_operations);
EXPORT_SYMBOL(xfs_file_operations);
EXPORT_SYMBOL(xfs_invis_file_operations);
EXPORT_SYMBOL(xfs_buf_delwri_dequeue);
EXPORT_SYMBOL(_xfs_buf_find);
EXPORT_SYMBOL(xfs_buf_iostart);
EXPORT_SYMBOL(xfs_buf_ispin);
#ifdef DEBUG
EXPORT_SYMBOL(xfs_buf_lock_value);
#endif
EXPORT_SYMBOL(xfs_buf_offset);
EXPORT_SYMBOL(xfs_buf_rele);
EXPORT_SYMBOL(xfs_buf_readahead);
EXPORT_SYMBOL(xfs_buf_unlock);
EXPORT_SYMBOL(uuid_create_nil);
EXPORT_SYMBOL(uuid_equal);
EXPORT_SYMBOL(uuid_getnodeuniq);
EXPORT_SYMBOL(uuid_hash64);
EXPORT_SYMBOL(uuid_is_nil);
EXPORT_SYMBOL(uuid_table_remove);

#if defined(CONFIG_XFS_POSIX_ACL)
EXPORT_SYMBOL(xfs_acl_vtoacl);
EXPORT_SYMBOL(xfs_acl_inherit);
#endif
EXPORT_SYMBOL(xfs_alloc_buftarg);
EXPORT_SYMBOL(xfs_flush_buftarg);
EXPORT_SYMBOL(xfs_free_buftarg);
EXPORT_SYMBOL(xfs_bdstrat_cb);
EXPORT_SYMBOL(xfs_bmap_cancel);
EXPORT_SYMBOL(xfs_bmap_count_blocks);
EXPORT_SYMBOL(xfs_bmap_finish);
EXPORT_SYMBOL(xfs_bmap_search_multi_extents);
EXPORT_SYMBOL(xfs_bmapi);
EXPORT_SYMBOL(xfs_bmapi_single);
EXPORT_SYMBOL(xfs_bmbt_get_blockcount);
EXPORT_SYMBOL(xfs_bmbt_get_state);
EXPORT_SYMBOL(xfs_bmbt_get_startoff);
EXPORT_SYMBOL(xfs_bmbt_set_all);
EXPORT_SYMBOL(xfs_bmbt_set_allf);
EXPORT_SYMBOL(xfs_bmbt_set_blockcount);
EXPORT_SYMBOL(xfs_bmbt_set_startblock);
EXPORT_SYMBOL(xfs_bmbt_set_startoff);
EXPORT_SYMBOL(xfs_bmbt_set_state);
EXPORT_SYMBOL(xfs_buf_attach_iodone);
EXPORT_SYMBOL(xfs_bulkstat);
EXPORT_SYMBOL(xfs_bunmapi);
EXPORT_SYMBOL(xfs_bwrite);
EXPORT_SYMBOL(xfs_change_file_space);
EXPORT_SYMBOL(xfs_dev_is_read_only);
EXPORT_SYMBOL(xfs_dir_ialloc);
EXPORT_SYMBOL(xfs_error_report);
#ifdef DEBUG
EXPORT_SYMBOL(xfs_error_trap);
#endif
EXPORT_SYMBOL(xfs_file_last_byte);
EXPORT_SYMBOL(xfs_freesb);
EXPORT_SYMBOL(xfs_fs_cmn_err);
EXPORT_SYMBOL(xfs_idestroy);
EXPORT_SYMBOL(xfs_iextract);
EXPORT_SYMBOL(xfs_iext_add);
EXPORT_SYMBOL(xfs_iext_destroy);
EXPORT_SYMBOL(xfs_iext_get_ext);
EXPORT_SYMBOL(xfs_iext_idx_to_irec);
EXPORT_SYMBOL(xfs_iext_insert);
EXPORT_SYMBOL(xfs_iext_remove);
EXPORT_SYMBOL(xfs_iflush);
EXPORT_SYMBOL(xfs_iget);
EXPORT_SYMBOL(xfs_ilock);
EXPORT_SYMBOL(xfs_ilock_map_shared);
EXPORT_SYMBOL(xfs_ilock_nowait);
#ifdef DEBUG
EXPORT_SYMBOL(xfs_isilocked);
#endif
EXPORT_SYMBOL(xfs_internal_inum);
EXPORT_SYMBOL(xfs_iput);
EXPORT_SYMBOL(xfs_iput_new);
EXPORT_SYMBOL(xfs_iread);
EXPORT_SYMBOL(xfs_iread_extents);
EXPORT_SYMBOL(xfs_itruncate_start);
EXPORT_SYMBOL(xfs_iunlock);
EXPORT_SYMBOL(xfs_iunlock_map_shared);
EXPORT_SYMBOL(xfs_itruncate_finish);
EXPORT_SYMBOL(xfs_log_force);
EXPORT_SYMBOL(_xfs_log_force);
EXPORT_SYMBOL(xfs_log_force_umount);
EXPORT_SYMBOL(xfs_log_unmount_dealloc);
EXPORT_SYMBOL(xfs_log_unmount_write);
EXPORT_SYMBOL(xfs_mod_sb);
EXPORT_SYMBOL(xfs_mount_reset_sbqflags);
EXPORT_SYMBOL(xfs_mountfs);
EXPORT_SYMBOL(xfs_qm_dqcheck);
EXPORT_SYMBOL(xfs_readsb);
EXPORT_SYMBOL(xfs_read_buf);
EXPORT_SYMBOL(xfs_setattr);
EXPORT_SYMBOL(xfs_attr_get);
EXPORT_SYMBOL(xfs_attr_set);
EXPORT_SYMBOL(xfs_fsync);
EXPORT_SYMBOL(xfs_attr_remove);
EXPORT_SYMBOL(xfs_attr_list);
EXPORT_SYMBOL(xfs_readdir);
EXPORT_SYMBOL(xfs_setsize_buftarg);
EXPORT_SYMBOL(xfs_sync_inodes);
EXPORT_SYMBOL(xfs_trans_add_item);
EXPORT_SYMBOL(xfs_trans_alloc);
EXPORT_SYMBOL(xfs_trans_bhold);
EXPORT_SYMBOL(xfs_trans_bhold_release);
EXPORT_SYMBOL(xfs_trans_bjoin);
EXPORT_SYMBOL(xfs_trans_brelse);
EXPORT_SYMBOL(xfs_trans_cancel);
EXPORT_SYMBOL(_xfs_trans_commit);
EXPORT_SYMBOL(xfs_trans_ail_delete);
EXPORT_SYMBOL(xfs_trans_dquot_buf);
EXPORT_SYMBOL(xfs_trans_find_item);
EXPORT_SYMBOL(xfs_trans_get_buf);
EXPORT_SYMBOL(xfs_trans_iget);
EXPORT_SYMBOL(xfs_trans_ihold);
EXPORT_SYMBOL(xfs_trans_ijoin);
EXPORT_SYMBOL(xfs_trans_log_buf);
EXPORT_SYMBOL(xfs_trans_log_inode);
EXPORT_SYMBOL(xfs_trans_mod_sb);
EXPORT_SYMBOL(xfs_trans_read_buf);
EXPORT_SYMBOL(xfs_trans_reserve);
EXPORT_SYMBOL(xfs_trans_unlocked_item);
EXPORT_SYMBOL(xfs_truncate_file);
EXPORT_SYMBOL(xfs_unmount_flush);
EXPORT_SYMBOL(xfs_unmountfs_writesb);
EXPORT_SYMBOL(xfs_write_clear_setuid);
EXPORT_SYMBOL(xfs_dinode_from_disk);
EXPORT_SYMBOL(xfs_sb_from_disk);
EXPORT_SYMBOL(xfs_zero_eof);
EXPORT_SYMBOL(xlog_recover_process_iunlinks);
EXPORT_SYMBOL(xfs_free_eofblocks);

EXPORT_SYMBOL(xfs_do_force_shutdown);
