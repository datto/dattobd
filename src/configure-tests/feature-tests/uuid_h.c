// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (C) 2017 Datto Inc.
 */

#include "includes.h"
#include <linux/uuid.h>

MODULE_LICENSE("GPL");

static inline void dummy(void){
	generate_random_uuid(NULL);
}
