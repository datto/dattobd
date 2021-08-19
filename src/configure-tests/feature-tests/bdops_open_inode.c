// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (C) 2015 Datto Inc.
 * Additional contributions by Elastio Software, Inc are Copyright (C) 2020 Elastio Software Inc.
 */

#include "includes.h"
MODULE_LICENSE("GPL");

static int snap_open(struct inode *inode, struct file *filp){
	return 0;
}

static int snap_release(struct inode *inode, struct file *filp){
	return 0;
}

static inline void dummy(void){
	struct inode i;
	struct file f;
	struct block_device_operations bdo = {
		.open = snap_open,
		.release = snap_release,
	};

	bdo.open(&i, &f);
	bdo.release(&i, &f);
}
