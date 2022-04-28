// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (C) 2020 Elastio Software Inc.
 */

// kernel_version = 3.16, 4.4, 4.6+

#include "includes.h"
MODULE_LICENSE("GPL");

static inline void dummy(void){
	long res;
	res = si_mem_available();
}
