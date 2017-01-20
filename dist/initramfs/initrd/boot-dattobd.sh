#!/bin/bash
# we need the dattobd module and the dbdctl binary in the initramfs
# blkid is already there but I just want to be extra sure. shmura.

#%stage: volumemanager
#%depends: lvm2
#%modules: dattobd
#%programs: /usr/bin/dbdctl
#%programs: /usr/sbin/blkid
#%programs: /sbin/lsmod
#%programs: /sbin/modprobe
#%programs: /usr/bin/mount
#%programs: /usr/bin/umount
#%programs: /sbin/blkid
#%programs: /bin/mount
#%programs: /bin/umount
#%programs: /usr/bin/readlink
#%programs: /bin/readlink

echo "datto dlad load_modules" > /dev/kmsg
# this is a function in linuxrc, modprobes dattobd for us.
load_modules

/sbin/modprobe --allow-unsupported dattobd

echo "datto dlad root = $root" > /dev/kmsg

rbd=""

if [ -n "$root"  ]; then
	rbd=""
	# check for UUID first
	echo "datto dlad checking UUID" > /dev/kmsg
	if [[ "$root" == "UUID="* ]]; then
	  echo "datto dlad UUID match" > /dev/kmsg
	  rbd=/dev/disk/by-uuid/${root:5}
	  echo "datto dlad found uuid: $rbd" > /dev/kmsg
        elif test "${root#*/dev/disk/by-uuid/}" != $root; then
        	echo "datto dlad disk by-uuid match" > /dev/kmsg
		rbd=${root#block:/dev/disk/by-uuid/}
        	echo "datto dlad disk by-uuid strip $rbd" > /dev/kmsg
		rbd=$(blkid -t UUID=$rbd -o device)
	  	echo "datto dlad found disk by-uuid: $rbd" > /dev/kmsg
	elif test "${root#*/dev/mapper/}" != $root; then
        	echo "datto dlad dev mapper match" > /dev/kmsg
		rbd=${root#block:}
	  	echo "datto dlad found dev mapper: $rbd" > /dev/kmsg
	elif test "${root#*/dev/disk/by-id/}" != $root; then
        	echo "datto dlad disk by id match" > /dev/kmsg
	        echo "rbd= (/usr/bin/readlink -f $root)" > /dev/kmsg
	        rbd=$(/usr/bin/readlink -f $root)
	  	echo "datto dlad found disk by id: $rbd" > /dev/kmsg
	else
	  	echo "datto dlad found nothing" > /dev/kmsg
	fi

	echo "datto dlad RBD = $rbd" > /dev/kmsg

	fstype=$rootfstype
	if [ -b "$rbd" ]; then
		if [ -z "$fstype" ]; then
		echo "fstype not set, scanning from blkid" > /dev/kmsg
			fstype=$(blkid -s TYPE $rbd)
			fstype=${fstype#*TYPE\=\"}
			fstype=${fstype%\"*}
		fi
		echo "datto dlad mounting $rbd as $fstype" > /dev/kmsg
		mount -t $fstype -o ro "$rbd" /etc/datto/dla/mnt > /dev/kmsg
		sleep 1
		echo "datto dlad checking for datto_reload" > /dev/kmsg
		if [ -x /sbin/datto_reload ]; then
			echo "datto dlad found datto_reload..." > /dev/kmsg
			/sbin/datto_reload > /dev/kmsg
		else
			echo "datto dlad did not find datto_reload..." > /dev/kmsg
		fi
		echo "datto dlad Unmounting $rbd from /etc/datto/dla/mnt" > /dev/kmsg
		umount -f /etc/datto/dla/mnt > /dev/kmsg
	else
		echo "datto dlad rbd: $rbd is not a block device" > /dev/kmsg
	fi
else
	echo "datto dlad root is empty" > /dev/kmsg
fi

