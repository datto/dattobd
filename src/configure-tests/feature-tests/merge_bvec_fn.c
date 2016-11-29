/*
    Copyright (C) 2015 Datto Inc.

    This file is part of dattobd.

    This program is free software; you can redistribute it and/or modify it
    under the terms of the GNU General Public License version 2 as published
    by the Free Software Foundation.
*/

#include "../../includes.h"

static int dummy_merge_bvec(struct request_queue *q, struct bvec_merge_data *bvm, struct bio_vec *bvec){
	return 0;
}

static inline void dummy(void){
	struct request_queue q;
	q.merge_bvec_fn = dummy_merge_bvec;
}
