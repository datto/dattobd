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

cleanup() {
    if ! is_mounted ${DEVICE}; then
        expect_pass mount ${DEVICE} ${MOUNT}
    fi
}
atexit cleanup

echo "Testing snapshotting an unmounted device will fail"

expect_pass umount ${MOUNT}
expect_fail ${DBDCTL} setup-snapshot ${DEVICE} ${MOUNT}/${COW_FILE} ${MINOR}

pass
