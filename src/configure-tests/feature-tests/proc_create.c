// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (C) 2015 Datto Inc.
 */

#include "includes.h"

MODULE_LICENSE("GPL");

static inline void dummy(void){
	struct proc_dir_entry *ent = proc_create("file", 0, NULL, NULL);
	(void)ent;
}
