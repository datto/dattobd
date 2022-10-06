// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (C) 2022 Datto Inc.
 */

#include "includes.h"

MODULE_LICENSE("GPL");

static inline void dummy(void){
    struct bio bio;
    struct block_device bd;
    struct gendisk gd;
    bd.bd_disk = &gd;
    bio.bi_bdev = &bd;
}
