// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (C) 2018 Datto Inc.
 * Additional contributions by Elastio Software, Inc are Copyright (C) 2020 Elastio Software Inc.
 */

#include "includes.h"
MODULE_LICENSE("GPL");

static inline void dummy(void){
	struct file *f = NULL;
	void *buf = NULL;
	loff_t *pos = NULL;

	(void)kernel_read(f, buf, 0, pos);
}
