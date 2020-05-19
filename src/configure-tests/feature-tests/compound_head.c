// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (C) 2019 Datto Inc.
 * Additional contributions by Elastio Software, Inc are Copyright (C) 2020 Elastio Software Inc.
 */

#include "includes.h"

static inline void dummy(void){
	struct page *p = NULL;

	(void)compound_head(p);
}
