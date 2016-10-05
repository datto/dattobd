/*
    Copyright (C) 2015 Datto Inc.

    This file is part of dattobd.

    This program is free software; you can redistribute it and/or modify it 
    under the terms of the GNU General Public License version 2 as published
    by the Free Software Foundation.
*/

#include "../../includes.h"

static int dummy_endio(struct bio *bio, unsigned int bytes, int err){
	return 0;
}

static inline void dummy(void){
	struct bio bio;
	bio.bi_end_io = dummy_endio;
}
