// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (C) 2017 Datto Inc.
 */

#include "includes.h"

MODULE_LICENSE("GPL");

static inline void dummy(void){
	int flags = BIOSET_NEED_BVECS;
	(void)flags;
}
