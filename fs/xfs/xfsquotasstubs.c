
/*
 * Copyright (C) 1999 Silicon Graphics, Inc.  All Rights Reserved.
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston MA 02111-1307, USA.
 */
/*
 *
 * $Header: /home/cattelan/xfs_cvs/xfs-for-git/fs/xfs/Attic/xfsquotasstubs.c,v 1.5 2000/06/09 01:50:04 nathans Exp $
 * $Author: nathans $
 * $Id: xfsquotasstubs.c,v 1.5 2000/06/09 01:50:04 nathans Exp $
 *
 * $Log: xfsquotasstubs.c,v $
 * Revision 1.5  2000/06/09 01:50:04  nathans
 * Merge of 2.3.99pre2-xfs:slinx:46428a by ananth.
 *
 *   Merge of 2.3.42-xfs:slinx:46428a by ananth.
 *   remove unused include files.
 *
 * Revision 1.5  2000/03/25 01:29:53  nathans
 * Merge of 2.3.42-xfs:slinx:46428a by ananth.
 *
 *   remove unused include files.
 *
 * Revision 1.5  2000/03/20 07:37:34  nathans
 * remove unused include files.
 *
 * Revision 1.4  2000/01/30 09:59:06  kenmcd
 * Encumbrance review done.
 * Add copyright and license words consistent with GPL.
 * Refer to http://fsg.melbourne.sgi.com/reviews/ for details.
 *
 * There is a slight change in the license terms and conditions words
 * to go with the copyrights, so most of the files are not getting
 * new GPL's, just updated versions ... but there are 20-30 more files
 * here as well.
 *
 * Revision 1.2  1999/09/01 00:35:47  mostek
 * Get rid of warnings (void) arguments.
 *
 */

/*
 * XFS Disk Quota stubs
 */
#include <sys/types.h>
#include <sys/systm.h>
#include <sys/quota.h>
#include <sys/fs/xfs_types.h>
#include <linux/xfs_sema.h>

struct xfs_qm *xfs_Gqm = NULL;
mutex_t	xfs_Gqm_lock;

struct xfs_qm   *xfs_qm_init(void) {return NULL;}

void 		xfs_qm_destroy(void) { return; }
int		xfs_qm_dqflush_all(void) { return nopkg(); }
int		xfs_qm_dqattach(void) { return nopkg(); }
int		xfs_quotactl(void) { return nopkg(); }
int		xfs_qm_dqpurge_all(void) { return nopkg(); }
void		xfs_qm_mount_quotainit(void) { return; }
void		xfs_qm_unmount_quotadestroy(void) { return; }
int		xfs_qm_mount_quotas(void) { return nopkg(); }
int 		xfs_qm_unmount_quotas(void) { return nopkg(); }
void		xfs_qm_dqdettach_inode(void) { return; }
int 		xfs_qm_sync(void) { return nopkg(); }
int		xfs_qm_check_inoquota(void) { return nopkg(); }
int		xfs_qm_check_inoquota2(void) { return nopkg(); }
void		xfs_qm_dqrele_all_inodes(void) { return; }

/*
 * dquot interface
 */
void		xfs_dqlock(void) { return; }
void		xfs_dqunlock(void) { return; }
void		xfs_dqunlock_nonotify(void) { return; }
void		xfs_dqlock2(void) {return;}
void 		xfs_qm_dqput(void) { return; }
void 		xfs_qm_dqrele(void) { return; }
int		xfs_qm_dqid(void) { return -1; }
int		xfs_qm_dqget(void) { return nopkg(); }	
int 		xfs_qm_dqcheck(void) { return nopkg(); }

/*
 * Transactions
 */
void 		xfs_trans_alloc_dqinfo(void) { return; }
void 		xfs_trans_free_dqinfo(void) { return; }
void		xfs_trans_dup_dqinfo(void) { return; }
void		xfs_trans_mod_dquot(void) { return; }
int		xfs_trans_mod_dquot_byino(void) { return nopkg(); }
void		xfs_trans_apply_dquot_deltas(void) { return; }
void		xfs_trans_unreserve_and_mod_dquots(void) { return; }
int		xfs_trans_reserve_quota_nblks(void) { return nopkg(); }
int		xfs_trans_reserve_quota_bydquots(void) { return nopkg(); }
void		xfs_trans_log_dquot(void) { return; }
void		xfs_trans_dqjoin(void) { return; }

/* 
 * Vnodeops Utility Functions
 */

struct xfs_dquot *xfs_qm_vop_chown(void) { return NULL; }
int		xfs_qm_vop_dqalloc(void) { return nopkg(); }
int		xfs_qm_vop_chown_dqalloc(void) { return nopkg(); }
int		xfs_qm_vop_chown_reserve(void) { return nopkg(); }
int		xfs_qm_vop_rename_dqattach(void) { return nopkg(); }
void		xfs_qm_vop_dqattach_and_dqmod_newinode(void) { return; }


