#!/bin/bash
#%stage: block

# we need a directory to mount the root filesystem in
# when the boot-dattobd.sh script runs so it can run reload

export MOUNTROOT=$tmp_mnt/etc/datto/dla/mnt
echo "datto dlad install making mountpoint directory $MOUNTROOT" > /dev/kmsg
mkdir -p $MOUNTROOT
cp /var/lib/datto/dla/reload $tmp_mnt/sbin/datto_reload

mkdir -p $tmp_mnt/usr/bin
mkdir -p $tmp_mnt/usr/sbin
