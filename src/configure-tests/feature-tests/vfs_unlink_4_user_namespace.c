// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (C) 2023 Elastio Software Inc.
 */

// 6.3 > kernel_version

#include "includes.h"
MODULE_LICENSE("GPL");

static inline void dummy(void){
	struct user_namespace *n;
	struct dentry d;
	struct inode *i;

	vfs_unlink(n, i, &d, &i);
}
