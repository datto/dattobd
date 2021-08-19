// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (C) 2015 Datto Inc.
 * Additional contributions by Elastio Software, Inc are Copyright (C) 2020 Elastio Software Inc.
 */

#include "includes.h"
MODULE_LICENSE("GPL");

static int snap_open(struct block_device *bdev, fmode_t mode){
	return 0;
}

static int snap_release(struct gendisk *gd, fmode_t mode){
	return 0;
}

static inline void dummy(void){
	struct gendisk gd;
	struct block_device bd;
	struct block_device_operations bdo = {
		.open = snap_open,
		.release = snap_release,
	};

	bdo.open(&bd, FMODE_READ);
	bdo.release(&gd, FMODE_READ);
}
