/*
 * Copyright (C) 2008 Christoph Hellwig.
 * Portions Copyright (C) 2000-2008 Silicon Graphics, Inc.
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
#include "xfs_attr.h"
#include "xfs_acl.h"
#include "xfs_vnodeops.h"

#include <linux/posix_acl_xattr.h>
#include <linux/xattr.h>


/*
 * ACL handling.  Should eventually be moved into xfs_acl.c
 */

static int
xfs_decode_acl(const char *name)
{
	if (strcmp(name, "posix_acl_access") == 0)
		return _ACL_TYPE_ACCESS;
	else if (strcmp(name, "posix_acl_default") == 0)
		return _ACL_TYPE_DEFAULT;
	return -EINVAL;
}

/*
 * Get system extended attributes which at the moment only
 * includes Posix ACLs.
 */
static int
xfs_xattr_system_get(struct inode *inode, const char *name,
		void *buffer, size_t size)
{
	int acl;

	acl = xfs_decode_acl(name);
	if (acl < 0)
		return acl;

	return xfs_acl_vget(inode, buffer, size, acl);
}

static int
xfs_xattr_system_set(struct inode *inode, const char *name,
		const void *value, size_t size, int flags)
{
	int error, acl;

	acl = xfs_decode_acl(name);
	if (acl < 0)
		return acl;
	if (flags & XATTR_CREATE)
		return -EINVAL;

	if (!value)
		return xfs_acl_vremove(inode, acl);

	error = xfs_acl_vset(inode, (void *)value, size, acl);
	if (!error)
		vn_revalidate(inode);
	return error;
}

static struct xattr_handler xfs_xattr_system_handler = {
	.prefix	= XATTR_SYSTEM_PREFIX,
	.get	= xfs_xattr_system_get,
	.set	= xfs_xattr_system_set,
};


/*
 * Real xattr handling.  The only difference between the namespaces is
 * a flag passed to the low-level attr code.
 */

static int
__xfs_xattr_get(struct inode *inode, const char *name,
		void *value, size_t size, int xflags)
{
	struct xfs_inode *ip = XFS_I(inode);
	int error, asize = size;

	if (strcmp(name, "") == 0)
		return -EINVAL;

	/* Convert Linux syscall to XFS internal ATTR flags */
	if (!size) {
		xflags |= ATTR_KERNOVAL;
		value = NULL;
	}

	error = -xfs_attr_get(ip, name, value, &asize, xflags);
	if (error)
		return error;
	return asize;
}

static int
__xfs_xattr_set(struct inode *inode, const char *name, const void *value,
		size_t size, int flags, int xflags)
{
	struct xfs_inode *ip = XFS_I(inode);

	if (strcmp(name, "") == 0)
		return -EINVAL;

	/* Convert Linux syscall to XFS internal ATTR flags */
	if (flags & XATTR_CREATE)
		xflags |= ATTR_CREATE;
	if (flags & XATTR_REPLACE)
		xflags |= ATTR_REPLACE;

	if (!value)
		return -xfs_attr_remove(ip, name, xflags);
	return -xfs_attr_set(ip, name, (void *)value, size, xflags);
}

static int
xfs_xattr_user_get(struct inode *inode, const char *name,
		void *value, size_t size)
{
	return __xfs_xattr_get(inode, name, value, size, 0);
}

static int
xfs_xattr_user_set(struct inode *inode, const char *name,
		const void *value, size_t size, int flags)
{
	return __xfs_xattr_set(inode, name, value, size, flags, 0);
}

struct attrnames attr_user = {
	.attr_name	= "user.",
	.attr_namelen	= sizeof("user.") - 1,
};

static struct xattr_handler xfs_xattr_user_handler = {
	.prefix	= XATTR_USER_PREFIX,
	.get	= xfs_xattr_user_get,
	.set	= xfs_xattr_user_set,
};


static int
xfs_xattr_trusted_get(struct inode *inode, const char *name,
		void *value, size_t size)
{
	return __xfs_xattr_get(inode, name, value, size, ATTR_ROOT);
}

static int
xfs_xattr_trusted_set(struct inode *inode, const char *name,
		const void *value, size_t size, int flags)
{
	return __xfs_xattr_set(inode, name, value, size, flags, ATTR_ROOT);
}

struct attrnames attr_trusted = {
	.attr_name	= "trusted.",
	.attr_namelen	= sizeof("trusted.") - 1,
};

static struct xattr_handler xfs_xattr_trusted_handler = {
	.prefix	= XATTR_TRUSTED_PREFIX,
	.get	= xfs_xattr_trusted_get,
	.set	= xfs_xattr_trusted_set,
};


static int
xfs_xattr_secure_get(struct inode *inode, const char *name,
		void *value, size_t size)
{
	return __xfs_xattr_get(inode, name, value, size, ATTR_SECURE);
}

static int
xfs_xattr_secure_set(struct inode *inode, const char *name,
		const void *value, size_t size, int flags)
{
	return __xfs_xattr_set(inode, name, value, size, flags, ATTR_SECURE);
}

struct attrnames attr_secure = {
	.attr_name	= "security.",
	.attr_namelen	= sizeof("security.") - 1,
};

static struct xattr_handler xfs_xattr_security_handler = {
	.prefix	= XATTR_SECURITY_PREFIX,
	.get	= xfs_xattr_secure_get,
	.set	= xfs_xattr_secure_set,
};


struct xattr_handler *xfs_xattr_handlers[] = {
	&xfs_xattr_user_handler,
	&xfs_xattr_trusted_handler,
	&xfs_xattr_security_handler,
	&xfs_xattr_system_handler,
	NULL
};

static int
list_one_attr(const char *name, const size_t len, void *data,
		size_t size, ssize_t *result)
{
	char *p = data + *result;

	*result += len;
	if (!size)
		return 0;
	if (*result > size)
		return -ERANGE;

	strcpy(p, name);
	return 0;
}

ssize_t
xfs_vn_listxattr(struct dentry *dentry, char *data, size_t size)
{
	struct inode		*inode = dentry->d_inode;
	struct xfs_inode	*ip = XFS_I(inode);
	attrlist_cursor_kern_t	cursor = { 0 };
	int			error, xflags;
	ssize_t			result;

	xflags = ATTR_KERNAMELS;
	if (!size)
		xflags |= ATTR_KERNOVAL;

	if (capable(CAP_SYS_ADMIN))
		xflags |= ATTR_KERNFULLS;
	else
		xflags |= ATTR_KERNORMALS;


	/*
	 * First read the regular on-disk attributes.
	 */
	result = -xfs_attr_list(ip, data, size, xflags, &cursor);
	if (result < 0)
		return result;

	/*
	 * Then add the two synthetic ACL attributes.
	 */
	if (xfs_acl_vhasacl_access(inode)) {
		error = list_one_attr(POSIX_ACL_XATTR_ACCESS,
				strlen(POSIX_ACL_XATTR_ACCESS) + 1,
				data, size, &result);
		if (error)
			return error;
	}

	if (xfs_acl_vhasacl_default(inode)) {
		error = list_one_attr(POSIX_ACL_XATTR_DEFAULT,
				strlen(POSIX_ACL_XATTR_DEFAULT) + 1,
				data, size, &result);
		if (error)
			return error;
	}

	return result;
}
