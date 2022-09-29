// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (C) 2022 Datto Inc.
 */

#ifndef SUBMIT_BIO_H_
#define SUBMIT_BIO_H_

#include "includes.h"
#include "bio_helper.h" // needed for USE_BDOPS_SUBMIT_BIO to be defined

struct snap_device;

#ifdef USE_BDOPS_SUBMIT_BIO

/**
 * submit_bio_fn() - Prototype for the submit_bio function, which will be our
 * hook to intercept IO on kernels >= 5.9 
 */
typedef blk_qc_t (submit_bio_fn) (struct bio *bio);

/**
 * dattobd_submit_bio_real() - Submit's given bio to the real device 
 *                            (as opposed to our driver).
 *
 * @dev: Pointer to the snap_device that keeps device state.
 * @bio: Pointer to the bio struct which describes the in-flight I/O.
 *
 * Return:
 * * 0 - success
 * * !0 - error
 */
int dattobd_submit_bio_real(
    struct snap_device* dev,
    struct bio *bio
);

#endif // USE_BDOPS_SUBMIT_BIO
#endif // SUBMIT_BIO_H_
