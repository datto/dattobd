// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (C) 2025 Datto Inc.
 */

#ifndef MEMORY_H_
#define MEMORY_H_

#include "includes.h"

unsigned long dattobd_get_unmapped_area(struct file *file, unsigned long addr, unsigned long len, unsigned long pgoff, unsigned long flags);

#endif /* MEMORY_H_ */
 