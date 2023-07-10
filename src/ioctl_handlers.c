// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (C) 2022 Datto Inc.
 */

#include "ioctl_handlers.h"

#include "blkdev.h"
#include "dattobd.h"
#include "hints.h"
#include "includes.h"
#include "logging.h"
#include "module_control.h"
#include "snap_device.h"
#include "tracer.h"
#include "tracer_helper.h"
#include "userspace_copy_helpers.h"

#ifdef HAVE_UAPI_MOUNT_H
#include <uapi/linux/mount.h>
#endif

#define verify_minor_available(minor) __verify_minor(minor, 0)
#define verify_minor_in_use_not_busy(minor) __verify_minor(minor, 1)
#define verify_minor_in_use(minor) __verify_minor(minor, 2)

#define ioctl_setup_snap(minor, bdev_path, cow_path, fallocated_space,         \
                         cache_size)                                           \
        __ioctl_setup(minor, bdev_path, cow_path, fallocated_space,            \
                      cache_size, 1, 0)
#define ioctl_reload_snap(minor, bdev_path, cow_path, cache_size)              \
        __ioctl_setup(minor, bdev_path, cow_path, 0, cache_size, 1, 1)
#define ioctl_reload_inc(minor, bdev_path, cow_path, cache_size)               \
        __ioctl_setup(minor, bdev_path, cow_path, 0, cache_size, 0, 1)

struct mutex ioctl_mutex;

/************************IOCTL HANDLER FUNCTIONS************************/

/**
 * __verify_minor() - Verify the supplied minor device number according to the
 *                    requested mode.
 *
 * @minor: the minor number to check.
 * @mode: what to verify:
 * * 0: the minor is not in allocated.
 * * 1: the minor is allocated and is not busy.
 * * 2: the minor is allocated whether busy or not.
 *
 * Return:
 * * 0 - successfully validated.
 * * 1 - a negative errno otherwise.
 */
static int __verify_minor(unsigned int minor, int mode)
{
        // check minor number is within range
        if (minor >= dattobd_max_snap_devices) {
                LOG_ERROR(-EINVAL, "minor number specified is out of range");
                return -EINVAL;
        }

        // check if the device is in use
        if (mode == 0) {
                if (snap_devices[minor]) {
                        LOG_ERROR(-EBUSY, "device specified already exists");
                        return -EBUSY;
                }
        } else {
                if (!snap_devices[minor]) {
                        LOG_ERROR(-ENOENT, "device specified does not exist");
                        return -ENOENT;
                }

                // check that the device is not busy if we care
                if (mode == 1 && atomic_read(&snap_devices[minor]->sd_refs)) {
                        LOG_ERROR(-EBUSY, "device specified is busy");
                        return -EBUSY;
                }
        }

        return 0;
}

/**
 * __verify_bdev_writable() - Determines if the block device is writable.
 *
 * @bdev_path: the path to the block device.
 * @out: the result
 *
 * Return:
 * * 0 - successful, @out contains a boolean value indicating whether the bdev
 * is writable.
 * * !0 - errno indicating the error.
 */
int __verify_bdev_writable(const char *bdev_path, int *out)
{
        int writable = 0;
        struct block_device *bdev;
        struct super_block *sb;

        // open the base block device
        bdev = blkdev_get_by_path(bdev_path, FMODE_READ, NULL);

        if (IS_ERR(bdev)) {
                *out = 0;
                return PTR_ERR(bdev);
        }

        sb = dattobd_get_super(bdev);
        if (sb) {
                writable = !(sb->s_flags & MS_RDONLY);
                dattobd_drop_super(sb);
        }

        dattobd_blkdev_put(bdev);
        *out = writable;
        return 0;
}

/**
 * __ioctl_setup() - Sets up for tracking for mounted or unmounted in
 * reload/setup mode as appropriate for the current mount state.
 * block devices
 *
 * @minor: An unallocated device minor number.
 * @bdev_path: The path to the block device.
 * @cow_path: The path to the cow file.
 * @fallocated_space: The specific amount of space to use if non-zero,
 *                    default otherwise.
 * @cache_size: The specific amount of RAM to use for cache, default otherwise.
 * @is_snap: snapshot or incremental.
 * @is_reload: is a reload or a new setup.
 *
 * Return:
 * * 0 - successfully set up.
 * * !0 - errno indicating the error.
 */
int __ioctl_setup(unsigned int minor, const char *bdev_path,
                  const char *cow_path, unsigned long fallocated_space,
                  unsigned long cache_size, int is_snap, int is_reload)
{
        int ret, is_mounted;
        struct snap_device *dev = NULL;

        LOG_DEBUG("received %s %s ioctl - %u : %s : %s",
                  (is_reload) ? "reload" : "setup", (is_snap) ? "snap" : "inc",
                  minor, bdev_path, cow_path);

        // verify that the minor number is valid
        ret = verify_minor_available(minor);
        if (ret){
                 LOG_ERROR(ret, "verify_minor_available");
                goto error;
        }
        // check if block device is mounted
        ret = __verify_bdev_writable(bdev_path, &is_mounted);
        if (ret){
                 LOG_ERROR(ret, "__verify_bdev_writable");
                goto error;
        }
        // check that reload / setup command matches current mount state
        if (is_mounted && is_reload) {
                ret = -EINVAL;
                LOG_ERROR(ret, "illegal to perform reload while mounted");
                goto error;
        } else if (!is_mounted && !is_reload) {
                ret = -EINVAL;
                LOG_ERROR(ret, "illegal to perform setup while unmounted");
                goto error;
        }

        // allocate the tracing struct
        ret = tracer_alloc(&dev);
        if (ret){
                 LOG_ERROR(ret, "tracer_alloc");
                goto error;
        }
        // route to the appropriate setup function
        if (is_snap) {
                if (is_mounted)
                        ret = tracer_setup_active_snap(dev, minor, bdev_path,
                                                       cow_path,
                                                       fallocated_space,
                                                       cache_size);
                else
                        ret = tracer_setup_unverified_snap(
                                dev, minor, bdev_path, cow_path, cache_size);
        } else {
                if (!is_mounted)
                        ret = tracer_setup_unverified_inc(dev, minor, bdev_path,
                                                          cow_path, cache_size);
                else {
                        ret = -EINVAL;
                        LOG_ERROR(ret,
                                  "illegal to setup as active incremental");
                        goto error;
                }
        }

        if (ret)
                goto error;

        return 0;

error:
        LOG_ERROR(ret, "error during setup ioctl handler");
        if (dev)
                kfree(dev);
        return ret;
}

/**
 * ioctl_destroy() - Tears down an allocated minor device as long as it is not
 *                   referenced(busy).
 *
 * @minor: An allocated device minor number.
 *
 * Return:
 * * 0 - successful.
 * * !0 - errno indicating the error.
 */
int ioctl_destroy(unsigned int minor)
{
        int ret;
        struct snap_device *dev;

        LOG_DEBUG("received destroy ioctl - %u", minor);

        // verify that the minor number is valid
        ret = verify_minor_in_use_not_busy(minor);
        if (ret) {
                LOG_ERROR(ret, "error during destroy ioctl handler");
                return ret;
        }

        dev = snap_devices[minor];
        tracer_destroy(snap_devices[minor]);
        kfree(dev);

        return 0;
}

/**
 * ioctl_transition_inc() - Transitions the snapshot device to incremental
 *                          tracking.
 *
 * @minor: An allocated device minor number.
 *
 * Return:
 * * 0 - successful.
 * * !0 - errno indicating the error.
 */
int ioctl_transition_inc(unsigned int minor)
{
        int ret;
        struct snap_device *dev;

        LOG_DEBUG("received transition inc ioctl - %u", minor);

        // verify that the minor number is valid
        ret = verify_minor_in_use_not_busy(minor);
        if (ret)
                goto error;

        dev = snap_devices[minor];

        // check that the device is not in the fail state
        if (tracer_read_fail_state(dev)) {
                ret = -EINVAL;
                LOG_ERROR(ret, "device specified is in the fail state");
                goto error;
        }

        // check that tracer is in active snapshot state
        if (!test_bit(SNAPSHOT, &dev->sd_state) ||
            !test_bit(ACTIVE, &dev->sd_state)) {
                ret = -EINVAL;
                LOG_ERROR(ret,
                          "device specified is not in active snapshot mode");
                goto error;
        }

        ret = tracer_active_snap_to_inc(dev);
        if (ret)
                goto error;

        return 0;

error:
        LOG_ERROR(ret, "error during transition to incremental ioctl handler");
        return ret;
}

/**
 * ioctl_transition_snap() - Transitions from active incremental mode to
 *                           snapshot mode.
 *
 * @minor: An allocated device minor number.
 * @cow_path: The path to the cow file.
 * @fallocated_space: The specific amount of space to use if non-zero,
 *                    default otherwise.
 *
 * As a result COW data will be used during snapshotting to preserve snapshot
 * data while the live volume might change.
 *
 * Return:
 * * 0 - successful.
 * * !0 - errno indicating the error.
 */
int ioctl_transition_snap(unsigned int minor, const char *cow_path,
                          unsigned long fallocated_space)
{
        int ret;
        struct snap_device *dev;

        LOG_DEBUG("received transition snap ioctl - %u : %s", minor, cow_path);

        // verify that the minor number is valid
        ret = verify_minor_in_use_not_busy(minor);
        if (ret)
                goto error;

        dev = snap_devices[minor];

        // check that the device is not in the fail state
        if (tracer_read_fail_state(dev)) {
                ret = -EINVAL;
                LOG_ERROR(ret, "device specified is in the fail state");
                goto error;
        }

        // check that tracer is in active incremental state
        if (test_bit(SNAPSHOT, &dev->sd_state) ||
            !test_bit(ACTIVE, &dev->sd_state)) {
                ret = -EINVAL;
                LOG_ERROR(ret,
                          "device specified is not in active incremental mode");
                goto error;
        }

        ret = tracer_active_inc_to_snap(dev, cow_path, fallocated_space);
        if (ret)
                goto error;

        return 0;

error:
        LOG_ERROR(ret, "error during transition to snapshot ioctl handler");
        return ret;
}

/**
 * ioctl_reconfigure() - Reconfigures the cache size to match the supplied
 *                       value.
 * @minor: An allocated device minor number.
 * @cache_size: The specific amount of RAM to use for cache, default otherwise.
 *
 * Return:
 * * 0 - successful.
 * * !0 - errno indicating the error.
 */
int ioctl_reconfigure(unsigned int minor, unsigned long cache_size)
{
        int ret;
        struct snap_device *dev;

        LOG_DEBUG("received reconfigure ioctl - %u : %lu", minor, cache_size);

        // verify that the minor number is valid
        ret = verify_minor_in_use_not_busy(minor);
        if (ret)
                goto error;

        dev = snap_devices[minor];

        // check that the device is not in the fail state
        if (tracer_read_fail_state(dev)) {
                ret = -EINVAL;
                LOG_ERROR(ret, "device specified is in the fail state");
                goto error;
        }

        tracer_reconfigure(dev, cache_size);

        return 0;

error:
        LOG_ERROR(ret, "error during reconfigure ioctl handler");
        return ret;
}

/**
 * ioctl_dattobd_info() - Stores relevant, current &struct snap_device state
 *                        in @info.
 *
 * @info: A @struct dattobd_info object pointer.
 *
 * Return:
 * * 0 - successful.
 * * !0 - errno indicating the error.
 */
int ioctl_dattobd_info(struct dattobd_info *info)
{
        int ret;
        struct snap_device *dev;

        LOG_DEBUG("received dattobd info ioctl - %u", info->minor);

        // verify that the minor number is valid
        ret = verify_minor_in_use(info->minor);
        if (ret)
                goto error;

        dev = snap_devices[info->minor];

        tracer_dattobd_info(dev, info);

        return 0;

error:
        LOG_ERROR(ret, "error during reconfigure ioctl handler");
        return ret;
}

/**
 * get_free_minor() - Determine the next available device minor number.
 *
 * Return: The next available minor number or an errno indicating the error.
 */
int get_free_minor(void)
{
        struct snap_device *dev;
        int i;

        tracer_for_each_full(dev, i)
        {
                if (!dev)
                        return i;
        }

        return -ENOENT;
}

/**
 * ctrl_ioctl() - Dispatches the supplied IOCTL command to the appropriate
 *                handler function(s).
 *
 * @filp: The &struct file object pointer associated with the misc driver
 *        that handles the IOCTL interface.
 * @cmd: The IOCTL command.
 * @arg: A user space argument supplied with the IOCTL call.
 *
 * Return:
 * * 0 - successful.
 * * !0 - errno indicating the error.
 */
long ctrl_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
        int ret, idx;
        char *bdev_path = NULL;
        char *cow_path = NULL;
        struct dattobd_info *info = NULL;
        unsigned int minor = 0;
        unsigned long fallocated_space = 0, cache_size = 0;

        LOG_DEBUG("ioctl command received: %i", cmd);
        mutex_lock(&ioctl_mutex);

        switch (cmd) {
        case IOCTL_SETUP_SNAP:
                // get params from user space
                ret = get_setup_params((struct setup_params __user *)arg,
                                       &minor, &bdev_path, &cow_path,
                                       &fallocated_space, &cache_size);
                if (ret)
                        break;

                ret = ioctl_setup_snap(minor, bdev_path, cow_path,
                                       fallocated_space, cache_size);
                if (ret)
                        break;

                break;
        case IOCTL_RELOAD_SNAP:
                // get params from user space
                ret = get_reload_params((struct reload_params __user *)arg,
                                        &minor, &bdev_path, &cow_path,
                                        &cache_size);
                if (ret)
                        break;

                ret = ioctl_reload_snap(minor, bdev_path, cow_path, cache_size);
                if (ret)
                        break;

                break;
        case IOCTL_RELOAD_INC:
                // get params from user space
                ret = get_reload_params((struct reload_params __user *)arg,
                                        &minor, &bdev_path, &cow_path,
                                        &cache_size);
                if (ret)
                        break;

                ret = ioctl_reload_inc(minor, bdev_path, cow_path, cache_size);
                if (ret)
                        break;

                break;
        case IOCTL_DESTROY:
                // get minor from user space
                ret = get_user(minor, (unsigned int __user *)arg);
                if (ret) {
                        LOG_ERROR(ret,
                                  "error copying minor number from user space");
                        break;
                }

                ret = ioctl_destroy(minor);
                if (ret)
                        break;

                break;
        case IOCTL_TRANSITION_INC:
                // get minor from user space
                ret = get_user(minor, (unsigned int __user *)arg);
                if (ret) {
                        LOG_ERROR(ret,
                                  "error copying minor number from user space");
                        break;
                }

                ret = ioctl_transition_inc(minor);
                if (ret)
                        break;

                break;
        case IOCTL_TRANSITION_SNAP:
                // get params from user space
                ret = get_transition_snap_params(
                        (struct transition_snap_params __user *)arg, &minor,
                        &cow_path, &fallocated_space);
                if (ret)
                        break;

                ret = ioctl_transition_snap(minor, cow_path, fallocated_space);
                if (ret)
                        break;

                break;
        case IOCTL_RECONFIGURE:
                // get params from user space
                ret = get_reconfigure_params(
                        (struct reconfigure_params __user *)arg, &minor,
                        &cache_size);
                if (ret)
                        break;

                ret = ioctl_reconfigure(minor, cache_size);
                if (ret)
                        break;

                break;
        case IOCTL_DATTOBD_INFO:
                // get params from user space
                info = kmalloc(sizeof(struct dattobd_info), GFP_KERNEL);
                if (!info) {
                        ret = -ENOMEM;
                        LOG_ERROR(ret,
                                  "error allocating memory for dattobd info");
                        break;
                }

                ret = copy_from_user(info, (struct dattobd_info __user *)arg,
                                     sizeof(struct dattobd_info));
                if (ret) {
                        ret = -EFAULT;
                        LOG_ERROR(ret, "error copying dattobd info struct from "
                                       "user space");
                        break;
                }

                ret = ioctl_dattobd_info(info);
                if (ret)
                        break;

                ret = copy_to_user((struct dattobd_info __user *)arg, info,
                                   sizeof(struct dattobd_info));
                if (ret) {
                        ret = -EFAULT;
                        LOG_ERROR(
                                ret,
                                "error copying dattobd info struct to user space");
                        break;
                }

                break;
        case IOCTL_GET_FREE:
                idx = get_free_minor();
                if (idx < 0) {
                        ret = idx;
                        LOG_ERROR(ret, "no free devices");
                        break;
                }

                ret = copy_to_user((int __user *)arg, &idx, sizeof(idx));
                if (ret) {
                        ret = -EFAULT;
                        LOG_ERROR(ret, "error copying minor to user space");
                        break;
                }

                break;
        default:
                ret = -EINVAL;
                LOG_ERROR(ret, "invalid ioctl called");
                break;
        }

        LOG_DEBUG("minor range = %u - %u", lowest_minor, highest_minor);
        mutex_unlock(&ioctl_mutex);

        if (bdev_path)
                kfree(bdev_path);
        if (cow_path)
                kfree(cow_path);
        if (info)
                kfree(info);

        return ret;
}
