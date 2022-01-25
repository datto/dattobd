// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (C) 2022 Datto Inc.
 */

#ifndef MODULE_CONTROL_H_
#define MODULE_CONTROL_H_

// name macros
#define INFO_PROC_FILE "datto-info"
#define DRIVER_NAME "datto"
#define CONTROL_DEVICE_NAME "datto-ctl"
#define SNAP_DEVICE_NAME "datto%u"
#define SNAP_COW_THREAD_NAME_FMT "datto_snap_cow%d"
#define SNAP_MRF_THREAD_NAME_FMT "datto_snap_mrf%d"
#define INC_THREAD_NAME_FMT "datto_inc%d"

// global module parameters
extern int dattobd_may_hook_syscalls;
extern unsigned long dattobd_cow_max_memory_default;
extern unsigned int dattobd_cow_fallocate_percentage_default;
extern unsigned int dattobd_max_snap_devices;

extern unsigned int highest_minor;
extern unsigned int lowest_minor;
extern int major;

extern struct snap_device **snap_devices;

#endif /* MODULE_CONTROL_H_ */
