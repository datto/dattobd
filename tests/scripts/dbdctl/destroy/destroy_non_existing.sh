#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-only

#
# Copyright (C) 2017 Datto, Inc.
#

source ${TEST_DIR}/include/common.sh
source ${TEST_DIR}/include/libtest.sh

init_env

echo "Testing destroying non-existing snapshot will fail"

expect_fail ${DBDCTL} destroy ${MINOR}

pass
