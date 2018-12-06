#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-only

#
# Copyright (C) 2017 Datto, Inc.
#

source ${TEST_DIR}/include/common.sh
source ${TEST_DIR}/include/libtest.sh

init_env

echo "Testing snapshotting with an invalid parameters will fail"

# Invalid block device
expect_fail ${DBDCTL} setup-snasphot ${MOUNT} ${MOUNT}/${COW_FILE} ${MINOR}

# Invalid cow file
expect_fail ${DBDCTL} setup-snapshot ${DEVICE} ${MOUNT} ${MINOR}
expect_fail ${DBDCTL} setup-snapshot ${DEVICE} /${COW_FILE} ${MINOR}

# Invalid minor
expect_fail ${DBDCTL} setup-snapshot ${DEVICE} ${MOUNT}/${COW_FILE} 99

pass
