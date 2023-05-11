// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (C) 2022 Datto Inc.
 */

#include "module_control.h"

#include "dattobd.h"
#include "includes.h"
#include "callback_refs.h"
#include "ioctl_handlers.h"
#include "logging.h"
#include "proc_seq_file.h"
#include "snap_device.h"
#include "tracer.h"
#include "tracer_helper.h"
#include "ftrace_hooking.h"

// current lowest supported kernel = 2.6.18

// basic information
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Tom Caputi");
MODULE_DESCRIPTION("Kernel module for supporting block device snapshots and "
                   "incremental backups.");
MODULE_VERSION(DATTOBD_VERSION);

#define DATTOBD_DEFAULT_SNAP_DEVICES 24
#define DATTOBD_MAX_SNAP_DEVICES 255

unsigned int highest_minor;
unsigned int lowest_minor;
int major;

struct snap_device **snap_devices;

/*
 * Global module parameters
 */
int dattobd_may_hook_syscalls = 1;
unsigned long dattobd_cow_max_memory_default = (300 * 1024 * 1024);
unsigned int dattobd_cow_fallocate_percentage_default = 10;
unsigned int dattobd_max_snap_devices = DATTOBD_DEFAULT_SNAP_DEVICES;
int dattobd_debug = 1;

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

/**
 * proc_create() - Creates the datto info file in /proc.
 * @name: The name of the /proc file.
 * @mode: The mode of the /proc file.
 * @parent: The parent directory, NULL for default.
 * @proc_fops: The file ops structure.
 *
 * Return:
 * The proc dir entry.
 */
static struct proc_dir_entry *
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

/**
 * unregister_ioctl_control_interface() - Tears down the miscellaneous device
 * driver created to provide user space with an IOCTL API to control this block
 * device driver.
 */
static void unregister_ioctl_control_interface(void)
{
        LOG_DEBUG("unregistering control device");
        misc_deregister(&snap_control_device);
}

/**
 * unregister_sequential_file_in_proc - Tears down the sequential file in /proc.
 */
static void unregister_sequential_file_in_proc(void)
{
        LOG_DEBUG("unregistering /proc file");
        remove_proc_entry(INFO_PROC_FILE, NULL);
}

/**
 * deallocate_snap_devices_array() - Deallocates the global @snap_devices array
 * and any allocated
 * @snap_device entries.
 */
static void deallocate_snap_devices_array(void)
{
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
}

/**
 * unregister_blkdev_from_kernel() - The dattobd device driver will be
 * unregistered with the kernel
 */
static void unregister_blkdev_from_kernel(void)
{
        LOG_DEBUG("unregistering device driver from the kernel");
        unregister_blkdev(major, DRIVER_NAME);
}

/**
 * agent_exit() - The function used to destroy the dattobd module.
 */
static void agent_exit(void)
{
        LOG_DEBUG("module exit");

        //restore_system_call_table();

        unregister_tracer_filter();

        unregister_ftrace_hooks();

        unregister_ioctl_control_interface();

        unregister_sequential_file_in_proc();

        deallocate_snap_devices_array();

        unregister_blkdev_from_kernel();
}

module_exit(agent_exit);

/**
 * calc_max_snap_devices_and_init_minor_range() - Determines the maximum allowed
 * snap devices and initializes the minor range.
 */
static void calc_max_snap_devices_and_init_minor_range(void)
{
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
}

/**
 * register_blkdev_and_get_major_number() - The dattobd device driver will be
 * registered with the kernel and ready for business after this call.
 *
 * Return:
 * * 0 - success
 * * !0 - Not successful, the value gives some indication of what went wrong.
 */
static int register_blkdev_and_get_major_number(void)
{
        // dynamically get a major number for the driver
        LOG_DEBUG("get major number");
        major = register_blkdev(0, DRIVER_NAME);
        if (major <= 0) {
                return -EBUSY;
        }

        return 0;
}

/**
 * allocate_snap_devices_array() - Allocates enough memory to store the maximum
 * number of supported snap devices.
 *
 * Return:
 * * 0 - success
 * * !0 - Not successful, the value gives some indication of what went wrong.
 */
static int allocate_snap_devices_array(void)
{
        LOG_DEBUG("allocate global device array");
        snap_devices =
                kzalloc(dattobd_max_snap_devices * sizeof(struct snap_device *),
                        GFP_KERNEL);
        if (!snap_devices) {
                return -ENOMEM;
        }

        return 0;
}

/**
 * register_sequential_file_in_proc - Sets up the sequential file in /proc.
 * The sequential file is used to provide access to information about snap
 * devices managed by this driver.
 *
 * Return:
 * * 0 - success
 * * !0 - Not successful, the value gives some indication of what went wrong.
 */
static int register_sequential_file_in_proc(void)
{
        LOG_DEBUG("registering proc file");
        info_proc = proc_create(INFO_PROC_FILE, 0, NULL, get_proc_fops());
        if (!info_proc) {
                return -ENOENT;
        }

        return 0;
}

/**
 * register_ioctl_control_interface() - Creates a miscellaneous device driver
 *                                      that provides an IOCTL control API for
 *                                      this block device driver. After this
 *                                      call there will be a device file
 *                                      created in /dev that can be used to
 *                                      issue IOCTL commands to, for instance,
 *                                      create or remove snapshots.
 *
 * Return:
 * * 0 - success
 * * !0 - Not successful, the value gives some indication of what went wrong.
 */
static int register_ioctl_control_interface(void)
{
        LOG_DEBUG("registering control device");
        return misc_register(&snap_control_device);
}

/**
 * agent_init() - The function to setup the dattobd module.
 *
 * Return:
 * * 0  - The module was successfully initialized.
 * * !0 - Not successful, the value gives some indication of what went wrong.
 */
static int __init agent_init(void)
{
        int ret;

        LOG_DEBUG("module init");

        mutex_init(&ioctl_mutex);

#ifndef USE_BDOPS_SUBMIT_BIO
        // mrf ref hashtable init
        mrf_tracking_init();
#endif

        calc_max_snap_devices_and_init_minor_range();

        ret = register_blkdev_and_get_major_number();
        if (ret) {
                LOG_ERROR(ret, "error requesting major number from the kernel");
                goto error;
        }

        ret = allocate_snap_devices_array();
        if (ret) {
                LOG_ERROR(ret, "error allocating global device array");
                goto error;
        }

        ret = register_sequential_file_in_proc();
        if (ret) {
                LOG_ERROR(ret, "error registering proc file");
                goto error;
        }

        ret = register_ioctl_control_interface();
        if (ret) {
                LOG_ERROR(ret, "error registering control device");
                goto error;
        }

        ret = register_tracer_filter();
        if (ret) {
                LOG_ERROR(ret, "error registering i/o tracing");
                goto error;
        }

        ret = register_ftrace_hooks();
        if (ret) {
                LOG_ERROR(ret, "error installing ftrace mount hook");
                goto error;
        }

        //if (dattobd_may_hook_syscalls)
         //       (void)hook_system_call_table();

        return 0;

error:
        agent_exit();
        return ret;
}
module_init(agent_init);
