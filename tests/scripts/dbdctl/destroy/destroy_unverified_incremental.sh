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
}
atexit cleanup

echo "Testing destroying unverified snapshot will succeed"

expect_pass umount ${MOUNT}
sleep 0.1

expect_pass ${DBDCTL} reload-incremental ${DEVICE} /${COW_FILE} ${MINOR}
sleep 0.1

expect_pass ${DBDCTL} destroy ${MINOR}

pass
