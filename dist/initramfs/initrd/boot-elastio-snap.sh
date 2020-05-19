#!/bin/bash
# we need the elastio-snap module and the elioctl binary in the initramfs
# blkid is already there but I just want to be extra sure. shmura.

#%stage: volumemanager
#%depends: lvm2
#%modules: elastio-snap
#%programs: /usr/bin/elioctl
#%programs: /sbin/lsmod
#%programs: /sbin/modprobe
#%programs: /sbin/blkid
#%programs: /usr/sbin/blkid
#%programs: /sbin/blockdev
#%programs: /usr/sbin/blockdev
#%programs: /bin/mount
#%programs: /usr/bin/mount
#%programs: /bin/umount
#%programs: /usr/bin/umount
#%programs: /bin/udevadm
#%programs: /usr/bin/udevadm

echo "elastio-snap dlad load_modules" > /dev/kmsg
# this is a function in linuxrc, modprobes elastio-snap for us.
load_modules

/sbin/modprobe --allow-unsupported elastio-snap

rbd="${root#block:}"
if [ -n "$rbd" ]; then
    case "$rbd" in
        LABEL=*)
            rbd="$(echo $rbd | sed 's,/,\\x2f,g')"
            rbd="/dev/disk/by-label/${rbd#LABEL=}"
            ;;
        UUID=*)
            rbd="/dev/disk/by-uuid/${rbd#UUID=}"
            ;;
        PARTLABEL=*)
            rbd="/dev/disk/by-partlabel/${rbd#PARTLABEL=}"
            ;;
        PARTUUID=*)
            rbd="/dev/disk/by-partuuid/${rbd#PARTUUID=}"
            ;;
    esac

    echo "elastio-snap: root block device = $rbd" > /dev/kmsg

    # Device might not be ready
    if [ ! -b "$rbd" ]; then
        udevadm settle
    fi

    # Kernel cmdline might not specify rootfstype
    [ -z "$rootfstype" ] && rootfstype=$(blkid -s TYPE -o value $rbd)

    echo "elastio-snap: mounting $rbd as $rootfstype" > /dev/kmsg
    blockdev --setro $rbd
    mount -t $fstype -o ro "$rbd" /etc/elastio/dla/mnt > /dev/kmsg
    udevadm settle

    if [ -x /sbin/elastio_reload ]; then
        /sbin/elastio_reload
    else
        echo "elastio-snap: error: cannot reload tracking data: missing /sbin/elastio_reload" > /dev/kmsg
    fi

    umount -f /etc/elastio/dla/mnt > /dev/kmsg
    blockdev --setrw $rbd
fi
