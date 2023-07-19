// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (C) 2015-2023 Datto Inc.
 */

#include "includes.h"

MODULE_LICENSE("GPL");

static inline void dummy(void){
    struct gendisk *gd = NULL;
	int ret = (int)add_disk(gd);
	printk(KERN_INFO "%d\n", ret);
}