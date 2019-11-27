// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (C) 2018 Datto Inc.
 * Additional contributions by Assurio Software, Inc are Copyright (C) 2019 Assurio Software Inc.
 */

#include "includes.h"

static inline void dummy(void){
	struct bio_set bs;

	(void)bioset_init(&bs, 0, 0, 0);
}
