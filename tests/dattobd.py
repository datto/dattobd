# SPDX-License-Identifier: GPL-2.0-only

#
# Copyright (C) 2019 Datto, Inc.
#

from cffi import FFI

import util

ffi = FFI()

ffi.cdef("""
#define COW_UUID_SIZE 16
#define PATH_MAX 4096

struct dattobd_info {
    unsigned int minor;
    unsigned long state;
    int error;
    unsigned long cache_size;
    unsigned long long falloc_size;
    unsigned long long seqid;
    char uuid[COW_UUID_SIZE];
    char cow[PATH_MAX];
    char bdev[PATH_MAX];
    unsigned long long version;
    unsigned long long nr_changed_blocks;
};

int dattobd_setup_snapshot(unsigned int minor, char *bdev, char *cow, unsigned long fallocated_space, unsigned long cache_size);
int dattobd_reload_snapshot(unsigned int minor, char *bdev, char *cow, unsigned long cache_size);
int dattobd_reload_incremental(unsigned int minor, char *bdev, char *cow, unsigned long cache_size);
int dattobd_destroy(unsigned int minor);
int dattobd_transition_incremental(unsigned int minor);
int dattobd_transition_snapshot(unsigned int minor, char *cow, unsigned long fallocated_space);
int dattobd_reconfigure(unsigned int minor, unsigned long cache_size);
int dattobd_info(unsigned int minor, struct dattobd_info *info);
""")

lib = ffi.dlopen("../lib/libdattobd.so")


def setup(minor, device, cow_file, fallocated_space=0, cache_size=0):
    ret = lib.dattobd_setup_snapshot(
        minor,
        device.encode("utf-8"),
        cow_file.encode("utf-8"),
        fallocated_space,
        cache_size
    )

    if ret != 0:
        return ffi.errno

    util.settle()
    return 0


def reload_snapshot(minor, device, cow_file, cache_size=0):
    ret = lib.dattobd_reload_snapshot(
        minor,
        device.encode("utf-8"),
        cow_file.encode("utf-8"),
        cache_size
    )

    if ret != 0:
        return ffi.errno

    util.settle()
    return 0


def reload_incremental(minor, device, cow_file, cache_size=0):
    ret = lib.dattobd_reload_incremental(
        minor,
        device.encode("utf-8"),
        cow_file.encode("utf-8"),
        cache_size
    )

    if ret != 0:
        return ffi.errno

    util.settle()
    return 0


def destroy(minor):
    ret = lib.dattobd_destroy(minor)
    if ret != 0:
        return ffi.errno

    util.settle()
    return 0


def transition_to_incremental(minor):
    ret = lib.dattobd_transition_incremental(minor)
    if ret != 0:
        return ffi.errno

    util.settle()
    return 0


def transition_to_snapshot(minor, cow_file, fallocated_space=0):
    ret = lib.dattobd_transition_snapshot(
        minor,
        cow_file.encode("utf-8"),
        fallocated_space
    )

    if ret != 0:
        return ffi.errno

    util.settle()
    return 0


def reconfigure(minor, cache_size):
    ret = lib.dattobd_reconfigure(minor, cache_size)
    if ret != 0:
        return ffi.errno

    util.settle()
    return 0


def info(minor):
    di = ffi.new("struct dattobd_info *")
    ret = lib.dattobd_info(minor, di)
    if ret != 0:
        return None

    # bytes.hex() is not available in Python <3.5
    uuid = "".join(map(lambda b: format(b, "02x"), ffi.string(di.uuid)))

    return {
        "minor": minor,
        "state": di.state,
        "error": di.error,
        "cache_size": di.cache_size,
        "falloc_size": di.falloc_size,
        "uuid": uuid,
        "cow": ffi.string(di.cow).decode("utf-8"),
        "bdev": ffi.string(di.bdev).decode("utf-8"),
        "version": di.version,
        "nr_changed_blocks": di.nr_changed_blocks,
    }

def version():
    with open("/sys/module/dattobd/version", "r") as v:
        return v.read().strip()
