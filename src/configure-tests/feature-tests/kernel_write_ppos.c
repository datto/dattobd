// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (C) 2017 Datto Inc.
 */

#include "includes.h"

MODULE_LICENSE("GPL");

static inline void dummy(void){
	struct file *f = NULL;
	const void *buf = NULL;
	loff_t *pos = NULL;

	(void)kernel_write(f, buf, 0, pos);
}
