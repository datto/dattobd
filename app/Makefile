# SPDX-License-Identifier: GPL-2.0-only

BINARY_NAME = aioctl
INSTALLDIR = $(PREFIX)/bin
SOURCES = aioctl.c

.PHONY: shared static install-static install uninstall clean

shared:
	$(CC) $(CCFLAGS) -o $(BINARY_NAME) -L $(BASE_DIR)/lib $(SOURCES) -lassurio-snap

static:
	$(CC) $(CCFLAGS) -o $(BINARY_NAME) $(SOURCES) $(BASE_DIR)/lib/libassurio-snap.a

install-static: static
	mkdir -p $(INSTALLDIR)
	install $(BINARY_NAME) $(INSTALLDIR)

install: shared
	mkdir -p $(INSTALLDIR)
	install $(BINARY_NAME) $(INSTALLDIR)

uninstall:
	$(RM) $(INSTALLDIR)/$(BINARY_NAME)

clean:
	$(RM) $(BINARY_NAME)
