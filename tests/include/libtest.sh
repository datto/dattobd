# SPDX-License-Identifier: GPL-2.0-only

#
# Copyright (C) 2017 Datto, Inc.
#

function atexit
{
    _CLEANUP="$@"
}

function _exit
{
    if [[ -n "${_CLEANUP}" ]]; then
        local cleanup="${_CLEANUP}"
        atexit ""
        ${cleanup}
    fi

    exit $1
}

function pass
{
    [[ -n "$1" ]] && echo "$@"
    _exit 0
}

function fail
{
    [[ -n "$1" ]] && echo "$@" >&2
    _exit 1
}

function _run
{
    echo "$@"
    "$@"
    return $?
}

function expect_pass
{
    _run "$@"
    local ret=$?

    if [[ "${ret}" != "0" ]]; then
        fail "Command failed unexpectedly"
    fi

    return 0
}

function expect_fail
{
    _run "$@"
    local ret=$?

    if [[ "${ret}" == "0" ]]; then
        fail "Command succeeded unexpectedly"
    fi

    return 0
}

function expect_equal
{
    if [[ "$1" != "$2" ]]; then
        fail "Expected $2, got $1"
    fi

    return 0
}
