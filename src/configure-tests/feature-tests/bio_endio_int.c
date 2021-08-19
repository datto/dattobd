// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (C) 2015 Datto Inc.
 * Additional contributions by Elastio Software, Inc are Copyright (C) 2020 Elastio Software Inc.
 */

#include "includes.h"
MODULE_LICENSE("GPL");

static int dummy_endio(struct bio *bio, unsigned int bytes, int err){
	return 0;
}

static inline void dummy(void){
	struct bio bio;
	bio.bi_end_io = dummy_endio;
}
