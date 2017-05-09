/*
 * Copyright (C) 2015 Datto Inc.
 *
 * This file is part of dattobd.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 */

#include "../../includes.h"

static void dummy_mrf(struct request_queue *q, struct bio *bio)
{
	return;
}

static inline void dummy(void)
{
	struct bio b;
	struct request_queue q = {
		.make_request_fn = dummy_mrf,
	};

	q.make_request_fn(&q, &b);
}
