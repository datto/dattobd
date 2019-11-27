// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (C) 2015 Datto Inc.
 * Additional contributions by Assurio Software, Inc are Copyright (C) 2019 Assurio Software Inc.
 */

#include "includes.h"

static inline void dummy(void){
	struct queue_limits *t;
	struct block_device *bdev;
	sector_t start;
	bdev_stack_limits(t, bdev, start);
}
