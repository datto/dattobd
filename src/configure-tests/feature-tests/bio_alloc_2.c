// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (C) 2023 Elastio Software Inc.
 */

// 5.18 <= kernel_version

#include "includes.h"
MODULE_LICENSE("GPL");

static inline void dummy(void){
	struct bio *new_bio;

	new_bio = bio_alloc(GFP_KERNEL, 1);
}
