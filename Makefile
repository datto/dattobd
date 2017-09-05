export CC = gcc
export RM = rm -f
export CCFLAGS = -Wall -std=gnu99
export PREFIX = /usr/local
export BASE_DIR = $(dir $(abspath ./Makefile))

.PHONY: all driver library-shared library-static library application application-shared utils clean install

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

install:
	$(MAKE) -C lib install CCFLAGS="$(CCFLAGS) -I$(BASE_DIR)/src"
	$(MAKE) -C app install CCFLAGS="$(CCFLAGS) -I$(BASE_DIR)/src -I$(BASE_DIR)/lib"
	$(MAKE) -C utils install CCFLAGS="$(CCFLAGS) -I$(BASE_DIR)/src -I$(BASE_DIR)/lib -D_XOPEN_SOURCE=500"
