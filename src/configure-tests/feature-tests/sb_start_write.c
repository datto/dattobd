// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (C) 2015 Datto Inc.
 */

#include "includes.h"

MODULE_LICENSE("GPL");

static inline void dummy(void){
	struct inode i;
	sb_start_write(i.i_sb);
	sb_end_write(i.i_sb);
}
