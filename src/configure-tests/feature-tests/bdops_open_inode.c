/*
    Copyright (C) 2015 Datto Inc.

    This file is part of dattobd.

    This program is free software; you can redistribute it and/or modify it 
    under the terms of the GNU General Public License version 2 as published
    by the Free Software Foundation.
*/

#include "../../includes.h"

static int snap_open(struct inode *inode, struct file *filp){
	return 0;
}

static int snap_release(struct inode *inode, struct file *filp){
	return 0;
}

static inline void dummy(void){
	struct inode i;
	struct file f;
	struct block_device_operations bdo = {
		.open = snap_open,
		.release = snap_release,
	};
	
	bdo.open(&i, &f);
	bdo.release(&i, &f);
}
