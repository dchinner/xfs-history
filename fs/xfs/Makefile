#
# Top-level Makefile for slinx
#

KERN_SPECS = kernel-2.4.spec

SGI_VERSION = `cat $(WORKAREA)/SPECS/_sgi_kvers_`

default all: tarballs headers i686kernel i386cmd

tarballs: xfs-cmds
	@echo "=== Making $@ `date`"; \
	cd $(WORKAREA)/SCRIPTS; \
	./make-tarballs
	cp $(WORKAREA)/cmd/xfs/build/xfs-cmds-*.src.tar.gz $(WORKAREA)/SOURCES

i386kernel:
	@echo "=== Making $@ `date`"; \
	cd $(WORKAREA)/SPECS; \
	rpm -ba --target i386 ${KERN_SPECS}; \
	echo "=== Done Making $@ `date`"

i686kernel:
	@echo "=== Making $@ `date`"; \
	cd $(WORKAREA)/SPECS; \
	rpm -ba --target i686 ${KERN_SPECS}; \
	echo "=== Done Making $@ `date`"

headers:i386kernel
	if [ -f `ls $(WORKAREA)/RPMS/i386/kernel-headers*.rpm` ] ; then \
		cd $(WORKAREA); \
		for header in $(WORKAREA)/RPMS/i386/kernel-headers*.rpm; do \
			echo "=== Installing $$header ==="; \
			rpm -Uh --force --nodeps --root / $$header; \
		done; \
	fi

i386cmd: xfs-cmds
	[ ! -d $(WORKAREA)/SRPMS ] \
		&& mkdir $(WORKAREA)/SRPMS || exit 0
	cp $(WORKAREA)/cmd/xfs/build/rpm/xfs-cmds-*.src.rpm $(WORKAREA)/SRPMS
	[ ! -d $(WORKAREA)/RPMS/i386 ] \
		&& mkdir -p $(WORKAREA)/RPMS/i386 || exit 0
	cp $(WORKAREA)/cmd/xfs/build/rpm/xfs-cmds-*.i386.rpm \
			$(WORKAREA)/RPMS/i386

xfs-cmds:
	echo "=== Making $@ `date`"
	[ ! -L $(WORKAREA)/linux/include/asm ] && \
		ln -s $(WORKAREA)/linux/include/asm-i386 \
			$(WORKAREA)/linux/include/asm || exit 0
	cd $(WORKAREA)/cmd/xfs && ./Makepkgs verbose
	rm -f $(WORKAREA)/linux/include/asm

clean:
	rm -rf RPMS SRPMS BUILD SOURCES/linux-2.4.0-test5-xfs-alpha.patch

realclean: clean
	rm -rf linux-2.4.0
	rm -f kern
	rm -f SPECS/_sgi_kvers_
	rm -f SOURCES/linux-2.4.0.tar.bz2 SOURCES/xfs-cmds-*.tar.gz
	if [ -e SPECS/kernel-2.4.spec.old ] ; then \
		rm SPECS/kernel-2.4.spec; \
		mv SPECS/kernel-2.4.spec.old SPECS/kernel-2.4.spec; \
	fi
	rm -f $(WORKAREA)/linux/include/asm
	cd $(WORKAREA)/cmd/xfs && make clean

