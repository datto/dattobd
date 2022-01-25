// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (C) 2022 Datto Inc.
 */

#include "blkdev.h"

#ifndef HAVE_BLKDEV_GET_BY_PATH
static struct block_device *dattobd_lookup_bdev(const char *pathname,
                                                fmode_t mode)
{
        int r;
        struct block_device *retbd;
        struct nameidata nd;
        struct inode *inode;
        dev_t dev;

        if ((r = path_lookup(pathname, LOOKUP_FOLLOW, &nd)))
                goto fail;

        inode = dattobd_get_nd_dentry(nd)->d_inode;
        if (!inode) {
                r = -ENOENT;
                goto fail;
        }

        if (!S_ISBLK(inode->i_mode)) {
                r = -ENOTBLK;
                goto fail;
        }
        dev = inode->i_rdev;
        retbd = open_by_devnum(dev, mode);

out:
#ifdef HAVE_PATH_PUT
        path_put(&nd.path);
#else
        dput(nd.dentry);
        mntput(nd.mnt);
#endif
        return retbd;
fail:
        retbd = ERR_PTR(r);
        goto out;
}
#endif

#ifndef HAVE_BLKDEV_GET_BY_PATH
//#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,38)
struct block_device *blkdev_get_by_path(const char *path, fmode_t mode,
                                        void *holder)
{
        struct block_device *bdev;
        bdev = dattobd_lookup_bdev(path, mode);
        if (IS_ERR(bdev))
                return bdev;

        if ((mode & FMODE_WRITE) && bdev_read_only(bdev)) {
#ifdef HAVE_BLKDEV_PUT_1
                blkdev_put(bdev);
#else
                blkdev_put(bdev, mode);
#endif
                return ERR_PTR(-EACCES);
        }

        return bdev;
}
#endif
