export CC = gcc
export RM = rm -f
CFLAGS ?= -Wall
export CCFLAGS = $(CFLAGS) -std=gnu99
export PREFIX = /usr/local
export BASE_DIR = $(abspath .)

.PHONY: all driver library-shared library-static library application application-shared utils clean install uninstall

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
	$(MAKE) -C utils CCFLAGS="$(CCFLAGS) -I$(BASE_DIR)/src -I$(BASE_DIR)/lib"

clean:
	$(MAKE) -C src clean
	$(MAKE) -C lib clean
	$(MAKE) -C app clean
	$(MAKE) -C utils clean

install:
	$(MAKE) -C src install
	$(MAKE) -C lib install CCFLAGS="$(CCFLAGS) -I$(BASE_DIR)/src"
	$(MAKE) -C app install CCFLAGS="$(CCFLAGS) -I$(BASE_DIR)/src -I$(BASE_DIR)/lib"
	$(MAKE) -C utils install CCFLAGS="$(CCFLAGS) -I$(BASE_DIR)/src -I$(BASE_DIR)/lib"

uninstall:
	$(MAKE) -C app uninstall
	$(MAKE) -C utils uninstall
	$(MAKE) -C lib uninstall
	$(MAKE) -C src uninstall
