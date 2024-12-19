// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (C) 2024 Datto Inc.
 */

#include "includes.h"

MODULE_LICENSE("GPL");

static inline void dummy(void){
	struct bdev_handle bh;
    
    struct block_device bd;
    int holder;

    bh.bdev = &bd;
    bh.holder = (void*)&holder;
}
