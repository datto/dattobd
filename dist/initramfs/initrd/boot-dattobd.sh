#!/bin/bash
# we need the dattobd module and the dbdctl binary in the initramfs
# blkid is already there but I just want to be extra sure. shmura.

#%stage: volumemanager
#%depends: lvm2
#%modules: dattobd
#%programs: /usr/bin/dbdctl
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

echo "datto dlad load_modules" > /dev/kmsg
# this is a function in linuxrc, modprobes dattobd for us.
load_modules

/sbin/modprobe --allow-unsupported dattobd

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
    [ -z "$rootfstype" ] && rootfstype=$(blkid -s TYPE -o value $rbd)

    echo "dattobd: mounting $rbd as $rootfstype" > /dev/kmsg
    blockdev --setro $rbd
    mount -t $fstype -o ro "$rbd" /etc/datto/dla/mnt > /dev/kmsg
    udevadm settle

    if [ -x /sbin/datto_reload ]; then
        /sbin/datto_reload
    else
        echo "dattobd: error: cannot reload tracking data: missing /sbin/datto_reload" > /dev/kmsg
    fi

    umount -f /etc/datto/dla/mnt > /dev/kmsg
    blockdev --setrw $rbd
fi
