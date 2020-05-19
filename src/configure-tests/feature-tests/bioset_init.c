// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (C) 2018 Datto Inc.
 * Additional contributions by Elastio Software, Inc are Copyright (C) 2020 Elastio Software Inc.
 */

#include "includes.h"

static inline void dummy(void){
	struct bio_set bs;

	(void)bioset_init(&bs, 0, 0, 0);
}
