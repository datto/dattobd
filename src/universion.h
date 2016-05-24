/*
    Copyright (C) 2015 Datto Inc.

    This file is part of dattobd.

    This program is free software; you can redistribute it and/or modify it 
    under the terms of the GNU General Public License version 2 as published
    by the Free Software Foundation.
*/

#ifndef DATTOBD_UNIVERSION_H_
#define DATTOBD_UNIVERSION_H_

#include "kernel-config.h"
#include "includes.h"

#ifndef HAVE_BIO_LIST
//#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,30)
struct bio_list {
	struct bio *head;
	struct bio *tail;
};

#define BIO_EMPTY_LIST	{ NULL, NULL }
#define bio_list_for_each(bio, bl) for((bio) = (bl)->head; (bio); (bio) = (bio)->bi_next)

static inline int bio_list_empty(const struct bio_list *bl){
	return bl->head == NULL;
}

static inline void bio_list_init(struct bio_list *bl){
	bl->head = bl->tail = NULL;
}

static inline void bio_list_add(struct bio_list *bl, struct bio *bio){
	bio->bi_next = NULL;

	if (bl->tail) bl->tail->bi_next = bio;
	else bl->head = bio;

	bl->tail = bio;
}

static inline struct bio *bio_list_pop(struct bio_list *bl){
	struct bio *bio = bl->head;

	if (bio) {
		bl->head = bl->head->bi_next;
		if (!bl->head) bl->tail = NULL;

		bio->bi_next = NULL;
	}

	return bio;
}
#endif

#ifndef HAVE_D_UNLINKED
//#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,31)
static inline int d_unlinked(struct dentry *dentry){
	return d_unhashed(dentry) && !IS_ROOT(dentry);
}
#endif

#ifndef HAVE_NOOP_LLSEEK
//#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,35)
static inline loff_t noop_llseek(struct file *file, loff_t offset, int origin){
	return file->f_pos;
}
#endif

#ifndef HAVE_BLKDEV_GET_BY_PATH
//#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,38)
struct block_device *blkdev_get_by_path(const char *path, fmode_t mode, void *holder);
#endif

#ifndef HAVE_SUBMIT_BIO_WAIT
//#if LINUX_VERSION_CODE < KERNEL_VERSION(3,10,0)
struct submit_bio_ret{
	struct completion event;
	int error;
};

void submit_bio_wait_endio(struct bio *bio, int error);
int submit_bio_wait(int rw, struct bio *bio);
#endif

//the kernel changed the usage of bio_for_each_segment in 3.14. Do not use any fields directly or you will lose compatibility.
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
	#define bio_iter_idx(iter) (iter.bi_idx)
	#define bio_sector(bio) (bio)->bi_iter.bi_sector
	#define bio_size(bio) (bio)->bi_iter.bi_size
	#define bio_idx(bio) (bio)->bi_iter.bi_idx
#endif

#ifndef HAVE_KERN_PATH
int kern_path(const char *name, unsigned int flags, struct path *path);
#endif

#ifdef HAVE_BLKDEV_PUT_1
//#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,28)
	#define dattobd_blkdev_put(bdev) blkdev_put(bdev);
#else
	#define dattobd_blkdev_put(bdev) blkdev_put(bdev, FMODE_READ);
#endif

#ifndef HAVE_PART_NR_SECTS_READ
//#if LINUX_VERSION_CODE < KERNEL_VERSION(3,6,0)
	#define dattobd_bdev_size(bdev) (bdev->bd_part->nr_sects)
#else
	#define dattobd_bdev_size(bdev) part_nr_sects_read(bdev->bd_part)
#endif

#ifndef HAVE_VZALLOC
	#define vzalloc(size) __vmalloc(size, GFP_KERNEL | __GFP_HIGHMEM | __GFP_ZERO, PAGE_KERNEL)
#endif

#ifndef HAVE_BIO_ENDIO_1
	#define dattobd_bio_endio(bio, err) bio_endio(bio, err)
#else
	#define dattobd_bio_endio(bio, err) \
		(bio)->bi_error = (err); \
		bio_endio(bio);
#endif

#ifdef HAVE_MAKE_REQUEST_FN_INT
	#define MRF_RETURN_TYPE int
	#define MRF_RETURN(ret) return ret

static inline int dattobd_call_mrf(make_request_fn *fn, struct request_queue *q, struct bio *bio){
	return fn(q, bio);
}
#elif defined HAVE_MAKE_REQUEST_FN_VOID
	#define MRF_RETURN_TYPE void
	#define MRF_RETURN(ret) return

static inline int dattobd_call_mrf(make_request_fn *fn, struct request_queue *q, struct bio *bio){
	fn(q, bio);
	return 0;
}
#else
	#define MRF_RETURN_TYPE blk_qc_t
	#define MRF_RETURN(ret) return BLK_QC_T_NONE

static inline int dattobd_call_mrf(make_request_fn *fn, struct request_queue *q, struct bio *bio){
	return fn(q, bio);
}
#endif

//this is defined in 3.16 and up
#ifndef MIN_NICE
	#define MIN_NICE -20
#endif

//if this isn't defined, we don't need it anyway
#ifndef FMODE_NONOTIFY
	#define FMODE_NONOTIFY 0
#endif

#ifndef HAVE_BLK_QUEUE_MAX_SECTORS
	#define blk_queue_max_sectors(q, sects) blk_queue_max_hw_sectors(q, sects)
#endif

#ifndef HAVE_BLK_SET_STACKING_LIMITS
	#define blk_set_stacking_limits(ql) blk_set_default_limits(ql)
#endif

#endif
