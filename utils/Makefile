BINARY_NAME = update-img
INSTALLDIR = $(PREFIX)/bin
INCLUDES = -I$(BASE_DIR)/include
SOURCES = update-img.c
CCFLAGS = -D_XOPEN_SOURCE=500

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
