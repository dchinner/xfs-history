#
# Top-level Makefile for slinx
#

WORKAREA ?= `pwd`

ARCH := $(shell uname -m | sed -e s/i.86/i386/)
KERN_SPEC = kernel-2.4.spec
KERNEL_VERSION = 2.4.2

SGI_VERSION = `cat $(WORKAREA)/SPECS/_sgi_kvers_`

COMMANDS =	cmd/acl cmd/attr cmd/xfsprogs cmd/dmapi cmd/xfsdump
COMMANDRPMS =	attr attr-devel acl acl-devel dmapi dmapi-devel \
		xfsprogs xfsprogs-devel xfsdump

default all: checkproc tarballs kernel cmds quota

checkproc:
	if [ ! -e /proc/stat ]; then \
		mount /proc; \
	fi

tarballs: cmds
	@echo "=== Making $@ `date`"; \
	cd $(WORKAREA)/SCRIPTS; \
	./make-tarballs; \
	for d in $(COMMANDS); do \
		cp $(WORKAREA)/$$d/build/*.src.tar.gz $(WORKAREA)/SOURCES; \
	done

kernel:
	@echo "=== Making $@ `date`";
	rpm -ba --target=$(ARCH) $(WORKAREA)/SPECS/${KERN_SPEC};
ifeq ($(ARCH),i386)
	rpm -ba --target=i586 $(WORKAREA)/SPECS/${KERN_SPEC};
	rpm -ba --target=i686 $(WORKAREA)/SPECS/${KERN_SPEC};
	rpm -ba $(WORKAREA)/SPECS/kernel-pcmcia-cs.spec;
endif
	echo "=== Done Making $@ `date`";

cmds: builddirs
	echo "=== Making $@ `date`"
	for d in $(COMMANDS); do \
		( cd $(WORKAREA)/$$d && ./Makepkgs verbose ) || exit 1; \
		( rpm -Uh $(WORKAREA)/$$d/build/rpm/*.$(ARCH).rpm ) || exit 1; \
	done
	for d in $(COMMANDS); do \
		( cd $(WORKAREA) && cp $$d/build/rpm/*.src.rpm SRPMS ) \
	done
	for d in $(COMMANDS); do \
		( cd $(WORKAREA) && cp $$d/build/rpm/*.$(ARCH).rpm RPMS/$(ARCH) ) \
	done

quota: builddirs
	@echo "=== Making $@ `date`";
	rpm -ba --target=$(ARCH) $(WORKAREA)/SPECS/quota.spec;

builddirs:
	[ -d $(WORKAREA)/BUILD ] || mkdir $(WORKAREA)/BUILD
	[ -d $(WORKAREA)/SRPMS ] || mkdir $(WORKAREA)/SRPMS
	[ -d $(WORKAREA)/RPMS/$(ARCH) ] || mkdir -p $(WORKAREA)/RPMS/$(ARCH)

clean:
	rm -rf RPMS SRPMS BUILD SOURCES/linux-${KERNEL_VERSION}-xfs.patch
	for d in $(COMMANDS); do \
		( cd $(WORKAREA)/$$d && make -i clean ) \
	done
	for d in $(COMMANDRPMS); do \
		( rpm -e --nodeps --noscripts $$d >/dev/null 2>&1 || exit 0 ) \
	done

realclean: clean
	rm -rf linux-${KERNEL_VERSION}
	rm -f kern
	rm -f SOURCES/linux-${KERNEL_VERSION}.tar.bz2
	if [ -e SPECS/${KERN_SPEC}.old ] ; then \
		rm SPECS/${KERN_SPEC}; \
		mv SPECS/${KERN_SPEC}.old SPECS/${KERN_SPEC}; \
	fi
	for d in $(COMMANDRPMS); do rm -f SOURCES/$$d-*; done
	for d in $(COMMANDS); do ( cd $(WORKAREA)/$$d && make realclean ) done
