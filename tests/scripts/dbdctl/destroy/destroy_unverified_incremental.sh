#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-only

#
# Copyright (C) 2017 Datto, Inc.
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
