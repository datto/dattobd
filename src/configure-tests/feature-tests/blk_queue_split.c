/*
    Copyright (C) 2015 Datto Inc.

    This file is part of dattobd.

    This program is free software; you can redistribute it and/or modify it 
    under the terms of the GNU General Public License version 2 as published
    by the Free Software Foundation.
*/

#include "../../includes.h"

static inline void dummy(void){
	struct request_queue *q = NULL;
	struct bio newbio;
	struct bio *bioptr = &newbio;
	struct bio_set *bs = NULL;
	blk_queue_split(q, &bioptr, bs);
}
