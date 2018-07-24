#!/bin/sh

type getarg >/dev/null 2>&1 || . /lib/dracut-lib.sh

modprobe dattobd

[ -z "$root" ] && root=$(getarg root=)
[ -z "$rootfstype" ] && rootfstype=$(getarg rootfstype=)

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

    echo "dattobd: root block device = $rbd" > /dev/kmsg

    # Device might not be ready
    if [ ! -b "$rbd" ]; then
        udevadm settle
    fi

    # Kernel cmdline might not specify rootfstype
    [ -z "$rootfstype" ] && rootfstype=$(blkid -s TYPE "$rbd" -o value)

    echo "dattobd: mounting $rbd as $rootfstype" > /dev/kmsg
    blockdev --setro $rbd
    mount -t $rootfstype -o ro "$rbd" /etc/datto/dla/mnt
    udevadm settle

    if [ -x /sbin/datto_reload ]; then
        /sbin/datto_reload
    else
        echo "dattobd: error: cannot reload tracking data: missing /sbin/datto_reload" > /dev/kmsg
    fi

    umount -f /etc/datto/dla/mnt
    blockdev --setrw $rbd
fi
