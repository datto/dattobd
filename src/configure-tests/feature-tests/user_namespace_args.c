// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (C) 2015 Datto Inc.
 */

#include "includes.h"

MODULE_LICENSE("GPL");

static inline void dummy(void){
	struct user_namespace* uns = &init_user_ns;
	(void)vfs_unlink(uns, NULL, NULL, NULL);
}
