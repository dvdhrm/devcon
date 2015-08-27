#
# Out-of-tree Developer Console
# This makefile builds the out-of-tree devcon module and all complementary
# elements, including samples and documentation provided alongside the module.
#

all: module

module:
	$(MAKE) -C drivers/ module

clean:
	$(MAKE) -C drivers/ clean

install:
	$(MAKE) -C drivers/ install

uninstall:
	$(MAKE) -C drivers/ uninstall

tt:
	$(MAKE) -C drivers/ tt

.PHONY: all module clean install uninstall tt
