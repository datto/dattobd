// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (C) 2024 Datto Inc.
 */

#include "includes.h"

MODULE_LICENSE("GPL");

static inline void dummy(void){
	struct file *file __attribute__ ((unused)) = NULL;
    const char *path = "";
    fmode_t mode = 0;
    void *holder = NULL;
    struct blk_holder_ops h;

    file = bdev_file_open_by_path(path, mode, holder, &h);
}
