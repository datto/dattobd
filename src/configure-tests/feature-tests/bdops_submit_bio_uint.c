// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (C) 2020 Elastio Software Inc.
 */

// 5.9 <= kernel_version

#include "includes.h"
MODULE_LICENSE("GPL");

static blk_qc_t snap_submit_bio(struct bio *bio)
{
	return BLK_QC_T_NONE;
}

static inline void dummy(void){
	struct bio b;
	struct block_device_operations bdo = {
		.submit_bio = snap_submit_bio,
	};

	bdo.submit_bio(&b);
}
