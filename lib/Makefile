# SPDX-License-Identifier: LGPL-2.1-or-later

LIBNAME = libassurio-snap.so
SOVER = 1
SHARED_CCFLAGS = -fPIC -lc -shared -Wl,-soname,$(LIBNAME).$(SOVER)
LIBDIR = $(PREFIX)/lib64

SOURCES = libassurio-snap.c

.PHONY: all shared static install uninstall clean

all: shared static

shared:
	$(CC) $(CCFLAGS) $(SHARED_CCFLAGS) $(SOURCES) -o $(LIBNAME).$(SOVER)
	ln -sf $(LIBNAME).$(SOVER) $(LIBNAME)

static:
	$(CC) $(CCFLAGS) -c $(SOURCES)
	ar rcs libassurio-snap.a libassurio-snap.o

install: shared
	install $(LIBNAME).$(SOVER) $(LIBDIR)
	install $(LIBNAME) $(LIBDIR)
	ldconfig

uninstall:
	$(RM) $(LIBDIR)/$(LIBNAME)
	$(RM) $(LIBDIR)/$(LIBNAME).$(SOVER)
	ldconfig

clean:
	$(RM) *.a *.o *.so*
