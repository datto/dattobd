// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (C) 2021 Datto Inc.
 */

#include "includes.h"

MODULE_LICENSE("GPL");

static inline void dummy(void){
    struct proc_ops test;
	proc_create("", 0, NULL, &test);
}
