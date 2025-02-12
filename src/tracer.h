// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (C) 2022 Datto Inc.
 */

#ifndef TRACER_H_
#define TRACER_H_

#include "mrf.h"
#include "snap_device.h"

struct request_queue;
struct dattobd_info;
struct snap_device;
struct bio;

//maximum number of clones per traced bio
#define MAX_CLONES_PER_BIO 10

#define tracer_setup_unverified_inc(dev, minor, bdev_path, cow_path,           \
                                    cache_size, snap_devices)                                \
        __tracer_setup_unverified(dev, minor, bdev_path, cow_path, cache_size, \
                                  0, snap_devices)
#define tracer_setup_unverified_snap(dev, minor, bdev_path, cow_path,          \
                                     cache_size, snap_devices)                               \
        __tracer_setup_unverified(dev, minor, bdev_path, cow_path, cache_size, \
                                  1, snap_devices)

/************************SETUP / DESTROY FUNCTIONS************************/
int tracer_alloc(struct snap_device **dev_ptr);
void tracer_destroy(struct snap_device *dev, snap_device_array_mut snap_devices);

int tracer_setup_active_snap(struct snap_device *dev, unsigned int minor,
                             const char *bdev_path, const char *cow_path,
                             unsigned long fallocated_space,
                             unsigned long cache_size, snap_device_array_mut snap_devices);

int __tracer_setup_unverified(struct snap_device *dev, unsigned int minor,
                              const char *bdev_path, const char *cow_path,
                              unsigned long cache_size, int is_snap, snap_device_array_mut snap_devices);
void dattobd_free_request_tracking_ptr(struct snap_device *dev);

/************************IOCTL TRANSITION FUNCTIONS************************/

int tracer_active_snap_to_inc(struct snap_device *old_dev, snap_device_array_mut snap_devices);

int tracer_active_inc_to_snap(struct snap_device *old_dev, const char *cow_path,
                              unsigned long fallocated_space, snap_device_array_mut snap_devices);

void tracer_reconfigure(struct snap_device *dev, unsigned long cache_size);

void tracer_dattobd_info(const struct snap_device *dev,
                         struct dattobd_info *info);

int tracer_expand_cow_file_no_check(struct snap_device *dev, uint64_t size);

/************************AUTOMATIC TRANSITION FUNCTIONS************************/

void __tracer_active_to_dormant(struct snap_device *dev);

void __tracer_unverified_snap_to_active(struct snap_device *dev,
                                        const char __user *user_mount_path, snap_device_array_mut snap_devices);

void __tracer_unverified_inc_to_active(struct snap_device *dev,
                                       const char __user *user_mount_path, snap_device_array_mut snap_devices);

void __tracer_dormant_to_active(struct snap_device *dev,
                                const char __user *user_mount_path);

#endif /* TRACER_H_ */
