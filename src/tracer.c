// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (C) 2023 Datto Inc.
 */

#include "tracer.h"

#include "bio_request_callback.h"
#include "blkdev.h"
#include "callback_refs.h"
#include "cow_manager.h"
#include "filesystem.h"
#include "hints.h"
#include "logging.h"
#include "module_control.h"
#include "module_threads.h"
#include "mrf.h"
#include "snap_device.h"
#include "snap_ops.h"
#include "submit_bio.h"
#include "task_helper.h"
#include "tracer_helper.h"
#include "tracing_params.h"
#include <linux/blk-mq.h>
#include <linux/version.h>
#ifdef HAVE_BLK_ALLOC_QUEUE
//#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,7,0)
#include <linux/percpu-refcount.h>
#endif

#if !defined(HAVE_BDEV_STACK_LIMITS) && !defined(HAVE_BLK_SET_DEFAULT_LIMITS)
//#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,31)

#ifndef min_not_zero
#define min_not_zero(l, r) ((l) == 0) ? (r) : (((r) == 0) ? (l) : min(l, r))
#endif

static int blk_stack_limits(struct request_queue *t, struct request_queue *b,
                            sector_t offset)
{
        t->max_sectors = min_not_zero(t->max_sectors, b->max_sectors);
        t->max_hw_sectors = min_not_zero(t->max_hw_sectors, b->max_hw_sectors);
        t->bounce_pfn = min_not_zero(t->bounce_pfn, b->bounce_pfn);
        t->seg_boundary_mask =
                min_not_zero(t->seg_boundary_mask, b->seg_boundary_mask);
        t->max_phys_segments =
                min_not_zero(t->max_phys_segments, b->max_phys_segments);
        t->max_hw_segments =
                min_not_zero(t->max_hw_segments, b->max_hw_segments);
        t->max_segment_size =
                min_not_zero(t->max_segment_size, b->max_segment_size);
        return 0;
}

static int dattobd_bdev_stack_limits(struct request_queue *t,
                                     struct block_device *bdev, sector_t start)
{
        struct request_queue *bq = bdev_get_queue(bdev);
        start += get_start_sect(bdev);
        return blk_stack_limits(t, bq, start << 9);
}

#elif !defined(HAVE_BDEV_STACK_LIMITS)
//#elif LINUX_VERSION_CODE < KERNEL_VERSION(2,6,32)

static int bdev_stack_limits(struct queue_limits *t, struct block_device *bdev,
                             sector_t start)
{
        struct request_queue *bq = bdev_get_queue(bdev);
        start += get_start_sect(bdev);
        return blk_stack_limits(t, &bq->limits, start << 9);
}

#define dattobd_bdev_stack_limits(queue, bdev, start)                          \
        bdev_stack_limits(&(queue)->limits, bdev, start)
#else
#define dattobd_bdev_stack_limits(queue, bdev, start)                          \
        bdev_stack_limits(&(queue)->limits, bdev, start)
#endif // # !HAVE_BDEV_STACK_LIMITS) && !HAVE_BLK_SET_DEFAULT_LIMITS

// Helpers to get/set either the make_request_fn or the submit_bio function 
// pointers in a block device.
static inline BIO_REQUEST_CALLBACK_FN* dattobd_get_bd_fn(
    struct block_device *bdev)
{
#ifdef USE_BDOPS_SUBMIT_BIO
    return bdev->bd_disk->fops->submit_bio;
#else
    return bdev->bd_disk->queue->make_request_fn;
#endif
}

#ifndef HAVE_BLK_SET_DEFAULT_LIMITS
#define blk_set_default_limits(ql)
#endif

#define __tracer_setup_cow_new(dev, bdev, cow_path, size, fallocated_space,    \
                               cache_size, uuid, seqid)                        \
        __tracer_setup_cow(dev, bdev, cow_path, size, fallocated_space,        \
                           cache_size, uuid, seqid, 0)
#define __tracer_setup_cow_reload_snap(dev, bdev, cow_path, size, cache_size)  \
        __tracer_setup_cow(dev, bdev, cow_path, size, 0, cache_size, NULL, 0, 1)
#define __tracer_setup_cow_reload_inc(dev, bdev, cow_path, size, cache_size)   \
        __tracer_setup_cow(dev, bdev, cow_path, size, 0, cache_size, NULL, 0, 2)
#define __tracer_setup_cow_reopen(dev, bdev, cow_path)                         \
        __tracer_setup_cow(dev, bdev, cow_path, 0, 0, 0, NULL, 0, 3)

#define __tracer_destroy_cow_free(dev) __tracer_destroy_cow(dev, 0)
#define __tracer_destroy_cow_sync_and_free(dev) __tracer_destroy_cow(dev, 1)
#define __tracer_destroy_cow_sync_and_close(dev) __tracer_destroy_cow(dev, 2)

#define __tracer_setup_inc_cow_thread(dev, minor)                              \
        __tracer_setup_cow_thread(dev, minor, 0)
#define __tracer_setup_snap_cow_thread(dev, minor)                             \
        __tracer_setup_cow_thread(dev, minor, 1)
#ifndef HAVE_BLK_SET_STACKING_LIMITS
#define blk_set_stacking_limits(ql) blk_set_default_limits(ql)
#endif

#ifdef HAVE_BIOSET_NEED_BVECS_FLAG
#define dattobd_bioset_create(bio_size, bvec_size, scale)                      \
        bioset_create(bio_size, bvec_size, BIOSET_NEED_BVECS)
#elif defined HAVE_BIOSET_CREATE_3
#define dattobd_bioset_create(bio_size, bvec_size, scale)                      \
        bioset_create(bio_size, bvec_size, scale)
#else
#define dattobd_bioset_create(bio_size, bvec_size, scale)                      \
        bioset_create(bio_size, scale)
#endif

#define tracer_setup_unverified_inc(dev, minor, bdev_path, cow_path,           \
                                    cache_size)                                \
        __tracer_setup_unverified(dev, minor, bdev_path, cow_path, cache_size, \
                                  0)
#define tracer_setup_unverified_snap(dev, minor, bdev_path, cow_path,          \
                                     cache_size)                               \
        __tracer_setup_unverified(dev, minor, bdev_path, cow_path, cache_size, \
                                  1)

#define ROUND_UP(x, chunk) ((((x) + (chunk)-1) / (chunk)) * (chunk))
#define ROUND_DOWN(x, chunk) (((x) / (chunk)) * (chunk))

// macros for defining sector and block sizes
#define SECTORS_PER_PAGE (PAGE_SIZE / SECTOR_SIZE)
#define BLOCK_TO_SECTOR(block) ((block)*SECTORS_PER_BLOCK)

void dattobd_free_request_tracking_ptr(struct snap_device *dev)
{
    dev->sd_orig_request_fn = NULL;
}
 
/**
 * snap_trace_bio() - Traces a bio when snapshotting.  For bio reads there is
 * nothing to do and the request is passed to the original driver.  For writes
 * the original data must be read and in the event that the original bio
 * cannot be read in a single try multiple attempts are made by creating
 * additional bio requests until the original bio is fully processed.
 *
 * @dev: The &struct snap_device that keeps device state.
 * @bio: The &struct bio which describes the I/O.
 *
 * Return:
 * * 0 - success
 * * !0 - error
 */
static int snap_trace_bio(struct snap_device *dev, struct bio *bio)
{
        int ret;
        struct bio *new_bio = NULL;
        struct tracing_params *tp = NULL;
        sector_t start_sect, end_sect;
        unsigned int bytes, pages;

        // if we don't need to cow this bio just call the real mrf normally
        if (!bio_needs_cow(bio, dev->sd_cow_inode))
                goto call_orig;

        // the cow manager works in 4096 byte blocks, so read clones must also
        // be 4096 byte aligned
        start_sect = ROUND_DOWN(bio_sector(bio) - dev->sd_sect_off,
                        SECTORS_PER_BLOCK) +
                dev->sd_sect_off;
        end_sect = ROUND_UP(bio_sector(bio) + (bio_size(bio) / SECTOR_SIZE) -
                        dev->sd_sect_off,
                        SECTORS_PER_BLOCK) +
                dev->sd_sect_off;
        pages = (end_sect - start_sect) / SECTORS_PER_PAGE;

        // allocate tracing_params struct to hold all pointers we will need
        // across contexts
        ret = tp_alloc(dev, bio, &tp);
        if (ret)
        {
                LOG_ERROR(ret, "error tracing bio for snapshot");
                tracer_set_fail_state(dev, ret);
                goto call_orig;
        }

        while (1) {
                // allocate and populate read bio clone. This bio may not have all the
                // pages we need due to queue restrictions
                ret = bio_make_read_clone(dev_bioset(dev), tp, bio, start_sect, pages,
                                        &new_bio, &bytes);
                if (ret)
                        goto error;

                // set pointers for read clone
                ret = tp_add(tp, new_bio);
                if (ret)
                        goto error;

                atomic64_inc(&dev->sd_submitted_cnt);
                smp_wmb();

                dattobd_submit_bio(new_bio);

                // if our bio didn't cover the entire clone we must keep creating bios
                // until we have
                if (bytes / PAGE_SIZE < pages) {
                        start_sect += bytes / SECTOR_SIZE;
                        pages -= bytes / PAGE_SIZE;
                        continue;
                }
                
                break;
        }
        
        // drop our reference to the tp
        tp_put(tp);

        return 0;

error:
        LOG_ERROR(ret, "error tracing bio for snapshot");
        tracer_set_fail_state(dev, ret);

        // clean up the bio we allocated (but did not submit)
        if (new_bio)
                bio_free_clone(new_bio);

        if (tp)
                tp_put(tp);
        
        return 0;

call_orig:
        return SUBMIT_BIO_REAL(dev, bio);
}

/**
 * inc_make_sset() - This allocates a recordkeeping object to remember the
 * changed passed in the call.  This object is then queued for processing
 * by a kernel thread.
 *
 * @dev: the &struct snap_device used to compute the relative sector offset.
 * @sect: the absolute sector offset of the first changed sector
 * @len: the length of the changes
 *
 * Return:
 * * 0 - success
 * * !0 - an errno indicating the error
 */
static int inc_make_sset(struct snap_device *dev, sector_t sect,
                         unsigned int len)
{
        struct sector_set *sset;

        // allocate sector set to hold record of change sectors
        sset = kmalloc(sizeof(struct sector_set), GFP_NOIO);
        if (!sset) {
                LOG_ERROR(-ENOMEM, "error allocating sector set");
                return -ENOMEM;
        }

        sset->sect = sect - dev->sd_sect_off;
        sset->len = len;

        // queue sset for processing by kernel thread
        sset_queue_add(&dev->sd_pending_ssets, sset);

        return 0;
}

/**
 * inc_trace_bio() - Determines the regions modified by the @bio and
 * queues their affected regions so that a record of what changed can be
 * kept.  The bio is then processed by the original io submit function
 * (make_request_fn or submit_bio function ptr) so that the
 * modification can be made permanent.  This mode of tracing only
 * records what has changed and does not COW data.
 *
 * @dev: The &struct snap_device that keeps device state.
 * @bio: The &struct bio which describes the I/O.
 *
 * Return:
 * * 0 - success
 * * !0 - an errno indicating the error
 */
static int inc_trace_bio(struct snap_device *dev, struct bio *bio)
{
        int ret = 0, is_initialized = 0;
        sector_t start_sect = 0, end_sect = bio_sector(bio);
        bio_iter_t iter;
        bio_iter_bvec_t bvec;

#ifdef HAVE_ENUM_REQ_OPF
        //#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,10,0)
        if (bio_op(bio) == REQ_OP_WRITE_ZEROES) {
                ret = inc_make_sset(dev, bio_sector(bio),
                                    bio_size(bio) / SECTOR_SIZE);
                goto out;
        }
#endif

        bio_for_each_segment (bvec, bio, iter) {
                if (page_get_inode(bio_iter_page(bio, iter)) !=
                    dev->sd_cow_inode) {
                        if (!is_initialized) {
                                is_initialized = 1;
                                start_sect = end_sect;
                        }
                } else {
                        if (is_initialized && end_sect - start_sect > 0) {
                                ret = inc_make_sset(dev, start_sect,
                                                    end_sect - start_sect);
                                if (ret)
                                        goto out;
                        }
                        is_initialized = 0;
                }
                end_sect += (bio_iter_len(bio, iter) >> 9);
        }

        if (is_initialized && end_sect - start_sect > 0) {
                ret = inc_make_sset(dev, start_sect, end_sect - start_sect);
                if (ret)
                        goto out;
        }

out:
        if (ret) {
                LOG_ERROR(ret, "error tracing bio for incremental");
                tracer_set_fail_state(dev, ret);
                ret = 0;
        }

        // call the original mrf
        ret = SUBMIT_BIO_REAL(dev, bio);

        return ret;
}

/**
 * bdev_is_already_traced() - Checks to for the existance of the
 * &struct block_device in this driver's tracking state.
 *
 * @bdev: The &struct block_device in question.
 *
 * Return:
 * * 0 - not being traced
 * * 1 - already being traced
 */
static int bdev_is_already_traced(const struct block_device *bdev)
{
        int i;
        struct snap_device *dev;

        tracer_for_each(dev, i)
        {
                if (!dev || test_bit(UNVERIFIED, &dev->sd_state))
                        continue;
                if (dev->sd_base_dev == bdev)
                        return 1;
        }

        return 0;
}

/**
 * file_is_on_bdev() - Checks to see if the &struct file object is contained
 * within the &struct block_device device.
 *
 * @file: the file to check
 * @bdev: the &struct block_device that might hold the @file.
 *
 * Return:
 * * 0 - the @file is not on the @bdev.
 * * !0 - the @file is on the @bdev.
 */
static int file_is_on_bdev(const struct file *file, struct block_device *bdev)
{
        struct super_block *sb = dattobd_get_super(bdev);
        struct super_block *sb_file = (dattobd_get_mnt(file))->mnt_sb;
        int ret = 0;

        if (sb) {
                LOG_DEBUG("file_is_on_bdev() if(sb)");
                LOG_DEBUG("sb name:%s, file->sb name:%s", sb->s_root->d_name.name, sb_file->s_root->d_name.name);
                ret = ((dattobd_get_mnt(file))->mnt_sb == sb);
                dattobd_drop_super(sb);
        }
        return ret;
}

/**
 * minor_range_recalculate() - Updates the device minors tracked by this
 * driver.  This must be done whenever a minor number is no longer in use.
 */
static void minor_range_recalculate(void)
{
        unsigned int i, highest = 0, lowest = dattobd_max_snap_devices - 1;
        struct snap_device *dev;

        tracer_for_each_full(dev, i)
        {
                if (!dev)
                        continue;

                if (i < lowest)
                        lowest = i;
                if (i > highest)
                        highest = i;
        }

        lowest_minor = lowest;
        highest_minor = highest;
}

/**
 * minor_range_include() - Used to possibly expand the bounds kept to track
 * the minimum and maximum device minor numbers services by this driver.
 *
 * @minor: the device's minor number
 */
static void minor_range_include(unsigned int minor)
{
        if (minor < lowest_minor)
                lowest_minor = minor;
        if (minor > highest_minor)
                highest_minor = minor;
}

/**
 * __tracer_init() - initializes the &struct snap_device object.
 *
 * @dev: the &struct snap_device used to track changes to a snapshot device.
 */
static void __tracer_init(struct snap_device *dev)
{
        LOG_DEBUG("initializing tracer");
        atomic_set(&dev->sd_fail_code, 0);
        atomic_set(&dev->sd_active, 0);
        bio_queue_init(&dev->sd_cow_bios);
        bio_queue_init(&dev->sd_orig_bios);
        sset_queue_init(&dev->sd_pending_ssets);
}

/**
 * tracer_alloc() - Allocates and initializes the &struct snap_device object
 * used to track changes to the newly created snapshot device.
 *
 * @dev_ptr: resultant &struct snap_device allocated by this call.
 *
 * Return:
 * * 0 - success
 * * !0 - errno indicating the error
 */
int tracer_alloc(struct snap_device **dev_ptr)
{
        int ret;
        struct snap_device *dev;

        // allocate struct
        LOG_DEBUG("allocating device struct");
        dev = kzalloc(sizeof(struct snap_device), GFP_KERNEL);
        if (!dev) {
                ret = -ENOMEM;
                LOG_ERROR(ret, "error allocating memory for device struct");
                goto error;
        }

        __tracer_init(dev);

        *dev_ptr = dev;
        return 0;

error:
        LOG_ERROR(ret, "error allocating device struct");
        if (dev)
                kfree(dev);

        *dev_ptr = NULL;
        return ret;
}

/**
 * __tracer_destroy_cow() - Tears down COW tracking state, deallocating the
 * &struct cow_manager object in the process.
 *
 * @dev: The &struct snap_device that keeps snapshot device state.
 * @close_method: The close method.
 *                * 0: frees memory and unlinks the backing file.
 *                * 1: flushes section cache, closes COW file, deallocates
 *                     &struct cow_manager.
 *                * 2: flushes section cache, closes COW file.
 *                * other: undefined.
 *
 * Return:
 * * 0 - success
 * * !0 - errno indicating the error
 */
static int __tracer_destroy_cow(struct snap_device *dev, int close_method)
{
        int ret = 0;

        dev->sd_cow_inode = NULL;
        dev->sd_falloc_size = 0;
        dev->sd_cache_size = 0;

        if (dev->sd_cow) {
                LOG_DEBUG("destroying cow manager");

                if (close_method == 0) {
                        cow_free(dev->sd_cow);
                        dev->sd_cow = NULL;
                } else if (close_method == 1) {
                        ret = cow_sync_and_free(dev->sd_cow);
                        dev->sd_cow = NULL;
                } else if (close_method == 2) {
                        ret = cow_sync_and_close(dev->sd_cow);
                        task_work_flush();
                }
        }

        return ret;
}

/**
 * __tracer_setup_cow() - Sets up the COW tracking structures.
 *
 * @dev: The &struct snap_device that keeps snapshot device state.
 * @bdev: The &struct block_device that stores the COW data.
 * @cow_path: The path to the COW backing file.
 * @size: The number of sectors to allocate to the COW file.
 * @fallocated_space: A value of zero defaults the size.
 * @cache_size: Limits the size of the COW section cache (in bytes).
 * @uuid: A unique UUID assigned to a series of snapshots, or NULL to
 *        auto-generate a UUID.
 * @seqid: The current sequence ID to use in the header.
 * @open_method: The open method.
 *               The value of open_method determines how the
 *               &struct cow_manager and its cache will be handled.
 *               * 0: creates and initializes a new COW file.
 *               * 3: opens an existing COW file.
 *               * other: reloads the COW manager but not the cache.
 *
 * Return:
 * * 0 - success
 * * !0 - errno indicating the error
 */
static int __tracer_setup_cow(struct snap_device *dev,
                              struct block_device *bdev, const char *cow_path,
                              sector_t size, unsigned long fallocated_space,
                              unsigned long cache_size, const uint8_t *uuid,
                              uint64_t seqid, int open_method)
{
        int ret;
        uint64_t max_file_size;
        char bdev_name[BDEVNAME_SIZE];
 
        bdevname(bdev, bdev_name);
        LOG_DEBUG("bdevname %s, cow_path: %s", bdev_name, cow_path);

        if (open_method == 3) {
                // reopen the cow manager
                LOG_DEBUG("reopening the cow manager with file '%s'", cow_path);
                ret = cow_reopen(dev->sd_cow, cow_path);
                if (ret)
                        goto error;
        } else {
                if (!cache_size)
                        dev->sd_cache_size = dattobd_cow_max_memory_default;
                else
                        dev->sd_cache_size = cache_size;

                if (open_method == 0) {
                        // calculate how much space should be allocated to the
                        // cow file
                        if (!fallocated_space) {
                                max_file_size =
                                        size * SECTOR_SIZE *
                                        dattobd_cow_fallocate_percentage_default;
                                do_div(max_file_size, 100);
                                dev->sd_falloc_size = max_file_size;
                                do_div(dev->sd_falloc_size, (1024 * 1024));
                        } else {
                                max_file_size =
                                        fallocated_space * (1024 * 1024);
                                dev->sd_falloc_size = fallocated_space;
                        }

                        // create and open the cow manager
                        LOG_DEBUG("creating cow manager");
                        ret = cow_init(cow_path, SECTOR_TO_BLOCK(size),
                                       COW_SECTION_SIZE, dev->sd_cache_size,
                                       max_file_size, uuid, seqid,
                                       &dev->sd_cow);
                        if (ret)
                                goto error;
                } else {
                        // reload the cow manager
                        LOG_DEBUG("reloading cow manager");
                        ret = cow_reload(cow_path, SECTOR_TO_BLOCK(size),
                                         COW_SECTION_SIZE, dev->sd_cache_size,
                                         (open_method == 2), &dev->sd_cow);
                        if (ret)
                                goto error;

                        dev->sd_falloc_size = dev->sd_cow->file_max;
                        do_div(dev->sd_falloc_size, (1024 * 1024));
                }
        }

        // verify that file is on block device
        if (!file_is_on_bdev(dev->sd_cow->filp, bdev)) {
                ret = -EINVAL;
                LOG_ERROR(ret, "'%s' is not on '%s'", cow_path, bdev_name);
                goto error;
        }

        // find the cow file's inode number
        LOG_DEBUG("finding cow file inode");
        dev->sd_cow_inode = dattobd_get_dentry(dev->sd_cow->filp)->d_inode;

        return 0;

error:
        LOG_ERROR(ret, "error setting up cow manager");
        if (open_method != 3)
                __tracer_destroy_cow_free(dev);
        return ret;
}

/**
 * __tracer_destroy_base_dev() - Tears down the base block device.
 *
 * @dev: The &struct snap_device object pointer.
 */
static void __tracer_destroy_base_dev(struct snap_device *dev)
{
        dev->sd_size = 0;
        dev->sd_sect_off = 0;

        if (dev->sd_bdev_path) {
                LOG_DEBUG("freeing base block device path");
                kfree(dev->sd_bdev_path);
                dev->sd_bdev_path = NULL;
        }

        if (dev->sd_base_dev) {
                LOG_DEBUG("freeing base block device");
                dattobd_blkdev_put(dev->sd_base_dev);
                dev->sd_base_dev = NULL;
        }
}

/**
 * __tracer_setup_base_dev() - Sets up the base block device.
 *
 * @dev: The &struct snap_device object pointer.
 * @bdev_path: The block device path, e.g., '/dev/loop0'.
 *
 * Return:
 * * 0 - success
 * * !0 - errno indicating the error
 */
static int __tracer_setup_base_dev(struct snap_device *dev,
                                   const char *bdev_path)
{
        int ret;

        // open the base block device
        LOG_DEBUG("ENTER __tracer_setup_base_dev");
        dev->sd_base_dev = blkdev_get_by_path(bdev_path, FMODE_READ, NULL);
        if (IS_ERR(dev->sd_base_dev)) {
                ret = PTR_ERR(dev->sd_base_dev);
                dev->sd_base_dev = NULL;
                LOG_ERROR(ret, "error finding block device '%s'", bdev_path);
                goto error;
        } else if (!dev->sd_base_dev->bd_disk) {
                ret = -EFAULT;
                LOG_ERROR(ret, "error finding block device gendisk");
                goto error;
        }

        // check block device is not already being traced
        LOG_DEBUG("checking block device is not already being traced");
        if (bdev_is_already_traced(dev->sd_base_dev)) {
                ret = -EINVAL;
                LOG_ERROR(ret, "block device is already being traced");
                goto error;
        }

        // fetch the absolute pathname for the base device
        LOG_DEBUG("fetching the absolute pathname for the base device");
        ret = pathname_to_absolute(bdev_path, &dev->sd_bdev_path, NULL);
        if (ret)
                goto error;

        // check if device represents a partition, calculate size and offset
        LOG_DEBUG("calculating block device size and offset");
        if (bdev_whole(dev->sd_base_dev) != dev->sd_base_dev) {
                dev->sd_sect_off = get_start_sect(dev->sd_base_dev);
                dev->sd_size = dattobd_bdev_size(dev->sd_base_dev);
        } else {
                dev->sd_sect_off = 0;
                dev->sd_size = get_capacity(dev->sd_base_dev->bd_disk);
        }

        LOG_DEBUG("bdev size = %llu, offset = %llu",
                  (unsigned long long)dev->sd_size,
                  (unsigned long long)dev->sd_sect_off);

        return 0;

error:
        LOG_ERROR(ret, "error setting up base block device");
        __tracer_destroy_base_dev(dev);
        return ret;
}

/**
 * __tracer_copy_base_dev() - Copies base block device fields from @src
 *                            to @dest.
 *
 * @src: The &struct snap_device source object pointer.
 * @dest: The &struct snap_device destination object pointer.
 */
static void __tracer_copy_base_dev(const struct snap_device *src,
                                   struct snap_device *dest)
{
        dest->sd_size = src->sd_size;
        dest->sd_sect_off = src->sd_sect_off;
        dest->sd_base_dev = src->sd_base_dev;
        dest->sd_bdev_path = src->sd_bdev_path;
}

#ifdef HAVE_MERGE_BVEC_FN
#ifdef HAVE_BVEC_MERGE_DATA

/**
 * snap_merge_bvec() - Determines if it can augment an existing request with
 *                     more data.  Requests queues usually have fixed size
 *                     limits for their requests but specialized devices might
 *                     have varying limits.
 *
 * @q: The &struct request_queue object pointer.
 * @bvm: the &struct bvec_merge_data passed to the merge_bvec_fn() call.
 * @bvec: the &struct bio_vec passed to the merge_bvec_fn() call.
 *
 * Return: the result returned from the wrapped function.
 */
static int snap_merge_bvec(struct request_queue *q, struct bvec_merge_data *bvm,
                           struct bio_vec *bvec)
{
        struct snap_device *dev = q->queuedata;
        struct request_queue *base_queue = bdev_get_queue(dev->sd_base_dev);

        bvm->bi_bdev = dev->sd_base_dev;

        return base_queue->merge_bvec_fn(base_queue, bvm, bvec);
}

#else

/**
 * snap_merge_bvec() - Determines if it can augment an existing request with
 *                     more data.  Requests queues usually have fixed size
 *                     limits for their requests but specialized devices might
 *                     have varying limits.
 *
 * @q: The &struct request_queue object pointer.
 * @bio_bvm: the &struct bvec_merge_data passed to the merge_bvec_fn() call.
 * @bvec: the &struct bio_vec passed to the merge_bvec_fn() call.
 *
 * Return: the result returned from the wrapped function.
 */
static int snap_merge_bvec(struct request_queue *q, struct bio *bio_bvm,
                           struct bio_vec *bvec)
{
        struct snap_device *dev = q->queuedata;
        struct request_queue *base_queue = bdev_get_queue(dev->sd_base_dev);

        bio_bvm->bi_bdev = dev->sd_base_dev;

        return base_queue->merge_bvec_fn(base_queue, bio_bvm, bvec);
}
#endif
#endif

/**
 * __tracer_copy_cow() - Copies COW fields from @src to @dest.
 *
 * @src: The &struct snap_device source object pointer.
 * @dest: The &struct snap_device destination object pointer.
 */
static void __tracer_copy_cow(const struct snap_device *src,
                              struct snap_device *dest)
{
        dest->sd_cow = src->sd_cow;
        dest->sd_cow_inode = src->sd_cow_inode;
        dest->sd_cache_size = src->sd_cache_size;
        dest->sd_falloc_size = src->sd_falloc_size;
}

/**
 * __tracer_destroy_cow_path() - Tears down the COW path
 *
 * @dev: The &struct snap_device object pointer.
 */
static void __tracer_destroy_cow_path(struct snap_device *dev)
{
        if (dev->sd_cow_path) {
                LOG_DEBUG("freeing cow path");
                kfree(dev->sd_cow_path);
                dev->sd_cow_path = NULL;
        }
}

/**
 * __tracer_setup_cow_path() - Sets up the COW file path given a &struct file.
 *
 * @dev: The &struct snap_device object pointer.
 * @cow_file: The &struct file object pointer.
 *
 * Return:
 * * 0 - success
 * * !0 - errno indicating the error
 */
static int __tracer_setup_cow_path(struct snap_device *dev,
                                   const struct file *cow_file)
{
        int ret;

        // get the pathname of the cow file (relative to the mountpoint)
        LOG_DEBUG("getting relative pathname of cow file");
        ret = dentry_get_relative_pathname(dattobd_get_dentry(cow_file),
                                           &dev->sd_cow_path, NULL);
        if (ret)
                goto error;

        return 0;

error:
        LOG_ERROR(ret, "error setting up cow file path");
        __tracer_destroy_cow_path(dev);
        return ret;
}

/**
 * __tracer_setup_cow_path() - Sets up the COW file path given a &struct file.
 *
 * @dev: The &struct snap_device object pointer.
 * @cow_file: The &struct file object pointer.
 *
 * Return:
 * * 0 - success
 * * !0 - errno indicating the error
 */
static void __tracer_copy_cow_path(const struct snap_device *src,
                                   struct snap_device *dest)
{
        dest->sd_cow_path = src->sd_cow_path;
}

/**
 * __tracer_bioset_exit() - Releases the bioset within the &struct snap_device.
 *
 * @dev: The &struct snap_device object pointer.
 */
static void __tracer_bioset_exit(struct snap_device *dev)
{
#ifndef HAVE_BIOSET_INIT
        //#if LINUX_VERSION_CODE < KERNEL_VERSION(4,18,0)
        if (dev->sd_bioset) {
                LOG_DEBUG("freeing bio set");
                bioset_free(dev->sd_bioset);
                dev->sd_bioset = NULL;
        }
#else
        bioset_exit(&dev->sd_bioset);
#endif
}

/**
 * __tracer_destroy_snap() - Tears down a snap device.
 *
 * @dev: The &struct snap_device object pointer.
 */
static void __tracer_destroy_snap(struct snap_device *dev)
{
        if (dev->sd_mrf_thread) {
                LOG_DEBUG("stopping mrf thread");
                kthread_stop(dev->sd_mrf_thread);
                dev->sd_mrf_thread = NULL;
        }

        if (dev->sd_gd) {
                LOG_DEBUG("freeing gendisk");
#ifdef GENHD_FL_UP
                if (dev->sd_gd->flags & GENHD_FL_UP)
#else
                if (disk_live(dev->sd_gd))
#endif
                        del_gendisk(dev->sd_gd);
                put_disk(dev->sd_gd);
                dev->sd_gd = NULL;
        }

        if (dev->sd_queue) {
                LOG_DEBUG("freeing request queue");
                blk_cleanup_queue(dev->sd_queue);
                dev->sd_queue = NULL;
        }

        __tracer_bioset_exit(dev);
}

/**
 * __tracer_bioset_init() - Initializes the bioset field for the
 *                          &struct snap_device.
 *
 * @dev: The &struct snap_device object pointer.
 *
 * Return:
 * * 0 - success
 * * !0 - errno indicating the error
 */
static int __tracer_bioset_init(struct snap_device *dev)
{
#ifndef HAVE_BIOSET_INIT
        //#if LINUX_VERSION_CODE < KERNEL_VERSION(4,18,0)
        dev->sd_bioset = dattobd_bioset_create(BIO_SET_SIZE, BIO_SET_SIZE, 0);
        if (!dev->sd_bioset)
                return -ENOMEM;
        return 0;
#else
        return bioset_init(&dev->sd_bioset, BIO_SET_SIZE, BIO_SET_SIZE,
                           BIOSET_NEED_BVECS);
#endif
}

/**
 * __tracer_setup_snap() - Allocates &struct snap_device fields for use when
 *                         tracking an active snapshot.  Also sets up the
 *                         read-only disk used to present a snapshot image of
 *                         the underlying live volume and registers it with
 *                         the kernel.
 *
 * @dev: The &struct snap_device object pointer.
 * @minor: the device's minor number.
 * @bdev: The &struct block_device that stores the COW data.
 * @size: The number of sectors to allocate to the block device.
 *
 * Return:
 * * 0 - success
 * * !0 - errno indicating the error
 */
static int __tracer_setup_snap(struct snap_device *dev, unsigned int minor,
                               struct block_device *bdev, sector_t size)
{
        int ret;
        
        ret = __tracer_bioset_init(dev);
        if (ret) {
                LOG_ERROR(ret, "error initializing bio set");
                goto error;
        }

        // allocate a gendisk struct
        LOG_DEBUG("allocating gendisk");
#ifdef HAVE_BLK_ALLOC_DISK
        dev->sd_gd = blk_alloc_disk(NUMA_NO_NODE);
#else
        dev->sd_gd = alloc_disk(1);
#endif
        if (!dev->sd_gd) {
                ret = -ENOMEM;
                LOG_ERROR(ret, "error allocating gendisk");
                goto error;
        }

        // allocate request queue
        LOG_DEBUG("allocating queue"); 
#ifdef HAVE_BLK_ALLOC_QUEUE_1
        //#if LINUX_VERSION_CODE < KERNEL_VERSION(5,7,0)
        dev->sd_queue = blk_alloc_queue(GFP_KERNEL);
#elif defined HAVE_BLK_ALLOC_QUEUE_RH_2 // el8
        dev->sd_queue = blk_alloc_queue_rh(snap_mrf, NUMA_NO_NODE);
#elif defined HAVE_BLK_ALLOC_QUEUE_2
        dev->sd_queue = blk_alloc_queue(snap_mrf, NUMA_NO_NODE);
#elif !defined HAVE_BLK_ALLOC_DISK
        //#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,9,0)
        dev->sd_queue = blk_alloc_queue(NUMA_NO_NODE);
#else
        dev->sd_queue = dev->sd_gd->queue;
#endif

        if (!dev->sd_queue) {
                ret = -ENOMEM;
                LOG_ERROR(ret, "error allocating request queue");
                goto error;
        }

#if !defined HAVE_BLK_ALLOC_QUEUE && !defined USE_BDOPS_SUBMIT_BIO
        LOG_DEBUG("setting up make request function");
        blk_queue_make_request(dev->sd_queue, snap_mrf);
#endif

        // give our request queue the same properties as the base device's
        LOG_DEBUG("setting queue limits");
        blk_set_stacking_limits(&dev->sd_queue->limits);
        dattobd_bdev_stack_limits(dev->sd_queue, bdev, 0);

#ifdef HAVE_MERGE_BVEC_FN
        // use a thin wrapper around the base device's merge_bvec_fn
        if (bdev_get_queue(bdev)->merge_bvec_fn)
                blk_queue_merge_bvec(dev->sd_queue, snap_merge_bvec);
#endif

        // initialize gendisk and request queue values
        LOG_DEBUG("initializing gendisk");
        dev->sd_queue->queuedata = dev;
        dev->sd_gd->private_data = dev;
        dev->sd_gd->major = major;
        dev->sd_gd->first_minor = minor;
        dev->sd_gd->minors = 1;
        dev->sd_gd->fops = get_snap_ops();
        dev->sd_gd->queue = dev->sd_queue;

        // name our gendisk
        LOG_DEBUG("naming gendisk");
        snprintf(dev->sd_gd->disk_name, 32, SNAP_DEVICE_NAME, minor);

        // set the capacity of our gendisk
        LOG_DEBUG("block device size: %llu", (unsigned long long)size);
        set_capacity(dev->sd_gd, size);

#ifdef HAVE_GENHD_FL_NO_PART_SCAN
        //#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,2,0)
        // disable partition scanning (the device should not have any
        // sub-partitions)
        dev->sd_gd->flags |= GENHD_FL_NO_PART_SCAN;
#endif

        // set the device as read-only
        set_disk_ro(dev->sd_gd, 1);

        // register gendisk with the kernel
        LOG_DEBUG("adding disk");
        add_disk(dev->sd_gd);

        LOG_DEBUG("starting mrf kernel thread");
        dev->sd_mrf_thread = kthread_run(snap_mrf_thread, dev,
                                         SNAP_MRF_THREAD_NAME_FMT, minor);
        if (IS_ERR(dev->sd_mrf_thread)) {
                ret = PTR_ERR(dev->sd_mrf_thread);
                dev->sd_mrf_thread = NULL;
                LOG_ERROR(ret, "error starting mrf kernel thread");
                goto error;
        }

        atomic64_set(&dev->sd_submitted_cnt, 0);
        atomic64_set(&dev->sd_received_cnt, 0);

        return 0;

error:
        LOG_ERROR(ret, "error setting up snapshot");
        __tracer_destroy_snap(dev);
        return ret;
}

/**
 * __tracer_bioset_exit() - Releases the bioset within the &struct snap_device.
 *
 * @dev: The &struct snap_device object pointer.
 */
static void __tracer_destroy_cow_thread(struct snap_device *dev)
{
        if (dev->sd_cow_thread) {
                LOG_DEBUG("stopping cow thread");
                kthread_stop(dev->sd_cow_thread);
                dev->sd_cow_thread = NULL;
        }
}

/**
 * __tracer_setup_cow_thread() - Creates a COW thread and associates it with
 *                               the &struct snap_device.
 *
 * @dev: The &struct snap_device object pointer.
 * @minor: the device's minor number.
 * @is_snap: snapshot or incremental.
 *
 * Return:
 * * 0 - success
 * * !0 - errno indicating the error
 */
static int __tracer_setup_cow_thread(struct snap_device *dev,
                                     unsigned int minor, int is_snap)
{
        int ret;

        LOG_DEBUG("creating kernel cow thread");
        if (is_snap)
                dev->sd_cow_thread = kthread_create(
                        snap_cow_thread, dev, SNAP_COW_THREAD_NAME_FMT, minor);
        else
                dev->sd_cow_thread = kthread_create(inc_sset_thread, dev,
                                                    INC_THREAD_NAME_FMT, minor);

        if (IS_ERR(dev->sd_cow_thread)) {
                ret = PTR_ERR(dev->sd_cow_thread);
                dev->sd_cow_thread = NULL;
                LOG_ERROR(ret, "error creating kernel thread");
                goto error;
        }

        return 0;

error:
        LOG_ERROR(ret, "error setting up cow thread");
        __tracer_destroy_cow_thread(dev);
        return ret;
}

#ifdef USE_BDOPS_SUBMIT_BIO
#ifdef HAVE_FTRACE_REGS
static void notrace ftrace_handler_submit_bio_noacct(unsigned long ip,
        unsigned long parent_ip,
        struct ftrace_ops *fops,
        struct ftrace_regs *fregs);
#else
static void notrace ftrace_handler_submit_bio_noacct(unsigned long ip,
        unsigned long parent_ip,
        struct ftrace_ops *fops,
        struct pt_regs *fregs);
#endif

unsigned char* funcname_submit_bio_noacct = "submit_bio_noacct";
struct ftrace_ops ops_submit_bio_noacct = {
	.func = ftrace_handler_submit_bio_noacct,
	.flags = FTRACE_OPS_FL_SAVE_REGS | FTRACE_OPS_FL_PERMANENT | FTRACE_OPS_FL_IPMODIFY
};
#endif

int tracer_registered = 0;
int register_tracer_filter(void)
{
        int ret = 0;
#ifdef USE_BDOPS_SUBMIT_BIO
        ret = ftrace_set_filter(
                &ops_submit_bio_noacct,
                funcname_submit_bio_noacct,
                strlen(funcname_submit_bio_noacct),
                0);
        if (ret)
        {
                return ret;
        }

        ret = register_ftrace_function(&ops_submit_bio_noacct);
#endif
        tracer_registered = 1;
        return ret;
}

int unregister_tracer_filter(void)
{
        if (tracer_registered) {
                tracer_registered = 0;
#ifdef USE_BDOPS_SUBMIT_BIO
                return unregister_ftrace_function(&ops_submit_bio_noacct);
#endif
        }
        return 0;
}

/**
 * __tracer_transition_tracing() - Starts or ends tracing on @bdev depending
 *                                 on whether @dev is defined.  The @bdev is
 *                                 frozen while transitioning and then thawed
 *                                 afterwards so that requests  can be
 *                                 reinstated on @bdev.
 *
 * @dev: The &struct snap_device object pointer.
 * @bdev: The &struct block_device that stores the COW data.
 * @new_bio_tracking_ptr: Optional function pointer to be used by the snapshot disk
 *         i/o handling, may be NULL to continue using the current function pointer.
 * @dev_ptr: Contains the output &struct snap_device when successful.
 *
 * Return:
 * * 0 - success
 * * !0 - errno indicating the error
 */
static int __tracer_transition_tracing(
    struct snap_device *dev,
    struct block_device *bdev,
    BIO_REQUEST_CALLBACK_FN *new_bio_tracking_ptr,
    struct snap_device **dev_ptr)
{
        int ret;
        struct super_block *origsb = dattobd_get_super(bdev);
        #ifdef HAVE_FREEZE_SB
        struct super_block *sb = NULL;
        #endif
        char bdev_name[BDEVNAME_SIZE];
        MAYBE_UNUSED(ret);

        bdevname(bdev, bdev_name);
        if(origsb){
                dattobd_drop_super(origsb);

                //freeze and sync block device
                LOG_DEBUG("freezing '%s'", bdev_name);
#ifdef HAVE_FREEZE_SB
//#if LINUX_VERSION_CODE < KERNEL_VERSION(5,11,0)
                sb = freeze_bdev(bdev);
                if(!sb){
                        LOG_ERROR(-EFAULT, "error freezing '%s': null",
                                  bdev_name);
                        return -EFAULT;
                }else if(IS_ERR(sb)){
                        LOG_ERROR((int)PTR_ERR(sb),
                                  "error freezing '%s': error", bdev_name);
                        return (int)PTR_ERR(sb);
                }
#else
                ret = freeze_bdev(bdev);
                if (ret) {
                        LOG_ERROR(ret, "error freezing '%s'", bdev_name);
                        return -ret;
                }
#endif
        }
        else {
                LOG_WARN(
                        "warning: no super found for device '%s', "
                        "unable to freeze it",
                        bdev_name);
        }
        smp_wmb();
        if(dev){
                LOG_DEBUG("starting tracing");
                *dev_ptr = dev;
                smp_wmb();
#ifndef USE_BDOPS_SUBMIT_BIO
                if(new_bio_tracking_ptr){
                        bdev->bd_disk->queue->make_request_fn = 
                                new_bio_tracking_ptr;
                }
#endif
                atomic_inc(&(*dev_ptr)->sd_active);
        }else{
                LOG_DEBUG("ending tracing");
                atomic_dec(&(*dev_ptr)->sd_active);
#ifndef USE_BDOPS_SUBMIT_BIO
                new_bio_tracking_ptr = mrf_put(bdev);
                if (new_bio_tracking_ptr){
                        bdev->bd_disk->queue->make_request_fn =
                                new_bio_tracking_ptr;
                }
#endif
                *dev_ptr = dev;
                smp_wmb();
        }
        if(origsb){
                // thaw the block device
                LOG_DEBUG("thawing '%s'", bdev_name);
#ifdef HAVE_THAW_BDEV_INT
                ret = thaw_bdev(bdev, sb);
#else
                ret = thaw_bdev(bdev);
#endif
                if(ret){
                        LOG_ERROR(ret, "error thawing '%s'", bdev_name);
                        // We can't reasonably undo what we've done at this 
                        // point, and we've replaced the mrf. pretend we 
                        // succeeded so we don't break the block device
                }
        }
        return 0;
}

/**     
 * tracing_fn() - This is the entry point for in-flight i/o we intercepted.
 * @q: The &struct request_queue.
 * @bio: The &struct bio which describes the I/O.
 *
 * If the BIO has been marked as passthrough then the block device's
 * original device's function pointer for handling i/o is used to process the BIO.
 * Otherwise, depending on whether we're in snapshot or incremental mode, the appropriate 
 * handler is called.
 *
 * Return: varies across versions of Linux and is what's expected by each for
 *         a make request function.
 */
#ifdef USE_BDOPS_SUBMIT_BIO
static asmlinkage MRF_RETURN_TYPE tracing_fn(struct bio *bio)
#else
static MRF_RETURN_TYPE tracing_fn(struct request_queue *q, struct bio *bio)
#endif
{
        int i, ret = 0;
        struct snap_device *dev = NULL;
        MAYBE_UNUSED(ret);

        smp_rmb();
        tracer_for_each(dev, i)
        {
                if (!tracer_is_bio_for_dev(dev, bio)) continue;
                // If we get here, then we know this is a device we're managing
                // and the current bio belongs to said device.
                if (dattobd_bio_op_flagged(bio, DATTOBD_PASSTHROUGH))
                {
                        dattobd_bio_op_clear_flag(bio, DATTOBD_PASSTHROUGH);
                }
                else
                {
                        if (tracer_should_trace_bio(dev, bio))
                        {
                                if (test_bit(SNAPSHOT, &dev->sd_state))
                                        ret = snap_trace_bio(dev, bio);
                                else
                                        ret = inc_trace_bio(dev, bio);
                                goto out;
                        }
                }

                // Now we can submit the bio.
                ret = SUBMIT_BIO_REAL(dev, bio);

                goto out;
                
        } // tracer_for_each(dev, i)

#ifdef USE_BDOPS_SUBMIT_BIO
        ret = SUBMIT_BIO_REAL(NULL, bio);
#endif

out:
        MRF_RETURN(ret);
}

#ifdef USE_BDOPS_SUBMIT_BIO
#ifdef HAVE_FTRACE_REGS
static void notrace ftrace_handler_submit_bio_noacct(unsigned long ip,
        unsigned long parent_ip,
        struct ftrace_ops *fops,
        struct ftrace_regs *fregs)
{
        ftrace_instruction_pointer_set(fregs, (unsigned long)tracing_fn);
}
#else
static void notrace ftrace_handler_submit_bio_noacct(unsigned long ip,
        unsigned long parent_ip,
        struct ftrace_ops *fops,
        struct pt_regs *fregs)
{
        fregs->ip = (unsigned long)tracing_fn;
}
#endif
#endif

#ifndef USE_BDOPS_SUBMIT_BIO

/**
 * dattobd_find_orig_mrf() - Locates the original MRF function associated with
 *                   the @bdev block device.  All tracked block devices
 *                   are checked until a match is found.
 *
 * @bdev: The &struct block_device that stores the COW data.
 * @mrf: The original MRF function, if found.
 *
 * Return:
 * * 0 - success
 * * !0 - errno indicating the error
 */
static int dattobd_find_orig_mrf(struct block_device *bdev,
                                 make_request_fn **mrf){
        int i;
        struct snap_device *dev;
        struct request_queue *q = bdev_get_queue(bdev);
        make_request_fn *orig_mrf = dattobd_get_bd_mrf(bdev);

        if(orig_mrf != tracing_fn){
#ifdef HAVE_BLK_MQ_MAKE_REQUEST
                // Linux version 5.8
                if (!orig_mrf){
                        orig_mrf = dattobd_null_mrf;
                        LOG_DEBUG(
                            "original mrf is empty, set to dattobd_null_mrf");
                }
#endif
            *mrf = orig_mrf;
            return 0;
        }

        tracer_for_each(dev, i){
                if(!dev || test_bit(UNVERIFIED, &dev->sd_state)) continue;
                if(q == bdev_get_queue(dev->sd_base_dev)){
                        *mrf = dev->sd_orig_request_fn;
                        return 0;
                }
        }

        *mrf = NULL;
        return -EFAULT;
}

#endif

/**
 * __tracer_should_reset_mrf() - Searches the traced devices and verifies that
 * the device would have had a make_request_fn when tracing was initiated.
 * @dev: The &struct snap_device object pointer.
 *
 * Return:
 * * 0 - success
 * * !0 - errno indicating the error
 */
static int __tracer_should_reset_mrf(const struct snap_device *dev)
{
    int i;
    struct snap_device *cur_dev;
    struct request_queue *q = bdev_get_queue(dev->sd_base_dev);

#ifndef USE_BDOPS_SUBMIT_BIO
    if (GET_BIO_REQUEST_TRACKING_PTR(dev->sd_base_dev) != tracing_fn) return 0;
#endif
    if (dev != snap_devices[dev->sd_minor]) return 0;

    //return 0 if there is another device tracing the same queue as dev.
    if (snap_devices){
        tracer_for_each(cur_dev, i){
            if (!cur_dev || test_bit(UNVERIFIED, &cur_dev->sd_state) 
                || cur_dev == dev) continue;
            if (q == bdev_get_queue(cur_dev->sd_base_dev)) return 0;
        }
    }

    return 1;
}

/**
 * __tracer_destroy_tracing() - Stops tracing of the &struct snap_device
 *                              and possibly reinstates the original MRF
 *                              function if necessary.
 *
 * @dev: The &struct snap_device object pointer.
 */
static void __tracer_destroy_tracing(struct snap_device *dev)
{
        if(dev->sd_orig_request_fn){
                LOG_DEBUG("replacing make_request_fn if needed");
                if(__tracer_should_reset_mrf(dev)){
                        __tracer_transition_tracing(
                            NULL,
                            dev->sd_base_dev,
                            dev->sd_orig_request_fn,
                            &snap_devices[dev->sd_minor]
                        );
                }
        else
        {
                __tracer_transition_tracing(
                        NULL,
                        dev->sd_base_dev,
                        NULL,
                        &snap_devices[dev->sd_minor]
                );
        }
        smp_wmb();
        dattobd_free_request_tracking_ptr(dev);

        }
        else if(snap_devices[dev->sd_minor] == dev)
        {
                smp_wmb();
                snap_devices[dev->sd_minor] = NULL;
                smp_wmb();
        }

        dev->sd_minor = 0;
        minor_range_recalculate();
}

/**
 * __tracer_setup_tracing_unverified() - Assigns @dev to the array of snap
 *                                       devices begin tracked by this
 *                                       driver.
 *
 * @dev: The &struct snap_device object pointer.
 * @minor: the device's minor number.
 */
static void __tracer_setup_tracing_unverified(struct snap_device *dev,
                                              unsigned int minor)
{
    minor_range_include(minor);
    smp_wmb();
    dev->sd_minor = minor;
    snap_devices[minor] = dev;
    smp_wmb();
}

/**
 * __tracer_setup_tracing() - Adds @minor to the range of included tracked
 *                            minors, saves the original io submit function ptr
 *                            and replaces it with the tracing_fn function ptr for 
 *                            the block device associated with our &struct snap_device.
 *
 * @dev: The &struct snap_device object pointer.
 * @minor: the device's minor number.
 *
 * Return:
 * * 0 - success
 * * !0 - errno indicating the error
 */
int __tracer_setup_tracing(struct snap_device *dev, unsigned int minor)
{
        int ret = 0;

        dev->sd_minor = minor;
        minor_range_include(minor);

        // get the base block device's make_request_fn
        LOG_DEBUG("getting the base block device's make_request_fn");

#ifndef USE_BDOPS_SUBMIT_BIO
        ret = dattobd_find_orig_mrf(dev->sd_base_dev, &dev->sd_orig_request_fn);
        if (ret)
                goto error;
#endif
        ret = __tracer_transition_tracing(
                dev,
                dev->sd_base_dev,
                tracing_fn,
                &snap_devices[minor]);
        if (ret)
                goto error;
        return 0;

error:
        LOG_ERROR(ret, "error setting up tracing");
        dev->sd_minor = 0;
        dev->sd_orig_request_fn = NULL;
        minor_range_recalculate();
        return ret;
}

/**
 * __tracer_setup_unverified() - Sets up tracing for an unverified device.
 *
 * @dev: The &struct snap_device object pointer.
 * @minor: the device's minor number.
 * @bdev_path: The block device path, e.g., '/dev/loop0'.
 * @cow_path: The path to the COW backing file.
 * @cache_size: Limits the size of the COW section cache (in bytes).
 * @is_snap: snapshot or incremental.
 *
 * Return:
 * * 0 - success
 * * !0 - errno indicating the error
 */
int __tracer_setup_unverified(struct snap_device *dev, unsigned int minor,
                              const char *bdev_path, const char *cow_path,
                              unsigned long cache_size, int is_snap)
{       
        LOG_DEBUG("Enter __tracer_setup_unverified path %s", bdev_path);

        if (is_snap)
                set_bit(SNAPSHOT, &dev->sd_state);
        else
                clear_bit(SNAPSHOT, &dev->sd_state);
        clear_bit(ACTIVE, &dev->sd_state);
        set_bit(UNVERIFIED, &dev->sd_state);

        dev->sd_cache_size = cache_size;

        dev->sd_bdev_path = kstrdup(bdev_path, GFP_KERNEL);
        if (!dev->sd_bdev_path)
                goto error;

        dev->sd_cow_path = kstrdup(cow_path, GFP_KERNEL);
        if (!dev->sd_cow_path)
                goto error;

        // add the tracer to the array of devices
        __tracer_setup_tracing_unverified(dev, minor);

        return 0;

error:
        LOG_ERROR(-ENOMEM, "error setting up unverified tracer");
        tracer_destroy(dev);
        return -ENOMEM;
}

/************************SETUP / DESTROY FUNCTIONS************************/

/**
 * tracer_destroy() - Tears down tracing of the snap device freeing necessary
 *                    fields in the process.
 *
 * @dev: The &struct snap_device object pointer.
 */
void tracer_destroy(struct snap_device *dev)
{
        __tracer_destroy_tracing(dev);
        __tracer_destroy_cow_thread(dev);
        __tracer_destroy_snap(dev);
        __tracer_destroy_cow_path(dev);
        __tracer_destroy_cow_free(dev);
        __tracer_destroy_base_dev(dev);
}

/**
 * tracer_setup_active_snap() - Sets up for a snapshot.
 *
 * @dev: The &struct snap_device object pointer.
 * @minor: the device's minor number.
 * @bdev_path: The block device path, e.g., '/dev/loop0'.
 * @cow_path: The path to the COW backing file.
 * @fallocated_space: A value of zero defaults the size.
 * @cache_size: Limits the size of the COW section cache (in bytes).
 *
 * This call sets up the snapshot device, creates the COW file including the
 * data region, determines the COW path, sets up the COW thread, and finally
 * initiates tracing.
 *
 * Return:
 * * 0 - success
 * * !0 - errno indicating the error
 */
int tracer_setup_active_snap(struct snap_device *dev, unsigned int minor,
                             const char *bdev_path, const char *cow_path,
                             unsigned long fallocated_space,
                             unsigned long cache_size)
{
        int ret;

        LOG_DEBUG("ENTER tracer_setup_active_snap");
        set_bit(SNAPSHOT, &dev->sd_state);
        set_bit(ACTIVE, &dev->sd_state);
        clear_bit(UNVERIFIED, &dev->sd_state);

        // setup base device
        ret = __tracer_setup_base_dev(dev, bdev_path);
        if (ret)
                goto error;

        // setup the cow manager
        ret = __tracer_setup_cow_new(dev, dev->sd_base_dev, cow_path,
                                     dev->sd_size, fallocated_space, cache_size,
                                     NULL, 1);
        if (ret)
                goto error;

        // setup the cow path
        ret = __tracer_setup_cow_path(dev, dev->sd_cow->filp);
        if (ret)
                goto error;

#ifndef USE_BDOPS_SUBMIT_BIO
        // retain an association between the original mrf and the block device
        ret = mrf_get(dev->sd_base_dev, GET_BIO_REQUEST_TRACKING_PTR(dev->sd_base_dev));
        if (ret)
                goto error;
#endif

        // setup the snapshot values
        ret = __tracer_setup_snap(dev, minor, dev->sd_base_dev, dev->sd_size);
        if (ret)
                goto error;

        // setup the cow thread and run it
        ret = __tracer_setup_snap_cow_thread(dev, minor);
        if (ret)
                goto error;

        wake_up_process(dev->sd_cow_thread);

        // inject the tracing function
        ret = __tracer_setup_tracing(dev, minor);
        if (ret)
                goto error;

        return 0;

error:
        LOG_ERROR(ret, "error setting up tracer as active snapshot");
        tracer_destroy(dev);
        return ret;
}

/************************IOCTL TRANSITION FUNCTIONS************************/

/**
 * tracer_active_snap_to_inc() - Transitions from snapshot mode to incremental
 * tracking.
 * @old_dev: The &struct snap_device being replaced by this call.
 *
 * Return:
 * * 0 - success
 * * !0 - errno indicating the error
 */
int tracer_active_snap_to_inc(struct snap_device *old_dev)
{
        int ret;
        struct snap_device *dev;
        char *abs_path = NULL;
        int abs_path_len;

        // allocate new tracer
        ret = tracer_alloc(&dev);
        if (ret)
                return ret;

        clear_bit(SNAPSHOT, &dev->sd_state);
        set_bit(ACTIVE, &dev->sd_state);
        clear_bit(UNVERIFIED, &dev->sd_state);

        // copy / set fields we need
        __tracer_copy_base_dev(old_dev, dev);
        __tracer_copy_cow_path(old_dev, dev);

        // copy cow manager to new device. Care must be taken to make sure it
        // isn't used by multiple threads at once.
        __tracer_copy_cow(old_dev, dev);

        // setup the cow thread
        ret = __tracer_setup_inc_cow_thread(dev, old_dev->sd_minor);
        if (ret)
                goto error;

        // inject the tracing function
        dev->sd_orig_request_fn = old_dev->sd_orig_request_fn;
        ret = __tracer_setup_tracing(dev, old_dev->sd_minor);
        if (ret)
                goto error;

        // Below this point, we are commited to the new device, so we must make
        // sure it is in a good state.

        // stop the old cow thread. Must be done before starting the new cow
        // thread to prevent concurrent access.
        __tracer_destroy_cow_thread(old_dev);

        // sanity check to ensure no errors have occurred while cleaning up the
        // old cow thread
        ret = tracer_read_fail_state(old_dev);
        if (ret) {
                LOG_ERROR(
                        ret,
                        "errors occurred while cleaning up cow thread, putting "
                        "incremental into error state");
                tracer_set_fail_state(dev, ret);

                // must make up the new thread regardless of errors so that any
                // queued ssets are cleaned up
                wake_up_process(dev->sd_cow_thread);

                // clean up the old device no matter what
                __tracer_destroy_snap(old_dev);
                kfree(old_dev);

                return ret;
        }

        // wake up new cow thread. Must happen regardless of errors syncing the
        // old cow thread in order to ensure no IO's are leaked.
        wake_up_process(dev->sd_cow_thread);

        // truncate the cow file
        ret = cow_truncate_to_index(dev->sd_cow);
        if (ret) {
                // not a critical error, we can just print a warning
                file_get_absolute_pathname(dev->sd_cow->filp, &abs_path,
                                           &abs_path_len);
                if (!abs_path) {
                        LOG_WARN("warning: cow file truncation failed, "
                                 "incremental will use more "
                                 "disk space than needed");
                } else {
                        LOG_WARN("warning: failed to truncate '%s', "
                                 "incremental will use more "
                                 "disk space than needed",
                                 abs_path);
                        kfree(abs_path);
                }
        }

        // destroy the unneeded fields of the old_dev and the old_dev itself
        __tracer_destroy_snap(old_dev);
        kfree(old_dev);

        return 0;

error:
        LOG_ERROR(ret, "error transitioning to incremental mode");
        __tracer_destroy_cow_thread(dev);
        kfree(dev);

        return ret;
}

/**
 * tracer_active_inc_to_snap() - Transitions from incremental mode to
 * snapshot mode.
 *
 * @old_dev: The &struct snap_device tracing in incremental mode.
 * @cow_path: The path to the COW backing file.
 * @fallocated_space: A value of zero carries over the current setting.
 *
 * Return:
 * * 0 - success
 * * !0 - error
 */
int tracer_active_inc_to_snap(struct snap_device *old_dev, const char *cow_path,
                              unsigned long fallocated_space)
{
        int ret;
        struct snap_device *dev;

        LOG_DEBUG("ENTER tracer_active_inc_to_snap");

        // allocate new tracer
        ret = tracer_alloc(&dev);
        if (ret)
                return ret;

        set_bit(SNAPSHOT, &dev->sd_state);
        set_bit(ACTIVE, &dev->sd_state);
        clear_bit(UNVERIFIED, &dev->sd_state);

        fallocated_space =
                (fallocated_space) ? fallocated_space : old_dev->sd_falloc_size;

        // copy / set fields we need
        __tracer_copy_base_dev(old_dev, dev);

        // setup the cow manager
        ret = __tracer_setup_cow_new(dev, dev->sd_base_dev, cow_path,
                                     dev->sd_size, fallocated_space,
                                     dev->sd_cache_size, old_dev->sd_cow->uuid,
                                     old_dev->sd_cow->seqid + 1);
        if (ret)
                goto error;

        // setup the cow path
        ret = __tracer_setup_cow_path(dev, dev->sd_cow->filp);
        if (ret)
                goto error;

        // setup the snapshot values
        ret = __tracer_setup_snap(dev, old_dev->sd_minor, dev->sd_base_dev,
                                  dev->sd_size);
        if (ret)
                goto error;

        // setup the cow thread
        ret = __tracer_setup_snap_cow_thread(dev, old_dev->sd_minor);
        if (ret)
                goto error;

        // start tracing (overwrites old_dev's tracing)
        dev->sd_orig_request_fn = old_dev->sd_orig_request_fn;
        ret = __tracer_setup_tracing(dev, old_dev->sd_minor);
        if (ret)
                goto error;

        // stop the old cow thread and start the new one
        __tracer_destroy_cow_thread(old_dev);
        wake_up_process(dev->sd_cow_thread);

        // destroy the unneeded fields of the old_dev and the old_dev itself
        __tracer_destroy_cow_path(old_dev);
        __tracer_destroy_cow_sync_and_free(old_dev);
        kfree(old_dev);

        return 0;

error:
        LOG_ERROR(ret, "error transitioning tracer to snapshot mode");
        __tracer_destroy_cow_thread(dev);
        __tracer_destroy_snap(dev);
        __tracer_destroy_cow_path(dev);
        __tracer_destroy_cow_free(dev);
        kfree(dev);

        return ret;
}

/**
 * tracer_reconfigure() - Reconfigures the cache size associated with @dev.
 *
 * @dev: The &struct snap_device object pointer.
 * @cache_size: Limits the size of the COW section cache (in bytes).
 */
void tracer_reconfigure(struct snap_device *dev, unsigned long cache_size)
{
        dev->sd_cache_size = cache_size;
        if (!cache_size)
                cache_size = dattobd_cow_max_memory_default;
        if (test_bit(ACTIVE, &dev->sd_state))
                cow_modify_cache_size(dev->sd_cow, cache_size);
}

/**
 * tracer_dattobd_info() - Copies relevant, current information in @dev to
 *                         @info.
 *
 * @dev: The source &struct snap_device tracking block device state.
 * @info: A destination &struct dattobd_info object pointer.
 */
void tracer_dattobd_info(const struct snap_device *dev,
                         struct dattobd_info *info)
{
        info->minor = dev->sd_minor;
        info->state = dev->sd_state;
        info->error = tracer_read_fail_state(dev);
        info->cache_size = (dev->sd_cache_size) ?
                                   dev->sd_cache_size :
                                   dattobd_cow_max_memory_default;
        strlcpy(info->cow, dev->sd_cow_path, PATH_MAX);
        strlcpy(info->bdev, dev->sd_bdev_path, PATH_MAX);

        if (!test_bit(UNVERIFIED, &dev->sd_state)) {
                info->falloc_size = dev->sd_cow->file_max;
                info->seqid = dev->sd_cow->seqid;
                memcpy(info->uuid, dev->sd_cow->uuid, COW_UUID_SIZE);
                info->version = dev->sd_cow->version;
                info->nr_changed_blocks = dev->sd_cow->nr_changed_blocks;
        } else {
                info->falloc_size = 0;
                info->seqid = 0;
                memset(info->uuid, 0, COW_UUID_SIZE);
        }
}

/************************AUTOMATIC TRANSITION FUNCTIONS************************/

/**
 * __tracer_active_to_dormant() - Transitions from ACTIVE to DORMANT.  This
 *                                happens, for example, when the underlying
 *                                block device becomes unwritable.
 *
 * @dev: The &struct snap_device object pointer.
 */
void __tracer_active_to_dormant(struct snap_device *dev)
{
        int ret;

        LOG_DEBUG("ENTER __tracer_active_to_dormant");
        // stop the cow thread
        __tracer_destroy_cow_thread(dev);

        // close the cow manager
        ret = __tracer_destroy_cow_sync_and_close(dev);
        if (ret)
                goto error;

        // mark as dormant
        smp_wmb();
        clear_bit(ACTIVE, &dev->sd_state);

        return;

error:
        LOG_ERROR(ret, "error transitioning tracer to dormant state");
        tracer_set_fail_state(dev, ret);
}

/**
 * __tracer_unverified_snap_to_active() - Initiates tracking from a device
 *                                        that was unverified and makes it
 *                                        active.
 *
 * @dev: The &struct snap_device object pointer.
 * @user_mount_path: A userspace supplied path used to build the COW file path.
 */
void __tracer_unverified_snap_to_active(struct snap_device *dev,
                                        const char __user *user_mount_path)
{
        int ret;
        unsigned int minor = dev->sd_minor;
        char *cow_path, *bdev_path = dev->sd_bdev_path,
                        *rel_path = dev->sd_cow_path;
        unsigned long cache_size = dev->sd_cache_size;

        LOG_DEBUG("ENTER __tracer_unverified_snap_to_active");
        // remove tracing while we setup the struct
        __tracer_destroy_tracing(dev);

        // mark as active
        set_bit(ACTIVE, &dev->sd_state);
        clear_bit(UNVERIFIED, &dev->sd_state);

        dev->sd_bdev_path = NULL;
        dev->sd_cow_path = NULL;

        // setup base device
        ret = __tracer_setup_base_dev(dev, bdev_path);
        if (ret)
                goto error;

        // generate the full pathname
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,5,0)
        ret = pathname_concat(user_mount_path, rel_path, &cow_path);        
#else
        ret = user_mount_pathname_concat(user_mount_path, rel_path, &cow_path);
#endif
        if (ret)
                goto error;

        // setup the cow manager
        ret = __tracer_setup_cow_reload_snap(dev, dev->sd_base_dev, cow_path,
                                             dev->sd_size, dev->sd_cache_size);
        if (ret)
                goto error;

        // setup the cow path
        ret = __tracer_setup_cow_path(dev, dev->sd_cow->filp);
        if (ret)
                goto error;

#ifndef USE_BDOPS_SUBMIT_BIO
        // retain an association between the original mrf and the block device
        ret = mrf_get(dev->sd_base_dev, GET_BIO_REQUEST_TRACKING_PTR(dev->sd_base_dev));
        if (ret)
                goto error;
#endif

        // setup the snapshot values
        ret = __tracer_setup_snap(dev, minor, dev->sd_base_dev, dev->sd_size);
        if (ret)
                goto error;

        // setup the cow thread and run it
        ret = __tracer_setup_snap_cow_thread(dev, minor);
        if (ret)
                goto error;

        wake_up_process(dev->sd_cow_thread);

        // inject the tracing function
        ret = __tracer_setup_tracing(dev, minor);
        if (ret)
                goto error;

        kfree(bdev_path);
        kfree(rel_path);
        kfree(cow_path);

        return;

error:
        LOG_ERROR(ret, "error transitioning snapshot tracer to active state");
        tracer_destroy(dev);
        tracer_setup_unverified_snap(dev, minor, bdev_path, rel_path,
                                     cache_size);
        tracer_set_fail_state(dev, ret);
        kfree(bdev_path);
        kfree(rel_path);
        if (cow_path)
                kfree(cow_path);
}

/**
 * __tracer_unverified_inc_to_active() - Moves from an unverified state to
 *                                       and active state.
 * @dev: The &struct snap_device object pointer.
 * @user_mount_path: A userspace supplied path used to build the COW file path.
 *
 * Tracing is configured for the block device after this call completes.
 */
void __tracer_unverified_inc_to_active(struct snap_device *dev,
                                       const char __user *user_mount_path)
{
        int ret;
        unsigned int minor = dev->sd_minor;
        char *cow_path, *bdev_path = dev->sd_bdev_path,
                        *rel_path = dev->sd_cow_path;
        unsigned long cache_size = dev->sd_cache_size;

        LOG_DEBUG("ENTER %s", __func__);

        // remove tracing while we setup the struct
        __tracer_destroy_tracing(dev);

        // mark as active
        set_bit(ACTIVE, &dev->sd_state);
        clear_bit(UNVERIFIED, &dev->sd_state);

        dev->sd_bdev_path = NULL;
        dev->sd_cow_path = NULL;

        // setup base device
        ret = __tracer_setup_base_dev(dev, bdev_path);
        if (ret)
                goto error;

        // generate the full pathname
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,5,0)
        ret = pathname_concat(user_mount_path, rel_path, &cow_path);        
#else
        ret = user_mount_pathname_concat(user_mount_path, rel_path, &cow_path);
#endif
        if (ret)
                goto error;

        // setup the cow manager
        ret = __tracer_setup_cow_reload_inc(dev, dev->sd_base_dev, cow_path,
                                            dev->sd_size, dev->sd_cache_size);
        if (ret)
                goto error;

        // setup the cow path
        ret = __tracer_setup_cow_path(dev, dev->sd_cow->filp);
        if (ret)
                goto error;

#ifndef USE_BDOPS_SUBMIT_BIO
        // retain an association between the original mrf and the block device
        ret = mrf_get(dev->sd_base_dev, GET_BIO_REQUEST_TRACKING_PTR(dev->sd_base_dev));
        if (ret)
                goto error;
#endif

        // setup the cow thread and run it
        ret = __tracer_setup_inc_cow_thread(dev, minor);
        if (ret)
                goto error;

        wake_up_process(dev->sd_cow_thread);

        // inject the tracing function
        ret = __tracer_setup_tracing(dev, minor);
        if (ret)
                goto error;

        kfree(bdev_path);
        kfree(rel_path);
        kfree(cow_path);

        return;

error:
        LOG_ERROR(ret, "error transitioning incremental to active state");
        tracer_destroy(dev);
        tracer_setup_unverified_inc(dev, minor, bdev_path, rel_path,
                                    cache_size);
        tracer_set_fail_state(dev, ret);
        kfree(bdev_path);
        kfree(rel_path);
        if (cow_path)
                kfree(cow_path);
}

/**
 * __tracer_dormant_to_active() - Starts tracing on a previously traced
 *                                device.
 * @dev: The &struct snap_device object pointer.
 * @user_mount_path: A userspace supplied path used to build the COW file path.
 *
 * Will continue tracing in the previous mode, i.e., snapshot or incremental.
 */
void __tracer_dormant_to_active(struct snap_device *dev,
                                const char __user *user_mount_path)
{
        int ret;
        char *cow_path;

        LOG_DEBUG("ENTER __tracer_dormant_to_active");

        // generate the full pathname

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,5,0)
        ret = pathname_concat(user_mount_path, dev->sd_cow_path, &cow_path);        
#else
        ret = user_mount_pathname_concat(user_mount_path, dev->sd_cow_path, &cow_path);
#endif
        if (ret)
                goto error;

        // setup the cow manager
        ret = __tracer_setup_cow_reopen(dev, dev->sd_base_dev, cow_path);
        if (ret)
                goto error;

        // restart the cow thread
        if (test_bit(SNAPSHOT, &dev->sd_state))
                ret = __tracer_setup_snap_cow_thread(dev, dev->sd_minor);
        else
                ret = __tracer_setup_inc_cow_thread(dev, dev->sd_minor);

        if (ret)
                goto error;

        wake_up_process(dev->sd_cow_thread);

        // set the state to active
        smp_wmb();
        set_bit(ACTIVE, &dev->sd_state);
        clear_bit(UNVERIFIED, &dev->sd_state);

        kfree(cow_path);

        return;

error:
        LOG_ERROR(ret, "error transitioning tracer to active state");
        if (cow_path)
                kfree(cow_path);
        tracer_set_fail_state(dev, ret);
}
