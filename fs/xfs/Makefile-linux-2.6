#
# Copyright (c) 2000 Silicon Graphics, Inc.  All Rights Reserved.
# 
# This program is free software; you can redistribute it and/or modify it
# under the terms of version 2 of the GNU General Public License as
# published by the Free Software Foundation.
# 
# This program is distributed in the hope that it would be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
# 
# Further, this software is distributed without any warranty that it is
# free of the rightful claim of any third person regarding infringement
# or the like.  Any license provided herein, whether implied or
# otherwise, applies only to this software file.  Patent licenses, if
# any, provided herein do not apply to combinations of this program with
# other software, or any other product whatsoever.
# 
# You should have received a copy of the GNU General Public License along
# with this program; if not, write the Free Software Foundation, Inc., 59
# Temple Place - Suite 330, Boston MA 02111-1307, USA.
# 
# Contact information: Silicon Graphics, Inc., 1600 Amphitheatre Pkwy,
# Mountain View, CA  94043, or:
# 
# http://www.sgi.com 
# 
# For further information regarding this notice, see: 
# 
# http://oss.sgi.com/projects/GenInfo/SGIGPLNoticeExplan/
#
# Makefile for XFS on Linux.
#

# Ensure that none of the new files for XFS try to include files from outside
# the kernel.  These overrides are not required for kbuild 2.5.  KAO
EXTRA_CFLAGS += $(kbuild_2_4_nostdinc)

# This needs -I. because everything does #include <xfs.h> instead of "xfs.h".
# The code is wrong, local files should be included using "xfs.h", not <xfs.h>
# but I am not going to change every file at the moment.
EXTRA_CFLAGS +=  -I. -funsigned-char

ifeq ($(CONFIG_XFS_DEBUG),y)
	EXTRA_CFLAGS += -g -DSTATIC="" -DDEBUG -DXFSDEBUG
endif

subdir-$(CONFIG_XFS_FS)		+= pagebuf linux

# fs/Makefile enters fs/xfs twice if CONFIG_XFS_FS is y, once for kernel and
# once for modules.  This is necessary because xfsidbg can be built as a module
# even if xfs is in kernel.  Alas the shorthand form
#   O_TARGET := xfs.o
#   obj-m := $(O_TARGET)
# fails when the makefile is run more than once, code gets compiled as both
# kernel and as module, which one gets linked depends on the phase of the moon.
# I just love these layer violations where a makefile behaves differently
# depending on changes to its parent.  Work around by only setting obj-m when
# xfs is selected as a module.  Keith Owens.

O_TARGET			:= xfs.o
ifeq ($(CONFIG_XFS_FS),m)
  obj-m				:= $(O_TARGET)
endif

ifeq ($(CONFIG_XFS_RT),)
  obj-y				+= xfsrtstubs.o
else
  obj-y				+= xfs_rtalloc.o xfs_rtbit.o
endif

ifneq ($(CONFIG_XFS_GRIO),)
  obj-y				+= xfs_grio.o
endif

ifeq ($(CONFIG_HAVE_XFS_DMAPI),)
  obj-y				+= xfsdmapistubs.o
else
  obj-y				+= xfs_dmapi.o
endif

ifeq ($(CONFIG_XFS_QUOTA),)
  obj-y				+= xfsquotasstubs.o
else
  obj-y				+= xfs_dquot.o xfs_trans_dquot.o xfs_dquot_item.o xfs_qm_syscalls.o xfs_qm.o
endif

ifneq ($(CONFIG_FS_POSIX_ACL),)
  obj-y				+= xfs_acl.o
endif

ifneq ($(CONFIG_FS_POSIX_MAC),)
  obj-y				+= xfs_mac.o
endif

obj-y				+= xfs_alloc.o \
				   xfs_alloc_btree.o \
				   xfs_attr.o \
				   xfs_attr_fetch.o \
				   xfs_attr_leaf.o \
				   xfs_bit.o \
				   xfs_bmap.o \
				   xfs_bmap_btree.o \
				   xfs_btree.o \
				   xfs_buf_item.o \
				   xfs_da_btree.o \
				   xfs_dir.o \
				   xfs_dir2.o \
				   xfs_dir2_block.o \
				   xfs_dir2_data.o \
				   xfs_dir2_leaf.o \
				   xfs_dir2_node.o \
				   xfs_dir2_sf.o \
				   xfs_dir2_trace.o \
				   xfs_dir_leaf.o \
				   xfs_error.o \
				   xfs_extfree_item.o \
				   xfs_fsops.o \
				   xfs_ialloc.o \
				   xfs_ialloc_btree.o \
				   xfs_iget.o \
				   xfs_inode.o \
				   xfs_inode_item.o \
				   xfs_iocore.o \
				   xfs_itable.o \
				   xfs_dfrag.o \
				   xfs_log.o \
				   xfs_log_recover.o \
				   xfs_macros.o \
				   xfs_mount.o \
				   xfs_rename.o \
				   xfs_trans.o \
				   xfs_trans_ail.o \
				   xfs_trans_buf.o \
				   xfs_trans_extfree.o \
				   xfs_trans_inode.o \
				   xfs_trans_item.o \
				   xfs_utils.o \
				   xfs_vfsops.o \
				   xfs_vnodeops.o \
				   xfs_rw.o

# Objects not built in this directory
obj-y				+= pagebuf/pagebuf.o linux/linux_xfs.o

# If both xfs and kdb modules are built in then xfsidbg is built in.  If xfs is
# a module and kdb modules are being compiled then xfsidbg must be a module, to
# follow xfs.  If xfs is built in then xfsidbg tracks the kdb module state.
# This must come after the main xfs code so xfs initialises before xfsidbg.
# KAO
ifneq ($(CONFIG_KDB_MODULES),)
  ifeq ($(CONFIG_XFS_FS),y)
    obj-$(CONFIG_KDB_MODULES)	+= xfsidbg.o
  else
    obj-$(CONFIG_XFS_FS)	+= xfsidbg.o
  endif
endif

CFLAGS_xfsidbg.o += -I $(TOPDIR)/arch/$(ARCH)/kdb

include $(TOPDIR)/Rules.make

# This is really nasty, but Rules.make was never designed for multi directory
# modules.  Keith Owens.

xfs.o: $(patsubst %,_modsubdir_%,$(subdir-m))
