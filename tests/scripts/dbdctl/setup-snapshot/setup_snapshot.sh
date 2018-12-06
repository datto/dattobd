#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-only

#
# Copyright (C) 2017 Datto, Inc.
#

source ${TEST_DIR}/include/common.sh
source ${TEST_DIR}/include/libtest.sh

init_env

echo "Testing snapshotting a device will succeed"

expect_pass ${DBDCTL} setup-snapshot ${DEVICE} ${MOUNT}/${COW_FILE} ${MINOR}

error=$(parse ${MINOR} "error")
expect_equal "${error}" "0"

state=$(parse ${MINOR} "state")
expect_equal "${state}" "3"

cow=$(parse ${MINOR} "cow_file")
expect_equal "${cow}" "/${COW_FILE}"

bdev=$(parse ${MINOR} "block_device")
expect_equal "${bdev}" "${DEVICE}"

expect_pass ${DBDCTL} destroy ${MINOR}

pass
