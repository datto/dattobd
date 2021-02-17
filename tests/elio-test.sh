#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-only

#
# Copyright (C) 2019 Datto, Inc.
# Additional contributions by Elastio Software, Inc are Copyright (C) 2020 Elastio Software Inc.
#

if [ "$EUID" -ne 0 ]; then
    echo "Run as sudo or root."
    exit 1
fi

packman="apt-get"
which yum >/dev/null && packman=yum
which pip3 >/dev/null || $packman install -y python3-pip

if ! pip3 list --format=legacy 2>/dev/null | grep -q cffi ; then
    echo "Python module CFFI is not installed. Installing it..."
    pip3 install cffi
fi

echo
echo "elastio-snap: $(git rev-parse --short HEAD)"
echo "kernel: $(uname -r)"
echo "gcc: $(gcc --version | awk 'NR==1 {print $3}')"
echo "bash: ${BASH_VERSION}"
echo "python: $(python3 --version)"
echo

dmesg -c &> /dev/null
>| dmesg.log

[ ! -z "$1" ] && export TEST_DEVICE="$1"
if ! lsblk $TEST_DEVICE >/dev/null 2>&1; then
    echo "The script's argumet $TEST_DEVICE seems to be not a block device."
    exit 1
fi

python3 -m unittest -v
ret=$?
dmesg > dmesg.log

exit ${ret}
