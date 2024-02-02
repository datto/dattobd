// SPDX-License-Identifier: GPL-2.0-only

/*
* Copyright (C) 2024 Kaseya
*/

#include "includes.h"
MODULE_LICENSE("GPL");

static inline void dummy(void){
	struct mm_struct *mm;

	mmap_write_lock(mm);
}
