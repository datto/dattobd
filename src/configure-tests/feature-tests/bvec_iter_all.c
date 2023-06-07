// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (C) 2023 Datto Inc.
 */

#include "includes.h"

MODULE_LICENSE("GPL");

static inline void dummy(void){
    struct bvec_iter_all iter_all;
    if(iter_all.idx) iter_all.idx = 0;	
}
