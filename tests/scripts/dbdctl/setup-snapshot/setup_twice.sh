#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-only

#
# Copyright (C) 2017 Datto, Inc.
#

source ${TEST_DIR}/include/common.sh
source ${TEST_DIR}/include/libtest.sh

init_env

echo "Testing snapshotting a device a second time will fail"

expect_pass ${DBDCTL} setup-snapshot ${DEVICE} ${MOUNT}/${COW_FILE} 1
sleep 0.1

expect_fail ${DBDCTL} setup-snapshot ${DEVICE} ${MOUNT}/${COW_FILE} 2
expect_pass ${DBDCTL} destroy 1

pass
