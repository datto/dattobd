// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (C) 2022 Datto Inc.
 */

#include "system_call_hooking.h"
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

#ifndef UMOUNT_NOFOLLOW
#define UMOUNT_NOFOLLOW 0
#endif

#define handle_bdev_mount_nowrite(dir_name, follow_flags, idx_out)             \
        handle_bdev_mount_event(dir_name, follow_flags, idx_out, 0)
#define handle_bdev_mounted_writable(dir_name, idx_out)                        \
        handle_bdev_mount_event(dir_name, 0, idx_out, 1)

#define set_syscall(sys_nr, orig_call_save, new_call)                          \
        orig_call_save = system_call_table[sys_nr];                            \
        system_call_table[sys_nr] = new_call;

#define restore_syscall(sys_nr, orig_call_save)                                \
        system_call_table[sys_nr] = orig_call_save;

void **system_call_table = NULL;

static asmlinkage long (*orig_mount)(char __user *, char __user *,
                                     char __user *, unsigned long,
                                     void __user *);
static asmlinkage long (*orig_umount)(char __user *, int);

#ifdef HAVE_SYS_OLDUMOUNT
static asmlinkage long (*orig_oldumount)(char __user *);
#endif

/**
 * auto_transition_dormant() - Transitions an active snapshot to dormant.
 *
 * @minor: the device's minor number.
 */
void auto_transition_dormant(unsigned int minor)
{
        mutex_lock(&ioctl_mutex);
        __tracer_active_to_dormant(snap_devices[minor]);
        mutex_unlock(&ioctl_mutex);
}

/**
 * auto_transition_active() - Transitions a device to an active state
 *                            whether snapshot or incremental.
 *
 * @minor: the device's minor number.
 * @dir_name: the user-space supplied directory name of the mount.
 */
void auto_transition_active(unsigned int minor, const char __user *dir_name)
{
        struct snap_device *dev = snap_devices[minor];

        mutex_lock(&ioctl_mutex);

        if (test_bit(UNVERIFIED, &dev->sd_state)) {
                if (test_bit(SNAPSHOT, &dev->sd_state))
                        __tracer_unverified_snap_to_active(dev, dir_name);
                else
                        __tracer_unverified_inc_to_active(dev, dir_name);
        } else
                __tracer_dormant_to_active(dev, dir_name);

        mutex_unlock(&ioctl_mutex);
}

/**
 * __handle_bdev_mount_nowrite() - Transitions a device to a dormant state
 *                                 when it is unmounted.
 *
 * @mnt: The &struct vfsmount object pointer.
 * @idx_out: Output the minor device number of the transitioned device.
 *
 * Return:
 * * 0 - success
 * * !0 - errno indicating the error
 */
int __handle_bdev_mount_nowrite(const struct vfsmount *mnt,
                                unsigned int *idx_out)
{
        int ret;
        unsigned int i;
        struct snap_device *dev;

        tracer_for_each(dev, i)
        {
                if (!dev || !test_bit(ACTIVE, &dev->sd_state) ||
                    tracer_read_fail_state(dev) ||
                    dev->sd_base_dev != mnt->mnt_sb->s_bdev)
                        continue;

                // if we are unmounting the vfsmount we are using go to dormant
                // state
                if (mnt == dattobd_get_mnt(dev->sd_cow->filp)) {
                        LOG_DEBUG("block device umount detected for device %d",
                                  i);
                        auto_transition_dormant(i);

                        ret = 0;
                        goto out;
                }
        }
        i = 0;
        ret = -ENODEV;

out:
        *idx_out = i;
        return ret;
}

/**
 * __handle_bdev_mount_writable() - Transitions a dormant device to active
 *                                  on mount, if one exists.
 *
 * @dir_name: the user-space suplied directory name of the mount.
 * @bdev: The &struct block_device that stores the COW data.
 * @idx_out: Output the minor device number of the transitioned device.
 *
 * Return:
 * * 0 - success
 * * !0 - errno indicating the error
 */
int __handle_bdev_mount_writable(const char __user *dir_name,
                                 const struct block_device *bdev,
                                 unsigned int *idx_out)
{
        int ret;
        unsigned int i;
        struct snap_device *dev;
        struct block_device *cur_bdev;

        tracer_for_each(dev, i)
        {
                if (!dev || test_bit(ACTIVE, &dev->sd_state) ||
                    tracer_read_fail_state(dev))
                        continue;

                if (test_bit(UNVERIFIED, &dev->sd_state)) {
                        // get the block device for the unverified tracer we are
                        // looking into
                        cur_bdev = blkdev_get_by_path(dev->sd_bdev_path,
                                                      FMODE_READ, NULL);
                        if (IS_ERR(cur_bdev)) {
                                cur_bdev = NULL;
                                continue;
                        }

                        // if the tracer's block device exists and matches the
                        // one being mounted perform transition
                        if (cur_bdev == bdev) {
                                LOG_DEBUG("block device mount detected for "
                                          "unverified device %d",
                                          i);
                                auto_transition_active(i, dir_name);
                                dattobd_blkdev_put(cur_bdev);

                                ret = 0;
                                goto out;
                        }

                        // put the block device
                        dattobd_blkdev_put(cur_bdev);

                } else if (dev->sd_base_dev == bdev) {
                        LOG_DEBUG(
                                "block device mount detected for dormant device %d",
                                i);
                        auto_transition_active(i, dir_name);

                        ret = 0;
                        goto out;
                }
        }
        i = 0;
        ret = -ENODEV;

out:
        *idx_out = i;
        return ret;
}

/**
 * __handle_bdev_mount_event() - A common impl used to handle a mount event.
 *
 * @dir_name: the user-space supplied directory name of the mount.
 * @follow_flags: flags passed to the system call.
 * @idx_out: Output the minor device number of the transitioned device.
 * @mount_writable: Whether the mount is writable or not.
 *
 * Return:
 * * 0 - success.
 * * !0 - errno indicating the error.
 */
int handle_bdev_mount_event(const char __user *dir_name, int follow_flags,
                            unsigned int *idx_out, int mount_writable)
{
        int ret, lookup_flags = 0;
        char *pathname = NULL;
        struct path path = {};
        struct block_device *bdev;

        if (!(follow_flags & UMOUNT_NOFOLLOW))
                lookup_flags |= LOOKUP_FOLLOW;

        ret = user_path_at(AT_FDCWD, dir_name, lookup_flags, &path);
        if (ret) {
                // error finding path
                goto out;
        } else if (path.dentry != path.mnt->mnt_root) {
                // path specified isn't a mount point
                ret = -ENODEV;
                goto out;
        }

        bdev = path.mnt->mnt_sb->s_bdev;
        if (!bdev) {
                // path specified isn't mounted on a block device
                ret = -ENODEV;
                goto out;
        }

        if (!mount_writable)
                ret = __handle_bdev_mount_nowrite(path.mnt, idx_out);
        else
                ret = __handle_bdev_mount_writable(dir_name, bdev, idx_out);
        if (ret) {
                // no block device found that matched an incremental
                goto out;
        }

        kfree(pathname);
        path_put(&path);
        return 0;

out:
        if (pathname)
                kfree(pathname);
        path_put(&path);

        *idx_out = 0;
        return ret;
}

/**
 * post_umount_check() - Checks to make sure umount succeeded and the driver
 *                       is in a good state.
 *
 * @dormant_ret: the return value from transitioning to dormant.
 * @umount_ret: the return value from the original umount call.
 * @idx: the device minor number.
 * @dir_name: the user-space supplied directory name of the mount.
 */
void post_umount_check(int dormant_ret, long umount_ret, unsigned int idx,
                       const char __user *dir_name)
{
        struct snap_device *dev;
        struct super_block *sb;

        // if we didn't do anything or failed, just return
        if (dormant_ret)
                return;

        dev = snap_devices[idx];

        // if we successfully went dormant, but the umount call failed,
        // reactivate
        if (umount_ret) {
                struct block_device *bdev;
                bdev = blkdev_get_by_path(dev->sd_bdev_path, FMODE_READ, NULL);
                if (!bdev || IS_ERR(bdev)) {
                        LOG_DEBUG("device gone, moving to error state");
                        tracer_set_fail_state(dev, -ENODEV);
                        return;
                }

                dattobd_blkdev_put(bdev);

                LOG_DEBUG("umount call failed, reactivating tracer %u", idx);
                auto_transition_active(idx, dir_name);
                return;
        }

        // force the umount operation to complete synchronously
        task_work_flush();

        // if we went dormant, but the block device is still mounted somewhere,
        // goto fail state
        sb = dattobd_get_super(dev->sd_base_dev);
        if (sb) {
                if (!(sb->s_flags & MS_RDONLY)) {
                        LOG_ERROR(
                                -EIO,
                                "device still mounted after umounting cow file's "
                                "file-system. entering error state");
                        tracer_set_fail_state(dev, -EIO);
                        dattobd_drop_super(sb);
                        return;
                }
                dattobd_drop_super(sb);
        }

        LOG_DEBUG("post umount check succeeded");
}

/**
 * mount_hook() - Handles mounting a block device.
 *
 * @dev_name: the userspace supplied device file name.
 * @dir_name: the userspace supplied directory name of the mount.
 * @type: passed type to the original mount hook.
 * @flags: passed flags to the original mount hook.
 * @data: passed data to the original mount hook.
 *
 * Return:
 * * 0 - success.
 * * !0 - errno indicating the error.
 */
asmlinkage long mount_hook(char __user *dev_name, char __user *dir_name,
                           char __user *type, unsigned long flags,
                           void __user *data)
{
        int ret;
        int ret_dev;
        int ret_dir;
        long sys_ret;
        unsigned int idx;
        unsigned long real_flags = flags;
        char *buff_dev_name = NULL;
        char *buff_dir_name = NULL;

        // get rid of the magic value if its present
        if ((real_flags & MS_MGC_MSK) == MS_MGC_VAL)
                real_flags &= ~MS_MGC_MSK;

        buff_dev_name = kmalloc(PATH_MAX, GFP_ATOMIC);
        buff_dir_name = kmalloc(PATH_MAX, GFP_ATOMIC);
        if (!buff_dev_name || !buff_dir_name) {
                if (buff_dev_name)
                        kfree(buff_dev_name);
                if (buff_dir_name)
                        kfree(buff_dir_name);
                return -ENOMEM;
        }
        ret_dev = copy_from_user(buff_dev_name, dev_name, PATH_MAX);
        ret_dir = copy_from_user(buff_dir_name, dir_name, PATH_MAX);
        if (ret_dev || ret_dir)
                LOG_DEBUG("detected block device Get mount params error!");
        else
                LOG_DEBUG("detected block device mount: %s -> %s : 0x%lx",
                          buff_dev_name, buff_dir_name, real_flags);
        kfree(buff_dev_name);
        kfree(buff_dir_name);

        if (real_flags & (MS_BIND | MS_SHARED | MS_PRIVATE | MS_SLAVE |
                          MS_UNBINDABLE | MS_MOVE) ||
            ((real_flags & MS_RDONLY) && !(real_flags & MS_REMOUNT))) {
                // bind, shared, move, or new read-only mounts it do not affect
                // the state of the driver
                sys_ret = orig_mount(dev_name, dir_name, type, flags, data);
        } else if ((real_flags & MS_RDONLY) && (real_flags & MS_REMOUNT)) {
                // we are remounting read-only, same as umounting as far as the
                // driver is concerned
                ret = handle_bdev_mount_nowrite(dir_name, 0, &idx);
                sys_ret = orig_mount(dev_name, dir_name, type, flags, data);
                post_umount_check(ret, sys_ret, idx, dir_name);
        } else {
                // new read-write mount
                sys_ret = orig_mount(dev_name, dir_name, type, flags, data);
                if (!sys_ret)
                        handle_bdev_mounted_writable(dir_name, &idx);
        }

        LOG_DEBUG("mount returned: %ld", sys_ret);

        return sys_ret;
}

/**
 * umount_hook() - Handles umounting a block device.
 *
 * @name: the device file name.
 * @flags: the umount flags.
 *
 * Return:
 * * 0 - success.
 * * !0 - errno indicating the error.
 */
asmlinkage long umount_hook(char __user *name, int flags)
{
        int ret;
        long sys_ret;
        unsigned int idx;
        char *buff_dev_name = NULL;

        buff_dev_name = kmalloc(PATH_MAX, GFP_ATOMIC);
        if (!buff_dev_name) {
                return -ENOMEM;
        }
        ret = copy_from_user(buff_dev_name, name, PATH_MAX);
        if (ret)
                LOG_DEBUG("detected block device umount error:%d", ret);
        else
                LOG_DEBUG("detected block device umount: %s : %d",
                          buff_dev_name, flags);
        kfree(buff_dev_name);

        ret = handle_bdev_mount_nowrite(name, flags, &idx);
        sys_ret = orig_umount(name, flags);
        post_umount_check(ret, sys_ret, idx, name);

        LOG_DEBUG("umount returned: %ld", sys_ret);

        return sys_ret;
}

#ifdef HAVE_SYS_OLDUMOUNT

asmlinkage long oldumount_hook(char __user *name)
{
        int ret;
        long sys_ret;
        unsigned int idx;
        char *buff_dev_name = NULL;

        buff_dev_name = kmalloc(PATH_MAX, GFP_ATOMIC);
        if (!buff_dev_name) {
                return -ENOMEM;
        }
        ret = copy_from_user(buff_dev_name, name, PATH_MAX);
        if (ret)
                LOG_DEBUG("detected block device oldumount error:%d", ret);
        else
                LOG_DEBUG("detected block device oldumount: %s", name);
        kfree(buff_dev_name);

        ret = handle_bdev_mount_nowrite(name, 0, &idx);
        sys_ret = orig_oldumount(name);
        post_umount_check(ret, sys_ret, idx, name);

        LOG_DEBUG("oldumount returned: %ld", sys_ret);

        return sys_ret;
}
#endif

/**
 * find_sys_call_table() - Finds the system call table address.
 *
 * Return: the system call table address.
 */
void **find_sys_call_table(void)
{
        long long offset;
        void **sct;

        if (!SYS_CALL_TABLE_ADDR || !SYS_MOUNT_ADDR || !SYS_UMOUNT_ADDR)
                return NULL;

        offset = ((void *)printk) - (void *)PRINTK_ADDR;
        sct = (void **)SYS_CALL_TABLE_ADDR + offset / sizeof(void **);

        if (sct[__NR_mount] !=
            (void **)SYS_MOUNT_ADDR + offset / sizeof(void **))
                return NULL;
        if (sct[__NR_umount2] !=
            (void **)SYS_UMOUNT_ADDR + offset / sizeof(void **))
                return NULL;
#ifdef HAVE_SYS_OLDUMOUNT
        if (sct[__NR_umount] !=
            (void **)SYS_OLDUMOUNT_ADDR + offset / sizeof(void **))
                return NULL;
#endif

        LOG_DEBUG("system call table located at 0x%p", sct);

        return sct;
}

/**
 * restore_system_call_table() - Restored the system call table, removing this
 *                               driver's hooks.
 */
void restore_system_call_table(void)
{
        unsigned long cr0;

        if (system_call_table) {
                LOG_DEBUG("restoring system call table");
                // break back into the syscall table and replace the hooks we
                // stole
                preempt_disable();
                disable_page_protection(&cr0);
                restore_syscall(__NR_mount, orig_mount);
                restore_syscall(__NR_umount2, orig_umount);
#ifdef HAVE_SYS_OLDUMOUNT
                restore_syscall(__NR_umount, orig_oldumount);
#endif
                reenable_page_protection(&cr0);
                preempt_enable();
        }
}

/**
 * hook_system_call_table() - Insert this driver's hooks for detecting events
 * such as mount and umount.
 *
 * Return:
 * * 0 - success
 * * !0 - errno indicating the error
 */
int hook_system_call_table(void)
{
        unsigned long cr0;

        // find sys_call_table
        LOG_DEBUG("locating system call table");
        system_call_table = find_sys_call_table();
        if (!system_call_table) {
                LOG_WARN(
                        "failed to locate system call table, persistence disabled");
                return -ENOENT;
        }

        // break into the syscall table and steal the hooks we need
        preempt_disable();
        disable_page_protection(&cr0);
        set_syscall(__NR_mount, orig_mount, mount_hook);
        set_syscall(__NR_umount2, orig_umount, umount_hook);
#ifdef HAVE_SYS_OLDUMOUNT
        set_syscall(__NR_umount, orig_oldumount, oldumount_hook);
#endif
        reenable_page_protection(&cr0);
        preempt_enable();
        return 0;
}
