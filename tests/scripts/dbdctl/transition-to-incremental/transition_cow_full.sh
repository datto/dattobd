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

declare -r fallocate=50

cleanup() {
    expect_pass rm -f ${MOUNT}/zero
    is_tracked ${MINOR} && expect_pass ${DBDCTL} destroy ${MINOR}
}
atexit cleanup

echo "Testing transition to incremental will fail if it fills the cow file"

for i in {1..5}; do
    expect_pass ${DBDCTL} setup-snapshot -f ${fallocate} ${DEVICE} "${MOUNT}/${COW_FILE}" ${MINOR}

    expect_pass dd if=/dev/zero of=${MOUNT}/zero bs=1M count=$((fallocate + 10))

    # Possible errors doing this:
    # * EINVAL: The file system already performed the sync
    # * EFBIG: The module performed the sync
    # We want the later to happen. However, the module sets the error to -27 in
    # both cases, so the only way to check is the state it leaves the snapshot in.

    expect_fail ${DBDCTL} transition-to-incremental ${MINOR}

    error=$(parse ${MINOR} "error")
    expect_equal "${error}" "-27"

    state=$(parse ${MINOR} "state")
    if [[ "${state}" != "2" ]]; then
        cleanup
        continue
    fi

    pass
done

fail
