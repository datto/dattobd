// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (C) 2022 Datto Inc.
 */

#ifndef CALLBACK_REFS_H_
#define CALLBACK_REFS_H_

#include "includes.h"
#include "bio_helper.h"
#include "bio_request_callback.h"

#ifndef USE_BDOPS_SUBMIT_BIO

/**
 * mrf_tracking_init() - Initialize mrf tracking.
 *
 * Initializes the hash table used for mrf reference counting.
 */
void mrf_tracking_init(void);

/**
 * mrf_get() - Increments the reference count for given mrf.
 *
 * @disk: The block device being tracked.
 * @fn: The mrf to inc the reference count for.
 *
 * Return: 0 on success. Non-zero otherwise.
 */
int mrf_get(const struct block_device* disk, BIO_REQUEST_CALLBACK_FN* fn);

/**
 * mrf_put() - Decrements the reference count and returns mrf
 *
 * @disk: The block device being tracked
 * 
 * Return: Returns the mrf for the block device or NULL on error.
 */
const BIO_REQUEST_CALLBACK_FN* mrf_put(struct block_device* disk);

#endif // USE_BDOPS_SUBMIT_BIO
#endif // CALLBACK_REFS_H_
