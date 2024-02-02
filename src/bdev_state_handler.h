// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (C) 2023 Datto Inc.
 */

#ifndef BDEV_STATE_HANDLER_H
#define BDEV_STATE_HANDLER_H


#include "blkdev.h"
#include "cow_manager.h"
#include "filesystem.h"
#include "includes.h"
#include "ioctl_handlers.h"
#include "logging.h"
#include "paging_helper.h"
#include "snap_device.h"
#include "task_helper.h"
#include "tracer.h"
#include "tracer_helper.h"

#ifdef HAVE_UAPI_MOUNT_H
#include <uapi/linux/mount.h>
#endif


int handle_bdev_mount_event(const char *dir_name, int follow_flags,
                            unsigned int *idx_out, int mount_writable);

void post_umount_check(int dormant_ret, int umount_ret, unsigned int idx,
                       const char *dir_name);



#endif // BDEV_STATE_HANDLER_H