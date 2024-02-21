// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (C) 2022 Datto Inc.
 */

#include "blkdev.h"

#if !defined HAVE_BLKDEV_GET_BY_PATH && !defined HAVE_BLKDEV_GET_BY_PATH_1

/**
 * dattobd_lookup_bdev() - Looks up the inode associated with the path, verifies
 * that the file is a block special file, and asks the kernel for a &struct
 * block_device associated with the file.
 *
 * @pathname: the path name of a block special file.
 * @mode: The mode used to open the block special file, likely just FMODE_READ.
 *
 * Return:
 * On success the @block_device structure otherwise an error created via
 * ERR_PTR().
 */
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

#if !defined HAVE_BLKDEV_GET_BY_PATH && !defined HAVE_BLKDEV_GET_BY_PATH_1
//#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,38)

/**
 * blkdev_get_by_path() - Fetches the @block_device struct associated with the
 * @pathname.  This is very similar to @dattobd_lookup_bdev with minor
 * additional validation.
 *
 * @pathname: the path name of a block special file.
 * @mode: The mode used to open the block special file, likely just FMODE_READ.
 * @holder: unused
 *
 * Return:
 * On success the @block_device structure otherwise an error created via
 * ERR_PTR().
 */
struct block_device *blkdev_get_by_path(const char *pathname, fmode_t mode,
                                        void *holder)
{
        struct block_device *bdev;
        bdev = dattobd_lookup_bdev(pathname, mode);
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
#if !defined HAVE_BD_SUPER && !defined HAVE_GET_SUPER
struct super_block* (*dattobd_get_super)(struct block_device *) = (GET_SUPER_ADDR != 0) ?
	(struct super_block* (*)(struct block_device*)) (GET_SUPER_ADDR + (long long)(((void *)kfree) - (void *)KFREE_ADDR)) : NULL;

struct super_block* get_superblock(struct block_device* bdev){
        return dattobd_get_super(bdev);
}
#endif
