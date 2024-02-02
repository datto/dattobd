// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (C) 2022 Datto Inc.
 */

#include "tracing_params.h"
#include "bio_helper.h"
#include "bio_queue.h"
#include "includes.h"
#include "logging.h"
#include "snap_device.h"

/**
 * tp_alloc() - Allocates and initializes tracing params and increments the
 * reference count.
 *
 * @dev: The &struct snap_device object pointer.
 * @bio: The &struct bio which describes the I/O.
 * @tp_out: The caller owned &struct tracing_params object.  Use kfree().
 *
 * Return:
 * * 0 - success
 * * !0 - errno indicating the error.
 */
int tp_alloc(struct snap_device *dev, struct bio *bio,
             struct tracing_params **tp_out)
{
        struct tracing_params *tp;

        tp = kzalloc(1 * sizeof(struct tracing_params), GFP_NOIO);
        if (!tp) {
                LOG_ERROR(-ENOMEM,
                          "error allocating tracing parameters struct");
                *tp_out = tp;
                return -ENOMEM;
        }

        tp->dev = dev;
        tp->orig_bio = bio;
        tp->bio_sects.head = NULL;
        tp->bio_sects.tail = NULL;
        atomic_set(&tp->refs, 1);

        *tp_out = tp;
        return 0;
}

/**
 * tp_get() - Increments the reference count.
 *
 * @tp: The &struct tracing_params object pointer.
 */
void tp_get(struct tracing_params *tp)
{
        atomic_inc(&tp->refs);
}

/**
 * tp_put() - Decrement the reference count.
 * @tp: The &struct tracing_params object pointer.
 *
 * Frees memory associated with the @tp object.
 */
void tp_put(struct tracing_params *tp)
{
        // drop a reference to the tp
        if (atomic_dec_and_test(&tp->refs)) {
                struct bio_sector_map *next, *curr = NULL;

                // if there are no references left, its safe to release the
                // orig_bio
                bio_queue_add(&tp->dev->sd_orig_bios, tp->orig_bio);

                // free nodes in the sector map list
                for (curr = tp->bio_sects.head; curr != NULL; curr = next) {
                        next = curr->next;
                        kfree(curr);
                }
                kfree(tp);
        }
}

/**
 * tp_add() - Adds @bio to the &struct tracing_params object.
 *
 * @tp: The &struct tracing_params object pointer.
 * @bio: The &struct bio which describes the I/O.
 *
 * Return:
 * * 0 - success
 * * !0 - errno indicating the error.
 */
int tp_add(struct tracing_params *tp, struct bio *bio)
{
        struct bio_sector_map *map;
        map = kzalloc(1 * sizeof(struct bio_sector_map), GFP_NOIO);
        if (!map) {
                LOG_ERROR(-ENOMEM,
                          "error allocating new bio_sector_map struct");
                return -ENOMEM;
        }

        map->bio = bio;
        map->sect = bio_sector(bio);
        map->size = bio_size(bio);
        map->next = NULL;
        if (tp->bio_sects.head == NULL) {
                tp->bio_sects.head = map;
                tp->bio_sects.tail = map;
        } else {
                tp->bio_sects.tail->next = map;
                tp->bio_sects.tail = map;
        }
        return 0;
}
