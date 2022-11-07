// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (C) 2022 Elastio Software Inc.
 */

// kernel_version < 5.9

#include "includes.h"
MODULE_LICENSE("GPL");

static inline void dummy(void){
	struct bio _bio = { 0 };
	generic_make_request(&_bio);
}
