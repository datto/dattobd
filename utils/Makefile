BINARY_NAME = update-img
INSTALLDIR = $(PREFIX)/bin
SOURCES = update-img.c

.PHONY: all clean install

all:
	$(CC) $(CCFLAGS) -o $(BINARY_NAME) $(SOURCES)

install: all
	mkdir -p $(INSTALLDIR)
	install $(BINARY_NAME) $(INSTALLDIR)

clean:
	rm -f $(BINARY_NAME)
