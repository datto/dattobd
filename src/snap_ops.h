// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (C) 2022 Datto Inc.
 */
#ifndef SNAP_OPS_H_
#define SNAP_OPS_H_

#include "mrf.h"

const struct block_device_operations *get_snap_ops(void);

#ifndef USE_BDOPS_SUBMIT_BIO
// Linux version < 5.9
MRF_RETURN_TYPE snap_mrf(struct request_queue *q, struct bio *bio);
#else
// Linux version >= 5.9
MRF_RETURN_TYPE snap_mrf(struct bio *bio);
#endif

#endif /* SNAP_OPS_H_ */
