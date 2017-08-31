BINARY_NAME = dbdctl
INSTALLDIR = $(PREFIX)/bin
INCLUDES = -I$(BASE_DIR)/include
SOURCES = dbdctl.c

.PHONY: shared static install-static install clean

shared:
	$(CC) $(CCFLAGS) -o $(BINARY_NAME) $(INCLUDES) -L $(BASE_DIR)/lib $(SOURCES) -ldattobd

static:
	$(CC) $(CCFLAGS) -o $(BINARY_NAME) $(INCLUDES) $(SOURCES) $(BASE_DIR)/lib/libdattobd.a

install-static: static
	mkdir -p $(INSTALLDIR)
	install $(BINARY_NAME) $(INSTALLDIR)

install: shared
	mkdir -p $(INSTALLDIR)
	install $(BINARY_NAME) $(INSTALLDIR)

clean:
	rm -f $(BINARY_NAME)
