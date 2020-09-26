// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (C) 2020 Elastio Software Inc.
 */

// 5.6 <= kernel_version

#include "includes.h"

static inline void dummy(void){
 	static const struct proc_ops file_ops;
	struct proc_dir_entry *ent = proc_create("file", 0, NULL, &file_ops);
	(void)ent;
}
