// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (C) 2018 Datto Inc.
 */

#include "includes.h"

MODULE_LICENSE("GPL");

static inline void dummy(void){
    struct bio *bio = NULL;
    unsigned op;
    unsigned op_flags;
    bio->bi_opf = 0;
    bio_set_op_attrs(bio, op, op_flags);
}
