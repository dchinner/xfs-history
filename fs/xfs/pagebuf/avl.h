/*
 * Copyright (c) 2000 Silicon Graphics, Inc.  All Rights Reserved.
 * 
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 * 
 * This program is distributed in the hope that it would be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * 
 * Further, this software is distributed without any warranty that it is
 * free of the rightful claim of any third person regarding infringement
 * or the like.  Any license provided herein, whether implied or
 * otherwise, applies only to this software file.  Patent licenses, if
 * any, provided herein do not apply to combinations of this program with
 * other software, or any other product whatsoever.
 * 
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write the Free Software Foundation, Inc., 59
 * Temple Place - Suite 330, Boston MA 02111-1307, USA.
 * 
 * Contact information: Silicon Graphics, Inc., 1600 Amphitheatre Pkwy,
 * Mountain View, CA  94043, or:
 * 
 * http://www.sgi.com 
 * 
 * For further information regarding this notice, see: 
 * 
 * http://oss.sgi.com/projects/GenInfo/SGIGPLNoticeExplan/
 */

/*
 *	Written by William J. Earl at SGI
 */

#ifndef __AVL_H__
#define __AVL_H__

#include <linux/types.h>

/*
 *	Key and value types 
 */

typedef unsigned long avl_key_t;
typedef unsigned long avl_value_t;

/*
 *	AVL tree handle
 */

typedef void *avl_handle_t;

/*
 *	Option flags
 */

typedef enum {
	avl_opt_nowait = (1 << 0),	/* do not sleep 		*/
	avl_opt_nolock = (1 << 1)	/* caller handles synchronization */
} avl_opt_t;

/*
 *	Comparison function
 */

typedef int (*avl_comparison_t)(avl_key_t,avl_key_t);

/*
 *	Increment function
 */

typedef void (*avl_increment_t)(avl_key_t *,avl_key_t);


/*
 *	Interface Routines
 *
 *	For each routine with a return value:
 *
 *	Returns 0 on normal completion.
 *	Returns -EAGAIN  if memory cannot be allocated.
 *	Returns -EWOULDBLOCK if avl_opt_nowait was specified and
 *	sleeping would be required.
 */

/*
 *	avl_create
 *
 *	Allocate an AVL tree object, to be returned in the 
 *	avl_handle_t variable passed by reference.
 *	The avl_comparison_t value may be NULL, in which case a 
 *	default (integer) comparison is used.
 */

extern int avl_create(avl_handle_t *,avl_opt_t,avl_comparison_t,avl_increment_t);

/*
 *	avl_destroy
 *
 *	Destroy (deallocate) an AVL tree object.
 */

extern void avl_destroy(avl_handle_t);

/*
 *	avl_insert
 *
 *	Insert the key-value pair into the give AVL tree.
 *
 *	Returns -EEXIST if there is already an entry for the key.
 */

extern int avl_insert(avl_handle_t,avl_key_t,avl_value_t);

/*
 *	avl_replace
 *
 *	Replace (or insert) the key-value pair into the give AVL tree.
 */

extern int avl_replace(avl_handle_t,avl_key_t,avl_value_t);

/*
 *	avl_delete
 *
 *	Delete the key-value pair from the give AVL tree.
 *
 *	Returns -ENOENT if the key is not present in the tree.
 *	Returns -EINVAL if the value supplied does not match the
 *	value stored in the tree.
 */

extern int avl_delete(avl_handle_t,avl_key_t,avl_value_t);

/*
 *	avl_lookup
 *
 *	Lookup the key in the give AVL tree, and return the
 *	associated value in the avl_value_t variable passed by
 *	reference.
 *
 *	Returns -ENOENT if the key is not present in the tree.
 */

extern int avl_lookup(avl_handle_t,avl_key_t,avl_value_t *);
 
/*
 *	avl_lookup_next
 *
 *	Lookup the first key equal to or greater than the key
 *	in the first avl_key_t variable passed by reference, returning
 *	the associated key and value in the remaining variables passed
 *	by reference, and update the first variable to be the next key value
 *	greater than the key of the entry found.
 *
 *	If the starting key is set to the lowest possible key value,
 *	this may be used to search for all entries in the tree in a loop.
 *
 *	Returns -ENOENT if no matching key is present in the tree.
 */

extern int avl_lookup_next(avl_handle_t,avl_key_t *,avl_key_t *,avl_value_t *);

extern int avl_init(void);
extern void avl_terminate(void);

#ifdef CONFIG_AVL_DEBUG
/*
 *	avl_check
 *
 *	Perform consistency check on an AVL tree.
 */

extern void avl_check(avl_handle_t,char *);

#endif /* CONFIG_AVL_DEBUG */

#endif /* __AVL_H__ */
