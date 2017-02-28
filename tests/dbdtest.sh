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

export TEST_DIR="$(cd ${0%/*} && pwd)"

source ${TEST_DIR}/include/common.sh

init_env

[[ -x "${DBDCTL}" ]] || exit 127
[[ -x "${UPDATE_IMG}" ]] || exit 127

TESTS=0
FAILED=0
SKIPPED=0

function find_tests
{
    [[ -z "$1" ]] && return 1

    find "$1" -type f -name "*.sh"
}

function run_test
{
    (( TESTS += 1 ))
    echo -e "\n[TEST] $1" >> results

    bash "$@" 2>&1 >> results
    local ret=$?

    if [[ "${ret}" == "0" ]]; then
        str="[PASS] $1"
        echo -e "\033[0;32m${str}\033[0m"
    elif [[ "${ret}" == "1" ]]; then
        (( FAILED += 1 ))
        str="[FAIL] $1"
        echo -e "\033[0;31m${str}\033[0m"
    elif [[ "${ret}" == "2" ]]; then
        (( SKIPPED += 1 ))
        str="[SKIP] $1"
        echo -e "\033[1;33m${str}\033[0m"
    fi

    echo "${str}" >> results

    return ${ret}
}


if [[ "$EUID" != "0" ]]; then
    echo "Must be run as root"
    exit 1
fi

>| results
echo "dattobd: $(git rev-parse --short HEAD)" | tee -a results
echo "kernel: $(uname -r)" | tee -a results
echo "gcc: $(gcc --version | awk 'NR==1 { print $3 }')" | tee -a results
echo "bash: ${BASH_VERSION}" | tee -a results
echo

setup_default_device || {
    echo "Failed to setup the default device" 1>&2
    exit 1
}

insert_module DEBUG=1 || exit 1

ts=($(find_tests "${TEST_DIR}/scripts"))

for t in ${ts[@]}; do
    run_test $t
done

remove_module
cleanup_default_device

echo -e "\n${TESTS} tests executed (${FAILED} failed, ${SKIPPED} skipped)" | tee -a results
[[ "${FAILED}" == "0" ]] || exit 1
exit 0
