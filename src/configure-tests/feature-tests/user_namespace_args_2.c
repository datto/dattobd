// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (C) 2024 Datto Inc.
 */

#include "includes.h"

MODULE_LICENSE("GPL");

static inline void dummy(void){
	struct mnt_idmap* map = &nop_mnt_idmap ;
	(void)vfs_unlink(map, NULL, NULL, NULL);
}
