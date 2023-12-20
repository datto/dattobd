// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (C) 2023 Datto Inc.
 */

#include "includes.h"

MODULE_LICENSE("GPL");

static inline void dummy(void){
       unsigned long parent_ip = 0;
       within_module(parent_ip, THIS_MODULE);
}