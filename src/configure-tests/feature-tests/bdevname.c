// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (C) 2023 Datto Inc.
 */

#include "includes.h"

MODULE_LICENSE("GPL");
#define BDEVNAME_SIZE 32
static inline void dummy(void){
        char bdev_name[BDEVNAME_SIZE];
        struct block_device *bdev;
        bdevname(bdev, bdev_name);
}
