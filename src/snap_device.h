// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (C) 2022 Datto Inc.
 */

#ifndef SNAP_DEVICE_H_
#define SNAP_DEVICE_H_

#include "bio_helper.h" // needed for USE_BDOPS_SUBMIT_BIO to be defined
#include "bio_queue.h"
#include "bio_request_callback.h"
#include "includes.h"
#include "submit_bio.h"
#include "sset_queue.h"

// macros for defining the state of a tracing struct (bit offsets)
#define SNAPSHOT 0
#define ACTIVE 1
#define UNVERIFIED 2

struct snap_device {
        unsigned int sd_minor; // minor number of the snapshot
        unsigned long sd_state; // current state of the snapshot
        unsigned long sd_falloc_size; // space allocated to the cow file (in
                                      // megabytes)
        unsigned long sd_cache_size; // maximum cache size (in bytes)
        atomic_t sd_refs; // number of users who have this device open
        atomic_t sd_fail_code; // failure return code
        atomic_t sd_active; // boolean for whether the snap device is set up and ready to trace i/o
        sector_t sd_sect_off; // starting sector of base block device
        sector_t sd_size; // size of device in sectors
        struct request_queue *sd_queue; // snap device request queue
        struct gendisk *sd_gd; // snap device gendisk
        struct block_device *sd_base_dev; // device being snapshot
        char *sd_bdev_path; // base device file path
        struct cow_manager *sd_cow; // cow manager
        char *sd_cow_path; // cow file path
        struct inode *sd_cow_inode; // cow file inode
        BIO_REQUEST_CALLBACK_FN *sd_orig_request_fn; // block device's original make_request_fn or submit_bio function ptr.
        struct task_struct *sd_cow_thread; // thread for handling file read/writes
        struct bio_queue sd_cow_bios; // list of outstanding cow bios
        struct task_struct *sd_mrf_thread; // thread for handling file
                                           // read/writes
        struct bio_queue sd_orig_bios; // list of outstanding original bios
        struct sset_queue sd_pending_ssets; // list of outstanding sector sets
#ifndef HAVE_BIOSET_INIT
        //#if LINUX_VERSION_CODE < KERNEL_VERSION(4,18,0)
        struct bio_set *sd_bioset; // allocation pool for bios
#else
        struct bio_set sd_bioset; // allocation pool for bios
#endif
        atomic64_t sd_submitted_cnt; // count of read clones submitted to
                                     // underlying driver
        atomic64_t sd_received_cnt; // count of read clones submitted to
                                    // underlying driver
};

#endif /* SNAP_DEVICE_H_ */
