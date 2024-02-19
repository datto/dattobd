// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (C) 2015 Datto Inc.
 */

#include "includes.h"

MODULE_LICENSE("GPL");

static inline void dummy(void){
	struct block_device *bd = blkdev_get_by_path("path", FMODE_READ, NULL, NULL);
	if(bd) bd = NULL;
}
