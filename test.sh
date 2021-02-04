#!/bin/bash
for i in {1..1000};do
	insmod src/dattobd.ko
	app/dbdctl setup-snapshot /dev/sda1 /boot/.datto 0 2
	app/dbdctl setup-snapshot /dev/mapper/centos-root /.datto 1 2
	app/dbdctl wake-up
	cat /proc/datto-info
	sleep 0.5
	rmmod src/dattobd.ko
done
