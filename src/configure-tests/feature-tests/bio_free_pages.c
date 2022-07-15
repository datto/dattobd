// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (C) 2020 Elastio Software Inc.
 */

// 4.9 <= kernel_version

#include "includes.h"
MODULE_LICENSE("GPL");

static inline void dummy(void){
	struct bio bio;
	bio_free_pages(&bio);
}
