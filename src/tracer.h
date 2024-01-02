// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (C) 2022 Datto Inc.
 */

#ifndef TRACER_H_
#define TRACER_H_

#include "mrf.h"

struct request_queue;
struct dattobd_info;
struct snap_device;
struct bio;

//maximum number of clones per traced bio
#define MAX_CLONES_PER_BIO 10

#define tracer_setup_unverified_inc(dev, minor, bdev_path, cow_path,           \
                                    cache_size)                                \
        __tracer_setup_unverified(dev, minor, bdev_path, cow_path, cache_size, \
                                  0)
#define tracer_setup_unverified_snap(dev, minor, bdev_path, cow_path,          \
                                     cache_size)                               \
        __tracer_setup_unverified(dev, minor, bdev_path, cow_path, cache_size, \
                                  1)

/************************SETUP / DESTROY FUNCTIONS************************/
int tracer_alloc(struct snap_device **dev_ptr);
void tracer_destroy(struct snap_device *dev);

int tracer_setup_active_snap(struct snap_device *dev, unsigned int minor,
                             const char *bdev_path, const char *cow_path,
                             unsigned long fallocated_space,
                             unsigned long cache_size);

int __tracer_setup_unverified(struct snap_device *dev, unsigned int minor,
                              const char *bdev_path, const char *cow_path,
                              unsigned long cache_size, int is_snap);
void dattobd_free_request_tracking_ptr(struct snap_device *dev);

/************************IOCTL TRANSITION FUNCTIONS************************/

int tracer_active_snap_to_inc(struct snap_device *old_dev);

int tracer_active_inc_to_snap(struct snap_device *old_dev, const char *cow_path,
                              unsigned long fallocated_space);

void tracer_reconfigure(struct snap_device *dev, unsigned long cache_size);

void tracer_dattobd_info(const struct snap_device *dev,
                         struct dattobd_info *info);

/************************AUTOMATIC TRANSITION FUNCTIONS************************/

void __tracer_active_to_dormant(struct snap_device *dev);

void __tracer_unverified_snap_to_active(struct snap_device *dev,
                                        const char __user *user_mount_path);

void __tracer_unverified_inc_to_active(struct snap_device *dev,
                                       const char __user *user_mount_path);

void __tracer_dormant_to_active(struct snap_device *dev,
                                const char __user *user_mount_path);

#endif /* TRACER_H_ */
