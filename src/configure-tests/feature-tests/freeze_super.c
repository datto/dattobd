// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (C) 2020 Elastio Software Inc.
 */

// 2.6.34 < kernel_version

#include "includes.h"
MODULE_LICENSE("GPL");

static inline void dummy(void){
	struct super_block sb;
	int res;

	res = freeze_super(&sb);
	res = thaw_super(&sb);
}
