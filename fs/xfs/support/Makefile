#
# Copyright (c) 2000-2002 Silicon Graphics, Inc.
# All Rights Reserved.
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License as
# published by the Free Software Foundation.
#
# This program is distributed in the hope that it would be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write the Free Software Foundation,
# Inc.,  51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
#

EXTRA_CFLAGS += -I.. -I $(TOPDIR)/fs/xfs/linux-2.4

ifeq ($(CONFIG_XFS_DEBUG),y)
	EXTRA_CFLAGS += -DDEBUG
endif

O_TARGET			:= support_xfs.o
ifneq ($(MAKECMDGOALS),modules_install)
  obj-m				:= $(O_TARGET)
endif

obj-y				:= debug.o \
				   move.o \
				   qsort.o \
				   uuid.o

obj-$(CONFIG_XFS_TRACE)		+= ktrace.o

include $(TOPDIR)/Rules.make
