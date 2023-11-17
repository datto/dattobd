// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (C) 2023 Datto Inc.
 */

#include "includes.h"

MODULE_LICENSE("GPL");

static inline void dummy(void){
    struct bio *bio = NULL;
    struct bio_vec *bvec;
    struct bvec_iter_all iter_all;
	bio_for_each_segment_all(bvec, bio, iter_all);
}
