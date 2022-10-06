// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (C) 2022 Datto Inc.
 */

#include "includes.h"

#include "bio_helper.h"
#include "logging.h"
#include "snap_device.h"
#include "tracer_helper.h"
#include "tracing_params.h"
#include <linux/bio.h>

/**
 * dattobd_bio_get_queue() - Gets the request queue for the given block
 * I/O operation.
 *
 * @bio: The &struct bio which describes the I/O
 *
 * Return:
 * The @request_queue containing the @bio.
 */
struct request_queue *dattobd_bio_get_queue(struct bio *bio)
{
#ifdef HAVE_BIO_BI_BDEV
        //#if LINUX_VERSION_CODE < KERNEL_VERSION(4,14,0)
        return bdev_get_queue(bio->bi_bdev);
#else
        return bio->bi_disk->queue;
#endif
}

/**
 * dattobd_bio_set_dev() - Sets the block device associated with the
 * block I/O operation.
 *
 * @bio: The &struct bio which describes the I/O
 * @bdev: The associated block device.
 */
void dattobd_bio_set_dev(struct bio *bio, struct block_device *bdev)
{
#ifdef HAVE_BIO_BI_BDEV
        //#if LINUX_VERSION_CODE < KERNEL_VERSION(4,14,0)
        bio->bi_bdev = bdev;
#else
        bio_set_dev(bio, bdev);
#endif
}

/**
 * dattobd_bio_copy_dev() - copies the block I/O operation from @src to @dst
 * @src: the source bio
 * @dst: the destination bio
 */
void dattobd_bio_copy_dev(struct bio *dst, struct bio *src)
{
#ifdef HAVE_BIO_BI_BDEV
        //#if LINUX_VERSION_CODE < KERNEL_VERSION(4,14,0)
        dst->bi_bdev = src->bi_bdev;
#else
        bio_copy_dev(dst, src);
#endif
}

#ifndef REQ_WRITE
#define REQ_WRITE WRITE
#endif

#ifndef REQ_FLUSH
#define REQ_FLUSH (1 << BIO_RW_BARRIER)
#endif

// if these don't exist they are not supported
#ifndef REQ_SECURE
#define REQ_SECURE 0
#endif

#ifndef REQ_WRITE_SAME
#define REQ_WRITE_SAME 0
#endif

#ifndef HAVE_SUBMIT_BIO_1
//#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,8,0)

#ifndef HAVE_ENUM_REQ_OP
/**
 * dattobd_set_bio_ops() - Sets the I/O operation and additional flags in the
 * @bio.
 *
 * @bio: The &struct bio which describes the I/O
 * @op: The operation to be performed.
 * @op_flags: Additional flags.
 */
void dattobd_set_bio_ops(struct bio *bio, req_op_t op, unsigned op_flags)
{
        bio->bi_rw = 0;

        switch (op) {
        case REQ_OP_READ:
                break;
        case REQ_OP_WRITE:
                bio->bi_rw |= REQ_WRITE;
                break;
        case REQ_OP_DISCARD:
                bio->bi_rw |= REQ_DISCARD;
                break;
        case REQ_OP_SECURE_ERASE:
                bio->bi_rw |= REQ_DISCARD | REQ_SECURE;
                break;
        case REQ_OP_WRITE_SAME:
                bio->bi_rw |= REQ_WRITE_SAME;
                break;
        case REQ_OP_FLUSH:
                bio->bi_rw |= REQ_FLUSH;
                break;
        }

        bio->bi_rw |= op_flags;
}
#endif

/**
 * dattobd_bio_op_flagged() - Checks the bio for a given flag.
 *
 * @bio: The &struct bio which describes the I/O
 * @flag: The operation flag to test.
 *
 * Return:
 * * 0 - not set
 * * !0 - set
 */
int dattobd_bio_op_flagged(struct bio *bio, unsigned int flag)
{
        return bio->bi_rw & flag;
}

/**
 * dattobd_bio_op_set_flag() - Sets the given flag in the bio I/O
 * operation flags field.
 *
 * @bio: The &struct bio which describes the I/O
 * @flag: The operation flag to set.
 */
void dattobd_bio_op_set_flag(struct bio *bio, unsigned int flag)
{
        bio->bi_rw |= flag;
}

/**
 * dattobd_bio_op_clear_flag() - Clears the given flag in the bio I/O
 * operation flags field.
 *
 * @bio: The &struct bio which describes the I/O
 * @flag: The operation flag to clear.
 */
void dattobd_bio_op_clear_flag(struct bio *bio, unsigned int flag)
{
        bio->bi_rw &= ~flag;
}
#else

#ifndef HAVE_ENUM_REQ_OPF
//#if LINUX_VERSION_CODE < KERNEL_VERSION(4,10,0)
typedef enum req_op req_op_t;
#else
typedef enum req_opf req_op_t;
#endif

/**
 * dattobd_set_bio_ops() - Sets the op and its flags.
 *
 * @bio: The &struct bio which describes the I/O
 * @op: The operation to be performed.
 * @op_flags: Additional flags.
 */
void dattobd_set_bio_ops(struct bio *bio, req_op_t op, unsigned op_flags)
{
        bio->bi_opf = 0;
        bio_set_op_attrs(bio, op, op_flags);
}

/**
 * dattobd_bio_op_flagged() - Checks the bio for a given flag.
 *
 * @bio: The &struct bio which describes the I/O
 * @flag: The operation flag to test.
 *
 * Return:
 * * 0 - not set
 * * !0 - set
 */
int dattobd_bio_op_flagged(struct bio *bio, unsigned int flag)
{
        return bio->bi_opf & flag;
}

/**
 * dattobd_bio_op_set_flag() - Sets the given flag in the bio I/O
 * operation flags field.
 *
 * @bio: The &struct bio which describes the I/O
 * @flag: The operation flag to set.
 */
void dattobd_bio_op_set_flag(struct bio *bio, unsigned int flag)
{
        bio->bi_opf |= flag;
}

/**
 * dattobd_bio_op_clear_flag() - Clears the given flag in the bio I/O
 * operation flags field.
 *
 * @bio: The &struct bio which describes the I/O
 * @flag: The operation flag to clear.
 */
void dattobd_bio_op_clear_flag(struct bio *bio, unsigned int flag)
{
        bio->bi_opf &= ~flag;
}

#endif

#if !defined HAVE_SUBMIT_BIO_WAIT && !defined HAVE_SUBMIT_BIO_1
//#if LINUX_VERSION_CODE < KERNEL_VERSION(3,10,0)
struct submit_bio_ret {
        struct completion event;
        int error;
};

/**
 * __submit_bio_wait_endio() - Common endio completion routine used across
 * various versions of OS.
 * see http://canali.hopto.org/linux/source/kernel/sched/completion.c?v=4.14.14
 *
 * @bio: The &struct bio which describes the I/O
 * @error: an errno
 */
static void __submit_bio_wait_endio(struct bio *bio, int error)
{
        struct submit_bio_ret *ret = bio->bi_private;
        ret->error = error;
        complete(&ret->event);
}

#ifdef HAVE_BIO_ENDIO_INT

/**
 * submit_bio_wait_endio() - Intended to be used as the end I/O routine for a
 * @struct bio
 *
 * @bio: The &struct bio which describes the I/O
 * @bytes: unused
 * @error: an errno
 *
 * Return:
 * * 0 - I/O has ended on this whole bio.
 * * 1 - The &struct bio has bytes remaining
 */
static int submit_bio_wait_endio(struct bio *bio, unsigned int bytes, int error)
{
        if (bio->bi_size)
                return 1;

        __submit_bio_wait_endio(bio, error);
        return 0;
}

#else

/**
 * submit_bio_wait_endio() - Intended to be used as the end I/O routine for a
 * @struct bio
 *
 * @bio: The &struct bio which describes the I/O
 * @error: an errno
 */
static void submit_bio_wait_endio(struct bio *bio, int error)
{
        __submit_bio_wait_endio(bio, error);
}

#endif

/**
 * submit_bio_wait() - submit a bio and wait until it completes
 *
 * @rw: flags, i.e., whether to READ or WRITE, or maybe to READA (read ahead).
 * @bio: The &struct bio which describes the I/O.
 *
 * Return:
 * * 0 - success.
 * * !0 - errno indicating the error.
 */
int submit_bio_wait(int rw, struct bio *bio)
{
        struct submit_bio_ret ret;

        // kernel implementation has the line below, but all our calls will have
        // this already and it changes across kernel versions rw |= REQ_SYNC;

        init_completion(&ret.event);
        bio->bi_private = &ret;
        bio->bi_end_io = submit_bio_wait_endio;
        submit_bio(rw, bio);
        wait_for_completion(&ret.event);

        return ret.error;
}

#endif

#ifdef HAVE_BIO_ENDIO_INT

/**
 * dattobd_bio_endio() - end I/O on a bio
 *
 * @bio: The &struct bio which describes the I/O
 * @err: an errno
 */
void dattobd_bio_endio(struct bio *bio, int err)
{
        bio_endio(bio, bio->bi_size, err);
}

#elif !defined HAVE_BIO_ENDIO_1

/**
 * dattobd_bio_endio - end I/O on a bio
 *
 * @bio: The &struct bio which describes the I/O
 * @err: an errno
 */
void dattobd_bio_endio(struct bio *bio, int err)
{
        bio_endio(bio, err);
}

#elif defined HAVE_BLK_STATUS_T

/**
 * dattobd_bio_endio - end I/O on a bio
 *
 * @bio: The &struct bio which describes the I/O
 * @err: an errno
 */
void dattobd_bio_endio(struct bio *bio, int err)
{
        bio->bi_status = errno_to_blk_status(err);
        bio_endio(bio);
}

#else

/**
 * dattobd_bio_endio - end I/O on a bio
 *
 * @bio: The &struct bio which describes the I/O
 * @err: an errno
 */
void dattobd_bio_endio(struct bio *bio, int err)
{
        bio->bi_error = err;
        bio_endio(bio);
}

#endif

/**
 * __on_bio_read_complete() - A private shared implemention of the completion
 *                            procedure executed whenever the I/O operation on
 *                            the &struct bio is complete.
 * @bio: The &struct bio which describes the I/O
 * @err: an errno
 *
 * This completion routine is used when a write operation BIO is received and
 * a COW operation is required.  The write BIO is first cloned as a read BIO
 * and this is used by the cloned BIO as a completion routine.  This BIO is
 * handed off to the COW thread for further processing.
 */
static void __on_bio_read_complete(struct bio *bio, int err)
{
        int ret;
        struct tracing_params *tp = bio->bi_private;
        struct snap_device *dev = tp->dev;
        struct bio_sector_map *map = NULL;
#ifndef HAVE_BVEC_ITER
        unsigned short i = 0;
#endif

        // check for read errors
        if (err) {
                ret = err;
                LOG_ERROR(ret,
                          "error reading from base device for copy on write");
                goto error;
        }

        // change the bio into a write bio
        dattobd_set_bio_ops(bio, REQ_OP_WRITE, 0);

        // reset the bio iterator to its original state
        for (map = tp->bio_sects.head; map != NULL && map->bio != NULL;
             map = map->next) {
                if (bio == map->bio) {
                        bio_sector(bio) = map->sect - dev->sd_sect_off;
                        bio_size(bio) = map->size;
                        bio_idx(bio) = 0;
                        break;
                }
        }

        /*
         * Reset the position in each bvec. Unnecessary with bvec iterators.
         * Will cause multipage bvec capable kernels to lock up.
         */
#ifndef HAVE_BVEC_ITER
        //#if LINUX_VERSION_CODE < KERNEL_VERSION(3,14,0)
        for (i = 0; i < bio->bi_vcnt; i++) {
                bio->bi_io_vec[i].bv_len = PAGE_SIZE;
                bio->bi_io_vec[i].bv_offset = 0;
        }
#endif

        /*
         * drop our reference to the tp (will queue the orig_bio if nobody else
         * is using it) at this point we set bi_private to the snap_device and
         * change the destructor to use that instead. This only matters on older
         * kernels
         */
        bio->bi_private = dev;
#ifndef HAVE_BIO_BI_POOL
        bio->bi_destructor = bio_destructor_snap_dev;
#endif

        // queue cow bio for processing by kernel thread
        bio_queue_add(&dev->sd_cow_bios, bio);
        atomic64_inc(&dev->sd_received_cnt);
        smp_wmb();

        tp_put(tp);

        return;

error:
        LOG_ERROR(ret, "error during bio read complete callback");
        tracer_set_fail_state(dev, ret);
        tp_put(tp);
        bio_free_clone(bio);
}

#ifdef HAVE_BIO_ENDIO_INT

/**
 * on_bio_read_complete() - The completion procedure executed whenever
 * the I/O operation on the &struct bio is complete.  It's meant to be
 * assigned to the bi_end_io field of a &struct bio.
 *
 * @bio: The &struct bio which describes the I/O
 * @bytes: unused
 * @err: an errno
 *
 * Return:
 * * 0 - I/O has ended on this whole bio.
 * * 1 - The &struct bio has bytes remaining
 */
static int on_bio_read_complete(struct bio *bio, unsigned int bytes, int err)
{
        if (bio->bi_size)
                return 1;
        __on_bio_read_complete(bio, err);
        return 0;
}

#elif !defined HAVE_BIO_ENDIO_1

/**
 * on_bio_read_complete() - The completion procedure executed whenever
 * the I/O operation on the &struct bio is complete.  It's meant to be
 * assigned to the bi_end_io field of a &struct bio.
 *
 * @bio: The &struct bio which describes the I/O
 * @err: an errno
 *
 * Return:
 * * 0 - I/O has ended on this whole bio.
 * * 1 - The &struct bio has bytes remaining
 */
static void on_bio_read_complete(struct bio *bio, int err)
{
        if (!test_bit(BIO_UPTODATE, &bio->bi_flags))
                err = -EIO;
        __on_bio_read_complete(bio, err);
}

#elif defined HAVE_BLK_STATUS_T

/**
 * on_bio_read_complete() - The completion procedure executed whenever
 * the I/O operation on the &struct bio is complete.  It's meant to be
 * assigned to the bi_end_io field of a &struct bio.
 *
 * @bio: The &struct bio which describes the I/O
 *
 * Return:
 * * 0: I/O has ended on this whole bio.
 * * 1: The &struct bio has bytes remaining
 */
static void on_bio_read_complete(struct bio *bio)
{
        __on_bio_read_complete(bio, blk_status_to_errno(bio->bi_status));
}

#else

/**
 * on_bio_read_complete() - The completion procedure executed whenever
 * the I/O operation on the &struct bio is complete.  It's meant to be
 * assigned to the bi_end_io field of a &struct bio.
 *
 * @bio: The &struct bio which describes the I/O
 *
 * Return:
 * * 0 - I/O has ended on this whole bio.
 * * 1 - The &struct bio has bytes remaining
 */
static void on_bio_read_complete(struct bio *bio)
{
        __on_bio_read_complete(bio, bio->bi_error);
}
#endif

/**
 * page_get_inode() - Get the inode hosting the page, if there is one
 *
 * @pg: the &struct page
 *
 * Return:
 * * The &struct inode if there is one
 * * NULL otherwise
 */
struct inode *page_get_inode(struct page *pg)
{
        if (!pg)
                return NULL;

                // page_mapping() was not exported until 4.8, use
                // compound_head() instead
#ifdef HAVE_COMPOUND_HEAD
        //#if LINUX_VERSION_CODE >= KERNEL_VERSION(2.6.22)
        pg = compound_head(pg);
#endif
        if (PageAnon(pg))
                return NULL;
        if (!pg->mapping)
                return NULL;
        return pg->mapping->host;
}

/**
 * bio_needs_cow() - Test to see if the &struct bio contains a write request
 * or if the bio inodes don't match our cow file.
 *
 * @bio: The &struct bio which describes the I/O.
 * @inode: The inode of the directory containing the cow file.
 *
 * Return:
 * * 0 - does not need to be copied.
 * * !0 - does need to be copied.
 */
int bio_needs_cow(struct bio *bio, struct inode *inode)
{
        bio_iter_t iter;
        bio_iter_bvec_t bvec;

#ifdef HAVE_ENUM_REQ_OPF
        //#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,10,0)
        if (bio_op(bio) == REQ_OP_WRITE_ZEROES)
                return 1;
#endif

        // check the inode of each page return true if it does not match our cow
        // file
        bio_for_each_segment (bvec, bio, iter) {
                if (page_get_inode(bio_iter_page(bio, iter)) != inode)
                        return 1;
        }

        return 0;
}

#ifndef HAVE_BIO_BI_POOL
/**
 * bio_destructor_tp - A destructor method invoked when a bio is being freed.
 *
 * @bio: The &struct bio which describes the I/O
 */
static void bio_destructor_tp(struct bio *bio)
{
        struct tracing_params *tp = bio->bi_private;
        bio_free(bio, dev_bioset(tp->dev));
}

/**
 * bio_destructor_snap_dev() - Used as completion routine to free the
 * &struct bio and return it to the bioset contained in the
 * &struct snap_device
 *
 * @bio: The &struct bio which describes the I/O
 */
static void bio_destructor_snap_dev(struct bio *bio)
{
        struct snap_device *dev = bio->bi_private;
        bio_free(bio, dev_bioset(dev));
}
#endif

/**
 * bio_free_clone() - Cleans up a bio allocated with bio_make_read_clone().
 *
 * @bio: The &struct bio which describes the I/O
 *
 * This is used indirectly by the endio completion routine set for the
 * cloned &struct bio.
 */
void bio_free_clone(struct bio *bio)
{
        int i;

        for (i = 0; i < bio->bi_vcnt; i++) {
                if (bio->bi_io_vec[i].bv_page)
                        __free_page(bio->bi_io_vec[i].bv_page);
        }
        bio_put(bio);
}

/**
 * bio_make_read_clone() - Creates a new &struct bio to read all data
 * contained in an existing bio.
 *
 * @bs: the allocation pool used to allocate the new &struct bio
 * @tp: The &struct tracing_params carried along with the output @struct bio
 * @orig_bio: The @struct bio to be constructed to read all data from the
 * original bio
 * @sect: The starting sector within the input &struct bio
 * @pages: The number of pages contained in the input &struct bio
 * @bio_out: The bio created for READing the pages contained in the input
 * @orig_bio
 * @bytes_added: The number of bytes contained in @bio_out
 *
 * It is possible that all of the data contained in the original bio
 * will not be contained in the new which requires that additional calls
 * be made to completely read the original bio.
 *
 * Return:
 * * 0 - success
 * * !0 - failure
 */
int bio_make_read_clone(struct bio_set *bs, struct tracing_params *tp,
                        struct bio *orig_bio, sector_t sect, unsigned int pages,
                        struct bio **bio_out, unsigned int *bytes_added)
{
        int ret;
        struct bio *new_bio;
        struct page *pg;
        unsigned int i;
        unsigned int bytes;
        unsigned int total = 0;
#ifdef BIO_MAX_PAGES
        unsigned int actual_pages =
                (pages > BIO_MAX_PAGES) ? BIO_MAX_PAGES : pages;
#else
        unsigned int actual_pages =
                (pages > BIO_MAX_VECS) ? BIO_MAX_VECS : pages;
#endif

        // allocate bio clone, instruct the allocator to not make I/O requests
        // while trying to allocate memory to prevent any possible lock
        // contention.
        new_bio = bio_alloc_bioset(GFP_NOIO, actual_pages, bs);
        if (!new_bio) {
                ret = -ENOMEM;
                LOG_ERROR(ret,
                          "error allocating bio clone - bs = %p, pages = %u",
                          bs, pages);
                goto error;
        }

#ifndef HAVE_BIO_BI_POOL
        new_bio->bi_destructor = bio_destructor_tp;
#endif

        // populate read bio
        new_bio->bi_private = tp;
        new_bio->bi_end_io = on_bio_read_complete;
        dattobd_bio_copy_dev(new_bio, orig_bio);
        dattobd_set_bio_ops(new_bio, REQ_OP_READ, 0);
        bio_sector(new_bio) = sect;
        bio_idx(new_bio) = 0;

        // fill the bio with pages
        for (i = 0; i < actual_pages; i++) {
                // allocate a page and add it to our bio
                pg = alloc_page(GFP_NOIO);
                if (!pg) {
                        ret = -ENOMEM;
                        LOG_ERROR(ret, "error allocating read bio page %u", i);
                        goto error;
                }

                // add the page to the bio
                bytes = bio_add_page(new_bio, pg, PAGE_SIZE, 0);
                if (bytes != PAGE_SIZE) {
                        __free_page(pg);
                        break;
                }

                total += bytes;
        }

        *bytes_added = total;
        *bio_out = new_bio;
        
        // increase ref when everything is fine
        tp_get(tp);
        return 0;

error:
        if (ret)
                LOG_ERROR(ret, "error creating read clone of write bio");
        if (new_bio)
                bio_free_clone(new_bio);

        *bytes_added = 0;
        *bio_out = NULL;
        return ret;
}
