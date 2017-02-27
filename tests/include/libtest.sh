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
