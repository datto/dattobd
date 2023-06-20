# SPDX-License-Identifier: GPL-2.0-only

export CC = gcc
export RM = rm -f
CFLAGS ?= -Wall
export CCFLAGS = $(CFLAGS) -std=gnu99
export PREFIX = /usr
export BASE_DIR = $(abspath .)

EUID := $(shell id -u -r)

BUILDDIR := $(CURDIR)/pkgbuild
KERNEL_CONFIG := src/kernel-config.h

# Flags to pass to debbuild/rpmbuild
PKGBUILDFLAGS := --define "_topdir $(BUILDDIR)" -ba --with devmode

# Command to create the build directory structure
PKGBUILDROOT_CREATE_CMD = mkdir -p $(BUILDDIR)/DEBS $(BUILDDIR)/SDEBS $(BUILDDIR)/RPMS $(BUILDDIR)/SRPMS \
			$(BUILDDIR)/SOURCES $(BUILDDIR)/SPECS $(BUILDDIR)/BUILD $(BUILDDIR)/BUILDROOT

.PHONY: all driver library-shared library-static library application application-shared utils clean install uninstall pkgclean pkgprep deb rpm

all: check_root driver library application utils

check_root:
ifneq ($(EUID),0)
	@echo "Run as sudo or root."
	@exit 1
endif

driver: check_root
	$(MAKE) -C src

$(KERNEL_CONFIG):
	$(BASE_DIR)/src/genconfig.sh "$(shell uname -r)"

library-shared: $(KERNEL_CONFIG)
	$(MAKE) -C lib CCFLAGS="$(CCFLAGS) -I$(BASE_DIR)/src" shared

library-static: $(KERNEL_CONFIG)
	$(MAKE) -C lib CCFLAGS="$(CCFLAGS) -I$(BASE_DIR)/src" static

library: library-shared library-static

application-static: library-static
	$(MAKE) -C app CCFLAGS="$(CCFLAGS) -I$(BASE_DIR)/src -I$(BASE_DIR)/lib"

application: check_root library-shared
	$(MAKE) -C app CCFLAGS="$(CCFLAGS) -I$(BASE_DIR)/src -I$(BASE_DIR)/lib" shared

utils: check_root library-shared
	$(MAKE) -C utils CCFLAGS="$(CCFLAGS) -I$(BASE_DIR)/src -I$(BASE_DIR)/lib -D_XOPEN_SOURCE=500"

clean:
	$(MAKE) -C src clean
	$(MAKE) -C lib clean
	$(MAKE) -C app clean
	$(MAKE) -C utils clean

pkgclean:
	rm -rf $(BUILDDIR)

pkgprep: pkgclean
	$(PKGBUILDROOT_CREATE_CMD)
	tar --exclude=./pkgbuild --exclude=.git --transform 's,^\.,elastio-snap,' -czf $(BUILDDIR)/SOURCES/elastio-snap.tar.gz .
	cp dist/elastio-snap.spec $(BUILDDIR)/SPECS/elastio-snap.spec

deb: check_root pkgprep
	debbuild $(PKGBUILDFLAGS) $(BUILDDIR)/SPECS/elastio-snap.spec

rpm: check_root pkgprep
	rpmbuild $(PKGBUILDFLAGS) $(BUILDDIR)/SPECS/elastio-snap.spec

install:
	$(MAKE) -C src install
	$(MAKE) -C lib install CCFLAGS="$(CCFLAGS) -I$(BASE_DIR)/src"
	$(MAKE) -C app install CCFLAGS="$(CCFLAGS) -I$(BASE_DIR)/src -I$(BASE_DIR)/lib"
	$(MAKE) -C utils install CCFLAGS="$(CCFLAGS) -I$(BASE_DIR)/src -I$(BASE_DIR)/lib -D_XOPEN_SOURCE=500"

uninstall:
	$(MAKE) -C app uninstall
	$(MAKE) -C utils uninstall
	$(MAKE) -C lib uninstall
	$(MAKE) -C src uninstall
