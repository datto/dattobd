// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (C) 2020 Elastio Software Inc.
 */

// kernel_version < 5.15

#include "includes.h"
MODULE_LICENSE("GPL");

static inline void dummy(void){
    struct gendisk *disk = alloc_disk(1);
	(void)disk;
}
