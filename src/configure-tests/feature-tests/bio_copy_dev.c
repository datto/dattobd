// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (C) 2021 Elastio Software Inc.
 */
 
#include "includes.h"
#include <linux/bio.h>
MODULE_LICENSE("GPL");

static inline void dummy(void){
	struct bio dst, src;
	bio_copy_dev(&dst, &src);
}
