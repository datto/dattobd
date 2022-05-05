// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (C) 2021 Datto Inc.
 */

#include "includes.h"

MODULE_LICENSE("GPL");

static inline void dummy(void){
    struct request_queue *rq = blk_alloc_queue(GFP_KERNEL);
}
