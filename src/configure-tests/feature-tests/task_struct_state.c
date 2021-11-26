// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (C) 2021 Elastio Software Inc.
 */

#include "includes.h"
MODULE_LICENSE("GPL");

static inline void dummy(void){
	unsigned int state = current->__state;
	(void)state;
}
