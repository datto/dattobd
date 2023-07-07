# SPDX-License-Identifier: GPL-2.0-only

INSTALLDIR = $(PREFIX)/bin
SOURCES_UPDATE_IMG = update-img.c
SOURCES_NL_DEBUG = nl_debug.c

.PHONY: all install uninstall clean

all: nl_debug update-img

nl_debug: $(SOURCES_NL_DEBUG)
	$(CC) $(CCFLAGS) -o nl_debug $(SOURCES_NL_DEBUG)

update-img: $(SOURCES_UPDATE_IMG)
	$(CC) $(CCFLAGS) -o update-img -L $(BASE_DIR)/lib $(SOURCES_UPDATE_IMG) -lelastio-snap

install: update-img
	mkdir -p $(INSTALLDIR)
	install update-img $(INSTALLDIR)

uninstall:
	$(RM) $(INSTALLDIR)/update-img

clean:
	$(RM) nl_debug
	$(RM) update-img
