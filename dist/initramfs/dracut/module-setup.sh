#!/bin/sh
# -*- mode: shell-script; indent-tabs-mode: nil; sh-basic-offset: 4; -*-
# ex: ts=8 sw=4 sts=4 et filetype=sh

check() {
    return 0
}

depends() {
    return 0
}

installkernel() {
    hostonly='' instmods dattobd
}

install() {
    inst_hook pre-mount 01 "$moddir/dattobd.sh"
    inst_dir /etc/datto/dla/mnt
    inst /sbin/blkid
    inst /usr/bin/udevadm
    inst /usr/bin/dbdctl
    inst_simple /var/lib/datto/dla/reload /sbin/datto_reload
}
