// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (C) 2015 Datto Inc.
 */

#include "includes.h"

MODULE_LICENSE("GPL");

static void dummy_mrf(struct request_queue *q, struct bio *bio){
	return;
}

static inline void dummy(void){
	struct bio b;
	struct request_queue q = {
		.make_request_fn = dummy_mrf,
	};

	q.make_request_fn(&q, &b);
}
