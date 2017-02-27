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

echo "Testing snapshotting a device a second time will fail"

expect_pass ${DBDCTL} setup-snapshot ${DEVICE} ${MOUNT}/${COW_FILE} 1
sleep 0.1

expect_fail ${DBDCTL} setup-snapshot ${DEVICE} ${MOUNT}/${COW_FILE} 2
expect_pass ${DBDCTL} destroy 1

pass
