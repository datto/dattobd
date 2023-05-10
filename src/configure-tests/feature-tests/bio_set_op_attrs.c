// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (C) 2023 Elastio Software Inc.
 */

// kernel_version < 6.2

#include "includes.h"
MODULE_LICENSE("GPL");

static inline void dummy(void){
	struct bio bio;

	bio_set_op_attrs(&bio, REQ_OP_READ, 0);
}
