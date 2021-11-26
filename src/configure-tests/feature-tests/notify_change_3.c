// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (C) 2020 Elastio Software Inc.
 */

#include "includes.h"
MODULE_LICENSE("GPL");

static inline void dummy(void){
	struct dentry d;
	struct iattr a;
	struct inode *pi;
	notify_change(&d, &a, &pi);
}
