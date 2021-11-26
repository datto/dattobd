// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (C) 2020 Elastio Software Inc.
 */

#include "includes.h"
MODULE_LICENSE("GPL");

static inline void dummy(void){
	struct inode i;
	struct dentry d;
	struct inode *pi;
	vfs_unlink(&i, &d, &pi);
}
