// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (C) 2022 Elastio Software Inc.
 */

// kernel_version < 6.0

#include "includes.h"
MODULE_LICENSE("GPL");

static inline void dummy(void){
	struct block_device *bdev = NULL;
	char b[BDEVNAME_SIZE];
	bdevname(bdev, b);
}
