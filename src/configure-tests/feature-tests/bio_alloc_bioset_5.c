// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (C) 2020 Elastio Software Inc.
 */

// 5.18 <= kernel_version

#include "includes.h"
MODULE_LICENSE("GPL");

static inline void dummy(void){
	struct bio_set bs;
	struct bio *new_bio;
	struct block_device bdev;

	new_bio = bio_alloc_bioset(&bdev, 0, 0, GFP_NOIO, &bs);
}
