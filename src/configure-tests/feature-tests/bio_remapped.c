// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (C) 2022 Elastio Software Inc.
 */

// kernel_version > 5.12

#include "includes.h"
MODULE_LICENSE("GPL");

static inline void dummy(void){
	struct bio bio;

	bio_set_flag(&bio, BIO_REMAPPED);
}
