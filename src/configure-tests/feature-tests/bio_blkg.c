// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (C) 2022 Datto Inc.
 */

#include "includes.h"
#include <linux/blk-types.h>

MODULE_LICENSE("GPL");

static inline void dummy(void){
    struct bio* b = NULL;
    blkg_get(b->bi_blkg);
}
