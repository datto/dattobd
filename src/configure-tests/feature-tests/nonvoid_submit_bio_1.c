// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (C) 2015-2023 Datto Inc.
 */

#include "includes.h"

MODULE_LICENSE("GPL");

static inline void dummy(void){
	int ret = (int)submit_bio(NULL);
	printk(KERN_INFO "%d\n", ret);
}
