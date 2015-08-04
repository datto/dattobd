TARGET_LIB = libdattobd.so.0
SONAME = libdattobd.so
SHARED_CCFLAGS = -fPIC -lc -shared -Wl,-soname,$(SONAME)
LIBDIR = $(PREFIX)/lib

SOURCES = libdattobd.c

.PHONY: all static clean install shared

all: shared static

install: shared 
	install $(TARGET_LIB) $(LIBDIR)
	ln -sf $(LIBDIR)/$(TARGET_LIB) $(LIBDIR)/$(SONAME)
	ldconfig

shared:
	$(CC) $(CCFLAGS) $(SHARED_CCFLAGS) $(SOURCES) \
		-o $(TARGET_LIB)
	ln -sf $(TARGET_LIB) $(SONAME)

static: 
	$(CC) $(CCFLAGS) -c $(SOURCES)
	ar rcs libdattobd.a libdattobd.o
		
clean:
	$(RM) *.a *.o *.so*
