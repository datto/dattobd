// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (C) 2015 Datto Inc.
 */

#include "includes.h"

static inline void dummy(void){
	struct hlist_head *hl = current->task_works;
	hlist_empty(hl);
}
