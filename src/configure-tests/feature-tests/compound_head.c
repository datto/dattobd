// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (C) 2019 Datto Inc.
 */

#include "includes.h"

MODULE_LICENSE("GPL");

static inline void dummy(void){
	struct page *p = NULL;

	(void)compound_head(p);
}
