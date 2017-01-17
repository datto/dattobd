#!/bin/sh

modprobe dattobd

if [ -n "$root" -o -n "${root#block:*}" ]; then
	echo "dattobd ROOT = $root" > /dev/kmsg
	rbd=""

	if test "${root#*/dev/disk/by-uuid/}" != $root; then
		orig=$rbd
		rbd=${root#block:/dev/disk/by-uuid/}
		rbd=$(blkid -t UUID=$rbd -o device)
		if test "$rbd" = ""; then
			# 12/5/2016 if blkid gets nothing, wait for udev to settle
			echo "dattobd waiting for udev to settle" > /dev/kmsg
			/usr/bin/udevadm settle
			echo "dattobd udev settled" > /dev/kmsg
			rbd=$orig
			rbd=${root#block:/dev/disk/by-uuid/}
			rbd=$(blkid -t UUID=$rbd -o device)
			echo "dattobd settled rbd $rbd" > /dev/kmsg
		else
			echo "dattobd rbd $rbd" > /dev/kmsg
		fi
	elif test "${root#*/dev/mapper/}" != $root; then
		rbd=${root#block:}
	else
		rbd=${root#block:}
	fi

	echo "RBD = $rbd" > /dev/kmsg

	if [ -b "$rbd" ]; then
		if [ -z "$fstype" ]; then
			fstype=$(blkid -s TYPE $rbd)
			fstype=${fstype#*TYPE\=\"}
			fstype=${fstype%\"*}
		fi
		echo "Mounting $rbd as $fstype" > /dev/kmsg
		mount -t $fstype -o ro "$rbd" /etc/datto/dla/mnt > /dev/kmsg
		sleep 1
echo "datto looking for datto_reload" > /dev/kmsg
		if [ -x /sbin/datto_reload ]; then
echo "datto found /sbin/datto_reload" > /dev/kmsg
			/sbin/datto_reload > /dev/kmsg
		else
echo "datto did not find /sbin/datto_reload" > /dev/kmsg
		fi
		umount -f /etc/datto/dla/mnt > /dev/kmsg
	fi
fi

