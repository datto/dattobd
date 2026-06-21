// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (C) 2026 Datto Inc.
 */

#include "includes.h"

MODULE_LICENSE("GPL");

static inline void dummy(void){
	struct ftrace_regs *fregs = NULL;
	ftrace_regs_set_instruction_pointer(fregs, 0);
}
