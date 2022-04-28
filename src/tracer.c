// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (C) 2022 Datto Inc.
 */

#include "tracer.h"
#include "blkdev.h"
#include "cow_manager.h"
#include "filesystem.h"
#include "hints.h"
#include "logging.h"
#include "module_control.h"
#include "module_threads.h"
#include "mrf.h"
#include "snap_device.h"
#include "snap_ops.h"
#include "task_helper.h"
#include "tracer_helper.h"

#ifdef HAVE_BLK_ALLOC_QUEUE
//#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,7,0)
#include <linux/blk-mq.h>
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
#endif

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

MRF_RETURN_TYPE snap_mrf(struct request_queue *q, struct bio *bio)
{
        struct snap_device *dev = q->queuedata;

        // if a write request somehow gets sent in, discard it
        if (bio_data_dir(bio)) {
                dattobd_bio_endio(bio, -EOPNOTSUPP);
                MRF_RETURN(0);
        } else if (tracer_read_fail_state(dev)) {
                dattobd_bio_endio(bio, -EIO);
                MRF_RETURN(0);
        } else if (!test_bit(ACTIVE, &dev->sd_state)) {
                dattobd_bio_endio(bio, -EBUSY);
                MRF_RETURN(0);
        }

        // queue bio for processing by kernel thread
        bio_queue_add(&dev->sd_cow_bios, bio);

        MRF_RETURN(0);
}

static int snap_trace_bio(struct snap_device *dev, struct bio *bio)
{
        int ret;
        struct bio *new_bio = NULL;
        struct tracing_params *tp = NULL;
        sector_t start_sect, end_sect;
        unsigned int bytes, pages;

        // if we don't need to cow this bio just call the real mrf normally
        if (!bio_needs_cow(bio, dev))
                return dattobd_call_mrf(dev->sd_orig_mrf,
                                        dattobd_bio_get_queue(bio), bio);

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
                goto error;

retry:
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

        // submit the bios
        dattobd_submit_bio(new_bio);

        // if our bio didn't cover the entire clone we must keep creating bios
        // until we have
        if (bytes / PAGE_SIZE < pages) {
                start_sect += bytes / SECTOR_SIZE;
                pages -= bytes / PAGE_SIZE;
                goto retry;
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

        // this function only returns non-zero if the real mrf does not. Errors
        // set the fail state.
        return 0;
}

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

static int inc_trace_bio(struct snap_device *dev, struct bio *bio)
{
        int ret = 0, is_initialized = 0;
        sector_t start_sect = 0, end_sect = bio_sector(bio);
        bio_iter_t iter;
        bio_iter_bvec_t bvec;

        if (!test_bit(SD_FLAG_COW_RESIDENT, &dev->sd_flags)) {
                // if the cow is non-resident, then we don't need to check if
                // the bio is for the cow file.
                ret = inc_make_sset(dev, bio_sector(bio),
                                    bio_size(bio) / SECTOR_SIZE);
                goto out;
        }

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
        ret = dattobd_call_mrf(dev->sd_orig_mrf, dattobd_bio_get_queue(bio),
                               bio);

        return ret;
}

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

static int file_is_on_bdev(const struct file *file, struct block_device *bdev)
{
        struct super_block *sb = dattobd_get_super(bdev);
        int ret = 0;
        if (sb) {
                ret = ((dattobd_get_mnt(file))->mnt_sb == sb);
                dattobd_drop_super(sb);
        }
        return ret;
}

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

static void minor_range_include(unsigned int minor)
{
        if (minor < lowest_minor)
                lowest_minor = minor;
        if (minor > highest_minor)
                highest_minor = minor;
}

static void __tracer_init(struct snap_device *dev)
{
        LOG_DEBUG("initializing tracer");
        atomic_set(&dev->sd_fail_code, 0);
        bio_queue_init(&dev->sd_cow_bios);
        bio_queue_init(&dev->sd_orig_bios);
        sset_queue_init(&dev->sd_pending_ssets);
}

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

static int __tracer_destroy_cow(struct snap_device *dev, int close_method)
{
        int ret = 0;

        dev->sd_cow_inode = NULL;
        dev->sd_falloc_size = 0;
        dev->sd_cache_size = 0;

        if (dev->sd_cow) {
                LOG_DEBUG("destroying cow manager");

                if (close_method == 0 || close_method == 1) {
                        ret = cow_sync_and_free(dev->sd_cow);
                        dev->sd_cow = NULL;
                } else if (close_method == 2) {
                        ret = cow_sync_and_close(dev->sd_cow);
                        task_work_flush();
                }
        }

        return ret;
}

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

        if (file_is_on_bdev(dev->sd_cow->filp, bdev)) {
                set_bit(SD_FLAG_COW_RESIDENT, &dev->sd_flags);
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

static int __tracer_setup_base_dev(struct snap_device *dev,
                                   const char *bdev_path)
{
        int ret;

        // open the base block device
        LOG_DEBUG("finding block device");
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
        if (dev->sd_base_dev->bd_contains != dev->sd_base_dev) {
                dev->sd_sect_off = dev->sd_base_dev->bd_part->start_sect;
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
static int snap_merge_bvec(struct request_queue *q, struct bvec_merge_data *bvm,
                           struct bio_vec *bvec)
{
        struct snap_device *dev = q->queuedata;
        struct request_queue *base_queue = bdev_get_queue(dev->sd_base_dev);

        bvm->bi_bdev = dev->sd_base_dev;

        return base_queue->merge_bvec_fn(base_queue, bvm, bvec);
}
#else
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

static void __tracer_copy_cow(const struct snap_device *src,
                              struct snap_device *dest)
{
        dest->sd_cow = src->sd_cow;
        dest->sd_cow_inode = src->sd_cow_inode;
        dest->sd_cache_size = src->sd_cache_size;
        dest->sd_falloc_size = src->sd_falloc_size;
}

static void __tracer_destroy_cow_path(struct snap_device *dev)
{
        if (dev->sd_cow_path) {
                LOG_DEBUG("freeing cow path");
                kfree(dev->sd_cow_path);
                dev->sd_cow_path = NULL;
        }

        if (dev->sd_cow_full_path) {
                LOG_DEBUG("freeing full cow path");
                kfree(dev->sd_cow_full_path);
                dev->sd_cow_full_path = NULL;
        }
}

static int __tracer_setup_cow_path(struct snap_device *dev,
                                   const struct file *cow_file)
{
        int ret;

        // get the pathname of the cow file (relative to the mountpoint)
        LOG_DEBUG("getting absolute pathname of cow file");
        ret = file_get_absolute_pathname(cow_file, &dev->sd_cow_full_path, NULL);
        if (ret)
                goto error;

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

static void __tracer_copy_cow_path(const struct snap_device *src,
                                   struct snap_device *dest)
{
        dest->sd_cow_path = src->sd_cow_path;
}

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

static void __tracer_destroy_snap(struct snap_device *dev)
{
        if (dev->sd_mrf_thread) {
                LOG_DEBUG("stopping mrf thread");
                kthread_stop(dev->sd_mrf_thread);
                dev->sd_mrf_thread = NULL;
        }

        if (dev->sd_gd) {
                LOG_DEBUG("freeing gendisk");
                if (dev->sd_gd->flags & GENHD_FL_UP)
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

static int __tracer_setup_snap(struct snap_device *dev, unsigned int minor,
                               struct block_device *bdev, sector_t size)
{
        int ret;

        ret = __tracer_bioset_init(dev);
        if (ret) {
                LOG_ERROR(ret, "error initializing bio set");
                goto error;
        }

        // allocate request queue
        LOG_DEBUG("allocating queue");
#ifndef HAVE_BLK_ALLOC_QUEUE
        //#if LINUX_VERSION_CODE < KERNEL_VERSION(5,7,0)
        dev->sd_queue = blk_alloc_queue(GFP_KERNEL);
#else
#ifdef HAVE_BLK_ALLOC_QUEUE_RH_2 // el8
        dev->sd_queue = blk_alloc_queue_rh(snap_mrf, NUMA_NO_NODE);
#else
        dev->sd_queue = blk_alloc_queue(snap_mrf, NUMA_NO_NODE);
#endif
#endif
        if (!dev->sd_queue) {
                ret = -ENOMEM;
                LOG_ERROR(ret, "error allocating request queue");
                goto error;
        }

#ifndef HAVE_BLK_ALLOC_QUEUE
        //#if LINUX_VERSION_CODE < KERNEL_VERSION(5,7,0)
        // register request handler
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

        // allocate a gendisk struct
        LOG_DEBUG("allocating gendisk");
        dev->sd_gd = alloc_disk(1);
        if (!dev->sd_gd) {
                ret = -ENOMEM;
                LOG_ERROR(ret, "error allocating gendisk");
                goto error;
        }

        // initialize gendisk and request queue values
        LOG_DEBUG("initializing gendisk");
        dev->sd_queue->queuedata = dev;
        dev->sd_gd->private_data = dev;
        dev->sd_gd->major = major;
        dev->sd_gd->first_minor = minor;
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

static void __tracer_destroy_cow_thread(struct snap_device *dev)
{
        if (dev->sd_cow_thread) {
                LOG_DEBUG("stopping cow thread");
                kthread_stop(dev->sd_cow_thread);
                dev->sd_cow_thread = NULL;
        }
}

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

static int __tracer_transition_tracing(struct snap_device *dev,
                                       struct block_device *bdev,
                                       make_request_fn *new_mrf,
                                       struct snap_device **dev_ptr)
{
        int ret;
        struct super_block *origsb = dattobd_get_super(bdev);
        struct super_block *sb = NULL;
        char bdev_name[BDEVNAME_SIZE];
        MAYBE_UNUSED(ret);

        bdevname(bdev, bdev_name);

        if (origsb) {
                // freeze and sync block device
                LOG_DEBUG("freezing '%s'", bdev_name);
                sb = freeze_bdev(bdev);
                if (!sb) {
                        LOG_ERROR(-EFAULT, "error freezing '%s': null",
                                  bdev_name);
                        dattobd_drop_super(origsb);
                        return -EFAULT;
                } else if (IS_ERR(sb)) {
                        LOG_ERROR((int)PTR_ERR(sb),
                                  "error freezing '%s': error", bdev_name);
                        dattobd_drop_super(origsb);
                        return (int)PTR_ERR(sb);
                }
        }

        smp_wmb();
        if (dev) {
                LOG_DEBUG("starting tracing");
                *dev_ptr = dev;
                smp_wmb();
                if (new_mrf)
                        bdev->bd_disk->queue->make_request_fn = new_mrf;
        } else {
                LOG_DEBUG("ending tracing");
#ifndef HAVE_BLK_ALLOC_QUEUE
                //#if LINUX_VERSION_CODE < KERNEL_VERSION(5,7,0)
                if (new_mrf)
                        bdev->bd_disk->queue->make_request_fn = new_mrf;
#else
                if (new_mrf)
                        bdev->bd_disk->queue->make_request_fn =
                                new_mrf == dattobd_null_mrf ? NULL : new_mrf;
#endif
                smp_wmb();
                *dev_ptr = dev;
        }
        smp_wmb();

        if (origsb) {
                // thaw the block device
                LOG_DEBUG("thawing '%s'", bdev_name);
#ifndef HAVE_THAW_BDEV_INT
                //#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,29)
                thaw_bdev(bdev, sb);
#else
                ret = thaw_bdev(bdev, sb);
                if (ret) {
                        LOG_ERROR(ret, "error thawing '%s'", bdev_name);
                        // we can't reasonably undo what we've done at this
                        // point, and we've replaced the mrf. pretend we
                        // succeeded so we don't break the block device
                }
#endif
                dattobd_drop_super(origsb);
        }

        return 0;
}

static MRF_RETURN_TYPE tracing_mrf(struct request_queue *q, struct bio *bio)
{
        int i, ret = 0;
        struct snap_device *dev;
        make_request_fn *orig_mrf = NULL;

        MAYBE_UNUSED(ret);

        smp_rmb();
        tracer_for_each(dev, i)
        {
                if (!dev || test_bit(UNVERIFIED, &dev->sd_state) ||
                    !tracer_queue_matches_bio(dev, bio))
                        continue;

                orig_mrf = dev->sd_orig_mrf;
                if (dattobd_bio_op_flagged(bio, DATTOBD_PASSTHROUGH)) {
                        dattobd_bio_op_clear_flag(bio, DATTOBD_PASSTHROUGH);
                        goto call_orig;
                }

                if (tracer_should_trace_bio(dev, bio)) {
                        if (test_bit(SNAPSHOT, &dev->sd_state))
                                ret = snap_trace_bio(dev, bio);
                        else
                                ret = inc_trace_bio(dev, bio);
                        goto out;
                }
        }

call_orig:
        if (orig_mrf)
                ret = dattobd_call_mrf(orig_mrf, q, bio);
        else
                LOG_ERROR(-EFAULT, "error finding original_mrf");

out:
        MRF_RETURN(ret);
}

static int __tracer_should_reset_mrf(const struct snap_device *dev)
{
        struct snap_device *cur_dev;
        struct request_queue *q = bdev_get_queue(dev->sd_base_dev);

        // since kernel 5.8 make_request_fn can be null.
        if (q->make_request_fn == NULL) {
                LOG_ERROR(-EINVAL, "make_request_fn is null");
                return -EINVAL;
        }

        if (q->make_request_fn != tracing_mrf)
                return 0;
        if (snap_devices) {
                int i;
                if (dev != snap_devices[dev->sd_minor])
                        return 0;

                // return 0 if there is another device tracing the same queue as
                // dev.
                tracer_for_each(cur_dev, i)
                {
                        if (!cur_dev ||
                            test_bit(UNVERIFIED, &cur_dev->sd_state) ||
                            cur_dev == dev)
                                continue;
                        if (q == bdev_get_queue(cur_dev->sd_base_dev))
                                return 0;
                }
        }

        return 1;
}

static void __tracer_destroy_tracing(struct snap_device *dev)
{
        if (dev->sd_orig_mrf) {
                LOG_DEBUG("replacing make_request_fn if needed");
                if (__tracer_should_reset_mrf(dev))
                        __tracer_transition_tracing(
                                NULL, dev->sd_base_dev, dev->sd_orig_mrf,
                                &snap_devices[dev->sd_minor]);
                else
                        __tracer_transition_tracing(
                                NULL, dev->sd_base_dev, NULL,
                                &snap_devices[dev->sd_minor]);

                dev->sd_orig_mrf = NULL;
        } else if (snap_devices[dev->sd_minor] == dev) {
                smp_wmb();
                snap_devices[dev->sd_minor] = NULL;
                smp_wmb();
        }

        dev->sd_minor = 0;
        minor_range_recalculate();
}

static void __tracer_setup_tracing_unverified(struct snap_device *dev,
                                              unsigned int minor)
{
        dev->sd_orig_mrf = NULL;
        minor_range_include(minor);
        smp_wmb();
        dev->sd_minor = minor;
        snap_devices[minor] = dev;
        smp_wmb();
}

static int find_orig_mrf(struct block_device *bdev, make_request_fn **mrf)
{
        int i;
        struct snap_device *dev;
        struct request_queue *q = bdev_get_queue(bdev);

        // since kernel 5.8 make_request_fn can be null.
        if (q->make_request_fn == NULL) {
#ifndef HAVE_BLK_ALLOC_QUEUE
                //#if LINUX_VERSION_CODE < KERNEL_VERSION(5,7,0)
                LOG_ERROR(-EINVAL, "make_request_fn is null");
                return -EINVAL;
#else
                *mrf = dattobd_null_mrf;
                return 0;
#endif
        }

        if (q->make_request_fn != tracing_mrf) {
                *mrf = q->make_request_fn;
                return 0;
        }

        tracer_for_each(dev, i)
        {
                if (!dev || test_bit(UNVERIFIED, &dev->sd_state))
                        continue;
                if (q == bdev_get_queue(dev->sd_base_dev)) {
                        *mrf = dev->sd_orig_mrf;
                        return 0;
                }
        }

        *mrf = NULL;
        return -EFAULT;
}

int __tracer_setup_tracing(struct snap_device *dev, unsigned int minor)
{
        int ret;

        dev->sd_minor = minor;
        minor_range_include(minor);

        // get the base block device's make_request_fn
        LOG_DEBUG("getting the base block device's make_request_fn");
        ret = find_orig_mrf(dev->sd_base_dev, &dev->sd_orig_mrf);
        if (ret)
                goto error;

        ret = __tracer_transition_tracing(dev, dev->sd_base_dev, tracing_mrf,
                                          &snap_devices[minor]);
        if (ret)
                goto error;

        return 0;

error:
        LOG_ERROR(ret, "error setting up tracing");
        dev->sd_minor = 0;
        dev->sd_orig_mrf = NULL;
        minor_range_recalculate();
        return ret;
}

int __tracer_setup_unverified(struct snap_device *dev, unsigned int minor,
                              const char *bdev_path, const char *cow_path,
                              unsigned long cache_size, int is_snap)
{
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

void tracer_destroy(struct snap_device *dev)
{
        __tracer_destroy_tracing(dev);
        __tracer_destroy_cow_thread(dev);
        __tracer_destroy_snap(dev);
        __tracer_destroy_cow_path(dev);
        __tracer_destroy_cow_free(dev);
        __tracer_destroy_base_dev(dev);
}

int tracer_setup_active_snap(struct snap_device *dev, unsigned int minor,
                             const char *bdev_path, const char *cow_path,
                             unsigned long fallocated_space,
                             unsigned long cache_size)
{
        int ret;

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

int tracer_active_inc_to_snap(struct snap_device *old_dev, const char *cow_path,
                              unsigned long fallocated_space)
{
        int ret;
        struct snap_device *dev;

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

void tracer_reconfigure(struct snap_device *dev, unsigned long cache_size)
{
        dev->sd_cache_size = cache_size;
        if (!cache_size)
                cache_size = dattobd_cow_max_memory_default;
        if (test_bit(ACTIVE, &dev->sd_state))
                cow_modify_cache_size(dev->sd_cow, cache_size);
}

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

void __tracer_active_to_dormant(struct snap_device *dev)
{
        int ret;

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

void __tracer_unverified_snap_to_active(struct snap_device *dev,
                                        const char __user *user_mount_path)
{
        int ret;
        unsigned int minor = dev->sd_minor;
        char *cow_path, *bdev_path = dev->sd_bdev_path,
                        *rel_path = dev->sd_cow_path;
        unsigned long cache_size = dev->sd_cache_size;

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
        ret = user_mount_pathname_concat(user_mount_path, rel_path, &cow_path);
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

void __tracer_unverified_inc_to_active(struct snap_device *dev,
                                       const char __user *user_mount_path)
{
        int ret;
        unsigned int minor = dev->sd_minor;
        char *cow_path, *bdev_path = dev->sd_bdev_path,
                        *rel_path = dev->sd_cow_path;
        unsigned long cache_size = dev->sd_cache_size;

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
        ret = user_mount_pathname_concat(user_mount_path, rel_path, &cow_path);
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

void __tracer_dormant_to_active(struct snap_device *dev,
                                const char __user *user_mount_path)
{
        int ret;
        char *resident_cow_path;
        char *cow_path;

        // generate the full pathname
        ret = user_mount_pathname_concat(user_mount_path, dev->sd_cow_path,
                                         &resident_cow_path);
        if (ret)
                goto error;

        cow_path = test_bit(SD_FLAG_COW_RESIDENT, &dev->sd_flags) ? resident_cow_path : dev->sd_cow_full_path;

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

        kfree(resident_cow_path);

        return;

error:
        LOG_ERROR(ret, "error transitioning tracer to active state");
        if (resident_cow_path)
                kfree(resident_cow_path);
        tracer_set_fail_state(dev, ret);
}
