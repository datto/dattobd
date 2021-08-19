// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (C) 2015 Datto Inc.
 * Additional contributions by Elastio Software, Inc are Copyright (C) 2020 Elastio Software Inc.
 */

#include "includes.h"
MODULE_LICENSE("GPL");

static int dummy_merge_bvec(struct request_queue *q, struct bvec_merge_data *bvm, struct bio_vec *bvec){
	return 0;
}

static inline void dummy(void){
	struct request_queue q;
	q.merge_bvec_fn = dummy_merge_bvec;
}
