// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (C) 2017 Datto Inc.
 */

#include "includes.h"

MODULE_LICENSE("GPL");

static inline void dummy(void){
	struct bio bio;
	bio.bi_bdev = NULL;
}
