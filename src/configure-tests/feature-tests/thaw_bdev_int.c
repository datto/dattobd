// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (C) 2015 Datto Inc.
 * Additional contributions by Elastio Software, Inc are Copyright (C) 2020 Elastio Software Inc.
 */

#include "includes.h"

static inline void dummy(void){
	struct block_device bd;
	struct super_block sb;

	if(thaw_bdev(&bd, &sb)) bd.bd_private = 0;
}
