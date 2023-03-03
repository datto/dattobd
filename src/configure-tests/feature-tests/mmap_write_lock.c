// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (C) 2023 Elastio Software Inc.
 */

// 5.8 <= kernel_version

#include "includes.h"
MODULE_LICENSE("GPL");

static inline void dummy(void){
	struct mm_struct *mm;

	mmap_write_lock(mm);
}
