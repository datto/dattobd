// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (C) 2024 Datto Inc.
 */

#include "includes.h"

MODULE_LICENSE("GPL");

static inline void dummy(void){
	struct block_device* __attribute__ ((unused)) bd;
    struct file* file = NULL;
	
    bd = file_bdev(file);
}
