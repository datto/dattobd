// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (C) 2022 Datto Inc.
 */

#ifndef BIO_HELPER_H

#define BIO_HELPER_H

#include "includes.h"
#include "tracing_params.h"

//#if LINUX_VERSION_CODE < KERNEL_VERSION(4,17,0)
#ifndef SECTOR_SHIFT
#define SECTOR_SHIFT 9
#endif
#ifndef SECTOR_SIZE
#define SECTOR_SIZE (1 << SECTOR_SHIFT)
#endif

#define SECTORS_PER_BLOCK (COW_BLOCK_SIZE / SECTOR_SIZE)
#define SECTOR_TO_BLOCK(sect) ((sect) / SECTORS_PER_BLOCK)

#if !defined HAVE_MAKE_REQUEST_FN_IN_QUEUE && defined HAVE_BDOPS_SUBMIT_BIO
        // Linux kernel version 5.9+
        // make_request_fn has been moved from the request queue structure to the
        // block_device_operations as submit_bio function.
        // See https://github.com/torvalds/linux/commit/c62b37d96b6eb3ec5ae4cbe00db107bf15aebc93
        #define USE_BDOPS_SUBMIT_BIO

#ifdef HAVE_NONVOID_SUBMIT_BIO_1
        typedef blk_qc_t (make_request_fn) (struct bio *bio);
#else
        typedef void (make_request_fn) (struct bio *bio);
#endif
#endif

// macros for working with bios
#define BIO_SET_SIZE 256
#define bio_last_sector(bio) (bio_sector(bio) + (bio_size(bio) / SECTOR_SIZE))

// the kernel changed the usage of bio_for_each_segment in 3.14. Do not use any
// fields directly or you will lose compatibility.
#ifndef HAVE_BVEC_ITER
//#if LINUX_VERSION_CODE < KERNEL_VERSION(3,14,0)
typedef int bio_iter_t;
typedef struct bio_vec *bio_iter_bvec_t;
#define bio_iter_len(bio, iter) ((bio)->bi_io_vec[(iter)].bv_len)
#define bio_iter_offset(bio, iter) ((bio)->bi_io_vec[(iter)].bv_offset)
#define bio_iter_page(bio, iter) ((bio)->bi_io_vec[(iter)].bv_page)
#define bio_iter_idx(iter) (iter)
#define bio_sector(bio) (bio)->bi_sector
#define bio_size(bio) (bio)->bi_size
#define bio_idx(bio) (bio)->bi_idx
#else
typedef struct bvec_iter bio_iter_t;
typedef struct bio_vec bio_iter_bvec_t;
#define bio_iter_idx(iter) ((iter).bi_idx)
#define bio_sector(bio) (bio)->bi_iter.bi_sector
#define bio_size(bio) (bio)->bi_iter.bi_size
#define bio_idx(bio) (bio)->bi_iter.bi_idx
#endif

#ifndef HAVE_BIOSET_INIT
//#if LINUX_VERSION_CODE < KERNEL_VERSION(4,18,0)
#define dev_bioset(dev) ((dev)->sd_bioset)
#else
#define dev_bioset(dev) (&(dev)->sd_bioset)
#endif

struct bio_sector_map {
        struct bio *bio;
        sector_t sect;
        unsigned int size;
        struct bio_sector_map *next;
};

struct request_queue *dattobd_bio_get_queue(struct bio *bio);

void dattobd_bio_set_dev(struct bio *bio, struct block_device *bdev);

void dattobd_bio_copy_dev(struct bio *dst, struct bio *src);

/* don't perform COW operation */
#ifdef HAVE_ENUM_REQ_OP
//#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,8,0) && LINUX_VERSION_CODE <
// KERNEL_VERSION(4,10,0)
/* special case for deb9's 4.9 train
 * Bit 30 conflicts with struct bio's bi_opf opcode bitfield, which occupies the
 * top 3 bits of the member. If we set that bit, it will mutate the operation
 * that the bio is representing. Setting this to 28 puts this in an unused flag
 * for bi_opf (that flag means something in struct request's cmd_flags, but
 * we're not setting that).
 */
#define __DATTOBD_PASSTHROUGH 28 // set as the last flag bit
#else
// set as an unused flag in versions older than 4.8
// set as an unused opcode bit in kernels newer than 4.9
#define __DATTOBD_PASSTHROUGH 30
#endif
#define DATTOBD_PASSTHROUGH (1ULL << __DATTOBD_PASSTHROUGH)

#ifndef HAVE_SUBMIT_BIO_1
//#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,8,0)

#ifndef REQ_DISCARD
#define REQ_DISCARD 0
#endif

#ifndef HAVE_ENUM_REQ_OP
typedef enum req_op {
        REQ_OP_READ,
        REQ_OP_WRITE,
        REQ_OP_DISCARD, /* request to discard sectors */
        REQ_OP_SECURE_ERASE, /* request to securely erase sectors */
        REQ_OP_WRITE_SAME, /* write same block many times */
        REQ_OP_FLUSH, /* request for cache flush */
} req_op_t;
#endif
typedef enum req_op req_op_t;

extern void dattobd_set_bio_ops(struct bio *bio, req_op_t op,	
                                unsigned op_flags);




#define bio_is_discard(bio) ((bio)->bi_rw & REQ_DISCARD)
#define dattobd_submit_bio(bio) submit_bio(0, bio)
#define dattobd_submit_bio_wait(bio) submit_bio_wait(0, bio)

int dattobd_bio_op_flagged(struct bio *bio, unsigned int flag);
void dattobd_bio_op_set_flag(struct bio *bio, unsigned int flag);
void dattobd_bio_op_clear_flag(struct bio *bio, unsigned int flag);

#else

#ifndef HAVE_ENUM_REQ_OPF
//#if LINUX_VERSION_CODE < KERNEL_VERSION(4,10,0)
typedef enum req_op req_op_t;
#else
typedef enum req_opf req_op_t;
#endif

void dattobd_set_bio_ops(struct bio *bio, req_op_t op, unsigned op_flags);
int dattobd_bio_op_flagged(struct bio *bio, unsigned int flag);
void dattobd_bio_op_set_flag(struct bio *bio, unsigned int flag);
void dattobd_bio_op_clear_flag(struct bio *bio, unsigned int flag);

#ifdef REQ_DISCARD
#define bio_is_discard(bio) ((bio)->bi_opf & REQ_DISCARD)
#else
#define bio_is_discard(bio)                                                    \
        (bio_op(bio) == REQ_OP_DISCARD || bio_op(bio) == REQ_OP_SECURE_ERASE)
#endif

#define dattobd_submit_bio(bio) submit_bio(bio)
#define dattobd_submit_bio_wait(bio) submit_bio_wait(bio)

#endif

struct inode *page_get_inode(struct page *pg);

int bio_needs_cow(struct bio *bio, struct inode *inode);

void bio_free_clone(struct bio *bio);

int bio_make_read_clone(struct bio_set *bs, struct tracing_params *tp,
                        struct bio *orig_bio, sector_t sect, unsigned int pages,
                        struct bio **bio_out, unsigned int *bytes_added);

#ifdef HAVE_BIO_ENDIO_INT
void dattobd_bio_endio(struct bio *bio, int err);
#elif !defined HAVE_BIO_ENDIO_1
void dattobd_bio_endio(struct bio *bio, int err);
#elif defined HAVE_BLK_STATUS_T
void dattobd_bio_endio(struct bio *bio, int err);
#else
void dattobd_bio_endio(struct bio *bio, int err);
#endif

#ifdef HAVE_BIO_BI_BDEV_BD_DISK
    #define dattobd_bio_bi_disk(bio) ((bio)->bi_bdev->bd_disk)
#else
    #define dattobd_bio_bi_disk(bio) ((bio)->bi_disk)
#endif

#if !defined HAVE_BIO_FOR_EACH_SEGMENT_ALL_1 && !defined HAVE_BIO_FOR_EACH_SEGMENT_ALL_2
        #define bio_for_each_segment_all(bvl, bio, i)				\
	        for (i = 0, bvl = (bio)->bi_io_vec; i < (bio)->bi_vcnt; i++, bvl++)
#endif

#ifdef USE_BDOPS_SUBMIT_BIO
int tracer_alloc_ops(struct snap_device* dev);
#endif

#endif /* BIO_HELPER_H */
