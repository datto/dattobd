// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (C) 2020 Elastio Software Inc.
 */

#include "includes.h"
MODULE_LICENSE("GPL");

static inline void dummy(void){
	struct bvec_iter_all iter;

	iter.idx = 0;
	iter.done = 0;
	memset(&iter.bv, 0, sizeof(iter.bv));
}
