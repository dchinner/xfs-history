#ifndef __XFS_BUF_H__
#define __XFS_BUF_H__

#define _USING_BUF_T

#ifdef _USING_BUF_T
#include <sys/buf.h>

typedef struct buf xfs_buf_t;
#define xfs_buf buf

#define XFS_BUF_IODONE_FUNC(buf)	(buf)->b_iodone
#define XFS_BUF_SET_IODONE_FUNC(buf, func)	\
			if ((buf)->b_iodone == NULL) { \
				(buf)->b_iodone = (func); \
			}
#define XFS_BUF_CLR_IODONE_FUNC(buf)		\
			(buf)->b_iodone = NULL

#define XFS_BUF_FSPRIVATE(buf, type)		\
			((type)(buf)->b_fsprivate)
#define XFS_BUF_SET_FSPRIVATE(buf, value)	\
			(buf)->b_fsprivate = (void *)(value)
#define XFS_BUF_FSPRIVATE2(buf, type)		\
			((type)(buf)->b_fsprivate2)
#define XFS_BUF_SET_FSPRIVATE2(buf, value)	\
			(buf)->b_fsprivate2 = (void *)(value)

#endif /* _USING_BUF_T */

#ifdef _USING_PAGEBUF_T
#include <linux/page_buf.h>

struct inode;

typedef struct buftarg {
	struct inode	*inode;
	dev_t		dev;
} buftarg_t;

#define XFS_BUF_IODONE_FUNC(buf)	(buf)->pb_done
#define XFS_BUF_SET_IODONE_FUNC(buf, func)	\
			if ((buf)->pb_done == NULL) { \
				pagebuf_hold(buf); \
				(buf)->pb_done = (func); \
			}
#define XFS_BUF_CLR_IODONE_FUNC(buf)		\
			if ((buf)->pb_done) { \
				(buf)->pb_done = NULL; \
				pagebuf_rele(buf); \
			}

#define XFS_BUF_FSPRIVATE(buf, type)		\
			((type)(buf)->pb_fspriv)
#define XFS_BUF_SET_FSPRIVATE(buf, value)	\
			(buf)->pb_fspriv = (void *)(value)
#define XFS_BUF_FSPRIVATE2(buf, type)		\
			((type)(buf)->pb_fspriv2)
#define XFS_BUF_SET_FSPRIVATE2(buf, value)	\
			(buf)->pb_fspriv2 = (void *)(value)


#endif /* _USING_PAGEBUF_T */

#endif
