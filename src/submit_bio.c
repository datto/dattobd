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

/*
 * For ftrace to work, each function has a preamble that calls a "function" (asm
 * snippet) called __fentry__ which then triggers the callbacks. If we want to
 * recurse without triggering ftrace, we'll need to skip this preamble. Don't
 * worry, the stack pointer manipulation is right after the call.
 */
blk_qc_t (*dattobd_submit_bio_noacct_passthrough)(struct bio *) =
	(blk_qc_t(*)(struct bio *))((unsigned long)(submit_bio_noacct) +
        FENTRY_CALL_INSTR_BYTES);

int dattobd_submit_bio_real(
    struct snap_device* dev,
    struct bio *bio)
{
    return dattobd_submit_bio_noacct_passthrough(bio);
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
