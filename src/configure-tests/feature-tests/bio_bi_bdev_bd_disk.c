// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (C) 2020 Elastio Software Inc.
 */

#include "includes.h"
MODULE_LICENSE("GPL");

static inline void dummy(void){
	struct bio bio;
	struct gendisk disk;
	bio.bi_bdev->bd_disk = &disk;
}
