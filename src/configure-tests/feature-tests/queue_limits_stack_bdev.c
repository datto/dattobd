// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (C) 2024 Datto Inc.
 */

#include "includes.h"

MODULE_LICENSE("GPL");

static inline void dummy(void){
    struct queue_limits *ql = NULL;
    struct block_device *bdev = NULL;
    sector_t offset;
    const char* pfx = "";

    queue_limits_stack_bdev(ql, bdev, offset, pfx);
}
