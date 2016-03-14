BINARY_NAME = dbdctl
INSTALLDIR = $(PREFIX)/bin
SOURCES = dbdctl.c

.PHONY: static shared install-static install clean

static:
	$(CC) $(CCFLAGS) -o $(BINARY_NAME) $(SOURCES) $(BASE_DIR)/lib/libdattobd.a

shared:
	$(CC) $(CCFLAGS) -o $(BINARY_NAME) -L $(BASE_DIR)/lib $(SOURCES) -ldattobd

install-static: static
	mkdir -p $(INSTALLDIR)
	install $(BINARY_NAME) $(INSTALLDIR)

install: shared
	mkdir -p $(INSTALLDIR)
	install $(BINARY_NAME) $(INSTALLDIR)

clean:
	rm -f $(BINARY_NAME)
