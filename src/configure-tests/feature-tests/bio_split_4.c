// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (C) 2022 Elastio Software Inc.
 */

// kernel version > 3.14

#include "includes.h"
MODULE_LICENSE("GPL");

static inline void dummy(void){
	struct bio _bio = { 0 };
	struct bio *split = NULL;
	split = bio_split(&_bio, 128, GFP_NOIO, NULL);
}
