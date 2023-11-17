// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (C) 2023 Datto Inc.
 */

#include "includes.h"

MODULE_LICENSE("GPL");

static inline void dummy(void){
    struct gendisk *sd_gd; = NULL;
    set_bit(GD_OWNS_QUEUE, &sd_gd->state);
}