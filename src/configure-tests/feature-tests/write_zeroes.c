// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (C) 2022 Elastio Software Inc.
 */

// kernel_version >= 4.10

#include "includes.h"
MODULE_LICENSE("GPL");

static inline void dummy(void){
		int n = (int)REQ_OP_WRITE_ZEROES;
		(void)n;
}

