#
# devcon.ko
# This builds the devcon.ko kernel module. It must be invoked via the kbuild
# system and will build devcon.ko. Anything below ./drivers/devcon/ can be
# diffed against the upstream kernel tree to get patches that apply to both,
# upstream and this downstream out-of-tree repository.
#

KERNELVER		?= $(shell uname -r)
KERNELDIR 		?= /lib/modules/$(KERNELVER)/build
PWD			:= $(shell pwd)

module:
	$(MAKE) -C $(KERNELDIR) M=$(PWD)/devcon CONFIG_DEVCON=m

clean:
	rm -f devcon/*.{cmd,ko,mod.c,o} devcon/.*.cmd
	rm -f devcon/{modules.order,Module.symvers}
	rm -rf devcon/.tmp_versions/

install: module
	mkdir -p /lib/modules/$(KERNELVER)/kernel/drivers/devcon/
	cp -f devcon/devcon.ko /lib/modules/$(KERNELVER)/kernel/drivers/devcon/
	depmod $(KERNELVER)

uninstall:
	rm -f /lib/modules/$(KERNELVER)/kernel/drivers/devcon/devcon.ko

tt-prepare: module
	-sudo sh -c 'dmesg -c > /dev/null'
	-sudo sh -c 'rmmod devcon'
	sudo sh -c 'insmod devcon/devcon.ko'

tt: tt-prepare
	echo ; (R=$$? ; dmesg ; exit $$R)

.PHONY: module clean install uninstall tt-prepare tt
