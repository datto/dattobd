// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (C) 2015 Datto Inc.
 * Additional contributions by Elastio Software, Inc are Copyright (C) 2020 Elastio Software Inc.
 */

#include "includes.h"
MODULE_LICENSE("GPL");

static inline void dummy(void){
	struct bio_set *bs;
	int bio_pool_size;
	int bvec_pool_size;
	int scale;
	bs = bioset_create(bio_pool_size, bvec_pool_size, scale);
}
