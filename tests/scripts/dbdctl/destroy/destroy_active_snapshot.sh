#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-only

#
# Copyright (C) 2017 Datto, Inc.
#

source ${TEST_DIR}/include/common.sh
source ${TEST_DIR}/include/libtest.sh

init_env

declare -r SNAP_DEVICE="/dev/datto${MINOR}"

echo "Testing destroying active snapshot will succeed"

expect_pass ${DBDCTL} setup-snapshot ${DEVICE} ${MOUNT}/${COW_FILE} ${MINOR}
sleep 0.1
expect_pass test -e "${SNAP_DEVICE}"

expect_pass ${DBDCTL} destroy ${MINOR}
sleep 0.1
expect_fail test -e "${SNAP_DEVICE}"

pass
