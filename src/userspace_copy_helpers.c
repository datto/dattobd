// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (C) 2022 Datto Inc.
 */

#include "dattobd.h"
#include "includes.h"
#include "logging.h"

/**
 * copy_string_from_user() - Copies string data from a user space address to
 * kernel space.
 *
 * @data: A user space address pointing to a string.
 * @out_ptr: The string in a kernel allocated buffer.  The caller owns the
 *           buffer.
 *
 * Will copy no more than a PAGE_SIZE from user space.
 *
 * Return:
 * * 0 - success
 * * !0 - errno indicating the error.
 */
int copy_string_from_user(const char __user *data, char **out_ptr)
{
        int ret;
        char *str;

        if (!data) {
                *out_ptr = NULL;
                return 0;
        }

        str = strndup_user(data, PAGE_SIZE);
        if (IS_ERR(str)) {
                ret = PTR_ERR(str);
                goto error;
        }

        *out_ptr = str;
        return 0;

error:
        LOG_ERROR(ret, "error copying string from user space");
        *out_ptr = NULL;
        return ret;
}

/**
 * get_setup_params() - Copies &struct setup_params from user space to the
 * kernel.
 *
 * @in: The &struct setup_params object pointer from user space.
 * @minor: The minor number.
 * @bdev_name: Uses @copy_string_from_user to transfer the block device name.
 * @cow_path: Uses @copy_string_from_user to transfer the cow file path.
 * @fallocated_space: A number of bytes for fallocated_space.
 * @cache_size: A number of bytes for section cache.
 *
 * Return:
 * * 0 - success
 * * !0 - errno indicating the error.
 */
int get_setup_params(const struct setup_params __user *in, unsigned int *minor,
                     char **bdev_name, char **cow_path,
                     unsigned long *fallocated_space, unsigned long *cache_size)
{
        int ret;
        struct setup_params params;

        // copy the params struct
        ret = copy_from_user(&params, in, sizeof(struct setup_params));
        if (ret) {
                ret = -EFAULT;
                LOG_ERROR(ret,
                          "error copying setup_params struct from user space");
                goto error;
        }

        ret = copy_string_from_user((char __user *)params.bdev, bdev_name);
        if (ret)
                goto error;

        if (!*bdev_name) {
                ret = -EINVAL;
                LOG_ERROR(ret, "NULL bdev given");
                goto error;
        }

        ret = copy_string_from_user((char __user *)params.cow, cow_path);
        if (ret)
                goto error;

        if (!*cow_path) {
                ret = -EINVAL;
                LOG_ERROR(ret, "NULL cow given");
                goto error;
        }

        *minor = params.minor;
        *fallocated_space = params.fallocated_space;
        *cache_size = params.cache_size;
        return 0;

error:
        LOG_ERROR(ret, "error copying setup_params from user space");
        if (*bdev_name)
                kfree(*bdev_name);
        if (*cow_path)
                kfree(*cow_path);

        *bdev_name = NULL;
        *cow_path = NULL;
        *minor = 0;
        *fallocated_space = 0;
        *cache_size = 0;
        return ret;
}

/**
 * get_reload_params() - Copies &struct reload_params from user space to the
 * kernel.
 * @in: The &struct reload_params object pointer from user space.
 * @minor: The minor number.
 * @bdev_name: Uses @copy_string_from_user to transfer the block device name.
 * @cow_path: Uses @copy_string_from_user to transfer the cow file path.
 * @cache_size: A number of bytes for section cache.
 *
 * Return:
 * * 0 - success
 * * !0 - errno indicating the error.
 */
int get_reload_params(const struct reload_params __user *in,
                      unsigned int *minor, char **bdev_name, char **cow_path,
                      unsigned long *cache_size)
{
        int ret;
        struct reload_params params;

        // copy the params struct
        ret = copy_from_user(&params, in, sizeof(struct reload_params));
        if (ret) {
                ret = -EFAULT;
                LOG_ERROR(ret,
                          "error copying reload_params struct from user space");
                goto error;
        }

        ret = copy_string_from_user((char __user *)params.bdev, bdev_name);
        if (ret)
                goto error;

        if (!*bdev_name) {
                ret = -EINVAL;
                LOG_ERROR(ret, "NULL bdev given");
                goto error;
        }

        ret = copy_string_from_user((char __user *)params.cow, cow_path);
        if (ret)
                goto error;

        if (!*cow_path) {
                ret = -EINVAL;
                LOG_ERROR(ret, "NULL cow given");
                goto error;
        }

        *minor = params.minor;
        *cache_size = params.cache_size;
        return 0;

error:
        LOG_ERROR(ret, "error copying reload_params from user space");
        if (*bdev_name)
                kfree(*bdev_name);
        if (*cow_path)
                kfree(*cow_path);

        *bdev_name = NULL;
        *cow_path = NULL;
        *minor = 0;
        *cache_size = 0;
        return ret;
}

/**
 * get_transition_snap_params() - Copies &struct transition_snap_params from
 * user space to the kernel.
 *
 * @in: The &struct transition_snap_params object pointer from user space.
 * @minor: The minor number.
 * @cow_path: Uses @copy_string_from_user to transfer the cow file path.
 * @fallocated_space: A number of bytes for fallocated_space.
 *
 * Return:
 * * 0 - success.
 * * !0 - errno indicating the error.
 */
int get_transition_snap_params(const struct transition_snap_params __user *in,
                               unsigned int *minor, char **cow_path,
                               unsigned long *fallocated_space)
{
        int ret;
        struct transition_snap_params params;

        // copy the params struct
        ret = copy_from_user(&params, in,
                             sizeof(struct transition_snap_params));
        if (ret) {
                ret = -EFAULT;
                LOG_ERROR(ret, "error copying transition_snap_params struct "
                               "from user space");
                goto error;
        }

        ret = copy_string_from_user((char __user *)params.cow, cow_path);
        if (ret)
                goto error;

        if (!*cow_path) {
                ret = -EINVAL;
                LOG_ERROR(ret, "NULL cow given");
                goto error;
        }

        *minor = params.minor;
        *fallocated_space = params.fallocated_space;
        return 0;

error:
        LOG_ERROR(ret, "error copying transition_snap_params from user space");
        if (*cow_path)
                kfree(*cow_path);

        *cow_path = NULL;
        *minor = 0;
        *fallocated_space = 0;
        return ret;
}

/**
 * get_reconfigure_params() - Copies &struct reconfigure_params from user
 * space to kernel space.
 * @in: The &struct reconfigure_params object pointer from user space.
 * @minor: The minor number.
 * @cache_size: A number of bytes for section cache.
 *
 * Return:
 * * 0 - success.
 * * !0 - errno indicating the error.
 */
int get_reconfigure_params(const struct reconfigure_params __user *in,
                           unsigned int *minor, unsigned long *cache_size)
{
        int ret;
        struct reconfigure_params params;

        // copy the params struct
        ret = copy_from_user(&params, in, sizeof(struct reconfigure_params));
        if (ret) {
                ret = -EFAULT;
                LOG_ERROR(
                        ret,
                        "error copying reconfigure_params struct from user space");
                goto error;
        }

        *minor = params.minor;
        *cache_size = params.cache_size;
        return 0;

error:
        LOG_ERROR(ret, "error copying reconfigure_params from user space");

        *minor = 0;
        *cache_size = 0;
        return ret;
}

#ifndef HAVE_USER_PATH_AT
int user_path_at(int dfd, const char __user *name, unsigned flags,
                 struct path *path)
{
        struct nameidata nd;
        char *tmp = getname(name);
        int err = PTR_ERR(tmp);
        if (!IS_ERR(tmp)) {
                BUG_ON(flags & LOOKUP_PARENT);
                err = path_lookup(tmp, flags, &nd);
                putname(tmp);
                if (!err) {
                        path->dentry = dattobd_get_nd_dentry(nd);
                        path->mnt = dattobd_get_nd_mnt(nd);
                }
        }
        return err;
}
#endif
