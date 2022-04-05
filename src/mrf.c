// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (C) 2022 Datto Inc.
 */

#include "mrf.h"
#include "includes.h"
#include "snap_device.h"

#ifdef HAVE_BLK_ALLOC_QUEUE
//#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,7,0)
#include <linux/blk-mq.h>
#include <linux/percpu-refcount.h>
#endif

#ifdef HAVE_MAKE_REQUEST_FN_INT
int dattobd_call_mrf(make_request_fn *fn, struct request_queue *q,
                     struct bio *bio)
{
        return fn(q, bio);
}
#elif defined HAVE_MAKE_REQUEST_FN_VOID
int dattobd_call_mrf(make_request_fn *fn, struct request_queue *q,
                     struct bio *bio)
{
        fn(q, bio);
        return 0;
}
#elif !defined USE_BDOPS_SUBMIT_BIO
int dattobd_call_mrf(make_request_fn *fn, struct request_queue *q,
                     struct bio *bio)
{
        return fn(q, bio);
}
#endif

#ifdef HAVE_BLK_ALLOC_QUEUE
//#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,7,0)
MRF_RETURN_TYPE dattobd_null_mrf(struct request_queue *q, struct bio *bio)
{
        percpu_ref_get(&q->q_usage_counter);
        // create a make_request_fn for the supplied request_queue.
        return blk_mq_make_request(q, bio);
}
#endif

#ifndef HAVE_BDOPS_SUBMIT_BIO
make_request_fn* dattobd_get_bd_mrf(struct block_device *bdev)
{
    return bdev->bd_disk->queue->make_request_fn;
}

void dattobd_set_bd_mrf(struct block_device *bdev, make_request_fn *mrf)
{
    bdev->bd_disk->queue->make_request_fn = mrf;
}
#endif

#ifdef USE_BDOPS_SUBMIT_BIO
MRF_RETURN_TYPE dattobd_null_mrf(struct bio *bio)
{
    // Before we can submit our bio to the original device... 
    // If there's any bio in the bio_list that ISN'T our bio, requeue it and 
    // return early, because that's what would happen anyway if we submit 
    // the bio. submit_bio has a mechanism to prevent multiple bio's from 
    // being active at once. This mechanism also messes us up, since 
    // tracing_fn was already called from a submit_bio - so we have to clear
    // the bio_list but only if the only bio in the list, is our bio... 
    if (current->bio_list)
    {
        struct bio* bio_in_list = current->bio_list->head;
        while (bio_in_list)
        {
            if (bio_in_list != bio)
            {
                bio_list_add(&current->bio_list[0], bio);
                MRF_RETURN(0); // return BLK_QC_T_NONE;
            }
            bio_in_list = bio_in_list->bi_next;
        }
        current->bio_list = NULL; // don't free- it's stack allocated.
    }
    // Note, this is the global submit_bio - not the submit_bio function ptr
    // that is a member of struct block_device_operations - because this is
    // what we'll be using to submit IO to the real disk - the kernel's internal
    // submit_bio impl. also knows to account for null function ptrs.
    return submit_bio(bio);
}
#else
int dattobd_call_mrf_real(struct snap_device *dev, struct bio *bio)
{
    return dattobd_call_mrf(
        dev->sd_orig_request_fn,
        dattobd_bio_get_queue(bio), 
        bio);
}
#endif
