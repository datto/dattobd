#!/bin/bash
#%stage: block

# we need a directory to mount the root filesystem in
# when the boot-elastio-snap.sh script runs so it can run reload

export MOUNTROOT=$tmp_mnt/etc/elastio/dla/mnt
echo "elastio-snap dlad install making mountpoint directory $MOUNTROOT" > /dev/kmsg
mkdir -p $MOUNTROOT
cp /var/lib/elastio/dla/reload $tmp_mnt/sbin/elastio_reload

mkdir -p $tmp_mnt/usr/bin
mkdir -p $tmp_mnt/usr/sbin
