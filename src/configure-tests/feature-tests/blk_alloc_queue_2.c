// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (C) 2021 Datto Inc.
 */

#include "includes.h"

MODULE_LICENSE("GPL");

static inline void dummy(void){
    make_request_fn *fn;
    struct request_queue *rq = blk_alloc_queue(fn, NUMA_NO_NODE);
    (void)rq;
}
