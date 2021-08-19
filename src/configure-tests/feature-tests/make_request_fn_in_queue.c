// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (C) 2020 Elastio Software Inc.
 */

// 5.8 <= kernel_version

#include "includes.h"
MODULE_LICENSE("GPL");

static inline void dummy(void){
    make_request_fn *mk_rq_fn;
	struct request_queue q = {
		.make_request_fn = mk_rq_fn,
	};
	(void)q;
}
