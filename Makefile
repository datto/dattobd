export CC = gcc
export RM = rm -f
export CCFLAGS = -Wall -std=gnu99
export PREFIX = /usr/local
export BASE_DIR = $(dir $(abspath ./Makefile))

.PHONY: all driver library application utils clean

all: driver library application utils

shared: driver utils
	$(MAKE) -C lib shared
	$(MAKE) -C app shared

driver:
	$(MAKE) -C src
	
library:
	$(MAKE) -C lib
	
library-static:
	$(MAKE) -C lib static
	
application: library-static
	$(MAKE) -C app
	
utils:
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

install-static:
	$(MAKE) -C app install-static
