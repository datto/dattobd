// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (C) 2022 Datto Inc.
 */

#include "blkdev.h"
#include "logging.h"
#include <linux/version.h>

#if !defined HAVE_BLKDEV_GET_BY_PATH && !defined HAVE_BLKDEV_GET_BY_PATH_4 && !defined HAVE_BDEV_OPEN_BY_PATH && !defined HAVE_BDEV_FILE_OPEN_BY_PATH

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
 * dattobd_blkdev_by_path() - Fetches the @block_device struct associated with the
 * @path. This function uses different methods based on available kernel functions
 * to retrieve the block device. Returns @bdev_handle struct which contains
 * information about @block_device and @holder. Made to be in compliance with kernel
 * version 6.8+ standard.
 *
 * @path: the path name of a block special file.
 * @mode: The mode used to open the block special file, likely just FMODE_READ.
 * @holder: unused.
 *
 * Return:
 * On success the @bdev_handle structure otherwise an error created via
 * ERR_PTR().
 */
struct bdev_wrapper *dattobd_blkdev_by_path(const char *path, fmode_t mode,
                                        void *holder)
{
        struct bdev_wrapper *bw = kmalloc(sizeof(struct bdev_wrapper), GFP_KERNEL);

        if(IS_ERR_OR_NULL(bw)){
                return ERR_PTR(-ENOMEM);
        } 

#if defined HAVE_BDEV_OPEN_BY_PATH
        bw->_internal.handle = bdev_open_by_path(path, mode, holder, NULL);
        bw->bdev = bw->_internal.handle->bdev;
#elif defined HAVE_BLKDEV_GET_BY_PATH_4
        bw->bdev = blkdev_get_by_path(path, mode, holder, NULL);
#elif defined HAVE_BLKDEV_GET_BY_PATH
        bw->bdev = blkdev_get_by_path(path, mode, holder);
#elif defined HAVE_BDEV_FILE_OPEN_BY_PATH
        bw->_internal.file = bdev_file_open_by_path(path, mode, holder, NULL);
        bw->bdev = file_bdev(bw->_internal.file);
#else
        bw->bdev = _blkdev_get_by_path(path, mode, holder);
#endif

        return bw;
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
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(6,6,0)
        return (struct super_block*)(bd -> bd_holder);
#elif GET_ACTIVE_SUPER_ADDR != 0
        struct super_block* (*get_active_superblock)(struct block_device*)= (GET_ACTIVE_SUPER_ADDR != 0) ? (struct super_block* (*)(struct block_device*))(GET_ACTIVE_SUPER_ADDR +(long long)(((void*)kfree)-(void*)KFREE_ADDR)):NULL;
        return get_active_superblock(bd);
#else
        #error "Could not determine super block of block device"
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
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(6,6,0)
        return;
#elif GET_ACTIVE_SUPER_ADDR != 0
        return;
#else
        #error "Could not determine super block of block device"
#endif
}

/**
 * dattobd_blkdev_put() - Releases a reference to a block device.
 * This function performs the appropriate action based on the available
 * kernel functions to release block device.
 *
 * @bh: bdev_handle structure pointer to be released.
 *
 * Return:
 * void.
 */
void dattobd_blkdev_put(struct bdev_wrapper *bw) 
{
        if(unlikely(IS_ERR_OR_NULL(bw)))
                return;

#ifdef USE_BDEV_AS_FILE
        if(bw->_internal.file)
                bdev_fput(bw->_internal.file);
#elif defined HAVE_BDEV_RELEASE
        if(bw->_internal.handle)
                bdev_release(bw->_internal.handle);
#elif defined HAVE_BLKDEV_PUT_1
        blkdev_put(bw->bdev);
#elif defined HAVE_BLKDEV_PUT_2
        blkdev_put(bw->bdev, NULL);
#else
        blkdev_put(bw->bdev, FMODE_READ);
#endif
        kfree(bw);
}

/**
 * dattobd_get_start_sect_by_gendisk_for_bio() - Get starting sector of partition according to gendisk and partition number. 
 * 
 * @gd: gendisk
 * @partno: partition number
 * @result: pointer to place result
 * 
 * Result:
 * 0 on success, error otherwise
 */
int dattobd_get_start_sect_by_gendisk_for_bio(struct gendisk* gd, u8 partno, sector_t* result){
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
#elif defined HAVE_BIO_BI_BDEV
        // Unreachable
        LOG_ERROR(-1, "Unreachable code.");
        return -1;
#else
        #error "Could not determine starting sector of partition by gendisk and partition number"
#endif
}


/**
 * dattobd_get_kstatfs() - Get the file system statistics of the block device.
 *
 * @bd: block device structure pointer.
 * @statfs: file system statistics structure pointer.
 *
 * Return:
 * 0 on success, error otherwise.
 */
int dattobd_get_kstatfs(struct block_device* bd, struct kstatfs* statfs){
        struct super_block* sb;
        int ret;

        ret = 0;
        sb = dattobd_get_super(bd);

        if(sb){
                if(sb->s_op && sb->s_op->statfs && sb->s_root){
                        ret = sb->s_op->statfs(sb->s_root, statfs);

                        if(ret){
                                LOG_ERROR(ret, "dattobd_get_kstatfs: error getting statfs from super block");
                                goto done;
                        }

                        LOG_DEBUG("dattobd_get_kstatfs: free blocks: %llu, block size: %ld, total: %llu\n", statfs->f_bavail, statfs->f_bsize, statfs->f_bavail*statfs->f_bsize);
                        goto done;
                }else{
                        ret = -EINVAL;
                        
                        LOG_ERROR(ret, "dattobd_get_kstatfs: super block does not have statfs operations or root dentry");
                        goto done;
                }
        }

done:
        dattobd_drop_super(sb);
        return ret;
}