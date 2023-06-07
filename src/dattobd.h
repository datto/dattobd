/* SPDX-License-Identifier: GPL-2.0-only WITH Linux-syscall-note */

/*
 * Copyright (C) 2015-2022 Datto Inc.
 */

#ifndef DATTOBD_H_
#define DATTOBD_H_

#ifndef __KERNEL__
#include <stdint.h>
#endif

#include <linux/ioctl.h>
#include <linux/limits.h>
#include <linux/types.h>

#define DATTOBD_VERSION "0.11.3"
#define DATTO_IOCTL_MAGIC 0x91

struct setup_params {
        char *bdev; // name of block device to snapshot
        char *cow; // name of cow file for snapshot
        unsigned long fallocated_space; // space allocated to the cow file (in
                                        // megabytes)
        unsigned long cache_size; // maximum cache size (in bytes)
        unsigned int minor; // requested minor number of the device
};

struct reload_params {
        char *bdev; // name of block device to snapshot
        char *cow; // name of cow file for snapshot
        unsigned long cache_size; // maximum cache size (in bytes)
        unsigned int minor; // requested minor number of the device
};

struct transition_snap_params {
        char *cow; // name of cow file for snapshot
        unsigned long fallocated_space; // space allocated to the cow file (in
                                        // bytes)
        unsigned int minor; // requested minor
};

struct reconfigure_params {
        unsigned long cache_size; // maximum cache size (in bytes)
        unsigned int minor; // requested minor number of the device
};

#define COW_UUID_SIZE 16
#define COW_BLOCK_LOG_SIZE 12
#define COW_BLOCK_SIZE (1 << COW_BLOCK_LOG_SIZE)
#define COW_HEADER_SIZE 4096
#define COW_MAGIC ((uint32_t)4776)
#define COW_CLEAN 0
#define COW_INDEX_ONLY 1
#define COW_VMALLOC_UPPER 2

#define COW_VERSION_0 0
#define COW_VERSION_CHANGED_BLOCKS 1

/**
 * struct cow_header - Encapsulates the values stored at the beginning of the
 * COW file.
 */
struct cow_header {
        uint32_t magic; // COW header magic
        uint32_t flags; // COW file flags
        uint64_t fpos; // current file offset
        uint64_t fsize; // file size
        uint64_t seqid; // seqential id of snapshot (starts at 1)
        uint8_t uuid[COW_UUID_SIZE]; // uuid for this series of snapshots
        uint64_t version; // version of cow file format
        uint64_t nr_changed_blocks; // number of changed blocks since last
                                    // snapshot
};

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

#define IOCTL_SETUP_SNAP                                                       \
        _IOW(DATTO_IOCTL_MAGIC, 1, struct setup_params) // in: see above
#define IOCTL_RELOAD_SNAP                                                      \
        _IOW(DATTO_IOCTL_MAGIC, 2, struct reload_params) // in: see above
#define IOCTL_RELOAD_INC                                                       \
        _IOW(DATTO_IOCTL_MAGIC, 3, struct reload_params) // in: see above
#define IOCTL_DESTROY _IOW(DATTO_IOCTL_MAGIC, 4, unsigned int) // in: minor
#define IOCTL_TRANSITION_INC                                                   \
        _IOW(DATTO_IOCTL_MAGIC, 5, unsigned int) // in: minor
#define IOCTL_TRANSITION_SNAP                                                  \
        _IOW(DATTO_IOCTL_MAGIC, 6, struct transition_snap_params) // in: see
                                                                  // above
#define IOCTL_RECONFIGURE                                                      \
        _IOW(DATTO_IOCTL_MAGIC, 7, struct reconfigure_params) // in: see above
#define IOCTL_DATTOBD_INFO                                                     \
        _IOR(DATTO_IOCTL_MAGIC, 8, struct dattobd_info) // in: see above
#define IOCTL_GET_FREE _IOR(DATTO_IOCTL_MAGIC, 9, int)

#endif /* DATTOBD_H_ */
