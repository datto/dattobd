// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (C) 2025 Datto Inc.
 */

#ifndef STACK_LIMITS_H_
#define STACK_LIMITS_H_

#include "logging.h"

// dattobd_bdev_stack_limits(request_queue, bdev, sector_t) -- our wrapper

// queue_limits_stack_bdev(queue_limits, bdev, sector_t, pfx) -- from 6.9
// bdev_stack_limits(queue_limits, bdev, sector_t) -- from 2.6.33 up to 5.8
// blk_stack_limits(queue_limits, queue_limits, sector_t) -- from 2.6.31

// dattobd_blk_set_stacking_limits(queue_limits) -- our wrapper

// blk_set_stacking_limits(queue_limits) -- from 3.3
// blk_set_default_limits(queue_limits) -- from 2.6.31 to 6.1


#if defined(HAVE_QUEUE_LIMITS_STACK_BDEV)

// queue_limits_stack_bdev is our top priority, if it is available -- we use it

#define dattobd_bdev_stack_limits(rq, bd, sec) queue_limits_stack_bdev(&(rq)->limits, bdev, sec, DATTO_TAG)

#else

// if queue_limits_stack_bdev is not available, we use bdev_stack_limits, if it is also not available, we emulate it

#if !defined(HAVE_BDEV_STACK_LIMITS)

static int bdev_stack_limits(struct queue_limits *t, struct block_device *bdev, sector_t start){
    struct request_queue *bq = bdev_get_queue(bdev);
    start += get_start_sect(bdev);
    return blk_stack_limits(t, &bq->limits, start << 9);
}

#endif

#define dattobd_bdev_stack_limits(rq, bd, sec) bdev_stack_limits(&(rq)->limits, bdev, sec)

#endif

#if !defined(HAVE_BLK_SET_STACKING_LIMITS)

#define blk_set_stacking_limits(lim) blk_set_default_limits(lim)

#endif

#endif