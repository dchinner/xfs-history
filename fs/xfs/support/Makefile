#
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
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#

EXTRA_INCLUDE_DIRECTORIES = -I..
EXTRA_CFLAGS += $(EXTRA_INCLUDE_DIRECTORIES)

ifeq ($(CONFIG_XFS_DEBUG),y)
EXTRA_CFLAGS += -DDEBUG -DXFSDEBUG
endif

obj-y :=
obj-m :=
obj-n :=
obj-  :=

# Kludge, 2.4.0-test13-pre3 Makefiles are still not quite right.  Keith Owens.
ifeq ($(CONFIG_XFS_SUPPORT),y)
  O_TARGET			:= xfs_support.o
else
  list-multi			:= xfs_support.o
  obj-$(CONFIG_XFS_SUPPORT)	:= xfs_support.o
endif

export-objs			:= support.o qsort.o kmem.o ktrace.o debug.o \
				   mrlock.o sv.o mutex.o move.o uuid.o

xfs_support-objs		:= support.o qsort.o kmem.o ktrace.o debug.o \
				   mrlock.o sv.o mutex.o move.o uuid.o

# Kludge, 2.4.0-test13-pre3 Makefiles are still not quite right.  Keith Owens.
ifeq ($(CONFIG_XFS_SUPPORT),y)
  obj-y				:= $(xfs_support-objs)
else
  ifeq ($(CONFIG_XFS_SUPPORT)$(MAKING_MODULES),m1)
  obj-m				+= $(xfs_support-objs)
xfs_support.o: $(xfs_support-objs)
	$(LD) -r -o $@ $(xfs_support-objs)
  endif
endif

include $(TOPDIR)/Rules.make
