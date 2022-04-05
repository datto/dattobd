// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2022 Datto Inc.

/**
 *
 * DOC: bio_request_callback header.
 *
 * Defines the various mechanisms and types needed to submit IO requests.
 *
 * Different types and callbacks are used between different versions of
 * Linux in order to submit in-flight IO to the kernel.
 *
 * The purpose of this header is to provide a unified interface to making this
 * happen.
 */

#ifndef BIO_REQUEST_CALLBACK_H_INCLUDE
#define BIO_REQUEST_CALLBACK_H_INCLUDE

#include "bio_helper.h" // needed for USE_BDOPS_SUBMIT_BIO to be defined
#include "includes.h"
#include "mrf.h"
#include "submit_bio.h"

#ifdef USE_BDOPS_SUBMIT_BIO
#define BIO_REQUEST_TRACKING_PTR_TYPE const struct gendisk
#define BIO_REQUEST_CALLBACK_FN submit_bio_fn
#define SUBMIT_BIO_REAL dattobd_submit_bio_real
#define GET_BIO_REQUEST_TRACKING_PTR dattobd_get_bd_submit_bio
#define SET_BIO_REQUEST_TRACKING_PTR dattobd_set_bd_ops
#else
#define BIO_REQUEST_TRACKING_PTR_TYPE make_request_fn
#define BIO_REQUEST_CALLBACK_FN make_request_fn
#define SUBMIT_BIO_REAL dattobd_call_mrf_real
#define GET_BIO_REQUEST_TRACKING_PTR dattobd_get_bd_mrf
#define SET_BIO_REQUEST_TRACKING_PTR dattobd_set_bd_mrf
#endif 

#endif
