// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (C) 2015 Datto Inc.
 */

#include "includes.h"

MODULE_LICENSE("GPL");

static inline void dummy(void){
	char *c = NULL;
	struct dentry d;
	dentry_path_raw(&d, c, 0);
}
