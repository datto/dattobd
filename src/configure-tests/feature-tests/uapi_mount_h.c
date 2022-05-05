// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (C) 2019 Datto Inc.
 */

#include "includes.h"
#include <uapi/linux/mount.h>

MODULE_LICENSE("GPL");

static inline void dummy(void){
	int f = MS_RDONLY;
	(void)f;
}
