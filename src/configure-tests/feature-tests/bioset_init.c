// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (C) 2018 Datto Inc.
 */

#include "includes.h"

MODULE_LICENSE("GPL");

static inline void dummy(void){
	struct bio_set bs;

	(void)bioset_init(&bs, 0, 0, 0);
}
