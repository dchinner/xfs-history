#
# Top-level Makefile for slinx
#

KERN_SPECS = kernel-2.4.0.spec
CMD_SPECS = 
#xfs-cmds.spec

SGI_VERSION = `cat $(WORKAREA)/SPECS/_sgi_kvers_`

default all: tarballs headers i686kernel
# i386cmd

tarballs:
	@echo "=== Making $@ `date`"; \
	cd $(WORKAREA)/SCRIPTS; \
	./make-tarballs

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

i386cmd:
	cd $(WORKAREA)/SPECS; \
	for spec in ${CMD_SPECS}; do \
		echo "=== Making $@ `date`"; \
		mv $(WORKAREA)/SPECS/$$spec $(WORKAREA)/SPECS/$$spec.old; \
		$(WORKAREA)/SCRIPTS/setvers.pl -s $(WORKAREA)/SPECS/$$spec.old > $(WORKAREA)/SPECS/$$spec; \
		rpm -ba --target i386 $$spec; \
	done

clean:
	rm -rf RPMS SRPMS BUILD SOURCES/linux-2.4.0-test5-xfs-alpha.patch

realclean: clean
	rm -rf linux-2.4.0
	rm -f kern
	rm -f SPECS/_sgi_kvers_
	rm -f SOURCES/linux-2.4.0.tar.bz2 SOURCES/pcmcia-cs-3.1.8.tar.gz SOURCES/ibcs-2.1-981105.tar.gz
	if [ -e SPECS/kernel-2.4.0.spec.old ] ; then \
		rm SPECS/kernel-2.4.0.spec; \
		mv SPECS/kernel-2.4.0.spec.old SPECS/kernel-2.4.0.spec; \
	fi
