# SPDX-License-Identifier: GPL-2.0-only

obj-m := elastio-snap.o

KERNELVERSION ?= $(shell uname -r)
KDIR := /lib/modules/$(KERNELVERSION)/build
PWD := $(shell pwd)
INSTALL_MOD_DIR ?= extra
EXTRA_CFLAGS := -g
FEATURE_TEST_BUILD_DIR := configure-tests/feature-tests/build

default:
	if [ ! -f kernel-config.h ] || tail -1 kernel-config.h | grep -qv '#endif'; then mkdir $(FEATURE_TEST_BUILD_DIR); ./genconfig.sh "$(KERNELVERSION)" "$(MFLAGS)"; fi;
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
	$(RM) kernel-config.h
	$(RM) -r $(FEATURE_TEST_BUILD_DIR)

install: default
	$(MAKE) -C $(KDIR) M=$(PWD) modules_install

uninstall:
	$(RM) /lib/modules/$(KERNELVERSION)/$(INSTALL_MOD_DIR)/elastio-snap.ko
