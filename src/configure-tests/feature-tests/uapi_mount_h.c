// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (C) 2019 Datto Inc.
 * Additional contributions by Assurio Software, Inc are Copyright (C) 2019 Assurio Software Inc.
 */

#include "includes.h"
#include <uapi/linux/mount.h>

static inline void dummy(void){
	int f = MS_RDONLY;
	(void)f;
}
