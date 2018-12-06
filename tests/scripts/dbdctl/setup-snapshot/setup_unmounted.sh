#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-only

#
# Copyright (C) 2017 Datto, Inc.
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
