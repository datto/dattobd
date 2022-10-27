// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (C) 2022 Datto Inc.
 */

#include "includes.h"

MODULE_LICENSE("GPL");

static inline void dummy(void){
    struct gendisk* gd = blk_alloc_disk(NUMA_NO_NODE);
    gd->minors = 1;
}
