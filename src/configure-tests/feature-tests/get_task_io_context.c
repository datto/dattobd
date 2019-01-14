// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (C) 2019 Datto Inc.
 */

#include "includes.h"

static inline void dummy(void)
{
	(void)get_task_io_context(NULL, 0, 0);
}
