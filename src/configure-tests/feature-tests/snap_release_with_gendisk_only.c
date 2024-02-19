// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (C) 2024 Datto Inc.
 */

#include "includes.h"

MODULE_LICENSE("GPL");

static int snap_open(struct gendisk *disk, blk_mode_t mode){
	return 0;
}

static void snap_release(struct gendisk *disk){
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