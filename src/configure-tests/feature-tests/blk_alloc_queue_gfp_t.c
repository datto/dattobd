// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (C) 2020 Elastio Software Inc.
 */

// 2.6.23 <= kernel_version < 5.7

#include "includes.h"

static inline void dummy(void){
    struct request_queue *queue = blk_alloc_queue(GFP_KERNEL);
	(void)queue;
}
