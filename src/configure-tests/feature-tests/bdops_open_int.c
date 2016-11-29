/*
    Copyright (C) 2015 Datto Inc.

    This file is part of dattobd.

    This program is free software; you can redistribute it and/or modify it
    under the terms of the GNU General Public License version 2 as published
    by the Free Software Foundation.
*/

#include "../../includes.h"

static int snap_open(struct block_device *bdev, fmode_t mode){
	return 0;
}

static int snap_release(struct gendisk *gd, fmode_t mode){
	return 0;
}

static inline void dummy(void){
	struct gendisk gd;
	struct block_device bd;
	struct block_device_operations bdo = {
		.open = snap_open,
		.release = snap_release,
	};

	bdo.open(&bd, FMODE_READ);
	bdo.release(&gd, FMODE_READ);
}
