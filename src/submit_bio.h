// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (C) 2022 Datto Inc.
 */

/**
 * This defines a set of helper functions to work with the submit_bio function
 * pointer and the gendisk / block_device_operations structs that hold it.
 *
 * This is the mechanism we use to intercept in-flight i/o on kernels >= 5.9.
 * Kernels older than that use make_request_function instead.
 *
 * The general approach here is that we make a copy of the gendisk of the
 * original disk, and replace the fops->submit_bio function pointer with a 
 * pointer to our own tracing_fn
 *
 * One gotcha here is that there is one gendisk for the entire device. So we
 * have to make sure we save the original one and replace it back whenever we
 * want to do i/o to the real device in order to avoid endless recursion. For
 * this reason we store pointers to the real/orig gendisk and the tracing
 * gendisk in our snap_device struct.
 */

#ifndef SUBMIT_BIO_H_
#define SUBMIT_BIO_H_

#include "includes.h"
#include "bio_helper.h" // needed for USE_BDOPS_SUBMIT_BIO to be defined

struct snap_device;

#ifdef USE_BDOPS_SUBMIT_BIO

/**
 * submit_bio_fn() - Prototype for the submit_bio function, which will be our
 * hook to intercept IO on kernels >= 5.9 
 */
typedef blk_qc_t (submit_bio_fn) (struct bio *bio);

/**
 * dattobd_submit_bio_real() - Submit's given bio to the real device 
 *                            (as opposed to our driver).
 *
 * @dev: Pointer to the snap_device that keeps device state.
 * @bio: Pointer to the bio struct which describes the in-flight I/O.
 *
 * Return:
 * * 0 - success
 * * !0 - error
 */
int dattobd_submit_bio_real(
    struct snap_device* dev,
    struct bio *bio
);

/**
 * dattobd_get_bd_submit_bio() - Return the submit_bio function pointer attached
 *                               to the device of given bio.
 *
 * @bdev: The block device for which to get the submit_bio function pointer.
 *
 * Return:
 * * Returns a submit_bio function pointer.
 */
submit_bio_fn* dattobd_get_bd_submit_bio(struct block_device *bdev);

/**
 * dattobd_get_gendisk() - Get a block device's gendisk struct.
 *
 * @bd_dev: The block device to get the gendisk struct for.
 *
 * Return:
 * * Returns a pointer to the block device's gendisk struct.
 */
struct gendisk* dattobd_get_gendisk(const struct block_device *bd_dev);

/**
 * dattobd_set_submit_bio() -  Assign the submit_bio function pointer in a block
 *                             device.
 *
 * @bdev: The block device to set the submit_bio function pointer for.
 * @func: Function pointer to set the submit_bio function pointer to.
 */
void dattobd_set_submit_bio(struct block_device *bdev, submit_bio_fn *func);

/**
 * dattobd_copy_block_device_operations() - Copy original device's 
 *                                          block_device_operations struct into
 *                                          one that can be used for tracing.
 * 
 * In kernels >= 5.9, this is used to create a block_device_operations struct to
 * be used within the tracing gendisk. It shallow-copies the 
 * block_device_operations structure, within the given block device and replaces
 * the submit_bio function pointer in the copy with the one provided. It also
 * sets the owner to this module.
 *
 * @fops: pointer to block_device_operations structure to copy, this would 
 *        normally be the gendisk->fops member of the original/real disk.
 * @submit_bio_fn: The submit_bio function pointer to set in the copy, this
 *                 would typically be tracing_fn so we can intercept io in the
 *                 tracing gendisk.
 *
 * Return:
 * * Returns a pointer to the newly created block_device_operations struct.
 */
struct block_device_operations* dattobd_copy_block_device_operations(
    struct block_device_operations* fops,
    submit_bio_fn* submit_bio_fn
);

/**
 * dattobd_copy_gendisk() - Copy the original devices gendisk struct into one
 *                        that can be used for tracing.
 *
 * Does a shallow copy of the gendisk struct itself, and calls 
 * dattobd_copy_block_device_operations() to make a copy of the fops member, but
 * with the submit_bio function pointer replaced with the one provided.
 *
 * @bdev: block device of the device being snapshotted.
 * @bi_disk: gendisk to be copied. This would typically be a ptr to the 
 *           real/orig device's gendisk.
 * @submit_bio_fn: Function pointer to set the new gendisk's fops->submit_bio to
 *                 - typically this would be tracing_fn to make a gendisk for
 *                 tracing that intercepts in-flight i/o.
 *
 * Return:
 * * Returns a pointer to the newly created gendisk struct.
 */
struct gendisk* dattobd_copy_gendisk(
    struct block_device* bdev,
    struct gendisk* bi_disk,
    submit_bio_fn* submit_bio_fn);

/**
 * dattobd_set_bd_ops() -  Set the block device operations struct pointer in a
 *                         block device.
 *
 * @bdev: Pointer to block_device struct to set the bd_ops member for.
 * @bd_ops: Pointer to block_device_operations struct that will be assigned to
 *          the block_device's bd_ops member.
 */
void dattobd_set_bd_ops(
    struct block_device *bdev,
    const struct block_device_operations *bd_ops);

/**
 * dattobd_set_gendisk() - Sets the gendisk for a block device.
 *
 * @bdev: Block device to set the gendisk for.
 * @bd_disk: Pointer to gendisk struct that will be assigned to the
 *           block_device's bd_disk member.
 */
void dattobd_set_gendisk(
    struct block_device *bdev,
    const struct gendisk* bd_disk
);

#endif // USE_BDOPS_SUBMIT_BIO
#endif // SUBMIT_BIO_H_
