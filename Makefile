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
	$(MAKE) -C lib shared

library-static:
	$(MAKE) -C lib static

library: library-shared library-static

application-static: library-static
	$(MAKE) -C app

application: library-shared
	$(MAKE) -C app shared

utils: library-shared
	$(MAKE) -C utils

clean:
	$(MAKE) -C src clean
	$(MAKE) -C lib clean
	$(MAKE) -C app clean
	$(MAKE) -C utils clean

install:
	$(MAKE) -C lib install
	$(MAKE) -C app install
	$(MAKE) -C utils install
