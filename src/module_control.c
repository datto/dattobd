// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (C) 2022 Datto Inc.
 */

#include "module_control.h"

#include "blkdev.h"
#include "dattobd.h"
#include "includes.h"
#include "ioctl_handlers.h"
#include "kernel-config.h"
#include "logging.h"
#include "proc_seq_file.h"
#include "snap_device.h"
#include "snap_ops.h"
#include "system_call_hooking.h"
#include "tracer_helper.h"
#include "tracer.h"

// current lowest supported kernel = 2.6.18

// basic information
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Tom Caputi");
MODULE_DESCRIPTION("Kernel module for supporting block device snapshots and "
                   "incremental backups.");
MODULE_VERSION(DATTOBD_VERSION);

#define DATTOBD_DEFAULT_SNAP_DEVICES 24
#define DATTOBD_MAX_SNAP_DEVICES 255

// global module parameters
int dattobd_may_hook_syscalls = 1;
unsigned long dattobd_cow_max_memory_default = (300 * 1024 * 1024);
unsigned int dattobd_cow_fallocate_percentage_default = 10;
unsigned int dattobd_max_snap_devices = DATTOBD_DEFAULT_SNAP_DEVICES;

unsigned int highest_minor;
unsigned int lowest_minor;
int major;

int dattobd_debug = 0;

struct snap_device **snap_devices;

/*********************************MACRO/PARAMETER
 * DEFINITIONS*******************************/

module_param_named(may_hook_syscalls, dattobd_may_hook_syscalls, int, S_IRUGO);
MODULE_PARM_DESC(may_hook_syscalls,
                 "if true, allows the kernel module to find and alter the "
                 "system call table to allow tracing to work across remounts");

module_param_named(cow_max_memory_default, dattobd_cow_max_memory_default,
                   ulong, 0);
MODULE_PARM_DESC(cow_max_memory_default,
                 "default maximum cache size (in bytes)");

module_param_named(cow_fallocate_percentage_default,
                   dattobd_cow_fallocate_percentage_default, uint, 0);
MODULE_PARM_DESC(
        cow_fallocate_percentage_default,
        "default space allocated to the cow file (as integer percentage)");

module_param_named(max_snap_devices, dattobd_max_snap_devices, uint, S_IRUGO);
MODULE_PARM_DESC(max_snap_devices, "maximum number of tracers available");

module_param_named(debug, dattobd_debug, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(debug, "enables debug logging");

static struct proc_dir_entry *info_proc;

static const struct file_operations snap_control_fops = {
        .owner = THIS_MODULE,
        .unlocked_ioctl = ctrl_ioctl,
        .compat_ioctl = ctrl_ioctl,
        .open = nonseekable_open,
        .llseek = noop_llseek,
};

static struct miscdevice snap_control_device = {
        .minor = MISC_DYNAMIC_MINOR,
        .name = CONTROL_DEVICE_NAME,
        .fops = &snap_control_fops,
};

#ifndef HAVE_PROC_CREATE
//#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,25)
static inline struct proc_dir_entry *
proc_create(const char *name, mode_t mode, struct proc_dir_entry *parent,
            const struct file_operations *proc_fops)
{
        struct proc_dir_entry *ent;

        ent = create_proc_entry(name, mode, parent);
        if (!ent)
                goto error;

        ent->proc_fops = proc_fops;

        return ent;

error:
        return NULL;
}
#endif

/************************MODULE SETUP AND DESTROY************************/

static void agent_exit(void)
{
        LOG_DEBUG("module exit");

        restore_system_call_table();

        // unregister control device
        LOG_DEBUG("unregistering control device");
        misc_deregister(&snap_control_device);

        // unregister proc info file
        LOG_DEBUG("unregistering /proc file");
        remove_proc_entry(INFO_PROC_FILE, NULL);

        // destroy our snap devices
        LOG_DEBUG("destroying snap devices");
        if (snap_devices) {
                int i;
                struct snap_device *dev;

                tracer_for_each(dev, i)
                {
                        if (dev) {
                                LOG_DEBUG("destroying minor - %d", i);
                                tracer_destroy(dev);
                        }
                }
                kfree(snap_devices);
                snap_devices = NULL;
        }

        // unregister our block device driver
        LOG_DEBUG("unregistering device driver from the kernel");
        unregister_blkdev(major, DRIVER_NAME);
}
module_exit(agent_exit);

static int __init agent_init(void)
{
        int ret;

        LOG_DEBUG("module init");

        // init ioctl mutex
        mutex_init(&ioctl_mutex);

        // init minor range
        if (dattobd_max_snap_devices == 0 ||
            dattobd_max_snap_devices > DATTOBD_MAX_SNAP_DEVICES) {
                const unsigned int nr_devices =
                        dattobd_max_snap_devices == 0 ?
                                DATTOBD_DEFAULT_SNAP_DEVICES :
                                DATTOBD_MAX_SNAP_DEVICES;
                LOG_WARN(
                        "invalid number of snapshot devices (%u), setting to %u",
                        dattobd_max_snap_devices, nr_devices);
                dattobd_max_snap_devices = nr_devices;
        }

        highest_minor = 0;
        lowest_minor = dattobd_max_snap_devices - 1;

        // get a major number for the driver
        LOG_DEBUG("get major number");
        major = register_blkdev(0, DRIVER_NAME);
        if (major <= 0) {
                ret = -EBUSY;
                LOG_ERROR(ret, "error requesting major number from the kernel");
                goto error;
        }

        // allocate global device array
        LOG_DEBUG("allocate global device array");
        snap_devices =
                kzalloc(dattobd_max_snap_devices * sizeof(struct snap_device *),
                        GFP_KERNEL);
        if (!snap_devices) {
                ret = -ENOMEM;
                LOG_ERROR(ret, "error allocating global device array");
                goto error;
        }

        // register proc file
        LOG_DEBUG("registering proc file");
        info_proc = proc_create(INFO_PROC_FILE, 0, NULL, get_proc_fops());
        if (!info_proc) {
                ret = -ENOENT;
                LOG_ERROR(ret, "error registering proc file");
                goto error;
        }

        // register control device
        LOG_DEBUG("registering control device");
        ret = misc_register(&snap_control_device);
        if (ret) {
                LOG_ERROR(ret, "error registering control device");
                goto error;
        }

        if (dattobd_may_hook_syscalls)
                (void)hook_system_call_table();

        return 0;

error:
        agent_exit();
        return ret;
}
module_init(agent_init);
