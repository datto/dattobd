/*
    Copyright (C) 2017 Datto Inc.

    This file is part of dattobd.

    This program is free software; you can redistribute it and/or modify it
    under the terms of the GNU General Public License version 2 as published
    by the Free Software Foundation.
*/

#include "../../includes.h"

static inline void dummy(void){
	struct file *f = NULL;
	const void *buf = NULL;
	loff_t *pos = NULL;

	(void)kernel_write(f, buf, 0, pos);
}
