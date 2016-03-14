TARGET_LIB = libdattobd.so.0
SONAME = libdattobd.so
SHARED_CCFLAGS = -fPIC -lc -shared -Wl,-soname,$(SONAME)
LIBDIR = $(PREFIX)/lib

SOURCES = libdattobd.c

.PHONY: all shared static install clean

all: shared static

shared:
	$(CC) $(CCFLAGS) $(SHARED_CCFLAGS) $(SOURCES) -o $(TARGET_LIB)
	ln -sf $(TARGET_LIB) $(SONAME)

static:
	$(CC) $(CCFLAGS) -c $(SOURCES)
	ar rcs libdattobd.a libdattobd.o

install: shared
	install $(TARGET_LIB) $(LIBDIR)
	ln -sf $(LIBDIR)/$(TARGET_LIB) $(LIBDIR)/$(SONAME)
	ldconfig

clean:
	$(RM) *.a *.o *.so*
