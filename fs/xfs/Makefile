#
# Top-level Makefile for slinx
#

KERN_SPECS = kernel-2.4.0.spec
LIB_SPECS  = 
#libdba.spec
CMD_SPECS = xfs-cmds.spec
#kernprof.spec ktrace.spec lockstat.spec sard.spec

SGI_VERSION = `cat $(WORKAREA)/SPECS/_sgi_kvers_`

default all: tarballs headers i686kernel i386cmd

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

i386lib:
	cd $(WORKAREA)/SPECS; \
	for  spec in ${LIB_SPECS}; do \
		echo "=== Making $@ `date`"; \
		mv $(WORKAREA)/SPECS/$$spec $(WORKAREA)/SPECS/$$spec.old; \
		$(WORKAREA)/SCRIPTS/setvers.pl -s $(WORKAREA)/SPECS/$$spec.old > $(WORKAREA)/SPECS/$$spec; \
		rpm -ba --target i386 $$spec; \
	done 
		
exports: i386lib
	cd $(WORKAREA); \
	for lib in $(WORKAREA)/RPMS/i386/lib*.rpm; do \
		echo "=== Installing $$lib ==="; \
		rpm -Uh --force --nodeps --root / $$lib; \
	done

i386cmd:
	cd $(WORKAREA)/SPECS; \
	for spec in ${CMD_SPECS}; do \
		echo "=== Making $@ `date`"; \
		mv $(WORKAREA)/SPECS/$$spec $(WORKAREA)/SPECS/$$spec.old; \
		$(WORKAREA)/SCRIPTS/setvers.pl -s $(WORKAREA)/SPECS/$$spec.old > $(WORKAREA)/SPECS/$$spec; \
		rpm -ba --target i386 $$spec; \
	done

clean:
	rm -rf RPMS SRPMS BUILD

realclean: clean
	rm -rf linux-2.4.0
	rm kern
	rm SPECS/_sgi_kvers_
	if [ -e SPECS/kernel-2.4.0.spec.old ] ; then \
		rm SPECS/kernel-2.4.0.spec; \
		mv SPECS/kernel-2.4.0.spec.old SPECS/kernel-2.4.0.spec; \
	fi
