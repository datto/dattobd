// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (C) 2022 Datto Inc.
 */

#include "blkdev.h"

#if !defined HAVE_BLKDEV_GET_BY_PATH && !defined HAVE_BLKDEV_GET_BY_PATH_4

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

/**
 * _blkdev_get_by_path() - Fetches the @block_device struct associated with the
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
static struct block_device *_blkdev_get_by_path(const char *pathname, fmode_t mode,
                                        void *holder)
{
        struct block_device *bdev;
        bdev = dattobd_lookup_bdev(pathname, mode);
        if (IS_ERR(bdev))
                return bdev;

        if ((mode & FMODE_WRITE) && bdev_read_only(bdev)) {
                dattobd_blkdev_put(bdev);
                return ERR_PTR(-EACCES);
        }

        return bdev;
}

#endif

/**
 * dattodb_blkdev_by_path() - Fetches the @block_device struct associated with the
 * @path. This function uses different methods based on available kernel functions
 * to retrieve the block device.
 *
 * @path: the path name of a block special file.
 * @mode: The mode used to open the block special file, likely just FMODE_READ.
 * @holder: unused.
 *
 * Return:
 * On success the @block_device structure otherwise an error created via
 * ERR_PTR().
 */
struct block_device *dattodb_blkdev_by_path(const char *path, fmode_t mode,
                                        void *holder)
{
#if defined HAVE_BLKDEV_GET_BY_PATH_4
        return blkdev_get_by_path(path, mode, holder, NULL);

#elif defined HAVE_BLKDEV_GET_BY_PATH
        return blkdev_get_by_path(path, mode, holder);

#else
        return _blkdev_get_by_path(path, mode, holder);
#endif
}

/**
 * dattobd_get_super() - Scans the superblock list and finds the superblock of the 
 * file system mounted on the @bd given. This function uses different methods 
 * based on available kernel functions to retrieve the super block.
 *
 * @block_device: mounted block device structure pointer.
 *
 * Return:
 * On success the @super_block structure pointer otherwise NULL.
 */
struct super_block *dattobd_get_super(struct block_device * bd)
{
#if defined HAVE_BD_SUPER
        return (bd != NULL) ? bd->bd_super : NULL;

#elif defined HAVE_GET_SUPER
        return get_super(bdev);

#else
        struct super_block* (*get_active_superblock)(struct block_device*)= (GET_ACTIVE_SUPER_ADDR != 0) ? (struct super_block* (*)(struct block_device*))(GET_ACTIVE_SUPER_ADDR +(long long)(((void*)kfree)-(void*)KFREE_ADDR)):NULL;
        return get_active_superblock(bd);
#endif
}

/**
 * dattobd_drop_super() - Releases the superblock of the file system.
 * This function performs the appropriate action based on the available
 * kernel functions to release or drop the superblock.
 *
 * @sb: super block structure pointer to be released.
 *
 * Return:
 * void.
 */
void dattobd_drop_super(struct super_block *sb) 
{
#if defined HAVE_BD_SUPER
        return;

#elif defined HAVE_GET_SUPER
        return drop_super(sb);

#else
        return;
#endif
}

/**
 * dattobd_blkdev_put() - Releases a reference to a block device.
 * This function performs the appropriate action based on the available
 * kernel functions to release block device.
 *
 * @bd: block device structure pointer to be released.
 *
 * Return:
 * void.
 */
void dattobd_blkdev_put(struct block_device *bd) 
{
#if defined HAVE_BLKDEV_PUT_1
        return blkdev_put(bd);

#elif defined HAVE_BLKDEV_PUT_2
        return blkdev_put(bd,NULL);

#else
        return blkdev_put(bd, FMODE_READ);
#endif
}

/**
 * dattobd_get_start_sect_by_gendisk() - Get starting sector of partition according to gendisk and partition number. 
 * 
 * @gd: gendisk
 * @partno: partition number
 * @result: pointer to place result
 * 
 * Result:
 * 0 on success, error otherwise
 */
int dattobd_get_start_sect_by_gendisk(struct gendisk* gd, u8 partno, sector_t* result){
#if defined HAVE_BDGET_DISK
        struct block_device* bd = bdget_disk(gd, partno);
        if(!bd)
                return -1;
        *result = get_start_sect(bd);
        return 0;
#elif defined HAVE_DISK_GET_PART
        struct hd_struct* hd = disk_get_part(gd, partno);
        if(!hd)
                return -1;
        *result = hd->start_sect;
        disk_put_part(hd);
        return 0;
#elif defined HAVE_GENDISK_PART
        *result = gd->part[partno]->start_sect;
        return 0;
#else
        #error Could not determine starting sector of partition by gendisk and partition number
#endif
}