// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (C) 2015 Datto Inc.
 * Additional contributions by Elastio Software, Inc are Copyright (C) 2020 Elastio Software Inc.
 */

#include "includes.h"

static inline void dummy(void){
	struct file f = { .f_version = 0 };
	f.f_op->fallocate(&f, 0, 0, 0);
}
