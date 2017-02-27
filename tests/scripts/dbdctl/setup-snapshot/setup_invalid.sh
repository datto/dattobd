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

echo "Testing snapshotting with an invalid parameters will fail"

# Invalid block device
expect_fail ${DBDCTL} setup-snasphot ${MOUNT} ${MOUNT}/${COW_FILE} ${MINOR}

# Invalid cow file
expect_fail ${DBDCTL} setup-snapshot ${DEVICE} ${MOUNT} ${MINOR}
expect_fail ${DBDCTL} setup-snapshot ${DEVICE} /${COW_FILE} ${MINOR}

# Invalid minor
expect_fail ${DBDCTL} setup-snapshot ${DEVICE} ${MOUNT}/${COW_FILE} 99

pass
