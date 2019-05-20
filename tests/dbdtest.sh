#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-only

#
# Copyright (C) 2019 Datto, Inc.
#

echo
echo "dattobd: $(git rev-parse --short HEAD)"
echo "kernel: $(uname -r)"
echo "gcc: $(gcc --version | awk 'NR==1 {print $3}')"
echo "bash: ${BASH_VERSION}"
echo "python: $(python3 --version)"
echo

dmesg -c &> /dev/null
>| dmesg.log
python3 -m unittest -v
ret=$?
dmesg > dmesg.log

exit ${ret}
