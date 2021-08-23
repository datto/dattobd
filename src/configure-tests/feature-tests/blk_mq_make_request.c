// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (C) 2020 Elastio Software Inc.
 */

// kernel_version = 5.8

#include "includes.h"
#include <linux/blk-mq.h>
MODULE_LICENSE("GPL");

static inline void dummy(void){
	struct request_queue rq;
	struct bio b;
	blk_qc_t qc = blk_mq_make_request(&rq, &b);
	(void)qc;
}
