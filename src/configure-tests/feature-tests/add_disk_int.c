// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (C) 2020 Elastio Software Inc.
 */

// kernel_version < 5.15

#include "includes.h"
MODULE_LICENSE("GPL");

static inline void dummy(void){
	struct gendisk disk;
	int ret = add_disk(&disk);
	(void)ret;
}
