// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (C) 2022 Datto Inc.
 */

#ifndef CALLBACK_REFS_H_
#define CALLBACK_REFS_H_

#include "includes.h"
#include "bio_request_callback.h"

/**
 * gendisk_tracking_init() - Initialize gendisk/mrf tracking.
 *
 * Initializes the hash table used for gendisk/mrf reference counting.
 */
void gendisk_tracking_init(void);

/**
 * gendisk_fn_get() - Increments the reference count for given gendisk or mrf.
 *
 * @disk: The block device being tracked.
 * @fn: The gendisk or mrf to inc the reference count for.
 *
 * Return: 0 on success. Non-zero otherwise.
 */
int gendisk_fn_get(const struct block_device* disk, const BIO_REQUEST_TRACKING_PTR_TYPE* fn);

/**
 * gendisk_fn_put() - Decrements the reference count and returns gendisk/mrf
 *
 * @disk: The block device being tracked
 * 
 * Return: Returns the mrf or gendisk for the block device or NULL on error.
 */
const BIO_REQUEST_TRACKING_PTR_TYPE* gendisk_fn_put(struct block_device* disk);

/**
 * gendisk_fn_refcount() - Get reference count for an mrf/gendisk
 *
 * @fn: The gendisk or make_request_fn to get the refcount for.
 *
 * Return: Returns the reference count for the given mrf or gendisk.
 */
size_t gendisk_fn_refcount(const BIO_REQUEST_TRACKING_PTR_TYPE *fn);

#endif // CALLBACK_REFS_H_
