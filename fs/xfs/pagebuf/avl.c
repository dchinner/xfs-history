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
 * Generic AVL module:  avl
 *
 * This module supports storing key-value pairs in an opaque
 * AVL tree.  Both the key and the value are pointer-sized integers.
 * The caller may provide an optional comparison function.  The
 * tree is kept separate from the objects to avoid locking problems
 * and cache thrashing with respect to the referenced objects on
 * larger SMP systems.
 *
 * Derived from mmap_avl.c, which was 
 * written by Bruno Haible <haible@ma2s2.mathematik.uni-karlsruhe.de>.
 *
 *
 * Written by William J. Earl at SGI
 */

#include <linux/config.h>
#include <linux/version.h>
#include <linux/module.h>
#include <linux/stddef.h>
#include <linux/errno.h>
#include <linux/spinlock.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/init.h>
#include "avl.h"

typedef struct avl_entry_struct {
	struct avl_entry_struct *avl_left;
	struct avl_entry_struct *avl_right;
	int		avl_height;
	avl_key_t	avl_key;
	avl_value_t	avl_value;
} avl_entry_t;

typedef struct avl_object_struct {
	avl_opt_t	avlo_options;
	avl_entry_t	*avlo_tree;
	avl_entry_t	*avlo_cache;
	spinlock_t	avlo_lock;
	avl_comparison_t avlo_compare;
	avl_increment_t avlo_increment;
} avl_object_t;

#define avl_empty ((avl_entry_t *) NULL)

#define avl_maxheight	100
#define heightof(tree)	(((tree) == avl_empty) ? 0 : (tree)->avl_height)

static kmem_cache_t *avl_entry_cache = NULL;
static kmem_cache_t *avl_object_cache = NULL;

/*
 * Consistency and balancing rules:
 * 1. tree->avl_height == 1+max(heightof(tree->avl_left),heightof(tree->avl_right))
 * 2. abs( heightof(tree->avl_left) - heightof(tree->avl_right) ) <= 1
 * 3. foreach node in tree->avl_left: node->avl_key <= tree->avl_key,
 *    foreach node in tree->avl_right: node->avl_key >= tree->avl_key.
 */

/*
 *	avl_compare
 *
 *	This is the default key comparison function.
 */

static int
avl_compare(avl_key_t a,
	    avl_key_t b)
{
	if (a == b)
		return(0);
	else if (a < b)
		return(-1);
	else
		return(1);
}


#ifdef CONFIG_AVL_DEBUG

#ifdef notyet
/* Look up the nodes at the left and at the right of a given node. */
static void 
avl_neighbours (avl_entry_t * node, 
		avl_entry_t * tree, 
		avl_entry_t ** to_the_left, 
		avl_entry_t ** to_the_right)
{
	avl_key_t key = node->avl_key;

	*to_the_left = *to_the_right = NULL;
	for (;;) {
		if (tree == avl_empty) {
			printk("avl_neighbours: node not found in the tree\n");
			return;
		}
		if (key == tree->avl_key)
			break;
		if (key < tree->avl_key) {
			*to_the_right = tree;
			tree = tree->avl_left;
		} else {
			*to_the_left = tree;
			tree = tree->avl_right;
		}
	}
	if (tree != node) {
		printk("avl_neighbours: node not exactly found in the tree\n");
		return;
	}
	if (tree->avl_left != avl_empty) {
		avl_entry_t * node;
		for (node = tree->avl_left; 
		     node->avl_right != avl_empty; 
		     node = node->avl_right)
			continue;
		*to_the_left = node;
	}
	if (tree->avl_right != avl_empty) {
		avl_entry_t * node;
		for (node = tree->avl_right; 
		     node->avl_left != avl_empty; 
		     node = node->avl_left)
			continue;
		*to_the_right = node;
	}
}
#endif /* notyet */

#endif /* CONFIG_AVL_DEBUG */

/*
 * Rebalance a tree.
 * After inserting or deleting a node of a tree we have a sequence of subtrees
 * nodes[0]..nodes[k-1] such that
 * nodes[0] is the root and nodes[i+1] = nodes[i]->{avl_left|avl_right}.
 */
static void 
avl_rebalance (avl_entry_t *** nodeplaces_ptr, 
	       int count)
{
	for ( ; count > 0 ; count--) {
		avl_entry_t ** nodeplace = *--nodeplaces_ptr;
		avl_entry_t * node = *nodeplace;
		avl_entry_t * nodeleft = node->avl_left;
		avl_entry_t * noderight = node->avl_right;
		int heightleft = heightof(nodeleft);
		int heightright = heightof(noderight);
		if (heightright + 1 < heightleft) {
	/*                                                      */
	/*                            *                         */
	/*                          /   \                       */
	/*                       n+2      n                     */
	/*                                                      */
			avl_entry_t * nodeleftleft = nodeleft->avl_left;
			avl_entry_t * nodeleftright = nodeleft->avl_right;
			int heightleftright = heightof(nodeleftright);
			if (heightof(nodeleftleft) >= heightleftright) {
	/*                                                        */
	/*                *                    n+2|n+3            */
	/*              /   \                  /    \             */
	/*           n+2      n      -->      /   n+1|n+2         */
	/*           / \                      |    /    \         */
	/*         n+1 n|n+1                 n+1  n|n+1  n        */
	/*                                                        */
				node->avl_left = nodeleftright; 
				nodeleft->avl_right = node;
				nodeleft->avl_height = 1 + (node->avl_height = 
							    (1 + heightleftright));
				*nodeplace = nodeleft;
			} else {
	/*                                                        */
	/*                *                     n+2               */
	/*              /   \                 /     \             */
	/*           n+2      n      -->    n+1     n+1           */
	/*           / \                    / \     / \           */
	/*          n  n+1                 n   L   R   n          */
	/*             / \                                        */
	/*            L   R                                       */
	/*                                                        */
				nodeleft->avl_right = nodeleftright->avl_left;
				node->avl_left = nodeleftright->avl_right;
				nodeleftright->avl_left = nodeleft;
				nodeleftright->avl_right = node;
				nodeleft->avl_height = 
					(node->avl_height = heightleftright);
				nodeleftright->avl_height = heightleft;
				*nodeplace = nodeleftright;
			}
		}
		else if (heightleft + 1 < heightright) {
			/* similar to the above, just interchange 'left' <--> 'right' */
			avl_entry_t * noderightright = noderight->avl_right;
			avl_entry_t * noderightleft = noderight->avl_left;
			int heightrightleft = heightof(noderightleft);
			if (heightof(noderightright) >= heightrightleft) {
				node->avl_right = noderightleft; 
				noderight->avl_left = node;
				noderight->avl_height = 1 + (node->avl_height = 
							     (1 + heightrightleft));
				*nodeplace = noderight;
			} else {
				noderight->avl_left = noderightleft->avl_right;
				node->avl_right = noderightleft->avl_left;
				noderightleft->avl_right = noderight;
				noderightleft->avl_left = node;
				noderight->avl_height = 
					(node->avl_height = heightrightleft);
				noderightleft->avl_height = heightright;
				*nodeplace = noderightleft;
			}
		}
		else {
			int height = (heightleft<heightright 
				      ? heightright
				      : heightleft) + 1;

			if (height == node->avl_height)
				break;
			node->avl_height = height;
		}
	}
}

/* Insert a node into a tree. */
static inline void 
avl_insert_entry (avl_entry_t * new_node, 
		  avl_object_t * avl)
{
	avl_key_t key = new_node->avl_key;
	avl_entry_t ** nodeplace = &avl->avlo_tree;
	avl_entry_t ** stack[avl_maxheight];
	int stack_count = 0;
	int	cval;
	avl_entry_t *** stack_ptr = &stack[0]; /* = &stack[stackcount] */

	for (;;) {
		avl_entry_t * node = *nodeplace;
		if (node == avl_empty)
			break;
		*stack_ptr++ = nodeplace; stack_count++;
		cval = avl->avlo_compare(node->avl_key, key);
		if (cval > 0)
			nodeplace = &node->avl_left;
		else
			nodeplace = &node->avl_right;
	}
	new_node->avl_left = avl_empty;
	new_node->avl_right = avl_empty;
	new_node->avl_height = 1;
	*nodeplace = new_node;
	avl_rebalance(stack_ptr,stack_count);
}


#if NOTDEF
/* Insert a node into a tree, and
 * return the node to the left of it and the node to the right of it.
 */
static inline void 
avl_insert_neighbours (avl_entry_t * new_node, 
		       avl_entry_t ** ptree,
		       avl_entry_t ** to_the_left, 
		       avl_entry_t ** to_the_right)
{
	avl_key_t key = new_node->avl_key;
	avl_entry_t ** nodeplace = ptree;
	avl_entry_t ** stack[avl_maxheight];
	int stack_count = 0;
	avl_entry_t *** stack_ptr = &stack[0]; /* = &stack[stackcount] */
	*to_the_left = *to_the_right = NULL;

	for (;;) {
		avl_entry_t * node = *nodeplace;
		if (node == avl_empty)
			break;
		*stack_ptr++ = nodeplace; stack_count++;
		if (key < node->avl_key) {
			*to_the_right = node;
			nodeplace = &node->avl_left;
		} else {
			*to_the_left = node;
			nodeplace = &node->avl_right;
		}
	}
	new_node->avl_left = avl_empty;
	new_node->avl_right = avl_empty;
	new_node->avl_height = 1;
	*nodeplace = new_node;
	avl_rebalance(stack_ptr,stack_count);
}
#endif

/* Removes a node out of a tree. */
static void 
avl_remove (avl_entry_t * node_to_delete, 
	    avl_object_t * avl)
{
	avl_key_t key = node_to_delete->avl_key;
	avl_entry_t ** nodeplace = &avl->avlo_tree;
	avl_entry_t ** stack[avl_maxheight];
	int stack_count = 0;
	int	cval;
	avl_entry_t *** stack_ptr = &stack[0]; /* = &stack[stackcount] */
	avl_entry_t ** nodeplace_to_delete;

	for (;;) {
		avl_entry_t * node = *nodeplace;
#ifdef CONFIG_AVL_DEBUG
		if (node == avl_empty) {
			/* what? node_to_delete not found in tree? */
			printk("avl_remove: node to delete not found in tree\n");
			return;
		}
#endif /* CONFIG_AVL_DEBUG */
		*stack_ptr++ = nodeplace; stack_count++;
		cval = avl->avlo_compare(node->avl_key, key);
		if (cval == 0)
			break;
		if (cval > 0)
			nodeplace = &node->avl_left;
		else
			nodeplace = &node->avl_right;
	}
	nodeplace_to_delete = nodeplace;
	/* Have to remove node_to_delete = *nodeplace_to_delete. */
	if (node_to_delete->avl_left == avl_empty) {
		*nodeplace_to_delete = node_to_delete->avl_right;
		stack_ptr--; stack_count--;
	} else {
		avl_entry_t *** stack_ptr_to_delete = stack_ptr;
		avl_entry_t ** nodeplace = &node_to_delete->avl_left;
		avl_entry_t * node;
		for (;;) {
			node = *nodeplace;
			if (node->avl_right == avl_empty)
				break;
			*stack_ptr++ = nodeplace; stack_count++;
			nodeplace = &node->avl_right;
		}
		*nodeplace = node->avl_left;
		/* node replaces node_to_delete */
		node->avl_left = node_to_delete->avl_left;
		node->avl_right = node_to_delete->avl_right;
		node->avl_height = node_to_delete->avl_height;
		*nodeplace_to_delete = node; /* replace node_to_delete */
		/* replace &node_to_delete->avl_left */
		*stack_ptr_to_delete = &node->avl_left; 
	}
	avl_rebalance(stack_ptr,stack_count);
}

#ifdef CONFIG_AVL_DEBUG

/* print a tree */
static void 
avl_printk_avl (avl_entry_t * entry)
{
	if (entry != avl_empty) {
		printk("(");
		if (entry->avl_left != avl_empty) {
			avl_printk_avl(entry->avl_left);
			printk("<");
		}
		printk("0x%08lX=0x%08lX", 
		       (long) entry->avl_key, 
		       (long) entry->avl_value);
		if (entry->avl_right != avl_empty) {
			printk(">");
			avl_printk_avl(entry->avl_right);
		}
		printk(")");
	}
}

/* check a tree's consistency and balancing */
static void 
avl_checkheights (avl_object_t *avl,
		  char *avl_check_point,
		  avl_entry_t * tree)
{
	int h, hl, hr;

	if (tree == avl_empty)
		return;
	avl_checkheights(avl,avl_check_point,tree->avl_left);
	avl_checkheights(avl,avl_check_point,tree->avl_right);
	h = tree->avl_height;
	hl = heightof(tree->avl_left);
	hr = heightof(tree->avl_right);
	if ((h == hl+1) && (hr <= hl) && (hl <= hr+1))
		return;
	if ((h == hr+1) && (hl <= hr) && (hr <= hl+1))
		return;
	printk("%s: avl_checkheights: heights inconsistent\n",avl_check_point);
}

/* check that all values stored in a tree are < key */
static void 
avl_checkleft (avl_object_t *avl,
	       char *avl_check_point,
	       avl_entry_t * tree, 
	       avl_key_t key)
{
	if (tree == avl_empty)
		return;
	avl_checkleft(avl,avl_check_point,tree->avl_left,key);
	avl_checkleft(avl,avl_check_point,tree->avl_right,key);
	if (avl->avlo_compare(tree->avl_key,key) < 0)
		return;
	printk("%s: avl_checkleft: left key 0x%08lX >= top key 0x%08lX\n",
	       avl_check_point,
	       (long) tree->avl_key,
	       (long) key);
}

/* check that all values stored in a tree are > key */
static void 
avl_checkright (avl_object_t *avl,
		char *avl_check_point,
		avl_entry_t * tree, 
		avl_key_t key)
{
	if (tree == avl_empty)
		return;
	avl_checkright(avl,avl_check_point,tree->avl_left,key);
	avl_checkright(avl,avl_check_point,tree->avl_right,key);
	if (avl->avlo_compare(tree->avl_key,key) > 0)
		return;
	printk("%s: avl_checkright: right key 0x%08lX <= top key 0x%08lX\n",
	       avl_check_point,
	       (long) tree->avl_key,
	       (long) key);
}

/* check that all values are properly increasing */
static void 
avl_checkorder (avl_object_t *avl,
		char *avl_check_point,
		avl_entry_t * tree)
{
	if (tree == avl_empty)
		return;
	avl_checkorder(avl,avl_check_point,tree->avl_left);
	avl_checkorder(avl,avl_check_point,tree->avl_right);
	avl_checkleft(avl,avl_check_point,tree->avl_left,tree->avl_key);
	avl_checkright(avl,avl_check_point,tree->avl_right,tree->avl_key);
}

/* all checks */
void 
avl_check (avl_handle_t handle,
	   char *avl_check_point)
{
	avl_object_t *avl = (avl_object_t *) handle;

	if (! debug)
		return;
#ifdef nolonger
	printk("avl 0x%08lX, %s tree:\n",(long) handle,avl_check_point);
	avl_printk_avl(avl->avlo_tree); 
	printk("\n"); 
#endif /* nolonger */
	avl_checkheights(avl,avl_check_point,avl->avlo_tree);
	avl_checkorder(avl,avl_check_point,avl->avlo_tree);
}

#endif /* CONFIG_AVL_DEBUG */

/*
 *	avl_init
 *
 *	Initialize avl module.
 */

int __init avl_init(void)
{
	if (avl_entry_cache == NULL) {
		avl_entry_cache = kmem_cache_create("avl_entry_t",
						    sizeof(avl_entry_t),
						    0,
						    SLAB_HWCACHE_ALIGN,
						    NULL,
						    NULL);
		if (avl_entry_cache == NULL) 
			return(-ENOMEM);
	}
	if (avl_object_cache == NULL) {
		avl_object_cache = kmem_cache_create("avl_object_t",
						     sizeof(avl_object_t),
						     0,
						     SLAB_HWCACHE_ALIGN,
						     NULL,
						     NULL);
		if (avl_object_cache == NULL) {
			kmem_cache_shrink(avl_entry_cache);
			return(-ENOMEM);
		}
	}
	return(0);
}

/*
 *	avl_terminate.  Do not mark as __exit, it is called from
 *	pagebuf_terminate.
 */

void avl_terminate(void)
{
	if (avl_object_cache != NULL)
		kmem_cache_destroy(avl_object_cache);
	if (avl_entry_cache != NULL)
		kmem_cache_destroy(avl_entry_cache);
}

/*
 *	avl_create
 *
 *	Allocate an AVL tree object, to be returned in the 
 *	avl_handle_t variable passed by reference.
 *	The avl_comparison_t value may be NULL, in which case a 
 *	default (integer) comparison is used.
 */

int
avl_create(avl_handle_t *handle_p,
	   avl_opt_t options,
	   avl_comparison_t cmpfn,
	   avl_increment_t incfn)
{
	avl_object_t *avl;

	if (cmpfn == NULL)
		cmpfn = avl_compare;

	avl = kmem_cache_alloc(avl_object_cache,SLAB_KERNEL);
	if (avl == NULL)
		return(-ENOMEM);
	memset(avl,0,sizeof(avl_object_t));
	avl->avlo_options = options;
	spin_lock_init(&avl->avlo_lock);
	avl->avlo_compare = cmpfn;
	avl->avlo_increment = incfn;
	*handle_p = (avl_handle_t) avl;
	return(0);
}

/*
 *	avl_destroy
 *
 *	Destroy (deallocate) an AVL tree object.
 */

void 
avl_destroy(avl_handle_t handle)
{
        avl_object_t *avl = (avl_object_t *) handle;

	while (avl->avlo_tree != NULL) {
		(void) avl_delete(handle,
				  avl->avlo_tree->avl_key,
				  avl->avlo_tree->avl_value);
	}
	kmem_cache_free(avl_object_cache,avl);
}

/*
 *	avl_insert
 *
 *	Insert the key-value pair into the give AVL tree.
 *
 *	Returns -EEXIST if there is already an entry for the key.
 */

int
avl_insert(avl_handle_t handle,
	   avl_key_t key,
	   avl_value_t value)
{
        avl_object_t *avl = (avl_object_t *) handle;
	avl_entry_t *entry;
	avl_value_t old_value;

	if (! avl_lookup(handle,key,&old_value))
		return(-EEXIST);
	
	/* We have a spinlock - do not sleep */
	entry = kmem_cache_alloc(avl_entry_cache,SLAB_ATOMIC);
	if (entry == NULL)
		return(-ENOMEM);
	entry->avl_key = key;
	entry->avl_value = value;
	avl_insert_entry(entry, avl);
	avl->avlo_cache = entry;
	return(0);
}

/*
 *	avl_replace
 *
 *	Replace (or insert) the key-value pair into the give AVL tree.
 */

int
avl_replace(avl_handle_t handle,
	    avl_key_t key,
	    avl_value_t value)
{
	avl_object_t *avl = (avl_object_t *) handle;
	avl_entry_t *entry;
	int	cval;

	entry = avl->avlo_cache;
	if (entry == NULL ||
	    entry->avl_key != key) {
		entry = avl->avlo_tree;
		for (;;) {
			if (entry == avl_empty)
				return(avl_insert(handle,key,value));
			cval = avl->avlo_compare(entry->avl_key,key);
			if (cval == 0)
				break;
			if (cval > 0)
				entry = entry->avl_left;
			else
				entry = entry->avl_right;
		}
		avl->avlo_cache = entry;
	}
	entry->avl_value = value;
	return(0);
}

/*
 *	avl_delete
 *
 *	Delete the key-value pair from the give AVL tree.
 *
 *	Returns -ENOENT if the key is not present in the tree.
 *	Returns -EINVAL if the value supplied does not match the
 *	value stored in the tree.
 */

int
avl_delete(avl_handle_t handle,
	   avl_key_t key,
	   avl_value_t value)
{
	avl_object_t *avl = (avl_object_t *) handle;
	avl_entry_t *entry;
	int	cval;

	entry = avl->avlo_cache;
	if (entry == NULL ||
	    entry->avl_key != key) {
		entry = avl->avlo_tree;
		for (;;) {
			if (entry == avl_empty)
				return(-ENOENT);
			cval = avl->avlo_compare(entry->avl_key,key);
			if (cval == 0)
				break;
			if (cval > 0)
				entry = entry->avl_left;
			else
				entry = entry->avl_right;
		}
		avl->avlo_cache = entry;
	}
	if (value != entry->avl_value) 
		return(-EINVAL);
	avl->avlo_cache = NULL;
	avl_remove(entry, avl);
	kmem_cache_free(avl_entry_cache,entry);
	return(0);
	
}

/*
 *	avl_lookup
 *
 *	Lookup the key in the give AVL tree, and return the
 *	associated value in the avl_value_t variable passed by
 *	reference.
 *
 *	Returns -ENOENT if the key is not present in the tree.
 */

int
avl_lookup(avl_handle_t handle,
	   avl_key_t key,
	   avl_value_t *value)
{
	avl_object_t *avl = (avl_object_t *) handle;
	avl_entry_t *entry;
	int	cval;

	entry = avl->avlo_cache;
	if (entry == NULL ||
	    entry->avl_key != key) {
		entry = avl->avlo_tree;
		for (;;) {
			if (entry == avl_empty) {
				*value = (avl_value_t)0;
				return(-ENOENT);
			}
			cval = avl->avlo_compare(entry->avl_key,key);
			if (cval == 0)
				break;
			if (cval > 0)
				entry = entry->avl_left;
			else
				entry = entry->avl_right;
		}
		avl->avlo_cache = entry;
	}
	*value = entry->avl_value;
	return(0);
} 

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

int
avl_lookup_next(avl_handle_t handle,
		avl_key_t *next_key,
		avl_key_t *key,
		avl_value_t *value)
{
	avl_object_t *avl = (avl_object_t *) handle;
	avl_entry_t *entry;
	int	cval;

	entry = avl->avlo_cache;
	if (entry == NULL ||
	    entry->avl_key != *next_key) {
		entry = avl->avlo_tree;
		for (;;) {
			if (entry == avl_empty)
				return(-ENOENT);
			cval = avl->avlo_compare(entry->avl_key,*next_key);
			if (cval == 0)
				break;
			if (cval > 0) {
				if (entry->avl_left == NULL)
					break;
				entry = entry->avl_left;
			} else
				entry = entry->avl_right;
		}
		avl->avlo_cache = entry;
	}
	*key = entry->avl_key;
	*value = entry->avl_value;
	if (avl->avlo_increment != NULL) 
		avl->avlo_increment(next_key,entry->avl_key);
	else
		*next_key = (*key) + 1;

	return(0);
}

