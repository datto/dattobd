// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (C) 2022 Datto Inc.
 */

#include "includes.h"

#include "bio_helper.h"
#include "kernel-config.h"
#include "logging.h"
#include "snap_device.h"
#include "tracer_helper.h"
#include "tracing_params.h"
#include <linux/bio.h>

struct request_queue *dattobd_bio_get_queue(struct bio *bio)
{
#ifdef HAVE_BIO_BI_BDEV
        //#if LINUX_VERSION_CODE < KERNEL_VERSION(4,14,0)
        return bdev_get_queue(bio->bi_bdev);
#else
        return bio->bi_disk->queue;
#endif
}

void dattobd_bio_set_dev(struct bio *bio, struct block_device *bdev)
{
#ifdef HAVE_BIO_BI_BDEV
        //#if LINUX_VERSION_CODE < KERNEL_VERSION(4,14,0)
        bio->bi_bdev = bdev;
#else
        bio_set_dev(bio, bdev);
#endif
}

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

#ifndef REQ_DISCARD
#define REQ_DISCARD 0
#endif

#ifndef HAVE_ENUM_REQ_OP
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

#define bio_is_discard(bio) ((bio)->bi_rw & REQ_DISCARD)
#define dattobd_submit_bio(bio) submit_bio(0, bio)
#define dattobd_submit_bio_wait(bio) submit_bio_wait(0, bio)

int dattobd_bio_op_flagged(struct bio *bio, unsigned int flag)
{
        return bio->bi_rw & flag;
}

void dattobd_bio_op_set_flag(struct bio *bio, unsigned int flag)
{
        bio->bi_rw |= flag;
}

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

void dattobd_set_bio_ops(struct bio *bio, req_op_t op, unsigned op_flags)
{
        bio->bi_opf = 0;
        bio_set_op_attrs(bio, op, op_flags);
}

int dattobd_bio_op_flagged(struct bio *bio, unsigned int flag)
{
        return bio->bi_opf & flag;
}

void dattobd_bio_op_set_flag(struct bio *bio, unsigned int flag)
{
        bio->bi_opf |= flag;
}

void dattobd_bio_op_clear_flag(struct bio *bio, unsigned int flag)
{
        bio->bi_opf &= ~flag;
}

#ifdef REQ_DISCARD
#define bio_is_discard(bio) ((bio)->bi_opf & REQ_DISCARD)
#else
#define bio_is_discard(bio)                                                    \
        (bio_op(bio) == REQ_OP_DISCARD || bio_op(bio) == REQ_OP_SECURE_ERASE)
#endif
#define dattobd_submit_bio(bio) submit_bio(bio)
#define dattobd_submit_bio_wait(bio) submit_bio_wait(bio)
#endif

#if !defined HAVE_SUBMIT_BIO_WAIT && !defined HAVE_SUBMIT_BIO_1
//#if LINUX_VERSION_CODE < KERNEL_VERSION(3,10,0)
struct submit_bio_ret {
        struct completion event;
        int error;
};

static void __submit_bio_wait_endio(struct bio *bio, int error)
{
        struct submit_bio_ret *ret = bio->bi_private;
        ret->error = error;
        complete(&ret->event);
}

#ifdef HAVE_BIO_ENDIO_INT
static int submit_bio_wait_endio(struct bio *bio, unsigned int bytes, int error)
{
        if (bio->bi_size)
                return 1;

        __submit_bio_wait_endio(bio, error);
        return 0;
}
#else
static void submit_bio_wait_endio(struct bio *bio, int error)
{
        __submit_bio_wait_endio(bio, error);
}
#endif

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
void dattobd_bio_endio(struct bio *bio, int err)
{
        bio_endio(bio, bio->bi_size, err);
}
#elif !defined HAVE_BIO_ENDIO_1
void dattobd_bio_endio(struct bio *bio, int err)
{
        bio_endio(bio, err);
}
#elif defined HAVE_BLK_STATUS_T
void dattobd_bio_endio(struct bio *bio, int err)
{
        bio->bi_status = errno_to_blk_status(err);
        bio_endio(bio);
}
#else
void dattobd_bio_endio(struct bio *bio, int err)
{
        bio->bi_error = err;
        bio_endio(bio);
}
#endif

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
static int on_bio_read_complete(struct bio *bio, unsigned int bytes, int err)
{
        if (bio->bi_size)
                return 1;
        __on_bio_read_complete(bio, err);
        return 0;
}
#elif !defined HAVE_BIO_ENDIO_1
static void on_bio_read_complete(struct bio *bio, int err)
{
        if (!test_bit(BIO_UPTODATE, &bio->bi_flags))
                err = -EIO;
        __on_bio_read_complete(bio, err);
}
#elif defined HAVE_BLK_STATUS_T
static void on_bio_read_complete(struct bio *bio)
{
        __on_bio_read_complete(bio, blk_status_to_errno(bio->bi_status));
}
#else
static void on_bio_read_complete(struct bio *bio)
{
        __on_bio_read_complete(bio, bio->bi_error);
}
#endif

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
static void bio_destructor_tp(struct bio *bio)
{
        struct tracing_params *tp = bio->bi_private;
        bio_free(bio, dev_bioset(tp->dev));
}

static void bio_destructor_snap_dev(struct bio *bio)
{
        struct snap_device *dev = bio->bi_private;
        bio_free(bio, dev_bioset(dev));
}
#endif

void bio_free_clone(struct bio *bio)
{
        int i;

        for (i = 0; i < bio->bi_vcnt; i++) {
                if (bio->bi_io_vec[i].bv_page)
                        __free_page(bio->bi_io_vec[i].bv_page);
        }
        bio_put(bio);
}

int bio_make_read_clone(struct bio_set *bs, struct tracing_params *tp,
                        struct bio *orig_bio, sector_t sect, unsigned int pages,
                        struct bio **bio_out, unsigned int *bytes_added)
{
        int ret;
        struct bio *new_bio;
        struct page *pg;
        unsigned int i, bytes,
                total = 0,
                actual_pages = (pages > BIO_MAX_PAGES) ? BIO_MAX_PAGES : pages;

        // allocate bio clone
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
        tp_get(tp);
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
