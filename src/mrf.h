// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (C) 2022-2023 Datto Inc.
 */

#ifndef MRF_H_
#define MRF_H_

#include "bio_helper.h"
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

#elif defined HAVE_NONVOID_SUBMIT_BIO_1

#define MRF_RETURN_TYPE blk_qc_t
#define MRF_RETURN(ret) return BLK_QC_T_NONE
#ifndef USE_BDOPS_SUBMIT_BIO
int dattobd_call_mrf(make_request_fn *fn, struct request_queue *q,
                     struct bio *bio);
#endif // USE_BDOPS_SUBMIT_BIO

#else

#define MRF_RETURN_TYPE void
#define MRF_RETURN(ret) return

#endif

#if defined HAVE_BLK_ALLOC_QUEUE_2 || defined HAVE_BLK_ALLOC_QUEUE_RH_2
#define HAVE_BLK_ALLOC_QUEUE
#endif

#ifdef HAVE_BLK_ALLOC_QUEUE
//#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,7,0)
MRF_RETURN_TYPE dattobd_null_mrf(struct request_queue *q, struct bio *bio);
#endif

#ifdef USE_BDOPS_SUBMIT_BIO
MRF_RETURN_TYPE dattobd_snap_null_mrf(struct bio *bio);
MRF_RETURN_TYPE dattobd_null_mrf(struct bio *bio);
make_request_fn* dattobd_get_gd_mrf(struct gendisk *gd);
struct block_device_operations* dattobd_get_bd_ops(struct block_device *bdev);
int dattobd_call_mrf_real(struct snap_device *dev, struct bio *bio);
int dattobd_call_mrf(make_request_fn *fn, struct request_queue *q, struct bio *bio);
#else
make_request_fn* dattobd_get_gd_mrf(struct gendisk *gd);


/**
 * dattobd_call_mrf_real() - Submits i/o to the real/original device.
 *
 * This function submits i/o to the real/original make_request_fn (as opposed
 * to the one we replaced for intercepting i/o) - we look up the mrf ptr from
 * the snap_device which we saved when setting up tracking, and call it with
 * our bio.
 *
 * @dev: the snap_device struct we set up with all of our state.
 * @bio: the in-flight i/o to be submitted to the mrf.
 *
 * Returns:
 * * Returns the result of the mrf function call as returned by dattobd_call_mrf
 */
int dattobd_call_mrf_real(struct snap_device *dev, struct bio *bio);
#endif

make_request_fn* dattobd_get_bd_mrf(struct block_device *bdev);

#endif /* MRF_H_ */
