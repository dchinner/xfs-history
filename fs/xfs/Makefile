#
# Top-level Makefile for slinx
#

WORKAREA ?= `pwd`

ARCH = `uname -m`
ifeq ($(ARCH),"i686")
	ARCH = i386
endif
ifeq ($(ARCH),"586")
	ARCH = i386
endif
KERN_SPEC = kernel-2.4.spec
KERNEL_VERSION = 2.4.0

SGI_VERSION = `cat $(WORKAREA)/SPECS/_sgi_kvers_`

default all: checkproc tarballs i386kernel i586kernel i686kernel ia64kernel cmds

checkproc:
	if [ ! -e /proc/stat ]; then \
		mount /proc; \
	fi

tarballs: xfs-cmds
	@echo "=== Making $@ `date`"; \
	cd $(WORKAREA)/SCRIPTS; \
	./make-tarballs
	cp $(WORKAREA)/cmd/xfs/build/xfs-cmds-*.src.tar.gz $(WORKAREA)/SOURCES

i386kernel:
	if [[ "i386" == $(ARCH) ]]; then \
		@echo "=== Making $@ `date`"; \
		cd $(WORKAREA)/SPECS; \
		rpm -ba --target=i386 ${KERN_SPEC}; \
		echo "=== Done Making $@ `date`"; \
	fi

i586kernel:
	if [[ "i386" == $(ARCH) ]]; then \
		@echo "=== Making $@ `date`"; \
		cd $(WORKAREA)/SPECS; \
		rpm -ba --target=i586 ${KERN_SPEC}; \
		echo "=== Done Making $@ `date`"; \
	fi

i686kernel:
	if [[ "i386" == $(ARCH) ]]; then \
		@echo "=== Making $@ `date`"; \
		cd $(WORKAREA)/SPECS; \
		rpm -ba --target=i686 ${KERN_SPEC}; \
		echo "=== Done Making $@ `date`"; \
	fi

ia64kernel:
	if [[ "ia64" == $(ARCH) ]]; then \
		@echo "=== Making $@ `date`"; \
		cd $(WORKAREA)/SPECS; \
		rpm -ba --target=ia64 ${KERN_SPEC}; \
		echo "=== Done Making $@ `date`"; \
	fi

cmds: xfs-cmds
	[ ! -d $(WORKAREA)/SRPMS ] \
		&& mkdir $(WORKAREA)/SRPMS || exit 0
	cp $(WORKAREA)/cmd/xfs/build/rpm/xfs-cmds-*.src.rpm $(WORKAREA)/SRPMS
	[ ! -d $(WORKAREA)/RPMS/$(ARCH) ] \
		&& mkdir -p $(WORKAREA)/RPMS/$(ARCH) || exit 0
	cp $(WORKAREA)/cmd/xfs/build/rpm/xfs-cmds-*.$(ARCH).rpm \
			$(WORKAREA)/RPMS/$(ARCH)

xfs-cmds:
	echo "=== Making $@ `date`"
	cd $(WORKAREA)/cmd/xfs && ./Makepkgs verbose

clean:
	rm -rf RPMS SRPMS BUILD SOURCES/linux-${KERNEL_VERSION}-xfs.patch
	cd $(WORKAREA)/cmd/xfs && make clean

realclean: clean
	rm -rf linux-${KERNEL_VERSION}
	rm -f kern
	rm -f SOURCES/xfs-cmds-*.tar.gz
	if [ -e SPECS/${KERN_SPEC}.old ] ; then \
		rm SPECS/${KERN_SPEC}; \
		mv SPECS/${KERN_SPEC}.old SPECS/${KERN_SPEC}; \
	fi
	cd $(WORKAREA)/cmd/xfs && make realclean
