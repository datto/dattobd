// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (C) 2023 Datto Inc.
 */

// 5.16 <= kernel_version

#include "includes.h"

MODULE_LICENSE("GPL");

static inline void dummy(void){
    struct block_device* bdev = NULL;
    gfp_t mask = 0;
    struct bio_set* bs = NULL;
	bio_alloc_bioset(bdev, 0, 0, mask, bs);
}
