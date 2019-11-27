#!/bin/bash
#%stage: block

# we need a directory to mount the root filesystem in
# when the boot-assurio-snap.sh script runs so it can run reload

export MOUNTROOT=$tmp_mnt/etc/assurio/dla/mnt
echo "assurio-snap dlad install making mountpoint directory $MOUNTROOT" > /dev/kmsg
mkdir -p $MOUNTROOT
cp /var/lib/assurio/dla/reload $tmp_mnt/sbin/assurio_reload

mkdir -p $tmp_mnt/usr/bin
mkdir -p $tmp_mnt/usr/sbin
