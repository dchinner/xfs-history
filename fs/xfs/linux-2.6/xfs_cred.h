/*
 *
 * /ptools/plroot/pingu/slinx-xfs/kern/fs/xfs/pseudo-inc/sys/RCS/cred.h,v 1.1 1999/09/23 22:23:15 cattelan Exp
 * cattelan
 * cred.h,v 1.1 1999/09/23 22:23:15 cattelan Exp
 *
 * cred.h,v
 * Revision 1.1  1999/09/23 22:23:15  cattelan
 * kern/fs/xfs/linux-sys/cred.h 1.4 Renamed to kern/fs/xfs/pseudo-inc/sys/cred.h
 *
 * Revision 1.4  1999/08/27 22:00:32  cattelan
 * Header file checkpoint... libsim mkfs xfsdb build\n as well as most of the kernel files
 *
 * Revision 1.3  1999/08/26 19:43:29  mostek
 * Add some needed by linux_ops_super.c.
 *
 * Revision 1.2  1999/08/19 17:36:03  cattelan
 * Clean up most warning messages.
 *
 * Revision 1.1  1999/08/11 04:33:02  cattelan
 * TAKE - Additional include files for lib sim
 *
 */


#ifndef _XFS_CRED_H    /* wrapper symbol for kernel use */
#define _XFS_CRED_H    /* subject to change without notice */

#include <asm/param.h>		/* For NGROUPS */
#include <sys/types.h>          /* REQUIRED */
#include <sys/capability.h>     /* REQUIRED for cap_set_t */

typedef struct cred {
        int     cr_ref;                 /* reference count */
        ushort  cr_ngroups;             /* number of groups in cr_groups */
        uid_t   cr_uid;                 /* effective user id */
        gid_t   cr_gid;                 /* effective group id */
        uid_t   cr_ruid;                /* real user id */
        gid_t   cr_rgid;                /* real group id */
        uid_t   cr_suid;                /* "saved" user id (from exec) */
        gid_t   cr_sgid;                /* "saved" group id (from exec) */
        struct mac_label *cr_mac;       /* MAC label for B1 and beyond */
        cap_set_t cr_cap;               /* capability (privilege) sets */
#if CELL_CAPABLE || CELL_PREPARE
        credid_t cr_id;                 /* cred id */
#endif
        gid_t   cr_groups[NGROUPS];	/* supplementary group list */
} cred_t;

#define	ANYCRED	((struct cred *)(__psint_t)-1)
#define	NOCRED	((struct cred *)(__psint_t)0)

extern void cred_init(void);
extern cred_t *get_current_cred(void);
extern void cred_fill_from_current(cred_t *);
extern struct cred *sys_cred;

#endif  /* _XFS_CRED_H */
