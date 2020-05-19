# SPDX-License-Identifier: GPL-2.0-only

export CC = gcc
export RM = rm -f
CFLAGS ?= -Wall
export CCFLAGS = $(CFLAGS) -std=gnu99
export PREFIX = /usr/local
export BASE_DIR = $(abspath .)

BUILDDIR := $(CURDIR)/pkgbuild

# Flags to pass to debbuild/rpmbuild
PKGBUILDFLAGS := --define "_topdir $(BUILDDIR)" -ba --with devmode

# Command to create the build directory structure
PKGBUILDROOT_CREATE_CMD = mkdir -p $(BUILDDIR)/DEBS $(BUILDDIR)/SDEBS $(BUILDDIR)/RPMS $(BUILDDIR)/SRPMS \
			$(BUILDDIR)/SOURCES $(BUILDDIR)/SPECS $(BUILDDIR)/BUILD $(BUILDDIR)/BUILDROOT

.PHONY: all driver library-shared library-static library application application-shared utils clean install uninstall pkgclean pkgprep deb rpm

all: driver library application utils

driver:
	$(MAKE) -C src

library-shared:
	$(MAKE) -C lib CCFLAGS="$(CCFLAGS) -I$(BASE_DIR)/src" shared

library-static:
	$(MAKE) -C lib CCFLAGS="$(CCFLAGS) -I$(BASE_DIR)/src" static

library: library-shared library-static

application-static: library-static
	$(MAKE) -C app CCFLAGS="$(CCFLAGS) -I$(BASE_DIR)/src -I$(BASE_DIR)/lib"

application: library-shared
	$(MAKE) -C app CCFLAGS="$(CCFLAGS) -I$(BASE_DIR)/src -I$(BASE_DIR)/lib" shared

utils: library-shared
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

deb: pkgprep
	debbuild $(PKGBUILDFLAGS) $(BUILDDIR)/SPECS/elastio-snap.spec

rpm: pkgprep
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
