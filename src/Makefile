MODULE := dattobd
obj-m := $(MODULE).o
$(MODULE)-objs += driver.o universion.o fileops.o

KERNELVERSION = $(shell uname -r)
KDIR := /lib/modules/$(KERNELVERSION)/build
PWD := $(shell pwd)
EXTRA_CFLAGS := -g -I$(src)/include -include $(src)/kernel-config.h -DDATTO_DEBUG

default:
	if [ ! -f kernel-config.h ] || tail -1 kernel-config.h | grep -qv '#endif'; then ./genconfig.sh $(KERNELVERSION); fi;
	$(MAKE) -C $(KDIR) SUBDIRS=$(PWD) modules
	
clean:
	$(MAKE) -C $(KDIR) SUBDIRS=$(PWD) clean
	rm -f kernel-config.h

insmod: default
	insmod dattobd.ko
	
rmmod:
	rmmod dattobd
