// SPDX-License-Identifier: GPL-2.0-only

/*
 * Feature test: detect presence of bio_set_dev(bio, bdev)
 */

#include "includes.h"

MODULE_LICENSE("GPL");

static inline void dummy(void)
{
        struct bio *b = NULL;
        struct block_device *d = NULL;
        /* compile-time presence check */
        bio_set_dev(b, d);
}


