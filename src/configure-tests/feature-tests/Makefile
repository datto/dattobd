# SPDX-License-Identifier: GPL-2.0-only

obj-m := $(OBJ)

KERNELVERSION ?= $(shell uname -r)
KDIR := /lib/modules/$(KERNELVERSION)/build
PWD := $(shell pwd)
EXTRA_CFLAGS := -g -Werror -I$(src)/../..
BUILDDIR ?= $(PWD)/build/$(OBJ)
BUILDDIR_MAKEFILE ?= $(PWD)/build/$(OBJ)/Makefile

default: $(BUILDDIR_MAKEFILE)
	$(MAKE) -C $(KDIR) M=$(BUILDDIR) src=$(PWD) modules

$(BUILDDIR):
	mkdir -p "$@"

$(BUILDDIR_MAKEFILE): $(BUILDDIR)
	touch "$@"

clean: $(BUILDDIR)
	$(MAKE) -C $(KDIR) M=$(BUILDDIR) src=$(PWD) clean
