// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (C) 2015 Datto Inc.
 * Additional contributions by Elastio Software, Inc are Copyright (C) 2020 Elastio Software Inc.
 */

// 2.6.25 <= kernel_version < 5.6

#include "includes.h"

static inline void dummy(void){
 	static const struct file_operations file_ops;
	struct proc_dir_entry *ent = proc_create("file", 0, NULL, &file_ops);
	(void)ent;
}
