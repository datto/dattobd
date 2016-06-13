obj-m := $(OBJ)

KDIR := /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)
EXTRA_CFLAGS := -g -I$(src)/../../include -Werror
default:
	$(MAKE) -I/usr/include -C $(KDIR) SUBDIRS=$(PWD) modules
	
clean:
	$(MAKE) -I/usr/include -C $(KDIR) SUBDIRS=$(PWD) clean
