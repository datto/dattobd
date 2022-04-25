// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (C) 2022 Datto Inc.
 */

#include "mrf.h"
#include "includes.h"

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
#else
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
