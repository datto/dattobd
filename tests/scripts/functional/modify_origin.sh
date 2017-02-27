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

declare -r DATA_1="The quick brown fox"
declare -r DATA_2="jumps over the lazy dog"
declare -r FILE="file"
declare -r SNAP_MOUNT="/mnt"
declare -r SNAP_DEVICE="/dev/datto${MINOR}"

echo "Testing snapshot does not change with original device"

expect_pass eval "echo '${DATA_1}' > '${MOUNT}/${FILE}'"
sync
md5_orig=$(md5 "${MOUNT}/${FILE}")

expect_pass ${DBDCTL} setup-snapshot ${DEVICE} "${MOUNT}/${COW_FILE}" ${MINOR}
sleep 0.1

expect_pass eval "echo '${DATA_2}' > '${MOUNT}/${FILE}'"
sync

expect_pass mount -o ro ${SNAP_DEVICE} ${SNAP_MOUNT}
sleep 0.1

expect_pass test -e "${SNAP_MOUNT}/${FILE}"
md5_snap=$(md5 "${SNAP_MOUNT}/${FILE}")
expect_equal ${md5_orig} ${md5_snap}

expect_pass umount ${SNAP_MOUNT}
sleep 0.1

expect_pass ${DBDCTL} destroy ${MINOR}
sleep 0.1

expect_pass rm ${MOUNT}/${FILE}
sync

pass
