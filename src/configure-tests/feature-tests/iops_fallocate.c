// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (C) 2015 Datto Inc.
 */

#include "includes.h"

MODULE_LICENSE("GPL");

static inline void dummy(void){
	struct inode i = { .i_sb = NULL };
	i.i_op->fallocate(&i, 0, 0, 0);
}
