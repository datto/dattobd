// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (C) 2024 Datto Inc.
 */

#include "includes.h"

MODULE_LICENSE("GPL");

static inline void dummy(void){
	struct gendisk* gd;
    u8 partno;
    struct hd_struct* hd;
    hd = disk_get_part(gd, partno);
}
