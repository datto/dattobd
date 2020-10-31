// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (C) 2020 Elastio Software Inc.
 */

// 5.7 <= kernel_version

#include "includes.h"

static inline void dummy(void){
    make_request_fn *mk_rq_fn;
    struct request_queue *queue = blk_alloc_queue(mk_rq_fn, 0);
	(void)queue;
}
