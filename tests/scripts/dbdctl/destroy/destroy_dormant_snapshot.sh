#!/bin/bash

#
# Copyright (C) 2017 Datto, Inc.
#
# This file is part of dattobd.
#
# This file is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public
# License v2 as published by the Free Software Foundation.
#
# This file is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#

source ${TEST_DIR}/include/common.sh
source ${TEST_DIR}/include/libtest.sh

init_env

declare -r SNAP_DEVICE="/dev/datto${MINOR}"

cleanup() {
    if ! is_mounted ${DEVICE}; then
        expect_pass mount ${DEVICE} ${MOUNT}
    fi

    rm -r ${MOUNT}/${COW_FILE}
}
atexit cleanup

echo "Testing destroying dormant snapshot will succeed"

expect_pass ${DBDCTL} setup-snapshot ${DEVICE} ${MOUNT}/${COW_FILE} ${MINOR}
sleep 0.1
expect_pass test -e "${SNAP_DEVICE}"

expect_pass umount ${DEVICE}
sleep 0.1

expect_pass ${DBDCTL} destroy ${MINOR}
sleep 0.1
expect_fail test -e "${SNAP_DEVICE}"

pass
