#
# Makefile for the linux pagebuf routines.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definitions are now in the main makefile...

# Ensure that none of the new files for XFS try to include files from outside
# the kernel.  These overrides are not required for kbuild 2.5.  KAO
EXTRA_CFLAGS += $(kbuild_2_2_4ostdinc)

# Debug options:
#	-DCONFIG_AVL_DEBUG
#	-DPAGEBUF_TRACKING
#	-DPAGEBUF_LOCKING
#	-DPAGEBUF_DEBUG
#	-DPAGEBUF_TRACE
ifeq ($(CONFIG_PAGEBUF_DEBUG),y)
	EXTRA_CFLAGS += -g -DPAGEBUF_DEBUG -DPAGEBUF_TRACKING -DSTATIC=""
endif

O_TARGET			:= pagebuf.o

ifneq ($(MAKECMDGOALS),modules_install)
  obj-m                         := $(O_TARGET)
endif

export-objs			+= page_buf.o page_buf_io.o page_buf_locking.o
obj-y				+= avl.o \
				   page_buf.o \
				   page_buf_io.o \
				   page_buf_locking.o

include $(TOPDIR)/Rules.make
