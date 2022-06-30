// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (C) 2022 Datto Inc.
 */

#include "submit_bio.h"

#include "bio_helper.h" // needed for USE_BDOPS_SUBMIT_BIO to be defined
#include "callback_refs.h"
#include "includes.h"
#include "logging.h"
#include "paging_helper.h"
#include "snap_device.h"

#ifdef USE_BDOPS_SUBMIT_BIO

int dattobd_submit_bio_real(
    struct snap_device* dev,
    struct bio *bio)
{
    BIO_REQUEST_CALLBACK_FN *fn = NULL;
    int ret = 0;
    gendisk_fn_lock(dev->sd_base_dev);
    if (!dev)
    {
        LOG_ERROR(-EFAULT,
                  "Missing snap_device when calling dattobd_call_submit_bio");
        gendisk_fn_unlock(dev->sd_base_dev);
        return -EFAULT;
    }
    fn = dev->sd_orig_request_fn;
    if (!fn)
    {
        LOG_ERROR(-EFAULT, "error finding original_mrf");
        gendisk_fn_unlock(dev->sd_base_dev);
        return -EFAULT;
    }
    bio->bi_disk = dev->sd_orig_gendisk;
    LOG_DEBUG(
            "SUBMIT_BIO_REAL | bdev path: %s | bio partno: %d | ",
            dev->sd_bdev_path,
            bio->bi_partno
    );
    ret = fn(bio);
    gendisk_fn_unlock(dev->sd_base_dev);
    return ret;
}

submit_bio_fn* dattobd_get_bd_submit_bio(struct block_device *bdev)
{
    return bdev->bd_disk->fops->submit_bio;
}

struct gendisk* dattobd_get_gendisk(const struct block_device *bd_dev)
{
    return bd_dev->bd_disk;
}

void dattobd_set_submit_bio(struct block_device *bdev, submit_bio_fn *func)
{
    unsigned long cr0 = 0;
    gendisk_fn_lock(bdev);
    preempt_disable();
    disable_page_protection(&cr0);
    ((struct block_device_operations*)bdev->bd_disk->fops)->submit_bio = func;
    reenable_page_protection(&cr0);
    preempt_enable();
    gendisk_fn_unlock(bdev);
}

struct block_device_operations* dattobd_copy_block_device_operations(
    struct block_device_operations* fops,
    submit_bio_fn* submit_bio_fn)
{
    const size_t block_device_operations_size = sizeof(
        struct block_device_operations);
    struct block_device_operations* result = kmalloc(
        block_device_operations_size,
        GFP_KERNEL
    );
    memcpy(result, fops, block_device_operations_size);
    result->owner = THIS_MODULE;
    result->submit_bio = submit_bio_fn;
    return result;
}

struct gendisk* dattobd_copy_gendisk(
    struct block_device* bdev,
    struct gendisk* bi_disk,
    submit_bio_fn* submit_bio_fn)
{
    const size_t gendisk_size = sizeof(struct gendisk);
    struct gendisk* result = kmalloc(gendisk_size, GFP_KERNEL);
    gendisk_fn_lock(bdev);
    memcpy(result, bi_disk, gendisk_size); // Initial shallow copy.

    // Shallow copy of the fops struct.
    result->fops = dattobd_copy_block_device_operations(
        (struct block_device_operations*) bi_disk->fops,
        submit_bio_fn
    ); 
    gendisk_fn_unlock(bdev);
    return result;
}

void dattobd_set_bd_ops(
    struct block_device *bdev,
    const struct block_device_operations *bd_ops)
{
    bdev->bd_disk->fops = bd_ops;
}

void dattobd_set_gendisk(
    struct block_device *bdev,
    const struct gendisk* bd_disk
)
{
    gendisk_fn_lock(bdev);
    bdev->bd_disk = (struct gendisk*)bd_disk;
    gendisk_fn_unlock(bdev);
}


#endif
