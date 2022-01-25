// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (C) 2022 Datto Inc.
 */

#ifndef MRF_H_
#define MRF_H_

#include "bio_helper.h"
#include "kernel-config.h"
#include "tracing_params.h"

#ifdef HAVE_MAKE_REQUEST_FN_INT
#define MRF_RETURN_TYPE int
#define MRF_RETURN(ret) return ret

int dattobd_call_mrf(make_request_fn *fn, struct request_queue *q,
                     struct bio *bio);
#elif defined HAVE_MAKE_REQUEST_FN_VOID
#define MRF_RETURN_TYPE void
#define MRF_RETURN(ret) return

int dattobd_call_mrf(make_request_fn *fn, struct request_queue *q,
                     struct bio *bio);
#else
#define MRF_RETURN_TYPE blk_qc_t
#define MRF_RETURN(ret) return BLK_QC_T_NONE

int dattobd_call_mrf(make_request_fn *fn, struct request_queue *q,
                     struct bio *bio);
#endif

#ifdef HAVE_BLK_ALLOC_QUEUE
//#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,7,0)
MRF_RETURN_TYPE dattobd_null_mrf(struct request_queue *q, struct bio *bio);
#endif

#endif /* MRF_H_ */
