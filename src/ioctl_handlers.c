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
#include "cow_manager.h"

#ifdef HAVE_UAPI_MOUNT_H
#include <uapi/linux/mount.h>
#endif

#define verify_minor_available(minor, snap_devices) __verify_minor(minor, 0, snap_devices)
#define verify_minor_in_use_not_busy(minor, snap_devices) __verify_minor(minor, 1, snap_devices)
#define verify_minor_in_use(minor, snap_devices) __verify_minor(minor, 2, snap_devices)

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
 * @snap_devices: the array of snap devices.
 *
 * Return:
 * * 0 - successfully validated.
 * * 1 - a negative errno otherwise.
 */
static int __verify_minor(unsigned int minor, int mode, snap_device_array snap_devices)
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
static int __verify_bdev_writable(const char *bdev_path, int *out)
{
        int writable = 0;
        struct bdev_wrapper *bdev_w;
        struct super_block *sb;

        // open the base block device
        bdev_w = dattobd_blkdev_by_path(bdev_path, FMODE_READ, NULL);

        if (IS_ERR(bdev_w)) {
                *out = 0;
                return PTR_ERR(bdev_w);
        }

        sb = dattobd_get_super(bdev_w->bdev);
        if (!IS_ERR_OR_NULL(sb)) {
                writable = !(sb->s_flags & MS_RDONLY);
                dattobd_drop_super(sb);
        }

        dattobd_blkdev_put(bdev_w);
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
static int __ioctl_setup(unsigned int minor, const char *bdev_path,
                  const char *cow_path, unsigned long fallocated_space,
                  unsigned long cache_size, int is_snap, int is_reload)
{
        int ret, is_mounted;
        struct snap_device *dev = NULL;
        snap_device_array_mut snap_devices = get_snap_device_array_mut();

        LOG_DEBUG("received %s %s ioctl - %u : %s : %s",
                  (is_reload) ? "reload" : "setup", (is_snap) ? "snap" : "inc",
                  minor, bdev_path, cow_path);

        // verify that the minor number is valid
        ret = verify_minor_available(minor, snap_devices);
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
                                                       cache_size, snap_devices);
                else
                        ret = tracer_setup_unverified_snap(
                                dev, minor, bdev_path, cow_path, cache_size, snap_devices);
        } else {
                if (!is_mounted)
                        ret = tracer_setup_unverified_inc(dev, minor, bdev_path,
                                                          cow_path, cache_size, snap_devices);
                else {
                        ret = -EINVAL;
                        LOG_ERROR(ret,
                                  "illegal to setup as active incremental");
                        goto error;
                }
        }

        if (ret)
                goto error;

        put_snap_device_array_mut(snap_devices);
        return 0;

error:
        LOG_ERROR(ret, "error during setup ioctl handler");
        if (dev)
                kfree(dev);
        put_snap_device_array_mut(snap_devices);
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
static int ioctl_destroy(unsigned int minor)
{
        int ret;
        struct snap_device *dev;
        snap_device_array_mut snap_devices = get_snap_device_array_mut();

        LOG_DEBUG("received destroy ioctl - %u", minor);

        // verify that the minor number is valid
        ret = verify_minor_in_use_not_busy(minor, snap_devices);
        if (ret) {
                LOG_ERROR(ret, "error during destroy ioctl handler");
                put_snap_device_array_mut(snap_devices);
                return ret;
        }

        dev = snap_devices[minor];
        tracer_destroy(dev, snap_devices);
        kfree(dev);

        put_snap_device_array_mut(snap_devices);
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
static int ioctl_transition_inc(unsigned int minor)
{
        int ret;
        struct snap_device *dev;
        snap_device_array_mut snap_devices = get_snap_device_array_mut();

        LOG_DEBUG("received transition inc ioctl - %u", minor);

        // verify that the minor number is valid
        ret = verify_minor_in_use_not_busy(minor, snap_devices);
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

        ret = tracer_active_snap_to_inc(dev, snap_devices);
        if (ret)
                goto error;

        put_snap_device_array_mut(snap_devices);
        return 0;

error:
        LOG_ERROR(ret, "error during transition to incremental ioctl handler");
        put_snap_device_array_mut(snap_devices);
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
static int ioctl_transition_snap(unsigned int minor, const char *cow_path,
                          unsigned long fallocated_space)
{
        int ret;
        struct snap_device *dev;
        snap_device_array_mut snap_devices = get_snap_device_array_mut();

        LOG_DEBUG("received transition snap ioctl - %u : %s", minor, cow_path);

        // verify that the minor number is valid
        ret = verify_minor_in_use_not_busy(minor, snap_devices);
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

        ret = tracer_active_inc_to_snap(dev, cow_path, fallocated_space, snap_devices);
        if (ret)
                goto error;

        put_snap_device_array_mut(snap_devices);
        return 0;

error:
        LOG_ERROR(ret, "error during transition to snapshot ioctl handler");
        put_snap_device_array_mut(snap_devices);
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
static int ioctl_reconfigure(unsigned int minor, unsigned long cache_size)
{
        int ret;
        struct snap_device *dev;
        snap_device_array snap_devices = get_snap_device_array();

        LOG_DEBUG("received reconfigure ioctl - %u : %lu", minor, cache_size);

        // verify that the minor number is valid
        ret = verify_minor_in_use_not_busy(minor, snap_devices);
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

        put_snap_device_array(snap_devices);
        return 0;

error:
        LOG_ERROR(ret, "error during reconfigure ioctl handler");
        put_snap_device_array(snap_devices);
        return ret;
}

/**
 * ioctl_expand_cow_file() - Expands cow file by the specified size.
 * @size: The size in MiB to expand the cow file by.
 * @minor: An allocated device minor number.
 *
 * Return:
 * * 0 - successful.
 * * !0 - errno indicating the error.
 */
static int ioctl_expand_cow_file(uint64_t size, unsigned int minor)
{
        int ret;
        struct snap_device *dev;
        snap_device_array snap_devices = get_snap_device_array();

        LOG_DEBUG("received expand cow file ioctl - %u : %llu", minor, size);

        // verify that the minor number is valid
        ret = verify_minor_in_use(minor, snap_devices);
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
            !test_bit(ACTIVE, &dev->sd_state) ||
            test_bit(UNVERIFIED, &dev->sd_state)) {
                ret = -EINVAL;
                LOG_ERROR(ret,
                          "device specified is not in active snapshot mode");
                goto error;
        }

        ret = tracer_expand_cow_file_no_check(dev, size * 1024 * 1024);

        if(ret)
                goto error;

        put_snap_device_array(snap_devices);
        return 0;

error:
        LOG_ERROR(ret, "error during expand cow file ioctl handler");
        put_snap_device_array(snap_devices);
        return ret;
}

/**
 * ioctl_reconfigure_auto_expand() - Allows cow file to expand by the specified size during snapshot, specified number of times.
 * @step_size: The step size in MiB to expand the cow file by.
 * @reserved_space: The reserved space in MiB to keep free on the block device.
 * @minor: An allocated device minor number.
 *
 * Return:
 * * 0 - successful.
 * * !0 - errno indicating the error.
 */
static int ioctl_reconfigure_auto_expand(uint64_t step_size, uint64_t reserved_space, unsigned int minor)
{
        int ret;
        struct snap_device *dev;
        snap_device_array snap_devices = get_snap_device_array();

        LOG_DEBUG("received reconfigure auto expand ioctl - %u : %llu, %llu", minor, step_size, reserved_space);

        // verify that the minor number is valid
        ret = verify_minor_in_use(minor, snap_devices);
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
            !test_bit(ACTIVE, &dev->sd_state) ||
            test_bit(UNVERIFIED, &dev->sd_state)) {
                ret = -EINVAL;
                LOG_ERROR(ret,
                          "device specified is not in active snapshot mode");
                goto error;
        }

        if(dev->sd_cow->auto_expand == NULL){
                dev->sd_cow->auto_expand = cow_auto_expand_manager_init();
                if(IS_ERR(dev->sd_cow->auto_expand)){
                        ret = PTR_ERR(dev->sd_cow->auto_expand);
                        LOG_ERROR(ret, "error initializing auto expand manager");
                        goto error;
                }
        }

        ret = cow_auto_expand_manager_reconfigure(dev->sd_cow->auto_expand, step_size, reserved_space);

        if(ret)
                goto error;

        put_snap_device_array(snap_devices);
        return 0;

error:
        LOG_ERROR(ret, "error during reconfigure auto expand ioctl handler");
        put_snap_device_array(snap_devices);
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
static int ioctl_dattobd_info(struct dattobd_info *info)
{
        int ret;
        struct snap_device *dev;
        snap_device_array snap_devices = get_snap_device_array();

        LOG_DEBUG("received dattobd info ioctl - %u", info->minor);

        // verify that the minor number is valid
        ret = verify_minor_in_use(info->minor, snap_devices);
        if (ret)
                goto error;

        dev = snap_devices[info->minor];

        tracer_dattobd_info(dev, info);

        put_snap_device_array(snap_devices);
        return 0;

error:
        LOG_ERROR(ret, "error during reconfigure ioctl handler");
        put_snap_device_array(snap_devices);
        return ret;
}

/**
 * get_free_minor() - Determine the next available device minor number.
 *
 * Return: The next available minor number or an errno indicating the error.
 */
static int get_free_minor(void)
{
        struct snap_device *dev;
        int i;
        bool found = false;

        snap_device_array snap_devices = get_snap_device_array();

        tracer_for_each_full(dev, i)
        {
                if (!dev){
                        found = true;
                        break;
                }
        }

        put_snap_device_array(snap_devices);

        return (found ? i : -ENOENT);
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
        struct expand_cow_file_params *expand_params = NULL;
        struct reconfigure_auto_expand_params *reconfigure_auto_expand_params = NULL;

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
        case IOCTL_EXPAND_COW_FILE:
                // get params from user space
                expand_params = kmalloc(sizeof(struct expand_cow_file_params), GFP_KERNEL);
                ret = copy_from_user(expand_params, (struct expand_cow_file_params __user *)arg, sizeof(struct expand_cow_file_params));
                if (ret){
                        ret = -EFAULT;
                        LOG_ERROR(ret, "error copying expand_cow_file_params from user space");
                        break;
                }

                ret = ioctl_expand_cow_file(expand_params->size, expand_params->minor);
                if (ret){
                        break;
                }

                break;
        case IOCTL_RECONFIGURE_AUTO_EXPAND:
                // get params from user space
                reconfigure_auto_expand_params = kmalloc(sizeof(struct reconfigure_auto_expand_params), GFP_KERNEL);
                ret = copy_from_user(reconfigure_auto_expand_params, (struct reconfigure_auto_expand_params __user *)arg, sizeof(struct reconfigure_auto_expand_params));
                if (ret){
                        ret = -EFAULT;
                        LOG_ERROR(ret, "error copying reconfigure_auto_expand_params from user space");
                        break;
                }

                ret = ioctl_reconfigure_auto_expand(reconfigure_auto_expand_params->step_size, reconfigure_auto_expand_params->reserved_space, reconfigure_auto_expand_params->minor);
                if (ret){
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
