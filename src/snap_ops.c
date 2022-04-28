// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (C) 2022 Datto Inc.
 */

#include "snap_ops.h"

#include "includes.h"
#include "logging.h"
#include "snap_device.h"

/**
 * __tracer_add_ref() - Adds a reference to &snap_device->sd_refs.
 *
 * @dev: The &struct snap_device object pointer.
 * @ref_cnt: The number of references to be added.
 *
 * Return:
 * 0 - success
 * !0 - an errno indicating the error
 */
static int __tracer_add_ref(struct snap_device *dev, int ref_cnt)
{
        int ret = 0;

        if (!dev) {
                ret = -EFAULT;
                LOG_ERROR(ret, "requested snapshot device does not exist");
                goto error;
        }

        atomic_add(ref_cnt, &dev->sd_refs);

error:
        return ret;
}

#define __tracer_open(dev) __tracer_add_ref(dev, 1)
#define __tracer_close(dev) __tracer_add_ref(dev, -1)

#ifdef HAVE_BDOPS_OPEN_INODE
//#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,28)

static int snap_open(struct inode *inode, struct file *filp)
{
        return __tracer_open(inode->i_bdev->bd_disk->private_data);
}

static int snap_release(struct inode *inode, struct file *filp)
{
        return __tracer_close(inode->i_bdev->bd_disk->private_data);
}
#elif defined HAVE_BDOPS_OPEN_INT
//#elif LINUX_VERSION_CODE < KERNEL_VERSION(3,10,0)

static int snap_open(struct block_device *bdev, fmode_t mode)
{
        return __tracer_open(bdev->bd_disk->private_data);
}

static int snap_release(struct gendisk *gd, fmode_t mode)
{
        return __tracer_close(gd->private_data);
}
#else
static int snap_open(struct block_device *bdev, fmode_t mode)
{
        return __tracer_open(bdev->bd_disk->private_data);
}

static void snap_release(struct gendisk *gd, fmode_t mode)
{
        __tracer_close(gd->private_data);
}
#endif

static const struct block_device_operations snap_ops = {
        .owner = THIS_MODULE,
        .open = snap_open,
        .release = snap_release,
};

const struct block_device_operations *get_snap_ops(void)
{
        return &snap_ops;
}
