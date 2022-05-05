// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (C) 2015 Datto Inc.
 */

#include "includes.h"

MODULE_LICENSE("GPL");

static inline void dummy(void){
	struct queue_limits *t;
	struct block_device *bdev;
	sector_t start;
	bdev_stack_limits(t, bdev, start);
}
