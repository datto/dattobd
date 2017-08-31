LIBNAME = libdattobd.so
SOVER = 1
SHARED_CCFLAGS = -fPIC -lc -shared -Wl,-soname,$(LIBNAME).$(SOVER)
LIBDIR = $(PREFIX)/lib64
INCLUDES = -I$(BASE_DIR)/include
SOURCES = libdattobd.c

.PHONY: all shared static install clean

all: shared static

shared:
	$(CC) $(CCFLAGS) $(SHARED_CCFLAGS) $(INCLUDES) $(SOURCES) -o $(LIBNAME).$(SOVER)
	ln -sf $(LIBNAME).$(SOVER) $(LIBNAME)

static:
	$(CC) $(CCFLAGS) -c $(INCLUDES) $(SOURCES)
	ar rcs libdattobd.a libdattobd.o

install: shared
	install $(LIBNAME).$(SOVER) $(LIBDIR)
	install $(LIBNAME) $(LIBDIR)
	ldconfig

clean:
	$(RM) *.a *.o *.so*
