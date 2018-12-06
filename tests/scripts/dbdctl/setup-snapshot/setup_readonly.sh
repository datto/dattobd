#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-only

#
# Copyright (C) 2017 Datto, Inc.
#

source ${TEST_DIR}/include/common.sh
source ${TEST_DIR}/include/libtest.sh

init_env

cleanup() {
    expect_pass mount -o remount,rw ${DEVICE} ${MOUNT}
}
atexit cleanup

echo "Testing snapshotting a read-only device will fail"

expect_pass is_mounted ${DEVICE}
expect_pass mount -o remount,ro ${DEVICE} ${MOUNT}
expect_fail ${DBDCTL} setup-snapshot ${DEVICE} ${MOUNT}/${COW_FILE} ${MINOR}

pass
