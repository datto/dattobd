// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (C) 2025 Datto Inc.
 */

#include "includes.h"

MODULE_LICENSE("GPL");

static inline void dummy(void){
    char *dest;
    const char *src;
    size_t count;
    size_t ret;
    
    (void)ret;

    ret = strlcpy(dest, src, count);
}