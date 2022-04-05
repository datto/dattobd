// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (C) 2022 Datto Inc.
 */

#include "includes.h"

static inline void dummy(void){
    struct bio b;
    blk_mq_submit_bio(&b);
}
