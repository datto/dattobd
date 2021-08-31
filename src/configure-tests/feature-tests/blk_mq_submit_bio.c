// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (C) 2020 Elastio Software Inc.
 */

#include "includes.h"
#include <linux/blk-mq.h>
MODULE_LICENSE("GPL");

static inline void dummy(void){
	struct bio b;
	blk_mq_submit_bio(&b);
}
