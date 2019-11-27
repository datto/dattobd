// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (C) 2015 Datto Inc.
 * Additional contributions by Assurio Software, Inc are Copyright (C) 2019 Assurio Software Inc.
 */

#include "includes.h"

static inline void dummy(void){
	struct gendisk gd;
	gd.flags = GENHD_FL_NO_PART_SCAN;
}
