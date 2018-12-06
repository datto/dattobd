/*
    Copyright (C) 2015 Datto Inc.

    This file is part of dattobd.

    This program is free software; you can redistribute it and/or modify it
    under the terms of the GNU General Public License version 2 as published
    by the Free Software Foundation.
*/

#include "includes.h"

static inline void dummy(void){
	struct bio_set *bs;
	int bio_pool_size;
	int bvec_pool_size;
	int scale;
	bs = bioset_create(bio_pool_size, bvec_pool_size, scale);
}
